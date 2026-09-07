// Catalog cards are design starting points, never alternate template identities.
// Existing product instances continue to use their saved parameters verbatim.
export function catalogText(value, fallback = "") {
  if (typeof value === "string" && value.trim()) return value.trim();
  if (value && typeof value === "object") {
    for (const locale of ["zh-CN", "zh", "en-US", "en"]) {
      if (typeof value[locale] === "string" && value[locale].trim()) return value[locale].trim();
    }
  }
  return fallback;
}

export function getCatalogTemplatePath(template) {
  const path = catalogText(template?.name, catalogText(template?.displayName, String(template?.id ?? "产品")))
    .split("/").map((part) => part.trim()).filter(Boolean);
  return path.length > 1 ? path : ["其他产品", path[0] ?? "产品"];
}

export function getCatalogEntryId(templateId, presetId = "") {
  return presetId ? `${encodeURIComponent(templateId)}::${encodeURIComponent(presetId)}` : String(templateId ?? "");
}

export function sortTemplatesByCatalog(templates = []) {
  return [...templates].sort((left, right) => {
    const a = left?.extensions?.catalog ?? {};
    const b = right?.extensions?.catalog ?? {};
    return Number(a.groupOrder ?? 1000) - Number(b.groupOrder ?? 1000)
      || Number(a.templateOrder ?? 1000) - Number(b.templateOrder ?? 1000)
      || getCatalogTemplatePath(left).join("/").localeCompare(getCatalogTemplatePath(right).join("/"), "zh-CN");
  });
}

export function getCatalogParameters(template, preset = null) {
  const values = Object.fromEntries((template?.parameters ?? [])
    .map((field) => [field.key ?? field.name, field.defaultValue]));
  // Restrict the overlay to declared parameters: IDs and other request metadata
  // cannot be overwritten by a card's preset.
  for (const [key, value] of Object.entries(preset?.parameters ?? {})) {
    if (Object.hasOwn(values, key)) values[key] = value;
  }
  return structuredClone(values);
}

export function buildCatalogEntries(templates = []) {
  return sortTemplatesByCatalog(templates).filter((template) => template?.available).flatMap((template) => {
    const path = getCatalogTemplatePath(template);
    const rawPresets = template?.extensions?.catalog?.presets;
    const seen = new Set();
    const presets = (Array.isArray(rawPresets) ? rawPresets : []).filter((preset) => {
      const id = String(preset?.id ?? "").trim();
      if (!id || seen.has(id) || preset?.available === false
        || !preset?.parameters || typeof preset.parameters !== "object" || Array.isArray(preset.parameters)) return false;
      seen.add(id);
      return true;
    });
    return (presets.length ? presets : [null]).map((preset) => {
      const presetId = String(preset?.id ?? "").trim();
      const displayName = catalogText(preset?.displayName, path.at(-1));
      const categories = Array.isArray(preset?.categoryPath)
        ? preset.categoryPath.filter((part) => typeof part === "string").map((part) => part.trim()).filter(Boolean)
        : [];
      return {
        ...template,
        templateId: template.id,
        presetId,
        catalogEntryId: getCatalogEntryId(template.id, presetId),
        displayName,
        catalogPath: [...(categories.length ? categories : path.slice(0, -1)), displayName],
        catalogParameters: getCatalogParameters(template, preset),
      };
    });
  });
}

export function getCatalogEntry(templates, templateId, presetId = "") {
  const entries = buildCatalogEntries(templates).filter((entry) => entry.templateId === templateId);
  return presetId ? entries.find((entry) => entry.presetId === presetId) ?? null : entries[0] ?? null;
}

export function buildTemplateGroupTree(templates = []) {
  const nodes = new Map();
  for (const [index, entry] of buildCatalogEntries(templates).entries()) {
    let parentKey = "";
    entry.catalogPath.slice(0, -1).forEach((title, depth, groupPath) => {
      const key = `template-path:${groupPath.slice(0, depth + 1).join("/")}`;
      if (!nodes.has(key)) nodes.set(key, { key, title, parentKey, order: index, templates: [], children: [] });
      parentKey = key;
    });
    nodes.get(parentKey).templates.push(entry);
  }
  const roots = [];
  for (const node of nodes.values()) {
    const parent = nodes.get(node.parentKey);
    if (parent) parent.children.push(node);
    else roots.push(node);
  }
  return roots;
}

// Return every enclosing category for one catalog card, from the root down.
// The add dialog uses this to keep its left-hand tree in sync with its active card.
export function getCatalogEntryGroupKeys(templates = [], templateId = "", presetId = "") {
  const entry = getCatalogEntry(templates, templateId, presetId);
  if (!entry) return [];
  const find = (groups, ancestors = []) => {
    for (const group of groups) {
      const next = [...ancestors, group.key];
      if (group.templates.some((item) => item.catalogEntryId === entry.catalogEntryId)) return next;
      const nested = find(group.children, next);
      if (nested.length) return nested;
    }
    return [];
  };
  return find(buildTemplateGroupTree(templates));
}

