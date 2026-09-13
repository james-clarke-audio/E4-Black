import SwiftUI
import E4Core

/// Replay transport, pinned beside the status strip.
///
/// It lives here rather than on the Maze screen because a replay drives the
/// WHOLE app — the same message stream the radio produces, lighting the same
/// screens. Confined to one screen you would have to choose between watching
/// the map and watching the sensors, when seeing both together is most of why
/// you are replaying at all.
struct ReplayBar: View {
    @Environment(E4ReplayPlayer.self) private var player

    var body: some View {
        @Bindable var player = player

        VStack(spacing: 0) {
            HStack(spacing: 12) {
                // Unmissable, because everything on screen below this bar is
                // history. Mistaking a replay for a live mouse is the one
                // failure this feature can cause.
                Label("REPLAY", systemImage: "clock.arrow.circlepath")
                    .font(.caption.monospaced().weight(.semibold))
                    .foregroundStyle(Palette.warn)

                Text(player.name)
                    .font(.system(size: 10.5, design: .monospaced))
                    .foregroundStyle(Palette.faint)
                    .lineLimit(1)
                    .truncationMode(.middle)
                    .frame(maxWidth: 190, alignment: .leading)

                Button {
                    player.isPlaying ? player.pause() : player.play()
                } label: {
                    Image(systemName: player.isPlaying ? "pause.fill" : "play.fill")
                        .frame(width: 15)
                }
                .buttonStyle(.bordered)
                .touchTarget()

                Button { player.step() } label: {
                    Image(systemName: "forward.frame.fill").frame(width: 15)
                }
                .buttonStyle(.bordered)
                .touchTarget()
                .disabled(player.atEnd)

                Button { player.restart() } label: {
                    Image(systemName: "backward.end.fill").frame(width: 15)
                }
                .buttonStyle(.bordered)
                .touchTarget()

                Slider(value: Binding(
                    get: { player.position },
                    set: { player.seek(to: $0) }
                ), in: 0...max(player.duration, 0.001))
                .frame(minWidth: 120)

                Text(clock(player.position) + " / " + clock(player.duration))
                    .font(.system(size: 10.5, design: .monospaced))
                    .monospacedDigit()
                    .foregroundStyle(Palette.dim)

                Picker("Speed", selection: $player.rate) {
                    ForEach(E4ReplayPlayer.rates, id: \.self) { rate in
                        Text(rate < 1 ? "\(rate, specifier: "%.2g")×" : "\(Int(rate))×").tag(rate)
                    }
                }
                .labelsHidden()
                .pickerStyle(.menu)
                .frame(width: 74)

                Button("Close") { player.close() }
                    .buttonStyle(.bordered)
                    .touchTarget()
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 9)
            .background(Palette.warn.opacity(0.10))

            Rectangle().fill(Palette.warn.opacity(0.45)).frame(height: 1)
        }
    }

    private func clock(_ t: TimeInterval) -> String {
        let total = Int(t.rounded())
        return String(format: "%d:%02d", total / 60, total % 60)
    }
}
