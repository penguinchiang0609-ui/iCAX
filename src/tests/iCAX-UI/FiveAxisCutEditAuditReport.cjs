// Consolidate the actual standalone and native results without rerunning edits.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const{createHash}=require('node:crypto');
const output=path.resolve('output/tests/cut-edit-audit-20261023');
const phases=['core','process','pose','advanced','points','structure','creation','interaction','gizmo','influence','reopen','fresh'];
const read=file=>JSON.parse(fs.readFileSync(path.join(output,file),'utf8'));
const standalone=phases.map(phase=>{const result=read(phase+'-report.json');assert.equal(result.passed,true,phase);assert.ok(!result.error,phase);return{phase,caseCount:result.cases.length,cases:result.cases,report:phase+'-report.json',screenshot:phase+'-final.png'};});
const native=read('native-cut-tests.json');assert.equal(native.failures,0);assert.equal(native.errors,0);assert.equal(native.tests,64);
const tree=read('tree-order-browser/tree-ordering-report.json');assert.equal(tree.passed,true);
const treeMain=read('tree-order-browser/report.json');treeMain.cases=['Native manual and automatic tree ordering, nested groups, locked boundaries, interaction preservation and save/reopen; see tree-ordering-report.json'];fs.writeFileSync(path.join(output,'tree-order-browser/report.json'),JSON.stringify(treeMain,null,2));
const inventory=[
 ['cutParameters.mjs','刀路名称、前指令、后指令','CamCut.Rename / CamCut.SetCommands'],
 ['cutEditing.mjs','保留侧','CamCut.SetRetainedSide'],
 ['processEditing.mjs','补偿五项、连续姿态策略及参数','CamProcess.Update / CamProcess.UpdatePose'],
 ['cutStarting.mjs','实际点选起点','CamProcess.Update (start anchor)'],
 ['cutSplitting.mjs','实际点选打断','CamCut.Split'],
 ['cutReversing.mjs','单条及批量反向','CamCut.Reverse'],
 ['cutClearing.mjs','清除引线、过切、欠切和补偿','CamProcess.Clear'],
 ['cutLeads.mjs','直线/圆弧进出刀、过切、欠切','CamProcess.Update'],
 ['cutTree.mjs','启用、禁用、锁定、解锁、移动、拖动、删除、分组及解组','CamCut.ManageTree'],
 ['toolpathOrdering.mjs','手动确认、前移、后移、自动排序','CamPlan.SetOrder / CamCut.ManageTree'],
 ['standardHoles.mjs','标准孔和轮廓切换','CamCut.SetType'],
 ['extractionActions.mjs','提取结果确认创建','CamCut.Create'],
 ['toolAxisParameters.mjs','整条刀路 IJK、单条及全部姿态反向','CamCut.SetToolAxis / CamProcess.UpdatePose'],
 ['toolAxisPoints.mjs','新增、修改、删除、复位局部关键点，IJK/欧拉角、旋转环、影响范围及曲线','CamCut.EditToolAxisPoint'],
 ['toolpathGeneration.mjs','生成的切割及空移资源','CamPlan.Generate'],
].map(([file,entrances,nativeCommands])=>({file:'src/apps/five-axis-cam/webpage/'+file,entrances,nativeCommands,automaticSave:'saveCommittedCutEdit -> shared serialized project writer'}));
for(const item of inventory){const source=fs.readFileSync(item.file);assert.match(source.toString(),/await saveCommittedCutEdit\(/,item.file);item.sha256=createHash('sha256').update(source).digest('hex');}
const report={passed:true,date:'2026-10-23',scope:'FiveAxisCAM implemented cutting editor mutation entrances',
 authority:'Existing product SDO mutations call Transaction(scene.Database()), whose helper opens BeginUndoCommand and commits a Database transaction. No frontend engineering undo stack.',
 derivedResults:'Generated resources are persisted through the native transaction without adding an extra engineering undo step.',
 historyRefresh:'Clear stale previews, read the authoritative domain and resources, restore valid selection, rebuild necessary render buffers, and save the restored native state.',
 automaticSave:'All listed acknowledged cut mutations call the shared serialized project writer. File failure does not re-submit the native mutation. Retry and manual save write the current native state.',
 drafts:'Unsubmitted invalid input is discarded as a preview; it never creates a synthetic Database transaction or destroys native redo.',
 displayOnly:'Camera, projection mode, notation, coordinate display, selection, tree collapse and unapplied previews do not create engineering history.',
 existingUnsupported:'Unimplemented sketch projection and spatial-lead apply remain unsupported; this audit does not claim new geometry features or machine/controller qualification.',
 inventory,standalone:{passed:true,caseCount:standalone.reduce((n,r)=>n+r.caseCount,0),phases:standalone},
 native:{passed:true,tests:native.tests,report:'native-cut-tests.json'},
 supplementalBrowser:{passed:true,caseGroups:tree.cases.length,report:'tree-order-browser/tree-ordering-report.json',standaloneCef:false},
 relatedPages:read('related-page-close-report.json'),
 verification:['Real standalone CEF controls and mouse picking/dragging','Native Database undo/redo before and after each committed edit','Native parameters and actual rendered geometry/direction buffer equality','Latest focus, selection, canvas/input identity, listeners and nested sidebar scrolls after a delayed native receipt','Actual file-write failure and successful file-only retry','Save/close/reopen with a fresh project view','New native application process reads exactly the saved business values and generated geometry'],
 evidence:{project:'cut-audit.i5cam',savedExpected:'saved-expected.json',freshScreenshot:'fresh-final.png'}};
fs.writeFileSync(path.join(output,'audit-report.json'),JSON.stringify(report,null,2));
console.log(`PASS consolidated cutting audit: ${report.standalone.caseCount} standalone cases, ${native.tests} native tests, ${tree.cases.length} browser groups`);
