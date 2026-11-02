import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const stageFolders={heal:'Heal',align:'Align',contour:'Coutour',classify:'Classify',edgeAnalysis:'EdgeAnalysis'};
export function stagePlan(target='edge-analysis',merge=false) {
  const definitions={
    align:[['heal','align'],['--align-only','--align-merge-only']],
    contour:[['heal','align','contour'],['--contour-only','--contour-merge-only']],
    classify:[['heal','align','contour','classify'],['--classify-only','--classify-merge-only']],
    'edge-analysis':[['heal','align','contour','classify','edgeAnalysis'],['--part','--part-merge']],
  };
  if(!Object.hasOwn(definitions,target))throw Error('--until must be align, contour, classify or edge-analysis');
  const [stageKeys,modes]=definitions[target];
  return {stageKeys,publishFolders:stageKeys.map(key=>stageFolders[key]),nativeMode:modes[merge?1:0]};
}

export function publishArtifacts(root, staging, group, id, groups, folders = stagePlan().publishFolders) {
  const file=path.basename(group),stem=path.parse(file).name;
  const duplicates=groups.filter(g=>path.parse(path.basename(g)).name.toLowerCase()===stem.toLowerCase());
  const base=duplicates.length>1?`${stem}__${path.extname(file).slice(1).toLowerCase()}`:stem;
  const definitions=[
    ['heal','Heal','.step','.step'],
    ['healData','Heal','.json','.json'],
    ['align','Align','.step','.step'],
    ['contour','Coutour','.step','.step'],
    ['contourData','Coutour','.json','.json'],
    ['contourSvg','Coutour','.svg','.svg'],
    ['rawProjectionSvg','Coutour','.raw.svg','.raw.svg'],
    ['preparedRawProjectionSvg','Coutour','.solid.raw.svg','.solid.raw.svg'],
    ['machining','Classify','-machining.step','.step'],
    ['nonMachining','Classify','-non-machining.step','__非加工面.step'],
    ['split','Classify','-split.step','__裁剪全部面.step'],
    ['edgeAnalysis','EdgeAnalysis','.step','.step'],
    ['edgeAnalysisData','EdgeAnalysis','.json','.json'],
    ['edgeAnalysisBrep','EdgeAnalysis','.brep','.brep'],
    ['edgeAnalysisView','EdgeAnalysis','.html','.html'],
  ];
  const outputs={}, pending=[];
  for(const [key,folder,oldSuffix,newSuffix] of definitions){
    if(!folders.includes(folder))continue;
    const source=path.join(staging,folder,group,id+oldSuffix),target=path.join(root,folder,base+newSuffix);
    // On Windows the old directory "sample.STEP" and new file "sample.step"
    // collide. Move all siblings aside before removing the empty directory.
    const temporary=path.join(root,'Reports/publish-temp',group,folder,id+oldSuffix);
    if(fs.existsSync(source)&&fs.statSync(source).isFile()){
      fs.mkdirSync(path.dirname(temporary),{recursive:true});fs.renameSync(source,temporary);
    }
    pending.push({key,folder,target,temporary,directory:path.dirname(source)});
  }
  for(const {directory} of pending){
    if(fs.existsSync(directory)&&fs.statSync(directory).isDirectory()&&fs.readdirSync(directory).length===0)fs.rmdirSync(directory);
  }
  for(const {key,folder,target,temporary} of pending){
    if(fs.existsSync(temporary)){
      fs.mkdirSync(path.dirname(target),{recursive:true});fs.renameSync(temporary,target);
      outputs[key]=target;
    }else if(staging!==root&&fs.existsSync(target)&&fs.statSync(target).isFile()){
      // A failed rerun must not leave an earlier successful file looking current.
      const previous=path.join(root,'Reports/previous-artifacts',folder,path.basename(target));
      fs.mkdirSync(path.dirname(previous),{recursive:true});fs.renameSync(target,previous);
    }else if(staging===root&&fs.existsSync(target)&&fs.statSync(target).isFile()){
      outputs[key]=target;
    }
  }
  return outputs;
}

// One-time publisher for a running batch started before flat output was selected.
if(process.argv[1]&&import.meta.url===pathToFileURL(path.resolve(process.argv[1])).href){
  const root=path.resolve(process.argv[2]),summaryFile=path.join(root,'Reports/summary.json');
  const done=new Map();
  while(true){
    let summary;try{summary=JSON.parse(fs.readFileSync(summaryFile,'utf8'));}catch{}
    if(summary){
      const groups=summary.sources.map(s=>s.group);
      for(const part of summary.parts){
        const key=part.group+'/'+part.id;
        if(!done.has(key))done.set(key,publishArtifacts(root,root,part.group,part.id,groups));
      }
      fs.writeFileSync(path.join(root,'Reports/flat-output-map.json'),JSON.stringify(Object.fromEntries(done),null,2));
      if(summary.status==='completed'){
        for(const part of summary.parts){part.artifacts=done.get(part.group+'/'+part.id)??{};fs.writeFileSync(part.reportPath,JSON.stringify(part,null,2));}
        fs.writeFileSync(summaryFile,JSON.stringify(summary,null,2));console.log(`Published ${done.size} model results as flat files.`);break;
      }
    }
    await new Promise(r=>setTimeout(r,4000));
  }
}
