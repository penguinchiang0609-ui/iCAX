import { availableParameterChoices, parameterEnabled, parameterVisible } from "./parameterConditions.mjs";
import { createFinishedProduct, finishedProductInput, finishedProductShape, finishedProductShapes,
  finishedProductState } from "./finishedProductModel.mjs";
import { libraryProfiles, profileName, profileRef, profileScope, profileSelectionKey, profileSnapshot } from "./profileLibrary.mjs";
import { renderProfileParameterDiagram } from "./profileParameterDiagram.mjs";
import { renderProfileSvg } from "./profileSvg.mjs";

const text = (value) => String(value ?? "").replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;");
const attr = (value) => text(value).replaceAll('"', "&quot;");
const label = (value, fallback) => typeof value === "object" && value
  ? value["zh-CN"] ?? value.zh ?? value.en ?? fallback : value ?? fallback;
const diagramKey = (profile, span) => JSON.stringify([span.profileRef, profile.packageDigest
  ?? profile.revision ?? profile.descriptor?.version ?? profile.version ?? "", span.parameters]);

function editorState(view) {
  const state = finishedProductState(view);
  state.disclosure ??= {};
  state.diagramCache ??= new Map();
  return state;
}

function field(definition, values, data, action, keyAttribute) {
  if (!parameterVisible(definition, values)) return "";
  const key = definition.key, value = values[key] ?? definition.defaultValue ?? "";
  const enabled = parameterEnabled(definition, values);
  const diagramAttribute = keyAttribute === "data-finished-parameter"
    ? "data-assembly-parameter-key" : "data-profile-parameter-key";
  const common = `data-cam-change-action="${action}" ${data} ${keyAttribute}="${attr(key)}" ${diagramAttribute}="${attr(key)}"${enabled ? "" : " disabled"}`;
  const name = text(label(definition.displayName ?? definition.name, key));
  if (definition.valueType === "boolean") return `<label class="tube-designer-field tube-designer-boolean-field"><span>${name}</span><input type="checkbox" ${common}${value ? " checked" : ""}></label>`;
  if (definition.valueType === "choice" || definition.options || definition.choices) {
    const options = availableParameterChoices(definition, values).map((option) => {
      const optionValue = typeof option === "object" ? option.value : option;
      return `<option value="${attr(optionValue)}"${String(optionValue) === String(value) ? " selected" : ""}>${text(label(typeof option === "object" ? option.label ?? option.displayName : option, optionValue))}</option>`;
    }).join("");
    return `<label class="tube-designer-field"><span>${name}</span><select ${common}>${options}</select></label>`;
  }
  return `<label class="tube-designer-field"><span>${name}${definition.unit ? `（${text(definition.unit)}）` : ""}</span><input type="${definition.valueType === "string" ? "text" : "number"}" value="${attr(value)}" ${common}${definition.min == null ? "" : ` min="${attr(definition.min)}"`}${definition.max == null ? "" : ` max="${attr(definition.max)}"`}${definition.step == null ? "" : ` step="${attr(definition.step)}"`}></label>`;
}

