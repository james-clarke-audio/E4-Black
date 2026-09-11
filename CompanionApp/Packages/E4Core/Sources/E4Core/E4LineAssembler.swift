import Foundation

/// Reassembles complete text lines from the arbitrary byte chunks that arrive
/// as BLE notifications.
///
/// This exists because of the single most common bug in BLE-UART clients:
/// notification payloads are **not** line-aligned. A 20-byte notification can
/// carry half a line, two and a half lines, or a lone `\n` left over from the
/// previous packet. Parsing each notification as if it were a message loses
/// data and invents malformed ones.
///
/// Not thread-safe by design — own it from one place (the transport's queue).
public struct E4LineAssembler {

    /// Bytes seen since the last terminator.
    private var buffer: [UInt8] = []

    /// Hard cap so a mouse that never sends a newline (wedged, or a baud
    /// mismatch producing garbage) cannot grow this without bound.
    private let limit: Int

    /// Set when a line was truncated at `limit`, so the remainder of that
    /// line is discarded rather than emitted as a bogus short line.
    private var discardingOverlong = false

    public init(limit: Int = 4096) {
        self.limit = limit
    }

    /// Feed a received chunk; returns every complete line it completed.
    ///
    /// CR and LF both terminate. A CRLF pair yields one line, not two, because
    /// empty lines are never emitted.
    public mutating func append<C: Collection>(_ bytes: C) -> [String] where C.Element == UInt8 {
        var lines: [String] = []

        for byte in bytes {
            if byte == 0x0A || byte == 0x0D {       // LF or CR
                if discardingOverlong {
                    discardingOverlong = false
                    buffer.removeAll(keepingCapacity: true)
                    continue
                }
                if !buffer.isEmpty {
                    lines.append(Self.decode(buffer))
                    buffer.removeAll(keepingCapacity: true)
                }
                continue
            }

            if discardingOverlong { continue }

            buffer.append(byte)
            if buffer.count >= limit {
                discardingOverlong = true
                buffer.removeAll(keepingCapacity: true)
            }
        }

        return lines
    }

    /// Drop any partial line. Call on disconnect so a half-received line does
    /// not fuse onto the first line of the next session.
    public mutating func reset() {
        buffer.removeAll(keepingCapacity: false)
        discardingOverlong = false
    }

    /// The firmware emits 7-bit ASCII, but decode leniently: a corrupted byte
    /// from a baud mismatch should show up in the log as a replacement
    /// character, not silently drop the whole line.
    private static func decode(_ bytes: [UInt8]) -> String {
        if let s = String(bytes: bytes, encoding: .utf8) { return s }
        return String(decoding: bytes, as: UTF8.self)
    }
}
