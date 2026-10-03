"""Stage barriers preserve hard graph and material-snapshot requirements."""
from copy import deepcopy
import ast
import importlib.util
import json
from pathlib import Path
import sys
import unittest


SRC = Path(__file__).resolve().parents[5]
SHARED = SRC / "apps/tube-designer/templates/_shared"
spec = importlib.util.spec_from_file_location("test_assembly_process_stages", SHARED / "assembly_process_stages.py")
STAGES = importlib.util.module_from_spec(spec)
spec.loader.exec_module(STAGES)


def process(key, template="surface-feature-cut", dependencies=(), *, parts=None, geometry=None):
    return {"key": key, "kind": "assembly-process", "definition": {
        "templateId": template, "dependencies": list(dependencies), "targets": {"stock": "stock"},
        "parameters": {}, "processDrafts": {}, "processInput": {
            "schema": "icax.assembly-process-input", "schemaVersion": 2,
            "parts": parts if parts is not None else {
                "stock": {"scope": "manufacturing", "itemKey": "stock", "state": "initial", "data": {}}},
            "geometry": geometry or {}}}}


class AssemblyProcessStagesTests(unittest.TestCase):
    def test_stage_barriers_apply_to_supplied_reverse_order_without_mutating_input(self):
        calls = [process("check", "tube-insertion-check"), process("socket", "tube-profile-aperture"),
                 process("bend.b", "node-v-notch-integrated"), process("bend.a", "bend"),
                 process("prepare", "profile-stock-preparation")]
        original = deepcopy(calls)
        plan = STAGES.plan_stages(calls)
        self.assertEqual(plan["orderedKeys"], ["prepare", "bend.b", "bend.a", "socket", "check"])
        self.assertEqual(calls, original)
        self.assertIs(STAGES.ordered_processes(calls)[0], calls[-1])
        self.assertEqual([batch["stage"] for batch in plan["stages"]], list(STAGES.STAGES))

    def test_same_stage_after_reads_the_explicit_writer_without_inventing_latest_state(self):
        after = {"scope": "manufacturing", "itemKey": "stock", "state": {"after": "male"}, "data": {}}
        calls = [process("female", dependencies=["male"], parts={"stock": after}),
                 process("independent"), process("male", "structural-male-head")]
        ordered = STAGES.ordered_processes(calls)
        self.assertEqual([p["key"] for p in ordered], ["male", "female", "independent"])
        self.assertEqual(calls[0]["definition"]["processInput"]["parts"]["stock"]["state"], {"after": "male"})
        self.assertEqual(calls[1]["definition"]["processInput"]["parts"]["stock"]["state"], "initial")

    def test_stage_cannot_override_an_explicit_backward_dependency(self):
        calls = [process("fold", "bend", dependencies=["socket"]), process("socket", "tube-profile-aperture")]
        with self.assertRaisesRegex(ValueError, "stage dependency conflict.*fold.*socket"):
            STAGES.plan_stages(calls)

    def test_boundary_and_cutter_facets_use_distinct_planning_tasks(self):
        boundary = process("end.requirement", "tube-end-joint", geometry={"allocation": "initial-material-boundary"})
        replay = process("end.cutter", "tube-end-joint", dependencies=["fold"])
        calls = [replay, process("fold", "bend", dependencies=[boundary["key"]]), boundary]
        self.assertEqual(STAGES.plan_stages(calls)["orderedKeys"], ["end.requirement", "fold", "end.cutter"])
        self.assertEqual(STAGES.stage_for_process(replay), "connection-machining")

    def test_circular_graph_reports_cycle_even_if_it_also_conflicts_with_stages(self):
        with self.assertRaisesRegex(ValueError, "cyclic assembly process dependency.*fold.*socket"):
            STAGES.plan_stages([process("fold", "bend", ["socket"]), process("socket", dependencies=["fold"])])

    def test_unknown_declared_stage_or_process_dependency_is_rejected(self):
        bad_cases = [({"stage_by_key": {"cut": "immediate"}}, "unknown assembly planning stage"),
                     ({"stage_by_key": {"missing": "validation"}}, "unknown processes"),
                     ({"template_stages": {"custom": 100}}, "unknown assembly planning stage")]
        for kwargs, error in bad_cases:
            with self.subTest(kwargs=kwargs), self.assertRaisesRegex(ValueError, error):
                STAGES.plan_stages([process("cut")], **kwargs)
        with self.assertRaisesRegex(ValueError, "depends on unknown process missing"):
            STAGES.plan_stages([process("cut", dependencies=["missing"])])
        with self.assertRaisesRegex(ValueError, "duplicate assembly stage process"):
            STAGES.plan_stages([process("cut"), process("cut")])

    def test_after_requires_a_dependency_and_a_writer_of_the_observed_material(self):
        observed = {"scope": "manufacturing", "itemKey": "mate", "state": {"after": "cut"}, "data": {}}
        for deps in ([], ["cut"]):
            with self.subTest(deps=deps), self.assertRaisesRegex(ValueError, "after state must depend on a writer"):
                STAGES.plan_stages([process("cut"), process("observe", dependencies=deps, parts={"mate": observed})])

    def test_external_providers_require_explicit_capabilities(self):
        custom = process("custom", "custom-provider")
        with self.assertRaisesRegex(ValueError, 'must declare its planning stage'):
            STAGES.stage_for_process(custom)
        plan = STAGES.plan_stages([custom, process("cut")], template_stages={"custom-provider": "validation"})
        self.assertEqual(plan["orderedKeys"], ["cut", "custom"])
        self.assertEqual(STAGES.plan_stages([custom], stage_by_key={'custom': 'validation'})['orderedKeys'], ['custom'])

    def test_all_current_local_providers_have_explicit_stage_capabilities(self):
        for descriptor in (SHARED.parent / 'assembly').glob('*/assembly.json'):
            self.assertIn(json.loads(descriptor.read_text('utf-8'))['id'], STAGES.TEMPLATE_STAGES)
        for name in ("assembly_tube_machining", "assembly_surface_machining", "assembly_structural_machining"):
            tree = ast.parse((SHARED / (name + ".py")).read_text(encoding="utf-8"))
            registered = next(node.value for node in tree.body if isinstance(node, ast.Assign)
                              and any(isinstance(target, ast.Name) and target.id == "FUNCTIONS" for target in node.targets))
            for key in registered.keys:
                self.assertIn(ast.literal_eval(key), STAGES.TEMPLATE_STAGES, name)

    def test_independent_output_plans_merge_without_losing_stage_records(self):
        left = STAGES.plan_stages([process("a.fold", "bend"), process("a.socket")])
        right = STAGES.plan_stages([process("b.prepare", "profile-stock-preparation"), process("b.check", "tube-insertion-check")])
        merged = STAGES.merge_stage_plans(left, right)
        self.assertEqual(merged["orderedKeys"], ["b.prepare", "a.fold", "a.socket", "b.check"])
        self.assertEqual(STAGES.merge_stage_plans(left, left), left)
        conflict = STAGES.plan_stages([process("a.fold", "tube-insertion-check")])
        with self.assertRaisesRegex(ValueError, "conflicting assembly output stage"):
            STAGES.merge_stage_plans(left, conflict)
        left["materializedProcessKeys"] = ["a.fold", "a.socket.replay"]
        right["materializedProcessKeys"] = ["b.check"]
        self.assertEqual(STAGES.merge_stage_plans(left, right)["materializedProcessKeys"],
                         ["a.fold", "a.socket.replay", "b.check"])

    def test_independent_material_plans_merge_by_stock_identity_and_reject_competing_allocations(self):
        boundary = {"stockId": "a", "sourceItemKey": "a.member", "end": "start", "point": [0., 0., 0.]}
        left = {"schema": "icax.assembly-material-plan", "schemaVersion": 1,
                "stocks": [{"stockId": "a", "length": 100.}], "initialMaterialAllocations": [boundary]}
        right = {"schema": "icax.assembly-material-plan", "schemaVersion": 1,
                 "stocks": [{"stockId": "b", "length": 200.}], "initialMaterialAllocations": []}
        original = deepcopy(left)
        self.assertEqual(STAGES.merge_material_plans(left, left), left)
        self.assertEqual([stock["stockId"] for stock in STAGES.merge_material_plans(left, right)["stocks"]], ["a", "b"])
        conflict = deepcopy(left)
        conflict["stocks"][0]["length"] = 99.
        with self.assertRaisesRegex(ValueError, "conflicting assembly material plan for stock a"):
            STAGES.merge_material_plans(left, conflict)
        conflict = deepcopy(left)
        conflict["initialMaterialAllocations"][0]["point"][0] = 1.
        with self.assertRaisesRegex(ValueError, "conflicting assembly material boundary for stock a"):
            STAGES.merge_material_plans(left, conflict)
        self.assertEqual(left, original)

    def test_executor_uses_the_public_stage_scheduler_for_actual_dispatch_order(self):
        sys.path.insert(0, str(SRC / "iCAX-Engine/framework/TemplateRuntime/python"))
        executor_spec = importlib.util.spec_from_file_location("test_assembly_stage_executor", SHARED / "assembly_manufacturing_executor.py")
        executor = importlib.util.module_from_spec(executor_spec)
        executor_spec.loader.exec_module(executor)
        calls = [process("socket"), process("fold", "bend"), process("prepare", "profile-stock-preparation")]
        self.assertEqual([call["key"] for call in executor._ordered(calls)], ["prepare", "fold", "socket"])

    def test_material_graph_adds_shared_stock_prerequisites_without_ordering_bends(self):
        calls = [process("check", "tube-insertion-check"), process("socket", "tube-profile-aperture"),
                 process("fold.b", "bend"), process("fold.a", "node-v-notch-integrated"),
                 process("boundary", "tube-end-joint", geometry={"allocation": "initial-material-boundary"})]
        original = deepcopy(calls)
        linked = STAGES.with_material_dependencies(calls)
        deps = {call["key"]: call["definition"]["dependencies"] for call in linked}
        self.assertEqual(deps["fold.a"], ["boundary"])
        self.assertEqual(deps["fold.b"], ["boundary"])
        self.assertEqual(deps["socket"], ["boundary", "fold.a", "fold.b"])
        self.assertEqual(deps["check"], ["boundary", "fold.a", "fold.b", "socket"])
        self.assertEqual(deps["boundary"], [])
        self.assertEqual(calls, original)
        self.assertEqual(STAGES.with_material_dependencies(linked), linked)
        frozen = STAGES.plan_stages(linked)
        self.assertEqual(frozen["dependencies"], deps)
        frozen["dependencies"]["socket"].append("changed-audit-copy")
        self.assertEqual(linked[1]["definition"]["dependencies"], ["boundary", "fold.a", "fold.b"])

    def test_automatic_edges_are_stable_under_reversed_submission_and_preserve_manual_edges(self):
        calls = [process("socket", dependencies=["manual", "manual"]), process("manual"),
                 process("fold.z", "bend"), process("fold.a", "bend"),
                 process("boundary", "profile-stock-preparation")]
        forward = STAGES.with_material_dependencies(calls)
        reverse = STAGES.with_material_dependencies(list(reversed(calls)))
        graph = lambda rows: {row["key"]: row["definition"]["dependencies"] for row in rows}
        self.assertEqual(graph(forward), graph(reverse))
        self.assertEqual(graph(forward)["socket"], ["manual", "boundary", "fold.a", "fold.z"])
        self.assertEqual(calls[0]["definition"]["dependencies"], ["manual", "manual"])

    def test_mate_observations_do_not_publish_dependencies_for_a_different_written_material(self):
        frame_fold = process("frame.fold", "bend")
        frame_fold["definition"]["targets"] = {"stock": "frame"}
        column_boundary = process("column.boundary", "tube-end-joint",
                                  geometry={"allocation": "initial-material-boundary"})
        column_boundary["definition"]["targets"] = {"stock": "column"}
        socket = process("frame.socket", "tube-profile-aperture")
        socket["definition"]["targets"] = {"stock": "frame"}
        check = process("column.check", "tube-insertion-check")
        check["definition"]["targets"] = {"stock": "column"}
        calls = [check, socket, frame_fold, column_boundary]
        owners = {"column.boundary": ["column", "frame"], "frame.fold": ["frame"],
                  "frame.socket": ["frame", "column"], "column.check": ["column"]}
        linked = STAGES.with_material_dependencies(calls, owners)
        graph = {row["key"]: row["definition"]["dependencies"] for row in linked}
        self.assertEqual(graph["frame.fold"], [])
        self.assertEqual(graph["frame.socket"], ["column.boundary", "frame.fold"])
        self.assertEqual(graph["column.check"], ["column.boundary"])

    def test_fallback_material_reads_include_manufacturing_observers_and_interval_targets(self):
        fold = process("mate.fold", "bend")
        fold["definition"]["targets"] = {"stock": {"stockId": "mate", "start": 0., "reverse": False}}
        socket = process("socket", "tube-profile-aperture", parts={
            "stock": {"scope": "manufacturing", "itemKey": "stock", "state": "initial", "data": {}},
            "mate": {"scope": "manufacturing", "itemKey": "mate", "state": "initial", "data": {}}})
        isolated = process("unrelated.fold", "bend")
        isolated["definition"]["targets"] = {"stock": "unrelated"}
        linked = STAGES.with_material_dependencies([socket, isolated, fold])
        self.assertEqual(linked[0]["definition"]["dependencies"], ["mate.fold"])

    def test_material_dependencies_preserve_explicit_initial_and_after_snapshots(self):
        after = {"scope": "manufacturing", "itemKey": "stock", "state": {"after": "fold"}, "data": {}}
        calls = [process("socket.after", parts={"stock": after}), process("socket.initial"), process("fold", "bend")]
        linked = STAGES.with_material_dependencies(calls)
        self.assertEqual(linked[0]["definition"]["processInput"]["parts"]["stock"]["state"], {"after": "fold"})
        self.assertEqual(linked[1]["definition"]["processInput"]["parts"]["stock"]["state"], "initial")
        invalid_after = deepcopy(after)
        invalid_after["state"] = {"after": "same-stage"}
        with self.assertRaisesRegex(ValueError, "after state must depend on a writer"):
            STAGES.with_material_dependencies([process("socket", parts={"stock": invalid_after}), process("same-stage")])

    def test_host_capability_facets_and_output_only_providers_can_use_an_owner_map(self):
        boundary = process("boundary", "custom-sizing")
        del boundary["definition"]["targets"]
        fold = process("fold", "custom-fold")
        del fold["definition"]["targets"]
        socket = process("socket")
        stages = {"boundary": "material-requirements", "fold": "bend-unfolding"}
        owners = {"boundary": "tube", "fold": ["tube"], "socket": {"tube"}}
        linked = STAGES.with_material_dependencies([socket, fold, boundary], owners, stages)
        self.assertEqual(linked[0]["definition"]["dependencies"], ["boundary", "fold"])
        self.assertEqual(linked[1]["definition"]["dependencies"], ["boundary"])

    def test_invalid_material_maps_and_competing_manual_graphs_are_rejected(self):
        for bindings in ({"missing": ["stock"]}, {"socket": [""]}, {"socket": {"stockId": "stock"}}):
            with self.subTest(bindings=bindings), self.assertRaisesRegex(ValueError, "material.*bindings|bindings.*unknown"):
                STAGES.with_material_dependencies([process("socket")], bindings)
        with self.assertRaisesRegex(ValueError, "cyclic assembly process dependency|stage dependency conflict"):
            STAGES.with_material_dependencies([process("fold", "bend", ["socket"]), process("socket")])
        with self.assertRaisesRegex(ValueError, "unknown assembly planning stage"):
            STAGES.with_material_dependencies([process("socket")], stage_by_key={"socket": "first"})

    def test_merged_audit_graph_requires_and_preserves_complete_dependencies(self):
        fresh = STAGES.plan_stages([process("a.socket", dependencies=["a.fold"]), process("a.fold", "bend")])
        second = STAGES.plan_stages([process("b.socket")])
        merged = STAGES.merge_stage_plans(fresh, second)
        self.assertEqual(merged["dependencies"], {"a.socket": ["a.fold"], "a.fold": [], "b.socket": []})
        del second["dependencies"]
        with self.assertRaisesRegex(ValueError, 'invalid assembly stage plan dependency graph'):
            STAGES.merge_stage_plans(fresh, second)
        conflicting = deepcopy(fresh)
        conflicting["dependencies"]["a.socket"] = []
        with self.assertRaisesRegex(ValueError, "conflicting assembly output dependencies for process a.socket"):
            STAGES.merge_stage_plans(fresh, conflicting)


if __name__ == "__main__":
    unittest.main()
