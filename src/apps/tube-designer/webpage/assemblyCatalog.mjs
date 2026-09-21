export function normalizeAssemblyCatalogue(items) {
  const result = [];
  const seen = new Set();
  for (const value of Array.isArray(items) ? items : []) {
    if (!value || value.schema !== "icax.assembly-template" || Number(value.schemaVersion) !== 1) continue;
    const id = String(value.id ?? "").trim();
    if (!id || seen.has(id) || !Array.isArray(value.participants) || value.participants.length !== 2) continue;
    const blankParts = value.manufacturingPlan?.blankParts;
    if (!Array.isArray(blankParts) || !blankParts.length) continue;
    seen.add(id);
    result.push({
      ...value,
      parameters: Array.isArray(value.parameters) ? value.parameters : [],
      partProcesses: Array.isArray(value.partProcesses) ? value.partProcesses : [],
      assemblyPath: Array.isArray(value.assemblyPath) ? value.assemblyPath : [],
    });
  }
  return result;
}

export function assemblyCategoryOrder(templates) {
  const preferred = ["integrated", "insert", "fastener", "weld"];
  const labels = new Map();
  for (const template of templates) {
    const key = String(template.category ?? "other");
    if (!labels.has(key)) labels.set(key, String(template.categoryName ?? key));
  }
  return [...labels].sort(([left], [right]) => {
    const leftIndex = preferred.indexOf(left), rightIndex = preferred.indexOf(right);
    return (leftIndex < 0 ? preferred.length : leftIndex) - (rightIndex < 0 ? preferred.length : rightIndex);
  });
}

export function assemblyTemplateById(templates, id) {
  return templates.find((item) => item.id === String(id ?? ""));
}

export function assemblyParameterDefaults(template) {
  return Object.fromEntries((template?.parameters ?? []).map((item) => [item.key, item.defaultValue]));
}
