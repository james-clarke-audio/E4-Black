import SwiftUI
import E4Core

struct ContentView: View {
    @Environment(E4Session.self) private var session

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 16) {
                    ConnectionCard()
                    if session.connection.isConnected {
                        SensorCard()
                        MenuCard()
                    }
                    LogCard()
                }
                .padding()
                .frame(maxWidth: 720)
                .frame(maxWidth: .infinity)
            }
            .navigationTitle("E4")
            .toolbar {
                ToolbarItem(placement: .primaryAction) {
                    if let version = session.firmwareVersion {
                        Text("fw v\(version)")
                            .font(.caption.monospaced())
                            .foregroundStyle(.secondary)
                    }
                }
            }
        }
    }
}

// MARK: - Connection

struct ConnectionCard: View {
    @Environment(E4Session.self) private var session

    var body: some View {
        @Bindable var session = session

        Card("Connection") {
            HStack {
                Circle()
                    .fill(indicatorColour)
                    .frame(width: 9, height: 9)
                Text(session.connection.label)
                    .font(.callout)
                Spacer()
                if let battery = session.battery, let state = session.batteryState {
                    HStack(spacing: 5) {
                        Text(String(format: "%.2f V", battery))
                            .font(.callout.monospacedDigit())
                        if state == .noPack {
                            Text("bench")
                                .font(.caption2.monospaced())
                        }
                    }
                    .foregroundStyle(batteryColour(state))
                    .help(batteryHelp(state))
                }
            }

            if session.connection.isConnected {
                Button("Disconnect", role: .destructive) { session.disconnect() }
                    .buttonStyle(.bordered)
            } else {
                Toggle("Show devices that don't advertise FFE0", isOn: $session.scanUnfiltered)
                    .font(.caption)
                    .toggleStyle(.switch)

                HStack {
                    Button("Scan") { session.startScan() }
                        .buttonStyle(.borderedProminent)
                    Button("Stop") { session.stopScan() }
                        .buttonStyle(.bordered)
                }

                ForEach(session.peripherals) { device in
                    Button {
                        session.connect(to: device)
                    } label: {
                        HStack {
                            Text(device.name)
                            Spacer()
                            Text("\(device.rssi) dBm")
                                .font(.caption.monospacedDigit())
                                .foregroundStyle(.secondary)
                        }
                    }
                    .buttonStyle(.bordered)
                }
            }
        }
    }

    /// Thresholds come from the firmware (1S LiPo), not from a guess — see
    /// E4BatteryState.
    private func batteryColour(_ state: E4BatteryState) -> Color {
        switch state {
        case .noPack:   return .secondary
        case .critical: return .red
        case .marginal: return .orange
        case .ok:       return .secondary
        }
    }

    private func batteryHelp(_ state: E4BatteryState) -> String {
        switch state {
        case .noPack:   return "Below \(E4Protocol.batteryValidVolts) V — no pack fitted, or bench power. The firmware ignores this."
        case .critical: return "Below the \(E4Protocol.batteryCutoffVolts) V cutoff. Held here for 200 ms and the motors park."
        case .marginal: return "Between the cutoff and the \(E4Protocol.batteryRecoverVolts) V re-arm point."
        case .ok:       return "Healthy for a 1S LiPo."
        }
    }

    private var indicatorColour: Color {
        switch session.connection {
        case .connected:            return .green
        case .connecting, .scanning: return .orange
        case .failed, .unauthorized, .unsupported, .poweredOff: return .red
        case .idle:                 return .secondary
        }
    }
}

// MARK: - Sensors

struct SensorCard: View {
    @Environment(E4Session.self) private var session

    /// The bars scale to the largest reading seen this connection, rounded up
    /// to a round number, with a sane floor.
    ///
    /// A fixed range cannot work: real ambient-subtracted IR runs to a few
    /// hundred, virtual (truth-derived) readings run into the thousands, and
    /// scaling one for the other pins every bar at full — which is exactly what
    /// a fixed 600 did on the first run.
    private var fullScale: Double {
        let peak = max(Double(session.sensorPeak), 300)
        let step = peak > 1500 ? 500.0 : 100.0
        return (peak / step).rounded(.up) * step
    }

