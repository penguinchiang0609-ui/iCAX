import { Matrix4, Vector4 } from '../ThirdParty/three/three.module.js';

// Face ranges refer to the original indexed display mesh. Clip filled triangles,
// rather than just their edges, so a rectangle inside a large face also hits it.
export function meshFacesMatchingRectangle(geometry, faces, worldMatrix, camera, viewport, rectangle) {
  if (viewport.width <= 0 || viewport.height <= 0) return [];
  const left=(rectangle.left-viewport.left)/viewport.width*2-1, right=(rectangle.right-viewport.left)/viewport.width*2-1;
  const top=1-(rectangle.top-viewport.top)/viewport.height*2, bottom=1-(rectangle.bottom-viewport.top)/viewport.height*2;
  const planes=[p=>p.x-left*p.w,p=>right*p.w-p.x,p=>p.y-bottom*p.w,p=>top*p.w-p.y,p=>p.z+p.w,p=>p.w-p.z,p=>p.w-1e-9];
  const matrix=new Matrix4().multiplyMatrices(camera.projectionMatrix,camera.matrixWorldInverse).multiply(worldMatrix);
  const position=geometry.getAttribute('position'), index=geometry.index, vertices=new Map();
  const vertex=n=>{
    const id=index?index.getX(n):n;
    if(!vertices.has(id))vertices.set(id,new Vector4(position.getX(id),position.getY(id),position.getZ(id),1).applyMatrix4(matrix));
    return vertices.get(id);
  };
  const intersects=triangle=>{
    let polygon=triangle;
    for(const distance of planes){
      const clipped=[];
      for(let i=0;i<polygon.length;i++){
        const a=polygon[i],b=polygon[(i+1)%polygon.length],da=distance(a),db=distance(b);
        if(da>=0)clipped.push(a);
        if((da>=0)!==(db>=0))clipped.push(a.clone().lerp(b,da/(da-db)));
      }
      if(!clipped.length)return false;
      polygon=clipped;
    }
    return true;
  };
  return faces.filter(face=>{
    let any=false;
    for(const range of face.triangleRanges)for(let t=Number(range.first);t<Number(range.first)+Number(range.count);t++){
      const triangle=[vertex(t*3),vertex(t*3+1),vertex(t*3+2)];any=true;
      if(rectangle.mode==='contain'){
        if(!triangle.every(p=>planes.every(distance=>distance(p)>=0)))return false;
      }else if(intersects(triangle))return true;
    }
    return rectangle.mode==='contain'&&any;
  });
}

// Clip against the selection volume in homogeneous coordinates. This also
// rejects curves behind the camera and handles segments crossing the near plane.
export function polylineMatchesRectangle(points, worldMatrix, camera, viewport, rectangle) {
  if (points.length < 2 || viewport.width <= 0 || viewport.height <= 0) return false;
  const left = (rectangle.left - viewport.left) / viewport.width * 2 - 1;
  const right = (rectangle.right - viewport.left) / viewport.width * 2 - 1;
  const top = 1 - (rectangle.top - viewport.top) / viewport.height * 2;
  const bottom = 1 - (rectangle.bottom - viewport.top) / viewport.height * 2;
  const matrix = new Matrix4().multiplyMatrices(camera.projectionMatrix, camera.matrixWorldInverse).multiply(worldMatrix);
  const clip = points.map(p => new Vector4(p[0], p[1], p[2], 1).applyMatrix4(matrix));
  const planes = [p=>p.x-left*p.w, p=>right*p.w-p.x, p=>p.y-bottom*p.w,
    p=>top*p.w-p.y, p=>p.z+p.w, p=>p.w-p.z, p=>p.w-1e-9];
  if (rectangle.mode === 'contain') return clip.every(p => planes.every(distance => distance(p) >= 0));
  for (let i=1; i<clip.length; ++i) {
    let enter=0, leave=1;
    for (const distance of planes) {
      const a=distance(clip[i-1]), b=distance(clip[i]);
      if (a<0 && b<0) { leave=-1; break; }
      if (a<0) enter=Math.max(enter,a/(a-b));
      else if (b<0) leave=Math.min(leave,a/(a-b));
      if (enter>leave) break;
    }
    if (enter<=leave) return true;
  }
  return false;
}
