import SwiftUI
import E4Core
import UniformTypeIdentifiers

struct MazeScreen: View {
    @Environment(E4Session.self) private var session
    @Environment(E4Maze.self) private var maze
    @Environment(E4MazeUploader.self) private var uploader

    @State private var picking = false
    @State private var loaded: E4MazeFile?
    @State private var loadedName = ""
    @State private var flipped = false
    @State private var loadError: String?

    /// What would actually be sent — the file, turned over if the orientation
    /// switch is on. Nothing in a maze file records which way up it was written.
    private var outgoing: E4MazeFile? {
        guard let loaded else { return nil }
        return flipped ? loaded.flippedVertically() : loaded
    }

    var body: some View {
        HStack(spacing: 0) {
            MazeCanvas()
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(Palette.Dark.bg)

            Rectangle().fill(Palette.line).frame(width: 1)

            side
                .frame(width: 244)
                .background(Palette.panel)
        }
    }

    private var side: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                Card("Pose") {
                    if let pose = maze.pose {
                        FieldRow(key: "x", value: String(format: "%.0f mm", pose.x))
                        FieldRow(key: "y", value: String(format: "%.0f mm", pose.y))
                        FieldRow(key: "heading", value: String(format: "%.0f°", pose.degrees))
                    } else {
                        Text("No pose reported yet.")
                            .font(.callout)
                            .foregroundStyle(Palette.dim)
                    }
                    if let cell = maze.cell {
                        FieldRow(key: "cell", value: "\(cell.x), \(cell.y)")
                    }
                }

                Card("Arena") {
                    FieldRow(key: "size", value: "\(maze.width) × \(maze.height)")
                    FieldRow(key: "mapped", value: "\(maze.mappedCells) cells")
                    if !maze.goals.isEmpty {
                        FieldRow(key: "goal", value: goalSummary)
                    }
                    if let ms = maze.solvedMilliseconds, let steps = maze.solvedSteps {
                        Divider()
                        FieldRow(key: "solved", value: String(format: "%.1f s", Double(ms) / 1000))
                        FieldRow(key: "path", value: "\(steps) cells")
                    }
                }

                importCard

