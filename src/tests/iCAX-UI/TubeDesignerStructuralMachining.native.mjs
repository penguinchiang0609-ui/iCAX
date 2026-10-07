import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createInterface } from "node:readline";
import { mkdirSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve } from "node:path";

const repository=fileURLToPath(new URL("../../../",import.meta.url));
const output=resolve(repository,"output/tests/assembly-process");
mkdirSync(output,{recursive:true});
const reports=[];
const requestTimeout=Number(process.env.ICAX_NATIVE_REQUEST_TIMEOUT_MS||240000);
assert(Number.isFinite(requestTimeout)&&requestTimeout>0,"native request timeout must be positive");
const reportFile=resolve(output,process.env.ICAX_STRUCTURAL_CASE
  ? `structural-pure-native-${process.env.ICAX_STRUCTURAL_CASE.replace(/[^A-Za-z0-9_-]/g,"_")}.json`
  : "structural-pure-native.json");
function connection() {
  const child=spawn(resolve(repository,"tmp/security-window-frames-native/SecurityWindowFramesBridge.exe"),[],{
    cwd:repository,windowsHide:true,env:{...process.env,PATH:`${resolve(repository,"src/x64/Debug")};${process.env.PATH}`},
    stdio:["pipe","pipe","pipe"]});
  let sequence=0,stderr="";
  const pending=new Map();
  function rejectAll(error){for(const request of pending.values()){clearTimeout(request.timer);request.reject(error);}pending.clear();}
  child.stderr.on("data",data=>{stderr+=data;if(process.env.ICAX_NATIVE_TRACE)process.stderr.write(data);});
  child.on("error",rejectAll);child.on("exit",code=>{if(pending.size)rejectAll(new Error(`native exited ${code}: ${stderr}`));});
  createInterface({input:child.stdout}).on("line",line=>{
    let response;try{response=JSON.parse(line);}catch{rejectAll(new Error(line));return;}
    const request=pending.get(response.id);if(!request)return;
    pending.delete(response.id);clearTimeout(request.timer);
    response.ok?request.resolve(response.result):request.reject(new Error(response.error));
  });
  return {close(){child.stdin.end();child.kill();},invoke(method,payload={}){return new Promise((resolveRequest,reject)=>{
    const id=++sequence;pending.set(id,{resolve:resolveRequest,reject,timer:setTimeout(()=>{pending.delete(id);reject(new Error(`timeout ${method}: ${stderr}`));},requestTimeout)});
    child.stdin.write(JSON.stringify({id,method,payload})+"\n");
  });}};
}
const defaults=["minimal-protective-grille","modular-guardrail",
  "modular-guardrail-cross-straight","modular-guardrail-diamond-straight","modular-guardrail-glass-straight","straight-steel-staircase"];
const cases=defaults.map(templateId=>({name:templateId+"-default",templateId,changes:{}}));
cases.push({name:"round-L-actual-envelope-fit",templateId:"modular-guardrail",changes:{layout:"left_l",
  ...Object.fromEntries(["handrail","post","rail","infill"].flatMap(prefix=>[[prefix+"ProfileType","round"],[prefix+"Width",40],[prefix+"Depth",40]]))}});
cases.push({name:"continuous-U-oval-post-witness",templateId:"straight-steel-staircase",changes:{stairRoute:"u_turn",wellGap:100,
  postProfileType:"oval",postWidth:100,postDepth:20,floorHeight:1080,totalRiserCount:6,firstFlightRiserCount:3,handrailConnection:"continuous"}});
cases.push({name:"grille-miter-square-head-side-apertures",templateId:"minimal-protective-grille",changes:{frameType:"closed_frame",
  frameCornerJoint:"miter_45",maleCornerType:"square",installHoleOrientation:"side",installHoleAutoAvoid:true,
  installHoleMaximumShift:30,handleEnabled:false}});

