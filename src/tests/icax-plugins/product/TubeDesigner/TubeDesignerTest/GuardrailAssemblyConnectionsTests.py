"""Real committed-member relationships for the four formal guardrail variants."""

import json
from pathlib import Path
import sys
import unittest


SRC = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(SRC / "iCAX-Engine/framework/TemplateRuntime/python"))
from icax_template_worker import _load_template


PRODUCTS = SRC / "apps/tube-designer/templates/product"
TEMPLATES = (
    "modular_guardrail",
    "modular_guardrail_cross-straight",
    "modular_guardrail_diamond-straight",
    "modular_guardrail_glass-straight",
)


class GuardrailAssemblyConnectionsTests(unittest.TestCase):
    def test_every_variant_declares_only_current_members_in_both_geometry_purposes(self):
        for name in TEMPLATES:
            with self.subTest(template=name):
                directory = PRODUCTS / name
                descriptor = json.loads((directory / "template.json").read_text(encoding="utf-8"))
                parameters = {field["key"]: field["defaultValue"]
                              for field in descriptor["parameters"]}
                parameters["sideLength1"] = 800.0
                module = _load_template(str(directory / "template.py"), "assembly-" + name)
                display = module.generate(parameters, {"template": descriptor, "geometryPurpose": "display"})
                manufacturing = module.generate(parameters, {"template": descriptor,
                                                            "geometryPurpose": "manufacturing"})
                self.assertEqual(parameters, display["parameters"])
                self.assertEqual(parameters, manufacturing["parameters"])
                self.assertEqual(display["relationships"], manufacturing["relationships"])
                items = {item["key"] for item in display["items"]}
                relations = display["relationships"]
                self.assertTrue(relations)
                self.assertEqual(len({relation["key"] for relation in relations}), len(relations))
                for relation in relations:
                    self.assertEqual(len(relation["items"]), 2)
                    self.assertTrue(set(relation["items"]).issubset(items), relation)

    def test_base_guardrail_square_butt_joints_cover_posts_rails_and_vertical_bars(self):
        directory = PRODUCTS / "modular_guardrail"
        descriptor = json.loads((directory / "template.json").read_text(encoding="utf-8"))
        parameters = {field["key"]: field["defaultValue"] for field in descriptor["parameters"]}
        parameters["sideLength1"] = 800.0
        module = _load_template(str(directory / "template.py"), "assembly-base-guardrail")
        document = module.generate(parameters, {"template": descriptor, "geometryPurpose": "manufacturing"})
        connections = {tuple(relation["items"]): relation for relation in document["relationships"]}
        for rail, post in (("segment.1.bay.1.bottom", "post.0001"),
                           ("segment.1.bay.1.bottom", "post.0002"),
                           ("segment.1.bay.2.bottom", "post.0002"),
                           ("segment.1.cap.1", "post.0001"),
                           ("segment.1.cap.1", "post.0002"),
                           ("segment.1.bay.1.bar.001", "segment.1.bay.1.bottom"),
                           ("segment.1.bay.1.bar.001", "segment.1.bay.1.upper")):
            self.assertIn((rail, post), connections)
            self.assertEqual(connections[(rail, post)]["properties"], {
                "geometry": "square-butt-contact", "manufacturingCut": "none"})

    def test_two_rail_vertical_bar_meets_actual_cap_instead_of_absent_upper_rail(self):
        directory = PRODUCTS / "modular_guardrail"
        descriptor = json.loads((directory / "template.json").read_text(encoding="utf-8"))
        parameters = {field["key"]: field["defaultValue"] for field in descriptor["parameters"]}
        parameters.update(sideLength1=800.0, railCount=2)
        module = _load_template(str(directory / "template.py"), "assembly-two-rail-guardrail")
        document = module.generate(parameters, {"template": descriptor, "geometryPurpose": "manufacturing"})
        connections = {tuple(relation["items"]) for relation in document["relationships"]}
        bar = "segment.1.bay.1.bar.001"
        self.assertIn((bar, "segment.1.bay.1.bottom"), connections)
        self.assertIn((bar, "segment.1.cap.1"), connections)
        self.assertNotIn((bar, "segment.1.bay.1.upper"), connections)

    def test_round_cope_joints_are_not_duplicated_as_square_butt_contacts(self):
        directory = PRODUCTS / "modular_guardrail"
        descriptor = json.loads((directory / "template.json").read_text(encoding="utf-8"))
        parameters = {field["key"]: field["defaultValue"] for field in descriptor["parameters"]}
        parameters.update(sideLength1=800.0, postProfileType="round",
                          handrailProfileType="round", railProfileType="round")
        module = _load_template(str(directory / "template.py"), "assembly-round-guardrail")
        document = module.generate(parameters, {"template": descriptor, "geometryPurpose": "manufacturing"})
        relations = document["relationships"]
        pairs = [tuple(sorted(relation["items"])) for relation in relations]
        self.assertEqual(len(pairs), len(set(pairs)))
        self.assertTrue(relations)
        self.assertTrue(all(relation["properties"]["geometry"] == "outer-envelope-cope"
                            for relation in relations))


if __name__ == "__main__":
    unittest.main()
