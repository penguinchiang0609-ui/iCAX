// Batch inspection implementation. Boundary labels come from the independently hashed face inventory.
#define CUT_UNIT_EXAMPLE_LIBRARY
#include "build_cut_unit_examples.cpp"
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRepTopAdaptor_FClass2d.hxx>
#include <Geom2d_Curve.hxx>
#include <Geom2d_Line.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <Geom2dAPI_InterCurveCurve.hxx>
#include <Geom2dAdaptor_Curve.hxx>
#include <BndLib_Add2dCurve.hxx>
#include <Bnd_Box2d.hxx>
#include <Geom_BezierSurface.hxx>
#include <TopoDS_Compound.hxx>
#include <sstream>
#include <set>

struct LogicalGroup {int wire=0,role=0;bool closed=false,straight=false;std::vector<std::pair<int,int>> edges;};
struct FaceControl {bool complete=false;int mask=0;std::vector<LogicalGroup> groups;};
struct Piece {TopoDS_Face face,exchange;int axis=0;double error=0;bool approximate=false,periodic=false;std::string method;std::vector<std::pair<gp_Pnt,gp_Pnt>> lines;};
struct Solve {std::vector<Piece> pieces;std::string reason;double error=-1;};
std::string quoted(const std::string& s){std::ostringstream o;o<<'"';for(unsigned char c:s){if(c=='"'||c=='\\')o<<'\\'<<c;else if(c<32)o<<' ';else o<<c;}o<<'"';return o.str();}
std::string numbered(int n,int width){std::ostringstream o;o<<std::setw(width)<<std::setfill('0')<<n;return o.str();}
std::string faceType(const TopoDS_Face& f){BRepAdaptor_Surface a(f);static const char* names[]={"plane","cylinder","cone","sphere","torus","bezier","bspline","revolution","extrusion","offset","other"};return names[std::min(10,int(a.GetType()))];}

Handle(Geom_BSplineCurve) rail(const LogicalGroup& g,const TopTools_IndexedMapOfShape& edges){std::ostringstream s;s<<g.edges.size()<<' ';for(auto [id,rev]:g.edges)s<<id<<' '<<rev<<' ';std::istringstream in(s.str());return readRail(in,edges);}
double crossValue(const gp_Pnt2d& p,int axis){return axis==0?p.X():p.Y();}
double crossDerivative(const gp_Vec2d& p,int axis){return axis==0?p.X():p.Y();}

// Find boundary extrema in the sweep coordinate, including extrema at joins, after logical grouping.
std::vector<double> events(const TopoDS_Face& f,int axis)
{
    double u,U,v,V;BRepTools::UVBounds(f,u,U,v,V);double lo=axis==0?u:v,hi=axis==0?U:V;
    std::vector<double> values={lo,hi};
    for(TopExp_Explorer wi(f,TopAbs_WIRE);wi.More();wi.Next()){
        struct Ends {double start,end,ds,de;};std::vector<Ends> ends;
        for(BRepTools_WireExplorer e(TopoDS::Wire(wi.Current()),f);e.More();e.Next()){
            auto edge=e.Current();if(BRep_Tool::Degenerated(edge))continue;double a,b;auto c=BRep_Tool::CurveOnSurface(edge,f,a,b);if(c.IsNull())throw std::runtime_error("missing-pcurve");
            if(edge.Orientation()==TopAbs_REVERSED)std::swap(a,b);double sign=b>=a?1:-1;
            auto d=[&](double t){gp_Pnt2d p;gp_Vec2d v;c->D1(t,p,v);return crossDerivative(v,axis)*sign;};
            double ds=d(a),de=d(b);ends.push_back({crossValue(c->Value(a),axis),crossValue(c->Value(b),axis),ds,de});
            if(std::abs(ds)<1e-11&&std::abs(de)<1e-11&&std::abs(crossValue(c->Value((a+b)/2),axis)-ends.back().start)<1e-9){values.push_back(ends.back().start);continue;}
            double prev=a,dp=ds;
            for(int j=1;j<=48;++j){double t=a+(b-a)*j/48.,dt=d(t);
                if(dp*dt<0){double x=prev,y=t,dx=dp;for(int k=0;k<40;++k){double m=(x+y)/2,dm=d(m);if(dx*dm>0){x=m;dx=dm;}else y=m;}values.push_back(crossValue(c->Value((x+y)/2),axis));}
                if(std::abs(dt)<1e-12&&j<48)values.push_back(crossValue(c->Value(t),axis));prev=t;dp=dt;
            }
        }
        for(size_t i=0;i<ends.size();++i){auto& a=ends[i];auto& b=ends[(i+1)%ends.size()];if(a.de*b.ds<=0)values.push_back(a.end);}
    }
    std::sort(values.begin(),values.end());std::vector<double> unique;
    double eps=std::max(1e-9,(hi-lo)*1e-10);for(double x:values)if(x>=lo-eps&&x<=hi+eps&&(unique.empty()||x-unique.back()>eps))unique.push_back(std::clamp(x,lo,hi));
    return unique;
}

