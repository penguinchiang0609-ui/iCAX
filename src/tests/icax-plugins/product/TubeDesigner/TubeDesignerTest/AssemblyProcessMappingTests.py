"""Replay separate connections on shared stock and independent material sites."""
import copy
from pathlib import Path
import unittest
from unittest.mock import patch

from AssemblyProcessReuseTests import runtime


def local_input(template_id):
    descriptor = runtime._template_by_id(template_id)
    product = runtime.get_example_product(template_id)["finishedProduct"]
    return runtime.process_contract.from_product(
        descriptor, product, runtime.finished_product_plan(product), example_selection=True)


def raw_stock(part, stock_id, length=None):
    return {"id": stock_id, **{key: copy.deepcopy(part[key])
            for key in ("profileRef", "parameters", "length", "matrix")},
            **({"length": length} if length is not None else {})}


def operation(instance_id, template_id, local, targets, parameters=None):
    return {"instanceId": instance_id, "templateId": template_id,
            "processInput": local, "targets": targets, "parameters": parameters or {}}


class AssemblyProcessMappingTests(unittest.TestCase):
    def test_geometry_plan_identity_tracks_resolver_contract_and_execution_dependencies(self):
        from AssemblyTubeMachiningTests import part, branch_at, input_of
        stock = part()
        stock["parameters"] = {"width": 38, "depth": 38, "wallThickness": 1.6,
                               "cornerRadius": 2, "innerRadius": .4}
        stocks = [raw_stock(stock, "stock")]
        local = input_of(copy.deepcopy(stock), {}, branch=branch_at(250))
        instance = operation("aperture", "tube-profile-aperture", local,
                             {"stock": "stock"}, {"clearance": .2})
        instances = [instance, copy.deepcopy(instance)]
        saved_input = copy.deepcopy((stocks, instances))
        original = runtime.resolve_process_plan(stocks, instances)
        saved_snapshot = copy.deepcopy(original)
        self.assertEqual(len(original["instances"]), 1)
        self.assertEqual(original["instances"][0]["parameters"], instance["parameters"])
        self.assertEqual(original, runtime.resolve_process_plan(stocks, [instance]))
        read_bytes = Path.read_bytes
        for name in ("assembly_process_functions.py", "assembly_process_contract.py",
                     "assembly_template_runtime.py", "assembly_applicability_geometry.py",
                     "assembly_geometry_process_runtime.py"):
            with self.subTest(dependency=name):
                source = Path(runtime.__file__).with_name(name).resolve()
                def changed_source(path):
                    content = read_bytes(path)
                    return content+b"\n# changed resolver protocol dependency\n" if path.resolve() == source else content
                with patch.object(Path, "read_bytes", changed_source):
                    changed = runtime.resolve_process_plan(stocks, instances)
                    self.assertEqual(changed, runtime.resolve_process_plan(stocks, instances))
                self.assertNotEqual(original["planDigest"], changed["planDigest"])
                self.assertEqual(original["manufacturingParts"], changed["manufacturingParts"])
                if name != "assembly_geometry_process_runtime.py":
                    self.assertEqual(original["instances"], changed["instances"],
                                     "Resolver identity must invalidate plans even when the function digest is unchanged")
                self.assertEqual((stocks, instances), saved_input)
                self.assertEqual(original, saved_snapshot, "Resolving a new plan must not rewrite a saved snapshot")
        self.assertEqual(original, runtime.resolve_process_plan(stocks, instances))

    def test_neutral_process_requires_the_actual_whole_stock_length_and_frame(self):
        from AssemblyTubeMachiningTests import part, branch_at, input_of
        stock = part()
        stock["parameters"] = {"width": 38, "depth": 38, "wallThickness": 1.6,
                               "cornerRadius": 2, "innerRadius": .4}
        raw = raw_stock(stock, "stock")
        local = input_of(copy.deepcopy(stock), {}, branch=branch_at(250))
        local["parts"]["stock"]["matrix"][3] = 100
        instance = operation("aperture", "tube-profile-aperture", local, {"stock": "stock"}, {"clearance": .2})
        before = copy.deepcopy((raw, instance))
        with self.assertRaisesRegex(ValueError, "原材坐标"):
            runtime.resolve_process_plan([raw], [instance])
        self.assertEqual((raw, instance), before)
        local["parts"]["stock"]["matrix"][3] = 0
        local["parts"]["stock"]["length"] = 500
        with self.assertRaisesRegex(ValueError, "完整实际母材"):
            runtime.resolve_process_plan([raw], [instance])
        local["parts"]["stock"]["length"] = 600
        plan = runtime.resolve_process_plan([raw], [instance])
        tool = plan["manufacturingParts"][0]["request"]["neutralOperations"][0]["geometry"][0]
        self.assertEqual(tool["arguments"]["placement"]["origin"][0], 250)

    def test_repeated_fasteners_keep_two_actual_stocks_and_distinct_hole_sites(self):
        local = local_input("mechanical-fastener")
        stocks = [raw_stock(part, role) for role, part in local["parts"].items()]
        targets = {role: role for role in local["parts"]}
        left, right = copy.deepcopy(local), copy.deepcopy(local)
        for part in left["parts"].values():
            part["anchor"]["station"] -= 80
        for part in right["parts"].values():
            part["anchor"]["station"] += 80
        instances = [operation("left", "mechanical-fastener", left, targets),
                     operation("right", "mechanical-fastener", right, targets)]
        original = copy.deepcopy((stocks, instances))
        plan = runtime.resolve_process_plan(stocks, instances)
        self.assertEqual(original, (stocks, instances))
        self.assertEqual(2, len(plan["manufacturingParts"]))
        for part in plan["manufacturingParts"]:
            features = part["request"]["features"]
            self.assertEqual([115, 275], [feature["station"] for feature in features])
            self.assertEqual([2, 2], [feature["arrayCount"] for feature in features])
            self.assertEqual([[115, 165], [275, 325]],
                             [[feature["station"]+index*feature["arrayPitch"] for index in range(2)]
                              for feature in features])
            self.assertEqual(2, len(set(feature["id"] for feature in features)))

    def two_end_plan(self, length):
        first = local_input("tab-slot-lock")
        second = copy.deepcopy(first)
        second["parts"]["tab"]["anchor"]["end"] = "start"
        second["parts"]["slot"]["anchor"]["end"] = "end"
        tab, slot = first["parts"]["tab"], first["parts"]["slot"]
        stocks = [raw_stock(tab, "shared", length), raw_stock(slot, "mate-left"),
                  raw_stock(slot, "mate-right")]
        instances = [operation("joint-end", "tab-slot-lock", first,
                               {"tab": "shared", "slot": "mate-right"}),
                     operation("joint-start", "tab-slot-lock", second,
                               {"tab": "shared", "slot": "mate-left"})]
        return runtime.resolve_process_plan(stocks, instances)

    def test_two_connections_apply_to_opposite_ends_of_one_allocated_stock(self):
        # Each male end adds 30 mm to the same 280 mm desired material span.
        plan = self.two_end_plan(340)
        self.assertEqual(3, len(plan["manufacturingParts"]))
        shared = next(part for part in plan["manufacturingParts"] if part["stockId"] == "shared")
        self.assertEqual(340, shared["request"]["length"])
        self.assertEqual("male", shared["request"]["ends"]["start"]["toolParameters"]["gender"])
        self.assertEqual("male", shared["request"]["ends"]["end"]["toolParameters"]["gender"])
        self.assertEqual(2, len(shared["operationIds"]))

    def test_both_end_allowances_cannot_reuse_the_same_material_margin(self):
        with self.assertRaisesRegex(ValueError, "两端加工余量合计不足"):
            self.two_end_plan(310)

    def test_native_profile_dependency_changes_invalidate_the_saved_plan_digest(self):
        local = local_input("mechanical-fastener")
        stocks = [raw_stock(part, role) for role, part in local["parts"].items()]
        instances = [operation("joint", "mechanical-fastener", local,
                               {role: role for role in local["parts"]})]
        first = runtime.resolve_process_plan(stocks, instances, {"resourceRevision": "1"})
        changed = runtime.resolve_process_plan(stocks, instances, {"resourceRevision": "2"})
        self.assertNotEqual(first["planDigest"], changed["planDigest"])
        self.assertEqual(first["instances"], changed["instances"])
        self.assertEqual(first["manufacturingParts"], changed["manufacturingParts"])


if __name__ == "__main__":
    unittest.main()
