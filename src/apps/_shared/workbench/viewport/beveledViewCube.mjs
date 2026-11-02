import { renderViewCompass } from './viewCompass.mjs';
// A small chamfered solid with face-aligned lettering. Rendering and picking
// use separate polygons: narrow visible bevels keep generous edge/corner hits.
const xyz = ['x','y','z'];
const vec = values => Object.fromEntries(xyz.map((key,i)=>[key,values[i]]));
const dot = (a,b) => xyz.reduce((sum,key)=>sum+a[key]*b[key],0);
const unit = a => {const n=Math.sqrt(dot(a,a));return vec(xyz.map(key=>a[key]/n));};
const project = (p,basis) => ({x:dot(p,basis.right),y:-dot(p,basis.up)});
const escape = s => String(s).replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;').replaceAll('"','&quot;');
const fixed = n => Number(n.toFixed(3));
const labelAxes = {
  front:[[1,0,0],[0,0,1]], back:[[-1,0,0],[0,0,1]],
  right:[[0,1,0],[0,0,1]], left:[[0,-1,0],[0,0,1]],
  top:[[1,0,0],[0,1,0]], bottom:[[1,0,0],[0,-1,0]],
};

function corners(piece,bevel) {
  const normal=[piece.x,piece.y,piece.z],axes=normal.map((v,i)=>v?i:-1).filter(i=>i>=0),free=[0,1,2].filter(i=>!normal[i]),a=1.5-bevel;
  if(axes.length===1)return [[-1,-1],[1,-1],[1,1],[-1,1]].map(signs=>{
    const p=normal.map(v=>v*1.5);free.forEach((axis,i)=>p[axis]=signs[i]*a);return vec(p);
  });
  if(axes.length===2)return [[0,-1],[0,1],[1,1],[1,-1]].map(([full,sign])=>{
    const p=normal.map(v=>v*a);p[axes[full]]=normal[axes[full]]*1.5;p[free[0]]=sign*a;return vec(p);
  });
  return axes.map(full=>vec(normal.map((v,i)=>v*(i===full?1.5:a))));
}

