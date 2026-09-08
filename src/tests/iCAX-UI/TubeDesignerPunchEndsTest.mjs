import assert from "node:assert/strict";
import {readFileSync,readdirSync} from "node:fs";
import {createPunchWizardState,installPunchCatalogue,getPunchWizardPayload,renderPunchWizardDialog,
  openPunchParameters,closePunchParameters,updatePunchWizardField,validatePunchWizard} from "../../apps/tube-designer/webpage/punchWizard.mjs";
import {changePunchRecordKind,selectPunchProfileSource,updatePunchProfileParameter,punchProfileChoices} from "../../apps/tube-designer/webpage/punchProfileSource.mjs";
import {handleNestingPunchPartAction} from "../../apps/tube-designer/webpage/nestingPunchPart.mjs";
import {buildPunchReviewSummary} from "../../apps/tube-designer/webpage/punchReview.mjs";

const root=new URL("../../apps/tube-designer/templates/_shared/punch-tools/",import.meta.url);
const tools=readdirSync(root,{withFileTypes:true}).filter(entry=>entry.isDirectory()).map(entry=>JSON.parse(readFileSync(new URL(entry.name+"/tool.json",root))))
  .map(tool=>({...tool,digest:"fixture",defaultParameters:Object.fromEntries(tool.parameters.map(p=>[p.key,p.defaultValue]))}));
const profile={id:"rect",name:"矩形支管",profileType:"parametric-package",defaultParameters:{width:20},
  descriptor:{parameters:[{key:"width",displayName:"宽度",valueType:"number",defaultValue:20,unit:"mm"}]},
  previewProfile:{kind:"rect",width:20,depth:10,parameters:{width:20},contours:[{kind:"polygon",points:[[-10,-5],[10,-5],[10,5],[-10,5]]}]}};
const part={entityId:"end-test",length:1000,profile:{kind:"rect",width:40,depth:20}};
const view={pending:false,tubeDesignerSystemProfiles:[profile],tubeDesignerPunchWizard:createPunchWizardState(part)};
const s=view.tubeDesignerPunchWizard;
installPunchCatalogue(s,{tools});
const start={tubeDesignerPunchIndex:"start",tubeDesignerPunchEnd:"start"};
const end={tubeDesignerPunchIndex:"end",tubeDesignerPunchEnd:"end"};
const originalDraft=structuredClone(s.draft),originalLibrary=structuredClone(profile);
const change=(dataset,field,value,parameter)=>updatePunchWizardField(view,{value,dataset:{...dataset,tubeDesignerPunchField:field,...(parameter?{tubeDesignerPunchParameter:parameter}:{})}});

openPunchParameters(view,"start","start");
assert.equal(changePunchRecordKind(view,{value:"branch",dataset:start}),true);
assert.equal(s.ends.start.toolRef.id,"end-profile");assert.equal(s.ends.start.datum,"long");
assert.equal(s.ends.start.section.key,"system:rect");
assert.deepEqual(s.draft,originalDraft);assert.equal(s.ends.end.type,"keep");
let html=renderPunchWizardDialog(part,view,{tableMode:true,showEnds:true,branchProfiles:punchProfileChoices(view)});
assert.match(html,/左端面参数/);assert.match(html,/data-tube-designer-punch-end="start"/);
assert.match(html,/截面拉伸为端部刀具/);assert.match(html,/支管参数示意图/);
assert.doesNotMatch(html,/data-tube-designer-punch-field="allowOpen"|允许端部开口|端部开口<\/th>/);
const endTable=html.slice(html.indexOf('<section class="tube-designer-punch-ends"'),html.indexOf('</section>',html.indexOf('<section class="tube-designer-punch-ends"')));
assert.match(endTable,/端面加工/);assert.match(endTable,/两端独立设置/);
assert.doesNotMatch(endTable,/data-tube-designer-punch-field="(?:arrayCount|arrayPitch|station|datum|trim|rotation)"/);
const holeTable=html.slice(html.indexOf('<section class="tube-designer-punch-sheet"'),html.indexOf('</section>',html.indexOf('<section class="tube-designer-punch-sheet"')));
assert.doesNotMatch(holeTable,/data-tube-designer-punch-end-row/);
assert.match(html,/class="tube-designer-punch-end-placement"/);
for(const key of ['datum','trim','rotation'])assert.match(html,new RegExp('data-tube-designer-punch-field="'+key+'"[^>]*data-tube-designer-punch-end="start"'));
const profileContext={sceneProxy:{async invoke(method,request){
  assert.equal(method,"TubeDesigner.GenerateProfilePreview");
  return {profile:{...structuredClone(profile.previewProfile),width:request.parameters.width}};
}}};
await updatePunchProfileParameter(profileContext,view,{value:"30",dataset:{...start,tubeDesignerPunchProfileParameter:"width"}});
assert.equal(s.ends.start.section.profile.width,30);assert.equal(s.ends.start.section.parameters.width,30);
assert.deepEqual(profile,originalLibrary,"End section edits cannot mutate the profile library");
assert.equal(closePunchParameters(view,false,part),true);assert.equal(s.ends.start.type,"keep");assert.deepEqual(s.draft,originalDraft);

