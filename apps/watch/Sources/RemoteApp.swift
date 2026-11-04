import SwiftUI

@main
struct RemoteApp: App {
    @StateObject private var remote = Remote()

    var body: some Scene {
        WindowGroup {
            TabView {
                ConnectView()
                PlayView()
                PadsView()
            }
            .tabViewStyle(.verticalPage)
            .environmentObject(remote)
            .environmentObject(remote.link)
        }
    }
}
