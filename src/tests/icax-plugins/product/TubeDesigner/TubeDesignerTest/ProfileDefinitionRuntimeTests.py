"""Profile form and ownership are independent; both resolve section geometry."""
import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = next(p for p in Path(__file__).resolve().parents if (p/"src/apps").is_dir())
SHARED = ROOT/"src/apps/tube-designer/templates/_shared"
def load(name, file):
    spec=importlib.util.spec_from_file_location(name,SHARED/file)
    module=importlib.util.module_from_spec(spec)
    sys.modules[name]=module
    spec.loader.exec_module(module)
    return module
runtime=load("definition_test_runtime","profile_package_runtime.py")
catalog=load("definition_test_catalog","tube_profile_catalog.py")

class Profiles(unittest.TestCase):
    def test_legacy_definition_is_rejected_by_runtime_but_migratable_offline(self):
        old=self.fixed();old["schemaVersion"]=2
        with self.assertRaises(ValueError):runtime._validate_descriptor(old)
        spec=importlib.util.spec_from_file_location("profile_migration",ROOT/"src/apps/tube-designer/migrations/profile_v3.py")
        migration=importlib.util.module_from_spec(spec);spec.loader.exec_module(migration)
        migrated=migration.migrate_definition(old)
        runtime._validate_descriptor(migrated)
        self.assertEqual(old["schemaVersion"],2)
        self.assertEqual(migration.migrate_definition(migrated),migrated)

    def test_missing_form_is_not_inferred(self):
        descriptor=self.fixed();del descriptor["profileForm"]
        with self.assertRaises(ValueError):runtime._validate_descriptor(descriptor)

    def fixed(self):
        return {"schema":runtime.DESCRIPTOR_SCHEMA,"schemaVersion":3,"id":"fixed-test","version":"1",
                "displayName":"Fixed","profileForm":"fixed","parameters":[],
                "section":{"width":40,"depth":20,"contours":[
                    {"kind":"roundedRectangle","width":40,"height":20,"radius":2},
                    {"kind":"roundedRectangle","width":36,"height":16,"radius":0}]}}

    def test_fixed_does_not_execute_script(self):
        result=runtime._evaluate(self.fixed(),"raise RuntimeError('must not execute')",{},"test","test")
        self.assertEqual(result["profileForm"],"fixed")
        self.assertFalse(result["editableParameters"])
        self.assertTrue(result["frozenGeometry"])
        self.assertEqual(result["contours"],self.fixed()["section"]["contours"])

    def test_fixed_rejects_shape_parameters(self):
        with self.assertRaises(ValueError):
            runtime._evaluate(self.fixed(),"",{"width":50},"test","test")
        descriptor=self.fixed();descriptor["parameters"]=[{"key":"width"}]
        with self.assertRaises(ValueError):runtime._validate_descriptor(descriptor)

    def test_invalid_form_is_rejected(self):
        descriptor=self.fixed();descriptor["profileForm"]="system"
        with self.assertRaises(ValueError):runtime._validate_descriptor(descriptor)

    def test_both_sources_support_fixed_without_script(self):
        with tempfile.TemporaryDirectory() as folder:
            directory=Path(folder)/"fixed-test";directory.mkdir()
            (directory/"profile.json").write_text(json.dumps(self.fixed()),encoding="utf8")
            for scope in ("system","user"):
                packages=runtime._list_system_packages(Path(folder),scope)
                self.assertEqual(len(packages),1)
                self.assertTrue(packages[0]["previewProfile"]["contours"])
                preview=runtime.generate({"action":"evaluate-"+scope,"profileRoot":folder,
                    scope+"ProfileId":"fixed-test"},{})["profile"]
                self.assertEqual(preview["profileScope"],scope)
                self.assertEqual(preview["profileForm"],"fixed")
            previous=catalog.PROFILE_ROOT
            try:
                catalog.PROFILE_ROOT=Path(folder)
                profile=catalog.load_profile({},"stock",profile_id="fixed-test")
                self.assertEqual(profile.contours(),self.fixed()["section"]["contours"])
                self.assertEqual(profile.properties()["profileForm"],"fixed")
            finally:catalog.PROFILE_ROOT=previous

    def test_existing_programs_match_catalog_and_library(self):
        for package in runtime.generate({"action":"list-system"},{})["systemProfiles"]:
            descriptor=package["descriptor"]
            package["previewProfile"] = runtime.generate({"action":"evaluate-system",
                "systemProfileId":descriptor["id"]},{})["profile"]
            profile=catalog.load_profile({},"stock",profile_id=descriptor["id"])
            self.assertEqual(profile.contours(),package["previewProfile"]["contours"])
            self.assertEqual(package["previewProfile"]["profileForm"],"parametric")

    def test_fixed_result_is_detached_from_definition(self):
        descriptor=self.fixed()
        result=runtime._evaluate(descriptor,"",{},"test","test")
        result["contours"][0]["width"]=999
        self.assertEqual(descriptor,self.fixed())

if __name__=="__main__":unittest.main()
