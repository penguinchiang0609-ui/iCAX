"""Independent, read-only GEOS audit of bound native material results.

This does not construct, repair, normalize or optimize any layout. Polygons
are the actual supplied native contours, including conservative curve imports.
Disks stay analytic; their boxes are used only by the broad-phase index.
"""
from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import re
import sys
import tempfile
import time

ROOT=Path(__file__).resolve().parents[5]
sys.path.insert(0,str(ROOT/"src/tools/sheet-nesting"))
sys.path.insert(0,str(ROOT/".deps/plane-dxf-python"))
from dxf_task import export
from shapely import LineString, Point, Polygon, STRtree, box
import shapely


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def contained(root,relative):
    path=(root/relative).resolve()
    if not path.is_relative_to(root.resolve()):raise ValueError("Evidence path escapes its material root")
    return path


def equivalent_interchange(a,b):
    """Compare identities exactly and represented numeric values without tolerance."""
    token=re.compile(r'"(?:\\.|[^"\\])*"|[^\s]+')
    left,right=token.findall(a),token.findall(b)
    if len(left)!=len(right):return False
    for x,y in zip(left,right):
        if x==y:continue
        if x.startswith('"') or y.startswith('"'):
            if not (x.startswith('"') and y.startswith('"') and json.loads(x)==json.loads(y)):return False
        else:
            try:
                if not math.isfinite(float(x)) or float(x)!=float(y):return False
            except ValueError:return False
    return True


def rotation(angle):
    angle=math.fmod(angle,360)
    if angle<0:angle+=360
    exact={0:(1.,0.),90:(0.,1.),180:(-1.,0.),270:(0.,-1.)}
    return exact.get(angle,(math.cos(angle*math.pi/180),math.sin(angle*math.pi/180)))


@dataclass
class World:
    identity: tuple
    bounds: tuple
    polygon: object=None
    center: tuple|None=None
    radius: float=0.
    eligible: list|None=None


def world_geometry(part,pose):
    geometry=part["geometry"];c,s=rotation(pose["angle"])
    sign=-1 if pose.get("mirrored",False) else 1
    def transform(point):return (c*point[0]-s*(sign*point[1])+pose["x"],s*point[0]+c*(sign*point[1])+pose["y"])
    identity=(pose["part"],pose["instance"])
    radius=geometry["radius"]
    if radius>0:
        if geometry["outer"] or geometry["holes"]:raise ValueError("Mixed analytic-circle/polygon source is invalid")
        cx,cy=transform(geometry["center"])
        return World(identity,(cx-radius,cy-radius,cx+radius,cy+radius),center=(cx,cy),radius=radius)
    rings=[[transform(point) for point in ring] for ring in [geometry["outer"],*geometry["holes"]]]
    polygon=Polygon(rings[0],rings[1:])
    if polygon.is_empty or not polygon.is_valid or polygon.area<=0:raise ValueError("Invalid represented polygon")
    mask=geometry.get("commonEdgeRings") or [True]*len(rings)
    if len(mask)!=len(rings):raise ValueError("Invalid source common-edge ring mask")
    eligible=[LineString(ring+[ring[0]]) for ring,enabled in zip(rings,mask) if enabled]
    return World(identity,polygon.bounds,polygon=polygon,eligible=eligible)


def pair_measure(a,b,tolerance):
    """Return material separation and overlap using analytic disks or GEOS."""
    if a.radius and b.radius:
        gap=math.dist(a.center,b.center)-a.radius-b.radius
        return gap,gap < -tolerance,0.
    if a.radius or b.radius:
        disk,poly=(a,b) if a.radius else (b,a)
        # Point.distance(Polygon) is zero in material and positive in a hole
        # or outside. Subtract the analytic radius; no circle tessellation.
        gap=Point(disk.center).distance(poly.polygon)-disk.radius
        return gap,gap < -tolerance,0.
    gap=a.polygon.distance(b.polygon)
    area=a.polygon.intersection(b.polygon).area if a.polygon.intersects(b.polygon) else 0.
    conflict=False
    if area>tolerance*tolerance:
        inside=a.polygon.buffer(-tolerance*.5,join_style=2).intersection(
            b.polygon.buffer(-tolerance*.5,join_style=2)).area
        conflict=inside>tolerance*tolerance
    return gap,conflict,area


def shared_eligible_length(a,b):
    if a.radius or b.radius:return 0.
    return math.fsum(first.intersection(second).length for first in a.eligible for second in b.eligible)


