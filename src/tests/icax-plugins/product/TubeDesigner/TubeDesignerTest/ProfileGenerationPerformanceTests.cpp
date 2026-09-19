#include "pch.h"

#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
#include <OpenCascadeResourceImport/OpenCascadeNeutralModelEvaluator.h>
#include <RenderInteraction/RenderResourceAdapter.h>
#include <Resources/ResourceLibrary.h>
#include <TemplateRuntime/PythonTemplateHost.h>
#include <TemplateRuntime/TemplateCodec.h>

#include <iomanip>
#include <iostream>
#include <numeric>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

namespace
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::Variant;
    using iCAX::Data::VariantArray;
    using Clock = std::chrono::steady_clock;

    double Milliseconds(const Clock::time_point Start_, const Clock::time_point End_)
    {
        return std::chrono::duration<double, std::milli>(End_ - Start_).count();
    }

    VariantArray Numbers(std::initializer_list<double> Values_)
    {
        VariantArray _Result;
        for (const auto _Value : Values_) _Result.emplace_back(_Value);
        return _Result;
    }

    // Keep this model identical to the production BuildProfileExtrusion route:
    // typed neutral-model decoding, profile2d, then a selected-root extrusion.
    TopoDS_Shape ExtrudeProfile(const ObjectMap& Profile_, const double Length_)
    {
        const ObjectMap _Document{
            { "schema", std::string("icax.neutral-model") },
            { "schemaVersion", 1ull },
            { "template", ObjectMap{
                { "id", std::string("icax.tube-profile-extrusion") },
                { "version", std::string("1.0.0") },
                { "packageDigest", Profile_.at("contentDigest") }
            } },
            { "geometry", VariantArray{
                ObjectMap{
                    { "key", std::string("profile") },
                    { "operator", std::string("profile2d") },
                    { "arguments", ObjectMap{
                        { "placement", ObjectMap{
                            { "origin", Numbers({ 0.0, 0.0, 0.0 }) },
                            { "xAxis", Numbers({ 1.0, 0.0, 0.0 }) },
                            { "yAxis", Numbers({ 0.0, 1.0, 0.0 }) }
                        } },
                        { "contours", Profile_.at("contours") }
                    } }
                },
                ObjectMap{
                    { "key", std::string("solid") },
                    { "operator", std::string("extrude") },
                    { "inputs", VariantArray{ Variant(std::string("profile")) } },
                    { "arguments", ObjectMap{ { "vector", Numbers({ 0.0, 0.0, Length_ }) } } }
                }
            } }
        };
        return iCAX::OpenCascade::EvaluateNeutralModel(
            iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(Variant(_Document)),
            { "solid" }).At("solid");
    }

    TEST(ProfileGeneration, GenericArcPathPreservesAnEccentricBore)
    {
        const double _Pi = std::acos(-1.0);
        const auto _CirclePath = [](double CenterX_, double CenterY_, double Radius_)
        {
            VariantArray _Segments;
            for (int _Index = 0; _Index < 4; ++_Index)
            {
                const auto _Point = [&](double Angle_)
                {
                    return Numbers({
                        CenterX_ + Radius_ * std::cos(Angle_),
                        CenterY_ + Radius_ * std::sin(Angle_) });
                };
                const double _Start = std::acos(-1.0) * _Index / 2.0;
                const double _Middle = _Start + std::acos(-1.0) / 4.0;
                const double _End = _Start + std::acos(-1.0) / 2.0;
                _Segments.emplace_back(ObjectMap{
                    { "kind", std::string("arc") },
                    { "start", _Point(_Start) },
                    { "middle", _Point(_Middle) },
                    { "end", _Point(_End) }
                });
            }
            return ObjectMap{
                { "kind", std::string("path") },
                { "closed", true },
                { "segments", _Segments }
            };
        };
        const ObjectMap _Profile{
            { "contentDigest", std::string("arc-path-regression") },
            { "contours", VariantArray{
                _CirclePath(0.0, 0.0, 10.0),
                _CirclePath(3.0, -2.0, 5.0)
            } }
        };
        const auto _Shape = ExtrudeProfile(_Profile, 20.0);
        ASSERT_FALSE(_Shape.IsNull());
        ASSERT_TRUE(BRepCheck_Analyzer(_Shape).IsValid());

        GProp_GProps _Properties;
        BRepGProp::VolumeProperties(_Shape, _Properties);
        EXPECT_NEAR(1500.0 * _Pi, _Properties.Mass(), 1.0e-6);
        EXPECT_NEAR(-1.0, _Properties.CentreOfMass().X(), 1.0e-6);
        EXPECT_NEAR(2.0 / 3.0, _Properties.CentreOfMass().Y(), 1.0e-6);
    }

    TEST(ProfileGeneration, Profile2DRejectsHighLevelContourKinds)
    {
        const ObjectMap _Profile{
            { "contentDigest", std::string("path-only-profile") },
            { "contours", VariantArray{ ObjectMap{
                { "kind", std::string("circle") }, { "radius", 10.0 }
            } } }
        };
        EXPECT_THROW(ExtrudeProfile(_Profile, 20.0), std::invalid_argument);
    }

    ObjectMap RuntimeRequest(const std::filesystem::path& Root_, const ObjectMap& Parameters_)
    {
        return {
            { "protocol", std::string("icax.template-runtime") },
            { "protocolVersion", 1ull },
            { "operation", std::string("evaluate") },
            { "templatePath", (Root_
                / "src/apps/tube-designer/templates/_shared/profile_package_runtime.py").string() },
            { "template", ObjectMap{
                { "id", std::string("icax.profile-package-runtime") },
                { "version", std::string("1.0.0") },
                { "packageDigest", std::string("profile-generation-performance") }
            } },
            { "parameters", Parameters_ },
            { "context", ObjectMap{
                { "coordinateSystem", std::string("right-handed-x-width-y-depth") },
                { "lengthUnit", std::string("mm") }
            } }
        };
    }

    struct SSample
    {
        double Python = 0.0;
        double Validation = 0.0;
        double Extrusion = 0.0;
        double MeshAndBRep = 0.0;
        double PublishAndEncode = 0.0;
        double Total = 0.0;
        std::size_t Triangles = 0;
    };

    SSample SampleProfile(iCAX::TemplateRuntime::CPythonTemplateHost& Host_,
        iCAX::Resource::CResourceLibrary& Resources_,
        const ObjectMap& Request_, const std::string& ID_)
    {
        const auto _Started = Clock::now();
        const auto _Profile = Host_.Invoke(Request_).at("profile").To<ObjectMap>();
        const auto _Evaluated = Clock::now();
        // The current production validation also builds a separate 1 mm solid.
        const auto _ValidationShape = ExtrudeProfile(_Profile, 1.0);
        if (_ValidationShape.IsNull()) throw std::runtime_error("invalid profile");
        const auto _Validated = Clock::now();
        const auto _Shape = ExtrudeProfile(_Profile, 1000.0);
        if (_Shape.IsNull()) throw std::runtime_error("empty preview extrusion");
        const auto _Extruded = Clock::now();
        const auto _URL = Resources_.MakeNamedResourceURL("performance/profile/" + ID_);
        auto _BRep = std::make_shared<iCAX::GeometryData::BRepModel>(
            iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(_Shape, ID_, _URL, 0.025));
        const auto _Meshed = Clock::now();
        std::size_t _TriangleCount = 0;
        for (const auto& _Record : _BRep->Triangulations3)
            _TriangleCount += _Record.Geometry.Triangles.size();
        if (_TriangleCount == 0) throw std::runtime_error("preview has no triangles");
        iCAX::Resource::CResourceInfo _Info;
        _Info.Name = ID_;
        _Info.ResourceTypeID = iCAX::GeometryData::BRepModel::kResourceTypeName;
        _Info.Persistence = iCAX::Resource::EResourcePersistenceMode::Embedded;
        iCAX::Resource::CResourceInfo _Stored;
        Resources_.PutVersioned<iCAX::GeometryData::BRepModel>(
            _URL, std::move(_BRep), _Info,
            iCAX::Resource::EResourceVersionCondition::None, 0, &_Stored);
        if (_Stored.nVersion == 0) throw std::runtime_error("preview resource not published");
        const auto _Render = iCAX::RenderInteraction::EnsureFrontendGeometryResource(
            Resources_, _URL, iCAX::Render::ERenderGeometryKind::Mesh);
        if (_Render.nVersion == 0) throw std::runtime_error("render resource not encoded");
        const auto _Ended = Clock::now();
        return { Milliseconds(_Started, _Evaluated), Milliseconds(_Evaluated, _Validated),
            Milliseconds(_Validated, _Extruded), Milliseconds(_Extruded, _Meshed),
            Milliseconds(_Meshed, _Ended), Milliseconds(_Started, _Ended), _TriangleCount };
    }

    void Report(const std::string& ID_, const std::string& Phase_, const SSample& Sample_)
    {
        std::cout << std::fixed << std::setprecision(3)
            << "[profile-performance] id=" << ID_ << " phase=" << Phase_
            << " python_ms=" << Sample_.Python << " validation_ms=" << Sample_.Validation
            << " extrusion_ms=" << Sample_.Extrusion << " mesh_brep_ms=" << Sample_.MeshAndBRep
            << " publish_encode_ms=" << Sample_.PublishAndEncode
            << " total_ms=" << Sample_.Total << " triangles=" << Sample_.Triangles << std::endl;
    }
}

