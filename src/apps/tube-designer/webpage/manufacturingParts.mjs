const NON_LINEAR_OR_PURCHASED_PROCESSES = new Set(["purchased", "bent", "curved", "bending", "tube-bending", "bent-tube", "curved-tube"]);

export function manufacturingPartKind(part) {
  const properties = part?.properties ?? {};
  return String(part?.partKind ?? properties["manufacturing.partKind"]
    ?? properties["manufacturing.materialCategory"] ?? "tube").trim().toLowerCase();
}

export function isPlatePart(part) {
  return manufacturingPartKind(part) === "plate";
}

export function isSheetPart(part) {
  return ["plate", "glass"].includes(manufacturingPartKind(part));
}

export function isComponentPart(part) {
  return manufacturingPartKind(part) === "accessory";
}

export function manufacturingPartKindLabel(part) {
  return { plate: "板件", glass: "玻璃", accessory: "配件", tube: "管材", profile: "管材", linear: "管材" }[manufacturingPartKind(part)] ?? "非管件";
}

export function partSourcingLabel(part) {
  const sourcing = String(part?.properties?.["manufacturing.sourcing"] ?? part?.sourcing ?? "").trim().toLowerCase();
  return sourcing === "purchased" ? "外购" : sourcing === "made" ? "自制" : "供料方式未指定";
}

export function componentBounds(part) {
  const bounds = part?.modelBounds ?? part?.properties?.["manufacturing.modelBounds"] ?? {};
  return Object.fromEntries(["width", "depth", "height"].map((key) => [key, Number(bounds[key]) || 0]));
}

export function tubeNestingExclusionReason(part) {
  const properties = part?.properties ?? {};
  if (isSheetPart(part) || isComponentPart(part)) return `${manufacturingPartKindLabel(part)}不参与管材排样，可导出 STEP`;
  if (partSourcingLabel(part) === "外购") return "外购件不参与管材排样";
  const process = String(properties["manufacturing.process"] ?? part?.process ?? "").toLowerCase();
  if (properties["manufacturing.requiresBending"] || /bend|bent|curv|roll|折弯|弯曲|弧形/.test(process))
    return "折弯 / 曲线件不参与直管排样，需单独下料";
  return "此零件不参与管材排样，可导出 STEP";
}

export function isTubeNestingPart(part) {
  const properties = part?.properties ?? {};
  if (Object.hasOwn(properties, "manufacturing.plate")) return false;
  if (partSourcingLabel(part) === "外购") return false;
  for (const key of ["manufacturing.sourcing", "manufacturing.process"]) {
    if (!Object.hasOwn(properties, key)) continue;
    if (typeof properties[key] !== "string" || NON_LINEAR_OR_PURCHASED_PROCESSES.has(properties[key].trim().toLowerCase())) return false;
  }
  if (Object.hasOwn(properties, "manufacturing.requiresBending") && properties["manufacturing.requiresBending"] !== false) return false;
  if (NON_LINEAR_OR_PURCHASED_PROCESSES.has(String(part?.process ?? "").trim().toLowerCase())) return false;
  const tags = [];
  if (Object.hasOwn(part ?? {}, "partKind")) tags.push(part.partKind);
  for (const key of ["manufacturing.partKind", "manufacturing.materialCategory"]) {
    if (Object.hasOwn(properties, key)) tags.push(properties[key]);
  }
  return tags.every((tag) => typeof tag === "string"
    && ["tube", "profile", "linear"].includes(tag.trim().toLowerCase()));
}

export function plateDimensions(part) {
  const plate = part?.plate ?? part?.properties?.["manufacturing.plate"] ?? {};
  return { width: Number(plate.width) || 0, height: Number(plate.height) || 0,
    thickness: Number(plate.thickness) || 0, areaMm2: Number(plate.areaMm2) || 0 };
}
