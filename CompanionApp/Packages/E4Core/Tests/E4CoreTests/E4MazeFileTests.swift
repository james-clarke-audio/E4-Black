import XCTest
@testable import E4Core

/// Maze-file parsing and the ground-truth upload wire format.
///
/// The fixture is a REAL file from the 407-maze corpus in the repo, not one
/// drawn by hand: a fixture I write shares whatever I misunderstood about the
/// format, and a corpus file does not.
final class E4MazeFileTests: XCTestCase {

    /// mazefiles/text/100.txt, lattice rows only.
    private let maze100 = """
        o---o---o---o---o---o---o---o---o---o---o---o---o---o---o---o---o
        |                                                               |
        o   o---o---o---o---o---o---o---o---o---o---o---o---o---o   o   o
        |   |                                                   |       |
        o   o   o---o---o---o---o---o---o---o---o---o---o---o   o   o   o
        |   |   |                                           |   |       |
        o   o   o   o   o---o---o---o---o   o   o   o   o   o   o   o   o
        |   |   |       |       |       |                   |   |       |
        o   o   o---o---o   o   o   o   o---o   o   o   o   o   o   o   o
        |   |               |       |       |               |   |       |
        o   o---o---o---o   o   o   o   o   o   o   o   o   o   o   o   o
        |               |       |       |   |               |   |       |
        o   o   o   o   o---o---o---o---o   o   o   o   o   o   o   o   o
        |                               |   |               |   |       |
        o   o   o   o   o   o   o   o---o   o   o   o   o   o   o   o   o
        |                           |       |               |   |       |
        o   o   o   o   o   o   o   o   o   o   o   o   o   o   o   o   o
        |                           |       |               |   |       |
        o   o   o   o   o   o   o   o---o---o   o   o   o   o   o   o   o
        |                                                   |   |       |
        o   o   o   o   o   o   o   o   o   o   o   o   o   o   o   o   o
        |                                                   |   |       |
        o   o   o   o---o---o---o---o   o   o---o---o---o---o   o   o   o
        |           |       |       |       |       |       |   |       |
        o---o---o---o   o   o   o   o---o---o   o   o   o   o   o   o   o
        |               |       |               |       |       |       |
        o   o---o---o   o   o   o   o---o---o   o   o   o   o---o   o   o
        |   |       |       |       |       |       |       |           |
        o   o   o   o---o---o---o---o   o   o---o---o---o---o   o   o   o
        |   |                                                           |
        o   o   o   o   o   o   o   o   o   o   o   o   o   o   o   o   o
        |   |                                                           |
        o---o---o---o---o---o---o---o---o---o---o---o---o---o---o---o---o
        """

    func testParsesARealCorpusFile() {
        guard let file = E4MazeFile.parseASCII(maze100) else {
            return XCTFail("corpus maze did not parse")
        }
        XCTAssertEqual(file.format, .ascii)
        XCTAssertFalse(file.looksDegenerate)

        // Known-good rows, cross-checked against the web app's parser over the
        // whole corpus before this test was written.
        XCTAssertEqual(file.rowHex(0), "ec44444444444446")
        XCTAssertEqual(file.rowHex(15), "9555555555555513")
        XCTAssertEqual(file.rowChecksum(0), 0x5a)
        XCTAssertEqual(file.rowChecksum(15), 0x5f)
    }

    /// y = 0 is the BOTTOM row and the start cell lives there, walled on three
    /// sides with only north open. If this flips, the maze parses upside down
    /// and still looks like a perfectly good maze.
    func testStartCellIsBottomLeftAndOpensNorth() {
        guard let file = E4MazeFile.parseASCII(maze100) else { return XCTFail("parse") }
        let start = file.mask(x: 0, y: 0)
        XCTAssertEqual(start & 1, 0, "north should be open")
        XCTAssertNotEqual(start & 2, 0, "east should be walled")
        XCTAssertNotEqual(start & 4, 0, "south should be walled")
        XCTAssertNotEqual(start & 8, 0, "west should be walled")
    }

    func testPerimeterIsSealed() {
        guard let file = E4MazeFile.parseASCII(maze100) else { return XCTFail("parse") }
        for x in 0..<16 {
            XCTAssertNotEqual(file.mask(x: x, y: 0) & 4, 0, "south edge at x=\(x)")
            XCTAssertNotEqual(file.mask(x: x, y: 15) & 1, 0, "north edge at x=\(x)")
        }
        for y in 0..<16 {
            XCTAssertNotEqual(file.mask(x: 0, y: y) & 8, 0, "west edge at y=\(y)")
            XCTAssertNotEqual(file.mask(x: 15, y: y) & 2, 0, "east edge at y=\(y)")
        }
    }

