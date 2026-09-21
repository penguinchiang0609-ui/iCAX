import assert from 'node:assert/strict';
import {existsSync,readFileSync,readdirSync} from 'node:fs';
import {libraryTools,toolLibraryState,renderToolLibraryRightPane,buildToolLibraryPreviewPayload,toolPreviewKey} from '../../apps/tube-designer/webpage/toolLibrary.mjs';
const root=new URL('../../apps/tube-designer/templates/',import.meta.url);
const read=p=>JSON.parse(readFileSync(new URL(p,root)));
const defaults=d=>Object.fromEntries(d.parameters.map(p=>[p.key,p.defaultValue]));
const tools=readdirSync(new URL('mold/',root)).filter(id=>id.startsWith('end-')&&existsSync(new URL(`mold/${id}/tool.json`,root))).map(id=>{
  const d=read(`mold/${id}/tool.json`);return {...d,defaultParameters:defaults(d)};
});
const profiles=['round','rect'].map(id=>{const d=read(`profile/${id}/profile.json`);return {
  id,name:d.displayName,libraryScope:'system',profileForm:'parametric',profileType:'profile-package',descriptor:d,
  defaultParameters:defaults(d),previewProfile:{contours:[{kind:'circle',radius:id==='round'?20:30}]},
};});
const view={activeAreaId:'tools',tubeDesignerSystemPunchTools:tools,tubeDesignerSystemProfiles:profiles};
const state=toolLibraryState(view);
for(const tool of libraryTools(view)){
  state.selectedKey=tool.libraryKey;
  const before=JSON.stringify(tool),html=renderToolLibraryRightPane({},view),payload=buildToolLibraryPreviewPayload(view,tool);
  assert.match(html,/目标管型/);assert.doesNotMatch(html,/当前为定式工艺/);
  for(const p of tool.operationParameters){
    assert.match(html,new RegExp(`data-tube-tool-library-operation-parameter="${p.key}"`));
    assert.equal(payload.ends.start[p.key],p.defaultValue);
  }
  const key=toolPreviewKey(view,tool);
  state.operationDrafts[tool.libraryKey]={trim:17};
  assert.equal(buildToolLibraryPreviewPayload(view,tool).ends.start.trim,17);
  assert.notEqual(toolPreviewKey(view,tool),key);
  assert.equal(JSON.stringify(tool),before);
  if(tool.requiresSection){
    assert.match(html,/刀具截面/);
    state.profileKey='system:rect';state.branchProfileKey='system:round';
    const mainKey=toolPreviewKey(view,tool);
    const result=buildToolLibraryPreviewPayload(view,tool);
    assert.equal(result.profileRef.id,'rect');assert.equal(result.ends.start.section.profileRef.id,'round');
    assert.deepEqual(result.ends.start.section.profile,profiles[0].previewProfile);
    state.branchProfileDrafts['system:round']={outerDiameter:55};
    assert.notEqual(toolPreviewKey(view,tool),mainKey,'the tool section must invalidate its own preview');
    assert.equal(buildToolLibraryPreviewPayload(view,tool).ends.start.section.parameters.outerDiameter,55);
  }
}
assert.equal(tools.length,4);
assert.deepEqual(tools.map(tool=>tool.id).sort(),['end-key-joint','end-miter','end-profile','end-step-z']);
console.log('Four end resources: declarative pose controls, target input, independent tool section, preview identity and immutable descriptors passed.');
