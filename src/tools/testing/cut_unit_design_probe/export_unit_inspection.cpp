// Native CAD inspection: exact original faces plus separately named display markers.
#define CUT_UNIT_EXAMPLE_LIBRARY
#include "build_cut_unit_examples.cpp"
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <Geom_Circle.hxx>
#include <TDF_LabelSequence.hxx>
#include <map>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <sstream>
#include <array>

struct DisplayBoundary{std::string name,role;std::vector<std::pair<int,bool>> edges;};
struct DisplayUnit{std::string name;std::vector<int> faces;double r,g,b;std::vector<DisplayBoundary> boundaries;};
struct DisplayFallback{std::string boundary,reason;int sourceEdge=0,segments=0;double sampledDeflection=0,requestedDeflection=0;};
struct DisplayScale{double modelDiagonalMm=0,skinRadiusMm=0,localWidthMm=0;};
struct BoundaryStyle{double radiusMm=0,dashMm=0,samplingDeflectionMm=0;};
struct DisplayMarkerStyle{std::string name;double radiusMm,dashMm,samplingDeflectionMm,localWidthMm;};
std::array<double,6> exactBounds(const TopoDS_Shape& shape){Bnd_Box box;BRepBndLib::AddOptimal(shape,box,false,false);std::array<double,6> result;box.Get(result[0],result[1],result[2],result[3],result[4],result[5]);return result;}
double displayModelDiagonal(const TopoDS_Shape& shape){const auto b=exactBounds(shape);return gp_Pnt(b[0],b[1],b[2]).Distance(gp_Pnt(b[3],b[4],b[5]));}
double displayBoundaryLength(const DisplayBoundary& boundary,const TopTools_IndexedMapOfShape& edges){double length=0;for(auto [id,reverse]:boundary.edges){BRepAdaptor_Curve curve(TopoDS::Edge(edges(id)));length+=GCPnts_AbscissaPoint::Length(curve,curve.FirstParameter(),curve.LastParameter(),1e-8);}return length;}
DisplayScale displayScale(const TopoDS_Shape& model,const DisplayUnit& unit,const TopTools_IndexedMapOfShape& faces,const TopTools_IndexedMapOfShape& edges,double modelDiagonalMm=0){
    DisplayScale scale;scale.modelDiagonalMm=modelDiagonalMm>0?modelDiagonalMm:displayModelDiagonal(model);scale.skinRadiusMm=std::min(.3,scale.modelDiagonalMm*.0015);
    double surfaceArea=0,perimeter=0;for(int id:unit.faces)surfaceArea+=area(faces(id));for(const auto& boundary:unit.boundaries)if(boundary.role!="InternalSeam")perimeter+=displayBoundaryLength(boundary,edges);
    // 2A/P measures the width of a narrow cut face; fragmented topology does
    // not set line width. The limit is display-only and never alters source CAD.
    if(perimeter>Precision::Confusion()&&surfaceArea>0){scale.localWidthMm=2*surfaceArea/perimeter;scale.skinRadiusMm=std::min(scale.skinRadiusMm,scale.localWidthMm*.06);}return scale;
}
BoundaryStyle boundaryStyle(const std::string& role,double length,const DisplayScale& scale){
    BoundaryStyle style;const double skin=std::min(scale.skinRadiusMm,length*.025);
    style.radiusMm=role=="Skin"?skin:role=="Side"?skin*(.055/.3):role=="InternalSeam"?skin*(.035/.3):0.;
    // Below the CAD kernel's working resolution use the exact curve, rather
    // than inflating tiny model features to a fixed minimum tube radius.
    if(style.radiusMm<4*Precision::Confusion())style.radiusMm=0;
    style.dashMm=length>0?std::clamp(scale.skinRadiusMm*4,length/90.,length/9.):0;
    style.samplingDeflectionMm=std::min(.002,std::max(4*Precision::Confusion(),style.radiusMm*.1));return style;
}
std::string quoted(const std::string& value){std::ostringstream o;o<<'"';for(char c:value){if(c=='"'||c=='\\')o<<'\\';o<<c;}o<<'"';return o.str();}
std::string faceName(int fid){std::ostringstream o;o<<"FACE_F"<<std::setw(4)<<std::setfill('0')<<fid;return o.str();}
TopoDS_Shape independent(const TopoDS_Shape& shape){auto local=shape;auto pose=local.Location().Transformation();local.Location(TopLoc_Location());return BRepBuilderAPI_Transform(local,pose,true).Shape();}
Quantity_Color roleColor(const std::string& role){if(role=="Skin")return Quantity_Color(.314,.847,.890,Quantity_TOC_sRGB);if(role=="Side")return Quantity_Color(1.,.675,.345,Quantity_TOC_sRGB);if(role=="Internal")return Quantity_Color(.808,.569,1.,Quantity_TOC_sRGB);if(role=="InternalSeam")return Quantity_Color(1.,.32,.68,Quantity_TOC_sRGB);return Quantity_Color(1.,.43,.51,Quantity_TOC_sRGB);}
TopoDS_Shape stroke(const TopoDS_Edge& edge,double radius,int sourceEdge,const std::string& context,std::vector<DisplayFallback>& fallbacks,double displayDeflection=.002){
    if(radius==0)return independent(edge);
    std::string failure;
    try{
    BRepAdaptor_Curve curve(edge);if(curve.GetType()==GeomAbs_BSplineCurve||curve.GetType()==GeomAbs_BezierCurve||curve.Continuity()==GeomAbs_C0)throw std::runtime_error("display-spline-uses-sampled-tube-to-avoid-unsafe-pipe");gp_Pnt p;gp_Vec tangent;const bool reverse=edge.Orientation()==TopAbs_REVERSED;curve.D1(reverse?curve.LastParameter():curve.FirstParameter(),p,tangent);if(reverse)tangent.Reverse();
    if(tangent.SquareMagnitude()<1e-20)throw std::runtime_error("display-stroke-zero-tangent");
    auto circle=new Geom_Circle(gp_Ax2(p,gp_Dir(tangent)),radius);
    auto profile=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(circle).Edge()).Wire();
    auto spine=BRepBuilderAPI_MakeWire(edge).Wire();BRepOffsetAPI_MakePipe pipe(spine,profile);
    if(!pipe.IsDone()||pipe.Shape().IsNull())throw std::runtime_error("display-stroke-pipe-failed");
    return pipe.Shape();
    }catch(const Standard_Failure& error){failure=error.GetMessageString()?error.GetMessageString():"OCCT display sweep failed";}catch(const std::exception& error){failure=error.what();}
    // A display tube can fail on an otherwise usable source curve (for example,
    // a spline's singular end tangent). Keep the exact curve and add a sampled
    // display-only tube; this never changes the machining face or source edge.
    BRepAdaptor_Curve curve(edge);
    GCPnts_QuasiUniformDeflection sample(curve,displayDeflection,GeomAbs_C0);
    if(!sample.IsDone()||sample.NbPoints()<2)throw std::runtime_error(context+" E"+std::to_string(sourceEdge)+": display-polyline-sampling-failed after "+failure);
    BRep_Builder builder;TopoDS_Compound result;builder.MakeCompound(result);builder.Add(result,independent(edge));int pieces=0;double observed=0;
    for(int i=1;i<sample.NbPoints();++i){const auto a=sample.Value(i),b=sample.Value(i+1);gp_Vec direction(a,b);double length=direction.Magnitude();
        if(length>Precision::Confusion()){builder.Add(result,BRepPrimAPI_MakeCylinder(gp_Ax2(a,gp_Dir(direction)),radius,length).Shape());++pieces;}
        else {builder.Add(result,BRepPrimAPI_MakeSphere(a,radius).Shape());++pieces;}
        const auto midpoint=curve.Value((sample.Parameter(i)+sample.Parameter(i+1))*.5);observed=std::max(observed,midpoint.Distance(gp_Pnt((a.X()+b.X())*.5,(a.Y()+b.Y())*.5,(a.Z()+b.Z())*.5)));
    }
    fallbacks.push_back({context,failure,sourceEdge,pieces,observed,displayDeflection});std::cerr<<"DISPLAY FALLBACK "<<context<<" E"<<sourceEdge<<": "<<failure<<" -> "<<pieces<<" sampled tube pieces"<<std::endl;
    return result;
}
TopoDS_Shape boundaryDisplay(const DisplayBoundary& boundary,const TopTools_IndexedMapOfShape& edges,int& segments,const std::string& unit,std::vector<DisplayFallback>& fallbacks,const DisplayScale& scale){
    BRep_Builder builder;TopoDS_Compound result;builder.MakeCompound(result);segments=0;
    const bool dashed=boundary.role=="Side"||boundary.role=="InternalSeam"||boundary.role=="Unresolved";
    std::vector<TopoDS_Edge> source;std::vector<int> sourceIds;std::vector<double> lengths;double total=0;
    for(auto [id,reverse]:boundary.edges){std::cerr<<"INSPECTION EDGE "<<unit<<"/"<<boundary.name<<" E"<<id<<std::endl;auto edge=TopoDS::Edge(independent(edges(id)));edge.Orientation(TopAbs_FORWARD);if(reverse)edge.Reverse();BRepAdaptor_Curve curve(edge);double length=GCPnts_AbscissaPoint::Length(curve,curve.FirstParameter(),curve.LastParameter(),1e-8);if(length<1e-9)continue;source.push_back(edge);sourceIds.push_back(id);lengths.push_back(length);total+=length;}
    const auto style=boundaryStyle(boundary.role,total,scale);const double radius=style.radiusMm,dash=style.dashMm,period=dash*1.65;double offset=0;
    for(size_t i=0;i<source.size();++i){const auto& edge=source[i];const double length=lengths[i];
        if(!dashed){builder.Add(result,stroke(edge,radius,sourceIds[i],unit+"/"+boundary.name+"_"+boundary.role,fallbacks,style.samplingDeflectionMm));++segments;offset+=length;continue;}
        BRepAdaptor_Curve adaptor(edge);double first,last;auto curve=BRep_Tool::Curve(edge,first,last);bool reversed=edge.Orientation()==TopAbs_REVERSED;
        auto parameter=[&](double distance){if(distance<1e-9)return first;if(length-distance<1e-9)return last;GCPnts_AbscissaPoint point(1e-8,adaptor,distance,first);if(!point.IsDone())throw std::runtime_error("display-dash-parameter-failed");return point.Parameter();};
        for(long k=static_cast<long>(std::floor(offset/period));k*period<offset+length;++k){double a=std::max(0.,k*period-offset),b=std::min(length,k*period+dash-offset);if(b-a<1e-7)continue;
            double u=parameter(reversed?length-b:a),v=parameter(reversed?length-a:b);auto part=BRepBuilderAPI_MakeEdge(curve,u,v).Edge();builder.Add(result,stroke(part,radius,sourceIds[i],unit+"/"+boundary.name+"_"+boundary.role,fallbacks,style.samplingDeflectionMm));++segments;}
        offset+=length;
    }
    return result;
}

