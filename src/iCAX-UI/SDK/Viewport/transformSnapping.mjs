const dot=(a,b)=>a.reduce((s,v,i)=>s+v*b[i],0);
const add=(a,b)=>a.map((v,i)=>v+b[i]);
const norm=a=>Math.hypot(...a);
const solve=(matrix,right)=>{
  const a=matrix.map((row,i)=>[...row,right[i]]),n=a.length;
  for(let i=0;i<n;i++){
    let pivot=i;for(let j=i+1;j<n;j++)if(Math.abs(a[j][i])>Math.abs(a[pivot][i]))pivot=j;
    if(Math.abs(a[pivot][i])<1e-10)return null;
    [a[i],a[pivot]]=[a[pivot],a[i]];
    const divisor=a[i][i];for(let k=i;k<=n;k++)a[i][k]/=divisor;
    for(let j=0;j<n;j++)if(j!==i){const factor=a[j][i];for(let k=i;k<=n;k++)a[j][k]-=factor*a[i][k];}
  }
  return a.map(row=>row[n]);
};

// Snap only within the selected handle's motion subspace. The reference is an
// object's actual origin; a display pivot at its bounding-box center is separate.
export function snapTranslation({point,basis,spacing,origin=[0,0,0],project,tolerancePx=8,maxDistance=Infinity,gridEnabled=true}){
  if(!basis.length||!Number.isFinite(spacing)||spacing<=0)return null;
  const screen=project(point);if(!screen)return null;
  let best=null;
  const consider=(correction,rank,kind)=>{
    if(norm(correction)>maxDistance)return;
    const target=add(point,correction),pixel=project(target);if(!pixel)return;
    const distance=Math.hypot(pixel[0]-screen[0],pixel[1]-screen[1]);
    if(distance>tolerancePx)return;
    if(!best||rank>best.rank||rank===best.rank&&distance<best.distance)
      best={point:target,correction,rank,kind,distance,spacing};
  };
  const toOrigin=point.map(v=>-v),projected=[0,0,0];
  for(const b of basis){const t=dot(toOrigin,b);for(let i=0;i<3;i++)projected[i]+=b[i]*t;}
  if(norm(add(point,projected))<1e-7)consider(projected,4,'origin');
  if(!gridEnabled)return best;
  const axes=[0,1,2].filter(axis=>basis.some(b=>Math.abs(b[axis])>1e-7));
  for(let mask=1;mask<8;mask++){
    const selected=axes.filter(axis=>mask&(1<<axis));
    if(selected.length!==[0,1,2].filter(axis=>mask&(1<<axis)).length||selected.length>basis.length)continue;
    const rows=selected.map(axis=>basis.map(b=>b[axis])),gram=rows.map(a=>rows.map(b=>dot(a,b)));
    const targets=selected.map(axis=>[...new Set([origin[axis]+Math.round((point[axis]-origin[axis])/spacing)*spacing,0])]);
    const visit=(index,values)=>{
      if(index<targets.length){for(const value of targets[index])visit(index+1,[...values,value]);return;}
      const coefficients=solve(gram,selected.map((axis,i)=>values[i]-point[axis]));if(!coefficients)return;
      const parameters=basis.map((_,j)=>rows.reduce((s,row,i)=>s+row[j]*coefficients[i],0));
      const correction=[0,1,2].map(axis=>basis.reduce((s,b,j)=>s+b[axis]*parameters[j],0));
      consider(correction,selected.length,'grid');
    };
    visit(0,[]);
  }
  return best;
}

export function snapRotation(angle,step=Math.PI/12,tolerance=Math.PI/60){
  if(!Number.isFinite(step)||step<=0)return null;
  const target=Math.round(angle/step)*step;
  return Math.abs(target-angle)<=tolerance?target:null;
}
