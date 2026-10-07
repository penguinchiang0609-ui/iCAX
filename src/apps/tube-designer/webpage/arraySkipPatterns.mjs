// Pure array-index grammar shared by drawing and machining; no editor state or
// geometry resolution belongs in this module.
const LIMIT = 1000;

export function parsePunchArraySkipText(text, groups) {
  const input = String(text ?? "").trim(); if (!input) return [];
  if (input.length > 50000) throw new Error("跳过组合文本过长。");
  return input.split(/[\s,，;；]+/u).map(token => {
    const indices = token.split(/[:：]/u);
    if (indices.length !== groups.length || indices.some(value => value !== "*" && !/^\d+$/.test(value))) throw new Error(`跳过组合“${token}”须按 ${groups.length} 组填写，例如 2:3，* 表示该组任意实例。`);
    const entries = indices.flatMap((value, index) => {
      if (value === "*") return [];
      const n = Number(value); if (!Number.isSafeInteger(n) || n < 1) throw new Error("跳过组合序号须为正整数。");
      return [[groups[index].id, n - 1]];
    });
    if (!entries.length) throw new Error("不能跳过所有阵列组合。");
    return Object.fromEntries(entries);
  });
}

export function resolvePunchArraySkipPatterns(feature, groups) {
  const patterns = feature.arraySkipText === undefined ? feature.arraySkips ?? [] : parsePunchArraySkipText(feature.arraySkipText, groups);
  if (!Array.isArray(patterns) || patterns.length > LIMIT) throw new Error("跳过组合须为有效列表，且不超过 1000 条。");
  const seen = new Set();
  for (const pattern of patterns) {
    if (!pattern || typeof pattern !== "object" || Array.isArray(pattern) || !Object.keys(pattern).length) throw new Error("跳过组合须指定至少一个阵列组序号。");
    for (const [id,index] of Object.entries(pattern)) {
      const group = groups.find(item => item.id === id);
      if (!group) throw new Error(`跳过组合引用了不存在的阵列组 ${id}。`);
      if (!Number.isSafeInteger(index) || index < 0 || (group.enabled !== false && index >= group.instanceCount)) throw new Error(`跳过组合超出阵列组 ${id} 的实例范围。`);
    }
    const key = JSON.stringify(Object.entries(pattern).sort(([a],[b]) => a.localeCompare(b)));
    if (seen.has(key)) throw new Error("跳过组合重复填写。"); seen.add(key);
  }
  return patterns;
}
