#define wmain probe_main
#include "face_boundary_probe.cpp"
#undef wmain

void roleSegmentTests(){
    // The cap follows the stock skin on both ends of one straight primitive,
    // while its middle crosses material below the raised section boundary.
    Profile profile;BRepBuilderAPI_MakePolygon outline;
    for(const auto p:std::vector<gp_Pnt>{{35,-5,0},{35,5,0},{35,5,10},{35,2,10},
        {35,2,12},{35,-2,12},{35,-2,10},{35,-5,10}})outline.Add(p);
    outline.Close();for(TopExp_Explorer e(outline.Wire(),TopAbs_EDGE);e.More();e.Next())
        profile.edges.push_back(TopoDS::Edge(e.Current()));
    profile.prepare();
    const auto makeUse=[](double a,double b,int id){return readUse(
        BRepBuilderAPI_MakeEdge(gp_Pnt(10,a,10),gp_Pnt(10,b,10)).Edge(),id);};
    std::vector<EdgeUse> uses{makeUse(-5,-4,101),makeUse(-4,-2,102),makeUse(-2,2,103),makeUse(2,5,104)};
    auto groups=groupByContinuity(uses);
    if(groups.size()!=1||groups[0].uses!=std::vector<int>{0,1,2,3})
        throw std::runtime_error("role-segments-changed-geometric-grouping");
    auto& parent=groups[0];classify(parent,uses,profile);
    if(parent.kind!="Unresolved"||parent.reason!="mixed-skin-contact"||parent.roleSegments.size()!=3)
        throw std::runtime_error("mixed-complete-source-uses-not-segmented");
    const auto& parts=parent.roleSegments;
    if(parts[0].kind!="Skin"||parts[1].kind!="Side"||parts[2].kind!="Skin"
       ||parts[0].uses!=std::vector<int>{0,1}||parts[1].uses!=std::vector<int>{2}
       ||parts[2].uses!=std::vector<int>{3})throw std::runtime_error("role-segment-roles-or-source-partition-wrong");
    std::vector<int> sourcePartition;int sampleCount=0;
    for(const auto& part:parts){
        sourcePartition.insert(sourcePartition.end(),part.uses.begin(),part.uses.end());sampleCount+=part.samples;
        if(!part.roleSegments.empty()||part.seam||part.event||part.closed||!part.reason.empty())
            throw std::runtime_error("invalid-role-segment-contract");
    }
    if(sourcePartition!=parent.uses||sampleCount!=parent.samples||parts[0].samples!=2*(sampleIntervals+1)
       ||parts[1].bodyInside!=sampleIntervals-1||parts[1].bodyOutside)
        throw std::runtime_error("role-segments-do-not-conserve-source-evidence");
    // One native edge contains both contacts: report unresolved instead of
    // fabricating source parameters from a sampled contact transition.
    std::vector<EdgeUse> single{makeUse(-5,5,201)};Group unresolved;unresolved.uses={0};classify(unresolved,single,profile);
    if(unresolved.kind!="Unresolved"||unresolved.reason!="mixed-skin-contact"||!unresolved.roleSegments.empty())
        throw std::runtime_error("single-topological-edge-mixed-contact-was-invented-as-exact-split");
    std::vector<EdgeUse> partial{makeUse(-5,-4,301),makeUse(-4,5,302)};Group incomplete;incomplete.uses={0,1};classify(incomplete,partial,profile);
    if(incomplete.kind!="Unresolved"||incomplete.reason!="mixed-skin-contact"||!incomplete.roleSegments.empty())
        throw std::runtime_error("partial-source-use-segmentation-lost-unresolved-evidence");
}

int wmain(int argc,wchar_t** argv){try{
    roleSegmentTests();
    auto line=readUse(BRepBuilderAPI_MakeEdge(gp_Pnt(-2,0,0),gp_Pnt(0,0,0)).Edge(),1);
    auto circle=gp_Circ(gp_Ax2(gp_Pnt(0,1,0),gp_Dir(0,0,1),gp_Dir(0,-1,0)),1);
    auto arc1=readUse(BRepBuilderAPI_MakeEdge(circle,0,std::acos(-1.)/4).Edge(),2),arc2=readUse(BRepBuilderAPI_MakeEdge(circle,std::acos(-1.)/4,std::acos(-1.)/2).Edge(),3);
    if(continuous(line,arc1)||!continuous(arc1,arc2)||groupByContinuity({line,arc1,arc2}).size()!=2)throw std::runtime_error("tangent-line-arc-not-separated-or-arc-fragments-not-merged");
    auto line2=readUse(BRepBuilderAPI_MakeEdge(gp_Pnt(0,0,0),gp_Pnt(2,0,0)).Edge(),4);if(!continuous(line,line2))throw std::runtime_error("collinear-fragments-not-merged");
    auto arc0=readUse(BRepBuilderAPI_MakeEdge(circle,-std::acos(-1.)/4,0).Edge(),5);auto other=gp_Circ(gp_Ax2(gp_Pnt(0,2,0),gp_Dir(0,0,1),gp_Dir(0,-1,0)),2);auto otherArc=readUse(BRepBuilderAPI_MakeEdge(other,0,std::acos(-1.)/4).Edge(),6);if(continuous(arc0,otherArc))throw std::runtime_error("different-tangent-circles-merged");
    if(argc!=4)throw std::runtime_error("usage: logical_grouping_tests source.step face-id output.json");STEPControl_Reader reader;if(reader.ReadFile(utf8(fs::path(argv[1])).c_str())!=IFSelect_RetDone)throw std::runtime_error("read-failed");reader.TransferRoots();TopTools_IndexedMapOfShape faces,edges;TopExp::MapShapes(reader.OneShape(),TopAbs_FACE,faces);TopExp::MapShapes(reader.OneShape(),TopAbs_EDGE,edges);auto face=TopoDS::Face(faces(std::stoi(argv[2])));std::vector<EdgeUse> uses;auto wire=BRepTools::OuterWire(face);for(BRepTools_WireExplorer e(wire,face);e.More();e.Next())uses.push_back(readUse(e.Current(),edges.FindIndex(e.Current())));
    auto groups=groupByContinuity(uses);int lines=0,arcs=0,covered=0;for(const auto& g:groups){bool lineLike=uses[g.uses.front()].lineLike;for(int id:g.uses)if(uses[id].lineLike!=lineLike)throw std::runtime_error("mixed-line-curve-group");covered+=int(g.uses.size());if(lineLike)++lines;else{++arcs;if(g.uses.size()!=8)throw std::runtime_error("F2-arc-fragments-not-preserved");}}
    if(uses.size()!=44||groups.size()!=16||lines!=12||arcs!=4||covered!=44)throw std::runtime_error("F2-logical-group-count-wrong");std::ofstream o(argv[3]);o<<"{\"sourceEdges\":44,\"logicalGroups\":16,\"lines\":12,\"arcs\":4,\"arcFragmentsEach\":8,\"tangentLineArcSeparated\":true,\"collinearFragmentsMerged\":true,\"sameCircleFragmentsMerged\":true,\"differentTangentCirclesSeparated\":true,\"geometricParentRetainedForMixedRoles\":true,\"completeSourceUseRolePartitionPassed\":true,\"adjacentSkinUsesCoalesced\":true,\"singleTopoEdgeMixedContactRetained\":true,\"partialUnknownUseRetained\":true}";
    std::cout<<"grouping tests passed: F2 44 edges -> 12 lines + 4 arcs; tangent line/arc remain separate\n";return 0;
}catch(const Standard_Failure& e){std::cerr<<e.GetMessageString();return 1;}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
