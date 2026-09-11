import Foundation
import Observation

/// The single object the UI observes: connection state, the latest values from
/// the mouse, and the command entry points.
///
/// Holds *current state*, not history. Logging runs to disk and plotting
/// tuning runs over time are deliberately out of scope for the thin slice —
/// but `observe(_:)` and `recorder` are the seams they attach to.
@MainActor
@Observable
public final class E4Session {

    // MARK: Connection

    public private(set) var connection: E4ConnectionState = .idle
    public private(set) var peripherals: [E4Peripheral] = []

    /// Widen the scan to devices that do not advertise the FFE0 service.
    /// Off by default; the equivalent of the web app's "All devices" checkbox.
    public var scanUnfiltered = false

    // MARK: Latest values

    public private(set) var firmwareVersion: String?
    public private(set) var firmwareBuild: String?
    public private(set) var battery: Double?
    public private(set) var telemetry: E4Telemetry?
    public private(set) var sensors: [Int] = [0, 0, 0, 0]
    public private(set) var sensorStatus: E4SensorStatus?

    /// Largest reading seen this connection.
    ///
    /// The bars scale to this rather than to a fixed range, because the useful
    /// range depends entirely on mode: real ambient-subtracted IR runs to a few
    /// hundred, while virtual (truth-derived) readings run into the thousands.
    /// It only ever ratchets up, so bars stay comparable over a session instead
    /// of rescaling under you while you're watching them.
    public private(set) var sensorPeak = 0

    /// Pack health against the firmware's own cutoff, or nil before the first
    /// telemetry frame.
    public var batteryState: E4BatteryState? {
        battery.map(E4BatteryState.init(volts:))
    }
    public private(set) var pose: (x: Double, y: Double, degrees: Double)?
    public private(set) var stateLabel: String?
    public private(set) var gyroScale: Double?
    public private(set) var lastTurnResult: E4TurnResult?
    public private(set) var lastGyroCal: E4GyroCalReport?

    /// Which emitter the hold routine has lit, for the aiming workflow.
    public private(set) var litEmitter: Int?

    /// The menu action the mouse is currently inside, as SHE reports it.
    ///
    /// Driven by the firmware's own `RUN,<idx>,<name>` and `DONE,<idx>` lines
    /// rather than guessed from what we sent, so it stays right when an action
    /// is started from the on-board buttons instead of from here.
    ///
    /// This matters because several actions are modal: `SPIN` and `ARC` are
    /// only read inside the turn tuner, and `ERR`/`GS` only inside gyro
    /// calibration. Outside them the firmware silently ignores the line, which
    /// looks exactly like a dead button.
    public private(set) var runningAction: E4MenuAction?

    /// The action's name as the firmware reported it — which may be an action
    /// this app's table does not know, if the firmware is ahead of it.
    public private(set) var runningActionName: String?

    // MARK: Log

    /// Bounded ring of recent lines. The raw log stays in the thin slice
    /// because it is the fastest route to diagnosing anything at the bench.
    public private(set) var log: [E4LogEntry] = []
    public var logLimit = 500

    /// Called for every decoded message, after the session has folded it in.
    ///
    /// A list, not a single slot: the maze wants messages and so does anything
    /// added later. One closure meant whoever attached last silently replaced
    /// whoever attached first.
    private var observers: [(E4Message) -> Void] = []

    /// Receives every line, raw, in both directions. This is the seam session
    /// logging hangs off, and it takes raw text rather than decoded messages on
    /// purpose: a log of raw lines can be re-read by a better decoder later, a
    /// log of decoded structs is frozen at the decoder that wrote it.
    public var recorder: (any E4LineRecorder)?

    public func observe(_ handler: @escaping (E4Message) -> Void) {
        observers.append(handler)
    }

    private let transport: any E4Transport

    public init(transport: (any E4Transport)? = nil) {
        self.transport = transport ?? E4BluetoothTransport()
        wire()
    }

