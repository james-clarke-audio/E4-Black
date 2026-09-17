import SwiftUI
import E4Core

/// The full menu, grouped the way the firmware groups it.
///
/// Everything here is also reachable from the mouse's own two-button menu —
/// this is the same tree, with a keyboard's worth of shortcuts and no scrolling
/// through categories.
struct ActionsScreen: View {
    @Environment(E4Session.self) private var session
    @State private var pending: E4MenuAction?

    // The zigzag form. Local so it can be edited freely, seeded from what she
    // is actually holding so the two never silently disagree.
    @State private var zigTurns = 3
    @State private var zigSpin = false
    @State private var zigRight = true
    @State private var zigSpeedRow = true

    private struct Group: Identifiable {
        let id = UUID()
        let name: String
        let actions: [E4MenuAction]
    }

    // Mirrors CAT_* in app_main.cpp.
    //
    // This list is hand-kept, so it drifts: an action added to E4MenuAction and
    // not added here simply does not appear, with nothing to say so. That has
    // already happened once. `groups` below therefore appends an "Other"
    // catch-all for anything unplaced, so the failure mode is an item in the
    // wrong section rather than an item you cannot reach.
    private static let curated: [Group] = [
        .init(name: "Calibration", actions: [.recalGyro, .gyroScaleCal, .irMonitor, .turnTuning, .motionTest]),
        .init(name: "Moves", actions: [.forward180, .right90, .left90, .spin180]),
        .init(name: "In-maze bench", actions: [.setMazeSize, .setGoal, .search]),
        .init(name: "Simulation", actions: [.simulate, .simExplore, .recallMaze]),
        .init(name: "Diagnostics", actions: [.eepromTest, .sensorMode, .irSampler, .resetPose,
                                             .testMode, .setBT57k, .btProvision, .emitterHold,
                                             .firmwareVersion]),
        .init(name: "Wall follow", actions: [.wallFollowLeft, .wallFollowRight,
                                            .simFollowLeft, .simFollowRight]),
        .init(name: "Competition", actions: [.explore, .planRoute, .speedRun, .resumeSaved, .runOptions]),
    ]

    /// The curated groups, plus anything E4MenuAction gained that nobody placed.
    /// thresholdCal is deliberately absent from the grid — it has its own
    /// screen, and a duplicate button that skips the guided flow is worse than
    /// no button.
    /// zigzagTest joins it: it has a panel of its own above, with the settings
    /// that decide what its answer means. A grid button beside that panel would
    /// drive the test with whatever was last configured, which is the one way
    /// to get a number you cannot interpret.
    private static let placedElsewhere: Set<E4MenuAction> = [.thresholdCal, .zigzagTest]

