"""Index original DXF collections and prepare original-input native PK tasks.

This module only handles source files and evidence. Nesting is performed by
the repository's SheetNestingTask --initial entry and its production library.
"""
from __future__ import annotations

import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, wait, FIRST_COMPLETED
from datetime import datetime, timezone
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import shlex
import subprocess
from threading import TIMEOUT_MAX
import time
import uuid
import zipfile

from dxf_task import straight_contour, import_shape, export
from plane_materials_textile import build_tasks as textile_tasks, iter_source_contour_preparations
from plane_materials_mechanical import build_tasks as mechanical_tasks
from plane_materials_metrics import calculate_metrics, source_quantity_rows

ROOT = Path(__file__).resolve().parents[3]
DEFAULTS = dict(stockWidthMm=3000, stockHeightMm=6000, unlimited=True,
                unlimitedRollDimensionMm=1000000,
                edgeMarginMm=.5, partGapMm=.2, allowCommonEdges=True,
                freeRotation=True, allowMirror=True, unspecifiedQuantity=1,
                unspecifiedUnits="source coordinate interpreted as mm; no resizing",
                timeLimitSeconds=60, maxPlacementAttempts=8000000,
                workers=max(1,min(32,os.cpu_count() or 1)))
UNIT_SCALES = {0:("未指定，测试按毫米",1),1:("英寸",25.4),2:("英尺",304.8),
               4:("毫米",1),5:("厘米",10),6:("米",1000),7:("千米",1000000),
               8:("微英寸",.0000254),9:("密耳",.0254),10:("码",914.4),
               12:("纳米",.000001),13:("微米",.001),14:("分米",100),17:("十亿米",1e12)}


def effective_stock(source_stock):
    """Keep source dimensions; cap an unlimited roll axis only as requested."""
    source_stock = source_stock or {}
    def known(value):
        return type(value) in (int,float) and math.isfinite(value) and value > 0
    width,height=source_stock.get("width"),source_stock.get("height")
    roll=source_stock.get("kind")=="roll"
    def unlimited(value):
        return type(value) in (int,float) and math.isfinite(value) and value < 0
    width_unlimited=roll and unlimited(width)
    height_unlimited=roll and (unlimited(height) or height is None)
    w=width if known(width) else 1000000 if width_unlimited else 3000
    h=height if known(height) else 1000000 if height_unlimited else 6000
    source=("source-roll-unlimited-capped-1000000" if roll else
            "source-finite-sheet" if known(width) and known(height) else
            "source-partial-dimensions" if known(width) or known(height) else "user-default-3000x6000")
    return dict(widthMm=w,heightMm=h,unlimited=True,quantity=None,source=source)


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    try:
        temp.write_text(json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")), encoding="utf-8")
        replace_file(temp, path)
    finally:
        temp.unlink(missing_ok=True)


def replace_file(temp, path):
    # A concurrent Windows reader can briefly deny replacement. Keep publishing
    # atomic; never truncate the document the webpage is currently reading.
    deadline = time.monotonic() + 5
    while True:
        try:
            temp.replace(path)
            return
        except PermissionError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(.025)


def csv_file(path, rows, fields):
    temp = path.with_suffix(".csv.tmp")
    with temp.open("w", encoding="utf-8-sig", newline="") as out:
        writer = csv.DictWriter(out, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)
    replace_file(temp, path)


def matching_incumbent_seconds(task, raw, final):
    """Read a final layout's own publication time, without inferring missing timing."""
    try:
        snapshot=json.loads(raw.with_suffix(".incumbent.json").read_text(encoding="utf-8-sig"))
        if not isinstance(snapshot,dict) or snapshot.get("live") is not True:
            return None
        for payload in (final,snapshot):
            if payload.get("format")!="ICAX_NESTING_TASK_RESULT_1" or payload.get("strategy")!="plane.initial":
                return None
            actual=payload["result"];validation=payload["validation"];layouts=actual["layouts"]
            if (not isinstance(layouts,list) or
                any(value is not True for value in (actual.get("valid"),actual.get("complete"),
                    validation.get("valid"),validation.get("complete"))) or validation.get("errors")!=[] or
                actual.get("remaining")!=[] or type(actual.get("remainingCount")) is not int or actual["remainingCount"]!=0 or
                type(actual.get("boards")) is not int or actual["boards"]!=len(layouts) or
                not all(isinstance(board,dict) and isinstance(board.get("placements"),list) for board in layouts) or
                sum(len(board["placements"]) for board in layouts)!=task["required"] or
                any(type(value) is not int or value!=task["required"] for value in
                    (actual.get("placed"),validation.get("placed")))):
                return None
        # JSON serialization preserves boolean-vs-number distinctions and list
        # order. No pose tolerance, coordinate normalization or sorting applies.
        def exact(value):
            return json.dumps(value,sort_keys=True,allow_nan=False,separators=(",", ":"))
        if (not isinstance(final["problem"],dict) or exact(snapshot["problem"])!=exact(final["problem"]) or
            exact(snapshot["result"]["layouts"])!=exact(final["result"]["layouts"])):
            return None
        found,whole=snapshot.get("wholeJobWallSeconds"),final.get("wholeJobWallSeconds")
        if (type(found) not in (int,float) or type(whole) not in (int,float) or
            not math.isfinite(found) or not math.isfinite(whole) or not 0<=found<=whole):
            return None
        return found
    except (OSError,UnicodeError,ValueError,KeyError,TypeError,AttributeError,OverflowError):
        return None


def native_benchmark(task, raw, root, executable_hash, concurrency, end_to_end=None):
    result=json.loads(raw.read_text(encoding="utf-8-sig"))
    actual=result["result"];validation=result["validation"];layouts=actual["layouts"]
    sheet_area=sum(b["width"]*b["height"] for b in layouts)
    benchmark=dict(status="finished",nativeWallSeconds=result["wholeJobWallSeconds"],
        boards=actual["boards"],placed=actual["placed"],required=task["required"],
        complete=actual["complete"],valid=actual["valid"],
        utilization=validation["materialArea"]/sheet_area if sheet_area else 0,
        layouts=layouts,problem=result["problem"],diagnostics=actual.get("diagnostics",[]),
        validationErrors=validation.get("errors",[]),settings=result["settings"],
        rawPath=raw.relative_to(root).as_posix(),inputSha256=task["inputSha256"],
        executableSha256=executable_hash,caseConcurrency=concurrency,
        measurementScope="native RunJob wall; original source preparation excluded; outer case concurrency recorded")
    if "remnants" in actual:benchmark["remnants"]=actual["remnants"]
    if result.get("nativeConstructionTrace"):
        trace_file=(raw.parent/result["nativeConstructionTrace"]).resolve()
        expected=raw.with_suffix(".trace-index.json").resolve()
        if trace_file!=expected or trace_file.parent!=(root/"catalog/raw").resolve():
            raise ValueError("Native construction trace is not bound to its own raw result")
        benchmark["nativeConstructionTrace"]=dict(indexPath=trace_file.relative_to(root).as_posix(),
            inputSha256=task["inputSha256"],executableSha256=executable_hash)
        benchmark["instrumentedTiming"]=bool(result.get("instrumentedTiming",True))
    if end_to_end is not None:benchmark["endToEndWallSeconds"]=end_to_end
    route=next((line for line in benchmark["diagnostics"] if line.startswith("parallel.construction portfolio ")),"")
    first_complete=result.get("firstCompleteWallSeconds",-1)
    match=re.search(r" first-complete-wall-s=([0-9.]+)",route)
    if isinstance(first_complete,(int,float)) and first_complete>=0:
        benchmark["firstCompleteWallSeconds"]=first_complete
    elif match:benchmark["firstCompleteWallSeconds"]=float(match.group(1))
    best_found=matching_incumbent_seconds(task,raw,result)
    if best_found is not None:benchmark["bestFoundWallSeconds"]=best_found
    benchmark["usedEnvelopeArea"]=validation.get("usedEnvelopeArea")
    benchmark.update(calculate_metrics(task,benchmark,material_area=validation["materialArea"]))
    return benchmark


def report_result(row,task,benchmark):
    for key,field in (("boards","icax_boards"),("placed","icax_placed"),("complete","icax_complete"),
                      ("valid","icax_valid"),("utilization","icax_utilization"),("nativeWallSeconds","icax_native_seconds"),
                      ("firstCompleteWallSeconds","icax_first_complete_seconds"),
                      ("bestFoundWallSeconds","icax_best_found_seconds")):
        row[field]=benchmark.get(key,"")
    metrics=calculate_metrics(task,benchmark)
    row["icax_material_area_mm2"]=metrics.get("materialArea")
    row["icax_stock_area_mm2"]=metrics.get("sheetArea")
    usage=metrics.get("rollUsage") or {}
    row["icax_roll_axis"]=usage.get("axis","")
    row["icax_roll_used_lengths_mm"]=json.dumps(usage["lengths"]) if "lengths" in usage else ""
    row["icax_roll_total_length_mm"]=usage.get("totalLength","")
    row["icax_roll_used_area_mm2"]=usage.get("usedArea","")
    row["icax_roll_segment_utilization"]=usage.get("utilization","")
    row["icax_metrics_notes"]="; ".join(map(str,metrics.get("metricsIssues",[])))
    remnants=benchmark.get("remnants") or {}
    row["icax_remnants_measured"]=remnants.get("measured","")
    row["icax_last_regular_remnant_mm2"]=remnants.get("lastSheetRegularArea","") if remnants.get("measured") else ""
    row["icax_last_regular_remnant_upper_mm2"]=remnants.get("lastSheetAreaUpperBound","") if remnants.get("measured") else ""
    row["icax_remnants_json"]=json.dumps(remnants,ensure_ascii=False,separators=(",", ":")) if remnants else ""


def geometry(path):
    # Preserve originals byte-for-byte. Only declared unit conversion is applied
    # to the separate native input; unsupported drawings remain downloadable.
    text = path.read_bytes()[:131072].decode("latin1")
    match = re.search(r"\$INSUNITS\s*\r?\n\s*70\s*\r?\n\s*(\d+)", text)
    code = int(match.group(1)) if match else 0
    label, scale = UNIT_SCALES.get(code, (f"不支持的单位代码 {code}", None))
    units = dict(code=code, label=label, scaleToMm=scale, assumed=code == 0)
    if scale is None:
        return units, dict(status="unsupported", reason=label, partCount=None), None
    if path.stat().st_size > 32 * 1024 * 1024:
        return units, dict(status="unsupported", reason="DXF超过现有读取器32MB上限", partCount=None), None
    try:
        points = straight_contour(path)
        points = [[x*scale, y*scale] for x,y in points]
        area = abs(sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(points, points[1:]+points[:1])))/2
        if not math.isfinite(area) or area <= 0:
            raise ValueError("Degenerate or nonfinite contour")
        return units, dict(status="ready", reason="单个闭合直线轮廓", partCount=1,
            entityTypes=["closed-polyline"], areaMm2=area, vertexCount=len(points)), points
    except (ValueError, KeyError, UnicodeError, OverflowError) as error:
        reason = str(error).replace(str(path), path.name)
        return units, dict(status="unsupported", reason=reason, partCount=None), None


