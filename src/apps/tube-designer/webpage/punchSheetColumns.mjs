import { escapeAttr as attr, escapeText as text } from '../../_shared/workbench/utils/format.mjs';

const select=(key,title,options,width=100)=>({key,title,options,width});
const number=(key,title,unit='mm',width=76)=>({key,title,unit,width});
const check=(key,title)=>({key,title,type:'checkbox',width:44});
const directions=[['positive','正向'],['negative','反向']];
const lengthModes=[['pitch','固定间距'],['end-margins','两端留距均分'],['middle-fixed','整体居中'],['center-out','中心扩散'],['fill','区间排满'],['max-spacing','最大间距均分'],['sequence','不等距序列'],['positions','位置表'],['equal','首尾等距均分']];

export function punchPoseColumns(item,descriptor) {
  const fields=[select('reference','定位基准',[['start','距起点'],['end','距终点'],['center','距中心']],90),number('station','位置')];
  if(item.toolTarget!=='part')fields.push(select('face','所在面',[['top','上方（+Z）'],['bottom','下方（-Z）'],['left','左方（-Y）'],['right','右方（+Y）'],['round','周向角度']],106),number('offset',item.face==='round'?'周向角度':'面内偏移',item.face==='round'?'°':'mm'),number('rotation','孔形旋转','°'));
  if(item.toolTarget!=='part'&&item.layoutDatum!=='base')fields.push(select('endDatum','端面基准',[['long','长点 / 包络'],['center','端面中心'],['short','短点']],100));
  if(descriptor?.requiresSection)fields.push(number('angle','轴夹角','°'),number('azimuth','方位角','°'),number('roll','绕轴旋转','°'),number('offsetY','横向偏移'),number('offsetZ','高度偏移'),select('direction','拉伸方向',[['through','贯穿主管'],['symmetric','对称'],['positive','正向'],['negative','反向']],100),number('length','拉伸长度'));
  else if(descriptor?.target==='part')fields.push(number('rotation','绕主管旋转','°'));
  return fields;
}

// These are the editable fields of each independent copy recipe. The same
// keys/actions feed the existing layout solver and native transport unchanged.
export function punchArrayColumns(group) {
  const fields=[select('type','类型',[['linear','直线'],['polar','圆周']],72)];
  if(group.enabled===false)return fields;
  fields.push(select('axis','方向',[['X','X · 长度'],['Y','Y · 横向'],['Z','Z · 高度']],86));
  const count=()=>number('count','数量','个',64),spacing=()=>number('spacing','间距'),direction=()=>select('direction','复制方向',directions,74);
  if(group.type==='polar') {
    fields.push(select('angleMode','圆周规则',[['pitch','固定角距'],['full-circle','整圈均分'],['angle-range','角区间均分']],110),count(),number('startAngle','起始角','°'));
    if(group.angleMode==='angle-range')fields.push(number('endAngle','结束角','°'));
    else if(group.angleMode!=='full-circle')fields.push(number('angleStep','角距','°'));
    fields.push(check('originAuto','中心轴'),number('originX','轴位置 X'),number('originY','轴位置 Y'),number('originZ','轴位置 Z'));
  } else if(group.axis!=='X')fields.push(count(),spacing(),direction());
  else {
    const mode=group.distributionMode??'pitch';
    fields.push(select('distributionMode','长度规则',lengthModes,130));
    if(['pitch','equal','end-margins','middle-fixed','center-out'].includes(mode))fields.push(count());
    if(['pitch','middle-fixed','center-out','fill'].includes(mode))fields.push(spacing());
    if(['end-margins','fill','max-spacing'].includes(mode))fields.push(number('headMargin','首端距'),number('tailMargin','尾端距'));
    if(mode==='center-out') {
      fields.push(number('centerOffset','中心偏移'),select('centerMode','中心形式',[['hole','中心有孔'],['gap','中心留空']],100));
      if(group.centerMode==='gap')fields.push({...number('centerFirstOffset','首对孔偏移'),placeholder:'半步距'});
    }
    if(mode==='fill')fields.push(select('fillAlign','余量分配',[['start','留尾端'],['center','两端平分'],['end','留首端']],100));
    if(mode==='max-spacing')fields.push(number('maxSpacing','最大中心距'));
    if(mode==='sequence')fields.push({key:'spacingSequence',title:'间距序列',unit:'mm',type:'text',width:150,placeholder:'100, 150, 50*3'});
    if(mode==='positions')fields.push({key:'positionList',title:'位置表',unit:'mm',type:'text',width:150,placeholder:'100, 250, 400'});
    if(['pitch','sequence'].includes(mode))fields.push(direction());
  }
  return fields;
}

export function mergePunchColumns(lists) {
  const merged=new Map();
  for(const list of lists)for(const field of list) {
    const previous=merged.get(field.key);
    if(!previous)merged.set(field.key,{...field});
    else if(previous.title!==field.title||previous.unit!==field.unit)merged.set(field.key,{...previous,title:field.key==='offset'?'面内偏移 / 周向角度':previous.title+' / '+field.title,unit:previous.unit===field.unit?previous.unit:'mm / °'});
  }
  return [...merged.values()];
}

export function renderPunchColumnControl(field,value,attrs,disabled=false) {
  const attributes=attrs+' aria-label="'+attr(field.title+(field.unit?' / '+field.unit:''))+'"'+(disabled?' disabled':'');
  if(field.type==='checkbox')return '<input type="checkbox"'+attributes+(value?' checked':'')+'/>';
  if(field.options)return '<select'+attributes+'>'+field.options.map(([key,title])=>'<option value="'+attr(key)+'"'+(String(key)===String(value)?' selected':'')+'>'+text(title)+'</option>').join('')+'</select>';
  return '<input type="'+(field.type==='text'?'text':'number')+'" step="any" value="'+attr(value??'')+'"'+attributes+(field.placeholder?' placeholder="'+attr(field.placeholder)+'"':'')+'/>';
}
