#include "pch.h"

#include <Data/Variant.h>
#include <OpenCascadeResourceImport/OpenCascadeTaskExecution.h>
#include <Task/Task.h>

#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepBndLib.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <map>
#include <stdexcept>
#include <set>
#include <string>
#include <vector>

namespace
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::VariantArray;

#include <TubeDesigner/ProductAssemblyPairGeometry.inc>
#include <TubeDesigner/ProductAssemblyWholeGeometry.inc>
#include <TubeDesigner/ProductAssemblyFinalNodeGeometry.inc>

    VariantArray TransformMatrix(const gp_Trsf& Transform_)
    {
        return VariantArray{
            Transform_.Value(1,1),Transform_.Value(1,2),Transform_.Value(1,3),Transform_.Value(1,4),
            Transform_.Value(2,1),Transform_.Value(2,2),Transform_.Value(2,3),Transform_.Value(2,4),
            Transform_.Value(3,1),Transform_.Value(3,2),Transform_.Value(3,3),Transform_.Value(3,4),
            0.0,0.0,0.0,1.0,
        };
    }

    ObjectMap VerifiedMapping(const gp_Trsf& PartToSource_)
    {
        return ObjectMap{{"status",std::string("verified")},
            {"verification",std::string("normalized-source-brep-replay")},
            {"partToSource",TransformMatrix(PartToSource_)},
            {"sourceToPart",TransformMatrix(PartToSource_.Inverted())}};
    }

    std::string Status(const ObjectMap& Measurement_)
    {
        return Measurement_.at("status").To<std::string>();
    }
}

TEST(ProductAssemblyPairGeometryTests, MeasuresLocalIntersectionAfterRigidPlacement)
{
    const auto _PartA = BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),10,10,10).Shape();
    const auto _PartB = BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),10,10,10).Shape();
    gp_Trsf _MoveA;
    _MoveA.SetTranslation(gp_Vec(50,0,0));
    gp_Trsf _RotateB;
    _RotateB.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,0,1)),
        1.57079632679489661923);
    gp_Trsf _MoveB;
    _MoveB.SetTranslation(gp_Vec(55,0,0));
    const auto _Measurement = MeasureAssemblyPairNearNode(_PartA,VerifiedMapping(_MoveA),
        _PartB,VerifiedMapping(_MoveB.Multiplied(_RotateB)),gp_Pnt(52.5,5,5),20);

    ASSERT_EQ(Status(_Measurement),"measured");
    EXPECT_NEAR(_Measurement.at("intersectionVolumeMm3").To<double>(),500.0,1.0e-5);
    EXPECT_NEAR(_Measurement.at("nearestDistanceMm").To<double>(),0.0,1.0e-7);
    EXPECT_FALSE(_Measurement.at("interfaceClearanceCertified").To<bool>());
}

TEST(ProductAssemblyPairGeometryTests, MeasuresLocalSeparationWithoutCertifyingClearance)
{
    const auto _PartA = BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),10,10,10).Shape();
    const auto _PartB = BRepPrimAPI_MakeBox(gp_Pnt(14,0,0),10,10,10).Shape();
    const gp_Trsf _Identity;
    const auto _Mapping = VerifiedMapping(_Identity);
    const auto _Measurement = MeasureAssemblyPairNearNode(_PartA,_Mapping,
        _PartB,_Mapping,gp_Pnt(12,5,5),15);

    ASSERT_EQ(Status(_Measurement),"measured");
    EXPECT_NEAR(_Measurement.at("intersectionVolumeMm3").To<double>(),0.0,1.0e-7);
    EXPECT_NEAR(_Measurement.at("nearestDistanceMm").To<double>(),4.0,1.0e-6);
    EXPECT_FALSE(_Measurement.at("interfaceClearanceCertified").To<bool>());
}

