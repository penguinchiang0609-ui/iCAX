// Face-first diagnostic: group by geometry first, classify complete groups second.
// Whole-skin checks are sampled evidence, not continuous certificates.
#include <STEPControl_Reader.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepTopAdaptor_FClass2d.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <Poly_Triangulation.hxx>
#include <GProp_GProps.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <BRep_Builder.hxx>
#include <STEPControl_Writer.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BezierCurve.hxx>
#include <Bnd_Box.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <memory>
#include <map>
#include <string>
#include <vector>

namespace fs=std::filesystem;
constexpr double posTol=0.001, angleTol=0.001, lineTol=0.001, skinTol=0.001, materialTol=1e-7;
constexpr int sampleIntervals=48;
std::string utf8(const fs::path& p){auto u=p.u8string();return {u.begin(),u.end()};}
std::string quote(const std::string& s){std::string q="\"";for(auto c:s){if(c=='\\'||c=='\"')q+='\\';q+=c;}return q+'\"';}
void point(std::ostream& o,const gp_Pnt& p){o<<'['<<p.X()<<','<<p.Y()<<','<<p.Z()<<']';}
void vector3(std::ostream& o,const gp_Vec& p){o<<'['<<p.X()<<','<<p.Y()<<','<<p.Z()<<']';}

struct Profile
{
    std::vector<TopoDS_Edge> edges;
    std::vector<Bnd_Box> boxes;
    std::vector<TopoDS_Wire> wires;
    std::vector<std::unique_ptr<BRepTopAdaptor_FClass2d>> classifiers;
    std::vector<std::unique_ptr<GeomAPI_ProjectPointOnCurve>> projectors;
    std::vector<std::pair<gp_Pnt,gp_Pnt>> curveEnds;
    mutable std::map<std::pair<double,double>, double> distances;
    mutable std::map<std::pair<double,double>, TopAbs_State> states;
    double x=0;
    void prepare(){
        BRepAdaptor_Curve c(edges.front());x=c.Value(c.FirstParameter()).X();
        Handle(TopTools_HSequenceOfShape) list=new TopTools_HSequenceOfShape;
        for(auto& e:edges){Bnd_Box b;BRepBndLib::AddOptimal(e,b,false,false);boxes.push_back(b);list->Append(e);double a,z;auto curve=BRep_Tool::Curve(e,a,z);if(curve.IsNull())throw std::runtime_error("profile-missing-curve");auto projection=std::make_unique<GeomAPI_ProjectPointOnCurve>();projection->Init(curve,a,z);projectors.push_back(std::move(projection));curveEnds.push_back({curve->Value(a),curve->Value(z)});}
        auto connected=ShapeAnalysis_FreeBounds::ConnectEdgesToWires(list,posTol,false);
        for(int i=1;i<=connected->Length();++i){
            auto w=TopoDS::Wire(connected->Value(i));TopoDS_Vertex a,b;TopExp::Vertices(w,a,b);
            if(a.IsNull()||b.IsNull()||BRep_Tool::Pnt(a).Distance(BRep_Tool::Pnt(b))>posTol)throw std::runtime_error("open-profile-wire");
            gp_Pln plane(gp_Ax3(gp_Pnt(x,0,0),gp_Dir(1,0,0),gp_Dir(0,1,0)));
            BRepBuilderAPI_MakeFace make(plane,w,true);if(!make.IsDone())throw std::runtime_error("profile-face-failed");
            wires.push_back(w);classifiers.push_back(std::make_unique<BRepTopAdaptor_FClass2d>(make.Face(),materialTol));
        }
    }
    TopAbs_State state(const gp_Pnt& p) const{
        auto key=std::make_pair(p.Y(),p.Z());if(auto found=states.find(key);found!=states.end())return found->second;
        int inside=0;for(auto& c:classifiers){auto s=c->Perform(gp_Pnt2d(p.Y(),p.Z()),false);if(s==TopAbs_ON)return states[key]=TopAbs_ON;if(s==TopAbs_IN)++inside;if(s==TopAbs_UNKNOWN)return states[key]=s;}
        return states[key]=inside%2?TopAbs_IN:TopAbs_OUT;
    }
    double distance(gp_Pnt p) const
    {
        auto key=std::make_pair(p.Y(),p.Z());if(auto found=distances.find(key);found!=distances.end())return found->second;
        p.SetX(x);double best=1e100;Bnd_Box query;query.Add(p);
        for(size_t i=0;i<edges.size();++i){if(boxes[i].Distance(query)>best)continue;best=std::min({best,p.Distance(curveEnds[i].first),p.Distance(curveEnds[i].second)});auto& projection=*projectors[i];projection.Perform(p);if(projection.NbPoints())best=std::min(best,projection.LowerDistance());}
        return distances[key]=best;
    }
};

