import Foundation

/// Wire-level constants for the E4 link.
///
/// The mouse speaks a line-oriented ASCII protocol over a BLE-UART bridge
/// (an HM-10 class module). The BLE side is a single service with a single
/// characteristic that is both notify and write — everything is framed by
/// newlines, not by BLE packets.
public enum E4Protocol {

    // MARK: Bluetooth identity

    /// The BLE-UART service. Note the FULL canonical form: the 16-bit
    /// shorthand `0xffe0` is accepted by Chrome's Web Bluetooth but rejected
    /// outright by some stacks, and Core Bluetooth wants a real CBUUID
    /// anyway. Always use the expanded form.
    public static let serviceUUIDString = "0000FFE0-0000-1000-8000-00805F9B34FB"

    /// The single characteristic: notify (mouse -> app) and write (app -> mouse).
    public static let characteristicUUIDString = "0000FFE1-0000-1000-8000-00805F9B34FB"

    // MARK: Framing

    /// The mouse terminates every line it sends with CRLF.
    public static let outboundTerminator = "\r\n"

    /// The firmware's inbound line buffer is 80 bytes including the NUL, and
    /// it silently drops any line that overruns. Keep commands well under this.
    public static let maxInboundLineLength = 79

    /// The link runs at **57600** — that is what `usart.c` configures and what
    /// the `.ioc` says, so it is the rate in normal use. A module fresh out of
    /// its bag ships at 9600, which is why the firmware's baud sweep tries that
    /// first and why `BT provision` exists to move it.
    ///
    /// Either way BLE can hand over data far faster than the bridge can clock
    /// it out, so writes must be paced — see `E4BluetoothTransport`.
    public static let linkBaud = 57600
    public static let factoryBaud = 9600
}

/// Pack health, judged against the firmware's own cutoff.
///
/// These mirror `BATT_*` in `Program/inc/motion_config.h` — E4 runs a **1S
/// LiPo**, so a healthy reading sits around 3.6–4.2 V. Keep them in step with
/// the firmware: an app that warns at the wrong voltage is worse than one that
/// doesn't warn at all, because you learn to ignore it.
public enum E4BatteryState: Sendable, Equatable {
    /// Below `BATT_VALID_VOLTS` — no cell fitted, or USB bench power. The
    /// firmware ignores these readings rather than parking the motors.
    case noPack
    /// Below `BATT_CUTOFF_VOLTS`. Held here for 200 ms and the guard trips.
    case critical
    /// Between the cutoff and the re-arm point — a tripped guard stays tripped.
    case marginal
    case ok

    public init(volts: Double) {
        switch volts {
        case ..<E4Protocol.batteryValidVolts:   self = .noPack
        case ..<E4Protocol.batteryCutoffVolts:  self = .critical
        case ..<E4Protocol.batteryRecoverVolts: self = .marginal
        default:                                self = .ok
        }
    }
}

public extension E4Protocol {
    /// Trip floor under load.
    static var batteryCutoffVolts: Double { 3.30 }
    /// The guard re-arms above this.
    static var batteryRecoverVolts: Double { 3.45 }
    /// Below this the firmware assumes no pack is fitted.
    static var batteryValidVolts: Double { 2.50 }
}

/// A wall mask as the firmware packs it: one bit per side of a cell.
public struct E4WallMask: OptionSet, Sendable, Hashable {
    public let rawValue: Int
    public init(rawValue: Int) { self.rawValue = rawValue }

    public static let north = E4WallMask(rawValue: 1)
    public static let east  = E4WallMask(rawValue: 2)
    public static let south = E4WallMask(rawValue: 4)
    public static let west  = E4WallMask(rawValue: 8)
}

/// Compass heading as the firmware enumerates it (0 = north, clockwise).
public enum E4Heading: Int, Sendable, CaseIterable {
    case north = 0, east = 1, south = 2, west = 3

    /// Screen-space rotation in degrees, matching the web app's convention.
    public var degrees: Double {
        switch self {
        case .north: return 0
        case .east:  return -90
        case .south: return 180
        case .west:  return 90
        }
    }
}

/// The four wall sensors, in the order the `IR` message reports them.
public enum E4Sensor: Int, Sendable, CaseIterable, Identifiable {
    case left = 0, frontLeft = 1, frontRight = 2, right = 3
    public var id: Int { rawValue }

    /// Short label as used on the board and in the manual.
    public var label: String {
        switch self {
        case .left:       return "SL"
        case .frontLeft:  return "FL"
        case .frontRight: return "FR"
        case .right:      return "SR"
        }
    }
}
