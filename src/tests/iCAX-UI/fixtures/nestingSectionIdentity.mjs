// UI doubles provide an opaque native export, rather than deriving geometry
// identity in the browser. Actual curve equivalence is covered by native tests.
export function nativeSectionIdentity(tag) {
  return JSON.stringify({ schema: "icax.nesting-section.v1", contours: [JSON.stringify({ edges: [tag] })] });
}
