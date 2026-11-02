#include <gtest/gtest.h>
#include "IntentToolpath/EditableToolpath.h"
#include "IntentToolpath/EditableToolpathInternal.h"
#include <limits>
#include <numbers>

namespace
{
using namespace iCAX::CAM::Intent;
using namespace iCAX::CAM::Intent::Editable;
using namespace iCAX::Data;
using namespace iCAX::GeometryData;
SEditableGeometry Spatial(Curve3 curve,ParameterRange range={0,1,false})
{return MakeEditableGeometry(CompositeCurve3{{{std::move(curve),range}},false},"source");}
SEditableGeometry CylinderPath()
{
    SEditableGeometry geometry;geometry.Authority=EGeometryAuthority::SurfaceUV;geometry.SupportSurface=CylindricalSurface3{Placement3{},10};
    geometry.Segments.push_back({"uv",CurveSegment2{Segment2{{0,0},{std::numbers::pi/2,5}},{0,1,false}},{},{}});return geometry;
}
void Near(Point3 actual,Point3 expected)
{EXPECT_NEAR(actual.X,expected.X,1e-12);EXPECT_NEAR(actual.Y,expected.Y,1e-12);EXPECT_NEAR(actual.Z,expected.Z,1e-12);}
Point3 PhysicalMaterialSide(const SToolpathValue& value,const IToolpathGeometryServices& services)
{
    const auto material=MapAt(value.Channels,"material");const auto normal=ArrayAt(material,"referenceNormal");const Vector3 n{normal[0].To<double>(),normal[1].To<double>(),normal[2].To<double>()};
    const auto& segment=value.Geometry.Segments[0];const auto range=Range(segment);const auto a=services.Evaluate(value.Geometry,segment,First(range)),b=services.Evaluate(value.Geometry,segment,Last(range));
    const Vector3 tangent{b.X-a.X,b.Y-a.Y,b.Z-a.Z};const auto sign=Text(material,"retainedSide")=="Left"?1.:-1.;
    return {sign*(n.Y*tangent.Z-n.Z*tangent.Y),sign*(n.Z*tangent.X-n.X*tangent.Z),sign*(n.X*tangent.Y-n.Y*tangent.X)};
}
SToolpathValue ReverseValue(const SToolpathValue& value,const CToolpathOperationRegistry& registry,const IToolpathGeometryServices& services)
{
    const auto port=SPortRef{GenerateNewUUID(),GenerateNewUUID()};SOperationEvaluation in{GenerateNewUUID(),{{{"input",port,true,{}},GenerateNewUUID(),value,ToolpathFingerprint(value)}},{},{},{}};
    return registry.Find("icax.edit.reverse")->Evaluate(in,services).Outputs.at(0).Value;
}
}

