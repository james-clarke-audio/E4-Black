import SwiftUI
import E4Core
import UniformTypeIdentifiers

/// Drawing a maze, rather than hunting through 406 files for one that happens
/// to exercise what you are testing.
///
/// Editing takes over the BIG canvas, because the whole point is seeing the
/// maze at a size you can judge. That is also the hazard: the same canvas
/// normally shows what SHE believes — her map, her pose, her trail, the planned
/// routes — and editing one while looking at the other would be an easy and
/// very confusing mistake. So edit mode is a mode: it is entered deliberately,
/// it replaces the canvas rather than drawing over it, it says so on screen,
/// and none of her live state is shown while it is on.
@Observable
final class MazeEditor {
    var isEditing = false
    var file: E4MazeFile?
    var sourceName = ""
    var dirty = false
    var tool: Tool = .walls
    var format: Format = .text

    enum Tool: String, CaseIterable, Identifiable {
        case walls, goal
        var id: String { rawValue }
        var title: String { self == .walls ? "Walls" : "Goal" }
    }

    enum Format: String, CaseIterable, Identifiable {
        case text, binary
        var id: String { rawValue }
        var title: String { self == .text ? "Text (.txt)" : "Binary (.maz)" }
        var utType: UTType { self == .text ? .plainText : .data }
        var ext: String { self == .text ? "txt" : "maz" }
    }

    /// An empty arena: perimeter, and the start cell as the firmware builds it.
    /// Nothing else, because a blank sheet is easier to reason about than a
    /// half-remembered maze you then have to undo.
    static func blank() -> E4MazeFile {
        var w = [UInt8](repeating: 0, count: 256)
        for i in 0..<256 {
            let x = i % 16, y = i / 16
            var m: UInt8 = 0
            if y == 15 { m |= 1 }
            if x == 15 { m |= 2 }
            if y == 0  { m |= 4 }
            if x == 0  { m |= 8 }
            w[i] = m
        }
        return E4MazeFile(walls: w, goals: [], format: .bytes).withStandardStartCell()
    }

    func begin(from loaded: E4MazeFile?, name: String) {
        file = loaded ?? Self.blank()
        sourceName = loaded == nil ? "untitled" : name
        dirty = false
        isEditing = true
    }

    func tap(x: Int, y: Int, side: E4MazeFile.Side?) {
        guard var f = file else { return }
        switch tool {
        case .walls:
            guard let side else { return }          // a centre tap does nothing here
            f = f.togglingWall(x: x, y: y, side: side)
        case .goal:
            // One tap sets the goal to that cell; tapping a goal cell clears it.
            // A goal of several cells is expressible, but the mouse takes one,
            // so the common case is made the easy one.
            f = f.settingGoal(f.goals.contains(.init(x: x, y: y)) ? [] : [.init(x: x, y: y)])
        }
        file = f
        dirty = true
    }

    var exportData: Data {
        guard let f = file?.withStandardStartCell() else { return Data() }
        return format == .text ? Data(f.asciiFileText.utf8) : f.byteFileData
    }

    var suggestedFilename: String {
        let base = sourceName.split(separator: ".").first.map(String.init) ?? "maze"
        return "\(base)-edited.\(format.ext)"
    }
}

/// A saved maze. Writing only — loading goes through E4MazeFile.load, which
/// sniffs the content rather than trusting the extension.
struct MazeDocument: FileDocument {
    static var readableContentTypes: [UTType] { [.plainText, .data] }
    var data: Data
    init(_ data: Data) { self.data = data }
    init(configuration: ReadConfiguration) throws {
        data = configuration.file.regularFileContents ?? Data()
    }
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        FileWrapper(regularFileWithContents: data)
    }
}

/// The canvas in edit mode. Deliberately a DIFFERENT view from MazeCanvas
/// rather than a flag inside it: nothing she is doing can leak into this
/// drawing, because none of it is referenced here.
struct MazeEditCanvas: View {
    let editor: MazeEditor

