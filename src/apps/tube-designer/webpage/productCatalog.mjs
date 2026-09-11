import { escapeAttr } from "../../_shared/workbench/utils/format.mjs";

// Every catalog card represents one product-template descriptor.  A descriptor
// is therefore one exportable .itpt resource; product instances continue to
// use their saved parameters verbatim.

// Template-owned visual assets are exposed by the host as data URLs for
// filesystem packages. Imported personal packages can also arrive with a
// `resources` map while being hydrated, so this helper deliberately
// understands both forms. The UI never needs to know a template ID or a
// product family to show its artwork.
export function getTemplateVisualAsset(template, kind = "schematic") {
  const source = template?.descriptor && typeof template.descriptor === "object"
    ? { ...template.descriptor, ...template } : template;
  const catalog = source?.extensions?.catalog ?? {};
  const data = catalog?.assetData?.[kind] ?? source?.assetData?.[kind];
  if (typeof data === "string" && data.trim()) return data;
  const declaredPath = typeof catalog?.[kind] === "string" && catalog[kind].trim()
    ? catalog[kind].trim() : `resource/${kind}.svg`;
  const resources = source?.resources ?? template?.resources ?? {};
  const encoded = resources?.[declaredPath] ?? resources?.[`resource/${kind}.svg`];
  if (typeof encoded === "string" && encoded.trim()) {
    if (encoded.startsWith("data:")) return encoded;
    return `data:image/svg+xml;base64,${encoded}`;
  }
  return "";
}

export function renderTemplateVisualAsset(template, kind = "schematic", className = "", label = "") {
  const source = getTemplateVisualAsset(template, kind);
  if (!source) return "";
  return `<img class="${escapeAttr(className)}" src="${escapeAttr(source)}" alt="${escapeAttr(label)}" />`;
}

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
  const explicitCategory = Array.isArray(template?.catalogPath)
    ? template.catalogPath
    : template?.extensions?.catalog?.categoryPath;
  if (Array.isArray(explicitCategory) && explicitCategory.length) {
    const category = explicitCategory.map((part) => String(part).trim()).filter(Boolean);
    const leaf = catalogText(template?.displayName, catalogText(template?.name, String(template?.id ?? "产品")));
    if (category.length) return [...category, leaf].filter(Boolean);
  }
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
  // `presets` is retained only as a defensive reader for old in-memory
  // descriptors.  Shipped and imported .itpt descriptors must not contain it;
  // each former style is now its own descriptor/package and gets its own ID.
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

// Keep the catalogue thumbnail and the product-detail preview on the same
// schematic contract.  These are deliberately vector-only: they describe the
// product family and are not a substitute for the generated 3D model.
export function renderSteelStaircaseSchematic(templateId, parameters = {}) {
  const railingVisible = String(parameters.railingSide ?? "both") !== "none";
  if (templateId === "l-turn-steel-staircase") {
    const right = String(parameters.turnDirection ?? "left") === "right";
    const transform = right ? ' transform="translate(100 0) scale(-1 1)"' : "";
    return `<g${transform}>
      <path class="stair-reference" d="M8 82 h34 v-10 h10 V37 h10 V27 h29" />
      <path class="stair-infill" d="M8 78 h8 v-6 h8 v-6 h8 v-6 h10 M52 68 v-8 h8 v-8 h8 v-8 h8 v-8 h15" />
      <path class="stair-post" d="M42 82 V52 M52 72 V42 M91 27 V7" />
      ${railingVisible ? '<path class="stair-handrail" d="M8 58 L42 42 L52 32 L91 7" />' : ""}
      <rect class="stair-reference" x="42" y="68" width="10" height="14" rx="1" />
    </g>`;
  }
  if (templateId === "u-turn-steel-staircase") {
    return `<path class="stair-reference" d="M10 84 h34 V26 h46 M10 70 h27 V33 h53" />
      <path class="stair-infill" d="M14 78 h7 v-7 h7 v-7 h7 v-7 h9 M90 39 h-8 v7 h-8 v7 h-8 v7 h-8 v7 h-8" />
      <rect class="stair-reference" x="37" y="21" width="20" height="18" rx="1" />
      <path class="stair-post" d="M10 84 V59 M44 57 V32 M50 67 V42 M90 39 V14" />
      ${railingVisible ? '<path class="stair-handrail" d="M10 59 L44 32 M50 42 L90 14" />' : ""}`;
  }
  let content = '<path class="stair-reference" d="M7 84 h10 v-7 h10 v-7 h10 v-7 h10 v-7 h10 v-7 h10 v-7 h10 v-7 h16" />';
  content += '<line class="stair-infill" x1="8" y1="79" x2="92" y2="36" />';
  if (railingVisible) {
    content += '<line class="stair-handrail" x1="8" y1="55" x2="92" y2="12" />';
    for (const [x, lower, upper] of [[8, 79, 55], [36, 65, 41], [64, 51, 27], [92, 36, 12]]) {
      content += `<line class="stair-post" x1="${x}" y1="${lower}" x2="${x}" y2="${upper}" />`;
    }
  }
  return content;
}

export function renderStraightStairRailingSchematic(parameters = {}) {
  const start = [9, 80];
  const end = [91, 34];
  const railingHeight = 30;
  const point = (ratio, offset = 0) => [
    start[0] + (end[0] - start[0]) * ratio,
    start[1] + (end[1] - start[1]) * ratio - offset,
  ];
  const line = (from, to, className) => `<line class="${className}" x1="${from[0]}" y1="${from[1]}" x2="${to[0]}" y2="${to[1]}" />`;
  let content = '<path class="stair-reference" d="M7 84 h12 v-7 h12 v-7 h12 v-7 h12 v-7 h12 v-7 h12 v-7 h14" />';
  content += line(point(0, railingHeight), point(1, railingHeight), "stair-handrail");
  for (const ratio of [0, 0.34, 0.67, 1]) {
    content += line(point(ratio, 0), point(ratio, railingHeight), "stair-post");
  }
  const infillType = String(parameters.infillType ?? "vertical");
  if (infillType === "horizontal") {
    for (const offset of [9, 16, 23]) content += line(point(0, offset), point(1, offset), "stair-infill");
  } else if (infillType === "vertical") {
    content += line(point(0, 7), point(1, 7), "stair-infill");
    for (const ratio of [0.1, 0.2, 0.3, 0.43, 0.54, 0.64, 0.77, 0.88]) {
      content += line(point(ratio, 7), point(ratio, railingHeight), "stair-infill");
    }
  }
  return content;
}