    private func wire() {
        transport.onStateChange = { [weak self] state in
            guard let self else { return }
            self.connection = state
            if case .connected = state {
                // Ask who she is straight away — the version chip is the
                // fastest confirmation that the link is genuinely working,
                // not just nominally connected.
                self.send(.action(.firmwareVersion))
            }
            if case .idle = state { self.clearLiveValues() }
        }
        transport.onDiscover = { [weak self] found in
            self?.peripherals = found
        }
        transport.onLine = { [weak self] line in
            self?.ingest(line)
        }
    }

    // MARK: - Commands

    public func startScan() {
        transport.startScan(includeUnnamed: scanUnfiltered)
    }

    public func stopScan() {
        transport.stopScan()
    }

    public func connect(to peripheral: E4Peripheral) {
        transport.connect(to: peripheral.id)
    }

    public func disconnect() {
        transport.disconnect()
    }

    public func send(_ command: E4Command) {
        let text = command.line.trimmingCharacters(in: .whitespacesAndNewlines)
        transport.send(command.line)
        recorder?.record(text, tx: true)
        append(.init(text: "→ " + text, kind: .sent))
    }

    /// Send a raw line typed by the user. Useful for firmware commands the app
    /// does not model yet, and for the `GT*` maze-truth channel.
    public func sendRaw(_ text: String) {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        transport.send(trimmed + "\n")
        recorder?.record(trimmed, tx: true)
        append(.init(text: "→ " + trimmed, kind: .sent))
    }

    // MARK: - Receiving

    private func ingest(_ line: String) {
        recorder?.record(line, tx: false)
        let message = E4MessageDecoder.decode(line)
        apply(message)
        append(.init(text: line, kind: kind(for: message)))
        for observer in observers { observer(message) }
    }

    private func apply(_ message: E4Message) {
        switch message {
        case .version(let fw, let build):
            firmwareVersion = fw
            firmwareBuild = build

        case .telemetry(let t):
            telemetry = t
            battery = t.battery

        case .ir(let l, let fl, let fr, let r):
            sensors = [l, fl, fr, r]
            sensorPeak = max(sensorPeak, sensors.max() ?? 0)

        case .sensorStatus(let s):
            sensorStatus = s
            sensors = [s.left, s.frontLeft, s.frontRight, s.right]
            sensorPeak = max(sensorPeak, sensors.max() ?? 0)

        case .pose(let x, let y, let deg):
            pose = (x, y, deg)

        case .state(let label):
            stateLabel = label

        case .emitter(let index, _):
            litEmitter = index

        case .config(let cfg):
            if let scale = cfg.gyroScale { gyroScale = scale }

        case .gyroCal(let report):
            lastGyroCal = report
            if let scale = report.currentScale { gyroScale = scale }

        case .turnResult(let result):
            lastTurnResult = result

        case .runStarted(let index, let name):
            runningAction = E4MenuAction(rawValue: index)
            runningActionName = name.isEmpty ? nil : name

        case .runFinished:
            runningAction = nil
            runningActionName = nil

        case .text(let t) where t.contains("Emitter hold: off"):
            litEmitter = nil

        default:
            break
        }
    }

    private func clearLiveValues() {
        telemetry = nil
        litEmitter = nil
        stateLabel = nil
        sensorPeak = 0
        runningAction = nil
        runningActionName = nil
    }

    // MARK: - Log plumbing

    private func kind(for message: E4Message) -> E4LogEntry.Kind {
        switch message {
        case .text:   return .unknown
        case .state:  return .alert
        default:      return .event
        }
    }

    private func append(_ entry: E4LogEntry) {
        log.append(entry)
        if log.count > logLimit {
            log.removeFirst(log.count - logLimit)
        }
    }

    public func clearLog() {
        log.removeAll()
    }
}

/// One line in the on-screen log.
public struct E4LogEntry: Sendable, Identifiable, Equatable {
    public enum Kind: Sendable { case sent, event, unknown, alert }

    public let id = UUID()
    public let at = Date()
    public let text: String
    public let kind: Kind

    public init(text: String, kind: Kind) {
        self.text = text
        self.kind = kind
    }
}


/// Anything that wants every line as it arrives or leaves.
///
/// A protocol so `E4Session` need not know about files, and so a test can
/// record into memory.
@MainActor
public protocol E4LineRecorder: AnyObject {
    func record(_ line: String, tx: Bool)
}