def existing_cases(root):
    result = []
    for manifest in sorted(root.glob("*/manifest.json")):
        try:
            data = json.loads(manifest.read_text(encoding="utf-8-sig"))
        except (ValueError, OSError):
            continue
        if not isinstance(data,dict) or not data.get("caseID") or "parts" not in data:
            continue
        result.append(dict(id="existing-"+data["caseID"], label=data["caseID"],
            collection="existing-cases", kind="benchmark", sourcePath=manifest.relative_to(root).as_posix(),
            parts=[dict(sourcePath=(manifest.parent/p["file"]).relative_to(root).as_posix(),
                quantity=p["quantity"], quantitySource="original-case-manifest") for p in data["parts"]],
            # referenceConditions is the previous test setup, not source stock.
            sourceStock=None, sourceRules=data.get("referenceConditions",{}), issues=[]))
    return result


def build(root):
    root = root.resolve()
    catalog = root/"catalog"
    catalog.mkdir(exist_ok=True)
    central = root/"DXF"
    central.mkdir(exist_ok=True)
    paths = sorted(p for p in root.rglob("*") if p.is_file() and p.suffix.lower()==".dxf"
                   and p.relative_to(root).parts[0] not in {"DXF","catalog"})
    assets, by_hash, by_source, contours = [], {}, {}, {}
    for index,path in enumerate(paths):
        source = path.relative_to(root).as_posix()
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        asset = by_hash.get(digest)
        if asset is None:
            name = re.sub(r'[^\w.\-]+', '_', path.stem, flags=re.UNICODE)[:100] or "part"
            dest = central/f"{name}__{digest[:16]}.dxf"
            if dest.exists():
                if hashlib.sha256(dest.read_bytes()).hexdigest()!=digest:
                    raise ValueError(f"Central file differs: {dest}")
            else:
                shutil.copyfile(path,dest)
            units, info, points = geometry(path)
            asset = dict(id=digest[:24], name=path.name, relativePath=dest.relative_to(root).as_posix(),
                sha256=digest, bytes=path.stat().st_size, sourcePaths=[], units=units,
                geometry=info, taskCount=0, collections=[])
            by_hash[digest]=asset
            assets.append(asset)
            if points is not None: contours[asset["id"]]=points
        asset["sourcePaths"].append(source)
        by_source[source]=asset
        if index%1500==0: print(f"Indexed DXF {index+1}/{len(paths)}", flush=True)
    tasks = textile_tasks(root)+mechanical_tasks(root)+existing_cases(root)
    if len({t["id"] for t in tasks})!=len(tasks):
        raise ValueError("Duplicate task IDs")
    part_rows, task_rows = [], []
    for task in tasks:
        task.setdefault("kind","benchmark")
        task.setdefault("issues",[])
        source_stock=task.get("sourceStock") or {}
        stock=effective_stock(source_stock)
        task["stocks"]=[stock]
        task["rules"]={k:DEFAULTS[k] for k in ("edgeMarginMm","partGapMm","allowCommonEdges","freeRotation","allowMirror")}
        task["rules"]["profile"]="统一PK参数；来源规则另列，未作为原基准复现"
        task["missingAssets"]=[]
        unsupported=[]
        problem_parts=[]
        for i,part in enumerate(task["parts"]):
            source=part.get("sourcePath")
            asset=by_source.get(source)
            part["assetId"]=asset["id"] if asset else None
            part["id"]=f"P{i:04d}"
            if not asset:
                task["missingAssets"].append(part.get("requestedDxf") or source or part["id"])
            else:
                asset["taskCount"]+=1
                if task["collection"] not in asset["collections"]:asset["collections"].append(task["collection"])
                if asset["geometry"]["status"]!="ready":unsupported.append(asset["name"])
                else:
                    problem_parts.append(dict(id=part["id"],quantity=part["quantity"],material="pk-material",
                        freeRotation=True,allowMirror=mirror_permission(part,task),allowedAngles=[],geometry=dict(radius=0,center=[0,0],
                            outer=contours[asset["id"]],holes=[])))
            part_rows.append(dict(task_id=task["id"],task_name=task["label"],collection=task["collection"],
                kind=task["kind"],part_id=part["id"],dxf_file=asset["relativePath"] if asset else "",
                source_dxf=source or part.get("requestedDxf", ""),quantity=part["quantity"],
                quantity_source=part.get("quantitySource","source-task-quantity"),
                units=asset["units"]["label"] if asset else "",scale_to_mm=asset["units"]["scaleToMm"] if asset else "",
                stock_width_mm=stock["widthMm"],stock_height_mm=stock["heightMm"],stock_quantity="unlimited",
                stock_source=stock["source"],edge_margin_mm=.5,part_gap_mm=.2,allow_common_edges=True,free_rotation=True))
        task["required"]=sum(p["quantity"] for p in task["parts"])
        if task["kind"]=="reference":status="reference"
        elif task["missingAssets"]:status="missing-assets"
        elif unsupported:status="unsupported-geometry"
        elif any("歧义" in str(x) or "ambig" in str(x).lower() for x in task["issues"]):status="needs-review"
        else:status="ready"
        task["status"]=status
        task["benchmark"]=dict(status="pending" if status=="ready" else "blocked",required=task["required"])
        if unsupported:task["issues"].append(f"现有原始DXF导入器不支持: {', '.join(unsupported[:6])}")
        if status=="ready":
            problem=dict(parts=problem_parts,stocks=[dict(id="stock",width=stock["widthMm"],height=stock["heightMm"],
                quantity=max(1,task["required"]),cost=1,material="pk-material")],partGap=.2,edgeMargin=.5,
                tolerance=1e-7,allowCommonEdges=True)
            # Each piece on its own board is a sufficient finite upper bound.
            # Inventory zero means no stock, never unlimited in the native API.
            target=catalog/"inputs"/(task["id"]+".problem")
            target.parent.mkdir(exist_ok=True)
            if target.exists():
                previous=target.read_bytes()
                staging=target.with_suffix(".prepared")
                if staging.exists():staging.unlink()
                export(problem,staging)
                if previous!=staging.read_bytes():staging.replace(target)
                else:staging.unlink()
            else:export(problem,target)
            task["inputPath"]=target.relative_to(root).as_posix()
            task["inputSha256"]=hashlib.sha256(target.read_bytes()).hexdigest()
        task_rows.append(dict(task_id=task["id"],task_name=task["label"],collection=task["collection"],
            kind=task["kind"],status=status,specifications=len(task["parts"]),required=task["required"],
            missing_specifications=len(task["missingAssets"]),stock_width_mm=stock["widthMm"],
            stock_height_mm=stock["heightMm"],stock_quantity="unlimited",stock_source=stock["source"],
            source_stock=json.dumps(source_stock,ensure_ascii=False),issues="; ".join(map(str,task["issues"]))))
    archives=[]
    for path in sorted(root.glob("*.zip")):
        with zipfile.ZipFile(path) as z:
            members=[m for m in z.infolist() if not m.is_dir()]
            archives.append(dict(name=path.name,directory=path.stem,files=len(members),
                dxfCount=sum(Path(m.filename).suffix.lower()==".dxf" for m in members),
                sha256=hashlib.sha256(path.read_bytes()).hexdigest(),status="extracted"))
    inventory=[]
    for asset in assets:
        inventory.append(dict(asset_id=asset["id"],dxf_file=asset["relativePath"],name=asset["name"],
            source_paths=" | ".join(asset["sourcePaths"]),sha256=asset["sha256"],bytes=asset["bytes"],
            collections=" | ".join(asset["collections"]),task_count=asset["taskCount"],
            units=asset["units"]["label"],scale_to_mm=asset["units"]["scaleToMm"],
            units_assumed=asset["units"]["assumed"],geometry_status=asset["geometry"]["status"],
            part_count_in_dxf=asset["geometry"]["partCount"],geometry_note=asset["geometry"]["reason"]))
    csv_file(catalog/"dxf_inventory.csv",inventory,list(inventory[0]))
    csv_file(catalog/"task_parts.csv",part_rows,list(part_rows[0]))
    csv_file(catalog/"tasks.csv",task_rows,list(task_rows[0]))
    summary=dict(archiveCount=len(archives),sourceDxfCount=len(paths),assetCount=len(assets),
        taskCount=len(tasks),taskStatuses=dict(Counter(t["status"] for t in tasks)),
        readyTasks=sum(t["status"]=="ready" for t in tasks),
        assumedQuantityRows=sum(p["quantity_source"]=="benchmark-default-one" for p in part_rows),
        assumedUnitsAssets=sum(a["units"]["assumed"] for a in assets),
        partRows=len(part_rows),missingArchiveNames=["01_textile_dxf_2500.zip"] if not (root/"01_textile_dxf_2500.zip").exists() else [])
    document=dict(schemaVersion=1,generatedAt=datetime.now(timezone.utc).isoformat(),defaults=DEFAULTS,
                  summary=summary,archives=archives,assets=assets,tasks=tasks)
    atomic_json(catalog/"catalog.json",document)
    print(json.dumps(summary,ensure_ascii=False),flush=True)


