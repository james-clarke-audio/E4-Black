import SwiftUI
import E4Core

struct ContentView: View {
    @Environment(E4Session.self) private var session
    @State private var screen: E4Screen = .sensors
    @State private var columns = NavigationSplitViewVisibility.all

    var body: some View {
        VStack(spacing: 0) {
            // Above the split view, not inside it, so it spans the sidebar too
            // and survives the sidebar collapsing in portrait.
            StatusStrip()

            NavigationSplitView(columnVisibility: $columns) {
                sidebar
            } detail: {
                detail
            }
            .navigationSplitViewStyle(.balanced)
        }
        // The app does not follow the system appearance. Light chrome was a
        // deliberate choice on the web app — the dark panel made small text
        // hard to read at the bench — and following a dark-mode Mac would
        // quietly undo it.
        .preferredColorScheme(.light)
        .tint(Palette.accent)
        .background(Palette.panel)
    }

    private var sidebar: some View {
        // Selection-driven, NOT NavigationLink-driven. Using both makes two
        // mechanisms fight over the same selection every frame, which SwiftUI
        // reports as "tried to update multiple times per frame". The detail
        // pane reads `screen` directly, so links would be redundant anyway.
        List(E4Screen.allCases, selection: $screen) { item in
            HStack {
                Label(item.title, systemImage: item.symbol)
                Spacer()
                if let badge = item.badge(session) {
                    Text(badge)
                        .font(.system(size: 10, design: .monospaced))
                        .foregroundStyle(Palette.faint)
                }
            }
            .touchTarget()
            .tag(item)
        }
        .navigationSplitViewColumnWidth(min: 190, ideal: 204, max: 240)
        .scrollContentBackground(.hidden)
        .background(Palette.sidebar)
        .safeAreaInset(edge: .bottom) { connectionFooter }
    }

    @ViewBuilder
    private var detail: some View {
        Group {
            switch screen {
            case .maze:        MazeScreen()
            case .sensors:     SensorsScreen()
            case .actions:     ActionsScreen()
            case .tuning:      TuningScreen()
            case .calibration: CalibrationScreen()
            case .history:     HistoryScreen()
            case .log:         LogScreen()
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Palette.panel)
        .navigationTitle(screen.title)
        #if os(iOS)
        .navigationBarTitleDisplayMode(.inline)
        #endif
    }

    private var connectionFooter: some View {
        VStack(spacing: 8) {
            Divider()
            if session.connection.isConnected {
                Button("Disconnect", role: .destructive) { session.disconnect() }
                    .buttonStyle(.bordered)
                    .touchTarget()
            } else {
                ConnectSheetButton()
            }
        }
        .padding(.horizontal, 10)
        .padding(.bottom, 10)
        .background(Palette.sidebar)
    }
}

/// Scanning and connecting, kept out of the sidebar proper — it is a thing you
/// do once a session, not a place you navigate to.
struct ConnectSheetButton: View {
    @Environment(E4Session.self) private var session
    @State private var showing = false

    var body: some View {
        Button {
            showing = true
            session.startScan()
        } label: {
            Label("Connect", systemImage: "antenna.radiowaves.left.and.right")
                .frame(maxWidth: .infinity)
        }
        .buttonStyle(.borderedProminent)
        .touchTarget()
        .sheet(isPresented: $showing) {
            ConnectSheet(dismiss: {
                showing = false
                session.stopScan()
            })
        }
    }
}

struct ConnectSheet: View {
    @Environment(E4Session.self) private var session
    let dismiss: () -> Void

    var body: some View {
        @Bindable var session = session

        NavigationStack {
            List {
                Section {
                    ForEach(session.peripherals) { device in
                        Button {
                            session.connect(to: device)
                            dismiss()
                        } label: {
                            HStack {
                                Text(device.name)
                                Spacer()
                                Text("\(device.rssi) dBm")
                                    .font(.caption.monospaced())
                                    .foregroundStyle(.secondary)
                            }
                            .touchTarget()
                        }
                    }
                } header: {
                    Text(session.connection.label)
                } footer: {
                    Text("She advertises the FFE0 service, which is what the scan filters on. Some modules do not put it in the advertisement even though they expose it once connected — widen the scan if she does not appear.")
                }

                Section {
                    Toggle("Show devices without FFE0", isOn: $session.scanUnfiltered)
                        .touchTarget()
                        .onChange(of: session.scanUnfiltered) { session.startScan() }
                }
            }
            .navigationTitle("Connect")
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel", action: dismiss)
                }
                ToolbarItem(placement: .primaryAction) {
                    Button("Rescan") { session.startScan() }
                }
            }
        }
        #if os(macOS)
        .frame(minWidth: 420, minHeight: 380)
        #endif
    }
}
