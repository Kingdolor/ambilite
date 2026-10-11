import Foundation

/// Finds Ambilite TVs by probing every address of the local /24 network on the API port.
enum TVScanner {
    struct Found: Identifiable, Hashable {
        var id: String { host }
        var host: String
        var version: String
        var ledCount: Int?
    }

    /// IPv4 address of the Wi-Fi interface (en0), e.g. "192.168.178.23"
    static func localIPv4() -> String? {
        var addrs: UnsafeMutablePointer<ifaddrs>?
        guard getifaddrs(&addrs) == 0, let first = addrs else { return nil }
        defer { freeifaddrs(addrs) }
        var result: String?
        for ptr in sequence(first: first, next: { $0.pointee.ifa_next }) {
            let ifa = ptr.pointee
            guard let sa = ifa.ifa_addr, sa.pointee.sa_family == UInt8(AF_INET) else { continue }
            let name = String(cString: ifa.ifa_name)
            guard name == "en0" || (result == nil && name.hasPrefix("en")) else { continue }
            var host = [CChar](repeating: 0, count: Int(NI_MAXHOST))
            if getnameinfo(sa, socklen_t(sa.pointee.sa_len), &host, socklen_t(host.count), nil, 0, NI_NUMERICHOST) == 0 {
                result = String(cString: host)
                if name == "en0" { break }
            }
        }
        return result
    }

    static func scan(port: Int = 8780) async -> [Found] {
        guard let ip = localIPv4() else { return [] }
        let parts = ip.split(separator: ".")
        guard parts.count == 4 else { return [] }
        let prefix = parts[0...2].joined(separator: ".")
        let own = Int(parts[3]) ?? -1

        return await withTaskGroup(of: Found?.self) { group in
            var results: [Found] = []
            var next = 1
            let maxParallel = 48
            func addNext() {
                while next <= 254 && next == own { next += 1 }
                guard next <= 254 else { return }
                let host = "\(prefix).\(next)"
                next += 1
                group.addTask {
                    guard let p = await AmbiliteClient.ping(host: host, port: port, timeout: 0.8) else { return nil }
                    return Found(host: host, version: p.version, ledCount: p.ledCount)
                }
            }
            for _ in 0..<maxParallel { addNext() }
            while let r = await group.next() {
                if let r { results.append(r) }
                addNext()
            }
            return results.sorted { $0.host < $1.host }
        }
    }
}