    var body: some View {
        Card("Wall sensors") {
            ForEach(E4Sensor.allCases) { sensor in
                let value = session.sensors[sensor.rawValue]
                HStack(spacing: 10) {
                    Text(sensor.label)
                        .font(.caption.monospaced())
                        .frame(width: 26, alignment: .leading)
                        .foregroundStyle(session.litEmitter == sensor.rawValue ? .orange : .secondary)

                    GeometryReader { geo in
                        ZStack(alignment: .leading) {
                            RoundedRectangle(cornerRadius: 3)
                                .fill(.quaternary)
                            RoundedRectangle(cornerRadius: 3)
                                .fill(barColour(for: sensor, value: value))
                                .frame(width: geo.size.width * min(1, Double(value) / fullScale))

                            // Where this sensor's wall-detect threshold sits, so
                            // you can see the margin rather than just the colour
                            // flipping — the number that matters when setting
                            // thresholds is how far clear of the line you are.
                            if let mark = thresholdFraction(for: sensor) {
                                Rectangle()
                                    .fill(.primary)
                                    .opacity(0.45)
                                    .frame(width: 1.5)
                                    .offset(x: geo.size.width * mark)
                            }
                        }
                    }
                    .frame(height: 10)

                    Text("\(value)")
                        .font(.caption.monospacedDigit())
                        .frame(width: 46, alignment: .trailing)
                }
            }

            HStack {
                Text("0")
                Spacer()
                Text("scale \(Int(fullScale))")
                Spacer()
                Text(session.sensorPeak > 0 ? "peak \(session.sensorPeak)" : "")
            }
            .font(.caption2.monospacedDigit())
            .foregroundStyle(.tertiary)

            if let status = session.sensorStatus {
                HStack {
                    // The front pair is judged on its sum, so show that against
                    // its threshold rather than leaving it implicit.
                    Text("front \(status.frontSum)/\(status.frontThreshold)")
                        .foregroundStyle(status.frontSum >= status.frontThreshold
                                         ? Color.green : .secondary)
                    Spacer()
                    Text("side thr \(status.sideThreshold)")
                        .foregroundStyle(.secondary)
                    Spacer()
                    Text(status.usingRealIR ? "REAL" : "VIRTUAL")
                        .foregroundStyle(status.usingRealIR ? .green : .orange)
                }
                .font(.caption.monospacedDigit())
            }

            HStack {
                Button("IR monitor") { session.send(.action(.irMonitor)) }
                Button("Emitter hold") { session.send(.action(.emitterHold)) }
                Button("Sensor mode") { session.send(.action(.sensorMode)) }
            }
            .buttonStyle(.bordered)
            .font(.callout)
        }
    }

    /// The front threshold is tested against the FL+FR *sum*, not each sensor,
    /// so only the side pair gets a per-bar mark.
    private func threshold(for sensor: E4Sensor) -> Int? {
        guard let status = session.sensorStatus else { return nil }
        switch sensor {
        case .left, .right:            return status.sideThreshold
        case .frontLeft, .frontRight:  return nil
        }
    }

    private func thresholdFraction(for sensor: E4Sensor) -> Double? {
        guard let t = threshold(for: sensor), fullScale > 0 else { return nil }
        let fraction = Double(t) / fullScale
        return fraction < 1 ? fraction : nil
    }

    /// Green means "this sensor is reporting a wall", grey means it isn't.
    ///
    /// Deliberately not the system accent: the accent is whatever the user has
    /// chosen in System Settings, so it carries no meaning here and a pink or
    /// red accent reads as an alarm state on a perfectly healthy reading.
    ///
    /// The front pair takes its verdict from the FL+FR *sum*, because that is
    /// what the firmware actually tests — so both go green together, which is
    /// the honest picture even when one of them is doing most of the work.
    private func barColour(for sensor: E4Sensor, value: Int) -> Color {
        guard let status = session.sensorStatus else { return .secondary }
        switch sensor {
        case .left, .right:
            return value >= status.sideThreshold ? .green : .secondary
        case .frontLeft, .frontRight:
            return status.frontSum >= status.frontThreshold ? .green : .secondary
        }
    }
}

