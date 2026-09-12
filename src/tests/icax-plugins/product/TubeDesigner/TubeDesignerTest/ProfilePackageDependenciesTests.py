"""Complete package execution, lazy catalog isolation and curve export regressions."""
import base64
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = next(p for p in Path(__file__).resolve().parents if (p / "src/apps").is_dir())
SHARED = ROOT / "src/apps/tube-designer/templates/_shared"

def load(name):
    spec = importlib.util.spec_from_file_location(name, SHARED / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

runtime = load("profile_package_runtime")
export = load("profile_export_runtime")
geometry = load("section_geometry")

class Packages(unittest.TestCase):
    def descriptor(self):
        return {"schema": runtime.DESCRIPTOR_SCHEMA, "schemaVersion": 3,
                "id": "example", "version": "1", "displayName": "Example",
                "profileForm": "parametric", "parameters": [
                    {"key": "width", "displayName":"Width", "valueType": "number", "defaultValue": 10}]}

    def resources(self, width=12):
        return {name: base64.b64encode(data.encode()).decode() for name, data in {
            "helper.py": "from pathlib import Path\ndef width(): return float(Path(__file__).with_name('size.txt').read_text())",
            "size.txt": str(width), "nested/__init__.py": "from ..helper import width"}.items()}

    script = """from .nested import width
import helper
def build(parameters):
    assert helper.width() == width()
    profile = {'width':width(),'depth':5}
    from pathlib import Path
    assert Path(__file__).is_file()
    assert Path(__file__).with_name('profile.json').is_file()
    from importlib.resources import files
    assert float(files(__package__).joinpath('size.txt').read_text()) == profile['width']
    profile['contours'] = [{'kind':'roundedRectangle','width':profile['width'],'height':5,'radius':0}]
    return profile
"""

    def test_dependencies_and_data_are_available_and_isolated(self):
        before = dict(sys.modules)
        for width in (12, 17, 12):
            profile = runtime._evaluate(self.descriptor(), self.script, {}, "same", "example",
                                        self.resources(width))
            self.assertEqual(width, profile["width"])
        self.assertEqual(before.get("helper"), sys.modules.get("helper"))
        self.assertFalse(any(k.startswith("_icax_package_") for k in sys.modules))

    def test_dependency_changes_invalidate_digest(self):
        first = runtime._package_from_sources(json.dumps(self.descriptor()).encode(),
            self.script.encode(), "example", resources=self.resources())
        second = runtime._package_from_sources(json.dumps(self.descriptor()).encode(),
            self.script.encode(), "example", resources=self.resources(20))
        self.assertNotEqual(first["packageDigest"], second["packageDigest"])
        evaluated = runtime.generate({"action": "evaluate", **first}, {})["profile"]
        self.assertEqual(first["packageDigest"], evaluated["packageDigest"])
        self.assertEqual(12, evaluated["width"])

    def test_failed_script_cleans_up_modules_and_search_paths(self):
        previous = sys.path[:]
        with self.assertRaisesRegex(RuntimeError,"broken"):
            runtime._evaluate(self.descriptor(), "import helper\nraise RuntimeError('broken')",
                              {}, "failure", "example", self.resources())
        self.assertEqual(previous,sys.path)
        self.assertNotIn("helper",sys.modules)
        self.assertFalse(any(k.startswith("_icax_package_") for k in sys.modules))

    def test_directory_dependencies_match_archive_evaluation(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)/"example"
            root.mkdir()
            (root/"profile.json").write_text(json.dumps(self.descriptor()))
            (root/"profile.py").write_text(self.script)
            for name,data in runtime._resource_bytes(self.resources()).items():
                target = root/name
                target.parent.mkdir(parents=True,exist_ok=True)
                target.write_bytes(data)
            package = runtime._system_package(root)
            self.assertEqual(self.resources(),package["resources"])
            self.assertEqual(12,package["previewProfile"]["width"])

    def test_unsafe_resource_paths_are_rejected(self):
        for name in ("../outside", "/absolute", "C:/outside", "profile.py", "PROFILE.JSON", "a\\b"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                runtime._resource_bytes({name: "eA=="})

    def test_listing_never_executes_scripts_and_preserves_broken_entries(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            good = root / "example"
            good.mkdir()
            (good / "profile.json").write_text(json.dumps(self.descriptor()))
            (good / "profile.py").write_text("raise RuntimeError('selected only')")
            bad = root / "broken"
            bad.mkdir()
            (bad / "profile.json").write_text("{")
            entries = runtime._list_system_packages(root)
            self.assertEqual(2, len(entries))
            self.assertFalse(entries[0]["available"])
            self.assertTrue(entries[0]["error"])
            self.assertNotIn("previewProfile", entries[1])
            with self.assertRaisesRegex(RuntimeError, "selected only"):
                runtime.generate({"action":"evaluate-system", "profileRoot":folder,
                                  "systemProfileId":"example"}, {})

    def test_rational_bezier_export_preserves_weights(self):
        writer = export._Writer()
        export._spline(writer, {"controlPoints":[[0,0],[1,2],[2,0]],
                               "weights":[1,0.5,1]}, True)
        pairs = list(zip(writer.lines[::2], writer.lines[1::2]))
        self.assertEqual(["1", "0.5", "1"], [v for k,v in pairs if k == "41"])
        self.assertIn(("70", "12"), pairs)
        with self.assertRaises(ValueError):
            export._spline(export._Writer(), {"controlPoints":[[0,0],[1,1]], "weights":[1]}, True)

    def test_encrypted_archive_preserves_complete_package(self):
        writer = load("product_template_package_runtime")
        entries = [("profile.json", json.dumps(self.descriptor()).encode()),
                   ("profile.py", self.script.encode())]
        entries.extend(runtime._resource_bytes(self.resources()).items())
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/"example.ittt"
            path.write_bytes(writer._build_encrypted_zip(entries))
            package = runtime._inspect(path, runtime.PASSWORD)
            self.assertEqual(self.resources(), package["resources"])
            self.assertEqual(12, package["previewProfile"]["width"])

    def test_multispan_and_periodic_spline_bounds(self):
        segment = {"kind":"bspline", "degree":2,
                   "controlPoints":[[0,0],[1,2],[2,2],[3,0]],
                   "knots":[0,1,2], "multiplicities":[3,1,3]}
        self.assertEqual((0,3), geometry.spline_support(segment,[1,0]))
        self.assertAlmostEqual(2,geometry.spline_support(segment,[0,1])[1])
        periodic = {"kind":"nurbs", "degree":2, "periodic":True,
                    "controlPoints":[[-1,-1],[1,-1],[1,1],[-1,1]],
                    "weights":[1,1,1,1], "knots":[0,1,2,3,4]}
        for direction in ([1,0],[0,1]):
            self.assertEqual((-1,1), geometry.spline_support(periodic,direction))

    def test_high_degree_bezier_and_section_queries_share_bounds(self):
        for degree in (4,7,12,25):
            edge = {"kind":"bezier", "poles":[[i/degree, i/degree] for i in range(degree+1)],
                    "start":[0,0],"end":[1,1]}
            self.assertEqual({"min":[0,0],"max":[1,1]}, geometry.bounds({"edges":[edge]}))

    def test_trimmed_export_writes_only_selected_curve_interval(self):
        segment = {"kind":"bspline", "degree":1,"controlPoints":[[0,0],[10,0]],
                   "knots":[0,0,1,1],"startParameter":0.2,"endParameter":0.6}
        writer = export._Writer()
        export._spline(writer,segment)
        pairs = list(zip(writer.lines[::2],writer.lines[1::2]))
        self.assertEqual(["2","6"],[v for k,v in pairs if k == "10"])

if __name__ == "__main__":
    unittest.main()
