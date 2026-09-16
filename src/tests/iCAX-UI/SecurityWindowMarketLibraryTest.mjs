import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { renderSecurityWindowReview } from "../../apps/tube-designer/webpage/securityWindowReview.mjs";
import { renderProductTemplateLibraryRightPane } from "../../apps/tube-designer/webpage/templateLibrary.mjs";
for (const name of ["single"]) {
  const t=JSON.parse(readFileSync(new URL(`../../apps/tube-designer/templates/product/${name}_face_security_window/template.json`,import.meta.url),"utf8"));
  const values=Object.fromEntries(t.parameters.map(p=>[p.key,p.defaultValue]));
  const html=renderSecurityWindowReview(t,{...values,infillPattern:"horizontal"});
  assert.match(html,/纯横杆/);assert.match(html,/攀爬风险/);assert.match(html,/逃生窗/);
  assert.doesNotMatch(html,/安装场景|项目净尺寸下限|规则依据|全国统一法规/);
  if (t.extensions?.catalog?.listed === false) continue;
  const pane=renderProductTemplateLibraryRightPane({}, {scene:{tubeDesigner:{templates:[{...t,available:true}]}},tubeDesignerProductTemplateLibrary:{scope:"system",selectedId:t.id}});
  assert.match(pane,/立面格栅款式/);
  assert.doesNotMatch(pane,/项目开启口最小净宽|项目开启口最小净高|安装场景|开启口用途/);
}
console.log("Security-window market variants and streamlined parameter presentation passed.");
