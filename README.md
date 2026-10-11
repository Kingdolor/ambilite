# Ambilite – schlankes Ambilight für den LG OLED C2 (webOS 7, gerootet)

Ein einziger Dienst auf dem TV ersetzt die Kette **PicCap → HyperHDR → Adalight**:

```
TV-Bild (vtcapture + GUI)  →  320×180 RGB  →  323 LED-Farben  →  USB-Seriell (AWA, 2 Mbaud)  →  ESP32
```

Verarbeitung pro Bild: Kantenmittelung je LED (Layout 1:1 aus deinem HyperHDR-Export),
Schwarzbalken-Erkennung, Nachbar-Mischung, Sättigung, Gamma/Helligkeit, adaptive Glättung.

## Neu in 0.2

- **Adaptive Glättung** (Standard): kleine Schwankungen werden beruhigt, große Änderungen gehen
  sofort durch, Szenenwechsel (≥ 60 % der LEDs ändern sich stark) überspringen die Glättung
  komplett. Heller werden = sofort, dunkler werden = kurzes Nachleuchten (einstellbar).
- **Nachbar-LEDs mischen**: Licht gleitet bei Bewegungen statt von LED zu LED zu springen –
  ohne zusätzliche Verzögerung.
- **HDR-Profil**: der TV meldet SDR/HDR10/HLG/Dolby Vision; Ambilite schaltet automatisch auf ein
  eigenes Profil (Helligkeit, Sättigung, Gamma).
- **Licht ohne Bild**: feste Farbe oder Effekte (Regenbogen, Atmen, Kerze, Polarlicht).
- **iPhone-App** (`ios/`, SwiftUI) über eine kleine HTTP-Schnittstelle (Port 8780, mit
  6-stelligem Kopplungscode aus der TV-App).
- **Updates** aus GitHub-Releases – in der TV-App (*Update*) oder in der iPhone-App
  (*System → Versionen*), Installation über den Homebrew Channel, inkl. Rückkehr zu älteren
  Versionen. Der Dienst richtet sich danach selbst wieder Root-Rechte ein.

| Aus deiner HyperHDR-Config übernommen | Wert |
|---|---|
| LEDs / Layout | 323, identische Rechtecke |
| Gerät | Adalight, AWA-Protokoll (HyperSerialESP32), 2 000 000 Baud, Port „auto“ |
| Farbreihenfolge | GRB |
| Gamma / Sättigung | 1,5 / 1,5× |
| Schwarzbalken | an, Schwelle 5 % |
| Glättung (vorher 150 ms) | **aus** – in der App auf „Leicht“/„Weich“ umschaltbar |
| Stehleuchten (WLED) | bewusst weggelassen |

## Installation (Test)

1. **Alte Kette anhalten** – PicCap und HyperHDR belegen Bildquelle und USB-Port.
   Für einen Test reicht der Menüpunkt *PicCap/HyperHDR beenden* in der App. Für den Dauerbetrieb
   den Autostart in PicCap (Settings → Autostart) und in HyperHDR ausschalten.
2. `com.ambilite.tv_0.1.0_all.ipk` mit dem **webOS Dev Manager** installieren.
3. App **Ambilite** starten. Beim ersten Start richtet sie sich über den Homebrew Channel
   Root-Rechte ein (Status „Root: ja“ nach 2–3 Sekunden).
4. **LED-Test** ausführen (Ambilight dafür kurz ausschalten): Rot, Grün, Blau, Weiß, dann
   Lauflicht ab LED 1. Stimmen die Farben, passt die GRB-Reihenfolge.
5. Ambilight einschalten. Unten siehst du Bildquelle, Bilder/s, Rechenzeit und LED-Ausgang.

**Zurück zum alten Setup:** Ambilite ausschalten oder deinstallieren, PicCap/HyperHDR wieder starten.

## Bedienung per Fernbedienung

▲▼ wählen · ◀▶ ändern · OK ausführen · Zurück schließt. Alles wird sofort gespeichert und live
übernommen.

**Fernbedienungs-Taste:** *OK = Taste lernen*, dann eine Taste drücken – diese schaltet künftig
das Ambilight an/aus, auch ohne App. Die Taste behält zusätzlich ihre normale Funktion; also eine
nehmen, die du sonst kaum nutzt. ◀ löscht die Zuordnung.

