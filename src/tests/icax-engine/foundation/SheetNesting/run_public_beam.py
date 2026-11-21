"""Run independent native constructors; audit originals and compare final best layouts.

The baseline is a previous run on this machine, not a public best or a commercial
solver. Tight and wide stock sizes are always kept in separate comparisons.
"""
from pathlib import Path
import argparse
import concurrent.futures
import hashlib
import html
import json
import math
import subprocess
import sys
import textwrap

ROOT = Path(__file__).resolve().parents[5]
SOURCE = ROOT / 'TestData/Plane/Nesting/umetani-isp'
TARGETS = {'shapes0': 62., 'shapes1': 56.5, 'shapes2': 26.1,
           'fu': 31.571428571428573, 'mao': 1856.5312532230084,
           'albano': 10381.591826489177, 'dagli': 59.32515212981744,
           'dighe1': 100.20741924741924, 'dighe2': 100.,
           'jakobs1': 11.633333333333333, 'jakobs2': 24.2,
           'marques': 79.04355400696869, 'shirts': 65.22513749651905,
           'swim': 6368.356450919711, 'trousers': 248.52366199082581}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def original_parts(name):
    """Read the public DAT, independently of the native JSON exporter."""
    tokens = iter((SOURCE / (name + '.dat')).read_text().split())
    assert next(tokens) == 'h='
    height = float(next(tokens))
    assert next(tokens) == 'n='
    parts = {}
    for i in range(int(next(tokens))):
        assert next(tokens) == 'd='
        quantity = int(next(tokens))
        assert next(tokens) == 'm='
        angles = [float(next(tokens)) for _ in range(int(next(tokens)))]
        assert next(tokens) == 'v='
        outer = [[float(next(tokens)), float(next(tokens))]
                 for _ in range(int(next(tokens))) ]
        parts[f'P{i:03d}'] = {'quantity': quantity, 'allowedAngles': angles,
                             'geometry': {'outer': outer}}
    assert next(tokens, None) is None
    return height, parts


def geometry(part, pose):
    from shapely.geometry import Polygon
    angle = pose['angle'] % 360
    exact = {0: (1., 0.), 90: (0., 1.), 180: (-1., 0.), 270: (0., -1.)}
    c, s = exact.get(angle, (math.cos(math.radians(angle)), math.sin(math.radians(angle))))
    return Polygon([(c*x-s*y+pose['x'], s*x+c*y+pose['y'])
                    for x, y in part['geometry']['outer']])


