import SwiftUI
import E4Core

/// Gyro scale calibration, and the state of the EEPROM block it saves into.
///
/// The flow is semi-automatic by necessity, not by laziness: the spin is
/// gyro-closed, so she always believes she turned exactly what she was told.
/// The shortfall has to be read off the floor by a person and handed back.
struct CalibrationScreen: View {
    @Environment(E4Session.self) private var session

    @State private var shortfall = 0.0
    @State private var manualScale = 1.0

    private var active: Bool { session.runningAction == .gyroScaleCal }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                gate
                steps
                manual
                config
                scrub
            }
            .padding(18)
            .frame(maxWidth: 900)
            .frame(maxWidth: .infinity)
        }
        .onAppear { manualScale = session.gyroScale ?? 1.003 }
        .onChange(of: session.gyroScale) {
            if let s = session.gyroScale { manualScale = s }
        }
    }

    private var gate: some View {
        Card {
            HStack(alignment: .firstTextBaseline) {
                Text("Gyro scale")
                    .font(.title3.weight(.semibold))
                Text(session.gyroScale.map { String(format: "%.3f", $0) } ?? "—")
                    .font(.title3.monospaced())
                    .foregroundStyle(Palette.accent)
                Spacer()
                if active {
                    Button("Exit") { session.send(.back) }
                        .buttonStyle(.bordered)
                        .touchTarget()
                } else {
                    Button("Start calibration") { session.send(.action(.gyroScaleCal)) }
                        .buttonStyle(.borderedProminent)
                        .touchTarget()
                }
            }

            Text("It is a property of the individual MPU-9250 and it shifts with temperature, so it is worth re-measuring at a venue rather than trusting the number compiled in.")
                .font(.callout)
                .foregroundStyle(.secondary)
        }
    }

    private var steps: some View {
        Card("Procedure") {
            step(1, "Line her up",
                 "Against a straight edge on the floor, so the total turn can be read back against it.")
            step(2, "Eight 90° spins",
                 "720° in total — enough for a small scale error to become visible. Press Run.",
                 trailing: {
                     Button("Run") { session.send(.key("G")) }
                         .buttonStyle(.bordered)
                         .touchTarget()
                         .disabled(!active)
                 })
            step(3, "Measure the shortfall",
                 "How many degrees short of the straight edge did she finish? This is the number she cannot know herself.",
                 trailing: {
                     HStack(spacing: 6) {
                         NumberField("short", value: $shortfall, unit: "deg")
                             .frame(width: 108)
                         Button("Send") { session.send(.gyroError(degrees: shortfall)) }
                             .buttonStyle(.bordered)
                             .touchTarget()
                             .disabled(!active)
                     }
                 })
            step(4, "Save",
                 "new = old × (cmd − short) ÷ cmd, guarded to 0.85–1.15, then written to the EEPROM block.",
                 trailing: {
                     Button("Save") { session.send(.key("S")) }
                         .buttonStyle(.borderedProminent)
                         .touchTarget()
                         .disabled(!active)
                 })

            if let report = session.lastGyroCal {
                Divider()
                Text(report.raw)
                    .font(.caption.monospaced())
                    .foregroundStyle(report.rejected ? Palette.bad : Palette.dim)
                    .textSelection(.enabled)
            }
        }
    }

    private var manual: some View {
        Card("Set directly") {
            Text("If you already know the figure — from a previous session, or another mouse's — skip the spins and set it.")
                .font(.callout)
                .foregroundStyle(.secondary)
            HStack(spacing: 8) {
                NumberField("scale", value: $manualScale, unit: "")
                    .frame(width: 140)
                Button("Set") { session.send(.gyroScale(manualScale)) }
                    .buttonStyle(.bordered)
                    .touchTarget()
                    .disabled(!active)
                Button("Save") { session.send(.key("S")) }
                    .buttonStyle(.bordered)
                    .touchTarget()
                    .disabled(!active)
                Spacer()
            }
        }
    }

    private var config: some View {
        Card("EEPROM config") {
            FieldRow(key: "address", value: "512")
            FieldRow(key: "payload", value: "gyro_scale")
            if let scale = session.gyroScale {
                FieldRow(key: "loaded", value: String(format: "%.3f", scale))
            }
            Divider()
            Text("A board with no EEPROM is not an error: the compiled defaults stand and saving reports failure. The fitted display board has none — only 0x3C answers — so nothing persists until the newer board is in.")
                .font(.caption)
                .foregroundStyle(.secondary)
            Button("EEPROM test") { session.send(.action(.eepromTest)) }
                .buttonStyle(.bordered)
                .touchTarget()
        }
    }

    private var scrub: some View {
        Card("Scrub, measured") {
            HStack(alignment: .firstTextBaseline, spacing: 7) {
                Text("3.1").font(.system(size: 26, design: .monospaced))
                Text("mm per 90°").font(.callout).foregroundStyle(.secondary)
                Spacer()
            }
            Text("25 mm over eight spins, measured on 11 Sep. Two wheels a side off one pinion cannot pivot cleanly about the axle cross — this is the budget the side-sensor centring has to absorb, not a fault to fix in firmware.")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
    }

    @ViewBuilder
    private func step<T: View>(_ n: Int, _ title: String, _ body: String,
                               @ViewBuilder trailing: () -> T = { EmptyView() }) -> some View {
        HStack(alignment: .top, spacing: 12) {
            Text("\(n)")
                .font(.caption.monospaced().weight(.semibold))
                .frame(width: 22, height: 22)
                .background(Color.secondary.opacity(0.15), in: Circle())
            VStack(alignment: .leading, spacing: 3) {
                Text(title).font(.callout.weight(.semibold))
                Text(body).font(.caption).foregroundStyle(.secondary)
            }
            Spacer(minLength: 8)
            trailing()
        }
        .padding(.vertical, 3)
    }
}
