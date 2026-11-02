export function sameToolAxisAnchor(a,b,tolerance=1e-10){
  if(!a||!b)return false;
  if(a.port||b.port)return !!a.port&&!!b.port&&a.port.operationId===b.port.operationId&&a.port.portId===b.port.portId&&a.segmentId===b.segmentId&&(a.occurrence??0)===(b.occurrence??0)&&Number.isFinite(a.parameter)&&Number.isFinite(b.parameter)&&Math.abs(a.parameter-b.parameter)<=tolerance*Math.max(1,Math.abs(a.parameter),Math.abs(b.parameter));
  return typeof a.sourceSpanId==='string'&&a.sourceSpanId===b.sourceSpanId&&Number.isFinite(a.nativeParameter)&&Number.isFinite(b.nativeParameter)&&Math.abs(a.nativeParameter-b.nativeParameter)<=tolerance*Math.max(1,Math.abs(a.nativeParameter),Math.abs(b.nativeParameter));
}
