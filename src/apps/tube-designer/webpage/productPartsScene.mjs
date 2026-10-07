// Manufacturing parts and displayed assembly members have different identities.
// Always cross that boundary through the declared source members.
const PARAMETER_VISUAL = Object.freeze({ color: 0xa855f7, emissive: 0x6b21a8, emissiveIntensity: 0.78, linewidth: 2 });
const PART_HOVER_VISUAL = Object.freeze({ color: 0x39c9df, emissive: 0x087b95, emissiveIntensity: 0.5, linewidth: 2 });
const bindings = new WeakMap();
const text = value => String(value ?? "").trim();

export function activeProductManufacturingParts(view) {
  const designer = view.scene?.tubeDesigner ?? {};
  const productId = text(designer.product?.entityId ?? designer.activeProductId);
  const group = (designer.manufacturingGroups ?? []).find(item => text(item.productEntityId) === productId);
  return Array.isArray(designer.manufacturingGroups) ? group?.parts ?? [] : designer.parts ?? [];
}

export function productPartSceneMemberIds(view, part) {
  if (!part) return [];
  const sources = part.properties?.["manufacturing.sourceMembers"] ?? [];
  const stableKeys = new Set([
    ...(part.sourceStableKeys ?? []),
    ...sources.flatMap(source => [source.itemKey, ...(source.spans ?? []).map(span => span.itemKey)]),
  ].map(text).filter(Boolean));
  const sourceId = text(part.sourceMemberId);
  const partId = text(part.entityId);
  return [...new Set((view.scene?.tubeDesigner?.members ?? []).filter(member =>
    text(member.entityId) && (text(member.entityId) === sourceId
      || stableKeys.has(text(member.stableKey))
      || partId && text(member.manufacturingPartId) === partId))
    .map(member => text(member.entityId)))];
}

export function productPartsForSceneMember(view, memberId) {
  const id = text(memberId);
  return id ? activeProductManufacturingParts(view).filter(part => productPartSceneMemberIds(view, part).includes(id)) : [];
}

function rows(mount) {
  return [...(mount?.querySelectorAll?.("[data-tube-designer-product-part-row]") ?? [])];
}

function updateRows(mount, view) {
  const active = new Set(view.tubeDesignerActiveProductPartIds ?? []);
  if (view.tubeDesignerActivePartId) active.add(text(view.tubeDesignerActivePartId));
  const hovered = new Set(view.tubeDesignerHoveredProductPartIds ?? []);
  for (const row of rows(mount)) {
    const id = text(row.dataset.tubeDesignerProductPartRow), selected = active.has(id);
    row.classList.toggle("is-active", selected);
    row.classList.toggle("is-preview", hovered.has(id));
    row.setAttribute("aria-selected", String(selected));
  }
}

export function updateProductSceneEmphasis(view) {
  const parameterIds = view.tubeDesignerProductParameterHighlightIds ?? [];
  const parts = activeProductManufacturingParts(view);
  const partIds = view.tubeDesignerHoveredSceneMemberIds ?? [];
  if (partIds.length) {
    const active = new Set([...(view.tubeDesignerActiveProductPartIds ?? []), view.tubeDesignerActivePartId]
      .map(text).filter(Boolean));
    const selectedIds = new Set(parts.filter(item => active.has(text(item.entityId)))
      .flatMap(item => productPartSceneMemberIds(view, item)));
    if (view.selectedSceneObjectId) selectedIds.add(text(view.selectedSceneObjectId));
    // A hovered row may share source members with a selected part. Keep those
    // members selected while previewing only the other members in the hover color.
    view.viewport?.setEmphasizedObjectIds?.(partIds.filter(id => !selectedIds.has(id)), { visual: PART_HOVER_VISUAL });
  } else {
    view.viewport?.setEmphasizedObjectIds?.(parameterIds, { visual: PARAMETER_VISUAL });
  }
}

