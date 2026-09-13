import Foundation

/// A maze read from a file, as wall masks — not as text.
///
/// Two formats are understood. The classic ASCII lattice (`o---o` posts, `|`
/// and `-` walls) is what the 407-file corpus in the repo uses; a 256-byte
/// `.maz` is the other common form. Which one a file is gets decided by looking
/// at it, not by its extension, because the extensions in the wild are not
/// reliable and a misnamed file is not the user's mistake to debug.
public struct E4MazeFile: Sendable, Equatable {

    /// Wall masks indexed `y * 16 + x`, bits N1 E2 S4 W8 — the firmware's own
    /// layout, so nothing has to be reinterpreted on the way out.
    public let walls: [UInt8]

    /// Cells the file marked `G`. Empty for formats that cannot express a goal.
    public let goals: [Cell]

    /// How the file was read. Worth surfacing: "parsed as bytes" explains an
    /// odd-looking maze far faster than the maze itself does.
    public let format: Format

    public struct Cell: Sendable, Equatable, Hashable {
        public let x: Int
        public let y: Int
        public init(x: Int, y: Int) { self.x = x; self.y = y }
    }

    public enum Format: String, Sendable {
        case ascii
        case bytes
    }

    public static let size = 16

    public init(walls: [UInt8], goals: [Cell], format: Format) {
        self.walls = walls
        self.goals = goals
        self.format = format
    }

    // MARK: - Reading

    /// Decide text vs binary by content. A file that is >85% printable is text;
    /// anything else is tried as bytes. Same test the web app has used against
    /// this corpus since the start, kept deliberately identical so a file that
    /// loads in one loads in the other.
    public static func load(_ data: Data) -> E4MazeFile? {
        let printable = data.reduce(into: 0) { count, b in
            if b == 9 || b == 10 || b == 13 || (b >= 32 && b < 127) { count += 1 }
        }
        let looksTextual = !data.isEmpty && Double(printable) / Double(data.count) > 0.85
        if looksTextual, let text = String(data: data, encoding: .utf8),
           let parsed = parseASCII(text) {
            return parsed
        }
        return parseBytes(data)
    }

    /// The classic lattice. Posts sit on a 4-column / 2-row grid and the TOP of
    /// the file is north, so file row and maze y run opposite ways — hence the
    /// `15 - y`. Getting that backwards produces a maze that looks plausible and
    /// is upside down, which is why `flippedVertically()` exists as an escape
    /// hatch rather than a guess.
    public static func parseASCII(_ text: String) -> E4MazeFile? {
        let lines = text
            .replacingOccurrences(of: "\r", with: "")
            .split(separator: "\n", omittingEmptySubsequences: false)
            .map(Array.init)
            .filter { row in row.contains { "o+*-|".contains($0) } }

        // 16 cells need 33 lattice rows. Fewer means this is not the format,
        // not that the maze is small — a short file is rejected rather than
        // padded, because a half-read maze she would happily drive into is worse
        // than no maze at all.
        guard lines.count >= 33 else { return nil }

        func ch(_ r: Int, _ c: Int) -> Character {
            guard r >= 0, r < lines.count, c >= 0, c < lines[r].count else { return " " }
            return lines[r][c]
        }

        var walls = [UInt8](repeating: 0, count: size * size)
        var goals: [Cell] = []

        for y in 0..<size {
            let bodyRow = 2 * (size - 1 - y) + 1
            for x in 0..<size {
                let centre = 4 * x + 2
                var mask: UInt8 = 0
                if ch(bodyRow - 1, centre) == "-" { mask |= 1 }   // north
                if ch(bodyRow, 4 * x + 4) == "|" { mask |= 2 }    // east
                if ch(bodyRow + 1, centre) == "-" { mask |= 4 }   // south
                if ch(bodyRow, 4 * x) == "|" { mask |= 8 }        // west
                walls[y * size + x] = mask
                if ch(bodyRow, centre) == "G" { goals.append(Cell(x: x, y: y)) }
            }
        }
        return E4MazeFile(walls: walls, goals: goals, format: .ascii)
    }

    /// 256 bytes, one per cell, bits N1 E2 S4 W8, indexed `x * 16 + y`.
    public static func parseBytes(_ data: Data) -> E4MazeFile? {
        guard data.count >= size * size else { return nil }
        let b = [UInt8](data)
        var walls = [UInt8](repeating: 0, count: size * size)
        for x in 0..<size {
            for y in 0..<size {
                walls[y * size + x] = b[x * size + y] & 0x0F
            }
        }
        return E4MazeFile(walls: walls, goals: [], format: .bytes)
    }

    // MARK: - Orientation

    /// Same maze, turned upside down — row y becomes 15 - y and north/south
    /// swap with it. Some files in circulation put y = 0 at the top; nothing in
    /// the file says which convention it used, so this stays a switch the
    /// person flips after looking at the drawing, not something inferred.
    public func flippedVertically() -> E4MazeFile {
        var out = [UInt8](repeating: 0, count: Self.size * Self.size)
        for y in 0..<Self.size {
            for x in 0..<Self.size {
                let m = walls[y * Self.size + x]
                var f = m & 0x0A                      // east and west are unmoved
                if m & 1 != 0 { f |= 4 }              // north becomes south
                if m & 4 != 0 { f |= 1 }              // south becomes north
                out[(Self.size - 1 - y) * Self.size + x] = f
            }
        }
        return E4MazeFile(walls: out,
                          goals: goals.map { Cell(x: $0.x, y: Self.size - 1 - $0.y) },
                          format: format)
    }

    // MARK: - Wire form

    public func mask(x: Int, y: Int) -> UInt8 {
        guard x >= 0, x < Self.size, y >= 0, y < Self.size else { return 0 }
        return walls[y * Self.size + x]
    }

    /// One row as the 16 hex digits `GTR` expects.
    public func rowHex(_ y: Int) -> String {
        let digits = "0123456789abcdef"
        var out = ""
        for x in 0..<Self.size {
            let m = Int(mask(x: x, y: y)) & 0x0F
            out.append(digits[digits.index(digits.startIndex, offsetBy: m)])
        }
        return out
    }

    /// The firmware's `gt_rowsum`: position-weighted, so two transposed cells
    /// do not cancel out the way a plain sum would let them.
    public func rowChecksum(_ y: Int) -> UInt8 {
        var c = y & 0xFF
        for x in 0..<Self.size {
            c = (c + (x + 1) * Int(mask(x: x, y: y))) & 0xFF
        }
        return UInt8(c)
    }

    /// The single goal cell to send. She takes one cell, not a room; of a 2x2
    /// centre the corner nearest the start is the one she will reach first.
    /// Falls back to the classic centre when the file named no goal.
    public var chosenGoal: Cell {
        let candidates = goals.isEmpty
            ? [Cell(x: 7, y: 7), Cell(x: 8, y: 7), Cell(x: 7, y: 8), Cell(x: 8, y: 8)]
            : goals
        return candidates.min { ($0.x + $0.y) < ($1.x + $1.y) } ?? Cell(x: 7, y: 7)
    }

    /// True when every cell is walled on all four sides, or none is. Both mean
    /// the parse latched onto the wrong thing — a uniform maze is never a real
    /// one, and it is the failure an upside-down or misaligned parse produces.
    public var looksDegenerate: Bool {
        let first = walls.first ?? 0
        return walls.allSatisfy { $0 == first }
    }
}
