import Foundation

/// One decoded line from the mouse.
///
/// The decode is **total**: every line becomes a case, and anything the parser
/// does not recognise lands in `.text`. Nothing is ever dropped, because the
/// raw log is a first-class diagnostic — several bench sessions have been
/// rescued by reading lines the app didn't understand.
/// Which question the planner was asked. The firmware sends all three for the
/// same map, so they can be laid over one another and compared.
public enum E4RouteKind: Int, Sendable, Equatable, CaseIterable {
    case shortest = 0     // fewest cells
    case quickest = 1     // least time, orthogonal only
    case diagonal = 2     // least time, diagonals allowed

    public var title: String {
        switch self {
        case .shortest: return "shortest"
        case .quickest: return "quickest"
        case .diagonal: return "diagonal"
        }
    }
}

public enum E4Message: Sendable, Equatable {

    // MARK: Position and maze

    /// `POS,<x>,<y>,<deg>` — dead-reckoned pose. Millimetres, degrees.
    case pose(x: Double, y: Double, degrees: Double)

    /// `M,<x>,<y>,<heading>` — the logical cell she believes she is in.
    case cell(x: Int, y: Int, heading: E4Heading)

    /// `W,<x>,<y>,<mask>` — walls discovered in a cell.
    case walls(x: Int, y: Int, mask: E4WallMask)

    /// `C,<x>,<y>,<cost>` — flood-fill cost for a cell.
    case cost(x: Int, y: Int, cost: Int)

    /// `SIZE,<w>,<h>` — arena bounds.
    case size(width: Int, height: Int)

    /// `GOAL,<x>,<y>[,<x>,<y>...]` — goal cells, as pairs.
    case goal([E4Cell])

    /// `SP,<x>,<y>` — one cell of the solved path, streamed in order.
    case solutionCell(x: Int, y: Int)

    /// `SOLVED,<ms>,<steps>`
    case solved(milliseconds: Int, steps: Int)

    // --- planned routes ---------------------------------------------------
    // Points arrive in HALF-CELLS, which is the unit the planner's lattice
    // works in: cell centres land on odd coordinates, wall midpoints on mixed.
    // That is deliberate, and it is what lets a diagonal be just another line
    // segment here -- this end never has to know about the lattice, the parity
    // rule, or which wall a point sits on.
    case routeBegin(kind: E4RouteKind, milliseconds: Int)
    case routePoint(u: Int, v: Int, move: Int)
    case routeEnd(points: Int, cells: Int, turns: Int, spins: Int)

    /// The optimistic twin of a plan: the same search with every UNSEEN wall
    /// assumed open. That is a true lower bound -- the real maze has at least
    /// as many walls as the optimistic view of it -- so the gap to the proven
    /// route is the most that is still out there to find. `unknownCells` is
    /// how many cells on that best-possible line she has not fully seen.
    case routeBound(kind: E4RouteKind, milliseconds: Int, unknownCells: Int)

    /// One cell the best-possible route wants and she has not fully seen.
    /// These, and only these, are worth driving to: an unknown cell no
    /// optimistic route passes through cannot change the answer, however
    /// blank it looks.
    case routeUnknownCell(x: Int, y: Int)

    /// End of the unknown-cell list, with the total.
    case routeUnknownEnd(total: Int)

    // MARK: - the chained-turn bench test

    /// Her zigzag settings, from `ZIG,set` (changed) or `ZIG,start` (about to
    /// run — `started` true, and the setup then carries the speed).
    case zigzagSetup(E4ZigzagSetup, started: Bool)

    /// One turn of the run, as she took it.
    case zigzagTurn(E4ZigzagTurn)

    /// The end of the run, with the heading error and the distance covered.
    case zigzagDone(E4ZigzagResult)

    /// Simulator playback rate. Affects watching only — a simulated run's
    /// reported time comes from the motion model, not from how fast it was
    /// played back.
    case simRate(Double)

    /// `RST` — clear the maze model.
    case resetMaze

    // MARK: Telemetry

    /// `TEL,<t>,<v>,<omega>,<dist>,<ang>,<gyro>,<batt>`
    case telemetry(E4Telemetry)

    /// `IR,<sl>,<fl>,<fr>,<sr>` — raw ambient-subtracted wall readings.
    case ir(left: Int, frontLeft: Int, frontRight: Int, right: Int)

    /// `SENS,...` — the human-readable sensor line, which also carries the
    /// live thresholds and whether she is on real IR or virtual truth.
    case sensorStatus(E4SensorStatus)

    /// `EMIT,<index>,<name>` — which emitter the hold routine has lit.
    case emitter(index: Int, name: String)

    /// `STATE,<text>` — e.g. `STATE,LOWBATT 7.40V`
    case state(String)

    // MARK: Identity and configuration

    /// `VER,<version>,<build>`
    case version(firmware: String, build: String)

    /// `CFG,...` — EEPROM config block load result.
    case config(E4ConfigReport)

    /// `GCAL,...` — gyro-scale calibration progress and results.
    case gyroCal(E4GyroCalReport)