                Card("Run") {
                    Button("Search") { confirm = .search }
                        .buttonStyle(.borderedProminent)
                        .frame(maxWidth: .infinity)
                        .touchTarget()
                    Button("Explore") { confirm = .explore }
                        .buttonStyle(.bordered)
                        .frame(maxWidth: .infinity)
                        .touchTarget()
                    Button("Speed run") { confirm = .speedRun }
                        .buttonStyle(.bordered)
                        .frame(maxWidth: .infinity)
                        .touchTarget()

                    Text("Place her back edge against the wall before Search — she drives 49 mm forward, then declares centre.")
                        .font(.caption)
                        .foregroundStyle(Palette.faint)

                    Divider()

                    Button("Reset pose") { session.send(.action(.resetPose)) }
                        .buttonStyle(.bordered)
                        .frame(maxWidth: .infinity)
                        .touchTarget()
                    Button("Clear map") { maze.reset() }
                        .buttonStyle(.bordered)
                        .frame(maxWidth: .infinity)
                        .touchTarget()
                }
            }
            .padding(16)
        }
        .confirmationDialog(
            confirm.map { "Run \($0.title)?" } ?? "",
            isPresented: .init(get: { confirm != nil }, set: { if !$0 { confirm = nil } }),
            titleVisibility: .visible
        ) {
            Button("Run", role: .destructive) {
                if let action = confirm { session.send(.action(action)) }
                confirm = nil
            }
            Button("Cancel", role: .cancel) { confirm = nil }
        } message: {
            Text("This moves the mouse. Make sure she has room.")
        }
    }

    @State private var confirm: E4MenuAction?

    // MARK: - Maze file import

    private var importCard: some View {
        Card("Maze file") {
            if let file = outgoing {
                MazeFilePreview(file: file)
                    .frame(height: 128)
                    .background(Palette.Dark.bg, in: RoundedRectangle(cornerRadius: 6))

                Text(loadedName)
                    .font(.system(size: 10.5, design: .monospaced))
                    .foregroundStyle(Palette.faint)
                    .lineLimit(1)
                    .truncationMode(.middle)

                HStack(spacing: 6) {
                    Text(file.format == .ascii ? "ascii" : "bytes")
                    Text("·")
                    Text("goal \(file.chosenGoal.x),\(file.chosenGoal.y)")
                }
                .font(.system(size: 10, design: .monospaced))
                .foregroundStyle(Palette.faint)

                // Nothing in a maze file says which way up it was written, so
                // this stays a switch you flip after looking at the drawing
                // rather than something guessed from the contents.
                Toggle("Flip vertically", isOn: $flipped)
                    .font(.callout)
                    .toggleStyle(.switch)

                if file.looksDegenerate {
                    Text("Every cell came out identical — that is a parse that latched onto the wrong thing, not a maze. Try the flip, or send me the file.")
                        .font(.caption)
                        .foregroundStyle(Palette.bad)
                }
            } else {
                Text("Load a maze from a file and send it to her as ground truth, then Simulate to watch her solve it with no arena in front of you.")
                    .font(.callout)
                    .foregroundStyle(Palette.dim)
            }

            if let loadError {
                Text(loadError)
                    .font(.caption)
                    .foregroundStyle(Palette.bad)
            }

            HStack(spacing: 8) {
                Button("Load…") { picking = true }
                    .buttonStyle(.bordered)
                    .touchTarget()
                    .disabled(uploader.isUploading)
                Button("Send to mouse") { Task { await sendMaze() } }
                    .buttonStyle(.borderedProminent)
                    .touchTarget()
                    .disabled(outgoing == nil || uploader.isUploading
                              || !session.connection.isConnected)
            }

            uploadStatus
        }
        .fileImporter(isPresented: $picking,
                      allowedContentTypes: [.plainText, .data],
                      allowsMultipleSelection: false) { result in
            handlePick(result)
        }
    }

    @ViewBuilder
    private var uploadStatus: some View {
        switch uploader.phase {
        case .idle:
            EmptyView()
        case .clearing:
            uploadLine("Clearing her map…", Palette.dim)
        case .sendingRow(let y):
            uploadLine("Row \(y + 1) of 16…", Palette.dim)
        case .sendingGoal:
            uploadLine("Setting the goal…", Palette.dim)
        case .verifying:
            uploadLine("Reading it back…", Palette.dim)
        case .finished(let result):
            Text(result.summary)
                .font(.caption)
                .foregroundStyle(result.isClean ? Palette.good : Palette.warn)
        case .failed(let reason):
            Text(reason)
                .font(.caption)
                .foregroundStyle(Palette.bad)
        }
    }

    private func uploadLine(_ text: String, _ tint: Color) -> some View {
        HStack(spacing: 7) {
            ProgressView().controlSize(.small)
            Text(text).font(.caption).foregroundStyle(tint)
        }
    }

    private func sendMaze() async {
        guard let outgoing else { return }
        await uploader.upload(outgoing)
    }

    /// The picker hands back a security-scoped URL on both platforms — reading
    /// it without starting access works in the simulator and fails on a real
    /// sandboxed build, which is the worst way to find out.
    private func handlePick(_ result: Result<[URL], Error>) {
        loadError = nil
        uploader.reset()
        do {
            guard let url = try result.get().first else { return }
            let scoped = url.startAccessingSecurityScopedResource()
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }

            let data = try Data(contentsOf: url)
            guard let file = E4MazeFile.load(data) else {
                loadError = "Could not read \(url.lastPathComponent) as a maze."
                loaded = nil
                return
            }
            loaded = file
            loadedName = url.lastPathComponent
            flipped = false
        } catch {
            loadError = error.localizedDescription
            loaded = nil
        }
    }

    private var goalSummary: String {
        let xs = maze.goals.map(\.x), ys = maze.goals.map(\.y)
        guard let x0 = xs.min(), let x1 = xs.max(),
              let y0 = ys.min(), let y1 = ys.max() else { return "—" }
        return x0 == x1 && y0 == y1 ? "\(x0),\(y0)" : "\(x0),\(y0) – \(x1),\(y1)"
    }
}

