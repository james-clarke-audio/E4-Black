import XCTest
@testable import E4Core

/// Every line the threshold-calibration routine emits.
///
/// The strings here are copied from the report_printf calls in app_main.cpp
/// with the format specifiers filled in — not invented. A decoder tested
/// against lines I made up only proves the decoder agrees with me.
final class E4ThresholdCalTests: XCTestCase {

    /// decode(_:) returns a non-optional E4Message — an unrecognised line comes
    /// back as .text rather than nil, which is the whole point of a total
    /// decoder. So no optional pattern here.
    private func decode(_ line: String) -> E4ThresholdReport? {
        guard case .threshold(let report) = E4MessageDecoder.decode(line) else { return nil }
        return report
    }

    func testCapturingEachState() {
        XCTAssertEqual(decode("THR,capturing DEAD END..."), .capturing(.deadEnd))
        XCTAssertEqual(decode("THR,capturing OPEN FLOOR..."), .capturing(.openFloor))
        XCTAssertEqual(decode("THR,capturing CORRIDOR..."), .capturing(.corridor))
    }

    func testCapturedReadings() {
        XCTAssertEqual(decode("THR,deadend l=140 r=96 f=610"),
                       .captured(.deadEnd, left: 140, right: 96, front: 610))
        XCTAssertEqual(decode("THR,openfloor l=16 r=15 f=22"),
                       .captured(.openFloor, left: 16, right: 15, front: 22))
        XCTAssertEqual(decode("THR,corridor l=138 r=94 f=341"),
                       .captured(.corridor, left: 138, right: 94, front: 341))
    }

    /// The flags are L F R in that order — not L R F, which is the order the
    /// numbers in the same line use. Getting it wrong would light the wrong
    /// wall on the diagram and look like a sensor fault.
    func testLiveReadingAndItsFlags() {
        XCTAssertEqual(decode("THR,live l=138 r=94 f=22 -> L-R"),
                       .live(left: 138, right: 94, front: 22,
                             seesLeft: true, seesFront: false, seesRight: true))
        XCTAssertEqual(decode("THR,live l=16 r=15 f=610 -> -F-"),
                       .live(left: 16, right: 15, front: 610,
                             seesLeft: false, seesFront: true, seesRight: false))
        XCTAssertEqual(decode("THR,live l=16 r=15 f=22 -> ---"),
                       .live(left: 16, right: 15, front: 22,
                             seesLeft: false, seesFront: false, seesRight: false))
    }

    func testProposedAndCurrent() {
        XCTAssertEqual(decode("THR,new l=77 r=54 f=475 - S to save"),
                       .proposed(left: 77, right: 54, front: 475))
        XCTAssertEqual(decode("THR,set l=60 r=62 f=80 - S to save"),
                       .proposed(left: 60, right: 62, front: 80))
        XCTAssertEqual(decode("THR,now l=60 r=60 f=80"),
                       .current(left: 60, right: 60, front: 80))
    }

    /// The top-level THR command applies AND saves, and says so on the same
    /// line — so this one is a save, not a proposal.
    func testTopLevelSetIsASave() {
        XCTAssertEqual(decode("THR,set l=60 r=62 f=80 saved=1"), .saved(true))
        XCTAssertEqual(decode("THR,set l=60 r=62 f=80 saved=0"), .saved(false))
    }

    func testMargins() {
        guard case .margins(let m)? = decode("THR,margin L 16-140 good | R 15-96 ok | F 341-610 POOR") else {
            return XCTFail("margins did not decode")
        }
        XCTAssertEqual(m.count, 3)
        XCTAssertEqual(m[0].channel, .left)
        XCTAssertEqual(m[0].absent, 16)
        XCTAssertEqual(m[0].present, 140)
        XCTAssertEqual(m[0].verdict, .good)
        XCTAssertEqual(m[0].separation, 124)
        XCTAssertEqual(m[1].verdict, .ok)
        XCTAssertEqual(m[2].verdict, .poor)
        XCTAssertFalse(m[2].verdict.isTrustworthy)
    }

    func testDeadMarginIsNotTrustworthy() {
        guard case .margins(let m)? = decode("THR,margin L 300-140 DEAD | R 15-96 good | F 341-610 good") else {
            return XCTFail("margins did not decode")
        }
        XCTAssertEqual(m[0].verdict, .dead)
        XCTAssertFalse(m[0].verdict.isTrustworthy)
    }

    /// This line has a comma inside one human-readable phrase, so a decoder
    /// that split on commas would read it as two fields and lose the second
    /// number.
    func testCorridorBoundLineSurvivesItsOwnComma() {
        XCTAssertEqual(decode("THR,front bounded by CORRIDOR (341, floor was 22)"),
                       .frontBoundedByCorridor(corridor: 341, floor: 22))
    }

