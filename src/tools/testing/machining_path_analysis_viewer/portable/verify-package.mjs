import fs from 'node:fs/promises';
import path from 'node:path';
import {createReadStream} from 'node:fs';
import {createHash} from 'node:crypto';
import http from 'node:http';

const packageRoot=path.resolve(process.argv[2]);
const origin=process.argv[3];
const output=path.resolve(process.argv[4]??path.join(packageRoot,'../portable-validation.json'));
const report={schema:1,startedAt:new Date().toISOString(),packageRoot,origin,checks:[],errors:[]};
function check(name,passed,detail){report.checks.push({name,passed,...(detail?{detail}:{})});if(!passed)report.errors.push(name);}
function hashFile(file){return new Promise((resolve,reject)=>{const hash=createHash('sha256'),stream=createReadStream(file);stream.on('data',chunk=>hash.update(chunk));stream.on('error',reject);stream.on('end',()=>resolve(hash.digest('hex')));});}
const manifest=JSON.parse(await fs.readFile(path.join(packageRoot,'package-manifest.json'),'utf8'));
const catalog=JSON.parse(await fs.readFile(path.join(packageRoot,'site/data/work-manifest.json'),'utf8'));
check('270 models and seven Work stages',catalog.models.length===270&&catalog.works.length===7);
let hashIndex=0,hashPassed=0;
await Promise.all(Array.from({length:4},async()=>{while(hashIndex<manifest.files.length){const entry=manifest.files[hashIndex++],file=path.join(packageRoot,entry.path);try{const stat=await fs.stat(file),hash=await hashFile(file);if(stat.size!==entry.size||hash!==entry.sha256)report.errors.push('File hash mismatch: '+entry.path);else hashPassed++;}catch(error){report.errors.push(entry.path+': '+error.message);}}}));
check('Every extracted file matches packaged SHA256',hashPassed===manifest.files.length,{passed:hashPassed,total:manifest.files.length});
if(origin){
 const base=new URL(origin);
 function request(route,method='GET'){
  return new Promise((resolve,reject)=>{
   const req=http.request({hostname:base.hostname,port:base.port,path:route,method,agent:false,timeout:15000},response=>{const hash=createHash('sha256');let bytes=0;response.on('data',chunk=>{hash.update(chunk);bytes+=chunk.length;});response.on('error',reject);response.on('end',()=>resolve({status:response.statusCode,headers:response.headers,bytes,sha256:hash.digest('hex')}));});
   req.on('error',reject);req.on('timeout',()=>req.destroy(Error('HTTP timeout: '+route)));req.end();
  });
 }
 const assets=manifest.files.filter(file=>file.url);
 let headIndex=0,headPassed=0;
 await Promise.all(Array.from({length:6},async()=>{while(headIndex<assets.length){const entry=assets[headIndex++];try{const response=await request(entry.url,'HEAD');if(response.status!==200||Number(response.headers['content-length'])!==entry.size||response.bytes!==0)report.errors.push('HEAD mismatch: '+entry.url);else headPassed++;}catch(error){report.errors.push(error.message);}}}));
 check('Every referenced asset resolves over standalone HTTP',headPassed===assets.length,{passed:headPassed,total:assets.length});
 const routes=[['/',manifest.files.find(f=>f.path==='site/viewer.html')],...manifest.files.filter(f=>/^site\/(?:[^/]+\.(?:mjs|css)|three\/.*\.js|three-lines\/.*\.js)$/.test(f.path)).map(f=>['/'+f.path.slice('site/'.length),f])];
 const selected=new Map();
 for(const modelId of [7,13,69,158])for(const entry of Object.values(catalog.models.find(model=>model.id===modelId).works))for(const artifact of entry.artifacts??[])selected.set(artifact.url,assets.find(file=>file.url===artifact.url));
 for(const extension of ['.step','.brep','.json','.igs']){const example=assets.find(file=>file.path.toLowerCase().endsWith(extension));if(example)selected.set(example.url,example);}
 let getPassed=0;
 for(const [route,file] of [...routes,...selected]){if(!file){report.errors.push('Missing selected file: '+route);continue;}const response=await request(route+'?portable-verification=1');if(response.status===200&&response.bytes===file.size&&response.sha256===file.sha256)getPassed++;else report.errors.push('GET mismatch: '+route);}
 check('Browser modules and representative seven-stage downloads match hashes',getPassed===routes.length+selected.size,{passed:getPassed,total:routes.length+selected.size});
 const js=await request('/viewer.mjs','HEAD');check('JavaScript MIME type',/javascript/.test(js.headers['content-type']??''));
 const missing=await request('/__missing_portable_file__','HEAD');check('Missing files return 404',missing.status===404);
 const blocked=[];
 for(const route of ['/../使用说明.txt','/%2e%2e/%E4%BD%BF%E7%94%A8%E8%AF%B4%E6%98%8E.txt','/data/%2e%2e/%2e%2e/使用说明.txt','/data/%5c..%5c..%5c使用说明.txt']){
  const response=await request(encodeURI(route).replaceAll('%252','%2').replaceAll('%255','%5'));
  blocked.push({route,status:response.status});
 }
 check('Traversal paths cannot read outside site',blocked.every(result=>[400,403,404].includes(result.status)),blocked);
 const post=await request('/','POST');check('POST rejected',post.status===405);
}
report.finishedAt=new Date().toISOString();report.status=report.errors.length?'failed':'passed';
await fs.mkdir(path.dirname(output),{recursive:true});await fs.writeFile(output,JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify({status:report.status,checks:report.checks,errors:report.errors,report:output},null,2));
if(report.errors.length)process.exitCode=1;