openPunchParameters(view,"end","end");
change(end,"tool","end-key-joint");change(end,"parameter","female","gender");
change(end,"parameter","0.2","sideClearance");change(end,"parameter","25","depth");
assert.equal(closePunchParameters(view,true,part),true);
assert.equal(getPunchWizardPayload(view).ends.end.toolParameters.sideClearance,0.2);
assert.equal(s.ends.end.datum,"long");assert.equal(s.ends.start.type,"keep");
openPunchParameters(view,"end","end");
change(end,"tool","end-step-z");change(end,"parameter","negative","hand");
assert.equal(closePunchParameters(view,true,part),true);
assert.equal(s.ends.end.toolParameters.hand,"negative");
assert.equal(validatePunchWizard(view,part),"");
openPunchParameters(view,"end","end");change(end,"trim","-1");
assert.equal(closePunchParameters(view,true,part),false);assert.match(s.parameterEditor.error,/端部定位/);
closePunchParameters(view,false,part);assert.equal(s.ends.end.trim,0);

const dxfContext={appProxy:{bridge:{async openFileDialog(){return "C:\\fixtures\\end-cut.dxf";}}},
  productProxy:{async invoke(method){assert.equal(method,"TubeDesigner.ImportProfileDxf");return {profile:{name:"端部DXF",contours:[{kind:"circle",radius:10}]}};}}};
openPunchParameters(view,"start","start");
assert.equal(await selectPunchProfileSource(dxfContext,view,{value:"__dxf__",dataset:start}),true);
assert.equal(s.ends.start.toolRef.id,"end-profile");assert.equal(s.ends.start.recordKind,"dxf");
assert.equal(closePunchParameters(view,true,part),true);
const saved=getPunchWizardPayload(view);
assert.equal(saved.ends.start.section.source,"dxf");assert.equal(saved.ends.end.toolRef.id,"end-step-z");
const reopened=createPunchWizardState({...part,properties:{"tubeDesigner.punchWizard":{...saved,baseLength:1000}}});
assert.deepEqual(getPunchWizardPayload({tubeDesignerPunchWizard:reopened}).ends,saved.ends);

