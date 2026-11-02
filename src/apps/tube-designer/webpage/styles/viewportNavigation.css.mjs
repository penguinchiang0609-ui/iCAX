// Keep the same compact, Chinese navigation presentation in the main scene and
// independent 3D previews. Scope all rules to TubeDesigner, leaving Laser3D alone.
const scope = ":is(.tube-designer-workspace, [data-tube-assembly-scene-viewport], [data-tube-designer-part-inspection-viewport], [data-part-drawing-viewport], [data-tube-designer-punch-viewport])";
export const viewportNavigationCss = `
${scope} { --tube-viewcube-size:148px; --tube-viewcube-top:8px; --tube-viewcube-right:12px; --tube-projection-width:112px; }
${scope} .cam-viewcube { top:var(--tube-viewcube-top); right:var(--tube-viewcube-right); width:var(--tube-viewcube-size); height:var(--tube-viewcube-size); }
${scope} .cam-viewcube-axes { stroke-linecap:round; stroke-linejoin:round; }
${scope} .cam-viewcube-axis-label { font:600 11px "Segoe UI",sans-serif; text-anchor:middle; dominant-baseline:central; stroke:none; }
${scope} .cam-viewcube[data-appearance="beveled"] .cam-viewcube-surface { stroke:#7e8b97; stroke-width:.55; stroke-linejoin:round; vector-effect:non-scaling-stroke; }
${scope} .cam-viewcube[data-appearance="beveled"] .cam-viewcube-label { fill:#40515d; font:500 14px "Segoe UI","Microsoft YaHei",sans-serif; letter-spacing:1px; text-shadow:none; }
${scope} .icax-three-projection-toggle { top:calc(var(--tube-viewcube-top) + var(--tube-viewcube-size)); right:calc(var(--tube-viewcube-right) + (var(--tube-viewcube-size) - var(--tube-projection-width)) / 2); bottom:auto; left:auto; transform:none; width:var(--tube-projection-width); box-sizing:border-box; border-radius:3px; padding:2px; box-shadow:none; }
${scope} .icax-three-projection-toggle button { flex:1; min-width:0; height:23px; padding:0 5px; line-height:23px; border-radius:2px; }
${scope} .icax-three-projection-toggle button:focus-visible { outline:2px solid #60b6af; }
@media (max-width:1100px) and (max-height:700px) {
  ${scope} { --tube-viewcube-size:120px; --tube-projection-width:104px; }
}
`;
