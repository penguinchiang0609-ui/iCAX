// Transient hierarchy projected from the Scene's groups and flat machining order.
export const cutGroups = view => view.scene?.domain?.setups?.find(s => s.id === view.scene.domain.activeSetupId)?.cutGroups || [];
export function buildCutTree(view, cuts = view.scene?.intentToolpaths || []) {
  const order = new Map((view.scene?.domain?.setups?.find(s => s.id === view.scene.domain.activeSetupId)?.definitionOrder || []).map((id, i) => [id, i]));
  cuts = [...cuts].sort((a, b) => (order.get(a.id) ?? Infinity) - (order.get(b.id) ?? Infinity) || Number(a.number) - Number(b.number));
  const nodes = new Map(), roots = [];
  for (const g of cutGroups(view)) nodes.set(`g:${g.id}`, { ...g, key:`g:${g.id}`, kind:'Group', parent:g.parentId ? `g:${g.parentId}` : '', children:[], ownEnabled:g.enabled !== false, ownLocked:!!g.locked });
  for (const c of cuts) nodes.set(`c:${c.id}`, { ...c, key:`c:${c.id}`, kind:'Cut', parent:c.groupId ? `g:${c.groupId}` : '', children:[], ownEnabled:c.ownEnabled ?? c.enabled !== false, ownLocked:c.ownLocked ?? !!c.locked });
  const attach = (node, seen = new Set()) => {
    if (seen.has(node.key)) throw Error('刀路分组存在循环。');
    seen.add(node.key);
    const parent = nodes.get(node.parent);
    if (parent) attach(parent, seen);
    const siblings = parent ? parent.children : roots;
    if (!siblings.includes(node)) siblings.push(node);
  };
  for (const c of cuts) attach(nodes.get(`c:${c.id}`));
  const visit = (node, parent) => {
    node.inheritedLocked = !!parent?.locked; node.inheritedDisabled = parent?.enabled === false;
    node.locked = node.ownLocked || node.inheritedLocked || !!node.layers?.locked; node.enabled = node.ownEnabled && !node.inheritedDisabled;
    for (const child of node.children) visit(child, node);
    node.leafIds = node.kind === 'Cut' ? [node.id] : node.children.flatMap(c => c.leafIds);
    node.containsLocked = node.locked || node.children.some(c => c.containsLocked);
  };
  roots.forEach(n => visit(n));
  const walk = (items = roots) => items.flatMap(n => [n, ...walk(n.children)]);
  return { roots, nodes, walk, cuts };
}
export function selectedTreeNodes(tree, ui) {
  const leaves = new Set(ui.selectedIntentIds || [ui.selectedIntentId]);
  const groups = new Set(ui.selectedGroupIds || []), result = [];
  const visit = nodes => { for (const n of nodes) {
    if (n.kind === 'Group') {
      if (groups.has(n.id) && !n.containsLocked && n.leafIds.every(id => leaves.has(id))) result.push(n);
      else visit(n.children);
    } else if (!n.locked && leaves.has(n.id)) result.push(n);
  }};
  visit(tree.roots); return result;
}
// Sorting changes this sequence, never the user-authored name or fixed number.
export const cutSequenceNumbers=tree=>new Map(tree.cuts.filter(c=>c.enabled!==false).map((c,i)=>[c.id,String(i+1)]));