function applySelection(view, partIds, memberIds, primary = "") {
  view.tubeDesignerActiveProductPartIds = [...new Set(partIds.map(text).filter(Boolean))];
  view.tubeDesignerActivePartId = view.tubeDesignerActiveProductPartIds[0] ?? "";
  const ids = [...new Set(memberIds.map(text).filter(Boolean))];
  view.selectedSceneObjectId = ids.includes(primary) ? primary : ids[0] ?? "";
  if (view.viewport?.setSelectedObjectIds) view.viewport.setSelectedObjectIds(ids, view.selectedSceneObjectId);
  else view.viewport?.setSelectedObjectId?.(view.selectedSceneObjectId);
}

export function selectProductManufacturingPart(mount, view, part) {
  if (view.activeAreaId !== "view" || view.pending || view.tubeDesignerExportOperation || !part) return false;
  applySelection(view, [text(part.entityId)], productPartSceneMemberIds(view, part));
  updateRows(mount, view);
  updateProductSceneEmphasis(view);
  return true;
}

function revealRowInPartsTable(row) {
  const table = row?.closest?.(".tube-designer-product-parts-table");
  if (!table) return;
  const rect = table.getBoundingClientRect(), rowRect = row.getBoundingClientRect();
  const top = rect.top + (table.querySelector("thead")?.getBoundingClientRect().height ?? 0);
  if (rowRect.top < top) table.scrollTop += rowRect.top - top;
  else if (rowRect.bottom > rect.bottom) table.scrollTop += rowRect.bottom - rect.bottom;
}

export function selectProductSceneMember(mount, view, memberId, { reveal = false } = {}) {
  if (view.activeAreaId !== "view" || view.pending || view.tubeDesignerExportOperation) return false;
  const member = (view.scene?.tubeDesigner?.members ?? []).find(item => text(item.entityId) === text(memberId));
  if (text(memberId) && !member) return false;
  const parts = member ? productPartsForSceneMember(view, member.entityId) : [];
  const ids = parts.length ? parts.flatMap(part => productPartSceneMemberIds(view, part)) : member ? [member.entityId] : [];
  applySelection(view, parts.map(part => part.entityId), ids, text(member?.entityId));
  updateRows(mount, view);
  updateProductSceneEmphasis(view);
  if (reveal && parts.length) {
    const active = new Set(parts.map(part => text(part.entityId)));
    revealRowInPartsTable(rows(mount).find(row => active.has(text(row.dataset.tubeDesignerProductPartRow))));
  }
  return Boolean(member);
}

