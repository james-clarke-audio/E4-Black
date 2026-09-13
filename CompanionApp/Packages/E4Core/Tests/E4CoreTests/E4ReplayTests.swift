import XCTest
@testable import E4Core

/// Replay: reading a log, and the guard that keeps a replay from reaching a
/// real mouse.
final class E4ReplayTests: XCTestCase {

    private let log = """
    {"version":1,"started":"2026-09-13T08:57:42Z","device":"MMOUSE","firmware":"0.13"}
    {"t":0.10,"line":"VER,0.13,Sep 13 2026 08:37:32"}
    {"t":0.50,"line":"TEL,1000,0,0,0,0,0,3.70"}
    {"t":1.50,"line":"W,0,0,14"}
    {"t":2.00,"line":"h","tx":true}
    {"t":2.50,"line":"SENS,L265 FL199 FR142 R270 front341 thr(l60 r62 f80) REAL"}
    """

    private func data(_ s: String) -> Data { s.data(using: .utf8)! }

    // MARK: parsing

    func testParsesHeaderAndEntries() throws {
        let parsed = try E4SessionReader.parse(data(log))
        XCTAssertEqual(parsed.header?.device, "MMOUSE")
        XCTAssertEqual(parsed.header?.firmware, "0.13")
        XCTAssertEqual(parsed.entries.count, 5)
        XCTAssertEqual(parsed.entries.first?.line, "VER,0.13,Sep 13 2026 08:37:32")
        XCTAssertEqual(parsed.entries.last?.t, 2.50)
    }

    /// A sent line is marked, and must never be re-sent on playback.
    func testSentLinesAreMarked() throws {
        let parsed = try E4SessionReader.parse(data(log))
        let sent = parsed.entries.filter { $0.tx == true }
        XCTAssertEqual(sent.count, 1)
        XCTAssertEqual(sent.first?.line, "h")
    }

    /// A truncated final line is normal after a crash. Losing the whole
    /// recording over its last fragment would be absurd.
    func testTruncatedLastLineIsSkippedNotFatal() throws {
        let parsed = try E4SessionReader.parse(data(log + "\n{\"t\":3.0,\"lin"))
        XCTAssertEqual(parsed.entries.count, 5)
    }

    /// A hand-trimmed extract has no header, and is still worth playing.
    func testHeaderlessExtractStillParses() throws {
        let parsed = try E4SessionReader.parse(data("""
        {"t":0.1,"line":"W,0,0,14"}
        {"t":0.2,"line":"W,0,1,10"}
        """))
        XCTAssertNil(parsed.header)
        XCTAssertEqual(parsed.entries.count, 2)
    }

    func testEmptyFileIsAnError() {
        XCTAssertThrowsError(try E4SessionReader.parse(data("")))
        XCTAssertThrowsError(try E4SessionReader.parse(data("not a log at all")))
    }

    // MARK: the session guard

    @MainActor
    func testReplayedLinesDriveTheSameStateAsLiveOnes() {
        let transport = FakeTransport()
        let session = E4Session(transport: transport)
        session.beginReplay()

        session.replay(line: "VER,0.13,Sep 13 2026 08:37:32", sent: false)
        XCTAssertEqual(session.firmwareVersion, "0.13")

        session.replay(line: "TEL,1000,0,0,0,0,0,3.70", sent: false)
        XCTAssertEqual(session.battery ?? 0, 3.70, accuracy: 0.001)
    }

    /// The whole point of the guard: a screen showing history must not be able
    /// to move a mouse in the present.
    @MainActor
    func testNothingIsTransmittedWhileReplaying() {
        let transport = FakeTransport()
        let session = E4Session(transport: transport)

        session.beginReplay()
        session.send(.action(.search))
        session.sendRaw("GTC")
        XCTAssertTrue(transport.sent.isEmpty, "a replay must not reach the radio")

        session.endReplay()
        session.sendRaw("GTC")
        XCTAssertEqual(transport.sent.count, 1, "and must work again afterwards")
    }

    /// A recorded outbound line is shown, not re-sent.
    @MainActor
    func testRecordedSentLinesAreShownButNotTransmitted() {
        let transport = FakeTransport()
        let session = E4Session(transport: transport)
        session.beginReplay()
        session.replay(line: "h", sent: true)
        XCTAssertTrue(transport.sent.isEmpty)
        XCTAssertTrue(session.log.contains { $0.text.contains("h") })
    }

    /// A replay must not be recorded as though it were a new session — that
    /// would multiply one run into a fresh log every time you watched it.
    @MainActor
    func testReplayIsNotRecorded() {
        let transport = FakeTransport()
        let session = E4Session(transport: transport)
        let recorder = CountingRecorder()
        session.recorder = recorder
        session.beginReplay()
        session.replay(line: "TEL,1000,0,0,0,0,0,3.70", sent: false)
        XCTAssertEqual(recorder.count, 0)
    }

    @MainActor
    func testReplayIgnoresLinesWhenNotReplaying() {
        let session = E4Session(transport: FakeTransport())
        session.replay(line: "VER,9.99,x", sent: false)
        XCTAssertNil(session.firmwareVersion)
    }
}

// MARK: - doubles

@MainActor
private final class FakeTransport: E4Transport {
    var onStateChange: ((E4ConnectionState) -> Void)?
    var onDiscover: (([E4Peripheral]) -> Void)?
    var onLine: ((String) -> Void)?
    var sent: [String] = []

    func startScan(includeUnnamed: Bool) {}
    func stopScan() {}
    func connect(to id: UUID) {}
    func disconnect() {}
    func send(_ text: String) { sent.append(text) }
}

private final class CountingRecorder: E4LineRecorder {
    var count = 0
    func record(_ line: String, tx: Bool) { count += 1 }
}
