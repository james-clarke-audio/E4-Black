import XCTest
@testable import E4Core

// MARK: - Planned routes
//
// The route messages carry geometry, and geometry is where this protocol can
// go wrong quietly: a route that decodes to plausible-looking numbers still
// draws a wrong line. So these tests pin the half-cell convention itself, not
// just the field order.
//
// Half-cells: u = x/90, v = y/90. Parity says what a point is --
//   odd,odd   a cell centre       cell (x,y) is at (2x+1, 2y+1)
//   mixed     a wall midpoint     where a diagonal runs
//   even,even a POST              never occupied, so never sent

final class E4RouteMessageTests: XCTestCase {

    // --- RT ----------------------------------------------------------------

    func testRouteBeginCarriesKindAndTime() {
        guard case .routeBegin(let kind, let ms) = E4MessageDecoder.decode("RT,2,18450") else {
            return XCTFail("not a routeBegin")
        }
        XCTAssertEqual(kind, .diagonal)
        XCTAssertEqual(ms, 18450)
    }

    func testAllThreeKindsDecode() {
        let expected: [(String, E4RouteKind)] = [("RT,0,1", .shortest),
                                                 ("RT,1,1", .quickest),
                                                 ("RT,2,1", .diagonal)]
        for (line, want) in expected {
            guard case .routeBegin(let kind, _) = E4MessageDecoder.decode(line) else {
                return XCTFail("\(line) did not decode")
            }
            XCTAssertEqual(kind, want, "wrong kind for \(line)")
        }
    }

    func testNoRouteIsSignalledByNegativeTime() {
        // The firmware sends -1 rather than omitting the block, so the app can
        // tell "she could not find one" from "she has not been asked yet".
        guard case .routeBegin(let kind, let ms) = E4MessageDecoder.decode("RT,1,-1") else {
            return XCTFail("not a routeBegin")
        }
        XCTAssertEqual(kind, .quickest)
        XCTAssertEqual(ms, -1)
    }

    func testUnknownKindIsRejectedRatherThanGuessed() {
        // A kind the app does not understand must not be silently drawn as
        // some other route. Better to drop the line than to draw a lie.
        if case .routeBegin = E4MessageDecoder.decode("RT,7,1000") {
            XCTFail("kind 7 should not decode")
        }
    }

    // --- RP ----------------------------------------------------------------

    func testRoutePointIsHalfCells() {
        // (1,1) is the centre of cell (0,0): 2*0+1 = 1 on both axes.
        guard case .routePoint(let u, let v, let move) = E4MessageDecoder.decode("RP,1,1,0") else {
            return XCTFail("not a routePoint")
        }
        XCTAssertEqual([u, v], [1, 1])
        XCTAssertEqual(move, 0)
    }

    func testWallMidpointHasExactlyOneOddCoordinate() {
        // A point on a diagonal. If this ever decodes to an even,even pair the
        // firmware is sending a post, which is not a place she can be.
        guard case .routePoint(let u, let v, _) = E4MessageDecoder.decode("RP,2,3,7") else {
            return XCTFail("not a routePoint")
        }
        XCTAssertNotEqual(u % 2, v % 2, "a wall midpoint has one odd coordinate")
    }

    func testMoveDefaultsWhenOmitted() {
        guard case .routePoint(let u, let v, let move) = E4MessageDecoder.decode("RP,5,7") else {
            return XCTFail("not a routePoint")
        }
        XCTAssertEqual([u, v], [5, 7])
        XCTAssertEqual(move, 0)
    }

    // --- RTE ---------------------------------------------------------------

    func testRouteEndCarriesTheSummary() {
        guard case .routeEnd(let pts, let cells, let turns, let spins) =
                E4MessageDecoder.decode("RTE,34,71,20,13") else {
            return XCTFail("not a routeEnd")
        }
        XCTAssertEqual([pts, cells, turns, spins], [34, 71, 20, 13])
    }

    func testShortRouteEndIsRejected() {
        if case .routeEnd = E4MessageDecoder.decode("RTE,34,71") {
            XCTFail("a truncated RTE should not decode")
        }
    }

    // --- a whole route -----------------------------------------------------

