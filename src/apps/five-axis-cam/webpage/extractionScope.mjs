export const EXTRACTION_BOUNDARIES=Object.freeze([['inner','内孔'],['outer','外轮廓']]);
export const validExtractionScope=value=>['inner','outer','all','none'].includes(value);

// Use native semantic roles and topological loop nesting. Curve type alone
// does not establish a hole; explicit marking candidates stay out of cuts.
export function scopedExtractionCandidates(candidates,scope){
  return candidates.filter(c=>{
    if(c.boundaryRole==='Marking'||c.machiningKind==='Mark')return false;
    const inner=c.boundaryRole==='Opening'||c.boundary?.regionRelation==='InnerLoop',outer=c.boundaryRole==='OuterTrim'||c.boundary?.regionRelation==='OuterLoop';
    return scope==='inner'?inner:scope==='outer'?outer:scope==='all'&&(inner||outer);
  });
}
