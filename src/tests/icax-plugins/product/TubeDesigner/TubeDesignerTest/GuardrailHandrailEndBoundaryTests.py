"""The cap terminates outside the end posts in every elevation mode."""
import importlib.util,json,pathlib,sys,unittest
ROOT=pathlib.Path(__file__).resolve().parents[5]
sys.path.insert(0,str(ROOT/'iCAX-Engine/framework/TemplateRuntime/python'))
TEMPLATES=ROOT/'apps/tube-designer/templates/product'
class GuardrailHandrailEndBoundary(unittest.TestCase):
 def test_end_planes(self):
  for folder in sorted(TEMPLATES.glob('modular_guardrail*')):
   if not (folder/'template.json').is_file():continue
   raw=json.loads((folder/'template.json').read_text(encoding='utf-8'))
   spec=importlib.util.spec_from_file_location(folder.name,folder/'template.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
   defaults={f['key']:f['defaultValue'] for f in raw['parameters']}
   for mode,construction in [('level','per_bay'),('continuous','per_bay'),('continuous','continuous'),('stepped','per_bay')]:
    for extension in [0,30]:
     with self.subTest(template=raw['id'],mode=mode,handrail=construction,extension=extension):
      values={**defaults,'pathMode':mode,'handrailMode':construction,'slopeAngle':0,'startExtension':extension,'finishExtension':extension}
      before=json.loads(json.dumps(values));layout=module.build_layout(values);self.assertEqual(values,before)
      caps=[tube for tube in layout.tubes if tube.category=='guardrail.handrail']
      left,right=layout.bays[0].left,layout.bays[-1].right
      # Elevation stock carries 0.01 mm machining allowance; keep volumes
      # trim it to the declared endpoint planes in the final BRep.
      allowance=.01 if mode!='level' and construction!='continuous' else 0
      self.assertAlmostEqual(min(t.start[0] for t in caps)+allowance,left.point[0]-left.half_extent((1,0,0))-extension,places=4)
      self.assertAlmostEqual(max(t.end[0] for t in caps)-allowance,right.point[0]+right.half_extent((1,0,0))+extension,places=4)
      if mode!='level':self.assertTrue(all(not tube.clips for tube in caps),'caps rest above normal posts')
if __name__=='__main__':unittest.main()
