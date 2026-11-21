import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const source = path.dirname(fileURLToPath(import.meta.url));
const output = path.resolve(source, '../../../../TestData/Tube/SectionNormalSweepDemo');
const read = name => fs.readFileSync(path.join(source, name), 'utf8');
const importLine = "import { analyzeSection, castAt, curvePath } from './section-normal-sweep.mjs';";
const loader = /async function loadCases\(\) \{[^\n]*\n[\s\S]*?\n    \}/;
const sharedLoader = /async function loadSharedCases\(\) \{[^\n]*\n[\s\S]*?\n    \}/;
const viewer = read('index.html');
if (!viewer.includes(importLine) || !loader.test(viewer) || !sharedLoader.test(viewer))
  throw new Error('Unexpected viewer import or case loader');

const engine = read('section-normal-sweep.mjs').replace(/^export function /gm, 'function ');
const asLiteral = data => JSON.stringify(data).replaceAll('<', '\\u003c');
const cases = asLiteral(JSON.parse(read('cases.json')).cases);
const sharedFile = path.join(source, 'shared-cases.json');
const shared = asLiteral(fs.existsSync(sharedFile) ? JSON.parse(read('shared-cases.json')).cases : []);
const standalone = viewer
  .replace(importLine, () =>
    `const { analyzeSection, castAt, curvePath } = (() => {\n${engine}\nreturn { analyzeSection, castAt, curvePath };\n})();`)
  .replace(loader, () => `async function loadCases() { return ${cases}; }`)
  .replace(sharedLoader, () => `async function loadSharedCases() { return ${shared}; }`);
fs.mkdirSync(output, { recursive: true });
fs.writeFileSync(path.join(output, 'index.html'), standalone);
console.log(path.join(output, 'index.html'));