struct EdgeUse
{
    int id=0;
    bool reverse=false,valid=false,degenerate=false,seam=false;
    gp_Pnt start,end;
    gp_Vec firstTangent,lastTangent;
    bool tangentValid=false,lineCertificate=false;
    int curveType=-1;
    bool lineLike=false;
    gp_Pnt circleCenter;double circleRadius=0;gp_Dir circleNormal;
    std::vector<gp_Pnt> samples,controls;
    std::string issue;
};

EdgeUse readUse(const TopoDS_Edge& edge,int id)
{
    EdgeUse e;e.id=id;e.reverse=edge.Orientation()==TopAbs_REVERSED;e.degenerate=BRep_Tool::Degenerated(edge);
    auto v=TopExp::FirstVertex(edge,true);if(!v.IsNull())e.start=e.end=BRep_Tool::Pnt(v);
    if(e.degenerate){e.issue="degenerate-vertex-event";return e;}
    double a,b;auto curve=BRep_Tool::Curve(edge,a,b);if(curve.IsNull()){e.issue="missing-3d-curve";return e;}
    BRepAdaptor_Curve c(edge);e.curveType=int(c.GetType());a=c.FirstParameter();b=c.LastParameter();if(e.reverse)std::swap(a,b);
    e.start=c.Value(a);e.end=c.Value(b);
    try{gp_Pnt p;c.D1(a,p,e.firstTangent);c.D1(b,p,e.lastTangent);if(e.reverse){e.firstTangent.Reverse();e.lastTangent.Reverse();}if(e.firstTangent.Magnitude()>1e-12&&e.lastTangent.Magnitude()>1e-12){e.firstTangent.Normalize();e.lastTangent.Normalize();e.tangentValid=true;}}catch(const Standard_Failure&){}
    for(int i=0;i<=sampleIntervals;++i)e.samples.push_back(c.Value(a+(b-a)*i/sampleIntervals));
    // A positive-weight Bezier/B-spline is enclosed by its control polygon.
    if(c.GetType()==GeomAbs_Line){e.controls={e.start,e.end};e.lineCertificate=true;}
    else if(c.GetType()==GeomAbs_BSplineCurve){auto s=c.BSpline();bool positive=true;for(int i=1;i<=s->NbPoles();++i){e.controls.push_back(s->Pole(i));positive=positive&&s->Weight(i)>0;}e.lineCertificate=positive;}
    else if(c.GetType()==GeomAbs_BezierCurve){auto s=c.Bezier();bool positive=true;for(int i=1;i<=s->NbPoles();++i){e.controls.push_back(s->Pole(i));positive=positive&&s->Weight(i)>0;}e.lineCertificate=positive;}
    e.lineLike=c.GetType()==GeomAbs_Line;
    if(c.GetType()==GeomAbs_Circle){auto circle=c.Circle();e.circleCenter=circle.Location();e.circleRadius=circle.Radius();e.circleNormal=circle.Axis().Direction();}
    if(!e.lineLike&&e.lineCertificate&&e.start.Distance(e.end)>1e-9){gp_Vec direction(e.start,e.end);direction.Normalize();e.lineLike=std::all_of(e.controls.begin(),e.controls.end(),[&](const gp_Pnt& p){return gp_Vec(e.start,p).Crossed(direction).Magnitude()<=1e-7;});}
    e.valid=true;return e;
}

