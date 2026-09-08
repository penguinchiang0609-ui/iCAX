const tileWindowShells = [
  '.tube-designer-modal-backdrop > [role="dialog"]',
  '.tube-designer-punch-parameter-backdrop > [role="dialog"]',
  '.tube-nesting-settings-backdrop > [role="dialog"]',
  '.tube-sketch-save-choice-backdrop > [role="dialog"]',
  '.tube-section-sketch-dialog',
  '.tube-designer-confirm-dialog',
  '.tube-profile-library-preview-wait',
  '.tube-component-csg-dialog',
  '.tube-designer-export-progress-card',
  '.tube-designer-nesting-context-menu',
  '.new-project-dialog',
].join(',\n');

const tileWindowDescendants = [
  '.tube-designer-modal-backdrop > [role="dialog"] *',
  '.tube-designer-punch-parameter-backdrop > [role="dialog"] *',
  '.tube-nesting-settings-backdrop > [role="dialog"] *',
  '.tube-sketch-save-choice-backdrop > [role="dialog"] *',
  '.tube-section-sketch-dialog *',
  '.tube-designer-confirm-dialog *',
  '.tube-profile-library-preview-wait *',
  '.tube-component-csg-dialog *',
  '.tube-designer-export-progress-card *',
  '.tube-designer-nesting-context-menu *',
  '.new-project-dialog *',
  '.tube-component-csg-page-tree',
  '.tube-component-csg-page-tree *',
  '.tube-component-csg-page-inspector',
  '.tube-component-csg-page-inspector *',
  '.tube-component-csg-page-overlay',
  '.tube-component-csg-page-overlay *',
  '.tube-component-library-hud',
  '.tube-component-library-hud *',
].join(',\n');

export const tileWindowCss = String.raw`
/* Microsoft tile-style windows: square shells and square controls throughout. */
${tileWindowShells} {
  border-radius: 0 !important;
  box-shadow: 0 16px 42px rgba(3, 17, 23, .34) !important;
}
${tileWindowDescendants} {
  border-radius: 0 !important;
}

.tube-designer-dialog-header,
.tube-nesting-settings-header,
.td-draw-title,
.tube-section-sketch-title,
.tube-sketch-save-choice > header,
.new-project-dialog > header,
[data-tube-designer-window-drag] {
  cursor: grab;
  user-select: none;
  touch-action: none;
}
.tube-designer-dialog-header[data-td-window-dragging="true"],
.tube-nesting-settings-header[data-td-window-dragging="true"],
.td-draw-title[data-td-window-dragging="true"],
.tube-section-sketch-title[data-td-window-dragging="true"],
.tube-sketch-save-choice > header[data-td-window-dragging="true"],
.new-project-dialog > header[data-td-window-dragging="true"],
[data-tube-designer-window-drag][data-td-window-dragging="true"] {
  cursor: grabbing;
}
.tube-designer-dialog-header button,
.tube-nesting-settings-header button,
.td-draw-title button,
.tube-section-sketch-title button,
.tube-sketch-save-choice > header button,
.new-project-dialog > header button,
[data-tube-designer-window-drag] button,
[data-tube-designer-window-drag] input,
[data-tube-designer-window-drag] select,
[data-tube-designer-window-drag] textarea,
[data-tube-designer-window-drag] a {
  touch-action: auto;
}
`;
