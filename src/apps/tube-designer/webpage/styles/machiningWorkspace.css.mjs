export const machiningWorkspaceCss = `
.tube-designer-machining-workspace.cam-workbench {
  grid-template-rows: auto auto minmax(0, 1fr);
  grid-template-areas: "notice notice notice notice notice" "workflow workflow workflow workflow workflow" "left left-resize viewer right-resize right";
}
.tube-cam-workflow { grid-area: workflow; display: flex; align-items: center; gap: 20px; min-width: 0; padding: 14px 20px; background: #fff; border-bottom: 1px solid #ccdadd; color: #23434d; }
.tube-cam-workflow-brand { display: grid; gap: 5px; min-width: 92px; }
.tube-cam-workflow-brand strong { font-size: 17px; }
.tube-cam-workflow-brand > span { font-size: 11px; color: #72888e; }
.tube-cam-workflow-steps { display: flex; flex: 1; gap: 8px; min-width: 0; overflow-x: auto; }
.tube-cam-workflow-steps button { display: flex; flex: 1; align-items: center; gap: 9px; min-width: 135px; padding: 10px; text-align: left; color: #52707a; border: 1px solid transparent; border-radius: 7px; background: transparent; cursor: pointer; }
.tube-cam-workflow-steps button > span:last-child { display: grid; gap: 5px; }
.tube-cam-workflow-steps strong { font-size: 13px; white-space: nowrap; }
.tube-cam-workflow-steps small { color: #81959b; font-size: 10px; white-space: nowrap; }
.tube-cam-step-number { display: grid; place-items: center; width: 25px; height: 25px; flex-shrink: 0; border-radius: 50%; background: #eaf0f2; font-size: 12px; }
.tube-cam-workflow-steps button.active { border-color: #bddbd7; background: #ebf7f4; color: #157f76; }
.tube-cam-workflow-steps button.active .tube-cam-step-number { background: #218f85; color: white; }
.tube-cam-workflow-count { display: grid; gap: 3px; text-align: center; }
.tube-cam-workflow-count strong { font-size: 22px; font-weight: 600; }
.tube-cam-workflow-count span { font-size: 11px; color: #81959b; }
.tube-cam-source-actions { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin: 16px 0; }
.tube-cam-source-actions button { padding: 9px 7px; }
.tube-cam-job-search { display: grid; gap: 7px; color: #72888e; font-size: 11px; }
.tube-cam-job-search input { width: 100%; min-width: 0; padding: 9px 10px; color: #23434d; background: white; border: 1px solid #ccdadd; border-radius: 5px; }
.tube-cam-readiness { border: 1px solid #d6e2e4; border-radius: 7px; padding: 12px; background: #fff; margin: 14px 0 20px; }
.tube-cam-readiness header { display: flex; justify-content: space-between; gap: 8px; font-size: 13px; }
.tube-cam-readiness header > span { font-size: 11px; color: #81959b; }
.tube-cam-readiness ul { list-style: none; padding: 0; margin: 12px 0 0; display: grid; gap: 12px; }
.tube-cam-readiness li { display: flex; align-items: center; gap: 8px; font-size: 11px; }
.tube-cam-readiness li > strong { margin-left: auto; color: #81959b; font-weight: 400; }
.tube-cam-check-dot { width: 6px; height: 6px; flex-shrink: 0; border-radius: 50%; background: #bdcbd0; }
.tube-cam-readiness .ready .tube-cam-check-dot { background: #219b8d; }
.tube-cam-readiness .ready > strong { color: #218a7f; }
.tube-cam-readiness .warning .tube-cam-check-dot { background: #bf7d3c; }
.tube-cam-readiness .warning > strong { color: #ad542e; }
.tube-cam-setting-card { border: 1px solid #d3e0e3; padding: 14px; border-radius: 7px; background: #fff; margin: 16px 0; }
.tube-cam-setting-card header { display: flex; gap: 10px; align-items: center; justify-content: space-between; font-size: 14px; }
.tube-cam-capability-tag { font-size: 10px; color: #6d8793; background: #eef3f5; padding: 3px 6px; border-radius: 3px; }
.tube-cam-specification { display: grid; grid-template-columns: 85px minmax(0, 1fr); gap: 14px 8px; font-size: 12px; line-height: 1.7; margin: 18px 0; }
.tube-cam-specification dt { color: #81959b; }
.tube-cam-specification dd { margin: 0; }
.tube-cam-setting-card button { width: 100%; padding: 9px; }
.tube-cam-setting-card button:disabled { color: #8a9ca3; cursor: default; background: #f0f4f5; opacity: .75; }
.tube-cam-review-list { margin: 18px 0; padding-left: 18px; font-size: 12px; line-height: 2.3; }
.tube-cam-process-tabs { display: flex; gap: 5px; margin-top: 16px; }
.tube-cam-process-tabs button { flex: 1; background: #fff; border: 1px solid #ccdadd; border-radius: 4px; padding: 7px; color: #52707a; cursor: pointer; }
.tube-cam-process-tabs button.active { border-color: #219b8d; color: #157f76; background: #ebf7f4; }
.tube-cam-workflow button:focus-visible, .tube-cam-source-actions button:focus-visible, .tube-cam-process-tabs button:focus-visible { outline: 2px solid #24998e; outline-offset: 2px; }
.tube-designer-machining-workspace .tube-machining-job-list { max-height: 52vh; overflow-y: auto; }
.tube-designer-machining-workspace .cam-viewer-head > div > span:last-child { display: none; }
.tube-designer-machining-workspace .tube-machining-pane { padding: 10px; }
.tube-designer-machining-workspace .tube-machining-edit-toolbar { right: 110px; }
.tube-designer-machining-workspace .tube-machining-scene-header { display: grid; gap: 5px; right: auto; max-width: calc(100% - 190px); color: #dae8ec; }
.tube-designer-machining-workspace .tube-machining-scene-header small, .tube-designer-machining-workspace .tube-machining-scene-header span { color: #8ea9b3; }
@media (max-width: 1150px) { .tube-cam-workflow { gap: 10px; padding: 10px; } .tube-cam-workflow-brand { min-width: 72px; } .tube-cam-workflow-brand strong { font-size: 14px; } .tube-cam-workflow-steps button { min-width: 112px; padding: 8px 6px; gap: 6px; } .tube-cam-workflow-steps small { display: none; } .tube-cam-workflow-count { display: none; } }
`;
