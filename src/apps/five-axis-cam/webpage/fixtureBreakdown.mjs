import {escapeText as esc,escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';
import {boundaryDisplay} from './fixturePlateSketch.mjs';
import {fixtureIcon} from './fixtureIcons.mjs';

function thumbnail(part){
  const points=part.outline,xs=points.map(p=>p[0]),ys=points.map(p=>p[1]),box=[Math.min(...xs),Math.min(...ys),Math.max(...xs),Math.max(...ys)],margin=Math.max(box[2]-box[0],box[3]-box[1])*.06;
  const path=loop=>loop.map((p,i)=>`${i?'L':'M'}${p[0]} ${-p[1]}`).join(' ')+' Z';
  const d=part.exactProfile?part.exactProfile.Boundaries.map(b=>boundaryDisplay(b,p=>[p[0],-p[1]])).join(' '):[path(points),...part.holes.map(path)].join(' ');
  return `<svg viewBox="${box[0]-margin} ${-box[3]-margin} ${box[2]-box[0]+margin*2} ${box[3]-box[1]+margin*2}" role="img" aria-label="${attr(part.number)} 示意图"><path d="${attr(d)}" fill-rule="evenodd"/></svg>`;
}
export function renderFixtureBreakdown(view){
  const dialog=view.fiveFixtureBreakdown;if(!dialog)return '';
  const unit=view.scene.domain.fixtureUnits.find(u=>u.id===dialog.unitId&&u.setupId===view.scene.domain.activeSetupId),list=unit?.design?.breakdown?{...unit.design.breakdown,parts:dialog.parts||[]}:null;
  if(!list)return '';
  const busy=view.pending||view.fixtureRequest,disabled=busy?'disabled':'',materials={CarbonSteel:'碳钢',StainlessSteel:'不锈钢',Aluminum:'铝合金'};
  return `<div class="five-fixture-breakdown-backdrop" data-five-key="fixture-breakdown-backdrop"><section class="five-fixture-breakdown" data-five-key="fixture-breakdown" role="dialog" aria-modal="true" aria-label="夹具板件清单"><header><div><strong>夹具板件清单</strong><span>${esc(unit.name)} · ${list.parts.length} 块板</span></div><button type="button" class="five-plate-icon" data-five-command="five.fixture.design.breakdown-close" title="关闭" aria-label="关闭板件清单" ${disabled}>${fixtureIcon('close')}</button></header><div class="five-fixture-breakdown-list"><table><thead><tr><th>示意图</th><th>名称</th><th>材料</th><th>厚度 mm</th><th>数量</th></tr></thead><tbody>${list.parts.map(p=>`<tr data-five-key="breakdown-part-${attr(p.partId)}"><td>${thumbnail(p)}</td><td><strong>${esc(p.number)}</strong><span>${p.role==='Base'?'底板':'支撑板'}</span></td><td>${esc(materials[p.material]||p.material)}</td><td>${esc(p.thickness)}</td><td>1</td></tr>`).join('')}</tbody></table></div><footer><span role="status">${view.error?esc(view.error):unit.design.delivery?`已导出：${esc(unit.design.delivery.directory)}`:'尺寸单位：mm'}</span><button type="button" data-five-command="five.fixture.design.breakdown-close" ${disabled}>关闭</button><button type="button" class="five-primary" data-five-command="five.fixture.design.export" ${disabled}>导出下料包</button></footer></section></div>`;
}