// Run this filter in a fresh process from the repository root to measure cold
// embedded-Python initialization. Later profiles are first-use, NOT cold-process.
// No timing assertions: scheduling and hardware vary. This measures native CPU
// generation + resource encoding, not WebView dispatch, GPU upload or presentation.
TEST(ProfileTopologyTest, RolesFollowContainmentNotOrderOrWinding)
{
    const auto _Box = [](double x, double y, double w, double h, bool reverse) {
        VariantArray _Points{ Numbers({x,y}), Numbers({x+w,y}), Numbers({x+w,y+h}), Numbers({x,y+h}) };
        if (reverse) std::reverse(_Points.begin(), _Points.end());
        return ObjectMap{{"kind",std::string("polygon")},{"points",_Points}};
    };
    for (bool _Reverse : {false,true})
    {
        VariantArray _Loops{_Box(-20,-15,40,30,_Reverse),_Box(-15,-10,10,20,!_Reverse),_Box(5,-10,10,20,_Reverse)};
        for (int _Order=0; _Order<3; ++_Order)
        {
            const auto _Shape=ExtrudeProfile({{"contentDigest",std::string("topology")},{"contours",_Loops}},10);
            EXPECT_TRUE(BRepCheck_Analyzer(_Shape).IsValid());
            GProp_GProps _Volume;
            BRepGProp::VolumeProperties(_Shape,_Volume);
            EXPECT_NEAR(8000.0,_Volume.Mass(),1e-6);
            std::rotate(_Loops.begin(),_Loops.begin()+1,_Loops.end());
        }
    }
}

