import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';

const point = value => {
  if (!Array.isArray(value) || value.length !== 3 || !value.every(Number.isFinite)) throw Error('刀路坐标无效，请重新生成。');
  return new THREE.Vector3(...value);
};
const direction = value => {
  const v = point(value);
  if (v.lengthSq() < 1e-18) throw Error('刀路方向无效，请重新生成。');
  return v.normalize();
};

// Generated cuts contain beam targets in workpiece coordinates. Planned rapids
// already contain the nozzle TCP in setup coordinates. Never offset them twice.
// Preview uses distance and angle, not a claimed machine cycle time.
export function buildPlayback(setup, placements) {
  const generated = setup?.generatedGeometry;
  if (generated?.status !== 'Current' && !generated?.partialDisplay || !generated?.operations?.length) return { frames: [], reason: generated?.status==='Disabled'?'工件已禁用，请先启用工件。':generated?.status === 'Stale' ? '刀路已过期，请重新生成。' : '请先生成刀路。' };
  const frames = [], height = Number(setup.rapidSettings.followHeightMm);
  let failedConnections = 0;
  if (!Number.isFinite(height) || height < 0) throw Error('跟随高度无效。');
  let previousDirection = new THREE.Vector3(0,0,1), rotation = new THREE.Quaternion(),seededFirst=false;
  function append(points, directions, matrix, tcp, info, breakBefore = false) {
    if (!points?.length || points.length !== directions?.length) throw Error('刀路缺少姿态，请重新生成。');
    points.forEach((p, i) => {
      const axis = direction(directions[i]).transformDirection(matrix);
      const position = point(p).applyMatrix4(matrix);
      const target = tcp ? null : position.clone();
      if (!tcp) position.addScaledVector(axis, height);
      if(!(seededFirst&&previousDirection.distanceToSquared(axis)<1e-28))rotation = new THREE.Quaternion().setFromUnitVectors(previousDirection, axis).multiply(rotation).normalize();
      seededFirst=false;
      const previous = frames.at(-1), distance = previous ? position.distanceTo(previous.position) : 0;
      const disconnected = !!previous && i === 0 && (breakBefore || distance > 1e-4);
      const duration = previous && !disconnected ? Math.max(distance / 50, rotation.angleTo(previous.rotation), .001) : 0;
      frames.push({ ...info, position, target, direction: axis, rotation: rotation.clone(), breakBefore: disconnected, duration });
      previousDirection = axis;
    });
  }
  generated.operations.forEach((op, index) => {
    const placement = placements.find(p => p.id === op.placementId);
    if (!placement || placement.transform?.length !== 16) throw Error('刀路装夹坐标缺失。');
    const matrix = new THREE.Matrix4().set(...placement.transform);
    const seed=op.simulationStartFrame;if(seed&&JSON.stringify(seed.placementTransform)===JSON.stringify(placement.transform)){
      previousDirection=point(seed.direction);rotation=new THREE.Quaternion(...seed.rotation);seededFirst=true;
    }
    for (const part of op.parts) append(part.points, part.directions, matrix, false, { intentId: op.intentId, order: op.order, role: part.role, laser: true });
    if(frames.length)frames.at(-1).laser=false; // native operation End event
    const next = generated.operations[index + 1];
    if (!next) return;
    const rapid = generated.transitions.find(t => t.from === op.intentId && t.to === next.intentId);
    // A direct unplanned connector is not a trajectory. Do not animate it.
    if ((['Generated','Draft'].includes(rapid?.geometryStatus)||generated.partialDisplay&&rapid?.geometryStatus==='Stale') && rapid.frame === 'Setup' && rapid.referencePoint === 'ToolTip' && rapid.points?.length > 1 && rapid.directions?.length === rapid.points.length)
      append(rapid.points, rapid.directions, new THREE.Matrix4(), true, { intentId: next.intentId, from:op.intentId, to:next.intentId, order: `${op.order} → ${next.order}`, role: 'Rapid', laser: false, validation: rapid.validation });
    else if (op.parts.length) {
      frames.at(-1).missingRapid = true;
      const failed=rapid?.geometryStatus==='Failed';if(failed)failedConnections++;
      frames.at(-1).rapidFailure = failed?'此段空移未通过避障检查。请到“空移”页面编辑橙红色候选路径或调整刀路姿态，再重新检查。':'此处空移尚未生成，请先生成空移。';
    }
  });
  for (let i=1; i<frames.length; ++i) if (frames[i-1].missingRapid) frames[i].breakBefore = true;
  const blocked=frames.filter(f=>f.missingRapid).length;
  return { frames, reason: blocked ? failedConnections?`${blocked} 段空移不能播放。请到“空移”页面编辑候选路径或重新生成，再检查。`:`${blocked} 段空移尚未生成，请先生成空移。` : '' };
}

export function playbackPose(frames, cursor) {
  if (!frames.length) return null;
  const index = Math.max(0, Math.min(frames.length-1, Math.floor(cursor))), a=frames[index], b=frames[index+1];
  const fraction = b && !b.breakBefore ? cursor-index : 0;
  return { ...a, position: a.position.clone().lerp(b?.position ?? a.position, fraction),
    rotation: a.rotation.clone().slerp(b?.rotation ?? a.rotation, fraction) };
}

export function advancePlayback(frames, cursor, seconds) {
  let value = cursor;
  while (value < frames.length-1 && seconds > 0) {
    const index = Math.floor(value), next = frames[index+1];
    if (next.breakBefore) return { cursor: index, stopped: true, reason: frames[index].rapidFailure || '此处空移尚未生成或不连续，请先重新规划。' };
    const end=index+1,remaining = (end-value)*next.duration;
    if (seconds < remaining) { value += seconds/next.duration; break; }
    seconds -= remaining; value = end;
  }
  return { cursor: value, stopped: value >= frames.length-1, reason: '' };
}