TEST(ProductAssemblyPairGeometryTests, RejectsNodeWindowMissingEitherPart)
{
    const auto _PartA = BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),10,10,10).Shape();
    const auto _PartB = BRepPrimAPI_MakeBox(gp_Pnt(100,0,0),10,10,10).Shape();
    const gp_Trsf _Identity;
    const auto _Mapping = VerifiedMapping(_Identity);
    const auto _Measurement = MeasureAssemblyPairNearNode(_PartA,_Mapping,
        _PartB,_Mapping,gp_Pnt(5,5,5),10);

    EXPECT_EQ(Status(_Measurement),"unsupported");
    EXPECT_FALSE(_Measurement.contains("intersectionVolumeMm3"));
    EXPECT_FALSE(_Measurement.contains("nearestDistanceMm"));
}

TEST(ProductAssemblyPairGeometryTests, RejectsBadOrUnverifiedMatrices)
{
    const auto _Part = BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),10,10,10).Shape();
    const gp_Trsf _Identity;
    const auto _Good = VerifiedMapping(_Identity);

    auto _Scaled = _Good;
    auto _Matrix = _Scaled.at("partToSource").To<VariantArray>();
    _Matrix[0] = 2.0;
    _Scaled["partToSource"] = _Matrix;
    EXPECT_EQ(Status(MeasureAssemblyPairNearNode(_Part,_Scaled,
        _Part,_Good,gp_Pnt(5,5,5),10)),"unsupported");

    auto _Unverified = _Good;
    _Unverified["status"] = std::string("unsupported");
    EXPECT_EQ(Status(MeasureAssemblyPairNearNode(_Part,_Unverified,
        _Part,_Good,gp_Pnt(5,5,5),10)),"unsupported");
}

TEST(ProductAssemblyWholeGeometryTests, ChecksEveryNonNeighborPairAfterRigidPlacement)
{
    const auto _Solid = BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),10,10,10).Shape();
    gp_Trsf _PlaceA, _PlaceB, _PlaceC;
    _PlaceB.SetTranslation(gp_Vec(8,0,0)); // Deliberate neighboring overlap is excluded.
    _PlaceC.SetTranslation(gp_Vec(30,0,0));
    const std::vector<SAssemblyWholeAuditPart> _Parts{
        {"A",_Solid,VerifiedMapping(_PlaceA)},
        {"B",_Solid,VerifiedMapping(_PlaceB)},
        {"C",_Solid,VerifiedMapping(_PlaceC)},
    };
    const auto _Result = AuditAssemblyFinishedProductSolids(_Parts,{{"A","B"}});

    ASSERT_EQ(Status(_Result),"measured");
    EXPECT_EQ(_Result.at("partCount").To<std::uint64_t>(),3u);
    EXPECT_EQ(_Result.at("checkedNonNeighborPairCount").To<std::uint64_t>(),2u);
    EXPECT_EQ(_Result.at("excludedAdjacentPairCount").To<std::uint64_t>(),1u);
    EXPECT_TRUE(_Result.at("conflicts").To<VariantArray>().empty());
    EXPECT_FALSE(_Result.at("interfaceFitCertified").To<bool>());
}

TEST(ProductAssemblyWholeGeometryTests, ReportsNonNeighborSolidPenetration)
{
    const auto _Solid = BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),10,10,10).Shape();
    gp_Trsf _PlaceA, _PlaceB, _PlaceC;
    _PlaceB.SetTranslation(gp_Vec(30,0,0));
    _PlaceC.SetTranslation(gp_Vec(5,0,0));
    const auto _Result = AuditAssemblyFinishedProductSolids({
        {"A",_Solid,VerifiedMapping(_PlaceA)},
        {"B",_Solid,VerifiedMapping(_PlaceB)},
        {"C",_Solid,VerifiedMapping(_PlaceC)},
    },{{"A","B"},{"B","C"}});

    ASSERT_EQ(Status(_Result),"conflict");
    const auto _Conflicts = _Result.at("conflicts").To<VariantArray>();
    ASSERT_EQ(_Conflicts.size(),1u);
    const auto _Pair = _Conflicts.front().To<ObjectMap>();
    EXPECT_EQ(_Pair.at("firstItemKey").To<std::string>(),"A");
    EXPECT_EQ(_Pair.at("secondItemKey").To<std::string>(),"C");
    EXPECT_NEAR(_Pair.at("intersectionVolumeMm3").To<double>(),500.0,1.0e-5);
    EXPECT_FALSE(_Result.at("interfaceFitCertified").To<bool>());
}

