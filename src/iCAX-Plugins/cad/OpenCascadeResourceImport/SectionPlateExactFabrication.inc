namespace plate_exact {
std::vector<P2> Hull(std::vector<P2> points){std::sort(points.begin(),points.end());points.erase(std::unique(points.begin(),points.end()),points.end());if(points.size()<3)return {};std::vector<P2> hull;
    for(auto p:points){while(hull.size()>1&&Cross(hull[hull.size()-2],hull.back(),p)<=0)hull.pop_back();hull.push_back(p);}const auto n=hull.size();for(auto i=points.rbegin()+1;i!=points.rend();++i){while(hull.size()>n&&Cross(hull[hull.size()-2],hull.back(),*i)<=0)hull.pop_back();hull.push_back(*i);}hull.pop_back();return hull;}
// Keep-outs are explicitly supplied convex volumes. Their planar projection
// is a convex hull of vertices and plane/edge intersections, not a mesh.
void KeepOuts(Plate& p,const SectionPlateInput& in,const Check& check){
    const auto frame=PlateDirectionFrame::Transform(p.Data.Frame).Inverted();
    for(const auto& cell:in.KeepOuts){check();if(p.Data.Base&&!cell.AffectsBase)continue;const double margin=cell.Margin,lo=-margin,hi=p.Data.Thickness+margin;std::vector<gp_Pnt> points;std::vector<P2> projected;
        const auto add=[&](double u,double v){for(double dx:{-margin,margin})for(double dy:{-margin,margin})projected.push_back({u+dx,v+dy});};
        for(auto q:cell.Vertices){const auto v=gp_Pnt(q[0],q[1],q[2]).Transformed(frame);points.push_back(v);if(v.Z()>=lo&&v.Z()<=hi)add(v.X(),v.Y());}
        for(size_t a=0;a<points.size();++a)for(size_t b=a+1;b<points.size();++b){const auto x=points[a],y=points[b];if(std::abs(y.Z()-x.Z())<1e-15)continue;for(double z:{lo,hi}){const double t=(z-x.Z())/(y.Z()-x.Z());if(t>=0&&t<=1)add(x.X()+t*(y.X()-x.X()),x.Y()+t*(y.Y()-x.Y()));}}
        const auto hull=Hull(std::move(projected));if(hull.size()>=3)p.Forbidden.push_back(Polygon(hull));
    }
    if(p.Forbidden.empty())return;const auto removed=Common(p.Material,Compound(p.Forbidden),check);if(!Has(removed,TopAbs_FACE))return;
    Require(!(p.Data.Base&&in.SolidBase),"PLATE_BASE_PHYSICAL_CONFLICT");
    const auto remainder=Cut(p.Material,p.Forbidden,check);if(in.Automatic&&in.Pattern=="Skeleton"&&!p.Data.Base&&!p.Data.Connection)p.Material=Face(remainder,"PLATE_KEEPOUT_DISCONNECTED");else p.Material=Largest(remainder);
    if(p.Data.Base){const double radius=in.MinimumWeb/2;const auto core=Largest(Offset(p.Material,-radius));p.Material=Largest(Common(p.Material,Offset(core,radius),check));}
    Web(p.Material,in.MinimumWeb);
    if(!p.Data.Base){const double radius=in.MinimumWeb/2-Tol;const auto core=Face(Offset(p.Material,-radius),"PLATE_KEEPOUT_MINIMUM_WEB");p.BearingRegion=Common(p.Material,Offset(core,radius),check);}
    for(TopExp_Explorer e(removed,TopAbs_FACE);e.More();e.Next())++p.Data.KeepOutReliefCount;RefreshSpan(p);
}
void Foot(const Plate& p,const Plate& base,const SectionPlateInput& in,const Check& check){const auto b=Bounds(p.Material);const auto line=BRepBuilderAPI_MakeEdge(gp_Pnt(b[0]-1,0,0),gp_Pnt(b[3]+1,0,0)).Edge();const auto contact=Common(line,p.Material,check);double length=0;
    for(TopExp_Explorer e(contact,TopAbs_EDGE);e.More();e.Next()){const auto interval=Bounds(e.Current());std::vector<P2> footprint;const auto& m=p.Data.Frame;for(auto q:JointRect(interval[0],0,interval[3],p.Data.Thickness))footprint.push_back({m[0]*q[0]+m[2]*q[1]+m[3],m[4]*q[0]+m[6]*q[1]+m[7]});Require(Contains(base.Material,Polygon(footprint),check),"PLATE_KEEPOUT_FOOT_UNSUPPORTED");length+=interval[3]-interval[0];}
    Require(length>=in.MinimumWeb-Tol,"PLATE_KEEPOUT_FOOT_UNSUPPORTED");}
void Joints(std::vector<Plate>& plates,Plate& base,const SectionPlateInput& in,const Check& check){
    size_t next=0;const double half=(in.Thickness+in.JointClearance)/2,edge=in.JointWeb+in.JointClearance/2;std::vector<std::vector<TopoDS_Shape>> cuts(plates.size());std::vector<std::vector<P2>> footCuts(plates.size());
    for(size_t i=0;i<plates.size();++i)for(size_t j=i+1;j<plates.size();++j){check();const auto& a=plates[i];const auto& b=plates[j];const auto sa=in.Stations[a.Data.StationIndex],sb=in.Stations[b.Data.StationIndex];if(sa.AlongX==sb.AlongX)continue;const double ua=sb.Coordinate,ub=sa.Coordinate;
        if(ua+half<=a.Data.Span[0]||ua-half>=a.Data.Span[1]||ub+half<=b.Data.Span[0]||ub-half>=b.Data.Span[1])continue;
        if(ua-half<a.Data.Span[0]+in.JointWeb||ua+half>a.Data.Span[1]-in.JointWeb)throw PlateStationError(a.Data.StationIndex,"PLATE_LAP_EDGE_WEB");
        if(ub-half<b.Data.Span[0]+in.JointWeb||ub+half>b.Data.Span[1]-in.JointWeb)throw PlateStationError(b.Data.StationIndex,"PLATE_LAP_EDGE_WEB");
        if(std::getenv("ICAX_FIXTURE_JOINT_TRACE"))std::cerr<<"fixture joint heights "<<i<<","<<j<<std::endl;
        const double seat=std::min(JointHeight(a,ua-half,ua+half,check),JointHeight(b,ub-half,ub+half,check))/2;
        if(seat-in.JointClearance/2<in.JointWeb-Tol)throw PlateStationError(a.Data.StationIndex,"PLATE_LAP_MINIMUM_WEB");
        if(!in.KeepOuts.empty())for(const auto& [plate,u]:std::array<std::pair<const Plate*,double>,2>{{{&a,ua},{&b,ub}}}){
            const auto band=Rect(u-half-in.JointWeb,seat-in.JointWeb-in.JointClearance/2,u+half+in.JointWeb,seat+in.JointWeb+in.JointClearance/2);
            if(!Contains(plate->Material,band,check))throw PlateStationError(plate->Data.StationIndex,"PLATE_KEEPOUT_LAP_WEB");
        }
        ++next;const auto append=[&](size_t index,size_t mate,double u,bool bottom){auto& p=plates[index];const auto b=Bounds(p.Material);const auto boundary=JointRect(u-half,bottom?-in.BaseThickness-1:seat-in.JointClearance/2,u+half,bottom?seat+in.JointClearance/2:b[4]+1);
            cuts[index].push_back(Polygon(boundary));if(bottom)footCuts[index].push_back({u-half,u+half});p.Data.Joints.push_back({bottom?"LapBottom":"LapTop",next,plates[mate].Data.StationIndex,false,boundary});};
        append(i,j,ua,sa.AlongX);append(j,i,ub,!sa.AlongX);
    }
    if(std::getenv("ICAX_FIXTURE_JOINT_TRACE"))std::cerr<<"fixture joint cuts"<<std::endl;
    ForPlateIndices(plates.size(),in.MaximumConcurrency,[&](size_t i,size_t){auto& p=plates[i];try{p.Material=Face(Cut(p.Material,cuts[i],check),"PLATE_JOINT_DISCONNECTED");if(!in.KeepOuts.empty())Web(p.Material,in.JointWeb);}catch(const std::runtime_error&){
        if(const auto directory=std::getenv("ICAX_FIXTURE_BREP_DEBUG_DIR")){const auto stem=std::string(directory)+"/joint-"+std::to_string(p.Data.StationIndex);BRepTools::Write(p.Material,(stem+"-input.brep").c_str());BRepTools::Write(Compound(cuts[i]),(stem+"-cuts.brep").c_str());}
        throw PlateStationError(p.Data.StationIndex,"PLATE_JOINT_DISCONNECTED");}});
    if(in.JointMode=="Weld"){for(const auto& p:plates)Valid(p.Material,"PLATE_JOINT_INVALID",p.Data.StationIndex);return;}
    for(size_t i=0;i<plates.size();++i){if(std::getenv("ICAX_FIXTURE_JOINT_TRACE"))std::cerr<<"fixture joint tenons "<<i<<std::endl;auto& p=plates[i];std::vector<P2> intervals{{p.Data.Span[0]+edge,p.Data.Span[1]-edge}};for(auto cut:footCuts[i]){std::vector<P2> remaining;for(auto range:intervals){if(cut[1]+edge<=range[0]||cut[0]-edge>=range[1])remaining.push_back(range);else{if(cut[0]-edge>range[0])remaining.push_back({range[0],cut[0]-edge});if(cut[1]+edge<range[1])remaining.push_back({cut[1]+edge,range[1]});}}intervals=std::move(remaining);}
        size_t accepted=0;std::vector<TopoDS_Shape> tabs;
        for(auto interval:intervals){const double available=interval[1]-interval[0];if(available<in.TenonWidth-Tol)continue;const int count=1+static_cast<int>(std::floor((available-in.TenonWidth)/in.TenonPitch));Require(count<=128,"PLATE_JOINT_LIMIT");
            for(int n=0;n<count;++n){check();const double u=(interval[0]+interval[1])/2+(n-(count-1)/2.)*in.TenonPitch;const auto boundary=JointRect(u-in.TenonWidth/2,-in.BaseThickness,u+in.TenonWidth/2,0);const auto tab=Rect(u-in.TenonWidth/2,-in.BaseThickness,u+in.TenonWidth/2,in.JointWeb);
                if(!Contains(p.Material,Rect(u-in.TenonWidth/2,0,u+in.TenonWidth/2,in.JointWeb),check)){if(in.KeepOuts.empty())throw std::runtime_error("PLATE_JOINT_ABOVE_SUPPORT");continue;}
                if(!p.Forbidden.empty()&&Has(Common(tab,Compound(p.Forbidden),check),TopAbs_FACE))continue;
                std::vector<P2> hole;const auto& f=p.Data.Frame;for(auto q:JointRect(u-(in.TenonWidth+in.JointClearance)/2,-in.JointClearance/2,u+(in.TenonWidth+in.JointClearance)/2,in.Thickness+in.JointClearance/2))hole.push_back({f[0]*q[0]+f[2]*q[1]+f[3],f[4]*q[0]+f[6]*q[1]+f[7]});
                const auto mortise=Polygon(hole);if(!Contains(base.Material,Offset(mortise,in.JointWeb),check)){if(!in.KeepOuts.empty())continue;throw std::runtime_error("PLATE_MORTISE_MINIMUM_WEB");}
                Require(base.Data.Joints.size()<512,"PLATE_JOINT_LIMIT");base.Material=Face(Cut(base.Material,{mortise},check));tabs.push_back(tab);++accepted;++next;
                p.Data.Joints.push_back({"BaseTenon",next,0,true,boundary});base.Data.Joints.push_back({"BaseMortise",next,p.Data.StationIndex,false,hole});
            }
        }
        if(!accepted)throw PlateStationError(p.Data.StationIndex,"PLATE_TENON_NO_SPACE");p.Material=Face(Boolean<BRepAlgoAPI_Fuse>(p.Material,tabs,check));Valid(p.Material,"PLATE_JOINT_INVALID",p.Data.StationIndex);
    }
}
void RoundTips(Plate& p,const SectionPlateInput& in,const Check& check){
    if(in.JointMode!="TabSlot"||in.TenonRadius<=0||p.Data.Base)return;
    // Cut exact circular corner regions. Rebuilding an entire spline-bearing
    // face with Fillet2d can alter unrelated pcurves; these local booleans keep
    // the original support curves and every hole untouched.
    const double r=in.TenonRadius,pi=std::acos(-1.);std::vector<TopoDS_Shape> corners;
    for(const auto& joint:p.Data.Joints)if(joint.Type=="BaseTenon"){
        double left=INFINITY,right=-INFINITY,bottom=INFINITY;for(auto q:joint.Boundary){left=std::min(left,q[0]);right=std::max(right,q[0]);bottom=std::min(bottom,q[1]);}
        for(bool isLeft:{true,false}){const double x=isLeft?left:right,sign=isLeft?1.:-1.;const gp_Pnt a(x,bottom,0),b(x+sign*r,bottom,0),c(x,bottom+r,0),center(x+sign*r,bottom+r,0);
            const gp_Circ circle(gp_Ax2(center,gp_Dir(0,0,1)),r);auto arc=BRepBuilderAPI_MakeEdge(circle,isLeft?pi:1.5*pi,isLeft?1.5*pi:2*pi).Edge();if(isLeft)arc.Reverse();
            BRepBuilderAPI_MakeWire wire;wire.Add(BRepBuilderAPI_MakeEdge(a,b));wire.Add(arc);wire.Add(BRepBuilderAPI_MakeEdge(c,a));Require(wire.IsDone(),"PLATE_TENON_RADIUS_FAILED");BRepBuilderAPI_MakeFace face(Plane,wire.Wire(),true);Require(face.IsDone(),"PLATE_TENON_RADIUS_FAILED");corners.push_back(face.Face());
        }
    }
    if(!corners.empty())p.Material=Face(Cut(p.Material,corners,check),"PLATE_TENON_RADIUS_FAILED");
}

void Windows(Plate& p,const std::vector<Plate>& plates,const SectionPlateInput& in,const Check& check){
    if(in.WindowMode=="None"||(p.Data.Base&&in.SolidBase))return;const double web=std::max({in.WindowWeb,in.MinimumWeb,p.Data.Thickness,in.JointMode=="TabSlot"?in.JointWeb:0.});const auto b=Bounds(p.Material);
    if(b[3]-b[0]<=2*web||b[4]-b[1]<=2*web)return;
    // Bounds propose aperture locations only. Exact containment and distance
    // to every outer/inner BRep wire certify all retained material bands.
    TopoDS_Shape available=Rect(b[0]+web,b[1]+web,b[3]-web,b[4]-web);std::vector<TopoDS_Shape> reserved;
    std::vector<TopoDS_Shape> boundaries;for(const auto& wire:Wires(p.Material))boundaries.push_back(wire);const auto boundary=Compound(boundaries);
    const auto fits=[&](const TopoDS_Face& opening){return Contains(p.Material,opening,check)&&Distance(BRepTools::OuterWire(opening),boundary,check)>=web-Tol;};
    for(const auto& joint:p.Data.Joints){auto boundary=joint.Boundary;if(joint.Type=="LapTop"||joint.Type=="LapBottom"){double lo=INFINITY,hi=-INFINITY;for(auto q:boundary){lo=std::min(lo,q[0]);hi=std::max(hi,q[0]);}boundary=JointRect(lo,b[1]-web,hi,b[4]+web);}reserved.push_back(Offset(Polygon(boundary),web));}
    if(p.Data.Base)for(const auto& other:plates){const auto& d=other.Data;std::vector<P2> footprint;const auto& f=d.Frame;for(auto q:JointRect(d.Span[0],0,d.Span[1],d.Thickness))footprint.push_back({f[0]*q[0]+f[2]*q[1]+f[3],f[4]*q[0]+f[6]*q[1]+f[7]});reserved.push_back(Offset(Polygon(footprint),web));}
    else if(in.JointMode=="Weld"){
        const auto& station=in.Stations[p.Data.StationIndex];
        for(const auto& other:plates){check();const auto& crossing=in.Stations[other.Data.StationIndex];const double half=in.Thickness/2;
            if(crossing.AlongX==station.AlongX||crossing.Coordinate+half<p.Data.Span[0]-Tol||crossing.Coordinate-half>p.Data.Span[1]+Tol||station.Coordinate+half<other.Data.Span[0]-Tol||station.Coordinate-half>other.Data.Span[1]+Tol)continue;
            reserved.push_back(Rect(crossing.Coordinate-half-web,b[1]-web,crossing.Coordinate+half+web,b[4]+web));
        }
    }
    if(!reserved.empty())available=Cut(available,reserved,check);if(!Has(available,TopAbs_FACE))return;
    std::vector<TopoDS_Face> candidates;
    if(in.CountLayout){const double wantedWidth=std::min(p.Data.Connection?120.:80.,(p.Data.Span[1]-p.Data.Span[0])*.45),wantedHeight=std::min(p.Data.Connection?12.:25.,(b[4]-std::max(0.,b[1]))*.35),cx=(p.Data.Span[0]+p.Data.Span[1])/2,cy=(b[4]+std::max(0.,b[1]))/2;double bestArea=0,bestDistance=INFINITY;TopoDS_Face best;
        for(TopExp_Explorer regions(available,TopAbs_FACE);regions.More();regions.Next()){const auto region=TopoDS::Face(regions.Current());const auto r=Bounds(region);
            for(double scale:{1.,.8,.6}){const double width=std::min(wantedWidth,r[3]-r[0]-2*Tol)*scale,height=std::min(wantedHeight,r[4]-r[1]-2*Tol)*scale;if(std::min(width,height)<=2*p.Data.Thickness+.01)continue;
                for(double fy:{.5,.25,.75,.1,.9})for(double fx:{.5,.25,.75}){check();const double x=r[0]+width/2+(r[3]-r[0]-width)*fx,y=r[1]+height/2+(r[4]-r[1]-height)*fy,area=width*height,distance=(x-cx)*(x-cx)+(y-cy)*(y-cy);if(area<bestArea-1e-8||(std::abs(area-bestArea)<=1e-8&&distance>=bestDistance))continue;
                    const auto opening=Rect(x-width/2,y-height/2,x+width/2,y+height/2);if(!Contains(region,opening,check)||!fits(opening))continue;best=opening;bestArea=area;bestDistance=distance;}
            }
        }if(!best.IsNull())candidates.push_back(best);
    }else{
        const auto r=Bounds(available);const auto cells=[&](double a,double z){const int count=std::max(1,static_cast<int>(std::ceil((z-a+web)/(5*web))));Require(count<=8192,"PLATE_WINDOW_LIMIT");const double width=(z-a-(count-1)*web)/count;std::vector<P2> result;for(int i=0;i<count;++i)result.push_back({a+i*(width+web),a+i*(width+web)+width});return result;};const auto xs=cells(r[0],r[3]),ys=cells(r[1],r[4]);Require(xs.size()*ys.size()<=8192,"PLATE_WINDOW_LIMIT");
        for(auto x:xs)for(auto y:ys){const auto pieces=Common(available,Rect(x[0],y[0],x[1],y[1]),check);for(TopExp_Explorer e(pieces,TopAbs_FACE);e.More();e.Next())candidates.push_back(TopoDS::Face(e.Current()));}
    }
    for(const auto& candidate:candidates){check();const auto size=Bounds(candidate);if(std::min(size[3]-size[0],size[4]-size[1])<=2*p.Data.Thickness)continue;if(!fits(candidate))continue;BRepBuilderAPI_MakeFace perforated(p.Material);perforated.Add(TopoDS::Wire(BRepTools::OuterWire(candidate).Reversed()));Require(perforated.IsDone(),"PLATE_WINDOW_DISCONNECTED");p.Material=perforated.Face();p.Windows.push_back(candidate);}
}
// These polylines are terminal preview data only. No operation above reads
// SectionPlate::Outline or Holes to construct or validate geometry.
std::vector<P2> Preview(const TopoDS_Wire& wire){std::vector<P2> result;for(BRepTools_WireExplorer e(wire);e.More();e.Next()){BRepAdaptor_Curve curve(e.Current());GCPnts_QuasiUniformDeflection samples(curve,.05);Require(samples.IsDone(),"PLATE_DISPLAY_CURVE_FAILED");const bool reverse=e.Current().Orientation()==TopAbs_REVERSED;for(int i=1;i<samples.NbPoints();++i){const auto p=samples.Value(reverse?samples.NbPoints()+1-i:i);result.push_back({p.X(),p.Y()});}}return result;}
TopoDS_Face RefineProfileJunctions(const TopoDS_Face& face,const Check& check){
    // Boolean topology may join curves within a source vertex's tolerance
    // while leaving their trim parameters apart. Intersect the original 2D
    // curves and retrim at the shared vertex, before making either the solid
    // or the manufacturing profile. Never fit, snap or move control points.
    struct Edge {TopoDS_Edge Source;occ::handle<Geom_Curve> Geometry;double First,Last;bool Reversed;};
    std::vector<TopoDS_Wire> wires;bool changed=false;
    for(const auto& wire:Wires(face)){
        std::vector<Edge> edges;for(BRepTools_WireExplorer e(wire);e.More();e.Next()){TopLoc_Location location;double first,last;auto curve=BRep_Tool::Curve(e.Current(),location,first,last);Require(!curve.IsNull(),"PLATE_PROFILE_CURVE_INVALID");if(!location.IsIdentity())curve=occ::down_cast<Geom_Curve>(curve->Transformed(location.Transformation()));while(const auto trimmed=occ::down_cast<Geom_TrimmedCurve>(curve)){curve=trimmed->BasisCurve();}edges.push_back({e.Current(),curve,first,last,e.Current().Orientation()==TopAbs_REVERSED});}
        bool refined=false;std::vector<bool> refinedJunction(edges.size(),false);
        for(size_t i=0;i<edges.size();++i){check();auto& a=edges[i];auto& b=edges[(i+1)%edges.size()];auto& ta=a.Reversed?a.First:a.Last;auto& tb=b.Reversed?b.Last:b.First;
            if(a.Geometry->Value(ta).Distance(b.Geometry->Value(tb))<=Tol)continue;
            const auto vertex=TopExp::LastVertex(a.Source,true);const auto anchor=BRep_Tool::Pnt(vertex);const double allowance=BRep_Tool::Tolerance(vertex);
            Geom2dAPI_InterCurveCurve intersection(GeomAPI::To2d(a.Geometry,Plane),GeomAPI::To2d(b.Geometry,Plane),Tol*.01);Require(intersection.Intersector().IsDone(),"PLATE_PROFILE_JUNCTION_FAILED");
            double best=INFINITY,at=0,bt=0;
            const auto consider=[&](double u,double v){const auto p=a.Geometry->Value(u),q=b.Geometry->Value(v);const double distance=std::max(p.Distance(anchor),q.Distance(anchor));
                if(p.Distance(q)>Tol*.01||distance>allowance||distance>=best||(a.Reversed?u>=a.Last:u<=a.First)||(b.Reversed?v<=b.First:v>=b.Last))return;best=distance;at=u;bt=v;};
            for(int k=1;k<=intersection.NbPoints();++k){const auto& point=intersection.Intersector().Point(k);consider(point.ParamOnFirst(),point.ParamOnSecond());}
            // Coincident curves have no unique junction to retrim. Preserve
            // their original shared vertex and its existing tolerance; moving
            // the trim can collapse a narrow but valid adjacent surface patch.
            if(!std::isfinite(best)&&intersection.NbSegments()>0){Require(a.Geometry->Value(ta).Distance(anchor)<=allowance&&b.Geometry->Value(tb).Distance(anchor)<=allowance,"PLATE_PROFILE_JUNCTION_FAILED");continue;}
            if(!std::isfinite(best)){
                if(std::getenv("ICAX_FIXTURE_TRACE"))std::cerr<<"fixture junction gap="<<std::setprecision(17)<<a.Geometry->Value(ta).Distance(b.Geometry->Value(tb))<<" allowance="<<allowance<<" anchor="<<anchor.X()<<","<<anchor.Y()<<" points="<<intersection.NbPoints()<<" intervals="<<intersection.NbSegments()<<" edge="<<i<<" first="<<a.First<<","<<a.Last<<" second="<<b.First<<","<<b.Last<<std::endl;
                if(const auto directory=std::getenv("ICAX_FIXTURE_BREP_DEBUG_DIR")){static std::atomic_size_t sequence{0};const auto stem=std::string(directory)+"/junction-"+std::to_string(sequence.fetch_add(1));BRepTools::Write(face,(stem+"-face.brep").c_str());BRepTools::Write(Compound({a.Source,b.Source}),(stem+"-edges.brep").c_str());}
            }
            Require(std::isfinite(best),"PLATE_PROFILE_JUNCTION_NOT_INTERSECTING");ta=at;tb=bt;refined=true;refinedJunction[i]=true;
        }
        if(!refined){wires.push_back(wire);continue;}BRepBuilderAPI_MakeWire rebuilt;std::vector<TopoDS_Vertex> starts;
        for(size_t i=0;i<edges.size();++i){const auto& edge=edges[i];starts.push_back(refinedJunction[(i+edges.size()-1)%edges.size()]?BRepBuilderAPI_MakeVertex(edge.Geometry->Value(edge.Reversed?edge.Last:edge.First)).Vertex():TopExp::FirstVertex(edge.Source,true));}
        for(size_t i=0;i<edges.size();++i){const auto& edge=edges[i];const auto first=starts[i],last=starts[(i+1)%edges.size()];BRepBuilderAPI_MakeEdge made(edge.Geometry,edge.Reversed?last:first,edge.Reversed?first:last,edge.First,edge.Last);Require(made.IsDone(),"PLATE_PROFILE_JUNCTION_FAILED");auto value=made.Edge();BRep_Builder builder;builder.UpdateEdge(value,BRep_Tool::Tolerance(edge.Source));if(edge.Reversed)value.Reverse();rebuilt.Add(value);}
        Require(rebuilt.IsDone(),"PLATE_PROFILE_JUNCTION_FAILED");wires.push_back(rebuilt.Wire());changed=true;
    }
    if(!changed)return face;BRepBuilderAPI_MakeFace rebuilt(Plane,wires.front(),true);for(size_t i=1;i<wires.size();++i)rebuilt.Add(wires[i]);Require(rebuilt.IsDone()&&BRepCheck_Analyzer(rebuilt.Face()).IsValid(),"PLATE_PROFILE_JUNCTION_FAILED");return rebuilt.Face();
}
void PrepareFinish(Plate& p,const SectionPlateInput& in,const Check& check,const std::function<void(const std::string&)>& phase){Valid(p.Material,"PLATE_FACE_BEFORE_ROUND_INVALID",p.Data.StationIndex);RoundTips(p,in,check);p.Material=RefineProfileJunctions(p.Material,check);phase("rounding complete");Require(BRepCheck_Analyzer(p.Material).IsValid(),"PLATE_TENON_ROUND_FACE_INVALID");Contacts(p,check);phase("contacts complete");if(!p.Data.Base&&!p.Data.Connection)Require(!p.Data.Contacts.empty(),"PLATE_JOINT_CONTACT_LOST");
    p.Solid=BRepPrimAPI_MakePrism(p.Material,gp_Vec(0,0,p.Data.Thickness)).Shape();Valid(p.Solid,"PLATE_SOLID_INVALID",p.Data.StationIndex);
}
std::vector<TopoDS_Shape> CheckCopies(const std::vector<TopoDS_Shape>& sources){
    // Each independent kernel query owns its topology AND geometry. Copy a
    // compound once so aliases within that query (cap/prism/source) survive.
    BRepBuilderAPI_Copy copy(Compound(sources),true,false);Require(copy.IsDone(),"PLATE_BREP_COPY_FAILED");std::vector<TopoDS_Shape> result;result.reserve(sources.size());
    for(const auto& source:sources){const auto value=source.IsNull()?TopoDS_Shape{}:copy.ModifiedShape(source);Require(source.IsNull()||!value.IsNull(),"PLATE_BREP_COPY_FAILED");result.push_back(value);}return result;
}
void CheckStock(const Plate& p,const Check& check){
    if(!p.BlankRemainder.IsNull()){const auto copies=CheckCopies({p.Solid,p.Blank,p.VolumeWorkInSlab});
        // P \ (B \ W) = (P \ B) union ((P intersect B) intersect W).
        // The two terms are disjoint. Query the original stock and workpiece
        // instead of subtracting an already cut, nearly coincident BRep: that
        // nested difference can produce an invalid shell on repaired skins.
        // Both exact Boolean results must be valid before measuring volume.
        // P is extruded over [0, Thickness]. VolumeWorkInSlab contains ALL
        // original workpiece material in that domain, with its artificial
        // caps strictly outside P, so intersecting it is
        // equivalent to intersecting the full workpiece, including every void.
        const auto outside=Cut(copies[0],{copies[1]},check);Valid(outside,"PLATE_BREP_STOCK_CHECK_INVALID",p.Data.StationIndex);
        auto inside=copies[0];if(Has(outside,TopAbs_SOLID)){inside=Common(copies[0],copies[1],check);Valid(inside,"PLATE_BREP_STOCK_CHECK_INVALID",p.Data.StationIndex);}
        const auto overlap=Common(inside,copies[2],check);Valid(overlap,"PLATE_BREP_STOCK_CHECK_INVALID",p.Data.StationIndex);
        const auto measure=[](const TopoDS_Shape& shape){double volume=0;for(TopExp_Explorer s(shape,TopAbs_SOLID);s.More();s.Next()){GProp_GProps mass;BRepGProp::VolumeProperties(s.Current(),mass);volume+=std::abs(mass.Mass());}return volume;};
        const double outsideVolume=measure(outside),overlapVolume=measure(overlap),volume=outsideVolume+overlapVolume;
        if(volume>1e-7){
            if(std::getenv("ICAX_FIXTURE_TRACE")){
                std::cerr<<std::setprecision(17)<<"fixture stock outside station="<<p.Data.StationIndex<<" volume="<<volume<<" outsideBlankVolume="<<outsideVolume<<" workpieceOverlapVolume="<<overlapVolume<<std::endl;
            }
            if(const auto directory=std::getenv("ICAX_FIXTURE_BREP_DEBUG_DIR")){
                const auto stem=std::string(directory)+"/stock-"+std::to_string(p.Data.StationIndex);
                BRepTools::Write(copies[0],(stem+"-solid.brep").c_str());BRepTools::Write(copies[1],(stem+"-blank.brep").c_str());BRepTools::Write(outside,(stem+"-outside.brep").c_str());BRepTools::Write(overlap,(stem+"-overlap.brep").c_str());
            }
        }
        Require(volume<=1e-7,"PLATE_FLATTENING_OUTSIDE_STOCK");}
}
double CheckDistance(const Plate& p,const SectionPlateInput& in,const Check& check){
    if(p.Data.Base)return p.Data.Distance;
    {// The plate is contained in this exact slab. A zero distance is
        // already the global minimum; for an expanded slab a result below
        // its padding cannot come from an artificial cap or excluded CAD.
        // Contacts are evaluated on the original trimmed surface. If one is
        // also on the exact prism boundary, it certifies the global minimum
        // distance as zero. This avoids the generic solid classifier; the
        // independent full-thickness penetration check below still runs.
        bool contact=false;const auto toLocal=PlateDirectionFrame::Transform(p.Data.Frame).Inverted();const auto profile=TopoDS::Face(CheckCopies({p.Material}).front());
        for(const auto& value:p.Data.Contacts){const auto point=gp_Pnt(value[0],value[1],value[2]).Transformed(toLocal);if(point.Z()<-Tol||point.Z()>p.Data.Thickness+Tol)continue;BRepClass_FaceClassifier onProfile(profile,gp_Pnt(point.X(),point.Y(),0),Tol);if(onProfile.State()==TopAbs_ON){contact=true;break;}}
        double distance=0;if(!contact){const auto copies=CheckCopies({p.Solid,Has(p.WorkInSlab,TopAbs_VERTEX)?p.WorkInSlab:p.Work});distance=Distance(copies[0],copies[1],check);}
        if(p.Data.Connection?distance>=p.QueryRadius-Tol:distance>Tol){const auto copies=CheckCopies({p.Solid,p.Work});distance=Distance(copies[0],copies[1],check);}
        if(p.Data.Connection)Require(distance+Tol>=in.Gap,"PLATE_CONNECTION_CLEARANCE");else Require(distance<=1e-5,"PLATE_SUPPORT_NO_CONTACT");return distance;
    }
}
void CheckPenetration(const Plate& p,const Check& check){
    if(p.Data.Base)return;const auto copies=CheckCopies({p.Material,p.Solid,p.WorkInSlab,p.VolumeWorkInSlab});NoPenetration(TopoDS::Face(copies[0]),p.Data.Thickness,copies[1],copies[2],copies[3],check,p.Data.StationIndex);
}
void FinishProfile(Plate& p,const Check& check){
    p.Data.ExactProfile=ConstructedProfile(p.Material,{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1});
    const auto wires=Wires(p.Material);for(size_t i=1;i<wires.size();++i){const auto face=BRepBuilderAPI_MakeFace(Plane,wires[i],true).Face();for(const auto& window:p.Windows)if(Has(Common(face,window,check),TopAbs_FACE)){p.Data.WindowHoleIndices.push_back(i-1);break;}}
}
void Export(Plate& p,const gp_Trsf& toWorld,const Check& check){
    check();
    p.Data.Geometry=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRepGeometry(p.Solid.Moved(TopLoc_Location(toWorld*PlateDirectionFrame::Transform(p.Data.Frame))),"Section plate","generated");
    const auto wires=Wires(p.Material);p.Data.Outline=Preview(wires.front());for(size_t i=1;i<wires.size();++i)p.Data.Holes.push_back(Preview(wires[i]));
}
}
