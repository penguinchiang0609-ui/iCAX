import {refreshConstructionSamples} from './rapidConstructionEditing.mjs';

export const isRapidEndpoint=(edit,index=edit.selected)=>index===0||index===edit.points.length-1;
export function updateRapidEndpoints(edit,indices=[edit.selected]){
 let changed=false;
 for(const index of indices){if(!isRapidEndpoint(edit,index))continue;
  const key=index===0?'start':'end',point=[...edit.points[index]],direction=[...edit.directions[index]];
  (edit.endpointOverrides??={})[key]={point,direction};changed=true;
  if(edit.construction){const height=edit.construction.followHeightMm;
   edit[index===0?'sourceStart':'sourceEnd']={point:point.map((v,k)=>v-direction[k]*height),direction};}
 }
 if(changed&&edit.construction){refreshConstructionSamples(edit);
  edit.samplePoints[0]=[...edit.points[0]];edit.samplePoints[edit.samplePoints.length-1]=[...edit.points.at(-1)];
  edit.points[1]=edit.construction.turnPoint?[...edit.construction.turnPoint]:[...edit.samplePoints[Math.floor(edit.samplePoints.length/2)]];
 }
}
