"""Direct-inverse regressions for sharp and rounded structural U sections."""
import copy
import importlib.util
import math
import unittest
from contextlib import contextmanager, ExitStack
from unittest.mock import patch

from ProfileFamilyTests import runtime, ROOT


CATALOG = ROOT / "src/apps/tube-designer/templates/profile"
spec = importlib.util.spec_from_file_location(
    "structural_channel_geometry",
    ROOT / "src/apps/tube-designer/templates/_shared/profile_recognition_geometry.py",
)
geometry = importlib.util.module_from_spec(spec)
spec.loader.exec_module(geometry)


@contextmanager
def forbid_forward_evaluation():
    """Guard the whole recognition call, including loading the catalogue."""
    original_context = runtime._script_context
    attempts = []

    def forbidden(*args, **kwargs):
        attempts.append(True)
        raise AssertionError("Forward geometry evaluation is forbidden during fitting")

    @contextmanager
    def guarded_script(*args, **kwargs):
        with original_context(*args, **kwargs) as namespace:
            namespace["build"] = forbidden
            yield namespace

    with ExitStack() as stack:
        stack.enter_context(patch.object(runtime, "_evaluate", side_effect=forbidden))
        stack.enter_context(patch.object(runtime, "evaluate_section", side_effect=forbidden))
        stack.enter_context(patch.object(runtime, "_script_context", guarded_script))
        yield
    # Catalogue error reporting must not swallow a forbidden forward call.
    if attempts:
        raise AssertionError("The inverse path attempted forward geometry evaluation")


class StructuralChannelFittingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.package = runtime._system_package(CATALOG / "channel", preview=False)

    def evaluate(self, parameters):
        package = self.package
        return runtime._evaluate(
            package["descriptor"], package["scriptSource"],
            {**package["defaultParameters"], **parameters},
            package["packageDigest"], package["sourceFileName"], package["resources"],
        )

    def recognize_catalogue(self, section):
        with forbid_forward_evaluation():
            response = runtime.generate({
                "action": "recognize-system", "profileRoot": str(CATALOG),
                "section": section,
            }, {})
        return next(item for item in response["results"] if item["profileId"] == "channel")

    def test_sharp_and_rounded_roots_recover_complete_transformed_contour(self):
        # Include standard concentric bends, sharp outside/rounded inside,
        # all-sharp stock and independently different upper/lower roots.
        cases = (
            ((0, 0), (0, 0)),
            ((0, 0), (6, 6)),
            ((4, 4), (0, 0)),
            ((10, 10), (4, 4)),
            ((0, 4), (7, 2)),
            ((3, 0), (0, 5)),
        )
        for outside, inside in cases:
            values = dict(
                width=64, depth=120, wallThickness=6, useHotRolled=False,
                outerRadius=outside[0], useOuterRadii=True,
                outerRadius1=outside[0], outerRadius2=outside[1],
                useInnerRadius=True, innerRadius1=inside[0], innerRadius2=inside[1],
            )
            # Forward calls are used only to prepare the test input/oracle.
            base = geometry.normalize(self.evaluate(values), 0.001)
            for angle in (0, 37, 123):
                with self.subTest(outside=outside, inside=inside, angle=angle):
                    original = geometry.transform(base, math.radians(angle), [123, -45])
                    imported = copy.deepcopy(original)
                    edges = []
                    for edge in imported[0]["edges"]:
                        if edge["kind"] == "line":
                            midpoint = [(edge["start"][d] + edge["end"][d]) / 2 for d in (0, 1)]
                            edges.extend([{**edge, "end": midpoint}, {**edge, "start": midpoint}])
                        else:
                            edges.append(edge)
                    edges = [geometry._reverse(edge) for edge in reversed(edges)]
                    imported[0]["edges"] = edges[3:] + edges[:3]
                    match = self.recognize_catalogue(geometry.to_section(imported))
                    self.assertEqual("matched", match["status"], match)
                    candidate = match["candidates"][0]
                    recovered = candidate["parameters"]
                    for key in ("width", "depth", "wallThickness", "outerRadius1",
                                "outerRadius2", "innerRadius1", "innerRadius2"):
                        self.assertAlmostEqual(values[key], recovered[key], places=5, msg=key)
                    self.assertFalse(recovered["useHotRolled"])
                    rebuilt = geometry.normalize(self.evaluate(recovered), 0.001)
                    equivalent, error = geometry.verify(original, rebuilt, candidate["transform"], 0.001)
                    self.assertTrue(equivalent, (candidate, error))

    def test_nontangent_root_arc_is_rejected(self):
        raw = self.evaluate(dict(width=64, depth=120, wallThickness=6,
                                 outerRadius=0, useInnerRadius=True,
                                 innerRadius1=6, innerRadius2=6))
        arc = next(edge for edge in raw["contours"][0]["segments"] if edge["kind"] == "arc")
        arc["middle"][0] += 0.75
        # Endpoints, straight walls and nominal section dimensions still
        # match a channel, but the arc is no longer tangent to those walls.
        match = self.recognize_catalogue(raw)
        self.assertEqual("no-match", match["status"], match)

    def test_hot_rolled_channel_with_sharp_outer_roots_remains_supported(self):
        values = dict(width=65, depth=140, wallThickness=6, flangeThickness=9,
                      useHotRolled=True, outerRadius=0, useInnerRadius=True,
                      innerRadius1=7, innerRadius2=7, freeEndRadius=3)
        original = geometry.transform(geometry.normalize(self.evaluate(values), 0.001),
                                      math.radians(37), [123, -45])
        match = self.recognize_catalogue(geometry.to_section(original))
        self.assertEqual("matched", match["status"], match)
        candidate = match["candidates"][0]
        self.assertTrue(candidate["parameters"]["useHotRolled"])
        equivalent, error = geometry.verify(original,
            geometry.normalize(self.evaluate(candidate["parameters"]), 0.001),
            candidate["transform"], 0.001)
        self.assertTrue(equivalent, (candidate, error))

    def test_parallel_flanges_with_unsupported_round_toes_are_rejected(self):
        # Make a valid exact test contour with the existing geometric strip
        # helper, outside the guarded inverse call. The channel constructor
        # only enables these toe radii in its sloped hot-rolled branch.
        package = self.package
        with runtime._script_context(package["scriptSource"], package["packageDigest"],
                                     package["resources"], package["descriptor"]) as namespace:
            contour = namespace["strip"]([[32, -60], [-32, -60], [-32, 60], [32, 60]],
                                          6, 4, 10, free_end_radii=(1, 2))
        match = self.recognize_catalogue({"contours": [contour]})
        self.assertEqual("no-match", match["status"], match)

    def test_extra_unaccounted_boundary_is_rejected(self):
        raw = self.evaluate(dict(width=64, depth=120, wallThickness=6,
                                 outerRadius=0, useInnerRadius=True,
                                 innerRadius1=0, innerRadius2=0))
        raw["contours"].append(dict(kind="circle", center=[1000, 1000], radius=2))
        match = self.recognize_catalogue(raw)
        self.assertEqual("no-match", match["status"], match)


if __name__ == "__main__":
    unittest.main()
