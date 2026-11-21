import {chooseModelFile} from './workpieceFilePicker.mjs';
import {escapeText as esc,escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';
import {button,empty} from './workspaceControls.mjs';
import {selectTool} from './workspaceModel.mjs';
import {saveCommittedProjectEdit} from './projectEditPersistence.mjs';
import {refreshFixtureLibraryHistory} from './fixtureLibraryHistory.mjs';

const state=view=>view.fiveFixtureLibrary??={open:false,entries:[],names:{},preferencesRevision:'0',defaultId:''};
export function fixtureLibraryField(view,key,value){
  state(view).names[key]=value;
}
export function closeFixtureChooser(context,view){
  state(view).choosing=false;
}
export function renderFixtureLibrary(view){
  const s=state(view);if(!s.open&&!s.choosing)return '';
  const busy=view.pending?'disabled':'',setup=view.scene?.domain?.setups?.find(x=>x.id===view.scene.domain.activeSetupId);
  const selected=s.entries.find(e=>e.id===s.selected)||s.entries[0],canUse=!!setup&&view.scene?.model?.isLoaded&&!setup.locked&&setup.enabled!==false,title=s.choosing?'新增夹具':'工装库';
  const icon=(action,label,path,pressed=false,disabled=false)=>`<button type="button" class="five-library-icon" data-five-command="five.fixture.library.${action}" title="${label}" aria-label="${label}" aria-pressed="${pressed}" ${view.pending||disabled?'disabled':''}><svg viewBox="0 0 24 24" aria-hidden="true"><path d="${path}"/></svg></button>`;
  return `<div data-five-key="fixture-library" class="five-position-dialog five-floating-dialog five-library-dialog" role="dialog" aria-label="${title}"><header class="five-position-title"><strong>${title}</strong>${button('five.fixture.library.close','×',`aria-label="关闭${title}" ${busy}`)}</header>
  <div class="five-library-toolbar">${button('five.fixture.library.import','导入',busy)}<span>${s.entries.length} 套工装</span>${icon('refresh','刷新','M20 7v5h-5 M4 17v-5h5 M19 11a7 7 0 0 0-12-5L4 9 M5 13a7 7 0 0 0 12 5l3-3')}</div>
  <div class="five-position-body">
    <div class="five-library-list" data-five-key="fixture-library-entries">${s.entries.map(e=>`<div class="five-library-row ${selected?.id===e.id?'selected':''}" data-five-key="library-${attr(e.id)}"><svg class="five-library-part-icon" viewBox="0 0 24 24" aria-hidden="true"><path d="m3 7 9-4 9 4-9 4Zm0 0v10l9 4 9-4V7M12 11v10"/></svg><div class="five-library-name" data-five-key="library-name-wrap-${attr(e.id)}">${s.editing===e.id?`<input data-five-library-field="${attr(e.id)}" data-five-key="library-name-${attr(e.id)}" aria-label="工装名称" value="${attr(s.names[e.id]??e.name)}" ${view.pending?'readonly':''}>`:`<button type="button" data-five-command="five.fixture.library.select:${attr(e.id)}" data-five-library-select="${attr(e.id)}" title="双击修改名称" ${busy}>${esc(e.name)}</button>`}<small>${e.partCount} 个部件 · ${{Imported:'导入工装',Generated:'生成工装',Mixed:'组合工装'}[e.source]}</small></div>${icon('default:'+e.id,e.isDefault?'取消默认':'设为默认','m12 3 2.8 5.7 6.2.9-4.5 4.4 1.1 6.2-5.6-3-5.6 3 1.1-6.2L3 9.6l6.2-.9Z',e.isDefault)}${icon('delete:'+e.id,e.isDefault?'请先取消默认再删除':'删除工装','M3 6h18 M9 6V3h6v3 M5 6l1 15h12l1-15 M10 10v7 M14 10v7',false,e.isDefault)}</div>`).join('')||empty('工装库为空，点击“导入”添加。')}</div>
  </div><footer class="five-library-footer"><small>${s.choosing?'选择已有夹具，或导入模型。':'双击名称修改 · ★ 为默认工装'}</small>${button('five.fixture.library.use:'+(selected?.id||''),'添加到当前装夹',view.pending||!canUse||!selected?'disabled':'class="five-primary"')}</footer></div>`;
}
export async function fixtureLibraryCommand(context,view,action,ops){
  const s=state(view),alive=()=>!view.disposed&&context.isCurrentProject?.()!==false;
  if(view.pending||!alive())return;
  if(action.startsWith('select:')){s.selected=action.slice(7);s.editing=null;ops.renderProject(context,view);return;}
  if(action.startsWith('edit-name:')){const id=action.slice(10),e=s.entries.find(e=>e.id===id);if(!e)return;s.editing=id;s.selected=id;s.names[id]=e.name;ops.renderProject(context,view);const input=context.mount.querySelector('[data-five-library-field="'+id+'"]');input?.focus({preventScroll:true});input?.select();return;}
  if(action==='cancel-name'){if(s.editing)delete s.names[s.editing];s.editing=null;ops.renderProject(context,view);return;}
  if(action==='cancel-choose'){s.choosing=false;ops.renderProject(context,view);return;}
  if(action==='close'){
    s.open=false;s.choosing=false;s.editing=null;
    if(!(view.scene?.domain?.fixtureUnits||[]).some(u=>u.setupId===view.scene.domain.activeSetupId))selectTool(view,view.scene?.domain?.activeSetupId?'translate':'select');
    ops.renderProject(context,view);return;
  }
  const choosing=action==='choose'||(s.choosing&&action!=='open');
  if(action==='choose'){s.choosing=true;s.open=false;}else if(!choosing){s.open=true;}
  if(s.open)closeFixtureChooser(context,view);
  if(s.open||s.choosing){selectTool(view,'fixture');}view.fixtureDesignPicking=false;
  if(view.fiveAxisArea!=='operation')await context.actions?.selectRibbonTab?.('operation');
  const refresh=async()=>{const result=await context.sceneProxy.invoke('CamFixture.LibraryList',{}, {timeoutMs:120000});if(alive()){s.entries=result.entries;s.defaultId=result.defaultId;s.preferencesRevision=result.preferencesRevision;}};
  // The file picker owns interaction until a file is chosen. Native import
  // progress starts afterwards so it cannot mask the file picker controls.
  const sourcePath=action==='import'?await chooseModelFile(context,view):null;
  if(action==='import'&&(!sourcePath||!alive()))return;
  view.pending=true;view.error='';ops.renderProject(context,view);
  try{
    if(action==='import'){
      const request={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),setupId:view.scene.domain.activeSetupId,sourcePath};
      let task=await context.sceneProxy.invoke('CamFixture.LibraryImport',request,{timeoutMs:120000});
      while(task.status==='Running'&&alive()){await new Promise(r=>setTimeout(r,100));task=await context.sceneProxy.invoke('CamFixture.QueryTask',{epoch:task.epoch,taskId:task.taskId});}
      if(task.status!=='Completed')throw Error(task.error||'工装导入未完成');
      s.selected=task.result.id;await refresh();
      if(choosing){const entry=task.result;const result=await context.sceneProxy.invoke('CamFixture.LibraryUse',{commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),setupId:view.scene.domain.activeSetupId,entryId:entry.id,revision:entry.revision},{timeoutMs:120000});await saveCommittedProjectEdit(context,view,ops);await ops.refreshSceneState(context,view);if(view.fiveAxisFixtures)view.fiveAxisFixtures.selected=result.unitId;s.choosing=false;selectTool(view,'fixture');view.fiveFixtureDesignOpen=false;}
      ops.appendProjectLog(context,'ok','工装已导入产品工装库。');
    }else if(!['choose','open','refresh'].includes(action)){
      const [kind,id]=action.split(':'),entry=s.entries.find(e=>e.id===id);
      const request={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected)};
      if(!entry)throw Error('工装库条目已变化，请刷新。');
      Object.assign(request,{entryId:id,revision:entry.revision});
      let method;
      if(kind==='use'){s.choosing=false;if(choosing){selectTool(view,'fixture');view.fiveFixtureDesignOpen=false;}method='LibraryUse';request.setupId=view.scene.domain.activeSetupId;}
      else{method='LibraryUpdate';request.action=kind;
        if(kind==='rename'){request.name=(s.names[id]??entry.name).trim();if(!request.name)throw Error('请填写工装名称。');}
        if(kind==='default'){request.entryId=entry.isDefault?'':id;request.preferencesRevision=s.preferencesRevision;}
      }
      let result;
      try{result=await context.sceneProxy.invoke(`CamFixture.${method}`,request,{timeoutMs:120000});}
      catch(error){const receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:request.expected.epoch,commandId:request.commandId});if(receipt.status!=='Completed')throw error;result=receipt.result;}
      if(!alive())return;
      if(kind==='rename'||kind==='delete'){delete s.names[id];s.editing=null;}
      if(kind==='use'){
        s.open=false;
        await saveCommittedProjectEdit(context,view,ops);
        await ops.refreshSceneState(context,view);if(view.fiveAxisFixtures)view.fiveAxisFixtures.selected=result.unitId;
        await ops.fitViewAfterRenderPublish(context,view,{entityIds:[view.scene.activePlacementId,...result.fixtureIds]});
      }
      ops.appendProjectLog(context,'ok',({use:'工装已添加到当前装夹。',rename:'工装库名称已保存。',delete:'已从工装库删除，已选用的工装不受影响。',default:request.entryId?'已设置默认工装，新装夹将自动带入。':'已取消默认工装。'})[kind]);
    }
    await refresh();await refreshFixtureLibraryHistory(context);
  }catch(error){if(alive())view.error=error.message;}
  finally{view.pending=false;if(alive())ops.renderProject(context,view);}
}