// Trim-aware diagnostic. The geometry exported below is the actual trimmed face, not these samples.
bool scan(Piece& piece)
{
    double u,U,v,V;BRepTools::UVBounds(piece.face,u,U,v,V);BRepTopAdaptor_FClass2d classifier(piece.face,1e-8);BRepAdaptor_Surface s(piece.face);
    auto pointAt=[&](double cross,double depth){return piece.axis==0?gp_Pnt2d(cross,depth):gp_Pnt2d(depth,cross);};
    double c0=piece.axis==0?u:v,c1=piece.axis==0?U:V,d0=piece.axis==0?v:u,d1=piece.axis==0?V:U;
    int useful=0;for(int i=0;i<9;++i){double c=c0+(c1-c0)*(i+.5)/9.;
        auto inside=[&](double d){auto p=pointAt(c,d);auto state=classifier.Perform(p,false);return state==TopAbs_IN||state==TopAbs_ON;};
        bool prev=inside(d0);double start=d0;std::vector<std::pair<double,double>> spans;
        for(int j=1;j<=128;++j){double d=d0+(d1-d0)*j/128.;bool now=inside(d);
            if(now!=prev){double a=d0+(d1-d0)*(j-1)/128.,b=d;for(int k=0;k<30;++k){double m=(a+b)/2;if(inside(m)==prev)a=m;else b=m;}double hit=(a+b)/2;if(now)start=hit;else spans.push_back({start,hit});}
            if(j==128&&now)spans.push_back({start,d});prev=now;
        }
        if(spans.size()>1)return false;if(spans.empty())continue;++useful;auto [a,b]=spans[0];auto x=pointAt(c,a),y=pointAt(c,b);
        piece.lines.push_back({s.Value(x.X(),x.Y()),s.Value(y.X(),y.Y())});
    }return useful>=5;
}

std::vector<TopoDS_Face> clip(const TopoDS_Face& f,const Handle(Geom_Surface)& surface,int axis,double a,double b)
{
    double u,U,v,V;BRepTools::UVBounds(f,u,U,v,V);if(axis==0){u=a;U=b;}else{v=a;V=b;}
    auto strip=BRepBuilderAPI_MakeFace(surface,u,U,v,V,1e-7).Face();BRepAlgoAPI_Common common;NCollection_List<TopoDS_Shape> args,tools;args.Append(f);tools.Append(strip);common.SetArguments(args);common.SetTools(tools);common.SetNonDestructive(true);common.SetFuzzyValue(1e-7);common.Build();
    if(!common.IsDone()||common.HasErrors())throw std::runtime_error("sweep-clip-failed");
    std::vector<TopoDS_Face> result;for(TopExp_Explorer e(common.Shape(),TopAbs_FACE);e.More();e.Next())result.push_back(TopoDS::Face(e.Current()));return result;
}

