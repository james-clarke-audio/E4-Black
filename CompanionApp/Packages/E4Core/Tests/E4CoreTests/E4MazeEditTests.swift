import XCTest
@testable import E4Core

final class E4MazeEditTests: XCTestCase {

    /// An empty arena with only its perimeter, which is where drawing starts.
    private func blank() -> E4MazeFile {
        var w = [UInt8](repeating: 0, count: 256)
        for i in 0..<256 {
            let x = i % 16, y = i / 16
            var m: UInt8 = 0
            if y == 15 { m |= 1 }
            if x == 15 { m |= 2 }
            if y == 0  { m |= 4 }
            if x == 0  { m |= 8 }
            w[i] = m
        }
        return E4MazeFile(walls: w, goals: [], format: .bytes)
    }

    func testAWallIsOneObjectWithTwoOpinions() {
        // The failure this guards against is a maze that behaves differently
        // depending on which way she drives through it — findable only on a
        // floor, and miserable there.
        let m = blank().settingWall(x: 3, y: 4, side: .north, present: true)
        XCTAssertTrue(m.hasWall(x: 3, y: 4, side: .north))
        XCTAssertTrue(m.hasWall(x: 3, y: 5, side: .south), "the neighbour must agree")

        let back = m.settingWall(x: 3, y: 5, side: .south, present: false)
        XCTAssertFalse(back.hasWall(x: 3, y: 4, side: .north), "and agree when it goes too")
    }

    func testTogglingIsItsOwnInverse() {
        let start = blank()
        let there = start.togglingWall(x: 6, y: 6, side: .east)
        XCTAssertTrue(there.hasWall(x: 6, y: 6, side: .east))
        XCTAssertEqual(there.togglingWall(x: 6, y: 6, side: .east).walls, start.walls)
    }

    func testThePerimeterCannotBeDrawnAway() {
        // She is born knowing the border is there. A file that disagrees would
        // contradict the firmware before she has moved a cell.
        let m = blank()
        XCTAssertEqual(m.settingWall(x: 5, y: 0, side: .south, present: false).walls, m.walls)
        XCTAssertEqual(m.togglingWall(x: 0, y: 9, side: .west).walls, m.walls)
        XCTAssertEqual(m.togglingWall(x: 15, y: 2, side: .east).walls, m.walls)
        XCTAssertEqual(m.togglingWall(x: 4, y: 15, side: .north).walls, m.walls)
    }

    func testByteFileRoundTrips() {
        let m = blank()
            .settingWall(x: 2, y: 3, side: .north, present: true)
            .settingWall(x: 9, y: 11, side: .west, present: true)
        guard let back = E4MazeFile.parseBytes(m.byteFileData) else {
            return XCTFail("our own bytes did not parse")
        }
        XCTAssertEqual(back.walls, m.walls)
        XCTAssertEqual(m.byteFileData.count, 256)
    }

    func testAsciiFileRoundTrips() {
        // Verified against all 406 files in mazefiles/binary before this test
        // was written; this pins the same property in the Swift build.
        let m = blank()
            .settingWall(x: 4, y: 4, side: .east, present: true)
            .settingWall(x: 4, y: 5, side: .south, present: true)
            .settingWall(x: 0, y: 7, side: .north, present: true)
            .settingGoal([.init(x: 7, y: 7), .init(x: 8, y: 8)])
        guard let back = E4MazeFile.parseASCII(m.asciiFileText) else {
            return XCTFail("our own lattice did not parse")
        }
        XCTAssertEqual(back.walls, m.walls)
        XCTAssertEqual(Set(back.goals), Set(m.goals))
    }

    func testStandardStartCell() {
        let m = blank()
            .settingWall(x: 0, y: 0, side: .north, present: true)
            .withStandardStartCell()
        XCTAssertTrue(m.hasWall(x: 0, y: 0, side: .east), "she leaves north, so east is walled")
        XCTAssertFalse(m.hasWall(x: 0, y: 0, side: .north))
        XCTAssertFalse(m.hasWall(x: 0, y: 1, side: .south), "and cell (0,1) must agree")
    }

    func testAnEditedFileIsStillSendable() {
        // rowHex and rowChecksum are what the GT upload actually transmits, so
        // an edited maze has to survive them unchanged.
        let m = blank().settingWall(x: 1, y: 1, side: .north, present: true)
        XCTAssertEqual(m.rowHex(1).count, 16)
        XCTAssertEqual(m.rowChecksum(1), m.rowChecksum(1))
        XCTAssertFalse(m.looksDegenerate)
    }
}
