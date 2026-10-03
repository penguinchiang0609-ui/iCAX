"""Continuous corner receivers use physical walls and explicit node ends."""
from copy import deepcopy
import importlib.util
import math
from pathlib import Path
import unittest

from AssemblyProcessReuseTests import example_input, runtime
from AssemblyOrthogonalCornerTests import participants


TEMPLATES = Path(__file__).resolve().parents[5] / "apps/tube-designer/templates"


def load(path, name):
    specification = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


punch = load(TEMPLATES / "_shared/punch_tool_runtime.py", "continuous_corner_punch")


def profile(width, depth, wall):
    def rectangle(w, d, reverse=False):
        points = [[-w / 2, -d / 2], [w / 2, -d / 2], [w / 2, d / 2], [-w / 2, d / 2]]
        if reverse:
            points.reverse()
        return {"kind": "path", "closed": True, "segments": [
            {"kind": "line", "start": point, "end": points[(index + 1) % 4]}
            for index, point in enumerate(points)]}
    return {"contours": [rectangle(width, depth), rectangle(width - 2 * wall, depth - 2 * wall, True)]}


def turned_input(turn, end="start"):
    local = example_input("orthogonal-corner")
    for role, part in local["parts"].items():
        part["parameters"].update(width=16 if role == "memberC" else 40,
                                  depth=20 if role == "memberC" else 25,
                                  wallThickness=1.2, cornerRadius=0, innerRadius=0)
        if role != "memberC":
            for _ in range(turn):
                for row in range(3):
                    matrix = part["matrix"]
                    matrix[row * 4 + 1], matrix[row * 4 + 2] = matrix[row * 4 + 2], -matrix[row * 4 + 1]
    if end == "end":
        branch = local["parts"]["memberC"]
        matrix = branch["matrix"]
        for row in range(3):
            matrix[row * 4 + 3] += matrix[row * 4] * branch["length"]
            matrix[row * 4] *= -1
            matrix[row * 4 + 2] *= -1
        branch["anchor"]["end"] = "end"
    return local


def projected_axis(host, branch, column):
    return [sum(branch["matrix"][row * 4 + column] * host["matrix"][row * 4 + index]
                for row in range(3)) for index in range(3)]