def audit_geometry(problem,result):
    tolerance=problem["tolerance"];gap=problem["partGap"];margin=problem["edgeMargin"]
    parts={p["id"]:p for p in problem["parts"]};stocks={s["id"]:s for s in problem["stocks"]}
    errors=[];warnings=[];seen=set();used_stocks=set();counts=Counter();common=[]
    possible=queried=analytic_pairs=analytic_parts=0;minimum_gap=None;minimum_margin=None
    material_area=0.
    if len(parts)!=len(problem["parts"]) or len(stocks)!=len(problem["stocks"]):errors.append("Duplicate input identity")
    for board_index,layout in enumerate(result["layouts"]):
        key=(layout["stock"],layout["instance"])
        stock=stocks.get(layout["stock"])
        if stock is None:errors.append("Unknown stock identity");continue
        if key in used_stocks or type(key[1]) is not int or not 0<=key[1]<stock["quantity"]:errors.append("Duplicate/out-of-range stock instance")
        used_stocks.add(key)
        if layout["width"]!=stock["width"] or layout["height"]!=stock["height"]:errors.append("Stock dimensions changed")
        if not layout["placements"]:errors.append("Empty consumed stock")
        worlds=[]
        for pose in layout["placements"]:
            part=parts.get(pose["part"])
            if part is None:errors.append("Unknown part identity");continue
            identity=(pose["part"],pose["instance"])
            if identity in seen or type(identity[1]) is not int or not 0<=identity[1]<part["quantity"]:errors.append("Duplicate/out-of-range original instance")
            seen.add(identity);counts[part["id"]]+=1
            if part["material"]!=stock["material"]:errors.append("Material mismatch")
            if not all(isinstance(pose[k],(int,float)) and math.isfinite(pose[k]) for k in ("x","y","angle")):
                errors.append("Nonfinite placement");continue
            if not part["freeRotation"] and not any(abs(math.remainder(pose["angle"]-angle,360))<=tolerance for angle in (part["allowedAngles"] or [0])):
                errors.append("Forbidden angle")
            if type(pose.get("mirrored",False)) is not bool:errors.append("Invalid mirror state");continue
            if pose.get("mirrored",False) and not part.get("allowMirror",False):errors.append("Forbidden mirror")
            w=world_geometry(part,pose);worlds.append(w)
            edge=min(w.bounds[0],w.bounds[1],layout["width"]-w.bounds[2],layout["height"]-w.bounds[3])
            minimum_margin=edge if minimum_margin is None else min(minimum_margin,edge)
            if edge+tolerance<margin:errors.append(f"Board margin failed: {identity}")
            if w.radius:analytic_parts+=1;material_area+=math.pi*w.radius*w.radius
            else:material_area+=w.polygon.area
        # Index conservative outward AABBs. Every omitted pair is separated
        # by more than the required gap along at least one coordinate axis.
        boxes=[box(*w.bounds) for w in worlds];tree=STRtree(boxes);padding=gap+max(tolerance,1e-12)
        possible+=len(worlds)*(len(worlds)-1)//2
        for i,a in enumerate(worlds):
            x0,y0,x1,y1=a.bounds
            query=box(math.nextafter(x0-padding,-math.inf),math.nextafter(y0-padding,-math.inf),
                      math.nextafter(x1+padding,math.inf),math.nextafter(y1+padding,math.inf))
            for value in tree.query(query):
                j=int(value)
                if j<=i:continue
                queried+=1;b=worlds[j]
                analytic_pairs+=bool(a.radius or b.radius)
                distance,conflict,overlap=pair_measure(a,b,tolerance)
                pair=dict(board=board_index,first=a.identity,second=b.identity,distanceMm=distance,intersectionArea=overlap)
                if conflict:errors.append({"materialOverlap":pair});continue
                if overlap>0:warnings.append({"withinDeclaredToleranceOverlap":pair})
                if distance+tolerance<gap:
                    shared=shared_eligible_length(a,b) if problem["allowCommonEdges"] else 0.
                    if shared>tolerance:common.append({**pair,"eligibleCommonLengthMm":shared})
                    else:errors.append({"ordinaryGapFailed":pair})
                else:minimum_gap=distance if minimum_gap is None else min(minimum_gap,distance)
    remaining={}
    for item in result["remaining"]:
        identity=item["part"];quantity=item["quantity"]
        if identity not in parts or identity in remaining or type(quantity) is not int or quantity<=0:errors.append("Invalid remaining demand")
        remaining[identity]=quantity
    for identity,part in parts.items():
        if counts[identity]+remaining.get(identity,0)!=part["quantity"]:errors.append("Original demand not conserved: "+identity)
    required=sum(p["quantity"] for p in parts.values());placed=sum(counts.values());complete=placed==required
    if result["placed"]!=placed or result["complete"]!=complete or result["remainingCount"]!=required-placed or result["boards"]!=len(result["layouts"]):errors.append("Native reported counts differ from original identities")
    return dict(passed=not errors,complete=complete,required=required,placed=placed,boards=len(result["layouts"]),
        errors=errors,warnings=warnings,pairsPossible=possible,pairsChecked=queried,pairsSeparatedByBounds=possible-queried,
        analyticCirclePlacements=analytic_parts,analyticCirclePairs=analytic_pairs,eligibleCommonEdgePairs=common,
        minimumCheckedOrdinaryGapMm=minimum_gap,minimumBoardMarginMm=minimum_margin,materialAreaMm2=material_area,
        declaredGeometryToleranceMm=tolerance,geometryScope="Actual supplied polygon contours and holes; native disks remain analytic. This is not an exact original-curve certificate.")


def normalized_layouts(layouts,traced=False):
    result=[]
    for layout in layouts:
        result.append(dict(stock=layout["stockID" if traced else "stock"],instance=layout["stockInstance" if traced else "instance"],
            width=layout["width"],height=layout["height"],materialArea=layout["materialArea"],placements=[
                dict(part=p["part"],instance=p["instance"],x=(p["pose"] if traced else p)["x"],
                     y=(p["pose"] if traced else p)["y"],angle=(p["pose"] if traced else p)["angle"],
                     mirrored=(p["pose"] if traced else p).get("mirrored",False),group=p["group"])
                for p in layout["placements"]]))
    return result


COMMIT_BASE_RANK_NAMES=("negativeUsedSectionUtilization","negativeNetMaterial",
    "orientationOrOccupiedArea","intrinsicFirstMaterialCornerDistance","primaryAxis",
    "secondaryAxis","part","template","columns","rows")
COMMIT_SPACE_RANK_NAMES=COMMIT_BASE_RANK_NAMES[:2]+("negativeLargeRegularFreeSpace",
    "fragmentFreeArea","freeBoundaryPerimeter","negativeAttachment")+COMMIT_BASE_RANK_NAMES[2:]
COMMIT_CONTACT_SPACE_RANK_NAMES=COMMIT_SPACE_RANK_NAMES[:2]+("strategyContact",)+COMMIT_SPACE_RANK_NAMES[2:]
COMMIT_SPACE_MEASURES=("largestFreeArea","largeRegularFreeSpace","fragmentFreeArea",
    "freeBoundaryPerimeter","attachment","freeArea","freeComponents","spaceCircleProxy",
    "spaceEvaluationSeconds","spaceCandidatesEvaluated")


