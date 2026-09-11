import SwiftUI
import E4Core

/// The sidebar's sections.
///
/// `actions` is not in the original mockup and had to be added: the drawn
/// screens gave a home to about eight menu actions, and the firmware has
/// twenty-nine. It keeps the name the web app already uses for the same thing.
enum E4Screen: String, CaseIterable, Identifiable {
    case maze
    case sensors
    case actions
    case tuning
    case calibration
    case history
    case log

    var id: String { rawValue }

    var title: String {
        switch self {
        case .maze:        return "Maze"
        case .sensors:     return "Sensors"
        case .actions:     return "Actions"
        case .tuning:      return "Tuning"
        case .calibration: return "Calibration"
        case .history:     return "History"
        case .log:         return "Log"
        }
    }

    var symbol: String {
        switch self {
        case .maze:        return "square.grid.3x3"
        case .sensors:     return "waveform"
        case .actions:     return "square.grid.2x2"
        case .tuning:      return "slider.horizontal.3"
        case .calibration: return "gyroscope"
        case .history:     return "chart.xyaxis.line"
        case .log:         return "text.alignleft"
        }
    }

    /// A short right-aligned hint in the sidebar row. Kept to values that are
    /// genuinely worth knowing without opening the screen.
    ///
    /// Main-actor isolated because it reads the session, like everything else
    /// that touches it.
    @MainActor
    func badge(_ session: E4Session) -> String? {
        switch self {
        case .sensors:
            guard let status = session.sensorStatus else { return nil }
            return status.usingRealIR ? "REAL" : "VIRT"
        case .calibration:
            return session.gyroScale.map { String(format: "%.3f", $0) }
        case .log:
            return session.log.isEmpty ? nil : "\(session.log.count)"
        default:
            return nil
        }
    }
}
