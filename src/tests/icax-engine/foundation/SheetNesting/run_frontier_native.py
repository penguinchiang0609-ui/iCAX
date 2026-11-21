"""Run isolated native repair jobs from audited same-count research frontiers.

The executable and current core sources are hashed before/after. This runner
never modifies an existing report, source input, source seed, or strategy pool.
"""
from pathlib import Path
import argparse, concurrent.futures, copy, hashlib, importlib.util, json, re, subprocess, sys, time

ROOT=Path(__file__).resolve().parents[5]


def source_snapshot_inputs(location, executable):
    """Validate copied source files and the explicitly recorded executable.

    Contract: manifest.json contains files={snapshot-relative-path: sha256} and
    executableSha256. librarySha256 records the linked static library; copying
    that library into files additionally verifies its immutable payload.
    Original/live source paths may appear in metadata, but are never read here.
    """
    location=Path(location).resolve();executable=Path(executable).resolve()
    if location.is_dir():
        choices=[location/name for name in ('manifest.json','snapshot-manifest.json','source-manifest.json')
                 if (location/name).is_file()]
        if len(choices)!=1:raise ValueError('Snapshot directory needs one unambiguous manifest.json')
        manifest=choices[0]
    else:manifest=location
    data=json.loads(manifest.read_text(encoding='utf8'));snapshot_root=manifest.parent
    files=data.get('files',data.get('snapshotFiles',data.get('copiedFiles')))
    if not isinstance(files,dict) or not files:raise ValueError('Snapshot manifest needs a nonempty files mapping')
    digest=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
    verified={};paths=[]
    for relative,record in files.items():
        expected=record.get('sha256') if isinstance(record,dict) else record
        if not isinstance(expected,str) or not re.fullmatch('[0-9a-fA-F]{64}',expected):
            raise ValueError('Invalid snapshot SHA256 for '+relative)
        path=(snapshot_root/relative).resolve()
        if not path.is_relative_to(snapshot_root):
            raise ValueError('Snapshot source path must stay inside the copied snapshot: '+relative)
        if not path.is_file() or digest(path)!=expected.lower():
            raise ValueError('Snapshot copy is missing or changed: '+relative)
        paths.append(path);verified[relative]=expected.lower()
    if not any(path.suffix.lower() in ('.cpp','.h') for path in paths):
        raise ValueError('Snapshot files must include copied C++ sources or headers')
    expected_executable=data.get('executableSha256')
    if expected_executable is None and isinstance(data.get('executable'),dict):
        expected_executable=data['executable'].get('sha256')
    if expected_executable is None:
        matches={expected for relative,expected in verified.items() if Path(relative).name==executable.name}
        if len(matches)==1:expected_executable=next(iter(matches))
    if not isinstance(expected_executable,str) or not re.fullmatch('[0-9a-fA-F]{64}',expected_executable):
        raise ValueError('Snapshot needs executableSha256 for the exact native executable')
    if digest(executable)!=expected_executable.lower():
        raise ValueError('Native executable does not match the compiled-source snapshot')
    evidence={'manifest':str(manifest),'snapshotRoot':str(snapshot_root),'copiedFiles':verified,
        'executable':str(executable),'executableSha256':expected_executable.lower(),
        'librarySha256':data.get('librarySha256'),'buildMetadata':data,
        'note':'Only immutable snapshot copies and the recorded executable are validated. Live core source files are not read.'}
    return [manifest,*paths],evidence


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--frontiers',required=True);ap.add_argument('--baseline-summary',required=True)
    ap.add_argument('--build',required=True);ap.add_argument('--output',required=True);ap.add_argument('--packages',required=True)
    ap.add_argument('--cases',default='fu,jakobs1,shapes2');ap.add_argument('--per-case',type=int,default=3)
    ap.add_argument('--seconds',type=float,default=10);ap.add_argument('--attempts',type=int,default=2000000)
    ap.add_argument('--workers',type=int,default=2);ap.add_argument('--mode',choices=('reassemble','refill','linear','separate','separate-nfp','constraint','constraint-neighborhood'),default='reassemble')
    ap.add_argument('--constraint-scale',type=int,default=14)
    ap.add_argument('--constraint-workers',type=int,default=2)
    ap.add_argument('--constraint-conflicts',type=int,default=1000000)
    ap.add_argument('--complete-separation-hints',action='store_true')
    ap.add_argument('--neighborhood-max-movable',type=int,default=8)
    ap.add_argument('--neighborhood-max-plans',type=int,default=8)
    ap.add_argument('--neighborhood-seconds-per-plan',type=float,default=5)
    ap.add_argument('--neighborhood-contact-order',type=int,choices=(0,1),default=0)
    ap.add_argument('--frontier-filter',default='',help='Only use frontier filenames containing this text')
    ap.add_argument('--source-snapshot',help='Compiled-source snapshot directory or manifest; avoids reading live core sources')
    args=ap.parse_args();sys.path.insert(0,str(Path(args.packages).resolve()))
    import shapely
    spec=importlib.util.spec_from_file_location('evidence',Path(__file__).with_name('run_public_beam.py'))
    ev=importlib.util.module_from_spec(spec);spec.loader.exec_module(ev)
    output=Path(args.output).resolve();output.mkdir(parents=True,exist_ok=True)
    if (output/'summary.json').exists():raise ValueError('Refusing to overwrite completed native evidence')
    raw=output/'raw';raw.mkdir(exist_ok=True)
    exe=Path(args.build).resolve()/'Release/SheetNestingPublicBench.exe';assert exe.is_file(),exe
    folder=Path(args.frontiers).resolve();base_path=Path(args.baseline_summary).resolve()
    baseline=json.loads(base_path.read_text(encoding='utf8'))
    baseline_by_case={row['case']:row['selected'] for row in baseline['rows'] if not row['selected']['audit']['complete']}
    selected_cases=set(args.cases.split(','));tasks=[]
    for name in args.cases.split(','):
        candidates=[];seen=set()
        for path in sorted(folder.glob(name+'-frontier-*.json')):
            if args.frontier_filter and args.frontier_filter not in path.name:continue
            d=json.loads(path.read_text(encoding='utf8'));problem=d['problem'];parts={p['id']:p for p in problem['parts']}
            assert ev.audit(name,problem,d['result'])['valid'],path
            pieces=[]
            for layout in d['result']['layouts']:
                for pose in layout['placements']:
                    # Equivalent source rotations and permutations of identical
                    # copies do not create extra independent geometry jobs.
                    g=shapely.normalize(ev.geometry(parts[pose['part']],pose))
                    pieces.append((pose['part'],g.wkb_hex))
            key=tuple(sorted(pieces))
            if key in seen:continue
            seen.add(key);remaining=tuple((x['part'],x['quantity']) for x in d['result']['remaining'])
            candidates.append((remaining,path,d))
        buckets={}
        for remaining,path,d in candidates:buckets.setdefault(remaining,[]).append((path,d))
        selected=[];rank=0
        while len(selected)<args.per_case:
            batch=[items[rank] for items in buckets.values() if len(items)>rank]
            if not batch:break
            selected+=batch[:args.per_case-len(selected)];rank+=1
        for path,d in selected:
            assert d['problem']==baseline_by_case[name]['problem'],path
            seed=Path(d['seedPath']);assert seed.is_file() and ev.digest(seed)==d['seedSha256'],seed
            tasks.append({'case':name,'frontierSource':str(path),'frontier':d,'seed':str(seed),
                          'tag':path.stem,'width':d['problem']['stocks'][0]['width']})
    if not tasks:raise ValueError('No distinct frontier geometry found')
    core=ROOT/'src/iCAX-Engine/foundation/SheetNesting'
    tracked=[exe,Path(__file__).resolve(),base_path];snapshot_evidence=None
    if args.source_snapshot:
        snapshot_files,snapshot_evidence=source_snapshot_inputs(args.source_snapshot,exe)
        tracked+=snapshot_files
        (output/'source-snapshot-evidence.json').write_text(json.dumps(snapshot_evidence,indent=2),encoding='utf8')
    else:tracked+=sorted(core.glob('*.cpp'))+sorted(core.glob('*.h'))
    tracked+=list({Path(t['seed']) for t in tasks})+[Path(t['frontierSource']) for t in tasks]
    tracked+=[ROOT/'TestData/Plane/Nesting/umetani-isp'/(name+'.dat') for name in selected_cases]
    hashes={str(path):ev.digest(path) for path in tracked}
    (output/'run-start-sources.json').write_text(json.dumps(hashes,indent=2),encoding='utf8')
    def execute(task):
        path=raw/(task['tag']+'.json')
        if path.exists():raise ValueError('Refusing to reuse native raw result '+str(path))
        cmd=[str(exe),str(ROOT/'TestData/Plane/Nesting/umetani-isp'/(task['case']+'.dat')),
             format(task['width'],'.17g'),str(path),'.1',str(args.attempts),'seed@'+task['seed'],
             str(args.seconds),'original',args.mode]
        if args.mode in ('constraint','constraint-neighborhood'):
            cmd+=[str(args.constraint_scale),str(args.constraint_workers),str(args.constraint_conflicts),
                  str(int(args.complete_separation_hints))]
            if args.mode=='constraint-neighborhood':
                cmd+=[str(args.neighborhood_max_movable),str(args.neighborhood_max_plans)]
                if args.neighborhood_seconds_per_plan!=5 or args.neighborhood_contact_order:
                    cmd.append(str(args.neighborhood_seconds_per_plan))
                if args.neighborhood_contact_order:
                    cmd.append(str(args.neighborhood_contact_order))
        started=time.perf_counter();process=subprocess.run(cmd,capture_output=True,text=True,timeout=2*args.seconds+120)
        (raw/(task['tag']+'.log')).write_text(process.stdout+'\n'+process.stderr,encoding='utf8')
        if process.returncode:raise RuntimeError(task['tag']+': '+process.stderr)
        native=json.loads(path.read_text(encoding='utf8'));assert native['problem']==task['frontier']['problem']
        checked=[]
        for run in native['runs']:
            audits={stage:ev.audit(task['case'],native['problem'],run[stage]) for stage in ('before','candidate','selected')}
            for stage,a in audits.items():
                if run[stage]['valid'] and not a['valid']:raise ValueError((task['tag'],stage,a['errors']))
            checked.append(audits)
        # Native seed reading must retain the supplied original instance poses.
        seed_geometry=lambda result:sorted((p['part'],p['instance'],p['x'],p['y'],p['angle']) for l in result['layouts'] for p in l['placements'])
        assert seed_geometry(native['runs'][0]['before'])==seed_geometry(task['frontier']['result'])
        print(task['tag'],'native',max(r['selected']['placed'] for r in native['runs']),
              'complete',any(a['selected']['complete'] for a in checked),flush=True)
        return {'case':task['case'],'tag':task['tag'],'rawSource':str(path),'seed':task['seed'],
                'frontierSource':task['frontierSource'],'processWallSeconds':time.perf_counter()-started,
                'audits':checked,'native':native}
    results=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        for future in concurrent.futures.as_completed([pool.submit(execute,t) for t in tasks]):results.append(future.result())
    if hashes!={str(path):ev.digest(path) for path in tracked}:raise ValueError('Tracked snapshot/core, executable, original data or source seed changed during native run')
    if args.source_snapshot:
        _,snapshot_after=source_snapshot_inputs(args.source_snapshot,exe)
        if snapshot_after!=snapshot_evidence:raise ValueError('Compiled-source snapshot changed during native run')
    groups=ev.load_trials(raw);selections=[];sections=[]
    for key,trials in sorted(groups.items()):
        name=key[0];before=baseline_by_case[name];native_best=ev.best(trials)
        chosen=min((before,native_best),key=lambda t:ev.quality(t['problem'],t['result'],t['audit']))
        png=name+'-comparison.png';ev.render_pair(before,chosen,output/png)
        selections.append({'case':name,'baseline':before,'nativeBest':native_best,'selected':chosen,
                           'improved':ev.quality(chosen['problem'],chosen['result'],chosen['audit'])<ev.quality(before['problem'],before['result'],before['audit'])})
        sections.append(f'<section><h2>{name}</h2><p>从原始合法前沿重新运行独立原生修复。最终 {chosen["audit"]["placed"]}/{chosen["audit"]["required"]} 件。</p><img src="{png}"></section>')
    evidence={'tasks':results,'selections':selections,'nativeFrontierJobs':len(tasks),
        'newlyCompleteCases':sum(r['selected']['audit']['complete'] and not r['baseline']['audit']['complete'] for r in selections),
        'inputBaselineSummary':str(base_path),'sourceHashes':hashes,'sourceSnapshot':snapshot_evidence,
        'notes':['Frontiers are real original-geometry partial layouts, not reference-solution coordinates.',
                 'Every frontier was checked before native execution. The native executable independently validates and reads the exact supplied original identities and poses.',
                 'The before/candidate/selected states are independently audited against original DAT geometry. Unsuccessful jobs remain in tasks.']}
    (output/'summary.json').write_text(json.dumps(evidence,ensure_ascii=False,indent=2),encoding='utf8')
    (output/'index.html').write_text('<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>合法换洞前沿：独立原生修复实测</title><style>body{font:16px/1.6 "Microsoft YaHei",sans-serif;max-width:1900px;margin:auto;padding:24px;background:#eef3f6}section{background:white;padding:20px;margin:24px 0}img{width:100%}</style><h1>合法换洞前沿：独立原生修复实测</h1><p>件数相同、缺件规格不同的真实合法布局作为独立job输入。每个输入在原生读取后核对原始实例与坐标，修复后再次独立核验。图中只选同原始问题下质量更好的结果。</p>'+''.join(sections)+'<p><a href="summary.json">全部任务，包括未改善结果</a></p>',encoding='utf8')
    (output/'manifest.json').write_text(json.dumps({str(path.relative_to(output)):ev.digest(path) for path in output.rglob('*') if path.is_file() and path.name!='manifest.json'},indent=2),encoding='utf8')
    print('native jobs',len(tasks),'new complete cases',evidence['newlyCompleteCases'],flush=True)


if __name__=='__main__':main()
