import SwiftUI
import E4Core

@main
struct E4CompanionApp: App {

    /// One session for the whole app. `@State` on an `@Observable` class is the
    /// current idiom — no StateObject, no ObservableObject.
    @State private var session = E4Session()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environment(session)
        }
        #if os(macOS)
        .defaultSize(width: 940, height: 680)
        .commands {
            CommandGroup(after: .toolbar) {
                Button("Disconnect") { session.disconnect() }
                    .keyboardShortcut("d", modifiers: [.command, .shift])
                    .disabled(!session.connection.isConnected)
            }
        }
        #endif
    }
}
