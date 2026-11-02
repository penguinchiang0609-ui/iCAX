namespace
{
    Map ManualHoleDescriptor(Scene& scene,const iCAX::CAM::Intent::SResolvedPath& path,const Text& kind)
    {
        if(kind!="Circle"&&kind!="Ellipse")throw std::invalid_argument("CAM_MANUAL_HOLE_KIND_INVALID");
        if(!path.Participating||path.ResultState!="Current")throw std::invalid_argument("CAM_CUT_SOURCE_NOT_CURRENT");
        if(!IntentDesignPath(scene,path).Value.Geometry.Closed)throw std::invalid_argument("CAM_MANUAL_HOLE_CLOSED_REQUIRED");
        auto cutting=path;std::erase_if(cutting.Value.Geometry.Segments,[](const auto& s){return !s.ConstructionRole.empty();});
        const auto samples=IntentPreviewPoints(scene,cutting);if(samples.size()<3)throw std::invalid_argument("CAM_MANUAL_HOLE_DEGENERATE");
        using V=std::array<double,3>;std::vector<V> points;
        for(const auto& sample:samples){const auto p=sample.To<Array>();points.push_back({p[0].To<double>(),p[1].To<double>(),p[2].To<double>()});}
        const auto dot=[](V a,V b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
        const auto cross=[](V a,V b){return V{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};};
        const auto unit=[&](V v){const auto d=std::sqrt(dot(v,v));if(d<1e-10)throw std::invalid_argument("CAM_MANUAL_HOLE_DEGENERATE");for(auto& n:v)n/=d;return v;};
        V origin=points.front(),normal{};
        for(std::size_t i=0;i<points.size();++i){V a{},b{};for(int j=0;j<3;++j){a[j]=points[i][j]-origin[j];b[j]=points[(i+1)%points.size()][j]-origin[j];}const auto area=cross(a,b);for(int j=0;j<3;++j)normal[j]+=area[j];}
        normal=unit(normal);
        // Keep the sign aligned with the authored cutting direction.
        const auto runtime=Runtime(scene);const auto axis=ReadToolAxis(scene,{{"expected",Map{{"epoch",runtime->Epoch},{"mutationSequence",std::to_string(runtime->Sequence)}}},{"intentId",iCAX::Data::to_string(path.PathId)}}).at("direction").To<Array>();
        if(dot(normal,{axis[0].To<double>(),axis[1].To<double>(),axis[2].To<double>()})<0)for(auto& n:normal)n=-n;
        V reference=std::abs(normal[0])<.8?V{1,0,0}:V{0,1,0};auto x=unit(cross(reference,normal)),y=cross(normal,x);
        double xx=0,xy=0,yy=0;V mean{};for(const auto& p:points)for(int j=0;j<3;++j)mean[j]+=p[j]/points.size();
        for(const auto& p:points){V d{};for(int j=0;j<3;++j)d[j]=p[j]-mean[j];const auto a=dot(d,x),b=dot(d,y);xx+=a*a;xy+=a*b;yy+=b*b;}
        const auto angle=.5*std::atan2(2*xy,xx-yy);V principal{};for(int j=0;j<3;++j)principal[j]=std::cos(angle)*x[j]+std::sin(angle)*y[j];x=principal;y=cross(normal,x);
        // Exact conics retain their native parameter frame for later edits.
        if(!cutting.Value.Geometry.Segments.empty()){
            const auto& segment=cutting.Value.Geometry.Segments.front();const auto* c=std::get_if<iCAX::GeometryData::CurveSegment3>(&segment.Curve);const auto* o=std::get_if<iCAX::GeometryData::DirectionalCurveOffset3>(&segment.Curve);
            if(c||o)std::visit([&](const auto& curve){const auto use=[&](const auto& p){const V n{p.ZDirection.X,p.ZDirection.Y,p.ZDirection.Z};if(std::abs(dot(n,normal))>1-1e-8){x={p.XDirection.X,p.XDirection.Y,p.XDirection.Z};y=cross(normal,x);}};using T=std::decay_t<decltype(curve)>;if constexpr(std::is_same_v<T,iCAX::GeometryData::Circle3>||std::is_same_v<T,iCAX::GeometryData::Ellipse3>)use(curve.Placement);else if constexpr(std::is_same_v<T,iCAX::GeometryData::Arc3>||std::is_same_v<T,iCAX::GeometryData::EllipseArc3>)use(curve.Basis.Placement);},c?c->Curve:o->Source.Curve);
        }
        V lo{1e100,1e100,1e100},hi{-1e100,-1e100,-1e100};for(const auto& p:points){V d{};for(int j=0;j<3;++j)d[j]=p[j]-mean[j];const V q{dot(d,x),dot(d,y),dot(d,normal)};for(int j=0;j<3;++j){lo[j]=std::min(lo[j],q[j]);hi[j]=std::max(hi[j],q[j]);}}
        auto width=hi[0]-lo[0],height=hi[1]-lo[1];if(width<1e-6||height<1e-6)throw std::invalid_argument("CAM_MANUAL_HOLE_DEGENERATE");
        if(std::abs(width-height)<1e-3*std::max(width,height))width=height=(width+height)/2;
        if(kind=="Ellipse"&&height>width){std::swap(width,height);const auto old=x;x=y;for(int j=0;j<3;++j)y[j]=-old[j];const auto a=lo[0],b=hi[0];lo[0]=lo[1];hi[0]=hi[1];lo[1]=-b;hi[1]=-a;}
        V center=mean;for(int j=0;j<3;++j)center[j]+=(lo[0]+hi[0])*.5*x[j]+(lo[1]+hi[1])*.5*y[j]+(lo[2]+hi[2])*.5*normal[j];
        const auto data=[](V p){return Array{p[0],p[1],p[2]};};
        Map hole{{"kind",kind},{"frame",Text("Workpiece")},{"direction",data(normal)},{"xDirection",data(x)},{"center",data(center)},{"manual",true}};
        if(kind=="Circle")hole["diameterMm"]=(width+height)/2;else {hole["majorDiameterMm"]=width;hole["minorDiameterMm"]=height;}
        return hole;
    }
}
Map QueryStandardHole(Scene& scene,const Map& request)
{
    CutFields(request,{"expected","intentId"});CheckExpected(*Runtime(scene),request);const auto path=ReadIntentPath(scene,Identifier(request,"intentId"));
    return {{"suggestions",Map{{"Circle",ManualHoleDescriptor(scene,path,"Circle")},{"Ellipse",ManualHoleDescriptor(scene,path,"Ellipse")}}}};
}