// Both profile sources and both ends use the same explicit concave/convex
// contract, without exposing the removed inner/material region option.
for(const source of ["branch","dxf"])for(const key of ["start","end"]){
  const modeView={pending:false,tubeDesignerSystemProfiles:[profile],tubeDesignerPunchWizard:createPunchWizardState(part)};
  const modeState=modeView.tubeDesignerPunchWizard,target={tubeDesignerPunchIndex:key,tubeDesignerPunchEnd:key};
  installPunchCatalogue(modeState,{tools});
  openPunchParameters(modeView,key,key);
  if(source==="branch")assert.equal(changePunchRecordKind(modeView,{value:"branch",dataset:target}),true);
  else assert.equal(await selectPunchProfileSource(dxfContext,modeView,{value:"__dxf__",dataset:target}),true);
  assert.equal(modeState.ends[key].toolParameters.cutMode,"concave");
  const modeHtml=()=>renderPunchWizardDialog(part,modeView,{tableMode:true,showEnds:true,branchProfiles:punchProfileChoices(modeView)});
  assert.match(modeHtml(),/data-tube-designer-punch-parameter="cutMode"/);
  assert.match(modeHtml(),/<option value="concave" selected>凹口<\/option>/);
  assert.doesNotMatch(modeHtml(),/data-tube-designer-punch-parameter="cutRegion"|保留内孔/);
  assert.equal(updatePunchWizardField(modeView,{value:"convex",dataset:{...target,tubeDesignerPunchField:"parameter",tubeDesignerPunchParameter:"cutMode"}}),true);
  assert.match(modeHtml(),/0° 或 180°.*不能形成凸口/);
  assert.equal(closePunchParameters(modeView,true,part),true);
  const modePayload=getPunchWizardPayload(modeView);
  assert.equal(modePayload.ends[key].toolParameters.cutMode,"convex");
  assert.match(modeHtml(),/tube-designer-punch-end-summary">[^<]*凸口/);
  assert.match(buildPunchReviewSummary(modeState,part).ends.find(item=>item.key===key).label,/凸口/);
  const modeReopened=createPunchWizardState({...part,properties:{"tubeDesigner.punchWizard":{...modePayload,baseLength:1000}}});
  assert.equal(getPunchWizardPayload({tubeDesignerPunchWizard:modeReopened}).ends[key].toolParameters.cutMode,"convex");
  const nativeRecipe=structuredClone(modePayload);
  for(const item of Object.values(nativeRecipe.ends))delete item.recordKind;
  const nativeView={pending:false,tubeDesignerSystemProfiles:[profile],tubeDesignerPunchWizard:createPunchWizardState({...part,properties:{"tubeDesigner.punchWizard":{...nativeRecipe,baseLength:1000}}})};
  const nativeState=nativeView.tubeDesignerPunchWizard,nativeSource=source==="branch"?"library":"dxf";
  installPunchCatalogue(nativeState,{tools});
  assert.notEqual(nativeState,modeState,"The regression must reopen a native-style recipe into a fresh wizard state");
  const nativeHtml=()=>renderPunchWizardDialog(part,nativeView,{tableMode:true,showEnds:true,branchProfiles:punchProfileChoices(nativeView)});
  openPunchParameters(nativeView,key,key);
  const nativePopup=()=>nativeHtml().slice(nativeHtml().indexOf('<div class="tube-designer-punch-parameter-backdrop">'));
  assert.match(nativePopup(),/选择截面/);assert.doesNotMatch(nativePopup(),/选择刀具|保留原端面/);
  assert.match(nativePopup(),new RegExp('<option value="'+(source==="branch"?'system:rect':'__dxf__')+'" selected>'));
  assert.match(nativePopup(),/data-tube-designer-punch-parameter="cutMode"/);
  if(source==="branch"){
    assert.match(nativePopup(),/data-tube-designer-punch-profile-parameter="width"/);
    await updatePunchProfileParameter(profileContext,nativeView,{value:"34",dataset:{...target,tubeDesignerPunchProfileParameter:"width"}});
    assert.equal(nativeState.ends[key].section.parameters.width,34);
  }
  updatePunchWizardField(nativeView,{value:"concave",dataset:{...target,tubeDesignerPunchField:"parameter",tubeDesignerPunchParameter:"cutMode"}});
  assert.equal(closePunchParameters(nativeView,true,part),true);
  assert.equal(nativeState.ends[key].section.source,nativeSource);
  assert.equal(nativeState.ends[key].section.key,nativeRecipe.ends[key].section.key);
  const nativeCommitted=getPunchWizardPayload(nativeView).ends[key];
  openPunchParameters(nativeView,key,key);
  updatePunchWizardField(nativeView,{value:"convex",dataset:{...target,tubeDesignerPunchField:"parameter",tubeDesignerPunchParameter:"cutMode"}});
  if(source==="branch")await updatePunchProfileParameter(profileContext,nativeView,{value:"38",dataset:{...target,tubeDesignerPunchProfileParameter:"width"}});
  assert.equal(closePunchParameters(nativeView,false,part),true);
  assert.deepEqual(getPunchWizardPayload(nativeView).ends[key],nativeCommitted,"Cancel on a native-reloaded end restores parameters without changing its inferred source");
  openPunchParameters(modeView,key,key);
  updatePunchWizardField(modeView,{value:"concave",dataset:{...target,tubeDesignerPunchField:"parameter",tubeDesignerPunchParameter:"cutMode"}});
  assert.equal(closePunchParameters(modeView,false,part),true);
  assert.equal(modeState.ends[key].toolParameters.cutMode,"convex","Cancelling restores the selected end's saved convex mode");
  assert.equal(modeState.ends[key==="start"?"end":"start"].type,"keep","Changing cut mode never rewrites the other end");
  delete modeState.ends[key].toolParameters.cutMode;
  openPunchParameters(modeView,key,key);
  assert.match(modeHtml(),/<option value="concave" selected>凹口<\/option>/,"A saved legacy record with no mode still opens as concave");
  assert.match(buildPunchReviewSummary(modeState,part).ends.find(item=>item.key===key).label,/凹口/);
  closePunchParameters(modeView,false,part);
  assert.equal(modeState.ends[key].toolParameters.cutMode,undefined,"Reading/cancelling a legacy record does not silently rewrite its recipe");
}

// Verify the production creation handler routes end sources and can create an
// end-cut-only part, with no side holes and no accidental draft in the payload.
const creation={pending:false,tubeDesignerSystemProfiles:[profile]},requests=[];
let dxfPath="";
const context={appProxy:{bridge:{async openFileDialog(){return dxfPath;}}},productProxy:dxfContext.productProxy,
  sceneProxy:{async invoke(method,request){requests.push({method,request});
    if(method==="TubeDesigner.AddNestingPunchPart")return {tubeDesigner:{nestingGroups:[]},partEntityId:"created-end-only"};
    return {toolsOnly:true,baseGeometry:{url:"memory://end-base",version:1},toolGeometry:{url:"memory://end-tools",version:1},length:1000,baseBounds:{width:1000}};
  }},actions:{async refreshActiveSceneState(){}}};
const ops={renderProject(){},showNotice(){}};
const act=(suffix,target={})=>handleNestingPunchPartAction(context,creation,"tube-designer-nesting-punch-create-"+suffix,target,ops);
await act("open");installPunchCatalogue(creation.tubeDesignerPunchWizard,{tools});
await act("record-kind-change",{value:"dxf",dataset:start});
assert.equal(creation.tubeDesignerPunchWizard.parameterEditor,null,"Cancelling the native file picker restores the original end");
assert.equal(creation.tubeDesignerPunchWizard.ends.start.type,"keep");
dxfPath="C:\\fixtures\\end-cut.dxf";
await act("record-kind-change",{value:"dxf",dataset:start});
assert.equal(creation.tubeDesignerPunchWizard.parameterEditor.end,"start");
assert.equal(requests.at(-1).request.ends.start.section.source,"dxf");
assert.deepEqual(requests.at(-1).request.features,[]);
const countBeforeModeChange=requests.length;
await act("preview-mode",{dataset:{tubeDesignerPunchPreviewMode:"result"}});
assert.equal(creation.tubeDesignerPunchWizard.previewMode,"tools","Intermediate editing never switches to a boolean cut result");
assert.equal(requests.length,countBeforeModeChange,"A retired result-mode action cannot schedule geometry");
assert.equal(creation.tubeDesignerPunchWizard.parameterEditor.end,"start","The tool-only display keeps the parameter transaction open");
await act("parameters-apply");
await act("field-change",{value:"end-step-z",dataset:{...end,tubeDesignerPunchField:"tool"}});
assert.equal(creation.tubeDesignerPunchWizard.parameterEditor.end,"end");
await act("parameters-apply");
const beforeBlankSwitch=structuredClone(creation.tubeDesignerPunchWizard.ends);
const replacement={...structuredClone(profile),id:"another-rect",name:"另一主管"};
creation.tubeDesignerSystemProfiles.push(replacement);
await act("draft-change",{value:"system:another-rect",dataset:{tubeDesignerNestingPunchField:"profileKey"}});
assert.deepEqual(creation.tubeDesignerPunchWizard.ends,beforeBlankSwitch,"Changing the main section must retain both end operations for revalidation");
assert.equal(requests.at(-1).request.profileRef.id,"another-rect");
await act("apply");
const request=requests.find(row=>row.method==="TubeDesigner.AddNestingPunchPart").request;
assert.deepEqual(request.features,[]);assert.equal(request.ends.start.toolRef.id,"end-profile");assert.equal(request.ends.end.toolRef.id,"end-step-z");
assert.equal(creation.tubeDesignerPunchWizard,null);
console.log("End cutter UI: independent ends, profile/DXF sources, shape parameters, transactions, saved recipe and end-only creation passed.");
