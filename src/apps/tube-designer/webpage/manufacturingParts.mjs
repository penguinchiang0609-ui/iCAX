export function manufacturingPartKind(part) {
  const properties = part?.properties ?? {};
  return String(part?.partKind ?? properties["manufacturing.partKind"]
    ?? properties["manufacturing.materialCategory"] ?? "tube").trim().toLowerCase();
}

export function isPlatePart(part) {
  return manufacturingPartKind(part) === "plate";
}

export function isTubeNestingPart(part) {
  const properties = part?.properties ?? {};
  if (Object.hasOwn(properties, "manufacturing.plate")) return false;
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