TEST(ProductAssemblyWholeGeometryTests, RejectsUnverifiedMappingAndDisconnectedBlank)
{
    const auto _Solid = BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),10,10,10).Shape();
    const gp_Trsf _Identity;
    auto _BadMapping = VerifiedMapping(_Identity);
    _BadMapping["status"] = std::string("unsupported");
    EXPECT_EQ(Status(AuditAssemblyFinishedProductSolids(
        {{"A",_Solid,_BadMapping}},{})),"unsupported");

    BRep_Builder _Builder;
    TopoDS_Compound _Compound;
    _Builder.MakeCompound(_Compound);
    _Builder.Add(_Compound,_Solid);
    _Builder.Add(_Compound,BRepPrimAPI_MakeBox(gp_Pnt(20,0,0),10,10,10).Shape());
    EXPECT_EQ(Status(AuditAssemblyFinishedProductSolids(
        {{"A",_Compound,VerifiedMapping(_Identity)}},{})),"unsupported");
}

TEST(ProductAssemblyWholeGeometryTests, RejectsIncompleteOrDuplicateIdentity)
{
    const auto _Solid = BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),10,10,10).Shape();
    const gp_Trsf _Identity;
    const std::vector<SAssemblyWholeAuditPart> _Parts{
        {"A",_Solid,VerifiedMapping(_Identity)},
        {"B",_Solid,VerifiedMapping(_Identity)},
    };
    EXPECT_EQ(Status(AuditAssemblyFinishedProductSolids(_Parts,{{"A","C"}})),"unsupported");
    EXPECT_EQ(Status(AuditAssemblyFinishedProductSolids(
        {{"A",_Solid,VerifiedMapping(_Identity)},
         {"A",_Solid,VerifiedMapping(_Identity)}},{})),"unsupported");
}

TEST(ProductAssemblyWholeGeometryTests, BuildsAdjacencyOnlyFromCommittedAuditedMembers)
{
    const VariantArray _Connections{
        ObjectMap{{"participants",VariantArray{
            ObjectMap{{"itemKey",std::string("frame.left")}},
            ObjectMap{{"itemKey",std::string("frame.top")}},
            ObjectMap{{"itemKey",std::string("glass")}},
        }}},
        ObjectMap{{"participants",VariantArray{
            ObjectMap{{"itemKey",std::string("frame.left")}},
            ObjectMap{{"itemKey",std::string("bar.middle")}},
        }}},
    };
    const auto _Pairs = AssemblyWholeAdjacentPairs(_Connections,
        {"frame.left","frame.top","bar.middle"});
    EXPECT_EQ(_Pairs.size(),2u);
    EXPECT_TRUE(_Pairs.contains({"frame.left","frame.top"}));
    EXPECT_TRUE(_Pairs.contains({"bar.middle","frame.left"}));
    EXPECT_FALSE(_Pairs.contains({"frame.top","glass"}));
}