// MARK: - Menu

struct MenuCard: View {
    @Environment(E4Session.self) private var session
    @State private var pendingAction: E4MenuAction?

    var body: some View {
        Card("Menu") {
            HStack {
                Button("Prev") { session.send(.previous) }
                Button("Enter") { session.send(.enter) }
                Button("Next") { session.send(.next) }
                Button("Back") { session.send(.back) }
            }
            .buttonStyle(.bordered)

            Divider()

            // Actions that drive the wheels ask first. On a tablet propped up
            // next to a half-built mouse, an accidental tap on "Search" is not
            // a harmless mistake.
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 128), spacing: 8)], spacing: 8) {
                ForEach(E4MenuAction.allCases) { action in
                    Button {
                        if action.movesTheMouse {
                            pendingAction = action
                        } else {
                            session.send(.action(action))
                        }
                    } label: {
                        HStack(spacing: 6) {
                            if action.movesTheMouse {
                                Image(systemName: "figure.walk.motion")
                                    .font(.caption2)
                            }
                            Text(action.title)
                                .lineLimit(1)
                                .minimumScaleFactor(0.8)
                            Spacer(minLength: 0)
                            Text(String(action.key))
                                .font(.caption2.monospaced())
                                .foregroundStyle(.secondary)
                        }
                    }
                    .buttonStyle(.bordered)
                    .font(.callout)
                }
            }
        }
        .confirmationDialog(
            pendingAction.map { "Run \($0.title)?" } ?? "",
            isPresented: .init(get: { pendingAction != nil },
                               set: { if !$0 { pendingAction = nil } }),
            titleVisibility: .visible
        ) {
            Button("Run", role: .destructive) {
                if let action = pendingAction { session.send(.action(action)) }
                pendingAction = nil
            }
            Button("Cancel", role: .cancel) { pendingAction = nil }
        } message: {
            Text("This moves the mouse. Make sure she has room.")
        }
    }
}

// MARK: - Log

struct LogCard: View {
    @Environment(E4Session.self) private var session
    @State private var command = ""

    var body: some View {
        Card("Log") {
            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 1) {
                        ForEach(session.log) { entry in
                            Text(entry.text)
                                .font(.caption2.monospaced())
                                .foregroundStyle(colour(for: entry.kind))
                                .textSelection(.enabled)
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .id(entry.id)
                        }
                    }
                }
                .frame(height: 190)
                .onChange(of: session.log.count) {
                    if let last = session.log.last {
                        withAnimation(.easeOut(duration: 0.12)) {
                            proxy.scrollTo(last.id, anchor: .bottom)
                        }
                    }
                }
            }

            HStack {
                TextField("raw command", text: $command)
                    .textFieldStyle(.roundedBorder)
                    .font(.callout.monospaced())
                    #if os(iOS)
                    .autocorrectionDisabled()
                    .textInputAutocapitalization(.never)
                    #endif
                    .onSubmit(sendRaw)
                Button("Send", action: sendRaw)
                    .disabled(command.isEmpty || !session.connection.isConnected)
                Button("Clear") { session.clearLog() }
            }
            .buttonStyle(.bordered)
        }
    }

    private func sendRaw() {
        session.sendRaw(command)
        command = ""
    }

    private func colour(for kind: E4LogEntry.Kind) -> Color {
        switch kind {
        case .sent:    return .accentColor
        case .event:   return .primary
        case .unknown: return .secondary
        case .alert:   return .orange
        }
    }
}

// MARK: - Shared chrome

struct Card<Content: View>: View {
    let title: String
    let content: Content

    init(_ title: String, @ViewBuilder content: () -> Content) {
        self.title = title
        self.content = content()
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(title.uppercased())
                .font(.caption2.monospaced())
                .tracking(1.2)
                .foregroundStyle(.secondary)
            content
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(14)
        // Deliberately not a system "secondary background" style — those differ
        // between iOS and macOS. A tint of the foreground works identically on
        // both, and in light and dark.
        .background(Color.primary.opacity(0.045), in: RoundedRectangle(cornerRadius: 12))
    }
}
