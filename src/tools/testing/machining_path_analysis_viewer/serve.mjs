import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const viewer = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(viewer, '../../../..');
const args = process.argv.slice(2);
for (let i = 0; i < args.length; i += 2)
  if (!['--port', '--data'].includes(args[i]) || !args[i + 1]) throw new Error('Unknown or incomplete option: ' + args[i]);
const option = (name, fallback) => args.includes(name) ? args[args.indexOf(name) + 1] : fallback;
const port = Number(option('--port', '8770'));
const data = path.resolve(option('--data', path.join(repo, 'TestData/Tube/PathAnalysis')));
const roots = [
  ['/three/', path.join(repo, 'src/iCAX-UI/SDK/ThirdParty/three')],
  ['/three-lines/', path.join(repo, 'src/tools/testing/cut_unit_design_probe/vendor/three-lines')],
  ['/data/WorkInspection/', path.join(repo, 'TestData/Tube/WorkInspection')],
  ['/data/Penetration/', path.join(repo, 'TestData/Tube/Penetration')],
  ['/data/PathAnalysis/', path.join(repo, 'TestData/Tube/PathAnalysis')],
  ['/section/', path.join(repo, 'TestData/Tube')],
  ['/data/', data],
];
const contentTypes = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8', '.json': 'application/json; charset=utf-8',
  '.css': 'text/css; charset=utf-8', '.svg': 'image/svg+xml; charset=utf-8' };

function fileFor(url) {
  const pathname = decodeURIComponent(new URL(url, 'http://localhost').pathname);
  if (pathname === '/') return path.join(viewer, 'viewer.html');
  if (pathname === '/manifest.json') return path.join(data, 'manifest.json');
  const [prefix, root] = roots.find(([prefix]) => pathname.startsWith(prefix)) ?? ['/', viewer];
  const file = path.resolve(root, pathname.slice(prefix.length));
  if (!file.startsWith(root + path.sep)) return null;
  return file;
}

http.createServer((request, response) => {
  try {
    const file = fileFor(request.url);
    if (!file || !fs.statSync(file).isFile()) {
      response.writeHead(404).end();
      return;
    }
    response.setHeader('Content-Type', contentTypes[path.extname(file)] || 'application/octet-stream');
    response.setHeader('Cache-Control', 'no-store');
    if (request.method === 'HEAD') response.end();
    else fs.createReadStream(file).on('error', error => response.destroy(error)).pipe(response);
  } catch {
    response.writeHead(404).end();
  }
}).listen(port, '127.0.0.1', () => console.log(`http://127.0.0.1:${port}/ data=${data}`));
