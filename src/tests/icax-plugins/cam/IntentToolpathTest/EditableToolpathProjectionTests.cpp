#include <gtest/gtest.h>
#include "IntentToolpath/EditableToolpathProjection.h"
#include "IntentToolpath/EditableToolpathPreview.h"

namespace
{
using namespace iCAX::CAM::Intent;using namespace iCAX::GeometryData;
using Map=IntentMap;using Array=IntentArray;
struct ProjectionFixture
{
    SPortRef Nominal=SourceToolpathPort(iCAX::Data::GenerateNewUUID()),Selected=SourceToolpathPort(iCAX::Data::GenerateNewUUID());
    SFrozenPathInput Source,Selection;std::shared_ptr<const IToolpathGeometryServices> Geometry=CreateToolpathGeometryServices();
    ProjectionFixture(bool reversed=false)
    {
        SToolpathValue source;source.Geometry=MakeEditableGeometry(CompositeCurve3{{{Segment3{{0,0,0},{10,0,0}},{0,1,false}}},false},"nominal-line");
        Source={{"nominal",Nominal,false,{}},iCAX::Data::GenerateNewUUID(),source,ToolpathFingerprint(source)};
        SToolpathValue selection;selection.Geometry=MakeEditableGeometry(CompositeCurve3{{{Segment3{{0,1,0},{10,1,0}},{.2,.8,reversed}}},false},"processed-line");selection.Geometry.Meaning="ToolCenter";
        const ParameterRange range{.2,.8,false};selection.Geometry.Segments[0].Sources.push_back({Nominal,source.Geometry.Segments[0].Id,range,range,"Exact",0,0,ToolpathFingerprint(source,{"geometry","topology","traversal","frame"})});
        selection.Channels["points"]=Map{{"attachment",std::string("Points")},{"entries",Array{Map{{"id",std::string("kept point")},
            {"anchor",ToData(SPathAnchor{Selected,selection.Geometry.Segments[0].Id,.4})},{"boundaryPolicy",std::string("Duplicate")}}}}};
        selection.Channels["intervals"]=Map{{"attachment",std::string("Intervals")},{"entries",Array{Map{
            {"range",ToData(SPathRange{{Selected,selection.Geometry.Segments[0].Id,.3},{Selected,selection.Geometry.Segments[0].Id,.7},false,reversed?-1:1,{}})},
            {"value",Map{{"interpolation",std::string("Smooth")},{"originalDomain",Array{.1,.9}}}}}}}};
        Selection={{"selection",Selected,true,{}},iCAX::Data::GenerateNewUUID(),selection,ToolpathFingerprint(selection)};
    }
};
}
TEST(EditableToolpathProjection, ExactNativeInverseSelectionRetainsItsFieldAndTraversalWithoutRetainingTheProcessedCurve)
{
    for(const bool reversed:{false,true})
    {
        ProjectionFixture host(reversed);const auto operation=iCAX::Data::GenerateNewUUID();
        const auto result=ProjectToolpathSelection(host.Selection,host.Source,operation,{},*host.Geometry);
        ASSERT_EQ(1u,result.Value.Geometry.Segments.size());const auto& segment=result.Value.Geometry.Segments[0];const auto curve=std::get<CurveSegment3>(segment.Curve);
        EXPECT_EQ(reversed,curve.Range.Reversed);EXPECT_DOUBLE_EQ(.2,curve.Range.First);EXPECT_DOUBLE_EQ(.8,curve.Range.Last);
        const auto point=host.Geometry->Evaluate(result.Value.Geometry,segment,.4);EXPECT_NEAR(0.,point.Y,1e-12);
        const auto entry=result.Value.Channels.at("points").To<Map>().at("entries").To<Array>().at(0).To<Map>();const auto anchor=AnchorFromData(entry.at("anchor").To<Map>());
        EXPECT_EQ(DeclaredToolpathPort(operation,"main"),anchor.Port);EXPECT_EQ(segment.Id,anchor.SegmentId);EXPECT_DOUBLE_EQ(.4,anchor.Parameter);
        const auto interval=result.Value.Channels.at("intervals").To<Map>().at("entries").To<Array>().at(0).To<Map>();
        EXPECT_EQ("Smooth",interval.at("value").To<Map>().at("interpolation").To<std::string>());EXPECT_EQ(Array({.1,.9}),interval.at("value").To<Map>().at("originalDomain").To<Array>());
        EXPECT_TRUE(result.ExcludedRanges.empty());
        for(const auto& source:segment.Sources)if(source.Source==host.Selected)EXPECT_EQ(source.SourceRange.Reversed,source.OutputRange.Reversed);
    }
}
TEST(EditableToolpathProjection, AReadOnlyNominalInputDoesNotPreventTheUniqueReplaceInputFromRetainingItsIdentity)
{
    ProjectionFixture host;CToolpathOperationRegistry registry;SOperationType type;type.TypeId="test.project-selection";type.MinimumInputs=2;type.MaximumInputs=2;
    type.ParameterSchema={{"fields",Map{}}};type.WriteChannels={"*"};type.PropagateUnwrittenChannels=false;type.DeclaredOutputRoles={"main"};type.DeclaredOutputIdentityInputs={{"main",0}};
    type.Evaluate=[](const SOperationEvaluation& input,const IToolpathGeometryServices& geometry)
    {auto projection=ProjectToolpathSelection(input.Inputs[0],input.Inputs[1],input.OperationId,{},geometry);SOperationCandidate result;result.Outputs.push_back({"main",{},std::size_t(0),std::move(projection.Value)});result.ExcludedRanges=std::move(projection.ExcludedRanges);return result;};
    registry.Register(type);SOperationEvaluation input;input.OperationId=iCAX::Data::GenerateNewUUID();input.Inputs={host.Selection,host.Source};
    const auto result=EvaluateFrozenToolpathOperation(registry,*host.Geometry,type.TypeId,input);ASSERT_EQ(1u,result.Outputs.size());ASSERT_TRUE(result.Outputs[0].InheritInput);EXPECT_EQ(0u,*result.Outputs[0].InheritInput);
}
TEST(EditableToolpathProjection, FinalizationChecksNestedPrivateAnchorsWithoutChangingItsInput)
{
    for(const bool privateAnchor:{false,true})
    {
        ProjectionFixture host;const auto operation=iCAX::Data::GenerateNewUUID(),hidden=iCAX::Data::GenerateNewUUID();
        const auto anchor=ToData(SPathAnchor{DeclaredToolpathPort(privateAnchor?hidden:operation,"main"),"metadata point",.4});
        host.Selection.Value.Channels["nested"]=Map{{"attachment",std::string("Path")},
            {"data",Array{Map{{"children",Array{Map{{"anchor",anchor}}}}}}}};
        const auto before=ToData(host.Selection.Value);
        if(privateAnchor)
        {
            try{(void)FinalizeProjectedToolpath(host.Selection,operation,{hidden},*host.Geometry);FAIL()<<"Private anchor escaped validation";}
            catch(const std::invalid_argument& error){EXPECT_STREQ("TOOLPATH_SELECTION_PROJECTION_PRIVATE_ANCHOR",error.what());}
        }
        else
        {
            const auto result=FinalizeProjectedToolpath(host.Selection,operation,{hidden},*host.Geometry);
            EXPECT_EQ(host.Selection.Value.Channels.at("nested"),result.Channels.at("nested"));
        }
        EXPECT_EQ(before,ToData(host.Selection.Value));
    }
}
TEST(EditableToolpathProjection, DroppedConstructionPointsAndChangedOrApproximateProvenanceRequireRebind)
{
    for(const int invalid:{0,1})
    {
        ProjectionFixture host;auto& mapping=host.Selection.Value.Geometry.Segments[0].Sources[0];if(invalid==0)mapping.Precision="Approximate";else mapping.SourceGeometryFingerprint="changed input";
        EXPECT_THROW(ProjectToolpathSelection(host.Selection,host.Source,iCAX::Data::GenerateNewUUID(),{},*host.Geometry),std::invalid_argument);
    }
    ProjectionFixture host;auto& value=host.Selection.Value;SEditableSegment lead;lead.Id="authored construction";lead.ConstructionRole="LeadIn";lead.Curve=CurveSegment3{Segment3{{1,1,0},{2,1,0}},{0,1,false}};
    value.Geometry.Segments.insert(value.Geometry.Segments.begin(),lead);
    auto points=value.Channels.at("points").To<Map>();auto entries=points.at("entries").To<Array>();entries.emplace_back(Map{{"id",std::string("protected lead point")},{"anchor",ToData(SPathAnchor{host.Selected,lead.Id,.5})},{"boundaryPolicy",std::string("Duplicate")}});points["entries"]=entries;value.Channels["points"]=points;
    EXPECT_THROW(ProjectToolpathSelection(host.Selection,host.Source,iCAX::Data::GenerateNewUUID(),{"LeadIn"},*host.Geometry),std::invalid_argument);
}
