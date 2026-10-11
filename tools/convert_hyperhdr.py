#!/usr/bin/env python3
"""Uebernimmt LED-Layout und Farbwerte aus einem HyperHDR-Backup (JSON-Export)
und erzeugt daraus die Dateien fuer Ambilite:

  leds.json     – LED-Rechtecke (hmin/hmax/vmin/vmax, 0..1), Reihenfolge = Kabelreihenfolge
  config.json   – Startwerte (Adalight/AWA, Baudrate, Farbreihenfolge, Gamma, Saettigung, ...)

Aufruf:
  python3 convert_hyperhdr.py HyperHDR-Backup.json  ZIELORDNER  [--instance 0]
"""
import json
import sys
import argparse


def settings_for(backup, instance):
    out = {}
    for entry in backup["settings"]:
        inst = entry.get("hyperhdr_instance")
        if inst is not None and inst != instance:
            continue
        cfg = entry["config"]
        if isinstance(cfg, str):
            cfg = json.loads(cfg)
        out[entry["type"]] = cfg
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("backup")
    ap.add_argument("outdir")
    ap.add_argument("--instance", type=int, default=0)
    ap.add_argument("--repo", default="", help="GitHub owner/repo for updates")
    a = ap.parse_args()

    backup = json.load(open(a.backup, encoding="utf-8"))
    s = settings_for(backup, a.instance)

    leds = [
        {k: round(float(l[k]), 5) for k in ("hmin", "hmax", "vmin", "vmax")}
        for l in s["leds"]
    ]

    dev = s.get("device", {})
    color = s.get("color", {}).get("channelAdjustment", [{}])[0]
    bb = s.get("blackborderdetector", {})

    gamma = (color.get("gammaRed", 1.5) + color.get("gammaGreen", 1.5) + color.get("gammaBlue", 1.5)) / 3

    cfg = {
        "autostart": True,
        "enabled": True,
        "brightness": 100,
        "saturation": round(float(color.get("saturationGain", 1.0)), 2),
        "gamma": round(gamma, 2),
        "smoothing_mode": 2,
        "adaptive_strength": 50,
        "release_ms": 150,
        "spatial": 30,
        "hdr_auto": True,
        "hdr_brightness": 100,
        "hdr_saturation": round(min(float(color.get("saturationGain", 1.0)) + 0.4, 3.0), 2),
        "hdr_gamma": 1.2,
        "blackborder": bool(bb.get("enable", True)),
        "blackborder_threshold": int(bb.get("threshold", 5)),
        "serial_device": "auto",
        "baudrate": int(dev.get("rate", 2000000)),
        "awa_mode": bool(dev.get("awa_mode", True)),
        "color_order": dev.get("colorOrder", "rgb"),
        "width": 320,
        "height": 180,
        "fps": 0,
        "vsync": True,
        "backend": "auto",
        "uibackend": "auto",
        "no_gui": False,
        "quirks": 0,
        "hotkey": 0,
        "api_enabled": True,
        "api_port": 8780,
        "github_repo": a.repo,
    }

    with open(f"{a.outdir}/leds.json", "w") as f:
        json.dump(leds, f, separators=(",", ":"))
    with open(f"{a.outdir}/config.default.json", "w") as f:
        json.dump(cfg, f, indent=2)

    print(f"{len(leds)} LEDs uebernommen, Geraet: {dev.get('type')} @ {cfg['baudrate']} Baud, "
          f"AWA={cfg['awa_mode']}, Farbreihenfolge={cfg['color_order']}, "
          f"Gamma={cfg['gamma']}, Saettigung={cfg['saturation']}")


if __name__ == "__main__":
    sys.exit(main())