TopoDS_Face mapTrim(const TopoDS_Face& source,const Handle(Geom_Surface)& surface)
{
    BRepBuilderAPI_MakeFace make(surface,1e-7);if(!make.IsDone())throw std::runtime_error("approx-surface-failed");
    // Start with an unbounded support and add the original oriented pcurve loops.
    TopoDS_Face result;BRep_Builder builder;builder.MakeFace(result,surface,1e-7);
    for(TopExp_Explorer w(source,TopAbs_WIRE);w.More();w.Next()){
        BRepBuilderAPI_MakeWire wire;
        for(BRepTools_WireExplorer e(TopoDS::Wire(w.Current()),source);e.More();e.Next()){
            auto edge=e.Current();if(BRep_Tool::Degenerated(edge))throw std::runtime_error("approx-degenerate-boundary");double a,b;auto c=BRep_Tool::CurveOnSurface(edge,source,a,b);if(c.IsNull())throw std::runtime_error("approx-missing-pcurve");
            BRepBuilderAPI_MakeEdge edgeMaker(c,surface,a,b);if(!edgeMaker.IsDone())throw std::runtime_error("approx-edge-build-failed");auto mapped=edgeMaker.Edge();if(edge.Orientation()==TopAbs_REVERSED)mapped.Reverse();wire.Add(mapped);
        }if(!wire.IsDone())throw std::runtime_error("approx-wire-build-failed");builder.Add(result,wire.Wire());
    }result.Orientation(source.Orientation());BRepLib::BuildCurves3d(result);BRepLib::SameParameter(result,1e-7,true);
    if(!BRepCheck_Analyzer(result).IsValid())throw std::runtime_error("approx-trim-invalid");return result;
}

Handle(Geom_Surface) linearSupport(const TopoDS_Face& face,int axis)
{
    auto surface=BRep_Tool::Surface(face);double u,U,v,V;BRepTools::UVBounds(face,u,U,v,V);
    Handle(Geom_Curve) aa=axis==0?surface->VIso(v):surface->UIso(u),bb=axis==0?surface->VIso(V):surface->UIso(U);
    double low=axis==0?u:v,high=axis==0?U:V;
    auto a=GeomConvert::CurveToBSplineCurve(new Geom_TrimmedCurve(aa,low,high)),b=GeomConvert::CurveToBSplineCurve(new Geom_TrimmedCurve(bb,low,high));
    if(a->NbPoles()!=b->NbPoles()||a->Degree()!=b->Degree()||a->NbKnots()!=b->NbKnots())throw std::runtime_error("approx-rail-parameters-differ");
    for(int i=1;i<=a->NbKnots();++i)if(std::abs(a->Knot(i)-b->Knot(i))>1e-9||a->Multiplicity(i)!=b->Multiplicity(i))throw std::runtime_error("approx-rail-knots-differ");
    NCollection_Array2<gp_Pnt> poles(1,axis==0?a->NbPoles():2,1,axis==0?2:a->NbPoles());NCollection_Array2<double> weights(poles.LowerRow(),poles.UpperRow(),poles.LowerCol(),poles.UpperCol());
    for(int i=1;i<=a->NbPoles();++i)for(int j=1;j<=2;++j){auto c=j==1?a:b;poles(axis==0?i:j,axis==0?j:i)=c->Pole(i);weights(axis==0?i:j,axis==0?j:i)=c->Weight(i);}
    NCollection_Array1<double> dk(1,2);dk(1)=axis==0?v:u;dk(2)=axis==0?V:U;NCollection_Array1<int> dm(1,2);dm(1)=dm(2)=2;
    return new Geom_BSplineSurface(poles,weights,axis==0?a->Knots():dk,axis==0?dk:a->Knots(),axis==0?a->Multiplicities():dm,axis==0?dm:a->Multiplicities(),axis==0?a->Degree():1,axis==0?1:a->Degree());
}

