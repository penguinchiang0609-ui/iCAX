// Fresh current-source acceptance; no archived prefix or segmented report is accepted.
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createCachedNeutralInspectionPool } from './CachedNeutralInspectionPool.mjs';
import { inspectManufacturedShapes, compareBaselineShapes } from './productManufacturingShapeAssertions.mjs';

const repository = resolve(fileURLToPath(new URL('../../../', import.meta.url)));
const output = resolve(process.env.ICAX_NEW_PRODUCT_ACCEPTANCE_OUTPUT || resolve(repository, 'output/tests/new-product-cleanup-current-connections-20261003'));
mkdirSync(output, { recursive: true });
const runtimeRoot = resolve(process.env.ICAX_NEW_PRODUCT_RUNTIME_ROOT || resolve(repository, 'tmp/new-product-cleanup/runtime'));
const bridgePath = resolve(process.env.ICAX_NEW_PRODUCT_BRIDGE || resolve(runtimeRoot, 'SecurityWindowFramesBridge.exe'));
const python = process.env.ICAX_NEW_PRODUCT_PYTHON || 'C:\\Users\\pengu\\.cache\\codex-runtimes\\codex-primary-runtime\\dependencies\\python\\python.exe';
const inspector = 'C:\\Program Files\\PowerShell\\7\\pwsh.exe';
const sourceWorker = resolve(repository, 'src/tests/icax-plugins/product/TubeDesigner/TubeDesignerTest/NewProductWindowGuardrailSource.py');
const manifestFile = resolve(output, 'NewProductWindowGuardrail-input-manifest.json');
const selectedCaseIndex=process.argv.indexOf('--case');
const selectedCase=selectedCaseIndex<0 ? null : process.argv[selectedCaseIndex+1];
assert.ok(selectedCaseIndex<0 || selectedCase,'--case requires an explicit current case name');
const sourceReportFile = resolve(output, selectedCase ? 'NewProductWindowGuardrail-source-first-case.json' : 'NewProductWindowGuardrail-source.json');
const buildArtifactsFile = resolve(output, 'build-artifacts.json');
for (let index=2;index<process.argv.length;index++) {
  assert.equal(process.argv[index],'--case','Only a fresh full run or --case CASE_NAME is supported');
  assert.ok(process.argv[++index]&&!process.argv[index].startsWith('--'),'--case requires a case name');
}
const reportFile = resolve(output, selectedCase ? 'NewProductWindowGuardrail-native-first-case.json' : 'NewProductWindowGuardrail-native.json');
const read = path => JSON.parse(readFileSync(path, 'utf8'));
const hash = path => createHash('sha256').update(readFileSync(path)).digest('hex');
const write = (path, value) => writeFileSync(path, JSON.stringify(value, null, 2) + '\n');
const manifest = read(manifestFile), sourceReport = read(sourceReportFile);
const selectedInputs=manifest.cases.filter(row=>!selectedCase || row.name===selectedCase);
assert.ok(selectedInputs.length && (!selectedCase || selectedInputs.length===1));
assert.equal(process.arch, 'x64');
assert.equal(manifest.cases.length, 89);
assert.deepEqual({validBranches:manifest.coverage.validBranches, allowedLibraryCases:manifest.coverage.allowedLibraryCases,
  profileRejections:manifest.coverage.profileRejections, actualNativeSceneCases:manifest.coverage.actualNativeSceneCases,
  pointsPerEntity:manifest.coverage.pointsPerEntity},
  {validBranches:72,allowedLibraryCases:2,profileRejections:15,actualNativeSceneCases:16,pointsPerEntity:343});

assert.equal(sourceReport.passed, true, 'Every selected current source case must pass first');
assert.equal(sourceReport.cases.length,selectedInputs.length);
assert.ok(sourceReport.cases.every(row=>row.passed));
assert.deepEqual(sourceReport.cases.map(row=>row.name),selectedInputs.map(row=>row.name));
assert.equal(sourceReport.limitedCase,selectedCase);
assert.equal(sourceReport.sourceBeforeAfterUnchanged,true);
assert.equal(sourceReport.nativeExecuted, false);
assert.deepEqual(sourceReport.sourceDigests, manifest.sourceDigests);

