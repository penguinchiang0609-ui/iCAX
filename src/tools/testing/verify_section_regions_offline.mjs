import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import vm from 'node:vm';

const root = path.resolve(import.meta.dirname, '../../..');
const source = path.join(root, 'TestData/Tube/SectionRegionsQuasi-20261117');
const output = path.join(root, 'TestData/Tube/SectionRegionsQuasi-20261117-Offline');
const digest = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const context = {window: {}};
vm.runInNewContext(fs.readFileSync(path.join(output, 'bundle-data.js'), 'utf8'), context);
const bundle = context.window.SECTION_REGIONS_BUNDLE;
const failures = [];
const check = (ok, message) => {if (!ok) failures.push(message);};

for (const htmlName of ['index.html', 'fitting.html']) {
  const html = fs.readFileSync(path.join(output, htmlName), 'utf8');
  check(html.includes('<script src="bundle-data.js"></script><script>'), `${htmlName}: bundled data is not loaded before the page`);
  check(html.includes('const fetch = async url =>'), `${htmlName}: offline data reader is missing`);
  const inline = html.match(/<script>([\s\S]*?)<\/script>/)?.[1];
  try {new Function(inline);} catch (error) {failures.push(`${htmlName}: ${error.message}`);}
}

let cases = 0;
for (const set of ['contours', 'fixtures']) {
  const manifest = bundle[`${set}/manifest.json`];
  check(manifest?.allFinished, `${set}: incomplete manifest`);
  check(manifest?.records.length === (set === 'contours' ? 270 : 15), `${set}: unexpected record count`);
  for (const record of manifest.records) {
    cases++;
    const prefix = `${set}/${record.id}/`;
    const region = bundle[prefix + 'regions.json'];
    const carrier = bundle[prefix + 'regions-carrier.json'];
    const fitting = bundle[prefix + 'regions-fitting.json'];
    check(region?.coverageValid && region.regions.length === record.regionCount, `${prefix}: region data does not match manifest`);
    check(carrier && Array.isArray(carrier.lines), `${prefix}: missing carrier`);
    check(fitting?.curveCount === fitting.curves.length, `${prefix}: missing fitting data`);
    for (const name of ['regions.json', 'regions-carrier.json', 'regions-fitting.json', 'regions.brep', 'run.json']) {
      const file = path.join(output, prefix, name);
      check(fs.existsSync(file), `${prefix}: missing ${name}`);
      if (fs.existsSync(file)) check(digest(file) === digest(path.join(source, prefix, name)), `${prefix}: ${name} differs from source`);
    }
  }
}

const result = {cases, dataEntries: Object.keys(bundle).length, failures, passed: failures.length === 0};
console.log(JSON.stringify(result, null, 2));
if (failures.length) process.exitCode = 1;
