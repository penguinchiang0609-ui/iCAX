"""Adversarial controls for the independent read-only native audit."""
from copy import deepcopy
import json
import math
from pathlib import Path
import tempfile
import unittest

from audit_material_native import (PATH_SOURCES,audit_bound_native_run,audit_geometry,audit_trace_states,
    commit_rank_errors,copy_mapping_errors,digest,equivalent_interchange,export,normalized_layouts,world_geometry)
from shapely import Polygon
from shapely.affinity import rotate,scale,translate


def part(identity="p",geometry=None):
    return dict(id=identity,quantity=1,material="m",freeRotation=True,allowedAngles=[],geometry=geometry or
        dict(radius=0,center=[0,0],outer=[[0,0],[1,0],[1,1],[0,1]],holes=[],commonEdgeRings=[]))


def fixture(second=(1.5,.5)):
    problem=dict(partGap=.2,edgeMargin=.5,tolerance=1e-7,allowCommonEdges=True,
        parts=[part("a"),part("b")],stocks=[dict(id="s",width=20,height=20,quantity=1,cost=1,material="m")])
    result=dict(placed=2,complete=True,remainingCount=0,boards=1,remaining=[],layouts=[
        dict(stock="s",instance=0,width=20,height=20,placements=[
            dict(part="a",instance=0,x=.5,y=.5,angle=0,group=0),dict(part="b",instance=0,x=second[0],y=second[1],angle=0,group=0)])])
    return problem,result


def lane_index(events,path=True,omitted=0):
    states=sum(e.get("source") not in PATH_SOURCES and (e.get("stage")=="commit" or
        e.get("source")=="board.raw-construction") for e in events)
    index=dict(stateEventsSeen=states+omitted,stateEventsWritten=states,stateEventsOmitted=omitted,
        stateSnapshotsTruncated=0,coreObservationTruncated=bool(omitted))
    if path:
        count=sum(e.get("source") in PATH_SOURCES for e in events)
        index.update(pathEventsSeen=count,pathEventsWritten=count,pathEventsOmitted=0,pathSnapshotsTruncated=0)
    return index


