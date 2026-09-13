import Foundation

/// The three wall configurations the threshold calibration asks for.
///
/// They are states of the ARENA, not of the mouse — which is the whole reason a
/// picture beats a sentence here. "Corridor" in particular is easy to read as
/// "drive down a corridor" when what it means is "stand still with a wall each
/// side and nothing in front".
public enum E4CaptureState: String, Sendable, CaseIterable, Identifiable, Hashable {
    case deadEnd
    case openFloor
    case corridor

    public var id: String { rawValue }

    public var title: String {
        switch self {
        case .deadEnd:   return "Dead end"
        case .openFloor: return "Open floor"
        case .corridor:  return "Corridor"
        }
    }

    /// What has to be true of the arena for this capture to mean anything.
    public var instruction: String {
        switch self {
        case .deadEnd:   return "Walls on both sides and directly in front."
        case .openFloor: return "Nothing within range of any sensor."
        case .corridor:  return "A wall each side, nothing in front."
        }
    }

    /// Walls present in this configuration, from her point of view.
    public var walls: (left: Bool, front: Bool, right: Bool) {
        switch self {
        case .deadEnd:   return (true, true, true)
        case .openFloor: return (false, false, false)
        case .corridor:  return (true, false, true)
        }
    }

    /// The key that forces this capture, matching the firmware's own handling.
    public var key: Character {
        switch self {
        case .deadEnd:   return "P"
        case .openFloor: return "A"
        case .corridor:  return "C"
        }
    }

    /// The word the firmware prints in `THR,capturing <state>...`.
    static func fromFirmwareName(_ name: String) -> E4CaptureState? {
        switch name.uppercased() {
        case "DEAD END", "DEADEND":   return .deadEnd
        case "OPEN FLOOR", "OPENFLOOR": return .openFloor
        case "CORRIDOR":              return .corridor
        default:                      return nil
        }
    }
}

/// One channel's separation between wall-present and wall-absent.
public struct E4ThresholdMargin: Sendable, Equatable {
    public enum Channel: String, Sendable { case left = "L", right = "R", front = "F" }
    public enum Verdict: String, Sendable {
        case good = "good"
        case ok   = "ok"
        case poor = "POOR"
        case dead = "DEAD"

        /// A poor margin is not a number to nudge — it is a sensor that cannot
        /// separate the two states, and that is a mount problem.
        public var isTrustworthy: Bool { self == .good || self == .ok }
    }
    public let channel: Channel
    public let absent: Int
    public let present: Int
    public let verdict: Verdict

    public var separation: Int { present - absent }
}

/// A line from the threshold-calibration routine, decoded.
public enum E4ThresholdReport: Sendable, Equatable {
    case capturing(E4CaptureState)
    case captured(E4CaptureState, left: Int, right: Int, front: Int)
    case live(left: Int, right: Int, front: Int,
              seesLeft: Bool, seesFront: Bool, seesRight: Bool)
    case proposed(left: Int, right: Int, front: Int)
    case current(left: Int, right: Int, front: Int)
    case margins([E4ThresholdMargin])
    case frontBoundedByCorridor(corridor: Int, floor: Int)
    case saved(Bool)
    case rejected(String)
    case warning(String)
    case note(String)

