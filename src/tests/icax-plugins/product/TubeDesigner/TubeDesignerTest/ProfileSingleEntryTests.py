import copy
import unittest
from ProfileFamilyTests import runtime, CATALOG
from ProfileDefinitionRuntimeTests import catalog

class SingleEntry(unittest.TestCase):
    def test_build_is_called_once_and_contours_callback_is_never_called(self):
        descriptor={'profileForm':'parametric'}
        source='''calls=0
def build(parameters):
    global calls
    calls += 1
    assert calls == 1
    return {'width':10,'depth':10,'contours':[{'kind':'circle','radius':5}]}
def contours(*args,**kwargs):
    raise AssertionError('obsolete callback must not be called')
'''
        built, curves=runtime.evaluate_section(descriptor,source,{},'single-entry-test')
        self.assertEqual(built['contours'],curves)
        curves[0]['radius']=99
        self.assertEqual(5,built['contours'][0]['radius'])

    def test_missing_contours_is_an_explicit_error(self):
        with self.assertRaisesRegex(ValueError,'contours'):
            runtime.evaluate_section({'profileForm':'parametric'},'def build(p): return {"width":10,"depth":10}',{},'invalid-test')

    def test_all_system_packages_have_only_one_public_generation_entry(self):
        for path in CATALOG.iterdir():
            if not (path/'profile.json').exists():continue
            package=runtime._system_package(path,preview=False)
            self.assertNotIn('def contours(',package['scriptSource'],path.name)
            descriptor=package['descriptor']
            with runtime._script_context(package['scriptSource'],package['packageDigest'],package['resources'],descriptor) as ns:
                built=ns['build'](copy.deepcopy(package['defaultParameters']))
                self.assertTrue(built['contours'],path.name)

    def test_public_offset_does_not_mutate_stock_and_uses_geometric_kind(self):
        for name in ('rect','round'):
            p=runtime._system_package(CATALOG/name,preview=True)['previewProfile']
            original=copy.deepcopy(p['contours'])
            result=catalog._offset_outer_contour(p['contours'][0],1)
            self.assertEqual(original,p['contours'])
            self.assertNotEqual(original[0],result)
        c={'kind':'circle','radius':5,'center':[2,3]}
        self.assertEqual([3,2],catalog._swap_contour_axes(c)['center'])
        self.assertEqual([2,3],c['center'])

if __name__=='__main__':unittest.main()