TEST(EditableToolpathGeometry, RejectsInvalidSupportSurfacesBeforeUsingTheirCoordinates)
{
    const auto services=CreateToolpathGeometryServices();auto geometry=CylinderPath();EXPECT_NO_THROW(services->Validate(geometry));
    std::get<CylindricalSurface3>(*geometry.SupportSurface).Radius=-10;EXPECT_THROW(services->Validate(geometry),std::exception);
    geometry=CylinderPath();std::get<CylindricalSurface3>(*geometry.SupportSurface).Placement.YDirection={1,0,0};EXPECT_THROW(services->Validate(geometry),std::exception);
    geometry.SupportSurface=SphericalSurface3{Placement3{},std::numeric_limits<double>::infinity()};EXPECT_THROW(services->Validate(geometry),std::exception);
    geometry.SupportSurface=ConicalSurface3{Placement3{},10,std::numbers::pi};EXPECT_THROW(services->Validate(geometry),std::exception);
    geometry.SupportSurface.reset();EXPECT_THROW(services->Validate(geometry),std::exception);
}
TEST(EditableToolpathGeometry, BoundedNativeRangesCannotExtrapolateOrClampIntoAFalseCurve)
{
    const auto services=CreateToolpathGeometryServices();
    const std::vector<Curve3> curves{Segment3{{0,0,0},{10,0,0}},Arc3{Circle3{Placement3{},10},0,std::numbers::pi/2,true},
        EllipseArc3{Ellipse3{Placement3{},10,5},0,std::numbers::pi/2,true},Bezier3{{{0,0,0},{10,0,0}}},Polyline3{{{0,0,0},{10,0,0}},false}};
    for(const auto& curve:curves)
    {
        EXPECT_NO_THROW(services->Validate(Spatial(curve)));
        EXPECT_THROW(services->Validate(Spatial(curve,{-0.1,0.5,false})),std::exception);
        EXPECT_THROW(services->Validate(Spatial(curve,{0.5,1.1,false})),std::exception);
    }
    // Ray.First shifts its origin; native t remains nonnegative distance from
    // that origin, rather than an absolute coordinate along the backing line.
    Ray3 ray{{{0,0,0},{1,0,0}},5};EXPECT_NO_THROW(services->Validate(Spatial(ray,{0,2,false})));
    EXPECT_THROW(services->Validate(Spatial(ray,{-1,1,false})),std::exception);
}
TEST(EditableToolpathGeometry, ClockwiseEllipseArcMatchesTheEquivalentCircleArc)
{
    const auto services=CreateToolpathGeometryServices();
    for(const auto counterClockwise:{true,false})
    {
        const auto circle=Spatial(Arc3{Circle3{Placement3{},7},0,std::numbers::pi/2,counterClockwise});
        const auto ellipse=Spatial(EllipseArc3{Ellipse3{Placement3{},7,7},0,std::numbers::pi/2,counterClockwise});
        for(const auto parameter:{0.,0.25,0.5,1.})Near(services->Evaluate(ellipse,ellipse.Segments[0],parameter),services->Evaluate(circle,circle.Segments[0],parameter));
    }
    const auto clockwise=Spatial(EllipseArc3{Ellipse3{Placement3{},10,5},0,std::numbers::pi/2,false});
    Near(services->Evaluate(clockwise,clockwise.Segments[0],1),{0,-5,0});
}
TEST(EditableToolpathGeometry, TranslatingUvGeometryMovesPhysicalPointsAndKeepsUvParameters)
{
    CToolpathOperationRegistry registry;RegisterStandardToolpathOperations(registry);const auto services=CreateToolpathGeometryServices();
    SToolpathValue original;original.Geometry=CylinderPath();const auto port=SPortRef{GenerateNewUUID(),GenerateNewUUID()};
    SOperationEvaluation in{GenerateNewUUID(),{{{"input",port,true,{}},GenerateNewUUID(),original,ToolpathFingerprint(original)}},
        {{"delta",Array{2.,-3.,4.}}},{},{}};
    const auto candidate=registry.Find("icax.edit.translate")->Evaluate(in,*services);ASSERT_EQ(1,candidate.Outputs.size());const auto& translated=candidate.Outputs[0].Value.Geometry;
    EXPECT_NO_THROW(services->Validate(translated));EXPECT_EQ(ExactData(original.Geometry.Segments[0].Curve),ExactData(translated.Segments[0].Curve));
    for(const auto parameter:{0.,0.5,1.})
    {const auto before=services->Evaluate(original.Geometry,original.Geometry.Segments[0],parameter);Near(services->Evaluate(translated,translated.Segments[0],parameter),{before.X+2,before.Y-3,before.Z+4});}
}
TEST(EditableToolpathGeometry, LeadMaterialHalfSpaceRequiresAFiniteReferencePoint)
{
    const auto services=CreateToolpathGeometryServices();SOperationEvaluation in;in.Context={{"scrapHalfSpace",Map{{"point",Array{-std::numeric_limits<double>::infinity(),0.,0.}},{"normal",Array{1.,0.,0.}}}}};
    SToolpathValue value;value.Geometry=Spatial(Segment3{{0,0,0},{1,0,0}});value.Geometry.Segments[0].ConstructionRole="Lead";
    SOperationCandidate candidate;candidate.Outputs.push_back({"lead",{},{},value});
    EXPECT_THROW(services->ValidateManufacturing("icax.lead.linear",in,candidate),std::exception);
    in.Context={{"scrapHalfSpace",Map{{"point",Array{0.,0.,0.}},{"normal",Array{1.,0.,0.}}}}};
    EXPECT_NO_THROW(services->ValidateManufacturing("icax.lead.linear",in,candidate));
}
TEST(EditableToolpathGeometry, ReversePreservesPhysicalMaterialSideInPlanarAndSpatialPaths)
{
    CToolpathOperationRegistry registry;RegisterStandardToolpathOperations(registry);const auto services=CreateToolpathGeometryServices();
    EXPECT_TRUE(registry.Find("icax.edit.reverse")->ReadChannels.contains("material"));EXPECT_TRUE(registry.Find("icax.edit.reverse")->WriteChannels.contains("material"));
    for(const auto planar:{true,false})
    {
        SToolpathValue original;
        if(planar){Placement3 plane;plane.XDirection={0,1,0};plane.YDirection={-1,0,0};original.Geometry=MakeEditableGeometry(CompositeCurve2{{{Segment2{{0,0},{10,0}},{0,1,false}}},false},"planar",plane);}
        else original.Geometry=Spatial(Segment3{{1,2,3},{4,6,3}});
        original.Channels["material"]=Map{{"schema",std::string("icax.material-side/1")},{"attachment",std::string("Path")},{"retainedSide",std::string("Left")},{"referenceNormal",Array{0.,0.,1.}}};
        const auto before=PhysicalMaterialSide(original,*services);const auto reversed=ReverseValue(original,registry,*services);
        EXPECT_NO_THROW(services->Validate(reversed.Geometry));EXPECT_EQ("Right",Text(MapAt(reversed.Channels,"material"),"retainedSide"));Near(PhysicalMaterialSide(reversed,*services),before);
        const auto restored=ReverseValue(reversed,registry,*services);EXPECT_EQ(original.Channels,restored.Channels);Near(PhysicalMaterialSide(restored,*services),before);
        Near(services->Evaluate(original.Geometry,original.Geometry.Segments[0],First(Range(original.Geometry.Segments[0]))),services->Evaluate(restored.Geometry,restored.Geometry.Segments[0],First(Range(restored.Geometry.Segments[0]))));
    }
}
TEST(EditableToolpathGeometry, ReverseRejectsUnknownMaterialSchemaWithoutChangingItsData)
{
    CToolpathOperationRegistry registry;RegisterStandardToolpathOperations(registry);const auto services=CreateToolpathGeometryServices();SToolpathValue original;original.Geometry=Spatial(Segment3{{0,0,0},{10,0,0}});
    original.Channels["material"]=Map{{"schema",std::string("vendor.material/3")},{"attachment",std::string("Path")},{"retainedSide",std::string("Left")},{"referenceNormal",Array{0.,0.,1.}}};const auto saved=ToData(original);
    EXPECT_THROW(ReverseValue(original,registry,*services),std::exception);EXPECT_EQ(saved,ToData(original));
    original.Channels["material"]=Map{{"schema",std::string("icax.material-side/1")},{"attachment",std::string("Path")},{"retainedSide",std::string("Left")},{"referenceNormal",Array{0.,0.,0.}}};
    EXPECT_THROW(ReverseValue(original,registry,*services),std::exception);
}