    /// Layout has to be derived identically for drawing and for hit testing, or
    /// the wall you tap is not the wall that lights up.
    private struct Layout {
        let cell: Double, ox: Double, oy: Double, rows: Double
        init(_ size: CGSize) {
            let n = Double(E4MazeFile.size)
            cell = min(size.width / (n + 0.5), size.height / (n + 0.5))
            ox = (size.width - cell * n) / 2
            oy = (size.height - cell * n) / 2
            rows = n
        }
        func point(_ cx: Double, _ cy: Double) -> CGPoint {
            CGPoint(x: ox + cx * cell, y: oy + (rows - cy) * cell)
        }
        /// Screen point -> cell, and which side of it was tapped. Inside the
        /// middle half of a cell there is no side: that is a centre tap, which
        /// is what the goal tool wants and the wall tool ignores.
        func hit(_ p: CGPoint) -> (x: Int, y: Int, side: E4MazeFile.Side?)? {
            let fx = (p.x - ox) / cell
            let fy = rows - (p.y - oy) / cell
            guard fx >= 0, fx < rows, fy >= 0, fy < rows else { return nil }
            let x = Int(fx), y = Int(fy)
            let dx = fx - Double(x), dy = fy - Double(y)
            let edges: [(Double, E4MazeFile.Side)] =
                [(dx, .west), (1 - dx, .east), (dy, .south), (1 - dy, .north)]
            guard let nearest = edges.min(by: { $0.0 < $1.0 }), nearest.0 < 0.25 else {
                return (x, y, nil)
            }
            return (x, y, nearest.1)
        }
    }

    var body: some View {
        GeometryReader { geo in
            let layout = Layout(geo.size)
            Canvas { context, size in
                let l = Layout(size)
                let n = E4MazeFile.size
                guard let f = editor.file else { return }

                // Posts, so the lattice reads as a grid even where it is empty.
                for gy in 0...n {
                    for gx in 0...n {
                        let p = l.point(Double(gx), Double(gy))
                        context.fill(Path(CGRect(x: p.x - 1.5, y: p.y - 1.5, width: 3, height: 3)),
                                     with: .color(Palette.Dark.line))
                    }
                }

                // Goal, then the start cell, under the walls.
                for g in f.goals {
                    let o = l.point(Double(g.x), Double(g.y + 1))
                    context.fill(Path(CGRect(x: o.x, y: o.y, width: l.cell, height: l.cell)),
                                 with: .color(Palette.Dark.good.opacity(0.22)))
                }
                let s = l.point(0, 1)
                context.fill(Path(CGRect(x: s.x, y: s.y, width: l.cell, height: l.cell)),
                             with: .color(Palette.Dark.warn.opacity(0.16)))

                // Walls. Drawn from one cell's opinion only -- the model keeps
                // the two sides equal, so drawing both would just double the ink.
                var path = Path()
                for y in 0..<n {
                    for x in 0..<n {
                        if f.hasWall(x: x, y: y, side: .north) {
                            path.move(to: l.point(Double(x), Double(y + 1)))
                            path.addLine(to: l.point(Double(x + 1), Double(y + 1)))
                        }
                        if f.hasWall(x: x, y: y, side: .west) {
                            path.move(to: l.point(Double(x), Double(y)))
                            path.addLine(to: l.point(Double(x), Double(y + 1)))
                        }
                        if x == n - 1, f.hasWall(x: x, y: y, side: .east) {
                            path.move(to: l.point(Double(n), Double(y)))
                            path.addLine(to: l.point(Double(n), Double(y + 1)))
                        }
                        if y == 0, f.hasWall(x: x, y: y, side: .south) {
                            path.move(to: l.point(Double(x), 0))
                            path.addLine(to: l.point(Double(x + 1), 0))
                        }
                    }
                }
                context.stroke(path, with: .color(Palette.Dark.ink),
                               style: StrokeStyle(lineWidth: max(2, l.cell * 0.10), lineCap: .round))
            }
            // DragGesture with no minimum rather than onTapGesture: it reports a
            // location on every platform this app runs on, and a tap is just a
            // drag that went nowhere.
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0).onEnded { v in
                guard let h = layout.hit(v.location) else { return }
                editor.tap(x: h.x, y: h.y, side: h.side)
            })
        }
        .background(Palette.Dark.bg)
        .overlay(alignment: .top) {
            // The canvas normally shows what she believes. While this is up it
            // does not, and that has to be impossible to miss.
            Text("EDITING \(editor.sourceName)\(editor.dirty ? " — unsaved" : "")")
                .font(.caption.monospaced())
                .padding(.horizontal, 10).padding(.vertical, 5)
                .background(Palette.Dark.warn.opacity(0.9), in: Capsule())
                .foregroundStyle(Color.black)
                .padding(8)
        }
        .overlay(
            Rectangle().strokeBorder(Palette.Dark.warn.opacity(0.7), lineWidth: 2)
        )
    }
}
