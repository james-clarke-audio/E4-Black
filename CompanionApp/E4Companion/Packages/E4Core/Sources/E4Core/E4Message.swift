import Foundation

/// One decoded line from the mouse.
///
/// The decode is **total**: every line becomes a case, and anything the parser
/// does not recognise lands in `.text`. Nothing is ever dropped, because the
/// raw log is a first-class diagnostic — several bench sessions have been
/// rescued by reading lines the app didn't understand.
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
    public let sideThreshold: Int
    public let frontThreshold: Int
    public let usingRealIR: Bool
    public let raw: String
}

/// Decoded form of the `CFG` line.
public struct E4ConfigReport: Sendable, Equatable {
    public enum Outcome: Sendable, Equatable {
        case loaded(version: Int)
        case checksumFailure(version: Int)
        case valueRejected
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
