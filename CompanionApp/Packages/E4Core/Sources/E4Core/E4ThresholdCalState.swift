import Foundation

/// What the threshold calibration has told us so far.
///
/// The routine is a conversation — capture, reposition, capture — and the app's
/// job is to show where you are in it. Keeping that as state means a view can
/// ask "which capture is outstanding" instead of scanning the transcript for
/// the last line that looked relevant.
public struct E4ThresholdCalState: Sendable, Equatable {

    /// Medians from each capture that has been taken.
    public private(set) var captures: [E4CaptureState: Reading] = [:]

    /// The capture she is taking right now, if any.
    public private(set) var capturing: E4CaptureState?

    /// The most recent live reading, which arrives at about 5 Hz while the
    /// routine is open.
    public private(set) var live: Live?

    public private(set) var proposed: Reading?
    public private(set) var margins: [E4ThresholdMargin] = []
    /// A struct, not a tuple: Equatable is not synthesised through a tuple
    /// property, and this type has to be Equatable for @Observable to see a
    /// change as a change.
    public private(set) var frontBound: FrontBound?
    public private(set) var lastSaveSucceeded: Bool?
    public private(set) var problems: [String] = []

    public struct Reading: Sendable, Equatable {
        public let left: Int
        public let right: Int
        public let front: Int
        public init(left: Int, right: Int, front: Int) {
            self.left = left; self.right = right; self.front = front
        }
    }

    public struct Live: Sendable, Equatable {
        public let left: Int
        public let right: Int
        public let front: Int
        public let seesLeft: Bool
        public let seesFront: Bool
        public let seesRight: Bool
        public init(left: Int, right: Int, front: Int,
                    seesLeft: Bool, seesFront: Bool, seesRight: Bool) {
            self.left = left; self.right = right; self.front = front
            self.seesLeft = seesLeft; self.seesFront = seesFront; self.seesRight = seesRight
        }
    }

    /// What the front threshold had to clear, and what the bare floor was.
    public struct FrontBound: Sendable, Equatable {
        public let corridor: Int
        public let floor: Int
    }

    public init() {}

    /// The next capture to ask for, in the order the firmware's own button
    /// cycling uses. Nil once all three are in.
    public var nextOutstanding: E4CaptureState? {
        E4CaptureState.allCases.first { captures[$0] == nil }
    }

    public var isComplete: Bool { nextOutstanding == nil }

    public func has(_ state: E4CaptureState) -> Bool { captures[state] != nil }

    /// The configuration the diagram should be showing: whichever capture is
    /// being taken, else the next one outstanding.
    public var targetState: E4CaptureState? { capturing ?? nextOutstanding }

    public mutating func apply(_ report: E4ThresholdReport) {
        switch report {
        case .capturing(let state):
            capturing = state

        case .captured(let state, let l, let r, let f):
            captures[state] = Reading(left: l, right: r, front: f)
            capturing = nil

        case .live(let l, let r, let f, let sl, let sf, let sr):
            live = Live(left: l, right: r, front: f,
                        seesLeft: sl, seesFront: sf, seesRight: sr)

        case .proposed(let l, let r, let f):
            proposed = Reading(left: l, right: r, front: f)
            lastSaveSucceeded = nil          // proposed but not yet committed

        case .current:
            break                            // the live thresholds; the strip shows those

        case .margins(let m):
            margins = m

        case .frontBoundedByCorridor(let corridor, let floor):
            frontBound = FrontBound(corridor: corridor, floor: floor)

        case .saved(let ok):
            lastSaveSucceeded = ok

        case .rejected(let text), .warning(let text):
            // Kept rather than replaced: a warning about the mounts and a
            // warning about the front channel are different problems and both
            // matter. Bounded, because the routine can be left running.
            if !problems.contains(text) { problems.append(text) }
            if problems.count > 6 { problems.removeFirst(problems.count - 6) }

        case .note:
            break
        }
    }

    /// Called when the routine starts, so a second run does not inherit the
    /// first one's captures — which would silently mix two geometries.
    public mutating func reset() { self = E4ThresholdCalState() }
}
