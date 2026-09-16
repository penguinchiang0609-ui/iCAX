import assert from "node:assert/strict";
import {readFileSync,readdirSync,existsSync} from "node:fs";
import {renderProductTemplateLibraryRightPane} from "../../apps/tube-designer/webpage/templateLibrary.mjs";
import {buildCatalogEntries} from "../../apps/tube-designer/webpage/productCatalog.mjs";
import {matchesParameterCondition} from "../../apps/tube-designer/webpage/parameterConditions.mjs";
const root=new URL("../../apps/tube-designer/templates/product/",import.meta.url);
let count=0;
for(const name of readdirSync(root).filter(name=>name.startsWith("modular_guardrail"))){
  const file=new URL(`${name}/template.json`,root);
  if(!existsSync(file))continue;
  const t=JSON.parse(readFileSync(file,"utf8"));
  assert.equal(t.version,"1.3.0");
  assert.equal(new Set(t.parameters.map(p=>p.key)).size,t.parameters.length);
  const fields=new Map(t.parameters.map(p=>[p.key,p]));
  assert.equal(fields.get("pathMode").defaultValue,"level");
  const levelOnly = fields.get("pathMode").choices.length === 1;
  if(levelOnly) {
    assert.equal(fields.get("pathMode").readOnly, true);
    assert.equal(fields.get("pathMode").choices[0].value, "level");
  } else assert.ok(fields.get("pathMode").choices.some(c=>c.value==="continuous"));
  assert.ok(fields.get("elevationSource").choices.some(c=>c.value==="treads"));
  assert.equal(fields.get("slopeAngle2").defaultValue,0);
  assert.ok(fields.has("treadCount3"));
  assert.ok(fields.get("infillType").choices.some(c=>c.value==="horizontal"));
  assert.ok(fields.get("installation").choices.some(c=>c.value==="side_plate"));
  count++;
  if(t.extensions?.catalog?.listed === false) continue;
  const html=renderProductTemplateLibraryRightPane({}, {scene:{tubeDesigner:{templates:[{...t,available:true}]}},tubeDesignerProductTemplateLibrary:{scope:"system",selectedId:t.id}});
  assert.match(html,/高程排布/);
  if (!fields.get("infillType").readOnly) assert.match(html,/横向管材／板条填充/);
  assert.match(html,/侧装锚固板/);
  if(levelOnly) {
    assert.doesNotMatch(html, /value="continuous"|value="stepped"/);
    assert.doesNotMatch(html, /data-tube-template-library-parameter="(?:slopeAngle|elevationSource|treadGoing)"/);
    continue;
  }
  const treadView={scene:{tubeDesigner:{templates:[{...t,available:true}]}},tubeDesignerProductTemplateLibrary:{scope:"system",selectedId:t.id,
    parameterDrafts:{[t.id]:{pathMode:"continuous",elevationSource:"treads",layout:"u",treadCount1:10,treadCount2:0,treadCount3:4}}}};
  const treadHtml=renderProductTemplateLibraryRightPane({},treadView);
  assert.match(treadHtml,/data-tube-template-library-parameter="treadGoing"/);
  assert.match(treadHtml,/data-tube-template-library-parameter="treadCount3"/);
  assert.doesNotMatch(treadHtml,/data-tube-template-library-parameter="sideLength1"/);
  assert.match(treadHtml,/data-tube-template-library-parameter="sideLength2"/);
  assert.doesNotMatch(treadHtml,/data-tube-template-library-parameter="slopeAngle"/);
  assert.doesNotMatch(treadHtml,/data-tube-template-library-parameter="endExtension"/);
}
assert.equal(count,32);
console.log(`Guardrail parameter presentation: ${count} templates passed.`);
const families = buildCatalogEntries(readdirSync(root).filter(n => n.startsWith("modular_guardrail") && existsSync(new URL(`${n}/template.json`, root))).map(n => ({...JSON.parse(readFileSync(new URL(`${n}/template.json`, root), "utf8")), available: true})));
assert.deepEqual(families.map(t => t.displayName), ["挡板护栏", "菱形护栏", "X 形护栏", "竖杆护栏"]);
for (const t of families) assert.deepEqual(t.catalogPath.slice(0,-1), ["护栏"]);
assert.equal(existsSync(new URL("straight_stair_railing/template.json", root)), false);
for (const t of families) {
  assert.equal(t.parameters.some(p => p.key === "guardrailUse"), t.displayName === "竖杆护栏");
  for (const [layout, sides] of [["straight", 1], ["left_l", 2], ["right_l", 2], ["u", 3]]) {
    for (let i = 1; i <= 3; i++) {
      const p = t.parameters.find(p => p.key === `sideBayCount${i}`);
      assert.ok(p);
      assert.equal(matchesParameterCondition(p.visibleWhen, {layout}), i <= sides);
    }
  }
}
const glass = ["straight", "left-l"].map(layout => ({
  ...JSON.parse(readFileSync(new URL(`modular_guardrail_glass-${layout}/template.json`, root), "utf8")), available: true,
}));
assert.deepEqual(buildCatalogEntries(glass).map(t => t.displayName), ["挡板护栏"]);
for (const template of glass) {
  const fields = new Map(template.parameters.map(p => [p.key, p]));
  for (const key of ["guardrailUse", "wallPicketProjection", "picketHoleClearance", "spearTipEnabled", "spearTipModelReference"]) {
    assert.equal(fields.has(key), false, `${template.id}: unexpected wall field ${key}`);
  }
  assert.deepEqual(fields.get("layout").choices.map(c => c.value), ["straight", "left_l", "right_l", "u"]);
}
