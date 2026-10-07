"""Document/hash reference packages. Moving is deliberately done in PowerShell.

prepare: read the original directories, write appendices and before-move SHA.
verify: assert the archived directories match every source file and cache byte.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[6]
DOCS = ROOT / 'src/apps/tube-designer/docs/deferred-products'
PRODUCTS = ROOT / 'src/apps/tube-designer/templates/product'
PACKAGES = [('louver_window', 'louver-window'),
            ('aluminium_window', 'aluminium-window'),
            ('decorative_door', 'decorative-door')]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def files(path):
    result = []
    for file in sorted(path.rglob('*')):
        if not file.is_file():
            continue
        relative = file.relative_to(path).as_posix()
        cached = '__pycache__' in file.parts or file.suffix in ('.pyc', '.pyo')
        result.append({'path': relative, 'bytes': file.stat().st_size,
                       'sha256': sha(file), 'cache': cached})
    return result


def localized(value):
    return value.get('zh-CN', next(iter(value.values()), '')) if isinstance(value, dict) else value


def appendix(directory, descriptor):
    parameters = descriptor['parameters']
    lines = [f"# {localized(descriptor['displayName'])}参数声明附录", '',
             f"归档模板 `{descriptor['id']}` / `{descriptor['version']}`，共 {len(parameters)} 项。",
             '以下从 reference/template.json 原声明生成；默认值不是推荐工程值。未声明约束不表示没有脚本几何约束。',
             '显隐、禁用和选择条件的结构原样保留，供共享 parameterConditions.mjs 解释，不另造条件解释器。', '',
             '| 键 | 名称 | 类型 | 默认值 | 分组 | 单位 |',
             '| --- | --- | --- | --- | --- | --- |']
    for item in parameters:
        value = json.dumps(item.get('defaultValue'), ensure_ascii=False)
        cells = [item['key'], localized(item.get('displayName', '')), item.get('valueType', ''),
                 value, item.get('group', ''), item.get('unit', '')]
        lines.append('| ' + ' | '.join(str(cell).replace('|', '\\|').replace('\n', ' ') for cell in cells) + ' |')
    lines += ['', '## 完整声明', '',
              '每段包含原参数的全部字段：choices 的值和名称、constraints、visibleWhen、enabledWhen、readOnly、presentation、description 等（以实际存在字段为准）。', '']
    for item in parameters:
        lines += [f"### {item['key']} — {localized(item.get('displayName', ''))}", '',
                  '```json', json.dumps(item, ensure_ascii=False, indent=2), '```', '']
    path = directory / 'parameters.md'
    text = '\n'.join(lines)
    # Check complete declarations survived the documentation serialization.
    restored = [json.loads(block) for block in re.findall(r'```json\n(.*?)\n```', text, re.S)]
    assert restored == parameters
    path.write_text(text, encoding='utf-8')


def prepare():
    assert ROOT.resolve() == Path('D:/penguinchinage0609/iCAX').resolve()
    now = datetime.now(timezone.utc).isoformat()
    dependencies = set()
    for folder, ident in PACKAGES:
        source, destination = PRODUCTS / folder, DOCS / ident
        assert source.is_dir() and not (destination / 'reference').exists()
        descriptor = json.loads((source / 'template.json').read_text(encoding='utf-8-sig'))
        assert descriptor['id'] == ident
        destination.mkdir(parents=True, exist_ok=True)
        appendix(destination, descriptor)
        inventory = files(source)
        write_json(destination / 'archive.json', {
            'schema': 'icax.deferred-product-reference', 'schemaVersion': 1,
            'status': 'prepared', 'preparedAt': now,
            'templateId': ident, 'version': descriptor['version'],
            'originalDirectory': source.relative_to(ROOT).as_posix(),
            'referenceDirectory': (destination / 'reference').relative_to(ROOT).as_posix(),
            'nonCacheFileCount': sum(not file['cache'] for file in inventory),
            'parameterCount': len(descriptor['parameters']),
            'cachePolicy': 'Moved intact; cache bytes inventoried separately, excluded from source count.',
            'files': [file for file in inventory if not file['cache']],
            'retainedCacheFiles': [file for file in inventory if file['cache']]})
        for script in source.glob('*.py'):
            content = script.read_text(encoding='utf-8-sig')
            for name in re.findall(r'(?:_shared|shared|module)\(["\']([^"\']+\.py)["\']\)', content):
                dependency = ROOT / 'src/apps/tube-designer/templates/_shared' / name
                if dependency.is_file():
                    dependencies.add(dependency)
    for name in ('display.py', 'manufacturing.py', 'resources.py', 'model.py', '__init__.py'):
        dependency = ROOT / 'src/iCAX-Engine/framework/TemplateRuntime/python/icax_template_sdk' / name
        if dependency.is_file():
            dependencies.add(dependency)
    dependencies.add(ROOT / 'src/apps/tube-designer/webpage/parameterConditions.mjs')
    write_json(DOCS / 'dependencies.json', {
        'scope': 'Direct shared imports plus inspected SDK/condition boundary; not a transitive runtime lockfile.',
        'capturedAt': now,
        'files': [{'path': path.relative_to(ROOT).as_posix(), 'bytes': path.stat().st_size,
                   'sha256': sha(path)} for path in sorted(dependencies)]})
    print(json.dumps({'status': 'prepared', 'packages': len(PACKAGES),
                      'parameters': sum(json.loads((PRODUCTS / folder / 'template.json').read_text(encoding='utf-8-sig'))['parameters'].__len__() for folder, _ in PACKAGES)}, ensure_ascii=False))


def verify():
    for folder, ident in PACKAGES:
        directory = DOCS / ident
        record = json.loads((directory / 'archive.json').read_text(encoding='utf-8'))
        expected = sorted(record['files'] + record['retainedCacheFiles'], key=lambda file: file['path'])
        actual = sorted(files(directory / 'reference'), key=lambda file: file['path'])
        assert actual == expected, ident + ': archive bytes changed or file inventory differs'
        assert not (PRODUCTS / folder).exists(), ident + ': original still active'
        descriptor = json.loads((directory / 'reference/template.json').read_text(encoding='utf-8-sig'))
        text = (directory / 'parameters.md').read_text(encoding='utf-8')
        restored = [json.loads(block) for block in re.findall(r'```json\n(.*?)\n```', text, re.S)]
        assert restored == descriptor['parameters'], ident + ': appendix differs'
        record.update(status='archived', verifiedAt=datetime.now(timezone.utc).isoformat(),
                      verification={'allFilesAndCacheSha256Equal': True,
                                    'completeParameterDeclarationsEqual': True,
                                    'originalDirectoryAbsent': True})
        write_json(directory / 'archive.json', record)
        print(json.dumps({'templateId': ident, 'status': record['status'],
                          'files': record['nonCacheFileCount'], 'parameters': record['parameterCount']}, ensure_ascii=False))
    for path in DOCS.rglob('*.md'):
        for link in re.findall(r'\]\(([^)]+)\)', path.read_text(encoding='utf-8')):
            if '://' not in link and not link.startswith('#'):
                assert (path.parent / link.split('#')[0]).exists(), f'Broken link: {path}: {link}'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('prepare', 'verify'))
    args = parser.parse_args()
    prepare() if args.action == 'prepare' else verify()
