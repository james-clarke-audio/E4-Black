import SwiftUI
import E4Core

struct SensorsScreen: View {
    @Environment(E4Session.self) private var session

    var body: some View {
        HStack(spacing: 0) {
            stage
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(Palette.Dark.bg)

            Rectangle().fill(Palette.line).frame(width: 1)

            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    readout
                    aiming
                    thresholds
                }
                .padding(16)
            }
            .frame(width: 340)
            .background(Palette.panel)
        }
    }

    // MARK: stage

    /// The cell, and above it the three captures as a progress strip. Same
    /// shape as the Maze screen deliberately — the thing you are looking at is
    /// in the middle and the numbers are to the right of it, wherever you are.
    private var stage: some View {
        VStack(spacing: 0) {
            captureStrip
            WallCellView(target: cal.targetState,
                         detected: detectedWalls,
                         readings: liveReadings)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            legend
        }
    }

    private var cal: E4ThresholdCalState { session.thresholdCal }

    private var isCalibrating: Bool { session.runningAction == .thresholdCal }

    /// Her own verdict when the routine is running, otherwise the one derived
    /// from the SENS line. Both come from the same comparison in the firmware;
    /// the routine's is just fresher.
    private var detectedWalls: (left: Bool, front: Bool, right: Bool)? {
        if let live = cal.live {
            return (live.seesLeft, live.seesFront, live.seesRight)
        }
        guard let status = session.sensorStatus else { return nil }
        return (status.left >= status.leftThreshold,
                status.frontSum >= status.frontThreshold,
                status.right >= status.rightThreshold)
    }

    private var liveReadings: (left: Int, front: Int, right: Int)? {
        if let live = cal.live { return (live.left, live.front, live.right) }
        guard let status = session.sensorStatus else { return nil }
        return (status.left, status.frontSum, status.right)
    }

    @ViewBuilder
    private var captureStrip: some View {
        if isCalibrating {
            HStack(spacing: 8) {
                ForEach(E4CaptureState.allCases) { state in
                    captureChip(state)
                }
                Spacer()
                if cal.isComplete {
                    Text("all three captured")
                        .font(.caption.monospaced())
                        .foregroundStyle(Palette.Dark.dim)
                }
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 11)
            .background(Palette.Dark.panel)
        }
    }

    private func captureChip(_ state: E4CaptureState) -> some View {
        let done = cal.has(state)
        let active = cal.targetState == state
        return Button {
            session.send(.key(state.key))
        } label: {
            HStack(spacing: 6) {
                Image(systemName: done ? "checkmark.circle.fill" : "circle")
                    .font(.system(size: 11))
                Text(state.title)
                    .font(.caption.weight(active ? .semibold : .regular))
            }
            .foregroundStyle(done ? Palette.good : (active ? Palette.Dark.ink : Palette.Dark.dim))
            .padding(.horizontal, 10)
            .padding(.vertical, 6)
            .background(active ? Palette.Dark.line : Color.clear,
                        in: RoundedRectangle(cornerRadius: 6))
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .touchTarget()
    }

    @ViewBuilder
    private var legend: some View {
        VStack(spacing: 3) {
            if isCalibrating, let target = cal.targetState {
                Text(target.instruction)
                    .font(.callout.weight(.medium))
                    .foregroundStyle(Palette.Dark.ink)
                Text("Then press \(String(target.key)) — or the button on the mouse.")
                    .font(.caption)
                    .foregroundStyle(Palette.Dark.dim)
            } else {
                Text("Green walls are ones she is detecting right now.")
                    .font(.caption)
                    .foregroundStyle(Palette.Dark.dim)
            }
        }
        .multilineTextAlignment(.center)
        .padding(.horizontal, 18)
        .padding(.vertical, 13)
        .frame(maxWidth: .infinity)
        .background(Palette.Dark.panel)
    }

    // MARK: readout

    private var readout: some View {
        let scale = sensorFullScale(session)
        return Card("Wall sensors") {
            ForEach(E4Sensor.allCases) { sensor in
                let display = SensorDisplay(sensor, session: session, fullScale: scale)
                HStack(spacing: 13) {
                    VStack(alignment: .leading, spacing: 1) {
                        Text(sensor.label)
                            .font(.callout.monospaced().weight(.semibold))
                        Text(role(sensor))
                            .font(.system(size: 10.5))
                            .foregroundStyle(.secondary)
                    }
                    .frame(width: 84, alignment: .leading)

                    SensorBar(display: display, height: 22)

                    Text("\(display.value)")
                        .font(.title3.monospaced())
                        .monospacedDigit()
                        .frame(width: 68, alignment: .trailing)
                }
            }

            HStack {
                Text("scale \(Int(scale))")
                Spacer()
                if session.sensorPeak > 0 {
                    Text("peak \(session.sensorPeak)")
                }
            }
            .font(.caption2.monospaced())
            .foregroundStyle(.tertiary)

            if let status = session.sensorStatus {
                Divider()
                HStack(spacing: 22) {
                    // The front pair is judged on its sum, so that is what gets
                    // shown against a threshold.
                    Text("front \(status.frontSum)/\(status.frontThreshold)")
                        .foregroundStyle(status.frontSum >= status.frontThreshold ? Palette.good : Palette.dim)
                    Text("thr L\(status.leftThreshold) R\(status.rightThreshold)")
                        .foregroundStyle(.secondary)
                    Spacer()
                }
                .font(.callout.monospaced())
                .monospacedDigit()
            }

            HStack(spacing: 8) {
                Button("IR monitor") { session.send(.action(.irMonitor)) }
                Button("Sensor mode") { session.send(.action(.sensorMode)) }
                Button("IR sampler") { session.send(.action(.irSampler)) }
            }
            .buttonStyle(.bordered)
            .touchTarget()
        }
    }

    private func role(_ sensor: E4Sensor) -> String {
        switch sensor {
        case .left:       return "left wall"
        case .frontLeft:  return "front wall"
        case .frontRight: return "front wall"
        case .right:      return "right wall"
        }
    }

    // MARK: aiming

    private var aiming: some View {
        Card("Aiming") {
            Text("Light one emitter and sight it through a phone camera. The TSAL6100s are 940 nm — fainter to a rear camera than the old 850 nm parts, so use the front one.")
                .font(.callout)
                .foregroundStyle(.secondary)

            // Emitter hold toggles through the sensors on the mouse; the app
            // just presses the key and reads back which one she lit from EMIT.
            HStack(spacing: 8) {
                Button {
                    session.send(.action(.emitterHold))
                } label: {
                    Label("Emitter hold", systemImage: "flashlight.on.fill")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .touchTarget()
            }

            HStack {
                ForEach(E4Sensor.allCases) { sensor in
                    let lit = session.litEmitter == sensor.rawValue
                    Text(sensor.label)
                        .font(.callout.monospaced().weight(.semibold))
                        .frame(maxWidth: .infinity)
                        .touchTarget()
                        .background(
                            (lit ? Palette.warn : Palette.faint).opacity(lit ? 0.13 : 0.07),
                            in: RoundedRectangle(cornerRadius: 8)
                        )
                        .foregroundStyle(lit ? Palette.warn : Palette.faint)
                }
            }

            Text(session.litEmitter.map { "\(E4Sensor(rawValue: $0)?.label ?? "?") held at 50% duty — safe indefinitely" }
                 ?? "All emitters off")
                .font(.caption.monospaced())
                .foregroundStyle(session.litEmitter == nil ? Palette.faint : Palette.warn)
        }
    }

    // MARK: thresholds

    private var thresholds: some View {
        let running = session.runningAction == .thresholdCal
        return Card {
            HStack(spacing: 9) {
                Text("THRESHOLDS")
                    .font(.caption2.monospaced())
                    .tracking(1.2)
                    .foregroundStyle(.secondary)
                Spacer()
                if running {
                    Button("Exit") { session.send(.back) }
                        .buttonStyle(.bordered)
                        .touchTarget()
                } else {
                    Button("Calibrate") { session.send(.action(.thresholdCal)) }
                        .buttonStyle(.borderedProminent)
                        .touchTarget()
                }
            }

            Text("Three captures: a dead end (walls both sides and in front), open floor with nothing in range, and a corridor (side walls, no front wall). The corridor is the one that matters most — the forward pair clips the side walls through its splay, so without it the front threshold can land below what a corridor reads and she reports a front wall in every one.")
                .font(.callout)
                .foregroundStyle(.secondary)

            if let status = session.sensorStatus {
                VStack(spacing: 6) {
                    FieldRow(key: "left (SL)", value: "\(status.leftThreshold)")
                    FieldRow(key: "right (SR)", value: "\(status.rightThreshold)")
                    FieldRow(key: "front sum", value: "\(status.frontThreshold)")
                    if status.sidesShareOneThreshold {
                        // Either a pre-0.11 log, or both sides genuinely landed
                        // on the same number. Worth flagging rather than hiding:
                        // identical side thresholds on a hand-built pair is a
                        // coincidence, and coincidences are worth checking.
                        FieldRow(key: "note", value: "sides equal", tint: Palette.dim)
                    }
                }
            }

            if !cal.margins.isEmpty {
                Divider()
                Text("MARGIN")
                    .font(.caption2.monospaced())
                    .tracking(1.2)
                    .foregroundStyle(.secondary)

                // The gap between the weakest reading with the wall there and
                // the strongest without it. This, not the threshold, is what
                // says whether the sensor can separate the two states at all.
                ForEach(cal.margins, id: \.channel.rawValue) { margin in
                    HStack(spacing: 9) {
                        Text(margin.channel.rawValue)
                            .font(.callout.monospaced().weight(.semibold))
                            .frame(width: 16, alignment: .leading)
                        Text("\(margin.absent) → \(margin.present)")
                            .font(.callout.monospaced())
                            .monospacedDigit()
                            .foregroundStyle(Palette.dim)
                        Spacer()
                        Text(margin.verdict.rawValue)
                            .font(.caption.monospaced().weight(.semibold))
                            .foregroundStyle(verdictTint(margin.verdict))
                    }
                }

                if let bound = cal.frontBound {
                    Text("Front had to clear \(bound.corridor) — what a corridor reads with no front wall — rather than the \(bound.floor) of open floor.")
                        .font(.caption)
                        .foregroundStyle(Palette.dim)
                }
            }

            if let proposed = cal.proposed {
                Divider()
                HStack(spacing: 10) {
                    Text("proposed \(proposed.left) / \(proposed.right) / \(proposed.front)")
                        .font(.callout.monospaced())
                        .monospacedDigit()
                    Spacer()
                    Button("Save") { session.send(.key("S")) }
                        .buttonStyle(.borderedProminent)
                        .touchTarget()
                }
            }

            if let saved = cal.lastSaveSucceeded {
                Text(saved ? "Saved to EEPROM" : "Save failed — no EEPROM, or the write did not verify")
                    .font(.caption)
                    .foregroundStyle(saved ? Palette.good : Palette.bad)
            }

            ForEach(cal.problems, id: \.self) { problem in
                Text(problem)
                    .font(.caption)
                    .foregroundStyle(Palette.warn)
            }
        }
    }

    private func verdictTint(_ v: E4ThresholdMargin.Verdict) -> Color {
        switch v {
        case .good: return Palette.good
        case .ok:   return Palette.dim
        case .poor: return Palette.warn
        case .dead: return Palette.bad
        }
    }
}
