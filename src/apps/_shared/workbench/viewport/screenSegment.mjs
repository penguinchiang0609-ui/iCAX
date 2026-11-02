// Clip before dividing by W: a long edge can cross the visible view even when
// its midpoint or either original endpoint is behind a clipping plane.
export function clipScreenSegment(a,b,rect){
  let low=0,high=1;
  for(const values of [[a.w+a.x,b.w+b.x],[a.w-a.x,b.w-b.x],[a.w+a.y,b.w+b.y],[a.w-a.y,b.w-b.y],[a.w+a.z,b.w+b.z],[a.w-a.z,b.w-b.z]]){
    const [p,q]=values;if(p<0&&q<0)return null;
    if(p<0)low=Math.max(low,p/(p-q));else if(q<0)high=Math.min(high,p/(p-q));
    if(low>high)return null;
  }
  const project=t=>{const w=a.w+(b.w-a.w)*t;if(w<=1e-12)return null;const x=(a.x+(b.x-a.x)*t)/w,y=(a.y+(b.y-a.y)*t)/w,z=(a.z+(b.z-a.z)*t)/w;
    return {x:rect.left+(x+1)*rect.width/2,y:rect.top+(1-y)*rect.height/2,z:Math.max(-1,Math.min(1,z))};};
  const first=project(low),last=project(high);return first&&last?[first,last]:null;
}