def commit_rank_errors(problem,event,layouts=None):
    """Check recorded production rank schemas, without rewriting evidence.

    The primary material/utilization terms are independently recomputed from
    actual committed originals. Local-space terms bind exactly to their native
    measurements; this does not certify the heuristic polygon calculation.
    """
    errors=[];rank=event.get("rank",[])
    def finite(value):
        try:return type(value) in (int,float) and math.isfinite(value)
        except OverflowError:return False
    if not isinstance(rank,list):return ["Actual commit has an unknown or reordered Rank schema"]
    names=tuple(row.get("name") for row in rank if isinstance(row,dict))
    if names not in (COMMIT_BASE_RANK_NAMES,COMMIT_SPACE_RANK_NAMES,COMMIT_CONTACT_SPACE_RANK_NAMES) or len(names)!=len(rank):
        return ["Actual commit has an unknown or reordered Rank schema"]
    if any(not finite(row.get("value")) for row in rank):
        return ["Actual commit Rank contains a nonfinite or nonnumeric value"]
    values={row["name"]:row["value"] for row in rank}
    try:
        boards=layouts if layouts is not None else normalized_layouts(event["layouts"],True)
        if len(boards)!=1 or not boards[0]["placements"] or not event.get("members"):
            return ["Actual commit Rank is not bound to a nonempty original state"]
        board=boards[0];parts=problem["parts"];by_id={part["id"]:part for part in parts}
        def net_area(part):
            shape=part["geometry"]
            return math.pi*shape["radius"]**2 if shape["radius"]>0 else Polygon(shape["outer"],shape["holes"]).area
        areas={identity:net_area(part) for identity,part in by_id.items()}
        total=math.fsum(areas[placed["part"]] for placed in board["placements"])
        candidate=math.fsum(areas[parts[member["partIndex"]]["id"]] for member in event["members"])
        top=max(world_geometry(by_id[placed["part"]],placed).bounds[3] for placed in board["placements"])
        used=board["width"]*min(board["height"],max(0.,top+problem["edgeMargin"]))
        if not all(finite(value) and value>0 for value in (total,candidate,used)):
            errors.append("Actual commit Rank material or consumed section is not finite and positive")
        else:
            if not math.isclose(values["negativeUsedSectionUtilization"],-total/used,rel_tol=1e-10,abs_tol=1e-12):
                errors.append("Actual commit Rank primary utilization differs from committed original material/consumed section")
            if not math.isclose(values["negativeNetMaterial"],-candidate,rel_tol=1e-10,abs_tol=1e-8):
                errors.append("Actual commit Rank net material differs from its actual added original members")
        for name in COMMIT_BASE_RANK_NAMES[2:]:
            if values[name]<0:errors.append("Actual commit Rank has a negative base tie value: "+name)
        for name in ("part","template","columns","rows"):
            if values[name]!=math.floor(values[name]):errors.append("Actual commit Rank has a noninteger base identity/count: "+name)
        measures=event.get("measures",[])
        has_space=names==COMMIT_SPACE_RANK_NAMES
        if names==COMMIT_CONTACT_SPACE_RANK_NAMES:
            contact=[row.get("value") for row in measures if isinstance(row,dict) and row.get("name")=="strategyContact"]
            valid=[row.get("value") for row in measures if isinstance(row,dict) and row.get("name")=="spaceScoreValid"]
            if len(contact)!=1 or not finite(contact[0]) or contact[0]!=values["strategyContact"]:
                errors.append("Actual commit strategy contact differs from its native measure")
            if len(valid)!=1 or valid[0] not in (0,1):
                errors.append("Actual commit space availability marker is absent or invalid")
            else:has_space=valid[0]==1
            if not has_space and any(values[name]!=0 for name in COMMIT_SPACE_RANK_NAMES[2:6]):
                errors.append("Unavailable local-space score has nonzero ranking terms")
        if has_space:
            local={}
            for name in COMMIT_SPACE_MEASURES:
                matches=[row.get("value") for row in measures if isinstance(row,dict) and row.get("name")==name]
                if len(matches)!=1 or not finite(matches[0]) or matches[0]<0:
                    errors.append("Actual commit local-space measure is absent, duplicated or invalid: "+name)
                else:local[name]=matches[0]
            if len(local)==len(COMMIT_SPACE_MEASURES):
                for rank_name,measure_name,sign in (("negativeLargeRegularFreeSpace","largeRegularFreeSpace",-1),
                    ("fragmentFreeArea","fragmentFreeArea",1),("freeBoundaryPerimeter","freeBoundaryPerimeter",1),
                    ("negativeAttachment","attachment",-1)):
                    if values[rank_name]!=sign*local[measure_name]:
                        errors.append("Actual commit Rank differs from its local-space measure: "+rank_name)
                if not math.isclose(local["freeArea"],local["largestFreeArea"]+local["fragmentFreeArea"],rel_tol=1e-10,abs_tol=1e-8):
                    errors.append("Actual commit local-space areas do not conserve the measured free area")
                if local["largeRegularFreeSpace"]>local["largestFreeArea"] and not math.isclose(
                    local["largeRegularFreeSpace"],local["largestFreeArea"],rel_tol=1e-10,abs_tol=1e-8):
                    errors.append("Actual commit regular-space proxy exceeds its largest free component")
                if any(local[name]!=math.floor(local[name]) for name in ("freeComponents","spaceCandidatesEvaluated")) or local["spaceCandidatesEvaluated"]<1:
                    errors.append("Actual commit local-space component/candidate counts are invalid")
                if local["spaceCircleProxy"] not in (0,1):errors.append("Actual commit circle-proxy marker is invalid")
        elif any(isinstance(row,dict) and row.get("name") in COMMIT_SPACE_MEASURES for row in measures):
            errors.append("Actual commit mixes a base Rank with local-space measures")
    except (KeyError,IndexError,TypeError,ValueError,OverflowError) as error:
        errors.append("Actual commit Rank binding is malformed: "+str(error))
    return errors


PATH_SOURCES={"construction.path.commit","construction.path.raw","strategy.construction-selection",
    "competition.board-selection","sheet-template.copy"}
ACTUAL_COMMIT_SOURCES={"constructor.commit","fill.commit","envelope.skyline"}


def copy_mapping_errors(problem,event):
    """Check the recorded atomic copy, never reconstruct a proposed copy."""
    errors=[]
    try:
        layouts=normalized_layouts(event["layouts"],True)
        measures=event.get("measures",[])
        for name,expected in (("sourceLayoutIndex",0),("targetLayoutIndex",1)):
            matches=[item.get("value") for item in measures if item.get("name")==name]
            if len(matches)!=1 or type(matches[0]) not in (int,float) or matches[0]!=expected:
                errors.append("Atomic copy has an invalid layout-index mapping: "+name)
        if event.get("stage")!="commit" or len(layouts)!=2 or event.get("members"):
            return errors+["Atomic copy is not one recorded source/target whole-board operation"]
        source,target=layouts;stocks={stock["id"]:stock for stock in problem["stocks"]}
        stock=stocks.get(source["stock"])
        if stock is None or source["stock"]!=target["stock"] or any(
            board["width"]!=stock["width"] or board["height"]!=stock["height"] or
            type(board["instance"]) is not int or not 0<=board["instance"]<stock["quantity"]
            for board in (source,target)):
            errors.append("Atomic copy changed physical stock identity, dimensions or capacity")
        if source["instance"]>=target["instance"] or event.get("stockInstance")!=target["instance"]:
            errors.append("Atomic copy did not promote to its actual later stock instance")
        if not source["placements"] or len(source["placements"])!=len(target["placements"]):
            return errors+["Atomic copy source/target original counts differ"]
        parts={part["id"]:part for part in problem["parts"]};source_ids=set();target_ids=set()
        source_groups={p["group"] for p in source["placements"] if p["group"]};groups={};targets=set()
        for before,after in zip(source["placements"],target["placements"]):
            if any(before[key]!=after[key] for key in ("part","x","y","angle","mirrored")):
                errors.append("Atomic copy changed ordered original geometry, pose or mirror parity")
            part=parts.get(after["part"])
            if part is None or any(type(p["instance"]) is not int or not 0<=p["instance"]<part["quantity"]
                for p in (before,after)) or after["instance"]<=before["instance"]:
                errors.append("Atomic copy original instance mapping is out of demand or not fresh")
            source_identity=(before["part"],before["instance"])
            if source_identity in source_ids:errors.append("Atomic copy repeated a source original instance")
            source_ids.add(source_identity)
            identity=(after["part"],after["instance"])
            if identity in target_ids:errors.append("Atomic copy repeated an original target instance")
            target_ids.add(identity)
            if any(type(p["group"]) is not int or p["group"]<0 for p in (before,after)):
                errors.append("Atomic copy has an invalid original group tag")
            elif not before["group"]:
                if after["group"]:errors.append("Atomic copy changed a zero source group")
            elif not after["group"] or after["group"] in source_groups:
                errors.append("Atomic copy did not allocate a fresh positive target group")
            elif before["group"] in groups:
                if groups[before["group"]]!=after["group"]:errors.append("Atomic copy split a source group")
            elif after["group"] in targets:errors.append("Atomic copy merged distinct source groups")
            else:groups[before["group"]]=after["group"];targets.add(after["group"])
        if source_ids & target_ids:errors.append("Atomic copy reused a source original instance")
    except (KeyError,IndexError,TypeError,ValueError,OverflowError) as error:
        errors.append("Atomic copy mapping is malformed: "+str(error))
    return errors