    func testAWholeDiagonalRouteDecodesInOrder() {
        // Start at the centre of (0,0), run north, SD45 onto the diagonal,
        // two diagonal steps, DS45 off it, then the goal. The parities tell
        // the story: centre, centre, wall, wall, centre.
        let lines = ["RT,2,9000",
                     "RP,1,1,0",     // start, cell centre
                     "RP,1,5,7",     // SD45L at the centre of (0,2)
                     "RP,2,5,0",     // now on the diagonal: a wall midpoint
                     "RP,4,7,9",     // DS45L two steps along
                     "RP,5,7,0",     // back on a centre
                     "RP,5,9,13",    // goal
                     "RTE,6,4,0,0"]
        let msgs = lines.map { E4MessageDecoder.decode($0) }

        guard case .routeBegin = msgs.first else { return XCTFail("no begin") }
        guard case .routeEnd(let pts, _, _, _) = msgs.last else { return XCTFail("no end") }

        var points: [(Int, Int)] = []
        for m in msgs { if case .routePoint(let u, let v, _) = m { points.append((u, v)) } }
        XCTAssertEqual(points.count, 6)
        XCTAssertEqual(pts, points.count, "RTE point count must match the RPs sent")

        // No point may land on a post.
        for (u, v) in points {
            XCTAssertFalse(u % 2 == 0 && v % 2 == 0, "(\(u),\(v)) is a post")
        }
    }
}

// MARK: - Menu actions
//
// E4MenuAction's raw values ARE the firmware's MENU[] indices -- a BT key runs
// an action by index, so a mismatch runs the wrong thing rather than failing.
// These pin the two added for the planner work.

final class E4MenuActionRouteTests: XCTestCase {

    func testNewActionsKeepTheirFirmwareIndices() {
        XCTAssertEqual(E4MenuAction.zigzagTest.rawValue, 30)
        XCTAssertEqual(E4MenuAction.planRoute.rawValue, 31)
    }

    func testNewActionKeys() {
        // Uniqueness across the whole table is already covered by
        // testActionKeysAreUnique; these pin the two specific keys.
        XCTAssertEqual(E4MenuAction.zigzagTest.key, "Z")
        XCTAssertEqual(E4MenuAction.planRoute.key, "P")
    }

    func testZigzagCountsAsMovingHer() {
        // It drives three cells of maze. Missing this means the app fires it
        // without the confirmation every other driving action gets.
        XCTAssertTrue(E4MenuAction.zigzagTest.movesTheMouse)
        XCTAssertFalse(E4MenuAction.planRoute.movesTheMouse)
        XCTAssertFalse(E4MenuAction.zigzagTest.waitsForButtonPress)
    }

    func testEveryActionHasATitle() {
        for a in E4MenuAction.allCases {
            XCTAssertFalse(a.title.isEmpty, "\(a) has no title")
        }
    }

    // MARK: - the optimistic bound

    func testRouteBoundDecodes() {
        guard case .routeBound(let kind, let ms, let unknown) =
                E4MessageDecoder.decode("RB,2,23199,7") else {
            return XCTFail("RB did not decode")
        }
        XCTAssertEqual(kind, .diagonal)
        XCTAssertEqual(ms, 23199)
        XCTAssertEqual(unknown, 7)
    }

    func testRouteBoundWithoutCountDefaultsToZero() {
        // An older mouse, or a truncated line. Zero unknown cells is the safe
        // reading: it claims nothing is left to explore only when the times
        // also agree, and the caller checks both.
        guard case .routeBound(_, _, let unknown) =
                E4MessageDecoder.decode("RB,0,27245") else {
            return XCTFail("short RB did not decode")
        }
        XCTAssertEqual(unknown, 0)
    }

    func testRouteBoundRejectsAnUnknownKind() {
        if case .routeBound = E4MessageDecoder.decode("RB,9,1000,0") {
            XCTFail("kind 9 should not decode as a route bound")
        }
    }

    // MARK: - simulator playback

    func testSimRateDecodesAndEncodes() {
        guard case .simRate(let r) = E4MessageDecoder.decode("SIM,rate=2.00") else {
            return XCTFail("SIM did not decode")
        }
        XCTAssertEqual(r, 2.0, accuracy: 0.001)
        XCTAssertEqual(E4Command.simRate(5).line, "SIM,5.00\n")
        XCTAssertEqual(E4Command.readSimRate.line, "SIM?\n")
    }

    // MARK: - the wall follower

