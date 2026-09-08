import { escapeText } from "../../../UI/html.mjs";

export function renderRibbonCommandIcon(iconName, fallback = "□") {
  const body = RIBBON_ICON_SHAPES[String(iconName ?? "")];
  if (!body) {
    return `<span class="command-icon">${escapeText(fallback)}</span>`;
  }
  return `<span class="command-icon vector" aria-hidden="true">
    <svg viewBox="0 0 24 24" focusable="false">${body}</svg>
  </span>`;
}

const RIBBON_ICON_SHAPES = Object.freeze({
  new: `<path d="M5 3.5h9l5 5V21H5z"/><path d="M14 3.5V9h5"/><path class="accent" d="M8 14h8M12 10v8"/>`,
  "add-instance": `<rect x="3" y="4" width="13" height="16" rx="1"/><path d="M6 8h7M6 12h5"/><circle class="accent" cx="17.5" cy="16.5" r="4.5"/><path class="accent" d="M17.5 14v5M15 16.5h5"/>`,
  excel: `<rect x="3" y="3" width="18" height="18" rx="1"/><path d="M9 3v18M9 8h12M9 13h12M15 8v13"/><path class="accent" d="m4.8 8.5 3 7m0-7-3 7"/>`,
  machine: `<path d="M4 20V5h16v15M3 20h18M7 17V8h10v9zM12 5v3"/><path class="accent" d="M12 10v3m-3 3 3-3 3 3"/>`,
  nest: `<path d="M3 5h8v6H3zM13 13h8v6h-8z"/><path class="accent" d="M13 5h8v6h-8zM3 13h8v6H3z"/>`,
  export: `<path d="M5 3.5h9l5 5V21H5z"/><path d="M14 3.5V9h5"/><path class="accent" d="M12 17V8m-3 3 3-3 3 3"/>`,
  report: `<path d="M6 4h12v17H6z"/><path class="accent" d="M9 8h6M9 12h6M9 16h4"/><path d="M9 2.5h6V6H9z"/>`,
  select: `<path d="m5 3 12 9-6 1.5L8 19z"/><path class="accent" d="m12 14 4 6"/>`,
  display: `<rect x="3" y="4" width="18" height="16" rx="1"/><path d="M3 9h18M9 9v11"/><circle class="accent fill" cx="6" cy="6.5" r="1"/>`,
  repair: `<path d="M4 7c3-3 7-3 10 0l2 2"/><path class="accent" d="m13 9 3 .2.2-3"/><path d="M20 17c-3 3-7 3-10 0l-2-2"/><path class="accent" d="m11 15-3-.2-.2 3"/>`,
  edit2d: `<path d="M3 18 8 8l5 5 4-8 4 3"/><circle class="accent fill" cx="8" cy="8" r="1.5"/><path class="accent" d="m13 19 6-6 2 2-6 6-3 .5z"/>`,
  "profile-sketch": `<rect x="4" y="5" width="16" height="14" rx="3"/><rect class="accent" x="8" y="9" width="8" height="6" rx="1"/>`,
  "side-sketch": `<path d="m3 8 5-3 13 3-5 3zM3 8v9l13 3v-9M16 11l5-3v9l-5 3"/><path class="accent" d="M6 13c2-3 4 3 7 0"/>`,
  "sketch-line": `<path d="M4 19 20 5"/><circle class="accent fill" cx="4" cy="19" r="2"/><circle class="accent fill" cx="20" cy="5" r="2"/>`,
  "sketch-polyline": `<path d="m3 18 5-9 5 5 8-10"/><circle class="accent fill" cx="8" cy="9" r="1.5"/><circle class="accent fill" cx="13" cy="14" r="1.5"/>`,
  "sketch-rectangle": `<rect x="4" y="5" width="16" height="14" rx="1"/><path class="accent" d="M4 9V5h4"/>`,
  "sketch-circle": `<circle cx="12" cy="12" r="8"/><circle class="accent fill" cx="12" cy="12" r="1.5"/><path class="accent" d="M12 12h8"/>`,
  "sketch-arc": `<path d="M4 18C6 6 16 3 21 12"/><circle class="accent fill" cx="4" cy="18" r="1.5"/><circle class="accent fill" cx="21" cy="12" r="1.5"/>`,
  "sketch-spline": `<path d="M3 17C7 3 12 22 21 7"/><circle class="accent fill" cx="3" cy="17" r="1.5"/><circle class="accent fill" cx="21" cy="7" r="1.5"/>`,
  undo: `<path d="M9 7 4 11l5 4"/><path class="accent" d="M5 11h8c4 0 7 3 7 7"/>`,
  redo: `<path d="m15 7 5 4-5 4"/><path class="accent" d="M19 11h-8c-4 0-7 3-7 7"/>`,
  edit3d: `<path d="m4 8 8-4 8 4-8 4zM4 8v8l8 4 8-4V8M12 12v8"/><path class="accent" d="m14 16 5-5 2 2-5 5-3 1z"/>`,
  process: `<circle cx="12" cy="12" r="9"/><path class="accent" d="m7.5 12 3 3 6-7"/>`,
  hole: `<ellipse cx="12" cy="6" rx="7" ry="3"/><path d="M5 6v10c0 2 3 3 7 3s7-1 7-3V6"/><ellipse class="accent" cx="12" cy="11" rx="3" ry="1.5"/>`,
  micro: `<path d="M3 14c4-7 7-7 10 0s5 5 8-1"/><path class="accent" d="M10 13h4M12 11v4"/>`,
  weld: `<path d="M4 5v14M20 5v14M8 5c3 4 3 10 0 14M16 5c-3 4-3 10 0 14"/><path class="accent" d="M10 12h4"/>`,
  manual: `<rect x="3" y="4" width="7" height="6"/><rect x="13" y="14" width="8" height="6"/><path class="accent" d="m8 18 4-8 2 5"/>`,
  merge: `<path d="m3 8 7-4 7 4-7 4zM3 8v7l7 4 3-2"/><path class="accent" d="m11 12 5-3 5 3-5 3zM11 12v6l5 3 5-3v-6"/>`,
  autonest: `<rect x="3" y="4" width="7" height="6"/><rect x="14" y="4" width="7" height="6"/><rect x="3" y="14" width="7" height="6"/><path class="accent" d="M14 18h7m-3-3 3 3-3 3"/>`,
  sort: `<path d="M5 5h11M5 10h8M5 15h5"/><path class="accent" d="M18 8v11m-3-3 3 3 3-3"/>`,
  simulate: `<circle cx="12" cy="12" r="9"/><path class="accent fill" d="m10 8 6 4-6 4z"/>`,
  collision: `<path d="M12 3 20 6v6c0 5-3 8-8 10-5-2-8-5-8-10V6z"/><path class="accent" d="m8 12 2.5 2.5L16 9"/>`,
  measure: `<path d="m4 17 13-13 3 3L7 20z"/><path class="accent" d="m9 15 2 2m1-5 2 2m1-5 2 2"/>`,
  optimize: `<path d="M4 19V5m0 14h15"/><path class="accent" d="m7 15 4-5 3 2 5-7"/>`,
  support: `<circle cx="12" cy="12" r="9"/><path class="accent" d="M12 11v6"/><circle class="accent fill" cx="12" cy="7.5" r="1"/>`,
  delete: `<path d="M5 7h14M9 7V4h6v3M7 7l1 14h8l1-14"/><path class="accent" d="M10 10v7m4-7v7"/>`,
  move: `<path d="M12 3v18M3 12h18"/><path class="accent" d="m9 6 3-3 3 3m-6 12 3 3 3-3M6 9l-3 3 3 3m12-6 3 3-3 3"/>`,
  boolean: `<circle cx="9" cy="12" r="6"/><circle class="accent" cx="15" cy="12" r="6"/>`,
  base: `<ellipse cx="12" cy="5.5" rx="7" ry="3"/><path d="M5 5.5v13c0 2 3 3 7 3s7-1 7-3v-13"/><path class="accent" d="M5 15.5c0 2 3 3 7 3s7-1 7-3"/>`,
  branch: `<path d="M4 5h6v14H4z"/><path class="accent" d="M10 9h10v6H10z"/><ellipse cx="20" cy="12" rx="2" ry="3"/>`,
  bevel: `<path d="M4 5h16l-6 14h-4z"/><path class="accent" d="m8 5 4 8 4-8"/>`,
  cut: `<path d="M4 5h16v14H4z"/><path class="accent" d="m5 18 14-12"/>`,
  mark: `<path d="M5 5h14M12 5v14"/><path class="accent" d="M7 19h10"/>`,
  apply: `<path d="M4 4h16v16H4z"/><path class="accent" d="m7 12 3 3 7-7"/>`,
});
