import SwiftUI

/// Slider row that shows its value and reports changes to the model while dragging.
struct ValueSlider: View {
    let title: String
    @Binding var value: Double
    let range: ClosedRange<Double>
    var step: Double = 1
    var format: (Double) -> String = { "\(Int($0.rounded()))" }

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(title)
                Spacer()
                Text(format(value))
                    .monospacedDigit()
                    .foregroundStyle(.secondary)
            }
            Slider(value: $value, in: range, step: step)
        }
        .padding(.vertical, 2)
    }
}

/// Binding helpers: user changes update the local settings and send a (debounced) patch to the TV.
extension AppModel {
    func intBinding(_ kp: WritableKeyPath<TVSettings, Int>, key: String) -> Binding<Double> {
        Binding(
            get: { Double(self.settings[keyPath: kp]) },
            set: {
                let v = Int($0.rounded())
                guard v != self.settings[keyPath: kp] else { return }
                self.settings[keyPath: kp] = v
                self.set(key, v)
            }
        )
    }

    func doubleBinding(_ kp: WritableKeyPath<TVSettings, Double>, key: String) -> Binding<Double> {
        Binding(
            get: { self.settings[keyPath: kp] },
            set: {
                let v = ($0 * 10).rounded() / 10
                guard v != self.settings[keyPath: kp] else { return }
                self.settings[keyPath: kp] = v
                self.set(key, v)
            }
        )
    }
}

struct ConnectionBanner: View {
    @EnvironmentObject var model: AppModel

    var body: some View {
        if !model.connected {
            HStack(spacing: 8) {
                Image(systemName: "wifi.exclamationmark")
                Text(model.lastError ?? "Keine Verbindung zum TV")
                    .lineLimit(2)
                Spacer()
            }
            .font(.footnote)
            .padding(10)
            .background(.orange.opacity(0.18), in: RoundedRectangle(cornerRadius: 10))
            .foregroundStyle(.orange)
        }
    }
}
