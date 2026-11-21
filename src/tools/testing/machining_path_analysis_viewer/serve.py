"""Read-only whole-model path and tool-axis inspection server."""
from __future__ import annotations
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlsplit

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
THREE = REPO / 'src/iCAX-UI/SDK/ThirdParty/three'
LINES = REPO / 'src/tools/testing/cut_unit_design_probe/vendor/three-lines'

class Handler(SimpleHTTPRequestHandler):
    extensions_map = {**SimpleHTTPRequestHandler.extensions_map, '.mjs':'text/javascript', '.js':'text/javascript', '.json':'application/json; charset=utf-8'}
    def __init__(self, *args, data_root: Path, **kwargs):
        self.data_root=data_root.resolve()
        super().__init__(*args,directory=str(HERE),**kwargs)
    def end_headers(self):
        self.send_header('Cache-Control','no-store')
        super().end_headers()
    def translate_path(self,path):
        route=unquote(urlsplit(path).path)
        if route=='/': return str(HERE/'viewer.html')
        if route=='/manifest.json': return str(self.data_root/'manifest.json')
        for prefix,root in (('/three/',THREE),('/three-lines/',LINES),
                            ('/data/Penetration/',REPO/'TestData/Tube/Penetration'),
                            ('/data/PathAnalysis/',REPO/'TestData/Tube/PathAnalysis'),('/data/',self.data_root)):
            if route.startswith(prefix):
                resolved=(root/route[len(prefix):]).resolve()
                return str(resolved) if resolved.is_relative_to(root.resolve()) else str(HERE/'__invalid_path__')
        resolved=(HERE/route.lstrip('/')).resolve()
        return str(resolved) if resolved.is_relative_to(HERE) else str(HERE/'__invalid_path__')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',type=int,default=8770)
    parser.add_argument('--host',default='127.0.0.1')
    parser.add_argument('--data',type=Path,default=REPO/'TestData/Tube/PathAnalysis')
    options=parser.parse_args()
    server=ThreadingHTTPServer((options.host,options.port),partial(Handler,data_root=options.data))
    print(f'http://{options.host}:{options.port}/ data={options.data.resolve()}',flush=True)
    server.serve_forever()

if __name__=='__main__': main()
