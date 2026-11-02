"""Regenerate attribution from the pinned COPP source Cargo.lock (not at runtime)."""
import argparse
import concurrent.futures
import hashlib
import io
import json
from pathlib import Path
import tarfile
import time
import tomllib
import urllib.request

parser = argparse.ArgumentParser()
parser.add_argument("--source", type=Path, required=True)
parser.add_argument("--cache", type=Path, required=True)
args = parser.parse_args()
lock = tomllib.loads((args.source / "Cargo.lock").read_text(encoding="utf-8"))
packages = lock["package"]
roots = {"nalgebra": "0.35.0", "clarabel": "0.11.1", "thiserror": "2.0.20",
         "itertools": "0.15.0", "chrono": "0.4.45", "rayon": "1.12.0", "log": "0.4.34"}

def lookup(name, version=None):
    matches = [p for p in packages if p["name"] == name and (version is None or p["version"] == version)]
    if len(matches) != 1:
        raise ValueError((name, version))
    return matches[0]

selected = {}
def visit(package):
    key = (package["name"], package["version"])
    if key in selected:
        return
    selected[key] = package
    for dependency in package.get("dependencies", []):
        fields = dependency.split()
        visit(lookup(fields[0], fields[1] if len(fields) > 1 else None))

for name, version in roots.items():
    visit(lookup(name, version))
args.cache.mkdir(parents=True, exist_ok=True)

def collect(package):
    name, version = package["name"], package["version"]
    url = f"https://static.crates.io/crates/{name}/{name}-{version}.crate"
    cached = args.cache / f"{name}-{version}.crate"
    if not cached.exists():
        for attempt in range(4):
            try:
                with urllib.request.urlopen(url, timeout=60) as response:
                    cached.write_bytes(response.read())
                break
            except Exception:
                if attempt == 3:
                    raise
                time.sleep(1)
    data = cached.read_bytes()
    if hashlib.sha256(data).hexdigest() != package["checksum"]:
        raise ValueError(f"Checksum mismatch: {name} {version}")
    notices = []
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
        root = f"{name}-{version}/"
        metadata = tomllib.loads(archive.extractfile(root + "Cargo.toml").read().decode("utf-8"))["package"]
        for member in sorted(archive.getmembers(), key=lambda m: m.name):
            relative = member.name.removeprefix(root)
            if member.isfile() and any(token in Path(relative).name.lower() for token in ("license", "licence", "copying", "copyright", "notice")):
                if member.size < 150000:
                    notices.append((relative, archive.extractfile(member).read().decode("utf-8", errors="replace")))
        if not notices:
            raise ValueError(f"No license text in {name} {version}")
    entry = dict(name=name, version=version, license=metadata.get("license"), source=url,
                 sha256=package["checksum"], noticeFiles=[name for name, _ in notices])
    text = f"\n{'=' * 72}\n{name} {version}\nLicense: {entry['license']}\nSource: {url}\n"
    for filename, body in notices:
        text += f"\n--- {filename} ---\n{body.rstrip()}\n"
    return entry, text

with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
    results = list(pool.map(collect, sorted(selected.values(), key=lambda p: (p["name"], p["version"]))))
destination = Path(__file__).parent
header = """COPP 0.2.3 third-party attribution
Pinned source: https://github.com/TOPP-THU/copp/tree/78bc8fe907cb425be7c29b74ae3b1c44a79aaae9
SDK: copp-c-v0.2.3-windows-x86_64-msvc-dynamic
This conservative inventory follows the locked dependency closure of the seven
normal COPP dependencies used by its C ABI build. It also attributes optional
and non-Windows target packages in that closure; inclusion does not mean that
every package is linked into this Windows DLL. COPP's own MIT license is supplied
separately. Original crate archives were verified against Cargo.lock SHA256.
"""
(destination / "THIRD-PARTY-NOTICES.txt").write_text(header + "".join(text for _, text in results), encoding="utf-8")
(destination / "third-party-manifest.json").write_text(json.dumps({"coppCommit": "78bc8fe907cb425be7c29b74ae3b1c44a79aaae9",
    "cargoLockSha256": hashlib.sha256((args.source / "Cargo.lock").read_bytes()).hexdigest(),
    "packages": [entry for entry, _ in results]}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(f"Verified attribution for {len(results)} locked packages")