for (const scenario of cases) {
  if(process.env.ICAX_STRUCTURAL_CASE && scenario.name!==process.env.ICAX_STRUCTURAL_CASE)continue;
  const native=connection();
  try {
    const descriptor=(await native.invoke("GetTemplateDescriptor",{templateId:scenario.templateId})).template;
    const parameters={...Object.fromEntries(descriptor.parameters.map(field=>[field.key,field.defaultValue])),...scenario.changes};
    for(const key of Object.keys(parameters))assert(descriptor.parameters.some(field=>field.key===key),`undeclared ${key}`);
    const preview=await native.invoke("GenerateProductTemplatePreview",{templateId:scenario.templateId,parameters});
    assert.deepEqual(preview.parameters,parameters,"normalized descriptor inputs must echo without internal fields");
    assert(preview.items.length>0);
    const generated=await native.invoke("GeneratePreview",{templateId:scenario.templateId,...parameters});
    assert.deepEqual(generated.tubeDesigner.product.parameters,parameters);
    const product=generated.tubeDesigner.product;
    const disassembled=await native.invoke("Disassemble",{productEntityIds:[product.entityId]});
    const checks=disassembled.geometryChecks;
    assert(checks.length>0);
    const inspected=await native.invoke("InspectNativeGeometry",{});
    assert.equal(inspected.geometryChecks.length,checks.length);
    const invalid=checks.filter(check=>!check.valid||check.solids!==1||check.volume<=0);
    if(invalid.length){
      writeFileSync(resolve(output,`structural-pure-native-${scenario.name}-failure.json`),
        JSON.stringify({scenario,parameters,checks:invalid,inspected},null,2));
      console.log("INVALID "+JSON.stringify(invalid.map(check=>({key:check.key,valid:check.valid,solids:check.solids,
        volume:check.volume,errors:check.brepErrors?.length,
        statuses:[...new Set((check.brepErrors||[]).flatMap(error=>error.statusNames))]}))));
    }
    for(const check of checks){assert(check.valid,check.key+" invalid BRep");assert.equal(check.solids,1,check.key+" must remain one solid");assert(check.volume>0);}
    const instances=inspected.processPlans.flatMap(plan=>plan.instances);
    for(const instance of instances){assert.equal(instance.processInput.schema,"icax.assembly-process-input");assert(instance.processInput.parts.stock);
      assert.equal(instance.result.schema,"icax.assembly-process-result");assert(instance.result.applicable);assert(instance.result.operations.length>0);}
    if(scenario.name==="round-L-actual-envelope-fit")assert(instances.some(instance=>instance.templateId==="structural-stock-fit"));
    if(scenario.templateId==="modular-guardrail-cross-straight"){
      // Regression: world-space diagonal envelope construction previously left
      // an invalid sliver face on this half of the last bay's crossing bar.
      const regression=checks.find(check=>check.key==="segment.1.bay.3.pattern.cross.3");
      assert(regression?.valid);assert.equal(regression.solids,1);
      const fit=instances.find(instance=>instance.instanceId==="segment.1.bay.3.pattern.cross.3.process.fit");
      assert(fit);
      const section=fit.result.geometry.find(node=>node.key==="receiver.0.profile");
      assert.deepEqual(section.arguments.placement.origin,[0,0,0]);
      assert.equal(fit.result.geometry.find(node=>node.key==="receiver.0.solid").operator,"transform");
    }
    if(scenario.name==="continuous-U-oval-post-witness"){
      assert(instances.some(instance=>instance.instanceId.endsWith("transition_cope") && instance.result.operations.some(operation=>operation.arguments?.keepConnectedTo)));
      assert(instances.some(instance=>JSON.stringify(instance.processInput.geometry).includes("network_envelope")));
    }
    const report={name:scenario.name,templateId:scenario.templateId,parts:checks.length,processInstances:instances.length,
      functions:[...new Set(instances.map(instance=>instance.templateId))],validSingleSolids:checks.length,parameterEcho:true};
    reports.push(report);console.log("PASS "+JSON.stringify(report));
    writeFileSync(reportFile,JSON.stringify(reports,null,2));
  } finally {native.close();}
}
console.log(`STRUCTURAL NATIVE PASS ${reports.length}/${process.env.ICAX_STRUCTURAL_CASE?1:cases.length}`);
