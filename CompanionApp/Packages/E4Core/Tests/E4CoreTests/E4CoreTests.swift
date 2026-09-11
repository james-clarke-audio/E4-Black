import XCTest
@testable import E4Core

// MARK: - Line reassembly
//
// These are the tests that matter most. Every sample below is a real failure
// mode of BLE-UART links, and each one silently corrupts data if the assembler
// is wrong.

final class E4LineAssemblerTests: XCTestCase {

    private func bytes(_ s: String) -> [UInt8] { Array(s.utf8) }

    func testSplitsOnCRLF() {
        var a = E4LineAssembler()
        XCTAssertEqual(a.append(bytes("IR,1,2,3,4\r\n")), ["IR,1,2,3,4"])
    }

    func testCRLFYieldsOneLineNotTwo() {
        var a = E4LineAssembler()
        let lines = a.append(bytes("A\r\nB\r\n"))
        XCTAssertEqual(lines, ["A", "B"])
    }

    func testLineSplitAcrossTwoNotifications() {
        // The classic: a 20-byte MTU cuts a line in half.
        var a = E4LineAssembler()
        XCTAssertEqual(a.append(bytes("TEL,1234,0,0,0,0")), [])
        XCTAssertEqual(a.append(bytes(",0,7.40\r\n")), ["TEL,1234,0,0,0,0,0,7.40"])
    }

    func testMultipleLinesInOneNotification() {
        var a = E4LineAssembler()
        XCTAssertEqual(a.append(bytes("W,0,0,5\r\nM,0,0,0\r\nPOS,90,41,0\r\n")),
                       ["W,0,0,5", "M,0,0,0", "POS,90,41,0"])
    }

    func testTrailingPartialIsHeldNotEmitted() {
        var a = E4LineAssembler()
        XCTAssertEqual(a.append(bytes("VER,0.9,Sep 11 2026\r\nIR,1,2")), ["VER,0.9,Sep 11 2026"])
        XCTAssertEqual(a.append(bytes(",3,4\r\n")), ["IR,1,2,3,4"])
    }

    func testOrphanedTerminatorProducesNoEmptyLine() {
        // A notification boundary can land between CR and LF.
        var a = E4LineAssembler()
        XCTAssertEqual(a.append(bytes("A\r")), ["A"])
        XCTAssertEqual(a.append(bytes("\nB\r\n")), ["B"])
    }

    func testResetDropsPartialLine() {
        var a = E4LineAssembler()
        _ = a.append(bytes("GARBAGE-NO-NEWLINE"))
        a.reset()
        XCTAssertEqual(a.append(bytes("IR,1,2,3,4\r\n")), ["IR,1,2,3,4"])
    }

    func testOverlongLineIsDiscardedWholeNotTruncated() {
        // Small limit so the flood actually trips it — the production default
        // is 4096, which 200 bytes would never reach.
        var a = E4LineAssembler(limit: 100)
        let flood = String(repeating: "x", count: 200)
        _ = a.append(bytes(flood))
        // The tail of the overlong line must not surface as a short line.
        XCTAssertEqual(a.append(bytes("tail\r\nIR,1,2,3,4\r\n")), ["IR,1,2,3,4"])
    }

    func testNormalLinesAreNeverTruncatedByTheLimit() {
        // Guards the other side of the same behaviour: the firmware's longest
        // real line is nowhere near the default limit, so nothing it sends
        // should ever be discarded.
        var a = E4LineAssembler()
        let long = "SENS,L265 FL199 FR142 R270 front341 thr(s60 f80) REAL SAMPLER"
        XCTAssertEqual(a.append(bytes(long + "\r\n")), [long])
    }
}

// MARK: - Message decoding

final class E4MessageDecoderTests: XCTestCase {

    func testPose() {
        guard case .pose(let x, let y, let deg) = E4MessageDecoder.decode("POS,90,209,0") else {
            return XCTFail("not a pose")
        }
        XCTAssertEqual(x, 90); XCTAssertEqual(y, 209); XCTAssertEqual(deg, 0)
    }

    func testTelemetryIncludingFractionalBattery() {
        guard case .telemetry(let t) = E4MessageDecoder.decode("TEL,12345,300,0,180,90,2,7.40") else {
            return XCTFail("not telemetry")
        }
        XCTAssertEqual(t.timestamp, 12345)
        XCTAssertEqual(t.velocity, 300)
        XCTAssertEqual(t.battery, 7.40, accuracy: 0.001)
    }

    func testIR() {
        guard case .ir(let l, let fl, let fr, let r) = E4MessageDecoder.decode("IR,265,199,142,270") else {
            return XCTFail("not IR")
        }
        XCTAssertEqual([l, fl, fr, r], [265, 199, 142, 270])
    }

