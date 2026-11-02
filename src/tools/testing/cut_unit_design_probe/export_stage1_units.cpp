// Export the original face region and its boundary chains. No sweep or face split.
#define CUT_UNIT_EXAMPLE_LIBRARY
#include "build_cut_unit_examples.cpp"
#include <BRepBuilderAPI_MakeWire.hxx>
#include <TopoDS_Wire.hxx>
#include <sstream>
#include <map>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Vertex.hxx>
#include <cmath>

std::string q(const std::string& s){std::ostringstream o;o<<'"';for(unsigned char c:s){if(c=='"'||c=='\\')o<<'\\'<<c;else if(c<32)o<<' ';else o<<c;}o<<'"';return o.str();}
std::string num(int x,int n){std::ostringstream o;o<<std::setw(n)<<std::setfill('0')<<x;return o.str();}
struct ChainPlan{int id;bool closed=false;std::vector<std::pair<int,bool>> edges;};
struct FacePlan{bool unit=false;int assembly=0;std::vector<ChainPlan> chains;};
struct AssemblyPlan{int id;std::vector<int> faces;std::vector<ChainPlan> chains;};

struct PreservedChain{
    TopoDS_Shape shape;
    std::vector<TopoDS_Edge> sourceEdges;
    double maximumEndpointGap=0,closingGap=0,maximumVertexGap=0,maximumVertexTolerance=0;
    int unsharedVertexJunctions=0;
    bool geometricallyClosed=false,topologicallyClosed=false,sourceVerticesShared=false;
};
// The assembler proves endpoint continuity at 0.001 mm. That does not imply
// that independently imported faces share TopoDS vertices at their own source
// tolerances. MakeWire may copy edges and replace vertices to bridge that
// difference. Diagnostic chains must instead retain the exact source edges.
PreservedChain preserveAssemblyChain(const ChainPlan& chain,const TopTools_IndexedMapOfShape& edges){
    constexpr double closureTolerance=.001;
    if(chain.edges.empty())throw std::runtime_error("assembly-boundary-empty");
    PreservedChain result;std::vector<gp_Pnt> starts,ends;
    std::vector<TopoDS_Vertex> firstVertices,lastVertices;
    for(auto [eid,reverse]:chain.edges){
        if(eid<1||eid>edges.Extent())throw std::runtime_error("bad-assembly-source-edge-id");
        auto edge=TopoDS::Edge(edges(eid));edge.Orientation(reverse?TopAbs_REVERSED:TopAbs_FORWARD);
        if(BRep_Tool::Degenerated(edge))throw std::runtime_error("assembly-boundary-degenerate-physical-edge");
        double first,last;if(BRep_Tool::Curve(edge,first,last).IsNull())throw std::runtime_error("assembly-boundary-missing-3d-curve");
        BRepAdaptor_Curve curve(edge);auto start=curve.Value(reverse?curve.LastParameter():curve.FirstParameter());auto end=curve.Value(reverse?curve.FirstParameter():curve.LastParameter());
        TopoDS_Vertex firstVertex,lastVertex;TopExp::Vertices(edge,firstVertex,lastVertex,true);
        if(firstVertex.IsNull()||lastVertex.IsNull())throw std::runtime_error("assembly-boundary-missing-source-vertex");
        result.maximumVertexTolerance=std::max({result.maximumVertexTolerance,BRep_Tool::Tolerance(firstVertex),BRep_Tool::Tolerance(lastVertex)});
        result.sourceEdges.push_back(edge);starts.push_back(start);ends.push_back(end);firstVertices.push_back(firstVertex);lastVertices.push_back(lastVertex);
    }
    result.geometricallyClosed=chain.closed;
    result.closingGap=ends.back().Distance(starts.front());
    const size_t junctions=chain.closed?result.sourceEdges.size():result.sourceEdges.size()-1;
    for(size_t i=0;i<junctions;++i){
        size_t next=(i+1)%result.sourceEdges.size();double gap=ends[i].Distance(starts[next]);
        if(!std::isfinite(gap)||gap>closureTolerance){std::ostringstream message;message<<std::setprecision(15)<<"assembly-boundary-geometric-gap-exceeds-tolerance: chain="<<chain.id<<" afterEdge="<<chain.edges[i].first<<" gapMm="<<gap<<" toleranceMm="<<closureTolerance;throw std::runtime_error(message.str());}
        result.maximumEndpointGap=std::max(result.maximumEndpointGap,gap);if(next==0)result.closingGap=gap;
        result.maximumVertexGap=std::max(result.maximumVertexGap,BRep_Tool::Pnt(lastVertices[i]).Distance(BRep_Tool::Pnt(firstVertices[next])));
        if(!lastVertices[i].IsSame(firstVertices[next]))++result.unsharedVertexJunctions;
    }
    BRep_Builder builder;result.sourceVerticesShared=result.unsharedVertexJunctions==0;result.topologicallyClosed=chain.closed&&result.sourceVerticesShared;
    if(result.sourceVerticesShared){
        TopoDS_Wire wire;builder.MakeWire(wire);for(const auto& edge:result.sourceEdges)builder.Add(wire,edge);wire.Closed(chain.closed);
        if(!BRepCheck_Analyzer(wire).IsValid())throw std::runtime_error("assembly-boundary-shared-topology-wire-invalid");
        result.shape=wire;
    }else{
        TopoDS_Compound compound;builder.MakeCompound(compound);for(const auto& edge:result.sourceEdges)builder.Add(compound,edge);result.shape=compound;
    }
    return result;
}
void writeChainEvidence(std::ostream& out,const PreservedChain& chain){
    out<<"{\"shapeType\":"<<q(chain.shape.ShapeType()==TopAbs_WIRE?"Wire":"Compound")
       <<",\"geometricallyConnected\":true,\"geometricallyClosed\":"<<(chain.geometricallyClosed?"true":"false")<<",\"topologicallyClosed\":"<<(chain.topologicallyClosed?"true":"false")
       <<",\"closureToleranceMm\":0.001,\"maximumEndpointGapMm\":"<<chain.maximumEndpointGap
       <<",\"closingGapMm\":"<<chain.closingGap<<",\"maximumSourceVertexGapMm\":"<<chain.maximumVertexGap
       <<",\"maximumSourceVertexToleranceMm\":"<<chain.maximumVertexTolerance
       <<",\"unsharedVertexJunctions\":"<<chain.unsharedVertexJunctions<<",\"sourceEdgeOccurrences\":"<<chain.sourceEdges.size()
       <<",\"sourceEdgesUnmodified\":true,\"sourceVertexIdentityPreserved\":true,\"sourceTolerancesPreserved\":true,\"diagnosticReason\":"
       <<q(chain.sourceVerticesShared?"":"geometrically-connected-source-vertices-not-shared")<<'}';
}
void auditPreservedChain(const PreservedChain& expected,const TopoDS_Shape& restored){
    if(restored.ShapeType()!=expected.shape.ShapeType()||BRepCheck_Analyzer(restored).IsValid()!=BRepCheck_Analyzer(expected.shape).IsValid())throw std::runtime_error("assembly-chain-readback-topology-changed");
    size_t index=0;
    for(TopoDS_Iterator child(restored);child.More();child.Next(),++index){
        if(index>=expected.sourceEdges.size()||child.Value().ShapeType()!=TopAbs_EDGE)throw std::runtime_error("assembly-chain-readback-edge-count-changed");
        const auto& source=expected.sourceEdges[index];auto edge=TopoDS::Edge(child.Value());
        if(edge.Orientation()!=source.Orientation()||std::abs(BRep_Tool::Tolerance(edge)-BRep_Tool::Tolerance(source))>1e-12)throw std::runtime_error("assembly-chain-readback-source-edge-changed");
        BRepAdaptor_Curve before(source),after(edge);
        if(std::abs(before.FirstParameter()-after.FirstParameter())>1e-12||std::abs(before.LastParameter()-after.LastParameter())>1e-12)throw std::runtime_error("assembly-chain-readback-range-changed");
        for(double fraction:{0.,.5,1.}){double t=before.FirstParameter()+(before.LastParameter()-before.FirstParameter())*fraction;if(before.Value(t).Distance(after.Value(t))>1e-7)throw std::runtime_error("assembly-chain-readback-curve-changed");}
        TopoDS_Vertex bf,bl,af,al;TopExp::Vertices(source,bf,bl,true);TopExp::Vertices(edge,af,al,true);
        if(af.IsNull()||al.IsNull()||std::abs(BRep_Tool::Tolerance(bf)-BRep_Tool::Tolerance(af))>1e-12||std::abs(BRep_Tool::Tolerance(bl)-BRep_Tool::Tolerance(al))>1e-12)throw std::runtime_error("assembly-chain-readback-source-vertex-changed");
    }
    if(index!=expected.sourceEdges.size())throw std::runtime_error("assembly-chain-readback-source-coverage-changed");
}

