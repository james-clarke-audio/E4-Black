import SwiftUI
import E4Core

/// One maze cell seen from above, with her in it.
///
/// This exists because "corridor" is a word and a picture is not. Read as text,
/// "a wall each side, nothing in front" has to be turned into a mental image
/// before you can act on it, and at a bench with epoxy on your fingers that is
/// exactly the step that gets skipped. Drawn, there is nothing to interpret.
///
/// It has two jobs and shows both at once: the walls you are being ASKED for,
/// and the walls she is CURRENTLY DETECTING. When they disagree, that disagreement
/// is the whole diagnostic — it is the difference between "I have not set it up
/// yet" and "I have set it up and she cannot see it".
struct WallCellView: View {

    /// The configuration being asked for, if a capture is pending.
    var target: E4CaptureState?

    /// What she reports seeing right now, from her own threshold comparison.
    var detected: (left: Bool, front: Bool, right: Bool)?

    /// Live ambient-subtracted readings, drawn beside each sensor.
    var readings: (left: Int, front: Int, right: Int)?

    var body: some View {
        GeometryReader { geo in
            let side = min(geo.size.width, geo.size.height) - 40
            ZStack {
                Canvas { context, size in draw(&context, size: size, side: side) }
            }
            .frame(width: geo.size.width, height: geo.size.height)
        }
    }

    private func draw(_ context: inout GraphicsContext, size: CGSize, side: CGFloat) {
        guard side > 40 else { return }
        let x0 = (size.width - side) / 2
        let y0 = (size.height - side) / 2
        let cell = CGRect(x: x0, y: y0, width: side, height: side)

        // Floor
        context.fill(Path(roundedRect: cell, cornerRadius: 3),
                     with: .color(Palette.Dark.panel))

        let wallWidth = max(6.0, side * 0.035)

        // Three walls: left, front (top of the picture, she faces up), right.
        // The back wall is never drawn — no sensor looks at it, so drawing one
        // would imply a constraint the calibration does not have.
        drawWall(&context, rect: CGRect(x: cell.minX - wallWidth / 2, y: cell.minY,
                                        width: wallWidth, height: cell.height),
                 asked: target?.walls.left, seen: detected?.left)
        drawWall(&context, rect: CGRect(x: cell.minX, y: cell.minY - wallWidth / 2,
                                        width: cell.width, height: wallWidth),
                 asked: target?.walls.front, seen: detected?.front)
        drawWall(&context, rect: CGRect(x: cell.maxX - wallWidth / 2, y: cell.minY,
                                        width: wallWidth, height: cell.height),
                 asked: target?.walls.right, seen: detected?.right)

        drawMouse(&context, in: cell)
        drawSensors(&context, in: cell, side: side)
    }

    /// What a wall means right now — the five states worth telling apart.
    ///
    /// Named rather than switched on a tuple of two optionals: `(Bool?, Bool?)`
    /// has nine inhabitants, and the one that was missing was `(true, nil)` —
    /// a wall being asked for before she has reported anything at all, which is
    /// the state the diagram is in for the first second every single time.
    private enum WallLook {
        case confirmed      // wanted, and she sees it
        case expected       // wanted, and she does not — or has not said yet
        case unexpected     // not wanted, and she sees one anyway
        case absent         // not wanted, correctly nothing
        case idle           // nothing being asked for, nothing seen
    }

    private func look(asked: Bool?, seen: Bool?) -> WallLook {
        let sees = (seen == true)
        guard let asked else { return sees ? .confirmed : .idle }
        if asked { return sees ? .confirmed : .expected }
        return sees ? .unexpected : .absent
    }

    /// A wall is drawn solid when it is wanted, outlined when it is explicitly
    /// not, and tinted by whether she can actually see it.
    private func drawWall(_ context: inout GraphicsContext, rect: CGRect,
                          asked: Bool?, seen: Bool?) {
        let path = Path(roundedRect: rect, cornerRadius: 2)

        switch look(asked: asked, seen: seen) {
        case .confirmed:
            // The state you are trying to reach.
            context.fill(path, with: .color(Palette.good))

        case .expected:
            // Either it is not there yet, or it is there and she cannot detect
            // it; the live readings beside the sensor are what separate those
            // two, which is why they are drawn.
            context.fill(path, with: .color(Palette.warn.opacity(0.30)))
            context.stroke(path, with: .color(Palette.warn), lineWidth: 1.5)

        case .unexpected:
            // On the open-floor capture this is the finding, not a nuisance.
            context.fill(path, with: .color(Palette.bad.opacity(0.30)))
            context.stroke(path, with: .color(Palette.bad), lineWidth: 1.5)

        case .absent, .idle:
            context.stroke(path, with: .color(Palette.Dark.faint),
                           style: StrokeStyle(lineWidth: 1.2, dash: [4, 4]))
        }
    }

    /// Her outline, facing up the picture. Deliberately a simple wedge — this
    /// is a diagram of a situation, not a picture of the mouse, and detail here
    /// would compete with the thing you are meant to look at.
    private func drawMouse(_ context: inout GraphicsContext, in cell: CGRect) {
        let w = cell.width * 0.34
        let h = cell.height * 0.40
        let cx = cell.midX
        let cy = cell.midY + cell.height * 0.06

        var body = Path()
        body.move(to: CGPoint(x: cx, y: cy - h / 2))
        body.addLine(to: CGPoint(x: cx + w / 2, y: cy + h / 2))
        body.addLine(to: CGPoint(x: cx - w / 2, y: cy + h / 2))
        body.closeSubpath()

        context.fill(body, with: .color(Palette.Dark.ink.opacity(0.22)))
        context.stroke(body, with: .color(Palette.Dark.ink.opacity(0.75)), lineWidth: 1.4)
    }

    /// The four detectors in their real arrangement: the OUTER pair look
    /// sideways, the INNER pair forward. Drawn the right way round because the
    /// first time this mapping was guessed from a description it was backwards,
    /// and the hardware had to say so.
    private func drawSensors(_ context: inout GraphicsContext, in cell: CGRect, side: CGFloat) {
        let cx = cell.midX
        let noseY = cell.midY + cell.height * 0.06 - cell.height * 0.20
        let dot = max(5.0, side * 0.022)

        func mark(_ point: CGPoint, lit: Bool?, label: String, value: Int?) {
            let r = CGRect(x: point.x - dot / 2, y: point.y - dot / 2, width: dot, height: dot)
            let colour: Color = (lit == true) ? Palette.good : Palette.Dark.faint
            context.fill(Path(ellipseIn: r), with: .color(colour))

            var text = label
            if let value { text += " \(value)" }
            let resolved = context.resolve(
                Text(text)
                    .font(.system(size: max(9, side * 0.030), design: .monospaced))
                    .foregroundStyle(lit == true ? Palette.good : Palette.Dark.dim))
            context.draw(resolved, at: CGPoint(x: point.x, y: point.y - dot * 1.7), anchor: .center)
        }

        mark(CGPoint(x: cx - cell.width * 0.17, y: noseY + cell.height * 0.05),
             lit: detected?.left, label: "SL", value: readings?.left)
        mark(CGPoint(x: cx + cell.width * 0.17, y: noseY + cell.height * 0.05),
             lit: detected?.right, label: "SR", value: readings?.right)
        mark(CGPoint(x: cx, y: noseY),
             lit: detected?.front, label: "F", value: readings?.front)
    }
}
