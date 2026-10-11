#!/usr/bin/env python3
"""Packs frontend + service into a webOS IPK (same layout as ares-package).

  make_ipk.py APP_DIR SERVICE_DIR OUT_DIR
"""
import io
import json
import os
import sys
import tarfile
import time


def add_tree(tar, src, arc_prefix):
    for root, dirs, files in os.walk(src):
        dirs.sort()
        rel = os.path.relpath(root, src)
        arc_root = arc_prefix if rel == "." else f"{arc_prefix}/{rel}"
        ti = tarfile.TarInfo(arc_root)
        ti.type = tarfile.DIRTYPE
        ti.mode = 0o755
        ti.mtime = int(time.time())
        tar.addfile(ti)
        for f in sorted(files):
            p = os.path.join(root, f)
            ti = tar.gettarinfo(p, f"{arc_root}/{f}")
            ti.uid = ti.gid = 0
            ti.uname = ti.gname = "root"
            ti.mode = 0o755 if os.access(p, os.X_OK) else 0o644
            with open(p, "rb") as fh:
                tar.addfile(ti, fh)


def add_bytes(tar, name, data, mode=0o644):
    ti = tarfile.TarInfo(name)
    ti.size = len(data)
    ti.mode = mode
    ti.mtime = int(time.time())
    tar.addfile(ti, io.BytesIO(data))


def add_dirs(tar, *names):
    for n in names:
        ti = tarfile.TarInfo(n)
        ti.type = tarfile.DIRTYPE
        ti.mode = 0o755
        ti.mtime = int(time.time())
        tar.addfile(ti)


def ar_member(name, data):
    hdr = f"{name:<16}{int(time.time()):<12}{0:<6}{0:<6}{'100644':<8}{len(data):<10}`\n".encode()
    assert len(hdr) == 60
    return hdr + data + (b"\n" if len(data) % 2 else b"")


def main():
    app_dir, svc_dir, out_dir = sys.argv[1:4]
    app = json.load(open(os.path.join(app_dir, "appinfo.json")))
    svc = json.load(open(os.path.join(svc_dir, "services.json")))
    app_id, version = app["id"], app["version"]
    svc_id = svc["id"]

    pkginfo = {"id": app_id, "version": version, "app": app_id, "services": [svc_id]}

    data_buf = io.BytesIO()
    with tarfile.open(fileobj=data_buf, mode="w:gz", format=tarfile.GNU_FORMAT) as tar:
        add_dirs(tar, "usr", "usr/palm", "usr/palm/applications")
        add_tree(tar, app_dir, f"usr/palm/applications/{app_id}")
        add_dirs(tar, "usr/palm/packages", f"usr/palm/packages/{app_id}")
        add_bytes(tar, f"usr/palm/packages/{app_id}/packageinfo.json", json.dumps(pkginfo, indent=2).encode())
        add_dirs(tar, "usr/palm/services")
        add_tree(tar, svc_dir, f"usr/palm/services/{svc_id}")
    data = data_buf.getvalue()

    installed_bytes = 0
    for d in (app_dir, svc_dir):
        for root, _, files in os.walk(d):
            installed_bytes += sum(os.path.getsize(os.path.join(root, f)) for f in files)
    control = (
        f"Package: {app_id}\nVersion: {version}\nSection: misc\nPriority: optional\nArchitecture: all\n"
        f"Installed-Size: {installed_bytes}\nMaintainer: N/A <nomail@example.com>\n"
        f"Description: {app.get('appDescription', app.get('title', app_id))}\n"
        f"webOS-Package-Format-Version: 2\nwebOS-Packager-Version: x.y.x\n"
    ).encode()
    ctrl_buf = io.BytesIO()
    with tarfile.open(fileobj=ctrl_buf, mode="w:gz", format=tarfile.GNU_FORMAT) as tar:
        add_bytes(tar, "control", control)
    ctrl = ctrl_buf.getvalue()

    out = os.path.join(out_dir, f"{app_id}_{version}_all.ipk")
    with open(out, "wb") as f:
        f.write(b"!<arch>\n")
        f.write(ar_member("debian-binary", b"2.0\n"))
        f.write(ar_member("control.tar.gz", ctrl))
        f.write(ar_member("data.tar.gz", data))
    print(out)


if __name__ == "__main__":
    main()
