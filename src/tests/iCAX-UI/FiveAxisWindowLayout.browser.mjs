import assert from 'node:assert/strict';
import { resolve } from 'node:path';

export async function verifyFiveAxisWindowLayout(page, output) {
  await page.evaluate(()=>__fiveFixture.populate());
  await page.locator('[data-tab-id="workpiece"]').click();
  const cases=[];
  for(const command of ['five.tool.select','five.rough.point','five.rough.line','five.rough.face','five.tool.fine']){
    await page.locator(`[data-command-id="${command}"]`).click();
    if(command==='five.tool.fine'){
      const input=page.locator('[data-five-field="measured1X"]');await input.fill('120.25');
      await input.evaluate(input=>{input.focus({preventScroll:true});input.setSelectionRange(1,4);});
    }
    await page.evaluate(()=>window.layoutNodes={canvas:document.querySelector('.cam-viewport canvas'),input:document.activeElement});
    for(const size of [{width:1024,height:600},{width:1280,height:720},{width:1600,height:1000},{width:1366,height:768}]){
      await page.setViewportSize(size);
      const layout=await page.evaluate(()=>{
        const bounds=selector=>{const el=document.querySelector(selector);if(!el)return null;const r=el.getBoundingClientRect();return {top:r.top,bottom:r.bottom,left:r.left,right:r.right,height:r.height};};
        return {dock:bounds('.bottom-dock'),workspace:bounds('.workspace'),confirm:bounds('[data-five-command^="five.registration.confirm."]'),
          canvas:!!layoutNodes.canvas&&layoutNodes.canvas===document.querySelector('.cam-viewport canvas'),
          focused:document.activeElement===layoutNodes.input,selection:[layoutNodes.input.selectionStart,layoutNodes.input.selectionEnd]};
      });
      assert.ok(layout.dock.height>=48&&layout.dock.bottom<=size.height+1&&layout.dock.left>=0&&layout.dock.right<=size.width+1,JSON.stringify(layout));
      assert.ok(layout.workspace.bottom<=layout.dock.top+1,JSON.stringify(layout));
      if(layout.confirm)assert.ok(layout.confirm.bottom<=layout.dock.top+1&&layout.confirm.top>=layout.workspace.top,JSON.stringify(layout));
      assert.equal(layout.canvas,true);
      if(command==='five.tool.fine'){assert.equal(layout.focused,true);assert.deepEqual(layout.selection,[1,4]);}
    }
    cases.push(`${command}: visible log dock and controls at four window sizes; existing canvas retained`);
  }
  await page.screenshot({path:resolve(output,'window-log-layout.png')});
  // Drag the real splitter, shrink the window and ensure the dock stays inside.
  const handle=await page.locator('[data-bottom-dock-resize]').boundingBox();
  await page.mouse.move(handle.x+handle.width/2,handle.y+handle.height/2);
  await page.mouse.down();await page.mouse.move(handle.x+handle.width/2,handle.y-100,{steps:5});await page.mouse.up();
  await page.setViewportSize({width:1024,height:600});
  const dock=await page.locator('.bottom-dock').boundingBox();
  assert.ok(dock.height>140&&dock.y+dock.height<=601);
  await page.screenshot({path:resolve(output,'window-log-resized.png')});
  cases.push('log splitter remains visible after dragging and shrinking the window; fine input focus and selection survive ordinary resizing');
  return cases;
}
