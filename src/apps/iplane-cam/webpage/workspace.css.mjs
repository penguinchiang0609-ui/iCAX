const css = `
.cam-workbench.iplane-workspace{grid-template-rows:auto minmax(0,1fr);grid-template-areas:"notice notice notice notice notice" "left left-resize viewer right-resize right"}
.iplane-workspace[data-iplane-area=nesting]{grid-template-rows:auto minmax(0,1fr) 6px var(--cam-bottom-height,180px);grid-template-areas:"notice notice notice notice notice" "left left-resize viewer right-resize right" "left left-resize bottom-resize right-resize right" "left left-resize bottom right-resize right"}
.iplane-workspace .cam-viewer{grid-template-rows:32px minmax(0,1fr) 30px}
.iplane-workspace .cam-viewer-head>div>span:last-child{display:none}
.iplane-workspace .cam-viewport{position:relative}
.iplane-workspace[data-iplane-objects-open=false]{grid-template-columns:0 0 minmax(420px,1fr) 6px var(--cam-right-width,340px)}
.iplane-workspace[data-iplane-objects-open=false]>.cam-context-pane{width:var(--cam-left-width,320px);box-sizing:border-box;visibility:hidden;pointer-events:none}
.iplane-workspace[data-iplane-objects-open=false]>.cam-splitter-left{visibility:hidden;pointer-events:none}
.iplane-workspace [data-iplane-tool-palette-host][hidden]{display:none!important}
.iplane-workspace [data-iplane-layer-host]{position:absolute;inset:0;pointer-events:none;z-index:30}
.iplane-workspace .icax-three-viewport[data-iplane-drawing-tool] .icax-three-selection-rectangle{display:none!important}
.iplane-drawing-step{margin:0;color:#304f60;font-size:13px;line-height:1.6}
.iplane-polyline-kinds{display:flex;flex-wrap:wrap;gap:5px}.iplane-polyline-kinds>button{border:1px solid #c5ced4;border-radius:3px;background:#f8fafb;padding:6px 8px;color:#405864;font-size:12px;cursor:pointer}.iplane-polyline-kinds>button[aria-pressed="true"]{border-color:#5798b3;background:#e9f4f8;color:#225d77}.iplane-polyline-kinds>button:disabled{opacity:.55;cursor:default}
.iplane-context-menu.ribbon-command-menu{position:fixed;z-index:1000;min-width:0;width:176px;box-sizing:border-box;overflow:visible}
.iplane-context-menu .icax-planar-context-submenu{width:184px;box-sizing:border-box;min-width:0}
.iplane-context-menu .ribbon-command-menu-item{grid-template-columns:16px minmax(0,1fr) 12px;font-size:12px}
.iplane-context-menu .icax-planar-context-menu-label{min-width:0;white-space:normal;overflow-wrap:anywhere}
.iplane-context-menu .icax-planar-context-menu-check,.iplane-context-menu .icax-planar-context-menu-arrow{text-align:center}
.iplane-context-menu .icax-planar-context-menu-hint{grid-column:2/-1;white-space:normal;font-size:11px;line-height:1.5;color:#75828a}
.iplane-context-menu .icax-planar-context-submenu-hint{padding:6px 8px;border-bottom:1px solid #d9e0e4;color:#6a747a;font-size:11px;line-height:1.55;white-space:normal;overflow-wrap:anywhere}
.iplane-dimension-overlay{position:absolute;inset:0;z-index:35;pointer-events:none}
.iplane-dimension-overlay[hidden],.iplane-dimension-overlay [hidden]{display:none!important}
.iplane-dimension-label.icax-three-specification-label{position:fixed;z-index:36;white-space:nowrap}
.iplane-dimension-label .iplane-dimension-trigger{pointer-events:auto}
.iplane-dimension-label input[aria-invalid=true]{border-color:#ffbaa5;box-shadow:0 0 0 1px #ffe3d9}
.iplane-dimension-leaders{position:fixed;inset:0;width:100vw;height:100vh;overflow:visible;pointer-events:none}
.iplane-dimension-leaders path{fill:none;stroke:#27c27a;stroke-width:1}
.iplane-dimension-error{position:fixed;max-width:260px;padding:3px 6px;transform:translateX(-50%);border:1px solid #c7786d;background:#fff3ed;color:#934335;font-size:11px;line-height:1.45;pointer-events:none}
.iplane-workspace .cam-context-pane{grid-template-rows:minmax(0,1fr);align-content:stretch;overflow:hidden}
.iplane-panel{display:grid;gap:10px;padding:12px;border:1px solid #d3dade;border-radius:4px;background:#fff;min-width:0}
.iplane-objects-panel{grid-template-rows:auto minmax(0,1fr) auto;min-height:0}
.iplane-panel h3{margin:0;font-size:13px;font-weight:600;color:#30444f}
.iplane-panel-heading{display:flex;align-items:center;justify-content:space-between;gap:8px;font-size:13px}
.iplane-panel-heading>span{font-size:11px;color:#72818a;white-space:nowrap}
.iplane-object-list{display:grid;align-content:start;gap:3px;min-height:0;overflow:auto;padding:2px}
.iplane-object-row{display:flex;align-items:center;justify-content:space-between;gap:6px;min-height:34px;padding:7px 8px 7px calc(9px + var(--iplane-depth,0)*12px);text-align:left;border:1px solid transparent;border-radius:3px;background:#f6f8f9;color:#354953;cursor:pointer;min-width:0}
.iplane-object-row>span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.iplane-object-row>small{font-size:10px;color:#7b8990}
.iplane-layer-dot{display:inline-block;flex-shrink:0;width:9px;height:9px;border-radius:2px;background:var(--object-layer-color);box-shadow:inset 0 0 0 1px #0002}.iplane-object-row>.iplane-layer-dot+span{flex:1}
.iplane-object-row:hover{border-color:#becbd2;background:#edf3f5}.iplane-object-row.is-selected{border-color:#6b9caf;background:#dfeef4}
.iplane-empty,.iplane-context-note,.iplane-draft-note{margin:0;line-height:1.65;font-size:12px;color:#6f7d85}
.iplane-fields{display:grid;gap:10px;min-width:0}.iplane-field{display:grid;gap:5px;min-width:0;font-size:12px;color:#455b67}
.iplane-field input,.iplane-field textarea,.iplane-field select,.iplane-command-line input{box-sizing:border-box;width:100%;min-width:0;border:1px solid #c3cdd3;border-radius:3px;padding:7px 8px;background:white;color:#253d49;font:inherit}
.iplane-field textarea{resize:vertical;min-height:72px}.iplane-field input:focus,.iplane-field textarea:focus,.iplane-command-line input:focus{outline:2px solid #86b3c8;outline-offset:1px}
.iplane-fields>button,.iplane-setting-controls>button,.iplane-panel>button{border:1px solid #c5ced4;border-radius:3px;background:#f8fafb;padding:6px 8px;color:#405864;font-size:12px;cursor:pointer}
.iplane-fields>button:disabled,.iplane-panel>button:disabled{opacity:.55;cursor:default}.iplane-checkbox{display:flex;align-items:center;gap:7px;font-size:12px;color:#455b67}
.iplane-operation-error{padding:10px 12px;border:1px solid #d7b4ae;background:#fff5f3;color:#8c4135;border-radius:4px;font-size:12px;line-height:1.6;overflow-wrap:anywhere}
.iplane-setting-controls{display:flex;gap:6px;flex-wrap:wrap}.iplane-setting-controls>button[aria-pressed=true]{background:#dfedf2;border-color:#8bacbc}
.iplane-facts{display:grid;gap:8px;margin:3px 0 0}.iplane-fact{display:grid;grid-template-columns:72px minmax(0,1fr);gap:8px;font-size:11px}.iplane-fact dt{color:#7a8992}.iplane-fact dd{margin:0;color:#4b616d;overflow-wrap:anywhere}
.iplane-bottom-root{display:contents}
.iplane-bottom-splitter{grid-area:bottom-resize;min-width:0;min-height:6px;cursor:ns-resize;border-top:1px solid #c4ccd1;border-bottom:1px solid #c4ccd1}
.iplane-bottom-splitter::before{width:54px;height:2px}
.iplane-nesting-dock{grid-area:bottom;display:grid;grid-template-columns:minmax(0,.9fr) minmax(0,1.1fr);min-width:0;min-height:0;overflow:hidden;background:#eef2f4}
.iplane-bottom-splitter[hidden],.iplane-nesting-dock[hidden]{display:none}
.iplane-dock-group{display:grid;grid-template-rows:30px minmax(0,1fr);min-width:0;min-height:0;border-top:1px solid #b8c5cd;overflow:hidden}
.iplane-dock-group+.iplane-dock-group{border-left:1px solid #b8c5cd}
.iplane-dock-group>.iplane-panel-heading{padding:0 10px;background:#f3f6f7;border-bottom:1px solid #ced7dc}
.iplane-dock-cards{display:grid;grid-auto-flow:column;grid-auto-columns:164px;align-items:stretch;align-content:stretch;justify-content:start;gap:7px;min-width:0;min-height:0;overflow:auto;padding:8px;scrollbar-gutter:stable}
.iplane-dock-cards>.iplane-empty{align-self:center;grid-column:1/-1;min-width:145px}
.iplane-dock-card{display:flex;flex-direction:column;align-items:flex-start;gap:5px;min-width:0;min-height:82px;padding:9px 10px;border:1px solid #c4cfd5;border-radius:3px;background:#fff;text-align:left;color:#3b525e;font:inherit;cursor:pointer}
.iplane-dock-card strong{font-size:12px;max-width:100%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.iplane-dock-card span{font-size:11px;color:#6b7d87;line-height:1.4;overflow-wrap:anywhere}.iplane-dock-card small{margin-top:auto;font-size:10px;color:#387587}
.iplane-dock-card:hover{border-color:#8daab8;background:#f5fafb}.iplane-dock-card.is-selected{border-color:#6b9caf;background:#e5f1f5}.iplane-dock-card.is-current-canvas{box-shadow:inset 0 3px #6b9caf}
.iplane-dock-card:focus-visible,.iplane-object-row:focus-visible{outline:2px solid #6b9caf;outline-offset:-2px}
.iplane-command-panel{display:grid;align-content:start;padding:6px 12px;background:#f8fafb;min-width:0;height:100%;box-sizing:border-box}
.iplane-command-line{display:grid;grid-template-columns:auto minmax(160px,360px) minmax(0,1fr);align-items:center;gap:10px;font-size:12px;color:#405763}
.iplane-command-line small{font-size:11px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.iplane-status-bar{display:flex;align-items:center;gap:14px;min-width:0;overflow:hidden;padding:0 10px;border-top:1px solid #c5d0d6;background:#f2f5f6;color:#526b78;font-size:11px;box-sizing:border-box}
.iplane-status-unit{flex-shrink:0}.iplane-status-tool{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.iplane-status-controls{display:flex;align-items:center;gap:4px;margin-left:auto;flex-shrink:0}
.iplane-status-controls button{font:inherit;padding:3px 9px;border:1px solid transparent;border-radius:3px;background:transparent;color:inherit;cursor:pointer}.iplane-status-controls button[aria-pressed=true]{border-color:#8bacbc;background:#dfedf2;color:#24566b}.iplane-status-controls button:focus-visible{outline:2px solid #6b9caf;outline-offset:-1px}
`;
export function ensureWorkspaceStyles() {
  if (document.getElementById("iplane-workspace-panel-styles")) return;
  const style = document.createElement("style");style.id = "iplane-workspace-panel-styles";style.textContent = css;document.head.append(style);
}
