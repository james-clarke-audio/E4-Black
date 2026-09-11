import Foundation

/// Turns one line of firmware output into an `E4Message`.
///
/// The protocol grew organically alongside the firmware, so it is not uniform:
/// most messages are strict CSV, but `SENS`, `GCAL` and `CFG` are human-readable
/// text that the app scrapes, and `ACT` has both a structured and a free-text
/// form sharing one tag. The decoder handles each on its own terms rather than
/// pretending they are all CSV.
public enum E4MessageDecoder {

    public static func decode(_ line: String) -> E4Message {
        let raw = line.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !raw.isEmpty else { return .text("") }

        let fields = raw.split(separator: ",", omittingEmptySubsequences: false).map(String.init)
        let tag = fields[0].uppercased()

        switch tag {

        case "POS":
            guard let x = dbl(fields, 1), let y = dbl(fields, 2), let d = dbl(fields, 3) else { break }
            return .pose(x: x, y: y, degrees: d)

        case "M":
            guard let x = int(fields, 1), let y = int(fields, 2), let h = int(fields, 3) else { break }
            // Heading arrives unnormalised in some paths; wrap it rather than fail.
            let heading = E4Heading(rawValue: ((h % 4) + 4) % 4) ?? .north
            return .cell(x: x, y: y, heading: heading)

        case "W":
            guard let x = int(fields, 1), let y = int(fields, 2), let m = int(fields, 3) else { break }
            return .walls(x: x, y: y, mask: E4WallMask(rawValue: m & 0xF))

        case "C":
            guard let x = int(fields, 1), let y = int(fields, 2), let c = int(fields, 3) else { break }
            return .cost(x: x, y: y, cost: c)

        case "SIZE":
            guard let w = int(fields, 1) else { break }
            return .size(width: w, height: int(fields, 2) ?? w)

        case "GOAL":
            var cells: [E4Cell] = []
            var i = 1
            while i + 1 < fields.count {
                if let x = int(fields, i), let y = int(fields, i + 1) {
                    cells.append(E4Cell(x: x, y: y))
                }
                i += 2
            }
            guard !cells.isEmpty else { break }
            return .goal(cells)

        case "SP":
            guard let x = int(fields, 1), let y = int(fields, 2) else { break }
            return .solutionCell(x: x, y: y)

        case "SOLVED":
            guard let ms = int(fields, 1), let steps = int(fields, 2) else { break }
            return .solved(milliseconds: ms, steps: steps)

        case "RST":
            return .resetMaze

        case "TEL":
            guard fields.count >= 8,
                  let t = int(fields, 1), let v = dbl(fields, 2), let om = dbl(fields, 3),
                  let d = dbl(fields, 4), let a = dbl(fields, 5), let g = dbl(fields, 6),
                  let b = dbl(fields, 7)
            else { break }
            return .telemetry(E4Telemetry(timestamp: t, velocity: v, omega: om,
                                          distance: d, angle: a, gyro: g, battery: b))

        case "IR":
            guard let l = int(fields, 1), let fl = int(fields, 2),
                  let fr = int(fields, 3), let r = int(fields, 4) else { break }
            return .ir(left: l, frontLeft: fl, frontRight: fr, right: r)

        case "SENS":
            if let status = decodeSensorStatus(raw) { return .sensorStatus(status) }

        case "EMIT":
            guard let i = int(fields, 1) else { break }
            return .emitter(index: i, name: fields.count > 2 ? fields[2] : "")

        case "STATE":
            return .state(fields.dropFirst().joined(separator: ",")
                .trimmingCharacters(in: .whitespaces))

        case "VER":
            let version = fields.count > 1 ? fields[1] : "?"
            let build = fields.dropFirst(2).joined(separator: ",")
                .trimmingCharacters(in: .whitespaces)
            return .version(firmware: version, build: build)

        case "CFG":
            return .config(decodeConfig(raw))

        case "GCAL":
            return .gyroCal(decodeGyroCal(raw))

        case "TURNRES":
            let pairs = keyValues(in: fields.dropFirst(2).joined(separator: ","))
            let kind = E4TurnResult.Kind(
                rawValue: (fields.count > 1 ? fields[1] : "").trimmingCharacters(in: .whitespaces)
            ) ?? .unknown
            return .turnResult(E4TurnResult(kind: kind,
                                            commanded: pairs["cmd"].flatMap(Double.init),
                                            gyro: pairs["gyro"].flatMap(Double.init),
                                            distance: pairs["dist"].flatMap(Double.init)))

        case "RUN":
            guard let i = int(fields, 1) else { break }
            return .runStarted(index: i, name: fields.dropFirst(2).joined(separator: ","))

        case "DONE":
            // `DONE,<idx> gyro_a=.. d=..` — index and detail share one field.
            let body = fields.dropFirst().joined(separator: ",")
            let head = body.prefix(while: { $0.isNumber || $0 == "-" })
            guard let i = Int(head) else { break }
            return .runFinished(index: i,
                                detail: String(body.dropFirst(head.count))
                                    .trimmingCharacters(in: .whitespaces))

        case "ACT":
            // Structured form is exactly 6 single-purpose fields with
            // single-character action and note. Everything else is a note.
            if fields.count == 6,
               let a = fields[1].first, fields[1].count == 1,
               let n = fields[2].first, fields[2].count == 1,
               let x = int(fields, 3), let y = int(fields, 4), let h = int(fields, 5),
               let heading = E4Heading(rawValue: ((h % 4) + 4) % 4) {
                return .action(E4Action(action: a, note: n, x: x, y: y, heading: heading))
            }
            return .actionNote(fields.dropFirst().joined(separator: ",")
                .trimmingCharacters(in: .whitespaces))

        default:
            if tag.hasPrefix("GT"), let truth = decodeMazeTruth(tag: tag, fields: fields) {
                return .mazeTruth(truth)
            }
        }

        return .text(raw)
    }

