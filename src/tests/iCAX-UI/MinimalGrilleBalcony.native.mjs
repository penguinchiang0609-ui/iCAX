// The same real SDO/solid/persistence bridge as five-face acceptance, with
// balcony-specific source and geometry assertions. No native responses mocked.
import { mkdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const output = resolve(process.env.ICAX_GRILLE_REPORT || resolve(root,
  'output/tests/minimal-grille-balcony-20261110/native-acceptance'));
mkdirSync(output, { recursive: true });
const base = { layoutStyle: 'balcony_closure', width: 1200, height: 600, handleEnabled: 'no',
  supportStyle: 'single', frameProfileType: 'rect', innerProfileType: 'rect', supportProfileType: 'rect',
  frameWidth: 10, frameDepth: 5, innerWidth: 10, innerDepth: 5, supportWidth: 20, supportDepth: 10,
  barLayoutMode: 'fixed_count', barCount: 3, balconyTopDepth: 120, balconyBottomDepth: 220,
  balconyTopDrop: 0, supportMaxSpacing: 800, jointMode: 'butt_weld', returnJointMode: 'butt_weld' };
const complete = (id, parameters, options = {}) => ({ id, parameters: { ...base, ...parameters },
  create: true, disassemble: true, reopen: true, ...options });
const configuration = {
  cases: [
    complete('balcony-bare', {}, { captureTubeGeometry: true }),
    complete('balcony-asymmetric-round-support', { balconyTopDepth: 250, balconyBottomDepth: 150,
      balconyTopDrop: 30, supportProfileType: 'round', supportWidth: 20 },
      { expectTubeGeometryDifferentFrom: 'balcony-bare' }),
    complete('balcony-front-mounting-holes', { frameWidth: 30, frameDepth: 20,
      balconyTopDepth: 160, balconyBottomDepth: 240, balconyTopDrop: 20,
      installHoleEnabled: 'yes', installHoleCountPerSide: 2, installHoleTopOffset: 70, installHoleBottomOffset: 80 }),
    { id: 'balcony-open-slot-installation-datum', parameters: { ...base, height: 1250,
      handleEnabled: 'yes', handleSide: 'left', handleClosure: 'open_slot', barLayoutMode: 'maximum_gap',
      installHoleEnabled: 'yes', installHoleCountPerSide: 2, installHoleTopOffset: 120, installHoleBottomOffset: 900 },
      annotationExpectations: [
        { parameter: 'installHoleTopOffset', startZ: 1250, endZ: 1130 },
        { parameter: 'installHoleBottomOffset', startZ: 0, endZ: 900 },
      ] },
  ],
  batches: [],
};
const path = resolve(output, 'cases.json');
writeFileSync(path, JSON.stringify(configuration, null, 2));
process.env.ICAX_GRILLE_REPORT = output;
process.env.ICAX_GRILLE_CASES = path;
await import('./MinimalGrilleFiveFace.native.mjs');
