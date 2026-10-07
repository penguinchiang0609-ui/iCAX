import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

from feature_catalog import ALL_FEATURES, BY_ID, FEATURES, labels_for_mask, load_catalogue, validate_mask


class FeatureCatalogueTests(unittest.TestCase):
    def test_current_shared_protocol_contract(self):
        self.assertEqual({feature.id: feature.bit for feature in FEATURES}, {
            "page.product": 1, "page.nesting": 2, "page.machining": 4, "page.simulation": 8,
            "product.design": 16, "product.breakdown": 32, "product.export": 64,
            "nesting.edit": 128, "nesting.calculate": 256, "nesting.export": 512,
            "machining.toolpath": 1024, "machining.export": 2048, "simulation.run": 4096,
        })
        self.assertEqual(ALL_FEATURES, 8191)

    def test_each_page_can_be_authorized_independently(self):
        for feature in FEATURES:
            if feature.parent is None:
                with self.subTest(page=feature.id):
                    self.assertEqual(validate_mask(feature.bit), feature.bit)
                    self.assertEqual(labels_for_mask(feature.bit), (feature.label,))

    def test_operation_masks_require_page_and_reject_unknown_bits(self):
        for feature in FEATURES:
            if feature.parent:
                with self.subTest(operation=feature.id):
                    with self.assertRaises(ValueError):
                        validate_mask(feature.bit)
                    validate_mask(feature.bit | BY_ID[feature.parent].bit)
        for value in [0, -1, True, "17", 17.0, 8192, 0xffffffff, 0x100000000]:
            with self.subTest(value=value), self.assertRaises(ValueError):
                validate_mask(value)

    def test_frozen_bundle_uses_its_descriptor_and_never_source_fallback(self):
        with tempfile.TemporaryDirectory(prefix="td-feature-bundle-") as directory:
            with patch.object(sys, "frozen", True, create=True), patch.object(sys, "_MEIPASS", directory, create=True):
                with self.assertRaises(FileNotFoundError):
                    load_catalogue()
                descriptor = Path(__file__).resolve().parent.parent / "features.json"
                (Path(directory) / "features.json").write_bytes(descriptor.read_bytes())
                self.assertEqual(load_catalogue(), FEATURES)

    def test_invalid_descriptor_cannot_change_the_ui_or_issuer_mask(self):
        original = json.loads((Path(__file__).resolve().parent.parent / "features.json").read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory(prefix="td-feature-invalid-") as directory:
            path = Path(directory) / "features.json"
            for mutation in [
                lambda c: c.update(schemaVersion=True), lambda c: c.update(certificateFormat="TDLIC002"),
                lambda c: c["features"][4].update(parent="page.missing"),
                lambda c: c["features"][4].update(parent="product.design"),
                lambda c: c["features"][4].update(bit=1),
                lambda c: c["features"][4].update(id="page.product"),
                lambda c: c["features"][4].update(parent=None),
                lambda c: c["features"][4].update(bit=True),
            ]:
                candidate = json.loads(json.dumps(original))
                mutation(candidate)
                path.write_text(json.dumps(candidate), encoding="utf-8")
                with self.subTest(candidate=candidate["features"][4]), self.assertRaises(ValueError):
                    load_catalogue(path)
            path.write_text('{"schemaVersion":1,"schemaVersion":1}', encoding="utf-8")
            with self.assertRaises(ValueError):
                load_catalogue(path)


if __name__ == "__main__":
    unittest.main()