def reprofile(root):
    """Change only stock records in an already verified source catalogue."""
    root=root.resolve();catalog=root/"catalog"
    document=json.loads((catalog/"catalog.json").read_text(encoding="utf-8"))
    changed=0
    for task in document["tasks"]:
        stock=effective_stock(task.get("sourceStock"))
        task["stocks"]=[stock]
        if task.get("inputPath"):
            path=root/task["inputPath"]
            before=path.read_bytes()
            if hashlib.sha256(before).hexdigest()!=task["inputSha256"]:
                raise ValueError(f"Input changed outside catalogue: {task['id']}")
            lines=before.decode("utf-8").splitlines()
            if int(lines[1].split()[1])!=1:
                raise ValueError("Stock-only profiling expects one source stock per prepared task")
            fields=shlex.split(lines[-1])
            if len(fields)!=6:
                raise ValueError("Invalid prepared stock record")
            lines[-1]=(f'{json.dumps(fields[0])} {float(stock["widthMm"]):.17g} '
                f'{float(stock["heightMm"]):.17g} {fields[3]} {fields[4]} {json.dumps(fields[5])}')
            newline="\r\n" if b"\r\n" in before else "\n"
            after=(newline.join(lines)+newline).encode("utf-8")
            if after!=before:
                temp=path.with_suffix(".problem.tmp");temp.write_bytes(after);temp.replace(path);changed+=1
            task["inputSha256"]=hashlib.sha256(after).hexdigest()
    by_id={t["id"]:t for t in document["tasks"]}
    for name in ("task_parts.csv","tasks.csv"):
        with (catalog/name).open(encoding="utf-8-sig",newline="") as source:
            reader=csv.DictReader(source);fields=reader.fieldnames;rows=list(reader)
        for row in rows:
            stock=by_id[row["task_id"]]["stocks"][0]
            row.update(stock_width_mm=stock["widthMm"],stock_height_mm=stock["heightMm"],stock_source=stock["source"])
        csv_file(catalog/name,rows,fields)
    document["defaults"]=DEFAULTS
    document["generatedAt"]=datetime.now(timezone.utc).isoformat()
    document["stockPolicy"]="Preserve source dimensions; unlimited roll axes=1000000; unknown dimensions=3000x6000."
    atomic_json(catalog/"catalog.json",document)
    print(json.dumps(dict(stockInputsChanged=changed,tasks=len(document["tasks"])),ensure_ascii=False),flush=True)