TEST(ProfileTopologyTest, InvalidBoundariesAreRejectedByTheCommonEntry)
{
    const auto _Box=[](double x,double y,double w,double h) {
        return ObjectMap{{"kind",std::string("polygon")},{"points",VariantArray{
            Numbers({x,y}),Numbers({x+w,y}),Numbers({x+w,y+h}),Numbers({x,y+h})}}};
    };
    const auto _Outer=_Box(0,0,40,30);
    const VariantArray _Invalid[]{
        {_Outer,_Box(35,5,10,10)}, // crossing exterior
        {_Outer,_Box(30,5,10,10)}, // touching exterior
        {_Outer,_Outer}, // duplicate
        {_Outer,_Box(5,5,15,15),_Box(10,10,15,15)}, // crossing holes
        {_Outer,_Box(50,0,10,10)}, // disconnected material
        {_Outer,_Box(5,5,20,20),_Box(10,10,5,5)}, // material island
        {ObjectMap{{"kind",std::string("polygon")},{"points",VariantArray{
            Numbers({0,0}),Numbers({20,20}),Numbers({0,20}),Numbers({20,0})}}}}, // self crossing
    };
    for (const auto& _Loops:_Invalid)
        EXPECT_THROW(ExtrudeProfile({{"contentDigest",std::string("invalid-topology")},{"contours",_Loops}},10),std::exception);
}

TEST(ProfileTopologyTest, RationalBezierWeightsArePreservedAndValidated)
{
    const ObjectMap _Arc{{"kind",std::string("bezier")},
        {"controlPoints",VariantArray{Numbers({10,0}),Numbers({10,10}),Numbers({0,10})}},
        {"weights",Numbers({1,std::sqrt(0.5),1})}};
    const ObjectMap _Line{{"kind",std::string("line")},{"start",Numbers({0,10})},{"end",Numbers({10,0})}};
    const auto _Build=[&](const ObjectMap& _Curve) {
        return ExtrudeProfile({{"contentDigest",std::string("weighted-bezier")},
            {"contours",VariantArray{ObjectMap{{"kind",std::string("path")},{"segments",VariantArray{_Curve,_Line}}}}}},10);
    };
    const auto _Shape=_Build(_Arc);
    EXPECT_TRUE(BRepCheck_Analyzer(_Shape).IsValid());
    GProp_GProps _Volume;
    BRepGProp::VolumeProperties(_Shape,_Volume);
    EXPECT_NEAR((25*std::acos(-1.0)-50)*10,_Volume.Mass(),1e-5);
    for (const auto& _Weights : {Numbers({1,0,1}),Numbers({1,-1,1}),Numbers({1,1})})
    {
        auto _Invalid=_Arc;
        _Invalid["weights"]=_Weights;
        EXPECT_THROW(_Build(_Invalid),std::exception);
    }
}

