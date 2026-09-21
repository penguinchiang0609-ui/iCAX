// Display only: applicability belongs to the mould's backend analyze function.
export function moldApplicability(descriptor, parameters, analysis) {
  if(!descriptor?.sectionAnalysis)return [];
  if(!analysis)return [{id:"section-pending",status:"unknown",message:"需更新预览，由单件工艺分析截面"}];
  if(analysis.applicable===true)return [];
  return [{id:"section-rejected",status:"incompatible",message:analysis.reason??"截面不满足单件工艺要求"}];
}
export function previewSectionAnalysis(state,item,end="") {
  if(end||state.preview?.revision!==state.revision)return undefined;
  let index=state.features?.indexOf(item)??-1;
  if(item===state.draft) {
    if(!state.preview.includesDraft)return undefined;
    const editingIndex=state.editingId?(state.features?.findIndex(feature=>feature.id===state.editingId)??-1):-1;
    index=editingIndex>=0?editingIndex:(state.features?.length??0);
  }
  if(index<0)return undefined;
  return state.preview.sectionAnalyses?.find(result=>result.index===index);
}