    func testWallFollowActionsMatchTheFirmwareTable() {
        XCTAssertEqual(E4MenuAction.wallFollowLeft.rawValue, 20)
        XCTAssertEqual(E4MenuAction.wallFollowRight.rawValue, 32)
        XCTAssertEqual(E4MenuAction.simFollowLeft.rawValue, 33)
        XCTAssertEqual(E4MenuAction.simFollowRight.rawValue, 34)
        XCTAssertEqual(E4MenuAction.wallFollowLeft.key, "w")
        XCTAssertEqual(E4MenuAction.wallFollowRight.key, "W")
        XCTAssertEqual(E4MenuAction.simFollowLeft.key, "q")
        XCTAssertEqual(E4MenuAction.simFollowRight.key, "Q")
    }

    func testOnlyTheDrivenFollowersMoveHer() {
        // The simulated pair never arms a motor. If they fall through into
        // movesTheMouse the app puts a "this moves the mouse" dialog in front
        // of something that cannot, which is how you learn to dismiss the
        // dialog that matters.
        XCTAssertTrue(E4MenuAction.wallFollowLeft.movesTheMouse)
        XCTAssertTrue(E4MenuAction.wallFollowRight.movesTheMouse)
        XCTAssertFalse(E4MenuAction.simFollowLeft.movesTheMouse)
        XCTAssertFalse(E4MenuAction.simFollowRight.movesTheMouse)
    }

    // MARK: - the chained-turn test

    func testZigzagStartDecodes() {
        guard case .zigzagSetup(let z, let started) =
                E4MessageDecoder.decode("ZIG,start turns=3 mode=arc first=R row=SS90 v=300") else {
            return XCTFail("ZIG,start did not decode")
        }
        XCTAssertTrue(started)
        XCTAssertEqual(z.turns, 3)
        XCTAssertFalse(z.spin)
        XCTAssertTrue(z.firstRight)
        XCTAssertTrue(z.speedRunRow)
        XCTAssertEqual(z.speed, 300)
    }

    func testZigzagSetIsNotAStart() {
        guard case .zigzagSetup(let z, let started) =
                E4MessageDecoder.decode("ZIG,set turns=5 mode=spin first=L row=SS90E") else {
            return XCTFail("ZIG,set did not decode")
        }
        XCTAssertFalse(started, "a set must not clear the last run's turns")
        XCTAssertEqual(z.turns, 5)
        XCTAssertTrue(z.spin)
        XCTAssertFalse(z.firstRight)
        XCTAssertFalse(z.speedRunRow)
        XCTAssertNil(z.speed)
    }

    func testZigzagWithoutARowMeansTheSearchTurn() {
        // Firmware before 0.24 sent no row and drove SS90E. Defaulting a
        // missing field to SS90 would relabel every old log as a speed-run
        // test that never happened.
        guard case .zigzagSetup(let z, _) =
                E4MessageDecoder.decode("ZIG,set turns=3 mode=arc first=R") else {
            return XCTFail("legacy ZIG,set did not decode")
        }
        XCTAssertFalse(z.speedRunRow)
    }

    func testZigzagTurnAndDoneDecode() {
        guard case .zigzagTurn(let t) =
                E4MessageDecoder.decode("ZIG,turn 2 L gyro=-1 pos=200") else {
            return XCTFail("ZIG,turn did not decode")
        }
        XCTAssertEqual(t.index, 2)
        XCTAssertFalse(t.right)
        XCTAssertEqual(t.gyro, -1)
        XCTAssertEqual(t.position, 200)

        guard case .zigzagDone(let r) =
                E4MessageDecoder.decode("ZIG,done row=SS90 gyro=-87 expect=-90 err=3 dist=541") else {
            return XCTFail("ZIG,done did not decode")
        }
        XCTAssertTrue(r.speedRunRow)
        XCTAssertEqual(r.error, 3)
        XCTAssertEqual(r.distance, 541)
    }

    func testZigzagCommandEncodesFourFields() {
        XCTAssertEqual(E4Command.zigzagSetup(turns: 4, spin: true,
                                             firstRight: false, speedRunRow: true).line,
                       "ZIG,4,1,0,1\n")
    }

    func testUnknownCellListDecodes() {
        guard case .routeUnknownCell(let x, let y) =
                E4MessageDecoder.decode("RU,12,3") else {
            return XCTFail("RU did not decode")
        }
        XCTAssertEqual(x, 12)
        XCTAssertEqual(y, 3)

        guard case .routeUnknownEnd(let total) =
                E4MessageDecoder.decode("RUE,7") else {
            return XCTFail("RUE did not decode")
        }
        XCTAssertEqual(total, 7)
    }
}
