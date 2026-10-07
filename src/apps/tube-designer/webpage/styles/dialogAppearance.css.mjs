// Dialog appearance is shared; each page still owns its layout and modality.
const dialogs = ':is(.tube-designer-modal-backdrop [role="dialog"], .tube-nesting-settings-backdrop > [role="dialog"], .tube-designer-punch-parameter-dialog, .tube-sketch-save-choice, .tube-section-sketch-dialog, .tube-component-csg-dialog, .tube-designer-confirm-dialog, .new-project-dialog, .tube-library-diagram-dock, .tube-floating-parameter-diagram)';
const backdrops = ':is(.tube-designer-modal-backdrop:not(.tube-designer-part-inspection-backdrop), .tube-nesting-settings-backdrop, .tube-sketch-save-choice-backdrop, .modal-backdrop:has(> .new-project-dialog))';

export const dialogAppearanceCss = String.raw`
:root {
  --td-dialog-ink: #263e47;
  --td-dialog-muted: #6d818a;
  --td-dialog-border: #b3c8d0;
  --td-dialog-surface: #f5f8f9;
  --td-dialog-accent: #168e84;
  --td-dialog-shadow: 0 16px 42px rgba(3, 17, 23, .34);
}
${backdrops} { background: rgba(12, 25, 31, .48); backdrop-filter: blur(2px); }
${dialogs}::backdrop { background: rgba(12, 25, 31, .48); backdrop-filter: blur(2px); }
${dialogs} {
  box-sizing: border-box;
  color: var(--td-dialog-ink);
  background: var(--td-dialog-surface);
  border: 1px solid var(--td-dialog-border);
  border-radius: 0;
  box-shadow: var(--td-dialog-shadow);
  font: var(--td-dialog-font-size, 12px)/1.4 "Segoe UI", "Microsoft YaHei", sans-serif;
}
${dialogs} > header {
  box-sizing: border-box;
  position: relative;
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex: 0 0 auto;
  gap: 12px;
  min-height: var(--td-dialog-header-height, 48px);
  padding: var(--td-dialog-header-padding, 7px 16px);
  color: var(--td-dialog-ink);
  background: #fff;
  border-bottom: 1px solid var(--td-dialog-border);
  font-size: 14px;
}
${dialogs} > header > div { min-width: 0; }
${dialogs} > header :is(strong, h1, h2, h3) { margin: 0; font-size: var(--td-dialog-title-size, 14px); font-weight: 600; line-height: 20px; }
${dialogs} > header :is(small, span:not(.command-icon)) { color: var(--td-dialog-muted); font-size: 12px; }
${dialogs} > header > button {
  position: static;
  display: grid;
  place-items: center;
  flex: 0 0 var(--td-dialog-close-size, 32px);
  width: var(--td-dialog-close-size, 32px);
  height: var(--td-dialog-close-size, 32px);
  min-height: var(--td-dialog-close-size, 32px);
  margin: 0 0 0 auto;
  padding: 0;
  border: 1px solid var(--td-dialog-border);
  background: #fff;
  color: #365760;
  font: 22px/1 "Segoe UI", sans-serif;
  cursor: pointer;
}
${dialogs} > header > button:hover:not(:disabled) { background: #e5f2f1; border-color: #60b6af; }
${dialogs} > footer {
  box-sizing: border-box;
  gap: 9px;
  padding: 12px 16px;
  border-top: 1px solid var(--td-dialog-border);
  background: #fff;
}
${dialogs} :is(.tube-designer-secondary, .tube-designer-primary, .tube-designer-danger, .tube-nesting-settings-save, .tube-designer-confirm-dialog button, .tube-nesting-file-navigation-button, [data-nesting-file-action="open"], [data-nesting-file-action="fit"], [data-nesting-file-action="cancel"], footer > button, .tube-designer-punch-sidebar-actions > button) {
  box-sizing: border-box;
  min-height: var(--td-dialog-control-height, 32px);
  padding: var(--td-dialog-button-padding, 6px 12px);
  border: 1px solid var(--td-dialog-border);
  background: #fff;
  color: #365760;
  font: inherit;
  font-weight: 400;
  cursor: pointer;
}
${dialogs} :is(.tube-designer-primary, .primary-button, .tube-nesting-settings-save, button.confirm, [data-nesting-file-action="open"], .tube-designer-punch-sidebar-actions > [data-cam-action$="-apply"], .tube-designer-punch-footer > [data-cam-action$="-apply"]) {
  border-color: var(--td-dialog-accent);
  background: var(--td-dialog-accent);
  color: #fff;
  font-weight: 600;
}
${dialogs} .tube-designer-danger { color: #b94a48; border-color: #cf7974; }
${dialogs} :is(button, input, select, textarea):focus-visible { outline: 2px solid var(--td-dialog-accent); outline-offset: 1px; }
${dialogs} :is(button, input, select, textarea):disabled { opacity: .45; cursor: default; }
${dialogs} :is(input:not([type="checkbox"]):not([type="radio"]):not([type="range"]):not([type="hidden"]), select, textarea) {
  box-sizing: border-box;
  min-height: var(--td-dialog-control-height, 32px);
  border: 1px solid var(--td-dialog-border);
  background: #fff;
  color: var(--td-dialog-ink);
  font-family: inherit;
  font-size: var(--td-dialog-font-size, 12px);
}
${dialogs} :is(input[type="checkbox"], input[type="radio"]) {
  width: 16px;
  height: 16px;
  min-width: 16px;
  min-height: 16px;
  max-width: 16px;
  margin: 0;
  padding: 0;
  accent-color: var(--td-dialog-accent);
}
/* Close buttons also carry page actions; keep caption sizing after action rules. */
${dialogs} > header > button:is(:enabled, :disabled) { padding: 0; font: 22px/1 "Segoe UI", sans-serif; }
.td-draw-workbench { grid-template-rows: auto 88px minmax(0,1fr) 26px; }
:is(.td-draw-sidebar, .td-draw-parameters, .tube-designer-punch-sidebar, .tube-designer-punch-rightbar) { background: var(--td-dialog-surface); }
`;
