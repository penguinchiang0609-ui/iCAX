const types=new Set(['HeadHead','HeadTable','TableTable']);
const setupOf=view=>view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId);
export function operationMachineType(view){const type=setupOf(view)?.machineType?.machineType;return types.has(type)?type:null;}
// Compatibility is authoritative native metadata from the effective tool chain.
// Names, display kinds and file extensions never establish compatibility.
export function headFitsOperation(view,head){const type=operationMachineType(view);return !!type&&Array.isArray(head?.machineTypes)&&head.machineTypes.includes(type);}
export function headChoiceFitsOperation(view,choice){
 if(choice===''||choice==='import')return !!operationMachineType(view);
 const head=choice.startsWith('builtin:')?view.fiveSimulation?.builtinHeads?.find(h=>h.id===choice.slice(8))
  :choice.startsWith('profile:')?view.fiveHeadEditor?.entries?.find(h=>h.id===choice.slice(8))
  :view.scene?.domain?.headInstances?.find(h=>h.id===choice);
 return headFitsOperation(view,head);
}