    // MARK: - The semi-structured lines

    /// `SENS,L265 FL199 FR142 R270 front341 thr(s60 f80) REAL`
    ///
    /// This line is formatted for a human reading a terminal, not for a parser,
    /// so it gets scraped — but by splitting on spaces rather than by regex,
    /// which keeps it obvious and total. Prefix order is load bearing: `FL` and
    /// `front` must be tested before the bare `L` and `f`, or `L199` matches
    /// inside `FL199`.
    private static func decodeSensorStatus(_ raw: String) -> E4SensorStatus? {
        let body = raw.dropFirst(min(5, raw.count))          // past "SENS,"
        var left = 0, fl = 0, fr = 0, right = 0, front = 0
        var sideThreshold: Int?
        var frontThreshold: Int?

        for token in body.split(separator: " ") {
            // Strip the bracketing of the `thr(s60 f80)` pair as we go.
            let t = token.trimmingCharacters(in: CharacterSet(charactersIn: "()"))

            if let v = value(t, after: "thr(s") ?? value(t, after: "thrs") { sideThreshold = v }
            else if let v = value(t, after: "FL")    { fl = v }
            else if let v = value(t, after: "FR")    { fr = v }
            else if let v = value(t, after: "front") { front = v }
            else if let v = value(t, after: "L")     { left = v }
            else if let v = value(t, after: "R")     { right = v }
            else if let v = value(t, after: "f")     { frontThreshold = v }
        }

        // Without the threshold pair this isn't a SENS status line at all.
        guard let side = sideThreshold, let frontThr = frontThreshold else { return nil }

        return E4SensorStatus(left: left,
                              frontLeft: fl,
                              frontRight: fr,
                              right: right,
                              frontSum: front == 0 ? fl + fr : front,
                              sideThreshold: side,
                              frontThreshold: frontThr,
                              usingRealIR: raw.contains("REAL"),
                              raw: raw)
    }

    /// The integer following `prefix`, if `token` starts with it and the rest
    /// is entirely digits. Returns nil otherwise, so the caller can try the
    /// next prefix.
    private static func value(_ token: String, after prefix: String) -> Int? {
        guard token.hasPrefix(prefix) else { return nil }
        let rest = token.dropFirst(prefix.count)
        guard !rest.isEmpty, rest.allSatisfy(\.isNumber) else { return nil }
        return Int(rest)
    }

