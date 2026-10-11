import SwiftUI

@main
struct AmbiliteApp: App {
    @StateObject private var model = AppModel()
    @Environment(\.scenePhase) private var phase

    var body: some Scene {
        WindowGroup {
            Group {
                if model.paired {
                    MainView()
                } else {
                    SetupView()
                }
            }
            .environmentObject(model)
            .preferredColorScheme(.dark)
            .tint(Color(rgb: 0xFFA640))
            .onChange(of: phase) { _, newPhase in
                if newPhase == .active { model.startPolling() } else { model.stopPolling() }
            }
        }
    }
}

struct MainView: View {
    var body: some View {
        TabView {
            LightView()
                .tabItem { Label("Licht", systemImage: "lightbulb.led.fill") }
            PictureView()
                .tabItem { Label("Bild", systemImage: "slider.horizontal.3") }
            SystemView()
                .tabItem { Label("System", systemImage: "gearshape.fill") }
        }
    }
}