    func testVersionKeepsCommasInBuildStamp() {
        // FW_BUILD is __DATE__ " " __TIME__ — no comma today, but the decoder
        // must not lose one if the format ever changes.
        guard case .version(let fw, let build) =
                E4MessageDecoder.decode("VER,0.9,Sep 11 2026 18:22:01") else {
            return XCTFail("not a version")
        }
        XCTAssertEqual(fw, "0.9")
        XCTAssertEqual(build, "Sep 11 2026 18:22:01")
    }

    func testWallsMask() {
        guard case .walls(_, _, let mask) = E4MessageDecoder.decode("W,3,4,5") else {
            return XCTFail("not walls")
        }
        XCTAssertTrue(mask.contains(.north))
        XCTAssertTrue(mask.contains(.south))
        XCTAssertFalse(mask.contains(.east))
    }

    func testSensorStatusScrapesThresholdsAndMode() {
        let line = "SENS,L265 FL199 FR142 R270 front341 thr(s60 f80) REAL"
        guard case .sensorStatus(let s) = E4MessageDecoder.decode(line) else {
            return XCTFail("not sensor status")
        }
        XCTAssertEqual(s.left, 265)
        XCTAssertEqual(s.frontLeft, 199)
        XCTAssertEqual(s.frontRight, 142)
        XCTAssertEqual(s.right, 270)
        XCTAssertEqual(s.frontSum, 341)
        XCTAssertEqual(s.sideThreshold, 60)
        XCTAssertEqual(s.frontThreshold, 80)
        XCTAssertTrue(s.usingRealIR)
    }

    func testSensorStatusVirtualMode() {
        let line = "SENS,L0 FL0 FR0 R0 front0 thr(s60 f80) VIRTUAL"
        guard case .sensorStatus(let s) = E4MessageDecoder.decode(line) else {
            return XCTFail("not sensor status")
        }
        XCTAssertFalse(s.usingRealIR)
    }

    func testTurnResult() {
        guard case .turnResult(let r) =
                E4MessageDecoder.decode("TURNRES,spin,cmd=90,gyro=90,dist=3") else {
            return XCTFail("not a turn result")
        }
        XCTAssertEqual(r.kind, .spin)
        XCTAssertEqual(r.commanded, 90)
        XCTAssertEqual(r.distance, 3)
    }

    func testGyroCalProposal() {
        guard case .gyroCal(let g) =
                E4MessageDecoder.decode("GCAL,old=1.003,short=4,new=0.997 - S to save") else {
            return XCTFail("not a gyro cal report")
        }
        XCTAssertEqual(g.oldScale!, 1.003, accuracy: 0.0001)
        XCTAssertEqual(g.shortfall!, 4, accuracy: 0.0001)
        XCTAssertEqual(g.currentScale!, 0.997, accuracy: 0.0001)
        XCTAssertFalse(g.rejected)
    }

    func testGyroCalRejection() {
        guard case .gyroCal(let g) =
                E4MessageDecoder.decode("GCAL,rejected (would give 1.400, expect 0.85-1.15)") else {
            return XCTFail("not a gyro cal report")
        }
        XCTAssertTrue(g.rejected)
    }

    func testGyroCalSaved() {
        guard case .gyroCal(let g) = E4MessageDecoder.decode("GCAL,saved=0 (no EEPROM or write failed)") else {
            return XCTFail("not a gyro cal report")
        }
        XCTAssertEqual(g.saved, false)
    }

    func testConfigLoaded() {
        guard case .config(let c) = E4MessageDecoder.decode("CFG,loaded v1 gyro_scale=1.003") else {
            return XCTFail("not a config report")
        }
        XCTAssertEqual(c.outcome, .loaded(version: 1))
        XCTAssertEqual(c.gyroScale!, 1.003, accuracy: 0.0001)
    }

    func testConfigChecksumFailure() {
        guard case .config(let c) = E4MessageDecoder.decode("CFG,checksum-fail v1") else {
            return XCTFail("not a config report")
        }
        XCTAssertEqual(c.outcome, .checksumFailure(version: 1))
    }

    func testStructuredActionVersusFreeText() {
        guard case .action(let a) = E4MessageDecoder.decode("ACT,F,s,3,4,1") else {
            return XCTFail("structured ACT should decode as an action")
        }
        XCTAssertEqual(a.action, "F")
        XCTAssertEqual(a.heading, .east)

        guard case .actionNote(let note) = E4MessageDecoder.decode("ACT,todo Speed run") else {
            return XCTFail("free-text ACT should decode as a note")
        }
        XCTAssertEqual(note, "todo Speed run")

        guard case .actionNote = E4MessageDecoder.decode("ACT,lowbatt") else {
            return XCTFail("ACT,lowbatt should decode as a note")
        }
    }

