"""Face-local escape-window anchors, clear margins and shared frame boundaries."""
import math


POSITIONS = {f'{x}_{z}' for x in ('left','center','right') for z in ('bottom','middle','top')}


def select_cells(lines, count, alignment, label):
    if isinstance(count,bool) or not isinstance(count,int) or count < 1:
        raise ValueError(f'{label}占格数必须为正整数')
    cells=len(lines)-1
    if count > cells:
        raise ValueError(f'{label}占格数 {count} 超过可用 {cells} 格，请减小占格数或调整主格栅')
    if alignment in ('left','bottom'):start=0
    elif alignment in ('right','top'):start=cells-count
    else:
        candidates=range(1,cells-count)
        if not candidates:
            raise ValueError(f'{label}中部位置两侧须保留格栅，请减小占格数')
        center=(lines[0]+lines[-1])/2
        start=min(candidates,key=lambda i:(round(abs((lines[i]+lines[i+count])/2-center),7),i))
    return start,start+count


def plan(values, *, frame, fixed, leaf, horizontal_positions, vertical_positions):
    position=values.get('doorPosition','center_middle')
    if position not in POSITIONS:raise ValueError('逃生窗位置须为九宫格中的一项')
    x_alignment,z_alignment=position.split('_')
    width,height=float(values['width']),float(values['height'])
    layout=values['frameLayout']; side=layout!='top_bottom'; ends=layout!='left_right'
    x0,x1=(frame.width/2,width-frame.width/2) if side else (fixed.width/2,width-fixed.width/2)
    z0,z1=(frame.width/2,height-frame.width/2) if ends else (fixed.width/2,height-fixed.width/2)
    xs=[x0,*[x for x in vertical_positions if x0+1.e-7<x<x1-1.e-7],x1]
    zs=[z0,*[z for z in horizontal_positions if z0+1.e-7<z<z1-1.e-7],z1]
    ix,jx=select_cells(xs,values.get('doorColumnCount',6),x_alignment,'横向')
    iz,jz=select_cells(zs,values.get('doorRowCount',2),z_alignment,'竖向')
    shared={'left':side and ix==0,'right':side and jx==len(xs)-1,
            'bottom':ends and iz==0,'top':ends and jz==len(zs)-1}
    bounds={'left':xs[ix],'right':xs[jx],'bottom':zs[iz],'top':zs[jz]}
    size=_finish(values,bounds,shared,xs,zs,horizontal_positions,vertical_positions,frame,fixed,leaf)
    result=plan_single(values,frame=frame,fixed=fixed,leaf=leaf,
        clear_size=(size['fixedClearWidth'],size['fixedClearHeight']),
        horizontal_positions=horizontal_positions,vertical_positions=vertical_positions)
    result.update(layout='grid',sizing='cells',columnRange=[ix,jx],rowRange=[iz,jz],columnLines=xs,rowLines=zs)
    return result


def plan_single(values, *, frame, fixed, leaf, clear_size=None,
                    horizontal_positions=None, vertical_positions=None):
    """Position any single-face infill using the common clear-margin contract."""
    position=values['doorPosition']
    if position not in POSITIONS:raise ValueError('逃生窗位置须为九宫格中的一项')
    w,h=float(values['width']),float(values['height'])
    side=values['frameLayout']!='top_bottom';ends=values['frameLayout']!='left_right'
    x0,x1=(frame.width/2,w-frame.width/2) if side else (fixed.width/2,w-fixed.width/2)
    z0,z1=(frame.width/2,h-frame.width/2) if ends else (fixed.width/2,h-fixed.width/2)
    cw,ch=clear_size or (float(values['doorClearWidth'])+leaf.depth,float(values['doorClearHeight']))
    inner={'left':frame.width if side else fixed.width,'right':w-frame.width if side else w-fixed.width,
           'bottom':frame.width if ends else fixed.width,'top':h-frame.width if ends else h-fixed.width}
    result=position_clear(values,inner=inner,side_widths={k:frame.width for k in inner},
        available_sides={k:side if k in ('left','right') else ends for k in inner},
        fixed=fixed,leaf=leaf,clear_size=(cw,ch))
    l,r=(result['boundaryAxes'][k] for k in ('left','right'))
    b,t=(result['boundaryAxes'][k] for k in ('bottom','top'))
    if not x0-1.e-7<=l<r<=x1+1.e-7 or not z0-1.e-7<=b<t<=z1+1.e-7:
        raise ValueError('逃生窗净口及框料超出产品边界')
    pattern=values.get('infillPattern','grid')
    rows=[z0,z1]
    if pattern=='horizontal':
        maximum=float(values['horizontalMaximumCenterSpacing'])
        if not math.isfinite(maximum) or maximum<=0:raise ValueError('横杆最大中心距必须大于零')
        limits=sorted(set((z0,b,t,z1)));rows=[]
        for a,end in zip(limits,limits[1:]):
            count=max(1,math.ceil((end-a)/maximum-1.e-9))
            rows.extend(a+(end-a)*i/count for i in range(count))
        rows.append(z1)
        if len(rows)>102:raise ValueError('按最大中心距计算超过100根横杆')
        horizontal_positions=rows[1:-1]
    result.update(layout=pattern,columnLines=[x0,*(vertical_positions or []),x1],rowLines=rows)
    _infill_axes(result,horizontal_positions or [],vertical_positions or [],leaf)
    return result


