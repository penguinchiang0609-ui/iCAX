// Exercise the production chooser. Only the OS directory chooser is bypassed;
// directory listing, model preview and final import use the native Scene SDO.
export async function selectWorkpieceFile(page, sourcePath, { preview = false, open = true } = {}) {
  await page.locator('[data-five-command="workpiece.import"]').first().click();
  await selectModelFile(page,sourcePath,{preview,open});
  if(open)await page.evaluate(() => { fixture.fileSelections++; });
}

export async function selectModelFile(page,sourcePath,{preview=false,open=true}={}){
  const path = sourcePath.replaceAll('\\', '/');
  const directory = path.slice(0, path.lastIndexOf('/'));
  const input = page.locator('[data-nesting-file-directory]');
  await page.waitForFunction(()=>{
    const picker=window.fixture.view.fiveAxisFilePicker,list=document.querySelector('.tube-nesting-file-list');
    return !!picker?.directory&&!!list&&!list.hasAttribute('aria-busy');
  });
  await input.fill(directory);
  await input.press('Enter');
  const row = page.locator('[data-nesting-file-is-directory="false"]').filter({ hasText: path.split('/').at(-1) }).first();
  await row.click();
  if (preview) await page.waitForFunction(() => window.fixture.view.fiveAxisFilePicker?.previewStatus === 'ready', null, { timeout: 120000 });
  if (open) {
    await page.locator('footer [data-nesting-file-action="open"]').click();
  }
}
