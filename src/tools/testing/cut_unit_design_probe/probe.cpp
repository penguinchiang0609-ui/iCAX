// Read-only design probe: finite sampling supplies evidence, not machining certification.
#include <STEPControl_Reader.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepTopAdaptor_FClass2d.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRepGProp.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRep_Tool.hxx>
#include <BRep_Builder.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <GeomLProp_SLProps.hxx>
#include <BRepLProp_SLProps.hxx>
#include <Geom2d_Curve.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Pnt2d.hxx>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs=std::filesystem;
constexpr double tol=0.001;
std::string utf8(const fs::path& p){auto a=p.u8string();return {a.begin(),a.end()};}
std::string quote(const std::string& s){std::ostringstream o;o<<'"';for(unsigned char c:s){if(c=='"'||c=='\\')o<<'\\'<<c;else if(c<32)o<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<int(c)<<std::dec;else o<<c;}o<<'"';return o.str();}
void point(std::ostream& o,const gp_Pnt& p){o<<'['<<p.X()<<','<<p.Y()<<','<<p.Z()<<']';}
double area(const TopoDS_Shape& s){GProp_GProps g;BRepGProp::SurfaceProperties(s,g);return g.Mass();}
std::string typeName(GeomAbs_SurfaceType t){static const char* n[]={"plane","cylinder","cone","sphere","torus","bezier","bspline","revolution","extrusion","offset","other"};return n[std::min(10,int(t))];}
bool inside(const TopoDS_Face& f,double u,double v){static TopoDS_Face cached;static std::unique_ptr<BRepTopAdaptor_FClass2d> classifier;if(cached.IsNull()||!cached.IsSame(f)){cached=f;classifier=std::make_unique<BRepTopAdaptor_FClass2d>(f,1e-7);}auto s=classifier->Perform(gp_Pnt2d(u,v));return s==TopAbs_IN||s==TopAbs_ON;}
double segmentDistance(const gp_Pnt& p,const gp_Pnt& a,const gp_Pnt& b){gp_Vec ab(a,b),ap(a,p);if(ab.SquareMagnitude()<1e-24)return p.Distance(a);return p.Distance(a.Translated(ab*std::clamp(ap.Dot(ab)/ab.SquareMagnitude(),0.0,1.0)));}
struct ChordResult{double max=0;int lines=0,points=0,maxIntervals=0;};
ChordResult chord(const TopoDS_Face& f,const BRepAdaptor_Surface& s,double u,double U,double v,double V,bool alongU,int subdivisions){
    ChordResult r;static TopoDS_Face cached;static bool direction=false;static std::vector<std::array<double,4>> intervals;static int maxIntervals;
    if(cached.IsNull()||!cached.IsSame(f)||direction!=alongU){cached=f;direction=alongU;intervals.clear();maxIntervals=0;
        for(int i=0;i<=12;i++){
            double cross=(i+0.25)/12.5;auto uv=[&](double t){return gp_Pnt2d(alongU?u+(U-u)*t:u+(U-u)*cross,alongU?v+(V-v)*cross:v+(V-v)*t);};
            auto test=[&](double t){auto p=uv(t);return inside(f,p.X(),p.Y());};
            bool previous=test(0);double start=0;int count=0;
            for(int j=1;j<=128;j++){double t=j/128.;bool current=test(t);if(current!=previous){double a=(j-1)/128.,b=t;for(int k=0;k<24;k++){double m=(a+b)/2;if(test(m)==previous)a=m;else b=m;}double transition=(a+b)/2;if(current)start=transition;else {auto p=uv(start),q=uv(transition);intervals.push_back({p.X(),p.Y(),q.X(),q.Y()});count++;}}
                if(j==128&&current){auto p=uv(start),q=uv(1);intervals.push_back({p.X(),p.Y(),q.X(),q.Y()});count++;}previous=current;
            }maxIntervals=std::max(maxIntervals,count);
        }
    }
    r.maxIntervals=maxIntervals;
    for(const auto& interval:intervals)for(int k=0;k<subdivisions;k++){
        double a=double(k)/subdivisions,b=double(k+1)/subdivisions;
        double u0=interval[0]+(interval[2]-interval[0])*a,v0=interval[1]+(interval[3]-interval[1])*a;
        double u1=interval[0]+(interval[2]-interval[0])*b,v1=interval[1]+(interval[3]-interval[1])*b;
        std::vector<gp_Pnt> ps;for(int j=0;j<=24;j++){double t=j/24.;ps.push_back(s.Value(u0+(u1-u0)*t,v0+(v1-v0)*t));}
        r.lines++;for(const auto& p:ps){r.max=std::max(r.max,segmentDistance(p,ps.front(),ps.back()));r.points++;}
    }return r;
}
struct Profile{std::vector<TopoDS_Edge> edges;std::vector<Bnd_Box> bounds;double x=0;void prepare(){for(auto& e:edges){Bnd_Box b;BRepBndLib::AddOptimal(e,b,false,false);bounds.push_back(b);}}
double distance(gp_Pnt p) const{p.SetX(x);double best=1e100;for(size_t i=0;i<edges.size();i++){Bnd_Box query;query.Add(p);if(bounds[i].Distance(query)>best)continue;double a,b;auto c=BRep_Tool::Curve(edges[i],a,b);if(c.IsNull())continue;best=std::min({best,p.Distance(c->Value(a)),p.Distance(c->Value(b))});GeomAPI_ProjectPointOnCurve q(p,c,a,b);if(q.NbPoints())best=std::min(best,q.LowerDistance());}return best;}};
struct EdgeInfo{std::vector<int> faces;std::vector<TopoDS_Edge> uses;};
std::vector<EdgeInfo> indexEdges(const TopTools_IndexedMapOfShape& faces,TopTools_IndexedMapOfShape& edges){std::vector<EdgeInfo> ret(1);for(int i=1;i<=faces.Extent();i++){auto f=TopoDS::Face(faces(i));for(TopExp_Explorer w(f,TopAbs_WIRE);w.More();w.Next())for(BRepTools_WireExplorer e(TopoDS::Wire(w.Current()),f);e.More();e.Next()){int n=edges.Add(e.Current());if(int(ret.size())<=n)ret.resize(n+1);ret[n].faces.push_back(i);ret[n].uses.push_back(e.Current());}}return ret;}
gp_Vec normalAt(const TopoDS_Face& f,const TopoDS_Edge& e,double t){double a,b;auto c=BRep_Tool::CurveOnSurface(e,f,a,b);if(c.IsNull())throw std::runtime_error("missing-pcurve");auto uv=c->Value(t);BRepAdaptor_Surface s(f);gp_Pnt p;gp_Vec du,dv;s.D1(uv.X(),uv.Y(),p,du,dv);auto n=du.Crossed(dv);if(n.Magnitude()<1e-12)throw std::runtime_error("singular-normal");n.Normalize();if(f.Orientation()==TopAbs_REVERSED)n.Reverse();return n;}
void edgeJson(std::ostream& o,int id,const TopoDS_Edge& edge,const EdgeInfo& info,const TopTools_IndexedMapOfShape& faces,const Profile& profile){
    double ca,cb;auto sourceCurve=BRep_Tool::Curve(edge,ca,cb);
    if(BRep_Tool::Degenerated(edge)||sourceCurve.IsNull()){
        auto vertex=TopExp::FirstVertex(edge);gp_Pnt p;if(!vertex.IsNull())p=BRep_Tool::Pnt(vertex);double d=profile.distance(p);
        o<<"{\"id\":"<<id<<",\"degenerate\":true,\"faces\":[";for(size_t j=0;j<info.faces.size();j++){if(j)o<<',';o<<info.faces[j];}o<<"],\"profileDistanceMin\":"<<d<<",\"profileDistanceMax\":"<<d<<",\"profileSamplesOn\":"<<(d<=tol?1:0)<<",\"sampleCount\":1,\"length\":0,\"relations\":[],\"samples\":[";point(o,p);o<<"]}";return;
    }
    BRepAdaptor_Curve c(edge);double a=c.FirstParameter(),b=c.LastParameter(),lo=1e100,hi=0;int on=0;
    std::vector<gp_Pnt> ps;for(int i=0;i<=16;i++){auto p=c.Value(a+(b-a)*i/16.);ps.push_back(p);double d=profile.distance(p);lo=std::min(lo,d);hi=std::max(hi,d);if(d<=tol)on++;}
    o<<"{\"id\":"<<id<<",\"faces\":[";for(size_t j=0;j<info.faces.size();j++){if(j)o<<',';o<<info.faces[j];}o<<"],\"profileDistanceMin\":"<<lo<<",\"profileDistanceMax\":"<<hi<<",\"profileSamplesOn\":"<<on<<",\"sampleCount\":17,\"length\":";GProp_GProps g;BRepGProp::LinearProperties(edge,g);o<<g.Mass()<<",\"relations\":[";
    bool comma=false;for(size_t i=0;i<info.faces.size();i++)for(size_t j=i+1;j<info.faces.size();j++){
        if(comma)o<<',';comma=true;o<<"{\"a\":"<<info.faces[i]<<",\"b\":"<<info.faces[j]<<",\"angles\":[";bool ac=false;std::string failure;
        for(double t:{0.1,0.3,0.5,0.7,0.9})try{gp_Pnt p;gp_Vec tangent;double par=a+(b-a)*t;c.D1(par,p,tangent);tangent.Normalize();if(info.uses[i].Orientation()==TopAbs_REVERSED)tangent.Reverse();auto n=normalAt(TopoDS::Face(faces(info.faces[i])),info.uses[i],par);auto m=normalAt(TopoDS::Face(faces(info.faces[j])),info.uses[j],par);double angle=std::atan2(tangent.Dot(n.Crossed(m)),n.Dot(m));if(ac)o<<',';ac=true;o<<angle;}catch(const Standard_Failure& e){failure=e.GetMessageString();}catch(const std::exception& e){failure=e.what();}
        o<<"],\"error\":"<<quote(failure)<<'}';
    }o<<"],\"samples\":[";for(size_t j=0;j<ps.size();j++){if(j)o<<',';point(o,ps[j]);}o<<"]}";
}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=3)throw std::runtime_error("usage: probe input.step output.json");
    fs::path input=argv[1],output=argv[2];STEPControl_Reader reader;auto file=utf8(input);if(reader.ReadFile(file.c_str())!=IFSelect_RetDone)throw std::runtime_error("read-failed");reader.TransferRoots();auto shape=reader.OneShape();
    TopTools_IndexedMapOfShape original,allEdges,faceEdges;TopExp::MapShapes(shape,TopAbs_FACE,original);TopExp::MapShapes(shape,TopAbs_EDGE,allEdges);
    auto raw=indexEdges(original,faceEdges);Profile profile;for(int i=1;i<=allEdges.Extent();i++)if(!faceEdges.Contains(allEdges(i)))profile.edges.push_back(TopoDS::Edge(allEdges(i)));
    if(profile.edges.empty())throw std::runtime_error("no-loose-profile-edges");BRepAdaptor_Curve first(profile.edges.front());profile.x=first.Value(first.FirstParameter()).X();profile.prepare();
    BRepBuilderAPI_Sewing sewing(tol,true,true,true,false);TopoDS_Compound body;BRep_Builder builder;builder.MakeCompound(body);for(int i=1;i<=original.Extent();i++){builder.Add(body,original(i));sewing.Add(original(i));}sewing.Perform();auto sewn=sewing.SewedShape();TopTools_IndexedMapOfShape faces,edges;TopExp::MapShapes(sewn,TopAbs_FACE,faces);auto adjacency=indexEdges(faces,edges);
    fs::create_directories(output.parent_path());std::ofstream trace(output.wstring()+L".trace");std::ofstream o(output);o<<std::setprecision(15)<<"{\"probeVersion\":2,\"file\":"<<quote(utf8(input.filename()))<<",\"originalFaceCount\":"<<original.Extent()<<",\"sewnFaceCount\":"<<faces.Extent()<<",\"originalArea\":"<<area(body)<<",\"sewnArea\":"<<area(sewn)<<",\"rawSharedEdgeCount\":";int shared=0;for(const auto& e:raw)if(std::set<int>(e.faces.begin(),e.faces.end()).size()>1)shared++;o<<shared<<",\"profileEdgeCount\":"<<profile.edges.size()<<",\"profileX\":"<<profile.x<<",\"faces\":[";
    for(int i=1;i<=faces.Extent();i++){
        trace<<"face "<<i<<std::endl;if(i>1)o<<',';auto f=TopoDS::Face(faces(i));BRepAdaptor_Surface s(f);double u,U,v,V;BRepTools::UVBounds(f,u,U,v,V);std::vector<int> source;
        for(int k=1;k<=original.Extent();k++)if(original(k).IsSame(f)||(sewing.IsModified(original(k))&&sewing.Modified(original(k)).IsSame(f)))source.push_back(k);
        int wires=0,edgeN=0;for(TopExp_Explorer w(f,TopAbs_WIRE);w.More();w.Next())wires++;for(TopExp_Explorer e(f,TopAbs_EDGE);e.More();e.Next())edgeN++;
        o<<"{\"id\":"<<i<<",\"sourceFaceIndices\":[";for(size_t j=0;j<source.size();j++){if(j)o<<',';o<<source[j];}o<<"],\"type\":"<<quote(typeName(s.GetType()))<<",\"area\":"<<area(f)<<",\"wires\":"<<wires<<",\"edgeUses\":"<<edgeN<<",\"orientation\":"<<int(f.Orientation())<<",\"uv\":["<<u<<','<<U<<','<<v<<','<<V<<']';
        if(s.GetType()==GeomAbs_BSplineSurface)o<<",\"uDegree\":"<<s.UDegree()<<",\"vDegree\":"<<s.VDegree();
        double kmin=1e100,kmax=-1e100;int curvatureN=0;std::vector<gp_Pnt> samplePts;
        for(int a=0;a<7;a++)for(int b=0;b<7;b++)try{double x=u+(U-u)*(a+.5)/7,y=v+(V-v)*(b+.5)/7;if(!inside(f,x,y))continue;samplePts.push_back(s.Value(x,y));BRepLProp_SLProps props(s,x,y,2,1e-9);if(props.IsCurvatureDefined()){double k=props.GaussianCurvature();if(std::isfinite(k)){kmin=std::min(kmin,k);kmax=std::max(kmax,k);curvatureN++;}}}catch(const Standard_Failure&){}
        o<<",\"gaussianSamples\":"<<curvatureN<<",\"gaussianMin\":"<<(curvatureN?kmin:0)<<",\"gaussianMax\":"<<(curvatureN?kmax:0)<<",\"chordTrials\":[";
        bool cc=false;bool needsFit=s.GetType()!=GeomAbs_Plane&&s.GetType()!=GeomAbs_Cylinder&&s.GetType()!=GeomAbs_Cone&&s.GetType()!=GeomAbs_SurfaceOfExtrusion;
        if(needsFit)for(bool alongU:{true,false})for(int n:{1,2,4,8}){if(cc)o<<',';cc=true;auto r=chord(f,s,u,U,v,V,alongU,n);o<<"{\"direction\":"<<quote(alongU?"u":"v")<<",\"depthSubdivisions\":"<<n<<",\"maxSampledDeviation\":"<<r.max<<",\"validLines\":"<<r.lines<<",\"maxIntervals\":"<<r.maxIntervals<<",\"samples\":"<<r.points<<'}';}
        o<<"],\"surfaceSamples\":[";for(size_t j=0;j<samplePts.size();j++){if(j)o<<',';point(o,samplePts[j]);}o<<"]}";
    }
    o<<"],\"edges\":[";for(int i=1;i<=edges.Extent();i++){trace<<"edge "<<i<<std::endl;if(i>1)o<<',';edgeJson(o,i,TopoDS::Edge(edges(i)),adjacency[i],faces,profile);}o<<"],\"profile\":[";
    for(size_t k=0;k<profile.edges.size();k++){if(k)o<<',';BRepAdaptor_Curve c(profile.edges[k]);o<<'[';for(int j=0;j<=32;j++){if(j)o<<',';point(o,c.Value(c.FirstParameter()+(c.LastParameter()-c.FirstParameter())*j/32.));}o<<']';}o<<"]}";return 0;
}catch(const Standard_Failure& e){std::cerr<<e.GetMessageString()<<'\n';return 1;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