double fitError(const TopoDS_Face& f,const Handle(Geom_Surface)& target)
{
    auto source=BRep_Tool::Surface(f);BRepTopAdaptor_FClass2d classifier(f,1e-8);double u,U,v,V;BRepTools::UVBounds(f,u,U,v,V);double worst=0;int samples=0;
    auto check=[&](gp_Pnt2d p){worst=std::max(worst,source->Value(p.X(),p.Y()).Distance(target->Value(p.X(),p.Y())));++samples;};
    for(int i=0;i<=24;++i)for(int j=0;j<=24;++j){gp_Pnt2d p(u+(U-u)*i/24.,v+(V-v)*j/24.);auto state=classifier.Perform(p,false);if(state==TopAbs_IN||state==TopAbs_ON)check(p);}
    for(TopExp_Explorer e(f,TopAbs_EDGE);e.More();e.Next()){double a,b;auto c=BRep_Tool::CurveOnSurface(TopoDS::Edge(e.Current()),f,a,b);if(c.IsNull())continue;for(int j=0;j<=48;++j)check(c->Value(a+(b-a)*j/48.));}
    if(samples<20)throw std::runtime_error("insufficient-fit-samples");return worst;
}

// A planar annulus can sweep around its hole. Cartesian UV extrema are not unit boundaries.
// Keep the source trim exactly; certify only the sampled radial candidate, not production access.
bool radialRing(const TopoDS_Face& face,Piece& piece)
{
    std::vector<TopoDS_Wire> wires;for(TopExp_Explorer w(face,TopAbs_WIRE);w.More();w.Next())wires.push_back(TopoDS::Wire(w.Current()));
    if(wires.size()!=2)return false;auto outer=BRepTools::OuterWire(face);if(wires[0].IsSame(outer))std::swap(wires[0],wires[1]);
    double u,U,v,V;BRepTools::UVBounds(face,wires[0],u,U,v,V);gp_Pnt2d center((u+U)/2,(v+V)/2);
    struct BoundaryCurve{Handle(Geom2d_Curve) curve;double xmin,ymin,xmax,ymax;};
    std::vector<BoundaryCurve> curves[2];std::vector<double> angles;
    for(int i=0;i<128;++i)angles.push_back(2*std::acos(-1.)*(i+.371)/128.);
    for(int wi=0;wi<2;++wi)for(BRepTools_WireExplorer e(wires[wi],face);e.More();e.Next()){
        if(BRep_Tool::Degenerated(e.Current()))return false;double a,b;auto c=BRep_Tool::CurveOnSurface(e.Current(),face,a,b);if(c.IsNull())return false;
        Handle(Geom2d_Curve) trimmed=new Geom2d_TrimmedCurve(c,a,b);BoundaryCurve bounded;bounded.curve=trimmed;
        if(Geom2dAdaptor_Curve(trimmed).GetType()<=GeomAbs_BSplineCurve){Bnd_Box2d box;BndLib_Add2dCurve::Add(trimmed,1e-7,box);box.Get(bounded.xmin,bounded.ymin,bounded.xmax,bounded.ymax);}
        else{bounded.xmin=bounded.ymin=-std::numeric_limits<double>::infinity();bounded.xmax=bounded.ymax=std::numeric_limits<double>::infinity();}
        curves[wi].push_back(bounded);
        for(double t:{a,b}){auto p=c->Value(t);double angle=std::atan2(p.Y()-center.Y(),p.X()-center.X());angles.push_back(angle-1e-5);angles.push_back(angle+1e-5);}
    }
    BRepAdaptor_Surface surface(face);BRepTopAdaptor_FClass2d classifier(face,1e-8);piece.face=piece.exchange=face;piece.periodic=true;piece.method="planar-radial-ring";
    for(size_t i=0;i<angles.size();++i){gp_Dir2d direction(std::cos(angles[i]),std::sin(angles[i]));Handle(Geom2d_Line) line=new Geom2d_Line(center,direction);double hits[2];
        for(int wi=0;wi<2;++wi){std::vector<double> radii;
            for(const auto& c:curves[wi]){
                // Conservative exact-curve bounds only reject impossible ray/edge pairs.
                double low=0,high=std::numeric_limits<double>::infinity();
                auto slab=[&](double origin,double delta,double minimum,double maximum){if(std::abs(delta)<1e-15)return origin>=minimum&&origin<=maximum;double a=(minimum-origin)/delta,b=(maximum-origin)/delta;if(a>b)std::swap(a,b);low=std::max(low,a);high=std::min(high,b);return low<=high;};
                if(!slab(center.X(),direction.X(),c.xmin,c.xmax)||!slab(center.Y(),direction.Y(),c.ymin,c.ymax))continue;
                Geom2dAPI_InterCurveCurve inter(c.curve,line,1e-7);if(!inter.Intersector().IsDone()||inter.NbSegments())return false;
                for(int j=1;j<=inter.NbPoints();++j){gp_Vec2d delta(center,inter.Point(j));double r=delta.Dot(gp_Vec2d(direction));if(r>1e-7)radii.push_back(r);}}
            std::sort(radii.begin(),radii.end());radii.erase(std::unique(radii.begin(),radii.end(),[](double a,double b){return std::abs(a-b)<1e-6;}),radii.end());if(radii.size()!=1)return false;hits[wi]=radii[0];
        }
        if(hits[1]-hits[0]<1e-7)return false;
        for(int j=1;j<8;++j){double r=hits[0]+(hits[1]-hits[0])*j/8.;auto state=classifier.Perform(center.Translated(gp_Vec2d(direction)*r),false);if(state!=TopAbs_IN&&state!=TopAbs_ON)return false;}
        if(i<128&&i%8==0){auto a=center.Translated(gp_Vec2d(direction)*hits[0]),b=center.Translated(gp_Vec2d(direction)*hits[1]);piece.lines.push_back({surface.Value(a.X(),a.Y()),surface.Value(b.X(),b.Y())});}
    }return true;
}