// Original parametric schematic: no downloaded competitor thumbnails or assets.
export function renderGuardrailSchematic(parameters = {}) {
  const layout = String(parameters.layout ?? "straight");
  const railCount = Number(parameters.railCount ?? 3);
  const double = parameters.cornerPostMode === "double";
  const large = String(parameters.largePostMode ?? "none");
  const infill = String(parameters.infillType ?? "bars");
  const round = parameters.handrailProfileType === "round";
  const wall = parameters.guardrailUse === "wall";
  const spear = wall && parameters.spearTipEnabled === true;
  const height = 43;
  const segments = layout === "u"
    ? [[[12, 62], [31, 88]], [[31, 88], [83, 74]], [[83, 74], [90, 46]]]
    : layout === "left_l" || layout === "right_l"
      ? [[[9, 67], [32, 86]], [[32, 86], [91, 70]]]
      : [[[10, 84], [90, 67]]];
  const point = (a, b, t, z = 0) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t - z];
  const line = (a, b, cls) => `<line class="${cls}" x1="${a[0].toFixed(2)}" y1="${a[1].toFixed(2)}" x2="${b[0].toFixed(2)}" y2="${b[1].toFixed(2)}" />`;
  const polygons = [];
  const bars = [];
  const posts = new Map();
  const recordPost = (p, big = false) => {
    const key = p.map((value) => value.toFixed(2)).join(",");
    posts.set(key, { p, big: big || posts.get(key)?.big });
  };
  segments.forEach(([a, b], segmentIndex) => {
    const span = Math.hypot(b[0] - a[0], b[1] - a[1]);
    const start = double && segmentIndex > 0 ? 2.5 / span : 0;
    const end = double && segmentIndex < segments.length - 1 ? 1 - 2.5 / span : 1;
    const bottom = 5;
    const top = height - (railCount === 3 ? 8 : 0);
    if (["plate", "lower_plate", "glass"].includes(infill)) {
      const panelTop = infill === "lower_plate" ? 22 : top;
      const corners = [[start, bottom], [end, bottom], [end, panelTop], [start, panelTop]]
        .map(([t, z]) => point(a, b, t, z).map((value) => value.toFixed(2)).join(",")).join(" ");
      polygons.push(`<polygon class="guardrail-panel${infill === "glass" ? " guardrail-glass" : ""}" points="${corners}" />`);
    }
    if (infill === "cross" || infill === "diamond") {
      const count = Math.max(1, Math.round(span / 25));
      for (let i = 0; i < count; i++) {
        const left = start + (end - start) * i / count;
        const right = start + (end - start) * (i + 1) / count;
        if (infill === "cross") {
          bars.push(line(point(a, b, left, bottom), point(a, b, right, top), "guardrail-infill guardrail-cross"));
          bars.push(line(point(a, b, left, top), point(a, b, right, bottom), "guardrail-infill guardrail-cross"));
        } else {
          const middle = (left + right) / 2;
          const vertices = [point(a, b, left, (bottom + top) / 2), point(a, b, middle, top), point(a, b, right, (bottom + top) / 2), point(a, b, middle, bottom)];
          vertices.forEach((p, index) => bars.push(line(p, vertices[(index + 1) % 4], "guardrail-infill guardrail-diamond")));
        }
      }
    } else if (!["plate", "glass"].includes(infill)) {
      const count = Math.max(3, Math.round(span / 5));
      for (let i = 1; i < count; i++) {
        const t = start + (end - start) * i / count;
        const picketTop = wall ? height + 6 : top;
        bars.push(line(point(a, b, t, infill === "lower_plate" ? 22 : bottom), point(a, b, t, picketTop), "guardrail-infill"));
        if (spear) {
          const p = point(a, b, t, picketTop);
          bars.push(`<path class="guardrail-spear" d="M${p[0]},${p[1] - 4} l-1.5,4 1.5,2 1.5,-2Z" />`);
        }
      }
    }
    for (const z of railCount === 3 ? [bottom, height - 8, height] : [bottom, height]) {
      bars.push(line(point(a, b, start, z), point(a, b, end, z), "guardrail-rail"));
    }
    recordPost(point(a, b, start), large === "ends" && segmentIndex === 0);
    recordPost(point(a, b, end), large === "ends" && segmentIndex === segments.length - 1);
    if (span > 45) recordPost(point(a, b, .5), large === "middle");
  });
  for (const { p, big } of posts.values()) {
    bars.push(line(p, [p[0], p[1] - height], `guardrail-post${big ? " guardrail-large-post" : ""}`));
    if (big && parameters.postCapEnabled === true)
      bars.push(`<rect class="guardrail-post-cap" x="${p[0] - 3.5}" y="${p[1] - height - 2}" width="7" height="2.5" />`);
    bars.push(line([p[0] - 2, p[1]], [p[0] + 2, p[1]], "guardrail-base"));
  }
  return `<g class="guardrail-schematic${round ? " guardrail-round" : ""}${wall ? " guardrail-wall" : ""}"${layout === "right_l" ? ' transform="translate(100 0) scale(-1 1)"' : ""}>${polygons.join("")}${bars.join("")}</g>`;
}
