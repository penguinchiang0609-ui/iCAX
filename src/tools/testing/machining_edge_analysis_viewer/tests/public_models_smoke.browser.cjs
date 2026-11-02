const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../../../../..'),artifacts=path.join(__dirname,'artifacts');
const {chromium}=require(path.join(root,'src/tests/iCAX-UI/node_modules/playwright'));
const digest=value=>crypto.createHash('sha256').update(value).digest('hex');
const hash=file=>digest(fs.readFileSync(file));
const read=file=>JSON.parse(fs.readFileSync(file,'utf8'));
const colors={OnSkin:'#00b8d9',OffSkin:'#f59e0b',PartialOnSkin:'#a855f7'};
const patterns={Convex:[],Concave:[9,5],Coplanar:[11,4,2,4],Free:[2,4]};
function bounds(data){
 const min=[Infinity,Infinity,Infinity],max=[-Infinity,-Infinity,-Infinity];
 for(const face of data.geometry?.faces??[]){const values=(face.positions??face.vertices??[]).flat();for(let i=0;i<values.length;i+=3)for(let k=0;k<3;k++){min[k]=Math.min(min[k],values[i+k]);max[k]=Math.max(max[k],values[i+k]);}}
 const diagonal=Math.hypot(...max.map((n,k)=>n-min[k]));assert.ok(Number.isFinite(diagonal)&&diagonal>0);
 const tolerance=diagonal*.01+1e-6;let points=0,maxSegment=0;
 for(const edge of data.edges){const source=edge.samples??edge.previewSamples??[];const xyz=typeof source[0]==='number'?Array.from({length:source.length/3},(_,i)=>source.slice(i*3,i*3+3)):source;
  for(let i=0;i<xyz.length;i++){const point=xyz[i];assert.equal(point.length,3);assert.ok(point.every(Number.isFinite));for(let k=0;k<3;k++)assert.ok(point[k]>=min[k]-tolerance&&point[k]<=max[k]+tolerance,`${edge.edgeKey}: out-of-model sample`);points++;if(i)maxSegment=Math.max(maxSegment,Math.hypot(...point.map((n,k)=>n-xyz[i-1][k])));}
 }
 assert.ok(maxSegment<=diagonal+tolerance);
 return {meshMin:min,meshMax:max,diagonalMm:diagonal,checkedEdgeSamplePoints:points,maxSampleSegmentMm:maxSegment,outOfModelSamplePoints:0};
}
(async()=>{
 fs.mkdirSync(artifacts,{recursive:true});
 const ids=(process.argv.slice(2).length?process.argv.slice(2):['247','158','160','246','131','24']);
 const dataRoot=path.join(root,'TestData/Tube/EdgeAnalysis'),manifest=read(path.join(dataRoot,'manifest.json'));
 const browser=await chromium.launch({channel:'msedge',headless:true});
 const checked=[];
 try{
  for(const id of ids){
   const model=manifest.models.find(row=>String(row.id)===id);assert.ok(model);
   const dataUrl=new URL(model.dataUrl,'http://127.0.0.1:8769'),file=path.join(dataRoot,decodeURIComponent(dataUrl.pathname.slice(6))),data=read(file);
   const before=Object.fromEntries(['.json','.brep','.step'].map(ext=>{const name=file.replace(/\.json$/i,ext);return [name,hash(name)];}));
   const geometry=bounds(data),page=await browser.newPage({viewport:{width:1800,height:1200}}),errors=[];
   page.on('pageerror',error=>errors.push(error.message));
   const url=`http://127.0.0.1:8769/?revision=repair-round2-review-20261028#case=${id}`;
   await page.goto(url,{waitUntil:'networkidle'});await page.waitForFunction(()=>window.__edgeAnalysisViewer?.loaded||window.__edgeAnalysisViewer?.error);
   const state=await page.evaluate(()=>({loaded:window.__edgeAnalysisViewer.loaded,error:window.__edgeAnalysisViewer.error,faces:window.__edgeAnalysisViewer.faceCount,segments:window.__edgeAnalysisViewer.segmentCount,rendered:window.__edgeAnalysisViewer.renderedSegmentCount,success:window.__edgeAnalysisViewer.analysisSuccess,complete:window.__edgeAnalysisViewer.complete,conflicts:window.__edgeAnalysisViewer.displayConflicts,styles:window.__edgeAnalysisViewer.getEdgeStyles?.()}));
   assert.equal(state.loaded,true,state.error);assert.equal(state.success,true);assert.equal(state.complete,true);assert.deepEqual(state.conflicts,[]);
   assert.equal(state.faces,data.faces.length);assert.equal(state.segments,data.edges.length);assert.ok(state.rendered>0);
   for(const style of state.styles){assert.equal(style.color,colors[style.skinRelation]);assert.deepEqual(style.pattern,patterns[style.convexity]);assert.equal(style.width,2.4);assert.equal(style.worldUnits,false);}
   await page.locator('#fit').click();
   await page.screenshot({path:path.join(artifacts,`public-case-${id}-whole.png`)});
   const downloads=[];
   for(const link of await page.locator('#modelInfo a').all()){
    const href=await link.getAttribute('href'),response=await page.request.get(href);assert.equal(response.status(),200);
    const bytes=await response.body(),ext=new URL(href,url).pathname.match(/\.(json|brep|step)$/i)?.[0];assert.ok(ext);
    const local=file.replace(/\.json$/i,ext);assert.equal(digest(bytes),hash(local));
    downloads.push({label:await link.innerText(),url:href,bytes:bytes.length,sha256:digest(bytes),status:response.status()});
   }
   assert.equal(downloads.length,3);
   const faceButton=page.locator('#faceList [data-face]').first(),faceId=await faceButton.getAttribute('data-face');
   await faceButton.click();assert.equal((await page.evaluate(()=>window.__edgeAnalysisViewer.getSelection())).face,faceId);
   const edgeButton=page.locator('#details [data-edge]').first(),edgeKey=await edgeButton.getAttribute('data-edge');
   await edgeButton.click();let selected=await page.evaluate(()=>window.__edgeAnalysisViewer.getSelection());assert.equal(selected.edge,edgeKey);
   await page.locator('#fitSelection').click();
   await page.screenshot({path:path.join(artifacts,`public-case-${id}-selected.png`)});
   await page.locator('#clearSelection').click();await page.locator('#fit').click();await page.locator('#xray').check();
   const selectedForPick=state.styles.find(style=>{const edge=data.edges.find(row=>row.edgeKey===style.edgeKey);return (edge?.samples??edge?.previewSamples??[]).length>6;});
   let canvasPick=null;
   if(selectedForPick){
    const edge=data.edges.find(row=>row.edgeKey===selectedForPick.edgeKey),raw=edge.samples??edge.previewSamples;
    const points=typeof raw[0]==='number'?Array.from({length:raw.length/3},(_,i)=>raw.slice(i*3,i*3+3)):raw;
    const rect=await page.locator('#scene').boundingBox();
    for(const fraction of [.45,.55,.35,.65,.25,.75]){
     const index=Math.min(points.length-2,Math.max(0,Math.floor((points.length-1)*fraction))),point=points[index].map((n,k)=>(n+points[index+1][k])/2);
     const screen=await page.evaluate(point=>window.__edgeAnalysisViewer.projectPoint(point),point);
     if(screen.x<0||screen.x>rect.width||screen.y<0||screen.y>rect.height)continue;
     await page.mouse.click(rect.x+screen.x,rect.y+screen.y);
     selected=await page.evaluate(()=>window.__edgeAnalysisViewer.getSelection());
     if(selected.edge){canvasPick={requestedEdgeKey:selectedForPick.edgeKey,selectedEdgeKey:selected.edge,representative:selected.representative,worldPoint:point};break;}
    }
   }
   assert.ok(canvasPick,`case ${id}: canvas edge selection failed`);
   await page.locator('#clearSelection').click();
   const stylesBeforeZoom=await page.evaluate(()=>window.__edgeAnalysisViewer.getEdgeStyles());
   await page.locator('#scene').hover();await page.mouse.wheel(0,-500);
   const stylesAfterZoom=await page.evaluate(()=>window.__edgeAnalysisViewer.getEdgeStyles());assert.deepEqual(stylesAfterZoom,stylesBeforeZoom);
   assert.deepEqual(errors,[]);
   const after=Object.fromEntries(Object.keys(before).map(name=>[name,hash(name)]));assert.deepEqual(after,before);
   checked.push({caseId:id,name:model.name,url,faces:state.faces,segments:state.segments,renderedSegments:state.rendered,
    skinRelations:[...new Set(state.styles.map(s=>s.skinRelation))],convexities:[...new Set(state.styles.map(s=>s.convexity))],
    pixelWidth:2.4,worldUnits:false,zoomDoesNotChangeWidthsOrPatterns:true,geometry,downloads,faceListSelection:faceId,edgeListSelection:edgeKey,canvasPick,sourceFilesBefore:before,sourceFilesAfter:after,pageErrors:errors});
   await page.close();
  }
  const skin=[...new Set(checked.flatMap(row=>row.skinRelations))],convexity=[...new Set(checked.flatMap(row=>row.convexities))];
  assert.deepEqual([...skin].sort(),Object.keys(colors).sort());assert.deepEqual([...convexity].sort(),Object.keys(patterns).sort());
  fs.writeFileSync(path.join(artifacts,'public-models-smoke-real.json'),JSON.stringify({passed:true,publicData:true,checked,combinedSkinRelations:skin,combinedConvexities:convexity,sourceFilesUnchanged:true},null,2));
  console.log(JSON.stringify({passed:true,cases:checked.map(row=>({id:row.caseId,faces:row.faces,segments:row.segments,canvasSelected:row.canvasPick.selectedEdgeKey,downloads:row.downloads.length})),combinedSkinRelations:skin,combinedConvexities:convexity,sourceFilesUnchanged:true}));
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
