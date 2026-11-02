import { escapeText as esc } from '../../_shared/workbench/utils/format.mjs';
import { button, field } from './workspaceControls.mjs';
import { registrationPreview, selectedPlacement, registrationPickOrder } from './registrationActions.mjs';

const number = value => Number.isFinite(Number(value)) && String(value).trim() ? Number(Number(value).toFixed(3)).toString() : '';
const captureModes = [['point', '捕捉点', '·'], ['center', '中心点', '⊙'], ['midpoint', '曲线中点', '╱'], ['endpoint', '端点', '⊸']];
export function renderRegistration(tool, d, view) {
  const fine = tool === 'fine', pick = view.fiveAxisRegistrationPick;
  const report = registrationPreview(view, d, tool)?.report;
  const ready = !!selectedPlacement(view), blocked = view.pending ? 'disabled' : '';
  const row = (prefix, label) => {
    const values = [...'XYZ'].map(axis => number(d[`${prefix}${axis}`]));
    const selected = values.every(Boolean);
    const reference = d.referenceResults?.[prefix];
    const vector = !fine && (d.roughMethod === 'line' && prefix !== 'origin' || d.roughMethod === 'face' && prefix === 'plane');
    const text = selected ? (vector && reference ? (reference.query.mode === 'plane' ? '平面已选' : '直线已选') : values.join(', ')) : vector ? (d.roughMethod === 'face' ? '请选择平面' : '请选择直线') : '请选择点';
    const active = pick?.prefix === prefix;
    return `<div class="five-reference-row ${vector ? 'has-direction' : ''}" data-five-key="reference-${prefix}"><span>${label}</span>${button(`five.reference.pick.${prefix}`, `${selected ? '✓ ' : ''}${text}`, `class="five-reference-value ${active ? 'is-active' : ''}" aria-label="选择${label}" aria-pressed="${active}" title="${esc(values.join(', '))}" ${!ready ? 'disabled' : blocked}`)}${vector ? button(`five.reference.flip.${prefix}`, '⇄', `class="five-reference-flip" aria-label="反向${label}" title="反向${label}" ${!selected ? 'disabled' : blocked}`) : ''}${button(`five.reference.clear.${prefix}`, '×', `class="five-reference-clear" aria-label="清除${label}" ${!selected ? 'disabled' : blocked}`)}</div>`;
  };
  const labels = { origin: '原点', axisX: d.roughMethod === 'line' ? 'X 轴直线' : 'X 轴点', plane: ({ point: 'XY 面内点', line: 'XY 面内直线', face: 'XY 平面' })[d.roughMethod] };
  const caption = fine ? '在模型上选择三点' : ({ point: '由3点建立坐标系', line: '由两根不平行直线建立坐标系', face: '以面的法线建立坐标系' })[d.roughMethod];
  return `<div class="five-position-dialog" data-five-key="registration-dialog" data-five-tool="${tool}" role="dialog" aria-label="${fine ? '实测校准' : '工件坐标系'}">
    <header class="five-position-title"><strong>${fine ? '实测校准' : '工件坐标系'}</strong>${button('five.registration.cancel', '×', `aria-label="关闭定位" ${blocked}`)}</header>
    <div class="five-position-body">
      <div class="five-position-label">点捕捉方式</div>
      <div class="five-capture-modes" role="group" aria-label="点捕捉方式">${captureModes.map(([id, label, icon]) => button(`five.reference.capture.${id}`, icon, `title="${label}" aria-label="${label}" aria-pressed="${d.capture === id}" ${blocked}`)).join('')}</div>
      <div class="five-position-label">${caption}</div>
      <div class="five-reference-list" data-reference-method="${fine ? 'fine' : d.roughMethod}" data-five-key="registration-references">${fine ? [1, 2, 3].map(n => row(`model${n}`, `P${n}`)).join('') : registrationPickOrder(d, tool).map(prefix => row(prefix, labels[prefix])).join('')}</div>
      ${fine ? `<div class="five-position-label">填入对应三点的坐标值 <small>mm</small></div><div class="five-measured-grid" data-five-key="registration-measured"><span></span><span class="axis-x">X</span><span class="axis-y">Y</span><span class="axis-z">Z</span>${[1, 2, 3].map(n => `<span>P${n}</span>${[...'XYZ'].map(axis => field(d, `measured${n}${axis}`, `P${n} ${axis}`)).join('')}`).join('')}</div>` : ''}
      ${report && !report.qualified ? '<div class="five-position-error" role="alert">点位误差超限，请检查对应点和实测坐标。</div>' : ''}
      ${view.error ? `<div class="five-position-error" role="alert">${esc(view.error)}</div>` : ''}
    </div>
    <footer class="five-position-actions">${button(`five.registration.confirm.${tool}`, d.registrationRequest ? '核对结果' : '确定', `class="five-primary" ${!ready ? 'disabled' : blocked}`)}${button('five.registration.cancel', '取消', blocked)}</footer>
  </div>`;
}
