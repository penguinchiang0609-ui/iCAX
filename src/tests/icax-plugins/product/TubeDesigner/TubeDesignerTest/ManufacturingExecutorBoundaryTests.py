"""Manufacturing ownership, frozen inputs and coordinates at the executor boundary."""
from copy import deepcopy
import hashlib
import importlib.util
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
from WindowCatalogueTests import package

SRC = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(SRC / "iCAX-Engine/framework/TemplateRuntime/python"))
from icax_template_sdk import NeutralModel, expand_resource_model, to_display_model
from icax_template_sdk.manufacturing import (
    IDENTITY_FRAME, manufacturing_declaration, to_manufacturing_model,
    validate_manufacturing_model, compose_manufacturing_model,
)

SHARED = SRC / "apps/tube-designer/templates/_shared"


def load_executor():
    path = SHARED / "assembly_manufacturing_executor.py"
    spec = importlib.util.spec_from_file_location("test_manufacturing_boundary_executor", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def pose(origin=(0, 0, 0), *, turned=False):
    return {"origin": list(origin), "xAxis": [0, 1, 0] if turned else [1, 0, 0],
            "yAxis": [-1, 0, 0] if turned else [0, 1, 0], "zAxis": [0, 0, 1]}


def shape_nodes(key):
    points = [[-2, -2], [2, -2], [2, 2], [-2, 2]]
    contour = {"kind": "path", "closed": True, "segments": [
        {"kind": "line", "start": a, "end": b}
        for a, b in zip(points, points[1:] + points[:1])]}
    return [
        {"key": key + ".profile", "operator": "profile2d", "inputs": [],
         "arguments": {"placement": {"origin": [0, 0, 0], "xAxis": [1, 0, 0],
                                     "yAxis": [0, 1, 0]}, "contours": [contour]}},
        {"key": key, "operator": "extrude", "inputs": [key + ".profile"],
         "arguments": {"vector": [0, 0, 10]}},
    ]


def document(items=None, resources=None):
    return {"schema": "icax.manufacturing-model", "schemaVersion": 2,
            "coordinateSystem": "right-handed-x-width-y-depth-z-height", "lengthUnit": "mm",
            "resources": resources if resources is not None else shape_nodes("stock.raw"),
            "items": items if items is not None else [
                {"key": "stock", "displayName": "Stock", "geometry": {"resource": "stock.raw"}}],
            "roots": [item["key"] for item in items] if items is not None else ["stock"],
            "sourceMappings": [], "processes": []}


def binding(item="stock", state="initial", *, data=None):
    return {"scope": "manufacturing", "itemKey": item, "state": deepcopy(state),
            "data": deepcopy(data if data is not None else {"intrinsicGeometryKey": item + ".raw"})}


def keep_region():
    return {"placement": {"origin": [0, 0, 0], "xAxis": [1, 0, 0], "yAxis": [0, 1, 0]},
            "polygon": [[-5, -5], [5, -5], [5, 5], [-5, 5]], "vector": [0, 0, 5]}


def process(key, *, item="stock", state="initial", dependencies=(), parts=None, geometry=None):
    return {"key": key, "kind": "assembly-process", "definition": {
        "templateId": "structural-stock-fit",
        "processInput": {"schema": "icax.assembly-process-input", "schemaVersion": 2,
                         "parts": parts if parts is not None else {"stock": binding(item, state)},
                         "geometry": geometry if geometry is not None else {"keepRegions": [keep_region()]}},
        "parameters": {}, "processDrafts": {}, "targets": {"stock": item},
        "dependencies": list(dependencies)}}


class ManufacturingExecutorBoundaryTests(unittest.TestCase):
    def setUp(self):
        self.executor = load_executor()

    def test_local_provider_rejects_unhandled_selected_resources_and_drafts(self):
        for field in ('resources', 'processDrafts'):
            with self.subTest(field=field):
                declaration = document()
                declaration['processes'] = [process('cut.selected')]
                definition = declaration['processes'][0]['definition']
                if field == 'resources':
                    definition['processInput']['resources'] = {'node-slot': {'ref': {'scope': 'user', 'id': 'selected'}}}
                else:
                    definition['processDrafts'] = {'node-slot': {'selected': {'clearance': 0.25}}}
                validate_manufacturing_model(declaration)
                original = deepcopy(declaration)
                runtime = self.executor._load('assembly_geometry_process_runtime')
                with patch.object(runtime, 'evaluate') as evaluate:
                    with self.assertRaisesRegex(ValueError, 'does not support selected resource slots or drafts'):
                        self.executor.execute_manufacturing(declaration, {})
                    evaluate.assert_not_called()
                self.assertEqual(declaration, original)

    def execute(self, declaration, context=None, design=None):
        """Spy on actual provider inputs; preserve real provider/executor behavior."""
        runtime = self.executor._load("assembly_geometry_process_runtime")
        evaluate = runtime.evaluate
        observed = []

        def record(function_id, process_input, parameters):
            observed.append(deepcopy(process_input))
            return evaluate(function_id, process_input, parameters)

        raw, environment = deepcopy(declaration), deepcopy(context or {})
        frozen_design = deepcopy(design)
        with patch.object(runtime, "evaluate", side_effect=record):
            result = self.executor.execute_manufacturing(declaration, context or {}, design_model=design)
        self.assertEqual(declaration, raw)
        self.assertEqual(context or {}, environment)
        self.assertEqual(design, frozen_design)
        return result, observed

    def shared_declaration(self, *, explicit_owner=True):
        runtime = self.executor._load("assembly_geometry_process_runtime")
        with manufacturing_declaration():
            model = NeutralModel(template_id="boundary", template_version="1", package_digest="", parameters={})
            for node in shape_nodes("shared"):
                model.geometry(node["key"], node["operator"], inputs=node["inputs"], arguments=node["arguments"])
            model.geometry("second.pose", "transform", inputs=["shared"],
                           arguments={"placement": pose((20, 0, 0))})
            for item in ("first", "second"):
                runtime.invoke(model, "shared", "structural-stock-fit",
                    {"schema": "icax.assembly-process-input", "schemaVersion": 1,
                     "parts": {"stock": {"intrinsicGeometryKey": "shared"}}, "geometry": {"keepRegions": []}},
                    {}, "cut." + item, item if explicit_owner else "shared")
            model.item("first", "First", representations={"result": "shared"})
            model.item("second", "Second", representations={"result": "second.pose"})
            model.output("result", "result", ["first", "second"])
            return to_manufacturing_model(model.build()), to_display_model(model.build())

    def test_shared_initial_resource_keeps_explicit_process_owners_and_instance_positions(self):
        declaration, design = self.shared_declaration()
        calls = {call["key"]: call["definition"] for call in declaration["processes"]}
        self.assertEqual(calls["cut.first"]["targets"], {"stock": "first"})
        self.assertEqual(calls["cut.second"]["targets"], {"stock": "second"})
        self.assertEqual(calls["cut.second"]["processInput"]["parts"]["stock"]["itemKey"], "second")
        result, _ = self.execute(declaration, design=design)
        items = {item["key"]: item for item in result["items"]}
        self.assertEqual(items["first"]["representations"]["result"].get("placement", IDENTITY_FRAME), pose())
        self.assertEqual(items["second"]["representations"]["result"]["placement"], pose((20, 0, 0)))
        records = result["extensions"]["tubeDesigner.assemblyGeometryProcesses"]["instances"]
        self.assertEqual({record["stockId"] for record in records}, {"first", "second"})

    def test_shared_initial_resource_without_explicit_owner_rejects_ambiguous_ancestry(self):
        with self.assertRaisesRegex(ValueError, "ambiguous|unique manufacturing owner"):
            self.shared_declaration(explicit_owner=False)

    def test_component_reference_uses_frozen_geometry_and_placement_without_mutating_inputs(self):
        # The Python boundary transports BRep bytes opaquely; native CAD tests
        # separately verify BRep parsing. This fixture detects snapshot replacement.
        brep = "opaque host-frozen BRep fixture\n"
        snapshot = {"brep": brep, "geometryDigest": hashlib.sha256(brep.encode()).hexdigest(),
                    "sourcing": "purchased", "material": "fixture steel"}
        placement = pose((12, 34, 56), turned=True)
        declaration = document(items=[{"key": "accessory", "displayName": "Accessory",
            "componentReference": {"ref": {"scope": "user", "id": "fixture"}, "placement": placement}}],
            resources=[])
        context = {"componentSnapshots": {"user:fixture": snapshot}}
        result, observed = self.execute(declaration, context)
        self.assertEqual(observed, [])
        representation = result["items"][0]["representations"]["result"]
        node = next(node for node in result["resources"] if node["key"] == representation["resource"])
        self.assertEqual(node["arguments"]["brep"], brep)
        self.assertEqual(node["arguments"]["geometryDigest"], snapshot["geometryDigest"])
        self.assertEqual(representation["placement"], placement)
        self.assertEqual(result["items"][0]["properties"]["manufacturing.modelReference"], "user:fixture")
        with self.assertRaisesRegex(ValueError, "host-frozen snapshot"):
            self.executor.execute_manufacturing(declaration, {})

    def test_known_initial_geometry_preserves_external_display_source_mapping(self):
        declaration = document()
        sources = [{"itemKey": "display.member"}]
        declaration["sourceMappings"] = [{"itemKey": "stock", "sources": sources}]
        result, _ = self.execute(declaration)
        self.assertEqual(result["items"][0]["properties"]["manufacturing.sourceMembers"], sources)
        self.assertEqual(result["extensions"]["tubeDesigner.productStockMappingVersion"], 1)
        self.assertEqual(declaration["sourceMappings"], [{"itemKey": "stock", "sources": sources}])

    def test_resource_observer_is_placed_in_the_target_material_frame(self):
        stock_pose = pose((10, 20, 30), turned=True)
        observer_pose = pose((10, 25, 30), turned=True)
        declaration = document(items=[{"key": "stock", "displayName": "Stock",
            "geometry": {"resource": "stock.raw", "placement": stock_pose}}],
            resources=shape_nodes("stock.raw") + shape_nodes("observer.raw"))
        stock = binding(data={"intrinsicGeometryKey": "stock.raw",
                              "initialGeometry": {"resource": "stock.raw", "itemPlacement": stock_pose}})
        declaration["processes"] = [process("observe", parts={"stock": stock,
            "mate": {"scope": "resource", "resourceKey": "observer.raw", "placement": observer_pose}},
            geometry={"keepRegions": []})]
        result, observed = self.execute(declaration)
        key = observed[0]["parts"]["mate"]["intrinsicGeometryKey"]
        nodes = {node["key"]: node for node in expand_resource_model(result)["geometry"]}
        self.assertEqual(nodes[key]["operator"], "transform")
        self.assertEqual(nodes[key]["inputs"], ["observer.raw"])
        self.assertEqual(nodes[key]["arguments"]["placement"], pose((5, 0, 0)))

    def test_manufacturing_observers_keep_their_own_initial_and_after_material_identity(self):
        declaration = document(items=[
            {"key": "stock", "displayName": "Stock", "geometry": {"resource": "stock.raw"}},
            {"key": "mate", "displayName": "Mate", "geometry": {"resource": "mate.raw"}}],
            resources=shape_nodes("stock.raw") + shape_nodes("mate.raw"))
        declaration["processes"] = [
            process("mate.cut", item="mate"),
            process("stock.observe", dependencies=["mate.cut"],
                parts={"stock": binding(), "mateInitial": binding("mate"),
                       "mateAfter": binding("mate", {"after": "mate.cut"})},
                geometry={"keepRegions": []}),
        ]
        result, observed = self.execute(declaration)
        first = result["extensions"]["tubeDesigner.assemblyGeometryProcesses"]["instances"][0]
        parts = observed[1]["parts"]
        self.assertEqual(parts["stock"]["intrinsicGeometryKey"], "stock.raw")
        self.assertEqual(parts["mateInitial"]["intrinsicGeometryKey"], "mate.raw")
        self.assertEqual(parts["mateAfter"]["intrinsicGeometryKey"], first["resultGeometry"])
        self.assertNotEqual(parts["stock"]["intrinsicGeometryKey"], parts["mateAfter"]["intrinsicGeometryKey"])

    def test_initial_and_after_states_are_distinct_inputs_to_real_tool_evaluation(self):
        declaration = document()
        declaration["processes"] = [
            process("cut.first"),
            process("cut.initial"),
            process("cut.after", state={"after": "cut.first"}, dependencies=["cut.first"]),
        ]
        result, observed = self.execute(declaration)
        records = result["extensions"]["tubeDesigner.assemblyGeometryProcesses"]["instances"]
        self.assertEqual(observed[0]["parts"]["stock"]["intrinsicGeometryKey"], "stock.raw")
        self.assertEqual(observed[1]["parts"]["stock"]["intrinsicGeometryKey"], "stock.raw")
        self.assertEqual(observed[2]["parts"]["stock"]["intrinsicGeometryKey"], records[0]["resultGeometry"])
        # Final part CSG may accumulate independently generated cutters. State
        # assertions above concern the material seen by the actual provider.
        bad = deepcopy(declaration)
        bad["processes"][2]["definition"]["dependencies"] = []
        with self.assertRaisesRegex(ValueError, "after state"):
            validate_manufacturing_model(bad)

    def test_generated_parts_merge_intrinsic_and_frozen_components_without_changing_hierarchy(self):
        _, defaults, template = package("single_face_security_window")
        values = dict(defaults, accessDoorEnabled=False, frameManufacturingMode="plane_v_notch",
                      infillPattern="horizontal", firstHorizontalTopOffset=defaults["height"] / 2,
                      lastHorizontalBottomOffset=defaults["height"] / 2)
        input_values = deepcopy(values)
        raw = template.manufacturing(values)
        design = template.display(values)
        declaration = compose_manufacturing_model(raw, design)
        self.assertEqual(values, input_values)
        design_keys = [item["key"] for item in declaration["items"]]
        self.assertTrue(design_keys)
        self.assertTrue(all("designInput" in item for item in declaration["items"]))
        declaration["resources"].extend(shape_nodes("known.box.raw"))
        brep = "opaque mixed-assembly host-frozen BRep fixture\n"
        declaration["items"].extend([
            {"key": "known.box", "displayName": "Intrinsic box",
             "geometry": {"resource": "known.box.raw", "placement": pose((70, 80, 90))}},
            {"key": "fixture.component", "displayName": "Frozen component",
             "componentReference": {"ref": {"scope": "user", "id": "mixed"},
                                    "placement": pose((100, 200, 300), turned=True)}},
            {"key": "window.group", "displayName": "Window design subgroup", "children": design_keys},
            {"key": "assembly.group", "displayName": "Mixed assembly",
             "children": ["window.group", "known.box", "fixture.component"]},
        ])
        declaration["roots"] = ["assembly.group"]
        context = {"componentSnapshots": {"user:mixed": {"brep": brep,
            "geometryDigest": hashlib.sha256(brep.encode()).hexdigest(), "sourcing": "purchased"}}}
        validate_manufacturing_model(declaration)
        result, _ = self.execute(declaration, context)
        items = {item["key"]: item for item in result["items"]}
        generated = result['extensions']['tubeDesigner.assemblyOutputSets'][0]
        part_keys = generated['itemKeys']
        self.assertEqual(set(generated['sourceItemKeys']), set(design_keys))
        self.assertNotEqual(part_keys, design_keys)
        self.assertEqual(set(items), set(part_keys) | {'window.group', 'assembly.group', 'known.box', 'fixture.component'})
        self.assertEqual(items["window.group"]["children"], part_keys)
        self.assertEqual(items["assembly.group"]["children"], ["window.group", "known.box", "fixture.component"])
        self.assertEqual(items["window.group"]["representations"], {})
        self.assertEqual(items["assembly.group"]["representations"], {})
        expected_leaves = part_keys + ["known.box", "fixture.component"]
        self.assertEqual(result["outputs"][0]["items"], expected_leaves)
        resources = {node["key"]: node for node in result["resources"]}
        for key in expected_leaves:
            self.assertIn(items[key]["representations"]["result"]["resource"], resources)
        self.assertEqual(items["known.box"]["representations"]["result"]["placement"], pose((70, 80, 90)))
        component = items["fixture.component"]["representations"]["result"]
        self.assertEqual(component["placement"], pose((100, 200, 300), turned=True))
        self.assertEqual(resources[component["resource"]]["arguments"]["brep"], brep)
        self.assertTrue(result["extensions"]["tubeDesigner.assemblyProcessSource"]["instances"])
        self.assertEqual(result["extensions"]["tubeDesigner.manufacturingPartCount"], len(expected_leaves))


if __name__ == "__main__":
    unittest.main()
