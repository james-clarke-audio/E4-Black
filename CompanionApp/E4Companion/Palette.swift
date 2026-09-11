import SwiftUI

/// The app's colours, lifted from the web app's light chrome.
///
/// These are not system colours and the app does not follow the system theme.
/// That is a decision already made once, on the web app: the dark panel made
/// small text hard to read at the bench, and the fix was a light surface with
/// properly contrasted ink. Following the Mac's appearance would undo it on a
/// dark-mode machine.
///
/// The semantic colours are the light-chrome variants too. System `.green` and
/// `.orange` are tuned to read on dark and go shrill on a pale card — these sit
/// at 4.5:1 or better against `panel`.
enum Palette {

    // Surfaces, lightest content outward
    static let card    = Color(hex: 0xFFFFFF)
    static let panel   = Color(hex: 0xF3F6F9)
    static let panel2  = Color(hex: 0xE5EBF2)
    static let sidebar = Color(hex: 0xE9EEF4)
    static let line    = Color(hex: 0xC9D3DE)

    // Ink. `faint` is 4.8:1 on panel — the web app's was 2.3:1 and that is
    // exactly what made the old readout hard to read.
    static let ink   = Color(hex: 0x0F1822)
    static let dim   = Color(hex: 0x4A5A6E)
    static let faint = Color(hex: 0x5F6E80)

    // Semantic
    static let accent = Color(hex: 0x1266C9)
    static let good   = Color(hex: 0x0F7A48)
    static let warn   = Color(hex: 0x96550A)
    static let bad    = Color(hex: 0xBF2D2D)

    // The two surfaces that stay dark, in the app as in the web app: a maze
    // floor is black, and a scrolling transcript reads better light-on-dark.
    enum Dark {
        static let bg    = Color(hex: 0x0E1116)
        static let panel = Color(hex: 0x161B22)
        static let line  = Color(hex: 0x2A3340)
        static let ink   = Color(hex: 0xE6EDF3)
        static let dim   = Color(hex: 0x8B98A8)
        static let faint = Color(hex: 0x4B5563)
        static let sent  = Color(hex: 0x4AA3FF)
        static let warn  = Color(hex: 0xF0A04B)
    }
}

extension Color {
    /// 0xRRGGBB, so the values above can be pasted straight from the web app's
    /// stylesheet without translating each one.
    init(hex: UInt32) {
        self.init(
            .sRGB,
            red:   Double((hex >> 16) & 0xFF) / 255,
            green: Double((hex >> 8) & 0xFF) / 255,
            blue:  Double(hex & 0xFF) / 255,
            opacity: 1
        )
    }
}
