"""Read-only local viewer for the machining-edge analysis stage."""
from __future__ import annotations

import argparse
import json
import mimetypes
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import quote, unquote, urlsplit


HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
THREE = REPO / "src/iCAX-UI/SDK/ThirdParty/three"
LINES = REPO / "src/tools/testing/cut_unit_design_probe/vendor/three-lines"


class Handler(SimpleHTTPRequestHandler):
    extensions_map = {**SimpleHTTPRequestHandler.extensions_map,
                      ".mjs": "text/javascript", ".js": "text/javascript",
                      ".json": "application/json; charset=utf-8"}

    def __init__(self, *args, data_root: Path, **kwargs):
        self.data_root = data_root.resolve()
        super().__init__(*args, directory=str(HERE), **kwargs)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def do_GET(self):
        if urlsplit(self.path).path == "/manifest.json":
            catalog = self.data_root / "manifest.json"
            if catalog.is_file():
                return self._file(catalog)
            models = []
            if self.data_root.is_dir():
                for file in sorted(self.data_root.rglob("*.json")):
                    if file.name in {"manifest.json", "index.json", "summary.json", "validation.json"}:
                        continue
                    try:
                        data = json.loads(file.read_text(encoding="utf-8-sig"))
                    except (OSError, ValueError):
                        continue
                    if not isinstance(data, dict) or not isinstance(data.get("edges"), list):
                        continue
                    rel = file.relative_to(self.data_root).as_posix()
                    models.append({"id": len(models) + 1, "name": data.get("name", file.stem),
                                   "dataUrl": "/data/" + quote(rel, safe="/"),
                                   "faceCount": len(data.get("faces", [])),
                                   "edgeCount": len(data["edges"])})
            content = json.dumps({"schema": 1, "stage": "edgeAnalysis", "models": models},
                                 ensure_ascii=False).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(content)))
            self.end_headers()
            self.wfile.write(content)
            return
        return super().do_GET()

    def translate_path(self, path):
        route = unquote(urlsplit(path).path)
        if route == "/":
            return str(HERE / "viewer.html")
        for prefix, root in (("/three/", THREE), ("/three-lines/", LINES), ("/data/", self.data_root)):
            if route.startswith(prefix):
                resolved = (root / route[len(prefix):]).resolve()
                if resolved.is_relative_to(root.resolve()):
                    return str(resolved)
                return str(HERE / "__invalid_path__")
        resolved = (HERE / route.lstrip("/")).resolve()
        return str(resolved) if resolved.is_relative_to(HERE) else str(HERE / "__invalid_path__")

    def _file(self, path):
        try:
            content = path.read_bytes()
        except OSError:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", mimetypes.guess_type(path)[0] or "application/octet-stream")
        self.send_header("Content-Length", str(len(content)))
        self.end_headers()
        self.wfile.write(content)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8769)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--data", type=Path, default=REPO / "TestData/Tube/EdgeAnalysis")
    options = parser.parse_args()
    server = ThreadingHTTPServer((options.host, options.port), partial(Handler, data_root=options.data))
    print(f"http://{options.host}:{options.port}/  data={options.data.resolve()}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
