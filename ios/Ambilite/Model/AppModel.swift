import Foundation
import SwiftUI

@MainActor
final class AppModel: ObservableObject {
    // pairing (persisted)
    @AppStorage("tvHost") var host: String = ""
    @AppStorage("tvKey") var key: String = ""
    @AppStorage("githubRepo") var repoOverride: String = ""

    @Published var paired = false
    @Published var connected = false
    @Published var status = TVStatus()
    @Published var settings = TVSettings()
    @Published var lastError: String?

    @Published var releases: [GitHubRelease] = []
    @Published var releasesError: String?
    @Published var loadingReleases = false
    @Published var installing: String?

    private var pollTask: Task<Void, Never>?
    private var pending: [String: Any] = [:]
    private var flushTask: Task<Void, Never>?

    init() {
        paired = !host.isEmpty && !key.isEmpty
    }

    var client: AmbiliteClient { AmbiliteClient(host: host, key: key) }
    var repo: String { repoOverride.isEmpty ? settings.github_repo : repoOverride }
    var hdrActive: Bool { status.hdrActive == true && settings.hdr_auto }

    // MARK: pairing

    func pair(host: String, key: String) async -> Bool {
        let c = AmbiliteClient(host: host, key: key)
        do {
            let s = try await c.settings()
            self.host = host
            self.key = key
            settings = s
            paired = true
            lastError = nil
            startPolling()
            return true
        } catch {
            lastError = error.localizedDescription
            return false
        }
    }

    func unpair() {
        stopPolling()
        host = ""
        key = ""
        paired = false
        connected = false
    }

    // MARK: polling

    func startPolling() {
        guard paired else { return }
        pollTask?.cancel()
        pollTask = Task { [weak self] in
            var first = true
            while !Task.isCancelled {
                await self?.refresh(includeSettings: first)
                first = false
                try? await Task.sleep(nanoseconds: 2_000_000_000)
            }
        }
    }

    func stopPolling() {
        pollTask?.cancel()
        pollTask = nil
    }

    func refresh(includeSettings: Bool = false) async {
        do {
            let st = try await client.status()
            status = st
            if includeSettings || !connected {
                if pending.isEmpty { settings = try await client.settings() }
            } else if let en = st.enabled, en != settings.enabled, pending["enabled"] == nil {
                settings.enabled = en // switched by remote / TV app
            }
            if let m = st.mode, m != settings.mode, pending["mode"] == nil { settings.mode = m }
            connected = true
            lastError = nil
            if installing != nil, st.updateState == "done" || st.updateState == "failed" {
                installing = nil
                settings = try await client.settings()
            }
        } catch {
            connected = false
            lastError = error.localizedDescription
        }
    }

    // MARK: changes (debounced so sliders don't flood the TV)

    func set(_ key: String, _ value: Any) {
        pending[key] = value
        flushTask?.cancel()
        flushTask = Task { [weak self] in
            try? await Task.sleep(nanoseconds: 120_000_000)
            await self?.flush()
        }
    }

    private func flush() async {
        guard !pending.isEmpty else { return }
        let values = pending
        pending = [:]
        do {
            _ = try await client.patch(values)
        } catch {
            lastError = error.localizedDescription
        }
    }

    func toggle() {
        settings.enabled.toggle()
        Task {
            do { try await client.post(settings.enabled ? "/api/on" : "/api/off") } catch { lastError = error.localizedDescription }
            await refresh()
        }
    }

    func testPattern() {
        Task {
            do { try await client.post("/api/test") } catch { lastError = "Test nur möglich, wenn das Licht aus ist" }
        }
    }

    // MARK: versions

    func loadReleases() async {
        guard !repo.isEmpty else {
            releasesError = "Kein GitHub-Repository eingestellt"
            return
        }
        loadingReleases = true
        defer { loadingReleases = false }
        do {
            releases = try await GitHub.releases(repo: repo)
            releasesError = releases.isEmpty ? "Noch keine Versionen veröffentlicht" : nil
        } catch {
            releasesError = "GitHub nicht erreichbar (\(error.localizedDescription))"
        }
    }

    func install(_ r: GitHubRelease) {
        guard let ipk = r.ipk else {
            lastError = "Diese Version hat keine IPK mit Prüfsumme"
            return
        }
        installing = r.tag_name
        Task {
            do {
                try await client.install(url: ipk.url, sha256: ipk.sha256)
            } catch {
                installing = nil
                lastError = error.localizedDescription
            }
        }
    }
}

extension Color {
    init(rgb: Int) {
        self.init(red: Double((rgb >> 16) & 0xFF) / 255, green: Double((rgb >> 8) & 0xFF) / 255, blue: Double(rgb & 0xFF) / 255)
    }

    var rgbInt: Int {
        let ui = UIColor(self)
        var r: CGFloat = 0, g: CGFloat = 0, b: CGFloat = 0, a: CGFloat = 0
        ui.getRed(&r, green: &g, blue: &b, alpha: &a)
        func c(_ v: CGFloat) -> Int { Int((min(max(v, 0), 1) * 255).rounded()) }
        return (c(r) << 16) | (c(g) << 8) | c(b)
    }
}