// Product fields, identity and drafts depend only on the product catalogue.
// A process may map these spans to its roles after receiving this input.
export function renderFinishedProductEditor(view) {
  const state = editorState(view), product = finishedProductInput(view);
  const shape = finishedProductShape(product.shapeId, view), profiles = libraryProfiles(view);
  const data = `data-finished-shape="${attr(shape.id)}"`;
  const members = shape.spans.map((definition) => {
    const span = product.spans[definition.id];
    const profile = profiles.find((item) => profileScope(item) === span.profileRef.scope
      && String(item.id) === String(span.profileRef.id));
    const selectedKey = profile ? profileSelectionKey(profile) : `${span.profileRef.scope}:${span.profileRef.id}`;
    const options = (profile ? "" : `<option value="${attr(selectedKey)}" selected>当前管型 · ${text(span.profileRef.id)}</option>`)
      + profiles.map((item) => `<option value="${attr(profileSelectionKey(item))}"${profileSelectionKey(item) === selectedKey ? " selected" : ""}>${text(profileName(item))} · ${profileScope(item) === "user" ? "我的" : "系统"}</option>`).join("");
    const definitions = profile?.descriptor?.parameters ?? [];
    const snapshot = state.diagramCache.get(diagramKey(profile ?? {}, span))?.snapshot ?? (profile && profileSnapshot(profile));
    const diagram = snapshot ? renderProfileParameterDiagram(snapshot, { definitions, parameters: span.parameters,
      compact: true, title: "截面参数示意图" }) || renderProfileSvg(snapshot) : "";
    const spanData = `${data} data-finished-span="${attr(definition.id)}"`;
    const fields = definitions.map((p) => field(p, span.parameters, spanData,
      "tube-designer-finished-profile-parameter-change", "data-finished-profile-parameter")).join("");
    return `<details class="tube-assembly-scene-member" id="finished-${attr(shape.id)}-${attr(definition.id)}" data-profile-parameter-scope ${spanData}${state.disclosure[shape.id]?.[definition.id] ? " open" : ""}><summary><strong>${text(definition.label)}</strong><small>${text(profile ? profileName(profile) : "当前管型")} · ${text(span.length)} mm</small></summary>${diagram ? `<div class="tube-assembly-scene-diagram">${diagram}</div>` : ""}<div class="tube-connection-library-parameter-grid"><label class="tube-designer-field"><span>管型</span><select data-cam-change-action="tube-designer-finished-profile-change" ${spanData}${profiles.length ? "" : " disabled"}>${options}</select></label><label class="tube-designer-field"><span>长度（mm）</span><input type="number" min="1" max="100000" step="1" value="${attr(span.length)}" data-cam-change-action="tube-designer-finished-length-change" ${spanData}></label>${fields}</div></details>`;
  }).join("");
  const fields = shape.parameters.map((p) => field(p, product.parameters, data,
    "tube-designer-finished-parameter-change", "data-finished-parameter")).join("");
  return `<section class="tube-connection-library-parameter-section basic tube-assembly-scene-section" data-tube-assembly-scene-parameters data-finished-product-editor data-assembly-parameter-scope data-parameter-diagram-owner="finished-product:${attr(shape.id)}"><header><strong>成品参数</strong><small>${text(shape.displayName)}</small><button type="button" data-cam-action="tube-designer-finished-reset">重置成品</button></header><div class="tube-connection-library-parameter-grid"><label class="tube-designer-field"><span>成品造型</span><select data-cam-change-action="tube-designer-finished-shape-change">${finishedProductShapes(view).map((item) => `<option value="${attr(item.id)}"${item.id === shape.id ? " selected" : ""}>${text(item.displayName)}</option>`).join("")}</select></label></div><div class="tube-assembly-scene-members">${members}</div>${fields ? `<div class="tube-connection-library-parameter-grid">${fields}</div>` : ""}</section>`;
}

function readValue(definition, target, values) {
  if (!parameterVisible(definition, values) || !parameterEnabled(definition, values)) return undefined;
  const choices = definition.valueType === "choice" || definition.options || definition.choices;
  const option = choices && availableParameterChoices(definition, values).find((item) =>
    String(typeof item === "object" ? item.value : item) === String(target.value));
  const value = definition.valueType === "boolean" ? !!target.checked
    : choices ? typeof option === "object" ? option?.value : option
    : definition.valueType === "string" ? String(target.value ?? "") : Number(target.value);
  if (value === undefined || (["number", "integer"].includes(definition.valueType)
      && (!Number.isFinite(value) || definition.valueType === "integer" && !Number.isInteger(value)
        || definition.min != null && value < definition.min || definition.max != null && value > definition.max))) return undefined;
  return value;
}

