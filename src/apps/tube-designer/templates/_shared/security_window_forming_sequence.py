"""Choose a sampled, rigid-body-clear order for a closed spatial sharp-V frame.

This checks non-adjacent intact tube envelopes. Adjacent notch/deformation
regions and forming-machine fixtures are outside this check. The caller must
provide intervals bounded by the resolved cutters, not nominal member lengths.
"""
import math

_IDENTITY = (1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.)
_ANGLE_STEP = .5
_CONTACT_TOLERANCE = 1.e-6
_NEAR_CLOSURE = (89.75, 89.9, 89.95, 89.98, 89.99, 89.995, 89.999)


def _dot(a, b):
    return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]


def _cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def _point(matrix, value):
    return tuple(sum(matrix[4*i+j]*value[j] for j in range(3))+matrix[4*i+3] for i in range(3))


def _multiply(a, b):
    return tuple(sum(a[4*i+k]*b[4*k+j] for k in range(4)) for i in range(4) for j in range(4))


def _rotate(value, axis, cosine, sine):
    crossed = _cross(axis, value)
    along = _dot(axis, value)*(1.-cosine)
    return tuple(cosine*value[i]+sine*crossed[i]+along*axis[i] for i in range(3))


def _box(matrix, interval, width, depth):
    center = _point(matrix, ((interval[0]+interval[1])/2., 0., 0.))
    axes = tuple(tuple(matrix[4*i+j] for i in range(3)) for j in range(3))
    half = ((interval[1]-interval[0])/2., width/2., depth/2.)
    return _bounds(center, axes, half)


def _bounds(center, axes, half):
    radii = tuple(sum(half[j]*abs(axes[j][i]) for j in range(3)) for i in range(3))
    return center, axes, half, radii


def _rotated_box(box, axis, hinge, cosine, sine):
    center, axes, half, _ = box
    relative = tuple(center[i]-hinge[i] for i in range(3))
    rotated = _rotate(relative, axis, cosine, sine)
    return _bounds(tuple(rotated[i]+hinge[i] for i in range(3)),
                   tuple(_rotate(value, axis, cosine, sine) for value in axes), half)


def _overlap(a, b):
    """SAT with early separation; contact is allowed, positive volume is not."""
    ca, aa, ha, ra = a
    cb, ab, hb, rb = b
    delta = tuple(cb[i]-ca[i] for i in range(3))
    if any(ra[i]+rb[i]-abs(delta[i]) <= _CONTACT_TOLERANCE for i in range(3)):
        return False
    for axis in (*aa, *ab, *(_cross(u, v) for u in aa for v in ab)):
        norm_squared = _dot(axis, axis)
        if norm_squared < 1.e-18:
            continue
        radii = sum(half*abs(_dot(axis, basis)) for half, basis in zip(ha, aa))
        radii += sum(half*abs(_dot(axis, basis)) for half, basis in zip(hb, ab))
        if radii-abs(_dot(axis, delta)) <= _CONTACT_TOLERANCE*math.sqrt(norm_squared):
            return False
    return True


def _numbers(values, count, name):
    if (not isinstance(values, (list, tuple)) or len(values) != count
            or any(isinstance(value, bool) or not isinstance(value, (int, float))
                   or not math.isfinite(value) for value in values)):
        raise ValueError('空间折弯次序需要有限的实际'+name)
    return tuple(float(value) for value in values)


def _inputs(spans, folds, profile, closed):
    if closed is not True:
        raise ValueError('空间闭框折弯次序要求明确的闭合路径')
    ordered = sorted(folds, key=lambda value: value['station'])
    if not 2 <= len(ordered) <= 8 or len(spans) != len(ordered)+1:
        raise ValueError('空间闭框刚性管段必须与实际折弯逐一对应')
    width, depth = _numbers((profile['width'], profile['depth']), 2, '管材截面')
    if min(width, depth) <= 0:
        raise ValueError('空间折弯管材截面必须为正')
    intervals = [_numbers(span['interval'], 2, '管段区间') for span in spans]
    if any(a >= b for a, b in intervals) or any(a[1] >= b[0] for a, b in zip(intervals, intervals[1:])):
        raise ValueError('空间折弯刚性区间必须有序且避开局部槽口变形区')
    identities = set()
    for index, fold in enumerate(ordered):
        identity = fold['instanceId']
        if not isinstance(identity, str) or not identity or identity in identities:
            raise ValueError('空间折弯工艺身份必须非空且唯一')
        identities.add(identity)
        station, angle = _numbers((fold['station'], fold['angle']), 2, '折弯位置和角度')
        if not intervals[index][1] < station < intervals[index+1][0] or abs(angle-90.) > 1.e-6:
            raise ValueError('空间折弯必须使用刚性管段之间的实际90°铰点')
        axis = _numbers(fold['axis'], 3, '折弯轴')
        hinge = _numbers(fold['hingePoint'], 3, '铰点')
        _numbers(fold['targetTransform'], 16, '目标变换')
        if abs(_dot(axis, axis)-1.) > 1.e-6 or abs(hinge[0]-station) > 1.e-6:
            raise ValueError('空间折弯轴必须归一化，铰点必须落在实际加工位置')
    return ordered, intervals, width, depth


