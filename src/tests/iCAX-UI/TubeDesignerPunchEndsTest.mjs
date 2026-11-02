import assert from "node:assert/strict";
import {readFileSync,readdirSync,existsSync} from "node:fs";
import {createPunchWizardState,installPunchCatalogue,getPunchWizardPayload,renderPunchWizardDialog,
  openPunchParameters,closePunchParameters,updatePunchWizardField,validatePunchWizard} from "../../apps/tube-designer/webpage/punchWizard.mjs";
import {changePunchRecordKind,selectPunchProfileSource,updatePunchProfileParameter,punchProfileChoices} from "../../apps/tube-designer/webpage/punchProfileSource.mjs";
import {handleNestingPunchPartAction} from "../../apps/tube-designer/webpage/nestingPunchPart.mjs";
import {buildPunchReviewSummary} from "../../apps/tube-designer/webpage/punchReview.mjs";

const root=new URL("../../apps/tube-designer/templates/mold/",import.meta.url);
const tools=readdirSync(root,{withFileTypes:true}).filter(entry=>entry.isDirectory()&&existsSync(new URL(entry.name+"/tool.json",root))).map(entry=>JSON.parse(readFileSync(new URL(entry.name+"/tool.json",root))))
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
assert.equal(closePunchParameters(view,true,part),false);assert.match(s.parameterEditor.error,/端部修剪量/);
closePunchParameters(view,false,part);assert.equal(s.ends.end.trim,0);

const dxfContext={appProxy:{bridge:{async openFileDialog(){return "C:\\fixtures\\end-cut.dxf";}}},
  productProxy:{async invoke(method){assert.equal(method,"TubeDesigner.ImportProfileDxf");return {profile:{name:"端部DXF",contours:[{kind:"circle",radius:10}]}};}}};
openPunchParameters(view,"start","start");
assert.equal(await selectPunchProfileSource(dxfContext,view,{value:"__dxf__",dataset:start}),true);
assert.equal(s.ends.start.toolRef.id,"end-profile");assert.equal(s.ends.start.recordKind,"dxf");
assert.equal(closePunchParameters(view,true,part),true);
const saved=getPunchWizardPayload(view);
assert.equal(saved.ends.start.section.source,"dxf");assert.equal(saved.ends.end.toolRef.id,"end-step-z");
const reopened=createPunchWizardState(part,{...saved,baseLength:1000});
assert.deepEqual(getPunchWizardPayload({tubeDesignerPunchWizard:reopened}).ends,saved.ends);

// Creation drafts preserve exact current end-source fields through direct draft copies.
for(const source of ["branch","dxf"])for(const key of ["start","end"]){
  const current={pending:false,tubeDesignerSystemProfiles:[profile],tubeDesignerPunchWizard:createPunchWizardState(part)};
  installPunchCatalogue(current.tubeDesignerPunchWizard,{tools});
  const target={tubeDesignerPunchIndex:key,tubeDesignerPunchEnd:key};
  openPunchParameters(current,key,key);
  if(source==="branch")assert.equal(changePunchRecordKind(current,{value:"branch",dataset:target}),true);
  else assert.equal(await selectPunchProfileSource(dxfContext,current,{value:"__dxf__",dataset:target}),true);
  assert.equal(current.tubeDesignerPunchWizard.ends[key].toolParameters.cutMode,"concave");
  assert.equal(updatePunchWizardField(current,{value:"convex",dataset:{...target,tubeDesignerPunchField:"parameter",tubeDesignerPunchParameter:"cutMode"}}),true);
  assert.equal(closePunchParameters(current,true,part),true);
  const payload=getPunchWizardPayload(current);
  assert.equal(payload.ends[key].toolParameters.cutMode,"convex");
  assert.equal(payload.ends[key==="start"?"end":"start"].type,"keep");
  const copy=createPunchWizardState(part,{...payload,baseLength:1000});
  assert.deepEqual(getPunchWizardPayload({tubeDesignerPunchWizard:copy}).ends,payload.ends);
  assert.equal(payload.partEntityId,undefined);
}
console.log("Creation end draft helpers: independent ends, profile/DXF sources, parameters, transactions and exact draft copies passed.");
