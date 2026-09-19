// Presentation belongs to the parameter descriptor, not to its name or type.
export function isAdvancedParameter(definition) {
  return definition?.presentation?.advanced === true;
}

export function parameterDiagramLevelAttribute(definition) {
  return isAdvancedParameter(definition) ? ' data-parameter-level="advanced" style="display:none"' : '';
}

const attr = value => String(value ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

/** Keep every control/value in the DOM. Collapsing is not applicability. */
export function renderParameterLevels(definitions, render, { key, gridClass = '', open = false, trackDisclosure = false, disclosureAttribute = trackDisclosure ? 'data-tube-designer-parameter-group' : '', flattenAdvanced = false } = {}) {
  definitions = definitions.filter(d => d?.presentation?.visible !== false);
  const regular = definitions.filter(d => !isAdvancedParameter(d)).map(render).join('');
  const advanced = definitions.filter(isAdvancedParameter);
  if (!advanced.length) return regular;
  if (flattenAdvanced) {
    return `${regular}${advanced.map(d => `<div class="tube-parameter-advanced-item" data-parameter-level="advanced" data-parameter-advanced-item="${attr(d.key ?? d.name)}">${render(d)}</div>`).join('')}`;
  }
  const groupKey = `advanced:${key ?? definitions.map(d => d.key ?? d.name).join(':')}`;
  return `${regular}<details class="tube-parameter-advanced wide is-line-full" data-parameter-advanced data-parameter-advanced-key="${attr(groupKey)}" ${disclosureAttribute ? `${attr(disclosureAttribute)}="${attr(groupKey)}"` : ''} ${open ? 'open' : ''}><summary>高级设置 <small>${advanced.length} 项</small></summary><div class="${attr(gridClass)}">${advanced.map(d => `<div class="tube-parameter-advanced-item" data-parameter-level="advanced" data-parameter-advanced-item="${attr(d.key ?? d.name)}">${render(d)}</div>`).join('')}</div></details>`;
}

/** Explicit diagram navigation must reveal the target before focusing it. */
export function revealParameterControl(control) {
  for (let node = control?.parentElement; node; node = node.parentElement) {
    if (node.tagName === 'DETAILS') node.open = true;
  }
}
