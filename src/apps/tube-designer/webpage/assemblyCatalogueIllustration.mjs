const TONES = new Set(["a", "b", "c", "joint", "cut", "detail", "void"]);

// The catalogue picture belongs to the process template. Unlike the parameter
// diagram, it depicts the finished joint and stays legible at list-card size.
export function renderAssemblyCatalogueIllustration(template) {
  const paths = template?.catalogueDiagram?.paths;
  if (!Array.isArray(paths) || !paths.length) return "";
  const content = paths.slice(0, 20).map((path) => {
    if (!path || typeof path.d !== "string" || !path.d.trim()) return "";
    const tone = TONES.has(path.tone) ? path.tone : "detail";
    return `<path class="tube-assembly-catalogue-${tone}" d="${escapeAttribute(path.d)}"/>`;
  }).join("");
  return content ? `<svg viewBox="0 0 64 64" aria-hidden="true" focusable="false">${content}</svg>` : "";
}

function escapeAttribute(value) {
  return String(value).replaceAll("&", "&amp;").replaceAll('"', "&quot;")
    .replaceAll("<", "&lt;").replaceAll(">", "&gt;");
}
