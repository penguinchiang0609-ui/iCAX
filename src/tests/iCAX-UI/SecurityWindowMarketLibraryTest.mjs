import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { renderSecurityWindowReview } from "../../apps/tube-designer/webpage/securityWindowReview.mjs";
import { renderProductTemplateLibraryRightPane } from "../../apps/tube-designer/webpage/templateLibrary.mjs";
for (const name of ["single","two","three","five"]) {
  const t=JSON.parse(readFileSync(new URL(`../../apps/tube-designer/templates/product/${name}_face_security_window/template.json`,import.meta.url),"utf8"));
  const values=Object.fromEntries(t.parameters.map(p=>[p.key,p.defaultValue]));
  const html=renderSecurityWindowReview(t,{...values,infillPattern:"horizontal",installationMode:"recessed",projectEscapeMinWidth:700,projectEscapeMinHeight:900});
  assert.match(html,/纯横杆/);assert.match(html,/攀爬风险/);assert.match(html,/窗洞内嵌/);
  assert.match(html,/700 × 900/);assert.match(html,/不是全国统一法规/);
  assert.match(renderSecurityWindowReview(t,{...values,projectEscapeMinWidth:850}),/小于项目配置下限/);
  const pane=renderProductTemplateLibraryRightPane({}, {scene:{tubeDesigner:{templates:[{...t,available:true}]}},tubeDesignerProductTemplateLibrary:{scope:"system",selectedId:t.id}});
  assert.match(pane,/立面格栅款式/);assert.match(pane,/项目开启口最小净宽/);
  assert.match(pane,/安装场景/);
}
console.log("Security-window market variants and project rule presentation passed.");
