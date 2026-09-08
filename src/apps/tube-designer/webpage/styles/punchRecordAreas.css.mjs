export const punchRecordAreasCss = `
.tube-designer-punch-sheet-workspace > .tube-designer-punch-records {
  grid-column: 1; grid-row: 4; display: grid; grid-template-rows: minmax(0,1fr);
  height: clamp(266px, 35vh, 350px); min-width: 0; min-height: 0; gap: 6px; margin: 0 10px 9px;
}
.tube-designer-punch-sheet-workspace > .tube-designer-punch-records.has-ends { grid-template-rows: auto minmax(0,1fr); }
.tube-designer-punch-records > .tube-designer-punch-sheet { grid-area: auto; grid-template-rows: auto minmax(0,1fr); margin: 0; }
.tube-designer-punch-records .tube-designer-punch-sheet-scroll { max-height: none; }
.tube-designer-punch-records .tube-designer-punch-sheet table { width: 100%; min-width: 0; }
.tube-designer-punch-records .tube-designer-punch-sheet th:nth-child(1) { width: 30px; }
.tube-designer-punch-records .tube-designer-punch-sheet th:nth-child(2) { width: 42px; }
.tube-designer-punch-records .tube-designer-punch-sheet th:nth-child(3) { width: 23%; }
.tube-designer-punch-records .tube-designer-punch-sheet th:nth-child(4) { width: 24%; }
.tube-designer-punch-records .tube-designer-punch-sheet th:nth-child(5) { width: auto; }
.tube-designer-punch-records .tube-designer-punch-sheet th:nth-child(6) { width: 88px; }
.tube-designer-punch-records .tube-designer-punch-sheet thead small { display: block; font-weight: 400; font-size: 9px; color: #6a8088; }
.punch-record-cell { display: grid; gap: 6px; min-width: 0; padding: 3px 0; }
.punch-record-cell > button { justify-self: start; padding: 3px 7px; }
.punch-pose-summary,.punch-array-summary { font-size: 11px; line-height: 1.55; color: #42616c; overflow-wrap: anywhere; }
.punch-array-summary { display: grid; gap: 2px; }
.punch-array-summary > small { color: #6a8088; font-size: 10px; }
.tube-designer-punch-ends { min-width: 0; background: #fff; border: 1px solid #b8c7cc; }
.tube-designer-punch-ends table { width: 100%; border-collapse: collapse; table-layout: fixed; font-size: 11px; color: #314b53; }
.tube-designer-punch-ends caption { padding: 5px 8px; text-align: left; font-weight: 600; color: #304f58; background: #f4f7f8; }
.tube-designer-punch-ends caption small { margin-left: 8px; font-size: 10px; font-weight: 400; color: #6a8088; }
.tube-designer-punch-ends th,.tube-designer-punch-ends td { padding: 3px 5px; height: 28px; border-top: 1px solid #d2dcdf; text-align: left; }
.tube-designer-punch-ends thead th { height: 22px; background: #edf2f4; font-size: 10px; font-weight: 500; }
.tube-designer-punch-ends th:nth-child(1) { width: 58px; }
.tube-designer-punch-ends th:nth-child(2) { width: 104px; }
.tube-designer-punch-ends th:nth-child(3) { width: 170px; }
.tube-designer-punch-ends th:nth-child(4) { width: auto; }
.tube-designer-punch-ends th:nth-child(5) { width: 80px; }
.tube-designer-punch-ends select { width: 100%; min-width: 0; height: 24px; padding: 2px 3px; border: 1px solid #cedbdf; background: #fff; color: inherit; font: inherit; }
.tube-designer-punch-ends button { min-height: 24px; padding: 2px 5px; font-size: 10px; white-space: nowrap; }
.tube-designer-punch-end-summary { font-size: 10px; color: #68808a; overflow-wrap: anywhere; }
.tube-designer-punch-ends .tube-designer-punch-source-cell { display: block; overflow: hidden; }
.tube-designer-punch-ends .tube-designer-punch-source-cell > span { display: block; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.tube-designer-punch-ends .tube-designer-punch-source-cell > small { display: none; }
.tube-designer-punch-end-placement { min-width: 0; margin: 12px 0 16px; padding: 12px; border: 1px solid #c9d8dd; border-radius: 4px; }
.tube-designer-punch-end-placement legend { padding: 0 5px; color: #45636d; font-size: 12px; }
.tube-designer-punch-end-placement > div { display: grid; grid-template-columns: repeat(3,minmax(0,1fr)); gap: 12px; }
.tube-designer-punch-end-placement label { display: grid; gap: 5px; color: #58727b; font-size: 12px; }
.tube-designer-punch-row-layout > details > summary { white-space: normal; line-height: 1.45; }
.tube-designer-punch-row-layout > details[open] { display: grid; gap: 5px; }
.punch-pose-fields { display: grid; grid-template-columns: repeat(2,minmax(0,1fr)); gap: 12px; margin-bottom: 16px; }
.punch-pose-field { display: grid; gap: 5px; font-size: 12px; color: #58727b; }
.punch-array-editor { display: grid; gap: 12px; }
.punch-array-editor > p { margin: 0; font-size: 12px; line-height: 1.6; color: #58727b; }
.punch-array-add { display: flex; flex-wrap: wrap; gap: 6px; }
.punch-array-add button { min-height: 30px; font-size: 12px; }
.punch-array-group { min-width: 0; border: 1px solid #c9d8dd; border-radius: 4px; }
.punch-array-group > header { display: flex; align-items: center; gap: 12px; padding: 7px 10px; background: #f0f5f6; border-bottom: 1px solid #d5e0e3; font-size: 12px; }
.punch-array-group > header > button { margin-left: auto; font-size: 11px; }
.punch-array-fields { display: grid; grid-template-columns: repeat(2,minmax(0,1fr)); gap: 10px 12px; padding: 10px; }
.punch-array-field,.punch-array-skips label { display: grid; gap: 4px; min-width: 0; font-size: 12px; color: #58727b; }
.tube-designer-punch-parameter-content .punch-array-check { display: flex; align-items: center; gap: 4px; font-size: 12px; }
.punch-array-check input { width: 14px; height: 14px; }
.punch-array-origin,.punch-array-length-rules,.punch-array-help { grid-column: 1 / -1; }
.punch-array-help { color: #6a8088; font-size: 11px; line-height: 1.55; }
.punch-array-origin summary,.punch-array-skips summary { color: #376e72; cursor: pointer; font-size: 12px; padding: 4px 0; }
.punch-array-origin > .punch-array-check { margin: 7px 0; }
.punch-array-origin .punch-array-fields { grid-template-columns: repeat(3,minmax(0,1fr)); padding: 0 0 7px; }
.punch-array-editor .tube-designer-punch-layout-controls { gap: 8px; }
.punch-array-editor .tube-designer-punch-layout-fields { gap: 10px 12px; }
.punch-array-editor .tube-designer-punch-layout-field > span:first-child,.punch-array-editor .tube-designer-punch-layout-details { font-size: 12px; }
.punch-array-editor .tube-designer-punch-layout-hint,.punch-array-editor .tube-designer-punch-layout-text > small { font-size: 11px; }
.punch-array-editor .tube-designer-punch-layout-text { gap: 5px; font-size: 12px; }
.punch-array-editor textarea { min-height: 55px; padding: 7px; font: inherit; color: #29434b; border: 1px solid #b8c9cd; border-radius: 3px; resize: vertical; width: 100%; box-sizing: border-box; }
.punch-array-editor > footer { border-top: 1px solid #c9d8dd; padding-top: 10px; }
@media(max-width:1100px) {
  .tube-designer-punch-ends th:nth-child(2) { width: 90px; }
  .tube-designer-punch-ends th:nth-child(3) { width: 136px; }
  .tube-designer-punch-ends th:nth-child(4) { width: auto; }
  .tube-designer-punch-ends caption small { font-size: 9px; }
}
.tube-designer-punch-rightbar > .tube-designer-punch-records {
  grid-column: auto; grid-row: auto; height: auto; margin: 0;
}
.tube-designer-punch-rightbar > .tube-designer-punch-records > .tube-designer-punch-ends,
.tube-designer-punch-rightbar > .tube-designer-punch-records > .tube-designer-punch-sheet {
  grid-column: auto; grid-row: auto;
}
.tube-designer-punch-rightbar .tube-designer-punch-sheet table {
  table-layout: fixed; width: 100%; min-width: 0;
}
.tube-designer-punch-rightbar .tube-designer-punch-sheet th:nth-child(1) { width: 30px; }
.tube-designer-punch-rightbar .tube-designer-punch-sheet th:nth-child(2) { width: 40px; }
.tube-designer-punch-rightbar .tube-designer-punch-sheet th:nth-child(3) { width: 22%; }
.tube-designer-punch-rightbar .tube-designer-punch-sheet th:nth-child(4) { width: 25%; }
.tube-designer-punch-rightbar .tube-designer-punch-sheet th:nth-child(5) { width: auto; }
.tube-designer-punch-rightbar .tube-designer-punch-sheet th:nth-child(6) { width: 50px; }
.tube-designer-punch-rightbar .tube-designer-punch-ends th:nth-child(1) { width: 40px; }
.tube-designer-punch-rightbar .tube-designer-punch-ends th:nth-child(2) { width: 66px; }
.tube-designer-punch-rightbar .tube-designer-punch-ends th:nth-child(3) { width: 86px; }
.tube-designer-punch-rightbar .tube-designer-punch-ends th:nth-child(4) { width: auto; }
.tube-designer-punch-rightbar .tube-designer-punch-ends th:nth-child(5) { width: 78px; }
`;
