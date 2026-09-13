import assert from "node:assert/strict";
import {readFileSync,readdirSync,existsSync} from "node:fs";
import {renderProductTemplateLibraryRightPane} from "../../apps/tube-designer/webpage/templateLibrary.mjs";
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
  assert.ok(fields.get("pathMode").choices.some(c=>c.value==="continuous"));
  assert.ok(fields.get("elevationSource").choices.some(c=>c.value==="treads"));
  assert.equal(fields.get("slopeAngle2").defaultValue,0);
  assert.ok(fields.has("treadCount3"));
  assert.ok(fields.get("infillType").choices.some(c=>c.value==="horizontal"));
  assert.ok(fields.get("installation").choices.some(c=>c.value==="side_plate"));
  const html=renderProductTemplateLibraryRightPane({}, {scene:{tubeDesigner:{templates:[{...t,available:true}]}},tubeDesignerProductTemplateLibrary:{scope:"system",selectedId:t.id}});
  assert.match(html,/高程排布/);
  assert.match(html,/横向管材／板条填充/);
  assert.match(html,/侧装锚固板/);
  const treadView={scene:{tubeDesigner:{templates:[{...t,available:true}]}},tubeDesignerProductTemplateLibrary:{scope:"system",selectedId:t.id,
    parameterDrafts:{[t.id]:{pathMode:"continuous",elevationSource:"treads",layout:"u",treadCount1:10,treadCount2:0,treadCount3:4}}}};
  const treadHtml=renderProductTemplateLibraryRightPane({},treadView);
  assert.match(treadHtml,/data-tube-template-library-parameter="treadGoing"/);
  assert.match(treadHtml,/data-tube-template-library-parameter="treadCount3"/);
  assert.doesNotMatch(treadHtml,/data-tube-template-library-parameter="sideLength1"/);
  assert.match(treadHtml,/data-tube-template-library-parameter="sideLength2"/);
  assert.doesNotMatch(treadHtml,/data-tube-template-library-parameter="slopeAngle"/);
  assert.doesNotMatch(treadHtml,/data-tube-template-library-parameter="endExtension"/);
  count++;
}
assert.equal(count,32);
console.log(`Guardrail parameter presentation: ${count} templates passed.`);
