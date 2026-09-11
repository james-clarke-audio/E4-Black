import Foundation
import CoreBluetooth

/// Core Bluetooth link to the mouse. Identical on iOS and macOS — Core
/// Bluetooth is one of the few frameworks that genuinely needs no platform
/// conditionals here.
///
/// Deliberate simplification: the central manager runs on the **main queue**.
/// At this data rate (short telemetry lines over a 57600-baud UART bridge,
/// a few kB/s at the absolute most) there is nothing to gain from a background
/// queue, and it removes every actor-hop and data-race question from the
/// delegate callbacks. Revisit only if profiling says so.
@MainActor
public final class E4BluetoothTransport: NSObject, E4Transport {

    public var onStateChange: ((E4ConnectionState) -> Void)?
    public var onDiscover: (([E4Peripheral]) -> Void)?
    public var onLine: ((String) -> Void)?

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var characteristic: CBCharacteristic?

    private var assembler = E4LineAssembler()
    private var discovered: [UUID: E4Peripheral] = [:]

    /// Set when the caller asked to scan before Bluetooth finished powering on.
    private var scanPending = false
    private var scanIncludesUnnamed = false

    /// Outbound queue. See `pump()` for why this is not just a write call.
    private var outbox: [Data] = []
    private var writing = false

    private let serviceUUID = CBUUID(string: E4Protocol.serviceUUIDString)
    private let characteristicUUID = CBUUID(string: E4Protocol.characteristicUUIDString)

    public override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: nil)   // nil == main queue
    }

    // MARK: - E4Transport

    public func startScan(includeUnnamed: Bool) {
        scanIncludesUnnamed = includeUnnamed
        discovered.removeAll()
        onDiscover?([])

        guard central.state == .poweredOn else {
            // Powering on is asynchronous and usually still in flight when a
            // view appears. Remember the intent instead of failing.
            scanPending = true
            publishState(for: central.state)
            return
        }

        // Scanning by service UUID is the right default, but some BLE-UART
        // modules do not put FFE0 in their advertisement even though they
        // expose it after connecting. `includeUnnamed` widens the net for those.
        let services: [CBUUID]? = includeUnnamed ? nil : [serviceUUID]
        central.scanForPeripherals(withServices: services,
                                   options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
        onStateChange?(.scanning)
    }

    public func stopScan() {
        scanPending = false
        if central.state == .poweredOn { central.stopScan() }
        if peripheral == nil { onStateChange?(.idle) }
    }

    public func connect(to id: UUID) {
        guard let target = central.retrievePeripherals(withIdentifiers: [id]).first
                ?? peripheralFromDiscovery(id) else {
            onStateChange?(.failed("device no longer available"))
            return
        }
        central.stopScan()
        scanPending = false
        peripheral = target
        target.delegate = self
        onStateChange?(.connecting(target.name ?? "mouse"))
        central.connect(target, options: nil)
    }

    public func disconnect() {
        outbox.removeAll()
        writing = false
        assembler.reset()
        if let p = peripheral { central.cancelPeripheralConnection(p) }
        peripheral = nil
        characteristic = nil
        onStateChange?(.idle)
    }

    public func send(_ text: String) {
        guard let data = text.data(using: .utf8) else { return }
        // Chunk to the negotiated MTU. The BLE default payload is 20 bytes,
        // and several of the real commands are longer than that —
        // `ARC,300,90,600,3000,20,20` is 26. An unchunked write of those is
        // truncated with no error, which the firmware then drops as a garbled
        // line, and the symptom is "the arc button does nothing".
        let limit = maxWriteLength()
        var index = data.startIndex
        while index < data.endIndex {
            let end = data.index(index, offsetBy: limit, limitedBy: data.endIndex) ?? data.endIndex
            outbox.append(data[index..<end])
            index = end
        }
        pump()
    }

    // MARK: - Writing

    private func maxWriteLength() -> Int {
        guard let p = peripheral else { return 20 }
        let withResponse = p.maximumWriteValueLength(for: .withResponse)
        let withoutResponse = p.maximumWriteValueLength(for: .withoutResponse)
        return max(20, min(withResponse, max(withoutResponse, 20)))
    }

    /// Sends at most one chunk, then waits.
    ///
    /// Pacing matters more than it looks. BLE will happily accept writes far
    /// faster than the module can clock them out of its UART at 9600 or 57600
    /// baud, and the module's own buffer is small — flooding it drops bytes
    /// mid-line, which surfaces as randomly corrupted commands. So: prefer
    /// `.withResponse` (the peripheral acknowledges each write, which paces us
    /// for free), and fall back to waiting for `peripheralIsReady` otherwise.
    private func pump() {
        guard !writing,
              let p = peripheral,
              let c = characteristic,
              !outbox.isEmpty
        else { return }

        let chunk = outbox.removeFirst()

        if c.properties.contains(.write) {
            writing = true
            p.writeValue(chunk, for: c, type: .withResponse)
        } else if c.properties.contains(.writeWithoutResponse) {
            p.writeValue(chunk, for: c, type: .withoutResponse)
            if p.canSendWriteWithoutResponse {
                pump()
            } else {
                writing = true      // cleared by peripheralIsReadyToSendWriteWithoutResponse
            }
        }
    }

    private func peripheralFromDiscovery(_ id: UUID) -> CBPeripheral? {
        guard let p = peripheral, p.identifier == id else { return nil }
        return p
    }

    private func publishState(for state: CBManagerState) {
        switch state {
        case .poweredOn:   onStateChange?(.idle)
        case .poweredOff:  onStateChange?(.poweredOff)
        case .unauthorized: onStateChange?(.unauthorized)
        case .unsupported: onStateChange?(.unsupported)
        default:           break     // .resetting / .unknown are transient
        }
    }
}

