import {escapeText as esc,escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';
import {saveCommittedProjectEdit} from './projectEditPersistence.mjs';
import {fixtureIcon} from './fixtureIcons.mjs';
import {fixtureUnitDesign,readFixtureResource} from './fixtureQueries.mjs';
import {boundaryDisplay,boundaryBounds,boundaryProperties,sketchBoundary,transformBoundary,applyBoundaryProperties} from './fixturePlateSketch.mjs';

const sessions=new WeakMap(),clone=value=>structuredClone(value);
const current=(context,view,s)=>!view.disposed&&context.isCurrentProject?.()!==false&&view.fiveFixturePlate===s&&context.sceneProxy===s.proxy&&view.scene?.domain?.activeSetupId===s.setupId;
const selected=s=>s.profile?.Boundaries.find(loop=>loop.ID===s.selected);
const editable=(s,id)=>!!id&&!s.protectedIds.includes(id);
const button=(action,label,disabled=false)=>`<button type="button" data-plate-action="${action}" ${disabled?'disabled':''} ${['undo','redo','fit','close','delete'].includes(action)?`class="plate-icon-button" title="${label==='×'?'关闭':label}" aria-label="${label==='×'?'关闭':label}"`:''}>${['undo','redo','fit','delete'].includes(action)||action==='close'&&label==='×'?fixtureIcon(action):label}</button>`;
const message=error=>{const text=error.message||String(error),band=text.match(/PLATE_EDIT_MINIMUM_WEB: ([\d.]+) \/ ([\d.]+) mm/);if(band)return `剩余板带 ${Number(band[1]).toFixed(2)} mm，至少需要 ${Number(band[2])} mm。请调整开孔边距。`;if(/PROTECTED|JOINT|FOOT/.test(text))return '开孔会破坏接头或承托位置，请调整孔的位置和尺寸。';if(/DISCONNECTED/.test(text))return '开孔会切断板件，请调整开孔范围。';if(/MINIMUM_WEB/.test(text))return '剩余板带不足，请调整开孔边距。';if(/HOLE_OUTSIDE/.test(text))return '开孔未接触板件，请调整位置。';return text;};
function install(document){if(document.getElementById('five-fixture-plate-style'))return;const link=document.createElement('link');Object.assign(link,{id:'five-fixture-plate-style',rel:'stylesheet',href:new URL('./fixturePlateEditor.css',import.meta.url).href});document.head.append(link);}
function select(s,id){s.selected=id;const loop=selected(s);s.fields=loop?Object.fromEntries(Object.entries(boundaryProperties(loop)).map(([k,v])=>[k,String(v)])):{};}
function checkpoint(s){s.undo.push(clone(s.profile));if(s.undo.length>100)s.undo.shift();s.redo=[];s.dirty=true;s.error='';}
function fit(s){const b=boundaryBounds(s.profile.Boundaries[0]),w=Math.max(1,b[2]-b[0]),h=Math.max(1,b[3]-b[1]),margin=Math.max(w,h)*.08;s.box=[b[0]-margin,b[1]-margin,w+2*margin,h+2*margin];}
export function fixturePlateHistoryState(view){const s=view.fiveFixturePlate;return s?.open?{scope:'editor',canUndo:!!s.undo?.length,canRedo:!!s.redo?.length,blocked:!!(s.loading||s.saving||s.gesture)}:null;}
export function fixturePlateHistoryCommand(context,view,direction,ops){const s=view.fiveFixturePlate;if(!s?.open||s.loading||s.saving)return;const from=direction==='redo'?s.redo:s.undo,to=direction==='redo'?s.undo:s.redo;if(from.length){to.push(clone(s.profile));s.profile=from.pop();s.dirty=true;s.drawing=null;s.error='';select(s,s.profile.Boundaries.some(l=>l.ID===s.selected)?s.selected:'');ops.renderProject(context,view);context.actions?.refreshProjectHistoryControls?.();}}
async function mutate(context,method,request){try{return await context.sceneProxy.invoke(method,request);}catch(error){if(error.frame||!/timed?\s*out|transport|network|lost.*response|disconnect/i.test(error.message||''))throw error;const receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:request.expected.epoch,commandId:request.commandId});if(receipt.status==='Completed')return receipt.result;throw error;}}
export async function fixturePlateCommand(context,view,action,ops){
  if(view.disposed||context.isCurrentProject?.()===false)return;
  if(view.pending||view.fixtureRequest&&view.fiveFixturePlate?.saving)return;
  const kind=action.slice(0,action.indexOf(':')<0?undefined:action.indexOf(':')),partId=action.slice(action.indexOf(':')+1);
  if(kind==='part-close'){if(view.fiveFixturePlate?.saving)return;view.fiveFixturePlate=null;ops.renderProject(context,view);context.actions?.refreshProjectHistoryControls?.();return;}
  const unit=view.scene?.domain?.fixtureUnits?.find(u=>u.kind==='Generated'&&u.setupId===view.scene.domain.activeSetupId&&u.partIds.includes(partId));
  if(['part-select','part-edit','part-toggle'].includes(kind)&&!unit)return;
  if(kind==='part-select'){view.fiveFixtureSelectedPart=partId;ops.renderProject(context,view);return;}
  if(kind==='part-edit'){
    if(unit.locked){view.error='请先解锁夹具单元。';ops.renderProject(context,view);return;}
    const part=fixtureUnitDesign(view,unit).assembly?.parts?.find(p=>p.partId===partId)||{partId,number:view.scene.domain.fixtures.find(p=>p.id===partId)?.name},s={open:true,loading:true,saving:false,partId,unitId:unit.id,setupId:unit.setupId,part,proxy:context.sceneProxy,projectProxy:context.projectProxy,project:{...context.project},selected:'',protectedIds:[],undo:[],redo:[],tool:'select',dirty:false,error:'',fields:{},drawing:null};
    view.fiveFixturePlate=s;view.fiveFixtureSelectedPart=partId;install(context.mount.ownerDocument);ops.renderProject(context,view);
    try{const result=await s.proxy.invoke('CamFixture.PartProfile',{setupId:s.setupId,unitId:s.unitId,partId});const body=await readFixtureResource({sceneProxy:s.proxy},result.profileResourceRef);if(!current(context,view,s))return;s.profile=clone(body.profile);s.geometryRef=result.geometryRef;s.protectedIds=result.protectedBoundaryIds;s.original=clone(s.profile);fit(s);select(s,s.profile.Boundaries.find(l=>editable(s,l.ID))?.ID||'');}
    catch(error){if(current(context,view,s))s.error=message(error);}finally{s.loading=false;if(current(context,view,s))ops.renderProject(context,view);}return;
  }
  if(kind==='part-toggle'){
    const part=view.scene.domain.fixtures.find(p=>p.id===partId),proxy=context.sceneProxy,active=()=>!view.disposed&&context.sceneProxy===proxy&&context.isCurrentProject?.()!==false;
    view.pending=true;view.error='';ops.renderProject(context,view);
    try{await mutate(context,'CamFixture.Update',{commandId:crypto.randomUUID(),expected:view.scene.domain.expected,setupId:unit.setupId,fixtureId:partId,enabled:part.enabled===false});await saveCommittedProjectEdit(context,view,ops);if(active())await ops.refreshSceneState(context,view);}
    catch(error){if(active())view.error=message(error);}finally{view.pending=false;if(active())ops.renderProject(context,view);}return;
  }
  if(kind!=='part-save')return;
  const s=view.fiveFixturePlate;if(!s?.profile||s.saving||!current(context,view,s))return;
  if(s.drawing){s.error='请先完成当前开孔。';ops.renderProject(context,view);return;}
  if(!s.dirty){view.fiveFixturePlate=null;ops.renderProject(context,view);return;}
  s.saving=true;s.error='';view.pending=true;ops.renderProject(context,view);
  try{
    await mutate(context,'CamFixture.PartUpdate',{commandId:crypto.randomUUID(),expected:view.scene.domain.expected,setupId:s.setupId,unitId:s.unitId,partId:s.partId,geometryRef:s.geometryRef,profile:s.profile});
    await saveCommittedProjectEdit({...context,projectProxy:s.projectProxy,project:s.project},view,ops);
    if(current(context,view,s)){view.fiveFixturePlate=null;await ops.refreshSceneState(context,view);}
  }catch(error){if(current(context,view,s))s.error=message(error);}finally{s.saving=false;view.pending=false;if(context.sceneProxy===s.proxy&&!view.disposed&&context.isCurrentProject?.()!==false){ops.renderProject(context,view);context.actions?.refreshProjectHistoryControls?.();}}
}
function properties(s){
  const loop=selected(s);if(!loop)return '<p>选择一个孔进行编辑</p>';
  if(!editable(s,loop.ID))return '<p>接头与保留边界</p>';
  const kind=boundaryProperties(loop).kind,fields=kind==='circle'?[['x','中心 X'],['y','中心 Y'],['diameter','孔径']]:kind==='polygon'?[['x','左下 X'],['y','左下 Y'],['width','宽度'],['height','高度']]:[['x','左下 X'],['y','左下 Y'],['factor','放大倍数']];
  return fields.map(([key,label])=>`<label data-five-key="plate-property-${key}"><span>${label}</span><input type="text" inputmode="decimal" autocomplete="off" data-plate-field="${key}" data-five-key="plate-property-input-${key}" aria-label="${label}" value="${attr(s.fields[key]??'')}"></label>`).join('')+button('delete','删除此孔',s.saving);
}
function paths(s){
  const map=p=>[p[0],-p[1]],outer=boundaryDisplay(s.profile.Boundaries[0],map),box=boundaryBounds(s.profile.Boundaries[0]),mask='plate-material-'+s.partId;
  let html=`<defs><mask id="${attr(mask)}" maskUnits="userSpaceOnUse" x="${box[0]-1}" y="${-box[3]-1}" width="${box[2]-box[0]+2}" height="${box[3]-box[1]+2}" style="mask-type:luminance"><path d="${attr(outer)}" fill="white"/>${s.profile.Boundaries.slice(1).map(loop=>`<path d="${attr(boundaryDisplay(loop,map))}" fill="black"/>`).join('')}</mask></defs><path d="${attr(outer)}" class="plate-material" mask="url(#${attr(mask)})"/>`;
  for(const loop of s.profile.Boundaries)html+=`<path d="${attr(boundaryDisplay(loop,map))}" ${loop===s.profile.Boundaries[0]?`mask="url(#${attr(mask)})"`:''} class="plate-boundary ${editable(s,loop.ID)?'':'is-protected'} ${loop.ID===s.selected?'is-selected':''}" data-plate-boundary="${attr(loop.ID)}" data-five-key="plate-loop-${attr(loop.ID)}"/>`;
  const chosen=selected(s);if(chosen&&editable(s,chosen.ID)){
    const b=boundaryBounds(chosen),x=(b[0]+b[2])/2,y=(b[1]+b[3])/2,size=Math.max(s.box[2],s.box[3])/95;
    html+=`<rect class="plate-resize-box" x="${b[0]}" y="${-b[3]}" width="${b[2]-b[0]}" height="${b[3]-b[1]}"/>`;
    for(const [handle,u,v] of [['nw',b[0],b[3]],['n',x,b[3]],['ne',b[2],b[3]],['e',b[2],y],['se',b[2],b[1]],['s',x,b[1]],['sw',b[0],b[1]],['w',b[0],y]])html+=`<rect class="plate-resize-handle" data-plate-resize="${handle}" x="${u-size/2}" y="${-v-size/2}" width="${size}" height="${size}" style="cursor:${['n','s'].includes(handle)?'ns': ['e','w'].includes(handle)?'ew':['nw','se'].includes(handle)?'nwse':'nesw'}-resize"/>`;
  }
  if(s.drawing?.points?.length){const p=s.drawing.points.map(map),size=Math.max(s.box[2],s.box[3])/90;html+=`<path d="${p.map((q,i)=>`${i?'L':'M'}${q.join(' ')}`).join(' ')}${s.drawing.cursor?` L${map(s.drawing.cursor).join(' ')}`:''}" class="plate-draft"/>`;if(p.length>=3)html+=`<path d="M${(s.drawing.cursor?map(s.drawing.cursor):p.at(-1)).join(' ')} L${p[0].join(' ')}" class="plate-draft plate-closing-guide"/>`;for(let i=0;i<p.length;++i)html+=`<circle cx="${p[i][0]}" cy="${p[i][1]}" r="${i===0?size:size*.55}" class="plate-polygon-vertex ${i===0?'is-start':''}" ${i===0?'data-plate-close-polygon':''}><title>${i===0?'点击起点闭合':''}</title></circle>`;}
  if(s.drawing?.start&&s.drawing.current){const a=s.drawing.start,b=s.drawing.current,entity=s.tool==='circle'?{kind:'circle',cx:a[0],cy:a[1],radius:Math.hypot(b[0]-a[0],b[1]-a[1]),closed:true}:{kind:'rectangle',x:Math.min(a[0],b[0]),y:Math.min(a[1],b[1]),width:Math.abs(b[0]-a[0]),height:Math.abs(b[1]-a[1]),closed:true};if(s.tool==='circle'?entity.radius>1e-8:entity.width>1e-8&&entity.height>1e-8)html+=`<path d="${attr(boundaryDisplay(sketchBoundary(entity,'draft'),map))}" class="plate-draft"/>`;}
  return html;
}
export function renderFixturePlateEditor(view){
  const s=view.fiveFixturePlate;if(!s?.open)return '';
  const busy=s.loading||s.saving,box=s.box||[0,0,100,100];
  return `<section class="five-plate-editor" data-five-key="fixture-plate-editor" data-fixture-plate-editor role="dialog" aria-modal="true" aria-label="板件二维编辑"><header><strong>二维编辑 · ${esc(s.part.number)}</strong><span>mm</span>${button('close','×',s.saving)}</header><nav aria-label="二维绘图工具">${[['select','选择'],['rectangle','矩形孔'],['circle','圆孔'],['polyline','多边形孔']].map(([tool,label])=>`<button type="button" data-plate-tool="${tool}" aria-pressed="${s.tool===tool}" ${busy?'disabled':''} class="plate-icon-button" title="${label}" aria-label="${label}">${fixtureIcon(tool)}</button>`).join('')}<i class="plate-toolbar-separator" aria-hidden="true"></i>${button('undo','撤销',busy||!s.undo.length)}${button('redo','重做',busy||!s.redo.length)}${button('fit','适合窗口',busy)}</nav><div class="five-plate-layout"><aside aria-label="孔列表"><strong>开孔</strong>${s.profile?.Boundaries.slice(1).map((loop,i)=>`<button type="button" data-plate-select="${attr(loop.ID)}" data-five-key="plate-list-${attr(loop.ID)}" aria-pressed="${s.selected===loop.ID}" class="${editable(s,loop.ID)?'':'is-protected'}">${editable(s,loop.ID)?`孔 ${i+1}`:`保留孔 ${i+1}`}</button>`).join('')||''}</aside><div class="five-plate-canvas"><svg data-plate-canvas data-five-key="plate-canvas" tabindex="0" role="img" aria-label="板件二维轮廓" viewBox="${box[0]} ${-box[1]-box[3]} ${box[2]} ${box[3]}"><g data-plate-paths data-five-key="plate-paths">${s.profile?paths(s):''}</g></svg>${s.loading?'<div class="five-plate-loading">正在读取板件…</div>':''}<div class="five-plate-prompt">${s.tool==='circle'?'拖动指定圆心与半径':s.tool==='rectangle'?'拖动绘制矩形孔':s.tool==='polyline'?'点选至少 3 个顶点，点击亮色起点闭合；也可按 Enter 或双击':'拖动孔移动；拖动边框缩放，Shift 保持比例。滚轮缩放，中键平移'}</div></div><aside class="five-plate-properties" aria-label="孔尺寸"><strong>孔尺寸</strong>${s.profile?properties(s):''}</aside></div><footer>${s.error?`<p role="alert" data-five-key="plate-error">${esc(s.error)}</p>`:''}<span>${s.saving?'正在更新板件…':''}</span>${button('close','取消',s.saving)}${button('save','完成',busy||!s.profile)}</footer></section>`;
}
function commitProperties(context,view,s,ops){
  const loop=selected(s);if(!loop||!editable(s,loop.ID)||s.saving)return;
  const values=boundaryProperties(loop);if(Object.entries(values).filter(([key])=>key!=='kind').every(([key,value])=>String(s.fields[key]).trim()!==''&&Number(s.fields[key])===value))return;
  try{const next=applyBoundaryProperties(loop,s.fields);if(JSON.stringify(next)===JSON.stringify(loop))return;checkpoint(s);s.profile.Boundaries[s.profile.Boundaries.indexOf(loop)]=next;select(s,next.ID);ops.renderProject(context,view);}
  catch(error){s.error=message(error);ops.renderProject(context,view);}
}
export function attachFixturePlateEditor(context,view,ops){
  const root=context.mount?.querySelector('[data-fixture-plate-editor]'),s=view.fiveFixturePlate;if(!root||!s?.open)return;
  install(root.ownerDocument);let binding=sessions.get(root);if(binding){Object.assign(binding,{context,view,ops,s});return;}
  binding={context,view,ops,s};sessions.set(root,binding);
  const owner=()=>{const b=sessions.get(root);return b&&current(b.context,b.view,b.s)?b:null;},paint=b=>{const svg=root.querySelector('[data-plate-canvas]'),box=b.s.box;svg.setAttribute('viewBox',`${box[0]} ${-box[1]-box[3]} ${box[2]} ${box[3]}`);svg.querySelector('[data-plate-paths]').innerHTML=paths(b.s);};
  const at=(event,svg)=>{const p=svg.createSVGPoint();p.x=event.clientX;p.y=event.clientY;const q=p.matrixTransform(svg.getScreenCTM().inverse());return [q.x,-q.y];};
  const add=(b,entity)=>{checkpoint(b.s);const loop=sketchBoundary(entity);b.s.profile.Boundaries.push(loop);b.s.drawing=null;b.s.tool='select';select(b.s,loop.ID);b.ops.renderProject(b.context,b.view);};
  const finishPolygon=b=>{const points=b.s.drawing?.points;if(points?.length>=3)add(b,{kind:'polyline',points,closed:true});};
  root.addEventListener('click',event=>{const b=owner();if(!b||b.s.saving)return;const tool=event.target.closest('[data-plate-tool]')?.dataset.plateTool,id=event.target.closest('[data-plate-select]')?.dataset.plateSelect,action=event.target.closest('[data-plate-action]')?.dataset.plateAction;
    if(tool){b.s.tool=tool;b.s.drawing=null;b.ops.renderProject(b.context,b.view);}else if(id){select(b.s,id);b.ops.renderProject(b.context,b.view);}else if(action){
      if(action==='close')void fixturePlateCommand(b.context,b.view,'part-close',b.ops);
      if(action==='save'){b.s.error='';commitProperties(b.context,b.view,b.s,b.ops);if(!b.s.error)void fixturePlateCommand(b.context,b.view,'part-save',b.ops);}
      if(action==='fit'){fit(b.s);paint(b);}
      if(action==='undo'||action==='redo')fixturePlateHistoryCommand(b.context,b.view,action,b.ops);
      if(action==='delete'&&editable(b.s,b.s.selected)){checkpoint(b.s);b.s.profile.Boundaries=b.s.profile.Boundaries.filter(l=>l.ID!==b.s.selected);select(b.s,'');b.ops.renderProject(b.context,b.view);}
    }});
  root.addEventListener('input',event=>{const b=owner(),field=event.target.dataset.plateField;if(b&&field)b.s.fields[field]=event.target.value;});
  root.addEventListener('focusout',event=>{const b=owner();if(b&&event.target.dataset.plateField)commitProperties(b.context,b.view,b.s,b.ops);});
  root.addEventListener('keydown',event=>{const b=owner();if(!b||event.defaultPrevented)return;
    if(event.key==='Escape'){event.preventDefault();event.stopPropagation();b.s.drawing=null;b.s.tool='select';select(b.s,b.s.selected);b.ops.renderProject(b.context,b.view);}
    else if(event.key==='Enter'){event.preventDefault();event.stopPropagation();if(event.target.dataset.plateField)commitProperties(b.context,b.view,b.s,b.ops);else finishPolygon(b);}
    else if((event.key==='Delete'||event.key==='Backspace')&&!event.target.matches('input')){event.preventDefault();event.stopPropagation();root.querySelector('[data-plate-action="delete"]')?.click();}
    else if((event.ctrlKey||event.metaKey)&&event.key.toLowerCase()==='z'){event.preventDefault();event.stopPropagation();fixturePlateHistoryCommand(b.context,b.view,event.shiftKey?'redo':'undo',b.ops);}
  },true);
  const svg=root.querySelector('[data-plate-canvas]');
  svg.addEventListener('wheel',event=>{const b=owner();if(!b||b.s.loading)return;event.preventDefault();const p=at(event,svg),factor=event.deltaY>0?1.15:1/1.15,box=b.s.box;const width=box[2]*factor,height=box[3]*factor;if(width<.01||width>1e6)return;b.s.box=[p[0]+(box[0]-p[0])*factor,p[1]+(box[1]-p[1])*factor,width,height];paint(b);},{passive:false});
  svg.addEventListener('pointerdown',event=>{const b=owner();if(!b||b.s.loading||b.s.saving)return;const p=at(event,svg);svg.focus({preventScroll:true});
    if(event.button===1||event.button===0&&event.altKey){event.preventDefault();const matrix=svg.getScreenCTM();b.s.gesture={kind:'pan',point:[event.clientX,event.clientY],scale:matrix.a,box:[...b.s.box],pointer:event.pointerId};svg.setPointerCapture(event.pointerId);return;}
    if(event.button!==0)return;
    const handle=event.target.closest('[data-plate-resize]')?.dataset.plateResize;
    if(handle&&selected(b.s)&&editable(b.s,b.s.selected)){event.preventDefault();b.s.gesture={kind:'resize',handle,original:clone(selected(b.s)),bounds:boundaryBounds(selected(b.s)),pointer:event.pointerId,changed:false};svg.setPointerCapture(event.pointerId);return;}
    if(b.s.tool==='polyline'){b.s.drawing??={points:[]};const points=b.s.drawing.points;if(points.length>=3&&(event.target.closest('[data-plate-close-polygon]')||Math.hypot(p[0]-points[0][0],p[1]-points[0][1])*Math.abs(svg.getScreenCTM().a)<10)){finishPolygon(b);return;}if(!points.length||Math.hypot(p[0]-points.at(-1)[0],p[1]-points.at(-1)[1])>1e-7)points.push(p);paint(b);return;}
    if(b.s.tool==='circle'||b.s.tool==='rectangle'){b.s.drawing={start:p,current:p};b.s.gesture={kind:'draw',pointer:event.pointerId};svg.setPointerCapture(event.pointerId);return;}
    const id=event.target.closest('[data-plate-boundary]')?.dataset.plateBoundary;select(b.s,id||'');
    if(id&&editable(b.s,id)){b.s.gesture={kind:'move',point:p,original:clone(selected(b.s)),pointer:event.pointerId,changed:false};svg.setPointerCapture(event.pointerId);paint(b);}else b.ops.renderProject(b.context,b.view);
  });
  svg.addEventListener('pointermove',event=>{const b=owner(),g=b?.s.gesture;if(!b)return;const p=at(event,svg);if(!g){if(b.s.tool==='polyline'&&b.s.drawing?.points?.length){const previous=b.s.drawing.cursor;if(previous&&Math.hypot(previous[0]-p[0],previous[1]-p[1])<1e-7)return;b.s.drawing.cursor=p;paint(b);}return;}
    if(g.kind==='pan'){b.s.box=[g.box[0]-(event.clientX-g.point[0])/g.scale,g.box[1]+(event.clientY-g.point[1])/g.scale,g.box[2],g.box[3]];paint(b);}
    if(g.kind==='draw'){b.s.drawing.current=p;paint(b);}
    if(g.kind==='move'){const dx=p[0]-g.point[0],dy=p[1]-g.point[1];if(!g.changed&&Math.hypot(dx,dy)>1e-6){checkpoint(b.s);g.changed=true;}if(g.changed){const index=b.s.profile.Boundaries.findIndex(l=>l.ID===g.original.ID);b.s.profile.Boundaries[index]=transformBoundary(g.original,{dx,dy});paint(b);}}
    if(g.kind==='resize'){
      const [x0,y0,x1,y1]=g.bounds,w=x1-x0,h=y1-y0,west=g.handle.includes('w'),east=g.handle.includes('e'),north=g.handle.includes('n'),south=g.handle.includes('s');
      let sx=west?(x1-Math.min(p[0],x1-1e-5))/w:east?(Math.max(p[0],x0+1e-5)-x0)/w:1,sy=south?(y1-Math.min(p[1],y1-1e-5))/h:north?(Math.max(p[1],y0+1e-5)-y0)/h:1;
      if(event.shiftKey||boundaryProperties(g.original).kind==='circle'){const factor=west||east?north||south?Math.abs(sx-1)>Math.abs(sy-1)?sx:sy:sx:sy;sx=sy=factor;}
      if(!g.changed&&Math.max(Math.abs(sx-1),Math.abs(sy-1))>1e-6){checkpoint(b.s);g.changed=true;}
      if(g.changed){const origin=[west?x1:east?x0:(x0+x1)/2,south?y1:north?y0:(y0+y1)/2];try{const index=b.s.profile.Boundaries.findIndex(l=>l.ID===g.original.ID);b.s.profile.Boundaries[index]=transformBoundary(g.original,{origin,scaleX:sx,scaleY:sy});paint(b);}catch(error){b.s.error=message(error);}}
    }
  });
  const end=event=>{const b=owner(),g=b?.s.gesture;if(!g)return;b.s.gesture=null;
    if(g.kind==='draw'){const a=b.s.drawing.start,p=b.s.drawing.current;if(b.s.tool==='circle'&&Math.hypot(p[0]-a[0],p[1]-a[1])>1e-6)add(b,{kind:'circle',cx:a[0],cy:a[1],radius:Math.hypot(p[0]-a[0],p[1]-a[1]),closed:true});else if(b.s.tool==='rectangle'&&Math.abs(p[0]-a[0])>1e-6&&Math.abs(p[1]-a[1])>1e-6)add(b,{kind:'rectangle',x:Math.min(a[0],p[0]),y:Math.min(a[1],p[1]),width:Math.abs(p[0]-a[0]),height:Math.abs(p[1]-a[1]),closed:true});else{b.s.drawing=null;b.ops.renderProject(b.context,b.view);}}
    else if(g.kind==='move'||g.kind==='resize'){select(b.s,b.s.selected);b.ops.renderProject(b.context,b.view);}if(svg.hasPointerCapture(event.pointerId))svg.releasePointerCapture(event.pointerId);
  };
  svg.addEventListener('pointerup',end);svg.addEventListener('pointercancel',event=>{const b=owner();if(!b?.s.gesture)return;const g=b.s.gesture;b.s.gesture=null;b.s.drawing=null;if((g.kind==='move'||g.kind==='resize')&&g.changed){b.s.profile.Boundaries[b.s.profile.Boundaries.findIndex(l=>l.ID===g.original.ID)]=g.original;b.s.undo.pop();}paint(b);});
  svg.addEventListener('dblclick',event=>{const b=owner();if(b&&b.s.tool==='polyline'){event.preventDefault();finishPolygon(b);}});
}
export function disposeFixturePlateEditor(view){view.fiveFixturePlate=null;}