bool logicalSkinLoop(const TopoDS_Face& original,const FaceControl& control,Piece& piece)
{
    // A cut unit may have no side boundary and more than two cut-edge groups.
    // A G1 break between two Skin groups splits the boundary group, not the face region.
    if(!control.complete||control.groups.empty()||BRepAdaptor_Surface(original).GetType()!=GeomAbs_Plane)return false;
    int wireCount=0;for(TopExp_Explorer w(original,TopAbs_WIRE);w.More();w.Next())++wireCount;
    if(wireCount!=1)return false;
    std::set<int> referenced;int physical=0;for(TopExp_Explorer e(original,TopAbs_EDGE);e.More();e.Next())if(!BRep_Tool::Degenerated(TopoDS::Edge(e.Current())))++physical;
    for(const auto& g:control.groups){if(g.role!=1||g.wire!=control.groups.front().wire||g.edges.empty())return false;for(auto [id,reverse]:g.edges)if(id<1||!referenced.insert(id).second)return false;}
    if(int(referenced.size())!=physical)return false;
    piece.face=piece.exchange=original;piece.axis=-1;piece.method="logical-skin-loop-planar";return true;
}

Solve solve(const TopoDS_Face& original,const FaceControl& control,const TopTools_IndexedMapOfShape& edges,double fitTolerance)
{
    Solve best;if(!control.complete){best.reason="boundary-unresolved";return best;}if(area(original)<1e-10){best.reason="zero-area";return best;}
    if(!BRepCheck_Analyzer(original).IsValid()){best.reason="source-invalid-brep";return best;}
    BRepAdaptor_Surface adaptor(original);auto type=adaptor.GetType();
    Piece boundaryUnit;if(logicalSkinLoop(original,control,boundaryUnit)){best.pieces.push_back(boundaryUnit);best.error=0;return best;}
    if(type==GeomAbs_Plane)try{Piece p;if(radialRing(original,p)){best.pieces.push_back(p);best.error=0;return best;}}catch(const Standard_Failure&){}catch(const std::exception&){}
    // A pair of closed rails is a periodic candidate before generic parameter-domain partitioning.
    if(type==GeomAbs_Plane&&control.groups.size()==2&&control.groups[0].closed&&control.groups[1].closed){try{
        auto a=rail(control.groups[0],edges),b=rail(control.groups[1],edges);matchDirection(a,b);auto support=GeomFill::Surface(a,b);auto f=BRepBuilderAPI_MakeFace(support,1e-7).Face();BRepLib::BuildCurves3d(f);
        double tol=std::max(1e-6,area(original)*1e-7);if(BRepCheck_Analyzer(f).IsValid()&&std::abs(area(f)-area(original))<=tol&&differenceArea(original,f)<=tol&&differenceArea(f,original)<=tol){
            Piece p;p.face=f;p.exchange=original;p.periodic=true;p.method="paired-closed-rails";
            double u,U,v,V;support->Bounds(u,U,v,V);for(int i=0;i<9;++i){double x=u+(U-u)*i/9.;p.lines.push_back({support->Value(x,v),support->Value(x,V)});}best.pieces.push_back(p);return best;
        }
    }catch(const Standard_Failure&){}catch(const std::exception&){} }
    for(int axis:{0,1})try{
        bool exact=type==GeomAbs_Plane||(axis==0&&(type==GeomAbs_Cylinder||type==GeomAbs_Cone||type==GeomAbs_SurfaceOfExtrusion));
        if(type==GeomAbs_BSplineSurface)exact=exact||(axis==0?adaptor.VDegree():adaptor.UDegree())==1;
        if(!exact&&type!=GeomAbs_BSplineSurface&&type!=GeomAbs_BezierSurface&&type!=GeomAbs_SurfaceOfRevolution)continue;
        auto support=exact?BRep_Tool::Surface(original):linearSupport(original,axis);double error=exact?0:fitError(original,support);
        if(best.error<0||error<best.error)best.error=error;if(error>fitTolerance){best.reason="approximation-over-tolerance";continue;}
        auto mapped=exact?original:mapTrim(original,support);auto ev=events(original,axis);if(ev.size()>66){best.reason="too-many-sweep-events";continue;}
        std::vector<Piece> candidates;double sum=0;bool good=true;
        for(size_t i=1;i<ev.size();++i){auto parts=ev.size()==2?std::vector<TopoDS_Face>{mapped}:clip(mapped,support,axis,ev[i-1],ev[i]);
            for(auto& f:parts){if(area(f)<1e-12)continue;if(!BRepCheck_Analyzer(f).IsValid()){good=false;break;}
                Piece p;p.face=f;p.exchange=f;p.axis=axis;p.approximate=!exact;p.error=error;p.method=exact?"exact-surface-sweep":"sampled-ruled-approximation";
                if(!scan(p)){good=false;best.reason="sweep-interval-not-single";break;}sum+=area(f);candidates.push_back(p);
            }if(!good)break;
        }
        if(!good||candidates.empty())continue;
        if(std::abs(sum-area(mapped))>std::max(1e-6,area(mapped)*1e-7)){best.reason="partition-area-mismatch";continue;}
        if(best.pieces.empty()||candidates.size()<best.pieces.size()||(best.pieces[0].approximate&&exact)){best.pieces=candidates;best.reason="";}
        if(best.pieces.size()==1&&!best.pieces[0].approximate)break;
    }catch(const Standard_Failure& e){if(best.pieces.empty())best.reason=std::string("kernel: ")+e.GetMessageString();}catch(const std::exception& e){if(best.pieces.empty())best.reason=e.what();}
    if(best.pieces.empty()&&best.reason.empty())best.reason="no-supported-ruling-direction";if(!best.pieces.empty())best.reason="";return best;
}

