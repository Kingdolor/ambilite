import SwiftUI

struct SystemView: View {
    @EnvironmentObject var model: AppModel
    @State private var confirm: GitHubRelease?
    @State private var repoField = ""

    var body: some View {
        NavigationStack {
            Form {
                Section("Fernseher") {
                    LabeledContent("Adresse", value: model.host)
                    LabeledContent("Ambilite-Version", value: model.status.version ?? "–")
                    LabeledContent("Root-Rechte", value: model.status.elevated == true ? "ja" : "nein")
                    LabeledContent("Bildquelle", value: backend)
                    Toggle("Autostart beim Einschalten", isOn: Binding(
                        get: { model.settings.autostart },
                        set: { model.settings.autostart = $0; model.set("autostart", $0) }
                    ))
                    Button("LED-Test (Licht vorher aus)") { model.testPattern() }
                        .disabled(model.status.isRunning == true)
                }

                Section {
                    if model.loadingReleases {
                        HStack { ProgressView(); Text("Lade Versionen …").foregroundStyle(.secondary) }
                    }
                    if let e = model.releasesError {
                        Text(e).font(.footnote).foregroundStyle(.secondary)
                    }
                    ForEach(model.releases) { r in
                        releaseRow(r)
                    }
                } header: {
                    HStack {
                        Text("Versionen auf GitHub")
                        Spacer()
                        Button {
                            Task { await model.loadReleases() }
                        } label: { Image(systemName: "arrow.clockwise") }
                    }
                } footer: {
                    Text("Repository: \(model.repo.isEmpty ? "–" : model.repo). Installation läuft auf dem TV über den Homebrew Channel; der Dienst startet danach neu.")
                }

                Section("GitHub-Repository") {
                    TextField(model.settings.github_repo.isEmpty ? "besitzer/ambilite" : model.settings.github_repo, text: $repoField)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                    Button("Übernehmen") {
                        let v = repoField.trimmingCharacters(in: .whitespaces)
                        model.repoOverride = ""
                        model.settings.github_repo = v
                        model.set("github_repo", v)
                        Task { await model.loadReleases() }
                    }
                    .disabled(repoField.isEmpty)
                }

                Section {
                    Button("Verbindung trennen", role: .destructive) { model.unpair() }
                }
            }
            .navigationTitle("System")
            .task { if model.releases.isEmpty { await model.loadReleases() } }
            .confirmationDialog("Version installieren?", isPresented: Binding(
                get: { confirm != nil }, set: { if !$0 { confirm = nil } }
            ), presenting: confirm) { r in
                Button("\(r.tag_name) installieren") { model.install(r) }
            } message: { r in
                Text("Ambilite \(r.tag_name) wird auf dem TV installiert. Das Licht ist dabei kurz aus.")
            }
        }
    }

    private var backend: String {
        let parts = [model.status.videoBackend, model.status.uiBackend].compactMap { $0 }
            .map { $0.replacingOccurrences(of: "lib", with: "").replacingOccurrences(of: "_backend.so", with: "") }
        return parts.isEmpty ? "–" : parts.joined(separator: " + ")
    }

    @ViewBuilder
    private func releaseRow(_ r: GitHubRelease) -> some View {
        let current = model.status.version ?? "0"
        let cmp = Version.compare(r.tag_name, current)
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                HStack(spacing: 6) {
                    Text(r.tag_name).font(.headline)
                    if r.prerelease { tag("Test", .orange) }
                    if cmp == 0 { tag("Installiert", .green) }
                    if cmp > 0 { tag("Neu", .blue) }
                }
                if let n = r.name, !n.isEmpty, n != r.tag_name {
                    Text(n).font(.subheadline).foregroundStyle(.secondary)
                }
                if let d = r.published_at?.prefix(10) {
                    Text(String(d)).font(.caption).foregroundStyle(.secondary)
                }
            }
            Spacer()
            if model.installing == r.tag_name {
                ProgressView()
            } else if cmp != 0 {
                Button(cmp > 0 ? "Update" : "Zurück") { confirm = r }
                    .buttonStyle(.bordered)
                    .disabled(r.ipk == nil || model.installing != nil)
            }
        }
    }

    private func tag(_ t: String, _ c: Color) -> some View {
        Text(t)
            .font(.caption2.bold())
            .padding(.horizontal, 6).padding(.vertical, 2)
            .background(Capsule().fill(c.opacity(0.25)))
            .foregroundStyle(c)
    }
}
