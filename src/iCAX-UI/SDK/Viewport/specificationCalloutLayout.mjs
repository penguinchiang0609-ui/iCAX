// Keep a numeric callout near its projected anchor while avoiding captions
// and viewport controls. Choose the nearest clear rectangle, not a corner.
export function placeSpecificationCallout(entry, occupied, width, height) {
  const { x, y, w, h } = entry, gap = 8;
  const clamp = (value, maximum) => Math.max(gap, Math.min(value, Math.max(gap, maximum - gap)));
  const xs = [clamp(x - w / 2, width - w)], ys = [clamp(y - h / 2, height - h)];
  for (const box of occupied) {
    xs.push(clamp(box.x - w - gap, width - w), clamp(box.x + box.w + gap, width - w));
    ys.push(clamp(box.y - h - gap, height - h), clamp(box.y + box.h + gap, height - h));
  }
  const candidates = [...new Set(xs)].flatMap(left => [...new Set(ys)].map(top => ({ x: left, y: top })));
  candidates.sort((a, b) => ((a.x + w / 2 - x) ** 2 + (a.y + h / 2 - y) ** 2)
    - ((b.x + w / 2 - x) ** 2 + (b.y + h / 2 - y) ** 2));
  return candidates.find(candidate => !occupied.some(box => candidate.x < box.x + box.w + 4
    && candidate.x + w + 4 > box.x && candidate.y < box.y + box.h + 4 && candidate.y + h + 4 > box.y))
    ?? candidates[0];
}