bool continuous(const EdgeUse& a,const EdgeUse& b)
{
    if(!a.valid||!b.valid||!a.tangentValid||!b.tangentValid||a.end.Distance(b.start)>posTol||a.lastTangent.Dot(b.firstTangent)<std::cos(angleTol))return false;
    // Tangency does not merge distinct geometric primitives: a line and its tangent arc stay separate.
    if(a.lineLike!=b.lineLike)return false;
    if(a.curveType==GeomAbs_Circle&&b.curveType==GeomAbs_Circle)
        return a.circleCenter.Distance(b.circleCenter)<=posTol&&std::abs(a.circleRadius-b.circleRadius)<=posTol&&std::abs(a.circleNormal.Dot(b.circleNormal))>=std::cos(angleTol);
    return true;
}

struct Group
{
    std::vector<int> uses;
    // A geometric G1 group may cross a boundary role without changing its
    // primitive. Children partition complete source uses, never sampled trims.
    std::vector<Group> roleSegments;
    bool closed=false,straight=false,allOnSkin=false,firstOnSkin=false,lastOnSkin=false;
    double maxLineDistance=0,minSkinDistance=1e100,maxSkinDistance=0;
    bool chordDefined=false,chordBoundKnown=false;
    double chordLength=0,chordSampledError=0,chordUpperBound=0,sampledBacktrack=0;
    bool seam=false,event=false;
    int bodyInside=0,bodyOn=0,bodyOutside=0,bodyUnknown=0;
    std::string reason;
    int onSkinSamples=0,samples=0;
    std::string kind="Unresolved";
};

std::vector<Group> groupByContinuity(const std::vector<EdgeUse>& uses)
{
    const int n=int(uses.size());std::vector<Group> result;if(!n)return result;
    std::vector<bool> joins(n);int firstBreak=-1;
    for(int i=0;i<n;++i){joins[i]=continuous(uses[i],uses[(i+1)%n]);if(!joins[i]&&firstBreak<0)firstBreak=i;}
    if(firstBreak<0){Group g;g.closed=true;g.uses.resize(n);std::iota(g.uses.begin(),g.uses.end(),0);result.push_back(g);return result;}
    Group current;for(int k=0;k<n;++k){int i=(firstBreak+1+k)%n;current.uses.push_back(i);if(!joins[i]){result.push_back(current);current=Group{};}}
    return result;
}

