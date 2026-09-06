export const nestingSettingsCss = `
.tube-nesting-settings-backdrop {position:fixed;inset:0;z-index:1500;display:grid;place-items:center;padding:24px;box-sizing:border-box;background:rgba(16,32,39,.35);}
.tube-nesting-settings-dialog {display:flex;flex-direction:column;width:min(760px,100%);max-height:calc(100vh - 48px);min-width:0;overflow:hidden;background:#f7fafb;border:1px solid #bdcfd5;border-radius:8px;box-shadow:0 16px 52px rgba(10,30,40,.28);color:#24434f;font-size:13px;}
.tube-nesting-settings-dialog.is-parameters {width:min(480px,100%);}
.tube-nesting-settings-dialog button,.tube-nesting-settings-dialog input {font:inherit;box-sizing:border-box;}
.tube-nesting-settings-dialog button {border:1px solid #a9c0c8;border-radius:4px;background:#fff;color:#315762;padding:7px 12px;cursor:pointer;}
.tube-nesting-settings-dialog button:hover {background:#e9f4f3;border-color:#168f87;}
.tube-nesting-settings-dialog button:focus-visible,.tube-nesting-settings-dialog input:focus-visible {outline:2px solid #16998e;outline-offset:2px;}
.tube-nesting-settings-dialog button:disabled {opacity:.45;cursor:default;}
.tube-nesting-settings-header {flex:0 0 auto;display:flex;justify-content:space-between;gap:16px;padding:18px 20px;border-bottom:1px solid #cbd9de;background:#fff;}
.tube-nesting-settings-header>div {display:grid;gap:6px;}
.tube-nesting-settings-header strong {font-size:18px;line-height:1.3;}
.tube-nesting-settings-header span {color:#6c8792;font-size:12px;line-height:1.5;}
.tube-nesting-settings-header .tube-nesting-settings-close {align-self:flex-start;border:0;padding:0 5px;font-size:24px;line-height:24px;}
.tube-nesting-settings-body {flex:1 1 auto;min-height:0;overflow:auto;padding:18px 20px;overscroll-behavior:contain;}
.tube-nesting-stock-group {margin:0 0 14px;border:1px solid #c3d4da;border-radius:5px;overflow:hidden;background:#fff;}
.tube-nesting-stock-group:last-child {margin-bottom:0;}
.tube-nesting-stock-group>header {display:flex;justify-content:space-between;align-items:center;gap:16px;padding:12px 14px;background:#eaf2f4;border-bottom:1px solid #d8e3e7;}
.tube-nesting-stock-group>header>div {display:grid;gap:5px;min-width:0;}
.tube-nesting-stock-group>header strong {overflow-wrap:anywhere;}
.tube-nesting-stock-group>header span {color:#708b96;font-size:12px;}
.tube-nesting-stock-group>header button {flex:0 0 auto;}
.tube-nesting-stock-columns,.tube-nesting-stock-row {display:grid;grid-template-columns:minmax(0,1fr) minmax(0,.7fr) 54px;gap:12px;align-items:center;padding:7px 14px;}
.tube-nesting-stock-columns {padding-top:12px;padding-bottom:3px;color:#67818d;font-size:12px;}
.tube-nesting-stock-row:last-child {padding-bottom:13px;}
.tube-nesting-stock-row input,.tube-nesting-parameter-field input {width:100%;min-width:0;border:1px solid #b8cbd3;border-radius:4px;background:#fff;padding:8px 10px;color:#254651;}
.tube-nesting-stock-row .tube-nesting-remove-row {padding:7px 4px;border-color:transparent;color:#70858e;}
.tube-nesting-stock-group>.tube-nesting-settings-hint {margin:12px 14px;}
.tube-nesting-parameter-field {display:grid;gap:10px;max-width:100%;padding:4px 0 14px;}
.tube-nesting-parameter-field>span {font-weight:600;}
.tube-nesting-parameter-field small {color:#718a95;font-size:12px;}
.tube-nesting-settings-footer {flex:0 0 auto;display:flex;align-items:center;gap:10px;padding:14px 20px;border-top:1px solid #cbd9de;background:#fff;}
.tube-nesting-settings-footer>div {flex:1;min-width:0;}
.tube-nesting-settings-footer>button {flex:0 0 auto;min-width:64px;}
.tube-nesting-settings-footer .tube-nesting-settings-save {background:#158e83;border-color:#158e83;color:#fff;}
.tube-nesting-settings-footer .tube-nesting-settings-save:hover {background:#0c776f;}
.tube-nesting-settings-hint {color:#708a95;font-size:12px;line-height:1.5;}
.tube-nesting-settings-error {margin:0;color:#b64535;font-size:12px;line-height:1.5;}
.tube-nesting-settings-empty {display:grid;gap:10px;place-items:center;min-height:160px;color:#718993;}
.tube-nesting-settings-empty strong {color:#4a6976;}
@media (max-width:600px) {.tube-nesting-settings-backdrop {padding:12px;}.tube-nesting-settings-dialog {max-height:calc(100vh - 24px);}.tube-nesting-settings-body,.tube-nesting-settings-header,.tube-nesting-settings-footer {padding:12px;}.tube-nesting-stock-columns,.tube-nesting-stock-row {gap:7px;padding-left:10px;padding-right:10px;}}
`;