Quantity_Color unitColor(int face,int unit){double h=std::fmod(face*.61803398875+unit*.217,1.0),s=.58,v=.88;double x=h*6;int i=int(x);double f=x-i,p=v*(1-s),q=v*(1-s*f),t=v*(1-s*(1-f));double r=0,g=0,b=0;switch(i%6){case 0:r=v;g=t;b=p;break;case 1:r=q;g=v;b=p;break;case 2:r=p;g=v;b=t;break;case 3:r=p;g=q;b=v;break;case 4:r=t;g=p;b=v;break;case 5:r=v;g=p;b=q;break;}return Quantity_Color(r,g,b,Quantity_TOC_sRGB);}
void jsonColor(std::ostream& o,const Quantity_Color& c){double r,g,b;c.Values(r,g,b,Quantity_TOC_sRGB);o<<'['<<r<<','<<g<<','<<b<<']';}
void addColored(const Handle(TDocStd_Document)& doc,const TopoDS_Face& face,const std::string& name,const Quantity_Color& color){auto local=face;auto tr=local.Location().Transformation();local.Location(TopLoc_Location());auto shapes=XCAFDoc_DocumentTool::ShapeTool(doc->Main());auto label=shapes->AddShape(BRepBuilderAPI_Transform(local,tr,true).Shape(),false);TDataStd_Name::Set(label,name.c_str());XCAFDoc_DocumentTool::ColorTool(doc->Main())->SetColor(label,color,XCAFDoc_ColorSurf);}

