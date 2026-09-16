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
        .init(name: "Calibration", actions: [.recalGyro, .gyroScaleCal, .irMonitor, .turnTuning, .motionTest, .zigzagTest]),
        .init(name: "Moves", actions: [.forward180, .right90, .left90, .spin180]),
        .init(name: "In-maze bench", actions: [.setMazeSize, .setGoal, .search]),
        .init(name: "Simulation", actions: [.simulate, .simExplore, .recallMaze]),
        .init(name: "Diagnostics", actions: [.eepromTest, .sensorMode, .irSampler, .resetPose,
                                             .testMode, .setBT57k, .btProvision, .emitterHold,
                                             .firmwareVersion]),
        .init(name: "Competition", actions: [.wallFollower, .explore, .planRoute, .speedRun, .resumeSaved, .runOptions]),
    ]

    /// The curated groups, plus anything E4MenuAction gained that nobody placed.
    /// thresholdCal is deliberately absent from the grid — it has its own
    /// screen, and a duplicate button that skips the guided flow is worse than
    /// no button.
    private static let placedElsewhere: Set<E4MenuAction> = [.thresholdCal]

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

            if let name = session.runningActionName {
                Label("Running: \(name)", systemImage: "play.circle.fill")
                    .font(.callout)
                    .foregroundStyle(.orange)
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
