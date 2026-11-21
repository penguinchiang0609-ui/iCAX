import hashlib
import json
import sys
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path

NS = {'s': 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'}

def inspect(path):
    with zipfile.ZipFile(path) as archive:
        assert archive.testzip() is None
        entries = set(archive.namelist())
        shared = []
        if 'xl/sharedStrings.xml' in entries:
            shared = [''.join(node.itertext()) for node in ET.fromstring(archive.read('xl/sharedStrings.xml')).findall('s:si', NS)]
        def cells(sheet):
            values = {}
            for cell in sheet.findall('.//s:sheetData/s:row/s:c', NS):
                kind = cell.get('t', '')
                raw = cell.findtext('s:v', default='', namespaces=NS)
                if kind == 's': value = shared[int(raw)]
                elif kind == 'inlineStr': value = ''.join(node.text or '' for node in cell.findall('.//s:t', NS))
                elif kind == 'b': value = bool(int(raw))
                elif kind in ('str', 'e'): value = raw
                else: value = float(raw) if raw else None
                if value is not None and value != '':
                    values[cell.get('r')] = value
            return values
        sheet = ET.fromstring(archive.read('xl/worksheets/sheet1.xml'))
        metadata = ET.fromstring(archive.read('xl/worksheets/sheet2.xml'))
        definitions = cells(metadata)
        workbook = ET.fromstring(archive.read('xl/workbook.xml'))
        return {
            'path': str(path.resolve()), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
            'sheets': [(item.get('name'), item.get('state', 'visible')) for item in workbook.findall('s:sheets/s:sheet', NS)],
            'contract': json.loads(definitions['A1']), 'metadataCells': definitions,
            'values': cells(sheet),
            'validations': [{'attributes': dict(item.attrib), 'formula1': item.findtext('s:formula1', namespaces=NS)}
                            for item in sheet.findall('s:dataValidations/s:dataValidation', NS)],
            'merges': [item.get('ref') for item in sheet.findall('s:mergeCells/s:mergeCell', NS)],
            'panes': [dict(item.attrib) for item in sheet.findall('s:sheetViews/s:sheetView/s:pane', NS)],
        }

source, target = map(Path, sys.argv[1:3])
before, after = inspect(source), inspect(target)
assert before['sheets'] == after['sheets'] == [('批量导入', 'visible'), ('__iCAX_列定义', 'hidden')]
assert before['contract'] == after['contract']
assert before['metadataCells'] == after['metadataCells']
assert before['validations'] == after['validations'], (before['validations'], after['validations'])
assert before['merges'] == after['merges']
assert before['panes'] == after['panes']
for key, value in before['values'].items():
    assert after['values'].get(key) == value, (key, value, after['values'].get(key))
expected = [
    {'__instanceName': 'A01-平面', '__instanceQuantity': 1, 'layoutStyle': '平面款', 'width': 568, 'height': 1250, 'barCenterSpacing': 115},
    {'__instanceName': 'A02-五面', '__instanceQuantity': 2, 'layoutStyle': '五面飘网', 'width': 800, 'height': 1400, 'barCenterSpacing': 115},
    {'__instanceName': 'A03-阳台', '__instanceQuantity': 3, 'layoutStyle': '阳台上下收口', 'width': 1000, 'height': 1200, 'barCenterSpacing': 100},
]
for row_number, row in enumerate(expected, 3):
    for index, column in enumerate(after['contract']['columns']):
        assert after['values'][f'{chr(65+index)}{row_number}'] == row[column['key']]
assert set(after['values']) - set(before['values']) == {f'{col}{row}' for col in 'ABCDEF' for row in range(3, 6)}
report = {'status': 'passed', 'source': before['path'], 'sourceSha256': before['sha256'],
          'target': after['path'], 'targetSha256': after['sha256'],
          'rows': 3, 'expectedInstances': 3, 'expectedProductionQuantity': 6,
          'nativeContractUnchanged': True, 'hiddenSheetUnchanged': True, 'dropdownValidationUnchanged': True,
          'titleHeaderMergeFreezePreserved': True, 'nativeImportCreationVerified': False}
Path(__file__).with_name('workbook-preservation-report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(report, ensure_ascii=False))

