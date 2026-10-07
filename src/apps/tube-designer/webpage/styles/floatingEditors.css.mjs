export const floatingEditorsCss = String.raw`
.tube-floating-editor-layer[data-floating-editor-layer],
.tube-designer-punch-parameter-backdrop {
  position: fixed; inset: 0; display: grid; place-items: center;
  padding: 24px; box-sizing: border-box; pointer-events: none;
  background: transparent; backdrop-filter: none;
}
.tube-floating-editor-layer[data-floating-editor-layer] > [data-floating-editor-window] {
  position: relative; pointer-events: auto; overflow: visible;
  display: flex; flex-direction: column; min-width: 280px;
}
[data-floating-editor-window] > :is(.tube-designer-preset-dialog-body, .tube-designer-profile-library-list, .tube-nesting-settings-body) {
  min-height: 0; flex: 1 1 auto; overflow: auto; overscroll-behavior: contain;
}
.tube-floating-editor-resize { position: absolute; z-index: 20; pointer-events: auto; touch-action: none; }
.tube-floating-editor-resize.is-e { right: -4px; top: 10px; bottom: 10px; width: 8px; cursor: ew-resize; }
.tube-floating-editor-resize.is-s { bottom: -4px; left: 10px; right: 10px; height: 8px; cursor: ns-resize; }
.tube-floating-editor-resize.is-se { right: -5px; bottom: -5px; width: 14px; height: 14px; cursor: nwse-resize; }
`;
