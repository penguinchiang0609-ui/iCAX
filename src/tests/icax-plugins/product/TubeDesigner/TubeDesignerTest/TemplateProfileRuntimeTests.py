"""Template-private profile packages: pure Python, no CAD DLL dependency."""
from __future__ import annotations

import copy
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.dont_write_bytecode = True
ROOT = next(parent for parent in Path(__file__).resolve().parents
            if (parent / "src/apps/tube-designer/templates").is_dir())
RUNTIME_PATH = ROOT / "src/apps/tube-designer/templates/_shared/profile_package_runtime.py"
SPEC = importlib.util.spec_from_file_location("template_profile_runtime_subject", RUNTIME_PATH)
assert SPEC is not None and SPEC.loader is not None
RUNTIME = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNTIME)
FIXTURE = ROOT / "src/tests/icax-plugins/product/TubeDesigner/Fixtures/editable_profile_package"


class TemplateProfileRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="icax-template-profile-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.descriptor = json.loads((FIXTURE / "profile.json").read_text(encoding="utf-8"))
        self.script = (FIXTURE / "profile.py").read_text(encoding="utf-8")

    def package_directory(self, directory, width=60.0):
        directory.mkdir(parents=True, exist_ok=True)
        descriptor = copy.deepcopy(self.descriptor)
        descriptor["parameters"][0]["defaultValue"] = width
        (directory / "profile.json").write_text(json.dumps(descriptor, ensure_ascii=False), encoding="utf-8")
        (directory / "profile.py").write_text(self.script, encoding="utf-8")
        return directory

    def template(self, template_id="rail-a", width=60.0, relative="resources/profiles/custom"):
        directory = self.root / template_id
        self.package_directory(directory / relative, width)
        return {
            "templateId": template_id, "templateName": f"款式 {template_id}",
            "templateDirectory": str(directory),
            "profileResources": {"private-frame": {
                "path": relative, "name": f"{template_id} 专用框管", "category": "框管",
            }},
        }

    def run_action(self, action, **parameters):
        return RUNTIME.generate({"action": action, **parameters}, {})

    def assert_template_identity(self, value, template_id="rail-a", resource_id="private-frame"):
        self.assertEqual("template", value["profileScope"])
        self.assertEqual("template", value["libraryScope"])
        self.assertEqual(template_id, value["templateId"])
        self.assertEqual(resource_id, value["profileDefinitionId"])
        self.assertEqual({"scope": "template", "templateId": template_id, "id": resource_id}, value["profileRef"])
        self.assertEqual("icax.template-profile", value["sourceFormat"])
        self.assertNotIn("savedProfileId", value)

    def symlink(self, link, target, *, directory=False):
        try:
            link.symlink_to(target, target_is_directory=directory)
        except (OSError, NotImplementedError) as error:
            self.skipTest(f"host does not allow creating symbolic links: {error}")

    def test_two_templates_can_declare_the_same_key_without_colliding(self):
        first, second = self.template("rail-a", 40), self.template("rail-b", 90)
        packages = self.run_action("list-template", templates=[first, second])["templateProfiles"]
        self.assertEqual(2, len(packages))
        self.assertEqual([40, 90], [package["previewProfile"]["width"] for package in packages])
        self.assertNotEqual(packages[0]["packageDigest"], packages[1]["packageDigest"])
        for package, template_id in zip(packages, ("rail-a", "rail-b")):
            self.assertEqual("private-frame", package["id"])
            self.assertEqual("test-editable-rect", package["descriptor"]["id"])
            self.assertTrue(package["scriptSource"])
            self.assertEqual(RUNTIME.PACKAGE_SCHEMA, package["schema"])
            self.assert_template_identity(package, template_id)
            self.assert_template_identity(package["previewProfile"], template_id)
            self.assertEqual(f"{template_id} 专用框管", package["previewProfile"]["name"])
            self.assertEqual("框管", package["previewProfile"]["category"])
        self.assertEqual(40, self.run_action("evaluate-template", **first, profileId="private-frame")["profile"]["width"])
        self.assertEqual(90, self.run_action("evaluate-template", **second, profileId="private-frame")["profile"]["width"])

    def test_evaluation_preserves_parameters_scope_overrides_and_has_no_saved_id(self):
        template = self.template(relative="资源/profiles/自定义")
        profile = self.run_action("evaluate-template", **template, profileId="private-frame",
                                  savedProfileId="must-not-leak",
                                  values={"width": 95.5, "depth": 50.5, "wallThickness": 1.5})["profile"]
        self.assert_template_identity(profile)
        self.assertEqual({"width": 95.5, "depth": 50.5, "wallThickness": 1.5}, profile["parameters"])
        self.assertEqual("rail-a 专用框管", profile["name"])
        self.assertEqual("框管", profile["category"])
        self.assertEqual("资源/profiles/自定义/profile.py", profile["sourceFileName"])
        self.assertEqual(95.5, profile["contours"][0]["width"])
        self.assertEqual(92.5, profile["contours"][1]["width"])
        self.assertEqual(3, len(profile["parameterDefinitions"]))

    def test_directory_descriptor_id_is_independent_of_the_declared_resource_key(self):
        template = self.template()
        declaration = template["profileResources"]["private-frame"]
        declaration.pop("name")
        declaration.pop("category")
        package = self.run_action("list-template", templates=[template])["templateProfiles"][0]
        self.assertEqual("private-frame", package["id"])
        self.assertEqual("测试可编辑矩形管", package["name"])
        self.assertEqual("test-editable-rect", package["previewProfile"]["profilePackageId"])
        self.assertEqual("private-frame", package["previewProfile"]["profileDefinitionId"])

    def test_icaxprofile_and_zip_resources_use_the_same_scoped_contract(self):
        template = self.template()
        directory = Path(template["templateDirectory"])
        resources = {}
        for key, extension in (("archived", ".icaxprofile"), ("zipped", ".zip")):
            path = directory / (key + extension)
            with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
                archive.writestr("profile.json", json.dumps(self.descriptor))
                archive.writestr("profile.py", self.script)
            resources[key] = {"path": path.name}
        template["profileResources"] = resources
        packages = self.run_action("list-template", templates=[template])["templateProfiles"]
        self.assertEqual(2, len(packages))
        for package in packages:
            self.assert_template_identity(package, resource_id=package["id"])
            self.assert_template_identity(package["previewProfile"], resource_id=package["id"])
            evaluated = self.run_action("evaluate-template", **template, profileId=package["id"], values={"width": 100})["profile"]
            self.assertEqual(100, evaluated["width"])
            self.assert_template_identity(evaluated, resource_id=package["id"])

    def test_frozen_package_evaluation_does_not_reopen_modified_or_deleted_sources(self):
        template = self.template()
        package = self.run_action("list-template", templates=[template])["templateProfiles"][0]
        source = Path(template["templateDirectory"]) / "resources/profiles/custom"
        self.package_directory(source, width=500)
        self.assertEqual(500, self.run_action("evaluate-template", **template, profileId="private-frame")["profile"]["width"])
        for name in ("profile.json", "profile.py"):
            (source / name).unlink()
        frozen = self.run_action("evaluate", **package, values={"width": 78.25}, savedProfileId="do-not-copy")["profile"]
        self.assert_template_identity(frozen)
        self.assertEqual(package["packageDigest"], frozen["contentDigest"])
        self.assertEqual({"width": 78.25, "depth": 40.0, "wallThickness": 2.0}, frozen["parameters"])
        self.assertEqual("rail-a 专用框管", frozen["name"])
        self.assertEqual("框管", frozen["category"])
        with self.assertRaises((ValueError, FileNotFoundError)):
            self.run_action("evaluate-template", **template, profileId="private-frame")

    def test_private_resources_are_never_registered_in_the_system_list(self):
        template = self.template()
        self.run_action("list-template", templates=[template])
        system_root = self.root / "system"
        self.package_directory(system_root / self.descriptor["id"])
        packages = self.run_action("list-system", profileRoot=str(system_root))["systemProfiles"]
        self.assertEqual(1, len(packages))
        self.assertEqual("system", packages[0]["previewProfile"]["profileScope"])
        self.assertNotIn("templateId", packages[0])
        with self.assertRaises(ValueError):
            self.run_action("evaluate-system", profileRoot=str(system_root), systemProfileId="private-frame")

    def test_undeclared_resource_is_rejected_even_when_its_directory_exists(self):
        template = self.template()
        self.package_directory(Path(template["templateDirectory"]) / "unlisted")
        with self.assertRaisesRegex(ValueError, "未声明"):
            self.run_action("evaluate-template", **template, profileId="unlisted")

    def test_resource_keys_follow_the_native_lowercase_profile_id_rule(self):
        template = self.template()
        declaration = template["profileResources"]["private-frame"]
        for key in ("", "..", "../escape", "/absolute", "a/b", "a\\b", "Upper", "a.b", "a:b", "_x", "1x", "a" * 81):
            with self.subTest(key=key):
                invalid = {**template, "profileResources": {key: declaration}}
                with self.assertRaises(ValueError):
                    self.run_action("list-template", templates=[invalid])
                with self.assertRaises(ValueError):
                    self.run_action("evaluate-template", **invalid, profileId=key)

    def test_relative_paths_reject_parent_absolute_unc_and_drive_forms(self):
        template = self.template()
        outside = self.package_directory(self.root / "outside")
        for path in ("../outside", "resources/../../../outside", str(outside), "/outside", "//server/share/package",
                     "\\\\server\\share\\package", "C:/outside", "C:outside", "..\\outside", "resources:stream", "a\0b"):
            with self.subTest(path=path):
                invalid = {**template, "profileResources": {"private-frame": {"path": path}}}
                with self.assertRaises(ValueError):
                    self.run_action("evaluate-template", **invalid, profileId="private-frame")

    def test_directory_symlink_cannot_escape_the_template(self):
        template = self.template()
        outside = self.package_directory(self.root / "outside")
        self.symlink(Path(template["templateDirectory"]) / "linked", outside, directory=True)
        template["profileResources"]["private-frame"]["path"] = "linked"
        with self.assertRaisesRegex(ValueError, "链接|联接"):
            self.run_action("evaluate-template", **template, profileId="private-frame")

    def test_each_required_file_is_confined_even_inside_a_normal_directory(self):
        for name in ("profile.json", "profile.py"):
            with self.subTest(member=name):
                template = self.template("member-" + name.replace(".", "-"))
                source = Path(template["templateDirectory"]) / "resources/profiles/custom"
                outside = self.package_directory(self.root / ("outside-" + name))
                (source / name).unlink()
                self.symlink(source / name, outside / name)
                with self.assertRaisesRegex(ValueError, "链接|联接"):
                    self.run_action("evaluate-template", **template, profileId="private-frame")

    @unittest.skipUnless(os.name == "nt", "Windows junction test")
    def test_directory_junction_cannot_escape_the_template(self):
        template = self.template()
        outside = self.package_directory(self.root / "junction-target")
        link = Path(template["templateDirectory"]) / "linked"
        result = subprocess.run(["cmd.exe", "/c", "mklink", "/J", str(link), str(outside)],
                                capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW)
        if result.returncode:
            self.skipTest("junction creation is not permitted on this host")
        self.addCleanup(lambda: link.rmdir() if os.path.lexists(link) else None)
        template["profileResources"]["private-frame"]["path"] = "linked"
        with self.assertRaisesRegex(ValueError, "链接|联接"):
            self.run_action("evaluate-template", **template, profileId="private-frame")

    def test_file_and_archive_size_limits_are_enforced(self):
        template = self.template()
        with patch.object(RUNTIME, "MAX_MEMBER_BYTES", 16):
            with self.assertRaisesRegex(ValueError, "4 MB"):
                self.run_action("evaluate-template", **template, profileId="private-frame")
        with patch.object(RUNTIME, "MAX_TOTAL_BYTES", 16):
            with self.assertRaisesRegex(ValueError, "8 MB"):
                self.run_action("evaluate-template", **template, profileId="private-frame")
        archive_path = Path(template["templateDirectory"]) / "large.zip"
        with zipfile.ZipFile(archive_path, "w") as archive:
            archive.writestr("profile.json", json.dumps(self.descriptor))
            archive.writestr("profile.py", self.script)
        template["profileResources"]["private-frame"]["path"] = "large.zip"
        with patch.object(RUNTIME, "MAX_ARCHIVE_BYTES", 16):
            with self.assertRaisesRegex(ValueError, "8 MB"):
                self.run_action("evaluate-template", **template, profileId="private-frame")

    def test_invalid_declarations_duplicates_and_reference_mismatches_are_rejected(self):
        template = self.template()
        for templates in ({}, [None], [template, template], [{**template, "profileResources": []}],
                          [{**template, "profileResources": {"private-frame": None}}]):
            with self.subTest(templates=type(templates).__name__):
                with self.assertRaises(ValueError):
                    self.run_action("list-template", templates=templates)
        package = self.run_action("list-template", templates=[template])["templateProfiles"][0]
        package["profileRef"] = {"scope": "template", "templateId": "another-template", "id": "private-frame"}
        with self.assertRaisesRegex(ValueError, "不一致"):
            self.run_action("evaluate", **package)
        with self.assertRaises(ValueError):
            self.run_action("evaluate-template", **template, profileId="private-frame", values={"unexpected": 1})

    def test_empty_catalog_is_explicitly_supported(self):
        self.assertEqual({"templateProfiles": []}, self.run_action("list-template", templates=[]))
        self.assertEqual({"templateProfiles": []}, self.run_action("list-template", templates=[{
            "templateId": "empty", "templateName": "无私有管型", "profileResources": {},
        }]))


if __name__ == "__main__":
    unittest.main()
