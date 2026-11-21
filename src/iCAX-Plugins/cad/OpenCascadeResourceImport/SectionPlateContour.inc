// A silhouette solves (Su x Sv).Z = 0 on the ORIGINAL surface. Contap's
// tracing points locate branches and seed Newton; they never become a wire,
// polyline or the evaluated geometric answer. Every evaluation below solves
// that equation again, with analytic surface derivatives.
class ContourEquation final : public Adaptor2d_Curve2d {
    occ::handle<Adaptor3d_Surface> Surface;
    std::vector<gp_Pnt2d> Seeds;
    bool AlongU;
    double First,Last,Mix=0;
    Check Cancel;
    double Independent(const gp_Pnt2d& p)const{return AlongU?p.X()+Mix*p.Y():p.Y()+Mix*p.X();}
    double Dependent(const gp_Pnt2d& p)const{return AlongU?p.Y():p.X();}
public:
    ContourEquation(const occ::handle<Adaptor3d_Surface>& surface,std::vector<gp_Pnt2d> seeds,bool alongU,const Check& check)
        :Surface(surface),Seeds(std::move(seeds)),AlongU(alongU),Cancel(check){
        // At a collapsed parameter boundary, mixing the two coordinates
        // introduces the whole pole boundary as a competing Newton root.
        // Use the non-collapsed coordinate there and the analytic limiting
        // normal equation below. This also makes a 180-degree rotation
        // stable when its sine is a small floating-point residual.
        bool poleChart=false;
        const auto boundary=[](double x,double a,double b){return std::abs(x-a)<=Precision::PConfusion()||std::abs(x-b)<=Precision::PConfusion();};
        for(const auto& seed:{Seeds.front(),Seeds.back()}){gp_Pnt point;gp_Vec du,dv;Surface->D1(seed.X(),seed.Y(),point,du,dv);
            if(du.Magnitude()<=1e-12*std::max(1.,dv.Magnitude())&&boundary(seed.Y(),Surface->FirstVParameter(),Surface->LastVParameter())){AlongU=false;poleChart=true;}
            else if(dv.Magnitude()<=1e-12*std::max(1.,du.Magnitude())&&boundary(seed.X(),Surface->FirstUParameter(),Surface->LastUParameter())){AlongU=true;poleChart=true;}}
        const auto delta=gp_Vec2d(Seeds.front(),Seeds.back());if(!poleChart&&std::abs(delta.X())>1e-14&&std::abs(delta.Y())>1e-14)Mix=std::copysign(AlongU?surface->UResolution(1.)/surface->VResolution(1.):surface->VResolution(1.)/surface->UResolution(1.),delta.X()*delta.Y());
        if(Independent(Seeds.front())>Independent(Seeds.back()))std::reverse(Seeds.begin(),Seeds.end());
        First=Independent(Seeds.front());Last=Independent(Seeds.back());
    }
    occ::handle<Adaptor2d_Curve2d> ShallowCopy()const override{return new ContourEquation(*this);}
    occ::handle<Adaptor2d_Curve2d> Trim(double first,double last,double)const override{auto copy=new ContourEquation(*this);copy->First=first;copy->Last=last;return copy;}
    double FirstParameter()const override{return First;}
    double LastParameter()const override{return Last;}
    GeomAbs_Shape Continuity()const override{return GeomAbs_C1;}
    int NbIntervals(GeomAbs_Shape)const override{return 1;}
    void Intervals(NCollection_Array1<double>& values,GeomAbs_Shape)const override{values(values.Lower())=First;values(values.Lower()+1)=Last;}
    bool IsClosed()const override{return false;}
    bool IsPeriodic()const override{return false;}
    GeomAbs_CurveType GetType()const override{return GeomAbs_OtherCurve;}
    double Resolution(double r)const override{return std::min(Surface->UResolution(r),Surface->VResolution(r));}
    gp_Pnt2d Value(double t)const override{gp_Pnt2d p;D0(t,p);return p;}
    void D0(double t,gp_Pnt2d& p)const override{gp_Vec2d derivative;D1(t,p,derivative);}
    void D1(double t,gp_Pnt2d& p,gp_Vec2d& derivative)const override{
        Cancel();const auto upper=std::upper_bound(Seeds.begin(),Seeds.end(),t,[&](double v,const gp_Pnt2d& seed){return v<Independent(seed);});
        const size_t index=std::clamp<size_t>(upper-Seeds.begin(),1,Seeds.size()-1);
        const auto a=Seeds[index-1],b=Seeds[index];const double fraction=(t-Independent(a))/(Independent(b)-Independent(a));
        double value=Dependent(a)+(Dependent(b)-Dependent(a))*fraction;
        const double low=AlongU?Surface->FirstVParameter():Surface->FirstUParameter(),high=AlongU?Surface->LastVParameter():Surface->LastUParameter();
        for(int iteration=0;iteration<30;++iteration){
            p=AlongU?gp_Pnt2d(t-Mix*value,value):gp_Pnt2d(value,t-Mix*value);gp_Pnt point;gp_Vec du,dv,duu,dvv,duv;
            Surface->D2(p.X(),p.Y(),point,du,dv,duu,dvv,duv);
            double f=du.Crossed(dv).Z(),fu=(duu.Crossed(dv)+du.Crossed(duv)).Z(),fv=(duv.Crossed(dv)+du.Crossed(dvv)).Z();
            double divisor=AlongU?fv-Mix*fu:fu-Mix*fv;bool pole=false;
            // At a collapsed parameter boundary the normal equation has a
            // common zero factor. Use its analytic limiting equation rather
            // than a nearby sampled section or a fitted endpoint tangent.
            {const auto boundary=[](double x,double a,double b){return std::abs(x-a)<=Precision::PConfusion()||std::abs(x-b)<=Precision::PConfusion();};
                const bool collapsedU=du.Magnitude()<=1e-12*std::max(1.,dv.Magnitude())&&boundary(p.Y(),Surface->FirstVParameter(),Surface->LastVParameter());
                const bool collapsedV=dv.Magnitude()<=1e-12*std::max(1.,du.Magnitude())&&boundary(p.X(),Surface->FirstUParameter(),Surface->LastUParameter());
                if(collapsedU||collapsedV){gp_Vec duuu,dvvv,duuv,duvv;Surface->D3(p.X(),p.Y(),point,du,dv,duu,dvv,duv,duuu,dvvv,duuv,duvv);
                    const double fuv=(duuv.Crossed(dv)+duu.Crossed(dvv)+du.Crossed(duvv)).Z();
                    if(collapsedU){f=fv;fu=fuv;fv=(duvv.Crossed(dv)+2*duv.Crossed(dvv)+du.Crossed(dvvv)).Z()/2;}
                    else{f=fu;fu=(duuu.Crossed(dv)+2*duu.Crossed(duv)+du.Crossed(duuv)).Z()/2;fv=fuv;}
                    divisor=AlongU?fv-Mix*fu:fu-Mix*fv;pole=true;
                }
            }
            if(!(std::isfinite(divisor)&&std::abs(divisor)>1e-25)&&std::getenv("ICAX_FIXTURE_TRACE"))std::cerr<<"fixture singular contour u="<<p.X()<<" v="<<p.Y()<<" f="<<f<<" fu="<<fu<<" fv="<<fv<<" mix="<<Mix<<std::endl;
            Require(std::isfinite(divisor)&&std::abs(divisor)>1e-25,"PLATE_CONTOUR_SINGULAR");
            const double step=f/divisor;const double spatialStep=std::abs(step)*(AlongU?dv-du*Mix:du-dv*Mix).Magnitude();
            if(spatialStep<=Tol*.01&&(!pole||std::abs(step)<=(AlongU?Surface->VResolution(Tol*.01):Surface->UResolution(Tol*.01)))){const double slope=-(AlongU?fu:fv)/divisor;derivative=AlongU?gp_Vec2d(1-Mix*slope,slope):gp_Vec2d(slope,1-Mix*slope);return;}
            const double next=std::clamp(value-step,low,high);
            if((next==value||iteration==29)&&std::getenv("ICAX_FIXTURE_TRACE"))std::cerr<<"fixture contour root t="<<std::setprecision(17)<<t<<" uv="<<p.X()<<","<<p.Y()<<" value="<<value<<" step="<<step<<" spatialStep="<<spatialStep<<" bounds="<<low<<","<<high<<" pole="<<pole<<" residual="<<f<<" divisor="<<divisor<<std::endl;
            Require(next!=value,"PLATE_CONTOUR_ROOT_FAILED");value=next;
        }throw std::runtime_error("PLATE_CONTOUR_ROOT_FAILED");
    }
};

