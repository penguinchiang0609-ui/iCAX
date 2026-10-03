"""Atomic replay and placement of reusable local manufacturing functions."""
from copy import deepcopy
import unittest
from unittest.mock import patch

from AssemblySurfaceMachiningTests import load
from icax_template_sdk import NeutralModel


class AssemblyGeometryProcessRuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime = load("assembly_geometry_process_runtime")

    def model(self):
        model = NeutralModel(template_id="independent-stock", template_version="1",
                             package_digest="", parameters={})
        return model, model.geometry("raw", "box", arguments={"size": [100, 20, 20]})

    def result(self, matrix=None):
        return {"schema": self.runtime.SCHEMA, "schemaVersion": 1,
                "templateId": "local-test", "functionDigest": "immutable-test-function",
                "applicable": True, "parameters": {}, "geometrySpace": "stock",
                "processInput": {"parts": {"stock": {"matrix": matrix or [
                    0, -1, 0, 10, 1, 0, 0, 20, 0, 0, 1, 30, 0, 0, 0, 1]}}},
                "geometry": [{"key": "tool", "operator": "box", "arguments": {"size": [2, 3, 4]}}],
                "operations": [{"id": "cut", "kind": "neutral-csg", "role": "stock",
                                "operation": "subtract", "tool": "tool"}]}

    def test_rotated_stock_composes_local_site_in_actual_world_frame(self):
        model, raw = self.model()
        result = self.result()
        result["operations"][0]["matrix"] = [1, 0, 0, 5, 0, 1, 0, 7, 0, 0, 1, 9, 0, 0, 0, 1]
        self.runtime.apply(model, raw, result, "site.1", "stock.1")
        transform = next(node for node in model.build()["geometry"] if node["operator"] == "transform")
        placement = transform["arguments"]["placement"]
        self.assertEqual(placement["origin"], [3, 25, 39])
        self.assertEqual(placement["xAxis"], [0, 1, 0])
        self.assertEqual(placement["yAxis"], [-1, 0, 0])
        self.assertEqual(placement["zAxis"], [0, 0, 1])
        self.assertNotIn("matrix", transform["arguments"])

    def test_half_applied_graph_failure_rolls_back_nodes_keys_and_records(self):
        model, raw = self.model()
        before = deepcopy(model._document)
        keys = set(model._geometry_keys)
        emit = model.geometry
        def fail_on_boolean(key, operator, *args, **kwargs):
            if operator == "boolean":
                raise ValueError("injected native-graph failure")
            return emit(key, operator, *args, **kwargs)
        with patch.object(model, "geometry", side_effect=fail_on_boolean):
            with self.assertRaisesRegex(ValueError, "injected"):
                self.runtime.apply(model, raw, self.result(), "site.1", "stock.1")
        self.assertEqual(model._document, before)
        self.assertEqual(model._geometry_keys, keys)
        self.assertFalse(getattr(model, '_assembly_process_tool_prototypes', {}))
        finished = self.runtime.apply(model, raw, self.result(), "site.1", "stock.1")
        self.assertIn(finished, model._geometry_keys)
        self.assertEqual(len(model.build()["extensions"][self.runtime.EXTENSION]["instances"]), 1)

    def test_exact_local_tool_graph_is_shared_across_distinct_stocks_and_poses(self):
        model, raw = self.model()
        other_raw = model.geometry('other.raw', 'box', arguments={'size':[120,20,20]})
        first_input=self.result()
        first=self.runtime.apply(model,raw,first_input,'stock.1.site','stock.1')
        other_input=self.result([1,0,0,80,0,0,-1,90,0,1,0,100,0,0,0,1])
        second=self.runtime.apply(model,other_raw,other_input,'stock.2.site','stock.2')
        nodes=model.build()['geometry']
        self.assertEqual(1,sum(n['operator']=='box' and n['arguments']['size']==[2,3,4] for n in nodes))
        transforms=[n for n in nodes if n['operator']=='transform']
        self.assertEqual(2,len(transforms))
        self.assertEqual(transforms[0]['inputs'],transforms[1]['inputs'])
        self.assertEqual([10,20,30],transforms[0]['arguments']['placement']['origin'])
        self.assertEqual([80,90,100],transforms[1]['arguments']['placement']['origin'])
        self.assertNotEqual(transforms[0]['arguments']['placement']['yAxis'],transforms[1]['arguments']['placement']['yAxis'])
        operations={n['key']:n for n in nodes if n['operator']=='boolean'}
        self.assertEqual(raw,operations[first]['arguments']['target'])
        self.assertEqual(other_raw,operations[second]['arguments']['target'])
        self.assertNotEqual(first,second)
        records=model.build()['extensions'][self.runtime.EXTENSION]['instances']
        self.assertEqual([first_input['processInput'],other_input['processInput']],[r['processInput'] for r in records])
        before=deepcopy(model.build())
        self.assertEqual(second,self.runtime.apply(model,other_raw,other_input,'stock.2.site','stock.2'))
        self.assertEqual(before,model.build())

    def test_changed_local_graph_or_function_never_reuses_tools(self):
        model,raw=self.model()
        self.runtime.apply(model,raw,self.result(),'site.1','stock.1')
        changed=self.result()
        changed['geometry'][0]['arguments']['size']=[2,3,5]
        self.runtime.apply(model,raw,changed,'site.2','stock.2')
        changed=self.result()
        changed['functionDigest']='different-function'
        self.runtime.apply(model,raw,changed,'site.3','stock.3')
        tools=[n for n in model.build()['geometry'] if n['operator']=='box' and n['key']!='raw']
        self.assertEqual(3,len(tools))

    def test_stock_witness_moves_with_target_and_ignores_tool_site_offset(self):
        model, raw = self.model()
        result = self.result()
        result["operations"][0]["matrix"] = [1, 0, 0, 5, 0, 1, 0, 7, 0, 0, 1, 9, 0, 0, 0, 1]
        result["operations"][0]["arguments"] = {"keepConnectedTo": [10, 2, 3]}
        root = self.runtime.apply(model, raw, result, "site.1", "stock.1")
        operation = next(node for node in model.build()["geometry"] if node["key"] == root)
        self.assertEqual(operation["arguments"]["keepConnectedTo"], [8, 30, 33])

    def test_namespaced_plan_keeps_retry_identity_and_external_observers(self):
        model, raw = self.model()
        observer = model.geometry("actual-accessory", "box", arguments={"size": [3, 4, 5]})
        result = self.result()
        result["externalGeometry"] = [observer]
        result["operations"][0]["tool"] = observer
        self.runtime.apply(model, raw, result, "site.1", "stock.1")
        source = model.build()
        original = deepcopy(source["extensions"][self.runtime.EXTENSION])
        mapping = {node["key"]: "manufacturing." + node["key"] for node in source["geometry"]}
        combined = NeutralModel(template_id="combined", template_version="1", package_digest="", parameters={})
        for node in source["geometry"]:
            combined.geometry(mapping[node["key"]], node["operator"],
                              inputs=self.runtime._rewrite(node.get("inputs", []), mapping),
                              arguments=self.runtime._rewrite(node.get("arguments", {}), mapping))
        extension = self.runtime.remap_extension(original, mapping)
        combined._document.setdefault("extensions", {})[self.runtime.EXTENSION] = extension
        record = extension["instances"][0]
        before = deepcopy(combined.build())
        self.assertEqual(self.runtime.apply(combined, record["targetGeometry"], record["result"],
                                           "site.1", "stock.1"), record["resultGeometry"])
        self.assertEqual(combined.build(), before)
        self.assertEqual(original, source["extensions"][self.runtime.EXTENSION])
        self.assertEqual(record["sourceIdentity"], original["instances"][0]["identity"])
        self.assertNotEqual(record["identity"], record["sourceIdentity"])

    def test_same_local_names_at_distinct_sites_are_isolated_and_chained(self):
        model, raw = self.model()
        first = self.runtime.apply(model, raw, self.result(), "site.1", "stock.1")
        other = self.result([1, 0, 0, 30, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1])
        second = self.runtime.apply(model, first, other, "site.2", "stock.1")
        nodes = model.build()["geometry"]
        self.assertEqual(len(nodes), len({node["key"] for node in nodes}))
        operation = next(node for node in nodes if node["key"] == second)
        self.assertEqual(operation["arguments"]["target"], first)
        count = len(nodes)
        self.assertEqual(self.runtime.apply(model, first, other, "site.2", "stock.1"), second)
        self.assertEqual(len(model.build()["geometry"]), count)
        before = deepcopy(model._document)
        with self.assertRaisesRegex(ValueError, "同一工艺实例"):
            self.runtime.apply(model, first, other, "site.2", "stock.other")
        self.assertEqual(model._document, before)

    def test_missing_intrinsic_observer_and_invalid_placement_are_atomic(self):
        for invalid in ("observer", "scale", "reflection"):
            model, raw = self.model()
            result = self.result()
            if invalid == "observer":
                result["externalGeometry"] = ["missing-accessory"]
            elif invalid == "scale":
                result["processInput"]["parts"]["stock"]["matrix"][0] = 2
            else:
                result["processInput"]["parts"]["stock"]["matrix"][10] = -1
            before = deepcopy(model._document)
            with self.assertRaises(ValueError):
                self.runtime.apply(model, raw, result, "site.1", "stock.1")
            self.assertEqual(model._document, before, invalid)

    def test_dag_forward_reference_and_nonfinite_inputs_are_rejected(self):
        result = self.result()
        result["geometry"][0]["inputs"] = ["future"]
        with self.assertRaisesRegex(ValueError, "已声明"):
            self.runtime.validate_result(result)
        local = {"schema": "icax.assembly-process-input", "schemaVersion": 1,
                 "parts": {"stock": {"thickness": float("nan")}}, "geometry": {}}
        outcome = self.runtime.evaluate("surface-feature-cut", local, {})
        self.assertFalse(outcome["applicable"])
        self.assertEqual(outcome["geometry"], [])
        self.assertEqual(outcome["operations"], [])

    def tool_plan(self):
        return {"schema": "icax.assembly-process-plan", "planDigest": "resolved-resource-plan",
                "instances": [{"templateId": "independent-fold"}], "operations": [
                    {"requestFeature": {"toolRef": {"scope": "system", "id": "actual-tool"},
                                        "toolParameters": {"leaveBottom": 1}, "station": station},
                     "placement": {"frame": {"origin": [station, 0, 0],
                         "xAxis": [1, 0, 0], "yAxis": [0, 0, 1], "zAxis": [0, -1, 0]}}}
                    for station in (25, 75)]}

    def actual_section(self):
        return {"contours": [
            {"kind": "polygon", "role": "outer", "points": [[-10,-10],[10,-10],[10,10],[-10,10]]},
            {"kind": "polygon", "role": "inner", "points": [[-8,-8],[-8,8],[8,8],[8,-8]]}]}

    def resource_snapshot(self):
        return {"geometry": {"outputKey": "tool", "model": {"geometry": [
            {"key": "tool", "operator": "box", "arguments": {"size": [2, 3, 4]}}]}}}

    def test_resolved_resource_plan_materializes_actual_placements_without_changing_inputs(self):
        model, raw = self.model()
        plan, section = self.tool_plan(), self.actual_section()
        before = deepcopy((plan, section))
        punch = self.runtime._resource_runtime("punch_tool_runtime")
        with patch.object(punch, "_evaluate", return_value=self.resource_snapshot()) as evaluate:
            cutters = self.runtime.materialize_resolved_tool_plan(model, plan, section, 100, "allocated.tools")
        self.assertEqual(before, (plan, section))
        self.assertEqual(2, evaluate.call_count)
        for call, station in zip(evaluate.call_args_list, (25,75)):
            self.assertEqual(call.args[2]["feature"]["station"], station)
            self.assertEqual(call.args[2]["targetSection"], section)
            self.assertEqual(call.args[2]["bounds"], {"min": [0,-10,-10], "max": [100,10,10]})
        nodes = {node["key"]:node for node in model.build()["geometry"]}
        for cutter, operation in zip(cutters, plan["operations"]):
            self.assertEqual(nodes[cutter]["arguments"]["placement"], operation["placement"]["frame"])
            self.assertEqual("box", nodes[nodes[cutter]["inputs"][0]]["operator"])
        root = self.runtime.apply_frozen_cutters(model, raw, cutters, "allocated.folds", "actual.stock", plan)
        final_nodes = {node["key"]:node for node in model.build()["geometry"]}
        record = model.build()["extensions"][self.runtime.EXTENSION]["instances"][0]
        self.assertEqual(plan, record["result"]["resolvedPlan"])
        self.assertEqual(cutters, final_nodes[root]["arguments"]["tools"])
        self.assertEqual(before, (plan, section))

    def test_resource_and_graph_failures_leave_no_partial_materialized_cutters(self):
        punch = self.runtime._resource_runtime("punch_tool_runtime")
        for failure in ("resource", "graph"):
            with self.subTest(failure=failure):
                model, _ = self.model()
                before, keys = deepcopy(model._document), set(model._geometry_keys)
                emit = model.geometry
                def fail_on_second_site(key, operator, *args, **kwargs):
                    if key.endswith(".0002.mould.placed"):
                        raise ValueError("injected graph failure")
                    return emit(key, operator, *args, **kwargs)
                snapshots = ([self.resource_snapshot(), ValueError("injected resource failure")]
                             if failure == "resource" else [self.resource_snapshot(), self.resource_snapshot()])
                with patch.object(punch, "_evaluate", side_effect=snapshots), \
                     patch.object(model, "geometry", side_effect=fail_on_second_site):
                    with self.assertRaisesRegex(ValueError, "injected"):
                        self.runtime.materialize_resolved_tool_plan(
                            model, self.tool_plan(), self.actual_section(), 100, "allocated.tools")
                self.assertEqual(before, model._document)
                self.assertEqual(keys, model._geometry_keys)


if __name__ == "__main__":
    unittest.main()