    /// `TURNRES,<kind>,cmd=..,gyro=..,dist=..`
    case turnResult(E4TurnResult)

    // MARK: Menu and actions

    /// `RUN,<index>,<name>` — a menu action started.
    case runStarted(index: Int, name: String)

    /// `DONE,<index> gyro_a=.. d=..` — a menu action finished.
    case runFinished(index: Int, detail: String)

    /// `ACT,<action>,<note>,<x>,<y>,<heading>` — solver action log.
    case action(E4Action)

    /// `ACT,<free text>` — the non-structured ACT variants (`todo`, `size`,
    /// `goal`, `lowbatt`).
    case actionNote(String)

    // MARK: Maze-truth upload channel

    /// `GTOK` / `GTERR` / `GTDONE` / `GTGOK,x,y` / `GTQ,x,y` / `GTV,y,<hex>`
    case mazeTruth(E4MazeTruth)

    /// Anything the threshold-calibration routine says: which capture is being
    /// taken, what it read, the proposed thresholds and the margins behind them.
    case threshold(E4ThresholdReport)

    /// One row of the turn table, from a `CFG?` dump.
    case turn(E4Turn)

    // MARK: Fallback

    /// Anything else — EEPROM scan output, prompts, free-form firmware chatter.
    case text(String)
}

/// A maze cell coordinate.
public struct E4Cell: Sendable, Equatable, Hashable {
    public let x: Int
    public let y: Int
    public init(x: Int, y: Int) { self.x = x; self.y = y }
}

/// The periodic telemetry frame.
public struct E4Telemetry: Sendable, Equatable {
    public let timestamp: Int      // ms since boot
    public let velocity: Double    // mm/s
    public let omega: Double       // deg/s
    public let distance: Double    // mm
    public let angle: Double       // deg
    public let gyro: Double        // deg/s
    public let battery: Double     // volts
}

/// Decoded form of the `SENS` line.
public struct E4SensorStatus: Sendable, Equatable {
    public let left: Int
    public let frontLeft: Int
    public let frontRight: Int
    public let right: Int
    public let frontSum: Int
    public let leftThreshold: Int
    public let rightThreshold: Int
    public let frontThreshold: Int
    public let usingRealIR: Bool
    public let raw: String

    /// The threshold the firmware actually tests this sensor against.
    ///
    /// `nil` for the front pair on purpose: the front decision is made on the
    /// FL+FR **sum**, never on either detector alone, so there is no per-sensor
    /// figure to show. Returning `frontThreshold` here would invite a caller to
    /// compare it against one detector and draw a conclusion the firmware never
    /// makes.
    public func threshold(for sensor: E4Sensor) -> Int? {
        switch sensor {
        case .left:  return leftThreshold
        case .right: return rightThreshold
        case .frontLeft, .frontRight: return nil
        }
    }

    /// Firmware before v0.11 reported one shared side threshold. A log from
    /// then decodes with left == right, which is exactly what it meant.
    public var sidesShareOneThreshold: Bool { leftThreshold == rightThreshold }
}

/// Decoded form of the `CFG` line.
public struct E4ConfigReport: Sendable, Equatable {
    public enum Outcome: Sendable, Equatable {
        case loaded(version: Int)
        case checksumFailure(version: Int)
        case valueRejected
        /// `CFG,dump v5 present=1 loaded=1 turns=16` — the turn rows follow.
        case dumpBegin(version: Int, present: Bool, loaded: Bool, turns: Int)
        /// `CFG,dump end` — everything has arrived.
        case dumpEnd
        case other
    }
    public let outcome: Outcome
    public let gyroScale: Double?
    public let raw: String
}

/// Decoded form of the `GCAL` line. The firmware reports this as loose
/// `key=value` text rather than fixed fields, so the parser scrapes pairs.
public struct E4GyroCalReport: Sendable, Equatable {
    public let oldScale: Double?
    public let newScale: Double?
    public let setScale: Double?
    public let shortfall: Double?
    public let commandedDegrees: Double?
    public let gyroDegrees: Double?
    public let saved: Bool?
    public let rejected: Bool
    public let raw: String

    /// The value the app should now show as current, whichever field carried it.
    public var currentScale: Double? { newScale ?? setScale }
}

/// Decoded form of the `TURNRES` line.
public struct E4TurnResult: Sendable, Equatable {
    public enum Kind: String, Sendable { case spin, arc, unknown }
    public let kind: Kind
    public let commanded: Double?
    public let gyro: Double?
    public let distance: Double?
}

/// Decoded form of the structured `ACT` line.
public struct E4Action: Sendable, Equatable {
    public let action: Character   // F/L/R/B/#/-/*
    public let note: Character     // 's' sensor, 'd' distance, '-' none
    public let x: Int
    public let y: Int
    public let heading: E4Heading
}

/// Responses on the maze-truth upload channel.
public enum E4MazeTruth: Sendable, Equatable {
    case ok
    case error
    case done
    case goalAccepted(x: Int, y: Int)
    case goalReadback(x: Int, y: Int)
    case row(y: Int, masks: [E4WallMask])
}