def audit_trace_states(problem,events,index,final_layouts=None,final_route=None):
    """Audit actual state deltas and the independently retained selected paths.

    A bounded omission of unrelated search branches is a coverage warning only
    when the independent chosen-path lane is complete and exactly bound below.
    Recorded malformed ranks, identities, poses and selections always fail.
    """
    errors=[];warnings=[];states={};paths={};original_records={};raws=[];path_raws=[]
    checked_commits=set();ranked_commits=set();envelope_commits=set()
    has_path=any(e.get("source") in {"construction.path.commit","construction.path.raw"} for e in events)
    path_schema="pathEventsWritten" in index
    path_count=sum(e.get("source") in PATH_SOURCES for e in events)
    state_events=[e for e in events if e.get("source") not in PATH_SOURCES and
        (e.get("stage")=="commit" or e.get("source")=="board.raw-construction")]
    state_loss=bool(index.get("stateEventsOmitted") or index.get("stateSnapshotsTruncated") or
        index.get("stateEventsSeen")!=index.get("stateEventsWritten"))
    if len(state_events)!=index.get("stateEventsWritten"):
        errors.append("Actual state lane record count differs from its native index")
    if path_schema and (path_count!=index.get("pathEventsWritten") or
        index.get("pathEventsSeen")!=index.get("pathEventsWritten") or
        index.get("pathEventsOmitted") or index.get("pathSnapshotsTruncated")):
        errors.append("Selected path lane omitted or truncated a record")
    if has_path and not path_schema:errors.append("Selected path records have no native path-lane accounting")
    if state_loss:
        if has_path and path_schema:warnings.append("Actual state lane omitted or truncated nonselected branch coverage; selected paths are audited independently.")
        else:errors.append("Actual state lane omitted or truncated a record")
    if index.get("coreObservationTruncated") and not (has_path and path_schema and state_loss):
        errors.append("Native core observation is incomplete")

    def issue(message,coverage=False):
        (warnings if coverage and has_path and path_schema and state_loss else errors).append(message)

    def key(event):return event.get("route",""),event.get("scope","")

    def commit(event,path=False):
        scope=event.get("scope","");source=event.get("source","");table=paths if path else states
        if event.get("truncated"):
            issue("Actual committed snapshot is truncated: "+scope,not path);return
        try:
            layouts=normalized_layouts(event["layouts"],True);members=event.get("members",[])
            is_envelope=source=="envelope.skyline"
            if (path and event.get("stage")!="selected") or (not path and event.get("stage")!="commit"):
                errors.append("Actual commit has an invalid operation stage: "+scope)
            if len(layouts)!=1 or not layouts[0]["placements"] or not members:
                errors.append("Actual commit is not a nonempty original board state: "+scope);return
            state,parent=event["stateID"],event["parentStateID"]
            if type(state) is not int or state<=0 or type(parent) is not int or parent<0 or parent==state:
                errors.append("Actual committed state/parent identity is invalid: "+scope);return
            scoped=table.setdefault(key(event),{});before=scoped.get(parent) if parent else None
            if parent and before is None:issue("Missing actual parent state: "+scope,not path)
            added=[]
            for member in members:
                pi=member["partIndex"]
                if type(pi) is not int or not 0<=pi<len(problem["parts"]):raise ValueError("unknown original index")
                pose=member["pose"]
                if type(member["instance"]) is not int or not 0<=member["instance"]<problem["parts"][pi]["quantity"]:
                    raise ValueError("original instance is outside its unchanged demand")
                if type(member["group"]) is not int or member["group"]<0 or type(pose.get("mirrored",False)) is not bool:
                    raise ValueError("invalid original group or mirror state")
                if any(type(pose[name]) not in (int,float) or not math.isfinite(pose[name]) for name in ("x","y","angle")):
                    raise ValueError("invalid original pose coordinate")
                added.append(dict(part=problem["parts"][pi]["id"],instance=member["instance"],x=pose["x"],
                    y=pose["y"],angle=pose["angle"],mirrored=pose.get("mirrored",False),group=member["group"]))
            prior=before[0]["placements"] if before and len(before)==1 else []
            if before or not parent:
                if layouts[0]["placements"]!=prior+added:
                    errors.append("Actual commit differs from parent plus original members: "+scope)
                if before and any(layouts[0][field]!=before[0][field] for field in ("stock","instance","width","height")):
                    errors.append("Actual commit changed its parent physical board: "+scope)
            elif layouts[0]["placements"][-len(added):]!=added:
                errors.append("Actual commit suffix differs from its original members: "+scope)
            if state in scoped:errors.append("Actual committed state identity was reused: "+scope)
            scoped[state]=layouts
            identities=[(p["part"],p["instance"]) for p in layouts[0]["placements"]]
            if len(set(identities))!=len(identities):errors.append("Actual commit repeated an original instance: "+scope)
            stock=next((s for s in problem["stocks"] if s["id"]==layouts[0]["stock"]),None)
            if stock is None or layouts[0]["width"]!=stock["width"] or layouts[0]["height"]!=stock["height"] or \
                type(layouts[0]["instance"]) is not int or not 0<=layouts[0]["instance"]<stock["quantity"]:
                errors.append("Actual commit changed original stock dimensions or identity: "+scope)
            identity=(*key(event),state)
            if path:
                original=original_records.get(identity)
                if original is not None and (any(event.get(field)!=original.get(field)
                    for field in ("parentStateID","candidateID","proposalID","members","rank")) or
                    layouts!=normalized_layouts(original["layouts"],True)):
                    errors.append("Retained selected path differs from its recorded actual original commit: "+scope)
            else:original_records[identity]=event
            if is_envelope:
                envelope_commits.add(identity)
                if len(members)!=1:errors.append("Envelope commit did not add exactly one original: "+scope)
                rank=event.get("rank",[])
                if rank and (tuple(row.get("name") for row in rank)!=("top","x","y","direction") or
                    any(type(row.get("value")) not in (int,float) or not math.isfinite(row["value"]) for row in rank)):
                    errors.append("Envelope commit has an invalid independent skyline rank: "+scope)
            else:
                checked_commits.add(identity);rank_errors=commit_rank_errors(problem,event,layouts)
                if rank_errors:errors.extend(message+": "+scope for message in rank_errors)
                else:ranked_commits.add(identity)
        except (KeyError,IndexError,TypeError,ValueError,OverflowError) as error:
            errors.append("Actual commit binding is malformed: "+scope+": "+str(error))

    for event in events:
        source=event.get("source","");scope=event.get("scope","")
        if source=="sheet-template.copy":errors.extend(copy_mapping_errors(problem,event))
        elif source=="construction.path.commit":commit(event,True)
        elif source in ACTUAL_COMMIT_SOURCES and event.get("stage")=="commit":commit(event)
        if source in {"board.raw-construction","construction.path.raw"}:
            path=source=="construction.path.raw"
            if event.get("truncated"):
                issue("Raw construction snapshot is truncated: "+scope,not path);continue
            try:
                layouts=normalized_layouts(event["layouts"],True)
                if not layouts or not any(board["placements"] for board in layouts):continue
                table=paths if path else states
                if table.get(key(event),{}).get(event.get("stateID"))!=layouts:
                    issue("Raw construction differs from its actual committed state: "+scope,not path)
                (path_raws if path else raws).append((event,layouts))
            except (KeyError,TypeError,ValueError) as error:errors.append("Raw construction is malformed: "+str(error))

    # New selection records bind the actual retained child, promotions and whole
    # copies to each recorded route terminal. No last-branch inference is used.
    if has_path or path_schema:
        route_results={e.get("route",""):normalized_layouts(e["layouts"],True) for e in events
            if e.get("source") in {"scheduler.route-result","scheduler.serial-route-result","scheduler.portfolio-route-result"}
            and not e.get("truncated") and e.get("layouts")}
        if final_layouts is not None:route_results[final_route or ""]=normalized_layouts(final_layouts)
        for route,terminal in route_results.items():
            selected=[e for e in events if e.get("route","")==route and e.get("source")=="strategy.construction-selection"]
            promotions=[e for e in events if e.get("route","")==route and e.get("source")=="competition.board-selection"]
            candidates=[(e,ls) for e,ls in (path_raws if any(key(e)[0]==route for e,_ in path_raws) else raws) if key(e)[0]==route]
            known=[];selected_scope=None
            if selected:
                if len(selected)!=1 or selected[0].get("truncated") or normalized_layouts(selected[0].get("layouts",[]),True)!=terminal:
                    errors.append("Selected construction child differs from its actual route terminal: "+route)
                selected_scope=selected[0].get("scope","")
                known=[board for e,ls in candidates if e.get("scope","")==selected_scope for board in ls]
            elif promotions:
                for event in promotions:
                    try:
                        ls=normalized_layouts(event["layouts"],True)
                        if event.get("truncated") or len(ls)!=2 or not any(e.get("scope","")==event.get("scope","") and ls[0] in bs for e,bs in candidates):
                            errors.append("Promoted board has no exact selected child raw state: "+route);continue
                        measures=event.get("measures",[])
                        if any([m.get("value") for m in measures if m.get("name")==name]!=[value]
                            for name,value in (("sourceLayoutIndex",0),("targetLayoutIndex",1))):
                            errors.append("Promoted board has an invalid source/target layout mapping: "+route)
                        if len(ls[0]["placements"])!=len(ls[1]["placements"]) or any(
                            any(a[field]!=b[field] for field in ("part","x","y","angle","mirrored"))
                            for a,b in zip(ls[0]["placements"],ls[1]["placements"])):
                            errors.append("Promoted board changed ordered original poses or mirror parity: "+route)
                        if any(ls[0][field]!=ls[1][field] for field in ("stock","width","height")):
                            errors.append("Promoted board changed original physical stock: "+route)
                        part_by_id={p["id"]:p for p in problem["parts"]};group_map={};group_targets=set()
                        for before,after in zip(ls[0]["placements"],ls[1]["placements"]):
                            part=part_by_id.get(after["part"])
                            if part is None or type(after["instance"]) is not int or not 0<=after["instance"]<part["quantity"]:
                                errors.append("Promoted original instance is outside unchanged demand: "+route)
                            if type(after["group"]) is not int or after["group"]<=0:
                                errors.append("Promoted board has an invalid actual target group: "+route)
                            elif before["group"] in group_map:
                                if group_map[before["group"]]!=after["group"]:errors.append("Promoted board split a source group: "+route)
                            elif after["group"] in group_targets:errors.append("Promoted board merged distinct source groups: "+route)
                            else:group_map[before["group"]]=after["group"];group_targets.add(after["group"])
                        known.append(ls[1])
                    except (KeyError,TypeError,ValueError) as error:errors.append("Promoted board mapping is malformed: "+str(error))
            else:
                if any("/restart-" in e.get("scope","") or "/competitor-" in e.get("scope","") for e,_ in candidates):
                    errors.append("Route terminal has no explicit winning child selection: "+route)
                known=[board for _,ls in candidates for board in ls]
            for event in events:
                if event.get("route","")!=route or event.get("source")!="sheet-template.copy" or event.get("truncated"):continue
                scope=event.get("scope","")
                if selected_scope is not None and not (scope==selected_scope or selected_scope.startswith(scope+"/kind-")):continue
                try:
                    ls=normalized_layouts(event["layouts"],True)
                    if len(ls)!=2 or ls[0] not in known:errors.append("Atomic copy has no exact selected source template: "+route)
                    else:
                        identities={(p["part"],p["instance"]) for board in known for p in board["placements"]}
                        groups={p["group"] for board in known for p in board["placements"] if p["group"]}
                        if any((p["part"],p["instance"]) in identities for p in ls[1]["placements"]):
                            errors.append("Atomic copy reused an already selected original instance: "+route)
                        if any(p["group"] in groups for p in ls[1]["placements"] if p["group"]):
                            errors.append("Atomic copy reused an already selected positive group: "+route)
                        if any((board["stock"],board["instance"])==(ls[1]["stock"],ls[1]["instance"]) for board in known):
                            errors.append("Atomic copy reused an already selected physical stock instance: "+route)
                        known.append(ls[1])
                except (KeyError,TypeError,ValueError) as error:errors.append("Atomic copy source binding is malformed: "+str(error))
            if any(board not in known for board in terminal) or any(board not in terminal for board in known):
                errors.append("Route terminal differs from its retained original paths and actual board mappings: "+route)
    return dict(errors=errors,warnings=warnings,pathEvidencePresent=has_path,pathEventsChecked=path_count,
        actualCommitsChecked=len(checked_commits),actualCommitRanksChecked=len(ranked_commits),envelopeCommitsChecked=len(envelope_commits),
        stateEventsChecked=len(state_events),selectedPathRawBoards=len(path_raws),rawBoards=len(raws))