TEST(ProductAssemblyFinalNodeGeometryTests, RechecksAllSixFrameConnectionsIndependentlyOfOrder)
{
    const gp_Trsf _Identity;
    const auto _Mapping = VerifiedMapping(_Identity);
    const std::vector<SAssemblyWholeAuditPart> _Parts{
        {"bottom",BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),100,10,10).Shape(),_Mapping},
        {"top",BRepPrimAPI_MakeBox(gp_Pnt(0,90,0),100,10,10).Shape(),_Mapping},
        {"left",BRepPrimAPI_MakeBox(gp_Pnt(0,10,0),10,80,10).Shape(),_Mapping},
        {"right",BRepPrimAPI_MakeBox(gp_Pnt(90,10,0),10,80,10).Shape(),_Mapping},
        {"middle",BRepPrimAPI_MakeBox(gp_Pnt(10,45,0),80,10,10).Shape(),_Mapping},
    };
    std::vector<SAssemblyFinalNodeAuditNode> _Nodes{
        {"corner.bottom-left","bottom","left",gp_Pnt(5,10,5),15},
        {"corner.bottom-right","bottom","right",gp_Pnt(95,10,5),15},
        {"corner.top-left","top","left",gp_Pnt(5,90,5),15},
        {"corner.top-right","top","right",gp_Pnt(95,90,5),15},
        {"junction.middle-left","left","middle",gp_Pnt(10,50,5),15},
        {"junction.middle-right","right","middle",gp_Pnt(90,50,5),15},
    };
    const auto _Forward = AuditAssemblyFinalNodes(_Parts,_Nodes);
    ASSERT_EQ(Status(_Forward),"measured");
    EXPECT_EQ(_Forward.at("nodeCount").To<std::uint64_t>(),6u);
    EXPECT_EQ(_Forward.at("measuredNodeCount").To<std::uint64_t>(),6u);
    EXPECT_EQ(_Forward.at("conflictNodeCount").To<std::uint64_t>(),0u);
    EXPECT_FALSE(_Forward.at("interfaceFitCertified").To<bool>());
    for (const auto& _Value : _Forward.at("nodes").To<VariantArray>()) {
        const auto _Row = _Value.To<ObjectMap>();
        EXPECT_EQ(Status(_Row),"measured");
        EXPECT_NEAR(_Row.at("intersectionVolumeMm3").To<double>(),0.0,1.0e-5);
    }
    std::reverse(_Nodes.begin(),_Nodes.end());
    const auto _Reverse = AuditAssemblyFinalNodes(_Parts,_Nodes);
    ASSERT_EQ(Status(_Reverse),"measured");
    EXPECT_EQ(_Reverse.at("measuredNodeCount").To<std::uint64_t>(),6u);
    std::map<std::string,double> _Distances;
    for (const auto& _Value : _Forward.at("nodes").To<VariantArray>()) {
        const auto _Row = _Value.To<ObjectMap>();
        _Distances.emplace(_Row.at("connectionKey").To<std::string>(),
            _Row.at("nearestDistanceMm").To<double>());
    }
    for (const auto& _Value : _Reverse.at("nodes").To<VariantArray>()) {
        const auto _Row = _Value.To<ObjectMap>();
        const auto _Key = _Row.at("connectionKey").To<std::string>();
        EXPECT_NEAR(_Row.at("nearestDistanceMm").To<double>(),_Distances.at(_Key),1.0e-6);
    }
}

