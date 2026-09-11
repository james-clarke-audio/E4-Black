import SwiftUI
import E4Core

/// Connection, firmware, the four sensor bars and the pack — pinned above
/// everything, on every screen.
///
/// This never collapses. The web app dropped the sensor readout at narrow
/// widths and it took an evening to work out why the board photo had gone;
/// at the bench that readout is the reason the app is open. `ViewThatFits`
/// stacks it to two rows rather than hiding any of it.
struct StatusStrip: View {
    @Environment(E4Session.self) private var session

    var body: some View {
        VStack(spacing: 0) {
            ViewThatFits(in: .horizontal) {
                oneRow
                twoRows
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 10)

            Rectangle().fill(Palette.line).frame(height: 1)
        }
        .background(Palette.card)
    }

    private var oneRow: some View {
        HStack(spacing: 20) {
            identity
            Divider().frame(height: 28)
            bars
            Divider().frame(height: 28)
            pack
        }
    }

    private var twoRows: some View {
        VStack(spacing: 10) {
            HStack(spacing: 14) {
                identity
                Spacer(minLength: 12)
                pack
            }
            bars
        }
    }

    // MARK: pieces

    private var identity: some View {
        HStack(spacing: 8) {
            Circle()
                .fill(indicator)
                .frame(width: 9, height: 9)
            VStack(alignment: .leading, spacing: 1) {
                Text(session.connection.isConnected ? deviceName : session.connection.label)
                    .font(.callout.weight(.semibold))
                    .lineLimit(1)
                if let version = session.firmwareVersion {
                    Text("fw v\(version)")
                        .font(.system(size: 10.5, design: .monospaced))
                        .foregroundStyle(Palette.faint)
                }
            }
        }
        .fixedSize(horizontal: true, vertical: false)
    }

    private var bars: some View {
        let scale = sensorFullScale(session)
        return HStack(spacing: 14) {
            ForEach(E4Sensor.allCases) { sensor in
                let display = SensorDisplay(sensor, session: session, fullScale: scale)
                VStack(spacing: 3) {
                    HStack {
                        Text(sensor.label)
                            .font(.system(size: 10, weight: .semibold, design: .monospaced))
                            .foregroundStyle(session.litEmitter == sensor.rawValue ? Palette.warn : Palette.faint)
                        Spacer()
                        Text("\(display.value)")
                            .font(.system(size: 11, design: .monospaced))
                            .monospacedDigit()
                    }
                    SensorBar(display: display, height: 7)
                }
                .frame(minWidth: 54)
            }
        }
    }

    @ViewBuilder
    private var pack: some View {
        HStack(spacing: 12) {
            if let volts = session.battery, let state = session.batteryState {
                VStack(alignment: .trailing, spacing: 1) {
                    Text(String(format: "%.2f V", volts))
                        .font(.callout.monospaced())
                        .monospacedDigit()
                        .foregroundStyle(batteryTint(state))
                    Text(batteryNote(state))
                        .font(.system(size: 9.5, design: .monospaced))
                        .foregroundStyle(Palette.faint)
                }
            }
            if let status = session.sensorStatus {
                Text(status.usingRealIR ? "REAL" : "VIRTUAL")
                    .font(.system(size: 10, weight: .semibold, design: .monospaced))
                    .tracking(0.6)
                    .padding(.horizontal, 9)
                    .padding(.vertical, 5)
                    .background(
                        (status.usingRealIR ? Palette.good : Palette.warn).opacity(0.13),
                        in: RoundedRectangle(cornerRadius: 6)
                    )
                    .foregroundStyle(status.usingRealIR ? Palette.good : Palette.warn)
            }
        }
        .fixedSize(horizontal: true, vertical: false)
    }

    // MARK: derived

    private var deviceName: String {
        if case .connected(let name) = session.connection { return name }
        return "Connected"
    }

    private var indicator: Color {
        switch session.connection {
        case .connected:              return Palette.good
        case .connecting, .scanning:  return Palette.warn
        case .failed, .unauthorized, .unsupported, .poweredOff: return Palette.bad
        case .idle:                   return Palette.faint
        }
    }

    /// Thresholds come from the firmware's own BATT_* constants — she is a 1S
    /// LiPo, so 3.66 V is healthy. An app that warns at the wrong voltage is
    /// worse than one that never warns, because you learn to ignore it.
    private func batteryTint(_ state: E4BatteryState) -> Color {
        switch state {
        case .noPack:   return Palette.faint
        case .critical: return Palette.bad
        case .marginal: return Palette.warn
        case .ok:       return Palette.ink
        }
    }

    private func batteryNote(_ state: E4BatteryState) -> String {
        switch state {
        case .noPack:   return "bench"
        case .critical: return "below cutoff"
        case .marginal: return "not re-armed"
        case .ok:       return "1S · cut 3.30"
        }
    }
}