TEST(ProfileGenerationPerformanceTest, SystemProfilesColdAndWarmPipeline)
{
    const auto _Root = std::filesystem::current_path();
    const auto _Runtime = _Root / "src/x64/Debug/runtime/python/python312.dll";
    const auto _Worker = _Root
        / "src/iCAX-Engine/framework/TemplateRuntime/python/icax_template_worker.py";
    ASSERT_TRUE(std::filesystem::is_regular_file(_Runtime));
    iCAX::TemplateRuntime::CPythonTemplateHost _Host({ _Runtime, _Worker, _Worker.parent_path() });
    iCAX::Resource::CResourceLibrary _Resources;
    _Resources.SetScope(iCAX::Resource::MakeApplicationResourceScope("profile-performance"));
    const std::array<std::string, 10> _IDs{
        "rect", "round", "ellipse", "flat-oval", "polygon",
        "angle", "channel", "i-section", "t-section", "z-section"
    };
    std::array<ObjectMap, 10> _Requests;
    std::array<std::vector<SSample>, 10> _HotSamples;
    double _FirstTotal = 0.0;
    for (std::size_t _Index = 0; _Index < _IDs.size(); ++_Index)
    {
        SCOPED_TRACE(_IDs[_Index]);
        _Requests[_Index] = RuntimeRequest(_Root, {
            { "action", std::string("evaluate-system") },
            { "systemProfileId", _IDs[_Index] },
            { "profileRoot", (_Root / "src/apps/tube-designer/templates/profile").string() },
            { "values", ObjectMap{} }
        });
        const auto _Sample = SampleProfile(_Host, _Resources, _Requests[_Index], _IDs[_Index]);
        _FirstTotal += _Sample.Total;
        Report(_IDs[_Index], _Index == 0 ? "process-first" : "profile-first", _Sample);
    }
    constexpr int _Repetitions = 5;
    double _HotTotal = 0.0;
    for (int _Pass = 0; _Pass < _Repetitions; ++_Pass)
    {
        for (std::size_t _Index = 0; _Index < _IDs.size(); ++_Index)
        {
            SCOPED_TRACE(_IDs[_Index]);
            const auto _Sample = SampleProfile(_Host, _Resources, _Requests[_Index], _IDs[_Index]);
            _HotSamples[_Index].push_back(_Sample);
            _HotTotal += _Sample.Total;
        }
    }
    for (std::size_t _Index = 0; _Index < _IDs.size(); ++_Index)
    {
        auto& _Samples = _HotSamples[_Index];
        std::sort(_Samples.begin(), _Samples.end(),
            [](const SSample& A_, const SSample& B_) { return A_.Total < B_.Total; });
        Report(_IDs[_Index], "hot-median", _Samples[_Samples.size() / 2]);
        Report(_IDs[_Index], "hot-max", _Samples.back());
    }
    const auto _ListStarted = Clock::now();
    const auto _Catalog = _Host.Invoke(RuntimeRequest(_Root, {
        { "action", std::string("list-system") },
        { "profileRoot", (_Root / "src/apps/tube-designer/templates/profile").string() }
    }));
    const auto _ListEnded = Clock::now();
    ASSERT_EQ(_IDs.size(), _Catalog.at("systemProfiles").To<VariantArray>().size());
    std::cout << "[profile-performance] profiles=" << _IDs.size()
        << " first_sweep_ms=" << _FirstTotal
        << " hot_sweep_mean_ms=" << _HotTotal / _Repetitions
        << " hot_per_profile_mean_ms=" << _HotTotal / (_Repetitions * _IDs.size())
        << " hot_catalog_python_ms=" << Milliseconds(_ListStarted, _ListEnded)
        << " length_mm=1000 mesh_tolerance=0.025" << std::endl;
}