Quantity_Color colorFor(int id,bool unit){if(!unit)return Quantity_Color(.55,.59,.65,Quantity_TOC_sRGB);double h=std::fmod(id*.61803398875,1.)*6;int i=int(h);double f=h-i,v=.88,s=.52,p=v*(1-s),a=v*(1-s*f),b=v*(1-s*(1-f));double r=0,g=0,z=0;switch(i%6){case 0:r=v;g=b;z=p;break;case 1:r=a;g=v;z=p;break;case 2:r=p;g=v;z=b;break;case 3:r=p;g=a;z=v;break;case 4:r=b;g=p;z=v;break;default:r=v;g=p;z=a;}return Quantity_Color(r,g,z,Quantity_TOC_sRGB);}
void colorJson(std::ostream& out,const Quantity_Color& c){double r,g,b;c.Values(r,g,b,Quantity_TOC_sRGB);out<<'['<<r<<','<<g<<','<<b<<']';}
gp_Pnt areaCenter(const TopoDS_Shape& shape){GProp_GProps props;BRepGProp::SurfaceProperties(shape,props);return props.CentreOfMass();}
void addShape(const Handle(TDocStd_Document)& doc,const TopoDS_Face& f,const std::string& name,const Quantity_Color& color){auto local=f;auto placement=local.Location().Transformation();local.Location(TopLoc_Location());auto shape=BRepBuilderAPI_Transform(local,placement,true).Shape();auto label=XCAFDoc_DocumentTool::ShapeTool(doc->Main())->AddShape(shape,false);TDataStd_Name::Set(label,name.c_str());XCAFDoc_DocumentTool::ColorTool(doc->Main())->SetColor(label,color,XCAFDoc_ColorSurf);}