def audit_bound_native_run(stem, expected_input_sha=None, expected_executable_sha=None, same_part_only=None):
    """Audit one completed native run without changing its raw/index evidence.

    The adjacent run manifest binds the immutable program and original input.
    This entry point writes only independent-audit and group-summary reports;
    the established catalog CLI below keeps its existing behavior.
    """
    started=time.perf_counter();stem=Path(stem).resolve()
    raw_path=Path(str(stem)+".json");index_path=Path(str(stem)+".trace-index.json")
    manifest_path=Path(str(stem)+".run.json")
    raw_bytes=raw_path.read_bytes();index_bytes=index_path.read_bytes()
    raw=json.loads(raw_bytes.decode("utf-8-sig"));index=json.loads(index_bytes.decode("utf-8-sig"))
    manifest=json.loads(manifest_path.read_text(encoding="utf-8-sig"));errors=[]
    wanted_input=expected_input_sha or manifest["inputSha256"]
    wanted_executable=expected_executable_sha or manifest["executableSha256"]
    input_path=Path(manifest["inputPath"]);executable_path=Path(manifest["executablePath"])
    if manifest["inputSha256"]!=wanted_input or digest(input_path)!=wanted_input:errors.append("Bound original input SHA mismatch")
    if manifest["executableSha256"]!=wanted_executable or digest(executable_path)!=wanted_executable:errors.append("Bound native executable SHA mismatch")
    if manifest.get("nativeProcessExitCode",0)!=0:errors.append("Native process did not exit successfully")
    if manifest.get("runState") not in (None,"completed"):errors.append("Run manifest has not reached native completion")
    if same_part_only is None:same_part_only=manifest.get("combinationPolicy")=="same-original-type-only"
    work=ROOT/"output/work/sheet-nesting/geos-audit"
    if not work.resolve().is_relative_to(ROOT.resolve()):raise ValueError("Independent audit work directory escapes the workspace")
    work.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(dir=work) as directory:
        if not Path(directory).resolve().is_relative_to(work.resolve()):raise ValueError("Independent audit temporary directory escapes its workspace root")
        canonical=Path(directory)/"original.problem";export(raw["problem"],canonical)
        equivalent=equivalent_interchange(canonical.read_text(encoding="utf-8-sig"),input_path.read_text(encoding="utf-8-sig"))
    if not equivalent:errors.append("Native original problem differs from bound input")
    checked=audit_geometry(raw["problem"],raw["result"]);geometry_passed=checked["passed"]
    validation=raw["validation"]
    if not validation["valid"] or not raw["result"]["valid"]:errors.append("Native final geometry validation failed")
    if validation["complete"]!=checked["complete"] or validation["placed"]!=checked["placed"]:errors.append("Native and independent original counts differ")
    if not math.isclose(validation["materialArea"],checked["materialAreaMm2"],rel_tol=1e-10,abs_tol=1e-6):errors.append("Native and independent net material area differ")
    if index["problem"]!=raw["problem"] or index["resultFile"]!=raw_path.name or raw.get("nativeConstructionTrace")!=index_path.name:errors.append("Adjacent raw/index original problem binding differs")
    event_path=(index_path.parent/index["eventsFile"]).resolve()
    if event_path.parent!=index_path.parent or event_path.name!=stem.name+".construction.jsonl":raise ValueError("Native trace event path is not bound to the adjacent run")
    event_bytes=event_path.read_bytes();events=[json.loads(line)["event"] for line in event_bytes.decode("utf-8-sig").splitlines() if line]
    final_sources={"scheduler.final-selection","scheduler.serial-final-selection","scheduler.portfolio-final-selection"}
    selections=[event for event in events if event["source"] in final_sources]
    final_bound=len(selections)==1 and normalized_layouts(selections[0]["layouts"],True)==normalized_layouts(raw["result"]["layouts"]) and selections[0]["remaining"]==raw["result"]["remaining"]
    if not final_bound:errors.append("Unique final selection differs from final original stock/identity/pose/mirror/group/remaining")
    if not index.get("runCompleted") or index.get("observerFailed") or not index.get("finalSelectionObserved") or index.get("finalSnapshotTruncated"):errors.append("Native terminal observation is incomplete")
    trace_states=audit_trace_states(raw["problem"],events,index,raw["result"]["layouts"],
        selections[0].get("route","") if len(selections)==1 else None)
    errors.extend(trace_states["errors"]);checked["warnings"].extend(trace_states["warnings"])
    settings=raw.get("settings",{})
    if settings.get("enableBasicCompaction") or settings.get("refill") or settings.get("externalLayoutSeed"):errors.append("Run used layout refinement, refill or an external layout seed")
    parts=raw["problem"]["parts"];part_ids={part["id"] for part in parts}
    commits=trace_states["actualCommitsChecked"];ranked_commits=trace_states["actualCommitRanksChecked"]
    object_sources=Counter();summaries=[];policy_errors=[]
    explicit={"preparation.retained-template","preparation.certified-pattern","preparation.orientation-pair",
        "constructor.commit","fill.commit","construction.path.commit"}
    for event in events:
        source=event["source"];members=event.get("members",[]);scope=event.get("scope","")
        is_object=source in explicit or source.startswith("array.") or source.startswith("placement.") or event["stage"] in {"generate","candidate","filter","score"}
        if is_object and members:
            object_sources[source]+=1;indices={member.get("partIndex") for member in members}
            if same_part_only and (len(indices)!=1 or any(type(i) is not int or not 0<=i<len(parts) for i in indices)):policy_errors.append("Recorded object mixes original indices: "+source)
        for board in event.get("layouts",[]):
            groups={}
            for placed in board["placements"]:
                if placed["group"]:groups.setdefault(placed["group"],set()).add(placed["part"])
            if same_part_only and any(len(ids)!=1 for ids in groups.values()):policy_errors.append("A recorded nonzero group mixes original identities: "+source)
        if source=="preparation.combination-summary" and event["stage"]=="complete":
            measures={m["name"]:m["value"] for m in event["measures"]}
            zero_names=["crossPartRegionRequests","crossPartBeamChoices","basicCoverageSeeds","basicCoverageQueries","basicCoverageExactAttempts","basicCoverageRetained"]
            if same_part_only and (measures.get("samePartOnly")!=1 or any(measures.get(key)!=0 for key in zero_names)):policy_errors.append("Native cross-original combination work was observed: "+scope)
            summaries.append(dict(scope=scope,samePartOnly=measures.get("samePartOnly"),**{key:measures.get(key) for key in zero_names}))
    # Other query summaries share a bounded lane. Its aggregate omission
    # counter does not invalidate geometry or a summary that was recorded.
    # Account separately for each actual constructor's preparation families.
    core_scopes={e.get("scope","") for e in events if e["source"] in
        {"constructor.commit","fill.commit","board.raw-construction","construction.path.commit","construction.path.raw"}}
    expected_families={"nfp-general"}
    if any(p["quantity"]>=2 and not p["geometry"]["radius"] and not p["geometry"]["holes"] for p in parts):expected_families.add("nfp-self")
    observed_families={scope:set() for scope in core_scopes}
    for row in summaries:
        base,separator,family=row["scope"].rpartition("/nfp-")
        if separator and base in observed_families:observed_families[base].add("nfp-"+family)
    summary_coverage=[dict(scope=scope,expectedFamilies=sorted(expected_families),observedCompleteFamilies=sorted(observed_families[scope]),missingFamilies=sorted(expected_families-observed_families[scope])) for scope in sorted(core_scopes)]
    policy_evidence_complete=bool(summary_coverage) and all(not row["missingFamilies"] for row in summary_coverage)
    policy_warnings=[]
    if same_part_only and not policy_evidence_complete:
        policy_warnings.append("Native combination-summary evidence is incomplete; final geometry/groups remain independently audited, but zero cross-original preparation work is verified only for recorded summaries.")
        checked["warnings"].extend(policy_warnings)
    boards=[];group_rows=[];used_area=stock_area=used_length=0.;all_final_types=set()
    by_id={part["id"]:part for part in parts}
    for number,board in enumerate(raw["result"]["layouts"]):
        worlds=[world_geometry(by_id[p["part"]],p) for p in board["placements"]]
        length=min(board["height"],max((w.bounds[3] for w in worlds),default=0.)+raw["problem"]["edgeMargin"])
        material=math.fsum(math.pi*w.radius*w.radius if w.radius else w.polygon.area for w in worlds)
        area=board["width"]*length;physical=board["width"]*board["height"]
        used_area+=area;stock_area+=physical;used_length+=length;counts=Counter(p["part"] for p in board["placements"]);all_final_types.update(counts)
        boards.append(dict(boardIndex=number,stock=board["stock"],stockInstance=board["instance"],widthMm=board["width"],heightMm=board["height"],placed=len(worlds),originalTypeCounts=dict(counts),netMaterialArea=material,usedLengthMm=length,fullWidthUsedSectionArea=area,stockUtilization=material/physical if physical else 0.,usedSectionUtilization=material/area if area else 0.))
        groups={}
        for placed in board["placements"]:
            if placed["group"]:groups.setdefault(placed["group"],Counter())[placed["part"]]+=1
        for group,counts in groups.items():group_rows.append(dict(boardIndex=number,group=group,originalTypeCounts=dict(counts),quantity=sum(counts.values()),sameOriginalType=len(counts)==1))
    if same_part_only and any(not row["sameOriginalType"] for row in group_rows):policy_errors.append("A final nonzero group mixes original identities")
    if checked["complete"] and all_final_types!={p["id"] for p in parts if p["quantity"]}:errors.append("Complete output does not contain every demanded original type")
    metrics=dict(firstCompleteWallSeconds=raw.get("firstCompleteWallSeconds"),wholeJobWallSeconds=raw.get("wholeJobWallSeconds"),nativeProcessWallSeconds=manifest.get("nativeProcessWallSeconds"),netMaterialArea=checked["materialAreaMm2"],stockUtilization=checked["materialAreaMm2"]/stock_area if stock_area else 0.,usedLengthMm=used_length,usedSectionUtilization=checked["materialAreaMm2"]/used_area if used_area else 0.,boardMetrics=boards)
    counter_names=["crossPartRegionRequests","crossPartBeamChoices","basicCoverageSeeds","basicCoverageQueries","basicCoverageExactAttempts","basicCoverageRetained"]
    counter_values=[row.get(key) for row in summaries for key in counter_names]
    counters_zero=all(value==0 for value in counter_values) if counter_values and all(value is not None for value in counter_values) else None
    policy=dict(required=same_part_only,passed=not policy_errors,policyEvidenceComplete=policy_evidence_complete,recordedMemberPayloadsBySource=dict(object_sources),actualCommitsChecked=commits,actualCommitRanksChecked=ranked_commits,nativePreparationSummaries=summaries,nativePreparationSummaryCoverage=summary_coverage,crossOriginalSearchCountersZero=counters_zero if policy_evidence_complete else None,observedCrossOriginalSearchCountersZero=counters_zero,summaryEventsOmitted=index.get("summaryEventsOmitted"),omissionSourceAttribution="The native index counts summary omissions per route, not per source; exact omitted query-source totals are unavailable. The per-constructor coverage above identifies missing combination-summary families.",finalNonzeroGroups=len(group_rows),finalMixedNonzeroGroups=sum(not row["sameOriginalType"] for row in group_rows),errors=policy_errors,warnings=policy_warnings,evidenceScope="Recorded intermediate member payloads, actual parent/member deltas and independently retained chosen paths were checked. Missing nonselected branch coverage is reported separately; no sampled-away candidate was reconstructed.")
    if same_part_only:errors.extend(policy_errors)
    if raw_path.read_bytes()!=raw_bytes or index_path.read_bytes()!=index_bytes or event_path.read_bytes()!=event_bytes:errors.append("Completed raw/index/trace evidence changed during independent audit")
    checked["errors"].extend(errors);checked["passed"]=not checked["errors"]
    group_path=Path(str(stem)+".group-summary.json");audit_path=Path(str(stem)+".independent-audit.json")
    report=dict(**checked,scope="final-native-result",isInterim=False,geometryPassed=geometry_passed,legalFinalOutputAccepted=checked["passed"],finalOutputAccepted=checked["passed"] and checked["complete"],sourcePath=str(raw_path),sourceSha256=hashlib.sha256(raw_bytes).hexdigest(),inputSha256=wanted_input,executableSha256=wanted_executable,originalProblemRepresentedNumbersMatch=equivalent,finalTraceBound=final_bound,finalSelectionCount=len(selections),finalSelectionSource=selections[0]["source"] if len(selections)==1 else None,traceState={key:index.get(key) for key in ["runCompleted","observerFailed","stateEventsSeen","stateEventsWritten","stateEventsOmitted","stateSnapshotsTruncated","pathEventsSeen","pathEventsWritten","pathEventsOmitted","pathSnapshotsTruncated","summaryEventsOmitted","detailEventsOmitted","finalSelectionObserved","coreObservationTruncated","finalSnapshotTruncated"]},traceStateAudit=trace_states,sameOriginalTypeOnlyAudit=policy,sourceScope="Bound unchanged .problem input; actual supplied polygon contours/holes and analytic disks. Source stock dimensions are compared only through the same immutable input.",frozenSourceSnapshotDeclaration=manifest.get("sourceSha256",{}),frozenBuildMetadataReference=manifest.get("frozenBuildMetadataReference"),groupSummaryPath=str(group_path),independentAuditPath=str(audit_path),auditSeconds=time.perf_counter()-started,**metrics)
    group_summary=dict(sourcePath=str(raw_path),sourceSha256=report["sourceSha256"],inputSha256=wanted_input,executableSha256=wanted_executable,complete=checked["complete"],placed=checked["placed"],required=checked["required"],boards=checked["boards"],allFinalOriginalTypes=sorted(all_final_types),nonzeroGroups=group_rows,sameOriginalTypeOnlyAudit=policy,**metrics)
    group_path.write_text(json.dumps(group_summary,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    audit_path.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    return report


def audit_task(root,catalog,task,cache):
    started=time.perf_counter();errors=[]
    record=json.loads((root/"catalog/results"/(task["id"]+".json")).read_text(encoding="utf-8-sig"))
    signature=record["runSignature"];benchmark=record["benchmark"]
    if record["taskId"]!=task["id"] or signature["inputSha256"]!=task["inputSha256"]:raise ValueError("Result/input binding mismatch")
    if signature["executableSha256"]!=catalog["executionProfile"]["executableSha256"]:raise ValueError("Result is not from the current native executable")
    raw_path=contained(root,benchmark["rawPath"])
    if raw_path.parent!=(root/"catalog/raw").resolve() or not raw_path.name.startswith(task["id"]+"-"):raise ValueError("Unbound raw result path")
    raw=json.loads(raw_path.read_text(encoding="utf-8-sig"));input_path=contained(root,task["inputPath"])
    if digest(input_path)!=task["inputSha256"]:raise ValueError("Original native input SHA changed")
    folder=ROOT/"output/work/sheet-nesting/geos-audit";folder.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(dir=folder) as temporary:
        canonical=Path(temporary)/"input.problem";export(raw["problem"],canonical)
        same_bytes=canonical.read_bytes()==input_path.read_bytes()
        if not equivalent_interchange(canonical.read_text(),input_path.read_text(encoding="utf-8-sig")):
            raise ValueError("Native Problem differs from the bound original .problem")
    executable=contained(root,catalog["executionProfile"]["executablePath"])
    if executable not in cache:cache[executable]=digest(executable)
    if cache[executable]!=signature["executableSha256"]:raise ValueError("Pinned executable SHA mismatch")
    evidence={}
    assets={a["id"]:a for a in catalog["assets"]}
    for part in task.get("sourceParts",task["parts"]):
        if part.get("assetId") in assets:
            asset=assets[part["assetId"]];evidence[asset["relativePath"]]=asset["sha256"]
            for path in asset.get("sourcePaths",[]):evidence[path]=asset["sha256"]
        source=part.get("geometrySource") or {}
        if source.get("path") and source.get("sha256"):evidence[source["path"]]=source["sha256"]
    for item in (task.get("sourceProvenance") or {}).get("files",{}).values():evidence[item["path"]]=item["sha256"]
    for path,expected in evidence.items():
        source=contained(root,path)
        if source not in cache:cache[source]=digest(source)
        if cache[source]!=expected:raise ValueError("Original source SHA mismatch: "+path)
    checked=audit_geometry(raw["problem"],raw["result"])
    if task["required"]!=checked["required"]:errors.append("Task/native demand mismatch")
    if not raw["validation"]["valid"] or not raw["result"]["valid"]:errors.append("Native validation is not valid")
    if raw["validation"]["complete"]!=checked["complete"] or raw["validation"]["placed"]!=checked["placed"]:errors.append("Native validation counts differ")
    if not math.isclose(checked["materialAreaMm2"],raw["validation"]["materialArea"],rel_tol=1e-10,abs_tol=1e-6):errors.append("Independent net material area differs")
    descriptor=benchmark["nativeConstructionTrace"]
    if descriptor["runSignature"]!=signature:errors.append("Trace signature differs")
    index_path=contained(root,descriptor["indexPath"])
    if index_path!=raw_path.with_suffix(".trace-index.json"):errors.append("Trace is not adjacent to its own native raw result")
    index=json.loads(index_path.read_text(encoding="utf-8-sig"))
    if index["problem"]!=raw["problem"] or index["resultFile"]!=raw_path.name:errors.append("Trace/input/result binding differs")
    events_path=(index_path.parent/index["eventsFile"]).resolve()
    if events_path.parent!=index_path.parent or events_path.name!=raw_path.with_suffix(".construction.jsonl").name:raise ValueError("Unbound trace events path")
    finals=[]
    for line in events_path.open(encoding="utf-8-sig"):
        if 'scheduler.final-selection' not in line:continue
        event=json.loads(line)["event"]
        if event["source"]=="scheduler.final-selection":finals.append(event)
    if len(finals)!=1:errors.append("Expected exactly one final native selection")
    elif normalized_layouts(finals[0]["layouts"],True)!=normalized_layouts(raw["result"]["layouts"]) or finals[0]["remaining"]!=raw["result"]["remaining"]:
        errors.append("Final native trace snapshot differs from raw identities/poses/groups/stock/area")
    checked["errors"].extend(errors);checked["passed"]=not checked["errors"]
    return dict(taskId=task["id"],rawPath=raw_path.relative_to(root).as_posix(),rawSha256=digest(raw_path),
        inputSha256=task["inputSha256"],executableSha256=signature["executableSha256"],canonicalInputBytesMatch=same_bytes,
        originalProblemRepresentedNumbersMatch=True,sourceSha256Evidence=evidence,traceFinalEqualsRaw=not any("trace" in str(e).lower() or "selection" in str(e).lower() for e in errors),
        traceComparisonFields="stock identity, instance, dimensions, materialArea; placement identity, instance, x/y/angle/mirrored/group; remaining. gapFiller is not serialized by trace schema.",
        auditSeconds=time.perf_counter()-started,**checked)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--root",type=Path,default=ROOT/"TestData/Plane")
    parser.add_argument("--tasks",required=True);parser.add_argument("--output",type=Path,required=True);args=parser.parse_args()
    root=args.root.resolve();catalog=json.loads((root/"catalog/catalog.json").read_text(encoding="utf-8-sig"));tasks={t["id"]:t for t in catalog["tasks"]};cache={};rows=[]
    for identity in args.tasks.split(","):
        try:row=audit_task(root,catalog,tasks[identity],cache)
        except Exception as error:row=dict(taskId=identity,passed=False,errors=[f"{type(error).__name__}: {error}"])
        rows.append(row);print(json.dumps({k:row.get(k) for k in ("taskId","passed","placed","required","pairsChecked","auditSeconds","errors")}),flush=True)
    report=dict(engine=f"Independent Python / Shapely {shapely.__version__} / GEOS {shapely.geos_version_string}",
        geometryPolicy="Read-only original .problem and stored native poses. Conservative imported polygons remain represented polygons; no exact original-curve claim. Disks checked analytically.",
        passed=all(r["passed"] for r in rows),tasks=len(rows),rows=rows)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with args.output.open("x",encoding="utf-8") as output:json.dump(report,output,ensure_ascii=False,indent=2)
    return 0 if report["passed"] else 1


if __name__=="__main__":raise SystemExit(main())
