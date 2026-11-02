import { capturePaneInteraction } from '../../_shared/workbench/utils/paneInteractionState.mjs';

// Only HTML panels are patched. Canvas, camera and render resources belong to the SDK.
const key = node => node.nodeType === 1 ? (node.getAttribute('data-five-key') ?? (node.id || null)) : null;
const compatible = (a, b) => a.nodeType === b.nodeType && a.nodeName === b.nodeName && key(a) === key(b);
function patch(current, next) {
  if (current.nodeType === 3) { if (current.data !== next.data) current.data = next.data; return; }
  if (current.nodeType !== 1) return;
  const focused = current === current.ownerDocument.activeElement;
  const disclosureOpen = current.tagName === 'DETAILS' ? current.open : null;
  const control = ['INPUT', 'SELECT', 'TEXTAREA'].includes(current.tagName);
  const desiredValue = control ? (focused && next.getAttribute('data-model-value-authoritative')!=='true' ? current.value : next.value) : null;
  const desiredChecked = next.checked;
  for (const attr of [...current.attributes]) if (!next.hasAttribute(attr.name)) current.removeAttribute(attr.name);
  for (const attr of [...next.attributes]) if (current.getAttribute(attr.name) !== attr.value) current.setAttribute(attr.name, attr.value);
  const available = [...current.childNodes];
  let cursor = current.firstChild;
  for (const child of [...next.childNodes]) {
    const found = key(child) ? available.find(old => compatible(old, child)) : available.find(old => old === cursor && compatible(old, child));
    if (found) {
      available.splice(available.indexOf(found), 1);
      if (found !== cursor) current.insertBefore(found, cursor);
      patch(found, child); cursor = found.nextSibling;
    } else current.insertBefore(child, cursor);
  }
  available.forEach(node => node.remove());
  if (disclosureOpen !== null) current.open = disclosureOpen;
  if (control) {
    if (current.value !== desiredValue) current.value = desiredValue;
    if (current.tagName === 'INPUT') current.checked = desiredChecked;
  }
}
export function patchWorkspacePanels(mount, sections) {
  if (!sections.every(([selector]) => mount.querySelector(selector))) return false;
  const restore = capturePaneInteraction(mount, ['.cam-context-pane', '.cam-info-pane', '[data-five-dock]', '[data-five-overlay]', '[data-five-alert-host]', '.five-layer-swatches', '.five-layer-edit-list']);
  for (const [selector, html] of sections) {
    const node = mount.querySelector(selector), next = node.cloneNode(false);
    next.innerHTML = html;
    patch(node, next);
  }
  restore();
  return true;
}
