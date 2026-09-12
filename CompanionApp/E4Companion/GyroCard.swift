import SwiftUI
import E4Core

/// Live gyro readings, and the number that actually matters: how fast her
/// heading walks away while she is standing still.
///
/// Bias is the thing that ruins a long run. At a tenth of a degree per second —
/// small enough to look like nothing — a three-minute search ends eighteen
/// degrees out, and every wall she then expects is in the wrong place.
struct GyroCard: View {
    @Environment(E4Session.self) private var session

    /// How long a stationary run has to be before a drift figure is offered.
    ///
    /// `report_tel` casts the heading to int, so it moves in 1° steps. Fitting
    /// a line over a short window would mostly measure the quantisation; over
    /// half a minute the accumulated angle swamps it.
    private let minimumWindow: TimeInterval = 25

    var body: some View {
        Card("Gyro") {
            live
            Divider()
            drift
            if stationary.count > 2 {
                HeadingTrace(samples: stationary)
                    .frame(height: 54)
            }
            Divider()
            HStack(spacing: 8) {
                Button("Recal gyro") { session.send(.action(.recalGyro)) }
                    .buttonStyle(.borderedProminent)
                    .touchTarget()
                Button("Reset pose") { session.send(.action(.resetPose)) }
                    .buttonStyle(.bordered)
                    .touchTarget()
                Spacer()
            }
            Text("Recal zeroes the bias with her held still. Worth doing after anything that disturbs the board she sits on, and worth re-checking the drift afterwards.")
                .font(.caption)
                .foregroundStyle(Palette.faint)
        }
    }

    // MARK: live

    private var live: some View {
        HStack(alignment: .top, spacing: 22) {
            reading("heading", session.telemetry.map { "\(Int($0.gyro))°" } ?? "—",
                    "gyro, integrated")
            reading("encoder", session.telemetry.map { "\(Int($0.angle))°" } ?? "—",
                    "wheels, independent")
            reading("rate", session.telemetry.map { "\(Int($0.omega))°/s" } ?? "—",
                    "int-rounded")
            Spacer()
        }
    }

