using CorrectionMatrix=std::array<double,16>;
CorrectionMatrix IdentityCorrection(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
CorrectionMatrix ReadCorrection(const Map& parameters)
{
    const auto data=parameters.at("matrix").To<Array>();Check(data.size()==16,"CAM_PATH_TRANSFORM_INVALID");CorrectionMatrix matrix{};
    for(std::size_t i=0;i<16;++i)matrix[i]=Numeric(data[i]);
    Check(std::abs(matrix[12])+std::abs(matrix[13])+std::abs(matrix[14])+std::abs(matrix[15]-1)<1e-10,"CAM_PATH_TRANSFORM_INVALID");
    const auto scale=std::hypot(matrix[0],matrix[4],matrix[8]);Check(scale>1e-9&&scale<=1e6,"CAM_PATH_SCALE_INVALID");
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){double dot=0;for(int k=0;k<3;++k)dot+=matrix[k*4+i]*matrix[k*4+j];Check(std::abs(dot/(scale*scale)-(i==j?1.:0.))<1e-9,"CAM_PATH_TRANSFORM_REQUIRES_SIMILARITY");}
    const auto determinant=matrix[0]*(matrix[5]*matrix[10]-matrix[6]*matrix[9])-matrix[1]*(matrix[4]*matrix[10]-matrix[6]*matrix[8])+matrix[2]*(matrix[4]*matrix[9]-matrix[5]*matrix[8]);Check(determinant>0,"CAM_PATH_TRANSFORM_REFLECTION");return matrix;
}
template<class P> void CorrectPoint(P& point,const CorrectionMatrix& matrix,bool direction=false)
{
    const auto x=point.X,y=point.Y,z=point.Z;const auto scale=direction?std::hypot(matrix[0],matrix[4],matrix[8]):1.;
    point.X=(matrix[0]*x+matrix[1]*y+matrix[2]*z+(direction?0:matrix[3]))/scale;
    point.Y=(matrix[4]*x+matrix[5]*y+matrix[6]*z+(direction?0:matrix[7]))/scale;
    point.Z=(matrix[8]*x+matrix[9]*y+matrix[10]*z+(direction?0:matrix[11]))/scale;
}
void CorrectPlacement(Placement3& p,const CorrectionMatrix& m){CorrectPoint(p.Location,m);CorrectPoint(p.XDirection,m,true);CorrectPoint(p.YDirection,m,true);CorrectPoint(p.ZDirection,m,true);}
void CorrectCurve(Curve3& curve,ParameterRange range,const CorrectionMatrix& m)
{
    const auto scale=std::hypot(m[0],m[4],m[8]);
    if(const auto* line=std::get_if<Line3>(&curve);line&&std::abs(scale-1)>1e-12){const auto point=[&](double u){Point3 p{line->Axis.Location.X+u*line->Axis.Direction.X,line->Axis.Location.Y+u*line->Axis.Direction.Y,line->Axis.Location.Z+u*line->Axis.Direction.Z};CorrectPoint(p,m);return p;};BSpline3 replacement;replacement.Degree=1;replacement.Poles={point(range.First),point(range.Last)};replacement.Knots={range.First,range.Last};replacement.Multiplicities={2,2};curve=std::move(replacement);return;}
    std::visit([&](auto& c){using T=std::decay_t<decltype(c)>;
        if constexpr(std::is_same_v<T,Line3>||std::is_same_v<T,Ray3>){Check(std::abs(scale-1)<1e-12,"CAM_PATH_RAY_SCALE_UNSUPPORTED");CorrectPoint(c.Axis.Location,m);CorrectPoint(c.Axis.Direction,m,true);}
        else if constexpr(std::is_same_v<T,Segment3>){CorrectPoint(c.Start,m);CorrectPoint(c.End,m);}
        else if constexpr(std::is_same_v<T,Arc3>){CorrectPlacement(c.Basis.Placement,m);c.Basis.Radius*=scale;}
        else if constexpr(std::is_same_v<T,EllipseArc3>){CorrectPlacement(c.Basis.Placement,m);c.Basis.MajorRadius*=scale;c.Basis.MinorRadius*=scale;}
        else if constexpr(requires{c.Points;})for(auto& p:c.Points)CorrectPoint(p,m);
        else if constexpr(requires{c.Poles;})for(auto& p:c.Poles)CorrectPoint(p,m);
        else {CorrectPlacement(c.Placement,m);if constexpr(requires{c.Radius;})c.Radius*=scale;if constexpr(requires{c.MajorRadius;}){c.MajorRadius*=scale;c.MinorRadius*=scale;}if constexpr(std::is_same_v<T,Clothoid3>)Check(std::abs(scale-1)<1e-12,"CAM_PATH_CLOTHOID_SCALE_UNSUPPORTED");}
    },curve);
}
void CorrectSurface(Surface3& surface,const CorrectionMatrix& m)
{
    const auto scale=std::hypot(m[0],m[4],m[8]);std::visit([&](auto& s){using T=std::decay_t<decltype(s)>;
        if constexpr(std::is_same_v<T,BSplineSurface3>)for(auto& p:s.Poles)CorrectPoint(p,m);
        else if constexpr(std::is_same_v<T,ExtrusionSurface3>){CorrectCurve(s.Basis,{0,1},m);CorrectPoint(s.Direction,m,true);}
        else if constexpr(std::is_same_v<T,RevolutionSurface3>){CorrectCurve(s.Basis,{0,1},m);CorrectPoint(s.Axis.Location,m);CorrectPoint(s.Axis.Direction,m,true);}
        else {CorrectPlacement(s.Placement,m);if constexpr(requires{s.Radius;})s.Radius*=scale;if constexpr(requires{s.MajorRadius;}){s.MajorRadius*=scale;s.MinorRadius*=scale;}}
    },surface);
}
void CorrectGeometry(SEditableGeometry& geometry,const CorrectionMatrix& m)
{
    Check(geometry.Authority==EGeometryAuthority::Spatial3D,"CAM_PATH_SPATIAL_GEOMETRY_REQUIRED");const auto scale=std::hypot(m[0],m[4],m[8]);
    for(auto& segment:geometry.Segments){if(auto* offset=std::get_if<DirectionalCurveOffset3>(&segment.Curve)){CorrectCurve(offset->Source.Curve,offset->Source.Range,m);offset->SignedDistance*=scale;std::visit([&](auto& field){using T=std::decay_t<decltype(field)>;if constexpr(std::is_same_v<T,Direction3>)CorrectPoint(field,m,true);else if constexpr(std::is_same_v<T,SurfaceNormalField3>)CorrectSurface(field.Surface,m);else std::visit([&](auto& coefficients){for(auto& p:coefficients.Poles)CorrectPoint(p,m,true);},field.Coefficients);},offset->ReferenceDirection);}else {auto& curve=std::get<CurveSegment3>(segment.Curve);CorrectCurve(curve.Curve,curve.Range,m);}}
    if(geometry.SupportSurface)CorrectSurface(*geometry.SupportSurface,m);
}
Array CorrectionData(const CorrectionMatrix& m){Array values;for(auto v:m)values.emplace_back(v);return values;}
CorrectionMatrix SegmentCorrection(const SToolpathValue& value,const SEditableSegment& segment)
{
    auto local=IdentityCorrection();if(value.Channels.contains("fiveaxis.segmentCorrections")){const auto matrices=Object(value.Channels.at("fiveaxis.segmentCorrections").To<Map>(),"matrices");const auto lookup=[&](const std::string& id){const auto found=matrices.find(id);if(found==matrices.end())return false;local=ReadCorrection(Map{{"matrix",found->second}});return true;};if(!lookup(segment.Id))for(auto source=segment.Sources.rbegin();source!=segment.Sources.rend();++source)if(lookup(source->SourceSegmentId))break;}
    if(!value.Channels.contains("fiveaxis.correction"))return local;const auto global=ReadCorrection(value.Channels.at("fiveaxis.correction").To<Map>());CorrectionMatrix result{};for(int r=0;r<4;++r)for(int c=0;c<4;++c)for(int k=0;k<4;++k)result[r*4+c]+=global[r*4+k]*local[k*4+c];return result;
}
void RecordGeometryEdit(SToolpathValue& value,const std::string& type,const Map& parameters)
{auto channel=value.Channels.contains("fiveaxis.geometryEdits")?value.Channels.at("fiveaxis.geometryEdits").To<Map>():Map{{"schema",std::string("fiveaxis.geometry-edits/1")},{"attachment",std::string("Path")},{"entries",Array{}}};auto entries=channel.at("entries").To<Array>();entries.emplace_back(Map{{"type",type},{"parameters",parameters}});channel["entries"]=entries;value.Channels["fiveaxis.geometryEdits"]=channel;}
SOperationCandidate CorrectionOperation(const SOperationEvaluation& input,const IToolpathGeometryServices&)
{
    const auto matrix=ReadCorrection(input.Parameters);auto value=Whole(input.Inputs.at(0));const auto before=value;CorrectGeometry(value.Geometry,matrix);
    auto previous=IdentityCorrection();if(value.Channels.contains("fiveaxis.correction"))previous=ReadCorrection(value.Channels.at("fiveaxis.correction").To<Map>());CorrectionMatrix combined{};
    for(int r=0;r<4;++r)for(int c=0;c<4;++c)for(int k=0;k<4;++k)combined[r*4+c]+=matrix[r*4+k]*previous[k*4+c];
    value.Channels["fiveaxis.correction"]=Map{{"schema",std::string("fiveaxis.correction/1")},{"attachment",std::string("Path")},{"matrix",CorrectionData(combined)}};
    TransformFiveAxisIntentPoseFields(before,value,matrix);
    RecordGeometryEdit(value,"fiveaxis.correction",input.Parameters);auto metadata=ReadFiveAxisIntentMetadata(value);if(metadata.Feature.contains("standardHole")){auto hole=Object(metadata.Feature,"standardHole");const auto scale=std::hypot(matrix[0],matrix[4],matrix[8]);for(const auto* key:{"diameterMm","majorDiameterMm","minorDiameterMm","widthMm","lengthMm"})if(hole.contains(key))hole[key]=Number(hole,key)*scale;if(hole.contains("center")){const auto data=hole.at("center").To<Array>();Point3 center{Numeric(data[0]),Numeric(data[1]),Numeric(data[2])};CorrectPoint(center,matrix);hole["center"]=Array{center.X,center.Y,center.Z};}if(hole.contains("xDirection")){auto axis=Direction(hole.at("xDirection"));CorrectPoint(axis,matrix,true);hole["xDirection"]=Point(axis);}auto normal=Direction(hole.contains("normal")?hole.at("normal"):hole.at("direction"));CorrectPoint(normal,matrix,true);hole["normal"]=Point(normal);hole["direction"]=Point(normal);metadata.Feature["standardHole"]=hole;value.Channels["fiveaxis"]=ToFiveAxisIntentChannel(metadata);}return One(std::move(value));
}
void ResizeConic(Curve3& curve,const Map& hole,double major,double minor)
{
    const auto kind=Text(hole,"kind");
    if(Flag(hole,"manual")){
        const auto coordinates=hole.at("center").To<Array>();const Point3 center{Numeric(coordinates[0]),Numeric(coordinates[1]),Numeric(coordinates[2])};
        const auto x=Direction(hole.at("xDirection")),normal=Direction(hole.at("direction")),y=Cross(normal,x);
        const auto sx=major/Number(hole,kind=="Circle"?"diameterMm":"majorDiameterMm"),sy=minor/Number(hole,kind=="Circle"?"diameterMm":"minorDiameterMm");
        const auto point=[&](Point3 p){const Direction3 d{p.X-center.X,p.Y-center.Y,p.Z-center.Z};const auto a=Dot(d,x)*sx,b=Dot(d,y)*sy,z=Dot(d,normal);return Point3{center.X+a*x.X+b*y.X+z*normal.X,center.Y+a*x.Y+b*y.Y+z*normal.Y,center.Z+a*x.Z+b*y.Z+z*normal.Z};};
        const auto vector=[&](Direction3 d){const auto a=Dot(d,x)*sx,b=Dot(d,y)*sy,z=Dot(d,normal);return Direction3{a*x.X+b*y.X+z*normal.X,a*x.Y+b*y.Y+z*normal.Y,a*x.Z+b*y.Z+z*normal.Z};};
        std::optional<Curve3> replacement;
        std::visit([&](auto& c){using T=std::decay_t<decltype(c)>;
            if constexpr(requires{c.Poles;})for(auto& p:c.Poles)p=point(p);
            else if constexpr(requires{c.Points;})for(auto& p:c.Points)p=point(p);
            else if constexpr(std::is_same_v<T,Segment3>){c.Start=point(c.Start);c.End=point(c.End);}
            else if constexpr(std::is_same_v<T,Circle3>||std::is_same_v<T,Arc3>||std::is_same_v<T,Ellipse3>||std::is_same_v<T,EllipseArc3>){
                const auto conic=[&](const auto& source){auto p=source.Placement;const auto u=vector(p.XDirection),v=vector(p.YDirection);const auto a=std::hypot(u.X,u.Y,u.Z),b=std::hypot(v.X,v.Y,v.Z);Check(std::abs(Dot(u,v)/(a*b))<1e-8,"CAM_MANUAL_HOLE_CONIC_FRAME_UNSUPPORTED");p.Location=point(p.Location);p.XDirection=Times(u,1/a);p.YDirection=Times(v,1/b);p.ZDirection=Unit(Cross(p.XDirection,p.YDirection));Ellipse3 e;e.Placement=p;if constexpr(requires{source.Radius;}){e.MajorRadius=source.Radius*a;e.MinorRadius=source.Radius*b;}else {e.MajorRadius=source.MajorRadius*a;e.MinorRadius=source.MinorRadius*b;}Check(e.MajorRadius>=e.MinorRadius-1e-9,"CAM_MANUAL_HOLE_CONIC_FRAME_UNSUPPORTED");return e;};
                if constexpr(std::is_same_v<T,Arc3>||std::is_same_v<T,EllipseArc3>){EllipseArc3 e;e.Basis=conic(c.Basis);e.StartAngle=c.StartAngle;e.EndAngle=c.EndAngle;e.CounterClockwise=c.CounterClockwise;replacement=Curve3{e};}else replacement=Curve3{conic(c)};
            }else throw std::invalid_argument("CAM_HOLE_GEOMETRY_MISMATCH");
        },curve);
        if(replacement)curve=std::move(*replacement);return;
    }
    bool changed=false;std::visit([&](auto& c){using T=std::decay_t<decltype(c)>;
        if constexpr(std::is_same_v<T,Circle3>){Check(kind=="Circle","CAM_HOLE_GEOMETRY_MISMATCH");c.Radius=major/2;changed=true;}
        else if constexpr(std::is_same_v<T,Arc3>){Check(kind=="Circle","CAM_HOLE_GEOMETRY_MISMATCH");c.Basis.Radius=major/2;changed=true;}
        else if constexpr(std::is_same_v<T,Ellipse3>){Check(kind=="Ellipse","CAM_HOLE_GEOMETRY_MISMATCH");c.MajorRadius=major/2;c.MinorRadius=minor/2;changed=true;}
        else if constexpr(std::is_same_v<T,EllipseArc3>){Check(kind=="Ellipse","CAM_HOLE_GEOMETRY_MISMATCH");c.Basis.MajorRadius=major/2;c.Basis.MinorRadius=minor/2;changed=true;}
        else if constexpr(std::is_same_v<T,BSpline3>||std::is_same_v<T,NURBS3>){const auto coordinates=hole.at("center").To<Array>();Check(coordinates.size()==3,"CAM_HOLE_GEOMETRY_MISMATCH");const Point3 center{Numeric(coordinates[0]),Numeric(coordinates[1]),Numeric(coordinates[2])};const auto x=Direction(hole.at("xDirection")),normal=Direction(hole.contains("normal")?hole.at("normal"):hole.at("direction")),y=Cross(normal,x);const auto sx=major/Number(hole,kind=="Circle"?"diameterMm":"majorDiameterMm"),sy=minor/Number(hole,kind=="Circle"?"diameterMm":"minorDiameterMm");Check(std::isfinite(sx)&&std::isfinite(sy)&&sx>0&&sy>0,"CAM_HOLE_DIMENSIONS_INVALID");for(auto& p:c.Poles){const Direction3 delta{p.X-center.X,p.Y-center.Y,p.Z-center.Z};const auto a=Dot(delta,x)*sx,b=Dot(delta,y)*sy,z=Dot(delta,normal);p={center.X+a*x.X+b*y.X+z*normal.X,center.Y+a*x.Y+b*y.Y+z*normal.Y,center.Z+a*x.Z+b*y.Z+z*normal.Z};}changed=true;}
    },curve);Check(changed,"CAM_HOLE_GEOMETRY_MISMATCH");
}
#include "FiveAxisIntentPrismaticHole.inc"
SOperationCandidate HoleDimensionsOperation(const SOperationEvaluation& input,const IToolpathGeometryServices& geometry)
{
    if(input.Inputs.size()==2)return PrismaticHoleDimensions(input,geometry);
    auto value=Whole(input.Inputs.at(0));auto metadata=ReadFiveAxisIntentMetadata(value);const bool standard=metadata.Feature.contains("standardHole");auto hole=input.Parameters.contains("descriptor")?Object(input.Parameters,"descriptor"):Object(metadata.Feature,"standardHole");const auto kind=Text(hole,"kind");Check(kind=="Circle"||kind=="Ellipse","CAM_HOLE_DIMENSIONS_UNSUPPORTED");const auto parameters=Object(input.Parameters,"dimensions");Fields(parameters,kind=="Circle"?std::initializer_list<const char*>{"diameterMm"}:std::initializer_list<const char*>{"majorDiameterMm","minorDiameterMm"});
    const auto major=Number(parameters,kind=="Circle"?"diameterMm":"majorDiameterMm"),minor=kind=="Circle"?major:Number(parameters,"minorDiameterMm");Check(major>1e-6&&major<=1e6&&minor>1e-6&&minor<=major,"CAM_HOLE_DIMENSIONS_INVALID");
    std::vector<std::size_t> cutting;for(std::size_t i=0;i<value.Geometry.Segments.size();++i)if(value.Geometry.Segments[i].ConstructionRole!="LeadIn"&&value.Geometry.Segments[i].ConstructionRole!="LeadOut")cutting.push_back(i);Check(!cutting.empty(),"CAM_HOLE_GEOMETRY_MISMATCH");
    const auto old=value;for(const auto index:cutting){auto& segment=value.Geometry.Segments[index];auto& curve=std::holds_alternative<DirectionalCurveOffset3>(segment.Curve)?std::get<DirectionalCurveOffset3>(segment.Curve).Source:std::get<CurveSegment3>(segment.Curve);
        if(Flag(hole,"manual")&&std::holds_alternative<Line3>(curve.Curve)){const auto line=std::get<Line3>(curve.Curve);const auto at=[&](double u){return Point3{line.Axis.Location.X+u*line.Axis.Direction.X,line.Axis.Location.Y+u*line.Axis.Direction.Y,line.Axis.Location.Z+u*line.Axis.Direction.Z};};BSpline3 bounded;bounded.Degree=1;bounded.Poles={at(curve.Range.First),at(curve.Range.Last)};bounded.Knots={curve.Range.First,curve.Range.Last};bounded.Multiplicities={2,2};curve.Curve=std::move(bounded);}
        ResizeConic(curve.Curve,hole,major,minor);
    }
    // Leads keep their physical length and shape; move/rotate their local frame
    // to the new exact boundary endpoint instead of stretching the lead.
    Walk::Evaluator evaluator(Context(input).Offset());
    for(bool entering:{true,false}){const auto index=entering?cutting.front():cutting.back();const auto& before=old.Geometry.Segments[index];const auto& after=value.Geometry.Segments[index];const auto u=entering?Start(Range(after)):End(Range(after));const auto a=evaluator.At(Piece(before,index),u),b=evaluator.At(Piece(after,index),u);auto from=Unit({a.OffsetDerivative.X,a.OffsetDerivative.Y,a.OffsetDerivative.Z}),to=Unit({b.OffsetDerivative.X,b.OffsetDerivative.Y,b.OffsetDerivative.Z});const auto cross=Cross(from,to);const auto sine=std::hypot(cross.X,cross.Y,cross.Z),cosine=std::clamp(Dot(from,to),-1.,1.);auto transform=IdentityCorrection();if(sine>1e-12){const auto axis=Times(cross,1/sine);const double v[3]{axis.X,axis.Y,axis.Z};const double skew[9]{0,-axis.Z,axis.Y,axis.Z,0,-axis.X,-axis.Y,axis.X,0};for(int r=0;r<3;++r)for(int c=0;c<3;++c)transform[r*4+c]=(r==c?cosine:0)+(1-cosine)*v[r]*v[c]+sine*skew[r*3+c];}const double p[3]{a.OffsetPoint.X,a.OffsetPoint.Y,a.OffsetPoint.Z},q[3]{b.OffsetPoint.X,b.OffsetPoint.Y,b.OffsetPoint.Z};for(int r=0;r<3;++r){transform[r*4+3]=q[r];for(int c=0;c<3;++c)transform[r*4+3]-=transform[r*4+c]*p[c];}for(auto& segment:value.Geometry.Segments)if(segment.ConstructionRole==(entering?"LeadIn":"LeadOut")){auto& curve=std::get<CurveSegment3>(segment.Curve);CorrectCurve(curve.Curve,curve.Range,transform);}}
    auto saved=input.Parameters;saved["descriptor"]=hole;RecordGeometryEdit(value,"fiveaxis.holeDimensions",saved);for(const auto& [key,number]:parameters)hole[key]=Numeric(number);if(standard)metadata.Feature["standardHole"]=hole;value.Channels["fiveaxis"]=ToFiveAxisIntentChannel(metadata);return One(std::move(value));
}
