import Foundation

// MARK: - Models (mirror the JSON of the TV service)

struct TVStatus: Codable, Equatable {
    var version: String?
    var elevated: Bool?
    var enabled: Bool?
    var mode: String?
    var isRunning: Bool?
    var videoActive: Bool?
    var powerPaused: Bool?
    var videoBackend: String?
    var uiBackend: String?
    var framerate: Double?
    var procUs: Double?
    var ledCount: Int?
    var serialOpen: Bool?
    var serialPath: String?
    var borderTop: Int?
    var borderLeft: Int?
    var sceneCuts: Int?
    var hdrType: String?
    var hdrActive: Bool?
    var profile: String?
    var updateState: String?
}

struct TVSettings: Codable, Equatable {
    var enabled: Bool = true
    var autostart: Bool = true
    var mode: String = "video"
    var brightness: Int = 100
    var saturation: Double = 1.5
    var gamma: Double = 1.5
    var hdr_brightness: Int = 100
    var hdr_saturation: Double = 1.9
    var hdr_gamma: Double = 1.2
    var hdr_auto: Bool = true
    var blackborder: Bool = true
    var smoothing_mode: Int = 2
    var smoothing_ms: Int = 80
    var adaptive_strength: Int = 50
    var release_ms: Int = 150
    var spatial: Int = 30
    var color: Int = 0xFF8A00
    var effect: String = "rainbow"
    var effect_speed: Int = 4
    var github_repo: String = ""

    init() {}

    // tolerant decoding: missing keys keep their defaults (older/newer TV versions)
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        func v<T: Decodable>(_ k: CodingKeys, _ d: T) -> T { (try? c.decodeIfPresent(T.self, forKey: k)) ?? d }
        let d = TVSettings()
        enabled = v(.enabled, d.enabled)
        autostart = v(.autostart, d.autostart)
        mode = v(.mode, d.mode)
        brightness = v(.brightness, d.brightness)
        saturation = v(.saturation, d.saturation)
        gamma = v(.gamma, d.gamma)
        hdr_brightness = v(.hdr_brightness, d.hdr_brightness)
        hdr_saturation = v(.hdr_saturation, d.hdr_saturation)
        hdr_gamma = v(.hdr_gamma, d.hdr_gamma)
        hdr_auto = v(.hdr_auto, d.hdr_auto)
        blackborder = v(.blackborder, d.blackborder)
        smoothing_mode = v(.smoothing_mode, d.smoothing_mode)
        smoothing_ms = v(.smoothing_ms, d.smoothing_ms)
        adaptive_strength = v(.adaptive_strength, d.adaptive_strength)
        release_ms = v(.release_ms, d.release_ms)
        spatial = v(.spatial, d.spatial)
        color = v(.color, d.color)
        effect = v(.effect, d.effect)
        effect_speed = v(.effect_speed, d.effect_speed)
        github_repo = v(.github_repo, d.github_repo)
    }
}

struct Ping: Codable {
    var name: String
    var version: String
    var ledCount: Int?
}

struct GitHubAsset: Codable, Hashable {
    var name: String
    var browser_download_url: String
    var digest: String?
}

struct GitHubRelease: Codable, Identifiable, Hashable {
    var id: Int
    var tag_name: String
    var name: String?
    var body: String?
    var draft: Bool
    var prerelease: Bool
    var published_at: String?
    var assets: [GitHubAsset]

    var version: String { tag_name.hasPrefix("v") ? String(tag_name.dropFirst()) : tag_name }

    /// IPK download + sha256 (from GitHub's asset digest or a "sha256: …" line in the notes)
    var ipk: (url: String, sha256: String)? {
        guard let asset = assets.first(where: { $0.name.hasSuffix(".ipk") }) else { return nil }
        if let d = asset.digest, d.hasPrefix("sha256:") {
            return (asset.browser_download_url, String(d.dropFirst(7)))
        }
        if let body, let r = body.range(of: #"sha256[:\s`]*([a-fA-F0-9]{64})"#, options: .regularExpression) {
            let hex = body[r].suffix(64).lowercased()
            return (asset.browser_download_url, String(hex))
        }
        return nil
    }
}

