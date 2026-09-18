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

    /// Set all three wall thresholds. Sent at top level the firmware applies
    /// AND saves them in one step, reporting `saved=`; sent inside "Threshold
    /// cal" it only applies them and waits for an explicit save, so the margins
    /// can be read first.
    case thresholds(left: Int, right: Int, front: Int)

    /// Ask for the live thresholds without changing anything.
    case readThresholds

    /// Ask who she is. A QUERY — unlike the Firmware ver menu action, which
    /// holds her display and waits for a button press.
    case readVersion

    /// Ask her to report the whole live configuration, turn table included.
    case readConfig

    /// Pick which turn the tuner's ARC edits.
    case selectTurn(Int)

    /// Cells of approach before the turn's own cell, for a tuning run. She
    /// derives the lead-in from it and the selected row's entry offset, so it
    /// follows that offset instead of being re-typed after every change.
    case tuneApproach(cells: Int)

    /// Write the live turn table to the EEPROM. Everything the tuner changes is
    /// RAM until this lands.
    case saveTuning

    /// `KIND,<0|1|2>` — which planned route a speed run executes: shortest,
    /// quickest orthogonal, or quickest with diagonals. A setting rather than
    /// three actions, because the three are the same run with a different cost
    /// function and the point is comparing them on one maze.
    case runKind(Int)

    /// Ask which route kind a run would take.
    case readRunKind

    /// `SIM,<rate>` — how fast to WATCH a simulated run. The simulator animates
    /// at the speed the motion model says she would really move, so a full
    /// explore is forty-odd seconds; this shortens the sitting and never the
    /// reported time, which comes from the model rather than the clock.
    case simRate(Double)

    /// Ask what the playback rate is.
    case readSimRate

    /// `ZIG,<turns>,<mode>,<first>,<row>` — set up the chained-turn test. It
    /// only configures; the test is launched by the menu action. `row` picks
    /// SS90 (the speed-run turn, the only one that has to chain) or SS90E.
    case zigzagSetup(turns: Int, spin: Bool, firstRight: Bool, speedRunRow: Bool)

    /// Sets up the next post loop; it does not launch one, and it never
    /// touches the turn table -- 1440 degrees is not a turn.
    case postLoopSetup(laps: Int, omega: Double, alpha: Double,
                       right: Bool, velocity: Double)

    // --- ground-truth maze upload (GT channel) -----------------------------
    // Every one of these is acked by the mouse, and the row carries a
    // checksum she checks before accepting it. See E4MazeUploader.

    /// Wipe her ground-truth maze before sending a new one.
    case mazeClear

    /// One row: 16 hex wall masks plus the position-weighted checksum.
    case mazeRow(y: Int, hex: String, checksum: UInt8)

    /// Set the goal cell. She echoes it back as `GTGOK`.
    case mazeGoal(x: Int, y: Int)

    /// Ask her to read the whole truth maze back for verification.
    case mazeVerify

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
        case .thresholds(let l, let r, let f):
            return "THR,\(l),\(r),\(f)\n"
        case .readThresholds:
            return "THR?\n"
        case .readVersion:
            return "VER?\n"
        case .readConfig:
            return "CFG?\n"
        case .selectTurn(let index):
            return "SEL,\(index)\n"
        case .tuneApproach(let cells):
            return "POS,\(cells)\n"
        case .saveTuning:
            return "S\n"
        case .runKind(let k):
            return "KIND,\(k)\n"
        case .readRunKind:
            return "KIND?\n"
        case .simRate(let rate):
            return "SIM,\(String(format: "%.2f", rate))\n"
        case .readSimRate:
            return "SIM?\n"
        case .zigzagSetup(let turns, let spin, let first, let row):
            return "ZIG,\(turns),\(spin ? 1 : 0),\(first ? 1 : 0),\(row ? 1 : 0)\n"
        case .postLoopSetup(let laps, let omega, let alpha, let right, let v):
            return "LOOP,\(laps),\(n(omega)),\(n(alpha)),\(right ? 1 : 0),\(n(v))\n"
        case .mazeClear:
            return "GTC\n"
        case .mazeRow(let y, let hex, let checksum):
            return "GTR,\(y),\(hex),\(String(format: "%02x", checksum))\n"
        case .mazeGoal(let x, let y):
            return "GTG,\(x),\(y)\n"
        case .mazeVerify:
            return "GTE\n"
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
    case wallFollowLeft = 20
    case speedRun = 21
    case resumeSaved = 22
    case runOptions = 23
    case emitterHold = 24
    case firmwareVersion = 25
    case irSampler = 26
    case gyroScaleCal = 27
    case btProvision = 28
    case thresholdCal = 29
    case zigzagTest = 30
    case planRoute = 31
    case wallFollowRight = 32
    case simFollowLeft = 33
    case simFollowRight = 34
    case simSpeedRun = 35

    /// Four chained same-hand 90s of radius 90 mm are one circle centred on a
    /// post. Driven continuously there are no ramps between them, so the ideal
    /// R = v/omega applies and omega is pinned at 191 deg/s. Over a closed lap
    /// every consistent displacement error cancels and only HEADING drifts, so
    /// the centre walking away from the post is a direct read of alpha and
    /// gyro scale -- about 6 mm a lap per degree of error per turn.
    case postLoop = 36

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
        case .wallFollowLeft:  return "w"
        case .wallFollowRight: return "W"
        case .simFollowLeft:   return "q"
        case .simFollowRight:  return "Q"
        case .simSpeedRun:     return "F"
        case .postLoop:        return "A"
        case .speedRun:        return "l"
        case .resumeSaved:     return "R"
        case .runOptions:      return "O"
        case .emitterHold:     return "E"
        case .firmwareVersion: return "V"
        case .irSampler:       return "S"
        case .gyroScaleCal:    return "G"
        case .btProvision:     return "B"
        case .zigzagTest:      return "Z"
        case .planRoute:       return "P"
        case .thresholdCal:    return "T"
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
        case .wallFollowLeft:  return "Wall follow L"
        case .wallFollowRight: return "Wall follow R"
        case .simFollowLeft:   return "Sim follow L"
        case .simFollowRight:  return "Sim follow R"
        case .simSpeedRun:     return "Sim speed run"
        case .postLoop:        return "Post loop"
        case .speedRun:        return "Speed run"
        case .resumeSaved:     return "Resume saved"
        case .runOptions:      return "Run options"
        case .emitterHold:     return "Emitter hold"
        case .firmwareVersion: return "Firmware version"
        case .irSampler:       return "IR sampler"
        case .gyroScaleCal:    return "Gyro scale cal"
        case .btProvision:     return "BT provision"
        case .zigzagTest:      return "Zigzag test"
        case .planRoute:       return "Plan route"
        case .thresholdCal:    return "Threshold cal"
        }
    }

    /// True for actions that cannot work while this app is connected.
    ///
    /// Both BT actions drive the module with AT commands, and an HM-10/HM-18
    /// only accepts those while **no central is attached** — which this app is.
    /// Started over the link, the firmware's AT text goes out through a UART
    /// that is in transparent mode, so it arrives in the log as garbage and the
    /// sweep reports the module silent. They belong to the on-board menu.
    public var requiresNoCentral: Bool {
        switch self {
        case .setBT57k, .btProvision: return true
        default:                      return false
        }
    }

    /// True for actions that ARM and then wait for a button press ON THE MOUSE
    /// before anything happens.
    ///
    /// She calls `wait_for_user_start()` first, so sending one of these over the
    /// link looks exactly like nothing happening — which is indistinguishable
    /// from a command that never arrived, and sends you looking for a fault in
    /// the wrong place.
    public var waitsForButtonPress: Bool {
        switch self {
        case .search, .explore, .simulate, .simExplore,
             // All four followers call wait_for_user_start(), the driven pair
             // and the simulated pair alike. Leaving them out of this list is
             // why pressing one in the app looked like nothing happening.
             .wallFollowLeft, .wallFollowRight, .simFollowLeft, .simFollowRight,
             .simSpeedRun, .speedRun, .postLoop:
            return true
        default:
            return false
        }
    }

    /// True for actions that drive the wheels — worth confirming before firing
    /// one by accident from a phone in your pocket.
    public var movesTheMouse: Bool {
        switch self {
        case .forward180, .right90, .left90, .spin180, .motionTest,
             .search, .explore, .recalGyro, .turnTuning,
             .wallFollowLeft, .wallFollowRight,   // the sim pair never arms a motor
             .speedRun, .resumeSaved, .gyroScaleCal,
             .zigzagTest:              // drives a zigzag across three cells
            return true
        // planRoute is deliberately NOT here: it plans and streams, no motors.
        default:
            return false
        }
    }
}
