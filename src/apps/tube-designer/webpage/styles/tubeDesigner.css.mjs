export const tubeDesignerCss = String.raw`
.tube-designer-workspace {
  --designer-border: rgba(122, 151, 166, 0.22);
  --designer-panel: #f6f8f9;
  --designer-ink: #21313a;
  --designer-muted: #697a84;
  --designer-accent: #178f82;
}

.tube-designer-workspace .cam-context-pane,
.tube-designer-workspace .cam-info-pane {
  background: var(--designer-panel);
  color: var(--designer-ink);
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
`;
