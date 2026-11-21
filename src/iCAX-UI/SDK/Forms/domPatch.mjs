export function createDomPatcher({ keyAttribute = "data-key" } = {}) {
  const key = node => node.nodeType === 1 ? node.getAttribute(keyAttribute) ?? node.id ?? "" : "";
  function patchNode(current, next) {
    if (current.nodeType !== next.nodeType || current.nodeName !== next.nodeName) { current.replaceWith(next.cloneNode(true));return; }
    if (current.nodeType !== 1) { if (current.nodeValue !== next.nodeValue) current.nodeValue = next.nodeValue;return; }
    for (const attribute of [...current.attributes]) if (!next.hasAttribute(attribute.name)) current.removeAttribute(attribute.name);
    for (const attribute of [...next.attributes]) if (current.getAttribute(attribute.name) !== attribute.value) current.setAttribute(attribute.name, attribute.value);
    if (["INPUT", "TEXTAREA"].includes(current.tagName)) {
      if (current.value !== next.value) current.value = next.value;
      if (current.tagName === "INPUT") current.checked = next.checked;
      return;
    }
    patchChildren(current, next);
    if (current.tagName === "SELECT" && current.value !== next.value) current.value = next.value;
  }
  function patchChildren(current, next) {
    const children = [...current.childNodes], keyed = new Map(children.filter(child => key(child)).map(child => [key(child), child])), retained = new Set();
    for (const [index, candidate] of [...next.childNodes].entries()) {
      const identity = key(candidate);let child = identity ? keyed.get(identity) : children[index];
      if (child && (retained.has(child) || key(child) !== identity || child.nodeType !== candidate.nodeType || child.nodeName !== candidate.nodeName)) child = null;
      if (!child) child = candidate.cloneNode(true);else patchNode(child, candidate);
      retained.add(child);if (current.childNodes[index] !== child) current.insertBefore(child, current.childNodes[index] ?? null);
    }
    for (const child of [...current.childNodes]) if (!retained.has(child)) child.remove();
  }
  return { patchNode, patchChildren };
}