/// The map itself.
///
/// Drawn in a single `Canvas` pass rather than as stacked shapes: a 16 × 16
/// arena is 256 cells, ~500 wall segments and a trail of hundreds of points,
/// and building that many views per frame is what makes a maze view stutter.
struct MazeCanvas: View {
    @Environment(E4Maze.self) private var maze

    var body: some View {
        Canvas { context, size in
            let cols = Double(maze.width)
            let rows = Double(maze.height)
            let cell = min(size.width / (cols + 0.5), size.height / (rows + 0.5))
            let boardW = cell * cols
            let boardH = cell * rows
            let ox = (size.width - boardW) / 2
            let oy = (size.height - boardH) / 2

            // Maze y runs up from the SW corner; screen y runs down.
            func point(cellX: Double, cellY: Double) -> CGPoint {
                CGPoint(x: ox + cellX * cell, y: oy + (rows - cellY) * cell)
            }
            func point(mmX: Double, mmY: Double) -> CGPoint {
                point(cellX: mmX / E4Maze.cellMM, cellY: mmY / E4Maze.cellMM)
            }

            // Cells she has not visited, so the map never implies knowledge it
            // does not have.
            for y in 0..<maze.height {
                for x in 0..<maze.width where !maze.isKnown(x, y) {
                    let origin = point(cellX: Double(x), cellY: Double(y + 1))
                    context.fill(
                        Path(CGRect(x: origin.x, y: origin.y, width: cell, height: cell)),
                        with: .color(Palette.Dark.panel)
                    )
                }
            }

            // Goal
            for goal in maze.goals {
                let origin = point(cellX: Double(goal.x), cellY: Double(goal.y + 1))
                context.fill(
                    Path(CGRect(x: origin.x, y: origin.y, width: cell, height: cell)),
                    with: .color(Palette.good.opacity(0.22))
                )
            }

            // Flood costs, when she is streaming them
            for y in 0..<maze.height {
                for x in 0..<maze.width {
                    guard let i = maze.index(x, y), let c = maze.cost[i] else { continue }
                    let centre = point(cellX: Double(x) + 0.5, cellY: Double(y) + 0.5)
                    context.draw(
                        Text("\(c)")
                            .font(.system(size: max(7, cell * 0.28), design: .monospaced))
                            .foregroundColor(Palette.Dark.faint),
                        at: centre
                    )
                }
            }

            // Posts
            let post = max(2.0, cell * 0.09)
            for x in 0...maze.width {
                for y in 0...maze.height {
                    let p = point(cellX: Double(x), cellY: Double(y))
                    context.fill(
                        Path(CGRect(x: p.x - post / 2, y: p.y - post / 2, width: post, height: post)),
                        with: .color(Palette.Dark.line)
                    )
                }
            }

            // Walls
            var wallPath = Path()
            for y in 0..<maze.height {
                for x in 0..<maze.width {
                    let mask = maze.wallMask(x, y)
                    let sw = point(cellX: Double(x), cellY: Double(y))
                    let ne = point(cellX: Double(x + 1), cellY: Double(y + 1))
                    if mask & 1 != 0 { wallPath.move(to: CGPoint(x: sw.x, y: ne.y)); wallPath.addLine(to: ne) }
                    if mask & 4 != 0 { wallPath.move(to: sw); wallPath.addLine(to: CGPoint(x: ne.x, y: sw.y)) }
                    if mask & 2 != 0 { wallPath.move(to: CGPoint(x: ne.x, y: sw.y)); wallPath.addLine(to: ne) }
                    if mask & 8 != 0 { wallPath.move(to: sw); wallPath.addLine(to: CGPoint(x: sw.x, y: ne.y)) }
                }
            }
            context.stroke(wallPath, with: .color(Palette.Dark.dim),
                           style: StrokeStyle(lineWidth: max(2, cell * 0.09), lineCap: .square))

            // Solved path
            if maze.solution.count > 1 {
                var path = Path()
                for (i, c) in maze.solution.enumerated() {
                    let p = point(cellX: Double(c.x) + 0.5, cellY: Double(c.y) + 0.5)
                    i == 0 ? path.move(to: p) : path.addLine(to: p)
                }
                context.stroke(path, with: .color(Palette.Dark.sent),
                               style: StrokeStyle(lineWidth: max(1.5, cell * 0.06),
                                                  dash: [cell * 0.16, cell * 0.16]))
            }

            // Trail — where she believes she has been
            if maze.trail.count > 1 {
                var path = Path()
                for (i, p) in maze.trail.enumerated() {
                    let pt = point(mmX: p.x, mmY: p.y)
                    i == 0 ? path.move(to: pt) : path.addLine(to: pt)
                }
                context.stroke(path, with: .color(Palette.Dark.warn.opacity(0.85)),
                               style: StrokeStyle(lineWidth: max(1.5, cell * 0.05),
                                                  lineCap: .round, lineJoin: .round))
            }

            // Her
            if let pose = maze.pose {
                let centre = point(mmX: pose.x, mmY: pose.y)
                let r = cell * 0.3
                var nose = Path()
                nose.move(to: CGPoint(x: 0, y: -r))
                nose.addLine(to: CGPoint(x: -r * 0.8, y: r * 0.8))
                nose.addLine(to: CGPoint(x: r * 0.8, y: r * 0.8))
                nose.closeSubpath()

                context.drawLayer { layer in
                    layer.translateBy(x: centre.x, y: centre.y)
                    // Heading 0 is north and screen-up is north, so the angle
                    // goes straight through — but it is measured clockwise.
                    layer.rotate(by: .degrees(-pose.degrees))
                    layer.fill(nose, with: .color(Palette.Dark.warn))
                }
            }
        }
        .drawingGroup()
    }
}