// Roles remain separate from geometric primitive/G0/G1 grouping; Side need not be straight.
void classifyRole(Group& g,const std::vector<EdgeUse>& uses,const Profile& profile)
{
    auto& first=uses[g.uses.front()];auto& last=uses[g.uses.back()];
    g.event=std::all_of(g.uses.begin(),g.uses.end(),[&](int i){return uses[i].degenerate;});
    g.seam=std::all_of(g.uses.begin(),g.uses.end(),[&](int i){return uses[i].seam;});
    if(g.event){g.reason="degenerate-event";return;}
    if(std::any_of(g.uses.begin(),g.uses.end(),[&](int i){return !uses[i].valid;})){g.reason="invalid-source-curve";return;}
    if(!g.seam&&std::any_of(g.uses.begin(),g.uses.end(),[&](int i){return uses[i].seam;})){g.reason="mixed-seam-group";return;}
    g.firstOnSkin=profile.distance(first.start)<=skinTol;g.lastOnSkin=profile.distance(last.end)<=skinTol;
    gp_Vec line(first.start,last.end);double length=line.Magnitude();g.straight=!g.closed&&length>posTol;if(g.straight)line.Normalize();
    g.chordLength=length;g.chordDefined=!g.closed&&length>posTol;g.chordBoundKnown=g.chordDefined;
    double maximumProjection=0;
    auto chordDistance=[&](const gp_Pnt& p){double q=gp_Vec(first.start,p).Dot(line);return p.Distance(first.start.Translated(line*std::clamp(q,0.0,length)));};
    for(int i:g.uses){auto& e=uses[i];if(!e.lineCertificate)g.straight=false;
        if(!e.lineCertificate)g.chordBoundKnown=false;
        if(length>posTol)for(auto& p:e.controls)g.maxLineDistance=std::max(g.maxLineDistance,gp_Vec(first.start,p).Crossed(line).Magnitude());
        if(g.chordDefined){
            for(auto& p:e.controls)g.chordUpperBound=std::max(g.chordUpperBound,chordDistance(p));
            for(auto& p:e.samples){g.chordSampledError=std::max(g.chordSampledError,chordDistance(p));double q=gp_Vec(first.start,p).Dot(line);g.sampledBacktrack=std::max(g.sampledBacktrack,maximumProjection-q);maximumProjection=std::max(maximumProjection,q);}
        }
        for(size_t j=0;j<e.samples.size();++j){auto& p=e.samples[j];double d=profile.distance(p);g.minSkinDistance=std::min(g.minSkinDistance,d);g.maxSkinDistance=std::max(g.maxSkinDistance,d);++g.samples;if(d<=skinTol)++g.onSkinSamples;}
    }
    g.straight=g.straight&&g.maxLineDistance<=lineTol;
    g.allOnSkin=g.samples&&g.onSkinSamples==g.samples;
    if(g.seam){g.reason="periodic-parameter-seam";return;}
    // Whole-skin has priority. Material containment cannot change that role and
    // need not repeatedly classify coincident curved points with the face kernel.
    if(g.allOnSkin){g.kind="Skin";return;}
    for(int i:g.uses){auto& e=uses[i];for(size_t j=0;j<e.samples.size();++j){auto& p=e.samples[j];
        bool endpoint=!g.closed&&((i==g.uses.front()&&j==0)||(i==g.uses.back()&&j+1==e.samples.size()));
        if(!endpoint){auto s=profile.state(p);if(s==TopAbs_IN)++g.bodyInside;else if(s==TopAbs_ON)++g.bodyOn;else if(s==TopAbs_OUT){if(profile.distance(p)>skinTol)++g.bodyOutside;else ++g.bodyOn;}else ++g.bodyUnknown;}
    }}
    if(g.bodyOutside||g.bodyUnknown){g.reason=g.bodyOutside?"outside-section-material":"unknown-section-state";}
    else if(g.bodyOn){g.reason="mixed-skin-contact";}
    else if(!g.closed&&(g.firstOnSkin||g.lastOnSkin)&&g.bodyInside)g.kind="Side";
    else if(g.bodyInside&&profile.state(first.start)==TopAbs_IN&&profile.state(last.end)==TopAbs_IN)g.kind="Internal";
    else g.reason="unresolved-endpoint-state";
}

void classify(Group& g,const std::vector<EdgeUse>& uses,const Profile& profile)
{
    classifyRole(g,uses,profile);
    g.roleSegments.clear();
    if(g.kind!="Unresolved"||g.reason!="mixed-skin-contact"||g.uses.size()<2||g.seam||g.event)return;
    const auto known=[](const Group& part){return !part.seam&&!part.event
        &&(part.kind=="Skin"||part.kind=="Side"||part.kind=="Internal");};
    std::vector<Group> segments;std::vector<std::string> roles;
    for(const int ui:g.uses){
        Group part;part.uses={ui};classifyRole(part,uses,profile);
        // A role change inside this single topological edge is unresolved.
        // Sampling does not justify inventing a new parameter split point.
        if(!known(part))return;
        if(std::find(roles.begin(),roles.end(),part.kind)==roles.end())roles.push_back(part.kind);
        if(!segments.empty()&&segments.back().kind==part.kind){
            Group joined;joined.uses=segments.back().uses;joined.uses.push_back(ui);
            classifyRole(joined,uses,profile);
            if(known(joined)&&joined.kind==part.kind){segments.back()=std::move(joined);continue;}
        }
        segments.push_back(std::move(part));
    }
    if(roles.size()<2)return;
    std::vector<int> coverage;for(const auto& part:segments)
        coverage.insert(coverage.end(),part.uses.begin(),part.uses.end());
    if(coverage!=g.uses)throw std::runtime_error("role-segment-source-use-partition-failed");
    g.roleSegments=std::move(segments);
}