TEST(ProductAssemblyFinalNodeGeometryTests, LaterCutChangesEarlierNodeMeasurement)
{
    const gp_Trsf _Identity;
    const auto _Mapping = VerifiedMapping(_Identity);
    const auto _Host = BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),10,10,10).Shape();
    const auto _Branch = BRepPrimAPI_MakeBox(gp_Pnt(0,10,0),10,40,10).Shape();
    const std::vector<SAssemblyFinalNodeAuditNode> _Node{
        {"earlier-corner","host","branch",gp_Pnt(5,10,5),15}};
    const auto _Before = AuditAssemblyFinalNodes({
        {"host",_Host,_Mapping},{"branch",_Branch,_Mapping}},_Node);
    ASSERT_EQ(Status(_Before),"measured");
    const auto _BeforeNode = _Before.at("nodes").To<VariantArray>().front().To<ObjectMap>();
    EXPECT_NEAR(_BeforeNode.at("nearestDistanceMm").To<double>(),0.0,1.0e-6);

    // A later operation on the shared branch removes stock next to the
    // already-selected corner. The final audit must use this cumulative BRep.
    BRepAlgoAPI_Cut _Cut(_Branch,
        BRepPrimAPI_MakeBox(gp_Pnt(0,10,0),10,2,10).Shape());
    _Cut.Build();
    ASSERT_TRUE(_Cut.IsDone());
    const auto _After = AuditAssemblyFinalNodes({
        {"host",_Host,_Mapping},{"branch",_Cut.Shape(),_Mapping}},_Node);
    ASSERT_EQ(Status(_After),"measured");
    const auto _AfterNode = _After.at("nodes").To<VariantArray>().front().To<ObjectMap>();
    EXPECT_NEAR(_AfterNode.at("nearestDistanceMm").To<double>(),2.0,1.0e-6);
    EXPECT_LT(_AfterNode.at("localMaterialVolumeSecondMm3").To<double>(),
        _BeforeNode.at("localMaterialVolumeSecondMm3").To<double>());
    EXPECT_FALSE(_After.at("interfaceFitCertified").To<bool>());

    BRepAlgoAPI_Cut _DeepCut(_Branch,
        BRepPrimAPI_MakeBox(gp_Pnt(0,10,0),10,20,10).Shape());
    _DeepCut.Build();
    ASSERT_TRUE(_DeepCut.IsDone());
    const auto _Lost = AuditAssemblyFinalNodes({
        {"host",_Host,_Mapping},{"branch",_DeepCut.Shape(),_Mapping}},_Node);
    ASSERT_EQ(Status(_Lost),"unsupported");
    EXPECT_EQ(_Lost.at("unsupportedNodeCount").To<std::uint64_t>(),1u);
}

TEST(ProductAssemblyFinalNodeGeometryTests, ReportsPenetrationAndFailsClosedOnBadMapping)
{
    const gp_Trsf _Identity;
    const auto _Good = VerifiedMapping(_Identity);
    auto _Bad = _Good;
    _Bad["verification"] = std::string("unverified");
    const auto _Host = BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),10,10,10).Shape();
    const auto _Branch = BRepPrimAPI_MakeBox(gp_Pnt(0,8,0),10,10,10).Shape();
    const std::vector<SAssemblyFinalNodeAuditNode> _Node{
        {"conflicted-corner","host","branch",gp_Pnt(5,9,5),15}};
    const auto _Conflict = AuditAssemblyFinalNodes({
        {"host",_Host,_Good},{"branch",_Branch,_Good}},_Node);
    ASSERT_EQ(Status(_Conflict),"conflict");
    const auto _ConflictNode = _Conflict.at("nodes").To<VariantArray>().front().To<ObjectMap>();
    EXPECT_EQ(Status(_ConflictNode),"conflict");
    EXPECT_GT(_ConflictNode.at("intersectionVolumeMm3").To<double>(),
        _ConflictNode.at("penetrationToleranceMm3").To<double>());

    const auto _Unsupported = AuditAssemblyFinalNodes({
        {"host",_Host,_Good},{"branch",_Branch,_Bad}},_Node);
    ASSERT_EQ(Status(_Unsupported),"unsupported");
    const auto _BadNode = _Unsupported.at("nodes").To<VariantArray>().front().To<ObjectMap>();
    EXPECT_EQ(Status(_BadNode),"unsupported");
    EXPECT_FALSE(_BadNode.contains("intersectionVolumeMm3"));
    EXPECT_FALSE(_Unsupported.at("interfaceFitCertified").To<bool>());
}