/// A small drawing of a maze FILE — not of her map.
///
/// It exists for one question only: is this the right way up? A maze that
/// parsed upside down looks entirely plausible as a maze, and the only cheap
/// way to catch it is to look at it before it is sent.
struct MazeFilePreview: View {
    let file: E4MazeFile

    var body: some View {
        Canvas { context, size in
            let n = E4MazeFile.size
            let inset: CGFloat = 4
            let side = min(size.width, size.height) - inset * 2
            guard side > 0 else { return }
            let cell = side / CGFloat(n)
            let x0 = (size.width - side) / 2
            let y0 = (size.height - side) / 2

            var path = Path()
            for y in 0..<n {
                for x in 0..<n {
                    let m = file.mask(x: x, y: y)
                    // y runs north-up in the data and down the screen, so the
                    // row is mirrored here rather than in the parser.
                    let left = x0 + CGFloat(x) * cell
                    let top = y0 + CGFloat(n - 1 - y) * cell
                    if m & 1 != 0 {
                        path.move(to: CGPoint(x: left, y: top))
                        path.addLine(to: CGPoint(x: left + cell, y: top))
                    }
                    if m & 4 != 0 {
                        path.move(to: CGPoint(x: left, y: top + cell))
                        path.addLine(to: CGPoint(x: left + cell, y: top + cell))
                    }
                    if m & 8 != 0 {
                        path.move(to: CGPoint(x: left, y: top))
                        path.addLine(to: CGPoint(x: left, y: top + cell))
                    }
                    if m & 2 != 0 {
                        path.move(to: CGPoint(x: left + cell, y: top))
                        path.addLine(to: CGPoint(x: left + cell, y: top + cell))
                    }
                }
            }
            context.stroke(path, with: .color(Palette.Dark.ink.opacity(0.85)),
                           style: StrokeStyle(lineWidth: 1, lineCap: .square))

            // Start and goal, so "upside down" is obvious at a glance rather
            // than something you have to reason about from the wall pattern.
            let goal = file.chosenGoal
            let g = CGRect(x: x0 + CGFloat(goal.x) * cell + 1,
                           y: y0 + CGFloat(n - 1 - goal.y) * cell + 1,
                           width: cell - 2, height: cell - 2)
            context.fill(Path(ellipseIn: g), with: .color(Palette.Dark.sent))

            let s = CGRect(x: x0 + 1,
                           y: y0 + CGFloat(n - 1) * cell + 1,
                           width: cell - 2, height: cell - 2)
            context.fill(Path(ellipseIn: s), with: .color(Palette.Dark.warn))
        }
    }
}
