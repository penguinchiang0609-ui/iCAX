const point=value=>Array.isArray(value)&&value.length===3&&value.every(Number.isFinite);

// A failed check can still supply an editable candidate. Visibility depends
// on usable setup-space geometry, independently of collision qualification.
export function hasRapidGeometry(source){
 return !!source&&(['Generated','Draft','Failed'].includes(source.geometryStatus)||source.geometryStatus==='Stale'&&source.editingOnly===true&&!!source.manualPath)
  &&source.frame==='Setup'&&(!source.referencePoint||source.referencePoint==='ToolTip')
  &&Array.isArray(source.points)&&source.points.length>=2&&source.points.every(point)
  &&Array.isArray(source.directions)&&source.directions.length===source.points.length
  &&source.directions.every(value=>point(value)&&Math.hypot(...value)>1e-10);
}
