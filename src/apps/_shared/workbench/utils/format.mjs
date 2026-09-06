export function getArrayValue(values, index, fallback = 0) {
  const value = Number(values?.[index]);
  return Number.isFinite(value) ? value : fallback;
}

export function formatNumber(value, digits = 3) {
  const number = Number(value);
  if (!Number.isFinite(number)) {
    return "0";
  }
  return String(Number(number.toFixed(digits)));
}

export function escapeText(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;");
}

export function escapeAttr(value) {
  return escapeText(value)
    .replaceAll("\"", "&quot;")
    .replaceAll("'", "&#39;");
}
