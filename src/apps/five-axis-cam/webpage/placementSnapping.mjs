// Product defaults share the viewport's rendered grid; exact typed values are
// never quantized. Holding Alt during a gesture temporarily releases snapping.
export function placementSnapOptions(viewport,reference){
  const grid=viewport.getGridState();
  return {translation:{reference,spacing:grid.spacing,origin:grid.origin,gridEnabled:grid.visible,tolerancePx:8},rotation:{step:Math.PI/12,tolerance:Math.PI/60}};
}

export function updatePlacementSnapFeedback(context,snap){
  const node=context.mount.querySelector('[data-five-placement-snap]');if(!node)return;
  node.hidden=!snap;
  node.textContent=!snap?'':snap.kind==='origin'?'原点':snap.kind==='angle'?`${Math.round(snap.angle*180/Math.PI*100)/100}°`:`网格 ${snap.spacing} mm`;
}
