import SwiftUI

struct PictureView: View {
    @EnvironmentObject var model: AppModel

    var body: some View {
        NavigationStack {
            Form {
                if !model.connected {
                    Section { ConnectionBanner() }
                }

                Section {
                    Picker("Glättung", selection: Binding(
                        get: { model.settings.smoothing_mode },
                        set: { model.settings.smoothing_mode = $0; model.set("smoothing_mode", $0) }
                    )) {
                        Text("Direkt").tag(0)
                        Text("Klassisch").tag(1)
                        Text("Adaptiv").tag(2)
                    }
                    .pickerStyle(.segmented)

                    switch model.settings.smoothing_mode {
                    case 2:
                        ValueSlider(title: "Ruhe bei kleinen Änderungen", value: model.intBinding(\.adaptive_strength, key: "adaptive_strength"),
                                    range: 0...100, step: 5, format: { "\(Int($0)) %" })
                        ValueSlider(title: "Nachleuchten beim Abdunkeln", value: model.intBinding(\.release_ms, key: "release_ms"),
                                    range: 0...600, step: 25, format: { "\(Int($0)) ms" })
                    case 1:
                        ValueSlider(title: "Glättungszeit", value: model.intBinding(\.smoothing_ms, key: "smoothing_ms"),
                                    range: 0...500, step: 10, format: { "\(Int($0)) ms" })
                    default:
                        EmptyView()
                    }
                    ValueSlider(title: "Nachbar-LEDs mischen", value: model.intBinding(\.spatial, key: "spatial"),
                                range: 0...100, step: 5, format: { "\(Int($0)) %" })
                } header: {
                    Text("Bewegung")
                } footer: {
                    Text(smoothingHelp)
                }

                profileSection(title: "SDR-Profil", active: !model.hdrActive,
                               b: (\.brightness, "brightness"), s: (\.saturation, "saturation"), g: (\.gamma, "gamma"))

                profileSection(title: "HDR-Profil", active: model.hdrActive,
                               b: (\.hdr_brightness, "hdr_brightness"), s: (\.hdr_saturation, "hdr_saturation"), g: (\.hdr_gamma, "hdr_gamma"))

                Section {
                    Toggle("HDR automatisch erkennen", isOn: Binding(
                        get: { model.settings.hdr_auto },
                        set: { model.settings.hdr_auto = $0; model.set("hdr_auto", $0) }
                    ))
                    Toggle("Schwarzbalken erkennen", isOn: Binding(
                        get: { model.settings.blackborder },
                        set: { model.settings.blackborder = $0; model.set("blackborder", $0) }
                    ))
                } footer: {
                    Text("Aktuelles Signal: \((model.status.hdrType ?? "–").uppercased())")
                }
            }
            .navigationTitle("Bild")
        }
    }

    private var smoothingHelp: String {
        switch model.settings.smoothing_mode {
        case 2: return "Adaptiv: kleine Schwankungen werden beruhigt, große Änderungen und Szenenwechsel kommen sofort. Beim Abdunkeln klingt das Licht kurz nach."
        case 1: return "Klassisch: alles wird gleich stark verzögert."
        default: return "Direkt: keine zeitliche Glättung, maximal schnell."
        }
    }

    @ViewBuilder
    private func profileSection(title: String, active: Bool,
                                b: (WritableKeyPath<TVSettings, Int>, String),
                                s: (WritableKeyPath<TVSettings, Double>, String),
                                g: (WritableKeyPath<TVSettings, Double>, String)) -> some View {
        Section {
            ValueSlider(title: "Helligkeit", value: model.intBinding(b.0, key: b.1), range: 5...100, step: 1,
                        format: { "\(Int($0)) %" })
            ValueSlider(title: "Farbsättigung", value: model.doubleBinding(s.0, key: s.1), range: 0...3, step: 0.1,
                        format: { String(format: "%.1f×", $0) })
            ValueSlider(title: "Gamma", value: model.doubleBinding(g.0, key: g.1), range: 0.5...3, step: 0.1,
                        format: { String(format: "%.1f", $0) })
        } header: {
            HStack {
                Text(title)
                if active {
                    Text("AKTIV")
                        .font(.caption2.bold())
                        .padding(.horizontal, 6).padding(.vertical, 2)
                        .background(Capsule().fill(Color.accentColor.opacity(0.3)))
                }
            }
        }
    }
}