TEST(ProductAssemblyFinalNodeGeometryTests, SelectsOnlyBoundNodesAndRequiresEveryBoundIdentity)
{
    const auto _Connection = [](const std::string& Key_,const std::string& Status_) {
        return ObjectMap{{"key",Key_},
            {"nodeGeometry",ObjectMap{{"nodeGeometryStatus",Status_}}},
            {"properties",ObjectMap{{"centerlinePoint",VariantArray{5.0,10.0,5.0}}}},
            {"participants",VariantArray{
                ObjectMap{{"itemKey",std::string("host")}},
                ObjectMap{{"itemKey",std::string("branch")}}}}};
    };
    const VariantArray _Partial{
        _Connection("bound","verified"),
        _Connection("unbound","unverified")};
    const auto _Selected = AssemblyFinalNodeDeclarations(_Partial,{"bound"});
    ASSERT_EQ(_Selected.size(),1u);
    EXPECT_EQ(_Selected.front().ConnectionKey,"bound");
    EXPECT_THROW((void)AssemblyFinalNodeDeclarations(_Partial,{"bound","unbound"}),
        std::invalid_argument);
    EXPECT_THROW((void)AssemblyFinalNodeDeclarations(_Partial,{"missing"}),
        std::invalid_argument);

    const VariantArray _All{
        _Connection("bound","verified"),
        _Connection("other","verified")};
    const auto _Both = AssemblyFinalNodeDeclarations(_All,{"bound","other"});
    ASSERT_EQ(_Both.size(),2u);
    EXPECT_EQ(_Both[0].ConnectionKey,"bound");
    EXPECT_EQ(_Both[1].ConnectionKey,"other");
}

TEST(ProductAssemblyFinalNodeGeometryTests, ThreeMemberNodeChecksAllPairsIncludingThirdAgainstSecond)
{
    const auto mapping = VerifiedMapping(gp_Trsf{});
    const auto a = BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),10,10,10).Shape();
    const auto b = BRepPrimAPI_MakeBox(gp_Pnt(10,0,0),10,10,10).Shape();
    const auto c = BRepPrimAPI_MakeBox(gp_Pnt(10,10,0),10,10,10).Shape();
    const std::vector<SAssemblyFinalNodeAuditNode> node{
        {"corner","a","b",gp_Pnt(10,10,5),30,{"c"}}};
    const auto valid = AuditAssemblyFinalNodes({{"a",a,mapping},{"b",b,mapping},
        {"c",c,mapping}},node);
    ASSERT_EQ(Status(valid),"measured");
    const auto measured = valid.at("nodes").To<VariantArray>().front().To<ObjectMap>();
    ASSERT_EQ(measured.at("checkedPairCount").To<std::uint64_t>(),3u);
    ASSERT_EQ(measured.at("pairs").To<VariantArray>().size(),3u);
    const auto collidingC = BRepPrimAPI_MakeBox(gp_Pnt(11,8,0),8,10,10).Shape();
    const auto conflict = AuditAssemblyFinalNodes({{"a",a,mapping},{"b",b,mapping},
        {"c",collidingC,mapping}},node);
    ASSERT_EQ(Status(conflict),"conflict");
    const auto result = conflict.at("nodes").To<VariantArray>().front().To<ObjectMap>();
    ASSERT_EQ(result.at("checkedPairCount").To<std::uint64_t>(),3u);
    std::set<std::pair<std::string,std::string>> conflicts;
    for (const auto& value : result.at("pairs").To<VariantArray>()) {
        const auto pair = value.To<ObjectMap>();
        if (Status(pair) == "conflict") conflicts.emplace(
            pair.at("firstItemKey").To<std::string>(),pair.at("secondItemKey").To<std::string>());
    }
    EXPECT_EQ(conflicts,(std::set<std::pair<std::string,std::string>>{{"b","c"}}));
    auto badMapping = mapping;
    badMapping["verification"] = std::string("unverified");
    const auto invalid = AuditAssemblyFinalNodes({{"a",a,mapping},{"b",b,mapping},
        {"c",c,badMapping}},node);
    EXPECT_EQ(Status(invalid),"unsupported");
    EXPECT_FALSE(invalid.at("interfaceFitCertified").To<bool>());
    const std::vector<SAssemblyFinalNodeAuditNode> requiredContact{
        {"corner","a","b",gp_Pnt(10,10,5),30,{"c"},{{"b","c",0.05}}}};
    EXPECT_EQ(Status(AuditAssemblyFinalNodes({{"a",a,mapping},{"b",b,mapping},
        {"c",c,mapping}},requiredContact)),"measured");
    const auto separatedC = BRepPrimAPI_MakeBox(gp_Pnt(10,14,0),10,10,10).Shape();
    const auto separated = AuditAssemblyFinalNodes({{"a",a,mapping},{"b",b,mapping},
        {"c",separatedC,mapping}},requiredContact);
    ASSERT_EQ(Status(separated),"conflict");
    const auto separatedNode = separated.at("nodes").To<VariantArray>().front().To<ObjectMap>();
    EXPECT_EQ(separatedNode.at("checkedPairCount").To<std::uint64_t>(),3u);
    const auto failedContact = separatedNode.at("contacts").To<VariantArray>().front().To<ObjectMap>();
    EXPECT_NEAR(failedContact.at("nearestDistanceMm").To<double>(),4.0,1e-6);
    EXPECT_EQ(Status(failedContact),"conflict");
}

