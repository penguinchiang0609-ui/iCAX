"""Exercise actual Tk checkbuttons without opening a visible user window."""
from pathlib import Path
from types import SimpleNamespace
import tkinter as tk
import unittest
from unittest.mock import Mock, patch

from feature_catalog import ALL_FEATURES, BY_ID, FEATURES
from issuer_gui import PermissionSelection, Window


class IssuerGuiTests(unittest.TestCase):
    def setUp(self):
        self.root = tk.Tk()
        self.root.withdraw()
        self.addCleanup(self.root.destroy)
        self.window = Window(self.root)
        self.selection = self.window.permission_selection
        self.root.update_idletasks()

    def test_default_selection_grants_nothing_and_all_groups_are_real_controls(self):
        self.assertEqual(set(self.selection.buttons), set(BY_ID))
        with self.assertRaises(ValueError):
            self.selection.mask()
        for feature in FEATURES:
            self.assertFalse(self.selection.variables[feature.id].get())
            self.assertEqual(self.selection.buttons[feature.id].cget("text"), feature.label)
            self.assertEqual(self.selection.buttons[feature.id].instate(["disabled"]), feature.parent is not None)

    def test_page_and_operation_checkbox_actions_encode_exact_bits(self):
        for feature in FEATURES:
            if feature.parent is None:
                self.selection.buttons[feature.id].invoke()
                self.assertEqual(self.selection.mask(), feature.bit)
                for child in FEATURES:
                    if child.parent == feature.id:
                        self.assertFalse(self.selection.buttons[child.id].instate(["disabled"]))
                        self.selection.buttons[child.id].invoke()
                        self.assertEqual(self.selection.mask(), feature.bit | child.bit)
                        self.selection.buttons[child.id].invoke()
                self.selection.buttons[feature.id].invoke()

    def test_revoking_page_clears_and_disables_its_operations(self):
        self.selection.buttons["page.nesting"].invoke()
        self.selection.buttons["nesting.calculate"].invoke()
        self.selection.buttons["nesting.export"].invoke()
        self.assertEqual(self.selection.mask(), 2 | 256 | 512)
        self.selection.buttons["page.nesting"].invoke()
        for child in FEATURES:
            if child.parent == "page.nesting":
                self.assertFalse(self.selection.variables[child.id].get())
                self.assertTrue(self.selection.buttons[child.id].instate(["disabled"]))
        self.selection.buttons["page.nesting"].invoke()
        self.assertEqual(self.selection.mask(), 2)

    def test_all_checkbox_actions_cover_full_catalogue(self):
        for feature in FEATURES:
            self.selection.buttons[feature.id].invoke()
        self.assertEqual(self.selection.mask(), ALL_FEATURES)

    def test_production_issue_uses_descriptor_bits_and_operator_summary(self):
        self.selection.buttons["page.simulation"].invoke()
        self.selection.buttons["simulation.run"].invoke()
        self.window.request = Path("isolated-test.tdreq")
        self.window.issuer = SimpleNamespace(issue=Mock(return_value="test-license-id"))
        self.window.fields["客户名称"].set("GUI 测试")
        self.window.password = Mock(return_value=b"TEST-ONLY-EPHEMERAL-PASSWORD")
        self.window.refresh = lambda: self.window.table.insert("", "end", iid="test-license-id")
        self.window.export = Mock()
        with patch("issuer_gui.messagebox.askyesno", return_value=True) as confirmation:
            self.window.issue()
        self.assertEqual(self.window.issuer.issue.call_args.kwargs["features"], 8 | 4096)
        self.assertIn(BY_ID["page.simulation"].label, confirmation.call_args.args[1])
        self.assertIn(BY_ID["simulation.run"].label, confirmation.call_args.args[1])
        self.window.export.assert_called_once()

    def test_empty_or_orphan_selection_is_rejected_before_confirmation_or_password(self):
        self.window.request = Path("isolated-test.tdreq")
        self.window.issuer = SimpleNamespace(issue=Mock())
        self.window.password = Mock()
        with patch("issuer_gui.messagebox.askyesno") as confirmation:
            for mask in [False, True]:
                self.selection.variables["product.design"].set(mask)
                with self.assertRaises(ValueError):
                    self.window.issue()
            confirmation.assert_not_called()
            self.window.password.assert_not_called()
            self.window.issuer.issue.assert_not_called()


if __name__ == "__main__":
    unittest.main()
