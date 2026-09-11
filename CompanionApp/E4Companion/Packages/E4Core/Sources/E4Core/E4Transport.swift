import Foundation

/// Connection lifecycle, as the UI needs to see it.
public enum E4ConnectionState: Sendable, Equatable {
    case unsupported            // no BLE on this machine
    case unauthorized           // user declined the permission prompt
    case poweredOff             // Bluetooth switched off
    case idle                   // ready, not scanning
    case scanning
    case connecting(String)     // peripheral name
    case connected(String)
    case failed(String)         // human-readable reason

    public var isConnected: Bool {
        if case .connected = self { return true }
        return false
    }

    /// What to show the user. Deliberately specific: on the iPad the single
    /// most expensive debugging session was caused by an error path that
    /// reported nothing useful.
    public var label: String {
        switch self {
        case .unsupported:        return "Bluetooth not available"
        case .unauthorized:       return "Bluetooth permission denied"
        case .poweredOff:         return "Bluetooth is off"
        case .idle:               return "Disconnected"
        case .scanning:           return "Scanning…"
        case .connecting(let n):  return "Connecting to \(n)…"
        case .connected(let n):   return "Connected to \(n)"
        case .failed(let why):    return "Failed: \(why)"
        }
    }
}

/// A device found while scanning.
public struct E4Peripheral: Sendable, Equatable, Identifiable {
    public let id: UUID
    public let name: String
    public let rssi: Int

    public init(id: UUID, name: String, rssi: Int) {
        self.id = id
        self.name = name
        self.rssi = rssi
    }
}

/// Everything the session needs from a link, so the UI and the session logic
/// can be exercised without Core Bluetooth — and so a future USB/serial or
/// replay-from-log transport slots in unchanged.
@MainActor
public protocol E4Transport: AnyObject {
    var onStateChange: ((E4ConnectionState) -> Void)? { get set }
    var onDiscover: (([E4Peripheral]) -> Void)? { get set }
    var onLine: ((String) -> Void)? { get set }

    func startScan(includeUnnamed: Bool)
    func stopScan()
    func connect(to id: UUID)
    func disconnect()
    func send(_ text: String)
}
