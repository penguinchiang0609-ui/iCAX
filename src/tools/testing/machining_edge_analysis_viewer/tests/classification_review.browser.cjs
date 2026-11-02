const fs=require('node:fs');
const path=require('node:path');
const crypto=require('node:crypto');
const assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../../../../..');
const here=path.resolve(__dirname,'..');
const artifacts=path.join(__dirname,'artifacts');
const {chromium}=require(path.join(root,'src/tests/iCAX-UI/node_modules/playwright'));
const hash=value=>crypto.createHash('sha256').update(value).digest('hex');
const fileHash=file=>hash(fs.readFileSync(file));
const geometryHash=data=>hash(JSON.stringify(data.geometry));
const origin='http://127.0.0.1:8769';
const load=async page=>{await page.waitForFunction(()=>window.__edgeAnalysisViewer?.loaded||window.__edgeAnalysisViewer?.error);const state=await page.evaluate(()=>window.__edgeAnalysisViewer);assert.equal(state.loaded,true,state.error);};
(async()=>{
 fs.mkdirSync(artifacts,{recursive:true});
 const sourceFile=path.join(root,'TestData/Tube/EdgeAnalysis/0001.json');
 const sourceBytes=fs.readFileSync(sourceFile);
 const original=JSON.parse(sourceBytes),fixture=structuredClone(original);
 const geometryBefore=geometryHash(fixture),[first,second]=fixture.faces;
 assert.ok(first&&second);
 first.sourceId=second.faceId;
 first.sourceTags=[`classify.sourceFaceIds=${second.faceId}`,'classify.interfaceAnchorSourceFaceIds=81,82','classify.sourceEdgeIds=17,18','classify.sourceCurve3Ids=27,28','classify.interfaceDomain=machining-remainder'];
 second.sourceId='';second.sourceTags=[];
 fixture.classificationReview={machiningFaces:2,nonMachiningFaces:3,
  diagnostics:['Synthetic Classify UI contract fixture; real geometry and edge analysis unchanged'],
  nonMachiningDerivedFaces:[
   {faceId:'90001',sourceId:first.faceId,sourceTags:['classify.interfaceDomain=internal','classify.sourceFaceIds=81,82']},
   {faceId:'90002',sourceId:'source<not-a-current-face>',sourceTags:[
    'classify.zeroAreaTrim=true','classify.zeroAreaSourceFaceId=90002','classify.zeroAreaSourceSplitFaceIndex=350',
    'classify.zeroAreaProof=two-affine-pcurve-uses-retrace-the-same-native-vertex-pair-within-parameter-roundoff',
    'classify.zeroAreaUVBounds=-1.2345678901234567,2,3,4','classify.zeroAreaWireCount=1','classify.zeroAreaCoedgeUseCount=2',
    'classify.zeroAreaUVRoundoffBudget=1.2345678901234567e-34',
    'classify.zeroAreaMaximumEndpointUVGap=9.876543210987654e-35',
    'classify.zeroAreaUVAreaRepresentationBound=1.2345678901234567e-68']}
  ]};
 assert.equal(geometryHash(fixture),geometryBefore);
 const browser=await chromium.launch({channel:'msedge',headless:true});
 try{
  const page=await browser.newPage({viewport:{width:1800,height:1300}});
  const errors=[];page.on('pageerror',error=>errors.push(error.message));
  await page.goto(`${origin}/?data=/data/0001.json`,{waitUntil:'networkidle'});await load(page);
  const initial=await page.evaluate(()=>({styles:window.__edgeAnalysisViewer.getEdgeStyles(),faces:window.__edgeAnalysisViewer.faceCount,segments:window.__edgeAnalysisViewer.segmentCount}));
  assert.equal(await page.locator('.classification-review').count(),0);
  await page.route(url=>url.pathname==='/data/0001.json',route=>route.fulfill({contentType:'application/json',body:JSON.stringify(fixture)}));
  await page.reload({waitUntil:'networkidle'});await load(page);
  const current=await page.evaluate(()=>({styles:window.__edgeAnalysisViewer.getEdgeStyles(),faces:window.__edgeAnalysisViewer.faceCount,segments:window.__edgeAnalysisViewer.segmentCount}));
  assert.deepEqual(current,initial);
  const review=page.locator('.classification-review');
  assert.equal(await review.count(),1);
  const text=await review.innerText();
  for(const value of ['Classify 分类证据 · 外露 1 / 内部 1 / 零裁剪域 1','Classify 加工面','Classify 不加工面','3','内部片和零裁剪域不在当前加工面网格中'])assert.ok(text.includes(value),value);
  await page.locator('.exposed-domains > summary').click();
  await page.locator('.internal-domains > summary').click();
  await page.locator('.zero-trim-domains > summary').click();
  const full=await review.innerText();
  for(const value of ['来源 sourceId',String(second.faceId),'来源原面 ID','接合锚定原面 ID','81,82','17,18','27,28','外露剩余片（加工）','内部接合片（不加工）','零裁剪域证书','两条仿射参数曲线','1.2345678901234567e-34','9.876543210987654e-35','1.2345678901234567e-68','Classify 输出面 ID','分割前面索引不表示导入原模型的祖先面编号','UV 面积表示界不是物理面积阈值','source<not-a-current-face>'])assert.ok(full.includes(value),value);
  assert.equal(await review.locator('[data-face]').count(),1);
  assert.equal(await review.locator('[data-face]').getAttribute('data-face'),String(first.faceId));
  assert.equal(await review.locator('.face-source [data-face]').count(),0);
  assert.equal(await review.locator('[data-face="90001"],[data-face="90002"]').count(),0);
  await page.locator('.zero-trim-certificate').scrollIntoViewIfNeeded();
  await page.screenshot({path:path.join(artifacts,'classification-review-synthetic-certificate.png')});
  await page.locator('.exposed-domains [data-face]').click();
  const selected=await page.evaluate(()=>window.__edgeAnalysisViewer.getSelection());
  assert.equal(selected.face,String(first.faceId));
  const faceDetails=await page.locator('#details > .face-source').innerText();
  assert.ok(faceDetails.includes('来源 sourceId'));
  assert.ok(faceDetails.includes(String(second.faceId)));
  assert.equal(await page.locator('#details > .face-source [data-face]').count(),0);
  assert.ok((await page.locator(`#faceList [data-face="${first.faceId}"]`).innerText()).includes('外露剩余片（加工）'));
  await page.screenshot({path:path.join(artifacts,'classification-review-synthetic-face.png')});
  assert.deepEqual(errors,[]);
  assert.equal(fileHash(sourceFile),hash(sourceBytes));
  assert.equal(geometryHash(fixture),geometryBefore);
  const audit={passed:true,syntheticMetadataFixture:true,realGeometrySource:'TestData/Tube/EdgeAnalysis/0001.json',
   exactPublicDataBefore:hash(sourceBytes),exactPublicDataAfter:fileHash(sourceFile),geometryBefore,geometryAfter:geometryHash(fixture),
   sourceIdIntentionallyEqualsOtherCurrentFaceId:second.faceId,currentFaceId:first.faceId,nonMachiningFaceIds:['90001','90002'],
   sourceIdsArePlainText:true,onlyCurrentFaceIdCreatesNavigation:true,edgeStylesUnchanged:true,
   initialState:initial,certificateText:full,faceDetails,pageErrors:errors};
  fs.writeFileSync(path.join(artifacts,'classification-review-synthetic.json'),JSON.stringify(audit,null,2));
  console.log(JSON.stringify({passed:true,sourceIdNavigationGuard:true,nonMachiningNavigationGuard:true,
   originalGeometryAndEdgeStylesUnchanged:true,faces:initial.faces,segments:initial.segments,pageErrors:errors}));
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
