import assert from 'node:assert/strict';
import { readFileSync, mkdirSync } from 'node:fs';
import { renderDesignerRightPane } from '../../apps/tube-designer/webpage/designerViews.mjs';
import { catalogText } from '../../apps/tube-designer/webpage/productCatalog.mjs';
import { makeProductToolBinding, productToolRole } from '../../apps/tube-designer/webpage/productResourceBindings.mjs';
import { tubeDesignerCss } from '../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs';

const json = path => JSON.parse(readFileSync(new URL(path, import.meta.url), 'utf8'));
const raw = json('../../apps/tube-designer/templates/product/single_face_security_window/template.json');
const groups = raw.groups.map(g => ({...g, displayName:catalogText(g.displayName)}));
const template = {...raw, available:true, name:catalogText(raw.displayName), groups,
  display:json('../../apps/tube-designer/templates/product/single_face_security_window/display.json'),
  parameters:raw.parameters.map(p => ({...p, displayName:catalogText(p.displayName),
    type:({enum:'select',string:'text'}[p.valueType] ?? p.valueType), groupKey:p.group,
    group:groups.find(g=>g.key===p.group)?.displayName ?? p.group,
    options:p.choices?.map(c=>({...c,label:catalogText(c.displayName)}))}))};
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({headless:true,channel:process.env.ICAX_BROWSER_CHANNEL || 'msedge'});
try {
  const page = await browser.newPage({viewport:{width:1100,height:900}});
  for (const toolId of ['edge-arc-groove','v-notch-sharp']) {
    const tool = {...json(`../../apps/tube-designer/templates/mold/${toolId}/tool.json`),libraryScope:'system'};
    const values = {...Object.fromEntries(raw.parameters.map(p=>[p.key,p.defaultValue])),
      faceType:'three',frameManufacturingMode:'spatial_v_notch',accessDoorEnabled:true,
      doorFrameJoinType:'v_groove_90:tool_library',tubeDesignerToolBindings:{}};
    const field = template.parameters.find(p=>p.key==='doorFrameGrooveTool');
    const binding = makeProductToolBinding(field,tool,null,template);
    values[field.key] = binding.selectionKey;
    values.tubeDesignerToolBindings[productToolRole(field)] = binding;
    const product = {entityId:'window-layout',templateId:raw.id,name:'防盗窗',quantity:1,parameters:values};
    const view = {scene:{tubeDesigner:{templates:[template],product,activeProductId:product.entityId,members:[],joints:[],parts:[]}},
      tubeDesignerParameterPanelProductId:product.entityId,tubeDesignerExpandedParameterGroups:['section:materials','section:process',...groups.map(g=>g.key)],
      tubeDesignerParameterDisclosureState:{fixture:true},
      tubeDesignerSystemPunchTools:[tool]};
    for (const width of [760,520,360,320,240]) {
      await page.setContent(`<style>*{box-sizing:border-box}body{margin:0}.cam-info-pane{width:${width}px;height:900px}${tubeDesignerCss}</style><aside class="cam-info-pane">${renderDesignerRightPane({},view)}</aside>`);
      await page.locator('details').evaluateAll(elements=>elements.forEach(el=>el.open=true));
      const scope = page.locator(`[data-tool-parameter-scope]`).filter({has:page.locator('[data-tube-designer-tool-field="doorFrameGrooveTool"]')});
      assert.equal(await scope.count(),1,toolId);
      const layout = await scope.evaluate(el=>{
        const root=el.parentElement, grid=root.parentElement, bounds=root.getBoundingClientRect();
        const controls=[...el.querySelectorAll('input,select')];
        return {rootWidth:bounds.width,gridWidth:grid.getBoundingClientRect().width,
          overflow:controls.filter(c=>{const b=c.getBoundingClientRect();return b.left<bounds.left-1 || b.right>bounds.right+1;}).map(c=>c.dataset.toolParameterKey),
          inputWidths:controls.filter(c=>c.type==='number').map(c=>c.getBoundingClientRect().width),
          checkboxWidths:controls.filter(c=>c.type==='checkbox').map(c=>c.getBoundingClientRect().width),
          svgWidths:[...el.querySelectorAll('svg')].map(s=>s.getBoundingClientRect().width),
          scopeWidth:el.getBoundingClientRect().width,
          contentWidth:el.clientWidth-parseFloat(getComputedStyle(el).paddingLeft)-parseFloat(getComputedStyle(el).paddingRight),
          columns:getComputedStyle(el.querySelector('.tube-designer-field-grid')).gridTemplateColumns.split(' ').length};
      });
      assert.ok(layout.rootWidth>=layout.gridWidth-20,JSON.stringify({toolId,width,layout}));
      assert.deepEqual(layout.overflow,[],JSON.stringify({toolId,width,layout}));
      assert.ok(layout.inputWidths.every(w=>w>=56),JSON.stringify({toolId,width,layout}));
      assert.ok(layout.checkboxWidths.every(w=>w<=18));
      assert.equal(layout.svgWidths.length,0,'示意图不再占据产品参数栏');
      assert.equal(await scope.locator('[data-product-tool-diagram-open]').count(),1);
      assert.equal(layout.columns,layout.contentWidth<=279?1:2);
      if(toolId==='edge-arc-groove' && width===520){
        mkdirSync(new URL('../../../tmp/embedded-tool-layout/',import.meta.url),{recursive:true});
        await scope.screenshot({path:new URL('../../../tmp/embedded-tool-layout/edge-arc-520.png',import.meta.url).pathname.replace(/^\/(\w:)/,'$1')});
      }
      console.log(`${toolId}: ${width}px sidebar, full-width mould and contained controls passed`);
    }
  }
} finally { await browser.close(); }