def audit(name, problem, result):
    """GEOS audit using original DAT geometry, quantities and allowed angles."""
    from shapely.geometry import box
    height, parts = original_parts(name)
    errors, warnings, worlds = [], [], []
    declared = {p['id']: p for p in problem['parts']}
    if len(declared) != len(problem['parts']) or set(declared) != set(parts):
        errors.append('Changed original part catalogue')
    for identity, original in parts.items():
        current = declared.get(identity, {})
        if (current.get('quantity') != original['quantity'] or
            current.get('allowedAngles') != original['allowedAngles'] or
            current.get('geometry', {}).get('outer') != original['geometry']['outer'] or
            current.get('freeRotation') is not False or
            current.get('geometry', {}).get('holes') != []):
            errors.append('Changed original part data: ' + identity)
    stocks = problem['stocks']
    if (len(stocks) != 1 or stocks[0]['height'] != height or
        stocks[0]['width'] <= 0 or stocks[0]['quantity'] != 1 or
        problem['partGap'] != 0 or problem['edgeMargin'] != 0):
        errors.append('Changed public stock/gap convention')
    used = {identity: set() for identity in parts}
    tol = problem['tolerance']
    if not math.isfinite(tol) or not 0 < tol <= 1e-7:
        raise ValueError('Unexpected audit tolerance')
    seen_stocks = set()
    extrema = []
    material_area = 0.
    for layout in result['layouts']:
        stock_key = (layout['stock'], layout['instance'])
        if stock_key in seen_stocks or stock_key != ('sheet', 0):
            errors.append('Duplicate or invalid stock identity')
        seen_stocks.add(stock_key)
        if layout['width'] != stocks[0]['width'] or layout['height'] != height:
            errors.append('Changed original stock size')
        current = []
        for pose in layout['placements']:
            identity = pose['part']
            if identity not in parts:
                errors.append('Unknown original part identity')
                continue
            part = parts[identity]
            instance = pose['instance']
            if (not isinstance(instance, int) or instance in used[identity] or
                not 0 <= instance < part['quantity']):
                errors.append('Duplicate or invalid original instance')
            used[identity].add(instance)
            if not all(math.isfinite(pose[k]) for k in ('x', 'y', 'angle')):
                errors.append('Nonfinite original transform')
                continue
            if not any(abs((pose['angle']-a+180)%360-180) <= 1e-8
                       for a in part['allowedAngles']):
                errors.append('Disallowed orientation')
            poly = geometry(part, pose)
            if not poly.is_valid:
                errors.append('Invalid original polygon')
            b = poly.bounds
            if b[0] < -tol or b[1] < -tol or b[2] > layout['width']+tol or b[3] > height+tol:
                errors.append('Outside original stock')
            for other in current:
                overlap = poly.intersection(other).area
                if overlap > 0:
                    interior = poly.buffer(-tol*.5, join_style=2).intersection(
                        other.buffer(-tol*.5, join_style=2)).area
                    if interior > tol*tol:
                        errors.append(f'Material overlap {overlap:.12g}')
                    else:
                        warnings.append({'nearContactArea': overlap})
            current.append(poly)
            extrema.append(b)
            material_area += poly.area
        worlds.append(current)
    remaining = {}
    for row in result['remaining']:
        identity, quantity = row['part'], row['quantity']
        if identity not in parts or identity in remaining or not isinstance(quantity, int) or quantity <= 0:
            errors.append('Invalid remaining quantity record')
        remaining[identity] = quantity
    for identity, part in parts.items():
        if len(used[identity]) + remaining.get(identity, 0) != part['quantity']:
            errors.append('Original quantity not conserved: ' + identity)
    for i, cert in enumerate(result.get('remnantRects', [])):
        if not cert['verified'] or cert['area'] <= 0:
            continue
        if i >= len(worlds) or cert['width'] <= 0 or cert['height'] <= 0:
            errors.append('Invalid remnant rectangle')
            continue
        q = box(cert['x'], cert['y'], cert['x']+cert['width'], cert['y']+cert['height'])
        if (cert['x'] < -tol or cert['y'] < -tol or
            cert['x']+cert['width'] > stocks[0]['width']+tol or
            cert['y']+cert['height'] > height+tol):
            errors.append('Remnant rectangle outside stock')
        if abs(q.area-cert['area']) > tol*max(1., q.length):
            errors.append('Incorrect remnant rectangle area')
        if any(q.buffer(-tol*.5, join_style=2).intersection(
                poly.buffer(-tol*.5, join_style=2)).area > tol*tol for poly in worlds[i]):
            errors.append('Claimed remnant contains original material')
    placed = sum(len(v) for v in used.values())
    required = sum(p['quantity'] for p in parts.values())
    if result['placed'] != placed or (result['complete'] and placed != required):
        errors.append('Incorrect reported original quantity or completeness')
    right = max((b[2] for b in extrema), default=0.)
    top = max((b[3] for b in extrema), default=0.)
    if abs(result['right']-right) > tol or abs(result['top']-top) > tol:
        errors.append('Incorrect reported occupied extent')
    return {'valid': not errors, 'complete': not errors and placed == required,
            'placed': placed, 'required': required, 'missing': required-placed,
            'right': right, 'top': top, 'materialArea': material_area,
            'stripFill': material_area/(right*height) if right > 0 else 0.,
            'warnings': warnings, 'errors': errors, 'linearTolerance': tol,
            'perType': {k: {'placed': len(used[k]), 'required': v['quantity']}
                        for k, v in parts.items()}}


def quality(problem, result, check):
    """The same quantized regular-remnant ordering as the native selector."""
    tol = problem['tolerance']
    area_quantum = tol*max(1., problem['stocks'][0]['width']+problem['stocks'][0]['height'])
    remnant = result['regularRemnant'] if result.get('remnantMeasured') else -math.inf
    quantize = lambda value, quantum: math.floor(value/quantum+.5)
    return (not check['valid'], check['missing'], len(result['layouts']),
            -quantize(remnant, area_quantum) if math.isfinite(remnant) else math.inf,
            quantize(check['top'], tol), quantize(check['right'], tol))


def problem_key(data):
    p = data['problem']
    return data['case'], p['stocks'][0]['width'], p['stocks'][0]['height']