Ambilite schaltet bei Standby automatisch aus (LEDs schwarz) und beim Einschalten wieder an,
wenn *Ambilight* auf „An“ steht. Mit *Autostart* startet der Dienst beim Booten des TVs.

## Dateien auf dem TV

| Pfad | Inhalt |
|---|---|
| `/media/developer/apps/usr/palm/services/com.ambilite.tv.service/` | Programm, Capture-Backends, Standard-Config, `leds.json` |
| `/media/developer/ambilite/config.json` | deine Einstellungen (übersteht Neuinstallation) |
| `/media/developer/ambilite/leds.json` | optional: eigenes LED-Layout, überschreibt das mitgelieferte |

Neues Layout aus einem HyperHDR-Export erzeugen:
`python3 tools/convert_hyperhdr.py Backup.json ZIELORDNER` → `leds.json` auf den TV kopieren →
`luna-send -n 1 luna://com.ambilite.tv.service/reloadLayout '{}'`.

## Fehlersuche (SSH / Terminal auf dem TV)

```sh
luna-send -n 1 -f luna://com.ambilite.tv.service/status '{}'
luna-send -n 1 luna://com.ambilite.tv.service/toggle '{}'
# Direkt im Vordergrund mit Log (vorher Dienst beenden: killall ambilite)
/media/developer/apps/usr/palm/services/com.ambilite.tv.service/ambilite -v
# Nur LED-Test, ohne Bildabgriff
/media/developer/apps/usr/palm/services/com.ambilite.tv.service/ambilite --test
```

Luna-Methoden: `start`, `stop`, `toggle`, `status`, `getSettings`, `setSettings`, `testPattern`,
`learnHotkey`, `reloadLayout`, `install`.

HTTP (Header `X-Ambilite-Key: <Code>`): `GET /api/ping` (ohne Code), `GET /api/status`,
`GET|POST /api/settings`, `POST /api/toggle|on|off|test`, `POST /api/install {"url","sha256"}`
(nur `https://github.com/…/releases/download/…`).

## iPhone-App

`ios/Ambilite.xcodeproj` in Xcode (16 oder neuer) öffnen → Target *Ambilite* → *Signing &
Capabilities* → Team = deine Apple-ID → iPhone anschließen → ▶︎. Beim ersten Start fragt iOS nach
Zugriff aufs lokale Netzwerk – erlauben. Dann TV suchen, Code aus der TV-App eingeben.

## Neue Version veröffentlichen

1. Version in `CMakeLists.txt` und `frontend/appinfo.json` erhöhen, `./build.sh`.
2. GitHub-Release mit Tag `vX.Y.Z` anlegen, `dist/com.ambilite.tv_X.Y.Z_all.ipk` anhängen und in
   die Beschreibung `sha256: <sha256sum der IPK>` schreiben (wird von TV- und iPhone-App geprüft).

## Bauen

```sh
# Toolchain (einmalig)
curl -L -o tc.tar.gz https://github.com/openlgtv/buildroot-nc4/releases/download/webos-d7ed7ee/arm-webos-linux-gnueabi_sdk-buildroot.tar.gz
tar xf tc.tar.gz -C /opt && /opt/arm-webos-linux-gnueabi_sdk-buildroot/relocate-sdk.sh
./build.sh            # → dist/com.ambilite.tv_<version>_all.ipk
```

Host-Tests (Farblogik, Schwarzbalken, AWA-Rahmen):
`gcc -O2 -Isrc tests/*.c src/ledproc.c src/serial_out.c src/utils.c -lm -lutil -lpthread -o t && ./t servicefiles/leds.json`

## Herkunft / Lizenzen

Bildabgriff (`unicapture/`, Backends, `tv-native-apis/`) stammt unverändert aus
[webosbrew/hyperion-webos](https://github.com/webosbrew/hyperion-webos) (MIT, siehe
`LICENSE.hyperion-webos`), libyuv (BSD) liegt unter `third_party/`. Das AWA-Protokoll ist
kompatibel zu HyperHDR / HyperSerialESP32. Farbverarbeitung, Ausgabe, Dienst und App sind neu.
