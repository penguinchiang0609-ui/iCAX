"""Place installation hole groups on the actual side uprights, without machining."""
from __future__ import annotations

import math

EPS = 1.e-7


def _number(parameters, key):
    value = parameters[key]
    if isinstance(value, bool) or not isinstance(value, (int, float)) \
            or not math.isfinite(value) or value < 0:
        raise ValueError("安装孔参数 " + key + " 必须为非负有限数值")
    return float(value)


def plan(parameters, members):
    enabled = parameters.get('installHoleEnabled', 'no')
    if enabled not in ('yes', 'no'):
        raise ValueError('安装孔开关无效')
    if enabled == 'no':
        return dict(enabled=False, countPerSide=0, centers=[], groups=[])
    count = _number(parameters, 'installHoleCountPerSide')
    if count != int(count) or int(count) not in (2, 3, 4, 5):
        raise ValueError('安装孔每侧组数只能选择 2、3、4、5')
    count = int(count)
    height = _number(parameters, 'height')
    top = _number(parameters, 'installHoleTopOffset')
    bottom = _number(parameters, 'installHoleBottomOffset')
    span = height-top-bottom
    if span <= EPS:
        raise ValueError('安装孔上下端距之和必须小于成品高度')
    centers = [bottom + span*i/(count-1) for i in range(count)]
    groups = []
    for side, label in (('left', '左'), ('right', '右')):
        uprights = [m for m in members if m['role'] == 'frame.'+side]
        for index, z in enumerate(centers, 1):
            hosts = [m for m in uprights if m['start'][2]-EPS <= z <= m['end'][2]+EPS]
            if len(hosts) != 1:
                raise ValueError(f'{label}侧第 {index} 组安装孔落在把手开槽缺口或无实体处，请调整组数或上下端距')
            host = hosts[0]
            groups.append(dict(side=side, index=index, memberKey=host['key'],
                               station=z-host['start'][2], point=[host['start'][0], 0., z]))
    return dict(enabled=True, countPerSide=count, centers=centers, groups=groups)
