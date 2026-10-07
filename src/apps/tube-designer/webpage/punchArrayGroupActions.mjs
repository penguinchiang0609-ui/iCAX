import { normalizePunchArrayGroup, parsePunchArraySkipText, createPunchArrayGroupsFromLayout } from "./punchArrayGroups.mjs";
import { checkpointPunchWizard, isPunchToolReadOnly } from "./punchWizard.mjs";

const actions=new Set(["array-mode-change","array-group-field","array-group-add","array-group-create","array-group-remove","array-skips-change"]);
const strings=new Set(["type","axis","direction","distributionMode","centerMode","fillAlign","spacingSequence","positionList","angleMode"]);
const numbers=new Set(["count","spacing","headMargin","tailMargin","centerOffset","centerFirstOffset","maxSpacing","startAngle","endAngle","angleStep"]);
const number=value=>value===""||value==null?NaN:Number(value);

/** Table columns edit directly; floating editors require their matching transaction. */
export function handlePunchArrayGroupAction(view, action, target) {
  if(!actions.has(action))return {handled:false,changed:false};
  const s=view?.tubeDesignerPunchWizard,editor=s?.parameterEditor;
  const row=String(target?.dataset?.tubeDesignerPunchIndex??"draft");
  const result={handled:true,changed:false,includeDraft:row==="draft"};
  const table=target?.dataset?.punchSheetArrayField!==undefined;
  if(!s||view.pending||(!table&&(!editor||editor.mode!=="arrays"||editor.end||editor.index!==row)))return result;
  const item=row==="draft"?s.draft:/^\d+$/.test(row)?s.features?.[Number(row)]:null;
  if(!item||isPunchToolReadOnly(s,item))return result;
  const normalized=Array.isArray(item.arrayGroups)?item:createPunchArrayGroupsFromLayout(item,s.baseLength);
  if(normalized.arrayGroupsError||!Array.isArray(normalized.arrayGroups))return result;
  const groups=normalized.arrayGroups,id=String(target?.dataset?.tubeDesignerPunchArrayGroup??"");
  const group=groups.find(candidate=>candidate.id===id);
  let change;
  if(action==="array-mode-change") {
    const dimensions={none:0,one:1,two:2}[String(target?.value)];
    if(dimensions===undefined)return result;
    change=()=>{
      while(groups.length<dimensions)groups.push(nextGroup(groups,"linear",groups.length?"Y":"X"));
      groups.forEach((candidate,index)=>candidate.enabled=index<dimensions);
    };
  } else if(action==="array-group-add"||action==="array-group-create") {
    const selected=action==='array-group-create'?String(target?.value??'').split(':'):[];
    const type=String(selected[0]??target?.dataset?.tubeDesignerPunchArrayType??"linear"),axis=String(selected[1]??target?.dataset?.tubeDesignerPunchArrayAxis??(type==="polar"?"X":"Y"));
    if(!["linear","polar"].includes(type)||!["X","Y","Z"].includes(axis)||groups.length>=1000)return result;
    const added=nextGroup(groups,type,axis);
    change=()=>{groups.push(added);delete item.arraySkipText;};
  } else if(action==="array-group-remove") {
    if(!group)return result;
    // A selector for a removed axis must not become a wildcard that suppresses
    // unrelated tools. Drop only selectors that depended on the removed group.
    change=()=>{item.arrayGroups=groups.filter(candidate=>candidate!==group);
      item.arraySkips=(item.arraySkips??[]).filter(pattern=>!Object.hasOwn(pattern,id));delete item.arraySkipText;};
  } else if(action==="array-skips-change") {
    const text=String(target?.value??"");
    let parsed,error="";try{parsed=parsePunchArraySkipText(text,groups);}catch(e){error=e.message;}
    change=()=>{if(parsed){item.arraySkips=parsed;delete item.arraySkipText;}else item.arraySkipText=text;if(editor)editor.error=error;else s.error=error;};
  } else {
    if(!group)return result;
    const field=String(target?.dataset?.tubeDesignerPunchArrayField??"");
    if(field==="type") {
      const type=String(target.value);
      if(!["linear","polar"].includes(type))return result;
      change=()=>{group.type=type;};
    }
    else if(field==="originAuto")change=()=>{group.origin=target.checked?null:group.origin??[Number(s.baseLength)/2,0,0];};
    else if(["originX","originY","originZ"].includes(field))change=()=>{
      group.origin=Array.isArray(group.origin)?[...group.origin]:[Number(s.baseLength)/2,0,0];
      group.origin[{originX:0,originY:1,originZ:2}[field]]=number(target.value);
    };
    else if(field==="enabled")change=()=>{group.enabled=!!target.checked;};
    else if(strings.has(field))change=()=>{group[field]=String(target.value);};
    else if(numbers.has(field))change=()=>{group[field]=field==="centerFirstOffset"&&String(target.value??"").trim()===""?null:number(target.value);};
    else return result;
  }
  checkpointPunchWizard(s);if(editor)editor.error="";
  if(normalized!==item)Object.assign(item,normalized);
  change();
  if(row!=="draft")s.selectedFeatureId=item.id;
  return {...result,changed:true};
}

function nextGroup(groups,type,axis) {
  let index=1;while(groups.some(candidate=>candidate.id===`array-group-${index}`))index++;
  return normalizePunchArrayGroup({id:`array-group-${index}`,type,axis,count:type==="polar"?4:2,spacing:50});
}