def import_material(path,scale,material_semantics=None):
    """Keep strict imports; allow explicit budgeted joins only for open chains."""
    semantics={"material_semantics":material_semantics} if material_semantics is not None else {}
    try:
        return import_shape(path,scale_to_mm=scale,tolerance_mm=.01,**semantics)
    except ValueError as error:
        if not str(error).startswith("Open or branching"):
            raise
        return import_shape(path,scale_to_mm=scale,tolerance_mm=.01,connection_tolerance_mm=1e-5,**semantics)


def source_semantics_path(root,asset):
    path=root/"catalog/source-semantics"/(asset["id"]+".json")
    return path if path.is_file() else None


def refresh_imports(root,asset_ids=None):
    """Revisit source DXFs needed by outstanding material tasks."""
    root=root.resolve();catalog=root/"catalog"
    document=json.loads((catalog/"catalog.json").read_text(encoding="utf-8"))
    assets={a["id"]:a for a in document["assets"]}
    needed={part.get("assetId") for task in document["tasks"]
        if task["kind"]!="reference" and task["status"] not in {"ready","reference"}
        for part in task.get("sourceParts",task["parts"])}
    declarations={a["id"]:source_semantics_path(root,a) for a in document["assets"]}
    semantics_updates={a["id"] for a in document["assets"] if declarations[a["id"]] is not None and
        hashlib.sha256(declarations[a["id"]].read_bytes()).hexdigest()!=
        (a["geometry"].get("metadata",{}).get("materialSemantics") or {}).get("documentSha256")}
    shapes={};converted=0;components=0
    revised_assets=set()
    for asset in document["assets"]:
        if (asset["geometry"]["status"]=="ready" and asset["id"] not in semantics_updates) or asset["id"] not in needed|semantics_updates or (asset_ids is not None and asset["id"] not in asset_ids):continue
        scale=asset["units"].get("scaleToMm")
        if scale is None:continue
        path=root/asset["relativePath"]
        try:
            imported=import_material(path,scale,declarations[asset["id"]])
            source_hash=hashlib.sha256(path.read_bytes()).hexdigest()
            if source_hash!=asset["sha256"]:raise ValueError("Original DXF changed")
            geometries=imported["geometries"]
            info=imported["metadata"]
            asset["geometry"]=dict(status="ready",reason="共享CAD读取器已解析闭合材料与孔",partCount=len(geometries),
                exact=info["exact"],curveApproximation=info["curveApproximation"],maxDeviationMm=info["maxDeviationMm"],
                commonEdgeEligibility=info["commonEdgeEligibility"],metadata=info)
            atomic_json(catalog/"geometry"/(asset["id"]+".json"),dict(sha256=source_hash,**imported))
            shapes[asset["id"]]=geometries;converted+=1;components+=len(geometries)
            if asset["id"] in semantics_updates:revised_assets.add(asset["id"])
        except (ValueError,RuntimeError,OSError,KeyError) as error:
            if asset["id"] in semantics_updates:
                asset["geometry"]["status"]="unsupported"
                revised_assets.add(asset["id"])
            asset["geometry"]["reason"]=str(error).replace(str(path),asset["name"])
            proof={key:getattr(error,attribute) for attribute,key in (
                ("hatch_boundary_mappings","hatchBoundaryMappings"),
                ("dimension_copy_mappings","dimensionCopyMappings"),
                ("source_role_validation","sourceRoleValidation"),
                ("source_role_provenance","sourceRoleProvenance"),
                ("material_semantics_diagnostics","materialSemantics")) if getattr(error,attribute,None) is not None}
            if proof:asset["geometry"]["sourceDiagnostics"]=proof
            else:asset["geometry"].pop("sourceDiagnostics",None)
            non_material=getattr(error,"non_material_entities",None)
            if non_material is not None:
                asset["geometry"]["nonMaterialEntities"]=non_material
    activated=[]
    for task in document["tasks"]:
        originals=task.get("sourceParts",task["parts"])
        revised=any(part.get("assetId") in revised_assets for part in originals)
        if task["kind"]!="reference":
            task["issues"]=[s for s in task["issues"] if not str(s).startswith(
                ("现有原始DXF导入器不支持:","材料轮廓解析待处理:"))]
            blocked={p.get("assetId") for p in task.get("sourceParts",task["parts"])
                if p.get("assetId") in assets and assets[p["assetId"]]["geometry"]["status"]!="ready"}
            if blocked:
                reasons=[assets[key]["name"]+": "+assets[key]["geometry"]["reason"] for key in sorted(blocked)]
                task["issues"].append("材料轮廓解析待处理: "+"; ".join(reasons))
                if revised:task["status"]="unsupported-geometry"
        if task["kind"]=="reference" or task["missingAssets"] or (task["status"]=="ready" and not revised):continue
        if not all(part.get("assetId") in assets and assets[part["assetId"]]["geometry"]["status"]=="ready" for part in originals):continue
        if any("ambiguous" in str(s).lower() or "歧义" in str(s) for s in task["issues"]):
            task["status"]="needs-review";continue
        task["sourceParts"]=originals
        previous_input={"inputPath":task.get("inputPath"),"inputSha256":task.get("inputSha256"),"required":task.get("required")}
        prepared=[];native=[]
        for source_index,original in enumerate(originals):
            asset=assets[original["assetId"]]
            geometries=shapes.get(asset["id"])
            if geometries is None:
                cache=catalog/"geometry"/(asset["id"]+".json")
                if cache.exists():geometries=json.loads(cache.read_text(encoding="utf-8"))["geometries"]
                else:
                    points=straight_contour(root/asset["relativePath"]);scale=asset["units"]["scaleToMm"]
                    geometries=[dict(radius=0,center=[0,0],outer=[[x*scale,y*scale] for x,y in points],holes=[])]
            for component,shape in enumerate(geometries):
                part=dict(original)
                part["id"]=f"P{source_index:04d}"+(f"_C{component:03d}" if len(geometries)>1 else "")
                part["componentIndex"]=component;part["componentCount"]=len(geometries)
                part["geometryImport"]=asset["geometry"]
                prepared.append(part)
                native.append(dict(id=part["id"],quantity=part["quantity"],material="pk-material",freeRotation=True,allowMirror=mirror_permission(part,task),
                    allowedAngles=[],geometry=shape))
        task["parts"]=prepared;task["required"]=sum(p["quantity"] for p in prepared)
        task["status"]="ready";task["benchmark"]=dict(status="pending",required=task["required"])
        stock=task["stocks"][0]
        problem=dict(parts=native,stocks=[dict(id="stock",width=stock["widthMm"],height=stock["heightMm"],
            quantity=max(1,task["required"]),cost=1,material="pk-material")],partGap=.2,edgeMargin=.5,tolerance=1e-7,allowCommonEdges=True)
        staging=catalog/"inputs"/(task["id"]+"-"+uuid.uuid4().hex+".refreshed")
        export(problem,staging);digest=hashlib.sha256(staging.read_bytes()).hexdigest()
        # A running, version-bound job may still be reading the previous input.
        # Publish corrected material as a new immutable file, never overwrite it.
        if previous_input["inputSha256"]:
            target=catalog/"inputs"/(task["id"]+"-"+digest[:20]+".problem")
        else:target=catalog/"inputs"/(task["id"]+".problem")
        if target.exists():
            if hashlib.sha256(target.read_bytes()).hexdigest()!=digest:raise ValueError("Conflicting immutable native input")
            staging.unlink()
        else:staging.replace(target)
        if previous_input["inputSha256"] and previous_input["inputSha256"]!=digest:
            task.setdefault("inputHistory",[]).append(dict(previous_input,reason="source machining semantics corrected; original DXF preserved"))
        task["inputPath"]=target.relative_to(root).as_posix();task["inputSha256"]=digest
        activated.append(task["id"])
    save_catalog_views(root,document)
    print(json.dumps(dict(importedAssets=converted,materialComponents=components,newReadyTasks=len(activated),ready=document["summary"]["readyTasks"],activated=activated),ensure_ascii=False),flush=True)