assert.ok(existsSync(bridgePath));

// Bounded exact comparison never formats whole CAD documents on failure.
function firstDiff(actual, expected, path = '$') {
  if (Object.is(actual, expected)) return null;
  if (typeof actual !== typeof expected || actual === null || expected === null || typeof actual !== 'object')
    return { path, actual: String(actual).slice(0, 200), expected: String(expected).slice(0, 200) };
  if (Array.isArray(actual) !== Array.isArray(expected)) return { path, kind: 'container' };
  const a = Object.keys(actual), b = Object.keys(expected);
  if (a.length !== b.length) return { path, kind: 'key-count', actual: a.length, expected: b.length };
  for (const key of b) {
    if (!Object.hasOwn(actual, key)) return { path: `${path}.${key}`, kind: 'missing-key' };
    const diff = firstDiff(actual[key], expected[key], `${path}.${key}`); if (diff) return diff;
  }
  return null;
}
function exact(actual, expected, label) {
  const diff = firstDiff(actual, expected); if (diff) throw new Error(`${label}: ${JSON.stringify(diff)}`);
}
const captureEncoding = value => JSON.parse(JSON.stringify(value));
function checkSampleCoverage(proof,label) {
  assert.ok(Number.isInteger(proof.parts)&&proof.parts>0,label);
  const samples=proof.parts*343;
  for(const side of ['baseline','actual']) {
    const controls=proof.classificationReuseControls?.[side];
    assert.equal(controls?.available,true,`${label} ${side} actual classifier evidence`);
    assert.equal(controls.solids,proof.parts,`${label} ${side} every entity sampled`);
    assert.equal(controls.points,samples,`${label} ${side} all 343 samples per entity`);
    assert.equal(controls.everyEncounteredStateComparedWithFreshClassifier,true,`${label} ${side} fresh classifier control`);
  }
  assert.ok(Number.isInteger(proof.checkedPoints)&&proof.checkedPoints>0&&proof.checkedPoints<=samples,
    `${label} non-boundary comparable point count`);
  assert.ok(proof.interiorPoints>0&&proof.interiorPoints<=proof.checkedPoints,`${label} positive interior comparison`);
  assert.equal(proof.volumesAndBoundsEqual,true);assert.equal(proof.solidClassificationsEqual,true);
  return {sampledPointsPerSide:samples,comparableNonBoundaryPoints:proof.checkedPoints,
    excludedBecauseEitherSideBoundary:samples-proof.checkedPoints};
}
const frozen = new Map();
const freeze = path => { const sha256 = hash(path); frozen.set(path, sha256); return { path, sha256 }; };
const deployedPath = source => source.startsWith('iCAX-Engine/framework/TemplateRuntime/python/')
  ? resolve(runtimeRoot, 'runtime/template-python', source.slice('iCAX-Engine/framework/TemplateRuntime/python/'.length))
  : resolve(runtimeRoot, source);
for (const [source, sha256] of Object.entries(manifest.sourceDigests)) {
  assert.equal(hash(resolve(repository, 'src', source)), sha256, `Current source: ${source}`);
  assert.equal(hash(deployedPath(source)), sha256, `Actual deployed source: ${source}`);
  frozen.set(resolve(repository, 'src', source), sha256); frozen.set(deployedPath(source), sha256);
}
const modules = ['TemplateRuntime.dll', 'TubeDesigner.dll', 'SDO.dll', 'ProjectFile.dll'].map(file =>
  ({ file, ...freeze(resolve(runtimeRoot, file)) }));
