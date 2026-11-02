import { patchDomNode } from "./punchDomPatch.mjs";
import { capturePaneInteraction } from "../../_shared/workbench/utils/paneInteractionState.mjs";

const rendered = new WeakMap();
const dialogSelectors = ["[data-machining-source-modal]", "[data-tube-designer-operation-wait]"];
function parse(mount, html) {
  const template = mount.ownerDocument.createElement("template");
  template.innerHTML = html;
  return template.content;
}
function otherDialogs(mount, suffix) {
  const content = parse(mount, suffix);
  for (const selector of dialogSelectors) content.querySelector(selector)?.remove();
  const container = mount.ownerDocument.createElement("div");
  container.append(content);
  return container.innerHTML;
}
export function rememberMachiningDom(view, mount, suffix, sceneProxy) {
  if (!mount?.querySelector) return;
  rendered.set(mount, { view, area: view.activeAreaId, sceneProxy, dialogs: otherDialogs(mount, suffix) });
}
export function patchMachiningDom(view, mount, { left, right, overlay, workflow, suffix, sceneProxy }) {
  const previous = rendered.get(mount);
  if (view.activeAreaId !== "machining" || previous?.view !== view || previous.area !== "machining"
      || previous.sceneProxy !== sceneProxy || previous.dialogs !== otherDialogs(mount, suffix)) return false;
  const workspace = mount.querySelector(".tube-designer-machining-workspace");
  if (!workspace) return false;
  const targets = [[".cam-context-pane", left], [".cam-info-pane", right],
    ["[data-tube-machining-workflow]", workflow], ["[data-tube-machining-overlay]", overlay]];
  if (targets.some(([selector]) => !workspace.querySelector(selector))) return false;
  // Capture the latest state at the patch boundary, including scrolling while
  // a native response was pending. The renderer and its canvas remain intact.
  const restore = capturePaneInteraction(mount);
  for (const [selector, html] of targets) {
    const current = workspace.querySelector(selector);
    const content = parse(mount, html);
    const next = selector.startsWith(".cam-") ? current.cloneNode(false) : content.querySelector(selector);
    if (selector.startsWith(".cam-")) next.append(content);
    if (next) patchDomNode(current, next);
  }
  const dialogs = parse(mount, suffix);
  for (const selector of dialogSelectors) {
    const current = workspace.querySelector(selector), next = dialogs.querySelector(selector);
    if (current && next) patchDomNode(current, next);
    else if (current) current.remove();
    else if (next) workspace.append(next);
  }
  rememberMachiningDom(view, mount, suffix, sceneProxy);
  restore();
  return true;
}
