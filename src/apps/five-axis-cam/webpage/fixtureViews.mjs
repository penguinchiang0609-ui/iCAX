import { escapeText as esc, escapeAttr as attr } from '../../_shared/workbench/utils/format.mjs';
import { button, section, empty, facts } from './workspaceControls.mjs';
import {preparationKind} from './workspaceModel.mjs';
import {fixtureIcon} from './fixtureIcons.mjs';
import { renderFixtureDesign, renderFixtureCutClearance, fixtureCutClearanceReady } from './fixtureDesign.mjs';
import { fixtures, selectedFixture, fixtureDraft } from './fixtureActions.mjs';
import {fixtureUnitDesign} from './fixtureQueries.mjs';
const field = (key,label,value,unit='') => `<label class="five-fixture-field" data-five-key="fixture-row-${key}"><span>${label}</span><input type="text" ${key==='name'?'':'inputmode="decimal"'} data-five-fixture-field="${key}" data-five-key="fixture-input-${key}" value="${attr(value)}" autocomplete="off" aria-label="${label}${unit?`（${unit}）`:''}"></label>`;
export function renderFixtureList(view,items=fixtures(view),tree=false) {
  const selected=tree&&preparationKind(view)!=='unit'?null:selectedFixture(view);
  const busy=view.pending||view.fixtureRequest;
  const icon=(f,action,title,path,disabled=false)=>`<button class="five-setup-icon" data-five-command="${tree?'five.preparation.unit-action:':'five.fixture.row.'}${action}:${attr(f.id)}" title="${title}" aria-label="${title}" ${busy||disabled?'disabled':''}><svg viewBox="0 0 24 24" aria-hidden="true"><path d="${path}"/></svg></button>`;
  return items.map(f=>`<div class="five-setup-row ${selected?.id===f.id?'selected':''} ${f.enabled===false?'is-disabled':''}" data-five-key="fixture-${attr(f.id)}"><button class="five-setup-select" data-five-command="${tree?'five.preparation.unit:':'five.fixture.select:'}${attr(f.id)}" aria-pressed="${selected?.id===f.id}" ${busy?'disabled':''}>${esc(f.name)}${f.enabled===false?'（已禁用）':''}</button><div class="five-setup-icons">${icon(f,'lock',f.locked?'解锁夹具单元':'锁定夹具单元',f.locked?'M7 10V7a5 5 0 0 1 10 0v3 M5 10h14v11H5z M12 14v3':'M7 10V7a5 5 0 0 1 9-3 M5 10h14v11H5z M12 14v3')}${icon(f,'enable',f.enabled===false?'启用夹具单元':'禁用夹具单元','M12 2v10 M6 5a9 9 0 1 0 12 0')}</div></div>`).join('')||empty('尚未添加夹具单元');

}
function renderUnitSnapshot(view,unit){
  const parts=unit.partIds.map(id=>view.scene.domain.fixtures.find(p=>p.id===id)).filter(Boolean);
  if(unit.kind!=='Generated')return section('unit-import-info','导入信息',facts([['来源',unit.kind==='Imported'?'导入模型':'组合夹具'],['文件',parts[0]?.name||unit.name],['部件数量',parts.length],['格式',({'cad.step':'STEP','cad.iges':'IGES'}[parts[0]?.importMetadata?.formatId]||'模型快照')]]));
  const design=fixtureUnitDesign(view,unit),recipe=design.recipe||{},assembly=design.assembly,rows=assembly?.parts||[],busy=view.pending||view.fixtureRequest;
  const labels={family:'板片方向',material:'板材',layout:'支撑布置',pattern:'填充图案',baseShape:'底座形状',sizing:'底座范围',thickness:'支撑厚度',baseThickness:'底座厚度',baseDrop:'底座下移',margin:'边界留边',gap:'非承托区避空',minimumWeb:'最小连接板带',spacing:'支撑间距',postWidth:'支柱宽度',areaWidth:'底座宽度',areaDepth:'底座深度',areaX:'底座中心 X',areaY:'底座中心 Y',jointMode:'连接方式',jointClearance:'配合间隙',tenonWidth:'榫宽',tenonPitch:'榫间距',jointWeb:'榫卯保留板带',windowMode:'通风过水孔',windowWeb:'开孔保留板带',openingReliefDepth:'孔边避空深度'};
  const enums={ParallelX:'沿 X 排列',ParallelY:'沿 Y 排列',CarbonSteel:'碳钢',StainlessSteel:'不锈钢',Aluminum:'铝合金',Automatic:'自动',Manual:'手动',Skeleton:'骨架支撑',Grid:'井字板架',Staggered:'梅花点阵',Parallel:'平行板',Rectangle:'矩形',Circle:'圆形',TabSlot:'榫卯插接',Weld:'定位拼焊'};
  if(recipe.pattern==='Skeleton')labels.spacing='最大承托跨度';
  if(recipe.supportCount!==undefined){delete labels.spacing;labels.supportCount='横向板数量';labels.connectionCount='纵向板数量';}
  const applies=key=>!(['family'].includes(key)&&recipe.layout==='Automatic'&&recipe.pattern!=='Parallel')&&!(['pattern','spacing'].includes(key)&&recipe.layout!=='Automatic')&&!(key==='postWidth'&&(recipe.layout!=='Automatic'||recipe.pattern!=='Staggered'))&&!(['areaWidth','areaDepth','areaX','areaY'].includes(key)&&recipe.sizing!=='Manual')&&!(key==='areaDepth'&&recipe.baseShape==='Circle')&&!(['jointClearance','tenonWidth','tenonPitch','jointWeb'].includes(key)&&recipe.jointMode!=='TabSlot')&&!(key==='windowWeb'&&recipe.windowMode!=='Automatic')&&!(['cutClearanceMm','beamRadiusMm'].includes(key)&&recipe.cutClearanceMode!=='Toolpath');
  const parameters=Object.entries(labels).filter(([key])=>recipe[key]!==undefined&&applies(key)).map(([key,label])=>[label,key==='cutClearanceMode'?({None:'待刀路生成',Toolpath:'按实际刀路'}[recipe[key]]??recipe[key]):key==='windowMode'?({None:'不开孔',Automatic:'自动开孔'}[recipe[key]]??recipe[key]):enums[recipe[key]]??recipe[key]]);parameters.push(['承托面',recipe.supportFaces?.length??recipe.supportFaceCount??0]);if(recipe.layout==='Manual')parameters.push(['板片位置',recipe.stations?.map(s=>s.coordinate).join('，')]);
  if(assembly?.layoutSummary){const l=assembly.layoutSummary;parameters.push(['横向板分布方向',l.axis],['实际最大间距',Number(l.maximumSpacingMm).toFixed(1)],['横向板位置',l.supportPositions.map(n=>Number(n).toFixed(1)).join('，')],['纵向板位置',l.connectionPositions.map(n=>Number(n).toFixed(1)).join('，')]);}
  if(recipe.windowMode==='Automatic'&&recipe.supportCount!==undefined)parameters.push(['连通孔数量',assembly?.windowCount??rows.reduce((n,p)=>n+(p.windowCount||0),0)]);
  const enabled=p=>p.enabled!==false&&view.scene.domain.fixtures.find(f=>f.id===p.partId)?.enabled!==false;
  const enabledRows=rows.filter(enabled),actionsDisabled=busy||unit.locked||unit.enabled===false?'disabled':'';
  const editing=rows.some(p=>p.partId===view.fiveFixtureSelectedPart);
  const mode=`<button type="button" class="five-plate-mode-toggle ${editing?'is-editing':''}" data-five-key="fixture-parts-mode" data-five-command="five.fixture.parts-${editing?'done':'edit'}" aria-label="${editing?'完成板件编辑':'编辑板件组成'}" aria-pressed="${editing}" title="${editing?'结束单板高亮，恢复整体显示（Esc）':'选择板件进行编辑'}" ${busy?'disabled':''}>${fixtureIcon(editing?'check':'edit')}${editing?'完成':'编辑'}</button>`;
  const action=(p,kind,label,icon)=>`<button type="button" class="five-plate-icon ${kind==='toggle'&&enabled(p)?'is-on':''}" data-five-command="five.fixture.part-${kind}:${attr(p.partId)}" title="${attr(label+' '+p.number)}" aria-label="${attr(label+' '+p.number)}" ${actionsDisabled}>${fixtureIcon(icon)}</button>`;
  const partRows=rows.map(p=>`<tr data-five-key="fixture-part-${attr(p.partId)}" data-five-part-select="${attr(p.partId)}" aria-selected="${view.fiveFixtureSelectedPart===p.partId}" class="${enabled(p)?'':'is-disabled'} ${view.fiveFixtureSelectedPart===p.partId?'is-selected':''}"><td>${button('five.fixture.part-select:'+p.partId,esc(p.number),`${busy?'disabled ':''}data-five-fixture-part="${attr(p.partId)}" aria-pressed="${view.fiveFixtureSelectedPart===p.partId}"`)}</td><td>${p.thickness}</td><td>${p.role==='Base'?'底板':p.role==='Connection'?'连接板':'承托板'}</td><td class="five-plate-row-actions">${action(p,'edit','编辑','edit')}${action(p,'toggle',enabled(p)?'禁用':'启用','power')}</td></tr>`).join('');
  return section('unit-generation-info','生成参数',facts(parameters)+button('five.fixture.edit:'+unit.id,'修改支撑参数',actionsDisabled),'尺寸 mm',{collapsible:true,identity:'unit-generation-'+unit.id})
    +renderFixtureCutClearance(assembly,{adopted:true,unitId:unit.id})
    +`<section class="five-section" data-five-key="unit-generated-parts"><header><h3>板件组成</h3>${mode}</header><p class="five-hint">已启用 ${enabledRows.length} / ${rows.length} 块板</p><table class="five-fixture-parts"><thead><tr><th>板件</th><th>厚度 mm</th><th>类型</th><th>操作</th></tr></thead><tbody>${partRows}</tbody></table><div class="five-fixture-library-actions">${button('five.fixture.design.breakdown','拆单',busy||!enabledRows.length||!fixtureCutClearanceReady(design)?'disabled':'')}</div></section>`;
}
export function renderFixtureEditor(view) {
  if(view.fiveFixtureDesignOpen)return renderFixtureDesign(view);
  const f=selectedFixture(view),d=fixtureDraft(view);
  const input=(...args)=>field(...args).replace('<input ',`<input ${f?.locked?'readonly':''} `);
  return `<div class="five-fixture-editor" data-five-key="fixture-editor"><header class="five-fixture-heading"><strong>夹具单元</strong></header>
    ${f?section('fixture-properties','当前单元',input('name','名称',d.name))
      +(f.kind==='Generated'?'':section('fixture-position','位置',`<div class="five-fixture-xyz">${['x','y','z'].map(k=>input(k,k.toUpperCase(),d[k],'mm')).join('')}</div>`,'mm · 装夹坐标系')
      +section('fixture-rotation','旋转',`<div class="five-fixture-xyz">${['rx','ry','rz'].map(k=>input(k,k.toUpperCase(),d[k],'°')).join('')}</div>`,'° · Rz × Ry × Rx'))
      +renderUnitSnapshot(view,f):empty('点击菜单栏“新增支撑”或“新增夹具”添加。')}
    </div>`;
}

