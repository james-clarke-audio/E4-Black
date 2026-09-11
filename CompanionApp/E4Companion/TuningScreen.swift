import SwiftUI
import E4Core

/// Drives the firmware's turn tuner.
///
/// The tuner is **modal**: `SPIN` and `ARC` are only read while action 19 is
/// running, and outside it the firmware drops the line without complaint. So
/// the screen gates on `session.runningAction`, which comes from her own
/// `RUN`/`DONE` reports rather than from what we think we sent.
struct TuningScreen: View {
    @Environment(E4Session.self) private var session

    // Defaults lifted from mouse_config.h and the tuner's own starting values.
    @State private var spinAngle = 90.0
    @State private var spinOmega = 360.0
    @State private var spinAlpha = 3600.0

    @State private var arcVelocity = 300.0
    @State private var arcAngle = -90.0
    @State private var arcOmega = 170.0
    @State private var arcAlpha = 2500.0
    @State private var arcLeadIn = 100.0
    @State private var arcLeadOut = 30.0

    /// Results accumulate here for the session. The firmware only ever reports
    /// the latest one, so comparing a change against the run before it means
    /// keeping them ourselves.
    @State private var results: [Row] = []

    private struct Row: Identifiable {
        let id = UUID()
        let at = Date()
        let result: E4TurnResult
    }

    private var active: Bool { session.runningAction == .turnTuning }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                gate
                spinCard
                arcCard
                resultsCard
            }
            .padding(18)
            .frame(maxWidth: 900)
            .frame(maxWidth: .infinity)
        }
        .onChange(of: session.lastTurnResult) {
            if let r = session.lastTurnResult { results.insert(Row(result: r), at: 0) }
        }
    }

    private var gate: some View {
        Card {
            HStack(spacing: 12) {
                Image(systemName: active ? "dot.radiowaves.left.and.right" : "pause.circle")
                    .foregroundStyle(active ? Palette.warn : Palette.faint)
                VStack(alignment: .leading, spacing: 3) {
                    Text(active ? "Tuner running" : "Tuner not running")
                        .font(.callout.weight(.semibold))
                    Text(active
                         ? "She is waiting for a turn. Repeat re-runs the last one."
                         : "Spin and arc commands are ignored outside the tuner — enter it first.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                Spacer()
                if active {
                    Button("Exit") { session.send(.back) }
                        .buttonStyle(.bordered)
                        .touchTarget()
                } else {
                    Button("Enter tuner") { session.send(.action(.turnTuning)) }
                        .buttonStyle(.borderedProminent)
                        .touchTarget()
                }
            }
        }
    }

    private var spinCard: some View {
        Card {
            header("In-place spin", note: "scrubs ~3 mm per 90° — reserve for 180s")
            HStack(spacing: 9) {
                NumberField("angle", value: $spinAngle, unit: "deg")
                NumberField("omega", value: $spinOmega, unit: "deg/s")
                NumberField("alpha", value: $spinAlpha, unit: "deg/s²")
            }
            Button("Run spin") {
                session.send(.spin(angle: spinAngle, omega: spinOmega, alpha: spinAlpha))
            }
            .buttonStyle(.borderedProminent)
            .frame(maxWidth: .infinity)
            .touchTarget()
            .disabled(!active)
        }
    }

    private var arcCard: some View {
        Card {
            header("Arc turn", note: "wheels roll — far less scrub")
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 120), spacing: 9)], spacing: 9) {
                NumberField("velocity", value: $arcVelocity, unit: "mm/s")
                NumberField("angle", value: $arcAngle, unit: "deg")
                NumberField("omega", value: $arcOmega, unit: "deg/s")
                NumberField("alpha", value: $arcAlpha, unit: "deg/s²")
                NumberField("lead in", value: $arcLeadIn, unit: "mm")
                NumberField("lead out", value: $arcLeadOut, unit: "mm")
            }
            HStack(spacing: 8) {
                Button("Run arc") {
                    session.send(.arc(velocity: arcVelocity, angle: arcAngle, omega: arcOmega,
                                      alpha: arcAlpha, leadIn: arcLeadIn, leadOut: arcLeadOut))
                }
                .buttonStyle(.borderedProminent)
                .frame(maxWidth: .infinity)

                Button("Repeat") { session.send(.key("r")) }
                    .buttonStyle(.bordered)
            }
            .touchTarget()
            .disabled(!active)
        }
    }

    private var resultsCard: some View {
        Card("Results") {
            if let latest = session.lastTurnResult {
                HStack(spacing: 16) {
                    stat("cmd", latest.commanded, "°", Palette.ink)
                    stat("gyro", latest.gyro, "°", Palette.faint)
                    stat("dist", latest.distance, " mm", Palette.warn)
                    Spacer()
                }
                Text("The turn is gyro-closed, so gyro always matches cmd. What tells you anything is dist — the translation left behind by the scrub.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            } else {
                Text("No turns run yet this session.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }

            if !results.isEmpty {
                Divider()
                ForEach(results.prefix(12)) { row in
                    HStack(spacing: 10) {
                        Text(row.at, format: .dateTime.hour().minute())
                            .foregroundStyle(.tertiary)
                            .frame(width: 46, alignment: .leading)
                        Text(row.result.kind.rawValue)
                            .foregroundStyle(.secondary)
                            .frame(width: 38, alignment: .leading)
                        Text(row.result.commanded.map { "\(Int($0))°" } ?? "—")
                            .foregroundStyle(.secondary)
                        Spacer()
                        Text(row.result.distance.map { "\(Int($0)) mm" } ?? "—")
                            .monospacedDigit()
                    }
                    .font(.caption.monospaced())
                }
            }
        }
    }

    // MARK: bits

    private func header(_ title: String, note: String) -> some View {
        HStack(alignment: .firstTextBaseline, spacing: 9) {
            Text(title).font(.headline)
            Text(note).font(.caption).foregroundStyle(.secondary)
            Spacer()
        }
    }

    private func stat(_ key: String, _ value: Double?, _ unit: String, _ tint: Color) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(value.map { "\(Int($0))\(unit)" } ?? "—")
                .font(.title2.monospaced())
                .monospacedDigit()
                .foregroundStyle(tint)
            Text(key)
                .font(.system(size: 9.5, design: .monospaced))
                .foregroundStyle(.tertiary)
        }
    }
}

/// A labelled numeric field. Kept at 44pt so it is usable with a finger.
struct NumberField: View {
    let label: String
    @Binding var value: Double
    let unit: String

    init(_ label: String, value: Binding<Double>, unit: String) {
        self.label = label
        self._value = value
        self.unit = unit
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(label)
                .font(.system(size: 10, design: .monospaced))
                .foregroundStyle(.secondary)
            HStack(spacing: 4) {
                TextField(label, value: $value, format: .number)
                    .textFieldStyle(.plain)
                    .font(.callout.monospaced())
                    .monospacedDigit()
                    #if os(iOS)
                    .keyboardType(.numbersAndPunctuation)
                    #endif
                Text(unit)
                    .font(.system(size: 10, design: .monospaced))
                    .foregroundStyle(.tertiary)
            }
            .padding(.horizontal, 10)
            .frame(minHeight: 44)
            .background(Color.primary.opacity(0.05), in: RoundedRectangle(cornerRadius: 8))
        }
    }
}
