import { punchLayoutStyles } from "../punchLayoutView.mjs";
import { punchReviewStyles } from "../punchReview.mjs";
import { punchRecordAreasCss } from "./punchRecordAreas.css.mjs";
import { tileWindowCss } from "./tileWindow.css.mjs";
const baseTubeDesignerCss = String.raw`
.tube-machining-pane { color: #23434d; padding: 16px; }
.tube-machining-pane-title { display: flex; align-items: center; justify-content: space-between; gap: 12px; padding-bottom: 14px; border-bottom: 1px solid #c5d5db; }
.tube-machining-pane-title strong { font-size: 18px; }
.tube-machining-pane-title span, .tube-machining-note { color: #6d8793; font-size: 13px; line-height: 1.7; }
.tube-machining-job-list { display: grid; gap: 10px; }
.tube-machining-job { display: grid; gap: 8px; width: 100%; text-align: left; background: #fff; color: #23434d; border: 1px solid #c5d5db; border-radius: 6px; padding: 14px; cursor: pointer; overflow-wrap: anywhere; }
.tube-machining-job.active { border-color: #269d96; background: #e6f3f1; box-shadow: inset 3px 0 #269d96; }
.tube-machining-job small { color: #6d8793; }
.tube-machining-source-tag, .tube-machining-job-status { font-size: 12px; color: #26867f; }
.tube-machining-job-status.stale, .tube-machining-warning { color: #ad542e; }
.tube-machining-facts { display: grid; grid-template-columns: 80px minmax(0,1fr); gap: 14px 10px; font-size: 13px; line-height: 1.6; padding-top: 16px; border-top: 1px solid #c5d5db; }
.tube-machining-facts dt { color: #6d8793; }
.tube-machining-facts dd { margin: 0; overflow-wrap: anywhere; }
.tube-machining-pending, .tube-machining-empty-list { padding: 24px 12px; line-height: 1.8; text-align: center; border: 1px dashed #bed0d5; border-radius: 6px; color: #6d8793; }
.tube-machining-empty-stage { position: absolute; inset: 0; display: flex; align-items: center; justify-content: center; flex-direction: column; color: #dae8ec; pointer-events: none; }
.tube-machining-empty-stage > span { color: #54b9ae; font-size: 14px; letter-spacing: 4px; }
.tube-machining-empty-stage h2 { margin: 16px 0 4px; font-size: 26px; }
.tube-machining-empty-stage p, .tube-machining-empty-stage small { color: #8ea9b3; }
.tube-machining-scene-header { pointer-events: none; }
.tube-machining-source-dialog { width: min(700px, 90vw); }
.tube-machining-source-list { padding: 20px; max-height: 55vh; overflow: auto; display: grid; gap: 10px; }
.tube-machining-source-list label { display: flex; align-items: center; gap: 14px; padding: 12px; border: 1px solid #c5d5db; border-radius: 4px; }
.tube-machining-source-list label span { display: grid; gap: 6px; }
.tube-machining-source-list small { color: #6d8793; }
.tube-machining-paths { margin-bottom: 18px; }
.tube-machining-path-list { max-height: 32vh; min-height: 70px; overflow-y: auto; margin-top: 10px; }
.tube-machining-path-row { display: flex; gap: 8px; align-items: center; padding: 7px; border-bottom: 1px solid #d6e2e4; }
.tube-machining-path-row.active { background: #e2f1ee; border-left: 3px solid #259f94; padding-left: 4px; }
.tube-machining-path-row > button { display: grid; grid-template-columns: 22px minmax(0,1fr); flex: 1; min-width: 0; gap: 4px; border: 0; background: transparent; text-align: left; color: #28464f; cursor: pointer; }
.tube-machining-path-row small { grid-column: 2; color: #6d8793; }
.tube-machining-path-row strong { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.tube-machining-path-properties { margin: 16px 0 24px; }
.tube-machining-path-properties h4 { margin: 20px 0 10px; }
.tube-machining-field-grid { display: grid; grid-template-columns: repeat(3,minmax(0,1fr)); gap: 8px; margin: 10px 0; }
.tube-machining-field-grid label { display: grid; gap: 5px; font-size: 12px; }
.tube-machining-field-grid input, .tube-machining-field-grid select, .tube-machining-node-index input { min-width: 0; width: 100%; box-sizing: border-box; border: 1px solid #b8cbd0; background: white; color: #28464f; border-radius: 4px; padding: 7px; }
.tube-machining-button-row { display: flex; flex-wrap: wrap; gap: 8px; margin: 12px 0; }
.tube-machining-path-properties button, .tube-machining-edit-toolbar button { border: 1px solid #9fbcc3; border-radius: 4px; padding: 6px 10px; color: #28464f; background: #f5f9fa; cursor: pointer; }
.tube-machining-edit-toolbar { position: absolute; z-index: 9; top: 10px; left: 10px; right: 10px; padding: 8px; border: 1px solid #41636b; border-radius: 5px; background: #17303bec; color: #c4dbe0; font-size: 12px; pointer-events: auto; }
.tube-machining-edit-toolbar > div { display: flex; flex-wrap: wrap; align-items: center; gap: 6px; }
.tube-machining-edit-toolbar > div + div { margin-top: 7px; }
.tube-machining-edit-toolbar button.active { background: #4caaa0; color: white; border-color: #72ccc2; }
.tube-machining-edit-toolbar input { width: 65px; }
.tube-machining-edit-toolbar input, .tube-machining-edit-toolbar select { border: 1px solid #72949d; border-radius: 3px; padding: 3px; }
.tube-machining-edit-toolbar button:disabled, .tube-machining-path-properties button:disabled { opacity: .4; cursor: default; }
.tube-machining-side-editor { position: absolute; inset: 0; z-index: 7; background: #122833; pointer-events: auto; }
.tube-machining-side-editor svg { width: 100%; height: 100%; touch-action: none; }
.tube-machining-scene-header { top: auto !important; bottom: 18px; left: 18px; max-width: min(540px,75%); z-index: 8; }
.tube-machining-node-index { display: grid; grid-template-columns: 1fr 70px; gap: 10px; align-items: center; }
.tube-section-sketch-toolbar button { display: inline-flex; flex-direction: column; align-items: center; justify-content: center; gap: 5px; min-width: 48px; }
.tube-section-sketch-toolbar .command-icon { display: inline-flex; color: #40575d; }
.tube-section-sketch-toolbar .command-icon svg { width: 24px; height: 24px; fill: none; stroke: currentColor; stroke-width: 1.45; stroke-linecap: round; stroke-linejoin: round; }
.tube-section-sketch-toolbar .command-icon .accent { stroke: #35ae71; }
.tube-section-sketch-toolbar .command-icon .fill { fill: #35ae71; }
.tube-section-sketch-toolbar [data-icon-tone="orange"] .accent { stroke: #d57c4b; }
.tube-designer-confirm-dialog { width: min(420px, calc(100vw - 48px)); padding: 24px; border: 1px solid #abc8c7; border-radius: 8px; background: #f8fbfb; color: #29434b; box-shadow: 0 18px 44px #0005; }
.tube-designer-confirm-dialog::backdrop { background: rgba(4, 17, 22, .55); }
.tube-designer-confirm-dialog p { margin: 0 0 22px; white-space: pre-wrap; overflow-wrap: anywhere; line-height: 1.7; }
.tube-designer-confirm-dialog > div { display: flex; justify-content: flex-end; gap: 10px; }
.tube-designer-confirm-dialog button { padding: 7px 22px; border: 1px solid #b4c8c6; border-radius: 5px; background: #fff; color: #29434b; cursor: pointer; }
.tube-designer-confirm-dialog button.confirm { background: #a6352b; border-color: #a6352b; color: #fff; }
.tube-section-sketch-dialog { width: min(1500px, 96vw); height: 90vh; max-width: 96vw; max-height: 94vh; padding: 0; border: 1px solid #759b9d; border-radius: 8px; background: #f6f8f9; color: #29414a; overflow: hidden; }
.tube-section-sketch-dialog[open] { display: flex; flex-direction: column; }
.tube-section-sketch-dialog::backdrop { background: rgba(0, 15, 22, .65); }
.tube-section-sketch-title { padding: 10px 16px; background: #e1efed; font-weight: 700; }
.tube-section-sketch-toolbar { display: flex; flex-wrap: wrap; padding: 6px; gap: 8px; border-bottom: 1px solid #bbcece; }
.tube-section-sketch-toolbar section { display: grid; gap: 4px; padding: 0 8px; border-right: 1px solid #bbcece; }
.tube-section-sketch-toolbar section > div { display: flex; flex-wrap: wrap; gap: 3px; }
.tube-section-sketch-toolbar small { text-align: center; color: #678084; }
.tube-section-sketch-toolbar button { padding: 7px 9px; border: 1px solid transparent; border-radius: 4px; background: transparent; color: inherit; cursor: pointer; }
.tube-section-sketch-toolbar button:hover, .tube-section-sketch-toolbar button.selected { background: #d1e9e4; border-color: #70a49c; }
.tube-section-sketch-toolbar button:disabled { opacity: .4; cursor: default; }
.tube-section-sketch-body { flex: 1; min-height: 0; display: grid; grid-template-columns: minmax(150px, 220px) minmax(0, 1fr) minmax(190px, 250px); }
.tube-section-sketch-body > main { position: relative; min-width: 0; min-height: 0; }
.tube-section-sketch-body > aside { min-width: 0; overflow: auto; }
.tube-section-sketch-error { padding: 8px 14px; color: #9e3026; background: #fff1ed; }
.tube-profile-library-draw { margin-top: auto; padding: 12px; }
.tube-profile-library-draw button { width: 100%; }
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

/* The nesting page groups actions by task in the ribbon instead of repeating
   them in the parts pane. Keep this compact and scoped to this workspace. */
.workbench:has(.tube-designer-production-workspace) .product-ribbon .ribbon-commands {
  height: 70px;
  padding: 5px 10px 4px;
}
.workbench:has(.tube-designer-production-workspace) .product-ribbon .ribbon-group {
  min-width: 108px;
  padding: 0 7px;
}
.workbench:has(.tube-designer-production-workspace) .product-ribbon .ribbon-command-list {
  justify-content: center;
  gap: 5px;
}
.workbench:has(.tube-designer-production-workspace) .product-ribbon .ribbon-command.large,
.workbench:has(.tube-designer-production-workspace) .product-ribbon .ribbon-split-command.large {
  min-width: 82px;
  max-width: 92px;
}
.workbench:has(.tube-designer-production-workspace) .product-ribbon .ribbon-group-title {
  font-size: 10px;
  letter-spacing: .04em;
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
  display: block;
}
.tube-designer-subsection-list > * + * { margin-top: 6px; }
.tube-designer-config-subsection {
  min-width: 0;
  min-height: 0;
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
  display: block;
  padding: 8px;
}
.tube-designer-config-subsection-content > * + * { margin-top: 8px; }
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
.tube-designer-instance-quantity { max-width: 320px; margin: 12px 0; }
.tube-designer-instance-quantity small { color: #6b818c; font-size: 12px; }

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
  border-top-color: var(--designer-accent, #168f84);
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
  background: var(--designer-accent, #168f84);
  transition: width .16s ease-out;
}

.tube-designer-export-progress-track.is-indeterminate > i {
  animation: tube-designer-export-indeterminate 1.05s ease-in-out infinite;
}

.tube-designer-part-load-progress {
  position: absolute;
  left: 16px;
  bottom: 34px;
  z-index: 6;
  display: grid;
  gap: 8px;
  box-sizing: border-box;
  width: min(340px, calc(100% - 32px));
  padding: 14px 16px;
  border: 1px solid #9eb2ba;
  border-radius: 8px;
  background: #fff;
  box-shadow: 0 8px 24px rgba(8, 31, 40, .2);
  pointer-events: none;
}
.tube-designer-part-load-progress[hidden] { display: none; }
.tube-designer-part-load-progress > strong { color: #183741; font-size: 13px; }
.tube-designer-part-load-progress > span { color: #60747d; font-size: 12px; overflow-wrap: anywhere; }
.tube-designer-part-load-progress > .tube-designer-export-progress-track { grid-column: 1; }

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
.tube-profile-library-panel { grid-template-rows: auto auto minmax(0, 1fr); }
.tube-profile-library-panel:has(.tube-profile-library-draw) { grid-template-rows: auto auto minmax(0, 1fr) auto; }
.tube-profile-library-heading > div { display: grid; gap: 2px; }
.tube-profile-library-heading > button { flex: 0 0 auto; min-width: 78px; }
.tube-profile-library-list { display: flex; flex-direction: column; align-items: stretch; gap: 9px; min-height: 0; padding: 10px; overflow-x: hidden; overflow-y: auto; }
.tube-profile-library-group { flex: 0 0 auto; overflow: hidden; border: 1px solid #c5d5da; border-radius: 8px; background: #f7fafb; }
.tube-profile-library-group > summary { display: flex; align-items: center; min-height: 34px; padding: 0 10px; color: #35545e; cursor: pointer; list-style: none; user-select: none; }
.tube-profile-library-group > summary::-webkit-details-marker { display: none; }
.tube-profile-library-group > summary::before { width: 7px; height: 7px; margin: -3px 9px 0 2px; border-right: 2px solid #607982; border-bottom: 2px solid #607982; content: ""; transform: rotate(45deg); }
.tube-profile-library-group:not([open]) > summary::before { margin-top: 3px; transform: rotate(-45deg); }
.tube-profile-library-group > summary strong { font-size: 11px; }
.tube-profile-library-group > summary span { margin-left: auto; color: var(--designer-muted); font-size: 9px; }
.tube-profile-library-group-content { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(100%, 180px), 1fr)); gap: 7px; padding: 7px; border-top: 1px solid #d4dfe2; }
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
.tube-profile-library-parameter-list { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(100%, 128px), 1fr)); gap: 8px; }
.tube-profile-library-parameter-list > .tube-designer-field { min-width: 0; }
.tube-profile-library-parameter-list :is(input, select) { width: 100%; min-width: 0; box-sizing: border-box; }
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
.tube-profile-library-editor-footer { display: flex; align-items: center; justify-content: space-between; gap: 9px; padding: 11px 13px; border-top: 1px solid #cbd5da; background: #fff; }
.tube-profile-library-editor-footer > div { display: flex; flex-wrap: wrap; gap: 7px; }
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

.tube-designer-config-dialog { position: relative; width: min(1480px, calc(100vw - 48px)); height: min(860px, calc(100vh - 48px)); }
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
.tube-nesting-part-import-dialog { width: min(560px, calc(100vw - 48px)); }
.tube-nesting-part-import-note { padding: 9px 10px; border: 1px solid #b9ddd8; border-radius: 6px; background: #e7f4f2; color: #3f716c !important; }
.tube-nesting-part-import-file { display: grid; gap: 3px; padding: 9px 10px; border-radius: 6px; background: #edf2f3; color: #5b737a; font-size: 10px; }
.tube-nesting-part-import-file strong { overflow: hidden; color: #294d56; text-overflow: ellipsis; white-space: nowrap; }
.tube-nesting-part-import-file small { color: #7b8d92; font-size: 9px; }
.tube-nesting-standard-part-dialog { width: min(760px, calc(100vw - 40px)); max-height: calc(100vh - 40px); grid-template-rows: auto minmax(0, 1fr) auto; }
.tube-nesting-standard-part-body { display: grid; grid-template-columns: minmax(0, 1fr) minmax(0, 1.25fr); gap: 18px; padding: 18px; overflow: auto; min-height: 0; }
.tube-nesting-standard-part-preview { display: flex; flex-direction: column; min-width: 0; margin: 0; padding: 16px; align-self: start; border: 1px solid #cbdadd; border-radius: 8px; background: #ecf3f4; }
.tube-nesting-standard-part-section { display: grid; grid-template: minmax(0, 1fr) / minmax(0, 1fr); place-items: center; height: 240px; color: #66828a; font-size: 12px; }
.tube-nesting-standard-part-section svg { display: block; min-width: 0; min-height: 0; width: 100%; height: 100%; filter: none; }
.tube-nesting-standard-part-section .outer { stroke: #33837d; }
.tube-nesting-standard-part-section .hole { fill: #ecf3f4; stroke: #33837d; }
.tube-nesting-standard-part-preview figcaption { display: grid; gap: 6px; margin-top: 12px; text-align: center; overflow-wrap: anywhere; }
.tube-nesting-standard-part-preview figcaption strong { font-size: 13px; }
.tube-nesting-standard-part-preview figcaption span { font-size: 12px; color: #3f716c; }
.tube-nesting-standard-part-preview small, .tube-nesting-standard-part-fields small { font-size: 11px; color: #6c8188; }
.tube-nesting-standard-part-fields { display: grid; align-content: start; min-width: 0; gap: 14px; }
.tube-nesting-standard-part-fields .tube-designer-field { min-width: 0; }
.tube-nesting-standard-part-fields select, .tube-nesting-standard-part-fields input { min-width: 0; max-width: 100%; box-sizing: border-box; }
.tube-nesting-standard-part-source { display: flex; align-items: center; justify-content: space-between; gap: 8px; color: #6c8188; font-size: 11px; }
.tube-nesting-standard-part-parameters { display: grid; gap: 10px; padding-top: 12px; border-top: 1px solid #d3dee1; }
.tube-nesting-standard-part-parameters > strong { font-size: 12px; }
.tube-nesting-standard-part-parameters > div { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 12px; }
.tube-nesting-standard-part-note { margin: 0; color: #6c8188; font-size: 11px; line-height: 1.6; }
.tube-nesting-standard-part-dialog .tube-designer-preset-dialog-footer > small { color: #6c8188; font-size: 11px; }
.tube-nesting-standard-part-preview.has-parameter-diagram { padding: 0; border: 0; background: transparent; }
.td-profile-parameter-diagram { display: grid; grid-template-columns: minmax(0, 1fr); min-width: 0; width: 100%; border: 1px solid #bdced3; border-radius: 7px; background: #f4f8f9; color: #294f58; overflow: hidden; }
.td-profile-parameter-diagram > header { display: grid; gap: 4px; padding: 10px 11px 7px; border: 0; background: transparent; }
.td-profile-parameter-diagram > header > strong { color: #315761; font-size: 12px; }
.td-profile-parameter-diagram > header > small { color: #71878c; font-size: 10px; line-height: 1.5; }
.td-profile-parameter-canvas { display: grid; grid-template: minmax(0, 1fr) / minmax(0, 1fr); min-width: 0; padding: 5px 9px; background: #edf4f5; }
.td-profile-parameter-svg { display: block; width: 100%; height: auto; min-width: 0; overflow: visible; }
.td-profile-diagram-shape .outer { fill: #b8dcd8; stroke: #3a8c85; stroke-width: 1; vector-effect: non-scaling-stroke; }
.td-profile-diagram-shape .hole { fill: #edf4f5; stroke: #3a8c85; stroke-width: 1; vector-effect: non-scaling-stroke; }
.td-profile-annotation { color: #577580; cursor: pointer; outline: none; }
.td-profile-annotation line, .td-profile-annotation polyline { fill: none; stroke: currentColor; stroke-width: 1.3; }
.td-profile-annotation .td-profile-extension { stroke-dasharray: 3 2; opacity: .65; }
.td-profile-annotation .td-profile-dimension-arrow, .td-profile-annotation .td-profile-anchor { fill: currentColor; stroke: none; }
.td-profile-annotation .td-profile-dimension-label, .td-profile-annotation .td-profile-parameter-badge { fill: #edf4f5; stroke: currentColor; stroke-width: 1; }
.td-profile-annotation text { fill: currentColor; stroke: none; font: 12px "Segoe UI", "Microsoft YaHei", sans-serif; }
.td-profile-annotation.is-active, .td-profile-annotation:hover, .td-profile-annotation:focus-visible { color: #cf6917; }
.td-profile-annotation.is-active line, .td-profile-annotation.is-active polyline { stroke-width: 2.2; }
.td-profile-annotation.is-active .td-profile-dimension-label, .td-profile-annotation.is-active .td-profile-parameter-badge { fill: #fff4dd; stroke-width: 2; }
.td-profile-parameter-legend { display: grid; grid-template-columns: minmax(0, 1fr); gap: 1px; padding: 5px; min-width: 0; }
.td-profile-parameter-diagram .td-profile-parameter-legend-row { display: grid; grid-template-columns: 21px minmax(0, 1fr) auto; align-items: start; gap: 7px; min-width: 0; width: 100%; padding: 7px; border: 1px solid transparent; border-radius: 4px; background: transparent; color: #365b64; text-align: left; cursor: pointer; }
.td-profile-parameter-legend-row > b { display: grid; place-items: center; width: 20px; height: 20px; border: 1px solid #85a8af; border-radius: 50%; color: #416b76; font-size: 10px; }
.td-profile-parameter-legend-row > span { display: grid; gap: 3px; min-width: 0; }
.td-profile-parameter-legend-row strong { color: inherit; font-size: 11px; font-weight: 600; overflow-wrap: anywhere; }
.td-profile-parameter-legend-row small { color: #71868b; font-size: 10px; line-height: 1.5; overflow-wrap: anywhere; }
.td-profile-parameter-legend-row em { max-width: 90px; color: #327d74; font-size: 11px; font-style: normal; font-variant-numeric: tabular-nums; overflow-wrap: anywhere; }
.td-profile-parameter-diagram .td-profile-parameter-legend-row.is-active, .td-profile-parameter-diagram .td-profile-parameter-legend-row:hover, .td-profile-parameter-diagram .td-profile-parameter-legend-row:focus-visible { border-color: #dfa969; background: #fff4df; outline: none; }
.td-profile-parameter-legend-row.is-active > b { border-color: #cf6917; color: #b15a12; }
.td-profile-diagram-message { margin: 5px 10px; color: #916934; font-size: 10px; line-height: 1.6; }
[data-profile-parameter-scope] .is-profile-parameter-active > span { color: #b96519; }
[data-profile-parameter-scope] [data-profile-parameter-key].is-active { outline: 2px solid #e5ab68; outline-offset: 1px; }
.tube-designer-punch-profile-parameters { display: grid; gap: 8px; min-width: 0; }
.tube-designer-punch-profile-parameters > details { width: 100%; min-width: 0; }
.tube-designer-punch-profile-parameters > details > summary { padding: 5px 3px; color: #39796f; cursor: pointer; font-size: 11px; }
.tube-designer-nesting-profile-diagram { position: relative; flex: 0 0 auto; align-self: center; }
.tube-designer-nesting-profile-diagram > summary { padding: 6px 9px; border: 1px solid #9fbcbd; border-radius: 4px; color: #39796f; background: #f5fbfa; cursor: pointer; font-size: 11px; white-space: nowrap; }
.tube-designer-nesting-profile-diagram[open] > .td-profile-parameter-diagram { position: absolute; z-index: 5; top: calc(100% + 5px); right: 0; width: min(330px, calc(100vw - 60px)); max-height: 65vh; overflow-y: auto; box-shadow: 0 8px 28px #19353b33; }
@media (max-width: 620px) {
  .tube-nesting-standard-part-body { grid-template-columns: minmax(0, 1fr); }
  .tube-nesting-standard-part-section { height: 160px; }
  .tube-nesting-standard-part-dialog .tube-designer-preset-dialog-footer > small { display: none; }
  .tube-nesting-standard-part-dialog .tube-designer-preset-dialog-footer { grid-template-columns: 1fr auto; }
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

.tube-designer-config-body { display: grid; grid-template-columns: minmax(430px, 46%) minmax(0, 1fr); min-height: 0; }
.tube-designer-template-pane { display: grid; grid-template-rows: auto auto minmax(0, 1fr); min-height: 0; padding: 16px; border-right: 1px solid #cbd5da; background: #edf2f4; }
.tube-designer-pane-title { display: flex; justify-content: space-between; padding-bottom: 11px; }
.tube-designer-pane-title strong { font-size: 13px; }
.tube-designer-pane-title span { color: var(--designer-muted); font-size: 11px; }
.tube-designer-template-tabs { display: flex; gap: 5px; min-width: 0; padding: 0 0 11px; overflow-x: auto; }
.tube-designer-template-tabs button { display: inline-flex; flex: 0 0 auto; align-items: center; gap: 5px; min-height: 29px; padding: 4px 8px; border: 1px solid #bfced2; border-radius: 5px; background: #f8fbfb; color: #536c75; cursor: pointer; font-size: 11px; }
.tube-designer-template-tabs button:hover { border-color: #68aaa3; background: #f1f8f7; }
.tube-designer-template-tabs button.selected { border-color: var(--designer-accent); background: #dff1ee; color: #126f66; font-weight: 700; }
.tube-designer-template-tabs button small { min-width: 16px; padding: 1px 4px; border-radius: 8px; background: rgba(21, 123, 114, .12); color: inherit; font-size: 9px; }
.tube-designer-template-list { display: grid; grid-auto-rows: max-content; align-content: start; gap: 11px; min-height: 0; overflow: auto; }
.tube-designer-template-category { display: grid; min-height: max-content; gap: 7px; }
.tube-designer-template-category > header { display: flex; align-items: center; justify-content: space-between; gap: 8px; min-height: 26px; padding: 4px 7px; border-bottom: 1px solid #cadadd; color: #35555d; }
.tube-designer-template-category > header strong { font-size: 12px; }
.tube-designer-template-category > header em { padding: 2px 6px; border-radius: 9px; background: #d9e8e8; color: #5f777e; font-size: 9px; font-style: normal; }
.tube-designer-template-category[data-tube-designer-template-group-depth="1"] { margin-top: 4px; padding: 7px; border: 1px solid #d1dde0; border-radius: 7px; background: rgba(255,255,255,.55); }
.tube-designer-template-category[data-tube-designer-template-group-depth="1"] > header { min-height: 22px; padding: 0 2px 5px; }
.tube-designer-template-category[data-tube-designer-template-group-depth="1"] > header strong { font-size: 11px; }
.tube-designer-template-category-items { display: grid; grid-auto-rows: max-content; gap: 8px; }
.tube-designer-template-card-grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 8px; }
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
.tube-designer-template-card { display: grid; grid-template-columns: 72px minmax(0, 1fr) 10px; align-items: center; gap: 8px; min-height: 116px; padding: 8px; border: 1px solid #c6d2d7; border-radius: 8px; background: #fff; color: var(--designer-ink); cursor: pointer; text-align: left; }
.tube-designer-template-card:hover { border-color: #68aaa3; }
.tube-designer-template-card.selected { border-color: var(--designer-accent); background: #e9f5f3; box-shadow: inset 3px 0 var(--designer-accent); }
.tube-designer-template-card.disabled { opacity: .58; cursor: default; }
.tube-designer-template-card > span:nth-child(2) { display: grid; gap: 6px; }
.tube-designer-template-card strong { font-size: 12px; line-height: 1.4; }
.tube-designer-template-card small { color: var(--designer-muted); font-size: 10px; line-height: 1.45; }
.tube-designer-template-card > i { width: 8px; height: 8px; border: 2px solid #aab9bf; border-radius: 50%; }
.tube-designer-template-card.selected > i { border-color: var(--designer-accent); background: var(--designer-accent); box-shadow: inset 0 0 0 2px #fff; }
.tube-designer-template-schematic { display: grid; place-items: center; height: 94px; border-radius: 6px; background: #142a33; }
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
.tube-designer-schematic .guardrail-glass { fill: rgba(113, 194, 223, .22); stroke: #9fdbed; stroke-width: .8; }
.tube-designer-schematic .guardrail-spear { fill: #a2c8c3; stroke: #a2c8c3; stroke-width: .6; }
.tube-designer-schematic .guardrail-post-cap { fill: #c6dbd8; stroke: #91b6b2; stroke-width: .5; }
.tube-designer-schematic .guardrail-round { stroke-linecap: round; }
.tube-designer-schematic .security-window-plate { fill: #61969b; stroke: #cee2df; stroke-width: 2; }

/* Native details must grow in normal flow, not share constrained grid rows.
   Otherwise opening nested groups can paint content over the following group. */
.tube-designer-config-parameters { display: block; min-width: 0; min-height: 0; padding: 16px 18px; overflow: auto; }
.tube-designer-config-parameters > * + * { margin-top: 12px; }
.tube-designer-config-parameters > .tube-designer-empty { margin-bottom: 0; }
.tube-designer-config-parameters > .tube-designer-config-subsection > summary { min-height: 38px; font-size: 12px; }
.tube-designer-config-parameters > .tube-designer-config-subsection > summary::before { top: 14px; }
.tube-designer-review-disclosure { border: 1px solid #c5d0d4; border-radius: 6px; padding: 10px 12px; font-size: 12px; }
.tube-designer-review-disclosure > summary { cursor: pointer; color: #3e5963; }
.tube-designer-review-disclosure > summary > span { margin-left: 8px; color: #697a84; font-size: 11px; }
.tube-designer-config-summary { display: grid; grid-template-columns: 106px minmax(0, 1fr); align-items: center; gap: 14px; padding: 11px; border: 1px solid var(--designer-border); border-radius: 8px; background: #fff; }
.tube-designer-config-summary > div { display: grid; gap: 5px; }
.tube-designer-config-summary strong { font-size: 14px; }
.tube-designer-config-summary span { color: var(--designer-muted); font-size: 11px; }
.tube-designer-config-preview { display: grid; place-items: center; height: 104px; border-radius: 6px; background: #142a33; }
.tube-designer-config-parameters .tube-designer-field-grid { grid-template-columns: repeat(3, minmax(0, 1fr)); }
.tube-designer-config-subsection-content { container-type: inline-size; }
.tube-designer-config-parameters .tube-designer-field-grid:has(> .tube-designer-profile-field) { grid-template-columns: repeat(2, minmax(0, 1fr)); }
@container (min-width: 420px) {
  .tube-designer-config-parameters .tube-designer-field-grid:has(> .tube-designer-profile-field) { grid-template-columns: repeat(4, minmax(0, 1fr)); }
}

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
  min-height: 44px;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  padding: 6px 10px;
  border-bottom: 1px solid #cbd6d9;
  background: #fff;
}
.tube-designer-cutting-pane-header > div { display: grid; min-width: 0; gap: 2px; }
.tube-designer-cutting-pane-header strong { overflow: hidden; color: #243d46; font-size: 12px; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-cutting-pane-header span { overflow: hidden; color: #71838a; font-size: 9px; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-cutting-pane-menu-hint { flex: 0 0 auto; color: #8a9ba0 !important; font-size: 8px !important; }
.tube-designer-cutting-pane-header > button {
  min-height: 25px;
  padding: 3px 8px;
  border: 1px solid #9ab0b7;
  border-radius: 4px;
  background: #f6f9fa;
  color: #35515a;
  font-size: 9px;
}
.tube-designer-cutting-pane-header-actions { display: flex !important; align-items: center; gap: 5px; }
.tube-designer-cutting-pane-header-actions button { min-height: 25px; padding: 3px 8px; border: 1px solid #9ab0b7; border-radius: 4px; background: #f6f9fa; color: #35515a; font-size: 9px; }
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
.tube-designer-nesting-context-menu {
  position: fixed;
  z-index: 1200;
  display: grid;
  min-width: 168px;
  max-width: 260px;
  gap: 4px;
  padding: 7px;
  border: 1px solid #9bb4ba;
  border-radius: 6px;
  background: #fff;
  box-shadow: 0 10px 28px rgba(16, 47, 57, .22);
  color: #29434e;
}
.tube-designer-nesting-context-menu strong { overflow: hidden; color: #29434e; font-size: 10px; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-nesting-context-menu > span { overflow: hidden; color: #75868c; font-size: 8px; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-nesting-context-menu button { min-height: 28px; margin-top: 3px; padding: 4px 7px; border: 1px solid #7db6ae; border-radius: 4px; background: #e7f4f2; color: #176f68; cursor: pointer; font-size: 9px; text-align: left; }
.tube-designer-nesting-context-menu button:hover { border-color: #178f82; background: #d8efec; }
.tube-designer-nesting-context-menu button:disabled { border-color: #d1dcdf; background: #f5f7f8; color: #9aa9ad; cursor: default; }

.tube-designer-cutting-inspector-scroll { min-height: 0; padding: 8px; overflow: auto; }
.tube-designer-cutting-inspector-scroll > section { margin-bottom: 8px; overflow: hidden; border: 1px solid #cbd7da; border-radius: 5px; background: #fff; }
.tube-designer-cutting-inspector-scroll h3 { display: flex; align-items: center; justify-content: space-between; gap: 8px; min-height: 30px; margin: 0; padding: 5px 8px; border-bottom: 1px solid #dce4e6; color: #38545c; background: #edf2f3; font-size: 10px; }
.tube-designer-cutting-inspector-scroll h3 span { color: #75868c; font-size: 8px; font-weight: 400; }
.tube-designer-cutting-inspector-scroll dl { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 1px; margin: 0; background: #dfe6e8; }
.tube-designer-cutting-inspector-scroll dl > div { display: grid; min-width: 0; gap: 2px; padding: 7px 8px; background: #fff; }
.tube-designer-cutting-inspector-scroll dl > div.wide { grid-column: 1 / -1; }
.tube-designer-cutting-inspector-scroll dt { color: #7a8b91; font-size: 8px; }
.tube-designer-cutting-inspector-scroll dd { margin: 0; overflow: hidden; color: #2d4851; font-size: 9px; font-weight: 600; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-nesting-measurement-card { display: grid; gap: 8px; margin-bottom: 8px; padding: 8px; overflow: hidden; border: 1px solid #cbd7da; border-radius: 5px; background: #fff; }
.tube-designer-nesting-measurement-card > h3 { min-height: 24px; margin: -8px -8px 0; padding: 5px 8px; border-bottom: 1px solid #dce4e6; }
.tube-designer-nesting-measurement-card > h3 span:last-child { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-part-editor { padding-bottom: 8px; }
.tube-designer-part-editor > h3 { margin-bottom: 0; }
.tube-designer-part-editor > label { display: grid; gap: 3px; padding: 7px 8px 0; color: #71838a; font-size: 8px; }
.tube-designer-part-editor input { width: 100%; min-height: 27px; box-sizing: border-box; padding: 4px 6px; border: 1px solid #bdcbd0; border-radius: 4px; outline: 0; background: #fff; color: #29424b; font-size: 10px; }
.tube-designer-part-editor input:focus { border-color: #42a99f; box-shadow: 0 0 0 2px rgba(66, 169, 159, .16); }
.tube-designer-part-editor-actions { display: flex; flex-wrap: wrap; gap: 5px; padding: 8px 8px 0; }
.tube-designer-part-editor-actions button { min-height: 27px; padding: 3px 8px; border-radius: 4px; font-size: 9px; }
.tube-designer-part-editor-actions .tube-designer-primary { border: 0; }
.tube-designer-part-editor-actions .tube-designer-danger { min-height: 27px; padding: 3px 8px; }
.tube-designer-part-editor > small { display: block; padding: 7px 8px 0; color: #84969b; font-size: 8px; line-height: 1.45; }
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
.tube-designer-cutting-scene-title { display: grid; flex: 1 1 200px; min-width: 0; max-width: min(480px, 100%); gap: 2px; overflow: hidden; padding: 8px 10px; border: 1px solid rgba(126, 164, 176, .3); border-radius: 6px; background: rgba(13, 34, 42, .84); color: #e7f1f3; box-shadow: 0 5px 16px rgba(0, 0, 0, .14); backdrop-filter: blur(7px); }
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
.tube-designer-nesting-result-group {border-bottom:1px solid #b9cdd1;}
.tube-designer-nesting-result-group > summary {display:flex;align-items:center;gap:10px;padding:9px 12px;cursor:pointer;background:#e5f1f0;color:#23585a;}
.tube-designer-nesting-result-group > summary::before {content:"▸";}
.tube-designer-nesting-result-group[open] > summary::before {content:"▾";}
.tube-designer-nesting-result-group > summary strong {min-width:0;overflow-wrap:anywhere;}
.tube-designer-nesting-result-group > summary span {color:#667b80;font-size:11px;}
.tube-designer-nesting-result-group > summary button {margin-left:auto;flex-shrink:0;padding:4px 10px;border:1px solid #9cb8c0;border-radius:3px;background:#fff;color:#225663;cursor:pointer;}
.tube-designer-nesting-result-group > summary button:disabled {opacity:.45;cursor:default;}
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
.tube-designer-about-mark { display: grid; width: 58px; height: 58px; place-items: center; }
.tube-designer-about-mark svg, .tube-designer-about-symbol svg { display: block; width: 100%; height: 100%; }
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
.tube-designer-about-symbol { width: 144px; height: 144px; margin-bottom: 16px; }
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
.tube-sketch-canvas { display: block; width: 100%; height: 100%; min-height: 0; touch-action: none; cursor: crosshair; }
[data-tube-sketch-tool="select"] .tube-sketch-canvas { cursor: default; }
[data-tube-sketch-tool="break"] .tube-sketch-canvas { cursor: cell; }
[data-tube-sketch-tool="insert-point"] .tube-sketch-canvas { cursor: copy; }
.tube-sketch-canvas pattern path { fill: none; stroke: rgba(142, 183, 190, .09); stroke-width: 1; }
[data-tube-sketch-mode="section"] .tube-sketch-grid { fill: #0d2730; }
.tube-sketch-model-grid line { stroke: rgba(142, 183, 190, .1); stroke-width: 1; vector-effect: non-scaling-stroke; }
.tube-sketch-axis line { stroke: rgba(143, 177, 183, .45); stroke-width: 1; }
.tube-sketch-axis line.origin { stroke: rgba(121, 199, 190, .52); }
.tube-sketch-axis text,
.tube-sketch-side-guide text { fill: #88aeb4; font-size: 11px; }
.tube-sketch-side-guide rect { fill: rgba(129, 171, 179, .06); stroke: rgba(142, 183, 190, .48); stroke-width: 1; }
.tube-sketch-side-guide { pointer-events: none; }
.tube-sketch-side-guide .tube-sketch-side-region { fill: rgba(151, 177, 182, .12); stroke: rgba(184, 207, 211, .76); stroke-width: 1.5; vector-effect: non-scaling-stroke; }
.tube-sketch-side-reference-edges line { stroke: rgba(194, 216, 219, .6); stroke-width: 1; vector-effect: non-scaling-stroke; }
.tube-sketch-side-unfolding-panel { fill: rgba(56, 128, 133, .13); stroke: rgba(184, 223, 220, .7); stroke-width: 1.2; vector-effect: non-scaling-stroke; }
.tube-sketch-side-unfolding-feature { fill: rgba(9, 30, 35, .7); stroke: #efbd62; stroke-width: 1.5; vector-effect: non-scaling-stroke; }
.tube-sketch-side-unfolding-end { fill: none; stroke: rgba(239, 189, 98, .82); stroke-width: 1.4; stroke-dasharray: 5 3; vector-effect: non-scaling-stroke; }
.tube-sketch-side-unfolding-seam { stroke: rgba(239, 189, 98, .62); stroke-width: 1; stroke-dasharray: 3 4; vector-effect: non-scaling-stroke; }
.tube-sketch-side-reference-holes .tube-sketch-side-hole { fill: rgba(5, 21, 27, .88); stroke: #efbd62; stroke-width: 1.5; vector-effect: non-scaling-stroke; }
.tube-sketch-side-reference-holes .tube-sketch-side-hole.hidden { fill: rgba(5, 21, 27, .25); stroke: rgba(239, 189, 98, .72); stroke-dasharray: 6 4; }
.tube-sketch-preview-reference-holes { fill: rgba(10, 30, 36, .78); stroke: #efbd62; stroke-width: 1.5; }
.tube-sketch-preview-reference-holes .hidden { fill: none; stroke-dasharray: 4 3; opacity: .72; }
.tube-sketch-entity { fill: rgba(26, 153, 141, .08); stroke: #27afa1; stroke-width: 2; vector-effect: non-scaling-stroke; pointer-events: visibleStroke; }
.tube-sketch-entity.open { fill: none; }
.tube-sketch-entity.selected { fill: rgba(31, 180, 165, .16); stroke: #60ddd0; stroke-width: 3; }
.tube-sketch-entity.open.selected { fill: none; }
text.tube-sketch-entity { fill: #4fd0c2; stroke: none; font-size: 20px; pointer-events: all; }
.tube-sketch-handles rect { fill: #eefbfa; stroke: #159a8d; stroke-width: 1.5; vector-effect: non-scaling-stroke; pointer-events: all; cursor: move; }
.tube-sketch-handles rect.secondary { fill: #1a776f; stroke: #b6eee8; }
.tube-sketch-inactive-nodes .tube-sketch-handles rect { fill: #eefbfa; stroke: #729c99; }
.tube-sketch-handles rect.point-selected { fill: #f4d35e; stroke: #fff2a8; stroke-width: 2; }
.tube-sketch-draft-preview .tube-sketch-entity { opacity: .72; stroke-dasharray: 7 5; pointer-events: none; }
.tube-sketch-selection-window { fill: rgba(58, 149, 226, .12); stroke: #58a9e8; stroke-width: 1; vector-effect: non-scaling-stroke; pointer-events: none; }
.tube-sketch-selection-window.crossing { fill: rgba(62, 190, 135, .12); stroke: #4bc18c; stroke-dasharray: 6 4; }
.tube-sketch-snap-preview { pointer-events: none; }
.tube-sketch-snap-preview path { fill: rgba(13, 39, 48, .82); stroke: #f4d35e; stroke-width: 2; vector-effect: non-scaling-stroke; }
.tube-sketch-snap-preview text { fill: #f8df7b; font-size: 10px; paint-order: stroke; stroke: #0d2730; stroke-width: 3px; }
.tube-sketch-cursor-preview { pointer-events: none; }
.tube-sketch-cursor-preview rect { fill: rgba(8, 25, 31, .94); stroke: rgba(125, 187, 190, .62); stroke-width: 1; }
.tube-sketch-cursor-preview text { fill: #dcebed; font-size: 10px; }
.tube-sketch-canvas-empty { position: absolute; inset: 0; display: grid; place-content: center; justify-items: center; gap: 6px; padding: 24px; background: rgba(13, 39, 48, .8); text-align: center; pointer-events: none; }
.tube-sketch-canvas-empty strong { font-size: 13px; }
.tube-sketch-canvas-empty span { max-width: 360px; color: #95b2b7; font-size: 10px; line-height: 1.6; }
.tube-sketch-canvas-status { display: flex; align-items: center; gap: 15px; min-width: 0; padding: 0 12px; border-top: 1px solid rgba(142, 183, 190, .34); color: #93b2b7; font-size: 9px; }
.tube-sketch-canvas-status span:first-child { color: #66d1c5; }
.tube-sketch-canvas-status span:last-child { margin-left: auto; color: #73bbb3; }
.tube-sketch-status-toggles { display: flex; gap: 4px; }
.tube-sketch-status-toggles button { min-height: 22px; padding: 2px 7px; border: 1px solid rgba(130, 172, 179, .32); border-radius: 3px; background: transparent; color: #8eabb1; cursor: pointer; font-size: 9px; }
.tube-sketch-status-toggles button[aria-pressed="true"] { border-color: rgba(92, 208, 195, .55); background: rgba(25, 143, 131, .28); color: #c9f1ed; }
[data-tube-sketch-coordinate-status] { color: #afc5c9; font-variant-numeric: tabular-nums; white-space: nowrap; }

.tube-sketch-property-panel { display: grid; grid-template-rows: auto minmax(0, 1fr); width: 100%; height: 100%; min-height: 0; background: #f6f8f9; color: var(--designer-ink); }
.tube-sketch-property-panel > header { display: grid; gap: 3px; padding: 13px 14px; border-bottom: 1px solid #cdd8dc; }
.tube-sketch-property-panel > header strong { font-size: 13px; }
.tube-sketch-property-panel > header span { color: var(--designer-muted); font-size: 10px; }
.tube-sketch-property-body { min-height: 0; padding: 13px; overflow: auto; }
.tube-sketch-side-reference-summary { display: grid; gap: 9px; margin-bottom: 12px; padding: 10px; border: 1px solid #c2d2d5; border-radius: 6px; background: #edf2f3; }
.tube-sketch-side-reference-summary > div { display: grid; gap: 2px; }
.tube-sketch-side-reference-summary small { color: #73878c; font-size: 9px; line-height: 1.45; }
.tube-sketch-side-reference-summary dl { display: grid; gap: 5px; margin: 0; }
.tube-sketch-side-reference-summary dl > div { display: flex; justify-content: space-between; gap: 10px; }
.tube-sketch-side-reference-summary dt { color: #74858a; font-size: 9px; }
.tube-sketch-side-reference-summary dd { margin: 0; color: #2b4b53; font-size: 9px; font-weight: 700; }
.tube-sketch-section-session { display: grid; gap: 9px; margin-bottom: 12px; padding: 10px; border: 1px solid #b9d8d4; border-radius: 6px; background: #e7f3f1; }
.tube-sketch-section-session > div { display: grid; gap: 3px; }
.tube-sketch-section-session strong { color: #176f67; font-size: 11px; }
.tube-sketch-section-session small { color: #587a77; font-size: 9px; line-height: 1.45; }
.tube-sketch-section-session label { display: grid; gap: 4px; color: #5f747b; font-size: 9px; }
.tube-sketch-section-session input { box-sizing: border-box; width: 100%; min-height: 31px; padding: 5px 7px; border: 1px solid #b9cbc9; border-radius: 5px; background: #fff; color: #29414a; font: inherit; font-size: 11px; }
.tube-sketch-validation { display: grid; grid-template-columns: 24px minmax(0, 1fr); align-items: start; gap: 8px; margin-bottom: 14px; padding: 9px 10px; border: 1px solid #d1dce0; border-radius: 6px; background: #edf2f3; color: #526870; }
.tube-sketch-validation > span { display: grid; width: 20px; height: 20px; place-items: center; border-radius: 50%; background: #d8e3e5; font-size: 11px; font-weight: 700; }
.tube-sketch-validation > div { display: grid; gap: 3px; min-width: 0; }
.tube-sketch-validation strong { font-size: 10px; }
.tube-sketch-validation small { color: #71858b; font-size: 9px; line-height: 1.45; }
.tube-sketch-validation.valid { border-color: #b9ddd8; background: #e5f3f1; color: #147b70; }
.tube-sketch-validation.valid > span { background: #178f82; color: #fff; }
.tube-sketch-validation.valid small { color: #4d7e78; }
.tube-sketch-validation.warning { border-color: #e5cf9d; background: #fff7e5; color: #8a5a06; }
.tube-sketch-validation.warning > span { background: #d89c27; color: #fff; }
.tube-sketch-validation.warning small { color: #886d35; }
.tube-sketch-validation.invalid { border-color: #e5b9b4; background: #fff0ee; color: #a33d31; }
.tube-sketch-validation.invalid > span { background: #c75245; color: #fff; }
.tube-sketch-validation.invalid small { color: #8d5b55; }
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
.tube-sketch-save-choice-backdrop { position: fixed; z-index: 1000; inset: 0; display: grid; place-items: center; padding: 24px; background: rgba(4, 17, 22, .66); backdrop-filter: blur(2px); }
.tube-sketch-save-choice { display: grid; gap: 14px; width: min(430px, calc(100% - 32px)); box-sizing: border-box; padding: 18px; border: 1px solid #abc8c7; border-radius: 8px; background: #f8fbfb; color: #29434b; box-shadow: 0 18px 44px rgba(0, 0, 0, .34); }
.tube-sketch-save-choice > header { display: grid; gap: 4px; }
.tube-sketch-save-choice > header strong { font-size: 15px; }
.tube-sketch-save-choice > header span, .tube-sketch-save-choice > p { margin: 0; color: #667d84; font-size: 10px; line-height: 1.55; }
.tube-sketch-save-choice > div { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }
.tube-sketch-save-choice > div button:last-child { grid-column: 1 / -1; }

.tube-component-library-panel { height: 100%; min-height: 0; overflow: hidden; box-sizing: border-box; grid-template-rows: auto auto minmax(0, 1fr) auto; gap: 0; }
.tube-component-library-panel .tube-designer-heading > div, .tube-component-library-editor .tube-designer-heading > div { display: grid; gap: 4px; }
.tube-component-library-filters { display: grid; gap: 8px; padding: 10px; border-bottom: 1px solid #d2dde0; }
.tube-component-library-filters input { box-sizing: border-box; width: 100%; min-height: 33px; padding: 6px 8px; border: 1px solid #bdcdd2; border-radius: 5px; background: #fff; }
.tube-component-library-filters > div { display: flex; gap: 5px; }
.tube-component-library-filters button { flex: 1; min-width: 0; padding: 6px 3px; border: 1px solid #c5d4d8; border-radius: 4px; background: #fff; color: #59737b; font-size: 11px; }
.tube-component-library-filters button.selected { border-color: #57aca3; background: #e1f2ef; color: #11796d; }
.tube-component-library-list { min-height: 0; padding: 8px; overflow: auto; }
.tube-component-library-group { margin-bottom: 9px; overflow: hidden; border: 1px solid #cbd8dc; border-radius: 6px; background: #fff; }
.tube-component-library-group-heading { display: flex; align-items: center; justify-content: space-between; gap: 6px; width: 100%; padding: 8px; border: 0; background: #e9f0f2; color: #294e57; text-align: left; }
.tube-component-library-group-heading small { flex-shrink: 0; color: #72868c; }
.tube-component-library-group > div[hidden] { display: none; }
.tube-component-library-template > div { padding: 6px; }
.tube-component-library-template > div > .tube-component-library-group:last-child { margin-bottom: 0; }
.tube-component-library-card { display: flex; align-items: center; gap: 9px; width: 100%; padding: 10px 8px; border: 0; border-top: 1px solid #e0e7e9; background: #fff; color: #2b4c55; text-align: left; cursor: pointer; }
.tube-component-library-card:hover { background: #f0f7f6; }
.tube-component-library-card.selected { background: #e2f2ef; box-shadow: inset 3px 0 #178f82; }
.tube-component-library-icon { width: 34px; height: 34px; flex: 0 0 34px; color: #438e89; }
.tube-component-library-icon svg { width: 100%; height: 100%; }
.tube-component-library-card > span:last-child { display: grid; min-width: 0; gap: 3px; }
.tube-component-library-card strong, .tube-component-library-card small, .tube-component-library-card em { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.tube-component-library-card strong { font-size: 12px; }
.tube-component-library-card small, .tube-component-library-card em { color: #74888f; font-size: 10px; font-style: normal; }
.tube-component-library-panel > footer, .tube-component-library-editor > footer { display: flex; justify-content: space-between; gap: 8px; padding: 10px; border-top: 1px solid #cbd8dc; background: #fff; }
.tube-component-library-editor { height: 100%; min-height: 0; overflow: hidden; box-sizing: border-box; grid-template-rows: auto minmax(0, 1fr) auto; gap: 0; }
.tube-component-library-editor-body { display: grid; align-content: start; gap: 12px; min-height: 0; padding: 13px; overflow: auto; }
.tube-component-library-editor textarea, .tube-component-library-dialog textarea { box-sizing: border-box; width: 100%; padding: 7px 8px; border: 1px solid #bdcdd2; border-radius: 4px; resize: vertical; font: inherit; }
.tube-component-library-bounds { display: grid; gap: 7px; padding: 10px; border: 1px solid #cfdddf; border-radius: 5px; background: #fff; }
.tube-component-library-bounds > strong { color: #41606a; font-size: 11px; }
.tube-component-library-bounds dl { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 7px; margin: 0; }
.tube-component-library-bounds dt { color: #799097; font-size: 10px; }
.tube-component-library-bounds dd { margin: 4px 0 0; overflow-wrap: anywhere; color: #30525b; font-size: 12px; font-weight: 600; }
.tube-component-library-bounds small, .tube-component-library-note, .tube-component-library-source { color: #778b92; font-size: 11px; line-height: 1.6; }
.tube-component-library-source { display: grid; gap: 3px; overflow-wrap: anywhere; }
.tube-component-library-source strong { color: #45636c; font-weight: 500; }
.tube-component-library-error { display: grid; gap: 8px; padding: 10px; border: 1px solid #e8b5a1; border-radius: 4px; background: #fff4ee; color: #994825; overflow-wrap: anywhere; font-size: 12px; }
.tube-component-library-hud { position: absolute; top: 12px; left: 12px; right: 145px; z-index: 4; display: grid; gap: 4px; width: fit-content; max-width: calc(100% - 165px); padding: 11px 14px; border: 1px solid #36545d; border-radius: 6px; background: rgba(13, 34, 42, .92); color: #d6e8ea; pointer-events: none; }
.tube-component-library-hud strong { font-size: 14px; overflow-wrap: anywhere; }
.tube-component-library-hud span { color: #a8c0c6; font-size: 12px; }
.tube-component-library-hud small { color: #7d9ca6; font-size: 11px; }
.tube-component-library-hud p { color: #ffd0af; font-size: 12px; }
.tube-component-library-hud button { pointer-events: auto; }
.tube-component-library-dialog .tube-designer-preset-dialog-body { display: grid; gap: 12px; max-height: 65vh; overflow: auto; }
.tube-component-model-field { display: grid; gap: 5px; }
.tube-component-model-field > label { display: grid; gap: 5px; }
.tube-component-model-field small { color: #6a818a; font-size: 10px; }
.tube-component-model-field > button { justify-self: start; min-height: 25px; padding: 3px 7px; font-size: 10px; }

.tube-component-csg-page-tree { display: grid; grid-template-rows: auto auto auto minmax(0, 1fr) auto; min-height: 0; overflow: hidden; }
.tube-component-csg-page-tree .tube-designer-heading { align-items: center; }
.tube-component-csg-page-tree .tube-designer-heading > button { flex: 0 0 auto; padding: 5px 8px; font-size: 10px; }
.tube-component-csg-session-note { display: grid; gap: 3px; margin: 8px; padding: 9px 10px; border: 1px solid #a8cfca; border-radius: 5px; background: #e8f4f2; }
.tube-component-csg-session-note strong { color: #276c65; font-size: 11px; overflow-wrap: anywhere; }
.tube-component-csg-session-note span { color: #64817f; font-size: 9px; line-height: 1.45; }
.tube-component-csg-add-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 5px; padding: 4px 8px 9px; border-bottom: 1px solid #d1dee1; }
.tube-component-csg-add-grid > span { grid-column: 1 / -1; color: #71878c; font-size: 9px; font-weight: 700; }
.tube-component-csg-add-grid button { display: flex; align-items: center; gap: 6px; min-height: 30px; padding: 5px 7px; border: 1px solid #c3d2d5; border-radius: 4px; background: #fff; color: #45636b; cursor: pointer; font-size: 9px; }
.tube-component-csg-add-grid button:hover { border-color: #5db0a6; background: #eaf5f3; }
.tube-component-csg-add-grid button:disabled { opacity: .45; cursor: default; }
.tube-component-csg-add-grid b { color: #238d80; font-size: 15px; }
.tube-component-csg-expression-wrap { min-height: 0; padding: 8px 6px 12px; overflow: auto; background: #f9fbfb; }
.tube-component-csg-expression, .tube-component-csg-expression ul { margin: 0; padding: 0; list-style: none; }
.tube-component-csg-expression ul { position: relative; margin-left: 14px; padding-left: 12px; border-left: 1px solid #adc3c7; }
.tube-component-csg-expression ul > li { position: relative; padding-top: 5px; }
.tube-component-csg-expression ul > li::before { content: ""; position: absolute; top: 21px; left: -12px; width: 10px; border-top: 1px solid #adc3c7; }
.tube-component-csg-expression button { display: grid; grid-template-columns: 26px minmax(0, 1fr); align-items: center; gap: 7px; width: 100%; min-width: 0; padding: 6px 7px; border: 1px solid transparent; border-radius: 5px; background: transparent; color: #3d5b63; text-align: left; cursor: pointer; }
.tube-component-csg-expression button:hover { background: #edf4f4; }
.tube-component-csg-expression button.selected { border-color: #62aea5; background: #dff0ed; box-shadow: inset 3px 0 #2b968a; }
.tube-component-csg-expression button em { display: grid; place-items: center; width: 23px; height: 23px; border-radius: 5px; background: #d8ebe8; color: #177d71; font-size: 14px; font-style: normal; }
.tube-component-csg-expression .operation-difference > button em { background: #f5ded7; color: #b45e45; }
.tube-component-csg-expression .operation-intersection > button em { background: #f3e8ca; color: #94701d; }
.tube-component-csg-expression button > span { display: grid; gap: 1px; min-width: 0; }
.tube-component-csg-expression button strong, .tube-component-csg-expression button small { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.tube-component-csg-expression button strong { font-size: 10px; }
.tube-component-csg-expression button small { color: #7a8d92; font-size: 8px; }
.tube-component-csg-tree-actions { display: grid; grid-template-columns: 1fr 1fr; gap: 5px; padding: 8px; border-top: 1px solid #d0dde0; background: #f4f7f8; }
.tube-component-csg-tree-actions button { min-height: 28px; border: 1px solid #c1d0d3; border-radius: 4px; background: #fff; color: #526c73; cursor: pointer; font-size: 9px; }
.tube-component-csg-tree-actions .danger { grid-column: 1 / -1; color: #a45441; }
.tube-component-csg-tree-actions button:disabled { opacity: .4; cursor: default; }
.tube-component-csg-page-inspector { display: grid; grid-template-rows: auto minmax(0, 1fr) auto; min-height: 0; overflow: hidden; }
.tube-component-csg-page-inspector > .tube-component-csg-inspector { min-height: 0; }
.tube-component-csg-page-save { display: grid; gap: 7px; padding: 9px; border-top: 1px solid #c8d7da; background: #fff; }
.tube-component-csg-page-save small { color: #71878c; font-size: 9px; line-height: 1.4; }
.tube-component-csg-page-save button { width: 100%; min-height: 34px; }
.tube-component-csg-operands { display: grid; grid-template-columns: 1fr 1fr; gap: 6px; margin: 0; padding: 0 12px; }
.tube-component-csg-operands div { min-width: 0; padding: 7px; border: 1px solid #d0dcdf; border-radius: 4px; background: #f3f7f7; }
.tube-component-csg-operands dt { color: #7c8e93; font-size: 8px; }
.tube-component-csg-operands dd { margin: 2px 0 0; overflow: hidden; color: #405e66; font-size: 9px; text-overflow: ellipsis; white-space: nowrap; }
.tube-component-csg-page-overlay { position: absolute; inset: 0; z-index: 4; pointer-events: none; }
.tube-component-csg-page-toolbar { position: absolute; top: 12px; left: 12px; right: 76px; z-index: 6; display: flex; align-items: center; gap: 5px; width: fit-content; max-width: calc(100% - 98px); padding: 6px 8px; border: 1px solid #3b5c64; border-radius: 6px; background: rgba(10, 31, 39, .94); color: #9db5ba; pointer-events: auto; box-shadow: 0 5px 18px rgba(3, 18, 24, .22); }
.tube-component-csg-page-toolbar > strong { margin-right: 5px; color: #d8e8ea; font-size: 11px; white-space: nowrap; }
.tube-component-csg-page-toolbar > span { color: #78959c; font-size: 8px; }
.tube-component-csg-page-toolbar > i { align-self: stretch; width: 1px; margin: 1px 3px; background: #3d5961; }
.tube-component-csg-page-toolbar button { min-height: 27px; padding: 4px 7px; border: 1px solid #41636b; border-radius: 4px; background: #173840; color: #adc2c6; cursor: pointer; font-size: 8px; white-space: nowrap; }
.tube-component-csg-page-toolbar button:hover { border-color: #66b7ac; color: #e5f1f2; }
.tube-component-csg-page-toolbar .tube-component-csg-toolset { border-color: #36575f; background: #102d35; }
.tube-component-csg-page-caption { position: absolute; top: 64px; left: 16px; z-index: 5; display: grid; gap: 2px; color: #d5e6e9; pointer-events: none; }
.tube-component-csg-page-caption strong { font-size: 12px; }
.tube-component-csg-page-caption span { color: #7898a0; font-size: 9px; }
.tube-component-csg-page-overlay.tool-move .tube-component-csg-manipulation { cursor: move; }
.tube-component-csg-page-overlay.tool-rotate .tube-component-csg-manipulation { cursor: ew-resize; }
.tube-component-csg-page-overlay.tool-scale .tube-component-csg-manipulation { cursor: nwse-resize; }

.tube-component-csg-backdrop { z-index: 990; padding: 18px; }
.tube-component-csg-dialog { display: grid; grid-template-rows: auto auto minmax(0, 1fr) auto; width: min(1420px, calc(100vw - 36px)); height: min(860px, calc(100vh - 36px)); overflow: hidden; border: 1px solid #6f9199; border-radius: 10px; background: #f5f8f9; color: #29434b; box-shadow: 0 28px 80px rgba(3, 17, 23, .52); }
.tube-component-csg-toolbar { display: flex; align-items: center; gap: 6px; min-height: 54px; padding: 7px 13px; border-bottom: 1px solid #bfd0d4; background: #edf3f4; }
.tube-component-csg-toolbar > span { margin: 0 3px; color: #637b82; font-size: 10px; font-weight: 700; white-space: nowrap; }
.tube-component-csg-toolbar button { display: inline-flex; align-items: center; gap: 6px; min-height: 36px; padding: 5px 11px; border: 1px solid #b9cbcf; border-radius: 5px; background: #fff; color: #34545c; cursor: pointer; font-size: 10px; }
.tube-component-csg-toolbar button:hover { border-color: #48a99f; background: #e5f3f0; }
.tube-component-csg-toolbar button b { color: #279b8d; font-size: 18px; font-weight: 500; line-height: 1; }
.tube-component-csg-toolbar > i { align-self: stretch; width: 1px; margin: 2px 5px; background: #c6d4d7; }
.tube-component-csg-toolset { display: flex; align-items: center; gap: 3px; padding: 3px; border: 1px solid #c5d3d6; border-radius: 6px; background: #dfe8ea; }
.tube-component-csg-toolset button { min-height: 28px; padding: 4px 9px; border-color: transparent; background: transparent; }
.tube-component-csg-toolset button.selected { border-color: #72afa8; background: #fff; color: #177b70; box-shadow: 0 1px 3px rgba(24, 69, 73, .14); }
.tube-component-csg-hint { color: #72878d; font-size: 9px !important; font-weight: 400 !important; }
.tube-component-csg-body { display: grid; grid-template-columns: 230px minmax(390px, 1fr) 290px; min-height: 0; }
.tube-component-csg-body > aside { min-width: 0; min-height: 0; background: #f8fafb; }
.tube-component-csg-body > main { position: relative; display: block; min-width: 0; min-height: 0; overflow: hidden; border-inline: 1px solid #c3d1d5; background: #102832; }
.tube-component-csg-tree { display: grid; grid-template-rows: auto minmax(0, 1fr) auto; }
.tube-component-csg-tree > header, .tube-component-csg-inspector section > header { display: flex; align-items: center; justify-content: space-between; gap: 8px; padding: 11px 12px; border-bottom: 1px solid #d3dfe2; }
.tube-component-csg-tree header strong, .tube-component-csg-inspector header strong { color: #36565e; font-size: 11px; }
.tube-component-csg-tree header small, .tube-component-csg-inspector header small { color: #7a8d92; font-size: 9px; }
.tube-component-csg-tree > div { min-height: 0; padding: 7px; overflow: auto; }
.tube-component-csg-tree > div > button { display: grid; grid-template-columns: 27px minmax(0, 1fr); align-items: center; gap: 7px; width: 100%; padding: 8px; border: 1px solid transparent; border-radius: 5px; background: transparent; color: #3b5a62; text-align: left; cursor: pointer; }
.tube-component-csg-tree > div > button:hover { background: #edf4f4; }
.tube-component-csg-tree > div > button.selected { border-color: #77b8b1; background: #dff0ed; }
.tube-component-csg-tree em { display: grid; place-items: center; width: 24px; height: 24px; border-radius: 50%; background: #d5e9e6; color: #167d70; font-size: 15px; font-style: normal; }
.tube-component-csg-tree .operation-difference em { background: #f5ded7; color: #b45e45; }
.tube-component-csg-tree .operation-intersection em { background: #f3e8ca; color: #9a741e; }
.tube-component-csg-tree button > span { display: grid; gap: 2px; min-width: 0; }
.tube-component-csg-tree button strong, .tube-component-csg-tree button small { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.tube-component-csg-tree button strong { font-size: 10px; }
.tube-component-csg-tree button small { color: #788b90; font-size: 8px; }
.tube-component-csg-tree > footer { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 5px; padding: 8px; border-top: 1px solid #d3dfe2; }
.tube-component-csg-tree > footer button { min-height: 27px; border: 1px solid #c5d3d6; border-radius: 4px; background: #fff; color: #526b72; cursor: pointer; font-size: 9px; }
.tube-component-csg-tree > footer .danger { color: #a55343; }
.tube-component-csg-tree button:disabled, .tube-component-csg-toolbar button:disabled { opacity: .42; cursor: default; }
.tube-component-csg-viewport { position: absolute; inset: 0; min-height: 330px; }
.tube-component-csg-viewport > .icax-three-viewport { width: 100%; height: 100%; }
.tube-component-csg-viewport .icax-three-viewport-canvas { width: 100%; height: 100%; }
.tube-component-csg-manipulation { position: absolute; inset: 0; z-index: 3; pointer-events: none; outline: 0; }
.tube-component-csg-manipulation.active { pointer-events: auto; touch-action: none; }
.tube-component-csg-viewport-pane.tool-move .tube-component-csg-manipulation { cursor: move; }
.tube-component-csg-viewport-pane.tool-rotate .tube-component-csg-manipulation { cursor: ew-resize; }
.tube-component-csg-viewport-pane.tool-scale .tube-component-csg-manipulation { cursor: nwse-resize; }
.tube-component-csg-manipulation.dragging { background: radial-gradient(circle at center, rgba(52, 170, 156, .05), transparent 58%); }
.tube-component-csg-view-label { position: absolute; top: 14px; left: 16px; z-index: 5; display: grid; gap: 2px; max-width: 50%; color: #d5e6e9; pointer-events: none; }
.tube-component-csg-view-label strong { font-size: 12px; }
.tube-component-csg-view-label span { color: #7898a0; font-size: 9px; }
.tube-component-csg-view-actions { position: absolute; top: 12px; right: 72px; z-index: 6; display: flex; gap: 3px; }
.tube-component-csg-view-actions button { min-height: 25px; padding: 3px 7px; border: 1px solid #3d6068; border-radius: 4px; background: rgba(13, 36, 44, .88); color: #aac0c5; cursor: pointer; font-size: 8px; }
.tube-component-csg-view-actions button:hover { border-color: #59a99f; color: #e6f2f3; }
.tube-component-csg-preview-status { position: absolute; left: 112px; bottom: 12px; z-index: 5; display: flex; align-items: center; gap: 6px; max-width: calc(100% - 310px); padding: 6px 9px; border: 1px solid #385b63; border-radius: 5px; background: rgba(10, 31, 39, .9); color: #a7bdc2; font-size: 8px; pointer-events: none; }
.tube-component-csg-preview-status i { width: 7px; height: 7px; flex: 0 0 auto; border-radius: 50%; background: #6d8990; }
.tube-component-csg-preview-status.loading i { border: 2px solid rgba(96, 207, 191, .3); border-top-color: #60cfbf; background: transparent; animation: tube-designer-spin .8s linear infinite; }
.tube-component-csg-preview-status.ready i { background: #52c5b3; box-shadow: 0 0 0 3px rgba(82, 197, 179, .12); }
.tube-component-csg-preview-status.error { border-color: #8e5548; background: rgba(59, 28, 23, .94); color: #ffc3b3; }
.tube-component-csg-preview-status.error i { background: #e47d61; }
.tube-component-csg-legend { position: absolute; right: 14px; bottom: 14px; z-index: 5; display: flex; gap: 12px; color: #9cb2b8; font-size: 8px; pointer-events: none; }
.tube-component-csg-legend span { display: inline-flex; align-items: center; gap: 4px; }
.tube-component-csg-legend i { width: 8px; height: 8px; border-radius: 2px; }
.tube-component-csg-legend i.result { background: #78b5c6; }
.tube-component-csg-legend i.operand { background: rgba(240, 162, 61, .8); }
.tube-component-csg-inspector { overflow: auto; }
.tube-component-csg-inspector section { display: grid; gap: 9px; padding-bottom: 12px; border-bottom: 1px solid #d1dee1; }
.tube-component-csg-inspector section > label { display: grid; gap: 4px; padding-inline: 12px; color: #6c8187; font-size: 9px; }
.tube-component-csg-operation { display: grid; gap: 5px; padding-inline: 12px; color: #6c8187; font-size: 9px; }
.tube-component-csg-operation > div { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 4px; }
.tube-component-csg-operation button { min-height: 29px; border: 1px solid #bdccd0; border-radius: 4px; background: #fff; color: #536c73; cursor: pointer; font-size: 9px; }
.tube-component-csg-operation button.selected { border-color: #43a89d; background: #e0f1ee; color: #16796e; box-shadow: inset 0 0 0 1px rgba(67, 168, 157, .15); }
.tube-component-csg-operation button:disabled { opacity: .45; cursor: default; }
.tube-component-csg-base-note { margin: 0 12px; padding: 7px 8px; border-left: 3px solid #5bb8ad; background: #eaf5f3; color: #668087; font-size: 9px; line-height: 1.45; }
.tube-component-csg-inspector input, .tube-component-csg-inspector select, .tube-component-csg-inspector textarea { width: 100%; min-width: 0; min-height: 29px; box-sizing: border-box; padding: 5px 7px; border: 1px solid #bdccd0; border-radius: 4px; background: #fff; color: #29434b; font: inherit; font-size: 10px; resize: vertical; }
.tube-component-csg-inspector input:focus, .tube-component-csg-inspector select:focus, .tube-component-csg-inspector textarea:focus { border-color: #42a99f; outline: 0; box-shadow: 0 0 0 2px rgba(66, 169, 159, .14); }
.tube-component-csg-field-grid { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 6px; padding-inline: 12px; }
.tube-component-csg-field-grid label { display: grid; gap: 3px; min-width: 0; color: #71858b; font-size: 8px; }
.tube-component-csg-profile-editor { display: grid; gap: 8px; margin: 2px 9px; padding: 9px 0; border: 1px solid #c5d6d8; border-radius: 6px; background: #f5f9f9; }
.tube-component-csg-profile-editor > header { padding-block: 0 !important; }
.tube-component-csg-profile-editor > label { display: grid; gap: 4px; padding-inline: 9px !important; color: #647c82; font-size: 9px; }
.tube-component-csg-profile-summary { display: grid; gap: 2px; margin-inline: 9px; padding: 7px 8px; border-left: 3px solid #43a99e; background: #e6f3f1; }
.tube-component-csg-profile-summary strong { color: #286b64; font-size: 10px; }
.tube-component-csg-profile-summary span { color: #66817e; font-size: 8px; }
.tube-component-csg-profile-editor > p { margin: 0 9px; color: #70858a; font-size: 8px; line-height: 1.5; }
.tube-component-csg-profile-parameters { grid-template-columns: repeat(2, minmax(0, 1fr)); padding-inline: 9px; }
.tube-component-csg-profile-actions { display: grid; grid-template-columns: 1fr 1fr; gap: 5px; padding-inline: 9px; }
.tube-component-csg-profile-actions button { min-height: 29px; border: 1px solid #aebfc3; border-radius: 4px; background: #fff; color: #3d6769; cursor: pointer; font-size: 9px; }
.tube-component-csg-profile-actions button:hover { border-color: #42a99f; background: #e9f5f3; }
.tube-component-csg-profile-actions button:disabled { opacity: .45; cursor: default; }
.tube-component-csg-footer { display: flex; align-items: center; justify-content: space-between; gap: 16px; min-height: 58px; box-sizing: border-box; padding: 9px 14px; border-top: 1px solid #bdcdd1; background: #fff; }
.tube-component-csg-footer > span { min-width: 0; overflow: hidden; color: #71868c; font-size: 9px; text-overflow: ellipsis; white-space: nowrap; }
.tube-component-csg-footer > span strong { margin-right: 6px; color: #23897d; }
.tube-component-csg-footer > span.error { color: #a54f3b; white-space: normal; }
.tube-component-csg-footer > div { display: flex; flex: 0 0 auto; gap: 8px; }
@media (max-width: 980px) {
  .tube-component-csg-body { grid-template-columns: 190px minmax(320px, 1fr) 250px; }
  .tube-component-csg-toolbar { overflow-x: auto; }
  .tube-component-csg-hint { display: none; }
}

.tube-designer-punch-backdrop { z-index: 980; }
.tube-designer-punch-dialog > .tube-designer-dialog-header { gap: 12px; min-height: 46px; padding: 8px 12px; }
.tube-designer-punch-dialog > .tube-designer-dialog-header strong { font-size: 14px; }
.tube-designer-punch-dialog > .tube-designer-dialog-header span { font-size: 11px; }
.tube-designer-punch-sheet-dialog > .tube-designer-dialog-header { min-height: 34px; padding: 5px 10px; }
.tube-designer-punch-controls { min-width: 0; border: 0; padding: 0; margin: 0; }
.tube-designer-punch-preview-host { height: clamp(360px, 58vh, 760px); min-height: 360px; position: relative; display: grid; place-items: center; overflow: hidden; border-radius: 6px; color: #b9d5d8; background: #13252d; font-size: 12px; }
.tube-designer-punch-preview-host > .icax-three-viewport { width: 100%; height: 100%; min-width: 0; min-height: 0; }
.tube-designer-punch-scene-notice { position:absolute;z-index:4;left:12px;right:132px;top:12px;padding:9px 12px;border:1px solid #c49b50;border-radius:4px;color:#ffe2aa;background:rgba(54,43,26,.94);font-size:13px;pointer-events:none; }
.tube-designer-punch-scene-notice.is-blocking { top:40%;left:8%;right:8%;text-align:center;color:#e8f0f3;background:#1a303a;border-color:#54727f; }
.tube-designer-punch-preview-host canvas { display: block; width: 100%; height: 100%; }
.tube-designer-punch-preview-actions { display: flex; flex-wrap: wrap; gap: 8px; }
.tube-designer-punch-feature-list nav { display: flex; flex-wrap: wrap; gap: 4px; max-width: 130px; justify-content: flex-end; }
.tube-designer-punch-feature-list .is-suppressed { opacity: .6; }
.tube-designer-punch-fixed-note { grid-column: 1 / -1; padding: 8px; background: #edf6f4; font-size: 11px; color: #386660; }
.tube-designer-punch-dialog { display: grid; grid-template-rows: auto minmax(0, 1fr) auto; width: min(1120px, calc(100vw - 48px)); height: min(720px, calc(100vh - 48px)); overflow: hidden; border: 1px solid rgba(78, 109, 122, .38); border-radius: 11px; background: #f7f9fa; color: var(--designer-ink); box-shadow: 0 24px 64px rgba(3, 17, 23, .42); }
.tube-designer-nesting-punch-dialog { grid-template-rows: auto minmax(0, 1fr) auto; width: min(1440px, calc(100vw - 32px)); height: min(900px, calc(100vh - 32px)); }
.tube-designer-nesting-punch-setup { display: grid; gap: 9px; padding: 11px 16px; border-bottom: 1px solid #cbd5da; background: #fff; }
.tube-designer-nesting-punch-setup-heading { display: flex; align-items: center; justify-content: space-between; gap: 8px; }
.tube-designer-nesting-punch-setup-heading > div { display: grid; gap: 2px; }
.tube-designer-nesting-punch-setup-heading strong { color: #38545c; font-size: 11px; }
.tube-designer-nesting-punch-setup-heading span { color: #789097; font-size: 9px; }
.tube-designer-nesting-punch-setup-badge { padding: 3px 6px; border-radius: 999px; background: #e8f5f2; color: #2c766c !important; font-size: 9px !important; white-space: nowrap; }
.tube-designer-nesting-punch-setup-grid { display: grid; grid-template-columns: minmax(220px, 1.35fr) minmax(160px, .8fr) minmax(90px, .42fr); gap: 8px; align-items: end; }
.tube-designer-nesting-punch-profile-summary { display: grid; align-content: center; gap: 2px; min-height: 45px; padding: 7px 9px; border: 1px solid #cbd9dc; border-radius: 5px; background: #f5faf9; }
.tube-designer-nesting-punch-profile-summary strong { overflow: hidden; color: #315b5c; font-size: 10px; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-nesting-punch-profile-summary span, .tube-designer-nesting-punch-profile-summary small { overflow: hidden; color: #73888e; font-size: 8px; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-nesting-punch-setup-grid .wide { grid-column: span 1; }
.tube-designer-cutting-empty-actions { display: flex; justify-content: center; gap: 6px; }
.tube-designer-punch-body { display: grid; grid-template-columns: minmax(0, 1.15fr) minmax(350px, .85fr); min-height: 0; }
.tube-designer-punch-body--creation { grid-template-columns: minmax(330px, 390px) minmax(0, 1fr); }
.tube-designer-punch-body--creation .tube-designer-punch-editor { border-right: 1px solid #cbd5da; background: #f7f9fa; }
.tube-designer-punch-body--creation .tube-designer-nesting-punch-setup { padding: 0 0 2px; border-bottom: 0; background: transparent; }
.tube-designer-punch-body--creation .tube-designer-nesting-punch-setup-grid { grid-template-columns: minmax(0, 1fr); }
.tube-designer-punch-body--creation .tube-designer-nesting-punch-setup-grid .wide { grid-column: 1; }
.tube-designer-punch-body--creation .tube-designer-punch-preview-pane { align-content: stretch; grid-template-rows: auto minmax(360px, 1fr) auto auto auto; border-right: 0; }
.tube-designer-punch-body--creation .tube-designer-punch-preview-host { width: 100%; height: auto; min-height: 420px; }
.tube-designer-punch-preview-pane { display: grid; align-content: start; gap: 12px; min-width: 0; min-height: 0; padding: 18px; overflow: auto; border-right: 1px solid #cbd5da; background: #edf3f4; }
.tube-designer-punch-editor { display: grid; grid-auto-rows: max-content; align-content: start; gap: 10px; min-width: 0; min-height: 0; padding: 12px; overflow: auto; }
.tube-designer-punch-card { display: grid; gap: 10px; padding: 11px; border: 1px solid #cbd7da; border-radius: 7px; background: #fff; }
.tube-designer-punch-card h3 { display: flex; align-items: center; justify-content: space-between; gap: 8px; margin: 0; color: #38545c; font-size: 11px; }
.tube-designer-punch-card h3 small { color: #7a8d93; font-size: 9px; font-weight: 400; }
.tube-designer-punch-field-grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 8px; }
.tube-designer-punch-field-grid label { display: grid; grid-template-columns: minmax(0, 1fr) auto; gap: 4px 5px; min-width: 0; color: #6e8188; font-size: 9px; }
.tube-designer-punch-field-grid label > span { grid-column: 1 / -1; }
.tube-designer-punch-field-grid label.wide { grid-column: 1 / -1; }
.tube-designer-punch-field-grid input, .tube-designer-punch-field-grid select { width: 100%; min-width: 0; min-height: 27px; box-sizing: border-box; padding: 4px 6px; border: 1px solid #bdcbd0; border-radius: 4px; outline: 0; background: #fff; color: #29424b; font-size: 10px; }
.tube-designer-punch-field-grid input:focus, .tube-designer-punch-field-grid select:focus { border-color: #42a99f; box-shadow: 0 0 0 2px rgba(66, 169, 159, .16); }
.tube-designer-punch-field-grid input[type=checkbox] { width: 17px; min-height: 17px; accent-color: #168f84; }
.tube-designer-punch-field-grid small { align-self: center; color: #7e9197; font-size: 9px; }
.tube-designer-punch-add { justify-self: start; min-height: 27px; padding: 3px 9px; font-size: 9px; }
.tube-designer-punch-schematic-wrap { display: grid; place-items: center; min-height: 250px; padding: 18px; border: 1px solid #365961; border-radius: 8px; background: #122b34; box-shadow: inset 0 0 0 1px rgba(178, 224, 219, .08); }
.tube-designer-punch-schematic { width: 100%; max-height: 300px; }
.tube-designer-punch-preview-note { display: grid; gap: 4px; padding: 10px 11px; border-left: 3px solid #42a99f; background: #fff; color: #667b82; font-size: 10px; line-height: 1.55; }
.tube-designer-punch-preview-note strong { color: #315b5c; font-size: 10px; }
.tube-designer-punch-preview-legend { display: flex; flex-wrap: wrap; gap: 14px; align-items: center; color: #617980; font-size: 10px; }
.tube-designer-punch-preview-legend span { display: inline-flex; gap: 5px; align-items: center; }
.tube-designer-punch-preview-legend i { width: 12px; height: 12px; border: 1px solid rgba(31, 62, 73, .28); border-radius: 3px; }
.tube-designer-punch-preview-legend .is-blank { background: rgba(120, 175, 197, .72); }
.tube-designer-punch-preview-legend .is-tool { background: rgba(240, 162, 61, .72); }
.tube-designer-punch-feature-card { min-height: 0; }
.tube-designer-punch-feature-list { display: grid; gap: 5px; max-height: 250px; overflow: auto; }
.tube-designer-punch-feature-list article { display: grid; grid-template-columns: minmax(0, 1fr) auto; gap: 8px; align-items: center; padding: 7px 8px; border: 1px solid #d4dfe1; border-radius: 5px; background: #f8fbfb; }
.tube-designer-punch-feature-list article > div { display: grid; gap: 2px; min-width: 0; }
.tube-designer-punch-feature-list strong { color: #2f5158; font-size: 10px; }
.tube-designer-punch-feature-list span, .tube-designer-punch-feature-list small { overflow: hidden; color: #71848a; font-size: 8px; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-punch-feature-list button { min-height: 23px; padding: 2px 6px; border: 1px solid #d9b1a7; border-radius: 4px; background: #fff8f6; color: #a14c3d; cursor: pointer; font-size: 8px; }
.tube-designer-punch-empty { padding: 12px; border: 1px dashed #cbd7da; color: #7b8c91; font-size: 9px; line-height: 1.55; text-align: center; }
.tube-designer-punch-error { padding: 8px 10px; border: 1px solid #e8b5a1; border-radius: 5px; background: #fff3ed; color: #994825; font-size: 9px; line-height: 1.5; }
.tube-designer-punch-footer { display: grid; grid-template-columns: minmax(0, 1fr) auto auto; align-items: center; gap: 8px; min-height: 44px; max-height: 52px; box-sizing: border-box; overflow: hidden; padding: 6px 10px; border-top: 1px solid #cbd5da; background: #fff; }
.tube-designer-punch-footer > span { min-width: 0; overflow: hidden; color: #71838a; font-size: 9px; line-height: 1.3; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-punch-footer button { min-height: 28px; padding-inline: 11px; font-size: 9px; }
.tube-designer-punch-sheet-dialog { width: min(1540px, calc(100vw - 24px)); height: min(940px, calc(100vh - 24px)); }
.tube-designer-punch-sheet-workspace { display: grid; grid-template-rows: auto minmax(300px, 1fr) auto auto; min-width: 0; min-height: 0; overflow: hidden; background: #e9eff1; }
.tube-designer-punch-sheet-workspace > .tube-designer-nesting-punch-setup { position: relative; display: grid; gap: 5px; padding: 6px 10px; border-bottom: 1px solid #b9c8cc; background: #f7fafb; }
.tube-designer-punch-sheet-workspace > .tube-designer-nesting-punch-setup .tube-designer-nesting-punch-setup-heading { display: none; }
.tube-designer-punch-sheet-workspace > .tube-designer-nesting-punch-setup .tube-designer-nesting-punch-setup-grid { display: grid; grid-template-columns: minmax(250px, 300px) repeat(5, minmax(82px, 100px)) minmax(100px, 115px) minmax(70px, 78px) minmax(160px, 190px) minmax(180px, 220px); justify-content: start; gap: 5px 7px; min-width: 0; align-items: end; overflow: visible; }
.tube-designer-punch-sheet-workspace > .tube-designer-nesting-punch-setup .tube-designer-nesting-punch-setup-grid > .tube-designer-nesting-punch-profile-summary { display: none; }
.tube-designer-punch-sheet-workspace > .tube-designer-nesting-punch-setup .tube-designer-field { min-width: 0; gap: 4px; font-size: 10px; }
.tube-designer-punch-sheet-workspace > .tube-designer-nesting-punch-setup .tube-designer-field.wide { grid-column: span 1; }
.tube-designer-punch-sheet-workspace > .tube-designer-nesting-punch-setup .tube-designer-punch-main-parameter { min-width: 0; }
.tube-designer-punch-sheet-workspace > .tube-designer-nesting-punch-setup input,
.tube-designer-punch-sheet-workspace > .tube-designer-nesting-punch-setup select { min-height: 28px; padding-block: 3px; font-size: 11px; }
.tube-designer-punch-sheet-workspace > .tube-designer-nesting-punch-setup .tube-designer-nesting-profile-diagram { position: absolute; top: 6px; right: 10px; z-index: 4; }
.tube-designer-punch-sheet-workspace > .tube-designer-nesting-punch-setup .tube-designer-nesting-profile-diagram > summary { min-height: 24px; padding: 4px 8px; border: 1px solid #9fbcbd; border-radius: 3px; background: #f5fbfa; color: #39796f; cursor: pointer; font-size: 10px; white-space: nowrap; }
.tube-designer-punch-sheet-workspace > .tube-designer-nesting-punch-setup .tube-designer-field > span { white-space: nowrap; }
.tube-designer-punch-sheet-workspace > .tube-designer-nesting-punch-setup .tube-designer-field > small { font-size: 10px; }
.tube-designer-punch-sheet-workspace > .tube-designer-punch-existing-main { align-items: center; }
.tube-designer-punch-sheet-scene { display: grid; grid-template-rows: auto minmax(0, 1fr) auto; min-width: 0; min-height: 0; padding: 8px 10px 6px; }
.tube-designer-punch-sheet-scene > header { display: flex; flex-wrap: wrap; align-items: center; justify-content: space-between; gap: 5px 10px; min-height: 30px; }
.tube-designer-punch-sheet-scene > header > div { display: flex; align-items: baseline; gap: 8px; }
.tube-designer-punch-sheet-scene > header strong { color: #304f58; font-size: 11px; }
.tube-designer-punch-sheet-scene > header span { color: #73878e; font-size: 9px; }
.tube-designer-punch-sheet-scene > header nav { display: flex; gap: 5px; }
.tube-designer-punch-sheet-scene > header button { min-height: 25px; padding: 3px 8px; font-size: 9px; }
.tube-designer-punch-preview-modes { display:inline-flex; gap:0; margin-right:6px; }
.tube-designer-punch-preview-modes button[aria-pressed="true"] { color:#126e67; background:#e2f2ef; border-color:#61a59d; }
.tube-designer-punch-parameter-content > .tube-designer-punch-preview-modes { margin-top:14px; }
.tube-designer-punch-sheet-scene .tube-designer-punch-preview-host { width: 100%; height: auto; min-height: 285px; border: 1px solid #314d55; border-radius: 3px; }
.tube-designer-punch-sheet-scene > footer { display: flex; align-items: center; justify-content: space-between; gap: 12px; min-height: 25px; color: #667d84; font-size: 9px; }
.tube-designer-punch-sheet-scene > footer strong { color: #307269; }
.tube-designer-punch-sheet-errors { display: grid; gap: 4px; }
.tube-designer-punch-sheet-errors > .tube-designer-punch-error,
.tube-designer-punch-sheet-errors > .tube-designer-punch-fixed-note,
.tube-designer-punch-sheet-message { margin: 0 10px 5px; padding: 5px 8px; }
.tube-designer-punch-sheet { display: grid; grid-template-rows: auto auto; min-width: 0; min-height: 0; margin: 0 10px 9px; border: 1px solid #aebfc4; background: #fff; }
.tube-designer-punch-sheet > header { display: flex; align-items: center; justify-content: space-between; gap: 12px; min-height: 32px; padding: 0 9px; border-bottom: 1px solid #b8c7cb; background: #f4f7f8; }
.tube-designer-punch-sheet > header > div { display: flex; align-items: baseline; gap: 8px; }
.tube-designer-punch-sheet > header strong { color: #304f58; font-size: 10px; }
.tube-designer-punch-sheet > header span, .tube-designer-punch-sheet > header small { color: #72878d; font-size: 8px; }
.tube-designer-punch-sheet-scroll { min-width: 0; min-height: 0; max-height: 270px; overflow: auto; }
.tube-designer-punch-sheet table { width: max(100%, 1800px); border-spacing: 0; border-collapse: separate; table-layout: fixed; color: #314b53; font-size: 9px; }
.tube-designer-punch-sheet th, .tube-designer-punch-sheet td { height: 40px; box-sizing: border-box; padding: 3px 4px; border-right: 1px solid #d2dcdf; border-bottom: 1px solid #d2dcdf; vertical-align: middle; }
.tube-designer-punch-sheet thead th { position: sticky; top: 0; z-index: 2; height: 31px; background: #e7edef; color: #506a72; font-weight: 600; text-align: left; white-space: nowrap; }
.tube-designer-punch-sheet thead small { color: #84969b; font-size: 7px; font-weight: 400; }
.tube-designer-punch-sheet tbody th { position: sticky; left: 0; z-index: 1; width: 34px; background: #f2f5f6; color: #60777e; text-align: center; }
.tube-designer-punch-sheet tbody tr.is-selected td, .tube-designer-punch-sheet tbody tr.is-selected th { background-color: #edf8f6; }
.tube-designer-punch-sheet tbody tr.is-new td, .tube-designer-punch-sheet tbody tr.is-new th { background-color: #f1f8f5; border-bottom-color: #a9cbc4; }
.tube-designer-punch-sheet tbody tr.is-suppressed { opacity: .58; }
.tube-designer-punch-sheet tbody tr.is-end td, .tube-designer-punch-sheet tbody tr.is-end th { background: #f5f1e7; }
.tube-designer-punch-sheet tbody tr.is-end strong { color: #765d2e; white-space: nowrap; }
.tube-designer-punch-sheet th:nth-child(1) { width: 34px; }
.tube-designer-punch-sheet th:nth-child(2) { width: 44px; }
.tube-designer-punch-sheet th:nth-child(3) { width: 105px; }
.tube-designer-punch-sheet th:nth-child(4) { width: 180px; }
.tube-designer-punch-sheet th:nth-child(5) { width: 340px; }
.tube-designer-punch-sheet th:nth-child(6) { width: 130px; }
.tube-designer-punch-sheet th:nth-child(7) { width: 180px; }
.tube-designer-punch-sheet th:nth-child(8) { width: 220px; }
.tube-designer-punch-sheet th:nth-child(9) { width: 150px; }
.tube-designer-punch-sheet th:nth-child(10) { width: 120px; }
.tube-designer-punch-sheet th:nth-child(11) { width: 100px; }
.tube-designer-punch-sheet input:not([type=checkbox]), .tube-designer-punch-sheet select { width: 100%; min-width: 0; height: 25px; box-sizing: border-box; padding: 2px 4px; border: 1px solid transparent; border-radius: 1px; outline: 0; background: transparent; color: #29434b; font: inherit; }
.tube-designer-punch-sheet input:not([type=checkbox]):hover, .tube-designer-punch-sheet select:hover { border-color: #b7c8cc; background: #fff; }
.tube-designer-punch-sheet input:not([type=checkbox]):focus, .tube-designer-punch-sheet select:focus { border-color: #168f84; background: #fff; box-shadow: inset 0 0 0 1px #168f84; }
.tube-designer-punch-sheet-number { display: grid; grid-template-columns: minmax(0, 1fr) auto; align-items: center; gap: 2px; }
.tube-designer-punch-sheet-number small { color: #8a9a9f; font-size: 7px; }
.tube-designer-punch-sheet-parameters { display: flex; align-items: center; gap: 3px; min-width: 0; overflow-x: auto; }
.tube-designer-punch-sheet-parameters > label { display: grid; grid-template-columns: auto minmax(52px, 1fr); align-items: center; gap: 2px; min-width: 78px; }
.tube-designer-punch-sheet-parameters > label > small { color: #7c9096; font-size: 7px; white-space: nowrap; }
.tube-designer-punch-sheet-pair { display: grid; grid-template-columns: 1fr 1.25fr; gap: 2px; }
.tube-designer-punch-sheet-triple { display: grid; grid-template-columns: .8fr 1.2fr 1.2fr; gap: 2px; }
.tube-designer-punch-sheet-stack { display: grid; gap: 2px; }
.tube-designer-punch-sheet-flags { display: flex; flex-wrap: wrap; gap: 2px 5px; }
.tube-designer-punch-sheet-check { display: inline-flex; align-items: center; gap: 2px; white-space: nowrap; }
.tube-designer-punch-sheet-check input { width: 13px; height: 13px; margin: 0; accent-color: #168f84; }
.tube-designer-punch-sheet-readonly { color: #6e858b; font-size: 8px; white-space: nowrap; }
.tube-designer-punch-sheet-row-actions { display: flex; gap: 3px; }
.tube-designer-punch-sheet-row-actions button { min-height: 23px; padding: 2px 5px; border-radius: 2px; font-size: 8px; white-space: nowrap; }
.tube-designer-punch-sheet-row-actions button[data-tube-designer-punch-new] { border-color: #69aaa1; background: #e9f5f2; color: #176d64; }
.tube-designer-punch-sheet-dialog .cam-viewcube { top: 8px; right: 8px; }

.tube-designer-part-drawing-dialog { width: min(1450px, calc(100vw - 32px)); height: min(1040px, calc(100vh - 32px)); }
.tube-designer-part-drawing-dialog .tube-designer-punch-body { grid-template-columns: minmax(0, 1.25fr) minmax(360px, 1fr); }
.tube-designer-part-drawing-dialog .tube-designer-punch-preview-pane { padding: 12px; }
.tube-designer-part-drawing-dialog .tube-designer-punch-field-grid { grid-template-columns: repeat(auto-fit, minmax(min(145px,100%), 1fr)); }
.tube-designer-part-drawing-dialog .tube-designer-punch-field-grid label { font-size: 11px; }
.tube-designer-part-drawing-dialog .tube-designer-punch-field-grid input,
.tube-designer-part-drawing-dialog .tube-designer-punch-field-grid select { min-height: 32px; font-size: 12px; }
.tube-designer-part-drawing-dialog .tube-designer-punch-card h3 { font-size: 13px; }
.tube-drawing-section { display: grid; gap: 8px; min-width: 0; }
.tube-drawing-section.is-end-section { gap: 5px; padding: 6px 0; border-top: 1px solid #d6e2e4; border-bottom: 1px solid #d6e2e4; }
.tube-drawing-section.is-end-section .tube-designer-punch-field-grid { gap: 5px; }
.tube-drawing-section.is-end-section .td-draw-section-diagram > summary { padding-block: 4px; }
.td-draw-backdrop { padding: 0; }
.tube-drawing-preview-mode { display: flex; gap: 8px; flex-wrap: wrap; }
.tube-drawing-preview-mode [aria-pressed="true"] { background: #d9eeeb; border-color: #168e84; color: #146b64; }
.td-draw-workbench { display: grid; grid-template-rows: 36px 88px minmax(0,1fr) 26px; width: calc(100vw - 24px); height: calc(100vh - 24px); overflow: hidden; background: #f5f8f9; color: #263e47; border: 1px solid #8ca2aa; border-radius: 7px; box-shadow: 0 18px 70px #07182080; font: 12px "Segoe UI", "Microsoft YaHei", sans-serif; }
.td-draw-workbench * { box-sizing: border-box; }
.td-draw-title { display: flex; gap: 10px; align-items: center; padding: 0 14px; background: #e3eeef; border-bottom: 1px solid #bccdd0; }
.td-draw-title strong { font-size: 14px; }
.td-draw-title > span:not(.command-icon) { color: #6d8187; border-left: 1px solid #a9c0c4; padding-left: 12px; }
.td-draw-title > small { margin-left: auto; color: #688086; }
.td-draw-workbench .command-icon { display: inline-flex; flex: 0 0 auto; align-items: center; justify-content: center; width: 22px; height: 22px; font-size: 22px; }
.td-draw-workbench .command-icon svg { width: 100%; height: 100%; stroke: #4d6972; stroke-width: 1.5; fill: none; stroke-linecap: round; stroke-linejoin: round; }
.td-draw-workbench .command-icon svg .accent { stroke: #12998b; }
.td-draw-workbench .command-icon svg .fill { fill: #12998b; }
.td-draw-workbench button { font: inherit; cursor: pointer; color: inherit; }
.td-draw-workbench button:disabled { opacity: .38; cursor: default; }
.td-draw-workbench button:focus-visible { outline: 2px solid #19a99b; outline-offset: -2px; }
.td-draw-ribbon { display: flex; padding: 5px 8px; gap: 8px; overflow-x: auto; border-bottom: 1px solid #b7c8ce; background: #fff; }
.td-draw-ribbon-group { position: relative; display: flex; flex: 0 0 auto; gap: 3px; padding: 0 9px 17px 0; border-right: 1px solid #ccdade; }
.td-draw-ribbon-group > button { display: flex; flex-direction: column; justify-content: center; align-items: center; gap: 5px; min-width: 65px; padding: 4px 8px; border: 1px solid transparent; border-radius: 4px; background: transparent; }
.td-draw-ribbon-group > button .command-icon { width: 27px; height: 27px; }
.td-draw-ribbon-group > small { position: absolute; bottom: 0; inset-inline: 0; color: #7b939c; text-align: center; font-size: 10px; }
.td-draw-ribbon-group > button:hover:not(:disabled),.td-draw-workbench [aria-pressed="true"] { border-color: #79b7af; background: #e3f2ef; color: #166f65; }
.td-draw-complete { margin-left: auto; border: 0; padding-right: 0; }
.td-draw-body { display: grid; grid-template-columns: 220px minmax(0,1fr) clamp(330px,27vw,410px); min-height: 0; overflow: hidden; }
.td-draw-sidebar { display: grid; grid-template-rows: minmax(0,1fr); min-height: 0; min-width: 0; border-right: 1px solid #adc1c6; background: #f7fafb; }
.td-draw-parameters { display: grid; min-height: 0; min-width: 0; border-left: 1px solid #adc1c6; background: #f7fafb; }
.td-draw-tree { display: grid; grid-template-rows: 30px minmax(0,1fr); min-height: 0; border-bottom: 1px solid #b7cbd0; }
.td-draw-tree header,.td-draw-inspector header { display: flex; align-items: center; justify-content: space-between; gap: 6px; padding: 8px 12px; background: #eef4f5; border-bottom: 1px solid #d8e3e5; }
.td-draw-tree header span,.td-draw-inspector header small { color: #81969d; font-size: 10px; }
.td-draw-tree [role="tree"] { overflow: auto; padding: 5px; }
.td-draw-tree-node { display: flex; align-items: center; width: 100%; gap: 8px; padding: 5px 8px; text-align: left; border: 1px solid transparent; border-radius: 3px; background: transparent; }
.td-draw-tree-node [class="command-icon vector"] { width: 18px; height: 18px; }
.td-draw-tree-node > span:not(.command-icon) { min-width: 0; flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.td-draw-tree-node small { display: block; color: #7c969d; font-size: 10px; margin-top: 2px; }
.td-draw-tree-node[aria-selected="true"] { background: #dcefeb; border-color: #87c5bd; color: #137f72; }
.td-draw-tree-node:hover { background: #e8f1f2; }
.td-draw-tree-node.is-muted { opacity: .55; }
.td-draw-tree p { color: #81959b; line-height: 1.6; padding: 3px 9px; font-size: 11px; margin: 2px 0; }
.td-draw-inspector { display: grid; grid-template-rows: auto minmax(0,1fr) auto; min-height: 0; }
.td-draw-inspector > header { min-height: 35px; background: #e2efed; }
.td-draw-property-scroll { min-height: 0; min-width: 0; overflow: auto; padding: 12px; scrollbar-gutter: stable; }
.td-draw-properties { border-bottom: 1px solid #d6e2e4; padding: 0 2px 8px; margin-bottom: 8px; }
.td-draw-properties summary { cursor: pointer; color: #355b61; font-weight: 600; padding: 5px 0 8px; }
.td-draw-properties > .tube-designer-punch-field-grid { gap: 8px; }
.td-draw-workbench .tube-designer-punch-field-grid > .wide { grid-column: 1 / -1; min-width: 0; }
.td-draw-workbench .tube-designer-punch-field-grid label { display: grid; grid-template-columns: minmax(0,1fr) auto; gap: 3px; font-size: 11px; color: #617c85; }
.td-draw-workbench .tube-designer-punch-field-grid input,.td-draw-workbench .tube-designer-punch-field-grid select { min-height: 29px; padding: 4px 6px; font-size: 12px; }
.td-draw-workbench .tube-designer-punch-field-grid input[type="checkbox"] { min-height: 16px; }
.td-draw-workbench .tube-drawing-section-preview { gap: 8px; }
.td-draw-workbench .tube-drawing-section-preview > svg { width: 70px; height: 55px; flex-basis: 70px; }
.td-draw-workbench .tube-drawing-section-preview > span { font-size: 11px; }
.td-draw-section-diagram { min-width: 0; margin-top: 10px; }
.td-draw-section-diagram > summary { padding: 8px 0; color: #355b61; cursor: pointer; font-size: 12px; }
.td-draw-section-diagram .tube-profile-parameter-diagram { margin-top: 6px; }
.td-draw-workbench .tube-drawing-section-preview small,.td-draw-workbench .tube-drawing-section > button { display: none; }
.td-draw-inspector > footer { display: flex; justify-content: flex-end; gap: 8px; padding: 8px; border-top: 1px solid #cadbdd; background: #fff; }
.td-draw-inspector > footer button { display: flex; align-items: center; gap: 5px; min-height: 30px; padding: 4px 14px; border: 1px solid #acbec3; border-radius: 4px; background: #fff; }
.td-draw-inspector > footer button:first-child { background: #168f82; color: #fff; border-color: #168f82; }
.td-draw-inspector > footer .command-icon { display: none; }
.td-draw-readonly,.td-draw-inspector-hint { padding: 10px; color: #68848b; line-height: 1.65; font-size: 12px; }
.td-draw-canvas { position: relative; display: grid; grid-template-rows: 39px minmax(0,1fr) auto; min-width: 0; min-height: 0; overflow: hidden; background: #13252d; }
.td-draw-view-toolbar { display: flex; align-items: center; justify-content: space-between; gap: 8px; padding: 5px 12px; border-bottom: 1px solid #29434b; color: #8aabb3; font-size: 11px; }
.td-draw-view-toolbar > div { display: flex; gap: 4px; }
.td-draw-view-toolbar button { color: #aac9ce; background: #19343e; border: 1px solid #35515a; border-radius: 3px; padding: 4px 10px; font-size: 11px; }
.td-draw-preview-host { position: relative; height: 100%; min-height: 0; overflow: hidden; }
.td-draw-preview-status { position: absolute; bottom: 14px; left: 14px; z-index: 5; max-width: calc(100% - 28px); padding: 10px 14px; border: 1px solid #627e85; border-radius: 6px; color: #d8e8eb; background: #19343eef; font-size: 12px; pointer-events: none; }
.td-draw-preview-status[hidden],.td-draw-preview-progress[hidden] { display: none; }
.td-draw-preview-progress { height: 5px; margin-top: 7px; min-width: 160px; overflow: hidden; border-radius: 6px; background: #33545d; }
.td-draw-preview-progress > i { display: block; height: 100%; width: 36%; background: #3dc6af; animation: tube-designer-export-indeterminate 1.05s ease-in-out infinite; }
.td-draw-preview-status button { pointer-events: auto; margin: 6px 0 0 8px; padding: 3px 8px; border: 1px solid #729ba2; border-radius: 4px; background: #254852; color: #e5f5f4; }
.td-draw-view-controls { position: absolute; bottom: 8px; left: 8px; display: flex; flex-wrap: wrap; gap: 4px; z-index: 2; }
.td-draw-view-controls button { color: #cee9e5; background: #1a3b43; border: 1px solid #4c747c; border-radius: 4px; padding: 4px 7px; font-size: 10px; cursor: pointer; }
.td-draw-canvas-status { padding: 7px 12px; background: #152e37; border-top: 1px solid #29434b; color: #8ab1ba; font-size: 11px; }
.td-draw-error { position: absolute; top: 48px; left: 12px; right: 12px; z-index: 4; padding: 9px 12px; border: 1px solid #af7351; background: #fff0e6; color: #8c462b; box-shadow: 0 4px 15px #0003; border-radius: 4px; line-height: 1.5; }
.td-draw-status { display: flex; align-items: center; justify-content: space-between; gap: 12px; padding: 0 12px; background: #edf3f4; color: #6c838b; border-top: 1px solid #b8cdd2; font-size: 10px; }
.tube-designer-template-manager-backdrop { z-index: 970; }
.tube-designer-template-manager-dialog { width: min(980px, calc(100vw - 32px)); max-height: min(720px, calc(100vh - 28px)); display: grid; grid-template-rows: auto auto minmax(0,1fr) auto; overflow: hidden; background: #fff; border: 1px solid #b9cdd2; border-radius: 8px; box-shadow: 0 18px 48px #18313d38; color: #24434c; }
.tube-designer-template-manager-toolbar { display: flex; flex-wrap: wrap; gap: 8px; padding: 12px 18px; border-bottom: 1px solid #d1e0e3; background: #f6faf9; }
.tube-designer-template-manager-toolbar button { min-height: 32px; padding: 5px 13px; border: 1px solid #b7cbd0; border-radius: 4px; background: #fff; color: #315760; cursor: pointer; }
.tube-designer-template-manager-toolbar button.tube-designer-primary { border-color: #168f82; background: #168f82; color: #fff; }
.tube-designer-template-manager-toolbar button.tube-designer-danger { border-color: #d29a86; color: #9b4f37; }
.tube-designer-template-manager-toolbar button:disabled { opacity: .45; cursor: not-allowed; }
.tube-designer-template-manager-body { display: grid; grid-template-columns: minmax(0,1fr) 260px; min-height: 0; overflow: hidden; }
.tube-designer-template-manager-list { overflow: auto; min-width: 0; }
.tube-designer-template-manager-list table { width: 100%; border-collapse: collapse; table-layout: fixed; }
.tube-designer-template-manager-list th,.tube-designer-template-manager-list td { padding: 10px 12px; border-bottom: 1px solid #dce8ea; text-align: left; vertical-align: middle; font-size: 12px; }
.tube-designer-template-manager-list th { position: sticky; top: 0; z-index: 1; background: #edf5f5; color: #648089; font-weight: 600; }
.tube-designer-template-manager-list tbody tr { cursor: pointer; transition: background .12s ease; }
.tube-designer-template-manager-list tbody tr:hover { background: #f2f8f7; }
.tube-designer-template-manager-list tbody tr.selected { background: #e3f3ef; box-shadow: inset 3px 0 #169a8b; }
.tube-designer-template-manager-list td:first-child { width: 58%; }
.tube-designer-template-manager-list td strong,.tube-designer-template-manager-list td small { display: block; overflow-wrap: anywhere; }
.tube-designer-template-manager-list td small { margin-top: 4px; color: #779098; font-size: 10px; }
.tube-designer-template-manager-list td.empty { padding: 32px 16px; color: #789097; text-align: center; }
.tube-designer-template-scope { display: inline-block; padding: 2px 7px; border-radius: 10px; font-size: 10px; }
.tube-designer-template-scope.builtin { background: #e8eef0; color: #647a82; }
.tube-designer-template-scope.personal { background: #dff3ee; color: #167d70; }
.tube-designer-template-manager-summary { display: grid; align-content: start; gap: 8px; padding: 18px; border-left: 1px solid #d1e0e3; background: #f8fbfa; color: #55737b; font-size: 12px; line-height: 1.55; overflow: auto; }
.tube-designer-template-manager-summary strong { color: #244d56; font-size: 15px; overflow-wrap: anywhere; }
.tube-designer-template-manager-summary span { color: #178c7e; }
.tube-designer-template-manager-summary p { margin: 4px 0 8px; }
.tube-designer-template-manager-summary dl { display: grid; grid-template-columns: 60px minmax(0,1fr); gap: 6px 8px; margin: 0; }
.tube-designer-template-manager-summary dt { color: #8aa0a6; }
.tube-designer-template-manager-summary dd { margin: 0; overflow-wrap: anywhere; color: #486a72; }
.tube-designer-template-manager-summary.empty { display: block; color: #789097; }
.tube-designer-template-manager-note { margin: 0; padding: 8px 10px; border-left: 3px solid #38a89b; background: #eef8f6; color: #5a777d; font-size: 11px; line-height: 1.6; }
.tube-designer-template-manager-note code { padding: 1px 4px; border-radius: 3px; background: #dbeeea; color: #176f67; }
.tube-designer-template-manager-dialog textarea { width: 100%; resize: vertical; }
@media (max-width: 760px) { .tube-designer-template-manager-dialog { width: calc(100vw - 18px); max-height: calc(100vh - 16px); } .tube-designer-template-manager-body { grid-template-columns: minmax(0,1fr); } .tube-designer-template-manager-summary { display: none; } }
@media(max-width:1100px) { .td-draw-body { grid-template-columns: 160px minmax(0,1fr) 320px; } .td-draw-ribbon-group > button { min-width: 55px; padding-inline: 5px; font-size: 11px; } .td-draw-ribbon { gap: 4px; } .td-draw-ribbon-group { padding-right: 5px; } }
@media(max-width:850px) { .td-draw-body { grid-template-columns: 140px minmax(0,1fr) 300px; } .td-draw-edit-tools { display: grid; grid-template-rows: repeat(2,26px); grid-auto-flow: column; } .td-draw-edit-tools > button { flex-direction: row; gap: 3px; } .td-draw-edit-tools > button .command-icon { width: 16px; height: 16px; } .td-draw-view-toolbar > span,.td-draw-status > span:last-child { display:none; } }
.tube-drawing-section > button { justify-self: start; }
.tube-drawing-section small { color: #667f85; font-size: 10px; }
.tube-drawing-section .tube-drawing-warning { color: #995d23; }
.tube-drawing-section-preview { display: flex; gap: 12px; align-items: center; min-width: 0; }
.tube-drawing-section-preview > svg { width: 76px; height: 60px; flex: 0 0 76px; border-radius: 5px; background: #122b34; }
.tube-drawing-section-preview > span { display: grid; gap: 4px; min-width: 0; overflow-wrap: anywhere; font-size: 11px; }
.tube-drawing-branch-setup { grid-column: 1 / -1; padding: 10px; border: 1px solid #c6dbd8; border-radius: 6px; background: #f1f8f7; }
.tube-designer-punch-preview-host { position: relative; }
.tube-punch-view-controls { position: absolute; bottom: 8px; left: 8px; display: flex; flex-wrap: wrap; gap: 4px; z-index: 2; }
.tube-punch-view-controls button { color: #cee9e5; background: #1a3b43; border: 1px solid #4c747c; border-radius: 4px; padding: 4px 7px; font-size: 10px; cursor: pointer; }
@media (max-width: 900px) {
  .tube-designer-part-drawing-dialog .tube-designer-punch-body { grid-template-columns: minmax(0,1fr); }
  .tube-designer-part-drawing-dialog .tube-designer-punch-preview-pane,
  .tube-designer-part-drawing-dialog .tube-designer-punch-editor { min-height: auto; overflow: visible; }
}

@media (max-width: 1180px) {
  .tube-designer-parts-viewport-header { grid-template-columns: minmax(0, 1fr) auto; }
  .tube-designer-part-scene-facts { display: none; }
}

@media (max-width: 900px) {
  .tube-designer-breakdown-footer { grid-template-columns: 1fr auto auto; }
  .tube-designer-breakdown-footer > span { display: none; }
  .tube-designer-config-dialog { width: min(1480px, calc(100vw - 28px)); height: min(860px, calc(100vh - 28px)); }
  .tube-designer-config-body { grid-template-columns: minmax(400px, 46%) minmax(0, 1fr); }
  .tube-designer-config-parameters .tube-designer-field-grid { grid-template-columns: repeat(2, minmax(0, 1fr)); }
  .tube-designer-part-inspection-body { grid-template-columns: minmax(0, 1fr) 300px; }
  .tube-designer-production-workspace { grid-template-columns: 300px 5px minmax(360px, 1fr) 5px 260px; }
  .tube-designer-parts-view-actions button:first-child { display: none; }
  .tube-designer-punch-dialog { width: min(720px, calc(100vw - 28px)); height: min(860px, calc(100vh - 28px)); }
  .tube-designer-nesting-punch-dialog { grid-template-rows: auto minmax(0, 1fr) auto; height: min(900px, calc(100vh - 20px)); }
  .tube-designer-nesting-punch-setup-grid { grid-template-columns: repeat(2, minmax(0, 1fr)); }
  .tube-designer-nesting-punch-setup-grid .wide { grid-column: 1 / -1; }
  .tube-designer-punch-body { grid-template-columns: minmax(0, 1fr); overflow: auto; }
  .tube-designer-punch-body--creation .tube-designer-punch-editor { border-right: 0; border-bottom: 1px solid #cbd5da; }
  .tube-designer-punch-preview-pane { border-right: 0; border-bottom: 1px solid #cbd5da; }
}
`;
export const tubeDesignerCss=baseTubeDesignerCss+punchLayoutStyles+punchReviewStyles+punchRecordAreasCss+tileWindowCss;
