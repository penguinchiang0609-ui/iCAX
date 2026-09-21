import { revealParameterControl } from './parameterPresentation.mjs';
// A diagram may live beside the viewport while its controls remain in a pane.
// Ownership is explicit and shared by profile/mould editors, never template IDs.
const bindings = new WeakMap();
const windowBindings = new WeakSet();
export function bindParameterDiagramScopes(mount, kind) {
  if(mount?.addEventListener && !windowBindings.has(mount)){
    windowBindings.add(mount);
    mount.addEventListener('parameter-diagram-window-updated',()=>{
      bindParameterDiagramScopes(mount,'tool');bindParameterDiagramScopes(mount,'profile');bindParameterDiagramScopes(mount,'assembly');
    });
  }
  const scopeSelector = `[data-${kind}-parameter-scope]`;
  const controlAttr = `data-${kind}-parameter-key`;
  const annotationAttr = `data-${kind}-annotation-key`;
  const selector = `[${annotationAttr}], [${controlAttr}]`;
  for (const scope of mount?.querySelectorAll?.(scopeSelector) ?? []) {
    if (bindings.has(scope)) { bindings.get(scope)(); continue; }
    const boundRoots = new WeakSet();
    const roots = () => {
      const owner = scope.dataset.parameterDiagramOwner;
      return [scope, ...[...mount.querySelectorAll('[data-parameter-diagram-for]')]
        .filter((node) => owner && node.dataset.parameterDiagramFor === owner)];
    };
    const owns = (node) => node && (node.closest(scopeSelector) === scope
      || roots().slice(1).some((root) => root.contains(node)));
    const nodes = () => roots().flatMap((root) => [...root.querySelectorAll(selector)]).filter(owns);
    const propagatedToggles = new WeakMap();
    const advancedDisclosures = () => roots().flatMap(root => [...root.querySelectorAll('[data-parameter-advanced]')]).filter(owns);
    const advancedDiagramItems = () => roots().flatMap(root => [...root.querySelectorAll('[data-parameter-level="advanced"]')]).filter(owns);
    const advancedDividers = () => roots().flatMap(root => [...root.querySelectorAll('[data-parameter-advanced-divider]')]).filter(owns);
    const setAdvanced = open => {
      scope.dataset.parameterAdvancedOpen = String(open);
      for (const root of roots()) root.dataset.parameterAdvancedOpen = String(open);
      for (const node of nodes()) if (node.hasAttribute(annotationAttr) && node.dataset.parameterLevel === 'advanced') node.style.display = open ? '' : 'none';
      for (const node of advancedDiagramItems()) node.style.display = open ? '' : 'none';
      for (const divider of advancedDividers()) divider.hidden = !open;
      for (const details of advancedDisclosures()) if (details.open !== open) {
        propagatedToggles.set(details,open); details.open = open;
      }
    };
    const keyFor = (target) => {
      const node = target?.closest?.(selector);
      return owns(node) ? node.getAttribute(annotationAttr) ?? node.getAttribute(controlAttr) : '';
    };
    const highlight = (key) => {
      for (const node of nodes()) {
        const active = !!key && (node.getAttribute(annotationAttr) ?? node.getAttribute(controlAttr)) === key;
        node.classList.toggle('is-active', active);
        if (node.hasAttribute(annotationAttr)) node.setAttribute('aria-pressed', String(active));
        if (node.hasAttribute(controlAttr)) node.closest('label')?.classList.toggle(`is-${kind}-parameter-active`, active);
      }
    };
    const restoreFocus = () => highlight(keyFor(scope.ownerDocument.activeElement));
    const activate = (event) => {
      const annotation = event.target?.closest?.(`[${annotationAttr}]`);
      if (!owns(annotation)) return;
      const key = annotation.getAttribute(annotationAttr);
      const input = nodes().find((node) => node.hasAttribute(controlAttr) && node.getAttribute(controlAttr) === key && !node.disabled);
      if (input) { revealParameterControl(input); input.focus({ preventScroll: true }); input.scrollIntoView?.({ block: 'nearest', behavior: 'smooth' }); }
      highlight(key);
    };
    const bindRoots = () => {
      for (const root of roots()) {
        if (boundRoots.has(root)) continue;
        boundRoots.add(root);
        root.addEventListener('focusin', (event) => highlight(keyFor(event.target)));
        root.addEventListener('focusout', () => queueMicrotask(restoreFocus));
        root.addEventListener('pointerover', (event) => { const key = keyFor(event.target); if (key) highlight(key); });
        root.addEventListener('pointerout', (event) => { if (!keyFor(event.relatedTarget)) restoreFocus(); });
        root.addEventListener('click', activate);
        root.addEventListener('toggle', event => {
          const details=event.target;
          if(!details.matches?.('[data-parameter-advanced]') || !owns(details))return;
          const propagated=propagatedToggles.has(details) && propagatedToggles.get(details)===details.open;
          propagatedToggles.delete(details);
          if(!propagated)setAdvanced(details.open);
        }, true);
        root.addEventListener('keydown', (event) => {
          if (!['Enter', ' '].includes(event.key) || event.target?.tagName?.toLowerCase() !== 'g') return;
          event.preventDefault(); activate(event);
        });
      }
      setAdvanced(scope.dataset.parameterAdvancedOpen === 'true' || advancedDisclosures().some(d => d.open));
      restoreFocus();
    };
    scope.dataset[`${kind}DiagramBound`] = 'true';
    bindings.set(scope, bindRoots);
    bindRoots();
  }
}
