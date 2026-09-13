import SwiftUI
import E4Core

struct SensorsScreen: View {
    @Environment(E4Session.self) private var session

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                readout
                aiming
                thresholds
            }
            .padding(18)
            .frame(maxWidth: 900)
            .frame(maxWidth: .infinity)
        }
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

            if running {
                Divider()
                // Each button forces its own state, so any one can be redone
                // without starting over — which matters, because repositioning
                // her for the third capture is exactly when the first one gets
                // knocked.
                HStack(spacing: 8) {
                    Button("Dead end") { session.send(.key("P")) }
                        .buttonStyle(.bordered)
                        .touchTarget()
                    Button("Open floor") { session.send(.key("A")) }
                        .buttonStyle(.bordered)
                        .touchTarget()
                    Button("Corridor") { session.send(.key("C")) }
                        .buttonStyle(.bordered)
                        .touchTarget()
                    Button("Save") { session.send(.key("S")) }
                        .buttonStyle(.borderedProminent)
                        .touchTarget()
                    Spacer()
                }
                Text("She reports a margin per channel — the gap between the weakest reading with the wall there and the strongest without it. That figure, not the threshold, is what says whether the sensor can separate the two states at all.")
                    .font(.caption)
                    .foregroundStyle(Palette.faint)
            }
        }
    }
}
