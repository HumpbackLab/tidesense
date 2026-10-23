#!/usr/bin/env python3
"""Fetch the SDK manifest for a fresh CI checkout and record resolved commits."""

import json
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path


def git(*args):
    return subprocess.check_output(["git", *map(str, args)], text=True).strip()


def main():
    root = Path.cwd()
    manifest = ET.parse(root / "scripts/subtree.xml").getroot()
    default = manifest.find("default").attrib
    remotes = {r.attrib["name"]: r.attrib["fetch"] for r in manifest.findall("remote")}
    # SchurVINS must match the application's lock, rather than master HEAD.
    lock = dict(line.split("=", 1) for line in
                (root / "scripts/schurvins/sources.lock").read_text().splitlines()
                if line and not line.startswith("#"))
    sources = [{"path": ".", "commit": git("rev-parse", "HEAD")}]
    for project in manifest.findall("project"):
        spec = project.attrib
        path = spec.get("path", spec["name"])
        target = root / path
        if not target.resolve().is_relative_to(root) or target.exists():
            raise SystemExit(f"SDK destination must be new and inside the checkout: {path}")
        remote = spec.get("remote_url") or remotes[spec.get("remote", default["remote"])]
        url = f"{remote.rstrip('/')}/{spec['name']}.git"
        revision = spec.get("revision", default["revision"])
        if spec["name"] == "SchurVINS":
            revision = lock["SCHUR_REV"]
        target.mkdir(parents=True)
        git("init", target)
        git("-C", target, "remote", "add", "origin", url)
        # Fetching a ref also supports a manifest pinned to a full commit SHA.
        for attempt in range(3):
            try:
                git("-C", target, "fetch", "--depth", "1", "origin", revision)
                break
            except subprocess.CalledProcessError:
                if attempt == 2:
                    raise
        git("-C", target, "checkout", "--detach", "FETCH_HEAD")
        if spec.get("sync-s") == "true":
            git("-C", target, "submodule", "update", "--init", "--recursive")
        sources.append({"path": path, "url": url, "revision": revision,
                        "commit": git("-C", target, "rev-parse", "HEAD")})
    release_dir = root / "release"
    release_dir.mkdir(exist_ok=True)
    (release_dir / "sdk-sources.json").write_text(json.dumps(sources, indent=2) + "\n")


if __name__ == "__main__":
    main()