modules.push({file:'python312.dll',...freeze(resolve(runtimeRoot,'runtime/python/python312.dll'))});
const deploymentFiles = ['Product.dll','ApplicationContext.dll','ExtrusionRecognition.dll','TubeDesigner.exe'].map(file=>freeze(resolve(runtimeRoot,file)));

  const buildArtifacts=read(buildArtifactsFile);
  assert.equal(buildArtifacts.passed,true,'Final independently recorded build checks must pass');
  const builtRuntimeFiles=[...modules.filter(row=>!['SDO.dll','python312.dll'].includes(row.file)),...deploymentFiles];
  assert.equal(builtRuntimeFiles.length,7);
  for(const runtimeFile of builtRuntimeFiles) {
    const built=buildArtifacts.files.find(row=>resolve(row.path).toLowerCase()===resolve(runtimeFile.path).toLowerCase());
    assert.ok(built,'Independent build evidence must bind each of seven actual runtime binaries');
    assert.equal(runtimeFile.sha256,built.sha256,runtimeFile.path);
  }

const artifacts = [manifestFile, sourceReportFile,buildArtifactsFile, bridgePath, sourceWorker, fileURLToPath(import.meta.url),
  resolve(repository,'src/tests/icax-plugins/product/TubeDesigner/TubeDesignerTest/NewProductWindowGuardrailFinishedReference.py'),
  resolve(repository,'src/tests/icax-plugins/product/TubeDesigner/TubeDesignerTest/ProductManufacturingDeclarationTests.py'),
  resolve(repository,'src/tests/icax-plugins/product/TubeDesigner/TubeDesignerTest/WindowManufacturingInputTests.py'),
  fileURLToPath(new URL('./CachedNeutralInspectionPool.mjs', import.meta.url)),
  fileURLToPath(new URL('./productManufacturingShapeAssertions.mjs', import.meta.url))].map(freeze);
function unchanged() {
  for (const [path, digest] of frozen) assert.equal(hash(path), digest, `Frozen artifact changed: ${path}`);
}
function loadedModules(worker, caseName, requirePython = false) {
  const script = `[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new();
    $taskNativeProcess = Get-Process -Id ${worker.pid} -ErrorAction Stop;
    $taskNativeModules = @(Get-Process -Id ${worker.pid} -Module -ErrorAction Stop);
    [PSCustomObject]@{ processId=$taskNativeProcess.Id; processPath=$taskNativeProcess.Path;
      capturedAt=[DateTime]::UtcNow.ToString('o'); inspectorIs64Bit=[Environment]::Is64BitProcess;
      allLoadedModuleNames=@($taskNativeModules | ForEach-Object {$_.ModuleName});
      modules=@($taskNativeModules | Where-Object {$_.ModuleName -in @('TemplateRuntime.dll','TubeDesigner.dll','SDO.dll','ProjectFile.dll','python312.dll')} |
        ForEach-Object {[PSCustomObject]@{file=$_.ModuleName;actualPath=$_.FileName;
          actualSha256=(Get-FileHash -LiteralPath $_.FileName -Algorithm SHA256).Hash.ToLowerInvariant()}})
    } | ConvertTo-Json -Depth 5 -Compress`;
  const run = spawnSync(inspector, ['-NoProfile','-NonInteractive','-Command',script],
    { encoding:'utf8', windowsHide:true, timeout:30000, maxBuffer:1024*1024 });
  assert.equal(run.status, 0, run.stderr?.slice(0, 1500));
  const actual = JSON.parse(run.stdout.trim());
  assert.equal(actual.processId, worker.pid); assert.equal(actual.inspectorIs64Bit, true);
  assert.equal(resolve(actual.processPath).toLowerCase(), bridgePath.toLowerCase());
  assert.equal(hash(actual.processPath), hash(bridgePath));
  const required = modules.filter(row=>row.file!=='python312.dll'||requirePython).map(row=>row.file);
  for(const file of required)assert.ok(actual.modules.some(row=>row.file===file),`Required actual loaded module missing: ${file}`);
  for (const row of actual.modules) {
    const expected = modules.find(module => module.file === row.file);
    assert.equal(row.actualSha256, expected.sha256, row.actualPath);
    assert.equal(resolve(row.actualPath).toLowerCase(),resolve(expected.path).toLowerCase(),
      'Actual loaded DLL path must be the isolated frozen runtime file');
    Object.assign(row, { frozenRuntimePath: expected.path, frozenRuntimeSha256: expected.sha256 });
  }
  return { caseName, workerIndex: worker.workerIndex, pythonRequiredForActualProductCall:requirePython,
    pythonModuleActuallyLoaded:actual.modules.some(row=>row.file==='python312.dll'), ...actual };
}

