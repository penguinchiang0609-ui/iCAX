import fs from 'node:fs';
import path from 'node:path';

const root = path.resolve(import.meta.dirname, '../../..');
const source = path.join(root, 'TestData/Tube/SectionRegionsQuasi-20261117');
const output = path.join(root, 'TestData/Tube/SectionRegionsQuasi-20261117-Offline');
const sets = ['contours', 'fixtures'];
const data = Object.create(null);
const read = file => JSON.parse(fs.readFileSync(file, 'utf8'));

fs.mkdirSync(output, {recursive: true});
for (const set of sets) {
  const manifest = read(path.join(source, set, 'manifest.json'));
  if (!manifest.allFinished || manifest.records.some(record => !record.result || !record.brep)) {
    throw new Error(`${set} does not contain a completed batch`);
  }
  data[`${set}/manifest.json`] = manifest;
  for (const record of manifest.records) {
    for (const name of ['regions.json', 'regions-carrier.json', 'regions-fitting.json']) {
      const relative = `${set}/${record.id}/${name}`;
      data[relative] = read(path.join(source, relative));
    }
  }
  fs.cpSync(path.join(source, set), path.join(output, set), {recursive: true, force: true});
}

const offlineFetch = `<script src="bundle-data.js"></script><script>
const fetch = async url => {
  const found = Object.prototype.hasOwnProperty.call(window.SECTION_REGIONS_BUNDLE, url);
  return {ok: found, json: async () => {
    if (!found) throw new Error('离线包缺少数据：' + url);
    return window.SECTION_REGIONS_BUNDLE[url];
  }};
};`;
for (const name of ['index.html', 'fitting.html']) {
  const original = fs.readFileSync(path.join(source, name), 'utf8');
  if (!original.includes('<script type="module">')) throw new Error(`${name} has no module script`);
  fs.writeFileSync(path.join(output, name), original.replace('<script type="module">', offlineFetch));
}
fs.writeFileSync(path.join(output, 'bundle-data.js'),
  'window.SECTION_REGIONS_BUNDLE = ' + JSON.stringify(data) + ';\n');
fs.copyFileSync(path.join(source, 'verification.json'), path.join(output, 'verification.json'));
fs.writeFileSync(path.join(output, '打开说明.txt'),
  `解压后双击 index.html，即可在浏览器中查看全部 ${data['contours/manifest.json'].records.length} 份轮廓。\r\n` +
  '左侧可切换实际轮廓和验证样本；点击区域查看面积，点击“轮廓拟合”检查曲线。\r\n' +
  '结果 JSON 和完整 BREP 保存在 contours、fixtures 子目录，无需安装软件或启动服务。\r\n');

console.log(JSON.stringify({
  output,
  contours: data['contours/manifest.json'].records.length,
  fixtures: data['fixtures/manifest.json'].records.length,
  bundleBytes: fs.statSync(path.join(output, 'bundle-data.js')).size,
  dataEntries: Object.keys(data).length,
}, null, 2));
