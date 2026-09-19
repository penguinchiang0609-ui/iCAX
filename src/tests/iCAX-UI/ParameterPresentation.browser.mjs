import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {resolve,sep} from 'node:path';
import {tubeDesignerCss} from '../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs';
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const browser=await chromium.launch({headless:true,channel:'msedge'});
try{
  const page=await browser.newPage(),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  const root=fileURLToPath(new URL('../../',import.meta.url));
  await page.route('http://parameter-presentation.test/**',route=>{
    const pathname=new URL(route.request().url()).pathname;
    if(pathname==='/')return route.fulfill({contentType:'text/html',body:'<body></body>'});
    const path=resolve(root,pathname.replace(/^\/src\//,''));
    if(!pathname.startsWith('/src/')||!path.startsWith(root.replace(/[\\/]$/,'')+sep))return route.abort();
    return route.fulfill({contentType:'text/javascript',body:readFileSync(path,'utf8')});
  });
  await page.goto('http://parameter-presentation.test/');
  await page.addStyleTag({content:tubeDesignerCss});
  await page.evaluate(async()=>{
    const {renderDesignerRightParameterContent}=await import('/src/apps/tube-designer/webpage/designerViews.mjs');
    const {bindProductParameterDiagrams,renderProductParameterDiagram}=await import('/src/apps/tube-designer/webpage/productParameterDiagram.mjs');
    const template={id:'presentation-demo',available:true,name:'参数分级',groups:[{key:'size',displayName:'尺寸',defaultOpen:true}],parameters:[
      {key:'width',groupKey:'size',group:'尺寸',displayName:'宽度',type:'number',defaultValue:120,presentation:{advanced:true}},
      {key:'height',groupKey:'size',group:'尺寸',displayName:'高度',type:'number',defaultValue:200,presentation:{visible:false}},
      {key:'note',groupKey:'size',group:'尺寸',displayName:'备注',type:'text',defaultValue:'draft'}
    ],extensions:{primaryDimensions:{widthParameter:'width',heightParameter:'height'}}};
    const view={tubeDesignerAddTemplateId:template.id,tubeDesignerAddDraft:{width:120,height:200,note:'draft'}};
    const designer={templates:[template],product:{entityId:'demo-product',templateId:template.id,parameters:view.tubeDesignerAddDraft},parts:[]};
    view.scene={tubeDesigner:designer};
    document.body.innerHTML=`<main data-tube-designer-product-parameter-scope data-tube-designer-editor-mode="right">${renderProductParameterDiagram(template,view.tubeDesignerAddDraft,{mode:'right'})}${renderDesignerRightParameterContent(designer,view).scrollContent}</main>`;
    const mount=document.querySelector('main');
    mount.querySelectorAll('details:not([data-parameter-advanced])').forEach(d=>d.open=true);
    bindProductParameterDiagrams(document.body);
  });
  const width=page.locator('[data-product-diagram-parameter="width"]');
  assert.equal(await width.count(),1);
  assert.equal(await width.isVisible(),false);
  assert.equal(await page.locator('[data-product-diagram-parameter="height"], [data-product-parameter-key="height"]').count(),0);
  const summary=page.locator('[data-parameter-advanced] > summary');
  await summary.click();
  await width.waitFor({state:'visible',timeout:2000});
  await summary.click();await width.waitFor({state:'hidden'});
  assert.deepEqual(errors,[]);
  console.log('Product parameter presentation passed: hidden input/caption, advanced captions only while expanded.');
}finally{await browser.close();}