def load_trials(folder):
    groups = {}
    for path in sorted(Path(folder).glob('*.json')):
        data = json.loads(path.read_text(encoding='utf8'))
        if not isinstance(data, dict) or 'problem' not in data or 'runs' not in data:
            continue
        key = problem_key(data)
        for i, run in enumerate(data['runs']):
            for stage in ('before', 'candidate', 'selected'):
                result = run[stage]
                check = audit(data['case'], data['problem'], result)
                if result['valid'] and not check['valid']:
                    raise ValueError(f'{path.name} {i} {stage}: {check["errors"]}')
                if stage == 'candidate':
                    continue
                groups.setdefault(key, []).append({
                    'source': str(path.resolve()), 'sourceHash': digest(path),
                    'case': data['case'], 'problem': data['problem'],
                    'run': i, 'constructor': run['constructor'],
                    'axis': run.get('workAxis'), 'stage': stage, 'result': result,
                    'audit': check, 'constructorWallSeconds': run.get('constructorWallSeconds'),
                    'postWallSeconds': run.get('postWallSeconds') if stage == 'selected' else 0.,
                    'constructorBudgetSeconds': data.get('secondsPerConstructor'),
                    'postBudgetSeconds': data.get('secondsPerPostprocessor') if stage == 'selected' else 0.})
    return groups


def best(trials):
    return min(trials, key=lambda t: quality(t['problem'], t['result'], t['audit']))