function referenceFor(caseInput, descriptor) {
  const requestFile = resolve(output, `${caseInput.name}-native-host-input.json`);
  const referenceFile = resolve(output, `${caseInput.name}-native-source-reference.json`);
  write(requestFile, { ...caseInput, template: descriptor });
  const run = spawnSync(python, [sourceWorker, '--reference', requestFile, referenceFile],
    { cwd:repository, encoding:'utf8', windowsHide:true, timeout:300000,
      maxBuffer:1024*1024, env:{...process.env,PYTHONIOENCODING:'utf-8'} });
  assert.equal(run.status, 0, run.stderr?.slice(0, 3000));
  const evidence = [requestFile, referenceFile].map(freeze);
  return { reference: read(referenceFile), evidence };
}
function checkPlan(plan, reference, parameters) {
  exact(plan.parameters, parameters, 'Native plan parameters');
  assert.equal(plan.partCount, reference.partFacts.parts);
  const rows = plan.tables.flatMap(table => table.rows), items = reference.document.items;
  assert.equal(rows.length, items.length);
  assert.deepEqual(rows.map(row => row.itemKey).sort(), items.map(item => item.key).sort());
  let lengths = 0;
  for (const item of items) {
    const row = rows.find(candidate => candidate.itemKey === item.key), p = item.properties;
    if (typeof p.length === 'number') { assert.ok(Math.abs(Number(row.values.length)-p.length)<=0.001,item.key); lengths++; }
    if (typeof p.quantity === 'number') assert.equal(row.values.quantity,p.quantity,item.key);
    const material = p['manufacturing.materialGrade'] || p['manufacturing.material'] || p.materialGrade || p.material;
    if (material) assert.equal(row.values.materialGrade || row.values.material,material,item.key);
  }
  assert.equal(lengths, reference.partFacts.checkedLengths);
  return {parts:items.length, checkedLengths:lengths, identityMaterialQuantityLengthEqual:true};
}
const report = { schema:'icax.new-product-window-guardrail-native',schemaVersion:1,
  startedAt:new Date().toISOString(), runtimeRoot,bridgePath,artifacts,modules,deploymentFiles,
  defaultOnly:false,selectedCase,singleRun:true,fullAcceptanceAsserted:false,
  validationScope:selectedCase ? `One explicit current case ${selectedCase}; no full five-product acceptance` : '10 current default descriptor previews; 72 five-product branches + 2 library positives + 15 profile rejects; 16 real generate/disassemble scenes, including two three-face U post connectors, 343 points per entity, two save/reopens',
  sourceCaptureComparison:'Both operands use JSON capture encoding solely for frozen source-file comparisons; actual Native inputs, returns and live persistence snapshots retain Object.is numeric equality',
  defaultDescriptorPreviews:[], cases:[],passed:false, actualLoadedModules:[] };
