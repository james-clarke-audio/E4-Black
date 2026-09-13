import XCTest
@testable import E4Core

/// The `CFG?` dump: her whole live configuration, turn table included.
final class E4ConfigDumpTests: XCTestCase {

    private func decode(_ line: String) -> E4Message { E4MessageDecoder.decode(line) }

    func testTurnRowDecodes() {
        guard case .turn(let t) = decode("TRN,4,SS180L,v=300,in=90,ex=30,out=90,a=180,w=191,al=2500") else {
            return XCTFail("turn row did not decode")
        }
        XCTAssertEqual(t.index, 4)
        XCTAssertEqual(t.name, "SS180L")
        XCTAssertEqual(t.speed, 300)
        XCTAssertEqual(t.entryOffset, 90)
        XCTAssertEqual(t.exitOffset, 30)
        XCTAssertEqual(t.leadOut, 90)
        XCTAssertEqual(t.angle, 180)
        XCTAssertEqual(t.omega, 191)
        XCTAssertEqual(t.alpha, 2500)
        XCTAssertTrue(t.isLeft)
    }

    /// R = v / omega. The firmware does not send a radius because it does not
    /// store one, so if this drifts the whole table stops meaning anything.
    func testRadiusIsDerivedFromOmega() {
        guard case .turn(let t) = decode("TRN,1,SS90ER,v=300,in=100,ex=30,out=90,a=-90,w=170,al=2500") else {
            return XCTFail("decode")
        }
        XCTAssertEqual(t.radiusMM, 101, accuracy: 0.5)
        XCTAssertFalse(t.isLeft)
        XCTAssertTrue(t.isDriven)
    }

    func testTightDiagonalTurnHasHalfTheRadius() {
        guard case .turn(let t) = decode("TRN,14,DD90L,v=300,in=63,ex=30,out=63,a=90,w=273,al=2500") else {
            return XCTFail("decode")
        }
        XCTAssertEqual(t.radiusMM, 63, accuracy: 1.0)
        XCTAssertFalse(t.isDriven, "nothing drives the diagonal rows yet")
    }

    /// "CFG,dump v5 present=1 loaded=1" contains the word "loaded", so a
    /// decoder checking that first would read the dump header as a load report
    /// and lose the whole dump.
    func testDumpHeaderIsNotMistakenForALoadReport() {
        guard case .config(let cfg) = decode("CFG,dump v5 present=1 loaded=1 turns=16") else {
            return XCTFail("not a config line")
        }
        guard case .dumpBegin(let version, let present, let loaded, let turns) = cfg.outcome else {
            return XCTFail("expected dumpBegin, got \(cfg.outcome)")
        }
        XCTAssertEqual(version, 5)
        XCTAssertTrue(present)
        XCTAssertTrue(loaded)
        XCTAssertEqual(turns, 16)
    }

    func testDumpEndDecodes() {
        guard case .config(let cfg) = decode("CFG,dump end"),
              case .dumpEnd = cfg.outcome else {
            return XCTFail("expected dumpEnd")
        }
    }

    func testOrdinaryLoadReportStillDecodes() {
        guard case .config(let cfg) = decode("CFG,loaded v5 gyro_scale=1.002"),
              case .loaded(let v) = cfg.outcome else {
            return XCTFail("expected loaded")
        }
        XCTAssertEqual(v, 5)
        XCTAssertEqual(cfg.gyroScale ?? 0, 1.002, accuracy: 0.0005)
    }

    // MARK: collection

    @MainActor
    func testSessionCollectsAWholeDump() {
        let session = E4Session(transport: DumpTransport())
        session.beginReplay()          // feed lines without a radio

        session.replay(line: "CFG,dump v5 present=1 loaded=1 turns=3", sent: false)
        XCTAssertFalse(session.configDump.isComplete)

        session.replay(line: "TRN,0,SS90EL,v=300,in=100,ex=30,out=90,a=90,w=170,al=2500", sent: false)
        session.replay(line: "TRN,1,SS90ER,v=300,in=100,ex=30,out=90,a=-90,w=170,al=2500", sent: false)
        XCTAssertFalse(session.configDump.isComplete, "not complete until the end marker")

        session.replay(line: "TRN,2,SS90L,v=300,in=100,ex=30,out=90,a=90,w=170,al=2500", sent: false)
        session.replay(line: "CFG,dump end", sent: false)

        XCTAssertTrue(session.configDump.isComplete)
        XCTAssertEqual(session.configDump.turns.count, 3)
        XCTAssertEqual(session.configDump.version, 5)
        XCTAssertEqual(session.configDump.eepromPresent, true)
    }

    /// A second dump must replace the first outright. Merging would leave rows
    /// from a configuration she no longer holds.
    @MainActor
    func testASecondDumpReplacesTheFirst() {
        let session = E4Session(transport: DumpTransport())
        session.beginReplay()
        session.replay(line: "CFG,dump v5 present=1 loaded=1 turns=1", sent: false)
        session.replay(line: "TRN,0,SS90EL,v=300,in=100,ex=30,out=90,a=90,w=170,al=2500", sent: false)
        session.replay(line: "CFG,dump end", sent: false)
        XCTAssertEqual(session.configDump.turns.first?.entryOffset, 100)

        session.replay(line: "CFG,dump v5 present=1 loaded=1 turns=1", sent: false)
        session.replay(line: "TRN,0,SS90EL,v=300,in=77,ex=30,out=90,a=90,w=170,al=2500", sent: false)
        session.replay(line: "CFG,dump end", sent: false)
        XCTAssertEqual(session.configDump.turns.count, 1)
        XCTAssertEqual(session.configDump.turns.first?.entryOffset, 77)
    }

    func testCommandWireFormat() {
        XCTAssertEqual(E4Command.readConfig.line, "CFG?\n")
        XCTAssertEqual(E4Command.selectTurn(14).line, "SEL,14\n")
        XCTAssertEqual(E4Command.readVersion.line, "VER?\n")
    }

    /// Connecting must GREET her, not drive her. The Firmware ver menu action
    /// holds her OLED waiting for a button press — fired automatically on
    /// connect it looked like a hang on a screen resembling the boot splash,
    /// and left the physical menu parked in Diagnostics.
    @MainActor
    func testConnectingSendsAQueryNotAMenuAction() {
        let transport = RecordingTransport()
        let session = E4Session(transport: transport)

        // The session has to be held across the callback. wire() captures
        // [weak self], so a discarded session is deallocated and the handler
        // bails out before sending anything — which is a test measuring ARC
        // rather than the greeting.
        withExtendedLifetime(session) {
            transport.onStateChange?(.connected("MMOUSE"))

            XCTAssertEqual(transport.sent, ["VER?\n"])
            XCTAssertFalse(transport.sent.contains("V\n"),
                           "a bare V runs the blocking menu action")
        }
    }
}

@MainActor
private final class RecordingTransport: E4Transport {
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

@MainActor
private final class DumpTransport: E4Transport {
    var onStateChange: ((E4ConnectionState) -> Void)?
    var onDiscover: (([E4Peripheral]) -> Void)?
    var onLine: ((String) -> Void)?
    func startScan(includeUnnamed: Bool) {}
    func stopScan() {}
    func connect(to id: UUID) {}
    func disconnect() {}
    func send(_ text: String) {}
}
