import SwiftUI

struct LightView: View {
    @EnvironmentObject var model: AppModel
    @State private var pickedColor = Color(rgb: 0xFF8A00)

    private let presets: [(Int, String)] = [
        (0xFF8A00, "Orange"), (0xFFB45A, "Warmweiß"), (0xFFFFFF, "Weiß"), (0xFF0000, "Rot"),
        (0xFF2A6D, "Pink"), (0x9B30FF, "Lila"), (0x0040FF, "Blau"), (0x00D0FF, "Cyan"), (0x00FF40, "Grün"),
    ]
    private let effects: [(String, String, String)] = [
        ("rainbow", "Regenbogen", "rainbow"), ("breathe", "Atmen", "wind"),
        ("candle", "Kerze", "flame"), ("aurora", "Polarlicht", "sparkles"),
    ]

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 18) {
                    ConnectionBanner()
                    powerCard
                    modeCard
                    if model.settings.mode == "color" { colorCard }
                    if model.settings.mode == "effect" { effectCard }
                    statusCard
                }
                .padding()
            }
            .navigationTitle("Ambilite")
            .refreshable { await model.refresh(includeSettings: true) }
        }
        .onAppear { pickedColor = Color(rgb: model.settings.color) }
        .onChange(of: model.settings.color) { _, c in
            if pickedColor.rgbInt != c { pickedColor = Color(rgb: c) }
        }
    }

    private var glow: Color {
        switch model.settings.mode {
        case "color": return Color(rgb: model.settings.color)
        case "effect": return .purple
        default: return Color(rgb: 0xFF8A00)
        }
    }

    private var powerCard: some View {
        Button {
            model.toggle()
        } label: {
            HStack(spacing: 16) {
                Image(systemName: "power")
                    .font(.system(size: 30, weight: .bold))
                    .frame(width: 64, height: 64)
                    .background(Circle().fill(model.settings.enabled ? glow : Color.gray.opacity(0.3)))
                    .foregroundStyle(model.settings.enabled ? .black : .white)
                    .shadow(color: model.settings.enabled ? glow.opacity(0.7) : .clear, radius: 16)
                VStack(alignment: .leading, spacing: 4) {
                    Text(model.settings.enabled ? "Licht an" : "Licht aus").font(.title2.bold())
                    Text(subtitle).font(.subheadline).foregroundStyle(.secondary)
                }
                Spacer()
            }
            .padding()
            .background(RoundedRectangle(cornerRadius: 20).fill(.thinMaterial))
        }
        .buttonStyle(.plain)
    }

    private var subtitle: String {
        if model.status.powerPaused == true { return "TV im Standby" }
        switch model.settings.mode {
        case "color": return "Feste Farbe"
        case "effect": return effects.first { $0.0 == model.settings.effect }?.1 ?? "Effekt"
        default: return model.hdrActive ? "Ambilight · HDR-Profil" : "Ambilight · SDR-Profil"
        }
    }

    private var modeCard: some View {
        VStack(alignment: .leading, spacing: 14) {
            Picker("Modus", selection: Binding(
                get: { model.settings.mode },
                set: { model.settings.mode = $0; model.set("mode", $0) }
            )) {
                Text("Bild").tag("video")
                Text("Farbe").tag("color")
                Text("Effekt").tag("effect")
            }
            .pickerStyle(.segmented)

            let bKey = model.hdrActive && model.settings.mode == "video" ? "hdr_brightness" : "brightness"
            let bPath: WritableKeyPath<TVSettings, Int> = bKey == "brightness" ? \.brightness : \.hdr_brightness
            ValueSlider(title: bKey == "brightness" ? "Helligkeit" : "Helligkeit (HDR)",
                        value: model.intBinding(bPath, key: bKey), range: 5...100, step: 1,
                        format: { "\(Int($0)) %" })
        }
        .padding()
        .background(RoundedRectangle(cornerRadius: 20).fill(.thinMaterial))
    }

    private var colorCard: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Farbe").font(.headline)
            LazyVGrid(columns: Array(repeating: GridItem(.flexible()), count: 5), spacing: 12) {
                ForEach(presets, id: \.0) { p in
                    Button {
                        model.settings.color = p.0
                        pickedColor = Color(rgb: p.0)
                        model.set("color", p.0)
                    } label: {
                        Circle()
                            .fill(Color(rgb: p.0))
                            .frame(width: 44, height: 44)
                            .overlay(Circle().stroke(.white, lineWidth: model.settings.color == p.0 ? 3 : 0))
                    }
                    .accessibilityLabel(p.1)
                }
                ColorPicker("", selection: $pickedColor, supportsOpacity: false)
                    .labelsHidden()
                    .frame(width: 44, height: 44)
            }
            .onChange(of: pickedColor) { _, c in
                let v = c.rgbInt
                if v != model.settings.color {
                    model.settings.color = v
                    model.set("color", v)
                }
            }
        }
        .padding()
        .background(RoundedRectangle(cornerRadius: 20).fill(.thinMaterial))
    }

    private var effectCard: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Effekt").font(.headline)
            LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 12) {
                ForEach(effects, id: \.0) { e in
                    Button {
                        model.settings.effect = e.0
                        model.set("effect", e.0)
                    } label: {
                        Label(e.1, systemImage: e.2)
                            .frame(maxWidth: .infinity, minHeight: 48)
                            .background(RoundedRectangle(cornerRadius: 12)
                                .fill(model.settings.effect == e.0 ? Color.accentColor.opacity(0.3) : Color.gray.opacity(0.15)))
                    }
                    .buttonStyle(.plain)
                }
            }
            if ["breathe"].contains(model.settings.effect) {
                ColorPicker("Grundfarbe", selection: $pickedColor, supportsOpacity: false)
            }
            ValueSlider(title: "Tempo", value: model.intBinding(\.effect_speed, key: "effect_speed"),
                        range: 1...10, step: 1)
        }
        .padding()
        .background(RoundedRectangle(cornerRadius: 20).fill(.thinMaterial))
        .onChange(of: pickedColor) { _, c in
            let v = c.rgbInt
            if v != model.settings.color { model.settings.color = v; model.set("color", v) }
        }
    }

    private var statusCard: some View {
        let s = model.status
        return VStack(alignment: .leading, spacing: 8) {
            Text("Status").font(.headline)
            row("Verbindung", model.connected ? "verbunden" : "getrennt")
            if model.settings.mode == "video" {
                row("Bilder/s", s.videoActive == true ? String(format: "%.0f", s.framerate ?? 0) : "–")
                row("Rechenzeit", s.videoActive == true ? "\(Int(s.procUs ?? 0)) µs" : "–")
                row("Signal", (s.hdrType ?? "–").uppercased())
                row("Schwarzbalken", s.videoActive == true ? ((s.borderTop ?? 0) + (s.borderLeft ?? 0) > 0 ? "\(s.borderTop ?? 0) / \(s.borderLeft ?? 0) px" : "keine") : "–")
            }
            row("LED-Ausgang", s.serialOpen == true ? "\(s.serialPath ?? "") · \(s.ledCount ?? 0) LEDs" : "nicht verbunden")
        }
        .font(.subheadline)
        .padding()
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(RoundedRectangle(cornerRadius: 20).fill(.thinMaterial))
    }

    private func row(_ k: String, _ v: String) -> some View {
        HStack {
            Text(k).foregroundStyle(.secondary)
            Spacer()
            Text(v).monospacedDigit()
        }
    }
}
