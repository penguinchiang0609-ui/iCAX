const fs=require('node:fs');
const path=require('node:path');
const crypto=require('node:crypto');
const assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../../../../..');
const artifacts=path.join(__dirname,'artifacts');
const {chromium}=require(path.join(root,'src/tests/iCAX-UI/node_modules/playwright'));
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8'));
const tag=(face,key)=>(face.sourceTags??[]).find(value=>value.startsWith(`classify.${key}=`))?.slice(key.length+10);
const domain=face=>tag(face,'zeroAreaTrim')==='true'?'zero-area':tag(face,'interfaceDomain')??'';
(async()=>{
 fs.mkdirSync(artifacts,{recursive:true});
 const caseId=String(process.argv[2]??'247');
 const dataRoot=path.join(root,'TestData/Tube/EdgeAnalysis');
 const manifest=read(path.join(dataRoot,'manifest.json'));
 const model=manifest.models.find(row=>String(row.id)===caseId);assert.ok(model,`case ${caseId} not found`);
 const dataUrl=new URL(model.dataUrl,'http://127.0.0.1:8769');
 assert.ok(dataUrl.pathname.startsWith('/data/'));
 const dataFile=path.join(dataRoot,decodeURIComponent(dataUrl.pathname.slice(6)));
 const data=read(dataFile),review=data.classificationReview;
 assert.ok(review,'Newest exporter classificationReview is required for the real-data audit');
 assert.ok(data.faces.every(face=>typeof face.sourceId==='string'&&Array.isArray(face.sourceTags)));
 const exposed=data.faces.filter(face=>domain(face)==='machining-remainder');
 const internal=(review.nonMachiningDerivedFaces??[]).filter(face=>domain(face)==='internal');
 const zero=(review.nonMachiningDerivedFaces??[]).filter(face=>domain(face)==='zero-area');
 const paths=['.json','.brep','.step'].map(ext=>dataFile.replace(/\.json$/i,ext)).filter(file=>fs.existsSync(file));
 const before=Object.fromEntries(paths.map(file=>[file,hash(file)]));
 const browser=await chromium.launch({channel:'msedge',headless:true});
 try{
  const page=await browser.newPage({viewport:{width:1800,height:1300}});
  const errors=[];page.on('pageerror',error=>errors.push(error.message));
  await page.goto(`http://127.0.0.1:8769/?revision=classification-review-20261028#case=${caseId}`,{waitUntil:'networkidle'});
  await page.waitForFunction(()=>window.__edgeAnalysisViewer?.loaded||window.__edgeAnalysisViewer?.error);
  const state=await page.evaluate(()=>({loaded:window.__edgeAnalysisViewer.loaded,error:window.__edgeAnalysisViewer.error,faces:window.__edgeAnalysisViewer.faceCount,segments:window.__edgeAnalysisViewer.segmentCount,styles:window.__edgeAnalysisViewer.getEdgeStyles?.()}));
  assert.equal(state.loaded,true,state.error);assert.equal(state.faces,data.faces.length);
  const block=page.locator('.classification-review');
  assert.equal(await block.count(),1);
  const summary=await block.locator(':scope > summary').innerText();
  assert.ok(summary.includes(`外露 ${exposed.length} / 内部 ${internal.length} / 零裁剪域 ${zero.length}`));
  if(exposed.length)await block.locator('.exposed-domains > summary').click();
  if(internal.length)await block.locator('.internal-domains > summary').click();
  if(zero.length)await block.locator('.zero-trim-domains > summary').click();
  assert.equal(await block.locator('[data-face]').count(),exposed.length);
  const actualTargets=await block.locator('[data-face]').evaluateAll(nodes=>nodes.map(node=>node.dataset.face));
  assert.deepEqual(actualTargets,exposed.map(face=>String(face.faceId)));
  assert.equal(await block.locator('.face-source [data-face]').count(),0);
  assert.equal(await block.locator('.internal-domains [data-face],.zero-trim-domains [data-face]').count(),0);
  const text=await block.innerText();
  for(const face of [...exposed,...internal,...zero]){
   if(face.sourceId)assert.ok(text.includes(face.sourceId));
   for(const key of ['sourceFaceIds','interfaceAnchorSourceFaceIds','sourceEdgeIds','sourceCurve3Ids']){
    const value=tag(face,key);if(value)assert.ok(text.includes(value),`${key}: ${value}`);
   }
  }
  if(zero.length){
   for(const face of zero)for(const key of ['zeroAreaSourceFaceId','zeroAreaSourceSplitFaceIndex','zeroAreaUVBounds','zeroAreaWireCount','zeroAreaCoedgeUseCount','zeroAreaUVRoundoffBudget','zeroAreaMaximumEndpointUVGap','zeroAreaUVAreaRepresentationBound']){
    const value=tag(face,key);assert.notEqual(value,undefined,`${key} missing`);assert.ok(text.includes(value),`${key}: ${value}`);
   }
   assert.ok(text.includes('UV 面积表示界不是物理面积阈值'));
   assert.ok(text.includes('分割前面索引不表示导入原模型的祖先面编号'));
   await block.locator('.zero-trim-certificate').first().scrollIntoViewIfNeeded();
  }else await block.scrollIntoViewIfNeeded();
  await page.screenshot({path:path.join(artifacts,`classification-review-real-${caseId}.png`)});
  const selections=[];
  for(const face of exposed){
   await page.evaluate(id=>window.__edgeAnalysisViewer.selectFace(id),String(face.faceId));
   const selected=await page.evaluate(()=>window.__edgeAnalysisViewer.getSelection());
   assert.equal(selected.face,String(face.faceId));
   const source=page.locator('#details > .face-source');
   assert.equal(await source.locator('[data-face]').count(),0);
   const content=await source.innerText();
   assert.ok(content.includes(face.sourceId||'未记录'));
   assert.ok(content.includes('外露剩余片（加工）'));
   selections.push({currentFaceId:face.faceId,sourceId:face.sourceId,text:content});
  }
  if(exposed.length)await page.screenshot({path:path.join(artifacts,`classification-review-real-${caseId}-selected.png`)});
  await page.evaluate(()=>window.__edgeAnalysisViewer.selectFace(''));
  assert.deepEqual(await page.evaluate(()=>window.__edgeAnalysisViewer.getEdgeStyles()),state.styles);
  assert.deepEqual(errors,[]);
  const after=Object.fromEntries(paths.map(file=>[file,hash(file)]));assert.deepEqual(after,before);
  const audit={passed:true,publicData:true,caseId,name:model.name,faces:data.faces.length,segments:state.segments,
   classificationCounts:{machining:review.machiningFaces,nonMachining:review.nonMachiningFaces,exposed:exposed.length,internal:internal.length,zeroTrim:zero.length},
   sourceIdsArePlainText:true,onlyCurrentFaceIdCreatesNavigation:true,nonMachiningRecordsCreateNoFaceNavigation:true,
   classificationReview:review,selectedExposedFaces:selections,edgeStylesUnchanged:true,filesBefore:before,filesAfter:after,pageErrors:errors};
  fs.writeFileSync(path.join(artifacts,`classification-review-real-${caseId}.json`),JSON.stringify(audit,null,2));
  console.log(JSON.stringify({passed:true,caseId,name:model.name,faces:data.faces.length,segments:state.segments,
   classificationCounts:audit.classificationCounts,geometryFilesUnchanged:true,pageErrors:errors}));
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
