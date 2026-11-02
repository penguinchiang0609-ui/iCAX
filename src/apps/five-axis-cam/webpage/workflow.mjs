import {normalizeArea} from './workspaceModel.mjs';

export const hasImportedWorkpiece=view=>!!view.scene?.domain?.workpieces?.some(w=>w.enabled!==false&&w.geometryRef?.url);
export function canEnterArea(view,id){
  // Keep navigation available; prerequisites belong to individual commands.
  return true;
}
export function camPreparationReason(view){
  const d=view.scene?.domain,p=d?.placements?.find(p=>p.id===view.scene.activePlacementId)||d?.placements?.find(p=>p.setupId===d.activeSetupId),w=d?.workpieces?.find(w=>w.id===p?.workpieceId),s=d?.setups?.find(s=>s.id===p?.setupId);
  if(!w?.geometryRef?.url)return '请先导入工件。';
  if(w.enabled===false)return '当前工件已禁用，请在工件页启用。';
  if(!s)return '请先在工件页新增装夹。';
  if(s.enabled===false)return '当前装夹姿态已禁用，请在工件页启用。';
  return '';
}
export function workflowArea(view,id){
  const area=normalizeArea(id);
  return canEnterArea(view,area)?area:'workpiece';
}
