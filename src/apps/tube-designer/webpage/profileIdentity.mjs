// The native section exporter owns geometry equivalence. Never derive a stock
// identity from a name, bounding dimensions or a resource/business identifier.
export function nestingSectionIdentity(profile = {}) {
  const identity = profile.sectionIdentity;
  if (typeof identity !== "string" || !identity.length) {
    throw new Error("管型缺少截面身份，请重新读取零件");
  }
  const section = JSON.parse(identity);
  if (section?.schema !== "icax.nesting-section.v1" || !Array.isArray(section.contours)
    || !section.contours.length) {
    throw new Error("管型截面身份无效");
  }
  return identity;
}
