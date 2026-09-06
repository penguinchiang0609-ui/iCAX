export const tubeDesignerCss = String.raw`
.tube-designer-workspace {
  --designer-border: rgba(122, 151, 166, 0.22);
  --designer-panel: #f6f8f9;
  --designer-ink: #21313a;
  --designer-muted: #697a84;
  --designer-accent: #178f82;
}

.tube-designer-workspace .cam-viewer { grid-template-rows: minmax(0, 1fr); }
.tube-designer-workspace .cam-viewer-head { display: none; }
.tube-designer-workspace .cam-viewport {
  position: relative;
  margin: 0;
  border: 0;
}

.tube-designer-workspace .cam-context-pane,
.tube-designer-workspace .cam-info-pane {
  background: var(--designer-panel);
  color: var(--designer-ink);
}

.tube-designer-workspace .cam-info-pane {
  align-content: stretch;
  gap: 0;
  padding: 0;
  overflow: hidden;
}

.tube-designer-panel {
  display: grid;
  gap: 12px;
  padding: 14px;
  color: var(--designer-ink);
}

.tube-designer-heading { display: grid; gap: 3px; }
.tube-designer-heading strong { font-size: 15px; }

.tube-designer-heading span,
.tube-designer-help,
.tube-designer-empty {
  color: var(--designer-muted);
  font-size: 12px;
  line-height: 1.55;
}

.tube-designer-section {
  display: grid;
  gap: 9px;
  padding: 12px;
  border: 1px solid var(--designer-border);
  border-radius: 8px;
  background: #fff;
}

.tube-designer-section > strong { font-size: 12px; letter-spacing: .03em; }
.tube-designer-subsection-list {
  display: grid;
  grid-auto-rows: max-content;
  gap: 4px;
}
.tube-designer-config-subsection {
  min-height: max-content;
  overflow: hidden;
  border: 1px solid #c5d0d4;
  border-radius: 4px;
  background: #f8fafb;
}
.tube-designer-config-subsection > summary {
  position: relative;
  display: flex;
  align-items: center;
  min-height: 30px;
  padding: 4px 9px 4px 27px;
  background: #edf2f3;
  color: #3e5963;
  cursor: pointer;
  font-size: 11px;
  font-weight: 650;
  list-style: none;
  user-select: none;
}
.tube-designer-config-subsection > summary::-webkit-details-marker { display: none; }
.tube-designer-config-subsection > summary::before {
  position: absolute;
  left: 11px;
  top: 10px;
  width: 6px;
  height: 6px;
  border-right: 1.5px solid #61757e;
  border-bottom: 1.5px solid #61757e;
  content: "";
  transform: rotate(-45deg);
}
.tube-designer-config-subsection[open] > summary {
  border-bottom: 1px solid #cbd5d8;
  background: #e4eeec;
  color: #245e59;
}
.tube-designer-config-subsection[open] > summary::before {
  transform: rotate(45deg) translate(-1px, -1px);
}
.tube-designer-config-subsection > summary > small {
  margin-left: auto;
  color: #71838b;
  font-size: 9px;
  font-weight: 500;
}
.tube-designer-config-subsection-content {
  display: grid;
  gap: 4px;
  padding: 8px;
}
.tube-designer-config-subsection-content > .tube-designer-subsection-list {
  padding-left: 8px;
}
.tube-designer-field-grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 8px; }

.tube-designer-field {
  display: grid;
  gap: 4px;
  min-width: 0;
  color: var(--designer-muted);
  font-size: 11px;
}

.tube-designer-field.wide { grid-column: 1 / -1; }

.tube-designer-field input,
.tube-designer-field select,
.tube-designer-export-row input {
  box-sizing: border-box;
  width: 100%;
  min-width: 0;
  height: 30px;
  padding: 0 8px;
  border: 1px solid #c9d3d8;
  border-radius: 5px;
  background: #fff;
  color: var(--designer-ink);
  font: inherit;
  font-size: 12px;
}
.tube-designer-boolean-field { grid-template-columns: minmax(0, 1fr) auto; align-items: center; }
.tube-designer-boolean-field input[type="checkbox"] { width: 16px; height: 16px; margin: 0; accent-color: var(--designer-accent); }
.tube-designer-profile-field { padding: 8px; border: 1px solid #bdd2d4; border-radius: 7px; background: #f3f9f8; }
.tube-designer-profile-actions { display: flex; flex-wrap: wrap; gap: 6px; }
.tube-designer-profile-actions .tube-designer-secondary { min-height: 27px; padding: 0 9px; font-size: 10px; }
.tube-designer-profile-readonly-note { color: #3e6d69; font-size: 10px; line-height: 1.5; }
.tube-designer-profile-readonly-note strong { color: #176d64; }
.tube-designer-parametric-profile-parameters { display: grid; grid-column: 1 / -1; gap: 7px; padding: 8px; border-radius: 6px; background: #e8f2f0; }
.tube-designer-parametric-profile-parameters > strong { color: #315e5a; font-size: 10px; }
.tube-designer-parametric-profile-parameters .tube-designer-field-grid { margin: 0; }

.tube-designer-primary,
.tube-designer-secondary {
  min-height: 34px;
  padding: 0 12px;
  border-radius: 6px;
  cursor: pointer;
  font-size: 12px;
  font-weight: 600;
}

.tube-designer-primary { border: 0; background: var(--designer-accent); color: #fff; }
.tube-designer-secondary { border: 1px solid #c9d3d8; background: #fff; color: var(--designer-ink); }
.tube-designer-danger { min-height: 34px; padding: 0 12px; border: 1px solid #c85f5f; border-radius: 6px; background: #fff; color: #a53838; cursor: pointer; font-size: 12px; font-weight: 600; }
.tube-designer-danger:disabled { cursor: default; opacity: .55; }
.tube-designer-primary:disabled, .tube-designer-secondary:disabled { cursor: default; opacity: .55; }

.tube-designer-summary { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 7px; }
.tube-designer-metric { display: grid; gap: 2px; padding: 9px; border-radius: 7px; background: #eaf3f2; }
.tube-designer-metric strong { color: #176f68; font-size: 17px; }
.tube-designer-metric span { color: var(--designer-muted); font-size: 10px; }
.tube-designer-part-list { display: grid; gap: 5px; max-height: 410px; overflow: auto; }

.tube-designer-part {
  display: grid;
  grid-template-columns: 1fr auto;
  gap: 4px 10px;
  padding: 8px 9px;
  border: 1px solid var(--designer-border);
  border-radius: 6px;
  background: #fff;
}

.tube-designer-part strong { font-size: 12px; }
.tube-designer-part span { color: var(--designer-muted); font-size: 11px; }
.tube-designer-part .tube-designer-part-length {
  grid-row: 1 / span 2;
  grid-column: 2;
  align-self: center;
  color: var(--designer-ink);
  font-variant-numeric: tabular-nums;
}

.tube-designer-export-row { display: grid; gap: 7px; }

.tube-designer-overlay {
  position: absolute;
  top: 14px;
  left: 14px;
  display: grid;
  gap: 3px;
  padding: 9px 11px;
  border: 1px solid rgba(255,255,255,.18);
  border-radius: 7px;
  background: rgba(17, 34, 42, .78);
  color: #eff9fa;
  pointer-events: none;
  backdrop-filter: blur(8px);
}

.tube-designer-overlay strong { font-size: 12px; }
.tube-designer-overlay span { color: #b7cbd2; font-size: 10px; }

.tube-designer-breakdown-backdrop {
  position: fixed;
  inset: 0;
  z-index: 900;
  display: grid;
  place-items: center;
  padding: 28px;
  background: rgba(12, 25, 31, .56);
  backdrop-filter: blur(3px);
}

.tube-designer-breakdown-dialog {
  position: relative;
  display: grid;
  grid-template-rows: auto minmax(0, 1fr) auto;
  width: min(1180px, calc(100vw - 56px));
  height: min(760px, calc(100vh - 56px));
  overflow: hidden;
  border: 1px solid rgba(78, 109, 122, .38);
  border-radius: 10px;
  background: #f7f9fa;
  color: var(--designer-ink);
  box-shadow: 0 24px 64px rgba(3, 17, 23, .42);
}

.tube-designer-breakdown-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 20px;
  padding: 14px 18px;
  border-bottom: 1px solid #cbd5da;
  background: #fff;
}

.tube-designer-breakdown-header > div { display: grid; gap: 3px; }
.tube-designer-breakdown-header strong { font-size: 16px; }
.tube-designer-breakdown-header span { color: var(--designer-muted); font-size: 12px; }

.tube-designer-breakdown-body {
  display: grid;
  grid-template-rows: auto auto minmax(0, 1fr);
  gap: 8px;
  min-height: 0;
  padding: 12px 16px;
}

.tube-designer-breakdown-tools {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
}
.tube-designer-breakdown-tools > span { display: flex; align-items: baseline; gap: 8px; }
.tube-designer-breakdown-tools strong { color: #314750; font-size: 12px; }
.tube-designer-breakdown-tools small { color: var(--designer-muted); font-size: 10px; }
.tube-designer-breakdown-tools > div { display: flex; gap: 6px; }
.tube-designer-breakdown-tools button,
.tube-designer-breakdown-pagination button,
.tube-designer-part-inspection-button {
  min-height: 27px;
  padding: 4px 10px;
  border: 1px solid #b8c7cc;
  border-radius: 4px;
  background: #fff;
  color: #38515b;
  cursor: pointer;
  font-size: 10px;
}
.tube-designer-breakdown-tools button:hover,
.tube-designer-breakdown-pagination button:hover,
.tube-designer-part-inspection-button:hover { border-color: var(--designer-accent); color: var(--designer-accent); }
.tube-designer-breakdown-tools button:disabled,
.tube-designer-breakdown-pagination button:disabled { cursor: default; opacity: .5; }

.tube-designer-breakdown-current-product { min-width: 0; }
.tube-designer-breakdown-current-product strong {
  max-width: 520px;
  overflow: hidden;
  color: #176f83;
  font-size: 13px;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.tube-designer-breakdown-pagination {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  min-height: 31px;
  padding: 5px 8px;
  border: 1px solid #d3dde1;
  background: #f4f7f8;
}
.tube-designer-breakdown-pagination > span { color: var(--designer-muted); font-size: 10px; }
.tube-designer-breakdown-pagination > span strong { color: #31525e; font-size: 11px; }
.tube-designer-breakdown-page-numbers { display: flex; align-items: center; gap: 4px; }
.tube-designer-breakdown-page-numbers button { min-width: 28px; padding-inline: 7px; }
.tube-designer-breakdown-page-numbers button.is-current {
  border-color: var(--designer-accent);
  background: var(--designer-accent);
  color: #fff;
  opacity: 1;
}
.tube-designer-breakdown-page-gap { padding-inline: 2px; color: #83939a; }

.tube-designer-dialog-close {
  width: 32px;
  height: 32px;
  padding: 0;
  border: 1px solid #ccd5da;
  border-radius: 6px;
  background: #fff;
  color: #4c5e67;
  cursor: pointer;
  font-size: 21px;
  line-height: 1;
}

.tube-designer-sheet-wrap {
  min-height: 0;
  overflow: auto;
  border: 1px solid #bfcbd1;
  background: #fff;
}

.tube-designer-sheet {
  width: 100%;
  min-width: 780px;
  border-collapse: collapse;
  border-spacing: 0;
  table-layout: auto;
  font-size: 12px;
}

.tube-designer-sheet th,
.tube-designer-sheet td {
  padding: 7px 10px;
  border-right: 1px solid #d8e0e4;
  border-bottom: 1px solid #d8e0e4;
  text-align: left;
  vertical-align: middle;
  white-space: nowrap;
}

.tube-designer-sheet th {
  position: sticky;
  top: 0;
  z-index: 2;
  background: #edf2f4;
  color: #42545d;
  font-weight: 600;
}

.tube-designer-sheet tr:hover td { background: #f1f8f7; }
.tube-designer-sheet .tube-designer-check-cell { width: 36px; padding-inline: 10px; text-align: center; }
.tube-designer-sheet .tube-designer-action-column { width: 62px; text-align: center; }
.tube-designer-sheet .tube-designer-number { text-align: right; font-variant-numeric: tabular-nums; }
.tube-designer-sheet canvas { display: block; border: 1px solid #d1dadd; border-radius: 4px; background: #eef3f4; }
.tube-designer-sheet .tube-designer-product-column { min-width: 188px; }

.tube-designer-sheet .tube-designer-product-cell {
  width: 188px;
  min-width: 188px;
  padding: 0;
  border-right-color: #b6c8c8;
  border-bottom-color: #afc3c3;
  background: #e2eeee;
  vertical-align: top;
  white-space: normal;
}

.tube-designer-sheet tr:hover .tube-designer-product-cell { background: #e2eeee; }
.tube-designer-product-cell-content { position: sticky; top: 42px; display: grid; gap: 7px; padding: 12px; color: #294b4a; }
.tube-designer-product-cell-content > strong { overflow-wrap: anywhere; font-size: 12px; line-height: 1.4; }
.tube-designer-product-cell-content > span:not(.tube-designer-product-order):not(.tube-designer-product-thumbnail) { color: #607c7b; font-size: 10px; }
.tube-designer-product-order { justify-self: start; padding: 2px 7px; border-radius: 999px; background: #c6ddda; color: #315d59; font-size: 10px; font-weight: 600; }
.tube-designer-product-thumbnail { display: grid; place-items: center; height: 108px; border-radius: 5px; background: #142a33; }
.tube-designer-product-cell dl { display: grid; gap: 5px; margin: 2px 0 0; }
.tube-designer-product-cell dl > div { display: grid; grid-template-columns: 42px minmax(0, 1fr); gap: 6px; }
.tube-designer-product-cell dt { color: #708787; font-size: 10px; }
.tube-designer-product-cell dd { margin: 0; color: #345958; font-size: 10px; font-variant-numeric: tabular-nums; }

.tube-designer-breakdown-footer {
  display: grid;
  grid-template-columns: minmax(280px, 1fr) auto auto auto;
  align-items: end;
  gap: 10px;
  padding: 12px 16px;
  border-top: 1px solid #cbd5da;
  background: #fff;
}

.tube-designer-export-destination { display: grid; gap: 3px; min-width: 0; }
.tube-designer-export-destination strong { font-size: 12px; }
.tube-designer-export-destination span { overflow: hidden; color: var(--designer-muted); font-size: 11px; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-breakdown-footer > span { align-self: center; color: var(--designer-muted); font-size: 12px; }

.tube-designer-export-wait {
  position: absolute;
  inset: 0;
  z-index: 8;
  display: grid;
  place-items: center;
  padding: 24px;
  background: rgba(229, 237, 240, .82);
  backdrop-filter: blur(2px);
  cursor: wait;
}

.tube-designer-operation-wait {
  position: fixed;
  inset: 0;
  z-index: 11000;
  display: grid;
  place-items: center;
  padding: 24px;
  background: rgba(21, 39, 47, .46);
  backdrop-filter: blur(2px);
  cursor: wait;
}

.tube-designer-export-progress-card {
  display: grid;
  grid-template-columns: 30px minmax(0, 1fr) auto;
  align-items: center;
  gap: 7px 12px;
  width: min(500px, calc(100% - 48px));
  padding: 22px 24px;
  border: 1px solid #9eb2ba;
  border-radius: 10px;
  background: #fff;
  box-shadow: 0 18px 48px rgba(8, 31, 40, .24);
}

.tube-designer-export-progress-card > strong { color: #183741; font-size: 15px; }
.tube-designer-export-progress-card > span:not(.tube-designer-export-spinner) { grid-column: 2 / 4; color: #60747d; font-size: 12px; }
.tube-designer-export-progress-card > small { color: #49616a; font-size: 11px; font-variant-numeric: tabular-nums; text-align: right; }
.tube-designer-export-progress-card > em { grid-column: 1 / 4; color: #7b570b; font-size: 11px; font-style: normal; text-align: center; }

.tube-designer-export-spinner {
  grid-row: 1 / 3;
  width: 24px;
  height: 24px;
  box-sizing: border-box;
  border: 3px solid #cee0e1;
  border-top-color: var(--designer-accent);
  border-radius: 50%;
  animation: tube-designer-export-spin .75s linear infinite;
}

.tube-designer-export-progress-track {
  grid-column: 1 / 3;
  height: 7px;
  overflow: hidden;
  border-radius: 999px;
  background: #dce7e9;
}

.tube-designer-export-progress-track > i {
  display: block;
  height: 100%;
  border-radius: inherit;
  background: var(--designer-accent);
  transition: width .16s ease-out;
}

.tube-designer-export-progress-track.is-indeterminate > i {
  animation: tube-designer-export-indeterminate 1.05s ease-in-out infinite;
}

@keyframes tube-designer-export-spin { to { transform: rotate(360deg); } }
@keyframes tube-designer-export-indeterminate {
  from { transform: translateX(-120%); }
  to { transform: translateX(310%); }
}

.tube-designer-instance-panel { grid-template-rows: auto minmax(0, 1fr); height: 100%; box-sizing: border-box; }
.tube-designer-instance-heading { display: flex; align-items: center; justify-content: space-between; gap: 10px; }
.tube-designer-instance-heading > div { display: grid; gap: 2px; }
.tube-designer-add-button { min-height: 30px; padding-inline: 10px; white-space: nowrap; }
.tube-designer-instance-list { display: grid; align-content: start; gap: 8px; min-height: 0; overflow: auto; }
.tube-designer-instance-item { display: grid; overflow: hidden; border: 1px solid transparent; border-radius: 8px; background: #fff; }
.tube-designer-instance-item.selected { border-color: rgba(23,143,130,.45); }

.tube-designer-instance-card {
  position: relative;
  display: grid;
  grid-template-columns: 78px minmax(0, 1fr) 8px;
  align-items: center;
  gap: 10px;
  width: 100%;
  padding: 9px;
  border: 1px solid #cad5da;
  border-radius: 8px;
  background: #fff;
  color: var(--designer-ink);
  cursor: pointer;
  text-align: left;
}
.tube-designer-instance-item .tube-designer-instance-card { width: 100%; border: 0; border-radius: 0; }
.tube-designer-instance-part-list {
  min-height: 30px;
  border: 0;
  border-top: 1px solid #d7e1e4;
  background: #f4f8f9;
  color: #176f83;
  cursor: pointer;
  font-size: 10px;
  font-weight: 600;
}
.tube-designer-instance-part-list:hover { background: #e9f5f3; color: #147b71; }
.tube-designer-instance-part-list:disabled { color: #91a0a6; cursor: default; }

.tube-designer-instance-card:hover { border-color: #6baea7; background: #f4fbfa; }
.tube-designer-instance-card.selected { border-color: var(--designer-accent); background: #e9f5f3; box-shadow: inset 3px 0 var(--designer-accent); }
.tube-designer-instance-card:disabled { cursor: default; opacity: 1; }
.tube-designer-instance-card > i { width: 7px; height: 7px; border-radius: 50%; background: #c7d1d5; }
.tube-designer-instance-card.selected > i { background: var(--designer-accent); box-shadow: 0 0 0 3px rgba(23,143,130,.15); }
.tube-designer-instance-thumbnail { display: grid; place-items: center; height: 82px; border-radius: 6px; background: #122830; }
.tube-designer-instance-copy { display: grid; gap: 4px; min-width: 0; }
.tube-designer-instance-copy strong { display: -webkit-box; overflow: hidden; font-size: 12px; line-height: 1.35; -webkit-box-orient: vertical; -webkit-line-clamp: 2; }
.tube-designer-instance-copy span, .tube-designer-instance-copy small { overflow: hidden; color: var(--designer-muted); font-size: 10px; text-overflow: ellipsis; white-space: nowrap; }

.tube-designer-empty-state { display: grid; place-items: center; gap: 8px; padding: 28px 14px; border: 1px dashed #b8c7cd; border-radius: 9px; background: rgba(255,255,255,.6); text-align: center; }
.tube-designer-empty-state > span { color: var(--designer-muted); font-size: 11px; line-height: 1.5; }
.tube-designer-empty-state .tube-designer-primary { margin-top: 4px; }
.tube-designer-empty-schematic { width: 82px !important; height: 94px !important; }

.tube-designer-schematic { width: 100%; height: 100%; }
.tube-designer-schematic line, .tube-designer-schematic rect, .tube-designer-schematic path, .tube-designer-schematic polyline, .tube-designer-schematic polygon { fill: none; stroke: #8fc7c2; stroke-width: 3; vector-effect: non-scaling-stroke; }
.tube-designer-schematic .notch { stroke: #f0b56a; }
.tube-designer-schematic .placeholder { stroke: #9eafb7; stroke-dasharray: 5 4; }
.tube-designer-schematic .plus { stroke: var(--designer-accent); stroke-width: 4; }
.tube-designer-schematic .multi-face-fill { fill: rgba(88, 168, 160, .07); stroke: none; }
.tube-designer-schematic .multi-face-fill:nth-child(even) { fill: rgba(88, 168, 160, .13); }
.tube-designer-schematic .multi-face-frame { stroke: #9ed3ce; stroke-width: 2.4; }
.tube-designer-schematic .multi-face-corner { stroke: #d1ebe8; stroke-width: 3.6; }
.tube-designer-schematic .multi-face-rail { stroke: #72aaa5; stroke-width: 1.5; }
.tube-designer-schematic .multi-face-rod { stroke: #83bbb6; stroke-width: 1.25; }
.tube-designer-schematic .multi-face-door-frame { fill: rgba(227, 168, 63, .13); stroke: #e3a83f; stroke-width: 2.5; }
.tube-designer-schematic .multi-face-door-leaf { fill: rgba(19, 37, 45, .38); stroke: #f1c66f; stroke-width: 1.4; }

.tube-designer-parameter-panel {
  grid-template-rows: auto auto minmax(0, 1fr);
  align-content: stretch;
  gap: 0;
  box-sizing: border-box;
  height: 100%;
  min-height: 0;
  padding: 0;
  overflow: hidden;
  background: #e8edef;
}

.tube-designer-user-preset-bar {
  display: grid;
  grid-template-columns: minmax(0, 1fr) auto;
  align-items: end;
  gap: 8px;
  padding: 8px;
  border-bottom: 1px solid #c7d1d5;
  background: #edf4f3;
}
.tube-designer-user-preset-bar > label { display: grid; gap: 3px; min-width: 0; color: #52666f; font-size: 10px; }
.tube-designer-user-preset-bar select {
  width: 100%;
  min-width: 0;
  height: 28px;
  padding: 0 7px;
  border: 1px solid #aebdc4;
  border-radius: 3px;
  background: #fff;
  color: var(--designer-ink);
  font: inherit;
  font-size: 11px;
}
.tube-designer-user-preset-bar > div { display: flex; gap: 5px; }
.tube-designer-user-preset-bar button { min-height: 28px; padding-inline: 8px; white-space: nowrap; }
.tube-designer-config-parameters > .tube-designer-user-preset-bar {
  border: 1px solid #bfd0d1;
  border-radius: 8px;
}

.tube-designer-parameter-header {
  position: relative;
  z-index: 1;
  display: grid;
  flex: none;
  border-bottom: 1px solid #b9c6cc;
  background: linear-gradient(#fbfcfc, #f1f4f5);
  box-shadow: 0 1px 3px rgba(31, 53, 63, .08);
}

.tube-designer-parameter-toolbar {
  display: grid;
  grid-template-columns: minmax(0, 1fr) auto;
  align-items: center;
  gap: 8px;
  min-width: 0;
  padding: 8px 9px;
}

.tube-designer-parameter-toolbar > .tube-designer-heading {
  min-width: 0;
  gap: 2px;
}
.tube-designer-parameter-toolbar > .tube-designer-heading strong {
  overflow: hidden;
  font-size: 13px;
  line-height: 1.35;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.tube-designer-parameter-toolbar > .tube-designer-heading span {
  overflow: hidden;
  font-size: 10px;
  line-height: 1.35;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.tube-designer-regenerate-button {
  min-height: 28px;
  padding: 0 10px;
  border-radius: 3px;
  white-space: nowrap;
}

.tube-designer-parameter-header > .tube-designer-summary {
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 0;
  border-top: 1px solid #c7d1d5;
  background: #dfe7ea;
}
.tube-designer-parameter-panel .tube-designer-metric {
  display: flex;
  align-items: baseline;
  justify-content: center;
  gap: 4px;
  min-width: 0;
  padding: 6px 4px;
  border-right: 1px solid #c2cdd2;
  border-radius: 0;
  background: transparent;
}
.tube-designer-parameter-panel .tube-designer-metric:last-child { border-right: 0; }
.tube-designer-parameter-panel .tube-designer-metric strong { font-size: 14px; line-height: 1; }
.tube-designer-parameter-panel .tube-designer-metric span {
  overflow: hidden;
  font-size: 9px;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.tube-designer-parameter-sections {
  display: grid;
  grid-auto-rows: max-content;
  align-content: start;
  gap: 3px;
  min-height: 0;
  padding: 5px;
  overflow: auto;
  scrollbar-gutter: stable;
  scrollbar-width: thin;
}

.tube-designer-parameter-section {
  min-height: max-content;
  overflow: hidden;
  border: 1px solid #b9c5ca;
  border-radius: 3px;
  background: #fff;
}
.tube-designer-parameter-section > summary {
  position: relative;
  display: flex;
  align-items: center;
  gap: 7px;
  min-height: 27px;
  box-sizing: border-box;
  padding: 4px 7px 4px 23px;
  background: linear-gradient(#f8fafb, #e8edef);
  color: #31454f;
  cursor: pointer;
  font-size: 11px;
  font-weight: 650;
  list-style: none;
  user-select: none;
}
.tube-designer-parameter-section > summary::-webkit-details-marker { display: none; }
.tube-designer-parameter-section > summary::before {
  position: absolute;
  left: 9px;
  top: 9px;
  width: 6px;
  height: 6px;
  border-right: 1.5px solid #536a74;
  border-bottom: 1.5px solid #536a74;
  content: "";
  transform: rotate(-45deg);
  transition: transform 100ms ease;
}
.tube-designer-parameter-section[open] > summary {
  border-bottom: 1px solid #c8d2d6;
  background: linear-gradient(#f3f8f7, #e1ecea);
  color: #245e59;
}
.tube-designer-parameter-section[open] > summary::before { transform: rotate(45deg) translate(-1px, -1px); }
.tube-designer-parameter-section > summary > span {
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.tube-designer-parameter-section > summary > small {
  margin-left: auto;
  color: #73858d;
  font-size: 9px;
  font-weight: 500;
}
.tube-designer-parameter-group-content { display: grid; }
.tube-designer-parameter-subsection-list {
  display: grid;
  grid-auto-rows: max-content;
  gap: 2px;
  padding: 3px;
  background: #edf2f3;
}
.tube-designer-parameter-subsection {
  min-height: max-content;
  overflow: hidden;
  border: 1px solid #c8d2d6;
  border-radius: 2px;
  background: #fff;
}
.tube-designer-parameter-subsection > summary {
  position: relative;
  display: flex;
  align-items: center;
  min-height: 25px;
  box-sizing: border-box;
  padding: 3px 7px 3px 24px;
  background: #f5f7f8;
  color: #52666f;
  cursor: pointer;
  font-size: 10px;
  font-weight: 650;
  list-style: none;
  user-select: none;
}
.tube-designer-parameter-subsection > summary::-webkit-details-marker { display: none; }
.tube-designer-parameter-subsection > summary::before {
  position: absolute;
  left: 10px;
  top: 8px;
  width: 6px;
  height: 6px;
  border-right: 1.5px solid #657982;
  border-bottom: 1.5px solid #657982;
  content: "";
  transform: rotate(-45deg);
  transition: transform 100ms ease;
}
.tube-designer-parameter-subsection[open] > summary {
  border-bottom: 1px solid #d5dddf;
  background: #e8f0ef;
  color: #245e59;
}
.tube-designer-parameter-subsection[open] > summary::before {
  transform: rotate(45deg) translate(-1px, -1px);
}
.tube-designer-parameter-subsection > summary > small {
  margin-left: auto;
  color: #7a8b92;
  font-size: 9px;
  font-weight: 500;
}

.tube-designer-parameter-panel .tube-designer-field-grid {
  display: grid;
  grid-template-columns: minmax(0, 1fr);
  gap: 0;
}
.tube-designer-parameter-panel .tube-designer-field {
  display: grid;
  grid-template-columns: minmax(92px, .9fr) minmax(88px, 1.1fr);
  align-items: center;
  gap: 8px;
  min-height: 31px;
  box-sizing: border-box;
  padding: 3px 7px;
  border-bottom: 1px solid #e1e7e9;
  color: #53666f;
  font-size: 10px;
}
.tube-designer-parameter-panel .tube-designer-field:last-child { border-bottom: 0; }
.tube-designer-parameter-panel .tube-designer-field:hover { background: #f3f8f7; }
.tube-designer-parameter-panel .tube-designer-field.wide { grid-column: auto; }
.tube-designer-parameter-panel .tube-designer-field input,
.tube-designer-parameter-panel .tube-designer-field select {
  height: 24px;
  padding: 0 6px;
  border-color: #aebdc4;
  border-radius: 2px;
  background: #fbfcfc;
  font-size: 11px;
}
.tube-designer-parameter-panel .tube-designer-field input[type="number"] {
  text-align: right;
  font-variant-numeric: tabular-nums;
}
.tube-designer-parameter-panel .tube-designer-field input[readonly],
.tube-designer-parameter-panel .tube-designer-field select:disabled {
  opacity: 1;
  background: #edf1f2;
  color: #52636b;
}
.tube-designer-parameter-panel .tube-designer-boolean-field input[type="checkbox"] {
  justify-self: end;
  width: 15px;
  height: 15px;
}
.tube-designer-parameter-panel .tube-designer-profile-field {
  grid-template-columns: minmax(92px, .9fr) minmax(88px, 1.1fr);
  margin: 4px;
  min-height: 0;
  border-bottom: 1px solid #bdd2d4;
}
.tube-designer-parameter-panel .tube-designer-profile-actions,
.tube-designer-parameter-panel .tube-designer-profile-readonly-note { grid-column: 1 / -1; }
.tube-designer-parameter-panel .tube-designer-profile-actions { justify-content: flex-end; }
.tube-designer-imported-profile-summary {
  display: grid;
  gap: 4px;
  padding: 12px;
  border: 1px solid #c8d8da;
  border-radius: 7px;
  background: #eef7f5;
  color: #35515a;
}
.tube-designer-imported-profile-summary > span,
.tube-designer-imported-profile-summary > small { color: var(--designer-muted); }
.tube-designer-profile-library-dialog {
  grid-template-rows: auto minmax(0, 1fr) auto;
  width: min(720px, calc(100vw - 48px));
  max-height: min(680px, calc(100vh - 48px));
}
.tube-designer-profile-library-list { display: grid; align-content: start; gap: 9px; min-height: 0; padding: 14px; overflow: auto; }
.tube-designer-profile-library-row {
  display: grid;
  grid-template-columns: minmax(0, 1fr) auto;
  align-items: center;
  gap: 12px;
  padding: 11px;
  border: 1px solid #c8d8da;
  border-radius: 7px;
  background: #fff;
}
.tube-designer-profile-library-copy { display: grid; gap: 4px; min-width: 0; }
.tube-designer-profile-library-copy input { box-sizing: border-box; width: 100%; height: 30px; padding: 0 8px; border: 1px solid #bfcdd2; border-radius: 5px; color: var(--designer-ink); font: inherit; font-size: 12px; font-weight: 600; }
.tube-designer-profile-library-copy span,
.tube-designer-profile-library-copy small { overflow: hidden; color: var(--designer-muted); font-size: 10px; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-profile-library-actions { display: flex; gap: 7px; }
.tube-designer-profile-library-actions button { min-width: 68px; }
.tube-designer-profile-library-empty { display: grid; place-items: center; gap: 7px; min-height: 180px; color: var(--designer-muted); text-align: center; }
.tube-designer-profile-library-empty strong { color: var(--designer-ink); }

.tube-profile-library-panel,
.tube-profile-library-editor { box-sizing: border-box; height: 100%; min-height: 0; overflow: hidden; }
.tube-profile-library-panel { grid-template-rows: auto minmax(0, 1fr); }
.tube-profile-library-heading > div { display: grid; gap: 2px; }
.tube-profile-library-list { display: flex; flex-direction: column; align-items: stretch; gap: 9px; min-height: 0; padding: 10px; overflow-x: hidden; overflow-y: auto; }
.tube-profile-library-group { flex: 0 0 auto; overflow: hidden; border: 1px solid #c5d5da; border-radius: 8px; background: #f7fafb; }
.tube-profile-library-group > summary { display: flex; align-items: center; min-height: 34px; padding: 0 10px; color: #35545e; cursor: pointer; list-style: none; user-select: none; }
.tube-profile-library-group > summary::-webkit-details-marker { display: none; }
.tube-profile-library-group > summary::before { width: 7px; height: 7px; margin: -3px 9px 0 2px; border-right: 2px solid #607982; border-bottom: 2px solid #607982; content: ""; transform: rotate(45deg); }
.tube-profile-library-group:not([open]) > summary::before { margin-top: 3px; transform: rotate(-45deg); }
.tube-profile-library-group > summary strong { font-size: 11px; }
.tube-profile-library-group > summary span { margin-left: auto; color: var(--designer-muted); font-size: 9px; }
.tube-profile-library-group-content { display: grid; gap: 7px; padding: 7px; border-top: 1px solid #d4dfe2; }
.tube-profile-library-card {
  display: grid;
  grid-template-columns: 34px minmax(0, 1fr) 8px;
  align-items: center;
  gap: 9px;
  width: 100%;
  padding: 9px;
  border: 1px solid #c5d3d7;
  border-radius: 7px;
  background: #fff;
  color: var(--designer-ink);
  cursor: pointer;
  text-align: left;
}
.tube-profile-library-card.selected { border-color: var(--designer-accent); background: #e8f4f2; box-shadow: inset 3px 0 var(--designer-accent); }
.tube-profile-library-card:disabled { cursor: default; opacity: .6; }
.tube-profile-library-card-icon { display: grid; width: 32px; height: 32px; place-items: center; border-radius: 6px; background: #18313a; color: #9ed4cf; font-size: 12px; font-weight: 700; }
.tube-profile-library-card-icon.is-system { background: #d9ece9; color: #176f68; }
.tube-profile-library-card-copy { display: grid; gap: 2px; min-width: 0; }
.tube-profile-library-card-copy strong,
.tube-profile-library-card-copy small,
.tube-profile-library-card-copy em { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.tube-profile-library-card-copy strong { font-size: 11px; }
.tube-profile-library-card-copy small { color: #4b716f; font-size: 9px; }
.tube-profile-library-card-copy em { color: var(--designer-muted); font-size: 9px; font-style: normal; }
.tube-profile-library-card > i { width: 7px; height: 7px; border: 2px solid #aab9bf; border-radius: 50%; }
.tube-profile-library-card.selected > i { border-color: var(--designer-accent); background: var(--designer-accent); box-shadow: inset 0 0 0 2px #fff; }
.tube-profile-library-empty,
.tube-profile-library-editor-empty { display: grid; place-items: center; align-content: center; gap: 8px; min-height: 180px; padding: 18px; color: var(--designer-muted); text-align: center; }
.tube-profile-library-empty strong { color: var(--designer-ink); }
.tube-profile-library-empty.compact { min-height: 88px; padding: 11px; }
.tube-profile-library-editor { display: grid; grid-template-rows: auto minmax(0, 1fr) auto; }
.tube-profile-library-editor-body { display: grid; align-content: start; gap: 12px; min-height: 0; padding: 13px; overflow: auto; }
.tube-profile-library-parameter-section { display: grid; gap: 8px; }
.tube-profile-library-parameter-section > header { display: grid; gap: 2px; }
.tube-profile-library-parameter-section > header strong { font-size: 12px; }
.tube-profile-library-parameter-section > header span { color: var(--designer-muted); font-size: 9px; }
.tube-profile-library-parameter-list { display: grid; gap: 8px; }
.tube-profile-library-frozen-note { margin: 0; padding: 9px; border-left: 3px solid #67a9a2; background: #e9f3f1; color: #496b68; font-size: 10px; line-height: 1.5; }
.tube-profile-library-system-name { display: grid; gap: 4px; padding: 9px; border: 1px solid #c9d8dc; border-radius: 6px; background: #f7fafb; }
.tube-profile-library-system-name > span,
.tube-profile-library-system-name > small { color: var(--designer-muted); font-size: 9px; }
.tube-profile-library-system-name > strong { font-size: 12px; }
.tube-profile-library-system-note { margin: 0; padding: 9px; border-left: 3px solid #438f87; background: #e9f3f1; color: #3e6d69; font-size: 10px; line-height: 1.5; }
.tube-profile-library-preview-section { display: grid; gap: 9px; padding-top: 11px; border-top: 1px solid #d4dfe2; }
.tube-profile-library-preview-section > header { display: grid; gap: 2px; }
.tube-profile-library-preview-section > header strong { font-size: 12px; }
.tube-profile-library-preview-section > header span { color: var(--designer-muted); font-size: 9px; }
.tube-profile-library-miniature { position: relative; height: 128px; min-width: 0; min-height: 0; overflow: hidden; border: 1px solid #3b555f; border-radius: 7px; background: #13252d; }
.tube-profile-library-miniature .tube-profile-library-svg {
  position: absolute;
  top: 7%;
  left: 7%;
  display: block;
  width: 86%;
  height: 86%;
  min-width: 0;
  min-height: 0;
  max-width: none;
  max-height: none;
}
.tube-profile-library-export-actions { display: grid; grid-template-columns: 1fr 1fr; gap: 7px; }
.tube-profile-library-editor-footer { display: flex; justify-content: space-between; gap: 9px; padding: 11px 13px; border-top: 1px solid #cbd5da; background: #fff; }
.tube-profile-library-preview-hud {
  position: absolute;
  top: 12px;
  left: 12px;
  z-index: 3;
  display: grid;
  gap: 2px;
  max-width: min(360px, calc(100% - 24px));
  padding: 9px 12px;
  border: 1px solid rgba(121, 166, 171, .32);
  border-radius: 7px;
  background: rgba(17, 38, 47, .78);
  color: #dce9eb;
  pointer-events: none;
  backdrop-filter: blur(5px);
}
.tube-profile-library-preview-hud strong { overflow: hidden; font-size: 12px; text-overflow: ellipsis; white-space: nowrap; }
.tube-profile-library-preview-hud span { overflow: hidden; color: #a8c3c8; font-size: 9px; text-overflow: ellipsis; white-space: nowrap; }
.tube-profile-library-preview-hud small { color: #789ca2; font-size: 9px; }
.tube-profile-library-preview-hud.is-busy small { color: #9ed4cf; }
.tube-profile-library-preview-hud small.error { color: #ffaaa3; }
.tube-profile-library-preview-progress {
  width: 100%;
  height: 4px;
  margin-top: 4px;
  overflow: hidden;
  border-radius: 999px;
  background: rgba(158, 212, 207, .18);
}
.tube-profile-library-preview-progress[hidden] { display: none; }
.tube-profile-library-preview-progress > i {
  display: block;
  width: 36%;
  height: 100%;
  border-radius: inherit;
  background: #42b7ac;
  animation: tube-designer-export-indeterminate 1.05s ease-in-out infinite;
}
.tube-profile-library-preview-wait {
  position: fixed;
  inset: 0;
  z-index: 11000;
  display: grid;
  place-items: center;
  padding: 24px;
  background: rgba(21, 39, 47, .46);
  backdrop-filter: blur(2px);
  pointer-events: auto;
  cursor: wait;
}
.tube-profile-library-preview-wait[hidden] { display: none; }
.tube-profile-library-svg { overflow: visible; filter: drop-shadow(0 6px 10px rgba(0, 0, 0, .24)); }
.tube-profile-library-svg .outer { fill: #78c5bd; stroke: #d0f0ed; stroke-width: .7; vector-effect: non-scaling-stroke; }
.tube-profile-library-svg .hole { fill: #13252d; stroke: #d0f0ed; stroke-width: .7; vector-effect: non-scaling-stroke; }

.tube-designer-modal-backdrop {
  position: fixed;
  inset: 0;
  z-index: 900;
  display: grid;
  place-items: center;
  padding: 28px;
  background: rgba(12, 25, 31, .58);
}

.tube-designer-config-dialog,
.tube-designer-selection-dialog,
.tube-designer-breakdown-dialog {
  display: grid;
  grid-template-rows: auto minmax(0, 1fr) auto;
  overflow: hidden;
  border: 1px solid rgba(78, 109, 122, .38);
  border-radius: 11px;
  background: #f7f9fa;
  color: var(--designer-ink);
  box-shadow: 0 24px 64px rgba(3, 17, 23, .42);
}

.tube-designer-config-dialog { position: relative; width: min(1120px, calc(100vw - 56px)); height: min(780px, calc(100vh - 56px)); }
.tube-designer-selection-dialog { width: min(1080px, calc(100vw - 56px)); height: min(680px, calc(100vh - 56px)); }
.tube-designer-preset-dialog-backdrop { z-index: 960; }
.tube-designer-preset-dialog {
  display: grid;
  grid-template-rows: auto auto auto;
  width: min(520px, calc(100vw - 48px));
  overflow: hidden;
  border: 1px solid rgba(78, 109, 122, .38);
  border-radius: 11px;
  background: #f7f9fa;
  color: var(--designer-ink);
  box-shadow: 0 24px 64px rgba(3, 17, 23, .42);
}
.tube-designer-preset-dialog-body { display: grid; gap: 12px; padding: 18px; }
.tube-designer-preset-dialog-body > p { margin: 0; color: var(--designer-muted); font-size: 11px; line-height: 1.55; }
.tube-designer-preset-dialog-footer {
  display: grid;
  grid-template-columns: minmax(0, 1fr) auto auto;
  align-items: center;
  gap: 9px;
  padding: 12px 16px;
  border-top: 1px solid #cbd5da;
  background: #fff;
}

.tube-designer-add-template-progress {
  position: absolute;
  inset: 0;
  z-index: 4;
  display: grid;
  place-items: center;
  padding: 24px;
  background: rgba(232, 239, 241, .82);
  cursor: wait;
}

.tube-designer-dialog-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 20px;
  padding: 15px 18px;
  border-bottom: 1px solid #cbd5da;
  background: #fff;
}
.tube-designer-dialog-header > div { display: grid; gap: 3px; }
.tube-designer-dialog-header strong { font-size: 16px; }
.tube-designer-dialog-header span { color: var(--designer-muted); font-size: 12px; }

.tube-designer-config-body { display: grid; grid-template-columns: 340px minmax(0, 1fr); min-height: 0; }
.tube-designer-template-pane { display: grid; grid-template-rows: auto minmax(0, 1fr); min-height: 0; padding: 16px; border-right: 1px solid #cbd5da; background: #edf2f4; }
.tube-designer-pane-title { display: flex; justify-content: space-between; padding-bottom: 11px; }
.tube-designer-pane-title strong { font-size: 13px; }
.tube-designer-pane-title span { color: var(--designer-muted); font-size: 11px; }
.tube-designer-template-list { display: grid; grid-auto-rows: max-content; align-content: start; gap: 9px; min-height: 0; overflow: auto; }
.tube-designer-template-group { min-height: max-content; overflow: hidden; border: 1px solid #c8d4d9; border-radius: 8px; background: #f7fafb; }
.tube-designer-template-group.expanded { border-color: #abc4c8; background: #e6eef0; }
.tube-designer-template-group[data-tube-designer-template-group-depth="1"] { border-color: #d2dee1; background: #f9fbfb; }
.tube-designer-template-group[data-tube-designer-template-group-depth="1"].expanded { border-color: #b9ced1; background: #f2f7f7; }
.tube-designer-template-group[data-tube-designer-template-group-depth="1"] > .tube-designer-template-group-toggle { min-height: 39px; padding-block: 7px; }
.tube-designer-template-group-toggle { display: flex; width: 100%; align-items: center; justify-content: space-between; gap: 10px; min-height: 44px; padding: 9px 11px; border: 0; background: transparent; color: #294550; cursor: pointer; text-align: left; }
.tube-designer-template-group-toggle:hover { background: #e2ecee; }
.tube-designer-template-group-toggle:disabled { cursor: default; }
.tube-designer-template-group-toggle > span { display: flex; align-items: center; gap: 9px; min-width: 0; }
.tube-designer-template-group-toggle strong { overflow: hidden; font-size: 13px; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-template-group-toggle i { width: 7px; height: 7px; flex: 0 0 auto; border-right: 2px solid #617982; border-bottom: 2px solid #617982; transform: rotate(-45deg); transition: transform 120ms ease; }
.tube-designer-template-group.expanded > .tube-designer-template-group-toggle i { transform: rotate(45deg); }
.tube-designer-template-group-toggle em { min-width: 36px; padding: 2px 7px; border-radius: 11px; background: #d5e2e5; color: #607780; font-size: 10px; font-style: normal; text-align: center; }
.tube-designer-template-group-items { display: grid; grid-auto-rows: max-content; gap: 8px; padding: 0 8px 8px; }
.tube-designer-template-group-items[hidden] { display: none; }
.tube-designer-template-card { display: grid; grid-template-columns: 92px minmax(0, 1fr) 10px; align-items: center; gap: 10px; min-height: 112px; padding: 9px; border: 1px solid #c6d2d7; border-radius: 8px; background: #fff; color: var(--designer-ink); cursor: pointer; text-align: left; }
.tube-designer-template-card:hover { border-color: #68aaa3; }
.tube-designer-template-card.selected { border-color: var(--designer-accent); background: #e9f5f3; box-shadow: inset 3px 0 var(--designer-accent); }
.tube-designer-template-card.disabled { opacity: .58; cursor: default; }
.tube-designer-template-card > span:nth-child(2) { display: grid; gap: 6px; }
.tube-designer-template-card strong { font-size: 12px; line-height: 1.4; }
.tube-designer-template-card small { color: var(--designer-muted); font-size: 10px; line-height: 1.45; }
.tube-designer-template-card > i { width: 8px; height: 8px; border: 2px solid #aab9bf; border-radius: 50%; }
.tube-designer-template-card.selected > i { border-color: var(--designer-accent); background: var(--designer-accent); box-shadow: inset 0 0 0 2px #fff; }
.tube-designer-template-schematic { display: grid; place-items: center; height: 92px; border-radius: 6px; background: #142a33; }
.tube-designer-schematic .stair-reference { stroke: #55727c; stroke-width: 1.4; }
.tube-designer-schematic .stair-handrail { stroke: #c6e5e2; stroke-width: 3.2; }
.tube-designer-schematic .stair-post { stroke: #92c9c4; stroke-width: 2.2; }
.tube-designer-schematic .stair-infill { stroke: #70aaa5; stroke-width: 1.35; }
.tube-designer-schematic .guardrail-rail { stroke: #c6e5e2; stroke-width: 2.4; }
.tube-designer-schematic .guardrail-post { stroke: #9ed3ce; stroke-width: 2.8; }
.tube-designer-schematic .guardrail-large-post { stroke: #e1eeec; stroke-width: 5; }
.tube-designer-schematic .guardrail-infill { stroke: #83bbb6; stroke-width: 1.05; }
.tube-designer-schematic .guardrail-base { stroke: #b5d4d0; stroke-width: 1.8; }
.tube-designer-schematic .guardrail-panel { fill: rgba(113, 175, 181, .48); stroke: #9ac5c7; stroke-width: 1.2; }
.tube-designer-schematic .guardrail-round { stroke-linecap: round; }
.tube-designer-schematic .security-window-plate { fill: #61969b; stroke: #cee2df; stroke-width: 2; }

.tube-designer-config-parameters { display: grid; align-content: start; gap: 12px; min-height: 0; padding: 16px 18px; overflow: auto; }
.tube-designer-config-summary { display: grid; grid-template-columns: 106px minmax(0, 1fr); align-items: center; gap: 14px; padding: 11px; border: 1px solid var(--designer-border); border-radius: 8px; background: #fff; }
.tube-designer-config-summary > div { display: grid; gap: 5px; }
.tube-designer-config-summary strong { font-size: 14px; }
.tube-designer-config-summary span { color: var(--designer-muted); font-size: 11px; }
.tube-designer-config-preview { display: grid; place-items: center; height: 104px; border-radius: 6px; background: #142a33; }
.tube-designer-config-parameters .tube-designer-field-grid { grid-template-columns: repeat(3, minmax(0, 1fr)); }

.tube-designer-dialog-footer {
  display: grid;
  grid-template-columns: minmax(0, 1fr) auto auto;
  align-items: center;
  gap: 10px;
  padding: 12px 16px;
  border-top: 1px solid #cbd5da;
  background: #fff;
}
.tube-designer-dialog-footer > span { color: var(--designer-muted); font-size: 12px; }
.tube-designer-dialog-footer button { min-width: 88px; }

.tube-designer-part-inspection-backdrop { z-index: 930; padding: 24px; }
.tube-designer-part-inspection-dialog {
  display: grid;
  grid-template-rows: auto minmax(0, 1fr);
  width: min(1180px, calc(100vw - 48px));
  height: min(820px, calc(100vh - 48px));
  overflow: hidden;
  border: 1px solid rgba(84, 119, 132, .54);
  border-radius: 11px;
  background: #e7edef;
  box-shadow: 0 26px 72px rgba(3, 17, 23, .55);
}
.tube-designer-part-inspection-body { display: grid; grid-template-columns: minmax(0, 1fr) 360px; min-height: 0; }
.tube-designer-part-inspection-stage { display: grid; grid-template-rows: auto auto minmax(0, 1fr); min-width: 0; min-height: 0; background: #13252d; }
.tube-designer-part-inspection-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  min-height: 42px;
  padding: 6px 10px 6px 14px;
  border-bottom: 1px solid #36505b;
  background: #1d343e;
  color: #dce8ec;
  font-size: 11px;
}
.tube-designer-part-inspection-toolbar > span.error { color: #ffb9a8; }
.tube-designer-part-inspection-toolbar > div { display: flex; gap: 6px; }
.tube-designer-part-inspection-toolbar button,
.tube-designer-measurement-heading button {
  min-height: 28px;
  padding: 4px 10px;
  border: 1px solid #78909a;
  border-radius: 4px;
  background: #f8fafb;
  color: #29434e;
  cursor: pointer;
}
.tube-designer-part-inspection-toolbar button:hover,
.tube-designer-measurement-heading button:hover { border-color: var(--designer-accent); color: var(--designer-accent); }
.tube-designer-measurement-heading button[aria-pressed="true"] { border-color: #17877d; background: #17877d; color: #fff; }
.tube-designer-part-inspection-progress {
  width: 100%;
  height: 5px;
  overflow: hidden;
  background: #29454f;
}
.tube-designer-part-inspection-progress[hidden] { display: none; }
.tube-designer-part-inspection-progress > i {
  display: block;
  width: 36%;
  height: 100%;
  background: #42b7ac;
  animation: tube-designer-export-indeterminate 1.05s ease-in-out infinite;
}
.tube-designer-part-inspection-viewport { position: relative; min-width: 0; min-height: 0; overflow: hidden; }
.tube-designer-measurement-panel {
  display: flex;
  flex-direction: column;
  gap: 14px;
  min-height: 0;
  padding: 16px;
  overflow: auto;
  border-left: 1px solid #bac8ce;
  background: #f4f7f8;
  color: #2b414a;
}
.tube-designer-automatic-dimension-section { display: grid; gap: 10px; }
.tube-designer-measurement-heading { display: flex; align-items: center; justify-content: space-between; gap: 8px; }
.tube-designer-measurement-heading > div { display: grid; gap: 2px; }
.tube-designer-measurement-heading strong { font-size: 14px; }
.tube-designer-measurement-heading span { color: var(--designer-muted); font-size: 10px; }
.tube-designer-measurement-heading button { flex: 0 0 auto; min-height: 26px; padding: 3px 9px; font-size: 10px; }
.tube-designer-dimension-overview { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 5px; }
.tube-designer-dimension-overview > span { display: grid; gap: 2px; padding: 7px 8px; border: 1px solid #c7d4d8; border-radius: 4px; background: #fff; }
.tube-designer-dimension-overview > span:nth-child(2) { grid-column: span 2; }
.tube-designer-dimension-overview small { color: #72838a; font-size: 9px; }
.tube-designer-dimension-overview strong { color: #193c4b; font-size: 11px; font-variant-numeric: tabular-nums; }
.tube-designer-dimension-list { display: grid; gap: 5px; margin-top: 7px; }
.tube-designer-dimension-list article { display: grid; gap: 3px; padding: 7px 8px; border-left: 3px solid #69a9a2; background: #eaf1f2; }
.tube-designer-dimension-list article.tube-designer-dimension-pitch { border-left-color: #e3a83f; background: #f5f0e6; }
.tube-designer-dimension-list article strong { color: #29434e; font-size: 10px; }
.tube-designer-dimension-list article span { color: #5d7179; font-size: 9px; line-height: 1.45; font-variant-numeric: tabular-nums; }
.tube-designer-dimension-empty { padding: 10px; border: 1px dashed #b7c6cc; border-radius: 5px; color: #647981; font-size: 10px; line-height: 1.5; }
.tube-designer-measurement-help { display: grid; align-content: start; gap: 6px; padding-top: 5px; color: #60747c; font-size: 10px; }
.tube-designer-measurement-help strong { color: #344d57; font-size: 11px; }
.tube-designer-measurement-help small { margin-top: 5px; line-height: 1.5; }
.tube-designer-part-inspection-button { min-width: 48px; color: #176f83; font-weight: 600; }

.tube-designer-selection-table-wrap { min-height: 0; margin: 14px 16px; overflow: auto; border: 1px solid #bdc9ce; background: #fff; }
.tube-designer-selection-table { width: 100%; min-width: 720px; border-collapse: collapse; border-spacing: 0; font-size: 12px; }
.tube-designer-selection-table th, .tube-designer-selection-table td { padding: 8px 10px; border-right: 1px solid #d8e0e4; border-bottom: 1px solid #d8e0e4; text-align: left; vertical-align: middle; }
.tube-designer-selection-table th { position: sticky; top: 0; z-index: 2; background: #edf2f4; color: #42545d; }
.tube-designer-selection-table td:nth-child(3) > strong,
.tube-designer-selection-table td:nth-child(3) > small { display: block; }
.tube-designer-selection-table td:nth-child(3) > small { margin-top: 3px; }
.tube-designer-selection-table td small { color: var(--designer-muted); font-size: 10px; }
.tube-designer-table-thumbnail { display: grid; place-items: center; width: 78px; height: 62px; border-radius: 5px; background: #142a33; }

.tube-designer-sheet .tube-designer-tree-column { min-width: 150px; }
.tube-designer-tree-cell { box-sizing: border-box; }
.tube-designer-tree-level-0 { padding-left: 10px !important; }
.tube-designer-tree-level-1 { padding-left: 30px !important; }
.tube-designer-tree-level-2 { padding-left: 55px !important; }
.tube-designer-tree-node { display: flex; align-items: center; gap: 8px; min-width: 0; }
.tube-designer-tree-node-copy { display: grid; gap: 2px; min-width: 0; }
.tube-designer-tree-node-copy strong { overflow: hidden; font-size: 11px; text-overflow: ellipsis; }
.tube-designer-tree-node-copy small { color: #778990; font-size: 9px; font-weight: 400; }
.tube-designer-tree-toggle {
  position: relative;
  display: grid;
  width: 23px;
  height: 23px;
  flex: 0 0 auto;
  place-items: center;
  padding: 0;
  border: 1px solid #bfccd1;
  border-radius: 4px;
  background: #fff;
  cursor: pointer;
}
.tube-designer-tree-toggle i {
  width: 6px;
  height: 6px;
  border-right: 1.5px solid currentColor;
  border-bottom: 1.5px solid currentColor;
  color: #607780;
  transform: rotate(-45deg);
}
.tube-designer-tree-toggle[aria-expanded="true"] i { transform: rotate(45deg) translate(-1px, -1px); }
.tube-designer-tree-toggle:hover { border-color: var(--designer-accent); background: #f1f8f7; }
.tube-designer-tree-toggle:hover i { color: var(--designer-accent); }
.tube-designer-tree-kind { font-weight: 600; }
.tube-designer-product-row td { border-bottom-color: #bfd5da; background: #fff; color: #176f83; font-weight: 600; }
.tube-designer-product-row:hover td { background: #f2f8f9 !important; }
.tube-designer-category-row td { border-bottom-color: #cadbcf; background: #fff; color: #39705a; }
.tube-designer-category-row:hover td { background: #f4f9f6 !important; }
.tube-designer-product-row .tube-designer-merged-value,
.tube-designer-category-row .tube-designer-merged-value { color: #73878a; }
.tube-designer-product-row input[type="checkbox"],
.tube-designer-category-row input[type="checkbox"] { accent-color: var(--designer-accent); }
.tube-designer-part-row td { border-bottom-color: #edf1f2; background: #fff; }
.tube-designer-category-last-row td { border-bottom-color: #c4d2d6; }
.tube-designer-tree-product-thumbnail { display: grid; width: 116px; height: 68px; place-items: center; border-radius: 4px; background: #142a33; }
.tube-designer-tree-product-thumbnail .tube-designer-schematic { max-height: 62px; }
.tube-designer-part-name { max-width: 270px; white-space: normal !important; overflow-wrap: anywhere; }
.tube-designer-part-name > strong,
.tube-designer-part-name > small { display: block; }
.tube-designer-part-name > small,
.tube-designer-merged-value { margin-top: 3px; color: var(--designer-muted); font-size: 10px; }

.tube-designer-production-workspace .cam-context-pane,
.tube-designer-production-workspace .cam-info-pane { padding: 0; }

.tube-designer-cutting-parts,
.tube-designer-cutting-inspector {
  display: grid;
  width: 100%;
  height: 100%;
  min-height: 0;
  box-sizing: border-box;
  overflow: hidden;
  background: #f4f7f8;
  color: var(--designer-ink);
}
.tube-designer-cutting-parts { grid-template-rows: auto auto minmax(0, 1fr); }
.tube-designer-cutting-inspector { grid-template-rows: auto minmax(0, 1fr); }
.tube-designer-cutting-pane-header {
  display: flex;
  min-width: 0;
  min-height: 50px;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  padding: 8px 10px;
  border-bottom: 1px solid #cbd6d9;
  background: #fff;
}
.tube-designer-cutting-pane-header > div { display: grid; min-width: 0; gap: 2px; }
.tube-designer-cutting-pane-header strong { overflow: hidden; color: #243d46; font-size: 12px; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-cutting-pane-header span { overflow: hidden; color: #71838a; font-size: 9px; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-cutting-pane-header > button {
  min-height: 25px;
  padding: 3px 8px;
  border: 1px solid #9ab0b7;
  border-radius: 4px;
  background: #f6f9fa;
  color: #35515a;
  font-size: 9px;
}
.tube-designer-cutting-tools { display: grid; gap: 6px; padding: 8px; border-bottom: 1px solid #d4dee1; background: #edf2f3; }
.tube-designer-cutting-search {
  display: grid;
  grid-template-columns: 20px minmax(0, 1fr);
  align-items: center;
  overflow: hidden;
  border: 1px solid #b8c8cd;
  border-radius: 4px;
  background: #fff;
  color: #74868c;
}
.tube-designer-cutting-search > span { text-align: center; }
.tube-designer-cutting-search input { min-width: 0; height: 27px; padding: 3px 6px 3px 0; border: 0; outline: 0; color: #29424b; background: transparent; font-size: 10px; }
.tube-designer-cutting-filters { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 4px; }
.tube-designer-cutting-filters button {
  min-width: 0;
  height: 24px;
  padding: 2px 3px;
  overflow: hidden;
  border: 1px solid #bdcbd0;
  border-radius: 4px;
  background: #fff;
  color: #61757c;
  font-size: 8px;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.tube-designer-cutting-filters button[aria-pressed="true"] { border-color: #52a99f; background: #e3f2f0; color: #146e65; }
.tube-designer-cutting-filters b { font-size: inherit; font-weight: 500; }
.tube-designer-cutting-select-actions { display: flex; align-items: center; justify-content: space-between; gap: 6px; color: #72838a; font-size: 8px; }
.tube-designer-cutting-select-actions > div { display: flex; gap: 3px; }
.tube-designer-cutting-select-actions button { padding: 1px 3px; border: 0; background: transparent; color: #178f82; font-size: 8px; }
.tube-designer-cutting-group-list { min-height: 0; padding: 7px; overflow: auto; }
.tube-designer-cutting-group { overflow: hidden; margin-bottom: 6px; border: 1px solid #c8d6da; border-radius: 5px; background: #fff; }
.tube-designer-cutting-group > summary { display: grid; grid-template-columns: 16px minmax(0, 1fr) auto; align-items: center; gap: 6px; min-height: 38px; padding: 5px 7px; cursor: pointer; list-style: none; background: #e9f2f2; }
.tube-designer-cutting-group > summary::-webkit-details-marker { display: none; }
.tube-designer-cutting-group > summary > span { display: grid; min-width: 0; gap: 1px; }
.tube-designer-cutting-group > summary strong,
.tube-designer-cutting-group > summary small { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-cutting-group > summary strong { color: #176f68; font-size: 10px; }
.tube-designer-cutting-group > summary small { color: #698087; font-size: 8px; }
.tube-designer-cutting-group > summary > b { color: #547078; font-size: 8px; font-weight: 600; }
.tube-designer-cutting-group input { width: 13px; height: 13px; margin: 0; accent-color: #178f82; }
.tube-designer-cutting-part { display: grid; grid-template-columns: 16px minmax(0, 1fr); align-items: center; gap: 5px; min-height: 39px; padding: 3px 6px; border-top: 1px solid #e4eaec; }
.tube-designer-cutting-part.in-active-plan { background: #f5faf9; }
.tube-designer-cutting-part.active { background: #fff4b8; box-shadow: inset 4px 0 #ffd400; }
.tube-designer-cutting-part > button { display: grid; grid-template-columns: minmax(0, 1fr) auto auto; align-items: center; gap: 7px; min-width: 0; padding: 2px 0; border: 0; background: transparent; color: inherit; text-align: left; }
.tube-designer-cutting-part > button > span { display: grid; min-width: 0; gap: 2px; }
.tube-designer-cutting-part > button > span:last-child { justify-items: end; }
.tube-designer-nesting-part-locations { display: flex !important; grid-auto-flow: column; align-items: center; gap: 3px !important; }
.tube-designer-nesting-part-locations i { display: grid; width: 17px; height: 17px; box-sizing: border-box; place-items: center; border: 3px solid var(--location-color); border-radius: 50%; background: #fff; color: #2d444c; font-size: 7px; font-style: normal; font-weight: 700; }
.tube-designer-nesting-part-locations small { color: #62767d; font-size: 7px; }
.tube-designer-cutting-part strong,
.tube-designer-cutting-part small { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-cutting-part strong { color: #304a53; font-size: 9px; }
.tube-designer-cutting-part small { color: #7a8c91; font-size: 7px; }
.tube-designer-cutting-empty { display: grid; place-content: center; justify-items: center; gap: 5px; min-height: 110px; padding: 18px; color: #788a90; text-align: center; }
.tube-designer-cutting-empty strong { color: #4f676f; font-size: 11px; }
.tube-designer-cutting-empty span { font-size: 9px; line-height: 1.5; }

.tube-designer-cutting-inspector-scroll { min-height: 0; padding: 8px; overflow: auto; }
.tube-designer-cutting-inspector-scroll > section { margin-bottom: 8px; overflow: hidden; border: 1px solid #cbd7da; border-radius: 5px; background: #fff; }
.tube-designer-cutting-inspector-scroll h3 { display: flex; align-items: center; justify-content: space-between; gap: 8px; min-height: 30px; margin: 0; padding: 5px 8px; border-bottom: 1px solid #dce4e6; color: #38545c; background: #edf2f3; font-size: 10px; }
.tube-designer-cutting-inspector-scroll h3 span { color: #75868c; font-size: 8px; font-weight: 400; }
.tube-designer-cutting-inspector-scroll dl { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 1px; margin: 0; background: #dfe6e8; }
.tube-designer-cutting-inspector-scroll dl > div { display: grid; min-width: 0; gap: 2px; padding: 7px 8px; background: #fff; }
.tube-designer-cutting-inspector-scroll dl > div.wide { grid-column: 1 / -1; }
.tube-designer-cutting-inspector-scroll dt { color: #7a8b91; font-size: 8px; }
.tube-designer-cutting-inspector-scroll dd { margin: 0; overflow: hidden; color: #2d4851; font-size: 9px; font-weight: 600; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-cutting-status { padding: 3px 6px; border: 1px solid #70b7ad; border-radius: 10px; background: #e2f2f0; color: #176f68; font-size: 8px; font-style: normal; }
.tube-designer-cutting-order { display: grid; }
.tube-designer-cutting-order > button { display: grid; grid-template-columns: 22px minmax(0, 1fr); align-items: center; gap: 6px; min-height: 36px; padding: 4px 7px; border: 0; border-top: 1px solid #e5ebed; background: #fff; color: inherit; cursor: pointer; text-align: left; }
.tube-designer-cutting-order > button:first-child { border-top: 0; }
.tube-designer-cutting-order > button:hover { background: #edf6f5; }
.tube-designer-cutting-order > button.active { background: #fff2a6; box-shadow: inset 4px 0 #ffd400; }
.tube-designer-cutting-order > button > b { display: grid; width: 22px; height: 22px; box-sizing: border-box; place-items: center; border: 3px solid transparent; border-radius: 50%; background: #fff; color: #243f48; font-size: 8px; }
.tube-designer-cutting-order > button.active > b { border-color: #ffd400 !important; background: #ffd400; color: #3d3100; box-shadow: 0 0 0 2px rgba(255, 212, 0, .28); }
.tube-designer-cutting-order > button > span { display: grid; min-width: 0; gap: 2px; }
.tube-designer-cutting-order strong,
.tube-designer-cutting-order small { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-cutting-order strong { color: #314b54; font-size: 9px; }
.tube-designer-cutting-order small { color: #798b91; font-size: 8px; }

/* Reserve the top-right 116px navigation cube plus its inset and a 12px gap. */
.tube-designer-cutting-scene-header { position: absolute; z-index: 4; top: 10px; left: 10px; right: 140px; display: flex; flex-wrap: wrap; align-items: center; justify-content: space-between; gap: 8px 12px; pointer-events: none; }
.tube-designer-cutting-scene-title { display: grid; flex: 1 1 200px; min-width: 0; gap: 2px; max-width: min(480px, 100%); padding: 8px 10px; border: 1px solid rgba(126, 164, 176, .3); border-radius: 6px; background: rgba(13, 34, 42, .84); color: #e7f1f3; box-shadow: 0 5px 16px rgba(0, 0, 0, .14); backdrop-filter: blur(7px); }
.tube-designer-cutting-scene-title > span { color: #65c9bd; font-size: 8px; }
.tube-designer-cutting-scene-title > strong,
.tube-designer-cutting-scene-title > small { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-cutting-scene-title > strong { font-size: 12px; }
.tube-designer-cutting-scene-title > small { color: #9eb5bc; font-size: 8px; }
.tube-designer-cutting-scene-actions { display: flex; flex: 0 0 auto; gap: 5px; pointer-events: auto; }
.tube-designer-cutting-scene-actions button { min-height: 27px; padding: 3px 8px; border: 1px solid rgba(130, 167, 179, .55); border-radius: 4px; background: rgba(239, 246, 247, .94); color: #2c4851; font-size: 9px; }
.tube-designer-cutting-scene-summary { position: absolute; z-index: 4; left: 10px; bottom: 10px; display: grid; grid-template-columns: repeat(4, minmax(72px, 1fr)); overflow: hidden; border: 1px solid rgba(126, 164, 176, .32); border-radius: 6px; background: rgba(13, 34, 42, .84); color: #e7f1f3; backdrop-filter: blur(7px); pointer-events: none; }
.tube-designer-cutting-scene-summary > span { display: grid; gap: 2px; min-width: 72px; padding: 7px 9px; border-left: 1px solid rgba(140, 174, 185, .2); }
.tube-designer-cutting-scene-summary > span:first-child { border-left: 0; }
.tube-designer-cutting-scene-summary small { color: #8ba6ae; font-size: 7px; }
.tube-designer-cutting-scene-summary strong { font-size: 9px; }
.tube-designer-cutting-scene-help { position: absolute; z-index: 4; right: 10px; bottom: 10px; padding: 4px 7px; border-radius: 4px; background: rgba(12, 30, 37, .65); color: #849fa7; font-size: 8px; pointer-events: none; }
.tube-designer-parts-panel { height: 100%; box-sizing: border-box; grid-template-rows: auto auto auto minmax(0, 1fr); gap: 10px; padding: 12px; }
.tube-designer-parts-heading { display: flex; align-items: center; justify-content: space-between; gap: 8px; }
.tube-designer-parts-heading > div:first-child { display: grid; gap: 2px; min-width: 0; }
.tube-designer-parts-heading > div:last-child { display: flex; gap: 5px; }
.tube-designer-parts-heading button { min-height: 29px; padding: 4px 8px; white-space: nowrap; font-size: 10px; }
.tube-designer-parts-nest-button { display: inline-flex; align-items: center; gap: 6px; }
.tube-designer-parts-nest-button b {
  display: inline-grid;
  min-width: 16px;
  height: 16px;
  place-items: center;
  padding: 0 3px;
  border-radius: 8px;
  background: rgba(255, 255, 255, .22);
  font-size: 8px;
}
.tube-designer-parts-summary {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  overflow: hidden;
  border: 1px solid #c9d7db;
  border-radius: 7px;
  background: #fff;
}
.tube-designer-parts-summary > span { display: grid; gap: 3px; padding: 8px 9px; border-left: 1px solid #e1e8ea; }
.tube-designer-parts-summary > span:first-child { border-left: 0; }
.tube-designer-parts-summary small { color: #71838a; font-size: 8px; }
.tube-designer-parts-summary strong { color: #21434b; font-size: 11px; font-variant-numeric: tabular-nums; }
.tube-designer-parts-tools { display: grid; gap: 7px; }
.tube-designer-parts-search {
  display: grid;
  grid-template-columns: auto minmax(0, 1fr);
  align-items: center;
  gap: 5px;
  height: 31px;
  box-sizing: border-box;
  padding: 0 8px;
  border: 1px solid #bdccd1;
  border-radius: 5px;
  background: #fff;
  color: #6e8188;
}
.tube-designer-parts-search:focus-within { border-color: #4caaa0; box-shadow: 0 0 0 2px rgba(23, 143, 130, .1); }
.tube-designer-parts-search > span { font-size: 16px; line-height: 1; transform: rotate(-18deg); }
.tube-designer-parts-search input { min-width: 0; border: 0; outline: 0; background: transparent; color: #263e47; font-size: 10px; }
.tube-designer-parts-search input::placeholder { color: #8a999e; }
.tube-designer-parts-filter { display: flex; gap: 5px; overflow-x: auto; }
.tube-designer-parts-filter button {
  display: inline-flex;
  flex: 0 0 auto;
  align-items: center;
  gap: 4px;
  min-height: 26px;
  padding: 3px 7px;
  border: 1px solid #c5d2d6;
  border-radius: 13px;
  background: #f8fafb;
  color: #61757c;
  cursor: pointer;
  font-size: 9px;
}
.tube-designer-parts-filter button b { font-size: 8px; font-weight: 600; font-variant-numeric: tabular-nums; }
.tube-designer-parts-filter button[aria-pressed="true"] { border-color: #178f82; background: #e2f2f0; color: #126f66; }
.tube-designer-parts-selection-bar { display: flex; align-items: center; justify-content: space-between; gap: 8px; }
.tube-designer-parts-selection-bar > span { color: #657a81; font-size: 9px; }
.tube-designer-parts-selection-bar > div { display: flex; gap: 3px; }
.tube-designer-parts-selection-bar button {
  padding: 2px 5px;
  border: 0;
  background: transparent;
  color: #167c72;
  cursor: pointer;
  font-size: 9px;
}
.tube-designer-parts-selection-bar button:disabled { color: #aab5b9; cursor: default; }
.tube-designer-stock-groups { display: grid; align-content: start; gap: 8px; min-height: 0; overflow: auto; }
.tube-designer-stock-group { flex: none; overflow: hidden; border: 1px solid #c7d5da; border-radius: 7px; background: #fff; }
.tube-designer-stock-group > summary {
  display: grid;
  grid-template-columns: auto minmax(0, 1fr) auto;
  align-items: center;
  gap: 8px;
  padding: 9px;
  cursor: pointer;
  list-style: none;
  background: #eaf3f2;
}
.tube-designer-stock-group > summary::-webkit-details-marker { display: none; }
.tube-designer-stock-group > summary > span { display: grid; gap: 2px; min-width: 0; }
.tube-designer-stock-group > summary strong { overflow: hidden; color: #176f68; font-size: 11px; text-overflow: ellipsis; }
.tube-designer-stock-group > summary small { overflow: hidden; color: #637980; font-size: 9px; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-stock-group-totals { justify-items: end; }
.tube-designer-stock-group-totals strong { color: #3b5d64 !important; font-size: 9px !important; font-variant-numeric: tabular-nums; }
.tube-designer-stock-group-totals small { font-size: 8px !important; }
.tube-designer-stock-part-list { display: grid; }
.tube-designer-stock-part {
  display: grid;
  grid-template-columns: auto minmax(0, 1fr);
  align-items: center;
  gap: 5px;
  padding-left: 8px;
  border-top: 1px solid #e2e8ea;
}
.tube-designer-stock-part.selected { background: #eff8f7; box-shadow: inset 3px 0 #178f82; }
.tube-designer-stock-part > button {
  display: grid;
  grid-template-columns: 62px minmax(0, 1fr) auto;
  align-items: center;
  gap: 7px;
  min-width: 0;
  padding: 6px 7px 6px 2px;
  border: 0;
  background: transparent;
  color: #243a43;
  cursor: pointer;
  text-align: left;
}
.tube-designer-stock-part > button canvas { width: 58px; height: 38px; border-radius: 4px; background: #142a33; }
.tube-designer-stock-part-identity { display: grid; gap: 3px; min-width: 0; }
.tube-designer-stock-part-identity > span { display: flex; align-items: center; gap: 5px; min-width: 0; }
.tube-designer-stock-part > button strong,
.tube-designer-stock-part > button small { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-stock-part-identity strong { min-width: 0; font-size: 10px; }
.tube-designer-stock-part > button small { color: #74858c; font-size: 8px; }
.tube-designer-stock-part-measure { display: grid; justify-items: end; gap: 2px; }
.tube-designer-stock-part-measure strong { color: #315d62; font-size: 9px !important; font-variant-numeric: tabular-nums; }
.tube-designer-stock-part-measure small { font-size: 8px !important; }
.tube-designer-process-badge {
  display: inline-flex;
  flex: 0 0 auto;
  align-items: center;
  min-height: 15px;
  box-sizing: border-box;
  padding: 1px 5px;
  border: 1px solid #d0a34e;
  border-radius: 8px;
  background: #fff4dc;
  color: #986916;
  font-size: 7px;
  font-style: normal;
  font-weight: 600;
  white-space: nowrap;
}
.tube-designer-process-badge.straight { border-color: #88bbb4; background: #e7f4f2; color: #176f68; }
.tube-designer-parts-empty { align-self: start; }
.tube-designer-parts-no-results { margin-top: 2px; }
.tube-designer-parts-viewport-header {
  position: absolute;
  z-index: 4;
  top: 12px;
  left: 12px;
  right: 12px;
  display: grid;
  grid-template-columns: minmax(170px, 1fr) minmax(300px, 1.6fr) auto;
  align-items: center;
  gap: 12px;
  padding: 10px 11px;
  border: 1px solid rgba(124, 164, 178, .28);
  border-radius: 8px;
  background: rgba(15, 35, 43, .9);
  color: #e8f2f4;
  box-shadow: 0 7px 24px rgba(0, 0, 0, .16);
  backdrop-filter: blur(9px);
  pointer-events: auto;
}
.tube-designer-part-scene-identity { display: grid; gap: 3px; min-width: 0; }
.tube-designer-part-scene-identity > strong { overflow: hidden; font-size: 13px; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-part-scene-identity > span { overflow: hidden; color: #abc1c8; font-size: 9px; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-part-scene-kicker { display: flex; align-items: center; gap: 6px; min-width: 0; }
.tube-designer-part-scene-kicker > span:last-child { overflow: hidden; color: #8facb5; font-size: 8px; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-part-scene-kicker .tube-designer-process-badge { border-color: rgba(231, 182, 89, .54); background: rgba(169, 107, 18, .26); color: #f0c66f; }
.tube-designer-part-scene-kicker .tube-designer-process-badge.straight { border-color: rgba(112, 205, 191, .44); background: rgba(26, 128, 116, .27); color: #89d8ce; }
.tube-designer-part-scene-facts {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  overflow: hidden;
  border-left: 1px solid rgba(164, 191, 199, .16);
  border-right: 1px solid rgba(164, 191, 199, .16);
}
.tube-designer-part-scene-facts > span { display: grid; gap: 2px; min-width: 0; padding: 0 9px; }
.tube-designer-part-scene-facts small { color: #7f9da6; font-size: 7px; }
.tube-designer-part-scene-facts strong { overflow: hidden; color: #dbeaed; font-size: 9px; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-parts-view-actions { display: flex; gap: 5px; }
.tube-designer-parts-view-actions button,
.tube-designer-parts-measurement-heading button {
  min-height: 27px;
  padding: 3px 8px;
  border: 1px solid #718c96;
  border-radius: 4px;
  background: #f7fafb;
  color: #29434e;
  cursor: pointer;
  font-size: 9px;
}
.tube-designer-parts-view-actions button[aria-pressed="true"] { border-color: #73b7ae; background: #e3f2f0; color: #146e65; }
.tube-designer-parts-measurement {
  position: absolute;
  z-index: 4;
  right: 12px;
  bottom: 12px;
  display: grid;
  width: min(320px, calc(100% - 24px));
  max-height: calc(100% - 96px);
  box-sizing: border-box;
  overflow: auto;
  border: 1px solid rgba(117, 151, 162, .5);
  border-radius: 8px;
  background: rgba(239, 245, 246, .96);
  color: #29434e;
  box-shadow: 0 10px 28px rgba(1, 14, 19, .24);
  backdrop-filter: blur(8px);
  pointer-events: auto;
}
.tube-designer-part-manufacturing-card,
.tube-designer-part-dimension-card { display: grid; gap: 8px; padding: 11px; }
.tube-designer-part-dimension-card { border-top: 1px solid #cbd8dc; }
.tube-designer-parts-measurement-heading { display: flex; align-items: center; justify-content: space-between; gap: 8px; }
.tube-designer-parts-measurement-heading > div { display: grid; gap: 2px; }
.tube-designer-parts-measurement-heading strong { font-size: 11px; }
.tube-designer-parts-measurement-heading span { color: #6d8087; font-size: 8px; }
.tube-designer-part-ready-state {
  padding: 3px 7px;
  border: 1px solid #80bcb4;
  border-radius: 9px;
  background: #e1f1ef;
  color: #176f68;
  font-size: 8px;
  font-style: normal;
}
.tube-designer-part-property-grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 5px; }
.tube-designer-part-property-grid > span { display: grid; gap: 2px; min-width: 0; padding: 7px 8px; border: 1px solid #cedadd; border-radius: 4px; background: #fff; }
.tube-designer-part-property-grid > span.wide { grid-column: span 2; }
.tube-designer-part-property-grid small { color: #72838a; font-size: 8px; }
.tube-designer-part-property-grid strong { overflow: hidden; color: #29434e; font-size: 9px; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-part-dimension-overview > span:nth-child(2) { grid-column: auto; }
.tube-designer-part-no-holes { padding: 8px; border: 1px dashed #c5d2d6; border-radius: 4px; color: #73868d; font-size: 9px; text-align: center; }
.tube-designer-part-measurement-loading { display: flex; align-items: center; gap: 8px; color: #627980; font-size: 10px; }
.tube-designer-part-measurement-loading i { width: 12px; height: 12px; border: 2px solid #9ec1be; border-top-color: #178f82; border-radius: 50%; animation: tube-designer-export-spin .8s linear infinite; }
.tube-designer-parts-scene-help {
  position: absolute;
  z-index: 4;
  left: 112px;
  bottom: 18px;
  padding: 5px 8px;
  border: 1px solid rgba(139, 169, 179, .22);
  border-radius: 5px;
  background: rgba(14, 31, 38, .72);
  color: #8fabb4;
  font-size: 8px;
  pointer-events: none;
}
.tube-designer-parts-viewport-empty,
.tube-designer-nesting-empty {
  position: absolute;
  inset: 0;
  display: grid;
  place-content: center;
  gap: 7px;
  padding: 30px;
  color: #dce9ec;
  text-align: center;
  pointer-events: none;
}
.tube-designer-parts-viewport-empty span,
.tube-designer-nesting-empty span { max-width: 520px; color: #9fb8c0; font-size: 11px; line-height: 1.6; }
.tube-designer-nesting-panel { height: 100%; box-sizing: border-box; grid-template-rows: auto minmax(0, 1fr); }
.tube-designer-nesting-groups { display: grid; align-content: start; gap: 7px; min-height: 0; overflow: auto; }
.tube-designer-nesting-groups article { display: grid; gap: 3px; padding: 10px; border: 1px solid #cad7db; border-radius: 6px; background: #fff; }
.tube-designer-nesting-groups strong { color: #176f68; font-size: 11px; }
.tube-designer-nesting-groups span,
.tube-designer-nesting-groups small { color: #667b82; font-size: 9px; }
.tube-designer-nesting-inspector { height: 100%; box-sizing: border-box; grid-template-rows: auto minmax(0, 1fr); gap: 9px; padding: 12px; overflow: hidden; }
.tube-designer-nesting-inspector-heading { display: flex; align-items: center; justify-content: space-between; gap: 8px; }
.tube-designer-nesting-inspector-heading > div { display: grid; gap: 2px; min-width: 0; }
.tube-designer-nesting-inspector-heading > div > span { overflow: hidden; max-width: 220px; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-nesting-inspector-body { min-height: 0; overflow: auto; border: 1px solid #c9d6da; border-radius: 7px; background: #f0f4f5; }
.tube-designer-nesting-viewport-header { right: 12px; }

.tube-designer-bottom-splitter {
  position: relative;
  z-index: 6;
  grid-area: bottom-resize;
  min-width: 0;
  min-height: 5px;
  border-top: 1px solid #aeb8bc;
  border-bottom: 1px solid #aeb8bc;
  background: #c7ced1;
  cursor: ns-resize;
  user-select: none;
  touch-action: none;
}
.tube-designer-bottom-splitter::before {
  position: absolute;
  top: 1px;
  left: 50%;
  width: 46px;
  height: 2px;
  border-radius: 2px;
  background: #89979c;
  content: "";
  transform: translateX(-50%);
}
.tube-designer-nesting-bottom {
  z-index: 4;
  grid-area: bottom;
  min-width: 0;
  min-height: 0;
  display: grid;
  grid-template-rows: 31px minmax(0, 1fr);
  overflow: hidden;
  border-top: 1px solid #7d898d;
  color: #3d494e;
  background: #eef1f2;
  box-shadow: 0 -3px 10px rgba(0, 0, 0, .16);
}
.tube-designer-nesting-bottom > header { display: flex; align-items: center; justify-content: space-between; gap: 12px; padding: 0 10px; border-bottom: 1px solid #b9c2c5; background: linear-gradient(#f7f8f8, #dce2e4); }
.tube-designer-nesting-bottom > header > div { display: flex; align-items: baseline; gap: 8px; }
.tube-designer-nesting-bottom > header strong { color: #30474f; font-size: 12px; }
.tube-designer-nesting-bottom > header span { color: #77858a; font-size: 11px; }
.tube-designer-nesting-result-actions {display:flex;align-items:center;gap:10px;flex-shrink:0;}
.tube-designer-nesting-result-actions button {padding:4px 10px;border:1px solid #9cb8c0;border-radius:3px;background:#fff;color:#225663;font-size:11px;cursor:pointer;}
.tube-designer-nesting-result-actions button:hover {background:#e2f2ef;}
.tube-designer-nesting-result-actions button:disabled {opacity:.45;cursor:default;}
.tube-designer-nesting-result-table input[type="checkbox"] {width:15px;height:15px;accent-color:#148e82;cursor:pointer;vertical-align:middle;}
.tube-designer-nesting-result-table { min-height: 0; overflow: auto; background: #fff; }
.tube-designer-nesting-result-table .head,
.tube-designer-nesting-result-table .row {
  display: grid;
  grid-template-columns: 32px 42px minmax(150px, 1.15fr) minmax(180px, 1.4fr) 82px 82px 82px 62px minmax(120px, .8fr);
  align-items: center;
  min-height: 32px;
  border-bottom: 1px solid #d8dfe1;
  font-size: 12px;
}
.tube-designer-nesting-result-table .head { position: sticky; top: 0; z-index: 1; color: #66757a; background: #e4e8e9; }
.tube-designer-nesting-result-table .row {width:100%;background:#fff;}
.tube-designer-nesting-result-table .row:nth-child(odd) { background: #f7f9f9; }
.tube-designer-nesting-result-table .row:hover { background: #eef6f7; }
.tube-designer-nesting-result-table .row.active { background: #fff2a6; box-shadow: inset 4px 0 #ffd400; }
.tube-designer-nesting-result-table .row.locked { background: #fffaf0; }
.tube-designer-nesting-result-table .row.locked.active { background: #fff2a6; box-shadow: inset 4px 0 #ffd400; }
.tube-designer-nesting-plan-select {grid-column:3/-1;display:grid;grid-template-columns:minmax(150px,1.15fr) minmax(180px,1.4fr) 82px 82px 82px 62px minmax(120px,.8fr);align-items:center;align-self:stretch;width:100%;padding:0;border:0;background:transparent;color:inherit;font:inherit;text-align:left;cursor:pointer;}
.tube-designer-nesting-plan-select:focus-visible {outline:2px solid #148e82;outline-offset:-2px;}
.tube-designer-nesting-lock-heading { display: grid; place-items: center; color: #738187; }
.tube-designer-nesting-lock-heading svg { width: 14px; height: 14px; fill: none; stroke: currentColor; stroke-width: 1.8; stroke-linecap: round; stroke-linejoin: round; }
.tube-designer-nesting-lock-button { display: grid; width: 28px; height: 26px; place-items: center; margin: auto; padding: 0; border: 1px solid #a9b8bd; border-radius: 4px; background: #fff; color: #66777d; cursor: pointer; }
.tube-designer-nesting-lock-button svg { width: 16px; height: 16px; fill: none; stroke: currentColor; stroke-width: 1.8; stroke-linecap: round; stroke-linejoin: round; }
.tube-designer-nesting-lock-button:hover { border-color: #b98015; background: #fff8e5; color: #81570c; }
.tube-designer-nesting-lock-button[aria-pressed="true"] { border-color: #bd7c08; background: #f4b52f; color: #4c3300; }
.tube-designer-nesting-lock-button:disabled { opacity: .45; cursor: default; }
.tube-designer-nesting-result-table .head > span,
.tube-designer-nesting-plan-select > span,
.tube-designer-nesting-result-table .row > span { min-width: 0; padding: 0 8px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-nesting-result-table .utilization { display: grid; grid-template-columns: minmax(38px, 1fr) 34px; align-items: center; gap: 5px; }
.tube-designer-nesting-result-table .utilization i { display: block; width: var(--progress); max-width: 100%; height: 8px; background: #26958c; }
.tube-designer-nesting-result-table .utilization b { color: #507077; font-size: 11px; font-weight: 500; }
.tube-designer-nesting-result-table .empty { display: grid; place-content: center; gap: 4px; min-height: 82px; padding: 12px; color: #7c898e; font-size: 9px; text-align: center; }
.tube-designer-nesting-result-table .empty strong { color: #5f7076; font-size: 10px; }
.tube-designer-nesting-warning {padding:9px 12px;background:#fff2d9;color:#845715;font-size:12px;}
.tube-designer-nesting-unplaced {padding:8px 12px;background:#fff7ef;border-bottom:1px solid #e5c8a8;font-size:12px;color:#7c4d27;}
.tube-designer-nesting-unplaced summary {cursor:pointer;font-weight:600;}
.tube-designer-nesting-unplaced>div {display:grid;grid-template-columns:minmax(150px,1fr) 60px minmax(180px,2fr);gap:12px;padding:5px 0;}
.tube-designer-nesting-unplaced strong {font-weight:500;overflow-wrap:anywhere;}
.tube-designer-nesting-preview-status {position:absolute;top:94px;left:12px;max-width:calc(100% - 160px);color:#e4bf7e;font-size:12px;pointer-events:none;z-index:4;}
.tube-designer-nesting-preview-status:empty {display:none;}

.tube-designer-post-disassembly-dialog {
  position: relative;
  width: min(540px, calc(100vw - 48px));
  display: grid;
  justify-items: center;
  gap: 14px;
  box-sizing: border-box;
  padding: 28px;
  border: 1px solid #c3d0d5;
  border-radius: 10px;
  background: #f8fafb;
  color: var(--designer-ink);
  box-shadow: 0 22px 58px rgba(0, 0, 0, .34);
}
.tube-designer-post-disassembly-dialog > .tube-designer-dialog-close { position: absolute; top: 8px; right: 8px; }
.tube-designer-post-disassembly-icon { display: grid; width: 48px; height: 48px; place-items: center; border-radius: 50%; background: #dff2ec; color: #16806f; font-size: 24px; font-weight: 700; }
.tube-designer-post-disassembly-copy { display: grid; justify-items: center; gap: 6px; text-align: center; }
.tube-designer-post-disassembly-copy strong { font-size: 18px; }
.tube-designer-post-disassembly-copy span { max-width: 440px; color: #667b82; font-size: 11px; line-height: 1.6; }
.tube-designer-post-disassembly-summary { display: grid; grid-template-columns: repeat(3, minmax(90px, 1fr)); width: min(380px, 100%); overflow: hidden; border: 1px solid #cad7db; border-radius: 7px; background: #fff; }
.tube-designer-post-disassembly-summary > span { display: grid; justify-items: center; gap: 3px; padding: 9px; border-left: 1px solid #dde5e7; }
.tube-designer-post-disassembly-summary > span:first-child { border-left: 0; }
.tube-designer-post-disassembly-summary small { color: #72848a; font-size: 8px; }
.tube-designer-post-disassembly-summary strong { color: #24505a; font-size: 14px; }
.tube-designer-post-disassembly-dialog > footer { display: flex; justify-content: center; gap: 8px; }
.tube-designer-post-disassembly-dialog > footer button { min-width: 96px; }

.tube-designer-about-workspace .cam-context-pane,
.tube-designer-about-workspace .cam-info-pane { align-content: stretch; padding: 0; overflow: hidden; }
.tube-designer-about-panel { align-content: center; justify-items: center; height: 100%; box-sizing: border-box; text-align: center; }
.tube-designer-about-panel > p { max-width: 260px; margin: 0; color: #687b82; font-size: 10px; line-height: 1.65; }
.tube-designer-about-mark { display: grid; width: 58px; height: 58px; place-items: center; border-radius: 14px; background: linear-gradient(145deg, #1ba091, #176f78); color: #fff; font-size: 20px; font-weight: 700; box-shadow: 0 9px 22px rgba(23, 143, 130, .24); }
.tube-designer-about-panel dl,
.tube-designer-about-details dl { width: 100%; margin: 0; }
.tube-designer-about-panel dl > div,
.tube-designer-about-details dl > div { display: grid; grid-template-columns: 68px minmax(0, 1fr); gap: 8px; padding: 8px 0; border-bottom: 1px solid #dce4e6; text-align: left; }
.tube-designer-about-panel dt,
.tube-designer-about-details dt { color: #72858b; font-size: 9px; }
.tube-designer-about-panel dd,
.tube-designer-about-details dd { margin: 0; color: #2d4a53; font-size: 10px; }
.tube-designer-about-details { align-content: start; height: 100%; box-sizing: border-box; }
.tube-designer-about-viewport { position: absolute; inset: 0; display: grid; place-content: center; justify-items: center; gap: 8px; color: #dce9ec; text-align: center; pointer-events: none; }
.tube-designer-about-viewport strong { font-size: 24px; letter-spacing: .02em; }
.tube-designer-about-viewport span { color: #91aeb7; font-size: 11px; }
.tube-designer-about-symbol { position: relative; width: 150px; height: 58px; margin-bottom: 8px; }
.tube-designer-about-symbol i { position: absolute; top: 22px; width: 88px; height: 14px; border: 2px solid #5ebcaf; border-radius: 7px; background: rgba(65, 161, 149, .12); transform: rotate(-18deg); }
.tube-designer-about-symbol i:nth-child(1) { left: 0; }
.tube-designer-about-symbol i:nth-child(2) { left: 31px; transform: rotate(0deg); }
.tube-designer-about-symbol i:nth-child(3) { right: 0; transform: rotate(18deg); }
.tube-designer-production-upgrade { place-content: center; justify-items: center; gap: 9px; height: 100%; box-sizing: border-box; text-align: center; }
.tube-designer-production-upgrade > strong { color: #27454f; font-size: 14px; }
.tube-designer-production-upgrade > span:not(.tube-designer-production-lock),
.tube-designer-production-upgrade > small { max-width: 260px; color: #6a7f87; font-size: 10px; line-height: 1.6; }
.tube-designer-production-lock { display: grid; width: 42px; height: 42px; place-items: center; border: 2px solid #d39b3e; border-radius: 50%; color: #b77a18; font-size: 24px; }
.tube-designer-production-locked-viewport strong { color: #f1c36c; }

.tube-designer-sketch-workspace .cam-context-pane { padding: 0; overflow: hidden; background: #102832; }
.tube-designer-sketch-workspace .cam-info-pane { padding: 0; overflow: hidden; }
.tube-designer-sketch-workspace .cam-viewport { background: #0d2730; }
.tube-designer-sketch-workspace .cam-render-viewport-shell,
.tube-designer-sketch-workspace .cam-render-viewport { position: absolute; inset: 0; opacity: 0; pointer-events: none; }
.tube-designer-sketch-workspace .cam-viewcube { display: none; }

.tube-sketch-preview-panel {
  display: grid;
  grid-template-rows: auto minmax(0, 1fr) auto;
  width: 100%;
  height: 100%;
  min-height: 0;
  color: #dcebed;
  background: #102832;
}
.tube-sketch-preview-panel > header { display: grid; gap: 3px; padding: 13px 14px 11px; border-bottom: 1px solid rgba(142, 183, 190, .3); }
.tube-sketch-preview-panel > header strong { font-size: 13px; }
.tube-sketch-preview-panel > header span { color: #9ab8bd; font-size: 10px; line-height: 1.45; }
.tube-sketch-preview-stage { display: grid; min-height: 0; place-items: center; overflow: hidden; padding: 12px; }
.tube-sketch-preview-panel > footer { min-height: 42px; padding: 8px 12px; border-top: 1px solid rgba(142, 183, 190, .3); color: #8fc7c1; font-size: 10px; }
.tube-sketch-preview-panel > footer > span { display: block; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.tube-sketch-preview-panel > footer label { display: grid; gap: 5px; }
.tube-sketch-preview-panel > footer label > span { color: #86a4aa; }
.tube-sketch-preview-panel select { width: 100%; min-height: 30px; padding: 3px 7px; border: 1px solid rgba(134, 184, 188, .36); border-radius: 5px; background: #173842; color: #e0eeee; font-size: 10px; }
.tube-sketch-preview-svg { display: block; width: min(100%, 300px); max-height: 100%; }
.tube-sketch-preview-front path { fill: rgba(73, 136, 144, .76); stroke: #71d0c5; stroke-width: 2; }
.tube-sketch-preview-back path { fill: rgba(133, 171, 178, .55); stroke: #8bd6cd; stroke-width: 1.5; }
.tube-sketch-preview-connector { fill: none; stroke: #77c9c0; stroke-width: 1.5; }
.tube-sketch-metal-top { fill: #89a8ae; }
.tube-sketch-metal-side { fill: #355963; }
.tube-sketch-metal-front { fill: #557b84; stroke: #73cec5; stroke-width: 1.5; }
.tube-sketch-preview-cut { fill: rgba(9, 35, 43, .78); stroke: #61d0c4; stroke-width: 2; }
.tube-sketch-preview-cut text { fill: #61d0c4; stroke: none; font-size: 15px; }
.tube-sketch-preview-empty { display: grid; justify-items: center; gap: 7px; max-width: 230px; color: #d7e7e9; text-align: center; }
.tube-sketch-preview-empty-icon { display: grid; width: 42px; height: 42px; place-items: center; border: 1px solid rgba(114, 204, 195, .42); border-radius: 50%; color: #65cfc4; font-size: 22px; }
.tube-sketch-preview-empty strong { font-size: 12px; }
.tube-sketch-preview-empty span:last-child { color: #8eaeb4; font-size: 10px; line-height: 1.6; }

.tube-sketch-canvas-shell { position: absolute; inset: 0; z-index: 6; display: grid; grid-template-rows: auto minmax(0, 1fr) 34px; color: #dcebed; background: #0d2730; }
.tube-sketch-canvas-header { display: flex; align-items: center; justify-content: space-between; gap: 14px; min-width: 0; min-height: 48px; padding: 8px 12px; border-bottom: 1px solid rgba(142, 183, 190, .34); }
.tube-sketch-canvas-header > div:first-child { display: grid; gap: 2px; min-width: 0; }
.tube-sketch-canvas-header strong { overflow: hidden; font-size: 12px; text-overflow: ellipsis; white-space: nowrap; }
.tube-sketch-canvas-header span { overflow: hidden; color: #8eabb1; font-size: 9px; text-overflow: ellipsis; white-space: nowrap; }
.tube-sketch-mode-switch { display: flex; flex: none; gap: 4px; }
.tube-sketch-mode-switch button { min-height: 29px; padding: 4px 9px; border: 1px solid transparent; border-radius: 5px; background: transparent; color: #a6bec2; cursor: pointer; font-size: 10px; }
.tube-sketch-mode-switch button[aria-pressed="true"] { border-color: rgba(90, 200, 188, .38); background: rgba(23, 143, 130, .28); color: #dff8f5; }
.tube-sketch-canvas-stage { position: relative; min-width: 0; min-height: 0; overflow: hidden; }
.tube-sketch-canvas { display: block; width: 100%; height: 100%; min-height: 360px; touch-action: none; cursor: crosshair; }
.tube-sketch-canvas pattern path { fill: none; stroke: rgba(142, 183, 190, .09); stroke-width: 1; }
.tube-sketch-axis line { stroke: rgba(143, 177, 183, .45); stroke-width: 1; }
.tube-sketch-axis text,
.tube-sketch-side-guide text { fill: #88aeb4; font-size: 11px; }
.tube-sketch-side-guide rect { fill: rgba(129, 171, 179, .06); stroke: rgba(142, 183, 190, .48); stroke-width: 1; }
.tube-sketch-entity { fill: rgba(26, 153, 141, .08); stroke: #27afa1; stroke-width: 2; vector-effect: non-scaling-stroke; pointer-events: visibleStroke; }
.tube-sketch-entity.selected { fill: rgba(31, 180, 165, .16); stroke: #60ddd0; stroke-width: 3; }
text.tube-sketch-entity { fill: #4fd0c2; stroke: none; font-size: 20px; pointer-events: all; }
.tube-sketch-handles circle { fill: #eefbfa; stroke: #159a8d; stroke-width: 2; vector-effect: non-scaling-stroke; pointer-events: none; }
.tube-sketch-draft-preview .tube-sketch-entity { opacity: .72; stroke-dasharray: 7 5; pointer-events: none; }
.tube-sketch-canvas-empty { position: absolute; inset: 0; display: grid; place-content: center; justify-items: center; gap: 6px; padding: 24px; background: rgba(13, 39, 48, .8); text-align: center; pointer-events: none; }
.tube-sketch-canvas-empty strong { font-size: 13px; }
.tube-sketch-canvas-empty span { max-width: 360px; color: #95b2b7; font-size: 10px; line-height: 1.6; }
.tube-sketch-canvas-status { display: flex; align-items: center; gap: 15px; min-width: 0; padding: 0 12px; border-top: 1px solid rgba(142, 183, 190, .34); color: #93b2b7; font-size: 9px; }
.tube-sketch-canvas-status span:first-child { color: #66d1c5; }
.tube-sketch-canvas-status span:last-child { margin-left: auto; color: #73bbb3; }

.tube-sketch-property-panel { display: grid; grid-template-rows: auto minmax(0, 1fr) auto; width: 100%; height: 100%; min-height: 0; background: #f6f8f9; color: var(--designer-ink); }
.tube-sketch-property-panel > header { display: grid; gap: 3px; padding: 13px 14px; border-bottom: 1px solid #cdd8dc; }
.tube-sketch-property-panel > header strong { font-size: 13px; }
.tube-sketch-property-panel > header span { color: var(--designer-muted); font-size: 10px; }
.tube-sketch-property-body { min-height: 0; padding: 13px; overflow: auto; }
.tube-sketch-property-empty { display: grid; justify-items: center; gap: 7px; margin-top: 40px; color: #526870; text-align: center; }
.tube-sketch-property-empty-icon { color: #219b8f; font-size: 34px; }
.tube-sketch-property-empty strong { font-size: 12px; }
.tube-sketch-property-empty span:last-child { max-width: 230px; color: #71848b; font-size: 10px; line-height: 1.5; }
.tube-sketch-property-section { display: grid; gap: 10px; margin-bottom: 18px; }
.tube-sketch-property-title { display: flex; align-items: center; justify-content: space-between; gap: 8px; }
.tube-sketch-property-title strong { font-size: 11px; }
.tube-sketch-property-title span { color: #74868d; font-size: 9px; }
.tube-sketch-property-section dl { display: grid; gap: 1px; margin: 0; padding: 8px; border-radius: 5px; background: #eaf1f2; }
.tube-sketch-property-section dl > div { display: flex; align-items: center; justify-content: space-between; min-height: 24px; gap: 8px; }
.tube-sketch-property-section dt { color: #51666e; font-size: 10px; }
.tube-sketch-property-section dd { margin: 0; color: #688087; font-size: 10px; font-variant-numeric: tabular-nums; }
.tube-sketch-property-grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 8px; }
.tube-sketch-property-grid label { display: grid; gap: 4px; min-width: 0; color: #687b82; font-size: 9px; }
.tube-sketch-property-grid label.wide { grid-column: 1 / -1; }
.tube-sketch-property-grid input { box-sizing: border-box; width: 100%; min-height: 31px; padding: 4px 30px 4px 7px; border: 1px solid #c8d4d8; border-radius: 5px; background: #fff; color: #29414a; font-size: 11px; }
.tube-sketch-property-grid label.wide input { padding-right: 7px; }
.tube-sketch-property-input { position: relative; display: block; }
.tube-sketch-property-input small { position: absolute; top: 50%; right: 7px; color: #83949a; font-size: 8px; transform: translateY(-50%); pointer-events: none; }
.tube-sketch-property-actions { display: grid; grid-template-columns: 1fr 1fr; gap: 7px; }
.tube-sketch-property-panel > footer { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; padding: 11px 13px; border-top: 1px solid #cbd5da; background: #fff; }
.tube-sketch-property-panel > footer button { min-width: 0; }

@media (max-width: 1180px) {
  .tube-designer-parts-viewport-header { grid-template-columns: minmax(0, 1fr) auto; }
  .tube-designer-part-scene-facts { display: none; }
}

@media (max-width: 900px) {
  .tube-designer-breakdown-footer { grid-template-columns: 1fr auto auto; }
  .tube-designer-breakdown-footer > span { display: none; }
  .tube-designer-config-body { grid-template-columns: 280px minmax(0, 1fr); }
  .tube-designer-config-parameters .tube-designer-field-grid { grid-template-columns: repeat(2, minmax(0, 1fr)); }
  .tube-designer-part-inspection-body { grid-template-columns: minmax(0, 1fr) 300px; }
  .tube-designer-production-workspace { grid-template-columns: 300px 5px minmax(360px, 1fr) 5px 260px; }
  .tube-designer-parts-view-actions button:first-child { display: none; }
}
`;
