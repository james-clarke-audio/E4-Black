import Foundation

/// On-disk session logs: what she said, when, across every bench session.
///
/// **Raw lines are stored, not decoded messages.** A decoded struct freezes a
/// log at whatever the decoder understood on the day it was written; a raw line
/// can be re-read by a better decoder years later. It also means a log is
/// greppable and a line the app did not recognise is still on disk.
///
/// The format is JSON Lines: one self-contained JSON object per line, appended.
/// A truncated final line (power cut, crash) costs one line, not the file.
public enum E4SessionLog {

    public static let fileExtension = "e4log"

    /// First line of every file.
    public struct Header: Codable, Sendable {
        public var version: Int = 1
        public var started: Date
        public var device: String?
        public var firmware: String?
        public var build: String?

        public init(started: Date, device: String? = nil,
                    firmware: String? = nil, build: String? = nil) {
            self.started = started
            self.device = device
            self.firmware = firmware
            self.build = build
        }
    }

    /// Every line after the first. `t` is seconds since the header's `started`,
    /// which keeps entries small and makes a log independent of the clock
    /// having been right.
    public struct Entry: Codable, Sendable {
        public var t: Double
        public var line: String
        /// Set on lines the app sent, absent on lines she sent.
        public var tx: Bool?

        public init(t: Double, line: String, tx: Bool? = nil) {
            self.t = t
            self.line = line
            self.tx = tx
        }
    }

    static let encoder: JSONEncoder = {
        let e = JSONEncoder()
        e.dateEncodingStrategy = .iso8601
        e.outputFormatting = [.withoutEscapingSlashes]
        return e
    }()

    static let decoder: JSONDecoder = {
        let d = JSONDecoder()
        d.dateDecodingStrategy = .iso8601
        return d
    }()

    /// `E4-2026-09-11-163045.e4log` — sorts chronologically as plain text.
    public static func filename(for date: Date) -> String {
        let f = DateFormatter()
        f.dateFormat = "yyyy-MM-dd-HHmmss"
        f.locale = Locale(identifier: "en_US_POSIX")
        f.timeZone = .current
        return "E4-\(f.string(from: date)).\(fileExtension)"
    }
}

// MARK: - Writing

/// Appends to one session file.
///
/// Buffered: at 20 Hz telemetry plus a wall-sensor set every 5 ms, a write
/// syscall per line would be thousands a minute for no benefit. The buffer is
/// flushed on a size or time bound and on close — and `flush()` is public so
/// the app can force one when it goes to the background.
public final class E4SessionWriter {

    public let url: URL
    public private(set) var lineCount = 0
    public private(set) var started: Date

    private var handle: FileHandle?
    private var buffer = Data()
    private var lastFlush: Date

    private let flushBytes = 16 * 1024
    private let flushSeconds: TimeInterval = 2

    /// Creates the file and writes the header immediately, so a session that
    /// crashes before its first flush still leaves something identifiable.
    public init(directory: URL, header: E4SessionLog.Header) throws {
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        self.started = header.started
        self.url = directory.appendingPathComponent(E4SessionLog.filename(for: header.started))
        self.lastFlush = Date()

        var first = try E4SessionLog.encoder.encode(header)
        first.append(0x0A)
        try first.write(to: url, options: .atomic)

        handle = try FileHandle(forWritingTo: url)
        try handle?.seekToEnd()
    }

    public func record(_ line: String, tx: Bool = false, at date: Date = Date()) {
        let entry = E4SessionLog.Entry(t: date.timeIntervalSince(started),
                                       line: line,
                                       tx: tx ? true : nil)
        guard var data = try? E4SessionLog.encoder.encode(entry) else { return }
        data.append(0x0A)
        buffer.append(data)
        lineCount += 1

        if buffer.count >= flushBytes || Date().timeIntervalSince(lastFlush) >= flushSeconds {
            flush()
        }
    }

    public func flush() {
        guard !buffer.isEmpty, let handle else { return }
        try? handle.write(contentsOf: buffer)
        buffer.removeAll(keepingCapacity: true)
        lastFlush = Date()
    }

    public func close() {
        flush()
        try? handle?.close()
        handle = nil
    }

    deinit {
        // Not a substitute for close() — just the last line of defence.
        flush()
        try? handle?.close()
    }
}

// MARK: - Reading

/// What the History list shows without opening a whole file.
public struct E4SessionSummary: Identifiable, Sendable {
    public var id: URL { url }
    public let url: URL
    public let header: E4SessionLog.Header
    public let bytes: Int
    public let modified: Date

    /// Wall-clock span, from the file's timestamps. Not exact — the file's
    /// modification date is when the last flush landed, not the last line.
    public var duration: TimeInterval { modified.timeIntervalSince(header.started) }
}

