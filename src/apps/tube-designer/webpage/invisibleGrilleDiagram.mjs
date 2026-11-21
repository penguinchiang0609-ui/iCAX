const EPS=1e-7;
const escape=value=>String(value??'').replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;').replaceAll('"','&quot;');

// Production structural artwork uses the same actual through-height wires
// and paired rectangular clamp stocks as the native product declaration.
export function invisibleGrilleDiagramStocks(diagram,values) {
  const get=key=>values[diagram.bindings[key]];
  const number=(key,minimum=0)=>{
    const value=get(key);
    if(value==null||value===''||!Number.isFinite(Number(value))||Number(value)<minimum)throw new Error('请完善当前尺寸');
    return Number(value);
  };
  const dimension=(role,key,minimum)=>{
    const source=values.tubeDesignerProfileOverrides?.[role]?.[key];
    return Number.isFinite(source)&&source>=minimum?source:number(role+(key==='width'?'Width':'Depth'),minimum);
  };
  const width=number('width',100),height=number('height',100);
  const wireDiameter=dimension('wire','width',EPS),clampWidth=dimension('clamp','width',EPS),clampDepth=dimension('clamp','depth',EPS);
  const wireKind=values.tubeDesignerProfileOverrides?.wire?.sectionKind??get('wireProfileType');
  const clampKind=values.tubeDesignerProfileOverrides?.clamp?.sectionKind??get('clampProfileType');
  if(wireKind!=='round-bar'||clampKind!=='rect-bar')throw new Error('竖线须为实心圆线，夹条须为矩形实心条');
  const spacing=number('wireCenterSpacing',EPS),count=number('clampCount');
  if(spacing<=wireDiameter+EPS)throw new Error('竖线中心距须大于线径');
  if(!Number.isInteger(count)||count>100)throw new Error('中间夹条组数须为0～100的整数');
  if(width<wireDiameter||height<=2*clampWidth+EPS)throw new Error('成品尺寸不足以容纳线材和上下夹条');
  const n=Math.floor((width-wireDiameter)/spacing)+1;
  if(n+2*(count+2)>1500)throw new Error('当前线材和压条数量过多，请增大竖线中心距');
  const first=(width-(n-1)*spacing)/2;
  const wireCenters=Array.from({length:n},(_,i)=>first+i*spacing);
  const middleCenters=[];
  if(count) {
    const bottom=number('clampBottomOffset'),top=count>1?height-number('clampTopOffset'):bottom;
    if(bottom-clampWidth/2<=clampWidth+EPS||top+clampWidth/2>=height-clampWidth-EPS)
      throw new Error('中间夹条须与上下固定夹条保留净空');
    if(count>1&&(top-bottom)/(count-1)<=clampWidth+EPS)throw new Error('中间夹条组之间须保留净空');
    for(let i=0;i<count;i++)middleCenters.push(count===1?bottom:bottom+(top-bottom)*i/(count-1));
  }
  const members=wireCenters.map((x,i)=>({key:`wire.vertical.${String(i+1).padStart(4,'0')}`,role:'wire.vertical',profileRole:'wire',
    start:[x,0,0],end:[x,0,height],width:wireDiameter,depth:wireDiameter}));
  const groups=[{key:'clamp.bottom',role:'clamp.bottom',z:clampWidth/2},
    ...middleCenters.map((z,i)=>({key:`clamp.middle.${String(i+1).padStart(4,'0')}`,role:'clamp.middle',z})),
    {key:'clamp.top',role:'clamp.top',z:height-clampWidth/2}];
  for(const group of groups)for(const [side,sign]of [['front',-1],['back',1]]) {
    const y=sign*(wireDiameter+clampDepth)/2;
    members.push({key:`${group.key}.${side}`,role:group.role,profileRole:'clamp',side,
      start:[0,y,group.z],end:[width,y,group.z],width:clampWidth,depth:clampDepth});
  }
  return{width,height,wireDiameter,wireCenterSpacing:spacing,clampWidth,clampDepth,wireCenters,middleCenters,members};
}

export function invisibleGrilleStyleLabel(_template,diagram,values) {
  const key=name=>diagram.bindings[name];
  return `竖向通长圆线 · 正反压条夹固 · 中间 ${values[key('clampCount')]??0} 组`;
}

export function renderInvisibleGrilleArtwork(template,diagram,values) {
  let body;
  try {
    const state=invisibleGrilleDiagramStocks(diagram,values);
    const {width,height,members}=state,scale=Math.min(270/width,160/height);
    const project=([x,y,z])=>[170+(x-width/2+.65*y)*scale,95+(height/2-z-.35*y)*scale];
    const polygon=points=>points.map(p=>project(p).map(v=>v.toFixed(3)).join(',')).join(' ');
    const clamp=m=>{
      const y=m.start[1],z=m.start[2],w=m.width/2,d=m.depth/2;
      const frontY=y-d;
      return `<g data-invisible-grille-member="${escape(m.key)}" data-invisible-grille-profile-role="clamp"><polygon points="${polygon([[0,frontY,z-w],[width,frontY,z-w],[width,frontY,z+w],[0,frontY,z+w]])}" style="fill:${m.side==='front'?'#b5c4cc':'#7d919d'};stroke:#c8d3d9;stroke-width:.35"/><polygon points="${polygon([[0,y-d,z+w],[width,y-d,z+w],[width,y+d,z+w],[0,y+d,z+w]])}" style="fill:#d1dce1;stroke:#c8d3d9;stroke-width:.25"/></g>`;
    };
    body=members.filter(m=>m.side==='back').map(clamp).join('');
    body+=members.filter(m=>m.profileRole==='wire').map(m=>{
      const a=project(m.start),b=project(m.end);
      return `<line data-invisible-grille-member="${escape(m.key)}" data-invisible-grille-profile-role="wire" x1="${a[0]}" y1="${a[1]}" x2="${b[0]}" y2="${b[1]}" style="stroke:#d8eceb;stroke-width:${Math.max(.5,m.width*scale)};stroke-linecap:butt"/>`;
    }).join('');
    body+=members.filter(m=>m.side==='front').map(clamp).join('');
  } catch(error) {body=`<text x="170" y="94" text-anchor="middle" style="fill:#efc27a;stroke:none;font-size:12px">${escape(error.message)}</text>`;}
  return `<svg class="tube-designer-product-structure-svg" data-invisible-grille-diagram viewBox="0 0 340 190" role="img" aria-label="隐形防护网：${escape(invisibleGrilleStyleLabel(template,diagram,values))}">${body}</svg>`;
}