def position_clear(values, *, inner, side_widths, available_sides, fixed, leaf, clear_size):
    """Solve in the selected face's U/V plane; never mutate public parameters."""
    position=values['doorPosition']
    if position not in POSITIONS:raise ValueError('逃生窗位置须为九宫格中的一项')
    x,z=position.split('_');cw,ch=clear_size
    if not all(math.isfinite(v) and v>0 for v in (cw,ch)):raise ValueError('净开口尺寸必须大于零')
    def coordinate(key):
        value=values[key]
        if isinstance(value,bool) or not isinstance(value,(int,float)) or not math.isfinite(value):
            raise ValueError('逃生窗定位值须为有限数值')
        if key.endswith('Margin') and value<0:raise ValueError('逃生窗边距不能为负数')
        return float(value)
    if x=='left':cl=inner['left']+coordinate('doorLeftMargin');cr=cl+cw
    elif x=='right':cr=inner['right']-coordinate('doorRightMargin');cl=cr-cw
    else:cl=(inner['left']+inner['right']-cw)/2+coordinate('doorHorizontalOffset');cr=cl+cw
    if z=='bottom':cb=inner['bottom']+coordinate('doorBottomMargin');ct=cb+ch
    elif z=='top':ct=inner['top']-coordinate('doorTopMargin');cb=ct-ch
    else:cb=(inner['bottom']+inner['top']-ch)/2+coordinate('doorVerticalOffset');ct=cb+ch
    clear={'left':cl,'right':cr,'bottom':cb,'top':ct}
    shared={k:available_sides[k] and abs(clear[k]-inner[k])<1.e-7 for k in inner}
    for k in inner:
        if shared[k]:continue
        remaining=(clear[k]-inner[k]) if k in ('left','bottom') else (inner[k]-clear[k])
        required=fixed.width if available_sides[k] else 0.
        if remaining<required-1.e-7:raise ValueError('逃生窗边距不足以容纳独立固定窗框；共用外框时将对应边距设为0')
    half={k:(side_widths[k] if shared[k] else fixed.width)/2 for k in inner}
    axes={k:clear[k]+(-half[k] if k in ('left','bottom') else half[k]) for k in inner}
    result=_finish(values,axes,shared,[],[],[],[],None,fixed,leaf,side_widths=side_widths)
    result['positioning']={'reference':'outer-frame-inner-faces','margins':{k:abs(clear[k]-inner[k]) for k in inner},
                          'innerBounds':dict(inner),
                          'horizontalParameter':{'left':'doorLeftMargin','right':'doorRightMargin','center':'doorHorizontalOffset'}[x],
                          'verticalParameter':{'bottom':'doorBottomMargin','top':'doorTopMargin','middle':'doorVerticalOffset'}[z]}
    return result


def _infill_axes(result,horizontal_positions,vertical_positions,leaf):
    bounds=result['leafBounds']
    result['horizontalAxes']=[z for z in horizontal_positions if bounds['bottom']+leaf.width<z<bounds['top']-leaf.width]
    result['verticalAxes']=[x for x in vertical_positions if bounds['left']+leaf.width<x<bounds['right']-leaf.width]


def _finish(values,bounds,shared,xs,zs,horizontal_positions,vertical_positions,frame,fixed,leaf,side_widths=None,**ranges):
    half={k:((side_widths[k] if side_widths else frame.width) if shared[k] else fixed.width)/2 for k in bounds}
    clear={'left':bounds['left']+half['left'],'right':bounds['right']-half['right'],
           'bottom':bounds['bottom']+half['bottom'],'top':bounds['top']-half['top']}
    gap=float(values['doorGap'])
    leaf_bounds={k:v+(gap if k in ('left','bottom') else -gap) for k,v in clear.items()}
    if not math.isfinite(gap) or gap < 0:raise ValueError('窗扇间隙不能为负数')
    if (leaf_bounds['right']-leaf_bounds['left'] <= 2*leaf.width+1.e-7
            or leaf_bounds['top']-leaf_bounds['bottom'] <= 2*leaf.width+1.e-7):
        raise ValueError('逃生窗开口不足以容纳窗扇框及间隙，请增大开口尺寸或占格数')
    return {'schemaVersion':1,'position':values['doorPosition'],**ranges,
            'columnLines':xs,'rowLines':zs,'boundaryAxes':bounds,'sharedOuterSides':shared,
            'clearBounds':clear,'leafBounds':leaf_bounds,
            'fixedClearWidth':clear['right']-clear['left'],'fixedClearHeight':clear['top']-clear['bottom'],
            'designClearWidth':max(0.,clear['right']-clear['left']-leaf.depth),
            'designClearHeight':clear['top']-clear['bottom'],
            'horizontalAxes':[z for z in horizontal_positions if leaf_bounds['bottom']+leaf.width<z<leaf_bounds['top']-leaf.width],
            'verticalAxes':[x for x in vertical_positions if leaf_bounds['left']+leaf.width<x<leaf_bounds['right']-leaf.width]}
