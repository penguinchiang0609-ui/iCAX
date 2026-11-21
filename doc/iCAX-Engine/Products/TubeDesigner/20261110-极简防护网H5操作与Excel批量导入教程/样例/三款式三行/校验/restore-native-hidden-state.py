"""Restore one native XLSX feature not retained by Artifact Tool import/export.

All cell authoring is performed by artifact-tool. This adapter restores the
hidden contract sheet and the equivalent default namespace notation understood
by the native reader; it never edits any worksheet values.
"""
import hashlib
import json
import re
import sys
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path

source, target = map(Path, sys.argv[1:3])
ns = {'s': 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'}
with zipfile.ZipFile(source) as archive:
    original = ET.fromstring(archive.read('xl/workbook.xml'))
    native_sheets = {s.get('name'): s.get('state', 'visible') for s in original.findall('s:sheets/s:sheet', ns)}
assert native_sheets['__iCAX_列定义'] == 'hidden'
before_sha = hashlib.sha256(target.read_bytes()).hexdigest()
temporary = target.with_suffix('.xlsx.tmp')
assert not temporary.exists()
with zipfile.ZipFile(target) as archive:
    before = {entry.filename: archive.read(entry.filename) for entry in archive.infolist()}
    xml = before['xl/workbook.xml'].decode('utf-8')
    pattern = r'<(?:\w+:)?sheet\b[^>]*\bname="__iCAX_列定义"[^>]*/>'
    found = re.findall(pattern, xml)
    assert len(found) == 1
    assert not re.search(r'\bstate=', found[0]), 'Only repair the observed missing-state condition'
    replacement = found[0][:-2] + ' state="hidden"/>'
    patched = xml.replace(found[0], replacement, 1).encode('utf-8')
    patches = {'xl/workbook.xml': patched}
    main_namespace = 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'
    for name in ['xl/worksheets/sheet1.xml', 'xl/worksheets/sheet2.xml', 'xl/sharedStrings.xml']:
        if name not in before:
            continue
        text = before[name].decode('utf-8')
        assert f'xmlns:x="{main_namespace}"' in text
        equivalent = text.replace(f'xmlns:x="{main_namespace}"', f'xmlns="{main_namespace}"')
        equivalent = equivalent.replace('<x:', '<').replace('</x:', '</')
        # Expanded XML names, attributes, text, ordering and numbers stay exact.
        def semantic(node):
            return (node.tag, dict(node.attrib), node.text, node.tail, [semantic(child) for child in node])
        assert semantic(ET.fromstring(text)) == semantic(ET.fromstring(equivalent))
        patches[name] = equivalent.encode('utf-8')
    with zipfile.ZipFile(temporary, 'w') as result:
        for entry in archive.infolist():
            result.writestr(entry, patches.get(entry.filename, before[entry.filename]))
with zipfile.ZipFile(temporary) as archive:
    assert archive.testzip() is None
    after = {entry.filename: archive.read(entry.filename) for entry in archive.infolist()}
    changed = [name for name in before if before[name] != after[name]]
    assert set(before) == set(after)
    assert set(changed) == set(patches)
temporary.replace(target)
report = {'status': 'restored', 'reason': 'Artifact Tool roundtrip omitted hidden sheet state and emitted namespace prefixes unsupported by this native reader; worksheet.visibility API unavailable',
          'cellAuthoring': '@oai/artifact-tool', 'patchedEntries': changed,
          'sheet': '__iCAX_列定义', 'state': 'hidden', 'otherZipEntriesByteIdentical': True,
          'worksheetAndSharedStringXmlSemanticallyIdentical': True,
          'beforeSha256': before_sha, 'afterSha256': hashlib.sha256(target.read_bytes()).hexdigest()}
Path(__file__).with_name('artifact-hidden-state-preservation.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(report, ensure_ascii=True))
