import Foundation

/// Uploads a parsed maze to the mouse and checks she got it.
///
/// The link is a 20-byte-MTU BLE bridge onto a UART, and a corrupted row would
/// be a maze she solves confidently and wrongly — the worst failure mode there
/// is, because nothing about it looks like an error. So every row carries a
/// position-weighted checksum she verifies before accepting it, every line is
/// acked, and at the end the whole maze is read back and diffed against what
/// was sent. A row that will not go after ten attempts fails the upload rather
/// than being skipped.
@MainActor
@Observable
public final class E4MazeUploader {

    public enum Phase: Sendable, Equatable {
        case idle
        case clearing
        case sendingRow(Int)
        case sendingGoal
        case verifying
        case finished(E4MazeUploadResult)
        case failed(String)
    }

    public private(set) var phase: Phase = .idle

    /// True while an upload is in flight, so the UI can refuse a second one
    /// rather than interleaving two streams of GT lines on one link.
    public var isUploading: Bool {
        switch phase {
        case .idle, .finished, .failed: return false
        default: return true
        }
    }

    // Neither of these is UI state, and @Observable would otherwise generate
    // tracking for both — including for a weak var, which the macro handles
    // awkwardly. `phase` is the only thing a view should be watching.
    @ObservationIgnored private weak var session: E4Session?
    @ObservationIgnored private var truthHook: ((E4MazeTruth) -> Void)?

    @ObservationIgnored private let attempts = 10
    @ObservationIgnored private let ackTimeout = Duration.milliseconds(2500)
    @ObservationIgnored private let verifyTimeout = Duration.seconds(5)
    @ObservationIgnored private let settle = Duration.milliseconds(30)

    public init() {}

    /// One observer for the life of the uploader. The session's observer list
    /// has no removal, so registering per upload would leak one every time.
    public func attach(to session: E4Session) {
        self.session = session
        session.observe { [weak self] message in
            guard case .mazeTruth(let truth) = message else { return }
            self?.truthHook?(truth)
        }
    }

    public func reset() { phase = .idle }

    // MARK: - The upload

    @discardableResult
    public func upload(_ maze: E4MazeFile) async -> E4MazeUploadResult? {
        guard let session, session.connection.isConnected else {
            phase = .failed("Not connected")
            return nil
        }
        guard !isUploading else { return nil }

        phase = .clearing
        guard await sendAcked(.mazeClear, accept: { $0 == .ok }) else {
            phase = .failed("She did not acknowledge the clear")
            return nil
        }

        for y in 0..<E4MazeFile.size {
            phase = .sendingRow(y)
            let command = E4Command.mazeRow(y: y,
                                            hex: maze.rowHex(y),
                                            checksum: maze.rowChecksum(y))
            guard await sendAcked(command, accept: { $0 == .ok }) else {
                phase = .failed("Row \(y) failed after \(attempts) attempts")
                return nil
            }
        }

        phase = .sendingGoal
        let goal = maze.chosenGoal
        // She echoes the goal she actually set, so this compares against the
        // echo rather than accepting a bare OK — a goal one cell out is a maze
        // that solves to the wrong place.
        let goalOK = await sendAcked(.mazeGoal(x: goal.x, y: goal.y)) { truth in
            truth == .goalAccepted(x: goal.x, y: goal.y)
        }
        guard goalOK else {
            phase = .failed("Goal \(goal.x),\(goal.y) was not echoed back")
            return nil
        }

        phase = .verifying
        guard let result = await verify(against: maze, goal: goal) else {
            phase = .failed("She stopped responding during the read-back")
            return nil
        }
        phase = .finished(result)
        return result
    }

    // MARK: - Line level

    private func sendAcked(_ command: E4Command,
                           accept: @escaping (E4MazeTruth) -> Bool) async -> Bool {
        guard let session else { return false }
        for _ in 1...attempts {
            session.send(command)
            if let truth = await waitForTruth(timeout: ackTimeout), accept(truth) {
                return true
            }
            // A rejected row is usually a mangled line, not a broken mouse.
            // Pausing before the resend lets the UART bridge drain; hammering
            // it immediately tends to corrupt the retry as well.
            try? await Task.sleep(for: settle)
        }
        return false
    }

    /// Waits for the next GT reply, or gives up. Resolves exactly once: the
    /// timeout and the reply race, and resuming a continuation twice is a crash,
    /// not a warning.
    private func waitForTruth(timeout: Duration) async -> E4MazeTruth? {
        await withCheckedContinuation { (continuation: CheckedContinuation<E4MazeTruth?, Never>) in
            var done = false
            let finish: (E4MazeTruth?) -> Void = { [weak self] value in
                guard !done else { return }
                done = true
                self?.truthHook = nil
                continuation.resume(returning: value)
            }
            truthHook = { finish($0) }
            Task { @MainActor in
                try? await Task.sleep(for: timeout)
                finish(nil)
            }
        }
    }

    /// `GTE` makes her read the whole truth maze back. Rows are collected until
    /// `GTDONE`, then diffed against what was sent.
    private func verify(against maze: E4MazeFile, goal: E4MazeFile.Cell) async -> E4MazeUploadResult? {
        guard let session else { return nil }
        return await withCheckedContinuation { (continuation: CheckedContinuation<E4MazeUploadResult?, Never>) in
            var done = false
            var rows: [Int: String] = [:]
            var goalReadback: E4MazeFile.Cell?

            let finish: (E4MazeUploadResult?) -> Void = { [weak self] value in
                guard !done else { return }
                done = true
                self?.truthHook = nil
                continuation.resume(returning: value)
            }

            truthHook = { truth in
                switch truth {
                case .row(let y, let masks):
                    let digits = "0123456789abcdef"
                    rows[y] = masks.map { m -> String in
                        let v = m.rawValue & 0x0F
                        return String(digits[digits.index(digits.startIndex, offsetBy: v)])
                    }.joined()
                case .goalReadback(let x, let y):
                    goalReadback = E4MazeFile.Cell(x: x, y: y)
                case .done:
                    let mismatched = (0..<E4MazeFile.size).filter { rows[$0] != maze.rowHex($0) }
                    finish(E4MazeUploadResult(mismatchedRows: mismatched,
                                              goalSent: goal,
                                              goalReadback: goalReadback))
                default:
                    break
                }
            }

            session.send(.mazeVerify)
            Task { @MainActor in
                try? await Task.sleep(for: verifyTimeout)
                finish(nil)
            }
        }
    }
}

/// What came back from the read-back. An upload is only good if the maze she
/// holds matches the file — not if every line was acked along the way.
public struct E4MazeUploadResult: Sendable, Equatable {
    public let mismatchedRows: [Int]
    public let goalSent: E4MazeFile.Cell
    public let goalReadback: E4MazeFile.Cell?

    public var goalMatches: Bool { goalReadback == goalSent }
    public var isClean: Bool { mismatchedRows.isEmpty && goalMatches }

    public var summary: String {
        if isClean { return "Verified — her map matches the file" }
        var parts: [String] = []
        if !mismatchedRows.isEmpty {
            parts.append(mismatchedRows.count == 1
                         ? "row \(mismatchedRows[0]) differs"
                         : "\(mismatchedRows.count) rows differ (\(mismatchedRows.map(String.init).joined(separator: ", ")))")
        }
        if !goalMatches {
            parts.append(goalReadback.map { "goal came back as \($0.x),\($0.y)" } ?? "no goal read back")
        }
        return parts.joined(separator: " · ")
    }
}
