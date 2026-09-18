import Foundation
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
    @State private var arcEntryOffset = 100.0
    @State private var arcLeadOut = 90.0

    /// Whole cells of approach before the turn's own cell. The lead-in is
    /// derived from it, not typed: back against the wall she is 49 mm short of
    /// the first cell centre, each cell is 180, and the arc has to begin
    /// `entry offset` before the centre of the cell the turn belongs to.
    @State private var approachCells = 1

    /// Results accumulate here for the session. The firmware only ever reports
    /// the latest one, so comparing a change against the run before it means
    /// keeping them ourselves.
    @State private var results: [Row] = []
    @State private var selectedTurn = 1        // SS90ER, the usual one to test

    /// Names come from her dump when we have one, so the picker cannot drift
    /// from the firmware's table. Falls back to indices before the first CFG?.
    private var turnChoices: [(Int, String)] {
        let dumped = session.configDump.turns
        guard !dumped.isEmpty else { return (0..<16).map { ($0, "turn \($0)") } }
        return dumped.map { ($0.index, "\($0.index)  \($0.name)") }
    }

    private struct Row: Identifiable {
        let id = UUID()
        let at = Date()
        let result: E4TurnResult
    }

    private var active: Bool { session.runningAction == .turnTuning }

    // Geometry of the arc the fields currently describe -- not of the row she
    // is holding. These are what you are about to send, which is the thing
    // worth checking before anything moves.
    private var radius: Double {
        guard arcOmega != 0 else { return 0 }
        return arcVelocity / (arcOmega * .pi / 180)
    }
    /// An arc of radius R joining two lanes that cross at theta must begin
    /// R*tan(theta/2) before the crossing and end the same distance after.
    private var tangent: Double {
        let theta = abs(arcAngle) * .pi / 180
        guard theta > 0, theta < .pi else { return 0 }
        return radius * tan(theta / 2)
    }
    private var tangentError: Double { arcEntryOffset - tangent }

    /// DS45, DS135 and DD90 begin ON the diagonal: the pitch there is 127.279
    /// mm rather than 180, and there is no back wall to square up on because
    /// she is placed on the diagonal line by hand. The firmware works this out
    /// from the row too, so the two agree without either being told.
    private var entersOnDiagonal: Bool { [8, 9, 12, 13, 14, 15].contains(selectedTurn) }
    private var approachPitch: Double { entersOnDiagonal ? 127.279 : 180 }

    private var derivedLeadIn: Double {
        guard approachCells > 0 else { return arcEntryOffset }
        let base = entersOnDiagonal ? 0.0 : 49.0
        return max(0, base + approachPitch * Double(approachCells) - arcEntryOffset)
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                gate
                spinCard
                arcCard
                resultsCard
                configCard
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

            // ARC writes the SELECTED row of her live table, so which row is
            // selected is part of the command, not a detail. Sixteen exist;
            // twelve of them nothing drives yet.
            HStack(spacing: 8) {
                Text("editing")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Picker("Turn", selection: $selectedTurn) {
                    ForEach(turnChoices, id: \.0) { index, label in
                        Text(label).tag(index)
                    }
                }
                .labelsHidden()
                .pickerStyle(.menu)
                .frame(maxWidth: 190)
                .onChange(of: selectedTurn) { session.send(.selectTurn(selectedTurn)) }
                Spacer()
            }
            .disabled(!active)
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 120), spacing: 9)], spacing: 9) {
                NumberField("velocity", value: $arcVelocity, unit: "mm/s")
                NumberField("angle", value: $arcAngle, unit: "deg")
                NumberField("omega", value: $arcOmega, unit: "deg/s")
                NumberField("alpha", value: $arcAlpha, unit: "deg/s²")
                NumberField("entry offset", value: $arcEntryOffset, unit: "mm")
                NumberField("lead out", value: $arcLeadOut, unit: "mm")
            }
            approachRow
            geometryRow
            HStack(spacing: 8) {
                Button("Run arc") {
                    // She computes the lead-in at the moment of the run from
                    // whatever entry offset is live, so POS only has to arrive
                    // before ARC does -- and ARC re-reports the derived lead-in
                    // once it has written the new offset.
                    session.send(.tuneApproach(cells: approachCells))
                    session.send(.arc(velocity: arcVelocity, angle: arcAngle, omega: arcOmega,
                                      alpha: arcAlpha, leadIn: arcEntryOffset, leadOut: arcLeadOut))
                }
                .buttonStyle(.borderedProminent)
                .frame(maxWidth: .infinity)

                Button("Repeat") { session.send(.key("r")) }
                    .buttonStyle(.bordered)

                Button("Save to EEPROM") { session.send(.saveTuning) }
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
                        // The row and the approach: three runs back you will
                        // not remember which turn was selected or how far she
                        // ran into it, and that is exactly when it matters.
                        Text(row.result.row.map { "row \($0)" } ?? "")
                            .foregroundStyle(.tertiary)
                        Text(row.result.leadIn.map { "lead \(Int($0))" } ?? "")
                            .foregroundStyle(.tertiary)
                        Spacer()
                        Text(row.result.distance.map { "\(Int($0)) mm" } ?? "—")
                            .monospacedDigit()
                    }
                    .font(.caption.monospaced())
                }
            }
        }
    }

    // MARK: config dump

    /// What she is ACTUALLY holding, as opposed to what the fields above say.
    ///
    /// Those fields are what you are about to send; this is what she has. They
    /// diverge the moment anyone edits a number without pressing Run, and the
    /// difference is invisible without asking her.
    private var configCard: some View {
        let dump = session.configDump
        return Card {
            HStack(spacing: 9) {
                Text("HER CONFIGURATION")
                    .font(.caption2.monospaced())
                    .tracking(1.2)
                    .foregroundStyle(.secondary)
                Spacer()
                Button("Read from mouse") { session.send(.readConfig) }
                    .buttonStyle(.bordered)
                    .touchTarget()
                    .disabled(!session.connection.isConnected)
            }

            if dump.isComplete {
                HStack(spacing: 14) {
                    FieldRow(key: "block", value: dump.blockLoaded == true
                             ? "v\(dump.version ?? 0) from EEPROM" : "compiled defaults",
                             tint: dump.blockLoaded == true ? Palette.good : Palette.warn)
                }
                if dump.eepromPresent == false {
                    Text("No EEPROM answering — nothing you tune will survive a power cycle.")
                        .font(.caption)
                        .foregroundStyle(Palette.bad)
                }

                Divider()

                ForEach(dump.turns) { turn in
                    HStack(spacing: 10) {
                        Text(turn.name)
                            .font(.caption.monospaced().weight(.semibold))
                            .frame(width: 62, alignment: .leading)
                            .foregroundStyle(turn.isDriven ? Palette.ink : Palette.faint)
                        Text("\(turn.angle)°")
                            .font(.caption.monospaced())
                            .frame(width: 44, alignment: .trailing)
                            .foregroundStyle(Palette.dim)
                        Text("R\(Int(turn.radiusMM))")
                            .font(.caption.monospaced())
                            .frame(width: 44, alignment: .trailing)
                            .foregroundStyle(Palette.dim)
                        Text("in \(turn.entryOffset)  ex \(turn.exitOffset)  out \(turn.leadOut)")
                            .font(.system(size: 10, design: .monospaced))
                            .foregroundStyle(Palette.faint)
                        Spacer()
                        if !turn.isDriven {
                            Text("slot")
                                .font(.system(size: 9, design: .monospaced))
                                .foregroundStyle(Palette.faint)
                        }
                    }
                }

                Text("Radius is derived, not stored: R = v / ω. Rows marked “slot” are reachable by the tuner but nothing drives them — there is no diagonal navigation yet.")
                    .font(.caption)
                    .foregroundStyle(Palette.faint)
            } else {
                Text("Ask her what she is holding — gyro scale, thresholds, spin dynamics and all sixteen turns, with whether each came from the EEPROM or from a compiled default.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }
        }
    }

    /// Cells of approach, and the lead-in that falls out of it. She reports
    /// the lead-in back, so when the two disagree the fields have drifted from
    /// what she is holding and Run is what reconciles them.
    private var approachRow: some View {
        HStack(spacing: 12) {
            Text("approach")
                .font(.system(size: 10, design: .monospaced))
                .foregroundStyle(.secondary)
            Stepper(value: $approachCells, in: 0...6) {
                Text(approachCells == 0
                     ? "bare arc"
                     : "\(approachCells) \(entersOnDiagonal ? "diagonal step" : "cell")\(approachCells == 1 ? "" : "s")")
                    .font(.callout.monospaced())
            }
            .frame(maxWidth: 190)
            .onChange(of: approachCells) { session.send(.tuneApproach(cells: approachCells)) }

            VStack(alignment: .leading, spacing: 1) {
                Text("\(Int(derivedLeadIn)) mm")
                    .font(.callout.monospaced())
                    .monospacedDigit()
                Text(session.tuneLeadIn.map { her in
                    abs(her - derivedLeadIn) < 1 ? "she agrees" : "she has \(Int(her))"
                } ?? "lead-in")
                .font(.system(size: 9.5, design: .monospaced))
                .foregroundStyle(session.tuneLeadIn.map {
                    abs($0 - derivedLeadIn) < 1.5 ? Palette.faint : Palette.warn
                } ?? Palette.faint)
            }
            if entersOnDiagonal {
                Text("on the diagonal — 127.3 mm a step, placed by hand")
                    .font(.system(size: 9.5, design: .monospaced))
                    .foregroundStyle(Palette.faint)
            }
            Spacer()
        }
        .disabled(!active)
    }

    /// The arithmetic check, done before she moves rather than after.
    ///
    /// R = v / omega and t = R tan(theta/2) are facts about circles. If the
    /// entry offset disagrees with t, the arc cannot be tangent to both lanes
    /// whatever the floor does, and no amount of driving will tell you which of
    /// the two numbers to believe until they agree.
    private var geometryRow: some View {
        let off = abs(tangentError)
        let tint: Color = off < 5 ? Palette.good : off < 20 ? Palette.warn : Palette.bad
        return VStack(alignment: .leading, spacing: 4) {
            HStack(spacing: 16) {
                stat("R = v/w", radius, " mm", Palette.dim)
                stat("tangent", tangent, " mm", tint)
                stat("entry", arcEntryOffset, " mm", Palette.ink)
                stat("out by", tangentError, " mm", tint)
                Spacer()
                if off >= 5 {
                    Button("use tangent") { arcEntryOffset = (tangent * 10).rounded() / 10 }
                        .buttonStyle(.bordered)
                        .controlSize(.small)
                }
            }
            if off >= 5 {
                Text("An arc this size cannot meet both lanes at an entry offset of \(Int(arcEntryOffset)) — it wants \(Int(tangent)). Either omega is wrong or the offset is; the floor cannot tell you which until they agree.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
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
