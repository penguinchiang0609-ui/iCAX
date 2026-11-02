// Scene sequence numbers fence commands. They are not path dependencies.
export function rapidDependencyKey(view,id=view.fiveRapid?.id){
 const domain=view.scene?.domain,setup=domain?.setups?.find(s=>s.id===domain.activeSetupId);
 if(view.fiveRapid?.settings&&!id)return JSON.stringify([domain?.expected?.epoch,setup?.id,setup?.rapidSettings]);
 const t=setup?.generatedGeometry?.transitions?.find(t=>`${t.from}/${t.to}`===id);
 return JSON.stringify([domain?.expected?.epoch,setup?.id,id,t?.inputKey||[t?.points,t?.directions,t?.policy,t?.manualPath],t?.endpointStatus]);
}
