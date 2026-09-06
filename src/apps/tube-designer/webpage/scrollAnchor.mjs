const ANCHOR_ATTRIBUTES = Object.freeze([
  "data-tube-designer-parameter",
  "data-tube-designer-template-group-id",
  "data-tube-designer-template-id",
  "data-tube-designer-product-id",
  "data-tube-designer-category-id",
  "data-tube-designer-inspect-part-id",
  "data-tube-designer-preset-selection",
]);

export function captureScrollAnchor(scroller, target = null) {
  if (!scroller) return null;
  const activeElement = scroller.ownerDocument?.activeElement ?? null;
  const candidate = contains(scroller, activeElement) ? activeElement : target;
  const locator = locateStableElement(candidate);
  const scrollerTop = topOf(scroller);
  const anchorTop = topOf(locator?.element);
  return {
    scrollTop: finiteNumber(scroller.scrollTop),
    attribute: locator?.attribute ?? "",
    value: locator?.value ?? "",
    viewportOffset: anchorTop == null || scrollerTop == null ? null : anchorTop - scrollerTop,
    restoreFocus: Boolean(locator?.element && contains(locator.element, activeElement)),
  };
}

export function restoreScrollAnchor(scroller, anchor, options = {}) {
  if (!scroller || !anchor) return null;
  scroller.scrollTop = finiteNumber(anchor.scrollTop);
  const element = findStableElement(scroller, anchor.attribute, anchor.value);
  const scrollerTop = topOf(scroller);
  const elementTop = topOf(element);
  if (elementTop != null && scrollerTop != null
    && anchor.viewportOffset != null && Number.isFinite(Number(anchor.viewportOffset))) {
    scroller.scrollTop += elementTop - scrollerTop - Number(anchor.viewportOffset);
  }
  const shouldRestoreFocus = options.restoreFocus ?? anchor.restoreFocus;
  if (shouldRestoreFocus) element?.focus?.({ preventScroll: true });
  return element;
}

function locateStableElement(candidate) {
  if (!candidate) return null;
  for (const attribute of ANCHOR_ATTRIBUTES) {
    const element = candidate.closest?.(`[${attribute}]`) ?? null;
    const value = String(element?.getAttribute?.(attribute) ?? "");
    if (element && value) return { element, attribute, value };
  }
  return null;
}

function findStableElement(scroller, attribute, value) {
  if (!ANCHOR_ATTRIBUTES.includes(attribute) || !value) return null;
  return Array.from(scroller.querySelectorAll?.(`[${attribute}]`) ?? [])
    .find((element) => String(element.getAttribute?.(attribute) ?? "") === String(value)) ?? null;
}

function contains(container, element) {
  if (!container || !element) return false;
  return container === element || Boolean(container.contains?.(element));
}

function topOf(element) {
  if (!element?.getBoundingClientRect) return null;
  const top = Number(element.getBoundingClientRect().top);
  return Number.isFinite(top) ? top : null;
}

function finiteNumber(value) {
  const number = Number(value);
  return Number.isFinite(number) ? number : 0;
}
