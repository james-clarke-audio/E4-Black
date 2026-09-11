import SwiftUI
import E4Core

struct LogScreen: View {
    @Environment(E4Session.self) private var session
    @State private var filter: Filter = .all
    @State private var command = ""

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