def _check_order(ordered, intervals, width, depth, candidate):
    state = 0
    tested_angles = 0
    pairs_tested = 0
    angles = sorted({step*_ANGLE_STEP for step in range(round(90./_ANGLE_STEP)+1)} | set(_NEAR_CLOSURE))
    trig = [(angle, math.cos(math.radians(angle)), math.sin(math.radians(angle))) for angle in angles]
    for index in candidate:
        transform, poses = _IDENTITY, []
        for segment in range(len(intervals)):
            poses.append(transform)
            if segment < len(ordered) and state & (1 << segment):
                transform = _multiply(transform, ordered[segment]['targetTransform'])
        fold = ordered[index]
        hinge = _point(poses[index], fold['hingePoint'])
        axis = tuple(sum(poses[index][4*i+j]*fold['axis'][j] for j in range(3)) for i in range(3))
        boxes = [_box(pose, interval, width, depth) for pose, interval in zip(poses, intervals)]
        # Prefix and suffix are each rigid. Only cross-boundary pairs can gain
        # overlap in this transition; preceding transitions validated the rest.
        pairs = [(a, b) for a in range(index+1)
                 for b in range(max(a+2, index+1), len(boxes))]
        for angle, cosine, sine in trig:
            trial = [box if segment <= index else _rotated_box(box, axis, hinge, cosine, sine)
                     for segment, box in enumerate(boxes)]
            tested_angles += 1
            for a, b in pairs:
                pairs_tested += 1
                if _overlap(trial[a], trial[b]):
                    return {'instanceId': fold['instanceId'], 'angleDegrees': angle,
                            'spanIndices': [a, b]}, tested_angles, pairs_tested
        state |= 1 << index
    return None, tested_angles, pairs_tested


def choose_forming_order(spans, folds, profile, closed):
    """Return instance identities and an explicitly limited sampled motion check.

    All inputs are read-only. The preferred order postpones the second bend,
    so the first/last seam does not sweep through itself at the final bend.
    A finite set of alternate orders is checked, rather than claiming a full
    continuous-motion proof or exhaustive manufacturability search.
    """
    ordered, intervals, width, depth = _inputs(spans, folds, profile, closed)
    reverse = list(reversed(range(len(ordered))))
    candidates = [reverse[:-2]+[reverse[-1], reverse[-2]], reverse,
                  list(range(len(ordered)))]
    for last in range(len(ordered)):
        candidates.append([index for index in reverse if index != last]+[last])
    unique, attempts = set(), []
    for candidate in candidates:
        key = tuple(candidate)
        if key in unique:
            continue
        unique.add(key)
        collision, count, pairs = _check_order(ordered, intervals, width, depth, candidate)
        identities = [ordered[index]['instanceId'] for index in candidate]
        attempts.append({'order': identities, 'firstEnvelopeOverlap': collision})
        if collision is None:
            return {'order': identities, 'motionCheck': {
                'status': 'pass', 'method': 'sampled-nonadjacent-rigid-envelopes',
                'angleStepDegrees': _ANGLE_STEP, 'nearClosureAnglesDegrees': list(_NEAR_CLOSURE),
                'contactToleranceMm': _CONTACT_TOLERANCE, 'sampleCount': count,
                'pairCheckCount': pairs, 'includesClosureSeam': True,
                'rigidIntervals': [list(interval) for interval in intervals],
                'candidateAttempts': attempts,
                'limitations': ['angle sampling is not a continuous swept-volume proof',
                    'local notch deformation and forming-machine fixtures are not evaluated']}}
    collision = attempts[0]['firstEnvelopeOverlap']
    raise ValueError('空间连续折弯未找到无非相邻管段干涉的采样次序: '
                     +collision['instanceId']+' / '+str(collision['angleDegrees'])+'°')
