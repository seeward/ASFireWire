import SwiftUI

@main
struct ADKLabHostApp: App {
    var body: some Scene {
        WindowGroup {
            MainContainerView()
                .frame(minWidth: 960, minHeight: 700)
        }
    }
}

struct MainContainerView: View {
    var body: some View {
        TabView {
            GenericLabDashboardView()
                .tabItem {
                    Label("Generic Device Lab", systemImage: "waveform.path.ecg.rectangle")
                }

            DextManagementView()
                .tabItem {
                    Label("Dext Activation & Packets", systemImage: "cpu")
                }
        }
    }
}
