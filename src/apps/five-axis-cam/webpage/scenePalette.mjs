// Scene roles stay stable across cut definitions, generated paths and pose edits.
// XYZ handles keep the shared viewport's conventional axis colors.
export const sceneColors=Object.freeze({
  fixture:0xd6a064,
  cutting:0x55b9e7,
  simulationCut:0xff3030,
  simulationRapid:0xff3030,
  collision:0xff3030,
  original:0xffffff,
  leadIn:0x15a66b,
  leadOut:0xff862b,
  toolAxis:0xc5a3ff,
  selected:0xe83eff,
  reference:0xa6b7c2,
  rapidUnverified:0xff862b,
  stale:0x70818a,
  error:0xe65b59,
  label:0x253e49,
  labelText:0xf4f7fa
});
export const sceneColorCss=color=>`#${color.toString(16).padStart(6,'0')}`;
