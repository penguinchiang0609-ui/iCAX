export function ensurePlanarEditorStyles(document) {
  if (!document || document.getElementById("icax-planar-editor-styles")) return;
  const style = document.createElement("style");style.id = "icax-planar-editor-styles";
  style.textContent = `
    .icax-planar-dimension-overlay{position:absolute;inset:0;z-index:35;pointer-events:none}
    .icax-planar-dimension-overlay[hidden],.icax-planar-dimension-overlay [hidden]{display:none!important}
    .icax-planar-dimension-label.icax-three-specification-label{position:fixed;z-index:36;white-space:nowrap}
    .icax-planar-dimension-label input[aria-invalid=true]{border-color:#ffbaa5;box-shadow:0 0 0 1px #ffe3d9}
    .icax-planar-dimension-leaders{position:fixed;inset:0;width:100vw;height:100vh;overflow:visible;pointer-events:none}
    .icax-planar-dimension-leaders path{fill:none;stroke:#27c27a;stroke-width:1}
    .icax-planar-dimension-error{position:fixed;max-width:260px;padding:3px 6px;transform:translateX(-50%);border:1px solid #c7786d;background:#fff3ed;color:#934335;font-size:11px;line-height:1.45;pointer-events:none}
  `;
  document.head.appendChild(style);
}
