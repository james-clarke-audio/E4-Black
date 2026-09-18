import Foundation

/// One row of her turn table, as she reports it.
///
/// Sent by `TRN,<i>,<name>,v=..,in=..,ex=..,out=..,a=..,w=..,al=..` in response
/// to `CFG?`. Twelve of the sixteen drive nothing yet — there is no diagonal
/// navigation to call them — and that is precisely why they are worth dumping:
/// a slot you cannot inspect is a slot you cannot trust.
public struct E4Turn: Sendable, Equatable, Identifiable {
    public let index: Int
    public let name: String
    public let speed: Int
    public let entryOffset: Int
    public let exitOffset: Int
    public let leadOut: Int
    public let angle: Int
    public let omega: Int
    public let alpha: Int

    public var id: Int { index }

    /// The radius the omega implies, since that is what you actually reason
    /// about at a bench. R = v / omega, omega in radians.
    public var radiusMM: Double {
        guard omega != 0 else { return 0 }
        return Double(speed) / (Double(omega) * .pi / 180)
    }

    /// Half a cell, or nothing, depending on where the turn is booked in.
    ///
    /// `entry_offset` is measured back from the LATTICE POINT the planner
    /// applies the turn at. For most turns that point is also where the two
    /// lanes actually cross, so the two agree. Not for SD45 and SD135: those
    /// are booked in at a CELL CENTRE, but a straight lane meets a diagonal at
    /// the WALL MIDPOINT, 90 mm earlier. She has to start the arc before she
    /// gets there, so half a cell has to be added on top of the tangent.
    public var crossingOffsetMM: Double {
        switch index {
        case 6, 7, 10, 11: return 90.0      // SD45L/R, SD135L/R
        default:           return 0.0
        }
    }

    /// The tangent length alone: an arc of radius R joining two lanes that
    /// cross at theta is tangent to both only R*tan(theta/2) either side of
    /// the crossing. A fact about circles.
    public var tangentMM: Double {
        let theta = abs(Double(angle)) * .pi / 180
        guard theta > 0, theta < .pi else { return 0 }
        return radiusMM * tan(theta / 2)
    }

    /// What `entry_offset` has to be for this row to be self-consistent.
    public var requiredEntryMM: Double { crossingOffsetMM + tangentMM }

    /// How far the stored offset is from that, in millimetres. Positive means
    /// she starts the arc too early.
    public var tangentErrorMM: Double { Double(entryOffset) - requiredEntryMM }

    /// True for the four turns anything currently drives. The rest are slots.
    public var isDriven: Bool { index <= 3 }

    /// Left turns are positive in the firmware's sign convention.
    public var isLeft: Bool { angle > 0 }

    static func decode(_ raw: String) -> E4Turn? {
        // TRN,<i>,<name>,k=v,...
        let fields = raw.split(separator: ",").map(String.init)
        guard fields.count >= 3, fields[0] == "TRN",
              let index = Int(fields[1]) else { return nil }
        let name = fields[2]

        var pairs: [String: Int] = [:]
        for field in fields.dropFirst(3) {
            let kv = field.split(separator: "=", maxSplits: 1).map(String.init)
            guard kv.count == 2, let value = Int(kv[1]) else { continue }
            pairs[kv[0]] = value
        }
        guard let v = pairs["v"], let inn = pairs["in"], let ex = pairs["ex"],
              let out = pairs["out"], let a = pairs["a"], let w = pairs["w"],
              let al = pairs["al"] else { return nil }

        return E4Turn(index: index, name: name, speed: v, entryOffset: inn,
                      exitOffset: ex, leadOut: out, angle: a, omega: w, alpha: al)
    }
}

/// The whole configuration, as reported by `CFG?`.
///
/// Collected across several lines, so it is only complete when `isComplete`
/// says so — showing a half-arrived dump would be worse than showing nothing,
/// because it would look like a configuration rather than a fragment.
public struct E4ConfigDump: Sendable, Equatable {
    public var version: Int?
    public var eepromPresent: Bool?
    public var blockLoaded: Bool?
    public var expectedTurns: Int?
    public var turns: [E4Turn] = []
    public var receivedEnd = false

    public var isComplete: Bool {
        receivedEnd && (expectedTurns.map { turns.count == $0 } ?? false)
    }

    public mutating func reset() { self = E4ConfigDump() }
}