TEST(ProductAssemblyFinalNodeGeometryTests, ThreeMemberDeclarationsRequireDistinctCompleteCornerMembership)
{
    const ObjectMap corner{{"key",std::string("corner")},
        {"nodeGeometry",ObjectMap{{"nodeGeometryStatus",std::string("verified")}}},
        {"properties",ObjectMap{{"topology",std::string("orthogonal-corner")},
            {"centerlinePoint",VariantArray{0.0,0.0,0.0}}}},
        {"participants",VariantArray{ObjectMap{{"itemKey",std::string("a")}},
            ObjectMap{{"itemKey",std::string("b")}},ObjectMap{{"itemKey",std::string("c")}}}}};
    const auto declarations = AssemblyFinalNodeDeclarations({corner},{"corner"});
    ASSERT_EQ(declarations.size(),1u);
    EXPECT_EQ(declarations.front().AdditionalItemKeys,(std::vector<std::string>{"c"}));
    auto incomplete = corner;
    incomplete["participants"] = VariantArray{ObjectMap{{"itemKey",std::string("a")}},
        ObjectMap{{"itemKey",std::string("b")}}};
    EXPECT_THROW((void)AssemblyFinalNodeDeclarations({incomplete},{"corner"}),std::invalid_argument);
    auto duplicate = corner;
    duplicate["participants"] = VariantArray{ObjectMap{{"itemKey",std::string("a")}},
        ObjectMap{{"itemKey",std::string("b")}},ObjectMap{{"itemKey",std::string("a")}}};
    EXPECT_THROW((void)AssemblyFinalNodeDeclarations({duplicate},{"corner"}),std::invalid_argument);
    auto oldTopology = corner;
    auto properties = corner.at("properties").To<ObjectMap>();
    properties["topology"] = std::string("L");
    oldTopology["properties"] = std::move(properties);
    EXPECT_THROW((void)AssemblyFinalNodeDeclarations({oldTopology},{"corner"}),std::invalid_argument);
}

TEST(ProductAssemblyFinalNodeGeometryTests, AutomaticWindowUsesSectionNotBlankLength)
{
    const auto _Long = BRepPrimAPI_MakeBox(gp_Pnt(0,-5,-5),500,10,10).Shape();
    const auto _Short = BRepPrimAPI_MakeBox(gp_Pnt(0,-10,-8),80,20,16).Shape();
    EXPECT_NEAR(AssemblyFinalNodeLocalRadius(_Long,_Short),40.0,1.0e-6);
}
