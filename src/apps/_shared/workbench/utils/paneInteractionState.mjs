// Capture immediately before DOM replacement, never when an async request starts.
const panes = [".cam-context-pane", ".cam-info-pane"];
function identity(node) {
  if (node.id) return "id:" + node.id;
  const attributes = [...node.attributes].filter(a =>
    a.name.startsWith("data-") || ["name", "type", "aria-label"].includes(a.name));
  return node.tagName + JSON.stringify(attributes.map(a=>[a.name,a.value]).sort());
}
function locate(root, saved) {
  if (saved.node.isConnected && root.contains(saved.node)) return saved.node;
  const matches = [...root.querySelectorAll("*")].filter(n=>identity(n)===saved.key);
  return matches.length === 1 ? matches[0] : null;
}
export function capturePaneInteraction(mount) {
  const active = mount.ownerDocument?.activeElement;
  const snapshots = panes.map(selector => {
    const root=mount.querySelector(selector);
    if (!root) return null;
    const scrolls=[root,...root.querySelectorAll("*")].filter(n=>n.scrollTop || n.scrollLeft)
      .map(node=>({node,key:identity(node),classes:node.className,top:node.scrollTop,left:node.scrollLeft}));
    const focused=root.contains(active) ? {node:active,key:identity(active),
      value:active.value,start:active.selectionStart,end:active.selectionEnd,direction:active.selectionDirection} : null;
    return {selector,scrolls,focused};
  });
  return () => {
    for(const saved of snapshots) {
      const root=saved && mount.querySelector(saved.selector);
      if(!root)continue;
      const focus=saved.focused && locate(root,saved.focused);
      if(focus && !focus.disabled) {
        // Keep in-progress text, but don't revert checkbox/select model changes.
        if(focus.tagName==="TEXTAREA" || focus.tagName==="INPUT" &&
          !["checkbox","radio","button","submit","range","file"].includes(focus.type)) {
          focus.value=saved.focused.value;
        }
        focus.focus({preventScroll:true});
        if(saved.focused.start!=null && typeof focus.setSelectionRange==="function") {
          try { focus.setSelectionRange(saved.focused.start,saved.focused.end,saved.focused.direction); } catch {}
        }
      }
      for(const scroll of saved.scrolls) {
        let target=scroll.node===root ? root : locate(root,scroll);
        // Scroll wrappers often have no semantic attributes; a unique class
        // is safer than restoring by child index after conditional fields change.
        if(!target && scroll.classes) {
          const candidates=[root,...root.querySelectorAll("*")].filter(n=>n.className===scroll.classes);
          if(candidates.length===1)target=candidates[0];
        }
        if(target){target.scrollTop=scroll.top;target.scrollLeft=scroll.left;}
      }
    }
  };
}
