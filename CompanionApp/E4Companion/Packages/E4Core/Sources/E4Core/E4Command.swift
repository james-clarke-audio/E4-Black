import Foundation

/// A command the app can send to the mouse.
///
/// Two important constraints from the firmware's input loop, both easy to trip:
///
/// 1. A menu key is only recognised when the received line is **exactly one
///    character**. `bt_len == 1` is checked literally, so `r` works and `r `
///    does not. Never batch keys into one line.
/// 2. Every command is terminated by CR or LF, and the inbound buffer is 80
///    bytes. An overlong line is dropped silently, with no error back.
public enum E4Command: Sendable, Equatable {

    // MARK: Menu navigation (single keys)

    case next
    case previous
    case enter
    case back

    /// Jump straight to a menu action by its shortcut key, bypassing the
    /// three-level menu entirely.
    case action(E4MenuAction)

    /// Any other single character, for firmware keys this enum predates.
    case key(Character)

    // MARK: Structured commands

    /// `SIZE,<w>,<h>` — set arena bounds.
    case setSize(width: Int, height: Int)

    /// `GOAL,<x>,<y>` — set the goal cell.
    case setGoal(x: Int, y: Int)

    /// `SPIN,<angle>,<omega>,<alpha>` — only inside the turn-tuning action.
    case spin(angle: Double, omega: Double, alpha: Double)

    /// `ARC,<v>,<angle>,<omega>,<alpha>,<leadIn>,<leadOut>` — turn tuning only.
    case arc(velocity: Double, angle: Double, omega: Double,
             alpha: Double, leadIn: Double, leadOut: Double)

    /// `ERR,<degrees>` — the physical shortfall you measured, during gyro cal.
    case gyroError(degrees: Double)

    /// `GS,<value>` — set gyro scale directly, during gyro cal.
    case gyroScale(Double)

    /// The exact bytes to put on the wire, terminator included.
    public var line: String {
        switch self {
        case .next:                 return "n\n"
        case .previous:             return "p\n"
        case .enter:                return "r\n"
        case .back:                 return "<\n"
        case .action(let a):        return "\(a.key)\n"
        case .key(let c):           return "\(c)\n"

        case .setSize(let w, let h):
            return "SIZE,\(w),\(h)\n"
        case .setGoal(let x, let y):
            return "GOAL,\(x),\(y)\n"
        case .spin(let angle, let omega, let alpha):
            return "SPIN,\(n(angle)),\(n(omega)),\(n(alpha))\n"
        case .arc(let v, let angle, let omega, let alpha, let leadIn, let leadOut):
            return "ARC,\(n(v)),\(n(angle)),\(n(omega)),\(n(alpha)),\(n(leadIn)),\(n(leadOut))\n"
        case .gyroError(let deg):
            return "ERR,\(n(deg))\n"
        case .gyroScale(let value):
            return "GS,\(String(format: "%.3f", value))\n"
        }
    }

    /// Format a number the way the firmware's `atoi`/`atof` parsers expect —
    /// no thousands separators, no locale decimal comma, no exponent.
    private func n(_ value: Double) -> String {
        if value == value.rounded() && abs(value) < 1e9 {
            return String(Int(value))
        }
        return String(format: "%.3f", value)
    }
}

/// The firmware's menu actions and their Bluetooth shortcut keys.
///
/// Mirrors `MENU[]` in `app_main.cpp`. The raw value is the index, which is
/// what `RUN` and `DONE` report back.
public enum E4MenuAction: Int, Sendable, CaseIterable, Identifiable {
    case forward180 = 0
    case right90 = 1
    case left90 = 2
    case spin180 = 3
    case motionTest = 4
    case search = 5
    case simulate = 6
    case explore = 7
    case simExplore = 8
    case recallMaze = 9
    case irMonitor = 10
    case sensorMode = 11
    case recalGyro = 12
    case resetPose = 13
    case testMode = 14
    case eepromTest = 15
    case setBT57k = 16
    case setMazeSize = 17
    case setGoal = 18
    case turnTuning = 19
    case wallFollower = 20
    case speedRun = 21
    case resumeSaved = 22
    case runOptions = 23
    case emitterHold = 24
    case firmwareVersion = 25
    case irSampler = 26
    case gyroScaleCal = 27

    public var id: Int { rawValue }

    public var key: Character {
        switch self {
        case .forward180:      return "f"
        case .right90:         return "d"
        case .left90:          return "a"
        case .spin180:         return "s"
        case .motionTest:      return "o"
        case .search:          return "h"
        case .simulate:        return "u"
        case .explore:         return "e"
        case .simExplore:      return "x"
        case .recallMaze:      return "m"
        case .irMonitor:       return "i"
        case .sensorMode:      return "v"
        case .recalGyro:       return "g"
        case .resetPose:       return "z"
        case .testMode:        return "k"
        case .eepromTest:      return "t"
        case .setBT57k:        return "b"
        case .setMazeSize:     return "c"
        case .setGoal:         return "y"
        case .turnTuning:      return "j"
        case .wallFollower:    return "w"
        case .speedRun:        return "l"
        case .resumeSaved:     return "R"
        case .runOptions:      return "O"
        case .emitterHold:     return "E"
        case .firmwareVersion: return "V"
        case .irSampler:       return "S"
        case .gyroScaleCal:    return "G"
        }
    }

    public var title: String {
        switch self {
        case .forward180:      return "Forward 180"
        case .right90:         return "Right 90"
        case .left90:          return "Left 90"
        case .spin180:         return "Spin 180"
        case .motionTest:      return "Motion test"
        case .search:          return "Search"
        case .simulate:        return "Simulate"
        case .explore:         return "Explore"
        case .simExplore:      return "Sim explore"
        case .recallMaze:      return "Recall maze"
        case .irMonitor:       return "IR monitor"
        case .sensorMode:      return "Sensor mode"
        case .recalGyro:       return "Recal gyro"
        case .resetPose:       return "Reset pose"
        case .testMode:        return "Test mode"
        case .eepromTest:      return "EEPROM test"
        case .setBT57k:        return "BT to 57600"
        case .setMazeSize:     return "Set maze size"
        case .setGoal:         return "Set goal"
        case .turnTuning:      return "Turn tuning"
        case .wallFollower:    return "Wall follower"
        case .speedRun:        return "Speed run"
        case .resumeSaved:     return "Resume saved"
        case .runOptions:      return "Run options"
        case .emitterHold:     return "Emitter hold"
        case .firmwareVersion: return "Firmware version"
        case .irSampler:       return "IR sampler"
        case .gyroScaleCal:    return "Gyro scale cal"
        }
    }

    /// True for actions that drive the wheels — worth confirming before firing
    /// one by accident from a phone in your pocket.
    public var movesTheMouse: Bool {
        switch self {
        case .forward180, .right90, .left90, .spin180, .motionTest,
             .search, .explore, .recalGyro, .turnTuning, .wallFollower,
             .speedRun, .resumeSaved, .gyroScaleCal:
            return true
        default:
            return false
        }
    }
}
