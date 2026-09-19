import importlib.util
from pathlib import Path
import unittest
import json

ROOT=next(p for p in Path(__file__).resolve().parents if (p/'src/apps').is_dir())
spec=importlib.util.spec_from_file_location('family_runtime',ROOT/'src/apps/tube-designer/templates/_shared/profile_package_runtime.py')
runtime=importlib.util.module_from_spec(spec);spec.loader.exec_module(runtime)
STAGE=ROOT/'artifacts/profile-family-stage-20260913/profile'
CATALOG=STAGE if STAGE.exists() else ROOT/'src/apps/tube-designer/templates/profile'

def evaluate_model(model,**values):
    mapping=json.loads((ROOT/'doc/Apps/管型模板归并映射_2026-09-13.json').read_text(encoding='utf-8'))['oldToFamily']
    family=model if (CATALOG/model/'profile.json').is_file() else mapping.get(model,model)
    package=runtime._system_package(CATALOG/family,preview=False)
    d=package['descriptor']
    with runtime._script_context(package['scriptSource'],package['packageDigest'],package['resources'],d) as ns:
        maps=ns.get('PARAMETERS',{})
        if model not in maps:
            return runtime._evaluate(d,package['scriptSource'],values,package['packageDigest'],package['sourceFileName'],package['resources'])
        parameters={maps[model].get(k,k):v for k,v in values.items()}
        if any(p['key']=='sectionModel' for p in d['parameters']):parameters['sectionModel']=model
        if family=='angle':parameters['manufacturingRoute']={'angle-cold-unequal':'ColdBent','angle-hot-unequal-thickness':'HotRolled','angle-precision':'Precision','angle-welded':'Welded'}[model]
    return runtime._evaluate(d,package['scriptSource'],parameters,package['packageDigest'],package['sourceFileName'],package['resources'])

class ProfileFamilyTests(unittest.TestCase):
    def test_all_families_and_models_evaluate(self):
        packages=runtime._list_system_packages(CATALOG)
        self.assertEqual(21,len(packages))
        for package in packages:
            self.assertNotIn('error',package)
            d=package['descriptor']
            selectors=[p for p in d['parameters'] if p['key'] in ('sectionModel','manufacturingRoute')]
            options=selectors[0]['options'] if selectors else [None]
            for option in options:
                with self.subTest(family=d['id'],model=option):
                    values={} if option is None else {selectors[0]['key']:option['value']}
                    result=runtime._evaluate(d,package['scriptSource'],values,package['packageDigest'],package['sourceFileName'],package['resources'])
                    self.assertGreater(result['width'],0)
                    self.assertGreater(result['depth'],0)
                    self.assertTrue(result['contours'])

if __name__=='__main__':unittest.main()
