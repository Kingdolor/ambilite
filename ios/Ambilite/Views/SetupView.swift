import SwiftUI

struct SetupView: View {
    @EnvironmentObject var model: AppModel
    @State private var scanning = false
    @State private var found: [TVScanner.Found] = []
    @State private var host = ""
    @State private var code = ""
    @State private var busy = false
    @State private var scanDone = false

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    VStack(alignment: .leading, spacing: 8) {
                        Text("Mit dem Fernseher verbinden")
                            .font(.title2.bold())
                        Text("iPhone und TV müssen im selben WLAN sein. Den Kopplungscode findest du in der Ambilite-App auf dem TV unter „iPhone-Code“.")
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                    }
                    .padding(.vertical, 6)
                }

                Section("Fernseher") {
                    Button {
                        Task { await scan() }
                    } label: {
                        HStack {
                            Label(scanning ? "Suche im WLAN …" : "Im WLAN suchen", systemImage: "wifi")
                            Spacer()
                            if scanning { ProgressView() }
                        }
                    }
                    .disabled(scanning)

                    ForEach(found) { tv in
                        Button {
                            host = tv.host
                        } label: {
                            HStack {
                                Image(systemName: "tv")
                                VStack(alignment: .leading) {
                                    Text("Ambilite \(tv.version)")
                                    Text("\(tv.host)\(tv.ledCount.map { " · \($0) LEDs" } ?? "")")
                                        .font(.caption).foregroundStyle(.secondary)
                                }
                                Spacer()
                                if host == tv.host { Image(systemName: "checkmark").foregroundStyle(.tint) }
                            }
                        }
                        .foregroundStyle(.primary)
                    }
                    if scanDone && found.isEmpty && !scanning {
                        Text("Kein TV gefunden. Läuft Ambilite 0.2 oder neuer? Sonst IP-Adresse unten eintragen.")
                            .font(.footnote).foregroundStyle(.secondary)
                    }

                    TextField("IP-Adresse, z. B. 192.168.178.45", text: $host)
                        .keyboardType(.decimalPad)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                }

                Section("Kopplungscode") {
                    TextField("6-stelliger Code vom TV", text: $code)
                        .keyboardType(.numberPad)
                        .font(.title3.monospacedDigit())
                }

                Section {
                    Button {
                        Task {
                            busy = true
                            _ = await model.pair(host: host.trimmingCharacters(in: .whitespaces),
                                                 key: code.trimmingCharacters(in: .whitespaces))
                            busy = false
                        }
                    } label: {
                        HStack {
                            Spacer()
                            if busy { ProgressView() } else { Text("Verbinden").bold() }
                            Spacer()
                        }
                    }
                    .disabled(host.isEmpty || code.count < 4 || busy)

                    if let e = model.lastError {
                        Label(e, systemImage: "exclamationmark.triangle")
                            .foregroundStyle(.orange)
                            .font(.footnote)
                    }
                }
            }
            .navigationTitle("Ambilite")
            .task { if found.isEmpty { await scan() } }
        }
    }

    private func scan() async {
        scanning = true
        found = await TVScanner.scan()
        if found.count == 1 { host = found[0].host }
        scanning = false
        scanDone = true
    }
}