    func testDoneSplitsIndexFromDetail() {
        guard case .runFinished(let i, let detail) =
                E4MessageDecoder.decode("DONE,27 gyro_a=720 d=25") else {
            return XCTFail("not a run-finished")
        }
        XCTAssertEqual(i, 27)
        XCTAssertEqual(detail, "gyro_a=720 d=25")
    }

    func testGoalAcceptsMultiplePairs() {
        guard case .goal(let cells) = E4MessageDecoder.decode("GOAL,7,7,7,8,8,7,8,8") else {
            return XCTFail("not a goal")
        }
        XCTAssertEqual(cells.count, 4)
        XCTAssertEqual(cells.first, E4Cell(x: 7, y: 7))
    }

    func testMazeTruthRow() {
        guard case .mazeTruth(.row(let y, let masks)) =
                E4MessageDecoder.decode("GTV,0,e8a2c4061395bd7f") else {
            return XCTFail("not a truth row")
        }
        XCTAssertEqual(y, 0)
        XCTAssertEqual(masks.count, 16)
        XCTAssertEqual(masks[0], E4WallMask(rawValue: 0xe))
    }

    func testMazeTruthAcks() {
        XCTAssertEqual(E4MessageDecoder.decode("GTOK"), .mazeTruth(.ok))
        XCTAssertEqual(E4MessageDecoder.decode("GTERR"), .mazeTruth(.error))
        XCTAssertEqual(E4MessageDecoder.decode("GTDONE"), .mazeTruth(.done))
        XCTAssertEqual(E4MessageDecoder.decode("GTGOK,7,7"), .mazeTruth(.goalAccepted(x: 7, y: 7)))
    }

    func testUnknownLinesSurviveAsText() {
        // EEPROM scan output and prompts must reach the log intact.
        let line = "EE scan: 1 device(s)"
        XCTAssertEqual(E4MessageDecoder.decode(line), .text(line))
    }

    func testMalformedStructuredLineFallsBackToTextRatherThanCrashing() {
        XCTAssertEqual(E4MessageDecoder.decode("POS,"), .text("POS,"))
        XCTAssertEqual(E4MessageDecoder.decode("TEL,1,2"), .text("TEL,1,2"))
    }
}

// MARK: - Command encoding

final class E4CommandTests: XCTestCase {

    func testMenuKeysAreExactlyOneCharacterPlusTerminator() {
        // The firmware checks `bt_len == 1` literally. Any extra byte before
        // the newline and the key is silently ignored.
        for command: E4Command in [.next, .previous, .enter, .back] {
            XCTAssertEqual(command.line.count, 2, "\(command) must be one char + newline")
            XCTAssertTrue(command.line.hasSuffix("\n"))
        }
        for action in E4MenuAction.allCases {
            XCTAssertEqual(E4Command.action(action).line.count, 2)
        }
    }

    func testActionKeysAreUnique() {
        let keys = E4MenuAction.allCases.map(\.key)
        XCTAssertEqual(Set(keys).count, keys.count, "duplicate shortcut key in the menu table")
    }

    func testIntegersEncodeWithoutDecimalPoint() {
        // atoi() stops at the '.', so "90.0" would still parse as 90 — but
        // ARC's later fields would shift if a stray separator appeared.
        XCTAssertEqual(E4Command.spin(angle: 90, omega: 600, alpha: 3000).line,
                       "SPIN,90,600,3000\n")
    }

    func testArcEncodesAllSixFieldsInOrder() {
        XCTAssertEqual(
            E4Command.arc(velocity: 300, angle: 90, omega: 600,
                          alpha: 3000, leadIn: 20, leadOut: 20).line,
            "ARC,300,90,600,3000,20,20\n")
    }

    func testArcCommandExceedsDefaultMTU() {
        // Documents *why* the transport chunks: this real command is 26 bytes.
        let line = E4Command.arc(velocity: 300, angle: 90, omega: 600,
                                 alpha: 3000, leadIn: 20, leadOut: 20).line
        XCTAssertGreaterThan(line.utf8.count, 20)
    }

    func testCommandsFitTheFirmwareLineBuffer() {
        let longest = E4Command.arc(velocity: 1000, angle: 180, omega: 3600,
                                    alpha: 30000, leadIn: 100, leadOut: 100).line
        XCTAssertLessThanOrEqual(longest.utf8.count, E4Protocol.maxInboundLineLength)
    }

    func testGyroScaleUsesThreeDecimals() {
        XCTAssertEqual(E4Command.gyroScale(1.003).line, "GS,1.003\n")
    }
}