    private static let groups: [Group] = {
        let placed = Set(Self.curated.flatMap(\.actions)).union(Self.placedElsewhere)
        let missing = E4MenuAction.allCases.filter { !placed.contains($0) }
        return missing.isEmpty ? Self.curated
                               : Self.curated + [.init(name: "Other", actions: missing)]
    }()

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                navPad
                zigzagCard
                ForEach(Self.groups) { group in
                    Card(group.name) {
                        LazyVGrid(columns: [GridItem(.adaptive(minimum: 168), spacing: 8)], spacing: 8) {
                            ForEach(group.actions) { action in
                                button(for: action)
                            }
                        }
                        if group.actions.contains(where: \.requiresNoCentral) {
                            Label("Greyed items are on the mouse only — the Bluetooth module ignores configuration commands while this app is connected.",
                                  systemImage: "info.circle")
                                .font(.caption)
                                .foregroundStyle(.tertiary)
                        }
                    }
                }
            }
            .padding(18)
            .frame(maxWidth: 900)
            .frame(maxWidth: .infinity)
        }
        .confirmationDialog(
            pending.map { "Run \($0.title)?" } ?? "",
            isPresented: .init(get: { pending != nil }, set: { if !$0 { pending = nil } }),
            titleVisibility: .visible
        ) {
            Button("Run", role: .destructive) {
                if let action = pending { session.send(.action(action)) }
                pending = nil
            }
            Button("Cancel", role: .cancel) { pending = nil }
        } message: {
            Text("This moves the mouse. Make sure she has room.")
        }
    }

    /// The three-level menu as it appears on the OLED. Useful when you want to
    /// drive what she is showing rather than jump straight to an action.
    /// She prints RUN before the action runs, and the action's first act is to
    /// wait for a button — so between those two she is armed, not going. Once
    /// she actually moves she reports a STATE, which is what clears this.
    private var isArmedWaiting: Bool {
        guard let running = session.runningAction, running.waitsForButtonPress else { return false }
        let state = session.stateLabel?.uppercased()
        return state == nil || state == "IDLE"
    }

    private var navPad: some View {
        Card("Menu") {
            HStack(spacing: 8) {
                Button("Prev") { session.send(.previous) }
                Button("Enter") { session.send(.enter) }
                Button("Next") { session.send(.next) }
                Button("Back") { session.send(.back) }
            }
            .buttonStyle(.bordered)
            .touchTarget()

            // "Running" is not the same as "waiting for you", and the
            // difference matters: several actions print RUN and then block on a
            // press on HER. Showing only "Running" for that made a deliberate
            // pause look like a dropped command.
            if let running = session.runningAction, running.waitsForButtonPress, isArmedWaiting {
                Label("**\(running.title)** is armed — press a button on the mouse to start it.",
                      systemImage: "hand.tap.fill")
                    .font(.callout)
                    .foregroundStyle(Palette.warn)
                    .padding(9)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .background(Palette.warn.opacity(0.10), in: RoundedRectangle(cornerRadius: 7))
            } else if let name = session.runningActionName {
                Label("Running: \(name)", systemImage: "play.circle.fill")
                    .font(.callout)
                    .foregroundStyle(.orange)
            }
        }
    }

    /// The chained-turn test, with its settings.
    ///
    /// It has a panel of its own rather than a button in the grid because the
    /// answer depends entirely on how it was set up: which row, how many turns,
    /// and arcs against stop-and-spin. A bare button would drive whatever was
    /// last configured — possibly from the mouse's own menu, possibly from
    /// another app — and report a number you could not interpret.
    private var zigzagCard: some View {
        Card("Zigzag test") {
            Text("Alternating 90s in consecutive cells, with no straight between them. A turn that chains is a quarter circle of radius 90 mm — half a cell — and hers swings about 111. Needs a 3×3 open section for three turns.")
                .font(.caption)
                .foregroundStyle(Palette.faint)

            Stepper("Turns: \(zigTurns)", value: $zigTurns, in: 1...8)
                .font(.callout)

            Picker("", selection: $zigSpin) {
                Text("Chained arcs").tag(false)
                Text("Stop and spin").tag(true)
            }
            .pickerStyle(.segmented)
            .labelsHidden()

            Picker("", selection: $zigSpeedRow) {
                Text("SS90 · speed run").tag(true)
                Text("SS90E · search").tag(false)
            }
            .pickerStyle(.segmented)
            .labelsHidden()

            Picker("", selection: $zigRight) {
                Text("First right").tag(true)
                Text("First left").tag(false)
            }
            .pickerStyle(.segmented)
            .labelsHidden()

            HStack(spacing: 8) {
                Button("Send setup") { sendZigSetup() }
                    .buttonStyle(.bordered)
                Button {
                    // Settings first, then the action — so what she drives is
                    // what the form says, not whatever she was last told.
                    sendZigSetup()
                    pending = .zigzagTest
                } label: {
                    Label("Run", systemImage: "figure.walk.motion")
                }
                .buttonStyle(.borderedProminent)
            }
            .touchTarget()

            zigzagReadout
        }
        .onAppear { seedZig(from: session.zigzag.setup) }
        .onChange(of: session.zigzag.setup) { _, new in seedZig(from: new) }
    }

    private func sendZigSetup() {
        session.send(.zigzagSetup(turns: zigTurns, spin: zigSpin,
                                  firstRight: zigRight, speedRunRow: zigSpeedRow))
    }

    private func seedZig(from s: E4ZigzagSetup) {
        zigTurns = s.turns
        zigSpin = s.spin
        zigRight = s.firstRight
        zigSpeedRow = s.speedRunRow
    }

    @ViewBuilder
    private var zigzagReadout: some View {
        let z = session.zigzag
        if z.running || !z.turns.isEmpty || z.result != nil {
            Divider()
            if let v = z.setup.speed {
                Text("\(z.setup.rowName) at \(v) mm/s · \(z.setup.spin ? "stop and spin" : "chained arcs")")
                    .font(.caption.monospaced())
                    .foregroundStyle(Palette.dim)
            }
            ForEach(z.turns) { t in
                HStack {
                    Text("turn \(t.index) \(t.right ? "R" : "L")").foregroundStyle(Palette.dim)
                    Spacer()
                    Text("gyro \(t.gyro)°  pos \(t.position)").foregroundStyle(Palette.ink)
                }
                .font(.caption.monospaced())
                .monospacedDigit()
            }
            if let r = z.result {
                // The heading error is the headline: alternating 90s cancel in
                // pairs, so anything left over is turn error accumulating.
                HStack {
                    Text("error").foregroundStyle(Palette.dim)
                    Spacer()
                    Text("\(r.error)° off \(r.expected)°")
                        .foregroundStyle(abs(r.error) > 5 ? Palette.bad : Palette.good)
                    Text("· \(r.distance) mm").foregroundStyle(Palette.faint)
                }
                .font(.caption.monospaced())
                .monospacedDigit()
                Text("Heading is only half of it — the lateral offset is what says whether the arcs chained. Measure her against the wall of the cell she stopped in.")
                    .font(.caption)
                    .foregroundStyle(Palette.faint)
            } else if z.running {
                Text("Running…").font(.caption).foregroundStyle(Palette.warn)
            }
        }
    }

    @ViewBuilder
    private func button(for action: E4MenuAction) -> some View {
        if action.requiresNoCentral {
            // Not a button. Offering one that cannot work is worse than not
            // offering it: the firmware would send AT down a UART in
            // transparent mode, the text would land in the log as garbage, and
            // the sweep would report the module silent. This has to be done
            // from her own menu, with nothing connected.
            HStack(spacing: 6) {
                Image(systemName: "iphone.slash").font(.caption2)
                Text(action.title)
                    .lineLimit(1)
                    .minimumScaleFactor(0.8)
                Spacer(minLength: 0)
                Text(String(action.key))
                    .font(.caption2.monospaced())
            }
            .foregroundStyle(.tertiary)
            .padding(.horizontal, 12)
            .frame(maxWidth: .infinity, alignment: .leading)
            .touchTarget()
            .background(Color.primary.opacity(0.035), in: RoundedRectangle(cornerRadius: 8))
            .help("On the mouse only — the module ignores AT commands while this app is connected.")
        } else {
            // Anything that drives the wheels asks first. On a tablet propped
            // next to a half-built mouse, a stray tap on Search is not harmless.
            Button {
                if action.movesTheMouse { pending = action } else { session.send(.action(action)) }
            } label: {
                HStack(spacing: 6) {
                    if action.movesTheMouse {
                        Image(systemName: "figure.walk.motion").font(.caption2)
                    }
                    Text(action.title)
                        .lineLimit(1)
                        .minimumScaleFactor(0.8)
                    Spacer(minLength: 0)
                    Text(String(action.key))
                        .font(.caption2.monospaced())
                        .foregroundStyle(.tertiary)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            .buttonStyle(.bordered)
            .touchTarget()
            .tint(session.runningAction == action ? .orange : nil)
        }
    }
}