int wmain(int argc,wchar_t** argv)
{
    try{
        if(argc==4&&std::wstring(argv[1])==L"--assemble"){
            std::ifstream list(argv[2]);int count;list>>count;Handle(TDocStd_Document) document=new TDocStd_Document("BinXCAF");
            for(int i=0;i<count;++i){std::string path,name;double r,g,b;list>>std::quoted(path)>>std::quoted(name)>>r>>g>>b;if(!list)throw std::runtime_error("invalid-assembly-list");TopoDS_Shape shape;BRep_Builder builder;
                if(!BRepTools::Read(shape,utf8(fs::u8path(path)).c_str(),builder)||shape.ShapeType()!=TopAbs_FACE)throw std::runtime_error("assembly-face-read-failed");
                addColored(document,TopoDS::Face(shape),name,Quantity_Color(r,g,b,Quantity_TOC_sRGB));}
            STEPCAFControl_Writer writer;writer.SetColorMode(true);writer.SetNameMode(true);if(!writer.Transfer(document,STEPControl_AsIs)||writer.Write(utf8(fs::path(argv[3])).c_str())!=IFSelect_RetDone)throw std::runtime_error("assembly-step-write-failed");return 0;
        }
        if(argc!=5&&argc!=6)throw std::runtime_error("usage: batch_cut_units source.step controls.txt output-directory fit-tolerance-mm [source-face-id]");
        int selectedFace=argc==6?std::stoi(argv[5]):0;
        fs::path input=argv[1],out=argv[3];fs::create_directories(out/"units");fs::create_directories(out/"pending");double tolerance=std::stod(argv[4]);
        std::ifstream ctl(argv[2]);int expected;ctl>>expected;std::vector<FaceControl> controls(expected+1);
        for(int i=1;i<=expected;++i){int id,complete,mask,n;ctl>>id>>complete>>mask>>n;controls[id].complete=complete;controls[id].mask=mask;
            for(int j=0;j<n;++j){LogicalGroup g;int closed,straight,k;ctl>>g.wire>>g.role>>closed>>straight>>k;g.closed=closed;g.straight=straight;for(int q=0;q<k;++q){int e,r;ctl>>e>>r;g.edges.push_back({e,r});}controls[id].groups.push_back(g);}}
        if(!ctl)throw std::runtime_error("bad-controls");STEPControl_Reader reader;if(reader.ReadFile(utf8(input).c_str())!=IFSelect_RetDone)throw std::runtime_error("read-failed");reader.TransferRoots();auto shape=reader.OneShape();TopTools_IndexedMapOfShape faces,edges;TopExp::MapShapes(shape,TopAbs_FACE,faces);TopExp::MapShapes(shape,TopAbs_EDGE,edges);if(faces.Extent()!=expected)throw std::runtime_error("face-count-changed");
        if(selectedFace<0||selectedFace>expected)throw std::runtime_error("selected-face-out-of-range");
        Handle(TDocStd_Document) doc=new TDocStd_Document("BinXCAF");std::ofstream o(out/"native.json");std::ofstream progress(out/"progress.txt");o<<std::setprecision(15)<<"{\"sourceFaceCount\":"<<expected<<",\"faceCount\":"<<(selectedFace?1:expected)<<",\"fitToleranceMm\":"<<tolerance<<",\"faces\":[";int accepted=0,pending=0,unitTotal=0;bool faceComma=false;
        for(int id=1;id<=expected;++id){if(selectedFace&&id!=selectedFace)continue;progress<<id<<std::endl;auto original=TopoDS::Face(faces(id));Solve result;try{result=solve(original,controls[id],edges,tolerance);}catch(const Standard_Failure& e){result.reason=e.GetMessageString();}catch(const std::exception& e){result.reason=e.what();}
            // A bad mesh must leave this source face pending rather than truncate the whole file.
            try{int uid=0;for(auto& p:result.pieces){auto stem="F"+numbered(id,4)+"_U"+numbered(++uid,3);auto target=out/"units"/stem;auto brep=target;brep+=".brep";BRepTools::Write(p.face,utf8(brep).c_str());
                TopoDS_Shape round;BRep_Builder b;if(!BRepTools::Read(round,utf8(brep).c_str(),b)||!BRepCheck_Analyzer(round).IsValid())throw std::runtime_error("unit-brep-readback-failed");auto mesh=target;mesh+=".mesh.json";writeMesh(TopoDS::Face(round),mesh);
            }}catch(const Standard_Failure& e){result.reason=std::string("unit-export: ")+e.GetMessageString();result.pieces.clear();}catch(const std::exception& e){result.reason=std::string("unit-export: ")+e.what();result.pieces.clear();}
            if(faceComma)o<<',';faceComma=true;o<<"{\"faceId\":"<<id<<",\"surface\":"<<quoted(faceType(original))<<",\"sourceAreaMm2\":"<<area(original)<<",\"status\":"<<quoted(result.pieces.empty()?"pending":result.pieces[0].approximate?"approximate":"exact")<<",\"reason\":"<<quoted(result.reason)<<",\"bestSampledErrorMm\":"<<result.error<<",\"units\":[";
            int uid=0;for(auto& p:result.pieces){++uid;auto stem="F"+numbered(id,4)+"_U"+numbered(uid,3);
                auto color=unitColor(id,uid);addColored(doc,p.exchange,stem+(p.approximate?"_APPROX":"_EXACT"),color);
                if(uid>1)o<<',';o<<"{\"id\":"<<uid<<",\"file\":"<<quoted("units/"+stem+".brep")<<",\"mesh\":"<<quoted("units/"+stem+".mesh.json")<<",\"method\":"<<quoted(p.method)<<",\"axis\":"<<p.axis<<",\"periodic\":"<<(p.periodic?"true":"false")<<",\"areaMm2\":"<<area(p.face)<<",\"sampledErrorMm\":"<<p.error<<",\"color\":";jsonColor(o,color);o<<",\"rulings\":[";bool comma=false;for(auto [a,b]:p.lines){if(comma)o<<',';comma=true;o<<'[';point(o,a);o<<',';point(o,b);o<<']';}o<<"]}";
            }o<<']';
            if(result.pieces.empty()){++pending;auto stem="F"+numbered(id,4);auto path=out/"pending"/stem;auto brep=path;brep+=".brep";BRepTools::Write(original,utf8(brep).c_str());auto mesh=path;mesh+=".mesh.json";bool meshOk=true;
                try{writeMesh(original,mesh);}catch(...){meshOk=false;}
                o<<",\"pendingBrep\":"<<quoted("pending/"+stem+".brep")<<",\"pendingMesh\":"<<(meshOk?quoted("pending/"+stem+".mesh.json"):"null");
                addColored(doc,original,stem+"_PENDING",Quantity_Color(.62,.65,.69,Quantity_TOC_sRGB));
            }else{++accepted;unitTotal+=uid;}
            o<<'}';o.flush();
        }
        o<<"],\"acceptedFaces\":"<<accepted<<",\"pendingFaces\":"<<pending<<",\"unitCount\":"<<unitTotal<<'}';o.close();
        STEPCAFControl_Writer writer;writer.SetColorMode(true);writer.SetNameMode(true);if(!writer.Transfer(doc,STEPControl_AsIs)||writer.Write(utf8(out/"colored.step").c_str())!=IFSelect_RetDone)throw std::runtime_error("colored-step-export-failed");
        std::cout<<"faces="<<expected<<" accepted="<<accepted<<" pending="<<pending<<" units="<<unitTotal<<'\n';return 0;
    }catch(const Standard_Failure& e){std::cerr<<e.GetMessageString()<<'\n';return 1;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
