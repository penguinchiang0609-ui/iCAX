// ICAX-CL exact curve records retain native parameter ranges and transformed
// analytic/spline definitions. They are never fitted from display samples.
namespace {
    Map ProgramCurveData(const iCAX::GeometryData::Curve3& curve,const AlignMatrix& matrix){
        using namespace iCAX::GeometryData;
        const auto point=[&](auto p){const auto q=AlignMap(matrix,{p.X,p.Y,p.Z});return Array{q[0],q[1],q[2]};};
        const auto direction=[&](auto p){const auto q=AlignMap(matrix,{p.X,p.Y,p.Z},true);return Array{q[0],q[1],q[2]};};
        const auto placement=[&](const Placement3& p){return Map{{"origin",point(p.Location)},{"x",direction(p.XDirection)},{"y",direction(p.YDirection)},{"z",direction(p.ZDirection)}};};
        return std::visit([&](const auto& c)->Map{
            using T=std::decay_t<decltype(c)>;
            if constexpr(std::is_same_v<T,Line3>||std::is_same_v<T,Ray3>){
                Map result{{"kind",Text(std::is_same_v<T,Line3>?"Line":"Ray")},{"origin",point(c.Axis.Location)},{"direction",direction(c.Axis.Direction)}};
                if constexpr(std::is_same_v<T,Ray3>)result["first"]=c.First;return result;
            }else if constexpr(std::is_same_v<T,Segment3>)return {{"kind",Text("Segment")},{"start",point(c.Start)},{"end",point(c.End)}};
            else if constexpr(std::is_same_v<T,Circle3>)return {{"kind",Text("Circle")},{"placement",placement(c.Placement)},{"radius",c.Radius}};
            else if constexpr(std::is_same_v<T,Ellipse3>)return {{"kind",Text("Ellipse")},{"placement",placement(c.Placement)},{"majorRadius",c.MajorRadius},{"minorRadius",c.MinorRadius}};
            else if constexpr(std::is_same_v<T,Arc3>||std::is_same_v<T,EllipseArc3>){
                auto result=ProgramCurveData(Curve3(c.Basis),matrix);result["kind"]=Text(std::is_same_v<T,Arc3>?"Arc":"EllipseArc");
                result["startAngle"]=c.StartAngle;result["endAngle"]=c.EndAngle;result["counterClockwise"]=c.CounterClockwise;return result;
            }else if constexpr(std::is_same_v<T,Polyline3>){Array points;for(const auto p:c.Points)points.emplace_back(point(p));return {{"kind",Text("Polyline")},{"points",points},{"closed",c.Closed}};}
            else if constexpr(requires{c.Poles;}){
                Array poles;for(const auto p:c.Poles)poles.emplace_back(point(p));
                Map result{{"kind",Text(std::is_same_v<T,Bezier3>?"Bezier":std::is_same_v<T,BSpline3>?"BSpline":"NURBS")},{"poles",poles}};
                if constexpr(requires{c.Degree;}){Array knots,multiplicities;for(auto n:c.Knots)knots.emplace_back(n);for(auto n:c.Multiplicities)multiplicities.emplace_back(n);result["degree"]=c.Degree;result["knots"]=knots;result["multiplicities"]=multiplicities;result["periodic"]=c.Periodic;}
                if constexpr(requires{c.Weights;}){Array weights;for(auto n:c.Weights)weights.emplace_back(n);result["weights"]=weights;}return result;
            }else if constexpr(std::is_same_v<T,Clothoid3>)return {{"kind",Text("Clothoid")},{"placement",placement(c.Placement)},{"startCurvature",c.StartCurvature},{"endCurvature",c.EndCurvature},{"length",c.Length}};
            else return {};
        },curve);
    }
    Map ProgramCurveRecord(const iCAX::CAM::Intent::SEditableSegment& segment,const AlignMatrix& matrix,const iCAX::GeometryData::Direction3* planarOffsetDirection=nullptr){
        using namespace iCAX::GeometryData;Map result;ParameterRange range;
        if(const auto* c=std::get_if<CurveSegment3>(&segment.Curve)){result=ProgramCurveData(c->Curve,matrix);range=c->Range;}
        else if(const auto* c=std::get_if<DirectionalCurveOffset3>(&segment.Curve)){
            range=c->Source.Range;result=ProgramCurveData(c->Source.Curve,matrix);
            if(c->SignedDistance!=0){const auto* d=std::get_if<Direction3>(&c->ReferenceDirection);if(!d)d=planarOffsetDirection;if(!d)return {};
                const auto axis=AlignMap(matrix,{d->X,d->Y,d->Z},true);result={{"kind",Text("DirectionalOffset")},{"source",result},{"signedDistance",c->SignedDistance},{"referenceDirection",Array{axis[0],axis[1],axis[2]}}};}
        }else return {};
        if(result.empty())return {};
        return {{"schema",Text("icax.cl-curve/1")},{"segmentId",segment.Id},{"role",segment.ConstructionRole},{"parameterization",segment.Parameterization},
            {"range",Map{{"first",range.First},{"last",range.Last},{"reversed",range.Reversed}}},{"geometry",result}};
    }
    bool ProgramPlanarSurface(const iCAX::GeometryData::Surface3& surface){
        using namespace iCAX::GeometryData;
        if(std::holds_alternative<PlaneSurface3>(surface))return true;
        if(const auto* extrusion=std::get_if<ExtrusionSurface3>(&surface))return std::holds_alternative<Line3>(extrusion->Basis)||std::holds_alternative<Ray3>(extrusion->Basis)||std::holds_alternative<Segment3>(extrusion->Basis);
        const auto* spline=std::get_if<BSplineSurface3>(&surface);if(!spline||spline->Poles.size()<3)return false;
        const auto origin=spline->Poles.front();AlignVector first{},normal{};double scale=1;
        for(const auto p:spline->Poles){const AlignVector d{p.X-origin.X,p.Y-origin.Y,p.Z-origin.Z};scale=std::max(scale,std::sqrt(AlignDot(d,d)));if(AlignDot(first,first)<1e-20)first=d;else if(AlignDot(normal,normal)<1e-20)normal=AlignCross(first,d);}
        if(AlignDot(normal,normal)<1e-20)return false;normal=AlignUnit(normal);
        for(const auto p:spline->Poles)if(std::abs(AlignDot(normal,{p.X-origin.X,p.Y-origin.Y,p.Z-origin.Z}))>scale*1e-12)return false;
        for(const auto weight:spline->Weights)if(!std::isfinite(weight)||weight<=0)return false;return true;
    }
    bool ProgramConstantAxis(Scene& scene,const AxisPath& path,const Map& operation){
        for(const auto& v:AxisActiveControls(path))if(String(v.To<Map>(),"kind")!="Reference")return false;
        const auto policy=AxisPolicy(path);
        if(!FixedToolAxis(policy)){
            if(String(policy,"mode")!="Continuous"||String(policy,"initialization")!="SurfaceNormal"||RegistrationNumber(policy.at("leadAngleRad"))!=0||RegistrationNumber(policy.at("sideAngleRad"))!=0)return false;
            const auto context=FreezeIntentContext(scene,IntentSourceOperation(scene,path.Port));if(context->SourceBindings().empty())return false;
            std::set<Text> used;for(const auto& segment:path.Value.Geometry.Segments){
                if(context->SourceBindings().contains(segment.Id))used.insert(segment.Id);
                for(const auto& source:segment.Sources)if(context->SourceBindings().contains(source.SourceSegmentId))used.insert(source.SourceSegmentId);
            }
            if(used.empty())return false;
            for(const auto& key:used){const auto& binding=context->SourceBindings().at(key);
                const auto& model=context->Model();const auto face=std::find_if(model.Faces.begin(),model.Faces.end(),[&](const auto& f){return f.Id==binding.FaceId;});if(face==model.Faces.end())return false;
                const auto surface=std::find_if(model.Surfaces3.begin(),model.Surfaces3.end(),[&](const auto& s){return s.Id==face->Surface3Id;});
                if(surface==model.Surfaces3.end()||!ProgramPlanarSurface(surface->Geometry))return false;
            }
        }
        std::optional<AlignVector> previous;
        for(const auto& pv:operation.at("parts").To<Array>())for(const auto& v:pv.To<Map>().at("directions").To<Array>()){
            const auto axis=AxisUnit(v);if(previous){const auto d=AlignAdd(axis,*previous,-1);if(AlignDot(d,d)>1e-24)return false;}else previous=axis;
        }
        return previous.has_value();
    }
}