def save_catalog_views(root,document):
    """Publish all preparation tables from the same records as the website."""
    root=root.resolve();catalog=root/"catalog"
    assets={a["id"]:a for a in document["assets"]}
    inventory=[];part_rows=[];task_rows=[]
    for asset in document["assets"]:
        inventory.append(dict(asset_id=asset["id"],dxf_file=asset["relativePath"],name=asset["name"],
            source_paths=" | ".join(asset["sourcePaths"]),sha256=asset["sha256"],bytes=asset["bytes"],
            collections=" | ".join(asset["collections"]),task_count=asset["taskCount"],units=asset["units"]["label"],
            scale_to_mm=asset["units"]["scaleToMm"],units_assumed=asset["units"]["assumed"],
            geometry_status=asset["geometry"]["status"],part_count_in_dxf=asset["geometry"]["partCount"],
            geometry_note=asset["geometry"]["reason"]))
    for task in document["tasks"]:
        stock=task["stocks"][0]
        for part in source_quantity_rows(task):
            asset=assets.get(part.get("assetId"))
            source=part.get("geometrySource") or {}
            units=asset["units"] if asset else source.get("units",{})
            part_rows.append(dict(task_id=task["id"],task_name=task["label"],collection=task["collection"],kind=task["kind"],
                part_id=part["id"],dxf_file=asset["relativePath"] if asset else "",source_dxf=part.get("sourcePath") or part.get("requestedDxf", ""),
                quantity=part["quantity"],quantity_source=part.get("quantitySource","source-task-quantity"),
                units=units.get("label",""),scale_to_mm=units.get("scaleToMm",""),
                original_dxf_available=bool(asset),geometry_source_kind=source.get("kind","original-dxf" if asset else ""),
                contour_source=source.get("path",""),contour_source_sha256=source.get("sha256",""),
                contour_shape_hash=source.get("shapeHash",""),
                stock_width_mm=stock["widthMm"],stock_height_mm=stock["heightMm"],stock_quantity="unlimited",stock_source=stock["source"],
                edge_margin_mm=.5,part_gap_mm=.2,allow_common_edges=True,free_rotation=True,
                allow_mirror=mirror_permission(part,task),
                quantity_meaning="original DXF copies; source contours use original part quantity",
                components_in_dxf=part.get("materialComponentCount"),native_pieces=part.get("nativePieceQuantity"),
                native_part_ids=" | ".join(part.get("nativePartIds",[])),
                curve_tolerance_mm=asset["geometry"].get("maxDeviationMm",0) if asset else 0 if source.get("exact") else ""))
        task_rows.append(dict(task_id=task["id"],task_name=task["label"],collection=task["collection"],kind=task["kind"],status=task["status"],
            specifications=len(task["parts"]),required=task["required"],source_specifications=len(source_quantity_rows(task)),
            source_quantity=sum(p["quantity"] for p in source_quantity_rows(task)),missing_specifications=len(task["missingAssets"]),
            input_source=task.get("inputSource","original-dxf"),missing_original_dxf_specifications=len(task.get("missingDxfAssets",[])),
            stock_width_mm=stock["widthMm"],stock_height_mm=stock["heightMm"],stock_quantity="unlimited",stock_source=stock["source"],
            source_stock=json.dumps(task.get("sourceStock") or {},ensure_ascii=False),issues="; ".join(map(str,task["issues"]))))
    for name,rows in (("dxf_inventory.csv",inventory),("task_parts.csv",part_rows),("tasks.csv",task_rows)):
        csv_file(catalog/name,rows,list(rows[0]))
    document["summary"]["taskStatuses"]=dict(Counter(t["status"] for t in document["tasks"]))
    document["summary"]["readyTasks"]=sum(t["status"]=="ready" for t in document["tasks"])
    document["summary"]["partRows"]=len(part_rows)
    document["summary"]["nativePartRows"]=sum(len(task["parts"]) for task in document["tasks"])
    document["summary"]["assumedQuantityRows"]=sum(part.get("quantitySource")=="benchmark-default-one"
        for task in document["tasks"] for part in source_quantity_rows(task))
    document["summary"]["sourceContourTasks"]=sum(t.get("inputSource")=="source-contours" for t in document["tasks"])
    document["summary"]["pendingDxfTasks"]=sum(bool(t.get("missingDxfAssets")) for t in document["tasks"])
    document["generatedAt"]=datetime.now(timezone.utc).isoformat()
    atomic_json(catalog/"catalog.json",document)