enum Version {
    static func compare(_ a: String, _ b: String) -> Int {
        let x = a.replacingOccurrences(of: "v", with: "").split(separator: ".").map { Int($0) ?? 0 }
        let y = b.replacingOccurrences(of: "v", with: "").split(separator: ".").map { Int($0) ?? 0 }
        for i in 0..<3 {
            let d = (i < x.count ? x[i] : 0) - (i < y.count ? y[i] : 0)
            if d != 0 { return d }
        }
        return 0
    }
}

// MARK: - HTTP client for the TV

enum APIError: LocalizedError {
    case unauthorized, http(Int), invalid

    var errorDescription: String? {
        switch self {
        case .unauthorized: return "Falscher Kopplungscode"
        case .http(let c): return "Fehler vom TV (HTTP \(c))"
        case .invalid: return "Ungültige Antwort"
        }
    }
}

struct AmbiliteClient {
    var host: String
    var port: Int = 8780
    var key: String

    private var base: URL { URL(string: "http://\(host):\(port)")! }

    private static let session: URLSession = {
        let c = URLSessionConfiguration.ephemeral
        c.timeoutIntervalForRequest = 4
        c.timeoutIntervalForResource = 8
        c.waitsForConnectivity = false
        return URLSession(configuration: c)
    }()

    private func request(_ path: String, method: String = "GET", body: Data? = nil, timeout: TimeInterval = 4) async throws -> Data {
        var r = URLRequest(url: base.appendingPathComponent(path))
        r.httpMethod = method
        r.timeoutInterval = timeout
        r.setValue(key, forHTTPHeaderField: "X-Ambilite-Key")
        if let body {
            r.httpBody = body
            r.setValue("application/json", forHTTPHeaderField: "Content-Type")
        }
        let (data, resp) = try await Self.session.data(for: r)
        guard let http = resp as? HTTPURLResponse else { throw APIError.invalid }
        if http.statusCode == 401 { throw APIError.unauthorized }
        guard (200..<300).contains(http.statusCode) else { throw APIError.http(http.statusCode) }
        return data
    }

    static func ping(host: String, port: Int = 8780, timeout: TimeInterval = 1.0) async -> Ping? {
        guard let url = URL(string: "http://\(host):\(port)/api/ping") else { return nil }
        var r = URLRequest(url: url)
        r.timeoutInterval = timeout
        guard let (data, resp) = try? await session.data(for: r),
              (resp as? HTTPURLResponse)?.statusCode == 200 else { return nil }
        return try? JSONDecoder().decode(Ping.self, from: data)
    }

    func status() async throws -> TVStatus {
        try JSONDecoder().decode(TVStatus.self, from: try await request("/api/status"))
    }

    func settings() async throws -> TVSettings {
        try JSONDecoder().decode(TVSettings.self, from: try await request("/api/settings"))
    }

    @discardableResult
    func patch(_ values: [String: Any]) async throws -> TVSettings {
        let body = try JSONSerialization.data(withJSONObject: values)
        return try JSONDecoder().decode(TVSettings.self, from: try await request("/api/settings", method: "POST", body: body))
    }

    func post(_ path: String) async throws {
        _ = try await request(path, method: "POST", body: Data("{}".utf8))
    }

    func install(url: String, sha256: String) async throws {
        let body = try JSONSerialization.data(withJSONObject: ["url": url, "sha256": sha256])
        _ = try await request("/api/install", method: "POST", body: body)
    }
}

// MARK: - GitHub

enum GitHub {
    static func releases(repo: String) async throws -> [GitHubRelease] {
        guard let url = URL(string: "https://api.github.com/repos/\(repo)/releases?per_page=30") else { return [] }
        var r = URLRequest(url: url)
        r.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept")
        let (data, resp) = try await URLSession.shared.data(for: r)
        guard (resp as? HTTPURLResponse)?.statusCode == 200 else { throw APIError.http((resp as? HTTPURLResponse)?.statusCode ?? 0) }
        return try JSONDecoder().decode([GitHubRelease].self, from: data)
            .filter { !$0.draft }
            .sorted { Version.compare($0.tag_name, $1.tag_name) > 0 }
    }
}