export function renderBeveledViewCube({basis,pieces,labels,title,id,compass=false,triad=false}) {
  const scale=compass?23:22,origin=triad?{x:92,y:80}:compass?{x:72,y:72}:{x:58,y:54},point=p=>{const v=project(p,basis);return {x:origin.x+scale*v.x,y:origin.y+scale*v.y};};
  const points=vertices=>vertices.map(point).map(p=>`${fixed(p.x)},${fixed(p.y)}`).join(' ');
  const light=unit(vec(xyz.map(key=>-.42*basis.right[key]+.72*basis.up[key]+.65*basis.eye[key])));
  const visible=pieces.map(piece=>{
    const normal=unit(piece),facing=dot(normal,basis.eye),vertices=corners(piece,.17),center=vec(xyz.map(key=>vertices.reduce((v,p)=>v+p[key],0)/vertices.length));
    return {piece,normal,facing,vertices,center,depth:dot(center,basis.eye)};
  }).filter(face=>face.facing>.0001).sort((a,b)=>a.depth-b.depth);
  const shade=(face,offset)=>{
    const lit=.66+.34*Math.max(0,dot(face.normal,light)),base=face.piece.kind==='center'?[233,238,242]:[210,219,227];
    return `rgb(${base.map(v=>Math.round(Math.max(0,Math.min(255,v*lit+offset)))).join(',')})`;
  };
  const gradients=visible.map(face=>`<linearGradient id="${id}-${face.piece.name}" x1="0" y1="0" x2=".7" y2="1"><stop stop-color="${shade(face,9)}"/><stop offset="1" stop-color="${shade(face,-8)}"/></linearGradient>`).join('');
  const surfaces=visible.map(face=>{
    const name=face.piece.name,axes=labelAxes[name];let label='';
    if(axes&&face.facing>.19){
      const right=project(vec(axes[0]),basis),down=project(vec(axes[1].map(v=>-v)),basis),p=point(face.center);
      label=`<text class="cam-viewcube-label" data-face-label="${name}" transform="matrix(${fixed(right.x)} ${fixed(right.y)} ${fixed(down.x)} ${fixed(down.y)} ${fixed(p.x)} ${fixed(p.y)})" x="0" y="0">${escape(labels[name])}</text>`;
    }
    return `<g data-view-surface="${name}" pointer-events="none"><polygon class="cam-viewcube-surface" points="${points(face.vertices)}" fill="url(#${id}-${name})"/>${label}</g>`;
  }).join('');
  const rules=visible.map(({piece:{name}})=>`.cam-viewcube-svg:has([data-cam-view="${name}"]:is(:hover,:focus-visible)) [data-view-surface="${name}"] .cam-viewcube-surface {fill:url(#${id}-hover);stroke:#779ab8}`).join('\n');
  // Faces first, edges next, corners last: the larger invisible targets win at
  // boundaries without adding visible blocks or narrowing the face surface.
  const hits=[...visible].sort((a,b)=>({center:0,edge:1,corner:2}[a.piece.kind]-{center:0,edge:1,corner:2}[b.piece.kind])).map(({piece})=>`
    <g class="cam-viewcube-piece cam-viewcube-piece-${piece.kind}" data-cam-action="view-standard" data-cam-view="${piece.name}" role="button" tabindex="0" aria-label="${escape(title(piece.name))}">
      <polygon class="cam-viewcube-hit" points="${points(corners(piece,.17))}" fill="transparent" stroke="transparent" stroke-width="${piece.kind==='center'?0:9}" stroke-linejoin="round" pointer-events="all"/>
    </g>`).join('');
  const axes=triad?renderAxes(point):'';
  return `<svg class="cam-viewcube-svg" viewBox="0 0 ${triad?'184 184':compass?'144 148':'116 108'}" width="${triad?184:compass?144:116}" height="${triad?184:compass?148:108}" role="group" aria-label="视角导航">
    <defs>${gradients}<linearGradient id="${id}-hover" x2="0" y2="1"><stop stop-color="#d1e9f8"/><stop offset="1" stop-color="#8fb8d5"/></linearGradient>
      <filter id="${id}-shadow" x="-30%" y="-30%" width="160%" height="170%"><feDropShadow dx="0" dy="3" stdDeviation="2.4" flood-color="#030b10" flood-opacity=".28"/></filter>
    </defs><style>${rules}</style>${axes}${compass?renderViewCompass({basis,origin,scale,id}):''}<g class="cam-viewcube-solid" filter="url(#${id}-shadow)">${surfaces}</g>${hits}</svg>`;
}

function renderAxes(point){
  // These three axes and the cube share one world basis and one projection.
  // Drawing the triad first lets the solid naturally occlude its far sides.
  const base=[-1.9,-1.9,-1.9],start=point(vec(base));
  return `<g class="cam-viewcube-axes" role="img" aria-label="世界坐标方向 XYZ" pointer-events="none">${['#ef6469','#5bc780','#73a7ff'].map((color,i)=>{
    const end=point(vec(base.map((v,j)=>j===i?2.1:v))),dx=end.x-start.x,dy=end.y-start.y,n=Math.hypot(dx,dy),ux=n>1e-5?dx/n:0,uy=n>1e-5?dy/n:-1;
    const x=Math.max(8,Math.min(176,end.x+ux*10)),y=Math.max(8,Math.min(176,end.y+uy*10));
    return `<g data-cam-axis="${xyz[i]}" fill="${color}" stroke="${color}"><line x1="${fixed(start.x)}" y1="${fixed(start.y)}" x2="${fixed(end.x)}" y2="${fixed(end.y)}" stroke-width="1.4"/><path d="M${fixed(end.x)} ${fixed(end.y)}l${fixed(-ux*5+uy*2)} ${fixed(-uy*5-ux*2)}m${fixed(ux*5-uy*2)} ${fixed(uy*5+ux*2)}l${fixed(-ux*5-uy*2)} ${fixed(-uy*5+ux*2)}" fill="none" stroke-width="1.1"/><text class="cam-viewcube-axis-label" x="${fixed(x)}" y="${fixed(y)}">${xyz[i].toUpperCase()}</text></g>`;
  }).join('')}<circle cx="${fixed(start.x)}" cy="${fixed(start.y)}" r="2" fill="#d4e1e7"/></g>`;
}
