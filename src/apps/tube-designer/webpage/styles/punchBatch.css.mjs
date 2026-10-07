export const punchBatchCss = `
.tube-designer-punch-batch-dialog > .punch-batch-workspace {
  grid-template-rows:var(--punch-main-height,44px) 6px var(--punch-definitions-height,94px) 6px var(--punch-parts-height,clamp(150px,22vh,230px)) 6px var(--punch-holes-height,clamp(138px,18vh,220px)) 6px minmax(180px,1fr);
}
.tube-designer-punch-batch-dialog [data-punch-region="main"] { grid-row:1; --punch-region-min-height:36px; }
.tube-designer-punch-batch-dialog [data-punch-region="definitions"] { grid-row:3; --punch-region-min-height:64px; }
.tube-designer-punch-batch-dialog [data-punch-region="parts"] { grid-row:5; --punch-region-min-height:128px; }
.tube-designer-punch-batch-dialog [data-punch-region="holes"] { grid-row:7; --punch-region-min-height:128px; display:grid; grid-template-rows:28px minmax(0,1fr); }
.tube-designer-punch-batch-dialog [data-punch-region="scene"] { grid-row:9; --punch-region-min-height:180px; }
.tube-designer-punch-batch-dialog .tube-designer-dialog-header > div { display:flex; align-items:center; gap:12px; }
.tube-designer-punch-batch-dialog .punch-batch-header-count { color:#627d87; font-size:11px; }
.tube-designer-punch-batch-dialog .punch-batch-history { display:flex; align-items:center; gap:4px; }
.tube-designer-punch-batch-dialog .punch-batch-history > button { height:24px; min-height:24px; padding:2px 7px; font-size:11px; }
.tube-designer-punch-batch-dialog [data-punch-region="main"] { display:grid; grid-template-columns:minmax(0,1fr) auto; }
.punch-batch-main-actions { display:flex; align-items:center; align-self:start; gap:8px; padding:3px 7px; }
.tube-designer-punch-batch-dialog .punch-batch-main-actions .tube-designer-punch-sidebar-actions { display:flex; gap:4px; }
.tube-designer-punch-batch-dialog .punch-batch-main-actions button { min-height:24px; height:24px; padding:2px 9px; font-size:11px; }
.tube-designer-punch-batch-dialog .tube-designer-punch-sheet-scene { grid-template-rows:28px minmax(0,1fr); }
.tube-designer-punch-batch-dialog .tube-designer-nesting-punch-setup { padding:3px 7px; }
.tube-designer-punch-batch-dialog [data-punch-region="main"] .punch-batch-profile-toolbar {
  display:flex; align-items:center; flex-wrap:wrap; gap:3px 8px;
  width:max-content; min-width:100%; max-width:none; box-sizing:border-box;
}
.tube-designer-punch-batch-dialog [data-punch-region="main"] .tube-designer-nesting-punch-setup-grid {
  display:flex; flex:0 0 auto; align-items:center; justify-content:flex-start; gap:6px;
}
.tube-designer-punch-batch-dialog [data-punch-region="main"] .tube-designer-nesting-punch-setup-grid > .tube-designer-field {
  display:flex; flex:0 0 auto; align-items:center; gap:5px;
}
.tube-designer-punch-batch-dialog [data-punch-region="main"] .tube-designer-nesting-punch-setup-grid > .tube-designer-field > span { flex:0 0 auto; }
.tube-designer-punch-batch-dialog [data-punch-region="main"] .tube-designer-nesting-punch-setup-grid select {
  width:132px; min-width:132px; max-width:132px;
}
.tube-designer-punch-batch-dialog [data-punch-region="main"] .tube-designer-punch-main-profile-parameters {
  display:flex; flex:0 0 auto; align-items:center; flex-wrap:wrap; gap:3px 8px;
  width:max-content; min-width:0; max-width:none;
}
.tube-designer-punch-batch-dialog [data-punch-region="main"] .tube-designer-punch-main-profile-parameters > .tube-profile-library-parameter-group.is-inline { flex:0 0 auto; }
.tube-designer-punch-batch-dialog [data-punch-region="main"] .tube-designer-punch-main-profile-parameters > [data-punch-profile-advanced-open] { flex:0 0 auto; }
.tube-designer-punch-batch-dialog [data-punch-region="main"] .punch-main-diagram-button { flex:0 0 auto; margin-left:auto; }
.tube-designer-punch-batch-dialog [data-punch-region="main"] .punch-batch-profile-toolbar > :is(.tube-drawing-warning,.tube-designer-punch-error) { flex-basis:100%; }
.tube-designer-punch-batch-dialog .tube-designer-punch-main-profile-parameters { --punch-parameter-track-width:150px; }
.tube-designer-punch-batch-dialog .tube-profile-library-field-grid .tube-designer-field > small { font-size:11px; color:#58727b; }
.tube-designer-punch-batch-dialog .tube-profile-library-field-grid {
  display:flex; flex-wrap:wrap; align-items:center; justify-content:flex-start;
  gap:3px 8px; padding:2px 0;
}
.tube-designer-punch-batch-dialog .tube-profile-library-field-grid .tube-designer-field,
.tube-designer-punch-batch-dialog .tube-designer-punch-inline-editor .tube-designer-field {
  display:flex; flex:0 0 auto; align-items:center; justify-content:flex-start;
  width:auto; max-width:100%; margin:0; padding:1px 3px; gap:6px;
}
.tube-designer-punch-batch-dialog .tube-profile-library-field-grid .tube-designer-field > :is(span,small),
.tube-designer-punch-batch-dialog .tube-designer-punch-inline-editor .tube-designer-field > :is(span,small):not(.tube-designer-punch-sheet-number) {
  flex:0 0 auto; white-space:nowrap;
}
.tube-designer-punch-batch-dialog .tube-profile-library-field-grid > :is(.is-line-full,.wide),
.tube-designer-punch-batch-dialog .tube-profile-library-field-grid > .tube-parameter-advanced-item > :is(.is-line-full,.wide) {
  flex-basis:100%; width:100%;
}
.tube-designer-punch-batch-dialog .tube-profile-library-field-grid .tube-designer-field.is-number > input,
.tube-designer-punch-batch-dialog .tube-designer-punch-inline-editor .tube-designer-field > .tube-designer-punch-sheet-number {
  flex:0 0 auto; width:var(--tube-designer-control-preferred-width,64px);
  min-width:var(--tube-designer-control-min-width,40px); max-width:var(--tube-designer-control-max-width,84px);
}
.tube-designer-punch-batch-dialog .tube-designer-punch-inline-editor .tube-designer-field > .tube-designer-punch-sheet-number > input {
  width:100%; min-width:0; max-width:100%;
}
.tube-designer-punch-batch-dialog .tube-profile-library-field-grid .tube-designer-field.is-choice > select,
.tube-designer-punch-batch-dialog .tube-designer-punch-inline-editor .tube-designer-field.is-choice > select {
  flex:0 0 auto; width:var(--tube-designer-control-preferred-width,160px); max-width:var(--tube-designer-control-max-width,100%);
}
.tube-designer-punch-batch-dialog .tube-designer-punch-sheet-parameters > .tube-profile-library-parameter-group { max-width:100%; }
.punch-batch-definitions { display:grid; grid-template-rows:minmax(0,1fr); }
.punch-batch-definition-scroll,.punch-batch-part-scroll { overflow:auto; min-width:0; min-height:0; overscroll-behavior:contain; }
.punch-batch-definition-strip { display:flex; align-items:center; flex-wrap:wrap; gap:4px 8px; padding:3px 7px; border-bottom:1px solid #cbdadd; }
.punch-batch-definition-strip > span { color:#57757f; flex:0 0 auto; }
.punch-batch-definition-tabs { display:flex; flex-wrap:wrap; gap:3px; }
.punch-batch-definition-strip > [data-punch-batch-definition-diagram] { flex:0 0 auto; margin-left:auto; }
.tube-designer-punch-batch-dialog .punch-batch-definition-tabs > button { border:1px solid transparent; background:transparent; padding:2px 7px; }
.tube-designer-punch-batch-dialog .punch-batch-definition-tabs > button[aria-pressed="true"] { background:#e9f1f1; border-bottom:2px solid #168e84; }
.tube-designer-punch-batch-dialog .punch-batch-definition-editor > .tube-designer-punch-inline-editor { border:0; padding:3px 7px; background:transparent; }
.tube-designer-punch-batch-dialog .punch-batch-definition-editor .punch-inline-source { margin:0 0 2px; gap:3px 12px; }
.tube-designer-punch-batch-dialog .punch-batch-definition-editor .tube-designer-punch-parameter-content { display:flex; align-items:center; flex-wrap:wrap; column-gap:12px; }
.tube-designer-punch-batch-dialog .punch-batch-definition-editor .punch-inline-operation { flex:0 0 auto; max-width:100%; }
.tube-designer-punch-batch-dialog .punch-batch-definition-editor .punch-inline-source { flex:0 0 auto; }
.tube-designer-punch-batch-dialog .punch-batch-definition-editor .punch-inline-source label { grid-template-columns:auto 140px; }
.tube-designer-punch-batch-dialog .punch-batch-definition-editor .punch-inline-source select { width:140px; }
.tube-designer-punch-batch-dialog .punch-batch-definition-editor .tube-designer-punch-sheet-parameters { flex:1; min-width:250px; --punch-parameter-track-width:260px; }
.tube-designer-punch-batch-dialog .punch-batch-definition-editor .tube-designer-punch-sheet-parameters > .tube-profile-library-parameter-group.is-inline { display:inline-block; vertical-align:top; margin-right:8px; }
.punch-batch-parts { display:grid; grid-template-rows:28px minmax(0,1fr); }
.punch-batch-localbar { display:flex; align-items:center; gap:8px; min-width:0; min-height:28px; padding:2px 7px; border-bottom:1px solid #becfd5; background:#f5f8f9; }
.punch-batch-localbar strong { white-space:nowrap; font-size:11px; font-weight:600; max-width:45%; overflow:hidden; text-overflow:ellipsis; }
.punch-batch-localbar > nav { display:flex; align-items:center; gap:4px; margin-left:auto; }
.punch-batch-muted { color:#647f88; font-size:10px; white-space:nowrap; }
.tube-designer-punch-batch-dialog .punch-batch-localbar button { height:24px; min-height:24px; padding:2px 7px; }
.punch-batch-part-table { width:100%; min-width:985px; table-layout:fixed; border-collapse:separate; border-spacing:0; font-size:11px; }
.punch-batch-part-table th,.punch-batch-part-table td { height:29px; padding:2px 4px; text-align:left; vertical-align:middle; border-right:1px solid #cbd9dd; border-bottom:1px solid #cbd9dd; font-weight:normal; }
.punch-batch-part-table thead th { position:sticky; top:0; z-index:2; height:26px; color:#496a74; background:#e9f0f2; white-space:nowrap; font-weight:600; }
.punch-batch-part-table tbody > tr[data-punch-row-tone] { --punch-row-background:#f7fafb; }
.punch-batch-part-table tbody > tr[data-punch-row-tone="odd"] { --punch-row-background:#eaf2f4; }
.punch-batch-part-table tbody > tr.is-current { --punch-row-background:#fff3bf; }
.tube-designer-punch-batch-dialog .punch-batch-part-table tbody > tr > :is(td,th) { background:var(--punch-row-background); }
.punch-batch-part-table tbody > tr.is-current > td:first-child { box-shadow:inset 3px 0 #bd9222; }
.tube-designer-modal-backdrop .tube-designer-punch-dialog.tube-designer-punch-batch-dialog .punch-batch-part-table tbody input[data-punch-batch-part-id][data-punch-batch-field]:not([type=checkbox]),
.tube-designer-modal-backdrop .tube-designer-punch-dialog.tube-designer-punch-batch-dialog .punch-batch-part-table tbody select[data-punch-batch-part-id][data-punch-batch-field] { height:24px; min-height:24px; width:100%; min-width:0; padding:1px 3px; border:1px solid transparent; background:transparent; font-size:11px; }
.tube-designer-punch-batch-dialog .punch-batch-part-table tbody input[type=number] { text-align:right; font-variant-numeric:tabular-nums; }
.tube-designer-modal-backdrop .tube-designer-punch-dialog.tube-designer-punch-batch-dialog .punch-batch-part-table tbody input[data-punch-batch-part-id][data-punch-batch-field]:not([type=checkbox]):focus,
.tube-designer-modal-backdrop .tube-designer-punch-dialog.tube-designer-punch-batch-dialog .punch-batch-part-table tbody select[data-punch-batch-part-id][data-punch-batch-field]:focus { border-color:#168e84; background:#fff; }
.tube-designer-modal-backdrop .tube-designer-punch-dialog.tube-designer-punch-batch-dialog .punch-batch-part-table button { height:24px; min-height:24px; width:100%; padding:1px 3px; text-align:left; border:1px solid transparent; background:transparent; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.tube-designer-punch-batch-dialog .punch-batch-part-table button:hover { border-color:#9abdb8; }
.punch-batch-part-table .punch-batch-check { text-align:center; }
.punch-batch-part-table input[type=checkbox] { width:14px; height:14px; margin:0; accent-color:#168e84; }
.punch-batch-receipt { color:#168e84; font-size:10px; }
.tube-designer-punch-batch-dialog .punch-batch-end-row > td { padding:0; }
.punch-batch-end-heading { display:flex; align-items:center; justify-content:space-between; padding:2px 6px; border-top:1px solid #aabdbf; color:#496b73; }
.tube-designer-punch-batch-dialog .punch-batch-end-heading > button { width:auto; border:1px solid #becfd5; background:#fff; }
.tube-designer-punch-batch-dialog .punch-batch-end-row .tube-designer-punch-ends { display:block; height:auto; background:var(--punch-row-background); }
.tube-designer-punch-batch-dialog .punch-batch-end-row .punch-end-table-scroll { overflow:visible; }
.tube-designer-punch-batch-dialog .punch-batch-end-row .tube-designer-punch-ends table { min-width:760px; }
.tube-designer-punch-batch-dialog .punch-batch-end-row .tube-designer-punch-ends tr { --punch-row-background:inherit; }
.tube-designer-punch-batch-dialog .punch-batch-end-row .tube-designer-punch-ends :is(td,th),
.tube-designer-punch-batch-dialog .punch-batch-end-row .tube-designer-punch-inline-editor { background:transparent; }
.tube-designer-punch-batch-dialog .punch-batch-end-row .tube-designer-punch-inline-editor { position:sticky; left:0; width:var(--punch-table-visible-width,100%); }
.tube-designer-punch-batch-dialog [data-punch-region="holes"] > .tube-designer-punch-records {
  grid-row:2; display:grid; grid-template-rows:minmax(0,1fr);
  height:auto; min-height:0; overflow:hidden;
}
.tube-designer-punch-batch-dialog [data-punch-region="holes"] > .tube-designer-punch-records > .tube-designer-punch-sheet {
  height:auto; min-height:0; grid-template-rows:minmax(0,1fr); overflow:hidden;
}
.tube-designer-punch-batch-dialog [data-punch-region="holes"] .tube-designer-punch-sheet-scroll {
  height:auto; min-height:0; max-height:none; overflow:auto;
}
.tube-designer-punch-batch-dialog .punch-expanded-columns th[data-punch-frozen-column="shape"] { min-width:150px; }
.tube-designer-punch-batch-dialog .punch-expanded-columns td[data-punch-frozen-column="shape"] select { width:100%; border-color:transparent; background:transparent; }
.tube-designer-punch-batch-dialog .punch-expanded-columns td[data-punch-frozen-column="shape"] select:focus { background:#fff; border-color:#168e84; }
.tube-designer-modal-backdrop .tube-designer-punch-dialog.tube-designer-punch-batch-dialog .punch-expanded-columns tbody :is([data-punch-sheet-field][data-tube-designer-punch-field],[data-punch-sheet-array-field][data-tube-designer-punch-index],select[data-punch-batch-definition-feature-id]) { border-color:transparent; background:transparent; }
.tube-designer-modal-backdrop .tube-designer-punch-dialog.tube-designer-punch-batch-dialog .punch-expanded-columns tbody :is([data-punch-sheet-field][data-tube-designer-punch-field],[data-punch-sheet-array-field][data-tube-designer-punch-index],select[data-punch-batch-definition-feature-id]):focus { border-color:#168e84; background:#fff; }
.tube-designer-punch-batch-dialog > .punch-batch-footer { align-items:center; padding:3px 7px; gap:8px; }
.tube-designer-punch-batch-dialog .punch-batch-footer .tube-designer-punch-sheet-errors:empty { display:none; }
.tube-designer-punch-batch-dialog .punch-batch-footer .tube-designer-punch-sheet-errors { margin-right:0; max-height:52px; }
@media(max-height:850px) {
  .tube-designer-punch-batch-dialog > .punch-batch-workspace { grid-template-rows:var(--punch-main-height,44px) 6px var(--punch-definitions-height,84px) 6px var(--punch-parts-height,164px) 6px var(--punch-holes-height,128px) 6px minmax(180px,1fr); }
}
@media(max-height:760px) {
  .tube-designer-punch-batch-dialog > .punch-batch-workspace { grid-template-rows:var(--punch-main-height,44px) 6px var(--punch-definitions-height,64px) 6px var(--punch-parts-height,128px) 6px var(--punch-holes-height,128px) 6px minmax(180px,1fr); }
}
@media(max-height:650px) {
  .tube-designer-punch-batch-dialog > .punch-batch-workspace { overflow:auto; }
}
.punch-batch-profile-advanced-backdrop { z-index:980; padding:24px; }
.punch-batch-profile-advanced-dialog {
  display:grid; grid-template-rows:auto minmax(0,1fr) auto;
  width:min(1020px,calc(100vw - 48px)); max-height:calc(100vh - 48px);
  overflow:hidden; --td-dialog-font-size:11px; --td-dialog-control-height:24px;
}
.punch-batch-profile-advanced-body { min-height:0; min-width:0; padding:10px 12px; overflow:auto; overscroll-behavior:contain; }
.punch-batch-profile-advanced-body > section { margin:0; padding:0; border:0; }
.punch-batch-profile-advanced-dialog .tube-designer-field > :is(input:not([type=checkbox]),select) { height:24px; min-height:24px; padding:2px 4px; }
.punch-batch-profile-advanced-dialog > .tube-designer-dialog-footer { display:flex; justify-content:flex-end; padding:6px 12px; }
.punch-batch-profile-advanced-dialog > .tube-designer-dialog-footer > button { min-width:64px; }
`;
