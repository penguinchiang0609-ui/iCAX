import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { libraryTools, toolLibraryState, renderToolLibraryRightPane, buildToolLibraryPreviewPayload,
  toolPreviewKey, applyToolLibraryPreview, handleToolLibraryAction } from '../../apps/tube-designer/webpage/toolLibrary.mjs';
import { parameterVisible } from '../../apps/tube-designer/webpage/parameterConditions.mjs';

const ids = [
  'edge-arc-groove', 'embedded-arc-notch', 'segmented-bend',
  'flexible-slit-bend', 'v-notch-sharp',
];
const defaults = d => Object.fromEntries(d.parameters.map(p => [p.key, p.defaultValue]));
const read = path => JSON.parse(readFileSync(new URL(`../../apps/tube-designer/templates/${path}`, import.meta.url)));
const tools = ids.map(id => { const d = read(`mold/${id}/tool.json`); return {...d, defaultParameters: defaults(d)}; });
const profiles = ['round','rect'].map(id => { const d = read(`profile/${id}/profile.json`); return {
  id, name: d.displayName, libraryScope: 'system', profileForm: 'parametric', profileType: 'profile-package',
  descriptor: d, defaultParameters: defaults(d), previewProfile: {contours:[{kind:'circle',radius:20}]},
}; });
const view = {activeAreaId:'tools', tubeDesignerSystemPunchTools:tools, tubeDesignerSystemProfiles:profiles};
const state = toolLibraryState(view), snapshots = [];
view.viewport = { async applyViewSnapshot(snapshot) { snapshots.push(snapshot); return {applied:true, entityIds:snapshot.rows.map(row=>row.entityId)}; } };
for (const tool of libraryTools(view)) {
  state.selectedKey = tool.libraryKey;
  const payload = buildToolLibraryPreviewPayload(view, tool);
  assert.equal(payload.profileRef.id, 'rect', tool.id);
  assert.match(renderToolLibraryRightPane({},view), /目标管型/);
  const input = html => html.match(/<input[^>]*data-tube-tool-library-parameter="wallThickness"[^>]*>/)?.[0];
  assert.match(input(renderToolLibraryRightPane({},view)), /value=""/);
  const request = state.previewRequest = {};
  const key = toolPreviewKey(view, tool);
  await applyToolLibraryPreview({},view,tool,{
    baseGeometry:{url:'resource:base',version:1}, toolPreviews:[{key:'library-preview',target:'feature',geometry:{url:'resource:tool',version:1}}],
    sectionAnalyses:[{index:0,applicable:true,parameters:{wallThickness:3.5}}],previewToolsComplete:true,
  },key,request);
  const measured = input(renderToolLibraryRightPane({},view));
  assert.match(measured, /value="3.5"/); assert.match(measured, /disabled/);
  assert.equal(toolPreviewKey(view,tool),key,'measurement must not trigger a preview loop');
  const measurements = state.preview.response.sectionAnalyses[0].parameters;
  for (const [precise, displayed] of [[1.9999999999999998, '2'], [2.1234560000000005, '2.123456']]) {
    measurements.wallThickness = precise;
    assert.match(input(renderToolLibraryRightPane({},view)),new RegExp(`value="${displayed.replace('.', '\\.')}"`));
    assert.equal(measurements.wallThickness, precise, 'display rounding must not modify measured data');
    assert.equal(toolPreviewKey(view,tool), key);
  }
  await handleToolLibraryAction({},view,'tube-designer-tool-library-parameter-change',{
    value:'9',dataset:{tubeToolLibraryKey:tool.libraryKey,tubeToolLibraryParameter:'wallThickness'},
  },{renderProject(){throw Error('derived fields must not submit');}});
  assert.equal(state.parameterDrafts[tool.libraryKey]?.wallThickness,undefined);
  state.profileDrafts['system:rect']={wallThickness:4};
  assert.match(input(renderToolLibraryRightPane({},view)),/value=""/,'never show stale measurement');
  state.profileDrafts = {};
  await assert.rejects(applyToolLibraryPreview({},view,tool,{
    baseGeometry:{url:'resource:new-base',version:2},previewToolsComplete:false,resultError:'该截面缺少平直基准壁',
    toolPreviews:[{key:'partial',target:'feature',geometry:{url:'resource:partial-tool',version:1}}],
  },key,request),/缺少平直基准壁/);
  assert.deepEqual(snapshots.at(-1).rows.map(row=>row.entityId),['punch-preview-blank']);
  assert.equal(state.preview,null); assert.equal(view.preserveCustomViewportEntities,true);
}
// Switching tools applies declaration defaults, then remembers an explicit choice.
const [edge,embedded] = libraryTools(view);
state.selectedKey = edge.libraryKey; buildToolLibraryPreviewPayload(view,edge);
state.profileKey = 'system:round';
state.selectedKey = embedded.libraryKey; assert.equal(buildToolLibraryPreviewPayload(view,embedded).profileRef.id,'rect');
state.selectedKey = edge.libraryKey; assert.equal(buildToolLibraryPreviewPayload(view,edge).profileRef.id,'round');
const renamed = {...edge,id:'custom-notch',libraryKey:'user::custom-notch',preview:{profileRef:{scope:'system',id:'rect'}}};
assert.equal(buildToolLibraryPreviewPayload(view,renamed).profileRef.id,'rect','default is declarative, not ID-specific');
const delayedView = {tubeDesignerSystemPunchTools:tools};
const delayedTool = libraryTools(delayedView)[0];
buildToolLibraryPreviewPayload(delayedView,delayedTool);
delayedView.tubeDesignerSystemProfiles = profiles;
assert.equal(buildToolLibraryPreviewPayload(delayedView,delayedTool).profileRef.id,'rect',
  'the temporary fallback must not override a default when the profile catalogue arrives');

const v = tools.at(-1), values = {...defaults(v),segmentedBend:true,asymmetric:true,bottomStrategy:'rounded',maleFemale:true,rootSlotPattern:true};
for (const key of ['asymmetric','leftAngle','bottomStrategy','roundRadius','maleFemale','maleFemaleSize','rootSlotPattern','centerSlotLength','bendCompensation']) {
  assert.equal(parameterVisible(v.parameters.find(p=>p.key===key),values),false,key);
}
assert.equal(parameterVisible(v.parameters.find(p=>p.key==='angle'),values),true);
for (const shape of ['circle','capsule','roundedRectangle','circleWrap']) {
  const p={...defaults(v),bottomStrategy:'relief',reliefShape:shape};
  assert.equal(parameterVisible(v.parameters.find(f=>f.key==='reliefHeight'),p),!['circle','circleWrap'].includes(shape));
  assert.equal(parameterVisible(v.parameters.find(f=>f.key==='reliefRadius'),p),shape==='roundedRectangle');
}
const circleWrap={...defaults(v),bottomStrategy:'relief',reliefShape:'circleWrap',rootSlotPattern:true,maleFemale:true,asymmetric:true};
for (const key of ['enclosedDiameter','radialClearance','kFactor']) assert.equal(parameterVisible(v.parameters.find(f=>f.key===key),circleWrap),true,key);
for (const key of ['reliefLength','reliefDepth','rootSlotPattern','maleFemale','asymmetric']) assert.equal(parameterVisible(v.parameters.find(f=>f.key===key),circleWrap),false,key);
assert.deepEqual(v.parameters.find(f=>f.key==='reliefShape').options.map(o=>o.value),['roundedRectangle','capsule','circle','circleWrap']);
console.log('Notch library: five groove resources, nested V-groove relief shapes, target editors, live measurements and inactive drafts passed.');
