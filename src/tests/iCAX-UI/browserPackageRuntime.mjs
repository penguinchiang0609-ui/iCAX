// Test-only asset selection. Explicit installations never fall back to source.
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, realpathSync, writeFileSync } from 'node:fs';
import { resolve, sep } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const sourceRoot = fileURLToPath(new URL('../../', import.meta.url));
export const browserAssetRoot = resolve(process.env.ICAX_BROWSER_RUNTIME_ROOT || sourceRoot);
const realRoot = realpathSync(browserAssetRoot);
const assets = new Map();
const requests = [];

export function browserAssetPath(relative) {
  const path = resolve(browserAssetRoot, relative);
  if (!path.toLowerCase().startsWith((browserAssetRoot + sep).toLowerCase()) || !existsSync(path))
    throw new Error(`Browser production asset is missing or outside its selected root: ${path}`);
  const actual = realpathSync(path);
  if (!actual.toLowerCase().startsWith((realRoot + sep).toLowerCase()))
    throw new Error(`Browser production asset resolves outside its selected root: ${path}`);
  const bytes = readFileSync(path);
  assets.set(path, { path, bytes: bytes.length, sha256: createHash('sha256').update(bytes).digest('hex') });
  return path;
}

export function readBrowserAsset(relative) {
  return readFileSync(browserAssetPath(relative), 'utf8');
}

export function importBrowserAsset(relative) {
  return import(pathToFileURL(browserAssetPath(relative)).href);
}

export function browserReportDirectory(fallback) {
  const path = resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY || resolve(sourceRoot, '../output/tests', fallback));
  mkdirSync(path, { recursive: true });
  return path;
}

export function serveBrowserAsset(route) {
  const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
  if (pathname === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><body></body>' });
  if (!pathname.startsWith('/src/') || !/\.m?js$/.test(pathname)) return route.abort();
  const path = browserAssetPath(pathname.slice('/src/'.length));
  requests.push({ url: route.request().url(), path });
  return route.fulfill({ contentType: 'text/javascript', body: readFileSync(path, 'utf8') });
}

process.on('exit', () => {
  if (!process.env.ICAX_BROWSER_REPORT_DIRECTORY) return;
  const directory = browserReportDirectory('browser-package');
  writeFileSync(resolve(directory, 'asset-audit.json'), JSON.stringify({
    assetRoot: browserAssetRoot,
    installationSelected: Boolean(process.env.ICAX_BROWSER_RUNTIME_ROOT),
    browserEngine: 'Playwright Chromium',
    nativeTransport: 'controlled test protocol fixture',
    document: 'test DOM fixture with actual production UI modules',
    standaloneCefEndToEnd: false,
    assets: [...assets.values()], requests,
  }, null, 2));
});
