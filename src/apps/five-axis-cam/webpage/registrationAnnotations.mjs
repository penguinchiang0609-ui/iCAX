import { activeTool, currentDraft } from './workspaceModel.mjs';
import { selectedPlacement } from './registrationActions.mjs';
import {placementWorldMatrix} from './setupWorldFrame.mjs';

// Transient selection annotations only. Scene placement remains authoritative.
export function updateRegistrationAnnotations(view) {
  const viewport = view.viewport, tool = activeTool(view), placement = selectedPlacement(view);
  if (!viewport) return;
  if (!view.fiveAxisRegistrationOpen || !placement || !['rough', 'fine'].includes(tool)) {
    if (view.fiveRegistrationAnnotations) viewport.setSpecificationAnnotations([]);
    view.fiveRegistrationAnnotations = false; return;
  }
  const d = currentDraft(view, tool), matrix = placementWorldMatrix(view,placement).transpose().toArray();
  const read = prefix => {
    const values = [...'XYZ'].map(axis => d[`${prefix}${axis}`]);
    return values.every(v => String(v).trim() && Number.isFinite(Number(v))) ? values.map(Number) : null;
  };
  const world = p => [0, 1, 2].map(i => matrix[i*4] * p[0] + matrix[i*4+1] * p[1] + matrix[i*4+2] * p[2] + matrix[i*4+3]);
  const sub = (a,b) => a.map((v,i) => v-b[i]);
  const cross = (a,b) => [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
  const unit = a => { const n = Math.hypot(...a); return n > 1e-9 ? a.map(v=>v/n) : null; };
  const annotations = [];
  const mark = (p,label) => { if(p) annotations.push({ id:`registration-${label}`,kind:'parameter',start:world(p),end:world(p),label:`◇ ${label}`,color:0x418ae5 }); };
  if (tool === 'fine') [1,2,3].forEach(n=>mark(read(`model${n}`),`P${n}`));
  else {
    const o = read('origin'), a = read('axisX'), b = read('plane');
    mark(o,'O');
    if (d.roughMethod !== 'line') mark(a,'OX');
    if (d.roughMethod === 'point') mark(b,'OXY');
    if (o && a && b) {
      let x = unit(d.roughMethod === 'line' ? a : sub(a,o));
      let z = x && unit(d.roughMethod === 'face' ? b : cross(x,d.roughMethod === 'line' ? b : sub(b,o)));
      const y = x && z && unit(cross(z,x));
      if (y) { x=unit(cross(y,z)); const length=Math.max(10, d.roughMethod==='line'?30:Math.hypot(...sub(a,o)));
        [x,y,z].forEach((axis,i)=>annotations.push({id:`registration-axis-${i}`,start:world(o),end:world(o.map((v,j)=>v+axis[j]*length)),label:'XYZ'[i],color:[0xe95e64,0x52b76d,0x5286eb][i]})); }
    }
  }
  viewport.setSpecificationAnnotations(annotations); view.fiveRegistrationAnnotations = true;
}
