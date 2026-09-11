import SwiftUI
import E4Core

struct HistoryScreen: View {
    @Environment(SessionStore.self) private var store
    @State private var selected: E4SessionSummary?
    @State private var stats: E4SessionStats?

    var body: some View {
        HStack(spacing: 0) {
            list
                .frame(width: 300)
            Rectangle().fill(Palette.line).frame(width: 1)
            detail
                .frame(maxWidth: .infinity)
        }
        .onAppear { store.refresh() }
    }

    // MARK: list

    private var list: some View {
        VStack(spacing: 0) {
            if let recording = store.recording {
                HStack(spacing: 8) {
                    Circle().fill(Palette.bad).frame(width: 8, height: 8)
                    VStack(alignment: .leading, spacing: 1) {
                        Text("Recording")
                            .font(.callout.weight(.semibold))
                        Text("\(store.linesWritten) lines · \(recording.lastPathComponent)")
                            .font(.system(size: 10, design: .monospaced))
                            .foregroundStyle(Palette.faint)
                            .lineLimit(1)
                            .truncationMode(.middle)
                    }
                    Spacer()
                }
                .padding(.horizontal, 14)
                .padding(.vertical, 10)
                .background(Palette.card)
                Rectangle().fill(Palette.line).frame(height: 1)
            }

            if store.summaries.isEmpty {
                VStack(spacing: 9) {
                    Text("No sessions yet")
                        .font(.callout.weight(.semibold))
                    Text("One file per connection, started automatically.")
                        .font(.caption)
                        .foregroundStyle(Palette.faint)
                        .multilineTextAlignment(.center)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .padding(24)
            } else {
                ScrollView {
                    LazyVStack(spacing: 0) {
                        ForEach(store.summaries) { summary in
                            row(summary)
                        }
                    }
                }
            }
        }
        .background(Palette.panel)
    }

    private func row(_ summary: E4SessionSummary) -> some View {
        Button {
            selected = summary
            stats = store.stats(for: summary)
        } label: {
            VStack(alignment: .leading, spacing: 3) {
                HStack {
                    Text(summary.header.started, format: .dateTime.day().month().hour().minute())
                        .font(.callout.weight(.medium))
                    Spacer()
                    Text(duration(summary.duration))
                        .font(.system(size: 10.5, design: .monospaced))
                        .foregroundStyle(Palette.faint)
                }
                HStack(spacing: 8) {
                    if let fw = summary.header.firmware {
                        Text("fw \(fw)")
                    }
                    Text(bytes(summary.bytes))
                    Spacer()
                }
                .font(.system(size: 10, design: .monospaced))
                .foregroundStyle(Palette.faint)
            }
            .padding(.horizontal, 14)
            .padding(.vertical, 10)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(selected?.url == summary.url ? Palette.panel2 : Color.clear)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .contextMenu {
            Button("Show in Finder") { reveal(summary.url) }
            Button("Delete", role: .destructive) {
                store.delete(summary)
                if selected?.url == summary.url { selected = nil; stats = nil }
            }
        }
    }

    // MARK: detail

    @ViewBuilder
    private var detail: some View {
        if let selected, let stats {
            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    Card("Session") {
                        FieldRow(key: "started", value: selected.header.started
                            .formatted(date: .abbreviated, time: .standard))
                        FieldRow(key: "duration", value: duration(selected.duration))
                        if let device = selected.header.device {
                            FieldRow(key: "device", value: device)
                        }
                        if let fw = selected.header.firmware {
                            FieldRow(key: "firmware", value: "v\(fw)")
                        }
                        FieldRow(key: "lines", value: "\(stats.lineCount)")
                        if stats.unrecognisedCount > 0 {
                            FieldRow(key: "unrecognised", value: "\(stats.unrecognisedCount)",
                                     tint: Palette.warn)
                        }
                        if !stats.actionsRun.isEmpty {
                            Divider()
                            Text(stats.actionsRun.joined(separator: " · "))
                                .font(.caption.monospaced())
                                .foregroundStyle(Palette.dim)
                        }
                    }

                    if !stats.sensors.isEmpty {
                        sensorCard(stats)
                    }

                    if !stats.turnResults.isEmpty {
                        turnCard(stats)
                    }

                    Card("Pack") {
                        if let lo = stats.batteryMin, let hi = stats.batteryMax {
                            FieldRow(key: "range", value: String(format: "%.2f – %.2f V", lo, hi))
                            FieldRow(key: "sag", value: String(format: "%.2f V", hi - lo))
                            Text("A pack that sags further under the same work than it did last week is a pack on its way out.")
                                .font(.caption)
                                .foregroundStyle(Palette.faint)
                        } else {
                            Text("No telemetry in this session.")
                                .font(.callout)
                                .foregroundStyle(Palette.dim)
                        }
                    }

                    Button {
                        reveal(selected.url)
                    } label: {
                        Label("Show log in Finder", systemImage: "folder")
                    }
                    .buttonStyle(.bordered)
                    .touchTarget()
                }
                .padding(18)
                .frame(maxWidth: 860)
                .frame(maxWidth: .infinity)
            }
            .background(Palette.panel)
        } else {
            VStack(spacing: 11) {
                Text("Pick a session")
                    .font(.title3.weight(.semibold))
                Text("Raw lines are kept, not decoded ones — so an improved decoder can be applied to an old log rather than the log being stuck with whatever the app understood the day it was written.")
                    .font(.callout)
                    .foregroundStyle(Palette.dim)
                    .multilineTextAlignment(.center)
                    .frame(maxWidth: 420)
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(Palette.panel)
        }
    }

    private func sensorCard(_ stats: E4SessionStats) -> some View {
        // One scale across all four. Per-sensor autoscaling draws a sensor
        // doing nothing with the same amplitude as one doing something, which
        // is exactly backwards: a dead channel should look dead.
        let hi = stats.sensors.values.map(\.max).max() ?? 1
        let lo = 0

        return Card("Wall sensors") {
            ForEach(E4Sensor.allCases) { sensor in
                if let series = stats.sensors[sensor] {
                    HStack(spacing: 12) {
                        Text(sensor.label)
                            .font(.callout.monospaced().weight(.semibold))
                            .frame(width: 30, alignment: .leading)
                        Sparkline(samples: series.samples, low: lo, high: hi)
                            .frame(height: 26)
                        VStack(alignment: .trailing, spacing: 1) {
                            Text("\(series.median)")
                                .font(.callout.monospaced())
                                .monospacedDigit()
                            Text("\(series.min)–\(series.max)")
                                .font(.system(size: 9.5, design: .monospaced))
                                .foregroundStyle(Palette.faint)
                        }
                        .frame(width: 86, alignment: .trailing)
                    }
                }
            }

            HStack {
                Text("0")
                Spacer()
                Text("all four to \(hi)")
                Spacer()
                Text(" ")
            }
            .font(.system(size: 9.5, design: .monospaced))
            .foregroundStyle(Palette.faint)

            Divider()
            Text("Median over the whole session, so it mixes wall-present with open-floor readings — blunt, but the right shape for \u{201C}is SL behaving like it did last week\u{201D}. Compare sessions that did similar work.")
                .font(.caption)
                .foregroundStyle(Palette.faint)
        }
    }

    private func turnCard(_ stats: E4SessionStats) -> some View {
        Card("Turns") {
            ForEach(Array(stats.turnResults.enumerated()), id: \.offset) { _, result in
                HStack(spacing: 10) {
                    Text(result.kind.rawValue)
                        .frame(width: 42, alignment: .leading)
                        .foregroundStyle(Palette.dim)
                    Text(result.commanded.map { "\(Int($0))°" } ?? "—")
                        .foregroundStyle(Palette.dim)
                    Spacer()
                    Text(result.distance.map { "\(Int($0)) mm" } ?? "—")
                        .monospacedDigit()
                        .foregroundStyle(Palette.warn)
                }
                .font(.caption.monospaced())
            }
        }
    }

    // MARK: bits

    private func duration(_ seconds: TimeInterval) -> String {
        guard seconds > 0 else { return "—" }
        let m = Int(seconds) / 60, s = Int(seconds) % 60
        return m >= 60 ? "\(m / 60) h \(m % 60) m" : (m > 0 ? "\(m) m \(s) s" : "\(s) s")
    }

    private func bytes(_ n: Int) -> String {
        n > 1_048_576 ? String(format: "%.1f MB", Double(n) / 1_048_576)
                      : String(format: "%.0f KB", Double(n) / 1024)
    }

    private func reveal(_ url: URL) {
        #if os(macOS)
        NSWorkspace.shared.activateFileViewerSelecting([url])
        #endif
    }
}

/// Thinned samples across the session, drawn against a caller-supplied range so
/// several are comparable. No axes — it answers "was this steady or did it
/// wander", and a full chart would imply a precision the statistic lacks.
struct Sparkline: View {
    let samples: [Int]
    let low: Int
    let high: Int

    var body: some View {
        Canvas { context, size in
            // The baseline, so a flat trace reads as "sitting at the bottom"
            // rather than as a line floating somewhere ambiguous.
            var base = Path()
            base.move(to: CGPoint(x: 0, y: size.height - 0.5))
            base.addLine(to: CGPoint(x: size.width, y: size.height - 0.5))
            context.stroke(base, with: .color(Palette.line), lineWidth: 1)

            guard samples.count > 1, high > low else { return }
            let span = Double(high - low)
            var path = Path()
            for (i, v) in samples.enumerated() {
                let x = size.width * Double(i) / Double(samples.count - 1)
                let clamped = Swift.min(Swift.max(v, low), high)
                let y = size.height * (1 - (Double(clamped - low) / span))
                i == 0 ? path.move(to: CGPoint(x: x, y: y)) : path.addLine(to: CGPoint(x: x, y: y))
            }
            context.stroke(path, with: .color(Palette.accent),
                           style: StrokeStyle(lineWidth: 1.2, lineJoin: .round))
        }
    }
}
