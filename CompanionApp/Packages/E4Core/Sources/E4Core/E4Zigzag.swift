import Foundation

/// The chained-turn bench test, as the mouse reports it.
///
/// The question it settles: a 90 that chains is a quarter circle of radius
/// 90 mm — half a cell, wall midpoint to wall midpoint — and hers displaces
/// about 111 mm once the angular ramps are integrated, so two turns in
/// consecutive cells want 223 mm and have 180. The planner refuses and emits
/// stop-and-spin; the firmware relabels its frame past the next turn point and
/// runs the arcs back to back anyway. Only the floor can say which is right.
public struct E4ZigzagSetup: Sendable, Equatable {
    /// How many alternating 90s.
    public var turns: Int
    /// false = chained arcs (the case under test), true = stop-and-spin
    /// (what the planner currently believes she must do).
    public var spin: Bool
    /// Which hand the first turn takes.
    public var firstRight: Bool
    /// Which row of the turn table it drives. SS90 (the speed-run turn) is the
    /// only one that has to chain: searching, she stops and senses every cell
    /// and wall-following re-centres her, so SS90E can be as wide as it likes.
    public var speedRunRow: Bool
    /// Forward speed, reported on `start` — the row's own, not SEARCH_SPEED.
    public var speed: Int?

    public init(turns: Int, spin: Bool, firstRight: Bool,
                speedRunRow: Bool, speed: Int? = nil) {
        self.turns = turns
        self.spin = spin
        self.firstRight = firstRight
        self.speedRunRow = speedRunRow
        self.speed = speed
    }

    public var rowName: String { speedRunRow ? "SS90" : "SS90E" }
}

/// One turn as she took it.
public struct E4ZigzagTurn: Sendable, Equatable, Identifiable {
    public var index: Int
    public var right: Bool
    public var gyro: Int
    public var position: Int
    public var id: Int { index }

    public init(index: Int, right: Bool, gyro: Int, position: Int) {
        self.index = index; self.right = right; self.gyro = gyro; self.position = position
    }
}

/// What she made of the whole run.
public struct E4ZigzagResult: Sendable, Equatable {
    public var speedRunRow: Bool
    public var gyro: Int
    /// Alternating 90s cancel in pairs, so the net is one turn's worth for an
    /// odd count and nothing for an even one.
    public var expected: Int
    public var error: Int
    public var distance: Int

    public init(speedRunRow: Bool, gyro: Int, expected: Int, error: Int, distance: Int) {
        self.speedRunRow = speedRunRow; self.gyro = gyro
        self.expected = expected; self.error = error; self.distance = distance
    }

    public var rowName: String { speedRunRow ? "SS90" : "SS90E" }
}

/// Live state of the test, rebuilt from her own reports rather than from what
/// the app last asked for — so what is shown is what she is holding, including
/// after a reconnect or a setting changed from her own menu.
public struct E4ZigzagState: Sendable, Equatable {
    public var setup = E4ZigzagSetup(turns: 3, spin: false, firstRight: true, speedRunRow: true)
    public var turns: [E4ZigzagTurn] = []
    public var result: E4ZigzagResult?
    public var running = false

    public init() {}
}
