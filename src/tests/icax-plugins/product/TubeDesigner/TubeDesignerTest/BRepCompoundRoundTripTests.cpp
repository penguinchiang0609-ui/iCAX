#include "pch.h"
#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <GeometryData/BRepPersistence.h>
#include <BRep_Builder.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS.hxx>
#include <TopExp.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

namespace brep_compound_round_trip {
using iCAX::GeometryData::BRepModel;
using iCAX::GeometryData::EBRepShapeKind;

TopoDS_Shape nestedBox(bool located = false, bool reversed = false) {
    BRep_Builder builder;
    TopoDS_Compound inner, middle, outer;
    builder.MakeCompound(inner);builder.MakeCompound(middle);builder.MakeCompound(outer);
    builder.Add(inner,BRepPrimAPI_MakeBox(10.,20.,30.).Shape());
    if(located) {
        gp_Trsf translation;translation.SetTranslation(gp_Vec(15.,-8.,4.));
        inner.Location(TopLoc_Location(translation));
    }
    builder.Add(middle,inner);builder.Add(outer,middle);
    if(located) {
        gp_Trsf translation;translation.SetTranslation(gp_Vec(-5.,12.,7.));
        outer.Location(TopLoc_Location(translation));
    }
    return reversed?outer.Reversed():outer;
}

int uniqueCount(const TopoDS_Shape& shape,TopAbs_ShapeEnum type) {
    NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> shapes;
    TopExp::MapShapes(shape,type,shapes);return shapes.Extent();
}

std::array<double,6> bounds(const TopoDS_Shape& shape) {
    Bnd_Box box;BRepBndLib::AddOptimal(shape,box,false,false);
    std::array<double,6> values{};
    box.Get(values[0],values[1],values[2],values[3],values[4],values[5]);return values;
}

double signedVolume(const TopoDS_Shape& shape) {
    GProp_GProps props;BRepGProp::VolumeProperties(shape,props);return props.Mass();
}

BRepModel convert(const TopoDS_Shape& shape) {
    return iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(shape,"Nested compound regression","compound-probe",0.001);
}

void assertSameGeometry(const TopoDS_Shape& original,const BRepModel& neutral) {
    const auto rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(neutral);
    ASSERT_TRUE(rebuilt.bOK);
    ASSERT_FALSE(rebuilt.Shape.IsNull());
    EXPECT_EQ(uniqueCount(rebuilt.Shape,TopAbs_SOLID),1);
    EXPECT_EQ(uniqueCount(rebuilt.Shape,TopAbs_FACE),6);
    EXPECT_EQ(uniqueCount(rebuilt.Shape,TopAbs_EDGE),12);
    const auto before=bounds(original),after=bounds(rebuilt.Shape);
    for(std::size_t i=0;i<before.size();++i)EXPECT_NEAR(before[i],after[i],1e-6)<<"bounds index "<<i;
    EXPECT_NEAR(signedVolume(original),signedVolume(rebuilt.Shape),1e-5);
    EXPECT_NEAR(std::abs(signedVolume(rebuilt.Shape)),6000.,1e-5);
}

bool diagnosticContains(const iCAX::OpenCascade::SOpenCascadeBRepBuildResult& result,const std::string& needle) {
    return std::any_of(result.Diagnostics.begin(),result.Diagnostics.end(),[&](const auto& value){return value.find(needle)!=std::string::npos;});
}
}

TEST(BRepCompoundRoundTrip,NestedCompoundPreservesAllChildrenInEitherRecordOrder) {
    using namespace brep_compound_round_trip;
    const auto shape=nestedBox();auto neutral=convert(shape);
    ASSERT_EQ(neutral.Compounds.size(),3u);
    ASSERT_EQ(neutral.RootShapes.size(),1u);
    EXPECT_EQ(neutral.RootShapes[0].Kind,EBRepShapeKind::Compound);
    EXPECT_EQ(neutral.RootShapes[0].Id,neutral.Compounds.front().Id);
    for(const auto& compound:neutral.Compounds)ASSERT_EQ(compound.Children.size(),1u);
    assertSameGeometry(shape,neutral);
    std::reverse(neutral.Compounds.begin(),neutral.Compounds.end());
    assertSameGeometry(shape,neutral);
    const auto persisted=iCAX::GeometryData::Persistence::Serialize(neutral);
    assertSameGeometry(shape,iCAX::GeometryData::Persistence::Deserialize(persisted));
}

TEST(BRepCompoundRoundTrip,LocatedAndReversedRootsPreserveBoundsAndSignedVolume) {
    using namespace brep_compound_round_trip;
    for(const bool located:{false,true})for(const bool reversed:{false,true}) {
        SCOPED_TRACE(std::string("located=")+std::to_string(located)+" reversed="+std::to_string(reversed));
        const auto shape=nestedBox(located,reversed);auto neutral=convert(shape);
        ASSERT_EQ(neutral.Compounds.size(),3u);
        assertSameGeometry(shape,neutral);
        std::reverse(neutral.Compounds.begin(),neutral.Compounds.end());
        assertSameGeometry(shape,neutral);
    }
}

TEST(BRepCompoundRoundTrip,MissingCompoundReferenceIsRejected) {
    using namespace brep_compound_round_trip;
    auto neutral=convert(nestedBox());ASSERT_GE(neutral.Compounds.size(),2u);
    auto& child=neutral.Compounds.front().Children.front();child.Kind=EBRepShapeKind::Compound;child.Id=999999;
    const auto result=iCAX::OpenCascade::BuildOpenCascadeShape(neutral);
    EXPECT_FALSE(result.bOK);EXPECT_TRUE(diagnosticContains(result,"missing"));
}

TEST(BRepCompoundRoundTrip,CompoundCycleIsRejected) {
    using namespace brep_compound_round_trip;
    auto neutral=convert(nestedBox());ASSERT_GE(neutral.Compounds.size(),2u);
    const auto rootId=neutral.RootShapes.front().Id;
    auto& child=neutral.Compounds.back().Children.front();child.Kind=EBRepShapeKind::Compound;child.Id=rootId;
    const auto result=iCAX::OpenCascade::BuildOpenCascadeShape(neutral);
    EXPECT_FALSE(result.bOK);EXPECT_TRUE(diagnosticContains(result,"cycle"));
}

TEST(BRepCompoundRoundTrip,MissingSolidAndDuplicateCompoundIdsAreRejected) {
    using namespace brep_compound_round_trip;
    auto neutral=convert(nestedBox());ASSERT_GE(neutral.Compounds.size(),2u);
    auto missingSolid=neutral;
    auto& child=missingSolid.Compounds.back().Children.front();child.Kind=EBRepShapeKind::Solid;child.Id=999999;
    const auto missing=iCAX::OpenCascade::BuildOpenCascadeShape(missingSolid);
    EXPECT_FALSE(missing.bOK);EXPECT_TRUE(diagnosticContains(missing,"missing"));
    neutral.Compounds.push_back(neutral.Compounds.front());
    const auto duplicate=iCAX::OpenCascade::BuildOpenCascadeShape(neutral);
    EXPECT_FALSE(duplicate.bOK);EXPECT_TRUE(diagnosticContains(duplicate,"duplicate"));
}