void preview(const TopoDS_Face& face,const fs::path& target){
    BRepTools::Clean(face);BRepMesh_IncrementalMesh mesh(face,.03,false,.12,false);TopLoc_Location location;auto tri=BRep_Tool::Triangulation(face,location);if(tri.IsNull())throw std::runtime_error("mesh-failed");
    std::ofstream out(target);out<<std::setprecision(13)<<"{\"vertices\":[";
    for(int i=1;i<=tri->NbNodes();++i){if(i>1)out<<',';point(out,tri->Node(i).Transformed(location.Transformation()));}
    out<<"],\"triangles\":[";for(int i=1;i<=tri->NbTriangles();++i){if(i>1)out<<',';int a,b,c;tri->Triangle(i).Get(a,b,c);out<<'['<<a-1<<','<<b-1<<','<<c-1<<']';}
    out<<"],\"edges\":[]}";
}

int wmain(int argc,wchar_t** argv){try{
    if(argc!=4&&argc!=5)throw std::runtime_error("usage: export_stage1_units source.step controls.txt output [verified-preview-cache.txt]");
    fs::path input=argv[1],folder=argv[3];for(auto sub:{"units","pending","chains","meshes","sources"})fs::create_directories(folder/sub);
    std::ifstream control(argv[2]);int count;control>>count;std::vector<FacePlan> plans(count+1);
    for(int k=0;k<count;++k){int id,unit,n;control>>id>>unit>>n;if(id<1||id>count)throw std::runtime_error("bad-face-id");plans[id].unit=unit;
        for(int c=0;c<n;++c){int cid,m;control>>cid>>m;ChainPlan chain;chain.id=cid;for(int j=0;j<m;++j){int e,r;control>>e>>r;chain.edges.push_back({e,r!=0});}plans[id].chains.push_back(chain);}}
    int assemblyCount;control>>assemblyCount;std::vector<AssemblyPlan> assemblies;
    for(int k=0;k<assemblyCount;++k){AssemblyPlan a;int nf,nc;control>>a.id>>nf>>nc;
        for(int i=0;i<nf;++i){int fid;control>>fid;if(fid<1||fid>count||plans[fid].assembly)throw std::runtime_error("assembly-face-ownership-invalid");a.faces.push_back(fid);plans[fid].assembly=a.id;}
        for(int i=0;i<nc;++i){ChainPlan chain;int n,closed;control>>chain.id>>n>>closed;chain.closed=closed!=0;for(int j=0;j<n;++j){int eid,r;control>>eid>>r;chain.edges.push_back({eid,r!=0});}a.chains.push_back(chain);}assemblies.push_back(a);}
    if(!control)throw std::runtime_error("invalid-controls");
    struct CachedPreview{std::string mesh,issue;double area=0;bool valid=false;};
    std::map<int,CachedPreview> previewCache;std::string cacheSourceHash;
    if(argc==5){std::ifstream cache(argv[4]);int size;cache>>std::quoted(cacheSourceHash)>>size;for(int i=0;i<size;++i){int fid,valid;CachedPreview entry;cache>>fid>>std::quoted(entry.mesh)>>std::quoted(entry.issue)>>entry.area>>valid;entry.valid=valid!=0;if(fid<1||fid>count||previewCache.count(fid))throw std::runtime_error("invalid-preview-cache-face");if(!entry.mesh.empty()&&!fs::is_regular_file(folder/entry.mesh))throw std::runtime_error("verified-preview-cache-file-missing");previewCache[fid]=entry;}if(!cache)throw std::runtime_error("invalid-preview-cache-controls");}
    STEPControl_Reader reader;if(reader.ReadFile(utf8(input).c_str())!=IFSelect_RetDone)throw std::runtime_error("step-read-failed");reader.TransferRoots();auto shape=reader.OneShape();TopTools_IndexedMapOfShape faces,edges;TopExp::MapShapes(shape,TopAbs_FACE,faces);TopExp::MapShapes(shape,TopAbs_EDGE,edges);if(faces.Extent()!=count)throw std::runtime_error("source-face-count-changed");
    Handle(TDocStd_Document) document=new TDocStd_Document("BinXCAF");std::ofstream out(folder/"geometry.json"),progress(folder/"progress.txt");out<<std::setprecision(15)<<"{\"faces\":[";
    for(int id=1;id<=count;++id){progress<<id<<std::endl;auto face=TopoDS::Face(faces(id));auto& plan=plans[id];bool valid=BRepCheck_Analyzer(face).IsValid();bool unit=valid&&plan.unit;auto stem="F"+num(id,4);auto brep=plan.assembly?"sources/"+stem+".brep":std::string(unit?"units/":"pending/")+stem+(unit?"_U001":"")+".brep";
        if(!BRepTools::Write(face,utf8(folder/brep).c_str()))throw std::runtime_error("brep-write-failed");TopoDS_Shape round;BRep_Builder builder;if(!BRepTools::Read(round,utf8(folder/brep).c_str(),builder))throw std::runtime_error("brep-readback-failed");double sourceArea=area(face),roundArea=area(round),delta=std::abs(sourceArea-roundArea);bool roundValid=BRepCheck_Analyzer(round).IsValid();if(roundValid!=valid||delta>std::max(1e-7,sourceArea*1e-9))throw std::runtime_error("brep-readback-changed");
        int colorId=id;if(plan.assembly){auto a=std::find_if(assemblies.begin(),assemblies.end(),[&](auto& a){return a.id==plan.assembly;});colorId=a->faces.front();}
        auto color=colorFor(colorId,unit);addShape(document,face,stem+(plan.assembly?"_A"+num(plan.assembly,3)+"_UNIT":unit?"_U001_UNIT":"_PENDING"),color);
        std::string mesh="meshes/"+stem+".json",meshIssue;
        bool reusedPreview=false;
        if(auto cached=previewCache.find(id);cached!=previewCache.end()){const auto& entry=cached->second;if(entry.valid!=valid||std::abs(entry.area-sourceArea)>std::max(1e-7,sourceArea*1e-9))throw std::runtime_error("verified-preview-cache-source-face-changed");mesh=entry.mesh;meshIssue=entry.issue;reusedPreview=true;}
        else try{preview(TopoDS::Face(round),folder/mesh);}catch(const Standard_Failure& e){mesh.clear();meshIssue=e.GetMessageString();}catch(const std::exception& e){mesh.clear();meshIssue=e.what();}
        if(id>1)out<<',';out<<"{\"faceId\":"<<id<<",\"assemblyId\":"<<plan.assembly<<",\"valid\":"<<(valid?"true":"false")<<",\"areaMm2\":"<<sourceArea<<",\"brep\":"<<q(brep)<<",\"mesh\":"<<q(mesh)<<",\"meshIssue\":"<<q(meshIssue)<<",\"color\":";colorJson(out,color);
        out<<",\"previewReused\":"<<(reusedPreview?"true":"false")<<",\"roundTrip\":{\"valid\":"<<(roundValid?"true":"false")<<",\"areaDeltaMm2\":"<<delta<<",\"region\":\"OriginalFaceUnmodified\"},\"chainFiles\":{";
        bool comma=false;for(const auto& chain:plan.chains){TopoDS_Wire wire;builder.MakeWire(wire);for(auto [eid,reverse]:chain.edges){if(eid<1||eid>edges.Extent())throw std::runtime_error("bad-source-edge-id");auto edge=TopoDS::Edge(edges(eid));edge.Orientation(reverse?TopAbs_REVERSED:TopAbs_FORWARD);builder.Add(wire,edge);}auto file="chains/"+stem+"_C"+num(chain.id,2)+".brep";if(!BRepTools::Write(wire,utf8(folder/file).c_str()))throw std::runtime_error("chain-write-failed");if(comma)out<<',';comma=true;out<<q(std::to_string(chain.id))<<':'<<q(file);}
        out<<"}}";out.flush();
    }out<<"],\"assembledUnits\":[";
    bool ac=false;for(const auto& a:assemblies){BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);double expected=0;
        for(int fid:a.faces){auto f=TopoDS::Face(faces(fid));if(!BRepCheck_Analyzer(f).IsValid())throw std::runtime_error("assembly-source-face-invalid");builder.Add(compound,f);expected+=area(f);}
        auto file="units/A"+num(a.id,3)+".brep";if(!BRepTools::Write(compound,utf8(folder/file).c_str()))throw std::runtime_error("assembly-write-failed");TopoDS_Shape round;
        if(!BRepTools::Read(round,utf8(folder/file).c_str(),builder))throw std::runtime_error("assembly-readback-failed");TopTools_IndexedMapOfShape rf;TopExp::MapShapes(round,TopAbs_FACE,rf);
        double delta=std::abs(area(round)-expected);bool valid=BRepCheck_Analyzer(round).IsValid();if(!valid||rf.Extent()!=a.faces.size()||delta>std::max(1e-7,expected*1e-9))throw std::runtime_error("assembly-coverage-changed");
        if(ac)out<<',';ac=true;out<<"{\"assemblyId\":"<<a.id<<",\"brep\":"<<q(file)<<",\"areaMm2\":"<<expected<<",\"color\":";colorJson(out,colorFor(a.faces.front(),true));
        out<<",\"roundTrip\":{\"valid\":true,\"faceCount\":"<<rf.Extent()<<",\"areaDeltaMm2\":"<<delta<<",\"region\":\"OriginalFacesUnmodified\"},\"chainFiles\":{";
        bool cc=false;std::vector<std::pair<int,PreservedChain>> chainEvidence;for(const auto& c:a.chains){auto chain=preserveAssemblyChain(c,edges);
            auto chainFile="chains/A"+num(a.id,3)+"_C"+num(c.id,2)+".brep";if(!BRepTools::Write(chain.shape,utf8(folder/chainFile).c_str()))throw std::runtime_error("assembly-chain-write-failed");
            TopoDS_Shape readChain;if(!BRepTools::Read(readChain,utf8(folder/chainFile).c_str(),builder))throw std::runtime_error("assembly-chain-readback-failed");auditPreservedChain(chain,readChain);
            std::cout<<"Assembly A"<<num(a.id,3)<<" chain "<<c.id<<": "<<(chain.topologicallyClosed?"Wire":"Compound")<<", geometric gap "<<std::setprecision(15)<<chain.maximumEndpointGap<<" mm, unshared source-vertex junctions "<<chain.unsharedVertexJunctions<<std::endl;
            if(cc)out<<',';cc=true;out<<q(std::to_string(c.id))<<':'<<q(chainFile);
            chainEvidence.emplace_back(c.id,std::move(chain));
        }out<<"},\"chainTopology\":{";bool evidenceComma=false;for(const auto& [id,evidence]:chainEvidence){if(evidenceComma)out<<',';evidenceComma=true;out<<q(std::to_string(id))<<':';writeChainEvidence(out,evidence);}out<<"}}";
    }
    // One complete model: each direct child is a unit (or one unresolved face).
    // Keep source faces and placements intact; BREP carries hierarchy, not color.
    BRep_Builder wholeBuilder;TopoDS_Compound whole;wholeBuilder.MakeCompound(whole);
    struct WholeGroup{std::string key;bool unit;std::vector<int> faces;Quantity_Color color;};
    std::vector<WholeGroup> wholeGroups;
    for(const auto& a:assemblies)wholeGroups.push_back({"A"+num(a.id,3),true,a.faces,colorFor(a.faces.front(),true)});
    for(int fid=1;fid<=count;++fid)if(!plans[fid].assembly){bool unit=plans[fid].unit&&BRepCheck_Analyzer(faces(fid)).IsValid();
        wholeGroups.push_back({"F"+num(fid,4)+(unit?"_U001":"_PENDING"),unit,{fid},colorFor(fid,unit)});}
    double wholeArea=0;int groupedFaces=0;
    for(const auto& g:wholeGroups){TopoDS_Compound child;wholeBuilder.MakeCompound(child);
        for(int fid:g.faces){wholeBuilder.Add(child,faces(fid));wholeArea+=area(faces(fid));++groupedFaces;}wholeBuilder.Add(whole,child);}
    const std::string wholeFile="cut-units.brep";
    if(groupedFaces!=count||!BRepTools::Write(whole,utf8(folder/wholeFile).c_str()))throw std::runtime_error("whole-model-write-failed");
    TopoDS_Shape wholeRound;if(!BRepTools::Read(wholeRound,utf8(folder/wholeFile).c_str(),wholeBuilder))throw std::runtime_error("whole-model-readback-failed");
    TopTools_IndexedMapOfShape wholeFaces;TopExp::MapShapes(wholeRound,TopAbs_FACE,wholeFaces);
    const double wholeDelta=std::abs(area(wholeRound)-wholeArea);int childIndex=0;
    for(TopoDS_Iterator it(wholeRound);it.More();it.Next(),++childIndex){
        if(childIndex>=wholeGroups.size()||it.Value().ShapeType()!=TopAbs_COMPOUND)throw std::runtime_error("whole-model-group-changed");
        const auto& expected=wholeGroups[childIndex];TopTools_IndexedMapOfShape actual;TopExp::MapShapes(it.Value(),TopAbs_FACE,actual);
        if(actual.Extent()!=expected.faces.size())throw std::runtime_error("whole-model-group-coverage-changed");
        for(int k=1;k<=actual.Extent();++k){const auto& original=faces(expected.faces[k-1]);const auto& round=actual(k);
            if(BRepCheck_Analyzer(original).IsValid()!=BRepCheck_Analyzer(round).IsValid()||std::abs(area(original)-area(round))>std::max(1e-7,area(original)*1e-9))throw std::runtime_error("whole-model-face-changed");
            auto before=areaCenter(original),after=areaCenter(round);if(before.Distance(after)>1e-7)throw std::runtime_error("whole-model-placement-changed");}
    }
    if(childIndex!=wholeGroups.size()||wholeFaces.Extent()!=count||wholeDelta>std::max(1e-7,wholeArea*1e-9))throw std::runtime_error("whole-model-coverage-changed");
    std::ofstream colors(folder/"cut-units.colors.json");colors<<std::setprecision(15)<<"{\"brep\":"<<q(wholeFile)<<",\"faceCount\":"<<count<<",\"scope\":\"AllClassifyMachiningFaces\",\"groups\":[";
    for(size_t i=0;i<wholeGroups.size();++i){const auto& g=wholeGroups[i];if(i)colors<<',';colors<<"{\"childIndex\":"<<i+1<<",\"key\":"<<q(g.key)<<",\"status\":"<<q(g.unit?"organized":"pending")<<",\"sourceFaceIds\":[";
        for(size_t j=0;j<g.faces.size();++j){if(j)colors<<',';colors<<g.faces[j];}colors<<"],\"color\":";colorJson(colors,g.color);colors<<'}';}
    colors<<"],\"roundTrip\":{\"faceCount\":"<<wholeFaces.Extent()<<",\"groupCount\":"<<childIndex<<",\"areaDeltaMm2\":"<<wholeDelta<<",\"faceValidityPreserved\":true,\"placementsPreserved\":true}}";
    out<<"],\"wholeModel\":{\"file\":"<<q(wholeFile)<<",\"colors\":\"cut-units.colors.json\",\"faceCount\":"<<wholeFaces.Extent()<<",\"groupCount\":"<<childIndex<<",\"areaDeltaMm2\":"<<wholeDelta<<"},\"faceCount\":"<<count<<",\"newFaceBoundaries\":0,\"previewDeflectionMm\":0.03,\"verifiedPreviewCacheSourceHash\":"<<q(cacheSourceHash)<<"}";out.close();
    STEPCAFControl_Writer writer;writer.SetColorMode(true);writer.SetNameMode(true);if(!writer.Transfer(document,STEPControl_AsIs)||writer.Write(utf8(folder/"colored.step").c_str())!=IFSelect_RetDone)throw std::runtime_error("colored-step-write-failed");return 0;
}catch(const Standard_Failure& e){std::cerr<<e.GetMessageString();return 1;}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
