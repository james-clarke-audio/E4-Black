import SwiftUI
import E4Core

/// Her map, assembled from the messages she streams while she runs.
///
/// The decoder already turns every relevant line into a typed message; this is
/// what remembers them. Nothing here talks to the link — it only folds messages
/// in, so it can be driven from a replayed log just as easily as from a live
/// mouse.
@MainActor
@Observable
final class E4Maze {

    /// Arena bounds. She can be told to use a smaller arena for bench work
    /// (`SIZE,w,h`), and reports it back, so this is not fixed at 16.
    private(set) var width = 16
    private(set) var height = 16

    /// Wall bits per cell: N=1 E=2 S=4 W=8, matching the firmware's mask.
    private(set) var walls: [UInt8] = Array(repeating: 0, count: 16 * 16)

    /// Whether she has actually been somewhere — an unvisited cell is drawn
    /// differently from one she visited and found no walls in. Without this the
    /// map would claim knowledge it does not have.
    private(set) var known: [Bool] = Array(repeating: false, count: 16 * 16)

    private(set) var cost: [Int?] = Array(repeating: nil, count: 16 * 16)
    private(set) var goals: Set<Cell> = []
    private(set) var solution: [Cell] = []

    private(set) var pose: Pose?
    private(set) var cell: Cell?
    private(set) var heading: E4Heading = .north

    /// Dead-reckoned breadcrumbs. Capped: a long search would otherwise grow
    /// this without bound and slow the draw down.
    private(set) var trail: [Pose] = []
    private let trailLimit = 2000

    private(set) var solvedMilliseconds: Int?
    private(set) var solvedSteps: Int?

    struct Cell: Hashable { let x: Int; let y: Int }
    struct Pose: Equatable { let x: Double; let y: Double; let degrees: Double }

    /// Cell pitch in mm. Poses arrive in absolute mm from the arena's SW
    /// corner, so this is what maps one onto the other.
    static let cellMM = 180.0

    // MARK: - Wiring

    /// Subscribe to a session. `onMessage` is a single slot, so whoever calls
    /// this owns it — fine while the maze is the only thing that wants a tap.
    func attach(to session: E4Session) {
        session.onMessage = { [weak self] message in
            self?.apply(message)
        }
    }

    // MARK: - Folding messages in

    func apply(_ message: E4Message) {
        switch message {
        case .resetMaze:
            reset()

        case .size(let w, let h):
            resize(width: w, height: h)

        case .walls(let x, let y, let mask):
            setWalls(x: x, y: y, mask: UInt8(mask.rawValue & 0xF))

        case .cost(let x, let y, let c):
            if let i = index(x, y) { cost[i] = c }

        case .goal(let cells):
            goals = Set(cells.map { Cell(x: $0.x, y: $0.y) })

        case .cell(let x, let y, let h):
            cell = Cell(x: x, y: y)
            heading = h
            // Only drive the marker from the logical cell when there is no
            // dead-reckoned pose — POS is the better source when it is coming.
            if pose == nil {
                pose = Pose(x: (Double(x) + 0.5) * Self.cellMM,
                            y: (Double(y) + 0.5) * Self.cellMM,
                            degrees: h.degrees)
            }

        case .pose(let x, let y, let deg):
            let p = Pose(x: x, y: y, degrees: deg)
            pose = p
            if trail.last != p {
                trail.append(p)
                if trail.count > trailLimit { trail.removeFirst(trail.count - trailLimit) }
            }

        case .solutionCell(let x, let y):
            solution.append(Cell(x: x, y: y))

        case .solved(let ms, let steps):
            solvedMilliseconds = ms
            solvedSteps = steps

        case .mazeTruth(.row(let y, let masks)):
            // The read-back after a truth upload. Treated as known: it is the
            // maze you just told her about, not something she discovered.
            for (x, mask) in masks.enumerated() {
                setWalls(x: x, y: y, mask: UInt8(mask.rawValue & 0xF))
            }

        case .runStarted(let index, _):
            // A fresh search or explore starts a fresh picture. Simulation runs
            // are left alone — they replay a map she already has.
            if let action = E4MenuAction(rawValue: index),
               action == .search || action == .explore {
                clearRun()
            }

        default:
            break
        }
    }

    // MARK: - Mutation

    /// Walls are shared between neighbouring cells, so a wall reported from one
    /// side is written to both. Skipping the mirror leaves a maze whose walls
    /// appear and vanish depending on which way she passed.
    private func setWalls(x: Int, y: Int, mask: UInt8) {
        guard let i = index(x, y) else { return }
        walls[i] |= mask
        known[i] = true

        if mask & 1 != 0, let n = index(x, y + 1) { walls[n] |= 4; known[n] = true }
        if mask & 4 != 0, let n = index(x, y - 1) { walls[n] |= 1; known[n] = true }
        if mask & 2 != 0, let n = index(x + 1, y) { walls[n] |= 8; known[n] = true }
        if mask & 8 != 0, let n = index(x - 1, y) { walls[n] |= 2; known[n] = true }
    }

    private func resize(width w: Int, height h: Int) {
        guard w > 0, h > 0, w <= 32, h <= 32, w != width || h != height else { return }
        width = w
        height = h
        reset()
    }

    /// Everything she has discovered. Used on RST and on a resize.
    func reset() {
        let n = width * height
        walls = Array(repeating: 0, count: n)
        known = Array(repeating: false, count: n)
        cost = Array(repeating: nil, count: n)
        goals = []
        clearRun()
    }

    /// The parts that belong to one run, keeping the discovered map.
    private func clearRun() {
        solution = []
        trail = []
        solvedMilliseconds = nil
        solvedSteps = nil
    }

    // MARK: - Reading

    func index(_ x: Int, _ y: Int) -> Int? {
        guard x >= 0, x < width, y >= 0, y < height else { return nil }
        return y * width + x
    }

    func wallMask(_ x: Int, _ y: Int) -> UInt8 {
        index(x, y).map { walls[$0] } ?? 0
    }

    func isKnown(_ x: Int, _ y: Int) -> Bool {
        index(x, y).map { known[$0] } ?? false
    }

    var mappedCells: Int { known.filter { $0 }.count }
}
