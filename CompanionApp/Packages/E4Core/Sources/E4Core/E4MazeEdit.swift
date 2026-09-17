import Foundation

/// Editing a maze file, and writing one back out.
///
/// The point is to be able to MAKE a maze rather than hunt through the corpus
/// for one that happens to exercise what you are testing: a wall-follower
/// course, a maze where the shortest route and the quickest are genuinely
/// different, one with a long diagonal staircase in it.
///
/// Every edit here keeps BOTH SIDES of a wall consistent. A wall is one object
/// with two cells' opinions of it, and a file where cell (3,4) says it has a
/// north wall while (3,5) says it has no south wall is not a maze — it is a
/// maze that behaves differently depending on which way she drives through it,
/// which is the worst kind of bug to chase on a floor.
extension E4MazeFile {

    public enum Side: Int, Sendable, CaseIterable {
        case north = 0, east = 1, south = 2, west = 3
        var bit: UInt8 { UInt8(1 << rawValue) }
        /// The cell on the other side, and the side IT calls this same wall.
        var opposite: Side { Side(rawValue: (rawValue + 2) % 4)! }
        var dx: Int { self == .east ? 1 : (self == .west ? -1 : 0) }
        var dy: Int { self == .north ? 1 : (self == .south ? -1 : 0) }
    }

    public func hasWall(x: Int, y: Int, side: Side) -> Bool {
        mask(x: x, y: y) & side.bit != 0
    }

    /// The outside of the arena. It is not editable: a maze with a hole in its
    /// perimeter is not a maze, and she is born knowing the border is there --
    /// letting it be drawn away would make a file that contradicts the firmware
    /// before she has moved.
    public static func isPerimeter(x: Int, y: Int, side: Side) -> Bool {
        switch side {
        case .north: return y == size - 1
        case .south: return y == 0
        case .east:  return x == size - 1
        case .west:  return x == 0
        }
    }

    public func settingWall(x: Int, y: Int, side: Side, present: Bool) -> E4MazeFile {
        guard x >= 0, x < Self.size, y >= 0, y < Self.size else { return self }
        if Self.isPerimeter(x: x, y: y, side: side) && !present { return self }

        var w = walls
        func apply(_ cx: Int, _ cy: Int, _ s: Side) {
            guard cx >= 0, cx < Self.size, cy >= 0, cy < Self.size else { return }
            let i = cy * Self.size + cx
            if present { w[i] |= s.bit } else { w[i] &= ~s.bit }
        }
        apply(x, y, side)
        apply(x + side.dx, y + side.dy, side.opposite)   // the neighbour's view of it
        return E4MazeFile(walls: w, goals: goals, format: format)
    }

    public func togglingWall(x: Int, y: Int, side: Side) -> E4MazeFile {
        settingWall(x: x, y: y, side: side, present: !hasWall(x: x, y: y, side: side))
    }

    public func settingGoal(_ cells: [Cell]) -> E4MazeFile {
        E4MazeFile(walls: walls, goals: cells, format: format)
    }

    /// The start cell as the firmware builds it: open to the north, walled east.
    /// Applied on save so a hand-drawn maze cannot begin with her facing a wall.
    public func withStandardStartCell() -> E4MazeFile {
        settingWall(x: 0, y: 0, side: .east, present: true)
            .settingWall(x: 0, y: 0, side: .north, present: false)
    }

    /// Cells with no wall on any side AND no neighbour claiming one. Not an
    /// error -- an open arena is a legal maze -- but while drawing it usually
    /// means a wall was put down and its neighbour never agreed, so it is worth
    /// being able to see them.
    public var isolatedCells: [Cell] {
        var out: [Cell] = []
        for y in 0..<Self.size {
            for x in 0..<Self.size where mask(x: x, y: y) == 0 {
                out.append(Cell(x: x, y: y))
            }
        }
        return out
    }

    // MARK: - Writing

    /// 256 bytes, `x * 16 + y`, bits N1 E2 S4 W8 -- the exact inverse of
    /// parseBytes, and what the firmware's own .maz reader expects.
    public var byteFileData: Data {
        var out = [UInt8](repeating: 0, count: Self.size * Self.size)
        for x in 0..<Self.size {
            for y in 0..<Self.size {
                out[x * Self.size + y] = mask(x: x, y: y) & 0x0F
            }
        }
        return Data(out)
    }

    /// The classic lattice, written the way the corpus writes it: north at the
    /// TOP of the file, so file rows run opposite to y. Round-trips through
    /// parseASCII, which is the only test of it that means anything.
    public var asciiFileText: String {
        var lines: [String] = []
        for row in 0...Self.size {                 // posts row above each cell row
            let y = Self.size - 1 - row            // the cell BELOW this post row
            var post = ""
            for x in 0..<Self.size {
                post += "o"
                // The wall between this post row and the cells under it is the
                // north side of cell (x, y) -- and for the very last row there
                // is no cell under it, so ask the one above instead.
                let hasIt = (row == Self.size)
                    ? hasWall(x: x, y: 0, side: .south)
                    : hasWall(x: x, y: y, side: .north)
                post += hasIt ? "---" : "   "
            }
            post += "o"
            lines.append(post)

            if row == Self.size { break }
            var body = ""
            for x in 0..<Self.size {
                body += hasWall(x: x, y: y, side: .west) ? "|" : " "
                if goals.contains(Cell(x: x, y: y))      { body += " G " }
                else if x == 0 && y == 0                 { body += " S " }
                else                                     { body += "   " }
            }
            body += hasWall(x: Self.size - 1, y: y, side: .east) ? "|" : " "
            lines.append(body)
        }
        return lines.joined(separator: "\n") + "\n"
    }
}