export function handleFinishedProductAction(view, action, target) {
  if (!action.startsWith("tube-designer-finished-")) return { handled: false };
  const state = editorState(view), product = finishedProductInput(view);
  if (action === "tube-designer-finished-reset") {
    state.drafts[product.shapeId] = createFinishedProduct(product.shapeId, view);
    return { handled: true, changed: true };
  }
  if (action === "tube-designer-finished-shape-change") {
    if (!finishedProductShape(String(target.value), view)) return { handled: true };
    state.selectedShapeId = String(target.value);
    finishedProductInput(view);
    return { handled: true, changed: true };
  }
  if (target.dataset.finishedShape !== product.shapeId) return { handled: true };
  const shape = finishedProductShape(product.shapeId, view);
  if (action === "tube-designer-finished-parameter-change") {
    const key = target.dataset.finishedParameter, definition = shape.parameters.find((p) => p.key === key);
    const value = definition && readValue(definition, target, product.parameters);
    if (value === undefined) return { handled: true };
    product.parameters[key] = value;
  } else {
    const span = product.spans[target.dataset.finishedSpan];
    if (!span) return { handled: true };
    if (action === "tube-designer-finished-length-change") {
      const length = Number(target.value);
      if (!Number.isFinite(length) || length < 1 || length > 100000) return { handled: true };
      span.length = length;
    } else if (action === "tube-designer-finished-profile-change") {
      const profile = libraryProfiles(view).find((item) => profileSelectionKey(item) === String(target.value));
      if (!profile) return { handled: true };
      span.profileRef = profileRef(profile);
      span.parameters = { ...(profile.defaultParameters ?? Object.fromEntries((profile.descriptor?.parameters ?? [])
        .map((p) => [p.key, p.defaultValue]))) };
    } else if (action === "tube-designer-finished-profile-parameter-change") {
      const profile = libraryProfiles(view).find((item) => profileScope(item) === span.profileRef.scope
        && String(item.id) === String(span.profileRef.id));
      const key = target.dataset.finishedProfileParameter;
      const definition = profile?.descriptor?.parameters?.find((p) => p.key === key);
      const value = definition && readValue(definition, target, span.parameters);
      if (value === undefined) return { handled: true };
      span.parameters[key] = value;
    } else return { handled: false };
  }
  return { handled: true, changed: true };
}

const bindings = new WeakMap(), bound = new WeakSet();
export function bindFinishedProductEditor(mount, context, view) {
  if (!mount?.addEventListener) return;
  bindings.set(mount, { context, view });
  const ensureDiagram = (card) => {
    const binding = bindings.get(mount), state = editorState(binding.view), product = finishedProductInput(binding.view);
    if (card.dataset.finishedShape !== product.shapeId) return;
    const id = card.dataset.finishedSpan, span = product.spans[id];
    if (!span || !card.open || typeof binding.context?.sceneProxy?.invoke !== "function") return;
    const profile = libraryProfiles(binding.view).find((item) => profileScope(item) === span.profileRef.scope
      && String(item.id) === String(span.profileRef.id));
    if (!profile?.descriptor?.parameters?.length) return;
    const key = diagramKey(profile, span);
    if (state.diagramCache.has(key)) return;
    state.diagramCache.set(key, {});
    if (state.diagramCache.size > 48) state.diagramCache.delete(state.diagramCache.keys().next().value);
    void binding.context.sceneProxy.invoke("TubeDesigner.EvaluateProfilePackage", {
      profileRef: structuredClone(span.profileRef), parameters: structuredClone(span.parameters),
    }, { timeoutMs: 120000 }).then((response) => {
      if (!response?.profile?.contours?.length) throw new Error("管型截面无有效轮廓");
      state.diagramCache.set(key, { snapshot: response.profile });
      const current = finishedProductInput(binding.view);
      if (binding.view.activeAreaId === "assemblies" && current.shapeId === product.shapeId
          && state.disclosure[current.shapeId]?.[id] && current.spans[id]
          && diagramKey(profile, current.spans[id]) === key) binding.view.tubeDesignerAssemblyLibraryRenderProject?.();
    }).catch(() => {});
  };
  if (!bound.has(mount)) {
    bound.add(mount);
    mount.addEventListener("toggle", (event) => {
      const card = event.target;
      if (!card?.matches?.("details[data-finished-shape][data-finished-span]")) return;
      const state = editorState(bindings.get(mount).view);
      (state.disclosure[card.dataset.finishedShape] ??= {})[card.dataset.finishedSpan] = card.open;
      ensureDiagram(card);
    }, true);
  }
  for (const card of mount.querySelectorAll("details[data-finished-span][open]")) ensureDiagram(card);
}