void writeGroup(std::ostream& o,const Group& g,const std::vector<EdgeUse>& uses,int groupId,int segmentId=0)
{
    const auto& first=uses[g.uses.front()];const auto& last=uses[g.uses.back()];
    o<<"{\"groupId\":"<<groupId;
    if(segmentId)o<<",\"segmentId\":"<<segmentId;
    o<<",\"kind\":"<<quote(g.kind)<<",\"closed\":"<<(g.closed?"true":"false")
     <<",\"straight\":"<<(g.straight?"true":"false")<<",\"allOnSkin\":"<<(g.allOnSkin?"true":"false")
     <<",\"startOnSkin\":"<<(g.firstOnSkin?"true":"false")<<",\"endOnSkin\":"<<(g.lastOnSkin?"true":"false")
     <<",\"maxLineDistance\":"<<g.maxLineDistance<<",\"minSkinDistance\":"<<g.minSkinDistance
     <<",\"maxSkinDistance\":"<<g.maxSkinDistance<<",\"onSkinSamples\":"<<g.onSkinSamples
     <<",\"samples\":"<<g.samples<<",\"start\":";point(o,first.start);o<<",\"end\":";point(o,last.end);
    o<<",\"startTangent\":";if(first.tangentValid)vector3(o,first.firstTangent);else o<<"null";
    o<<",\"endTangent\":";if(last.tangentValid)vector3(o,last.lastTangent);else o<<"null";
    o<<",\"chordLength\":"<<g.chordLength<<",\"chordDefined\":"<<(g.chordDefined?"true":"false")
     <<",\"chordSampledError\":"<<g.chordSampledError<<",\"sampledBacktrack\":"<<g.sampledBacktrack
     <<",\"chordUpperBound\":";if(g.chordBoundKnown)o<<g.chordUpperBound;else o<<"null";
    o<<",\"sourceUses\":[";
    for(size_t j=0;j<g.uses.size();++j){if(j)o<<',';const auto& e=uses[g.uses[j]];
        o<<"{\"edgeId\":"<<e.id<<",\"reversed\":"<<(e.reverse?"true":"false")<<",\"curveType\":"<<e.curveType
         <<",\"lineLike\":"<<(e.lineLike?"true":"false")<<",\"issue\":"<<quote(e.issue)<<'}';}
    o<<"],\"bodyStatesEvaluated\":"<<((g.bodyInside+g.bodyOn+g.bodyOutside+g.bodyUnknown>0)?"true":"false")
     <<",\"seam\":"<<(g.seam?"true":"false")<<",\"event\":"<<(g.event?"true":"false")
     <<",\"reason\":"<<quote(g.reason)<<",\"bodyStates\":{\"inside\":"<<g.bodyInside<<",\"on\":"<<g.bodyOn
     <<",\"outside\":"<<g.bodyOutside<<",\"unknown\":"<<g.bodyUnknown<<"},\"polyline\":[";
    bool pc=false;for(int ui:g.uses)for(const auto& p:uses[ui].samples){if(pc)o<<',';pc=true;point(o,p);}
    o<<"],\"roleSegments\":[";
    for(size_t i=0;i<g.roleSegments.size();++i){if(i)o<<',';writeGroup(o,g.roleSegments[i],uses,groupId,int(i)+1);}
    o<<"]}";
}

