import assert from 'node:assert/strict';
import {existsSync,readFileSync,readdirSync} from 'node:fs';
import {buildCatalogEntries} from '../../apps/tube-designer/webpage/productCatalog.mjs';
const productRoot=new URL('../../apps/tube-designer/templates/product/',import.meta.url);
const read=path=>JSON.parse(readFileSync(path,'utf8'));
const retired=[['louver-window','louver_window'],['aluminium-window','aluminium_window'],['decorative-door','decorative_door']];
const active=readdirSync(productRoot,{withFileTypes:true}).filter(entry=>entry.isDirectory()&&existsSync(new URL(`${entry.name}/template.json`,productRoot))).map(entry=>read(new URL(`${entry.name}/template.json`,productRoot)));
assert.equal(active.length,10);
assert.equal(new Set(active.map(template=>template.id)).size,10);
assert.equal(active.filter(template=>template.extensions?.catalog?.listed===false).length,3);
assert.equal(buildCatalogEntries(active.map(template=>({...template,available:true}))).length,7);
const manifest=read(new URL('../../apps/tube-designer/product.manifest.json',import.meta.url));
const registrations=manifest.capabilities.tubeDesigner.templates;
assert.equal(registrations.length,8);
for(const registration of registrations){const template=active.find(template=>template.id===registration.templateId);assert.ok(template,registration.templateId);assert.equal(registration.version,template.version,`${template.id}: current manifest version`);}
for(const [id,directory]of retired){
  assert.equal(registrations.some(template=>template.templateId===id),false);
  assert.equal(active.some(template=>template.id===id),false);
  assert.equal(existsSync(new URL(`${directory}/`,productRoot)),false);
  const reference=read(new URL(`../../apps/tube-designer/docs/deferred-products/${id}/reference/template.json`,import.meta.url));
  assert.equal(reference.id,id,'The original descriptor remains documentation only');
}
const nativeSource=readFileSync(new URL('../../iCAX-Plugins/product/TubeDesigner/TubeDesignerSDO.cpp',import.meta.url),'utf8');
const discover=nativeSource.slice(nativeSource.indexOf('DiscoverPythonTemplateDirectoriesAt('),nativeSource.indexOf('DiscoverPythonTemplateDirectories(',nativeSource.indexOf('DiscoverPythonTemplateDirectoriesAt(')+1));
assert.match(discover,/TemplateRoot_\s*\/\s*"product"/);
assert.match(discover,/std::filesystem::directory_iterator/);
assert.doesNotMatch(discover,/recursive_directory_iterator|docs|deferred-products/);
for(const [id]of retired)assert.equal(nativeSource.includes(`"${id}"`),false,'Native host has no retired package registration');
console.log('Retired products are documentation references only; 10 active packages / 7 visible / 8 current manifest registrations.');
