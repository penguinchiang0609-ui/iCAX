// Regenerate browser modules from the product schemas and the repository's
// official FlatBuffers runtime. Temporary compiler output stays on C:.
import {mkdtemp,readFile,readdir,mkdir,writeFile,copyFile,rm} from 'node:fs/promises';
import {spawnSync} from 'node:child_process';
import {stripTypeScriptTypes} from 'node:module';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const here=path.dirname(fileURLToPath(import.meta.url));
const repo=path.resolve(here,'../../../..');
const flatc=process.env.ICAX_FLATC||path.join(repo,'.codex_tmp/flatbuffers-v25.12.19/build/Release/flatc.exe');
const runtime=path.join(repo,'src/third_party/flatbuffers/flatbuffers-25.12.19');
const temporary=await mkdtemp(path.join(tmpdir(),'icax-machine-ik-protocol-'));
const generated=path.join(here,'generated');
const runtimeOut=path.join(here,'runtime');
const transpile=source=>stripTypeScriptTypes(source,{mode:'transform'}).replace(/(from\s+['"]\.\.?\/[^'"]+)\.js(['"])/g,'$1.mjs$2');
try{
 const names=['MachineIKTarget','MachineIKResult','MachineJogControl','MachineJogResult','TaskProgress'];
 const schemas=names.map(name=>path.join(here,name+'.fbs'));
 const result=spawnSync(flatc,['--ts','-o',temporary,...schemas],{encoding:'utf8'});
 if(result.status!==0)throw Error(result.stderr||result.stdout||'flatc did not complete');
 await mkdir(generated,{recursive:true});await mkdir(runtimeOut,{recursive:true});
 const modules=['constants','types','utils','encoding','byte-buffer','builder','flatbuffers'];
 for(const name of modules){
  let source=await readFile(path.join(runtime,'ts',name+'.ts'),'utf8');
  // These declarations are type-only in the upstream runtime. Marking them
  // explicitly lets the syntax-only Node transpiler erase them correctly.
  source=source.replace(/import (\{[^}]+\}) from '\.\/types\.js';/g,"import type $1 from './types.js';")
   .replace(/export (\{[^}]+\}) from '\.\/types\.js';/g,"export type $1 from './types.js';");
  await writeFile(path.join(runtimeOut,name+'.mjs'),'// Official FlatBuffers 25.12.19 runtime, transpiled by generate.mjs.\n'+transpile(source));
 }
 await copyFile(path.join(runtime,'LICENSE'),path.join(runtimeOut,'LICENSE.txt'));
 async function find(folder,name){for(const item of await readdir(folder,{withFileTypes:true})){const p=path.join(folder,item.name);if(item.isDirectory()){const found=await find(p,name);if(found)return found;}else if(item.name===name)return p;}return null;}
 for(const name of ['machine-iktarget','machine-ikresult','machine-collision-collider','machine-collision-pair','machine-jog-control','machine-jog-result','task-progress']){
  const input=await find(temporary,name+'.ts');if(!input)throw Error('flatc omitted '+name);
  const source=(await readFile(input,'utf8')).replace("from 'flatbuffers'","from '../runtime/flatbuffers.mjs'")
   .replace(/from (['"])(\.\.[^'"]*\/)([^/'"]+)\.js\1/g,"from './$3.mjs'");
  await writeFile(path.join(generated,name+'.mjs'),transpile(source));
 }
}finally{
 if(path.dirname(path.resolve(temporary))!==path.resolve(tmpdir()))throw Error('Temporary compiler path is outside the intended directory');
 await rm(temporary,{recursive:true,force:true});
}
