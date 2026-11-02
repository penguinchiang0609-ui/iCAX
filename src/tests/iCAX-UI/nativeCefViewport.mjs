// Native CEF already owns a real desktop viewport. Device-metrics emulation
// changes only its web contents, leaving the desktop window unchanged and
// clipping controls outside the window. Keep native acceptance at real size.
export async function restoreNativeCefViewport(page){
  const session=await page.context().newCDPSession(page);
  try{
    // A different DevTools session may have installed the override. First take
    // ownership with disabled dimensions, then clear it; clearing a fresh
    // session alone does not remove another session's emulation in CEF.
    await session.send('Emulation.setDeviceMetricsOverride',{width:0,height:0,deviceScaleFactor:0,mobile:false});
    await session.send('Emulation.clearDeviceMetricsOverride');
    await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
    return page.evaluate(()=>({inner:[innerWidth,innerHeight],outer:[outerWidth,outerHeight],devicePixelRatio}));
  }finally{await session.detach();}
}
