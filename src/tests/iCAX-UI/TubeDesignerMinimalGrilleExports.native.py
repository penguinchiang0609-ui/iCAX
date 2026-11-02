"""Read actual exported STEP, compare workbook lengths/quantities and restore assembly."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile

root = Path(sys.argv[1]).resolve()
repository = Path(__file__).resolve().parents[3]
runtime = Path(os.environ.get('ICAX_STEP_AUDIT_RUNTIME', repository / 'output/tests/customer-joints-merge-20261023/runtime'))
read = lambda p: json.loads(p.read_text(encoding='utf-8-sig'))
report_path = root / os.environ.get('ICAX_GRILLE_AUDIT_REPORT', 'acceptance-report.json')
report = read(report_path)
assert report['passed'], 'Complete native acceptance is required before export audit'
ns = {'s': 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'}
result = {'passed': False, 'sourceReport': str(report_path), 'nativeStepReadback': True, 'exactAssemblyIntersection': True, 'cases': [],
          'tools': {name: {'path': str(runtime/name), 'sha256': hashlib.sha256((runtime/name).read_bytes()).hexdigest()}
                    for name in ('StepReadback.exe', 'AssemblyAudit.exe')}}


def run(name, *args):
    done = subprocess.run([str(runtime/name), *map(str, args)], cwd=runtime,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
    if done.returncode:
        raise AssertionError(done.stdout.decode('utf-8', errors='replace'))


try:
    for summary in report['cases']:
        case = read(Path(summary['reportPath'])) if 'reportPath' in summary else summary
        target, name = Path(case['target']), case['name']
        step_path = root / (name+'-step-readback.json')
        run('StepReadback.exe', target, step_path)
        measured = {str(Path(f['file']).resolve()).casefold(): f for f in read(step_path)['files']}
        with zipfile.ZipFile(target/'零件清单.xlsx') as archive:
            strings = [''.join(e.itertext()) for e in ET.fromstring(archive.read('xl/sharedStrings.xml'))] if 'xl/sharedStrings.xml' in archive.namelist() else []
            rows = []
            for row in ET.fromstring(archive.read('xl/worksheets/sheet1.xml')).findall('.//s:sheetData/s:row', ns):
                cells = {}
                for cell in row.findall('s:c', ns):
                    key = ''.join(c for c in cell.get('r') if c.isalpha())
                    value, inline = cell.find('s:v', ns), cell.find('s:is', ns)
                    text = value.text if value is not None else ''.join(inline.itertext()) if inline is not None else ''
                    cells[key] = strings[int(text)] if cell.get('t') == 's' else text
                rows.append(cells)
        data = [r for r in rows[2:] if r.get('K')]
        exported = {int(row['D']): row for row in data}
        quantities, worst, assembly = 0, 0., []
        stock_checks = []
        parameters = case['model']['product']['parameters']
        source_members = {m['stableKey']: m['properties']['assemblyFrame.member'] for m in case['model']['members']}
        files = []
        for part in case['model']['parts']:
            row = exported[part['index']]
            quantity = int(float(row['J']))
            assert quantity == part['quantity'] > 0
            quantities += quantity
            path = (target/row['L'].replace('\\', '/')).resolve()
            assert path.is_file() and path.name == row['K']
            step = measured[str(path).casefold()]
            assert step['valid'] and step['solids'] == 1 and step['volumeMm3'] > 0
            difference = abs(float(row['I'])-step['boundsMm'][0])
            assert difference < .0011, (path, difference)
            worst = max(worst, difference)
            files.append(path)
            for source in part['properties']['manufacturing.sourceMembers']:
                assert len(source['spans']) == 1
                member = source_members[source['itemKey']]
                expected = member['axisLength']
                inserted_ends = 0
                if parameters['jointMode'] == 'male_female_half_hole' and source['itemKey'].startswith('inner.') and abs(member['start'][2]-member['end'][2]) < 1.e-7:
                    inserted_ends = sum(abs(point[0]-parameters['frameWidth']) < 1.e-6 or abs(point[0]-(parameters['width']-parameters['frameWidth'])) < 1.e-6 for point in (member['start'], member['end']))
                    expected += inserted_ends*parameters['maleHeadLength']
                assert abs(expected-step['boundsMm'][0]) < 1.e-5, (source['itemKey'], expected, step['boundsMm'][0])
                stock_checks.append({'key': source['itemKey'], 'visibleLengthMm': member['axisLength'], 'insertedEnds': inserted_ends, 'expectedStockLengthMm': expected, 'measuredStockLengthMm': step['boundsMm'][0]})
                placement = source['spans'][0]['placement']
                matrix = [placement[axis][i] for i in range(3) for axis in ('xAxis', 'yAxis', 'zAxis', 'origin')]
                assembly.append(json.dumps(str(path), ensure_ascii=False)+' '+json.dumps(source['itemKey'])+' '+' '.join(format(v, '.17g') for v in matrix))
        assert quantities == len(assembly) == len(case['model']['members'])
        assert len(files) == len(set(files)) == len(measured) == len(data)
        assembly_path = root / (name+'-assembly.txt')
        assembly_path.write_text(str(len(assembly))+'\n'+'\n'.join(assembly), encoding='utf-8')
        audit_path = root / (name+'-assembly-audit.json')
        run('AssemblyAudit.exe', assembly_path, audit_path)
        audit = read(audit_path)
        assert audit['passed'] and audit['interferingPairs'] == 0 and audit['physicalMembers'] == quantities
        item = {'name': name, 'passed': True, 'types': len(files), 'physicalQuantity': quantities,
                'maxLengthDifferenceMm': worst, 'assemblyAudit': audit,
                'stockLengths': stock_checks,
                'allPathsExist': True, 'allStepShapesValidSingleSolids': True}
        result['cases'].append(item)
        print(json.dumps({k: v for k, v in item.items() if k != 'stockLengths'}, ensure_ascii=False), flush=True)
    result['passed'] = True
except Exception as error:
    result['error'] = repr(error)
    raise
finally:
    (root/'export-geometry-audit.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
