// Temporary acceptance host only. Production ribbon policy is unchanged.
// All business requests and resource fetches use the deployed CEF Scene proxy.
export async function installBoundaryAcceptanceHost(partId) {
  const f=window.__boundaryAcceptance;
  if(f.host){f.host.dispose();f.host=null;}
  const mod=name=>import(new URL('../../../apps/tube-designer/webpage/'+name+'.mjs',location.href));
  const [cad,sketch,drawing,drawingDom,drawingPreview,parts,actions,views,sidePreview]=await Promise.all([
    mod('tubeCadEditor'),mod('sketchArea'),mod('partDrawing'),mod('partDrawingDom'),
    mod('partDrawingPreview'),mod('partsArea'),mod('designerActions'),mod('designerViews'),mod('sideSketchPreview')]);
  const {createThreeViewport}=await import(new URL('../../../iCAX-UI/SDK/Viewport/threeViewport.mjs',location.href));
  const nativeList=await f.scene.invoke('TubeDesigner.List',{});
  const mount=document.createElement('div');mount.id='boundary-acceptance-mount';
  mount.className='tube-designer-workspace';
  Object.assign(mount.style,{position:'fixed',inset:'0',zIndex:'100000',background:'#f4f6f8'});
  document.body.append(mount);
  const view={activeAreaId:'nesting',pending:false,scene:{tubeDesigner:nativeList.tubeDesigner},
    tubeDesignerActivePartId:partId,tubeDesignerActiveNestingPartId:partId,
    tubeDesignerNestingSelectedPartIds:[partId],tubeDesignerNestingSelectionKind:'part'};
  const host=f.host={mount,view,pending:0,failures:[],calls:[],viewports:[],patches:0,renders:0};f.view=view;
  const invoke=proxy=>async(method,payload,options)=>{
    const record={method,payload:structuredClone(payload)};host.calls.push(record);
    try {const result=await proxy.invoke(method,payload,options);record.ok=true;return result;}
    catch(error){record.error=error.message;throw error;}
  };
  const context=host.context={mount,sceneProxy:{invoke:invoke(f.scene),resources:f.scene.resources},
    productProxy:{invoke:invoke(f.product),resources:f.product.resources},
    isCurrentProject:()=>!view.disposed&&__icaxAppShell.getState().activeProjectId===f.project.projectId,
    actions:{async selectRibbonTab(id){view.activeAreaId=id;}}};
  const entries=()=>{
    const template=document.createElement('template');template.innerHTML=parts.renderNestingViewportOverlay(context,view);
    return template.content.querySelector('.tube-designer-cutting-scene-actions')?.outerHTML??'';
  };
  const ops=host.ops={appendProjectLog(){},showNotice(){},
    createPartDrawingViewport(options){const viewport=createThreeViewport(options);host.viewport=viewport;host.viewports.push(viewport);return viewport;},
    createSideSketchViewport(options){const viewport=createThreeViewport(options);host.sideViewport=viewport;host.viewports.push(viewport);return viewport;},
    renderProject(){
      const html=cad.renderTubeCadEditor(view)+sketch.renderSectionSketchDialog(context,view)
        +drawing.renderPartDrawingDialog(view)+views.renderDesignerOperationOverlay(context,view);
      if(cad.patchTubeCadEditor(view,mount)||sketch.patchSketchDialogDom(context,view,mount,ops)
        ||drawingDom.patchPartDrawingDom(view,mount,html))host.patches++;
      else {
        host.renders++;mount.innerHTML='<main id="boundary-editor-workbench">'+entries()+'</main>'+html;
        sketch.rememberSketchDialogDom(view,mount,context.sceneProxy);
        drawingDom.rememberPartDrawingDom(view,mount);
      }
      cad.attachTubeCadEditor(context,view,mount,ops);
      sketch.attachSketchAreaInteractions(context,view,mount,ops);
      drawingPreview.attachPartDrawingPreview(context,view,mount,ops);
      drawing.attachPartDrawingEditor(context,view,mount,ops);
    }
  };
  host.dispatch=async(action,target)=>{
    if(!action)return;host.pending++;
    try{return await actions.handleDesignerAreaAction(context,view,action,target,ops);}
    catch(error){host.failures.push({action,error:error.message});throw error;}
    finally{host.pending--;}
  };
  mount.addEventListener('click',event=>{
    const target=event.target.closest('[data-cam-action]');
    if(target&&!target.disabled)void host.dispatch(target.dataset.camAction,target).catch(()=>{});
  });
  mount.addEventListener('change',event=>void host.dispatch(event.target.dataset.camChangeAction,event.target).catch(()=>{}));
  host.dispose=()=>{
    view.disposed=true;cad.closeTubeCadEditor(view,mount);
    drawingPreview.disposePartDrawingPreview(mount);sidePreview.disposeSideSketchPreview(mount);
    for(const viewport of host.viewports)try{viewport.dispose();}catch{}
    mount.remove();
  };
  ops.renderProject();
}