export function bindProductPartsScene(mount, view) {
  if (!mount?.addEventListener) return;
  let binding = bindings.get(mount);
  if (!binding) {
    binding = { view, identity: "", hovered: "", sceneHovered: "", sceneActive: false };
    bindings.set(mount, binding);
    const rowFor = target => target?.closest?.("[data-tube-designer-product-part-row]");
    const available = () => binding.view.activeAreaId === "view" && !binding.view.pending && !binding.view.tubeDesignerExportOperation;
    const preview = () => {
      const current = binding.view;
      const focused = rowFor(mount.ownerDocument.activeElement);
      const candidate = binding.hovered || text(focused?.dataset.tubeDesignerProductPartRow);
      const parts = activeProductManufacturingParts(current);
      const member = binding.sceneActive && available()
        ? (current.scene?.tubeDesigner?.members ?? []).find(item => text(item.entityId) === binding.sceneHovered) : null;
      const hoveredParts = !available() ? [] : binding.sceneActive
        ? productPartsForSceneMember(current, member?.entityId)
        : parts.filter(part => text(part.entityId) === candidate);
      current.tubeDesignerHoveredProductPartIds = hoveredParts.map(part => text(part.entityId));
      current.tubeDesignerHoveredProductPartId = current.tubeDesignerHoveredProductPartIds[0] ?? "";
      current.tubeDesignerHoveredSceneMemberIds = [...new Set(hoveredParts.length
        ? hoveredParts.flatMap(part => productPartSceneMemberIds(current, part))
        : member ? [text(member.entityId)] : [])];
      updateRows(mount, current); updateProductSceneEmphasis(current);
    };
    mount.addEventListener("pointerover", event => {
      const row = rowFor(event.target);
      if (!row || !mount.contains(row)) return;
      binding.sceneActive = false; binding.sceneHovered = "";
      binding.hovered = text(row.dataset.tubeDesignerProductPartRow); preview();
    });
    mount.addEventListener("pointerout", event => {
      const row = rowFor(event.target);
      if (!row || row.contains(event.relatedTarget)) return;
      binding.hovered = text(rowFor(event.relatedTarget)?.dataset.tubeDesignerProductPartRow); preview();
    });
    mount.addEventListener("focusin", event => {
      if (rowFor(event.target)) { binding.sceneActive = false; binding.sceneHovered = ""; }
      preview();
    });
    mount.addEventListener("focusout", () => queueMicrotask(preview));
    mount.addEventListener("keydown", event => {
      const row = rowFor(event.target);
      if (!available() || !row || event.target !== row || !["Enter", " "].includes(event.key)) return;
      event.preventDefault();
      const part = activeProductManufacturingParts(binding.view).find(item => text(item.entityId) === text(row.dataset.tubeDesignerProductPartRow));
      selectProductManufacturingPart(mount, binding.view, part);
    });
    binding.preview = preview;
  }
  binding.view = view;
  const designer = view.scene?.tubeDesigner ?? {};
  const identity = JSON.stringify([text(designer.product?.entityId ?? designer.activeProductId),
    text(designer.product?.activeGenerationRunId ?? designer.generationRun?.entityId)]);
  if (binding.identity && binding.identity !== identity) {
    binding.hovered = ""; binding.sceneHovered = ""; binding.sceneActive = false;
    view.tubeDesignerHoveredProductPartId = "";
    view.tubeDesignerHoveredProductPartIds = []; view.tubeDesignerHoveredSceneMemberIds = [];
    if (view.activeAreaId === "view") applySelection(view, [], []);
  }
  binding.identity = identity;
  if (view.activeAreaId !== "view") {
    binding.hovered = ""; binding.sceneHovered = ""; binding.sceneActive = false;
    view.tubeDesignerHoveredProductPartId = "";
    view.tubeDesignerHoveredProductPartIds = []; view.tubeDesignerHoveredSceneMemberIds = [];
    return;
  }
  const parts = activeProductManufacturingParts(view), partIds = new Set(parts.map(part => text(part.entityId)));
  const active = [...new Set([...(view.tubeDesignerActiveProductPartIds ?? []), view.tubeDesignerActivePartId].map(text).filter(id => partIds.has(id)))];
  if (active.length) {
    applySelection(view, active, parts.filter(part => active.includes(text(part.entityId))).flatMap(part => productPartSceneMemberIds(view, part)), text(view.selectedSceneObjectId));
  } else if (view.tubeDesignerActivePartId || view.tubeDesignerActiveProductPartIds?.length) {
    applySelection(view, [], []);
  }
  if (!rows(mount).some(row => text(row.dataset.tubeDesignerProductPartRow) === binding.hovered)) binding.hovered = "";
  binding.preview();
}

export function hoverProductSceneMember(mount, view, memberId, { active = true } = {}) {
  if (!bindings.has(mount)) bindProductPartsScene(mount, view);
  const binding = bindings.get(mount);
  if (!binding) return false;
  binding.view = view;
  const sceneActive = active && view.activeAreaId === "view";
  const sceneHovered = sceneActive && !view.pending && !view.tubeDesignerExportOperation ? text(memberId) : "";
  if (binding.sceneActive === sceneActive && binding.sceneHovered === sceneHovered) return sceneActive;
  binding.sceneActive = sceneActive; binding.sceneHovered = sceneHovered;
  binding.preview();
  return sceneActive;
}