class MaterialNativeAuditTests(unittest.TestCase):
    def rank_fixture(self,local=False):
        problem,result=fixture();board=result["layouts"][0]
        traced=dict(stockID="s",stockInstance=0,width=20,height=20,materialArea=2,placements=[
            dict(part=p["part"],instance=p["instance"],pose={key:p[key] for key in ("x","y","angle")},group=0)
            for p in board["placements"]])
        pairs=[("negativeUsedSectionUtilization",-.05),("negativeNetMaterial",-2),
            ("orientationOrOccupiedArea",2),("intrinsicFirstMaterialCornerDistance",0),
            ("primaryAxis",.5),("secondaryAxis",.5),("part",0),("template",1),("columns",2),("rows",1)]
        measures=[]
        if local:
            pairs[2:2]=[("negativeLargeRegularFreeSpace",-6),("fragmentFreeArea",2),
                ("freeBoundaryPerimeter",20),("negativeAttachment",-.5)]
            measures=[dict(name=name,value=value) for name,value in [
                ("largestFreeArea",8),("largeRegularFreeSpace",6),("fragmentFreeArea",2),
                ("freeBoundaryPerimeter",20),("attachment",.5),("freeArea",10),
                ("freeComponents",2),("spaceCircleProxy",0),("spaceEvaluationSeconds",.01),
                ("spaceCandidatesEvaluated",8)]]
        event=dict(layouts=[traced],members=[dict(partIndex=i,instance=0) for i in range(2)],
            rank=[dict(name=name,value=value) for name,value in pairs],measures=measures)
        return problem,event

    def trace_fixture(self,path=True,envelope=False,scope="actual-child",route="route-a"):
        problem,ranked=self.rank_fixture();board=ranked["layouts"][0];commits=[]
        for count in (1,2):
            prefix=deepcopy(board);prefix["placements"]=prefix["placements"][:count];prefix["materialArea"]=count
            for placed in prefix["placements"]:placed["pose"]["mirrored"]=False
            placed=prefix["placements"][-1];rank=deepcopy(ranked["rank"])
            rank[0]["value"]=-count/40;rank[1]["value"]=-1
            commits.append(dict(route=route,scope=scope,stage="commit",source="envelope.skyline" if envelope else "constructor.commit",
                stateID=count,parentStateID=count-1,candidateID=count,proposalID=count,stockInstance=0,
                layouts=[prefix],members=[dict(partIndex=count-1,instance=0,pose=deepcopy(placed["pose"]),group=0)],
                rank=[] if envelope else rank,measures=[],truncated=False))
        raw=dict(route=route,scope=scope,stage="complete",source="board.raw-construction",stateID=2,
            layouts=deepcopy(commits[-1]["layouts"]),members=[],rank=[],measures=[],truncated=False)
        events=deepcopy(commits)+[raw]
        if path:
            for original in commits:
                selected=deepcopy(original);selected.update(source="construction.path.commit",stage="selected")
                events.append(selected)
            selected=deepcopy(raw);selected["source"]="construction.path.raw";events.append(selected)
        terminal=dict(route=route,scope="",source="scheduler.route-result",stage="complete",stateID=0,
            layouts=deepcopy(raw["layouts"]),remaining=[],members=[],rank=[],measures=[],truncated=False)
        events.append(terminal)
        return problem,events,normalized_layouts(terminal["layouts"],True)

    def test_actual_commit_accepts_exact_legacy_and_local_space_rank_schemas(self):
        for local in (False,True):
            with self.subTest(local=local):
                p,e=self.rank_fixture(local);before=deepcopy((p,e))
                self.assertEqual(commit_rank_errors(p,e),[])
                self.assertEqual((p,e),before)

    def test_contact_space_rank_binds_contact_and_availability(self):
        for available in (False,True):
            p,e=self.rank_fixture(True)
            e["rank"].insert(2,dict(name="strategyContact",value=-.75))
            e["measures"] += [dict(name="strategyContact",value=-.75),
                dict(name="spaceScoreValid",value=int(available))]
            if not available:
                for row in e["rank"][3:7]:row["value"]=0
                e["measures"]=e["measures"][-2:]
            with self.subTest(available=available):
                self.assertEqual(commit_rank_errors(p,e),[])
                broken=deepcopy(e);broken["rank"][2]["value"]=-.5
                self.assertTrue(commit_rank_errors(p,broken))
                broken=deepcopy(e);broken["measures"].pop()
                self.assertTrue(commit_rank_errors(p,broken))
                if not available:
                    broken=deepcopy(e);broken["rank"][3]["value"]=-1
                    self.assertTrue(commit_rank_errors(p,broken))

    def test_actual_commit_rejects_unknown_reordered_duplicate_and_nonfinite_ranks(self):
        for local in (False,True):
            for mode in ("unknown","reordered","duplicate","extra","missing","nan","infinity","boolean","none"):
                p,e=self.rank_fixture(local)
                if mode=="unknown":e["rank"][2]["name"]="futureUnknownScore"
                elif mode=="reordered":e["rank"][0],e["rank"][1]=e["rank"][1],e["rank"][0]
                elif mode=="duplicate":e["rank"][2]["name"]=e["rank"][1]["name"]
                elif mode=="extra":e["rank"].append(dict(name="futureScore",value=0))
                elif mode=="missing":e["rank"].pop()
                elif mode=="none":e["rank"]=None
                else:e["rank"][-1]["value"]={"nan":math.nan,"infinity":math.inf,"boolean":True}[mode]
                with self.subTest(local=local,mode=mode):self.assertTrue(commit_rank_errors(p,e))

    def test_both_actual_rank_schemas_keep_primary_original_utilization_and_net_material(self):
        for local in (False,True):
            for index in (0,1):
                p,e=self.rank_fixture(local);e["rank"][index]["value"]*=.9
                with self.subTest(local=local,index=index):self.assertTrue(commit_rank_errors(p,e))
            p,e=self.rank_fixture(local);e["members"].pop()
            with self.subTest(local=local,mode="added-material-binding"):
                self.assertTrue(commit_rank_errors(p,e))

    def test_local_rank_measures_are_exactly_bound_and_finite_without_loosening_old_schema(self):
        for mode in ("rank-mismatch","missing","duplicate","negative","nan","area-sum","regular-area","count","proxy"):
            p,e=self.rank_fixture(True);by_name={row["name"]:row for row in e["measures"]}
            if mode=="rank-mismatch":e["rank"][2]["value"]=-6.000000000000001
            elif mode=="missing":e["measures"].pop()
            elif mode=="duplicate":e["measures"].append(deepcopy(e["measures"][0]))
            elif mode=="negative":by_name["attachment"]["value"]=-.5
            elif mode=="nan":by_name["freeArea"]["value"]=math.nan
            elif mode=="area-sum":by_name["freeArea"]["value"]=11
            elif mode=="regular-area":by_name["largeRegularFreeSpace"]["value"]=9;e["rank"][2]["value"]=-9
            elif mode=="count":by_name["freeComponents"]["value"]=1.5
            else:by_name["spaceCircleProxy"]["value"]=2
            with self.subTest(mode=mode):self.assertTrue(commit_rank_errors(p,e))
        p,e=self.rank_fixture();e["measures"]=[dict(name="attachment",value=0)]
        self.assertTrue(commit_rank_errors(p,e))

    def test_common_edge_requires_positive_eligible_line_materials_do_not_overlap(self):
        p,r=fixture();before=deepcopy((p,r));a=audit_geometry(p,r)
        self.assertTrue(a["passed"]);self.assertEqual(len(a["eligibleCommonEdgePairs"]),1)
        self.assertEqual(a["eligibleCommonEdgePairs"][0]["eligibleCommonLengthMm"],1)
        self.assertEqual((p,r),before)
        p["parts"][1]["geometry"]["commonEdgeRings"]=[False]
        self.assertFalse(audit_geometry(p,r)["passed"])

    def test_isolated_point_does_not_waive_gap(self):
        p,r=fixture((1.5,1.5));self.assertFalse(audit_geometry(p,r)["passed"])

    def test_overlap_and_insufficient_ordinary_gap_fail(self):
        for point in ((1.49,.5),(1.6,.5)):
            with self.subTest(point=point):
                p,r=fixture(point);self.assertFalse(audit_geometry(p,r)["passed"])

    def test_inventory_angle_and_board_margin_are_independent_checks(self):
        for mode in ("instance","angle","margin"):
            p,r=fixture((2,.5))
            if mode=="instance":r["layouts"][0]["placements"][1]["instance"]=1
            elif mode=="angle":p["parts"][1]["freeRotation"]=False;r["layouts"][0]["placements"][1].update(x=3,angle=90)
            else:r["layouts"][0]["placements"][0]["x"]=.49
            with self.subTest(mode=mode):self.assertFalse(audit_geometry(p,r)["passed"])

    def test_disks_are_checked_analytically_without_tessellation(self):
        p,r=fixture();p["parts"]=[part("a",dict(radius=1,center=[0,0],outer=[],holes=[],commonEdgeRings=[])),
            part("b",dict(radius=.5,center=[0,0],outer=[],holes=[],commonEdgeRings=[]))]
        r["layouts"][0]["placements"][0].update(x=2,y=2)
        r["layouts"][0]["placements"][1].update(x=3.7,y=2)
        a=audit_geometry(p,r);self.assertTrue(a["passed"]);self.assertEqual(a["analyticCirclePlacements"],2)
        self.assertEqual(a["analyticCirclePairs"],1);self.assertAlmostEqual(a["materialAreaMm2"],1.25*math.pi)
        r["layouts"][0]["placements"][1]["x"]=3.5
        self.assertFalse(audit_geometry(p,r)["passed"])  # Circular point tangency is not a common edge.

    def test_polygon_hole_is_material_free_for_an_analytic_disk(self):
        p,r=fixture();p["parts"][0]["geometry"].update(outer=[[0,0],[10,0],[10,10],[0,10]],
            holes=[[[2,2],[8,2],[8,8],[2,8]]],commonEdgeRings=[True,True])
        p["parts"][1]["geometry"]=dict(radius=1,center=[0,0],outer=[],holes=[],commonEdgeRings=[])
        r["layouts"][0]["placements"][1].update(x=5.5,y=5.5)
        self.assertTrue(audit_geometry(p,r)["passed"])
        r["layouts"][0]["placements"][1]["x"]=2.5
        self.assertFalse(audit_geometry(p,r)["passed"])

    def test_far_pairs_use_bounds_without_losing_near_pairs(self):
        p,r=fixture((10,10));a=audit_geometry(p,r)
        self.assertTrue(a["passed"]);self.assertEqual(a["pairsPossible"],1);self.assertEqual(a["pairsChecked"],0)
        self.assertEqual(a["pairsSeparatedByBounds"],1)

    def test_numeric_interchange_comparison_keeps_identity_strings_exact(self):
        self.assertTrue(equivalent_interchange('V "01" -0 1.000\n','V "01" 0 1\n'))
        self.assertFalse(equivalent_interchange('V "01" 1','V "1" 1'))
        self.assertFalse(equivalent_interchange('V "part" 1','V "part" 1.0000000000000002'))

    def test_mirrored_asymmetric_outer_and_hole_use_local_reflection_before_rotation(self):
        geometry=dict(radius=0,center=[0,0],outer=[[0,0],[10,0],[10,4],[7,4],[7,10],[0,10]],
            holes=[[[2,1],[5,1],[5,4],[2,4]]],commonEdgeRings=[True,False])
        source=part("host",geometry);source["allowMirror"]=True
        pose=dict(part="host",instance=0,x=20,y=30,angle=37,mirrored=True)
        before=deepcopy((source,pose))
        actual=world_geometry(source,pose)
        expected=translate(rotate(scale(Polygon(geometry["outer"],geometry["holes"]),
            xfact=1,yfact=-1,origin=(0,0)),37,origin=(0,0)),xoff=20,yoff=30)
        self.assertTrue(actual.polygon.equals_exact(expected,1e-12))
        self.assertAlmostEqual(actual.polygon.area,73)
        self.assertEqual(len(actual.eligible),1)
        self.assertEqual((source,pose),before)
        p,r=fixture((3.5,7.5));p["parts"][0]=source;r["layouts"][0]["placements"][0].update(
            part="host",x=1,y=11,angle=0,mirrored=True)
        self.assertTrue(audit_geometry(p,r)["passed"])
        r["layouts"][0]["placements"][0].update(y=1,mirrored=False)
        self.assertFalse(audit_geometry(p,r)["passed"])

    def test_mirrored_circle_center_at_arbitrary_angle_remains_analytic(self):
        p,r=fixture((9.5,13.5))
        p["parts"][0]=part("a",dict(radius=1,center=[2,3],outer=[],holes=[],commonEdgeRings=[]))
        p["parts"][0]["allowMirror"]=True
        pose=r["layouts"][0]["placements"][0];pose.update(x=10,y=10,angle=37,mirrored=True)
        world=world_geometry(p["parts"][0],pose)
        c,s=math.cos(math.radians(37)),math.sin(math.radians(37))
        self.assertAlmostEqual(world.center[0],10+2*c+3*s)
        self.assertAlmostEqual(world.center[1],10+2*s-3*c)
        self.assertIsNone(world.polygon)
        checked=audit_geometry(p,r)
        self.assertTrue(checked["passed"])
        self.assertEqual(checked["analyticCirclePlacements"],1)
        self.assertAlmostEqual(checked["materialAreaMm2"],math.pi+1)
        pose["mirrored"]=False
        self.assertFalse(audit_geometry(p,r)["passed"])

    def test_mirror_permission_is_a_hard_gate_and_legacy_absence_means_false(self):
        p,r=fixture((10,10));r["layouts"][0]["placements"][0].update(y=1.5,mirrored=True)
        checked=audit_geometry(p,r)
        self.assertFalse(checked["passed"]);self.assertIn("Forbidden mirror",checked["errors"])
        p["parts"][0]["allowMirror"]=True
        self.assertTrue(audit_geometry(p,r)["passed"])
        p["parts"][0]["allowMirror"]=False
        self.assertFalse(audit_geometry(p,r)["passed"])
        p,r=fixture((10,10))
        self.assertNotIn("allowMirror",p["parts"][0]);self.assertNotIn("mirrored",r["layouts"][0]["placements"][0])
        self.assertTrue(audit_geometry(p,r)["passed"])

    def test_mirror_pose_must_be_a_json_boolean(self):
        for value in (0,1,"false","true",None):
            with self.subTest(value=value):
                p,r=fixture((10,10));p["parts"][0]["allowMirror"]=True
                r["layouts"][0]["placements"][0]["mirrored"]=value
                checked=audit_geometry(p,r)
                self.assertFalse(checked["passed"]);self.assertIn("Invalid mirror state",checked["errors"])

    def test_trace_binding_preserves_mirror_parity_and_legacy_false(self):
        direct=[dict(stock="s",instance=0,width=20,height=20,materialArea=1,placements=[
            dict(part="original",instance=0,x=2,y=3,angle=37,mirrored=True,group=0)])]
        traced=[dict(stockID="s",stockInstance=0,width=20,height=20,materialArea=1,placements=[
            dict(part="original",instance=0,pose=dict(x=2,y=3,angle=37,mirrored=True),group=0)])]
        self.assertEqual(normalized_layouts(direct),normalized_layouts(traced,traced=True))
        traced[0]["placements"][0]["pose"]["mirrored"]=False
        self.assertNotEqual(normalized_layouts(direct),normalized_layouts(traced,traced=True))
        direct[0]["placements"][0].pop("mirrored")
        self.assertEqual(normalized_layouts(direct),normalized_layouts(traced,traced=True))

    def test_complete_independent_path_keeps_exact_original_parent_deltas_and_rank(self):
        problem,events,final=self.trace_fixture();before=deepcopy((problem,events,final))
        checked=audit_trace_states(problem,events,lane_index(events),final,"route-a")
        self.assertEqual(checked["errors"],[]);self.assertEqual(checked["warnings"],[])
        self.assertEqual(checked["actualCommitsChecked"],2)
        self.assertEqual(checked["actualCommitRanksChecked"],2)
        self.assertEqual((problem,events,final),before)

    def test_missing_nonselected_branch_state_is_only_coverage_warning_with_complete_bound_path(self):
        problem,events,final=self.trace_fixture();events.pop(0)
        checked=audit_trace_states(problem,events,lane_index(events,omitted=1),final,"route-a")
        self.assertEqual(checked["errors"],[])
        self.assertTrue(any("branch coverage" in row for row in checked["warnings"]))
        self.assertEqual(checked["actualCommitRanksChecked"],2)
        events[-2]["layouts"][0]["placements"][0]["pose"]["x"]+=.01
        self.assertTrue(audit_trace_states(problem,events,lane_index(events,omitted=1),final,"route-a")["errors"])

    def test_legacy_missing_state_and_unaccounted_state_count_still_fail(self):
        problem,events,final=self.trace_fixture(path=False)
        self.assertEqual(audit_trace_states(problem,events,lane_index(events,False),final,"route-a")["errors"],[])
        events.pop(0)
        self.assertTrue(audit_trace_states(problem,events,lane_index(events,False,1),final,"route-a")["errors"])
        problem,events,final=self.trace_fixture();index=lane_index(events);index["stateEventsWritten"]+=1
        self.assertTrue(audit_trace_states(problem,events,index,final,"route-a")["errors"])

    def test_selected_path_loss_truncation_bad_parent_pose_rank_or_raw_cannot_be_downgraded(self):
        for mode in ("omitted","snapshot","missing-parent","bad-pose","bad-rank","changed-native-tie",
            "bad-raw","bad-stage","bad-instance"):
            problem,events,final=self.trace_fixture();index=lane_index(events)
            path=[event for event in events if event["source"]=="construction.path.commit"]
            if mode=="omitted":index["pathEventsSeen"]+=1;index["pathEventsOmitted"]=1
            elif mode=="snapshot":index["pathSnapshotsTruncated"]=1
            elif mode=="missing-parent":events.remove(path[0]);index=lane_index(events)
            elif mode=="bad-pose":path[1]["members"][0]["pose"]["x"]+=.01
            elif mode=="bad-rank":path[1]["rank"][0]["value"]=-.8
            elif mode=="changed-native-tie":path[1]["rank"][3]["value"]=.1
            elif mode=="bad-raw":events[-2]["layouts"][0]["placements"][0]["pose"]["mirrored"]=True
            elif mode=="bad-stage":path[1]["stage"]="commit"
            else:path[1]["members"][0]["instance"]=True
            with self.subTest(mode=mode):
                self.assertTrue(audit_trace_states(problem,events,index,final,"route-a")["errors"])

    def test_envelope_commit_uses_its_actual_single_original_delta_and_independent_rank(self):
        problem,events,final=self.trace_fixture(path=False,envelope=True)
        checked=audit_trace_states(problem,events,lane_index(events),final,"route-a")
        self.assertEqual(checked["errors"],[]);self.assertEqual(checked["envelopeCommitsChecked"],2)
        self.assertEqual(checked["actualCommitRanksChecked"],0)
        for mode in ("missing-parent","two-members","raw-mismatch","invalid-rank"):
            broken=deepcopy(events)
            if mode=="missing-parent":broken.pop(0)
            elif mode=="two-members":broken[1]["members"].append(deepcopy(broken[0]["members"][0]))
            elif mode=="raw-mismatch":broken[2]["layouts"][0]["placements"][0]["pose"]["x"]+=1
            else:broken[0]["rank"]=[dict(name="inventedUtilization",value=0)]
            with self.subTest(mode=mode):
                self.assertTrue(audit_trace_states(problem,broken,lane_index(broken),final,"route-a")["errors"])

    def test_path_identity_is_scoped_by_real_route_as_well_as_child_scope(self):
        problem,first,final=self.trace_fixture(route="first")
        _,second,_=self.trace_fixture(route="second")
        events=first+second
        self.assertEqual(audit_trace_states(problem,events,lane_index(events),final,"first")["errors"],[])

    def test_random_selected_child_must_be_explicit_and_exact_not_the_last_restart(self):
        problem,first,final=self.trace_fixture(scope="/restart-0/kind-16/trial-0")
        _,second,_=self.trace_fixture(scope="/restart-1/kind-16/trial-0")
        events=first[:-1]+second[:-1]+[deepcopy(first[-1])]
        selection=dict(route="route-a",scope="/restart-0/kind-16/trial-0",stage="selected",
            source="strategy.construction-selection",layouts=deepcopy(first[-1]["layouts"]),remaining=[],
            measures=[dict(name="restartIndex",value=0)],members=[],rank=[],truncated=False)
        events.insert(-1,selection)
        self.assertEqual(audit_trace_states(problem,events,lane_index(events),final,"route-a")["errors"],[])
        for mode in ("missing","wrong-scope","wrong-pose"):
            broken=deepcopy(events)
            if mode=="missing":broken.pop(-2)
            elif mode=="wrong-scope":broken[-2]["scope"]="/restart-7/kind-16/trial-0"
            else:broken[-2]["layouts"][0]["placements"][0]["pose"]["x"]+=1
            with self.subTest(mode=mode):
                self.assertTrue(audit_trace_states(problem,broken,lane_index(broken),final,"route-a")["errors"])

    def test_mixed_board_selection_binds_real_child_to_ordered_promoted_originals(self):
        problem,events,_=self.trace_fixture(scope="/stock-0/board-1/competitor-0/kind-0/trial-0")
        problem["stocks"][0]["quantity"]=2
        for part in problem["parts"]:part["quantity"]=2
        for event in events:
            for board in event.get("layouts",[]):
                for placed in board["placements"]:placed["group"]=1 if placed["part"]=="a" else 2
            for member in event.get("members",[]):member["group"]=member["partIndex"]+1
        source=deepcopy(events[-1]["layouts"][0]);target=deepcopy(source);target["stockInstance"]=1
        for i,placed in enumerate(target["placements"]):placed.update(instance=1,group=i+3)
        promotion=dict(route="route-a",scope=events[0]["scope"],source="competition.board-selection",
            stage="selected",layouts=[source,target],members=[],rank=[],truncated=False,
            measures=[dict(name="sourceLayoutIndex",value=0),dict(name="targetLayoutIndex",value=1)])
        events[-1]["layouts"]=[deepcopy(target)];events.insert(-1,promotion)
        final=normalized_layouts([target],True)
        self.assertEqual(audit_trace_states(problem,events,lane_index(events),final,"route-a")["errors"],[])
        for mode in ("wrong-scope","changed-source","changed-target","wrong-index","merged-group","bad-instance"):
            broken=deepcopy(events);promotion=broken[-2]
            if mode=="wrong-scope":promotion["scope"]="/stock-1/board-1/competitor-0/kind-0/trial-0"
            elif mode=="wrong-index":promotion["measures"][1]["value"]=0
            elif mode=="merged-group":promotion["layouts"][1]["placements"][1]["group"]=3
            elif mode=="bad-instance":promotion["layouts"][1]["placements"][0]["instance"]=2
            else:promotion["layouts"][0 if mode=="changed-source" else 1]["placements"][0]["pose"]["x"]+=1
            with self.subTest(mode=mode):
                self.assertTrue(audit_trace_states(problem,broken,lane_index(broken),final,"route-a")["errors"])

    def copy_fixture(self):
        problem,events,_=self.trace_fixture()
        problem["stocks"][0]["quantity"]=2
        for part in problem["parts"]:part["quantity"]=2
        # The same original order is retained across source and target. Group
        # tags are remapped only in the copied physical board.
        for event in events:
            for board in event.get("layouts",[]):
                for placed in board["placements"]:placed["group"]=1 if placed["part"]=="a" else 2
            for member in event.get("members",[]):member["group"]=member["partIndex"]+1
        source=deepcopy(events[-1]["layouts"][0]);target=deepcopy(source);target["stockInstance"]=1
        for placed in target["placements"]:placed["instance"]=1;placed["group"]+=2
        copy=dict(route="route-a",scope="",stage="commit",source="sheet-template.copy",stateID=0,
            stockInstance=1,layouts=[source,target],members=[],rank=[],truncated=False,
            measures=[dict(name="sourceLayoutIndex",value=0),dict(name="targetLayoutIndex",value=1)])
        events[-1]["layouts"].append(deepcopy(target));events.insert(-1,copy)
        return problem,events,normalized_layouts([source,target],True)

    def test_atomic_copy_is_one_path_step_not_an_original_commit_or_state_lane_record(self):
        problem,events,final=self.copy_fixture();index=lane_index(events)
        self.assertEqual(index["stateEventsWritten"],3);self.assertEqual(index["pathEventsWritten"],4)
        self.assertEqual(copy_mapping_errors(problem,events[-2]),[])
        checked=audit_trace_states(problem,events,index,final,"route-a")
        self.assertEqual(checked["errors"],[]);self.assertEqual(checked["actualCommitsChecked"],2)
        self.assertEqual(checked["actualCommitRanksChecked"],2)

    def test_atomic_copy_rejects_pose_mirror_stock_identity_instance_and_group_mapping_changes(self):
        for mode in ("pose","mirror","reordered","instance","zero-group","old-group","merged-group","stock","index","members"):
            problem,events,final=self.copy_fixture();copy=events[-2];target=copy["layouts"][1]
            if mode=="pose":target["placements"][0]["pose"]["x"]+=.01
            elif mode=="mirror":target["placements"][0]["pose"]["mirrored"]=True
            elif mode=="reordered":target["placements"].reverse()
            elif mode=="instance":target["placements"][0]["instance"]=0
            elif mode=="zero-group":copy["layouts"][0]["placements"][0]["group"]=0
            elif mode=="old-group":target["placements"][0]["group"]=1
            elif mode=="merged-group":target["placements"][1]["group"]=target["placements"][0]["group"]
            elif mode=="stock":target["width"]+=1
            elif mode=="index":copy["measures"][1]["value"]=0
            else:copy["members"]=[dict(partIndex=0)]
            with self.subTest(mode=mode):
                self.assertTrue(copy_mapping_errors(problem,copy))
                self.assertTrue(audit_trace_states(problem,events,lane_index(events),final,"route-a")["errors"])

    def test_atomic_copy_cannot_bind_to_an_unrecorded_or_changed_source_template(self):
        problem,events,final=self.copy_fixture();events[-2]["layouts"][0]["placements"][0]["group"]+=10
        self.assertTrue(audit_trace_states(problem,events,lane_index(events),final,"route-a")["errors"])

    def test_bound_audit_accepts_complete_path_with_branch_loss_but_never_changed_final_coordinates(self):
        problem,events,final=self.trace_fixture();events.pop(0)
        work=Path(__file__).resolve().parents[5]/"output/work/sheet-nesting/material-audit-controls"
        work.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=work) as temporary:
            folder=Path(temporary);stem=folder/"observed";input_path=folder/"original.problem";export(problem,input_path)
            executable=folder/"bound.exe";executable.write_bytes(b"frozen native program identity for read-only audit controls")
            terminal=deepcopy(events[-1]);terminal["source"]="scheduler.final-selection";events.append(terminal)
            index=lane_index(events,omitted=1);index.update(problem=problem,resultFile="observed.json",
                eventsFile="observed.construction.jsonl",runCompleted=True,observerFailed=False,
                finalSelectionObserved=True,finalSnapshotTruncated=False)
            raw=dict(problem=problem,result=dict(layouts=final,remaining=[],placed=2,boards=1,complete=True,
                remainingCount=0,valid=True),validation=dict(valid=True,complete=True,placed=2,materialArea=2),
                settings={},nativeConstructionTrace="observed.trace-index.json")
            manifest=dict(inputPath=str(input_path),inputSha256=digest(input_path),executablePath=str(executable),
                executableSha256=digest(executable),runState="completed",nativeProcessExitCode=0)
            def write():
                for suffix,value in ((".json",raw),(".trace-index.json",index),(".run.json",manifest)):
                    Path(str(stem)+suffix).write_text(json.dumps(value),encoding="utf-8")
                Path(str(stem)+".construction.jsonl").write_text("".join(json.dumps(dict(ordinal=i+1,event=e))+"\n"
                    for i,e in enumerate(events)),encoding="utf-8")
            write();report=audit_bound_native_run(stem)
            self.assertTrue(report["passed"],report["errors"]);self.assertTrue(report["finalTraceBound"])
            self.assertTrue(report["warnings"])
            before=Path(str(stem)+".json").read_bytes()
            events[-1]["layouts"][0]["placements"][0]["pose"]["x"]+=.01;write()
            rejected=audit_bound_native_run(stem)
            self.assertFalse(rejected["passed"]);self.assertFalse(rejected["finalTraceBound"])
            self.assertEqual(Path(str(stem)+".json").read_bytes(),before)


if __name__=="__main__":unittest.main()
