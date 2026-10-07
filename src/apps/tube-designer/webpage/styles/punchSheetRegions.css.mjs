export const punchSheetRegionsCss = `
.tube-designer-punch-backdrop:has(> .tube-designer-punch-sheet-dialog) { padding:12px; }
.tube-designer-punch-sheet-dialog {
  --td-dialog-font-size:11px; --td-dialog-control-height:24px;
  --td-dialog-header-height:32px; --td-dialog-header-padding:3px 8px;
  --td-dialog-title-size:13px; --td-dialog-close-size:26px; --td-dialog-button-padding:2px 8px;
  position:relative;
  display:grid; grid-template-rows:auto minmax(0,1fr) auto;
  width:calc(100vw - 24px); height:calc(100vh - 24px); max-height:none;
  overflow:hidden;
}
.tube-designer-punch-sheet-dialog > .tube-designer-dialog-header { display:flex; align-items:center; justify-content:flex-start; }
.tube-designer-punch-sheet-dialog > .tube-designer-dialog-header > div { flex:1; min-width:0; }
.tube-designer-punch-sheet-dialog > .tube-designer-punch-sheet-workspace {
  display:grid; grid-template-columns:minmax(0,1fr);
  grid-template-rows:var(--punch-main-height,100px) 6px var(--punch-ends-height,104px) 6px var(--punch-holes-height,clamp(240px,30vh,360px)) 6px minmax(180px,1fr);
  min-width:0; min-height:0; overflow:hidden; background:#f5f8f9;
}
.tube-designer-punch-sheet-dialog [data-punch-region] {
  grid-column:1; min-width:0; min-height:0; overflow:hidden;
  background:var(--td-dialog-surface,#f5f8f9);
}
[data-punch-region="main"] { --punch-region-min-height:96px; grid-row:1; display:grid; grid-template-rows:minmax(0,1fr); }
[data-punch-region="ends"] { --punch-region-min-height:104px; grid-row:3; }
[data-punch-region="holes"] { --punch-region-min-height:128px; grid-row:5; }
.tube-designer-punch-sheet-dialog [data-punch-region="scene"] { --punch-region-min-height:180px; grid-row:7; }
.punch-main-scroll,.punch-end-table-scroll { min-width:0; min-height:0; overflow:auto; overscroll-behavior:contain; }
.tube-designer-punch-main-region .tube-designer-nesting-punch-setup {
  position:relative; min-width:0; padding:4px 6px; gap:4px; border:0; background:transparent;
}
.tube-designer-punch-main-region .tube-designer-nesting-punch-setup-grid {
  grid-template-columns:minmax(230px,1.35fr) minmax(185px,.8fr) minmax(105px,.4fr) minmax(210px,1fr) minmax(220px,1fr) auto;
  gap:4px 8px; align-items:center;
}
.tube-designer-punch-main-region .tube-designer-field { min-width:0; gap:3px; font-size:11px; }
.tube-designer-punch-main-region .tube-designer-nesting-punch-setup-grid > .tube-designer-field { display:grid; grid-template-columns:auto minmax(0,1fr); align-items:center; gap:5px; }
.tube-designer-punch-main-region .tube-designer-nesting-punch-setup-grid > .tube-designer-field > span { white-space:nowrap; }
.tube-designer-punch-main-region .tube-designer-field.wide { grid-column:auto; }
.tube-designer-punch-main-region input:not([type=checkbox]),.tube-designer-punch-main-region select { width:100%; min-width:0; min-height:24px; height:24px; padding:2px 4px; font-size:11px; }
.tube-designer-punch-main-profile-parameters { min-width:0; }
.tube-designer-punch-sheet-dialog .tube-profile-library-field-grid {
  display:grid; grid-template-columns:repeat(auto-fill,minmax(min(var(--punch-parameter-track-width,290px),100%),var(--punch-parameter-track-width,290px))); justify-content:start; gap:2px 8px; min-width:0; padding:3px;
}
.tube-designer-punch-main-profile-parameters { --punch-parameter-track-width:150px; }
.tube-designer-punch-main-profile-parameters [data-parameter-advanced] { --punch-parameter-track-width:250px; }
.tube-designer-punch-sheet-dialog .tube-profile-library-field-grid > .tube-designer-field,
.tube-designer-punch-sheet-dialog .tube-profile-library-field-grid > .tube-parameter-advanced-item > .tube-designer-field {
  display:grid; grid-template-columns:fit-content(175px) minmax(0,max-content); justify-content:start; align-items:center; gap:6px;
  padding:1px 3px; border:0; background:transparent;
}
.tube-designer-punch-sheet-dialog .tube-profile-library-field-grid .tube-designer-field > span { flex:0 0 auto; min-width:0; }
.tube-designer-punch-sheet-dialog .tube-profile-library-field-grid .is-number > input { width:var(--tube-designer-control-preferred-width,72px); min-width:40px; max-width:var(--tube-designer-control-max-width,84px); }
.tube-designer-punch-sheet-dialog .tube-profile-library-field-grid .tube-designer-field.is-choice:not(.is-line-full) { grid-column:auto; }
.tube-designer-punch-sheet-dialog .tube-profile-library-field-grid .tube-designer-field.is-choice > select { width:180px; max-width:100%; }
.tube-designer-punch-sheet-dialog .tube-profile-library-field-grid > .is-line-full,
.tube-designer-punch-sheet-dialog .tube-profile-library-field-grid > .wide { grid-column:1 / -1; }
.tube-designer-punch-sheet-dialog .tube-profile-library-parameter-group { min-width:0; border:1px solid #c6d4d9; margin:0 0 3px; }
.tube-designer-punch-sheet-dialog .tube-profile-library-parameter-group.is-inline { border:0; margin:0; }
.tube-designer-punch-sheet-dialog .tube-profile-library-parameter-group.is-inline > .tube-profile-library-field-grid { padding:0; }
.tube-designer-punch-sheet-dialog .tube-profile-library-parameter-group > summary { padding:3px 6px; color:#376e72; background:#edf3f4; cursor:pointer; font-size:11px; }
.tube-designer-punch-sheet-dialog .tube-parameter-advanced { padding:2px; }
.tube-designer-punch-sheet-dialog .tube-parameter-advanced > summary { padding:2px 4px; font-size:11px; }
.tube-designer-punch-sheet-dialog .tube-parameter-advanced[open] > summary { margin-bottom:2px; }
.tube-designer-punch-sheet-dialog .tube-profile-library-field-grid .tube-designer-boolean-field {
  display:flex; align-items:center; gap:8px; justify-content:flex-start;
}
.tube-designer-punch-sheet-dialog .tube-profile-library-field-grid .has-control-size > input:not([type=checkbox]),
.tube-designer-punch-sheet-dialog .tube-profile-library-field-grid .has-control-size > select,
.tube-designer-punch-sheet-dialog .tube-profile-library-field-grid .has-control-size > .tube-designer-punch-sheet-number {
  justify-self:start; min-width:min(100%,var(--tube-designer-control-min-width,40px));
  width:min(100%,var(--tube-designer-control-preferred-width,100%));
  max-width:min(100%,var(--tube-designer-control-max-width,100%));
}
.tube-designer-punch-main-region .punch-main-diagram-button { min-height:24px; height:24px; padding:2px 6px; font-size:11px; white-space:nowrap; }
.tube-designer-punch-sheet-dialog > .tube-designer-dialog-header > .tube-designer-punch-sheet-toolbar { margin-left:auto; flex-wrap:nowrap; }
.tube-designer-punch-sheet-dialog > .tube-floating-parameter-diagram-layer { grid-area:1 / 1 / -1 / -1; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-ends,
.tube-designer-punch-sheet-dialog .tube-designer-punch-records,
.tube-designer-punch-sheet-dialog .tube-designer-punch-sheet { height:100%; min-height:0; min-width:0; margin:0; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-ends { display:grid; grid-template-rows:minmax(0,1fr); border:0; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-records { display:block; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-sheet { grid-template-rows:minmax(0,1fr); border:0; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-sheet-toolbar { gap:4px; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-sheet-toolbar button { min-height:24px; height:24px; padding:2px 7px; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-sheet-scroll { max-height:none; overflow:auto; overscroll-behavior:contain; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-sheet table { width:100%; min-width:820px; table-layout:fixed; font-size:11px; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-sheet thead th { height:28px; font-size:11px; }
.tube-designer-punch-sheet-dialog .punch-expanded-columns th { width:auto; padding:2px 4px; line-height:13px; }
.tube-designer-punch-sheet-dialog .punch-expanded-columns thead tr:first-child th { top:0; height:20px; z-index:3; }
.tube-designer-punch-sheet-dialog .punch-expanded-columns thead tr:nth-child(2) th { top:20px; height:26px; }
.tube-designer-punch-sheet-dialog .punch-expanded-columns thead small { display:inline; font-weight:normal; font-size:10px; color:#68808a; }
.tube-designer-punch-sheet-dialog .punch-column-group-toggle { display:flex; width:100%; align-items:center; gap:8px; justify-content:center; padding:0; min-height:16px; border:0; background:transparent; color:#315c66; font-size:11px; }
.tube-designer-punch-sheet-dialog .punch-column-group-toggle small { display:inline; }
.tube-designer-punch-sheet-dialog .punch-expanded-columns td[data-punch-column-key],
.tube-designer-punch-sheet-dialog .punch-expanded-columns td[data-punch-record-column^="arrays:"] { padding:2px 3px; }
.tube-designer-punch-sheet-dialog .punch-expanded-columns input:not([type=checkbox]),
.tube-designer-punch-sheet-dialog .punch-expanded-columns select { width:100%; min-height:24px; height:24px; min-width:0; padding:2px 3px; border:1px solid #bdcfd5; background:#fff; font-size:11px; }
.tube-designer-punch-sheet-dialog .punch-expanded-columns input[type=checkbox] { width:15px; height:15px; accent-color:#16958b; }
.punch-array-column-stack { display:grid; gap:8px; }
.punch-array-column-stack > div { display:flex; align-items:center; justify-content:center; gap:4px; height:24px; min-width:0; }
.punch-array-column-stack > div + div { position:relative; }
.punch-array-column-stack > div + div::before { content:""; position:absolute; left:-4px; right:-4px; top:-5px; border-top:1px solid #d2dcdf; }
.punch-column-na { color:#91a3a9; }
.tube-designer-punch-sheet-dialog .punch-expanded-columns [data-punch-frozen-column] { position:sticky; z-index:4; }
.tube-designer-punch-sheet-dialog .punch-expanded-columns thead [data-punch-frozen-column] { z-index:6; }
.tube-designer-punch-sheet-dialog .punch-expanded-columns [data-punch-frozen-column="number"] { left:0; }
.tube-designer-punch-sheet-dialog .punch-expanded-columns [data-punch-frozen-column="enabled"] { left:44px; }
.tube-designer-punch-sheet-dialog .punch-expanded-columns [data-punch-frozen-column="shape"] { left:88px; border-right:2px solid #b6cccf; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-sheet tbody > tr > th,
.tube-designer-punch-sheet-dialog .tube-designer-punch-sheet td { height:30px; padding:2px 4px; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-sheet-empty { padding:12px 8px !important; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-row-select { display:flex; align-items:center; gap:3px; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-sheet .tube-designer-punch-row-select > button { border:0; padding:2px; min-width:12px; min-height:20px; background:transparent; box-shadow:none; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-source-cell { gap:1px; min-width:0; line-height:12px; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-source-cell > :is(span,small) { display:block; min-width:0; overflow:hidden; white-space:nowrap; text-overflow:ellipsis; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-sheet .tube-designer-punch-source-cell > small { font-size:10px; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-ends table { min-width:750px; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-ends thead th { position:sticky; top:0; z-index:2; height:22px; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-ends td { border-right:1px solid #d2dcdf; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-ends td { padding:2px 4px; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-ends :is(input:not([type=checkbox]),select) { min-height:24px; height:24px; padding:2px 4px; font-size:11px; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-ends th:nth-child(3) { width:220px; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-ends th:nth-child(4) { width:auto; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-inline-row > td { padding:0; }
.tube-designer-punch-sheet-dialog .punch-expanded-columns .tube-designer-punch-inline-editor { position:sticky; left:0; width:var(--punch-table-visible-width,100%); }
.tube-designer-punch-sheet-dialog [data-punch-row-tone="even"] { --punch-row-background:#f5f9fa; }
.tube-designer-punch-sheet-dialog [data-punch-row-tone="odd"] { --punch-row-background:#e7f0f2; }
.tube-designer-punch-sheet-dialog tbody > tr[data-punch-row-tone],
.tube-designer-punch-sheet-dialog tbody > tr[data-punch-row-tone] > td,
.tube-designer-punch-sheet-dialog tbody > tr[data-punch-row-tone] > th { background:var(--punch-row-background); }
.tube-designer-punch-sheet-dialog tbody > tr.is-selected > th { box-shadow:inset 3px 0 #168f88; }
.tube-designer-punch-sheet-dialog td[data-punch-cell-activate] { cursor:text; }
.tube-designer-punch-sheet-dialog td[data-punch-cell-activate]:hover { box-shadow:inset 0 0 0 1px #9cbcb9; }
.tube-designer-punch-sheet-dialog td[data-punch-cell-activate]:focus-visible,
.tube-designer-punch-sheet-dialog td[aria-expanded="true"] { outline:0; box-shadow:inset 0 0 0 2px #168e84; }
.tube-designer-punch-sheet-dialog .punch-record-cell { display:block; padding:0; }
.tube-designer-punch-inline-editor { display:grid; grid-template-columns:minmax(0,1fr) auto; align-items:start; gap:6px; padding:3px 6px; background:var(--punch-row-background); border-block:1px solid #91bdb7; }
.tube-designer-punch-inline-editor > .punch-inline-actions { grid-column:2; grid-row:1; display:flex; gap:5px; }
.tube-designer-punch-inline-editor > .tube-designer-punch-parameter-content { grid-column:1; grid-row:1; min-width:0; padding:0; overflow:visible; max-height:none; }
.tube-designer-punch-inline-editor .tube-designer-punch-sheet-parameters { display:block; overflow:visible; }
.tube-designer-punch-inline-editor .tube-designer-field { display:grid; grid-template-columns:fit-content(175px) minmax(0,max-content); justify-content:start; align-items:center; gap:6px; padding:1px 3px; border:0; background:transparent; }
.tube-designer-punch-inline-editor .tube-designer-field > .tube-designer-punch-sheet-number { width:84px; max-width:100%; }
.tube-designer-punch-inline-editor .tube-designer-field > :is(select,input:not([type=checkbox])) { width:180px; max-width:100%; }
.tube-designer-punch-inline-editor .tube-designer-field > small { font-size:11px; white-space:normal; color:#5c7780; }
.tube-designer-punch-inline-editor input:not([type=checkbox]),.tube-designer-punch-inline-editor select { min-height:24px; height:24px; min-width:0; width:100%; padding:2px 4px; font-size:11px; border:1px solid #becfd3; background:#fff; }
.tube-designer-punch-inline-editor input[type=checkbox] { width:15px; height:15px; margin:2px; justify-self:start; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-inline-editor button { min-height:24px; height:24px; padding:2px 7px; font-size:11px; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-parameter-content .tube-designer-punch-end-placement { margin:3px 0 4px; padding:3px 5px; }
.punch-inline-source { display:flex; flex-wrap:wrap; gap:4px 12px; margin-bottom:4px; }
.tube-designer-punch-cell-editor .punch-inline-source { grid-template-columns:1fr; }
.punch-inline-source label { display:grid; grid-template-columns:auto minmax(0,200px); align-items:center; gap:6px; color:#58727b; font-size:11px; }
.tube-designer-punch-inline-editor .punch-pose-fields { grid-template-columns:repeat(4,minmax(0,1fr)); margin:0; gap:8px 12px; }
.tube-designer-punch-inline-editor .punch-array-fields { grid-template-columns:repeat(4,minmax(0,1fr)); }
.tube-designer-punch-inline-editor .punch-array-group > header { gap:8px; }
.tube-designer-punch-inline-editor .punch-array-check { display:flex; align-items:center; gap:6px; }
.tube-designer-punch-sheet-dialog tbody > tr > td { vertical-align:middle; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-cell-editor {
  display:grid; grid-template-rows:auto minmax(0,1fr); min-width:0;
  max-height:min(360px,45vh); padding:6px; border:1px solid #91bdb7;
}
.tube-designer-punch-sheet-dialog .tube-designer-punch-cell-editor > header { margin-bottom:6px; align-items:flex-start; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-cell-editor > .tube-designer-punch-parameter-content {
  min-width:0; min-height:0; overflow:auto; overscroll-behavior:contain;
}
.tube-designer-punch-sheet-dialog .tube-designer-punch-cell-editor .punch-pose-fields,
.tube-designer-punch-sheet-dialog .tube-designer-punch-cell-editor .punch-array-fields,
.tube-designer-punch-sheet-dialog .tube-designer-punch-cell-editor .tube-designer-punch-layout-fields {
  grid-template-columns:repeat(2,minmax(0,1fr)); gap:6px;
}
.tube-designer-punch-cell-editor .punch-pose-field { min-width:0; }
.tube-designer-punch-cell-editor .punch-array-editor { gap:8px; }
.tube-designer-punch-cell-editor .punch-array-group > header { flex-wrap:wrap; padding:5px; }
.tube-designer-punch-cell-editor .punch-array-fields { padding:5px; }
.tube-designer-punch-cell-editor .punch-array-add { gap:4px; }
.tube-designer-punch-cell-editor .punch-array-add button { padding:3px 6px; }
.tube-designer-punch-cell-editor .punch-array-origin .punch-array-fields { grid-template-columns:1fr; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-sheet-scene {
  grid-column:1; grid-row:7; padding:0; display:grid; grid-template-rows:auto minmax(0,1fr) auto; overflow:auto;
}
.tube-designer-punch-sheet-dialog .tube-designer-punch-sheet-scene > header { min-height:28px; padding:2px 6px; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-sheet-scene > header button { min-height:24px; height:24px; padding:2px 7px; font-size:11px; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-sheet-scene .tube-designer-punch-preview-host { min-height:0; border:0; border-radius:0; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-sheet-scene > footer { padding:1px 6px; min-height:18px; overflow:hidden; }
.tube-designer-punch-sheet-dialog > .tube-designer-punch-footer { display:flex; justify-content:flex-end; gap:6px; min-height:34px; padding:3px 6px; overflow:hidden; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-sidebar-actions { border:0; padding:0; background:transparent; flex:0 0 auto; }
.tube-designer-punch-sheet-dialog .tube-designer-punch-sheet-errors { margin-right:auto; min-width:0; max-height:80px; overflow:auto; }
.tube-designer-punch-region-splitter { min-height:6px; height:6px; background:#d4e0e3; cursor:row-resize; touch-action:none; position:relative; }
.tube-designer-punch-region-splitter::after { content:""; position:absolute; width:38px; height:2px; background:#91a9af; top:2px; left:calc(50% - 19px); }
.tube-designer-punch-region-splitter:hover,.tube-designer-punch-region-splitter:focus-visible { background:#a8ccc6; outline:0; }
.tube-designer-punch-regions-resizing,.tube-designer-punch-regions-resizing * { cursor:row-resize !important; user-select:none !important; }
@media(max-width:1000px) {
  .tube-designer-punch-main-region .tube-designer-nesting-punch-setup-grid { grid-template-columns:repeat(3,minmax(150px,1fr)); }
  .tube-designer-punch-inline-editor .punch-pose-fields,.tube-designer-punch-inline-editor .punch-array-fields { grid-template-columns:repeat(2,minmax(0,1fr)); }
  .tube-designer-punch-sheet-dialog .tube-designer-punch-cell-editor .punch-pose-fields,
  .tube-designer-punch-sheet-dialog .tube-designer-punch-cell-editor .punch-array-fields,
  .tube-designer-punch-sheet-dialog .tube-designer-punch-cell-editor .tube-designer-punch-layout-fields { grid-template-columns:minmax(0,1fr); }
}
@media(max-height:680px) {
  .tube-designer-punch-sheet-dialog > .tube-designer-punch-sheet-workspace { grid-template-rows:var(--punch-main-height,96px) 6px var(--punch-ends-height,104px) 6px var(--punch-holes-height,128px) 6px minmax(180px,1fr); overflow:auto; }
}
`;
