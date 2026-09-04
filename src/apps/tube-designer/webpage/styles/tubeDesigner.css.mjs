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
.tube-designer-workspace .cam-viewport { margin: 0; border: 0; }

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
  margin: 14px 16px;
  overflow: auto;
  border: 1px solid #bfcbd1;
  background: #fff;
}

.tube-designer-sheet {
  width: 100%;
  min-width: 1180px;
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
  grid-template-columns: minmax(280px, 1fr) auto auto;
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
  position: absolute;
  inset: 0;
  z-index: 960;
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
  grid-template-rows: auto minmax(0, 1fr);
  align-content: stretch;
  gap: 0;
  box-sizing: border-box;
  height: 100%;
  min-height: 0;
  padding: 0;
  overflow: hidden;
  background: #e8edef;
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
  align-content: start;
  gap: 3px;
  min-height: 0;
  padding: 5px;
  overflow: auto;
  scrollbar-gutter: stable;
  scrollbar-width: thin;
}

.tube-designer-parameter-section {
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

.tube-designer-modal-backdrop {
  position: fixed;
  inset: 0;
  z-index: 900;
  display: grid;
  place-items: center;
  padding: 28px;
  background: rgba(12, 25, 31, .58);
  backdrop-filter: blur(3px);
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

.tube-designer-config-dialog { width: min(1120px, calc(100vw - 56px)); height: min(780px, calc(100vh - 56px)); }
.tube-designer-selection-dialog { width: min(1080px, calc(100vw - 56px)); height: min(680px, calc(100vh - 56px)); }

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
.tube-designer-template-list { display: grid; align-content: start; gap: 9px; min-height: 0; overflow: auto; }
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
.tube-designer-part-inspection-stage { display: grid; grid-template-rows: auto minmax(0, 1fr); min-width: 0; min-height: 0; background: #13252d; }
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
.tube-designer-measurement-clear,
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
.tube-designer-measurement-clear:hover,
.tube-designer-measurement-heading button:hover { border-color: var(--designer-accent); color: var(--designer-accent); }
.tube-designer-measurement-heading button[aria-pressed="true"] { border-color: #17877d; background: #17877d; color: #fff; }
.tube-designer-measurement-clear:disabled { opacity: .45; cursor: default; }
.tube-designer-part-inspection-viewport { position: relative; min-width: 0; min-height: 0; overflow: hidden; }
.tube-designer-part-inspection-dialog[data-measurement-enabled="true"] .tube-designer-part-inspection-viewport { box-shadow: inset 0 0 0 2px rgba(227, 168, 63, .78); }
.tube-designer-part-inspection-dialog[data-measurement-enabled="true"] .tube-designer-part-inspection-viewport .icax-three-viewport-canvas { cursor: crosshair; }
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
.tube-designer-automatic-dimension-section,
.tube-designer-manual-measurement-section { display: grid; gap: 10px; }
.tube-designer-manual-measurement-section { padding-top: 13px; border-top: 1px solid #ccd7db; }
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
.tube-designer-measurement-result {
  display: grid;
  gap: 7px;
  padding: 14px;
  border: 1px solid #b9c9ce;
  border-left: 4px solid #e3a83f;
  border-radius: 6px;
  background: #fff;
}
.tube-designer-measurement-result > strong { color: #193c4b; font-size: 16px; font-variant-numeric: tabular-nums; }
.tube-designer-measurement-result > span { color: #526871; font-size: 11px; }
.tube-designer-measurement-result > small { color: #72838a; font-size: 9px; line-height: 1.6; }
.tube-designer-measurement-result dl { display: grid; gap: 4px; margin: 4px 0; }
.tube-designer-measurement-result dl > div { display: flex; justify-content: space-between; gap: 12px; padding-top: 4px; border-top: 1px solid #e3eaec; }
.tube-designer-measurement-result dt { color: #6b7d84; }
.tube-designer-measurement-result dd { margin: 0; font-weight: 600; font-variant-numeric: tabular-nums; }
.tube-designer-measurement-clear { width: 100%; }
.tube-designer-measurement-help { display: grid; align-content: start; gap: 6px; padding-top: 5px; color: #60747c; font-size: 10px; }
.tube-designer-measurement-help strong { color: #344d57; font-size: 11px; }
.tube-designer-measurement-help small { margin-top: 5px; line-height: 1.5; }
.tube-designer-part-inspection-link {
  max-width: 270px;
  padding: 0;
  overflow-wrap: anywhere;
  border: 0;
  background: transparent;
  color: #176f83;
  font: inherit;
  font-weight: 600;
  text-align: left;
  text-decoration: underline;
  text-decoration-color: rgba(23, 111, 131, .34);
  text-underline-offset: 2px;
  cursor: pointer;
}
.tube-designer-part-inspection-link:hover { color: #0c8d80; text-decoration-color: currentColor; }

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
.tube-designer-tree-toggle {
  display: inline;
  padding: 0;
  border: 0;
  background: transparent;
  color: inherit;
  font: inherit;
  font-weight: 600;
  cursor: pointer;
}
.tube-designer-tree-toggle:hover { color: var(--designer-accent); text-decoration: underline; }
.tube-designer-tree-kind { font-weight: 600; }
.tube-designer-product-row td { border-color: #477086; background: #234f68; color: #f4f9fb; }
.tube-designer-product-row:hover td { background: #2b607c !important; }
.tube-designer-category-row td { border-color: #5d8371; background: #35684f; color: #f3faf6; }
.tube-designer-category-row:hover td { background: #40785e !important; }
.tube-designer-product-row .tube-designer-merged-value,
.tube-designer-category-row .tube-designer-merged-value { color: rgba(244, 250, 247, .78); }
.tube-designer-product-row .tube-designer-tree-toggle:hover,
.tube-designer-category-row .tube-designer-tree-toggle:hover { color: #fff2c7; }
.tube-designer-product-row input[type="checkbox"],
.tube-designer-category-row input[type="checkbox"] { accent-color: #f0b35a; }
.tube-designer-part-row td { border-bottom-color: #edf1f2; background: #fff; }
.tube-designer-category-last-row td { border-bottom-color: #c4d2d6; }
.tube-designer-tree-product-thumbnail { display: grid; width: 116px; height: 68px; place-items: center; border-radius: 4px; background: #142a33; }
.tube-designer-tree-product-thumbnail .tube-designer-schematic { max-height: 62px; }
.tube-designer-part-name { max-width: 270px; white-space: normal !important; overflow-wrap: anywhere; }
.tube-designer-part-name > strong,
.tube-designer-part-name > small { display: block; }
.tube-designer-part-name > small,
.tube-designer-merged-value { margin-top: 3px; color: var(--designer-muted); font-size: 10px; }

@media (max-width: 900px) {
  .tube-designer-breakdown-footer { grid-template-columns: 1fr auto; }
  .tube-designer-breakdown-footer > span { display: none; }
  .tube-designer-config-body { grid-template-columns: 280px minmax(0, 1fr); }
  .tube-designer-config-parameters .tube-designer-field-grid { grid-template-columns: repeat(2, minmax(0, 1fr)); }
  .tube-designer-part-inspection-body { grid-template-columns: minmax(0, 1fr) 300px; }
}
`;
