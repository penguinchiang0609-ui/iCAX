// The boundary document owns the shape. Construction nodes below describe
// optional editing intent; they are never substituted for boundary geometry.
const clone = value => structuredClone(value);
const uid = () => globalThis.crypto?.randomUUID?.() ?? `cad-${Date.now()}-${Math.random().toString(36).slice(2)}`;
const scalar = value => ({Value:value, Observability:0, Minimum:null, Maximum:null, Metadata:{}});
const point = (X=0,Y=0,Z=0) => ({X,Y,Z});
export const tubeCadKindLabels = {
  STubeSectionView:"管材截面",
  SExtrudedRegionNode:"拉伸体", STaperedRegionNode:"渐缩体", SHalfSpaceNode:"端面截切",
  SWrappedVolumeNode:"侧壁轮廓", SOpeningProfileSweepNode:"开口剖面",
  SCompositeVolumeNode:"精确复合实体", SResidualBRepNode:"保留实体",
  SBooleanNode:"材料组合", STransformNode:"构造变换", SAlternativeNode:"导入解释",
};
export function isTubeBoundaryGeometry(value) {
  return Array.isArray(value?.Section?.Boundaries)&&Array.isArray(value?.MachiningFaces?.Faces);
}
export function tubeCadCoreSectionNode(geometry) {
  if(!isTubeBoundaryGeometry(geometry))throw new Error("当前管材缺少有效的截面与加工面几何。");
  // This is an editor view of the same Section, not a copied sketch recipe.
  return {ID:"section",Label:"管材截面",Data:{type:"STubeSectionView",value:{Section:geometry.Section}}};
}
export function isTubeCadDocumentSnapshot(value) {
  return isTubeBoundaryGeometry(value?.geometry)&&Array.isArray(value?.construction?.SolidNodes)
    &&typeof value?.revision==="string";
}
export function tubeCadNode(geometry, nodeId) {
  if(nodeId==="section"&&isTubeBoundaryGeometry(geometry))return tubeCadCoreSectionNode(geometry);
  const node=geometry?.SolidNodes?.find(item=>item.ID===nodeId);
  if(!node)throw new Error("当前几何特征已不存在，请重新读取零件。");
  return node;
}
export function tubeCadRegion(node) {
  const data=node?.Data?.value;
  if(node?.Data?.type==="SOpeningProfileSweepNode")return {Boundaries:(data.ProfileField.Keys??[]).map((key,index)=>({
    ID:`profile:${index}`,Label:`剖面 ${index+1} · 轮廓位置 ${key.ContourParameter.Value} mm`,
    Curves:key.Profile.Segments.map((Segment,i)=>({ID:`profile:${index}:curve:${i}`,Segment})),
  }))};
  return data?.Section ?? data?.UVRegion ?? null;
}
export function tubeCadPlacement(node) {
  return node?.Data?.value?.Frame;
}
export function tubeCadLoopDimensions(node,loop) {
  if(!loop)return {kind:"curves"};
  const circle=loop.Curves[0]?.Segment.Curve;
  if(circle?.type==="Circle2"&&loop.Curves.every(item=>item.Segment.Curve.type==="Circle2"
    &&Math.abs(item.Segment.Curve.value.Radius-circle.value.Radius)<1e-9
    &&Math.hypot(item.Segment.Curve.value.Placement.Location.X-circle.value.Placement.Location.X,
      item.Segment.Curve.value.Placement.Location.Y-circle.value.Placement.Location.Y)<1e-9)) {
    const endpoint=(segment,end)=>{
      const data=segment.Curve.value,p=data.Placement,r=segment.Range;
      const angle=(end!==r.Reversed)?r.Last:r.First;
      return {X:p.Location.X+data.Radius*(Math.cos(angle)*p.XDirection.X+Math.sin(angle)*p.YDirection.X),
        Y:p.Location.Y+data.Radius*(Math.cos(angle)*p.XDirection.Y+Math.sin(angle)*p.YDirection.Y)};
    };
    const sweeps=loop.Curves.map(item=>{
      const {Range:r,Curve:{value:{Placement:p}}}=item.Segment;
      return (r.Last-r.First)*(r.Reversed?-1:1)*(p.XDirection.X*p.YDirection.Y-p.XDirection.Y*p.YDirection.X);
    });
    const complete=Math.abs(Math.abs(sweeps.reduce((sum,value)=>sum+value,0))-Math.PI*2)<1e-8
      &&sweeps.every(value=>Math.sign(value)===Math.sign(sweeps[0]))
      &&loop.Curves.every((item,index)=>{
        const end=endpoint(item.Segment,true),start=endpoint(loop.Curves[(index+1)%loop.Curves.length].Segment,false);
        return Math.hypot(end.X-start.X,end.Y-start.Y)<1e-8;
      });
    if(complete)return {kind:"circle",radius:circle.value.Radius,center:clone(circle.value.Placement.Location)};
  }
  const primitive=node?.Data?.value?.SectionPrimitives?.find(item=>item.LoopID===loop.ID);
  if(primitive?.Kind===2&&loop.Curves.length===4&&loop.Curves.every(item=>item.Segment.Curve.type==="Segment2")) {
    const first=loop.Curves[0].Segment.Curve.value,second=loop.Curves[1].Segment.Curve.value;
    return {kind:"rectangle",width:Math.hypot(first.End.X-first.Start.X,first.End.Y-first.Start.Y),
      height:Math.hypot(second.End.X-second.Start.X,second.End.Y-second.Start.Y),
      center:clone(primitive.Center),rotation:primitive.RotationRadians};
  }
  return {kind:"curves"};
}
export function setTubeCadSectionDimension(geometry,nodeId,loopId,dimension,value) {
  if(!Number.isFinite(value)||value<=1e-7)throw new Error("截面尺寸须大于零。");
  const node=tubeCadNode(geometry,nodeId),loop=tubeCadRegion(node)?.Boundaries.find(item=>item.ID===loopId);
  if(!loop)throw new Error("当前截面轮廓已不存在。");
  const dimensions=tubeCadLoopDimensions(node,loop);
  if(dimensions.kind==="circle"&&dimension==="radius")for(const curve of loop.Curves)curve.Segment.Curve.value.Radius=value;
  else if(dimensions.kind==="rectangle"&&["width","height"].includes(dimension)) {
    const c=Math.cos(dimensions.rotation),s=Math.sin(dimensions.rotation);
    const sx=dimension==="width"?value/dimensions.width:1,sy=dimension==="height"?value/dimensions.height:1;
    for(const curve of loop.Curves)for(const p of [curve.Segment.Curve.value.Start,curve.Segment.Curve.value.End]) {
      const dx=p.X-dimensions.center.X,dy=p.Y-dimensions.center.Y;
      const x=(c*dx+s*dy)*sx,y=(-s*dx+c*dy)*sy;
      p.X=dimensions.center.X+c*x-s*y;p.Y=dimensions.center.Y+s*x+c*y;
    }
  } else throw new Error("此精确轮廓没有可独立修改的尺寸，请编辑轮廓曲线。");
}
export function tubeCadCurveFields(curve) {
  const type=curve?.Segment?.Curve?.type,value=curve?.Segment?.Curve?.value;
  if(!value)return [];
  const number=(key,title)=>({path:[...key.split(".")],title});
  const xy=(key,title)=>[number(`${key}.X`,`${title} X`),number(`${key}.Y`,`${title} Y`)];
  if(type==="Segment2")return [...xy("Start","起点"),...xy("End","终点")];
  if(type==="Circle2")return [...xy("Placement.Location","圆心"),number("Radius","半径")];
  if(type==="Ellipse2")return [...xy("Placement.Location","中心"),number("MajorRadius","长半轴"),number("MinorRadius","短半轴")];
  if(type==="Arc2"||type==="EllipseArc2")return [...xy("Basis.Placement.Location","中心"),
    ...(type==="Arc2"?[number("Basis.Radius","半径")]:[number("Basis.MajorRadius","长半轴"),number("Basis.MinorRadius","短半轴")]),
    number("StartAngle","起始角（弧度）"),number("EndAngle","结束角（弧度）")];
  if(["BSpline2","NURBS2","Bezier2","Polyline2"].includes(type))return (value.Poles??value.Points??[])
    .flatMap((_,index)=>xy(`${value.Poles?"Poles":"Points"}.${index}`,`控制点 ${index+1}`));
  return [];
}
export function tubeCadValue(value,path) {return path.reduce((item,key)=>item?.[key],value);}
function write(value,path,next) {const key=path.at(-1),owner=tubeCadValue(value,path.slice(0,-1));if(!owner||!(key in owner))throw new Error("当前几何字段不可编辑。");owner[key]=next;}
export function editTubeCadCurve(geometry,nodeId,loopId,curveId,path,value) {
  if(!Number.isFinite(value))throw new Error("请输入有效的轮廓坐标或尺寸。");
  const region=tubeCadRegion(tubeCadNode(geometry,nodeId)),loop=region?.Boundaries.find(item=>item.ID===loopId);
  const curve=loop?.Curves.find(item=>item.ID===curveId);
  if(!tubeCadCurveFields(curve).some(field=>JSON.stringify(field.path)===JSON.stringify(path)))throw new Error("当前轮廓字段不可编辑。");
  if(path.at(-1).includes("Radius")&&value<=1e-7)throw new Error("轮廓半径须大于零。");
  const current=curve.Segment.Curve.value;
  // Connected line vertices stay connected. Exact arcs/splines and their
  // ranges remain intact; their join validity is checked by the native service.
  if(curve.Segment.Curve.type==="Segment2"&&["Start","End"].includes(path[0])) {
    const p=current[path[0]],old={...p};
    for(const sibling of loop.Curves)if(sibling.Segment.Curve.type==="Segment2")
      for(const q of [sibling.Segment.Curve.value.Start,sibling.Segment.Curve.value.End])
        if(Math.hypot(q.X-old.X,q.Y-old.Y)<1e-9)q[path[1]]=value;
  } else write(current,path,value);
}
export function tubeCadEditableNodes(geometry,mode) {
  if(isTubeBoundaryGeometry(geometry))return [tubeCadCoreSectionNode(geometry)];
  const nodes=geometry?.SolidNodes??[];
  if(mode==="sketch")return nodes.filter(node=>tubeCadRegion(node));
  if(mode==="punch")return nodes.filter(node=>node.ID!==geometry.BaseNodeID
    &&(tubeCadRegion(node)||node.Data.type==="SAlternativeNode"));
  return nodes;
}
export function setTubeCadLocation(geometry,nodeId,axis,value) {
  const node=tubeCadNode(geometry,nodeId),frame=tubeCadPlacement(node);
  if(!frame||!["X","Y","Z"].includes(axis)||!Number.isFinite(value))throw new Error("构造位置无效。");
  frame.Location[axis]=value;
  synchronizeLocalFrame(geometry,node);
}
function synchronizeLocalFrame(geometry,node) {
  const placement=node.Relations?.Placement;if(placement?.Rule!==1)return;
  const host=tubeCadNode(geometry,placement.HostNodeID).Data.value.Frame,world=tubeCadPlacement(node);
  if(!host||!world)throw new Error("相对宿主的构造坐标无法确定。");
  const local=p=>Object.fromEntries(["X","Y","Z"].map((axis,index)=>[axis,
    ["X","Y","Z"].reduce((sum,key)=>sum+p[key]*host[["XDirection","YDirection","ZDirection"][index]][key],0)]));
  placement.LocalFrame={...placement.LocalFrame,
    Location:local(Object.fromEntries(["X","Y","Z"].map(axis=>[axis,world.Location[axis]-host.Location[axis]]))),
    XDirection:local(world.XDirection),YDirection:local(world.YDirection),ZDirection:local(world.ZDirection)};
}
export function tubeCadRotation(node) {
  const frame=tubeCadPlacement(node);if(!frame)return null;
  const pitch=Math.asin(Math.max(-1,Math.min(1,-frame.XDirection.Z)));
  const roll=Math.abs(Math.cos(pitch))>1e-9?Math.atan2(frame.YDirection.Z,frame.ZDirection.Z):0;
  const yaw=Math.abs(Math.cos(pitch))>1e-9?Math.atan2(frame.XDirection.Y,frame.XDirection.X):Math.atan2(-frame.YDirection.X,frame.YDirection.Y);
  return {X:roll*180/Math.PI,Y:pitch*180/Math.PI,Z:yaw*180/Math.PI};
}
export function setTubeCadRotation(geometry,nodeId,axis,value) {
  const node=tubeCadNode(geometry,nodeId),frame=tubeCadPlacement(node),angles=tubeCadRotation(node);
  if(!frame||!["X","Y","Z"].includes(axis)||!Number.isFinite(value))throw new Error("构造旋转角无效。");
  angles[axis]=value;const [rx,ry,rz]=[angles.X,angles.Y,angles.Z].map(v=>v*Math.PI/180);
  const a=Math.cos(rx),b=Math.sin(rx),c=Math.cos(ry),d=Math.sin(ry),e=Math.cos(rz),f=Math.sin(rz);
  frame.XDirection=point(e*c,f*c,-d);
  frame.YDirection=point(e*d*b-f*a,f*d*b+e*a,c*b);
  frame.ZDirection=point(e*d*a+f*b,f*d*a-e*b,c*a);
  synchronizeLocalFrame(geometry,node);
}
// A response may normalize ranges or section descriptions while the user keeps
// typing. Apply only their later changes to the accepted document by stable IDs.
export function mergeTubeCadDraft(submitted,draft,accepted) {
  const same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
  const merge=(before,after,result,path)=>{
    if(same(before,after))return clone(result);
    if(Array.isArray(before)&&Array.isArray(after)&&Array.isArray(result)) {
      const id=value=>value&&typeof value==="object"?(value.ID??value.Id??null):null;
      if([...before,...after,...result].every(item=>id(item)!==null)) {
        const old=new Map(before.map(item=>[id(item),item])),next=new Map(after.map(item=>[id(item),item]));
        const merged=result.filter(item=>next.has(id(item))||!old.has(id(item))).map(item=>next.has(id(item))
          ?merge(old.get(id(item)),next.get(id(item)),item,`${path}/${id(item)}`):clone(item));
        for(const item of after)if(!merged.some(value=>id(value)===id(item))) {
          if(old.has(id(item)))throw new Error("保存响应移除了正在编辑的几何特征，请重新读取文档。");
          merged.push(clone(item));
        }
        return merged;
      }
      if(before.length===after.length&&before.length===result.length)return after.map((item,index)=>merge(before[index],item,result[index],`${path}/${index}`));
      if(!same(before,result))throw new Error("保存期间几何结构发生冲突，请重新读取文档。");
      return clone(after);
    }
    if(before&&after&&result&&typeof before==="object"&&typeof after==="object"&&typeof result==="object") {
      const merged=clone(result);
      for(const key of new Set([...Object.keys(before),...Object.keys(after)])) {
        if(same(before[key],after[key]))continue;
        if(!(key in after))delete merged[key];else merged[key]=merge(before[key],after[key],result[key],`${path}/${key}`);
      }
      return merged;
    }
    if(!same(before,result)&&!same(after,result))throw new Error("保存响应与继续输入的几何参数冲突；草稿已保留，请重新读取后确认。");
    return clone(after);
  };
  return merge(submitted,draft,accepted,"");
}
export function setTubeCadInterval(geometry,nodeId,key,value) {
  const node=tubeCadNode(geometry,nodeId),data=node.Data.value;
  if(!["First","Last","FirstScale","LastScale"].includes(key)||!Object.hasOwn(data,key)||!Number.isFinite(value))throw new Error("构造范围无效。");
  if(node.Relations?.Extent?.RecomputeAgainstHost&&["First","Last"].includes(key))throw new Error("范围由宿主材料约束确定。");
  if(key.includes("Scale")&&value<=0)throw new Error("渐缩比例须大于零。");
  if((key==="First"&&value>=data.Last)||(key==="Last"&&value<=data.First))throw new Error("构造结束位置须大于起始位置。");
  data[key]=value;
}
export function setTubeCadBlindDepth(geometry,nodeId,value) {
  const extent=tubeCadNode(geometry,nodeId).Relations?.Extent;
  if(extent?.Rule!==3||!extent.Depth||!Number.isFinite(value)||value<=0)throw new Error("当前特征没有可编辑的盲孔深度。");
  extent.Depth.Value=value;
}
export function addTubeCadHole(geometry,{diameter=10,station,offset=0,face="top"}={}) {
  const base=tubeCadNode(geometry,geometry.BaseNodeID),stock=base.Data.value;
  if(base.Data.type!=="SExtrudedRegionNode")throw new Error("当前原材没有可确定的直管构造坐标，不能添加孔。");
  station??=(stock.First+stock.Last)/2;
  if(![diameter,station,offset].every(Number.isFinite)||diameter<=0||station<stock.First||station>stock.Last
    ||!["top","bottom","front","back"].includes(face))throw new Error("请填写有效的孔径和孔位置。");
  const id=`hole-${uid()}`,cutId=`cut-${uid()}`,loopId=`${id}/boundary`;
  // A host constraint resolves the through range. The initial finite interval
  // is only a construction seed and never estimates a wall thickness.
  // The station belongs to the stock construction axis, including its origin.
  // Host-local XYZ mean section X, section Y and axial station respectively.
  const axes={top:[point(0,0,1),point(-1,0,0),point(0,-1,0)],bottom:[point(0,0,1),point(1,0,0),point(0,1,0)],
    front:[point(0,0,1),point(0,-1,0),point(1,0,0)],back:[point(0,0,1),point(0,1,0),point(-1,0,0)]}[face];
  const localFrame={Location:point(["top","bottom"].includes(face)?offset:0,["front","back"].includes(face)?offset:0,station),
    XDirection:axes[0],YDirection:axes[1],ZDirection:axes[2]};
  const worldDirection=p=>Object.fromEntries(["X","Y","Z"].map(axis=>[axis,stock.Frame.XDirection[axis]*p.X+stock.Frame.YDirection[axis]*p.Y+stock.Frame.ZDirection[axis]*p.Z]));
  const location=worldDirection(localFrame.Location);
  for(const axis of ["X","Y","Z"])location[axis]+=stock.Frame.Location[axis];
  const frame={Location:location,XDirection:worldDirection(axes[0]),YDirection:worldDirection(axes[1]),ZDirection:worldDirection(axes[2])};
  const node={ID:id,Label:"圆孔",Data:{type:"SExtrudedRegionNode",value:{
    Section:{FillRule:0,Boundaries:[{ID:loopId,Curves:[{ID:`${loopId}/circle`,Segment:{
      Curve:{type:"Circle2",value:{Placement:{Location:{X:0,Y:0},XDirection:{X:1,Y:0},YDirection:{X:0,Y:1}},Radius:diameter/2}},
      Range:{First:0,Last:Math.PI*2,Reversed:false}}}]}]},SectionPrimitives:[],Frame:frame,First:-1,Last:1,SideAtlases:[]}},
    Relations:{DependsOnNodeIDs:[geometry.BaseNodeID],Extent:{Rule:1,Direction:2,HostNodeID:geometry.BaseNodeID,
      PrimitiveFirst:scalar(-1),PrimitiveLast:scalar(1),Depth:null,Boundaries:[],RecomputeAgainstHost:true,Confidence:1,Metadata:{}},
      Placement:{Rule:1,HostNodeID:geometry.BaseNodeID,Support:null,LocalFrame:localFrame,SupportU:scalar(0),SupportV:scalar(0),Confidence:1,Metadata:{}},
      MaterialEffect:{Role:1,HostNodeID:geometry.BaseNodeID,SourceNodeIDs:[id],Confidence:1,Metadata:{}},
      SurfaceEvidence:[],MaterialSpans:[],Metadata:{}},RemovalSemantics:null,Metadata:{}};
  geometry.SolidNodes.push(node,{ID:cutId,Label:"孔切除",Data:{type:"SBooleanNode",value:{Operation:1,Children:[geometry.RootNodeID,id]}},
    Relations:null,RemovalSemantics:null,Metadata:{}});
  geometry.RootNodeID=cutId;
  return id;
}
