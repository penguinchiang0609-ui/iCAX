import { assemblyProcessWithInput } from "./finishedProductModel.mjs";

export function normalizeAssemblyCatalogue(items, view = null) {
  const result = [];
  const seen = new Set();
  for (const value of Array.isArray(items) ? items : []) {
    if (!value || value.schema !== "icax.assembly-template" || Number(value.schemaVersion) !== 1) continue;
    if (value.catalogueHidden === true) continue;
    const id = String(value.id ?? "").trim();
    if (!id || seen.has(id) || !Array.isArray(value.participants)
        || value.participants.length < 2 || value.participants.length > 4) continue;
    const blankParts = value.manufacturingPlan?.blankParts;
    if (!Array.isArray(blankParts) || !blankParts.length) continue;
    seen.add(id);
    result.push(assemblyProcessWithInput({
      ...value,
      parameters: Array.isArray(value.parameters) ? value.parameters : [],
      partProcesses: Array.isArray(value.partProcesses) ? value.partProcesses : [],
      assemblyPath: Array.isArray(value.assemblyPath) ? value.assemblyPath : [],
    }, view));
  }
  return result;
}

const layoutShapes = new Map([
  ["l", "L形"],
  ["t", "T形"],
  ["cross", "十字形"],
  ["straight", "直线形"],
  ["orthogonal-corner", "三向直角节点"],
  ["pi", "Π形（双支）"],
  ["parallel", "并行形"],
  ["other", "其他形态"],
]);

export function assemblyShapeOrder(templates) {
  // Presentation follows each template's declared layout, independently of
  // category (which still describes its manufacturing/connection family).
  const preferred = [...layoutShapes.keys()];
  const labels = new Map();
  for (const template of templates) {
    const key = assemblyPresentationShape(template);
    if (!labels.has(key)) labels.set(key, layoutShapes.get(key));
  }
  return [...labels].sort(([left], [right]) => {
    return preferred.indexOf(left) - preferred.indexOf(right);
  });
}

export function assemblyPresentationShape(template) {
  const shape = String(template?.layoutShape ?? "").trim().toLowerCase();
  return layoutShapes.has(shape) ? shape : "other";
}

export function assemblyLayoutShapeLabel(template) {
  return layoutShapes.get(assemblyPresentationShape(template));
}

export function assemblyTemplateById(templates, id) {
  return templates.find((item) => item.id === String(id ?? ""));
}

export function assemblyParameterDefaults(template) {
  return Object.fromEntries((template?.parameters ?? []).map((item) => [item.key, item.defaultValue]));
}
