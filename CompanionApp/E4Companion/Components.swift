import SwiftUI
import E4Core

// MARK: - Shared sensor presentation
//
// The scale, threshold and colour rules live here because the status strip and
// the Sensors screen must agree — two places deciding independently what counts
// as "wall detected" is how a readout starts lying to you.

@MainActor
struct SensorDisplay {
    let sensor: E4Sensor
    let value: Int
    let threshold: Int?      // nil for the front pair — see below
    let isLit: Bool
    let fraction: Double
    let thresholdFraction: Double?

    /// The front threshold is tested against the FL+FR **sum**, never against
    /// either sensor alone, so the front pair gets no per-bar mark and takes
    /// its lit state from the sum. Marking them individually would show you
    /// something the firmware never decides.
    init(_ sensor: E4Sensor, session: E4Session, fullScale: Double) {
        // Everything is computed into locals first. A closure here — even a
        // trivial `map` — captures self before the stored properties are all
        // assigned, which Swift rejects outright.
        let reading = session.sensors[sensor.rawValue]
        let status = session.sensorStatus

        let thr: Int?
        let lit: Bool
        switch sensor {
        case .left, .right:
            thr = status?.sideThreshold
            if let status { lit = reading >= status.sideThreshold } else { lit = false }
        case .frontLeft, .frontRight:
            thr = nil
            if let status { lit = status.frontSum >= status.frontThreshold } else { lit = false }
        }

        var mark: Double?
        if let thr, fullScale > 0 {
            let f = Double(thr) / fullScale
            mark = f < 1 ? f : nil
        }

        self.sensor = sensor
        self.value = reading
        self.threshold = thr
        self.isLit = lit
        self.fraction = fullScale > 0 ? min(1, Double(reading) / fullScale) : 0
        self.thresholdFraction = mark
    }

    /// Green for "reporting a wall", grey for not. Deliberately not the system
    /// accent: the accent is whatever the user picked in System Settings, so it
    /// carries no meaning here, and a red or pink one reads as an alarm on a
    /// perfectly healthy reading.
    var colour: Color { isLit ? Palette.good : Palette.faint }
}

/// The bar scale, shared for the same reason.
///
/// A fixed range cannot work: real ambient-subtracted IR runs to a few hundred,
/// virtual (truth-derived) readings run into the thousands. It ratchets up only,
/// so bars stay comparable rather than rescaling while you watch them.
@MainActor
func sensorFullScale(_ session: E4Session) -> Double {
    let peak = max(Double(session.sensorPeak), 300)
    let step: Double = peak > 1500 ? 500 : 100
    return (peak / step).rounded(.up) * step
}

struct SensorBar: View {
    let display: SensorDisplay
    var height: CGFloat = 10

    var body: some View {
        GeometryReader { geo in
            ZStack(alignment: .leading) {
                RoundedRectangle(cornerRadius: height / 2.6)
                    .fill(Palette.panel2)
                RoundedRectangle(cornerRadius: height / 2.6)
                    .fill(display.colour)
                    .frame(width: geo.size.width * display.fraction)

                // The wall-detect threshold. When you are setting thresholds the
                // number that matters is the margin, not whether it happens to
                // be green.
                if let mark = display.thresholdFraction {
                    Rectangle()
                        .fill(Palette.ink)
                        .opacity(0.45)
                        .frame(width: 1.5)
                        .offset(x: geo.size.width * mark)
                }
            }
        }
        .frame(height: height)
    }
}

// MARK: - Chrome

struct Card<Content: View>: View {
    let title: String?
    let content: Content

    init(_ title: String? = nil, @ViewBuilder content: () -> Content) {
        self.title = title
        self.content = content()
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 11) {
            if let title {
                Text(title.uppercased())
                    .font(.caption2.monospaced())
                    .tracking(1.2)
                    .foregroundStyle(Palette.faint)
            }
            content
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(15)
        .background(Palette.card, in: RoundedRectangle(cornerRadius: 12))
        .overlay(
            RoundedRectangle(cornerRadius: 12).strokeBorder(Palette.line, lineWidth: 1)
        )
    }
}

/// A labelled value pair, monospaced and tabular so columns of them line up.
struct FieldRow: View {
    let key: String
    let value: String
    var tint: Color = Palette.ink

    var body: some View {
        HStack {
            Text(key).foregroundStyle(Palette.dim)
            Spacer()
            Text(value).foregroundStyle(tint)
        }
        .font(.callout.monospaced())
        .monospacedDigit()
    }
}

/// Marks something the app can draw but the firmware cannot yet do.
struct NeedsFirmwareChip: View {
    var body: some View {
        Text("NEEDS FIRMWARE")
            .font(.system(size: 9, weight: .semibold, design: .monospaced))
            .tracking(0.6)
            .padding(.horizontal, 6)
            .padding(.vertical, 2)
            .background(Palette.warn.opacity(0.13), in: RoundedRectangle(cornerRadius: 4))
            .foregroundStyle(Palette.warn)
    }
}

/// Shown where a screen is deliberately not built yet. Says what will be here
/// and what it is waiting on, rather than pretending to be almost ready.
struct NotBuiltYet: View {
    let title: String
    let blurb: String
    let waitingOn: String

    var body: some View {
        VStack(spacing: 13) {
            Text(title)
                .font(.title3.weight(.semibold))
            Text(blurb)
                .font(.callout)
                .foregroundStyle(Palette.dim)
                .multilineTextAlignment(.center)
                .frame(maxWidth: 420)
            Text(waitingOn)
                .font(.caption.monospaced())
                .foregroundStyle(Palette.faint)
                .multilineTextAlignment(.center)
                .frame(maxWidth: 420)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .padding(40)
    }
}

/// Every control the app offers, at a size that works under a finger.
/// 44pt is Apple's touch minimum; on a Mac it reads slightly generous, which
/// is a fair trade for not maintaining two layouts.
extension View {
    func touchTarget() -> some View {
        frame(minHeight: 44)
    }
}