int wmain(int argc,wchar_t** argv){try{
    if(argc!=4)throw std::runtime_error("usage: export_unit_inspection source.step display-controls.txt output");
    fs::path input=argv[1],folder=argv[3];std::ifstream control(argv[2]);std::string modelName;int unitCount;control>>std::quoted(modelName)>>unitCount;std::vector<DisplayUnit> units;
    for(int n=0;n<unitCount;++n){DisplayUnit unit;int faceCount,boundaryCount;control>>unit.name>>faceCount>>boundaryCount>>unit.r>>unit.g>>unit.b;
        for(int i=0;i<faceCount;++i){int fid;control>>fid;unit.faces.push_back(fid);}for(int i=0;i<boundaryCount;++i){DisplayBoundary boundary;int count;control>>boundary.name>>boundary.role>>count;for(int j=0;j<count;++j){int eid,reverse;control>>eid>>reverse;boundary.edges.emplace_back(eid,reverse!=0);}unit.boundaries.push_back(boundary);}units.push_back(unit);}
    if(!control)throw std::runtime_error("invalid-display-controls");
    STEPControl_Reader sourceReader;if(sourceReader.ReadFile(utf8(input).c_str())!=IFSelect_RetDone)throw std::runtime_error("source-read-failed");sourceReader.TransferRoots();auto shape=sourceReader.OneShape();TopTools_IndexedMapOfShape faces,edges;TopExp::MapShapes(shape,TopAbs_FACE,faces);TopExp::MapShapes(shape,TopAbs_EDGE,edges);const double modelDiagonalMm=displayModelDiagonal(shape);
    Handle(TDocStd_Document) doc=new TDocStd_Document("BinXCAF");auto shapes=XCAFDoc_DocumentTool::ShapeTool(doc->Main());auto colors=XCAFDoc_DocumentTool::ColorTool(doc->Main());
    auto wholeModel=shapes->NewShape();TDataStd_Name::Set(wholeModel,TCollection_ExtendedString(modelName.c_str(),true));std::map<std::string,std::pair<double,Quantity_Color>> expectedFaces;std::map<std::string,int> expectedMarkers;std::map<std::string,bool> expectedValidity;std::map<std::string,std::array<double,6>> expectedBounds;std::map<std::string,std::string> expectedOwners;std::map<std::string,Quantity_Color> expectedMarkerColors;std::vector<DisplayFallback> fallbacks;std::vector<DisplayMarkerStyle> markerStyles;
    for(const auto& unit:units){std::cerr<<"INSPECTION UNIT "<<unit.name<<std::endl;auto parent=shapes->NewShape();TDataStd_Name::Set(parent,unit.name.c_str());Quantity_Color color(unit.r,unit.g,unit.b,Quantity_TOC_sRGB);const auto scale=displayScale(shape,unit,faces,edges,modelDiagonalMm);
        for(int fid:unit.faces){if(fid<1||fid>faces.Extent())throw std::runtime_error("invalid-display-face-id");auto name=faceName(fid);if(expectedFaces.count(name))throw std::runtime_error("repeated-display-face");auto leaf=shapes->AddShape(independent(faces(fid)),false);TDataStd_Name::Set(leaf,name.c_str());colors->SetColor(leaf,color,XCAFDoc_ColorSurf);auto instance=shapes->AddComponent(parent,leaf,TopLoc_Location());TDataStd_Name::Set(instance,name.c_str());expectedFaces.emplace(name,std::make_pair(area(faces(fid)),color));expectedValidity[name]=BRepCheck_Analyzer(faces(fid)).IsValid();std::cerr<<"INSPECTION BOUNDS "<<name<<std::endl;expectedBounds[name]=exactBounds(faces(fid));expectedOwners[name]=unit.name;}
        for(const auto& boundary:unit.boundaries){for(auto [eid,rev]:boundary.edges)if(eid<1||eid>edges.Extent())throw std::runtime_error("invalid-display-edge-id");int segments;auto marker=boundaryDisplay(boundary,edges,segments,unit.name,fallbacks,scale);const auto style=boundaryStyle(boundary.role,displayBoundaryLength(boundary,edges),scale);markerStyles.push_back({unit.name+"/DISPLAY_"+boundary.name+"_"+boundary.role,style.radiusMm,style.dashMm,style.samplingDeflectionMm,scale.localWidthMm});auto name="DISPLAY_"+boundary.name+"_"+boundary.role;auto leaf=shapes->AddShape(marker,false);TDataStd_Name::Set(leaf,name.c_str());auto color=roleColor(boundary.role);colors->SetColor(leaf,color,XCAFDoc_ColorSurf);colors->SetColor(leaf,color,XCAFDoc_ColorCurv);colors->SetColor(leaf,color,XCAFDoc_ColorGen);auto instance=shapes->AddComponent(parent,leaf,TopLoc_Location());TDataStd_Name::Set(instance,name.c_str());expectedMarkers.emplace(unit.name+"/"+name,segments);expectedMarkerColors.emplace(unit.name+"/"+name,color);}
        auto member=shapes->AddComponent(wholeModel,parent,TopLoc_Location());TDataStd_Name::Set(member,unit.name.c_str());
    }
    if(expectedFaces.size()!=faces.Extent())throw std::runtime_error("display-source-face-coverage-changed");shapes->UpdateAssemblies();
    STEPCAFControl_Writer writer;writer.SetColorMode(true);writer.SetNameMode(true);auto file=folder/"inspection.step";if(!writer.Transfer(doc,STEPControl_AsIs)||writer.Write(utf8(file).c_str())!=IFSelect_RetDone)throw std::runtime_error("inspection-write-failed");
    Handle(TDocStd_Document) restored=new TDocStd_Document("BinXCAF");STEPCAFControl_Reader reader;reader.SetColorMode(true);reader.SetNameMode(true);if(reader.ReadFile(utf8(file).c_str())!=IFSelect_RetDone||!reader.Transfer(restored))throw std::runtime_error("inspection-readback-failed");
    auto readShapes=XCAFDoc_DocumentTool::ShapeTool(restored->Main());auto readColors=XCAFDoc_DocumentTool::ColorTool(restored->Main());TDF_LabelSequence modelRoots,roots;readShapes->GetFreeShapes(modelRoots);if(modelRoots.Length()!=1)throw std::runtime_error("inspection-model-root-count-changed");Bnd_Box rootBox;BRepBndLib::Add(readShapes->GetShape(modelRoots.Value(1)),rootBox);double bx,by,bz,bX,bY,bZ;rootBox.Get(bx,by,bz,bX,bY,bZ);std::cout<<"Whole model bounds: "<<bx<<","<<by<<","<<bz<<" -> "<<bX<<","<<bY<<","<<bZ<<std::endl;readShapes->GetComponents(modelRoots.Value(1),roots,false);if(roots.Length()!=unitCount)throw std::runtime_error("inspection-unit-group-count-changed");
    std::map<std::string,int> foundFaces,foundMarkers;double maximumAreaDelta=0,maximumBoundsDelta=0;int faceCount=0;std::ofstream audit(folder/"inspection-audit.json");audit<<std::setprecision(15)<<"{\"modelRoots\":1,\"unitGroups\":"<<unitCount<<",\"groups\":[";
    for(int i=1;i<=roots.Length();++i){auto root=roots.Value(i);TDF_Label unitDefinition;if(readShapes->GetReferredShape(root,unitDefinition))root=unitDefinition;Handle(TDataStd_Name) groupName;root.FindAttribute(TDataStd_Name::GetID(),groupName);std::string name=groupName.IsNull()?"":TCollection_AsciiString(groupName->Get()).ToCString();auto expectedUnit=std::find_if(units.begin(),units.end(),[&](const auto& unit){return unit.name==name;});if(expectedUnit==units.end())throw std::runtime_error("inspection-unit-name-changed");TDF_LabelSequence children;readShapes->GetComponents(root,children,false);if(children.Length()!=expectedUnit->faces.size()+expectedUnit->boundaries.size())throw std::runtime_error("inspection-unit-members-changed");if(i>1)audit<<',';audit<<"{\"unit\":"<<quoted(name)<<",\"children\":[";
        for(int j=1;j<=children.Length();++j){auto child=children.Value(j);TDF_Label referred;if(readShapes->GetReferredShape(child,referred))child=referred;Handle(TDataStd_Name) childName;child.FindAttribute(TDataStd_Name::GetID(),childName);std::string key=childName.IsNull()?"":TCollection_AsciiString(childName->Get()).ToCString();auto shape=readShapes->GetShape(children.Value(j));shape.Move(readShapes->GetLocation(modelRoots.Value(1))*readShapes->GetLocation(roots.Value(i)));TopTools_IndexedMapOfShape ff,ee,colorFaces,colorEdges;TopExp::MapShapes(readShapes->GetShape(child),TopAbs_FACE,colorFaces);TopExp::MapShapes(readShapes->GetShape(child),TopAbs_EDGE,colorEdges);TopExp::MapShapes(shape,TopAbs_FACE,ff);TopExp::MapShapes(shape,TopAbs_EDGE,ee);
            if(expectedFaces.count(key)){if(expectedOwners.at(key)!=name)throw std::runtime_error("inspection-original-unit-membership-changed");const auto actualBounds=exactBounds(shape);for(size_t k=0;k<actualBounds.size();++k){const double delta=std::abs(actualBounds[k]-expectedBounds.at(key)[k]);maximumBoundsDelta=std::max(maximumBoundsDelta,delta);if(delta>1e-5)throw std::runtime_error("inspection-original-world-bounds-changed: "+key);}const auto& expected=expectedFaces.at(key);if(ff.Extent()!=1||BRepCheck_Analyzer(shape).IsValid()!=expectedValidity.at(key))throw std::runtime_error("inspection-original-face-invalid");double delta=std::abs(area(shape)-expected.first);maximumAreaDelta=std::max(maximumAreaDelta,delta);if(delta>std::max(1e-6,expected.first*1e-7))throw std::runtime_error("inspection-original-area-changed");Quantity_Color color;bool hasColor=readColors->GetColor(child,XCAFDoc_ColorSurf,color)||readColors->GetColor(children.Value(j),XCAFDoc_ColorSurf,color)||readColors->GetColor(colorFaces(1),XCAFDoc_ColorSurf,color)||readColors->GetColor(child,XCAFDoc_ColorGen,color)||readColors->GetColor(children.Value(j),XCAFDoc_ColorGen,color);if(!hasColor||color.Distance(expected.second)>1e-5){std::cerr<<key<<" color="<<hasColor<<" distance="<<color.Distance(expected.second)<<" ";throw std::runtime_error("inspection-original-color-changed");}++foundFaces[key];++faceCount;
            }else if(expectedMarkers.count(name+"/"+key)){if(ee.IsEmpty())throw std::runtime_error("inspection-marker-missing");Quantity_Color markerColor;bool hasMarkerColor=false;const auto& expectedColor=expectedMarkerColors.at(name+"/"+key);TopTools_IndexedMapOfShape styledShapes;TopExp::MapShapes(readShapes->GetShape(child),styledShapes);for(int k=1;k<=styledShapes.Extent()&&!hasMarkerColor;++k)for(const auto type:{XCAFDoc_ColorGen,XCAFDoc_ColorSurf,XCAFDoc_ColorCurv})if(readColors->GetColor(styledShapes(k),type,markerColor)&&markerColor.Distance(expectedColor)<=1e-5){hasMarkerColor=true;break;}if(!hasMarkerColor)throw std::runtime_error("inspection-marker-role-color-changed: "+name+"/"+key);++foundMarkers[name+"/"+key];}else throw std::runtime_error("unexpected-inspection-child");
            if(j>1)audit<<',';audit<<"{\"name\":"<<quoted(key)<<",\"originalFace\":"<<(expectedFaces.count(key)?"true":"false")<<",\"faces\":"<<ff.Extent()<<",\"edges\":"<<ee.Extent();Bnd_Box box;BRepBndLib::Add(shape,box);if(!box.IsVoid()){double a,b,c,d,e,f;box.Get(a,b,c,d,e,f);audit<<",\"bounds\":["<<a<<','<<b<<','<<c<<','<<d<<','<<e<<','<<f<<']';}audit<<'}';}
        audit<<"]}";
    }
    for(const auto& [key,value]:expectedFaces)if(foundFaces[key]!=1)throw std::runtime_error("inspection-face-coverage-changed");for(const auto& [key,value]:expectedMarkers)if(foundMarkers[key]!=1)throw std::runtime_error("inspection-marker-coverage-changed");
    audit<<"],\"originalFaces\":"<<faceCount<<",\"originalMaxAreaDeltaMm2\":"<<maximumAreaDelta<<",\"originalMaxBoundsDeltaMm\":"<<maximumBoundsDelta<<",\"markerGroups\":"<<expectedMarkers.size()<<",\"markers\":[";bool comma=false;for(const auto& [name,count]:expectedMarkers){if(comma)audit<<',';comma=true;audit<<"{\"name\":"<<quoted(name)<<",\"strokePieces\":"<<count<<'}';}audit<<"],\"displayStrokeFallbackCount\":"<<fallbacks.size()<<",\"displayStrokeFallbacks\":[";for(size_t i=0;i<fallbacks.size();++i){const auto& event=fallbacks[i];if(i)audit<<',';audit<<"{\"boundary\":"<<quoted(event.boundary)<<",\"sourceEdge\":"<<event.sourceEdge<<",\"reason\":"<<quoted(event.reason)<<",\"method\":\"SampledDisplayTubeWithExactSourceCurve\",\"displayDeflectionRequestMm\":"<<event.requestedDeflection<<",\"observedMidpointDeflectionMm\":"<<event.sampledDeflection<<",\"displayTubePieces\":"<<event.segments<<'}';}audit<<"],\"displayOnlyGeometry\":true,\"displayScalePolicy\":\"ModelDiagonalAndFaceWidthAndBoundaryLengthV1\",\"modelDiagonalMm\":"<<modelDiagonalMm<<",\"markerStyles\":[";for(size_t i=0;i<markerStyles.size();++i){const auto& style=markerStyles[i];if(i)audit<<',';audit<<"{\"name\":"<<quoted(style.name)<<",\"radiusMm\":"<<style.radiusMm<<",\"dashMm\":"<<style.dashMm<<",\"samplingDeflectionMm\":"<<style.samplingDeflectionMm<<",\"localFaceWidthMm\":"<<style.localWidthMm<<'}';}audit<<"],\"passed\":true}";
    return 0;
}catch(const Standard_Failure& error){std::cerr<<error.GetMessageString();return 1;}catch(const std::exception& error){std::cerr<<error.what();return 1;}}