def render_pair(old, new, path):
    from PIL import Image, ImageDraw, ImageFont
    p = new['problem']
    width, height = p['stocks'][0]['width'], p['stocks'][0]['height']
    scale = min(870/width, 620/height)
    panel, y0 = 940, 140
    count_lines = lambda trial: textwrap.wrap('  '.join(
        f'{k}: {v["placed"]}/{v["required"]}' for k, v in trial['audit']['perType'].items()),
        width=105, break_long_words=False, break_on_hyphens=False) if trial else []
    line_count = max(1, len(count_lines(old)), len(count_lines(new)))
    im = Image.new('RGB', (panel*2, int(height*scale+y0+88+20*line_count)), 'white')
    draw = ImageDraw.Draw(im)
    font = lambda n: ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', n)
    palette = ['#75a7c4', '#bc8499', '#bb8b62', '#7f7a9a', '#a3b265', '#77a29b', '#b77eae', '#89a4ab']
    _, parts = original_parts(new['case'])
    for side, trial in enumerate((old, new)):
        x0 = side*panel+28
        if trial is None:
            draw.text((x0, 18), '此尺寸没有历史本机基线', font=font(23), fill='#243746')
            continue
        r, check = trial['result'], trial['audit']
        title = '历史本机基线最佳' if side == 0 else '本轮最终最佳'
        draw.text((x0, 14), f'{title} · {check["placed"]}/{check["required"]} 件', font=font(24), fill='#243746')
        draw.text((x0, 50), f'占用右边界 {check["right"]:.7g} · 规整余料 {r["regularRemnant"]:.7g}', font=font(19), fill='#476072')
        fill_label = '完整布局条带填充率' if check['complete'] else '当前已放件条带填充率'
        draw.text((x0, 80), f'{fill_label} {100*check["stripFill"]:.2f}% · 独立原件核验 {"通过" if check["valid"] else "失败"}', font=font(17), fill='#476072')
        yb = y0+height*scale
        pixel = lambda x, y: (x0+x*scale, yb-y*scale)
        draw.rectangle((x0, y0, x0+width*scale, yb), fill='#f4f5f7', outline='#243746', width=2)
        for layout in r['layouts']:
            for pose in layout['placements']:
                poly = geometry(parts[pose['part']], pose)
                draw.polygon([pixel(x, y) for x, y in poly.exterior.coords],
                             fill=palette[int(pose['part'][1:])%len(palette)], outline='#344c5e', width=1)
        for cert in r.get('remnantRects', []):
            if cert['verified'] and cert['area'] > 0:
                x, y, w, h = [cert[k] for k in ('x', 'y', 'width', 'height')]
                draw.rectangle((*pixel(x, y+h), *pixel(x+w, y)), outline='#14804a', width=3)
        draw.text((x0, yb+12), f'原板 {width:.8g} × {height:g}；绿色框为核验后的规整余料', font=font(16), fill='#476072')
        for line_number, counts in enumerate(count_lines(trial)):
            draw.text((x0, yb+39+line_number*20), counts, font=font(13), fill='#476072')
    im.save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    parser.add_argument('--baseline', required=True)
    parser.add_argument('--results', help='Read existing native JSON results instead of launching jobs')
    parser.add_argument('--build')
    parser.add_argument('--source-snapshot',help='Use an immutable compiled-source snapshot instead of live core hashes')
    parser.add_argument('--packages')
    parser.add_argument('--specs', default='shapes0:80,shapes0:62,shapes1:56.5,shapes2:26.1,fu:31.571428571428573',
                        help='Comma-separated case:width pairs; omit :width for the historical tight target')
    parser.add_argument('--strategies', default='nfp.beam')
    parser.add_argument('--seconds', type=float, default=30.)
    parser.add_argument('--post-seconds', type=float, default=2.)
    parser.add_argument('--attempts', type=int, default=5000000)
    parser.add_argument('--workers', type=int, default=2)
    parser.add_argument('--rotated', action='store_true')
    parser.add_argument('--reassemble', action='store_true', help='Use local reconstruction instead of translation compaction')
    parser.add_argument('--seed-results', help='Load the final best original placements from an existing raw result directory')
    args = parser.parse_args()
    if args.packages:
        sys.path.insert(0, str(Path(args.packages).resolve()))
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    if (output/'summary.json').exists():
        raise ValueError('Refusing to overwrite a completed evidence report')
    tracked = [Path(__file__).resolve(), SOURCE/'source-manifest.json']
    snapshot_evidence = None
    source_root = ROOT/'src/iCAX-Engine/foundation/SheetNesting'
    if args.results:
        result_folder = Path(args.results).resolve()
    else:
        if not args.build:
            parser.error('--build is required when --results is omitted')
        exe = Path(args.build).resolve()/'Release/SheetNestingPublicBench.exe'
        if not exe.is_file():
            raise FileNotFoundError(exe)
        if args.source_snapshot:
            from run_frontier_native import source_snapshot_inputs
            snapshot_files,snapshot_evidence=source_snapshot_inputs(args.source_snapshot,exe)
            tracked += snapshot_files
        else:
            tracked += sorted(source_root.glob('*.cpp')) + sorted(source_root.glob('*.h'))
            tracked += [source_root/'CMakeLists.txt', Path(__file__).with_name('SheetNestingPublicBench.cpp')]
        tracked.append(exe)
        result_folder = output/'raw'
        result_folder.mkdir(exist_ok=True)
    fingerprints = {str(p): digest(p) for p in tracked}
    (output/'run-start-sources.json').write_text(json.dumps(fingerprints, indent=2), encoding='utf8')
    if not args.results:
        seed_trials = load_trials(args.seed_results) if args.seed_results else None
        if seed_trials and args.rotated:
            raise ValueError('Placement seeds require the original frame')
        tasks = []
        for spec in args.specs.split(','):
            name, _, width = spec.partition(':')
            width = float(width) if width else TARGETS[name]
            for frame in ('original', 'rot90') if args.rotated else ('original',):
                tasks.append((name, width, frame))
        def execute(task):
            name, width, frame = task
            tag = f'{name}-w{width:.12g}-{frame}'
            path = result_folder/(tag+'.json')
            if path.exists():
                raise ValueError('Refusing to reuse existing native evidence: '+str(path))
            strategies = args.strategies
            if seed_trials is not None:
                key = (name, width, original_parts(name)[0])
                seed = best(seed_trials[key])
                placements = [x for layout in seed['result']['layouts'] for x in layout['placements']]
                seed_file = result_folder/(tag+'.seed.tsv')
                seed_file.write_text(''.join(f'{x["part"]}\t{x["instance"]}\t{x["x"]:.17g}\t{x["y"]:.17g}\t{x["angle"]:.17g}\n'
                                             for x in placements), encoding='utf8')
                (result_folder/(tag+'.seed-source.json')).write_text(json.dumps({
                    'source': seed['source'], 'sourceHash': seed['sourceHash'],
                    'run': seed['run'], 'stage': seed['stage'], 'seedHash': digest(seed_file)}, indent=2), encoding='utf8')
                strategies = 'seed@'+str(seed_file)
            cmd = [str(exe), str(SOURCE/(name+'.dat')), format(width, '.17g'),
                   str(path), str(args.seconds), str(args.attempts), strategies,
                   str(args.post_seconds), frame]
            if args.reassemble:
                cmd.append('reassemble')
            process = subprocess.run(cmd, capture_output=True, text=True,
                                     timeout=max(600., (args.seconds+2*args.post_seconds+10)*len(args.strategies.split(','))))
            (result_folder/(tag+'.log')).write_text(process.stdout+'\n'+process.stderr, encoding='utf8')
            if process.returncode:
                raise RuntimeError(tag+': '+process.stderr)
            print(tag+' finished', flush=True)
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
            for future in concurrent.futures.as_completed([pool.submit(execute, task) for task in tasks]):
                future.result()
    current, previous = load_trials(result_folder), load_trials(args.baseline)
    if not current:
        raise ValueError('No native result JSON files found')
    rows, sections = [], []
    for key in sorted(current):
        new = best(current[key])
        old = best(previous[key]) if key in previous else None
        if old and old['problem'] != new['problem']:
            raise ValueError('Different original problem in comparison '+str(key))
        name, width, height = key
        image_name = f'{name}-w{width:.12g}-comparison.png'
        render_pair(old, new, output/image_name)
        row = {'case': name, 'width': width, 'height': height,
               'baseline': old, 'current': new, 'image': image_name,
               'improved': old is not None and quality(new['problem'], new['result'], new['audit']) <
                           quality(old['problem'], old['result'], old['audit'])}
        rows.append(row)
        title = f'{name} · 原板 {width:.8g} × {height:g}'
        budgets = f'本轮构造预算 {new["constructorBudgetSeconds"]}s；实际构造 {new["constructorWallSeconds"]:.3f}s' if new['constructorWallSeconds'] is not None else ''
        if new.get('postBudgetSeconds'):
            budgets += f'；重排预算 {new["postBudgetSeconds"]}s，实际 {new["postWallSeconds"]:.3f}s'
        paragraphs = f'<p>{html.escape(new["constructor"])} · {html.escape(new["stage"])} · {budgets}。历史本机基线与本轮预算不同，耗时不作为算法速度结论。</p>'
        sections.append(f'<section><h2>{html.escape(title)}</h2>{paragraphs}<a href="{image_name}"><img src="{image_name}"></a></section>')
        print(name, width, 'best', new['audit']['placed'], '/', new['audit']['required'],
              'right', new['audit']['right'], 'remnant', new['result']['regularRemnant'], flush=True)
    summary = {'rows': rows, 'notes': [
        'Best layouts selected by final quality, never by improvement delta.',
        'Baseline is a previous native run on this machine, not an external published solver result.',
        'All before/candidate/selected layouts claiming native validity were independently checked against original public DAT geometry.',
        'Tight and wide stock sizes are separate. Historical tight widths are feasible-run targets, not proven optima.',
        'Strip fill is placed original material area divided by occupied right extent times original board height; incomplete layouts are explicitly labelled.']}
    (output/'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf8')
    (output/'index.html').write_text('<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>异形件重新排样：实际最佳布局</title><style>body{font:16px/1.6 "Segoe UI","Microsoft YaHei",sans-serif;max-width:1900px;margin:auto;padding:24px;color:#243746;background:#eef3f6}section{padding:18px;background:white;border:1px solid #dbe2e8;margin:22px 0}img{width:100%}a{color:#1261a2}</style><h1>异形件重新排样：实际最佳布局</h1><p>每个板材尺寸单独比较，直接展示各轮按最终质量选出的最佳结果。左侧是之前的本机实现，右侧是本轮结果。数量、原件轮廓、角度、越界、材料重叠与余料框均独立核验；有缺件的布局明确标出。</p>'+''.join(sections)+'<p><a href="summary.json">全部坐标来源、审计、预算和选择依据</a> · <a href="manifest.json">文件校验</a></p></html>', encoding='utf8')
    if fingerprints != {str(p): digest(p) for p in tracked}:
        raise RuntimeError('Source or executable changed during this evidence run')
    (output/'input-sources.json').write_text(json.dumps({'tracked': fingerprints,
        'sourceSnapshot': snapshot_evidence,
        'originalCorpus': json.loads((SOURCE/'source-manifest.json').read_text())}, indent=2), encoding='utf8')
    manifest = {str(p.relative_to(output)): digest(p) for p in output.rglob('*')
                if p.is_file() and p != output/'manifest.json'}
    (output/'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf8')


if __name__ == '__main__':
    main()