void makeFragmentFixture(const fs::path& target,bool partial)
{
    double bottom=partial?6:0;
    BRepBuilderAPI_MakePolygon boundary;
    for(auto yz:std::vector<std::pair<double,double>>{{-2,10},{0,10},{2,10},{2,bottom},{0,bottom},{-2,bottom},{-2,bottom+(10-bottom)/3},{-2,bottom+2*(10-bottom)/3}})boundary.Add(gp_Pnt(10,yz.first,yz.second));
    boundary.Close();auto face=BRepBuilderAPI_MakeFace(boundary.Wire()).Face();
    BRepBuilderAPI_MakePolygon contour;for(auto yz:std::vector<std::pair<double,double>>{{-5,0},{5,0},{5,10},{-5,10}})contour.Add(gp_Pnt(35,yz.first,yz.second));contour.Close();
    TopoDS_Compound compound;BRep_Builder b;b.MakeCompound(compound);b.Add(compound,face);b.Add(compound,contour.Wire());
    STEPControl_Writer writer;writer.Transfer(compound,STEPControl_AsIs);if(writer.Write(utf8(target).c_str())!=IFSelect_RetDone)throw std::runtime_error("fixture-write-failed");
}

void writeFixture(const fs::path& target,const TopoDS_Face& face,const std::vector<TopoDS_Wire>& profiles)
{
    TopoDS_Compound c;BRep_Builder b;b.MakeCompound(c);b.Add(c,face);for(auto& w:profiles)b.Add(c,w);
    STEPControl_Writer writer;writer.Transfer(c,STEPControl_AsIs);if(writer.Write(utf8(target).c_str())!=IFSelect_RetDone)throw std::runtime_error("fixture-write-failed");
}

void makeRoleFixtures(const fs::path& directory)
{
    BRepBuilderAPI_MakePolygon rectangle;for(auto p:std::vector<gp_Pnt>{{35,-5,0},{35,5,0},{35,5,10},{35,-5,10}})rectangle.Add(p);rectangle.Close();
    auto triangle=[&](const char* name,std::vector<gp_Pnt> points){BRepBuilderAPI_MakePolygon p;for(auto q:points)p.Add(q);p.Close();writeFixture(directory/name,BRepBuilderAPI_MakeFace(p.Wire()).Face(),{rectangle.Wire()});};
    triangle("only-side.step",{{10,-5,5},{10,0,10},{10,5,5}});
    triangle("side-internal.step",{{10,-5,5},{10,0,7},{10,0,3}});
    triangle("only-internal.step",{{10,-2,3},{10,2,3},{10,0,7}});
    TColgp_Array1OfPnt poles(1,4);poles(1)=gp_Pnt(10,-2,6);poles(2)=gp_Pnt(10,-2.08,7);poles(3)=gp_Pnt(10,-2.08,9);poles(4)=gp_Pnt(10,-2,10);
    Handle(Geom_BezierCurve) curve=new Geom_BezierCurve(poles);BRepBuilderAPI_MakeWire w;
    w.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(10,-2,10),gp_Pnt(10,2,10)).Edge());w.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(10,2,10),gp_Pnt(10,2,6)).Edge());w.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(10,2,6),gp_Pnt(10,-2,6)).Edge());w.Add(BRepBuilderAPI_MakeEdge(curve).Edge());
    writeFixture(directory/"curved-side.step",BRepBuilderAPI_MakeFace(w.Wire()).Face(),{rectangle.Wire()});
    auto circle=[](double x,double radius){return BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(x,0,0),gp_Dir(1,0,0)),radius)).Edge()).Wire();};
    auto outer=circle(10,5),inner=circle(10,2);inner.Reverse();BRepBuilderAPI_MakeFace annulus(outer);annulus.Add(inner);
    writeFixture(directory/"skin-internal.step",annulus.Face(),{circle(35,5)});
    BRepBuilderAPI_MakePolygon pocket;pocket.Add(gp_Pnt(10,-.5,-.5));pocket.Add(gp_Pnt(10,.5,-.5));pocket.Add(gp_Pnt(10,0,.5));pocket.Close();
    writeFixture(directory/"void-is-not-internal.step",BRepBuilderAPI_MakeFace(pocket.Wire()).Face(),{circle(35,5),circle(35,2)});
}

std::string surfaceName(const TopoDS_Face& f){BRepAdaptor_Surface s(f);static const char* names[]={"plane","cylinder","cone","sphere","torus","bezier","bspline","revolution","extrusion","offset","other"};return names[std::min(int(s.GetType()),10)];}