def prepare_source_contours(root):
    """Use supplied original contours without manufacturing any missing DXF."""
    root=root.resolve();catalog=root/"catalog"
    document=json.loads((catalog/"catalog.json").read_text(encoding="utf-8"))
    by_id={task["id"]:task for task in document["tasks"]}
    ready_inputs={task["id"]:(task.get("inputSha256"),hashlib.sha256((root/task["inputPath"]).read_bytes()).hexdigest())
        for task in document["tasks"] if task["status"]=="ready"}
    prepared=[];invalid=[]
    for facts in iter_source_contour_preparations(root,document["tasks"]):
        task=by_id[facts["taskId"]]
        if task["status"]!="missing-assets":raise ValueError("Source preparation cannot replace an existing ready input")
        if sum(part["quantity"] for part in facts["parts"])!=task["required"]:
            raise ValueError("Source preparation changed original demand")
        if facts["status"]=="ready":
            stock=effective_stock(task.get("sourceStock"))
            permissions={identity:mirror_permission(part,task) for part in facts["parts"]
                for identity in (part.get("nativePartIds") or [part["id"]])}
            native_parts=[dict(part,allowMirror=permissions.get(part["id"],mirror_permission(part,task))) for part in facts["nativeParts"]]
            problem=dict(parts=native_parts,stocks=[dict(id="stock",width=stock["widthMm"],height=stock["heightMm"],
                quantity=max(1,task["required"]),cost=1,material="pk-material")],
                partGap=.2,edgeMargin=.5,tolerance=1e-7,allowCommonEdges=True)
            target=catalog/"inputs"/(task["id"]+".problem");staging=target.with_suffix(".source-contours.tmp")
            staging.unlink(missing_ok=True);export(problem,staging);replace_file(staging,target)
            task.update(stocks=[stock],inputPath=target.relative_to(root).as_posix(),
                inputSha256=hashlib.sha256(target.read_bytes()).hexdigest())
            prepared.append(task["id"])
        else:invalid.append(task["id"])
        task.update(status=facts["status"],parts=facts["parts"],inputSource="source-contours",
            missingAssets=[] if facts["status"]!="source-unavailable" else task["missingAssets"],
            missingDxfAssets=facts["missingDxfAssets"],sourceProvenance=facts["sourceProvenance"],issues=facts["issues"],
            invalidSourceShapes=facts.get("invalidSourceShapes",[]),
            benchmark=dict(status="pending" if facts["status"]=="ready" else "blocked",required=task["required"]))
    for identity,(recorded,original) in ready_inputs.items():
        task=by_id[identity]
        if task.get("inputSha256")!=recorded or hashlib.sha256((root/task["inputPath"]).read_bytes()).hexdigest()!=original:
            raise ValueError("Source preparation changed a previously ready input")
    save_catalog_views(root,document)
    print(json.dumps(dict(newReadyTasks=len(prepared),ready=document["summary"]["readyTasks"],
        sourceInvalid=invalid,originalReadyInputsUnchanged=len(ready_inputs),generatedDxfFiles=0),ensure_ascii=False),flush=True)


def mirror_permission(part,task=None):
    value=part.get("allowMirror",(task or {}).get("rules",{}).get("allowMirror",DEFAULTS["allowMirror"]))
    if type(value) is not bool:raise ValueError("Mirror permission must be an explicit boolean")
    return value


def mirror_permissions_signature(task):
    permissions={identity:mirror_permission(part,task) for part in task["parts"]
        for identity in (part.get("nativePartIds") or [part["id"]])}
    return hashlib.sha256(json.dumps(permissions,sort_keys=True,separators=(",", ":")).encode("utf-8")).hexdigest()


def apply_mirror_permissions(root,document,tasks):
    """Apply the declared PK rule only to selected runs, retaining immutable inputs."""
    from export_original_dxf import read_original
    catalog=root/"catalog";inputs=(catalog/"inputs").resolve();changed=[]
    for task in tasks:
        source=(root/task["inputPath"]).resolve()
        if not source.is_relative_to(inputs):raise ValueError("Native input is outside the catalog input directory")
        if hashlib.sha256(source.read_bytes()).hexdigest()!=task["inputSha256"]:
            raise ValueError("Native input does not match its recorded source hash")
        problem=read_original(source)
        permissions={identity:mirror_permission(part,task) for part in task["parts"]
            for identity in (part.get("nativePartIds") or [part["id"]])}
        differs=False
        for part in problem["parts"]:
            if part["id"] not in permissions:raise ValueError("Native part has no declared source permission mapping")
            desired=permissions[part["id"]]
            differs|=part.get("allowMirror",False)!=desired
            part["allowMirror"]=desired
        task.setdefault("rules",{}).setdefault("allowMirror",DEFAULTS["allowMirror"])
        if not differs:continue
        staging=inputs/(task["id"]+"-"+uuid.uuid4().hex+".mirror-permissions")
        if staging.parent!=inputs:raise ValueError("Invalid task input identity")
        export(problem,staging);digest=hashlib.sha256(staging.read_bytes()).hexdigest()
        target=inputs/(task["id"]+"-"+digest[:20]+".problem")
        if target.exists():
            if hashlib.sha256(target.read_bytes()).hexdigest()!=digest:raise ValueError("Conflicting immutable mirror input")
            staging.unlink()
        else:staging.replace(target)
        previous={key:task.get(key,"") for key in ("inputPath","inputSha256")}
        task.setdefault("inputHistory",[]).append(dict(previous,reason="declared mirror permissions; original contours and demand unchanged"))
        task.update(inputPath=target.relative_to(root).as_posix(),inputSha256=digest)
        changed.append(task["id"])
    if changed:save_catalog_views(root,document)
    return changed