    private func reading(_ key: String, _ value: String, _ note: String) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(value)
                .font(.title2.monospaced())
                .monospacedDigit()
            Text(key)
                .font(.system(size: 10, design: .monospaced))
                .foregroundStyle(Palette.dim)
            Text(note)
                .font(.system(size: 9, design: .monospaced))
                .foregroundStyle(Palette.faint)
        }
    }

    // MARK: drift

    @ViewBuilder
    private var drift: some View {
        if let rate = driftRate, let span = stationarySpan, span >= minimumWindow {
            let perMinute = rate * 60
            let overRun = abs(rate) * 180          // a three-minute search
            VStack(alignment: .leading, spacing: 6) {
                HStack(alignment: .firstTextBaseline, spacing: 8) {
                    Text(String(format: "%+.2f", perMinute))
                        .font(.system(size: 26, design: .monospaced))
                        .foregroundStyle(tint(for: overRun))
                    Text("°/min drift")
                        .font(.callout)
                        .foregroundStyle(Palette.dim)
                    Spacer()
                    Text(String(format: "over %.0f s still", span))
                        .font(.system(size: 10, design: .monospaced))
                        .foregroundStyle(Palette.faint)
                }
                Text(verdict(overRun))
                    .font(.caption)
                    .foregroundStyle(Palette.dim)
            }
        } else if let span = stationarySpan, span > 2 {
            HStack(spacing: 8) {
                ProgressView()
                    .controlSize(.small)
                Text(String(format: "Measuring drift — %.0f of %.0f s held still",
                            span, minimumWindow))
                    .font(.callout)
                    .foregroundStyle(Palette.dim)
            }
        } else {
            Text("Hold her still to measure drift. The reading needs her stationary, because any real rotation is indistinguishable from bias.")
                .font(.callout)
                .foregroundStyle(Palette.dim)
        }
    }

    private func tint(for overRun: Double) -> Color {
        // Judged by what it costs over a run, not by the raw number: a degree
        // of error is a few millimetres of lateral position by the far side of
        // the arena, and the side sensors can absorb some of that.
        switch overRun {
        case ..<3:  return Palette.good
        case ..<10: return Palette.warn
        default:    return Palette.bad
        }
    }

    private func verdict(_ overRun: Double) -> String {
        switch overRun {
        case ..<3:
            return String(format: "About %.0f° over a three-minute search — fine.", overRun)
        case ..<10:
            return String(format: "About %.0f° over a three-minute search. Worth a Recal before a real run.", overRun)
        default:
            return String(format: "About %.0f° over a three-minute search — she would be lost. Recal, and if it comes back, suspect the mounting or the board.", overRun)
        }
    }

    // MARK: the stationary window

    /// The tail of the history where she has not been moving.
    ///
    /// Anything with wheel speed is discarded: a real rotation and a biased
    /// gyro look identical in the heading alone, so the only honest place to
    /// measure bias is while nothing is turning her.
    private var stationary: [E4Telemetry] {
        var out: [E4Telemetry] = []
        for frame in session.telemetryHistory.reversed() {
            guard frame.velocity == 0 else { break }
            out.append(frame)
        }
        return out.reversed()
    }

    private var stationarySpan: TimeInterval? {
        guard let first = stationary.first, let last = stationary.last, stationary.count > 1
        else { return nil }
        let ms = Double(last.timestamp - first.timestamp)
        return ms > 0 ? ms / 1000 : nil
    }

    /// Least-squares slope of heading against time, in degrees per second.
    private var driftRate: Double? {
        let samples = stationary
        guard samples.count > 4, let t0 = samples.first?.timestamp else { return nil }

        let points = samples.map {
            (x: Double($0.timestamp - t0) / 1000, y: $0.gyro)
        }
        let n = Double(points.count)
        let sumX = points.reduce(0) { $0 + $1.x }
        let sumY = points.reduce(0) { $0 + $1.y }
        let sumXY = points.reduce(0) { $0 + $1.x * $1.y }
        let sumXX = points.reduce(0) { $0 + $1.x * $1.x }

        let denominator = n * sumXX - sumX * sumX
        guard abs(denominator) > 1e-6 else { return nil }
        return (n * sumXY - sumX * sumY) / denominator
    }
}

/// Heading against time while she has been still. Flat is what you want.
struct HeadingTrace: View {
    let samples: [E4Telemetry]

    var body: some View {
        Canvas { context, size in
            guard samples.count > 1,
                  let t0 = samples.first?.timestamp,
                  let t1 = samples.last?.timestamp, t1 > t0 else { return }

            let headings = samples.map(\.gyro)
            let lo = headings.min()!, hi = headings.max()!
            // Never less than a couple of degrees of scale, or quantisation
            // noise on a perfectly steady gyro fills the whole box and looks
            // like a fault.
            let mid = (lo + hi) / 2
            let span = Swift.max(hi - lo, 2.0)
            let low = mid - span / 2

            var zero = Path()
            let zeroY = size.height * (1 - ((headings[0] - low) / span))
            zero.move(to: CGPoint(x: 0, y: zeroY))
            zero.addLine(to: CGPoint(x: size.width, y: zeroY))
            context.stroke(zero, with: .color(Palette.line),
                           style: StrokeStyle(lineWidth: 1, dash: [3, 3]))

            var path = Path()
            for (i, frame) in samples.enumerated() {
                let x = size.width * Double(frame.timestamp - t0) / Double(t1 - t0)
                let y = size.height * (1 - ((frame.gyro - low) / span))
                i == 0 ? path.move(to: CGPoint(x: x, y: y)) : path.addLine(to: CGPoint(x: x, y: y))
            }
            context.stroke(path, with: .color(Palette.accent),
                           style: StrokeStyle(lineWidth: 1.4, lineJoin: .round))
        }
    }
}
