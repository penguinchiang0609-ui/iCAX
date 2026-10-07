import { capturePaneInteraction } from "../../_shared/workbench/utils/paneInteractionState.mjs";

const bindings = new WeakMap();
const splitterWidth = 6;
const minCenterWidth = 280;
const panes = [".td-draw-sidebar", ".td-draw-property-scroll"];

function widths(body) {
  return { leftWidth: body.querySelector(".td-draw-sidebar").getBoundingClientRect().width,
    rightWidth: body.querySelector(".td-draw-parameters").getBoundingClientRect().width };
}

function bounds(body, layout) {
  const available = Math.max(0, body.clientWidth - splitterWidth * 2);
  const leftMin = Math.min(140, available * .2);
  const rightMin = Math.min(280, available * .4);
  const centerMin = Math.min(minCenterWidth, available - leftMin - rightMin);
  return { available, leftMin, rightMin, centerMin,
    leftMax: available - centerMin - layout.rightWidth,
    rightMax: available - centerMin - layout.leftWidth };
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(Number(value) || min, Math.max(min, max)));
}

function normalize(body, requested) {
  const result = { ...requested }, limits = bounds(body, result);
  result.leftWidth = clamp(result.leftWidth, limits.leftMin,
    limits.available - limits.centerMin - limits.rightMin);
  result.rightWidth = clamp(result.rightWidth, limits.rightMin,
    limits.available - limits.centerMin - result.leftWidth);
  return result;
}

export function renderPartDrawingPaneStyle(drawing) {
  const layout = drawing?.layout;
  return ["left", "right"].flatMap(side => {
    const value = Number(layout?.[side + "Width"]);
    return Number.isFinite(value) && value > 0 ? [`--td-draw-${side}:${value}px`] : [];
  }).join(";");
}

export function disposePartDrawingLayout(mount) {
  const binding = bindings.get(mount);
  if (!binding) return;
  binding.drag?.end({ pointerId: binding.drag.pointerId });
  binding.abort.abort(); binding.observer?.disconnect(); bindings.delete(mount);
}

export function bindPartDrawingLayout(view, mount, options = {}) {
  const drawing = view?.tubeDesignerPartDrawing;
  const body = mount?.querySelector?.(".td-draw-body");
  if (!drawing || !body || view.activeAreaId !== "nesting") {
    disposePartDrawingLayout(mount); return;
  }
  let binding = bindings.get(mount);
  if (binding?.drawing !== drawing || binding.body !== body) {
    disposePartDrawingLayout(mount);
    const win = body.ownerDocument.defaultView;
    const abort = new win.AbortController();
    binding = { drawing, body, view, mount, abort, width: body.clientWidth, resizeViewport: null };
    bindings.set(mount, binding);
    const owned = () => bindings.get(mount) === binding && binding.view.activeAreaId === "nesting" && body.isConnected
      && binding.view.tubeDesignerPartDrawing === drawing
      && mount.querySelector(".td-draw-body") === body;
    const apply = requested => {
      if (!owned()) return;
      const restore = capturePaneInteraction(body, panes);
      const next = normalize(body, requested);
      drawing.layout = next;
      body.style.setProperty("--td-draw-left", `${next.leftWidth}px`);
      body.style.setProperty("--td-draw-right", `${next.rightWidth}px`);
      const limits = bounds(body, next);
      for (const separator of body.querySelectorAll("[data-drawing-splitter]")) {
        const side = separator.dataset.drawingSplitter;
        separator.setAttribute("aria-valuemin", String(Math.round(limits[side + "Min"])));
        separator.setAttribute("aria-valuemax", String(Math.round(limits[side + "Max"])));
        separator.setAttribute("aria-valuenow", String(Math.round(next[side + "Width"])));
      }
      binding.resizeViewport?.();
      restore();
      body.querySelector(".td-draw-property-scroll")?._captureDrawingLayout?.();
    };
    binding.apply = apply;
    body.addEventListener("pointerdown", event => {
      const separator = event.target.closest?.("[data-drawing-splitter]");
      if (!separator || !body.contains(separator) || event.button !== 0 || !owned()) return;
      event.preventDefault(); event.stopPropagation();
      const side = separator.dataset.drawingSplitter, initial = widths(body), startX = event.clientX;
      separator.setPointerCapture(event.pointerId);
      separator.dataset.dragging = "true";
      const move = next => {
        if (next.pointerId !== event.pointerId || !owned()) return;
        next.preventDefault();
        const limits = bounds(body, initial);
        apply({ ...initial, [side + "Width"]: clamp(initial[side + "Width"]
          + (next.clientX - startX) * (side === "left" ? 1 : -1), limits[side + "Min"], limits[side + "Max"]) });
      };
      const end = next => {
        if (next.pointerId !== event.pointerId) return;
        separator.removeEventListener("pointermove", move);
        for (const name of ["pointerup", "pointercancel", "lostpointercapture"]) separator.removeEventListener(name, end);
        delete separator.dataset.dragging;
        if (binding.drag?.separator === separator) binding.drag = null;
        if (separator.hasPointerCapture(event.pointerId)) separator.releasePointerCapture(event.pointerId);
      };
      binding.drag = { separator, pointerId: event.pointerId, end };
      separator.addEventListener("pointermove", move, { signal: abort.signal });
      for (const name of ["pointerup", "pointercancel", "lostpointercapture"])
        separator.addEventListener(name, end, { signal: abort.signal });
    }, { signal: abort.signal });
    body.addEventListener("keydown", event => {
      const separator = event.target.closest?.("[data-drawing-splitter]");
      if (!separator || !owned() || !["ArrowLeft", "ArrowRight", "Home", "End"].includes(event.key)) return;
      event.preventDefault(); event.stopPropagation();
      const side = separator.dataset.drawingSplitter, current = widths(body), limits = bounds(body, current);
      const delta = (event.key === "ArrowRight" ? 1 : -1) * (side === "left" ? 1 : -1) * (event.shiftKey ? 36 : 12);
      apply({ ...current, [side + "Width"]: clamp(event.key === "Home" ? limits[side + "Min"]
        : event.key === "End" ? limits[side + "Max"] : current[side + "Width"] + delta,
      limits[side + "Min"], limits[side + "Max"]) });
    }, { signal: abort.signal });
    if (typeof win.ResizeObserver === "function") {
      binding.observer = new win.ResizeObserver(() => {
        if (!owned()) {
          binding.observer.disconnect(); binding.abort.abort();
          if (bindings.get(mount) === binding) disposePartDrawingLayout(mount);
          return;
        }
        if (binding.width === body.clientWidth) return;
        binding.width = body.clientWidth;
        if (drawing.layout) apply(drawing.layout);
      });
      binding.observer.observe(body);
    }
  }
  binding.view = view;
  if (options.resizeViewport) binding.resizeViewport = options.resizeViewport;
  // The widths live in the current drawing session, so parameter patches and
  // feature selection never reset the user's chosen pane boundaries.
  binding.apply(drawing.layout ?? widths(body));
}
