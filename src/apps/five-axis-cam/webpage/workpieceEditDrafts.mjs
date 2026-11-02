import {placementMatrix,disposePlacementPreview,openPlacement,placementTools} from './workpiecePlacement.mjs';
import {disposeAlignment} from './workpieceAlignment.mjs';
import {disposeLocating} from './setupLocating.mjs';
import {activeTool,currentDraft,workspaceState} from './workspaceModel.mjs';

export function hasUnappliedWorkpieceDrafts(view){
 const d=view.fivePlacement;
 if(d?.open&&(d.panel||d.axis)){
  try{if(placementMatrix(d).clone().transpose().toArray().some((n,i)=>Math.abs(n-d.original[i])>1e-9))return true;}
  catch{return true;}
 }
 const setup=view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId);
 return !!([...view.fiveWorkpieceNames?.values()||[]].some(d=>d.dirty||d.request)||view.fiveSetupNameEdit&&currentDraft(view,'setup').setupName!==setup?.name
  ||view.fiveLocating?.dirty||view.fixtureDesignDraft?.dirty
  ||Object.values(view.fiveAxisFixtures?.drafts||{}).some(d=>d.dirty)
  ||Object.values(workspaceState(view).drafts).some(d=>d.registrationDirty)
  ||view.fiveAlignment?.open&&(view.fiveAlignment.dirty||Object.keys(view.fiveAlignment.fieldErrors).length));
}
export function clearWorkpieceEditorDrafts(view){
 disposePlacementPreview(view);view.fivePlacement=null;
 disposeAlignment(view);disposeLocating(view);view.fiveLocating=null;
 view.fiveSetupNameEdit=null;
 view.fiveWorkpieceNames?.clear();
 const setup=view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId);
 if(setup){const d=currentDraft(view,'setup');d.setupId=setup.id;d.setupName=setup.name;}
 for(const [key,d] of Object.entries(workspaceState(view).drafts))if(d.registrationDirty||d.registrationPreview)delete workspaceState(view).drafts[key];
 if(view.fiveAxisFixtures)view.fiveAxisFixtures.drafts={};
 view.fixtureDesignDraft=null;view.fixtureDesignPicking=false;view.error='';
 const tool=activeTool(view);if(placementTools.includes(tool))openPlacement(view,tool);
}