class ContinuousCornerInsertionTests(unittest.TestCase):
    def evaluate(self, local, joint):
        values = {"abJoint": "continuous", "cJoint": joint,
                  "insertDepth": 8, "tabLength": 8, "tabWidth": 6}
        before = deepcopy(local)
        result = runtime.evaluate_process("orthogonal-corner", local, values)
        self.assertEqual(before, local)
        self.assertTrue(result["applicable"], result["reason"])
        self.assertEqual(runtime._validated_values(runtime._template_by_id("orthogonal-corner"), values),
                         result["parameters"])
        self.assertFalse(any(operation["processId"].startswith("ab-") for operation in result["operations"]))
        self.assertTrue(all(value["lengthAddition"] == 0 for value in result["materialRequirements"]))
        return result

    def test_all_four_receiving_walls_and_both_c_ends_use_the_true_node(self):
        faces = set()
        for turn in range(4):
            for end in ("start", "end"):
                for joint in ("insert", "tabs"):
                    with self.subTest(turn=turn, end=end, joint=joint):
                        local = turned_input(turn, end)
                        result = self.evaluate(local, joint)
                        receivers = [operation for operation in result["operations"] if "requestFeature" in operation]
                        self.assertEqual(["memberA", "memberB"], [operation["role"] for operation in receivers])
                        for operation in receivers:
                            part = local["parts"][operation["role"]]
                            feature = operation["requestFeature"]
                            self.assertEqual(part["anchor"]["end"], feature["reference"])
                            self.assertEqual(0, feature["station"])
                            faces.add(feature["face"])
                            away = projected_axis(part, local["parts"]["memberC"], 0)
                            away = [(-1 if end == "end" else 1) * value for value in away]
                            normal = {"right": [0, 1, 0], "left": [0, -1, 0],
                                      "top": [0, 0, 1], "bottom": [0, 0, -1]}[feature["face"]]
                            self.assertLess(math.dist(away, normal), 1e-6)
                        c_end = next(operation for operation in result["operations"] if "requestEnd" in operation)
                        self.assertEqual(end, c_end["end"])
                        self.assertAlmostEqual((12.5 if turn % 2 else 20) - 8, c_end["requestEnd"]["trim"])
        self.assertEqual({"left", "right", "top", "bottom"}, faces)

    def test_actual_insert_tool_preserves_rectangle_axes_and_cuts_the_selected_half(self):
        for turn in range(4):
            for end in ("start", "end"):
                with self.subTest(turn=turn, end=end):
                    local = turned_input(turn, end)
                    result = self.evaluate(local, "insert")
                    for operation in result["operations"]:
                        if "requestFeature" not in operation:
                            continue
                        host, branch = local["parts"][operation["role"]], local["parts"]["memberC"]
                        feature = deepcopy(operation["requestFeature"])
                        # The allocator maps a local receiving end into an
                        # internal site of its full continuous stock.
                        feature.update(reference="start", station=100, section={"profile": profile(16, 20, 1.2)})
                        prepared = punch.prepare({"bounds": {"min": [0, -20, -12.5], "max": [500, 20, 12.5]},
                                                  "targetSection": profile(40, 25, 1.2), "features": [feature]})
                        nodes = prepared["features"][0]["toolSnapshot"]["geometry"]["model"]["geometry"]
                        placement = nodes[0]["arguments"]["placement"]
                        self.assertEqual(100, placement["origin"][0])
                        expected_u, expected_v = projected_axis(host, branch, 1), projected_axis(host, branch, 2)
                        for actual, expected in ((placement["xAxis"], expected_u), (placement["yAxis"], expected_v)):
                            self.assertAlmostEqual(abs(sum(a * b for a, b in zip(actual, expected))), 1)
                        axis = nodes[1]["arguments"]["vector"]
                        normal = projected_axis(host, branch, 0)
                        normal = [(-1 if end == "end" else 1) * value for value in normal]
                        self.assertGreater(sum(a * b for a, b in zip(axis, normal)), 0)

    def test_actual_tab_socket_pair_follows_retained_c_walls_on_all_faces(self):
        for turn in range(4):
            for end in ("start", "end"):
                with self.subTest(turn=turn, end=end):
                    local = turned_input(turn, end)
                    result = self.evaluate(local, "tabs")
                    for operation in result["operations"]:
                        if "requestFeature" not in operation:
                            continue
                        host, branch = local["parts"][operation["role"]], local["parts"]["memberC"]
                        feature = deepcopy(operation["requestFeature"])
                        feature.update(reference="start", station=100, section={"profile": profile(16, 20, 1.2)})
                        prepared = punch.prepare({"bounds": {"min": [0, -20, -12.5], "max": [500, 20, 12.5]},
                                                  "targetSection": profile(40, 25, 1.2), "features": [feature]})
                        nodes = prepared["features"][0]["toolSnapshot"]["geometry"]["model"]["geometry"]
                        sockets = [node for node in nodes if node["operator"] == "profile2d"]
                        self.assertEqual(2, len(sockets))
                        z = projected_axis(host, branch, 2)
                        axis = 0 if abs(z[0]) > 1 - 1e-6 else (1 if feature["face"] in ("top", "bottom") else 2)
                        coordinates = sorted(node["arguments"]["placement"]["origin"][axis] - (100 if axis == 0 else 0)
                                             for node in sockets)
                        self.assertAlmostEqual(-9.4, coordinates[0])
                        self.assertAlmostEqual(9.4, coordinates[1])
                    c_end = deepcopy(next(operation for operation in result["operations"] if "requestEnd" in operation))
                    c_end["requestEnd"]["section"] = {"profile": profile(40, 25, 1.2)}
                    prepared = punch.prepare({"bounds": {"min": [0, -8, -10], "max": [220, 8, 10]},
                                              "targetSection": profile(16, 20, 1.2),
                                              "ends": {end: c_end["requestEnd"]}})
                    nodes = prepared["ends"][end]["toolSnapshot"]["geometry"]["model"]["geometry"]
                    self.assertEqual(2, len([node for node in nodes if node["key"].endswith("-profile") and node["key"].startswith("ear-")]))

    def test_real_bound_continuous_receiver_can_use_depth_wall_without_widening_other_modes(self):
        actual = participants()
        for part in actual[:2]:
            frame = part["section"]["sectionFrame"]
            frame["xAxisWorld"], frame["yAxisWorld"] = frame["yAxisWorld"], [-value for value in frame["xAxisWorld"]]
        original = deepcopy(actual)
        for joint in ("insert", "tabs"):
            plan = runtime.resolve_bound_plan("orthogonal-corner", {"abJoint": "continuous", "cJoint": joint,
                "insertDepth": 8, "tabLength": 8, "tabWidth": 6}, participants=actual, product_topology="orthogonal-corner")
            self.assertTrue(plan["supported"])
            c = next(part for part in plan["parts"] if part["role"] == "memberC")
            self.assertAlmostEqual(12, c["ends"]["start"]["trim"])
        with self.assertRaisesRegex(ValueError, "宽向"):
            runtime.resolve_bound_plan("orthogonal-corner", {"abJoint": "miter", "cJoint": "insert"},
                                       participants=actual, product_topology="orthogonal-corner")
        self.assertEqual(original, actual)

    def test_same_size_insert_and_tabs_and_noncoincident_nodes_remain_rejected(self):
        for joint in ("insert", "tabs"):
            local = turned_input(1)
            local["parts"]["memberC"]["parameters"].update(width=40, depth=25)
            result = runtime.evaluate_process("orthogonal-corner", local, {"abJoint": "continuous", "cJoint": joint})
            self.assertFalse(result["applicable"])
            self.assertIn("C 端口", result["reason"])
            local = turned_input(1)
            local["parts"]["memberC"]["matrix"][3] += 1
            result = runtime.evaluate_process("orthogonal-corner", local, {"abJoint": "continuous", "cJoint": joint})
            self.assertFalse(result["applicable"])
            self.assertIn("实际端点", result["reason"])


    def test_oblique_c_rectangle_is_rejected_in_local_and_real_bound_insertion(self):
        local = turned_input(1)
        matrix = local["parts"]["memberC"]["matrix"]
        cosine = math.sqrt(.5)
        for row in range(3):
            first, second = matrix[row * 4 + 1], matrix[row * 4 + 2]
            matrix[row * 4 + 1] = cosine * (first + second)
            matrix[row * 4 + 2] = cosine * (-first + second)
        original = deepcopy(local)
        for joint in ("insert", "tabs"):
            result = runtime.evaluate_process("orthogonal-corner", local,
                {"abJoint": "continuous", "cJoint": joint, "insertDepth": 8, "tabLength": 8, "tabWidth": 6})
            self.assertFalse(result["applicable"])
            self.assertIn("截面壁面必须与 A / B 轴向对齐", result["reason"])
        self.assertEqual(original, local)
        actual = participants()
        frame = actual[2]["section"]["sectionFrame"]
        first, second = frame["xAxisWorld"], frame["yAxisWorld"]
        frame["xAxisWorld"] = [cosine * (a + b) for a, b in zip(first, second)]
        frame["yAxisWorld"] = [cosine * (-a + b) for a, b in zip(first, second)]
        original = deepcopy(actual)
        for joint in ("insert", "tabs"):
            with self.assertRaisesRegex(ValueError, "截面壁面必须与 A / B 轴向对齐"):
                runtime.resolve_bound_plan("orthogonal-corner", {"abJoint": "continuous", "cJoint": joint},
                                           participants=actual, product_topology="orthogonal-corner")
        self.assertEqual(original, actual)


if __name__ == "__main__":
    unittest.main()
