# Changelog

## 0.2.6
- Update-Abfrage: Dialoge, die schon vor dem Start von Ambilite offen sind, werden beim Start (und nach dem Aufwachen aus dem Standby) geschlossen. Danach greift die laufende Erkennung.

## 0.2.5
- Update-Abfrage: erkennt jetzt den Systemdialog (Benachrichtigungsdienst), drückt „Nein“ und schließt ihn.
- Diagnose: letzte Systemmeldung und LG-Update-Status in der Web-App.

## 0.2.4
- „Update-Abfrage automatisch ablehnen“ (Standard: an): erkennt die LG-Software-Update-App beim Start und schließt sie automatisch.
- Diagnose in der Web-App: zuletzt gesehene System-Apps und Anzahl abgelehnter Abfragen.

## 0.2.3
- „Firmware-Updates blockieren“ (Standard: an): sperrt die LG-Update-Server direkt auf dem TV statt im Router, wird bei jedem Start erneut angewendet. Schalter in TV-App und Web-App.

## 0.2.2
- iPhone-Web-App ohne Kopplungscode nutzbar (Standard). Code lässt sich in der TV-App unter „iPhone-Code nötig“ wieder einschalten.

## 0.2.1
- iPhone-Steuerung als Web-App direkt vom TV: im iPhone-Safari http://<TV-IP>:8780 öffnen und „Zum Home-Bildschirm“ – kein Xcode, kein Kabel nötig

## 0.2.0
- Adaptive Glättung (Szenenwechsel-Erkennung, schnelles Aufleuchten, weiches Abdunkeln) als Standard
- Nachbar-LEDs mischen (räumliche Glättung ohne Verzögerung)
- Automatisches HDR-Profil (HDR10/HLG/Dolby Vision)
- Modi ohne Bild: feste Farbe, Effekte (Regenbogen, Atmen, Kerze, Polarlicht)
- HTTP-API mit Kopplungscode für die iPhone-App
- Updates/Downgrades aus GitHub-Releases (TV-App und iPhone-App), Selbst-Elevation nach Update
- iPhone-App (SwiftUI)

## 0.1.0
- Erste Version: Bildabgriff → 323 LEDs → AWA/Adalight, Schwarzbalken, Gamma/Sättigung, TV-App