    func testSavedRejectedAndWarnings() {
        XCTAssertEqual(decode("THR,saved=1"), .saved(true))
        XCTAssertEqual(decode("THR,saved=0 (no EEPROM or write failed)"), .saved(false))
        XCTAssertEqual(decode("THR,rejected (expect 1..4095, front 1..8191)"),
                       .rejected("rejected (expect 1..4095, front 1..8191)"))
        XCTAssertEqual(decode("THR,WARN sides differ 31% - check mount depth/aim"),
                       .warning("sides differ 31% - check mount depth/aim"))
        XCTAssertEqual(decode("THR,NOTE front not yet checked against a corridor - capture C"),
                       .note("front not yet checked against a corridor - capture C"))
    }

    /// An unrecognised THR line must still reach the log as text rather than
    /// vanishing — the unparsed lines are exactly the ones worth reading when
    /// something is wrong.
    func testUnknownThrLineFallsBackToText() {
        guard case .text(let raw) = E4MessageDecoder.decode("THR,something new I have not written yet") else {
            return XCTFail("an unknown THR line should survive as text")
        }
        XCTAssertTrue(raw.contains("something new"))
    }

    // MARK: state machine

    func testStateTracksCapturesAndOrdersTheOutstandingOnes() {
        var state = E4ThresholdCalState()
        XCTAssertEqual(state.nextOutstanding, .deadEnd)
        XCTAssertFalse(state.isComplete)

        state.apply(.captured(.deadEnd, left: 140, right: 96, front: 610))
        XCTAssertTrue(state.has(.deadEnd))
        XCTAssertEqual(state.nextOutstanding, .openFloor)

        state.apply(.captured(.openFloor, left: 16, right: 15, front: 22))
        XCTAssertEqual(state.nextOutstanding, .corridor)

        state.apply(.captured(.corridor, left: 138, right: 94, front: 341))
        XCTAssertNil(state.nextOutstanding)
        XCTAssertTrue(state.isComplete)
    }

    /// While a capture is being taken the diagram must keep showing THAT
    /// configuration, not skip ahead to the next one.
    func testCapturingWinsOverNextOutstanding() {
        var state = E4ThresholdCalState()
        state.apply(.captured(.deadEnd, left: 1, right: 1, front: 1))
        XCTAssertEqual(state.targetState, .openFloor)
        state.apply(.capturing(.corridor))
        XCTAssertEqual(state.targetState, .corridor)
        state.apply(.captured(.corridor, left: 1, right: 1, front: 1))
        XCTAssertEqual(state.targetState, .openFloor)
    }

    func testProposingClearsAStaleSaveResult() {
        var state = E4ThresholdCalState()
        state.apply(.saved(true))
        XCTAssertEqual(state.lastSaveSucceeded, true)
        state.apply(.proposed(left: 1, right: 2, front: 3))
        XCTAssertNil(state.lastSaveSucceeded, "new numbers are not the saved ones")
    }

    func testProblemsAccumulateWithoutDuplicating() {
        var state = E4ThresholdCalState()
        state.apply(.warning("sides differ 31%"))
        state.apply(.warning("sides differ 31%"))
        state.apply(.warning("front channel cannot separate"))
        XCTAssertEqual(state.problems.count, 2)
    }

    func testResetClearsEverything() {
        var state = E4ThresholdCalState()
        state.apply(.captured(.deadEnd, left: 1, right: 1, front: 1))
        state.apply(.margins([E4ThresholdMargin(channel: .left, absent: 1, present: 2, verdict: .ok)]))
        state.reset()
        XCTAssertFalse(state.has(.deadEnd))
        XCTAssertTrue(state.margins.isEmpty)
        XCTAssertEqual(state.nextOutstanding, .deadEnd)
    }

    func testCaptureStateWallsMatchTheirNames() {
        XCTAssertEqual(E4CaptureState.deadEnd.walls.left, true)
        XCTAssertEqual(E4CaptureState.deadEnd.walls.front, true)
        XCTAssertEqual(E4CaptureState.corridor.walls.front, false)
        XCTAssertEqual(E4CaptureState.corridor.walls.left, true)
        XCTAssertEqual(E4CaptureState.openFloor.walls.right, false)
        XCTAssertEqual(E4CaptureState.deadEnd.key, "P")
        XCTAssertEqual(E4CaptureState.openFloor.key, "A")
        XCTAssertEqual(E4CaptureState.corridor.key, "C")
    }
}