const save = () => write(reportFile,report);
let pool;
function createPool(caseName, full = false) {
  pool = createCachedNeutralInspectionPool({ bridgePath,runtimeRoot,workerCount:full?4:1,timeoutMs:600000,
    onEvent:event => console.log(JSON.stringify({ caseName,...event })) });
  console.log(JSON.stringify({caseName,bridgePath,workers:pool.workerMetadata}));
  return pool;
}
try {
  // Each active descriptor changed in cleanup receives its own actual host call.
  const productRoot = resolve(repository,'src/apps/tube-designer/templates/product');
  const descriptors = readdirSync(productRoot,{withFileTypes:true}).filter(row=>row.isDirectory()
    && existsSync(resolve(productRoot,row.name,'template.json'))).map(row=>read(resolve(productRoot,row.name,'template.json')));
  assert.equal(descriptors.length,10);
  if (!selectedCase) createPool('10-default-descriptor-previews');
  for (const declaration of selectedCase ? [] : descriptors) {
    const descriptor = (await pool.invoke('GetTemplateDescriptor',{templateId:declaration.id})).template;
    const values = Object.fromEntries(declaration.parameters.map(field=>[field.key,field.defaultValue]));
    const input = structuredClone(values);
    const preview = await pool.invoke('GenerateProductTemplatePreview',{templateId:declaration.id,parameters:values});
    exact(values,input,'Default input unchanged');exact(preview.parameters,input,'Default preview parameters');
    assert.equal(preview.templateId,declaration.id);assert.ok(preview.items.length);
    const plan = await pool.invoke('GetProductManufacturingPlan',{templateId:declaration.id,parameters:values});
    const planRuleRejection=undefined;
    exact(values,input,'Default plan input unchanged');
    if(plan){exact(plan.parameters,input,'Default plan parameters');assert.ok(plan.partCount>0);}
    report.defaultDescriptorPreviews.push({templateId:declaration.id,passed:true,parameters:input,
      nativeDescriptorSha256:createHash('sha256').update(JSON.stringify(descriptor)).digest('hex'),
      previewItems:preview.items.length,manufacturingParts:plan?.partCount,
      nativePreviewAccepted:true,nativePlanAccepted:!!plan,
      currentProductRuleRejection:planRuleRejection,nativePlanAttempted:true});save();
  }
  if (!selectedCase) {
    report.actualLoadedModules.push(loadedModules(pool.workerMetadata[0],'10-default-descriptor-previews',true));
    await pool.close();pool=null;
  }
  for (const caseInput of selectedInputs) {
    unchanged();
    const row={name:caseInput.name,expected:caseInput.expected,fullNativeScene:caseInput.fullNativeScene,passed:false};
    report.cases.push(row);save();
    createPool(caseInput.name,caseInput.fullNativeScene);
    try {
      const descriptor=(await pool.invoke('GetTemplateDescriptor',{templateId:caseInput.templateId})).template;
      const input=structuredClone(caseInput.parameters), original=structuredClone(input);
      if(caseInput.expected==='reject') {
        row.actualRejections=[];
        for(const method of ['GenerateProductTemplatePreview','GetProductManufacturingPlan']) {
          let rejection;
          try {await pool.invoke(method,{templateId:caseInput.templateId,parameters:input});}
          catch(error){rejection=error.message;}
          assert.ok(rejection && /profile section kind is not applicable to role|unsupported product profile (snapshot schema|resource kind)|profile override.*sectionKind/i.test(rejection),`${method}: intended profile admission rejection required`);
          row.actualRejections.push({method,error:rejection});exact(input,original,'Rejected input unchanged');
        }
        row.parametersUnchanged=true;
      } else {
        const {reference,evidence}=referenceFor(caseInput,descriptor); row.sourceReference=evidence;
        const preview=await pool.invoke('GenerateProductTemplatePreview',{templateId:caseInput.templateId,parameters:input});
        exact(preview.parameters,original,'Native preview parameters');assert.equal(preview.templateId,caseInput.templateId);
        assert.deepEqual(preview.items.map(item=>item.key).sort(),reference.display.items.map(item=>item.key).sort());
        const plan=await pool.invoke('GetProductManufacturingPlan',{templateId:caseInput.templateId,parameters:input});
        row.partFacts=checkPlan(plan,reference,original); exact(input,original,'Accepted host input unchanged');
        row.nativePreviewAndPlanningAccepted=true;row.parametersUnchanged=true;
        if(caseInput.inactiveOverrideControl || caseInput.inactiveParameterControl) row.inactiveDraftAcceptedWithExactParameterEcho=true;
        if(caseInput.fullNativeScene) {
          const generated=await pool.invoke('GeneratePreview',{templateId:caseInput.templateId,...input});
          const before=await pool.invoke('InspectScriptResources');
          exact(before.generatedParameters,original,'Generated parameters');
          exact(captureEncoding(before.display.document),captureEncoding(reference.display),'Fresh source display');
          await pool.invoke('Disassemble',{productEntityIds:[generated.tubeDesigner.product.entityId]});
          const snapshot=await pool.invoke('InspectScriptResources');
          exact(snapshot.manufacturingParameters,original,'Disassembled parameters');
          exact(captureEncoding(snapshot.manufacturing.document),captureEncoding(reference.manufacturing),'Fresh source manufacturing declaration');
          exact(snapshot.manufacturingExecution.document.parameters,original,'Execution product parameter echo');
          const rawExecution=structuredClone(snapshot.manufacturingExecution.document);
          const rawSource=reference.document;
          exact(rawExecution.template,rawSource.template,'Raw execution source template identity');
          assert.equal(rawExecution.template.packageDigest,descriptor.packageDigest,'Actual current host package digest');
          assert.equal(Object.hasOwn(rawSource.extensions,'tubeDesigner.assemblyProcessNative'),false);
          exact(captureEncoding(rawExecution.extensions['tubeDesigner.componentSnapshots'] || {}),
            captureEncoding(rawSource.extensions['tubeDesigner.componentSnapshots'] || {}),'Independent source resolved component snapshots');
          delete rawExecution.extensions['tubeDesigner.assemblyProcessNative'];
          if(!Object.hasOwn(rawSource.extensions,'tubeDesigner.componentSnapshots'))delete rawExecution.extensions['tubeDesigner.componentSnapshots'];
          exact(captureEncoding(rawExecution),captureEncoding(rawSource),'Whole source raw execution document before Native machining');
          row.rawExecutionDocumentExactlyMatchesIndependentSource=true;
          row.currentHostPackageDigestBound=descriptor.packageDigest;
          if(reference.sourceAssemblyPlan) {
            const actualPlan=structuredClone(snapshot.manufacturingExecution.document.extensions['tubeDesigner.assemblyProcessNative'].plan);
            row.actualNativeContactChecks=actualPlan.nativeContactChecks;
            assert.ok(Array.isArray(actualPlan.nativeContactChecks),'Actual Native contact check record required');
            delete actualPlan.nativeContactChecks;
            exact(captureEncoding(actualPlan),captureEncoding(reference.sourceAssemblyPlan),'Independent source-compiled current assembly plan');
            row.sourceCompiledAssemblyPlanExactlyMatchesNative=true;
            if(reference.finishedReferenceProof.foldedPostConnections) {
              const proof=reference.finishedReferenceProof.foldedPostConnections;
              assert.ok(['insert','tabs'].includes(proof.joint));
              assert.equal(proof.cornerCount,4);assert.equal(proof.receiverFeatureCount,8);assert.equal(proof.postEndCutCount,4);
              row.foldedPostConnectionsVerifiedAgainstIndependentSource=proof;
            }
          }
          const nonlinear=reference.document.items.filter(item=>item.properties['manufacturing.partKind']!=='tube');
          if(nonlinear.length) {
            const connections=await pool.invoke('GetProductAssemblyConnections',{
              productEntityId:generated.tubeDesigner.product.entityId,generationRunId:snapshot.generationRunId});
            const stocks=connections.manufacturingStocks;
            assert.ok(Array.isArray(stocks));
            for(const item of nonlinear) {
              const stock=stocks.find(candidate=>candidate.itemKey===item.key);assert.ok(stock,item.key);
              assert.equal(stock.sourceMembers.length,1);
              const mapping=stock.sourceMembers[0];assert.equal(mapping.itemKey,item.key);
              assert.equal(mapping.mappingKind,'identity');assert.equal(Object.hasOwn(mapping,'spans'),false);
            }
            let nonlinearRefs=0;
            for(const connection of connections.connections)for(const participant of connection.participants) {
              if(!nonlinear.some(item=>item.key===participant.itemKey))continue;
              assert.equal(participant.stockRefs.length,1);
              for(const ref of participant.stockRefs){assert.equal(ref.mappingKind,'identity');assert.equal(Object.hasOwn(ref,'spans'),false);nonlinearRefs++;}
            }
            assert.ok(nonlinearRefs>0,'Actual connection participants must consume nonlinear identity stock refs');
            exact(await pool.invoke('InspectScriptResources'),snapshot,'Connection lookup must not mutate live snapshots');
            write(resolve(output,caseInput.name+'-assembly-connections.json'),connections);
            row.nonlinearConnectionIdentity={parts:nonlinear.length,checkedStockRefs:nonlinearRefs,
              actualNativeConsumptionVerified:true,noFabricatedTubeSpans:true};
          }
          const shapes=await inspectManufacturedShapes(pool);
          assert.equal(shapes.geometryChecks.length,reference.document.items.length);
          write(resolve(output,caseInput.name+'-scene.json'),{snapshot,shapes});
          const comparisons=await compareBaselineShapes(pool,reference.finishedDocument,{onStage:stage=>{
            const stageName=stage.stage.replace(/[^a-zA-Z0-9_.-]/g,'_');
            write(resolve(output,caseInput.name+'-shape-'+stageName+'.json'),stage);
          }});
          assert.equal(comparisons.parts,reference.document.items.length);
          assert.ok(comparisons.volumesAndBoundsEqual && comparisons.solidClassificationsEqual
            && comparisons.checkedPoints>0 && comparisons.interiorPoints>0);
          const samplingAccounting=checkSampleCoverage(comparisons,'Actual manufactured entity comparison');
          for(const layer of ['worldExecutionShapes','actualExecutionLocal'])if(comparisons[layer]) {
            const proof=comparisons[layer];
            assert.equal(proof.parts,reference.document.items.length);
            checkSampleCoverage(proof,layer);
            assert.equal(proof.volumesAndBoundsEqual,true);assert.equal(proof.solidClassificationsEqual,true);
            assert.ok(proof.interiorPoints>0);
          }
          row.actualNativeLocalShapeComparison={...comparisons,pointsPerEntity:343,samplingAccounting,
            volumeThreshold:'max(1e-4,referenceVolume*1e-7)',boundsThreshold:1e-5};
          row.neutralWorldComparisonIsActualNativeWorld=false;
          row.finishedReferenceProof=reference.finishedReferenceProof;
          for(let reopen=1;reopen<=2;reopen++) {
            await pool.invoke('SaveAndReopen');
            exact(await pool.invoke('InspectScriptResources'),snapshot,`Live documents after reopen ${reopen}`);
            exact(await pool.invoke('InspectNativeGeometry'),shapes,`Live BRep bytes/hash/placement/facts after reopen ${reopen}`);
          }
          row.realSaveReopenCount=2;row.exactLiveSnapshotsAndNativeGeometryPreserved=true;
          row.generatedAndDisassembled=true;
        }
      }
      // Inspect every actual worker while it is alive, including neutral inspectors.
      for(const worker of pool.workerMetadata) report.actualLoadedModules.push(loadedModules(worker,caseInput.name,
        worker.workerIndex===0 && caseInput.expected==='accept'));
      pool.verifySourceUnchanged();unchanged();row.passed=true;
    } catch(error) { row.error=error.message?.slice(0,4000);if(error.comparison)
      write(resolve(output,caseInput.name+'-shape-failure.json'),error.comparison);throw error;
    } finally {await pool.close();pool=null;save();}
  }
  unchanged();
  assert.equal(report.defaultDescriptorPreviews.length,selectedCase ? 0 : 10);

  assert.equal(report.cases.length,selectedInputs.length);
  assert.equal(report.cases.filter(row=>row.generatedAndDisassembled && row.realSaveReopenCount===2).length,selectedInputs.filter(row=>row.fullNativeScene).length);
  assert.ok(report.cases.every(row=>row.passed));report.fullAcceptanceAsserted=!selectedCase;

  report.sourceAndRuntimeBeforeAfterUnchanged=true;report.passed=true;
} catch(error) {report.passed=false;report.fullAcceptanceAsserted=false;report.error=error.message?.slice(0,4000);process.exitCode=1;}
finally {if(pool)await pool.close();report.finishedAt=new Date().toISOString();save();
  console.log(JSON.stringify({passed:report.passed,reportFile,defaultPreviews:report.defaultDescriptorPreviews.length,
    cases:report.cases.length,fullScenes:report.cases.filter(row=>row.generatedAndDisassembled).length}));}
