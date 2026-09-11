import SwiftUI
import E4Core

@main
struct E4CompanionApp: App {

    /// One session and one map for the whole app. `@State` on an `@Observable`
    /// class is the current idiom — no StateObject, no ObservableObject.
    @State private var session = E4Session()
    @State private var maze = E4Maze()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environment(session)
                .environment(maze)
                .task {
                    // The maze claims the session's single message tap. If
                    // anything else ever needs one, this becomes a fan-out.
                    maze.attach(to: session)
                }
        }
        #if os(macOS)
        // The size the layout was designed at — an iPad Pro 11" in landscape,
        // which is also a comfortable Mac window.
        .defaultSize(width: 1194, height: 834)
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
