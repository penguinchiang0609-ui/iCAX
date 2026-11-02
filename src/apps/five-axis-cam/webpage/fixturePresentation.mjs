import {sceneColors} from './scenePalette.mjs';

const isFixture=(view,instance)=>view.scene?.domain?.fixtures?.some(f=>f.id===instance?.objectId);

export function fixtureMeshColor(view,instance){
  if(!isFixture(view,instance))return undefined;
  return sceneColors.fixture*256+(view.fiveAxisArea==='toolpath'?102:255);
}

export function fixturePickable(view,instance){
  return view.fiveAxisArea!=='toolpath'||!isFixture(view,instance);
}

export function syncFixturePresentation(view){
  const viewport=view.viewport;if(!viewport)return;
  // Area switches reuse the viewport and its render resources.
  const previous=view.fiveFixturePresentation;
  if(previous?.viewport===viewport&&previous.area===view.fiveAxisArea)return;
  view.fiveFixturePresentation={viewport,area:view.fiveAxisArea};
  if(view.fiveAxisArea==='toolpath'){
    const ids=new Set((view.scene?.domain?.fixtures||[]).map(f=>f.id));
    if(ids.has(view.selectedSceneObjectId))view.selectedSceneObjectId='';
    const selected=[...viewport.selectedObjectIds].filter(id=>!ids.has(id));
    viewport.setSelectedObjectIds(selected,ids.has(viewport.selectedObjectId)?'':viewport.selectedObjectId);
  }
  viewport.refreshObjectAppearance();
}
