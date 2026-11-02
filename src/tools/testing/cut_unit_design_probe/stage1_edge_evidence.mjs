// Local visual evidence only; does not change boundary roles or unit counts.
const sub=(a,b)=>a.map((v,i)=>v-b[i]);
const dot=(a,b)=>a.reduce((s,v,i)=>s+v*b[i],0);
const norm=a=>Math.hypot(...a);
const scale=(a,s)=>a.map(v=>v*s);
const distance=(a,b)=>norm(sub(a,b));

export function inspectStraightBoundary(face,g,faces,tolerance=.001){
  if(g.geometry!=='Line'||!g.straight||g.closed)return null;
  const length=distance(g.start,g.end);if(length<=tolerance)return null;
  const u=scale(sub(g.end,g.start),1/length),matches=[];
  for(const f of faces){
    if(f.faceId===face.faceId)continue;
    for(const other of f.groups){
      if(other.geometry!=='Line'||!other.straight||other.closed)continue;
      const mismatch=Math.min(Math.max(distance(g.start,other.start),distance(g.end,other.end)),Math.max(distance(g.start,other.end),distance(g.end,other.start)));
      if(mismatch>tolerance)continue;
      const match={faceId:f.faceId,group:other.key,sourceEdges:other.sourceEdges,areaMm2:f.sourceAreaMm2,mismatchMm:mismatch,profile:null};
      // Show a small planar cap only when the full contour lies beside this
      // chord. Do not imply that any coincident edge proves a manifold solid.
      if(f.surface==='plane'&&f.wires.length===1){
        const points=f.groups.flatMap(x=>x.points);
        const offsets=points.map(p=>{const d=sub(p,g.start);return sub(d,scale(u,dot(d,u)));});
        const far=offsets.reduce((best,v)=>norm(v)>norm(best)?v:best,[0,0,0]);
        const height=norm(far);
        if(height>1e-8&&height<length*.25){
          const v=scale(far,1/height);
          const project=p=>{const d=sub(p,g.start);return [dot(d,u),dot(d,v)];};
          const coords=points.map(project);
          if(coords.every(p=>p[0]>=-tolerance&&p[0]<=length+tolerance&&p[1]>=-tolerance))match.profile={u,v,length,height,paths:f.groups.map(x=>({role:x.role,points:x.points.map(project)}))};
        }
      }
      matches.push(match);
    }
  }
  return {length,matches};
}