    /// `CFG,loaded v1 gyro_scale=1.003` / `CFG,checksum-fail v1` / `CFG,gyro_scale out of range, ignored`
    private static func decodeConfig(_ raw: String) -> E4ConfigReport {
        let body = String(raw.dropFirst(min(4, raw.count))).trimmingCharacters(in: .whitespaces)
        let scale = firstDouble(after: "gyro_scale=", in: raw)
        let version = firstInt(after: "v", in: body)

        let outcome: E4ConfigReport.Outcome
        if body.hasPrefix("loaded") {
            outcome = .loaded(version: version ?? 0)
        } else if body.contains("checksum-fail") {
            outcome = .checksumFailure(version: version ?? 0)
        } else if body.contains("out of range") {
            outcome = .valueRejected
        } else {
            outcome = .other
        }
        return E4ConfigReport(outcome: outcome, gyroScale: scale, raw: body)
    }

    /// `GCAL,old=1.003,short=4,new=0.997 - S to save` and friends.
    private static func decodeGyroCal(_ raw: String) -> E4GyroCalReport {
        let body = String(raw.dropFirst(min(5, raw.count))).trimmingCharacters(in: .whitespaces)
        let pairs = keyValues(in: body)
        let savedFlag = pairs["saved"].flatMap(Int.init).map { $0 != 0 }

        return E4GyroCalReport(
            oldScale: pairs["old"].flatMap(Double.init),
            newScale: pairs["new"].flatMap(Double.init),
            setScale: pairs["set"].flatMap(Double.init),
            shortfall: pairs["short"].flatMap(Double.init),
            commandedDegrees: pairs["cmd"].flatMap(Double.init),
            gyroDegrees: pairs["gyro"].flatMap(Double.init),
            saved: savedFlag,
            rejected: body.contains("rejected"),
            raw: body
        )
    }

    private static func decodeMazeTruth(tag: String, fields: [String]) -> E4MazeTruth? {
        switch tag {
        case "GTOK":   return .ok
        case "GTERR":  return .error
        case "GTDONE": return .done
        case "GTGOK":
            guard let x = int(fields, 1), let y = int(fields, 2) else { return nil }
            return .goalAccepted(x: x, y: y)
        case "GTQ":
            guard let x = int(fields, 1), let y = int(fields, 2) else { return nil }
            return .goalReadback(x: x, y: y)
        case "GTV":
            guard let y = int(fields, 1), fields.count > 2 else { return nil }
            let masks = fields[2].compactMap { c -> E4WallMask? in
                guard let v = c.hexDigitValue else { return nil }
                return E4WallMask(rawValue: v)
            }
            return .row(y: y, masks: masks)
        default:
            return nil
        }
    }

    // MARK: - Small helpers

    /// Scrapes `key=value` pairs out of free text, split on commas or spaces.
    private static func keyValues(in text: String) -> [String: String] {
        var out: [String: String] = [:]
        for token in text.split(whereSeparator: { $0 == "," || $0 == " " }) {
            let parts = token.split(separator: "=", maxSplits: 1)
            guard parts.count == 2 else { continue }
            let key = parts[0].trimmingCharacters(in: .whitespaces)
            let value = parts[1].trimmingCharacters(in: .whitespaces)
            if out[key] == nil { out[key] = value }
        }
        return out
    }

    private static func int(_ f: [String], _ i: Int) -> Int? {
        guard i < f.count else { return nil }
        return Int(f[i].trimmingCharacters(in: .whitespaces))
    }

    private static func dbl(_ f: [String], _ i: Int) -> Double? {
        guard i < f.count else { return nil }
        return Double(f[i].trimmingCharacters(in: .whitespaces))
    }

    private static func firstDouble(after prefix: String, in text: String) -> Double? {
        guard let r = text.range(of: prefix) else { return nil }
        let tail = text[r.upperBound...].prefix { $0.isNumber || $0 == "." || $0 == "-" }
        return Double(tail)
    }

    private static func firstInt(after prefix: String, in text: String) -> Int? {
        guard let r = text.range(of: prefix) else { return nil }
        let tail = text[r.upperBound...].prefix { $0.isNumber }
        return Int(tail)
    }
}