/// Statistics derived by replaying a log through the decoder.
///
/// Deliberately crude and honest about it: a session mixes readings taken
/// against a wall with readings taken on open floor, so a median over a whole
/// session is a blunt instrument. It is still the right shape for the question
/// it answers — "is SL behaving like it did last week" — and the per-action
/// breakdown narrows it where she was doing something deliberate.
public struct E4SessionStats: Sendable {
    public struct Series: Sendable {
        public var count = 0
        public var min = Int.max
        public var max = Int.min
        public var median = 0
        public var samples: [Int] = []
    }

    public var sensors: [E4Sensor: Series] = [:]
    public var turnResults: [E4TurnResult] = []
    public var batteryMin: Double?
    public var batteryMax: Double?
    public var gyroScale: Double?
    public var actionsRun: [String] = []
    public var lineCount = 0
    public var unrecognisedCount = 0
}

public enum E4SessionReader {

    /// Reads just the header. Cheap enough to run over a directory of logs.
    public static func summary(of url: URL) throws -> E4SessionSummary? {
        let handle = try FileHandle(forReadingFrom: url)
        defer { try? handle.close() }

        // The header is the first line; read a bounded chunk rather than the
        // file, which may be megabytes.
        guard let chunk = try handle.read(upToCount: 4096), !chunk.isEmpty else { return nil }
        guard let newline = chunk.firstIndex(of: 0x0A) else { return nil }
        let headerData = chunk[chunk.startIndex..<newline]
        guard let header = try? E4SessionLog.decoder.decode(E4SessionLog.Header.self, from: headerData)
        else { return nil }

        let attrs = try FileManager.default.attributesOfItem(atPath: url.path)
        return E4SessionSummary(
            url: url,
            header: header,
            bytes: (attrs[.size] as? Int) ?? 0,
            modified: (attrs[.modificationDate] as? Date) ?? header.started
        )
    }

    public static func summaries(in directory: URL) -> [E4SessionSummary] {
        let fm = FileManager.default
        guard let names = try? fm.contentsOfDirectory(at: directory,
                                                      includingPropertiesForKeys: [.fileSizeKey],
                                                      options: [.skipsHiddenFiles])
        else { return [] }
        return names
            .filter { $0.pathExtension == E4SessionLog.fileExtension }
            .compactMap { try? summary(of: $0) }
            .compactMap { $0 }
            .sorted { $0.header.started > $1.header.started }
    }

    /// Every entry, in order. A malformed line is skipped rather than failing
    /// the read — a truncated last line is normal after a crash.
    public static func entries(of url: URL) throws -> [E4SessionLog.Entry] {
        let text = try String(contentsOf: url, encoding: .utf8)
        var out: [E4SessionLog.Entry] = []
        for (i, line) in text.split(separator: "\n", omittingEmptySubsequences: true).enumerated() {
            if i == 0 { continue }                     // header
            guard let data = line.data(using: .utf8),
                  let entry = try? E4SessionLog.decoder.decode(E4SessionLog.Entry.self, from: data)
            else { continue }
            out.append(entry)
        }
        return out
    }

    /// Replays a log through the decoder.
    public static func stats(of url: URL) throws -> E4SessionStats {
        var stats = E4SessionStats()
        var raw: [E4Sensor: [Int]] = [:]

        for entry in try entries(of: url) {
            stats.lineCount += 1
            if entry.tx == true { continue }           // our own commands

            switch E4MessageDecoder.decode(entry.line) {
            case .ir(let l, let fl, let fr, let r):
                raw[.left, default: []].append(l)
                raw[.frontLeft, default: []].append(fl)
                raw[.frontRight, default: []].append(fr)
                raw[.right, default: []].append(r)

            case .sensorStatus(let s):
                raw[.left, default: []].append(s.left)
                raw[.frontLeft, default: []].append(s.frontLeft)
                raw[.frontRight, default: []].append(s.frontRight)
                raw[.right, default: []].append(s.right)

            case .telemetry(let t):
                stats.batteryMin = min(stats.batteryMin ?? t.battery, t.battery)
                stats.batteryMax = max(stats.batteryMax ?? t.battery, t.battery)

            case .turnResult(let r):
                stats.turnResults.append(r)

            case .gyroCal(let g):
                if let s = g.currentScale { stats.gyroScale = s }

            case .config(let c):
                if let s = c.gyroScale { stats.gyroScale = s }

            case .runStarted(_, let name):
                if !name.isEmpty && !stats.actionsRun.contains(name) {
                    stats.actionsRun.append(name)
                }

            case .text:
                stats.unrecognisedCount += 1

            default:
                break
            }
        }

        for (sensor, values) in raw where !values.isEmpty {
            let sorted = values.sorted()
            var series = E4SessionStats.Series()
            series.count = sorted.count
            series.min = sorted.first!
            series.max = sorted.last!
            series.median = sorted[sorted.count / 2]
            // Thinned for drawing — a session can hold tens of thousands.
            let stride = Swift.max(1, sorted.count / 400)
            series.samples = values.enumerated().compactMap { $0.offset % stride == 0 ? $0.element : nil }
            stats.sensors[sensor] = series
        }

        return stats
    }
}