    /// Text after `THR,`. Parsed from the body rather than from comma-split
    /// fields, because several of these lines contain commas of their own —
    /// "front bounded by CORRIDOR (341, floor was 22)" is one field to a human
    /// and two to a splitter.
    static func decode(body raw: String) -> E4ThresholdReport? {
        let body = raw.trimmingCharacters(in: .whitespaces)

        if body.hasPrefix("capturing") {
            let name = body
                .dropFirst("capturing".count)
                .replacingOccurrences(of: "...", with: "")
                .trimmingCharacters(in: .whitespaces)
            guard let state = E4CaptureState.fromFirmwareName(name) else { return nil }
            return .capturing(state)
        }

        if body.hasPrefix("live") {
            guard let v = triple(in: body) else { return nil }
            // The flags come after "-> " as three characters in L F R order,
            // a dash meaning no wall. Taken from her rather than recomputed
            // here: the firmware's own comparison is the one that decides.
            var l = false, f = false, r = false
            if let arrow = body.range(of: "-> ") {
                let flags = Array(body[arrow.upperBound...].trimmingCharacters(in: .whitespaces))
                if flags.count >= 3 {
                    l = flags[0] != "-"
                    f = flags[1] != "-"
                    r = flags[2] != "-"
                }
            }
            return .live(left: v.l, right: v.r, front: v.f,
                         seesLeft: l, seesFront: f, seesRight: r)
        }

        for state in E4CaptureState.allCases {
            let tag = firmwareTag(state)
            if body.hasPrefix(tag), let v = triple(in: body) {
                return .captured(state, left: v.l, right: v.r, front: v.f)
            }
        }

        if body.hasPrefix("new") || body.hasPrefix("set") {
            guard let v = triple(in: body) else { return nil }
            // "set ... saved=1" is the top-level THR command, which applies and
            // saves in one step; inside the routine it only proposes.
            if let saved = value(of: "saved", in: body) {
                return saved == 1 ? .saved(true) : .saved(false)
            }
            return .proposed(left: v.l, right: v.r, front: v.f)
        }

        if body.hasPrefix("now") {
            guard let v = triple(in: body) else { return nil }
            return .current(left: v.l, right: v.r, front: v.f)
        }

        if body.hasPrefix("margin") {
            let parts = body.dropFirst("margin".count).split(separator: "|")
            var margins: [E4ThresholdMargin] = []
            for part in parts {
                let tokens = part.split(separator: " ").map(String.init)
                guard tokens.count >= 3,
                      let channel = E4ThresholdMargin.Channel(rawValue: tokens[0]),
                      let verdict = E4ThresholdMargin.Verdict(rawValue: tokens[2]) else { continue }
                let range = tokens[1].split(separator: "-").map(String.init)
                guard range.count == 2, let a = Int(range[0]), let p = Int(range[1]) else { continue }
                margins.append(E4ThresholdMargin(channel: channel, absent: a,
                                                 present: p, verdict: verdict))
            }
            return margins.isEmpty ? nil : .margins(margins)
        }

        if body.hasPrefix("front bounded by CORRIDOR") {
            // Via a String: splitting [Character] gives ArraySlice, which Int
            // has no initialiser for. Substring does.
            let numbers = String(body.map { $0.isNumber ? $0 : " " })
                .split(separator: " ").compactMap { Int($0) }
            guard numbers.count >= 2 else { return nil }
            return .frontBoundedByCorridor(corridor: numbers[0], floor: numbers[1])
        }

        if body.hasPrefix("saved=") {
            return .saved(body.hasPrefix("saved=1"))
        }
        if body.hasPrefix("rejected") { return .rejected(body) }
        if body.hasPrefix("WARN")     { return .warning(String(body.dropFirst(4)).trimmingCharacters(in: .whitespaces)) }
        if body.hasPrefix("NOTE")     { return .note(String(body.dropFirst(4)).trimmingCharacters(in: .whitespaces)) }
        if body.hasPrefix("VIRTUAL")  { return .warning(body) }
        return nil
    }

    private static func firmwareTag(_ state: E4CaptureState) -> String {
        switch state {
        case .deadEnd:   return "deadend"
        case .openFloor: return "openfloor"
        case .corridor:  return "corridor"
        }
    }

    /// `l=.. r=.. f=..` in any of these lines.
    private static func triple(in body: String) -> (l: Int, r: Int, f: Int)? {
        guard let l = value(of: "l", in: body),
              let r = value(of: "r", in: body),
              let f = value(of: "f", in: body) else { return nil }
        return (l, r, f)
    }

    private static func value(of key: String, in body: String) -> Int? {
        // Whole-token match, so "f=" is not found inside "saved=" and "l=" is
        // not found inside a word that happens to end in l.
        for token in body.split(whereSeparator: { $0 == " " || $0 == "," }) {
            let parts = token.split(separator: "=", maxSplits: 1)
            guard parts.count == 2, parts[0] == key else { continue }
            let digits = parts[1].prefix { $0.isNumber || $0 == "-" }
            return Int(digits)
        }
        return nil
    }
}