void exportExample(const TopoDS_Face& face,const Profile& profile,const fs::path& output)
{
    auto stem=output.parent_path()/output.stem();auto brep=stem;brep+=".brep";BRepTools::Write(face,utf8(brep).c_str());
    auto step=stem;step+=".step";writeFixture(step,face,profile.wires);
    BRepMesh_IncrementalMesh mesh(face,0.03,false,0.12,true);TopLoc_Location loc;auto tri=BRep_Tool::Triangulation(face,loc);
    auto target=stem;target+=".mesh.json";std::ofstream o(target);o<<std::setprecision(15)<<"{\"vertices\":[";
    if(!tri.IsNull())for(int i=1;i<=tri->NbNodes();++i){if(i>1)o<<',';point(o,tri->Node(i).Transformed(loc.Transformation()));}o<<"],\"triangles\":[";
    if(!tri.IsNull())for(int i=1;i<=tri->NbTriangles();++i){if(i>1)o<<',';int a,b,c;tri->Triangle(i).Get(a,b,c);o<<'['<<a-1<<','<<b-1<<','<<c-1<<']';}o<<"]}";
}

void checkSegments(const fs::path& input,int faceId,const fs::path& lines,const fs::path& output)
{
    STEPControl_Reader reader;if(reader.ReadFile(utf8(input).c_str())!=IFSelect_RetDone)throw std::runtime_error("check-read-failed");reader.TransferRoots();TopTools_IndexedMapOfShape faces;TopExp::MapShapes(reader.OneShape(),TopAbs_FACE,faces);auto face=TopoDS::Face(faces(faceId));
    std::ifstream stream(lines);int count;stream>>count;std::ofstream o(output);o<<std::setprecision(15)<<"{\"faceId\":"<<faceId<<",\"samplesPerLine\":33,\"lines\":[";double total=0;
    for(int i=0;i<count;++i){double x,y,z,X,Y,Z;stream>>x>>y>>z>>X>>Y>>Z;if(!stream)throw std::runtime_error("invalid-segment-list");gp_Pnt a(x,y,z),b(X,Y,Z);double worst=0;int failures=0;
        for(int j=0;j<=32;++j){auto p=a.Translated(gp_Vec(a,b)*(j/32.));BRepExtrema_DistShapeShape d(BRepBuilderAPI_MakeVertex(p).Vertex(),face);if(d.IsDone())worst=std::max(worst,d.Value());else ++failures;}
        total=std::max(total,worst);if(i)o<<',';o<<"{\"index\":"<<i<<",\"maxSampledDistanceMm\":"<<worst<<",\"queryFailures\":"<<failures<<'}';
    }o<<"],\"maxSampledDistanceMm\":"<<total<<",\"scope\":\"finite mother-line membership check, not coverage or accessibility proof\"}";
}