// MARK: - CBCentralManagerDelegate

extension E4BluetoothTransport: CBCentralManagerDelegate {

    public func centralManagerDidUpdateState(_ central: CBCentralManager) {
        publishState(for: central.state)
        if central.state == .poweredOn && scanPending {
            scanPending = false
            startScan(includeUnnamed: scanIncludesUnnamed)
        }
    }

    public func centralManager(_ central: CBCentralManager,
                               didDiscover peripheral: CBPeripheral,
                               advertisementData: [String: Any],
                               rssi RSSI: NSNumber) {
        // Prefer the advertised local name: a peripheral's cached `name` can be
        // stale or absent until connection.
        let advertised = advertisementData[CBAdvertisementDataLocalNameKey] as? String
        let name = advertised ?? peripheral.name ?? "Unnamed device"

        discovered[peripheral.identifier] = E4Peripheral(id: peripheral.identifier,
                                                         name: name,
                                                         rssi: RSSI.intValue)
        // Keep a strong reference so `connect(to:)` can find it even if
        // retrievePeripherals misses.
        if self.peripheral == nil { self.peripheral = peripheral }

        onDiscover?(discovered.values.sorted { $0.rssi > $1.rssi })
    }

    public func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        assembler.reset()
        peripheral.discoverServices([serviceUUID])
    }

    public func centralManager(_ central: CBCentralManager,
                               didFailToConnect peripheral: CBPeripheral,
                               error: Error?) {
        onStateChange?(.failed(error?.localizedDescription ?? "could not connect"))
        self.peripheral = nil
    }

    public func centralManager(_ central: CBCentralManager,
                               didDisconnectPeripheral peripheral: CBPeripheral,
                               error: Error?) {
        self.peripheral = nil
        characteristic = nil
        outbox.removeAll()
        writing = false
        assembler.reset()
        if let error {
            onStateChange?(.failed(error.localizedDescription))
        } else {
            onStateChange?(.idle)
        }
    }
}

// MARK: - CBPeripheralDelegate

extension E4BluetoothTransport: CBPeripheralDelegate {

    public func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error {
            onStateChange?(.failed(error.localizedDescription))
            return
        }
        guard let service = peripheral.services?.first(where: { $0.uuid == serviceUUID }) else {
            onStateChange?(.failed("no FFE0 service — is this the right module?"))
            return
        }
        peripheral.discoverCharacteristics([characteristicUUID], for: service)
    }

    public func peripheral(_ peripheral: CBPeripheral,
                           didDiscoverCharacteristicsFor service: CBService,
                           error: Error?) {
        if let error {
            onStateChange?(.failed(error.localizedDescription))
            return
        }
        guard let c = service.characteristics?.first(where: { $0.uuid == characteristicUUID }) else {
            onStateChange?(.failed("no FFE1 characteristic"))
            return
        }
        characteristic = c
        peripheral.setNotifyValue(true, for: c)
        onStateChange?(.connected(peripheral.name ?? "mouse"))
        pump()
    }

    public func peripheral(_ peripheral: CBPeripheral,
                           didUpdateValueFor characteristic: CBCharacteristic,
                           error: Error?) {
        guard error == nil, let data = characteristic.value else { return }
        // Notifications are not line-aligned — the assembler is what makes
        // this correct. See E4LineAssembler.
        for line in assembler.append(data) {
            onLine?(line)
        }
    }

    public func peripheral(_ peripheral: CBPeripheral,
                           didWriteValueFor characteristic: CBCharacteristic,
                           error: Error?) {
        writing = false
        pump()
    }

    public func peripheralIsReady(toSendWriteWithoutResponse peripheral: CBPeripheral) {
        writing = false
        pump()
    }
}
