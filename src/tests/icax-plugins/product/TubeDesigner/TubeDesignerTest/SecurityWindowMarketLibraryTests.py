"""Market-facing infill variants must change parts, not just labels."""
import unittest
from copy import deepcopy
from SecurityWindowRulesTests import build, template_input, NAMES, REVIEW


class MarketLibraryTests(unittest.TestCase):
    def test_all_envelopes_and_infill_modes_with_opening(self):
        for name in NAMES:
            for pattern in ("grid", "vertical", "horizontal"):
                with self.subTest(name=name, pattern=pattern):
                    doc=build(name,infillPattern=pattern)
                    review=doc['extensions'][REVIEW]
                    self.assertEqual(pattern,review['infillPattern'])
                    self.assertEqual('BAR_GRILLE',review['productFamily'])
                    self.assertEqual('LOCAL_HINGED_HATCH',review['openingSystem'])
                    self.assertFalse(review['complianceCertified'])
                    keys=[i['key'] for i in doc['items'] if i['key'].startswith(('main_grid.','access_door.leaf.horizontal.','access_door.leaf.vertical.'))]
                    horizontal=[k for k in keys if '.horizontal.' in k]
                    vertical=[k for k in keys if '.vertical.' in k]
                    if pattern=='vertical':self.assertFalse(horizontal)
                    if pattern=='horizontal':self.assertFalse(vertical)
                    if pattern!='vertical':self.assertTrue(horizontal)
                    if pattern!='horizontal':self.assertTrue(vertical)
                    self.assertEqual(pattern,doc['parameters']['infillPattern'])

    def test_project_rules_not_universal_threshold(self):
        for name in NAMES:
            with self.subTest(name=name):
                doc=build(name,doorClearWidth=700,doorClearHeight=900,
                    projectEscapeMinWidth=700,projectEscapeMinHeight=900,
                    projectRuleReference='项目确认输入（测试）',installationMode='recessed')
                review=doc['extensions'][REVIEW]
                self.assertEqual((700,900),(review['designClearWidth'],review['designClearHeight']))
                self.assertEqual('recessed',review['installationMode'])
                self.assertTrue(all(v=='not_verified' for v in review['performance'].values()))
                self.assertIn('SW_PROJECT_ESCAPE_RULE',{d['code'] for d in doc['diagnostics']})
                with self.assertRaisesRegex(ValueError,'项目设计下限'):
                    build(name,projectEscapeMinWidth=801)

    def test_fixed_variants_and_cap_preservation(self):
        name='five_face_security_window'
        docs=[build(name,infillPattern=p,accessDoorEnabled=False) for p in ('grid','horizontal','vertical')]
        # All non-facade parts, including the roof/base, remain identical.
        def caps(d):
            items=deepcopy([i for i in d['items'] if i['key'].startswith('cap_grid.')])
            for item in items:item['properties'].pop('partNumber',None)
            return items
        self.assertTrue(caps(docs[0]))
        self.assertEqual(caps(docs[0]),caps(docs[1]))
        self.assertEqual(caps(docs[0]),caps(docs[2]))
        for d in docs:self.assertEqual('FIXED',d['extensions'][REVIEW]['openingSystem'])

    def test_invalid_pattern_and_project_limits_rejected(self):
        for values in ({'infillPattern':'mesh'},{'projectEscapeMinWidth':0},{'projectEscapeMinHeight':float('nan')}):
            with self.assertRaises(ValueError):build(NAMES[0],**values)


if __name__=='__main__':unittest.main()
