import { createWindowStateMemory } from "../../../iCAX-UI/SDK/Forms/windowStateMemory.mjs";

const memory = createWindowStateMemory({ namespace: "icax.tube-designer.window-state" });
const WINDOW = "component-csg-create";
const textLimits = { name: 120, category: 160, material: 160, description: 2000 };
const scalar = value => typeof value === "boolean" || typeof value === "string" && value.length <= 2048
  || typeof value === "number" && Number.isFinite(value);
const scalars = source => Object.fromEntries(Object.entries(source ?? {}).filter(([key, value]) =>
  /^[A-Za-z][A-Za-z0-9_]{0,119}$/.test(key) && scalar(value)).slice(0, 128));
const numbers = (source, defaults) => Object.fromEntries(Object.entries(defaults ?? {}).map(([key, value]) =>
  [key, typeof source?.[key] === "number" && Number.isFinite(source[key]) ? source[key] : value]));
const profileInputs = profile => scalars(Array.isArray(profile.parameterDefinitions)
  ? Object.fromEntries(profile.parameterDefinitions.filter(field => !field.readOnly && field.presentation?.visible !== false)
    .filter(field => Object.hasOwn(profile.parameters ?? {}, field.key)).map(field => [field.key, profile.parameters[field.key]]))
  : {});

/** Only creation inputs are durable. Saved entities, resolved geometry and catalogue records are never copied. */
export function rememberComponentCreationDraft(draft, { primitives, storage = memory } = {}) {
  if (draft?.mode !== "create" || draft.id || !primitives) return false;
  const features = (draft.features ?? []).filter(feature => Object.hasOwn(primitives, feature.primitive)).slice(0, 64).map(feature => ({
    id: String(feature.id ?? "").slice(0, 200), name: String(feature.name ?? "").slice(0, 80), primitive: feature.primitive,
    operation: featuresOperation(1, feature.operation), parameters: numbers(feature.parameters, primitives[feature.primitive].parameters),
    transform: { position: numbers(feature.transform?.position, { x: 0, y: 0, z: 0 }),
      rotation: numbers(feature.transform?.rotation, { x: 0, y: 0, z: 0 }) },
    ...(feature.primitive === "extrusion" && feature.profile ? { profile: {
      key: String(feature.profile.key ?? "").slice(0, 512), parameters: profileInputs(feature.profile),
      ...(feature.profile.sourcePath ? { sourcePath: String(feature.profile.sourcePath).slice(0, 4096) } : {}),
    } } : {}),
  }));
  const metadata = Object.fromEntries(Object.entries(textLimits).map(([key, limit]) => [key, String(draft[key] ?? "").slice(0, limit)]));
  return storage.write(WINDOW, "inputs", { ...metadata, features, selectedId: String(draft.selectedId ?? ""),
    selectedNodeKind: draft.selectedNodeKind === "operation" ? "operation" : "primitive",
    tool: ["view", "move", "rotate", "scale"].includes(draft.tool) ? draft.tool : "view" });
}

/** Rebuild current profiles through the owning application; memory contains keys and parameters, not snapshots. */
export function restoreComponentCreationDraft(draft, { primitives, createFeature, resolveProfile, storage = memory } = {}) {
  if (draft?.mode !== "create" || draft.id || !primitives || !createFeature) return false;
  const saved = storage.read(WINDOW, "inputs", null);
  if (!saved || typeof saved !== "object" || Array.isArray(saved)) return false;
  for (const [key, limit] of Object.entries(textLimits)) {
    if (typeof saved[key] === "string") draft[key] = saved[key].slice(0, limit);
  }
  const ids = new Set();
  const features = (Array.isArray(saved.features) ? saved.features : []).slice(0, 64).flatMap((input, index) => {
    if (!input || !Object.hasOwn(primitives, input.primitive) || typeof input.id !== "string" || !input.id || input.id.length > 200 || ids.has(input.id)) return [];
    ids.add(input.id);
    const feature = createFeature(input.primitive, index);
    feature.id = input.id;
    if (typeof input.name === "string") feature.name = input.name.slice(0, 80);
    feature.operation = featuresOperation(index, input.operation);
    feature.parameters = numbers(input.parameters, feature.parameters);
    feature.transform = { position: numbers(input.transform?.position, feature.transform.position),
      rotation: numbers(input.transform?.rotation, feature.transform.rotation) };
    if (input.primitive === "extrusion" && typeof input.profile?.key === "string") {
      const profile = resolveProfile?.(input.profile.key, scalars(input.profile.parameters), input.profile.sourcePath);
      if (profile) feature.profile = profile;
    }
    return [feature];
  });
  if (features.length) {
    features[0].operation = "base";
    draft.features = features;
    draft.selectedId = features.some(feature => feature.id === saved.selectedId) ? saved.selectedId : features[0].id;
    draft.selectedNodeKind = saved.selectedNodeKind === "operation" && draft.selectedId !== features[0].id ? "operation" : "primitive";
  }
  if (["view", "move", "rotate", "scale"].includes(saved.tool)) draft.tool = saved.tool;
  return true;
}

function featuresOperation(index, operation) {
  return index === 0 ? "base" : ["union", "difference", "intersection"].includes(operation) ? operation : "union";
}
