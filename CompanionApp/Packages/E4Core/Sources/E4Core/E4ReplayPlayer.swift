import Foundation

/// Plays a recorded session back through a live E4Session.
///
/// The point of this is not nostalgia. The logs keep RAW lines, so a run
/// recorded today is decoded by whatever decoder exists when you replay it —
/// a session watched again in a month gets the benefit of every fix made in
/// between. A log of decoded structs could never do that.
///
/// Timing comes from the recorded `t` values rather than from a fixed tick, so
/// a pause while you repositioned her is a pause on screen too. That matters:
/// the gaps are part of what happened.
@MainActor
@Observable
public final class E4ReplayPlayer {

    public private(set) var entries: [E4SessionLog.Entry] = []
    public private(set) var header: E4SessionLog.Header?
    public private(set) var index = 0
    public private(set) var isPlaying = false
    public private(set) var name = ""

    /// 1 = as it happened. Slower is for watching a turn go wrong; faster is
    /// for getting to the interesting part.
    public var rate: Double = 1.0 {
        didSet { rate = min(max(rate, 0.1), 32) }
    }

    public static let rates: [Double] = [0.25, 0.5, 1, 2, 4, 8, 16]

    @ObservationIgnored private weak var session: E4Session?
    @ObservationIgnored private var task: Task<Void, Never>?

    public init() {}

    public func attach(to session: E4Session) { self.session = session }

    public var duration: TimeInterval { entries.last?.t ?? 0 }
    public var position: TimeInterval {
        index > 0 && index <= entries.count ? entries[index - 1].t : 0
    }
    public var isLoaded: Bool { !entries.isEmpty }
    public var atEnd: Bool { index >= entries.count }

    // MARK: - Loading

    public func load(data: Data, name: String) throws {
        stop()
        let parsed = try E4SessionReader.parse(data)
        header = parsed.header
        entries = parsed.entries
        self.name = name
        index = 0
        session?.beginReplay()
    }

    /// Leave replay entirely and hand the session back to the radio.
    public func close() {
        stop()
        entries = []
        header = nil
        name = ""
        index = 0
        session?.endReplay()
    }

    // MARK: - Transport

    public func play() {
        guard isLoaded, !isPlaying else { return }
        if atEnd { restart() }
        isPlaying = true
        task = Task { @MainActor [weak self] in
            await self?.run()
        }
    }

    public func pause() {
        isPlaying = false
        task?.cancel()
        task = nil
    }

    public func stop() {
        pause()
        index = 0
    }

    /// Back to the beginning with a clean slate — otherwise the second viewing
    /// is drawn on top of the first.
    public func restart() {
        pause()
        index = 0
        session?.endReplay()
        session?.beginReplay()
    }

    /// Jump to a time. Everything before it is applied WITHOUT waiting, because
    /// the state at 90 seconds is the sum of every line before it — skipping
    /// them would show a maze with holes in it that were never really there.
    public func seek(to time: TimeInterval) {
        let wasPlaying = isPlaying
        pause()
        session?.endReplay()
        session?.beginReplay()
        index = 0
        while index < entries.count, entries[index].t <= time {
            emit(entries[index])
            index += 1
        }
        if wasPlaying { play() }
    }

    public func step() {
        guard isLoaded, index < entries.count else { return }
        pause()
        emit(entries[index])
        index += 1
    }

    // MARK: - The loop

    private func run() async {
        while isPlaying, index < entries.count {
            let entry = entries[index]
            let previous = index > 0 ? entries[index - 1].t : entry.t
            let gap = max(0, entry.t - previous) / rate

            // A long stop between lines is real, but nobody wants to sit
            // through ninety seconds of nothing to reach the next one.
            let wait = min(gap, 2.0)
            if wait > 0.001 {
                try? await Task.sleep(for: .seconds(wait))
                if Task.isCancelled || !isPlaying { return }
            }
            emit(entry)
            index += 1
        }
        if index >= entries.count { isPlaying = false }
    }

    private func emit(_ entry: E4SessionLog.Entry) {
        session?.replay(line: entry.line, sent: entry.tx ?? false)
    }
}
