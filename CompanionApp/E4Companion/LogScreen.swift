import SwiftUI
import E4Core
#if os(macOS)
import AppKit
#else
import UIKit
#endif

struct LogScreen: View {
    @Environment(E4Session.self) private var session
    @State private var filter: Filter = .all
    @State private var command = ""
    @State private var copied = false

    enum Filter: String, CaseIterable, Identifiable {
        case all, sent, unknown
        var id: String { rawValue }
        var title: String {
            switch self {
            case .all:     return "All"
            case .sent:    return "Sent"
            case .unknown: return "Unrecognised"
            }
        }
    }

    private var lines: [E4LogEntry] {
        switch filter {
        case .all:     return session.log
        case .sent:    return session.log.filter { $0.kind == .sent }
        case .unknown: return session.log.filter { $0.kind == .unknown }
        }
    }

    var body: some View {
        VStack(spacing: 0) {
            toolbar
            Divider()
            transcript
            Divider()
            entry
        }
    }

    private var toolbar: some View {
        HStack(spacing: 8) {
            Picker("Filter", selection: $filter) {
                ForEach(Filter.allCases) { Text($0.title).tag($0) }
            }
            .pickerStyle(.segmented)
            .frame(maxWidth: 320)

            Spacer()

            Text("\(session.log.count) lines")
                .font(.caption.monospaced())
                .foregroundStyle(Palette.faint)

            // Per-line textSelection only ever gives you ONE line: SwiftUI
            // cannot drag a selection across separate Text views, and on iPad
            // there is no Finder to fall back to. So the transcript needs an
            // explicit copy, and it copies what the filter is SHOWING — if you
            // filtered to Unrecognised, those lines are what you wanted.
            Button(copied ? "Copied" : "Copy") { copyTranscript() }
                .buttonStyle(.bordered)
                .disabled(lines.isEmpty)

            Button("Clear") { session.clearLog() }
                .buttonStyle(.bordered)
        }
        .touchTarget()
        .padding(.horizontal, 16)
        .padding(.vertical, 10)
    }

    private var transcript: some View {
        ScrollViewReader { proxy in
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 1) {
                    ForEach(lines) { entry in
                        Text(entry.text)
                            .font(.caption.monospaced())
                            .foregroundStyle(colour(entry.kind))
                            .textSelection(.enabled)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .id(entry.id)
                    }
                }
                .padding(.horizontal, 16)
                .padding(.vertical, 10)
            }
            .background(Palette.Dark.bg)
            .onChange(of: session.log.count) {
                if let last = lines.last {
                    withAnimation(.easeOut(duration: 0.12)) { proxy.scrollTo(last.id, anchor: .bottom) }
                }
            }
        }
    }

    private var entry: some View {
        HStack(spacing: 8) {
            TextField("raw command — e.g. GTG,7,7", text: $command)
                .textFieldStyle(.roundedBorder)
                .font(.callout.monospaced())
                #if os(iOS)
                .autocorrectionDisabled()
                .textInputAutocapitalization(.never)
                #endif
                .onSubmit(send)

            Button("Send", action: send)
                .buttonStyle(.borderedProminent)
                .disabled(command.isEmpty || !session.connection.isConnected)
        }
        .touchTarget()
        .padding(.horizontal, 16)
        .padding(.vertical, 10)
    }

    private func send() {
        session.sendRaw(command)
        command = ""
    }

    /// The whole filtered transcript, tab-free and newline separated, so it
    /// pastes into a message or an editor as the lines she actually sent.
    private func copyTranscript() {
        let text = lines.map(\.text).joined(separator: "\n")
        #if os(macOS)
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
        #else
        UIPasteboard.general.string = text
        #endif
        // The label reverts on its own; a copy with no acknowledgement leaves
        // you pressing it twice and wondering which press actually took.
        copied = true
        Task { @MainActor in
            try? await Task.sleep(for: .seconds(1.6))
            copied = false
        }
    }

    /// Unrecognised lines are kept and shown, not dropped. EEPROM scan output
    /// and firmware prompts arrive as plain text, and they are exactly the
    /// lines that matter when something is wrong.
    private func colour(_ kind: E4LogEntry.Kind) -> Color {
        switch kind {
        case .sent:    return Palette.Dark.sent
        case .event:   return Palette.Dark.ink
        case .unknown: return Palette.Dark.faint
        case .alert:   return Palette.Dark.warn
        }
    }
}