def run(root, executable, seconds, attempts, workers, limit, collection, first_task="", concurrency=1, construction_trace=False, only_task=""):
    catalog=root/"catalog"
    data=json.loads((catalog/"catalog.json").read_text(encoding="utf-8"))
    tasks=[t for t in data["tasks"] if t["status"]=="ready" and (not collection or t["collection"]==collection)]
    if only_task:
        tasks=[task for task in tasks if task["id"]==only_task]
        if len(tasks)!=1:raise ValueError("Requested task is missing, ambiguous or not ready: "+only_task)
    priorities={identity:index for index,identity in enumerate(first_task.split(",")) if identity}
    order={t["id"]:index for index,t in enumerate(tasks)}
    tasks.sort(key=lambda t:(priorities.get(t["id"],len(priorities)),order[t["id"]]))
    executable_hash=hashlib.sha256(executable.read_bytes()).hexdigest()
    program_directory=catalog/"programs"/executable_hash
    program_directory.mkdir(parents=True,exist_ok=True)
    pinned=program_directory/executable.name
    if not pinned.exists():shutil.copy2(executable,pinned)
    if hashlib.sha256(pinned.read_bytes()).hexdigest()!=executable_hash:
        raise ValueError("Pinned native executable does not match its source version")
    for library in executable.parent.glob("*.dll"):
        destination=program_directory/library.name
        if not destination.exists():shutil.copy2(library,destination)
    def signature(task):
        value=dict(inputSha256=task["inputSha256"],executableSha256=executable_hash,
                    timeLimitSeconds=seconds,maxPlacementAttempts=attempts,workers=workers,caseConcurrency=concurrency,
                    mirrorPermissionPolicy="allow-unless-explicitly-restricted-v1",
                    mirrorPermissionsSha256=mirror_permissions_signature(task))
        if construction_trace:value["constructionTraceFormat"]="native-construction-jsonl-v2"
        return value
    def trace_descriptor(task,raw):
        return dict(indexPath=raw.with_suffix(".trace-index.json").relative_to(root).as_posix(),
            inputSha256=task["inputSha256"],executableSha256=executable_hash,runSignature=signature(task))
    def bind_trace(task,raw,benchmark):
        if not construction_trace:return
        native_declared=bool(benchmark.get("nativeConstructionTrace"))
        benchmark["nativeConstructionTrace"]=trace_descriptor(task,raw)
        try:
            if not native_declared:raise ValueError("Native result did not declare its construction trace")
            index=json.loads(raw.with_suffix(".trace-index.json").read_text(encoding="utf-8-sig"))
            if index.get("format")!="native-construction-jsonl" or index.get("schemaVersion")!=2:
                raise ValueError("Unsupported native construction trace format")
            if index.get("resultFile")!=raw.name or index.get("problem")!=benchmark.get("problem"):
                raise ValueError("Native construction trace does not match its result and original problem")
            if index.get("observerFailed") or not index.get("runCompleted"):
                raise ValueError("Native construction observation did not finish")
            benchmark["constructionTraceSummary"]={key:index.get(key) for key in
                ("eventsWritten","eventsOmitted","truncated","observerFailed","runCompleted","routes")}
            benchmark["instrumentedTiming"]=True
        except (OSError,ValueError) as error:
            benchmark["constructionTraceError"]=str(error)
    def current(task):
        try:
            previous=json.loads((catalog/"results"/(task["id"]+".json")).read_text(encoding="utf-8"))
            benchmark=previous.get("benchmark",{})
            return (previous.get("taskId")==task["id"] and previous.get("runSignature")==signature(task)
                    and benchmark.get("status")=="finished"
                    and (not construction_trace or not benchmark.get("constructionTraceError")))
        except (OSError,ValueError):return False
    # Native evidence survives an interrupted publisher. Recover only the exact
    # invocation bound by its own locator, never a similarly named historical run.
    for task in tasks:
        if current(task):continue
        locator=catalog/"live"/(task["id"]+".json")
        try:
            live=json.loads(locator.read_text(encoding="utf-8-sig"))
            if live.get("taskId")!=task["id"] or live.get("runSignature")!=signature(task):continue
            relative=live.get("rawPath") or str(live["incumbentPath"]).removesuffix(".incumbent.json")+".json"
            raw=(root/relative).resolve()
            if raw.parent!=(catalog/"raw").resolve() or not raw.name.startswith(task["id"]+"-"):continue
            benchmark=native_benchmark(task,raw,root,executable_hash,concurrency)
            bind_trace(task,raw,benchmark)
            benchmark["recoveredNativeEvidence"]=True
            atomic_json(catalog/"results"/(task["id"]+".json"),dict(taskId=task["id"],runSignature=signature(task),benchmark=benchmark))
            atomic_json(locator,dict(taskId=task["id"],active=False,runSignature=signature(task)))
            print(json.dumps(dict(task=task["id"],status="recovered",placed=benchmark["placed"],required=task["required"])),flush=True)
        except (OSError,ValueError,KeyError):continue
    queue=[t for t in tasks if not current(t)]
    if limit:queue=queue[:limit]
    apply_mirror_permissions(root,data,queue)
    report=[]
    for task in data["tasks"]:
        stock=task["stocks"][0]
        row=dict(task_id=task["id"],task_name=task["label"],collection=task["collection"],
            preparation_status=task["status"],required=task["required"],stock_width_mm=stock["widthMm"],
            stock_height_mm=stock["heightMm"],stock_quantity="unlimited",edge_margin_mm=.5,part_gap_mm=.2,
            allow_common_edges=True,free_rotation=True,execution_status="pending" if task["status"]=="ready" else "blocked",
            icax_boards="",icax_placed="",icax_complete="",icax_valid="",icax_utilization="",icax_native_seconds="",
            icax_first_complete_seconds="",icax_best_found_seconds="",icax_material_area_mm2="",icax_stock_area_mm2="",
            icax_roll_axis="",icax_roll_used_lengths_mm="",icax_roll_total_length_mm="",
            icax_roll_used_area_mm2="",icax_roll_segment_utilization="",icax_metrics_notes="",
            icax_remnants_measured="",icax_last_regular_remnant_mm2="",icax_last_regular_remnant_upper_mm2="",icax_remnants_json="",
            baichu_boards="",baichu_utilization="",baichu_seconds="",baichu_notes="",
            baichu_roll_total_length_mm="",baichu_roll_segment_utilization="",input_sha256=task.get("inputSha256", ""))
        if task["status"]=="ready" and current(task):
            row["execution_status"]="finished"
            b=json.loads((catalog/"results"/(task["id"]+".json")).read_text(encoding="utf-8"))["benchmark"]
            report_result(row,task,b)
        report.append(row)
    report_by_id={row["task_id"]:row for row in report}
    def save_report():
        # Preserve user-entered competitor figures in the generated table.
        previous=catalog/"pk_results.csv"
        try:
            if previous.exists():
                with previous.open(encoding="utf-8-sig",newline="") as source:
                    for old in csv.DictReader(source):
                        row=report_by_id.get(old.get("task_id"))
                        if row and old.get("input_sha256")==row["input_sha256"]:
                            for field in ("baichu_boards","baichu_utilization","baichu_seconds","baichu_notes",
                                          "baichu_roll_total_length_mm","baichu_roll_segment_utilization"):
                                row[field]=old.get(field,"")
            csv_file(previous,report,list(report[0]))
        except PermissionError:
            print("PK CSV is open in another application; native JSON results remain available",flush=True)
    save_report()
    data["executionProfile"]=dict(executableSha256=executable_hash,timeLimitSeconds=seconds,
        maxPlacementAttempts=attempts,workers=workers,caseConcurrency=concurrency,
        executablePath=pinned.relative_to(root).as_posix(),strategy="production-plane-initial",liveInitialSnapshots=True)
    if construction_trace:data["executionProfile"].update(constructionTraceFormat="native-construction-jsonl-v2",instrumentedTiming=True)
    atomic_json(catalog/"catalog.json",data)
    batch_started=time.time()
    def execute(task):
        raw=catalog/"raw"/(task["id"]+"-"+uuid.uuid4().hex[:12]+".json")
        raw.parent.mkdir(exist_ok=True)
        live_manifest=catalog/"live"/(task["id"]+".json")
        descriptor=dict(taskId=task["id"],active=True,runSignature=signature(task),startedAtUnix=time.time(),
            rawPath=raw.relative_to(root).as_posix(),
            incumbentPath=raw.with_suffix(".incumbent.json").relative_to(root).as_posix())
        if construction_trace:descriptor["nativeConstructionTrace"]=trace_descriptor(task,raw)
        atomic_json(live_manifest,descriptor)
        started=time.perf_counter()
        try:
            arguments=[str(pinned),"--initial",str(root/task["inputPath"]),str(raw),str(seconds),str(attempts),str(workers)]
            if construction_trace:arguments.extend(["--trace",str(raw.with_suffix(".trace-index.json"))])
            # Diagnostic jobs may explicitly use a practically unlimited
            # native wall budget. Windows cannot represent that value in its
            # process-wait timeout; the native attempt/cancel limits still apply.
            parent_timeout=seconds+90 if seconds+90<TIMEOUT_MAX else None
            process=subprocess.run(arguments,capture_output=True,text=True,encoding="utf-8",
                errors="replace",timeout=parent_timeout)
            if process.returncode:raise RuntimeError(process.stderr.strip() or process.stdout.strip())
            benchmark=native_benchmark(task,raw,root,executable_hash,concurrency,time.perf_counter()-started)
            bind_trace(task,raw,benchmark)
        except (OSError,ValueError,KeyError,RuntimeError,subprocess.TimeoutExpired) as error:
            benchmark=dict(status="error",required=task["required"],error=str(error),
                inputSha256=task["inputSha256"],executableSha256=executable_hash)
        descriptor.update(active=False)
        atomic_json(live_manifest,descriptor)
        return benchmark
    completed=0;failed=0;incomplete=0;invalid=0;unresolved=0;observation_failed=0;iterator=iter(queue);active={}
    # Retained current results may include a genuine construction failure. A
    # resumed publisher must not turn that into an all-successful empty batch.
    for task in tasks:
        if current(task):
            prior=json.loads((catalog/"results"/(task["id"]+".json")).read_text(encoding="utf-8"))["benchmark"]
            incomplete+=not prior.get("complete",False)
            invalid+=not prior.get("valid",False)
            observation_failed+=bool(prior.get("constructionTraceError"))
            unresolved+=not (prior.get("complete",False) and prior.get("valid",False)) or bool(prior.get("constructionTraceError"))
    def progress(status):
        identities=[task["id"] for task in active.values()]
        try:
            atomic_json(catalog/"progress.json",dict(status=status,currentTask=identities[0] if identities else None,
                currentTasks=identities,completed=completed,total=len(queue),failed=failed,
                incomplete=incomplete,invalid=invalid,unresolved=unresolved,
                observationFailed=observation_failed,
                batchStarted=batch_started,timeLimitSeconds=seconds,attempts=attempts,workers=workers,
                batchElapsedSeconds=time.time()-batch_started,
                caseConcurrency=concurrency,executableSha256=executable_hash))
        except PermissionError:
            print("Progress display is temporarily locked; native jobs continue",flush=True)
    with ThreadPoolExecutor(max_workers=concurrency) as pool:
        def launch():
            task=next(iterator,None)
            if task is not None:active[pool.submit(execute,task)]=task
        for _ in range(concurrency):launch()
        progress("running")
        while active:
            done,_=wait(active,timeout=1,return_when=FIRST_COMPLETED)
            for future in done:
                task=active.pop(future);benchmark=future.result()
                atomic_json(catalog/"results"/(task["id"]+".json"),dict(taskId=task["id"],runSignature=signature(task),benchmark=benchmark))
                row=report_by_id[task["id"]];row["execution_status"]=benchmark["status"]
                report_result(row,task,benchmark)
                completed+=1;failed+=benchmark["status"]=="error"
                if benchmark["status"]=="finished":
                    incomplete+=not benchmark.get("complete",False)
                    invalid+=not benchmark.get("valid",False)
                observation_failed+=bool(benchmark.get("constructionTraceError"))
                unresolved+=not (benchmark["status"]=="finished" and benchmark.get("complete",False) and benchmark.get("valid",False)) or bool(benchmark.get("constructionTraceError"))
                save_report()
                print(json.dumps(dict(task=task["id"],position=completed,total=len(queue),
                    **{k:benchmark.get(k) for k in ("status","boards","placed","required","complete","valid","nativeWallSeconds")}),ensure_ascii=False),flush=True)
                launch()
            progress("running")
    progress("needs-fix" if unresolved else "finished")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action",choices=["build","reprofile","refresh-imports","prepare-source-contours","run"])
    parser.add_argument("--root",type=Path,default=ROOT/"TestData/Plane")
    parser.add_argument("--executable",type=Path,default=ROOT/"output/work/sheet-nesting/source-profile-fix-20261108/build/Release/SheetNestingTask.exe")
    parser.add_argument("--seconds",type=float,default=60)
    parser.add_argument("--attempts",type=int,default=8000000)
    parser.add_argument("--workers",type=int,default=DEFAULTS["workers"])
    parser.add_argument("--limit",type=int,default=0)
    parser.add_argument("--collection",default="")
    parser.add_argument("--first-task",default="")
    parser.add_argument("--task",default="",help="Run only this exact ready task; never fall through to another task")
    parser.add_argument("--case-concurrency",type=int,choices=range(1,5),default=1)
    parser.add_argument("--without-construction-trace",action="store_true",help="Run without native strategy evolution recording")
    args=parser.parse_args()
    if args.action=="build":build(args.root)
    elif args.action=="reprofile":reprofile(args.root)
    elif args.action=="refresh-imports":refresh_imports(args.root)
    elif args.action=="prepare-source-contours":prepare_source_contours(args.root)
    else:run(args.root.resolve(),args.executable.resolve(),args.seconds,args.attempts,args.workers,args.limit,args.collection,args.first_task,args.case_concurrency,not args.without_construction_trace,args.task)


if __name__=="__main__":main()
