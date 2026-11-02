import SwiftUI

/// Page 3: four pads. A press is one edge in the packet (pad 1-4 flag bits).
struct PadsView: View {
    @EnvironmentObject var remote: Remote

    var body: some View {
        Grid(horizontalSpacing: 6, verticalSpacing: 6) {
            GridRow { pad(0); pad(1) }
            GridRow { pad(2); pad(3) }
        }
        .navigationTitle("Pads")
    }

    private func pad(_ i: Int) -> some View {
        Button("\(i + 1)") { remote.press(pad: i) }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .disabled(!remote.playing)
    }
}