TopoDS_Edge OriginalSectionIso(const TopoDS_Edge& edge,const TopoDS_Face& face){
    double first,last;const auto pc=BRep_Tool::CurveOnSurface(edge,face,first,last);if(pc.IsNull())return edge;
    const Geom2dAdaptor_Curve adaptor(pc,first,last);std::vector<gp_Pnt2d> controls;
    if(adaptor.GetType()==GeomAbs_Line)controls={pc->Value(first),pc->Value(last)};
    else if(adaptor.GetType()==GeomAbs_BSplineCurve){auto spline=occ::down_cast<Geom2d_BSplineCurve>(adaptor.BSpline()->Copy());spline->Segment(first,last);for(int i=1;i<=spline->NbPoles();++i){if(spline->Weight(i)<=0)return edge;controls.push_back(spline->Pole(i));}}
    else return edge;
    double u0=INFINITY,u1=-INFINITY,v0=INFINITY,v1=-INFINITY;for(const auto& p:controls){u0=std::min(u0,p.X());u1=std::max(u1,p.X());v0=std::min(v0,p.Y());v1=std::max(v1,p.Y());}
    const double roundoff=64*std::numeric_limits<double>::epsilon()*std::max({1.,std::abs(u0),std::abs(u1),std::abs(v0),std::abs(v1)});
    const bool constantU=u1-u0<=roundoff,constantV=v1-v0<=roundoff;if(constantU==constantV)return edge;
    const auto a=pc->Value(first),b=pc->Value(last);const auto coordinate=[&](const gp_Pnt2d& p){return constantU?p.Y():p.X();};const double begin=coordinate(a),end=coordinate(b);if(std::abs(end-begin)<=roundoff)return edge;
    for(size_t i=1;i<controls.size();++i)if((coordinate(controls[i])-coordinate(controls[i-1]))*(end-begin)<-roundoff*std::abs(end-begin))return edge;
    // The Boolean section supplies the trimmed interval. For a proven iso,
    // use the original surface curve itself, with its native parameterization.
    // Fitted section vertices can carry much larger tolerances and snap an
    // adjacent steep profile to the wrong height despite a valid BRep.
    const double fixed=constantU?(u0+u1)/2:(v0+v1)/2;const auto surface=BRep_Tool::Surface(face);const auto geometry=constantU?surface->UIso(fixed):surface->VIso(fixed);
    const double low=std::min(begin,end),high=std::max(begin,end);BRepBuilderAPI_MakeEdge made(geometry,low,high);Require(made.IsDone(),"PLATE_CONTOUR_CURVE_FAILED");auto result=made.Edge();
    const occ::handle<Geom2d_Curve> uv=constantU?new Geom2d_Line(gp_Pnt2d(fixed,0),gp_Dir2d(0,1)):new Geom2d_Line(gp_Pnt2d(0,fixed),gp_Dir2d(1,0));BRep_Builder builder;builder.UpdateEdge(result,uv,face,Tol);builder.Range(result,face,low,high);return result;
}
std::vector<TopoDS_Edge> SurfaceContours(const TopoDS_Face& face,const Check& check){
    BRepTopAdaptor_Tool tool(face,Precision::PConfusion());const auto surface=tool.GetSurface();
    const bool trace=std::getenv("ICAX_FIXTURE_CONTOUR_TRACE")!=nullptr;
    static std::atomic_size_t faceSequence{0};const size_t faceNumber=trace?faceSequence.fetch_add(1):0;
    if(trace){
        double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
        std::cerr<<std::setprecision(17)<<"fixture contour face="<<faceNumber<<" type="<<surface->GetType()<<" trimUV="<<u0<<','<<u1<<','<<v0<<','<<v1
            <<" adaptorUV="<<surface->FirstUParameter()<<','<<surface->LastUParameter()<<','<<surface->FirstVParameter()<<','<<surface->LastVParameter()<<std::endl;
        if(const auto probe=std::getenv("ICAX_FIXTURE_CONTOUR_PROBE_UV")){
            std::string text(probe);std::replace(text.begin(),text.end(),',',' ');std::istringstream input(text);double u,v;
            Require(bool(input>>u>>v),"PLATE_CONTOUR_PROBE_UV_INVALID");
            if(u>=u0&&u<=u1&&v>=v0&&v<=v1)if(const auto directory=std::getenv("ICAX_FIXTURE_BREP_DEBUG_DIR")){
                const auto path=std::string(directory)+"/contour-face-"+std::to_string(faceNumber)+".brep";BRepTools::Write(face,path.c_str());
                std::cerr<<"fixture contour probe face="<<faceNumber<<" path="<<path<<std::endl;
            }
        }
    }
    // For a revolution viewed across its axis, the outline is a meridian
    // section plus circles at axial extrema of the generating curve. Obtain
    // these from BRep sections instead of solving a singular normal at poles.
    if(surface->GetType()==GeomAbs_SurfaceOfRevolution){auto geometry=BRep_Tool::Surface(face);if(const auto trimmed=occ::down_cast<Geom_RectangularTrimmedSurface>(geometry);!trimmed.IsNull())geometry=trimmed->BasisSurface();const auto revolution=occ::down_cast<Geom_SurfaceOfRevolution>(geometry);
        if(!revolution.IsNull()){const auto axis=revolution->Axis();const auto direction=axis.Direction();
            if(std::abs(direction.Z())<=64*std::numeric_limits<double>::epsilon()){
                std::vector<TopoDS_Edge> result;const auto section=[&](const gp_Pln& plane){check();BRepAlgoAPI_Section cut(face,plane,false);cut.SetNonDestructive(true);cut.SetRunParallel(false);cut.ComputePCurveOn1(true);cut.Build();check();Require(cut.IsDone(),"PLATE_CONTOUR_SECTION_FAILED");for(TopExp_Explorer e(cut.Shape(),TopAbs_EDGE);e.More();e.Next()){const auto edge=TopoDS::Edge(e.Current());if(BRep_Tool::Degenerated(edge))continue;double a,b;Require(!BRep_Tool::CurveOnSurface(edge,face,a,b).IsNull(),"PLATE_CONTOUR_PCURVE_MISSING");result.push_back(OriginalSectionIso(edge,face));}};
                section(gp_Pln(axis.Location(),gp_Dir(gp_Vec(0,0,1)-gp_Vec(direction)*direction.Z())));
                const auto basis=revolution->BasisCurve();const double first=surface->FirstVParameter(),last=surface->LastVParameter();const auto box=Bounds(BRepBuilderAPI_MakeEdge(basis,first,last).Edge());double low=INFINITY,high=-INFINITY;for(int corner=0;corner<8;++corner){const gp_Pnt p(box[(corner&1)?3:0],box[(corner&2)?4:1],box[(corner&4)?5:2]);const double height=gp_Vec(axis.Location(),p).Dot(gp_Vec(direction));low=std::min(low,height);high=std::max(high,height);}
                if(high-low>Tol){GeomAPI_ExtremaCurveSurface extrema(new Geom_TrimmedCurve(basis,first,last),new Geom_Plane(gp_Pln(axis.Location().Translated(gp_Vec(direction)*(low-1)),direction)));Require(extrema.Extrema().IsDone(),"PLATE_CONTOUR_EXTREMA_FAILED");for(int i=1;i<=extrema.NbExtrema();++i){double t,u,v;extrema.Parameters(i,t,u,v);if(t>first+Precision::PConfusion()&&t<last-Precision::PConfusion())section(gp_Pln(basis->Value(t),direction));}}
                return result;
            }
        }
    }
    // The polynomial path proves absence/regular graph coverage on original
    // knot cells. Unsupported surface/degenerate classes retain the existing
    // path; an unresolved applicable cell is an error, never a no-root claim.
    if(auto certified=regular_contour::Build(face,check))return std::move(*certified);
    Contap_Contour contour(surface,tool.GetTopolTool(),gp_Vec(0,0,1));check();Require(contour.IsDone(),"PLATE_CONTOUR_FAILED");std::vector<TopoDS_Edge> result;int activeLine=0;
    if(trace)std::cerr<<"fixture contour face="<<faceNumber<<" lines="<<contour.NbLines()<<std::endl;
    const auto append=[&](const occ::handle<Geom_Curve>& curve,const occ::handle<Geom2d_Curve>& uv,double first,double last){
        Require(!curve.IsNull()&&!uv.IsNull(),"PLATE_CONTOUR_CURVE_FAILED");BRepBuilderAPI_MakeEdge edge(curve,first,last);Require(edge.IsDone(),"PLATE_CONTOUR_CURVE_FAILED");auto value=edge.Edge();BRep_Builder builder;builder.UpdateEdge(value,uv,face,Tol);builder.Range(value,face,first,last);result.push_back(value);
        if(trace){
            try{const auto bounds=Bounds(value);std::cerr<<std::setprecision(17)<<"fixture contour emit face="<<faceNumber<<" line="<<activeLine<<" range="<<first<<','<<last<<" bounds=";for(double x:bounds)std::cerr<<x<<',';std::cerr<<std::endl;}
            catch(const Standard_Failure& error){std::cerr<<"fixture contour emit face="<<faceNumber<<" line="<<activeLine<<" diagnostic bounds unavailable: "<<error.what()<<std::endl;}
            catch(const std::exception& error){std::cerr<<"fixture contour emit face="<<faceNumber<<" line="<<activeLine<<" diagnostic bounds unavailable: "<<error.what()<<std::endl;}
        }
    };
    for(int i=1;i<=contour.NbLines();++i){activeLine=i;const auto& line=contour.Line(i);
        if(trace){
            const int points=line.TypeContour()==Contap_Walking?line.NbPnts():0;
            std::cerr<<std::setprecision(17)<<"fixture contour line face="<<faceNumber<<" line="<<i<<" type="<<line.TypeContour()<<" vertices="<<line.NbVertex()<<" points="<<points;
            for(int j=1;j<=line.NbVertex();++j){const auto& vertex=line.Vertex(j);double u,v;vertex.Parameters(u,v);const auto p=vertex.Value();std::cerr<<" vertex["<<j<<"]="<<vertex.ParameterOnLine()<<':'<<u<<','<<v<<':'<<p.X()<<','<<p.Y()<<','<<p.Z();}
            if(points){double u0=INFINITY,u1=-INFINITY,v0=INFINITY,v1=-INFINITY;std::array<double,6> box{INFINITY,INFINITY,INFINITY,-INFINITY,-INFINITY,-INFINITY};
                for(int j=1;j<=points;++j){double u,v;line.Point(j).ParametersOnS2(u,v);u0=std::min(u0,u);u1=std::max(u1,u);v0=std::min(v0,v);v1=std::max(v1,v);const auto p=line.Point(j).Value();for(int k=0;k<3;++k){box[k]=std::min(box[k],p.Coord(k+1));box[k+3]=std::max(box[k+3],p.Coord(k+1));}}
                std::cerr<<" walkUV="<<u0<<','<<u1<<','<<v0<<','<<v1<<" walkBounds=";for(double x:box)std::cerr<<x<<',';
                std::cerr<<" closureDistance="<<line.Point(1).Value().Distance(line.Point(points).Value());}
            std::cerr<<std::endl;
        }
        if(line.TypeContour()==Contap_Restriction)continue;
        for(int v=1;v<line.NbVertex();++v){check();const auto a=line.Vertex(v),b=line.Vertex(v+1);const double first=a.ParameterOnLine(),last=b.ParameterOnLine();if(last-first<=Precision::PConfusion())continue;
            if(line.TypeContour()==Contap_Lin||line.TypeContour()==Contap_Circle){occ::handle<Geom_Curve> curve;if(line.TypeContour()==Contap_Lin)curve=new Geom_Line(line.Line());else curve=new Geom_Circle(line.Circle());double tolerance=Tol;const auto uv=GeomProjLib::Curve2d(curve,first,last,BRep_Tool::Surface(face),tolerance);Require(tolerance<=Tol,"PLATE_CONTOUR_PRECISION");append(curve,uv,first,last);continue;}
            Require(line.TypeContour()==Contap_Walking,"PLATE_CONTOUR_UNSUPPORTED");std::vector<gp_Pnt2d> seeds;double u,w;a.Parameters(u,w);seeds.emplace_back(u,w);
            for(int k=static_cast<int>(std::floor(first))+1;k<last;++k){line.Point(k).ParametersOnS2(u,w);seeds.emplace_back(u,w);}b.Parameters(u,w);seeds.emplace_back(u,w);
            // Split only where the implicit branch changes parameter direction.
            // Each resulting branch is evaluated against the surface equation.
            size_t start=0;while(start+1<seeds.size()){
                const auto delta=gp_Vec2d(seeds[start],seeds[start+1]);const bool alongU=std::abs(delta.X())/surface->UResolution(1.)>=std::abs(delta.Y())/surface->VResolution(1.);
                const auto coord=[&](const gp_Pnt2d& p){return alongU?p.X():p.Y();};const double sign=coord(seeds[start+1])-coord(seeds[start]);Require(std::abs(sign)>1e-14,"PLATE_CONTOUR_SINGULAR");size_t end=start+1;
                while(end+1<seeds.size()&&(coord(seeds[end+1])-coord(seeds[end]))*sign>1e-28){const auto step=gp_Vec2d(seeds[end],seeds[end+1]);if(step.X()*delta.X()<-1e-28||step.Y()*delta.Y()<-1e-28)break;++end;}
                const occ::handle<Adaptor2d_Curve2d> equation=new ContourEquation(surface,std::vector<gp_Pnt2d>(seeds.begin()+start,seeds.begin()+end+1),alongU,check);
                Approx_CurveOnSurface spline(equation,surface,equation->FirstParameter(),equation->LastParameter(),Tol*.1);spline.Perform(256,14,GeomAbs_C1);
                Require(spline.IsDone()&&spline.MaxError3d()<=Tol*.1,"PLATE_CONTOUR_PRECISION");append(spline.Curve3d(),spline.Curve2d(),equation->FirstParameter(),equation->LastParameter());start=end;
            }
        }
    }return result;
}