int wmain(int argc,wchar_t** argv)
{
    try{
        if(argc==6&&std::wstring(argv[1])==L"--check-segments"){checkSegments(argv[2],std::stoi(argv[3]),argv[4],argv[5]);return 0;}
        if(argc==3&&std::wstring(argv[1])==L"--fixtures"){fs::create_directories(argv[2]);makeFragmentFixture(fs::path(argv[2])/L"fragmented-partial.step",true);makeFragmentFixture(fs::path(argv[2])/L"fragmented-through.step",false);makeRoleFixtures(argv[2]);return 0;}
        if(argc<3)throw std::runtime_error("usage: face_boundary_probe input.step output.json [face-id-to-export]");
        int selected=argc>=4?std::stoi(argv[3]):0;
        fs::path input=argv[1],output=argv[2];std::ofstream progress(output.string()+".progress.txt");progress<<"Reading STEP"<<std::endl;STEPControl_Reader reader;if(reader.ReadFile(utf8(input).c_str())!=IFSelect_RetDone)throw std::runtime_error("read-failed");progress<<"Transferring STEP roots"<<std::endl;reader.TransferRoots();auto shape=reader.OneShape();progress<<"Mapping topology"<<std::endl;
        TopTools_IndexedMapOfShape faces,allEdges,faceEdges;TopExp::MapShapes(shape,TopAbs_FACE,faces);TopExp::MapShapes(shape,TopAbs_EDGE,allEdges);for(int i=1;i<=faces.Extent();++i)TopExp::MapShapes(faces(i),TopAbs_EDGE,faceEdges);
        Profile profile;for(int i=1;i<=allEdges.Extent();++i)if(!faceEdges.Contains(allEdges(i)))profile.edges.push_back(TopoDS::Edge(allEdges(i)));if(profile.edges.empty())throw std::runtime_error("no-profile");progress<<"Preparing profile: "<<profile.edges.size()<<" edges; "<<faces.Extent()<<" faces"<<std::endl;profile.prepare();progress<<"Classifying faces"<<std::endl;
        fs::create_directories(output.parent_path());
        std::ofstream o(output);o<<std::setprecision(15)<<"{\"version\":4,\"groupingRule\":\"primitive-boundary-and-g1\",\"file\":"<<quote(utf8(input.filename()))<<",\"positionToleranceMm\":"<<posTol<<",\"tangentToleranceRadians\":"<<angleTol<<",\"lineToleranceMm\":"<<lineTol<<",\"skinToleranceMm\":"<<skinTol<<",\"materialToleranceMm\":"<<materialTol<<",\"profileLoopCount\":"<<profile.wires.size()<<",\"skinCheck\":\"49 samples per source edge; not a continuous proof\",\"faces\":[";
        bool fc=false;for(int fi=1;fi<=faces.Extent();++fi){if(selected&&fi!=selected)continue;if(fc)o<<',';fc=true;auto face=TopoDS::Face(faces(fi));auto outer=BRepTools::OuterWire(face);GProp_GProps props;BRepGProp::SurfaceProperties(face,props);o<<"{\"faceId\":"<<fi<<",\"surface\":"<<quote(surfaceName(face))<<",\"area\":"<<props.Mass()<<",\"wires\":[";int wi=0;
            for(TopExp_Explorer w(face,TopAbs_WIRE);w.More();w.Next()){
                if(wi++)o<<',';auto wire=TopoDS::Wire(w.Current());std::vector<EdgeUse> uses;for(BRepTools_WireExplorer e(wire,face);e.More();e.Next()){auto use=readUse(e.Current(),allEdges.FindIndex(e.Current()));use.seam=BRep_Tool::IsClosed(e.Current(),face);uses.push_back(use);}
                int expected=0;for(TopExp_Explorer e(wire,TopAbs_EDGE);e.More();e.Next())++expected;
                auto groups=groupByContinuity(uses);for(auto& g:groups)classify(g,uses,profile);
                o<<"{\"wireId\":"<<wi<<",\"outer\":"<<(wire.IsSame(outer)?"true":"false")<<",\"rawEdgeUses\":"<<uses.size()<<",\"expectedEdgeUses\":"<<expected<<",\"groups\":[";
                for(size_t gi=0;gi<groups.size();++gi){if(gi)o<<',';writeGroup(o,groups[gi],uses,int(gi)+1);}
                o<<"]}";
            }o<<"]}";if(fi%100==0)progress<<"Face "<<fi<<" / "<<faces.Extent()<<std::endl;
        }o<<"],\"profile\":[";bool pc=false;for(auto& e:profile.edges){if(pc)o<<',';pc=true;BRepAdaptor_Curve c(e);o<<'[';for(int j=0;j<=64;++j){if(j)o<<',';point(o,c.Value(c.FirstParameter()+(c.LastParameter()-c.FirstParameter())*j/64.));}o<<']';}o<<"]}";o.close();if(selected)exportExample(TopoDS::Face(faces(selected)),profile,output);return 0;
    }catch(const Standard_Failure& e){std::cerr<<e.GetMessageString()<<'\n';return 1;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
