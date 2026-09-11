import SwiftUI
import E4Core

/// Owns the folder of session logs, and starts and stops one as she connects
/// and disconnects.
///
/// A session is a connection, not an app launch: that is the unit you actually
/// compare against another. Reconnecting mid-evening starts a new file, which
/// is right — something changed, or you would not have reconnected.
@MainActor
@Observable
final class SessionStore: E4LineRecorder {

    private(set) var recording: URL?
    private(set) var linesWritten = 0
    private(set) var summaries: [E4SessionSummary] = []

    /// Off by default would be the wrong call: a session you did not record is
    /// gone, and the whole point is comparing today against a fortnight ago.
    var enabled = true

    private var writer: E4SessionWriter?
    private weak var session: E4Session?

    /// Application Support, not Documents: these are the app's records, not
    /// files the user manages by hand. Export puts a copy wherever they want.
    let directory: URL = {
        let base = FileManager.default.urls(for: .applicationSupportDirectory,
                                            in: .userDomainMask).first!
        return base.appendingPathComponent("E4Companion/Sessions", isDirectory: true)
    }()

    func attach(to session: E4Session) {
        self.session = session
        session.recorder = self
        refresh()
    }

    // MARK: - Recording

    func record(_ line: String, tx: Bool) {
        writer?.record(line, tx: tx)
        linesWritten = writer?.lineCount ?? 0
    }

    /// Called when the connection state changes. Starting on connect rather
    /// than on first line means the header carries the device name.
    func connectionChanged(_ state: E4ConnectionState, session: E4Session) {
        switch state {
        case .connected(let name):
            start(device: name, session: session)
        case .idle, .failed:
            stop()
        default:
            break
        }
    }

    private func start(device: String, session: E4Session) {
        guard enabled, writer == nil else { return }
        // Firmware version arrives a moment after connecting, so the header may
        // not have it. Better than delaying the start and losing those lines.
        let header = E4SessionLog.Header(started: Date(),
                                         device: device,
                                         firmware: session.firmwareVersion,
                                         build: session.firmwareBuild)
        do {
            let w = try E4SessionWriter(directory: directory, header: header)
            writer = w
            recording = w.url
            linesWritten = 0
        } catch {
            // Not fatal: the app is still usable, you just have no record.
            recording = nil
            writer = nil
        }
    }

    private func stop() {
        writer?.close()
        writer = nil
        recording = nil
        refresh()
    }

    /// Force a flush — call when the app goes to the background, where it may
    /// not get another chance.
    func flush() { writer?.flush() }

    // MARK: - Reading back

    func refresh() {
        summaries = E4SessionReader.summaries(in: directory)
    }

    func stats(for summary: E4SessionSummary) -> E4SessionStats? {
        try? E4SessionReader.stats(of: summary.url)
    }

    func delete(_ summary: E4SessionSummary) {
        try? FileManager.default.removeItem(at: summary.url)
        refresh()
    }
}
