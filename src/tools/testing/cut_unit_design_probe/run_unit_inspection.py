"""Export native CAD inspection from already accepted stage-one organization."""
import argparse
import hashlib
import json
import os
import subprocess
import zipfile
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
DEST = ROOT / 'output/tests/cut-unit-stage1-20261025'
EXE = ROOT / 'output/tests/cut-unit-design-20261023/build/Release/export_unit_inspection.exe'


def read(path):
    return json.loads(path.read_text('utf-8'))


def matches_exporter(report, expected_hash):
    """Geometry equality cannot certify which display exporter produced a STEP."""
    return report.get('passed') is True and report.get('inspectionExporterSha256') == expected_hash


def generate_inspection(item, folder):
    case = read(folder / 'view.json')
    source = Path(item['source'])
    assert hashlib.sha256(source.read_bytes()).hexdigest() == case['sourceHash'] == item['sha256']
    exporter_hash = hashlib.sha256(EXE.read_bytes()).hexdigest()
    seen, units = set(), []
    for face in case['faces']:
        key = face.get('assemblyKey') or f"F{face['faceId']:04d}_" + ('U001' if face['units'] else 'PENDING')
        if key in seen:
            continue
        seen.add(key)
        unit = face['units'][0] if face['units'] else None
        ids = unit['sourceRegion'].get('faceIds', [face['faceId']]) if unit else [face['faceId']]
        name = key + '__' + '_'.join('F' + str(fid) for fid in ids)
        boundaries = []
        for chain in (unit.get('boundaries') if unit else None) or face['chains']:
            # Preserve the traversal from the assembled boundary; mixed roles
            # are separate display groups, never silently recolored as Skin.
            runs = []
            for use in chain['sourceUses']:
                fid = use.get('sourceFaceId', face['faceId'])
                source_face = next(f for f in case['faces'] if f['faceId'] == fid)
                roles = {g['role'] for g in source_face['groups'] if use['edgeId'] in g['sourceEdges']}
                assert len(roles) == 1, (fid, use['edgeId'], roles)
                role = roles.pop()
                if not runs or runs[-1][0] != role:
                    runs.append((role, []))
                runs[-1][1].append(use)
            for i, (role, uses) in enumerate(runs, 1):
                boundaries.append((f"C{chain['id']}" + (f"_{i}" if len(runs) > 1 else ""), role, uses))
        if unit:
            for seam in unit.get('internalSeams', []):
                boundaries.append((f"S{seam['id']}",
                                   'InternalSeam', seam['sides'][0]['sourceUses']))
        units.append((name, ids, face['sourceColor'], boundaries))
    lines = [json.dumps(case['sourceFile'], ensure_ascii=False), str(len(units))]
    for name, ids, color, boundaries in units:
        lines.append(f"{name} {len(ids)} {len(boundaries)} " + ' '.join(map(str, color)))
        lines.append(' '.join(map(str, ids)))
        for name, role, uses in boundaries:
            lines.append(f'{name} {role} {len(uses)}')
            lines.extend(f"{use['edgeId']} {int(use['reversed'])}" for use in uses)
    controls = folder / 'inspection-controls.txt'
    controls.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    env = os.environ.copy()
    env['PATH'] = str(ROOT / 'src/third_party/opencascade/install/vs2022-x64/win64/vc14/bin') + os.pathsep + env['PATH']
    with (folder / 'inspection.log').open('wb') as log:
        subprocess.run([str(EXE), str(source), str(controls), str(folder)], env=env, stdout=log, stderr=log, check=True, timeout=600)
    assert hashlib.sha256(EXE.read_bytes()).hexdigest() == exporter_hash, 'Inspection exporter changed during generation'
    report = read(folder / 'inspection-audit.json')
    assert report['passed'] and report['originalFaces'] == case['faceCount']
    report.update(sourceHash=item['sha256'], sourceFile=item['file'], caseId=item['id'],
                  inspectionExporterSha256=exporter_hash,
                  inspectionSha256=hashlib.sha256((folder / 'inspection.step').read_bytes()).hexdigest())
    (folder / 'inspection-audit.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return package_inspection(case, folder, report)


def package_inspection(case, folder, report):
    assert report['passed'] and report['sourceHash']==case['sourceHash']
    assert hashlib.sha256((folder/'inspection.step').read_bytes()).hexdigest()==report['inspectionSha256']
    case['inspection'] = {'file': 'inspection.step', 'audit': 'inspection-audit.json', 'displayOnlyGeometry': True}
    (folder / 'view.json').write_text(json.dumps(case, ensure_ascii=False, separators=(',', ':')), encoding='utf-8')
    if case.get('wholeModel'):
        whole = case['wholeModel']
        display = read(folder / whole['colors'])
        display.update(inspectionStep='inspection.step', inspectionSha256=report['inspectionSha256'], displayOnlyGeometry=True)
        (folder / whole['colors']).write_text(json.dumps(display, ensure_ascii=False, separators=(',', ':')), encoding='utf-8')
        with zipfile.ZipFile(folder / whole['bundle'], 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            for file in [whole['file'], whole['colors'], 'inspection.step', 'inspection-audit.json']:
                archive.write(folder / file, file)
            archive.writestr('README.txt', f"模型：{case['sourceFile']}\n\ncut-units.brep 是完整的 Classify 加工面集合，一份 BREP 内按单元分组，保持全部原面与原位置。\n\n仅共面且公共侧边两端切割边界均相切的接缝才可消去；其余侧边保留方向约束。inspection.step 用于 CAD Assistant 检查：按单元组织模型树，FACE_F 开头为原始面；DISPLAY_ 开头为可单独隐藏的边界显示标记。皮上边以青色粗标记显示，侧边以橙色断续标记显示，内部边为紫色普通曲线；通过消边检查的内部接缝为单独标记。\n\n显示标记不属于加工几何，不是刀路；这些附加标记不写入原始 BREP。STEP 整体面数包含显示标记，请勿把所有面当成加工面。JSON 保存编号、颜色、边界角色、方向约束与文件哈希。\n")
    return report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--only', nargs='+', type=int, required=True)
    parser.add_argument('--workers', type=int, default=1)
    parser.add_argument('--publish', action='store_true')
    args = parser.parse_args()
    selected = [item for item in read(DEST / 'source-manifest.json') if item['id'] in args.only]
    def refresh(item):
        folder=DEST/'cases'/f"{item['id']:03d}"
        case=read(folder/'view.json');whole=folder/case['wholeModel']['file']
        original_brep_hash=hashlib.sha256(whole.read_bytes()).hexdigest()
        report=generate_inspection(item, folder)
        assert hashlib.sha256(whole.read_bytes()).hexdigest()==original_brep_hash,'Inspection changed whole BREP'
        if args.publish:
            from publish_stage1_artifacts import publish_case
            publish_case(item,folder)
        print(f"case {item['id']}: {report['unitGroups']} groups, {report['originalFaces']} unchanged faces, {report['markerGroups']} marker groups",flush=True)
        return {'caseId':item['id'],'passed':True,'brepSha256Unchanged':original_brep_hash,
                'inspectionSha256':report['inspectionSha256'],
                'inspectionExporterSha256':report['inspectionExporterSha256']}
    results=[]
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures={pool.submit(refresh,item):item for item in selected}
        for future in as_completed(futures):
            try:results.append(future.result())
            except Exception as error:
                failure={'caseId':futures[future]['id'],'passed':False,'error':repr(error)}
                results.append(failure);print(json.dumps(failure),flush=True)
    (DEST/'inspection-refresh.json').write_text(json.dumps({'selected':len(selected),'results':results},indent=2),'utf-8')
    if not all(result['passed'] for result in results):raise SystemExit(1)


if __name__ == '__main__':
    main()
