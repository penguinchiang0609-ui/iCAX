// The 3D editor owns a local DOM boundary. Parameter edits never remount the
// surrounding project or the native-resource viewport inside this boundary.
const sessions = new WeakMap();

export function rememberPartDrawingDom(view, mount) {
  if (mount && view.tubeDesignerPartDrawing) sessions.set(mount, view.tubeDesignerPartDrawing);
  else if (mount) sessions.delete(mount);
}

function identity(node) {
  if (node.nodeType !== 1) return null;
  if (node.hasAttribute("data-part-drawing-viewport")) return "drawing-viewport";
  if (node.tagName === "DETAILS") return "details:" + node.querySelector("summary")?.textContent;
  if (node.matches("input,select,textarea,button")) return node.tagName + ":" + JSON.stringify(
    [...node.attributes].filter(a => a.name.startsWith("data-")).map(a => [a.name, a.value]).sort());
  if (node.id) return node.tagName + "#" + node.id;
  return null;
}

function compatible(current, next) {
  return current.nodeType === next.nodeType && (current.nodeType !== 1
    || current.tagName === next.tagName && identity(current) === identity(next));
}

function reconcile(current, next) {
  if (current.nodeType !== 1) {
    if (current.nodeValue !== next.nodeValue) current.nodeValue = next.nodeValue;
    return;
  }
  // Geometry, camera, progress and navigation belong to the dedicated renderer.
  if (current.hasAttribute("data-part-drawing-viewport")) return;
  const control = current.matches("input,select,textarea");
  const focused = control && current.ownerDocument.activeElement === current;
  const value = focused ? current.value : next.value;
  const checked = focused ? current.checked : next.checked;
  const preserve = name => current.tagName === "DETAILS" && name === "open"
    || name === "data-drawing-attached";
  for (const attr of [...current.attributes]) {
    if (!preserve(attr.name) && !next.hasAttribute(attr.name)) current.removeAttribute(attr.name);
  }
  for (const attr of next.attributes) {
    if (!preserve(attr.name) && current.getAttribute(attr.name) !== attr.value) current.setAttribute(attr.name, attr.value);
  }
  const remaining = [...current.childNodes];
  let cursor = current.firstChild;
  for (const child of [...next.childNodes]) {
    const key = identity(child);
    const match = key === null ? remaining.find(item => item === cursor && compatible(item, child))
      : remaining.find(item => identity(item) === key && compatible(item, child));
    if (!match) { current.insertBefore(child, cursor); continue; }
    remaining.splice(remaining.indexOf(match), 1);
    if (match !== cursor) current.insertBefore(match, cursor);
    reconcile(match, child);
    cursor = match.nextSibling;
  }
  for (const child of remaining) child.remove();
  if (control) {
    if (current.value !== value) current.value = value;
    if (current.tagName === "INPUT" && current.checked !== checked) current.checked = checked;
  }
}

export function patchPartDrawingDom(view, mount, html) {
  const editor = view.tubeDesignerPartDrawing;
  const current = mount?.querySelector?.(".td-draw-backdrop");
  if (!editor || sessions.get(mount) !== editor || !current || view.activeAreaId !== "nesting") return false;
  const template = mount.ownerDocument.createElement("template");
  template.innerHTML = html;
  const next = template.content.querySelector(".td-draw-backdrop");
  if (!next) return false;
  reconcile(current, next);
  const oldProgress = mount.querySelector("[data-tube-designer-operation-wait]");
  const newProgress = template.content.querySelector("[data-tube-designer-operation-wait]");
  if (oldProgress && newProgress) reconcile(oldProgress, newProgress);
  else if (oldProgress) oldProgress.remove();
  else if (newProgress) current.parentNode.append(newProgress);
  return true;
}