    /// Flipping twice has to be the identity, or the flip is quietly losing
    /// walls — and you would only notice as a maze she cannot solve.
    func testFlipIsItsOwnInverse() {
        guard let file = E4MazeFile.parseASCII(maze100) else { return XCTFail("parse") }
        XCTAssertEqual(file.flippedVertically().flippedVertically().walls, file.walls)
    }

    func testFlipSwapsNorthAndSouthAndMovesTheRow() {
        let walls = [UInt8](repeating: 0, count: 256)
        var w = walls
        w[0] = 1                                   // (0,0) north only
        let file = E4MazeFile(walls: w, goals: [], format: .ascii)
        let flipped = file.flippedVertically()
        XCTAssertEqual(flipped.mask(x: 0, y: 0), 0)
        XCTAssertEqual(flipped.mask(x: 0, y: 15), 4, "north at y=0 becomes south at y=15")
    }

    /// The checksum is position-weighted. A plain sum would let two transposed
    /// cells cancel, which is exactly the corruption a serial link produces.
    func testChecksumCatchesATransposition() {
        var w = [UInt8](repeating: 0, count: 256)
        w[0] = 3; w[1] = 5
        let a = E4MazeFile(walls: w, goals: [], format: .ascii)
        w[0] = 5; w[1] = 3
        let b = E4MazeFile(walls: w, goals: [], format: .ascii)
        XCTAssertEqual(a.rowHex(0).count, 16)
        XCTAssertNotEqual(a.rowChecksum(0), b.rowChecksum(0))
    }

    /// No file in the 407-maze corpus marks a goal, so the fallback is the
    /// normal path here, not an edge case.
    func testGoalFallsBackToTheClassicCentre() {
        guard let file = E4MazeFile.parseASCII(maze100) else { return XCTFail("parse") }
        XCTAssertTrue(file.goals.isEmpty)
        XCTAssertEqual(file.chosenGoal, E4MazeFile.Cell(x: 7, y: 7))
    }

    func testShortFileIsRejectedNotPadded() {
        XCTAssertNil(E4MazeFile.parseASCII("o---o\n|   |\no---o"))
    }

    func testByteFormatIsIndexedByColumn() {
        var bytes = [UInt8](repeating: 0, count: 256)
        bytes[0 * 16 + 3] = 0x0A                   // x=0, y=3
        guard let file = E4MazeFile.parseBytes(Data(bytes)) else {
            return XCTFail("byte maze did not parse")
        }
        XCTAssertEqual(file.format, .bytes)
        XCTAssertEqual(file.mask(x: 0, y: 3), 0x0A)
    }

    // MARK: wire format

    func testGroundTruthCommandWireFormat() {
        XCTAssertEqual(E4Command.mazeClear.line, "GTC\n")
        XCTAssertEqual(E4Command.mazeVerify.line, "GTE\n")
        XCTAssertEqual(E4Command.mazeGoal(x: 7, y: 8).line, "GTG,7,8\n")
        XCTAssertEqual(
            E4Command.mazeRow(y: 3, hex: "0123456789abcdef", checksum: 0x5a).line,
            "GTR,3,0123456789abcdef,5a\n")
    }

    /// The checksum is two hex digits with the leading zero kept — the firmware
    /// reads a fixed pair, so "a" where "0a" belongs shifts everything after it.
    func testChecksumIsZeroPadded() {
        let line = E4Command.mazeRow(y: 0, hex: String(repeating: "0", count: 16),
                                     checksum: 0x09).line
        XCTAssertTrue(line.hasSuffix(",09\n"), line)
    }

    func testUploadResultReadsCleanOnlyWhenEverythingMatches() {
        let goal = E4MazeFile.Cell(x: 7, y: 7)
        let clean = E4MazeUploadResult(mismatchedRows: [], goalSent: goal, goalReadback: goal)
        XCTAssertTrue(clean.isClean)

        let badRow = E4MazeUploadResult(mismatchedRows: [4], goalSent: goal, goalReadback: goal)
        XCTAssertFalse(badRow.isClean)
        XCTAssertTrue(badRow.summary.contains("row 4"))

        let badGoal = E4MazeUploadResult(mismatchedRows: [], goalSent: goal,
                                         goalReadback: E4MazeFile.Cell(x: 8, y: 7))
        XCTAssertFalse(badGoal.isClean)
        XCTAssertTrue(badGoal.summary.contains("8,7"))
    }
}
