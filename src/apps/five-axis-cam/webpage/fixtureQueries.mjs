import {loadRenderResource} from '../../../iCAX-UI/SDK/Viewport/renderResource.mjs';
import {readJsonDocument} from '../../../iCAX-UI/SDK/Resources/jsonDocument.mjs';
import {activeTool} from './workspaceModel.mjs';

const request=(context,view,scope={})=>({setupId:view.scene.domain.activeSetupId,epoch:view.scene.domain.expected.epoch,...scope});
const current=(context,view,proxy,setupId)=>!view.disposed&&context.sceneProxy===proxy&&context.isCurrentProject?.()!==false&&view.scene?.domain?.activeSetupId===setupId;
export function fixtureDetailsKey(view,unit){return JSON.stringify([view.scene?.domain?.expected?.epoch,unit?.id,unit?.inputRevision,unit?.design?.assembly?.id,unit?.design?.breakdown?.id]);}
export function fixtureUnitDesign(view,unit){const cached=view.fiveFixtureDetails?.get(unit?.id);return cached?.key===fixtureDetailsKey(view,unit)&&cached.design?cached.design:unit?.design||{};}

// A selected unit's parameters, part rows and geometry are separate reads.
export async function loadFixtureUnitDetails(context,view,unit,{supportFaces=false}={}){
  const key=fixtureDetailsKey(view,unit),proxy=context.sceneProxy,setupId=unit.setupId;
  view.fiveFixtureDetails??=new Map();let entry=view.fiveFixtureDetails.get(unit.id);
  if(!entry||entry.key!==key){entry={key};view.fiveFixtureDetails.set(unit.id,entry);}
  if(!entry.pending&&!entry.design)entry.pending=(async()=>{
    const scope=request(context,view,{unitId:unit.id}),design=await proxy.invoke('CamFixture.ReadDesign',scope);
    if(design.assembly){const rows=await proxy.invoke('CamFixture.QueryParts',{...scope,snapshot:design.snapshot,list:'assembly'});design.assembly={...design.assembly,parts:rows.items};}
    const latest=view.scene?.domain?.fixtureUnits?.find(u=>u.id===unit.id);
    if(current(context,view,proxy,setupId)&&fixtureDetailsKey(view,latest)===key)entry.design=design;
    return design;
  })().finally(()=>{entry.pending=null;});
  const design=entry.design||await entry.pending;
  if(supportFaces&&design.recipe&&!design.recipe.supportFaces){
    const result=await proxy.invoke('CamFixture.QuerySupportFaces',request(context,view,{unitId:unit.id,snapshot:design.snapshot}));
    if(current(context,view,proxy,setupId))design.recipe={...design.recipe,supportFaces:result.supportFaces};
  }
  return design;
}
export function ensureFixtureUnitDetails(context,view,ops){
  if(view.fiveAxisArea!=='operation'||activeTool(view)!=='fixture'||view.fiveFixtureDesignOpen||view.pending)return;
  const unit=view.scene?.domain?.fixtureUnits?.find(u=>u.id===view.fiveAxisFixtures?.selected);
  if(unit?.kind!=='Generated')return;
  const key=fixtureDetailsKey(view,unit),cached=view.fiveFixtureDetails?.get(unit.id);
  if(cached?.key===key&&(cached.pending||cached.design||cached.error))return;
  const proxy=context.sceneProxy,setupId=unit.setupId;
  void loadFixtureUnitDetails(context,view,unit).then(()=>{if(current(context,view,proxy,setupId)&&view.fiveAxisFixtures?.selected===unit.id)ops.renderProject(context,view);}).catch(error=>{const entry=view.fiveFixtureDetails?.get(unit.id);if(entry?.key===key)entry.error=error.message;if(current(context,view,proxy,setupId)){view.error=error.message;ops.renderProject(context,view);}});
}
export async function loadFixtureCandidate(context,view,task){
  const scope=request(context,view,{taskId:task.taskId}),design=await context.sceneProxy.invoke('CamFixture.ReadDesign',scope);
  const response=await context.sceneProxy.invoke('CamFixture.QueryParts',{...scope,list:'candidate',snapshot:design.snapshot}),parts=response.items;
  if(design.candidate.id!==task.result.candidateId)throw Error('夹具查询结果与生成回执不一致。');
  const renderParts=await Promise.all(parts.map(async part=>({part,data:(await loadRenderResource(context.sceneProxy.resources,part.previewRef)).data})));
  return {task,design:{...design,candidate:{...design.candidate,parts}},renderParts};
}
export async function readFixtureResource(context,ref){
  return readJsonDocument(context.sceneProxy.resources,ref);
}
export async function loadFixtureBreakdown(context,view,unitId){
  const scope=request(context,view,{unitId}),rows=await context.sceneProxy.invoke('CamFixture.QueryParts',{...scope,list:'breakdown'}),parts=[];
  for(const row of rows.items){const ref=await context.sceneProxy.invoke('CamFixture.PartDetails',{...scope,snapshot:rows.snapshot,list:'breakdown',partId:row.partId});const part=await readFixtureResource(context,ref.detailsResourceRef);if(part.partId!==row.partId)throw Error('夹具板件详情不一致。');parts.push(part);}
  return {unitId,parts};
}
