import fs from 'node:fs/promises';
import path from 'node:path';
import {createReadStream} from 'node:fs';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';

// Package persisted results only; never regenerate CAD or run Work algorithms.
const here=path.dirname(fileURLToPath(import.meta.url));
const repo=path.resolve(here,'../../../../..');
if(!process.argv[2])throw Error('Usage: node build-package.mjs <complete-viewer-data-root> [package-directory]');
const dataRoot=path.resolve(process.argv[2]);
const packageRoot=path.resolve(process.argv[3]??path.join(repo,'output/packages/管材Work检查-20261105'));
const site=path.join(packageRoot,'site');
const catalog=JSON.parse(await fs.readFile(path.join(dataRoot,'work-manifest.json'),'utf8'));
const manifest=JSON.parse(await fs.readFile(path.join(dataRoot,'manifest.json'),'utf8'));
const urls=new Set(['/manifest.json','/data/work-manifest.json']);
function references(value){
 if(typeof value==='string'&&(value.startsWith('/data/')||value==='/manifest.json'))urls.add(value);
 else if(Array.isArray(value))value.forEach(references);
 else if(value&&typeof value==='object')Object.values(value).forEach(references);
}
references(manifest);references(catalog);
function dataPath(url){
 const relative=url==='/manifest.json'?'manifest.json':decodeURIComponent(url.slice('/data/'.length));
 const resolved=path.resolve(dataRoot,relative);
 if(!resolved.startsWith(dataRoot+path.sep)||path.isAbsolute(relative))throw Error('Unsafe data URL: '+url);
 return {relative,source:resolved,destination:path.join(site,'data',relative)};
}
const dataEntries=[...urls].sort().map(url=>({url,...dataPath(url)}));
try{await fs.access(packageRoot);throw Error('Refusing to overwrite an existing package: '+packageRoot);}catch(error){if(error.code!=='ENOENT')throw error;}
await fs.mkdir(site,{recursive:true});
const files=[];
function hashFile(file){return new Promise((resolve,reject)=>{const hash=createHash('sha256'),stream=createReadStream(file);stream.on('data',chunk=>hash.update(chunk));stream.on('error',reject);stream.on('end',()=>resolve(hash.digest('hex')));});}
async function copy(source,destination,url=null){
 await fs.mkdir(path.dirname(destination),{recursive:true});
 await fs.copyFile(source,destination);
 const stat=await fs.stat(destination),sha256=await hashFile(destination);
 files.push({path:path.relative(packageRoot,destination).split(path.sep).join('/'),size:stat.size,sha256,...(url?{url}:{})});
}
let next=0;
await Promise.all(Array.from({length:4},async()=>{
 while(next<dataEntries.length){const index=next++,entry=dataEntries[index];await copy(entry.source,entry.destination,entry.url);if((index+1)%1000===0)console.log('Data copied and hashed: '+(index+1)+' / '+dataEntries.length);}
}));
const viewerRoot=path.dirname(here);
for(const name of ['viewer.html','viewer.css','viewer.mjs','workInspectionData.mjs'])await copy(path.join(viewerRoot,name),path.join(site,name));
const three=path.join(repo,'src/iCAX-UI/SDK/ThirdParty/three');
for(const name of ['three.module.js','three.core.js','LICENSE'])await copy(path.join(three,name),path.join(site,'three',name));
const lines=path.join(repo,'src/tools/testing/cut_unit_design_probe/vendor/three-lines');
for(const name of ['LineMaterial.js','LineSegments2.js','LineSegmentsGeometry.js','LICENSE'])await copy(path.join(lines,name),path.join(site,'three-lines',name));
const csc=path.join(process.env.WINDIR??'C:/Windows','Microsoft.NET/Framework64/v4.0.30319/csc.exe');
const executable=path.join(packageRoot,'启动查看.exe');
const compile=spawnSync(csc,['/nologo','/target:winexe','/platform:anycpu','/optimize+','/reference:System.Windows.Forms.dll','/reference:System.Drawing.dll','/out:'+executable,path.join(here,'ViewerLauncher.cs')],{encoding:'utf8',windowsHide:true});
if(compile.status!==0)throw Error('Launcher build failed: '+compile.stdout+'\n'+compile.stderr);
files.push({path:'启动查看.exe',size:(await fs.stat(executable)).size,sha256:await hashFile(executable)});
const instructions=`管材 Work 产出检查（离线 Windows 版）\r\n\r\n1. 先把整个压缩包解压到一个目录，不要在压缩包内直接运行。\r\n2. 双击“启动查看.exe”，程序会自动打开默认浏览器。\r\n3. 启动窗口保持打开；结束查看时，点击“关闭查看服务”或关闭启动窗口。\r\n\r\n包含 ${catalog.models.length} 个模型及导入、Heal、Align、Contour、Classify、EdgeAnalysis、PathAnalysis 七个阶段的已有结果。\r\n在页面上方切换 Work，在模型上点选面或边，右侧查看判定依据；左侧可下载当前阶段的原始文件。\r\n左键旋转，中键或右键拖拽平移，滚轮缩放。\r\n\r\n无需安装 iCAX、Python 或 Node，无需联网。适用于已启用 .NET Framework 4.x 的 Windows 10/11，使用支持 WebGL 的现代浏览器（建议 Edge 或 Chrome）。\r\n启动程序仅为本机浏览器提供文件，不接收局域网连接；端口自动选择。不要单独移动 site 文件夹。\r\n\r\n这是本次计算结果的检查包，不会重新计算加工数据。原有失败、未运行、待处理和预览不完整状态均保留。\r\nBREP/STEP 等文件保持原始完整模型；网页的颜色与线型是检查显示，下载的 BREP 不自带这些显示样式。\r\n包内 sourcePath / provenance 为结果来源记录，不是运行时依赖。\r\n\r\n第三方显示库授权见 site/three/LICENSE 和 site/three-lines/LICENSE。\r\n`;
const instructionsPath=path.join(packageRoot,'使用说明.txt');
await fs.writeFile(instructionsPath,'\uFEFF'+instructions,'utf8');
files.push({path:'使用说明.txt',size:(await fs.stat(instructionsPath)).size,sha256:await hashFile(instructionsPath)});
files.sort((a,b)=>a.path.localeCompare(b.path,'en'));
const stages=Object.fromEntries(catalog.works.map(work=>[work.id,Object.fromEntries(['passed','partial','failed','not-run'].map(status=>[status,catalog.models.filter(model=>model.works[work.id]?.status===status).length]))]));
const report={schema:1,createdAt:new Date().toISOString(),models:catalog.models.length,works:catalog.works,stages,dataUrls:dataEntries.length,files,bytes:files.reduce((sum,file)=>sum+file.size,0)};
await fs.writeFile(path.join(packageRoot,'package-manifest.json'),JSON.stringify(report,null,2)+'\n','utf8');
console.log(JSON.stringify({packageRoot,models:report.models,dataUrls:report.dataUrls,files:files.length+1,bytes:report.bytes,stages},null,2));
