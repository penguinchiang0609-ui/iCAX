const setupOf=view=>view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId);

// A catalog selection is local preview state, never a replacement scene snapshot.
export function headWorkspaceSelection(view){
 const setupId=setupOf(view)?.id;
 let s=view.fiveHeadSelection;
 if(!s||s.setupId!==setupId)s=view.fiveHeadSelection={setupId,serial:(s?.serial||0)+1};
 return s;
}
export const hasHeadWorkspacePreview=view=>view.fiveCamPage==='head'&&headWorkspaceSelection(view).choice!==undefined;
export function headWorkspaceHead(view){
 const head=hasHeadWorkspacePreview(view)?headWorkspaceSelection(view).head:setupOf(view)?.headModel;
 // The native fallback schematic is not an assigned catalog cutting head.
 if(view.fiveCamPage==='head'&&head?.kind==='Builtin'&&!head.instance&&!head.builtinDefinitionId&&!head.assembly&&!head.previewRef)return null;
 return head;
}
export function clearHeadWorkspaceSelection(view){
 const s=headWorkspaceSelection(view);view.fiveHeadSelection={setupId:s.setupId,serial:s.serial+1};
}
export const headPreviewAxis=view=>hasHeadWorkspacePreview(view)?headWorkspaceSelection(view).axis||0:view.fiveHeadEditor?.axis||0;
