import { escapeText as esc, escapeAttr as attr } from '../../_shared/workbench/utils/format.mjs';
import { TOOLS } from './workspaceModel.mjs';
export const button = (command, label, extra = '') => `<button type="button" data-five-command="${attr(command)}" ${extra}>${esc(label)}</button>`;
export const tooltipAttributes = text => `data-toolbar-tooltip="${attr(text)}" title="${attr(text)}"`;
export const open = (id, label = TOOLS[id].title) => button(`five.tool.${id}`, label);
export const section = (id, title, content, meta = '', {collapsible=false,identity=id}={}) => {
  const heading=`<h3>${esc(title)}</h3><span>${esc(meta)}</span>`;
  return `<section class="five-section" data-five-key="${id}">${collapsible?`<details class="five-section-disclosure" data-five-key="${attr(identity)}"><summary>${heading}</summary>${content}</details>`:`<header>${heading}</header>${content}`}</section>`;
};
export const hint = text => `<p class="five-hint">${esc(text)}</p>`;
export const empty = text => `<div class="five-empty-row">${esc(text)}</div>`;
export const facts = rows => `<dl class="five-facts">${rows.map(([name, value]) => `<dt>${esc(name)}</dt><dd>${esc(value)}</dd>`).join('')}</dl>`;
export const badge = (label, tone = '') => `<span class="five-badge ${tone}">${esc(label)}</span>`;
export const disabled = label => button('five.unavailable', label, 'disabled title="操作能力待接入"');
export const choices = values => ({ choices: values });
export const steps = values => `<ol class="five-guide">${values.map(value => `<li>${esc(value)}</li>`).join('')}</ol>`;
export function field(draft, name, label, options = {}) {
  const id = `five-field-${name}`;
  const control = options.choices
    ? `<select id="${id}" data-five-field="${name}" data-five-key="${id}">${options.choices.map(([value, text]) => `<option value="${value}" ${draft[name] === value ? 'selected' : ''}>${esc(text)}</option>`).join('')}</select>`
    : `<input id="${id}" data-five-field="${name}" data-five-key="${id}" type="text" ${options.text ? '' : 'inputmode="decimal"'} ${options.disabled?'disabled':''} value="${attr(draft[name] ?? '')}" placeholder="${attr(options.placeholder ?? '')}" autocomplete="off">`;
  return `<div class="five-field" data-five-key="row-${name}"><label for="${id}">${esc(label)}</label>${control}</div>`;
}
