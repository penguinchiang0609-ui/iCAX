#include "pch.h"
#include "SDO.h"
#include "SDOSupport.h"
#include "ToolpathSDOImplement.h"
#include "WorkpieceSDOImplement.h"
#include "GeometryData/GeometryData.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepBuilder.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepReader.h"
#include "OpenCascadeResourceImport/OpenCascadeTubeCSGConverter.h"
#include "RenderInteraction/RenderInteraction.h"
#include "RenderInteraction/RenderResourceAdapter.h"
#include "Project/Project.h"
#include "Project/ProjectScene.h"
#include "SceneComponents.h"
#include "WorkpieceResourceKeys.h"

#include "SDO/SDORegistrationCatalog.h"
#include "SDO/SDO.h"

#include <BRepAlgoAPI_Common.hxx>
#include <cmath>
#include <set>
#include <type_traits>

namespace
{
    class CCADIntentSDO final : public iCAX::Interaction::CSDO
    {
    public:
        CCADIntentSDO()
            : CSDO("CADIntent")
        {
            ExposeMethod("Recognize", &iCAX::CAM::SDO::HandleRecognizeCADIntent);
            ExposeMethod("OpenEditorScene", &iCAX::CAM::SDO::HandleOpenCADIntentEditorScene);
            ExposeMethod("CloseEditorScene", &iCAX::CAM::SDO::HandleCloseCADIntentEditorScene);
            ExposeMethod("AddTool", &iCAX::CAM::SDO::HandleAddCADIntentTool);
            ExposeMethod("PreviewParameters", &iCAX::CAM::SDO::HandlePreviewCADIntentParameters);
            ExposeMethod("SetParameters", &iCAX::CAM::SDO::HandleSetCADIntentParameters);
            ExposeMethod("SelectInterpretation", &iCAX::CAM::SDO::HandleSelectCADIntentInterpretation);
        }
    };

    static_assert(iCAX::Interaction::IsStatelessSDOType<CCADIntentSDO>);
}

ICAX_REGISTER_SDO(CCADIntentSDO)

namespace iCAX::CAM::SDO
{
using namespace Internal;
namespace
{
    struct STubeIntentResource final
    {
        std::string ResourceID;
        std::shared_ptr<iCAX::GeometryData::Tube::CTubeNeutralGeometry> Geometry;
    };

    iCAX::Project::CProjectScene& _RequireConcreteScene(
        IN iCAX::Project::ISceneContext& Scene_)
    {
        auto* _pScene = dynamic_cast<iCAX::Project::CProjectScene*>(&Scene_);
        if (!_pScene)
        {
            throw std::invalid_argument(
                "CADIntent editor Scene requires a project-owned scene runtime");
        }
        return *_pScene;
    }

    Interaction::CInvocation _MakeLocalInvocation(
        IN const char* pSDO_,
        IN const char* pMethod_,
        IN const ObjectMap& Payload_)
    {
        Interaction::CInvocation _Request;
        _Request.Method = Interaction::MakeSDOMethod(pSDO_, pMethod_);
        const auto _Text = iCAX::Data::VariantSerializer::Serialize(Variant(Payload_));
        _Request.Payload.assign(_Text.begin(), _Text.end());
        return _Request;
    }

    Variant _MakeEditorPDOPayload(
        IN const iCAX::Project::CScenePDODescriptor& Descriptor_)
    {
        ObjectMap _PDO;
        _PDO["enabled"] = Descriptor_.bEnabled;
        std::string _ArenaName;
        _ArenaName.reserve(Descriptor_.SharedArenaName.size());
        for (const auto _Character : Descriptor_.SharedArenaName)
        {
            _ArenaName.push_back(static_cast<char>(_Character));
        }
        _PDO["sharedArenaName"] = std::move(_ArenaName);
        _PDO["sharedArenaSize"] = static_cast<unsigned long long>(
            Descriptor_.nSharedArenaSize);
        iCAX::Data::VariantArray _Declarations;
        _Declarations.reserve(Descriptor_.Declarations.size());
        for (const auto& _Declaration : Descriptor_.Declarations)
        {
            ObjectMap _Payload;
            _Payload["id"] = static_cast<unsigned long long>(_Declaration.nID);
            _Payload["version"] = static_cast<unsigned int>(_Declaration.nVersion);
            _Payload["direction"] = static_cast<unsigned int>(_Declaration.eDirection);
            _Payload["payloadSize"] = static_cast<int>(_Declaration.nPayloadSize);
            _Declarations.emplace_back(std::move(_Payload));
        }
        _PDO["declarations"] = std::move(_Declarations);
        return Variant(std::move(_PDO));
    }

    ObjectMap _MakeEditorScenePayload(
        IN const iCAX::Project::CProjectScene& Scene_)
    {
        ObjectMap _Scene;
        _Scene["sceneId"] = Scene_.GetSceneID();
        // 前端必须通过宿主注册这个新通道，不能把尚未注册的后端 channel id 当成可用通道。
        _Scene["sceneChannelId"] = std::string();
        _Scene["parentSceneId"] = Scene_.GetParentSceneID();
        _Scene["sceneName"] = Scene_.GetSceneName();
        _Scene["role"] = std::string("Transient");
        _Scene["state"] = std::string("Running");
        _Scene["isMainScene"] = false;
        _Scene["isTransientScene"] = true;
        _Scene["isOpen"] = Scene_.IsOpen();
        _Scene["isRunning"] = Scene_.IsRunning();
        _Scene["startupComponent"] = Scene_.GetStartupComponent();
        _Scene["pdo"] = _MakeEditorPDOPayload(Scene_.GetPDODescriptor());
        return _Scene;
    }

    STubeIntentResource _RequireTubeIntentResource(
        IN iCAX::Project::ISceneContext& Scene_)
    {
        auto [_pEntity, _pWorkpiece] = _GetActiveWorkpiece(Scene_.Database());
        if (!_pEntity || !_pWorkpiece)
        {
            throw std::invalid_argument("CADIntent requires an active workpiece");
        }
        const auto _ResourceID = _pWorkpiece->GetTubeNeutralGeometryResourceID();
        if (_ResourceID.empty())
        {
            throw std::invalid_argument(
                "CADIntent requires a workpiece with tube neutral geometry");
        }
        auto _pGeometry = Scene_.Resources().Get<
            iCAX::GeometryData::Tube::CTubeNeutralGeometry>(_ResourceID);
        if (!_pGeometry)
        {
            throw std::runtime_error(
                "CADIntent tube neutral geometry resource is not loaded: "
                + _ResourceID);
        }
        return { _ResourceID, std::move(_pGeometry) };
    }

    std::string _StoreIntentBRep(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const std::string& strSourceBRepResourceID_,
        IN const std::string& strSuffix_,
        IN const std::string& strName_,
        IN const TopoDS_Shape& Shape_,
        IN double dTolerance_)
    {
        if (Shape_.IsNull()) return {};
        auto& _Resources = Scene_.Resources();
        const auto _ResourceID = _Resources.MakeDerivedResourceURL(
            strSourceBRepResourceID_, strSuffix_);
        const auto _Version = _NextResourceVersion(_Resources, _ResourceID);
        auto _pBRep = std::make_shared<iCAX::GeometryData::BRepModel>(
            iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(
                Shape_, strName_, strSourceBRepResourceID_, dTolerance_));
        auto _Info = _MakeResourceInfo(
            _ResourceID,
            strName_,
            iCAX::GeometryData::BRepModel::kResourceTypeName,
            iCAX::Resource::EResourcePersistenceMode::Embedded,
            _Version);
        _Info.Metadata["sourceBRepResourceId"] = strSourceBRepResourceID_;
        _Info.Metadata["csgRole"] = strSuffix_;
        _Info.Dependencies.push_back({
            strSourceBRepResourceID_,
            _Resources.GetVersion(strSourceBRepResourceID_)
        });
        _Resources.Set<iCAX::GeometryData::BRepModel>(
            _ResourceID, std::move(_pBRep), _Info);
        return _ResourceID;
    }

    STubeIntentResource _RecognizeTubeIntentResource(
        IN iCAX::Project::ISceneContext& Scene_)
    {
        auto [_pEntity, _pWorkpiece] = _GetActiveWorkpiece(Scene_.Database());
        if (!_pEntity || !_pWorkpiece)
            throw std::invalid_argument("CADIntent requires an active workpiece");

        if (!_pWorkpiece->GetTubeNeutralGeometryResourceID().empty())
            return _RequireTubeIntentResource(Scene_);

        auto _GeometryResourceID = _pWorkpiece->GetGeometryResourceID();
        if (_GeometryResourceID.empty())
            _GeometryResourceID = _pWorkpiece->GetBRepResourceID();
        if (_GeometryResourceID.empty())
            throw std::invalid_argument("CADIntent requires workpiece geometry");
        const auto _pBRep = Scene_.Resources().Get<
            iCAX::GeometryData::BRepModel>(_GeometryResourceID);
        if (!_pBRep)
            throw std::runtime_error(
                "CADIntent geometry resource is not loaded: " + _GeometryResourceID);

        const auto _Rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(*_pBRep);
        if (!_Rebuilt.bOK)
        {
            std::ostringstream _Message;
            _Message << "无法从中性 BRep 恢复 CAD 编辑拓扑";
            for (const auto& _Diagnostic : _Rebuilt.Diagnostics)
                _Message << ": " << _Diagnostic;
            throw std::runtime_error(_Message.str());
        }

        constexpr double _Tolerance = 0.001;
        auto& _Resources = Scene_.Resources();
        const auto _NeutralResourceID = _Resources.MakeDerivedResourceURL(
            _GeometryResourceID, "tube.neutral_geometry");
        const auto _RemovalResourceID = _Resources.MakeDerivedResourceURL(
            _GeometryResourceID, "tube.csg.composite.removal");
        const auto _AdditionResourceID = _Resources.MakeDerivedResourceURL(
            _GeometryResourceID, "tube.csg.residual.addition");

        iCAX::OpenCascade::STubeCSGConversionOptions _Options;
        _Options.Tolerance = _Tolerance;
        _Options.SourceBRepResourceID = _GeometryResourceID;
        _Options.RemovalBRepResourceID = _RemovalResourceID;
        _Options.AdditionBRepResourceID = _AdditionResourceID;
        auto _Conversion = iCAX::OpenCascade::ConvertBRepToTubeCSG(
            _Rebuilt.Shape, _Options);
        if (_Conversion.Geometry.RecognitionStatus
            == iCAX::GeometryData::Tube::ERecognitionStatus::Failed)
        {
            std::ostringstream _Message;
            _Message << "CAD 意图识别没有生成可编辑 CSG";
            for (const auto& _Diagnostic : _Conversion.Geometry.Diagnostics)
                _Message << ": " << _Diagnostic.Message;
            throw std::runtime_error(_Message.str());
        }

        const auto _DisplayName = _pWorkpiece->GetName().empty()
            ? std::string("Tube CAD intent")
            : _pWorkpiece->GetName();
        _StoreIntentBRep(
            Scene_, _GeometryResourceID,
            "tube.csg.composite.removal",
            _DisplayName + " CSG removal",
            _Conversion.RemovalShape,
            _Tolerance);
        _StoreIntentBRep(
            Scene_, _GeometryResourceID,
            "tube.csg.residual.addition",
            _DisplayName + " CSG addition",
            _Conversion.AdditionShape,
            _Tolerance);

        for (const auto& [_NodeID, _Shape] : _Conversion.PreviewShapes)
        {
            const auto _PreviewBRepID = _StoreIntentBRep(
                Scene_,
                _GeometryResourceID,
                "tube.csg.preview." + _NodeID,
                _DisplayName + " CSG preview " + _NodeID,
                _Shape,
                _Tolerance);
            if (_PreviewBRepID.empty()) continue;
            const auto _Preview =
                iCAX::RenderInteraction::EnsureFrontendGeometryResource(
                    _Resources,
                    _PreviewBRepID,
                    iCAX::Render::ERenderGeometryKind::Mesh);
            const auto _Node = std::find_if(
                _Conversion.Geometry.SolidNodes.begin(),
                _Conversion.Geometry.SolidNodes.end(),
                [&_NodeID](IN const auto& Node_) { return Node_.ID == _NodeID; });
            if (_Node != _Conversion.Geometry.SolidNodes.end())
            {
                _Node->Metadata["previewResourceUrl"] = _Preview.URL;
                _Node->Metadata["previewResourceVersion"] = std::to_string(_Preview.nVersion);
                _Node->Metadata["previewKind"] = _NodeID == "base"
                    ? "base"
                    : "construction-body";
                continue;
            }
            for (auto& _OwnerNode : _Conversion.Geometry.SolidNodes)
            {
                auto _Attach = [&_NodeID, &_Preview](IN auto& Extrusion_) {
                    const auto _Primitive = std::find_if(
                        Extrusion_.SectionPrimitives.begin(),
                        Extrusion_.SectionPrimitives.end(),
                        [&_NodeID](IN const auto& Primitive_) {
                            return Primitive_.ID == _NodeID;
                        });
                    if (_Primitive == Extrusion_.SectionPrimitives.end())
                    {
                        return false;
                    }
                    _Primitive->Metadata["previewResourceUrl"] = _Preview.URL;
                    _Primitive->Metadata["previewResourceVersion"] =
                        std::to_string(_Preview.nVersion);
                    _Primitive->Metadata["previewKind"] = "section-primitive";
                    return true;
                };
                if (auto* _pExtrusion = std::get_if<
                    iCAX::GeometryData::Tube::SExtrudedRegionNode>(
                        &_OwnerNode.Data))
                {
                    if (_Attach(*_pExtrusion)) break;
                }
                else if (auto* _pTaper = std::get_if<
                    iCAX::GeometryData::Tube::STaperedRegionNode>(
                        &_OwnerNode.Data))
                {
                    if (_Attach(*_pTaper)) break;
                }
            }
        }

        const auto _Version = _NextResourceVersion(
            _Resources, _NeutralResourceID);
        auto _pGeometry = std::make_shared<
            iCAX::GeometryData::Tube::CTubeNeutralGeometry>(
                std::move(_Conversion.Geometry));
        _pGeometry->Version = _Version;
        _pGeometry->Metadata["sourceBRepResourceId"] = _GeometryResourceID;
        _pGeometry->Metadata["recognitionMode"] = "on-demand-cad-edit";
        auto _Info = _MakeResourceInfo(
            _NeutralResourceID,
            _DisplayName + " Tube neutral geometry",
            iCAX::GeometryData::Tube::CTubeNeutralGeometry::kResourceTypeName,
            iCAX::Resource::EResourcePersistenceMode::Embedded,
            _Version);
        _Info.Metadata["sourceBRepResourceId"] = _GeometryResourceID;
        _Info.Metadata["recognitionMode"] = "on-demand-cad-edit";
        _Info.Dependencies.push_back({
            _GeometryResourceID,
            _Resources.GetVersion(_GeometryResourceID)
        });
        _Resources.Set<iCAX::GeometryData::Tube::CTubeNeutralGeometry>(
            _NeutralResourceID, _pGeometry, _Info);

        auto _Undo = Scene_.Database().BeginUndoCommand("Recognize tube CAD intent");
        if (!_SetStringProperty(
            _pWorkpiece,
            iCAX::CAM::CWorkpieceComponent::PropertyName_TubeNeutralGeometryResourceID,
            _NeutralResourceID))
        {
            throw std::runtime_error("Cannot attach recognized CAD intent to workpiece");
        }
        _Undo->End();
        return { _NeutralResourceID, std::move(_pGeometry) };
    }

    void _StoreEditedTubeIntentResource(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const std::string& strResourceID_,
        IN const std::shared_ptr<iCAX::GeometryData::Tube::CTubeNeutralGeometry>& pGeometry_,
        IN bool bGeometricallyReevaluated_ = false)
    {
        const auto _Version = _NextResourceVersion(Scene_.Resources(), strResourceID_);
        pGeometry_->Version = _Version;
        pGeometry_->Metadata["editState"] = bGeometricallyReevaluated_
            ? "Reevaluated"
            : "ParameterEdited";
        pGeometry_->Metadata["requiresGeometricReevaluation"] =
            bGeometricallyReevaluated_ ? "false" : "true";
        auto _Info = Scene_.Resources().GetInfo(strResourceID_).value_or(
            _MakeResourceInfo(
                strResourceID_,
                "Tube CAD intent",
                iCAX::GeometryData::Tube::CTubeNeutralGeometry::kResourceTypeName,
                iCAX::Resource::EResourcePersistenceMode::Embedded,
                _Version));
        _Info.nVersion = _Version;
        _Info.Metadata["editState"] = pGeometry_->Metadata["editState"];
        _Info.Metadata["requiresGeometricReevaluation"] =
            pGeometry_->Metadata["requiresGeometricReevaluation"];
        Scene_.Resources().Set<iCAX::GeometryData::Tube::CTubeNeutralGeometry>(
            strResourceID_,
            pGeometry_,
            _Info);
    }

    iCAX::GeometryData::Tube::SSectionPrimitive _MakeBuiltInSectionPrimitive(
        IN const std::string& strNodeID_,
        IN bool bCircular_,
        IN double dWidth_,
        IN double dHeight_)
    {
        using namespace iCAX::GeometryData::Tube;
        SSectionPrimitive _Primitive;
        _Primitive.ID = strNodeID_ + "/section/outer";
        _Primitive.Label = bCircular_ ? "圆形刀具截面" : "矩形刀具截面";
        _Primitive.LoopID = "outer";
        _Primitive.Role = ESectionPrimitiveRole::OuterBoundary;
        _Primitive.Kind = bCircular_
            ? ESectionPrimitiveKind::Circle
            : ESectionPrimitiveKind::Rectangle;
        if (bCircular_)
        {
            _Primitive.Radius = SScalarParameter{ 0.5 * dWidth_ };
        }
        else
        {
            _Primitive.Width = SScalarParameter{ dWidth_ };
            _Primitive.Height = SScalarParameter{ dHeight_ };
        }
        _Primitive.Metadata["source"] = "built-in-cad-tool";
        _Primitive.Metadata["ownerNodeId"] = strNodeID_;
        return _Primitive;
    }

    iCAX::GeometryData::Tube::CPlanarRegion2 _MakeBuiltInToolSection(
        IN bool bCircular_,
        IN double dWidth_,
        IN double dHeight_)
    {
        using namespace iCAX::GeometryData;
        using namespace iCAX::GeometryData::Tube;
        CPlanarRegion2 _Section;
        SRegionLoop2 _Loop;
        _Loop.ID = "outer";
        if (bCircular_)
        {
            SRegionCurve2 _Curve;
            _Curve.ID = "outer/curve-1";
            Circle2 _Circle;
            _Circle.Placement.Location = { 0.0, 0.0 };
            _Circle.Placement.XDirection = { 1.0, 0.0 };
            _Circle.Placement.YDirection = { 0.0, 1.0 };
            _Circle.Radius = 0.5 * dWidth_;
            _Curve.Segment.Curve = _Circle;
            _Curve.Segment.Range = { 0.0, 2.0 * 3.14159265358979323846, false };
            _Loop.Curves.push_back(std::move(_Curve));
        }
        else
        {
            const auto _HalfWidth = 0.5 * dWidth_;
            const auto _HalfHeight = 0.5 * dHeight_;
            const std::array<Point2, 4> _Points = {
                Point2{ -_HalfWidth, -_HalfHeight },
                Point2{ _HalfWidth, -_HalfHeight },
                Point2{ _HalfWidth, _HalfHeight },
                Point2{ -_HalfWidth, _HalfHeight }
            };
            for (std::size_t _Index = 0; _Index < _Points.size(); ++_Index)
            {
                SRegionCurve2 _Curve;
                _Curve.ID = "outer/curve-" + std::to_string(_Index + 1);
                _Curve.Segment.Curve = Segment2{
                    _Points[_Index],
                    _Points[(_Index + 1) % _Points.size()]
                };
                _Curve.Segment.Range = { 0.0, 1.0, false };
                _Loop.Curves.push_back(std::move(_Curve));
            }
        }
        _Section.Boundaries.push_back(std::move(_Loop));
        return _Section;
    }

    std::string _NextBuiltInToolNodeID(
        IN const iCAX::GeometryData::Tube::CTubeNeutralGeometry& Geometry_)
    {
        for (std::size_t _Index = 1;; ++_Index)
        {
            const auto _ID = "edit/tool-" + std::to_string(_Index);
            if (std::none_of(
                Geometry_.SolidNodes.begin(),
                Geometry_.SolidNodes.end(),
                [&_ID](IN const auto& Node_) { return Node_.ID == _ID; }))
            {
                return _ID;
            }
        }
    }

    iCAX::GeometryData::Placement3 _MakeDefaultToolFrame(
        IN const iCAX::GeometryData::Tube::SExtrudedRegionNode& Base_)
    {
        using iCAX::GeometryData::Direction3;
        using iCAX::GeometryData::Placement3;
        Placement3 _Frame;
        const auto _Mid = 0.5 * (Base_.First + Base_.Last);
        _Frame.Location = {
            Base_.Frame.Location.X + _Mid * Base_.Frame.ZDirection.X,
            Base_.Frame.Location.Y + _Mid * Base_.Frame.ZDirection.Y,
            Base_.Frame.Location.Z + _Mid * Base_.Frame.ZDirection.Z
        };
        _Frame.XDirection = Base_.Frame.ZDirection;
        _Frame.ZDirection = Base_.Frame.XDirection;
        _Frame.YDirection = Direction3{
            _Frame.ZDirection.Y * _Frame.XDirection.Z - _Frame.ZDirection.Z * _Frame.XDirection.Y,
            _Frame.ZDirection.Z * _Frame.XDirection.X - _Frame.ZDirection.X * _Frame.XDirection.Z,
            _Frame.ZDirection.X * _Frame.XDirection.Y - _Frame.ZDirection.Y * _Frame.XDirection.X
        };
        return _Frame;
    }

    iCAX::GeometryData::Tube::SFeatureRelations _MakeBuiltInToolRelations(
        IN iCAX::GeometryData::Tube::EFeatureMaterialRole Role_)
    {
        using namespace iCAX::GeometryData::Tube;
        SFeatureRelations _Relations;
        SFeatureMaterialEffect _Effect;
        _Effect.Role = Role_;
        _Effect.HostNodeID = "base";
        _Effect.Confidence = 1.0;
        _Relations.MaterialEffect = std::move(_Effect);
        _Relations.DependsOnNodeIDs.push_back("base");
        return _Relations;
    }

    double _ParameterValue(
        IN const ObjectMap& Parameters_,
        IN const std::string& strName_,
        IN double dCurrent_)
    {
        const auto _Iter = Parameters_.find(strName_);
        return _Iter == Parameters_.end()
            ? dCurrent_
            : _ToDouble(_Iter->second, "parameters." + strName_);
    }

    bool _HasParameter(
        IN const ObjectMap& Parameters_,
        IN const std::string& strName_)
    {
        return Parameters_.find(strName_) != Parameters_.end();
    }

    void _TranslateCurve2(
        IN OUT iCAX::GeometryData::Curve2& Curve_,
        IN double dX_,
        IN double dY_)
    {
        using namespace iCAX::GeometryData;
        const auto _MovePoint = [dX_, dY_](IN OUT Point2& Point_) {
            Point_.X += dX_;
            Point_.Y += dY_;
        };
        std::visit([&_MovePoint](IN OUT auto& Value_) {
            using T = std::decay_t<decltype(Value_)>;
            if constexpr (std::is_same_v<T, Line2> || std::is_same_v<T, Ray2>)
            {
                _MovePoint(Value_.Axis.Location);
            }
            else if constexpr (std::is_same_v<T, Segment2>)
            {
                _MovePoint(Value_.Start);
                _MovePoint(Value_.End);
            }
            else if constexpr (std::is_same_v<T, Circle2>
                || std::is_same_v<T, Ellipse2>
                || std::is_same_v<T, Clothoid2>)
            {
                _MovePoint(Value_.Placement.Location);
            }
            else if constexpr (std::is_same_v<T, Arc2>
                || std::is_same_v<T, EllipseArc2>)
            {
                _MovePoint(Value_.Basis.Placement.Location);
            }
            else if constexpr (std::is_same_v<T, Polyline2>)
            {
                for (auto& _Point : Value_.Points) _MovePoint(_Point);
            }
            else if constexpr (std::is_same_v<T, Bezier2>
                || std::is_same_v<T, BSpline2>
                || std::is_same_v<T, NURBS2>)
            {
                for (auto& _Point : Value_.Poles) _MovePoint(_Point);
            }
        }, Curve_);
    }

    iCAX::GeometryData::Tube::SRegionLoop2* _FindSectionLoop(
        IN OUT iCAX::GeometryData::Tube::CPlanarRegion2& Section_,
        IN const std::string& strLoopID_)
    {
        const auto _Iter = std::find_if(
            Section_.Boundaries.begin(),
            Section_.Boundaries.end(),
            [&strLoopID_](IN const auto& Loop_) {
                return Loop_.ID == strLoopID_;
            });
        return _Iter == Section_.Boundaries.end() ? nullptr : &*_Iter;
    }

    void _RebuildRectangleLoop(
        IN OUT iCAX::GeometryData::Tube::SRegionLoop2& Loop_,
        IN const iCAX::GeometryData::Tube::SSectionPrimitive& Primitive_)
    {
        using namespace iCAX::GeometryData;
        using namespace iCAX::GeometryData::Tube;
        if (!Primitive_.Width || !Primitive_.Height
            || Primitive_.Width->Value <= 0.0
            || Primitive_.Height->Value <= 0.0)
        {
            throw std::invalid_argument(
                "CADIntent rectangle width and height must be positive");
        }
        const auto _Cos = std::cos(Primitive_.RotationRadians);
        const auto _Sin = std::sin(Primitive_.RotationRadians);
        const auto _HalfWidth = 0.5 * Primitive_.Width->Value;
        const auto _HalfHeight = 0.5 * Primitive_.Height->Value;
        const auto _Point = [&](IN double X_, IN double Y_) {
            return Point2{
                Primitive_.Center.X + X_ * _Cos - Y_ * _Sin,
                Primitive_.Center.Y + X_ * _Sin + Y_ * _Cos
            };
        };
        std::array<Point2, 4> _Points = {
            _Point(-_HalfWidth, -_HalfHeight),
            _Point(_HalfWidth, -_HalfHeight),
            _Point(_HalfWidth, _HalfHeight),
            _Point(-_HalfWidth, _HalfHeight)
        };
        if (Primitive_.Role == ESectionPrimitiveRole::Cavity)
        {
            std::reverse(_Points.begin(), _Points.end());
        }
        const auto _LoopID = Loop_.ID;
        Loop_.Curves.clear();
        for (std::size_t _Index = 0; _Index < _Points.size(); ++_Index)
        {
            SRegionCurve2 _Curve;
            _Curve.ID = _LoopID + "/curve-" + std::to_string(_Index + 1);
            _Curve.Segment.Curve = Segment2{
                _Points[_Index],
                _Points[(_Index + 1) % _Points.size()]
            };
            _Curve.Segment.Range = { 0.0, 1.0, false };
            Loop_.Curves.push_back(std::move(_Curve));
        }
    }

    bool _ApplySectionPrimitiveParameters(
        IN OUT iCAX::GeometryData::Tube::CTubeNeutralGeometry& Geometry_,
        IN const std::string& strPrimitiveID_,
        IN const ObjectMap& Parameters_)
    {
        using namespace iCAX::GeometryData;
        using namespace iCAX::GeometryData::Tube;
        for (auto& _OwnerNode : Geometry_.SolidNodes)
        {
            auto _Apply = [&](IN OUT auto& Extrusion_) {
                const auto _Iter = std::find_if(
                    Extrusion_.SectionPrimitives.begin(),
                    Extrusion_.SectionPrimitives.end(),
                    [&strPrimitiveID_](IN const auto& Primitive_) {
                        return Primitive_.ID == strPrimitiveID_;
                    });
                if (_Iter == Extrusion_.SectionPrimitives.end()) return false;
                auto& _Primitive = *_Iter;
                auto* _pLoop = _FindSectionLoop(
                    Extrusion_.Section, _Primitive.LoopID);
                if (!_pLoop)
                {
                    throw std::runtime_error(
                        "CADIntent section primitive loop is missing: "
                        + _Primitive.LoopID);
                }
                const auto _OldCenter = _Primitive.Center;
                _Primitive.Center.X = _ParameterValue(
                    Parameters_, "centerX", _Primitive.Center.X);
                _Primitive.Center.Y = _ParameterValue(
                    Parameters_, "centerY", _Primitive.Center.Y);
                _Primitive.RotationRadians = _ParameterValue(
                    Parameters_, "rotationRadians", _Primitive.RotationRadians);
                if (_Primitive.Width)
                    _Primitive.Width->Value = _ParameterValue(
                        Parameters_, "width", _Primitive.Width->Value);
                if (_Primitive.Height)
                    _Primitive.Height->Value = _ParameterValue(
                        Parameters_, "height", _Primitive.Height->Value);
                if (_Primitive.Radius)
                    _Primitive.Radius->Value = _ParameterValue(
                        Parameters_, "radius", _Primitive.Radius->Value);

                if (_Primitive.Kind == ESectionPrimitiveKind::Rectangle)
                {
                    _RebuildRectangleLoop(*_pLoop, _Primitive);
                }
                else if (_Primitive.Kind == ESectionPrimitiveKind::Circle)
                {
                    if (!_Primitive.Radius || _Primitive.Radius->Value <= 0.0)
                    {
                        throw std::invalid_argument(
                            "CADIntent circle radius must be positive");
                    }
                    bool _Updated = false;
                    for (auto& _Curve : _pLoop->Curves)
                    {
                        if (auto* _pCircle = std::get_if<Circle2>(
                            &_Curve.Segment.Curve))
                        {
                            _pCircle->Placement.Location = _Primitive.Center;
                            _pCircle->Radius = _Primitive.Radius->Value;
                            _Updated = true;
                        }
                    }
                    if (!_Updated)
                    {
                        throw std::runtime_error(
                            "CADIntent circle primitive has no circular source curve");
                    }
                }
                else
                {
                    const auto _DeltaX = _Primitive.Center.X - _OldCenter.X;
                    const auto _DeltaY = _Primitive.Center.Y - _OldCenter.Y;
                    for (auto& _Curve : _pLoop->Curves)
                    {
                        _TranslateCurve2(_Curve.Segment.Curve, _DeltaX, _DeltaY);
                    }
                }
                _Primitive.Metadata["parameterEdited"] = "true";
                _Primitive.Metadata["requiresGeometricReevaluation"] = "true";
                _OwnerNode.Metadata["sectionParameterEdited"] = "true";
                return true;
            };
            if (auto* _pExtrusion = std::get_if<SExtrudedRegionNode>(
                &_OwnerNode.Data))
            {
                if (_Apply(*_pExtrusion)) return true;
            }
            else if (auto* _pTaper = std::get_if<STaperedRegionNode>(
                &_OwnerNode.Data))
            {
                if (_Apply(*_pTaper)) return true;
            }
        }
        return false;
    }

    void _ApplyNodeParameters(
        IN OUT iCAX::GeometryData::Tube::SSolidNode& Node_,
        IN const ObjectMap& Parameters_)
    {
        using namespace iCAX::GeometryData::Tube;
        if (auto _pExtrusion = std::get_if<SExtrudedRegionNode>(&Node_.Data))
        {
            _pExtrusion->First = _ParameterValue(Parameters_, "first", _pExtrusion->First);
            _pExtrusion->Last = _ParameterValue(Parameters_, "last", _pExtrusion->Last);
            if (_HasParameter(Parameters_, "length"))
            {
                const auto _Length = _ParameterValue(
                    Parameters_,
                    "length",
                    _pExtrusion->Last - _pExtrusion->First);
                if (_Length <= 0.0)
                {
                    throw std::invalid_argument("CADIntent extrusion length must be positive");
                }
                _pExtrusion->Last = _pExtrusion->First + _Length;
            }
            if (_pExtrusion->Last <= _pExtrusion->First)
            {
                throw std::invalid_argument("CADIntent extrusion last must be greater than first");
            }
            _pExtrusion->Frame.Location.X = _ParameterValue(
                Parameters_, "locationX", _pExtrusion->Frame.Location.X);
            _pExtrusion->Frame.Location.Y = _ParameterValue(
                Parameters_, "locationY", _pExtrusion->Frame.Location.Y);
            _pExtrusion->Frame.Location.Z = _ParameterValue(
                Parameters_, "locationZ", _pExtrusion->Frame.Location.Z);
        }
        else if (auto _pTaper = std::get_if<STaperedRegionNode>(&Node_.Data))
        {
            _pTaper->First = _ParameterValue(Parameters_, "first", _pTaper->First);
            _pTaper->Last = _ParameterValue(Parameters_, "last", _pTaper->Last);
            if (_HasParameter(Parameters_, "length"))
            {
                const auto _Length = _ParameterValue(
                    Parameters_, "length", _pTaper->Last - _pTaper->First);
                if (_Length <= 0.0)
                {
                    throw std::invalid_argument("CADIntent tapered length must be positive");
                }
                _pTaper->Last = _pTaper->First + _Length;
            }
            _pTaper->FirstScale = _ParameterValue(
                Parameters_, "firstScale", _pTaper->FirstScale);
            _pTaper->LastScale = _ParameterValue(
                Parameters_, "lastScale", _pTaper->LastScale);
            if (_pTaper->Last <= _pTaper->First
                || _pTaper->FirstScale <= 0.0
                || _pTaper->LastScale <= 0.0)
            {
                throw std::invalid_argument(
                    "CADIntent tapered interval and scales must be positive");
            }
            _pTaper->Frame.Location.X = _ParameterValue(
                Parameters_, "locationX", _pTaper->Frame.Location.X);
            _pTaper->Frame.Location.Y = _ParameterValue(
                Parameters_, "locationY", _pTaper->Frame.Location.Y);
            _pTaper->Frame.Location.Z = _ParameterValue(
                Parameters_, "locationZ", _pTaper->Frame.Location.Z);
        }
        else if (auto _pHalfSpace = std::get_if<SHalfSpaceNode>(&Node_.Data))
        {
            _pHalfSpace->Boundary.Location.X = _ParameterValue(
                Parameters_, "locationX", _pHalfSpace->Boundary.Location.X);
            _pHalfSpace->Boundary.Location.Y = _ParameterValue(
                Parameters_, "locationY", _pHalfSpace->Boundary.Location.Y);
            _pHalfSpace->Boundary.Location.Z = _ParameterValue(
                Parameters_, "locationZ", _pHalfSpace->Boundary.Location.Z);
            auto _NormalX = _ParameterValue(
                Parameters_, "normalX", _pHalfSpace->Boundary.Normal.X);
            auto _NormalY = _ParameterValue(
                Parameters_, "normalY", _pHalfSpace->Boundary.Normal.Y);
            auto _NormalZ = _ParameterValue(
                Parameters_, "normalZ", _pHalfSpace->Boundary.Normal.Z);
            const auto _Magnitude = std::sqrt(
                _NormalX * _NormalX + _NormalY * _NormalY + _NormalZ * _NormalZ);
            if (_Magnitude <= 1.0e-12)
            {
                throw std::invalid_argument("CADIntent half-space normal must be non-zero");
            }
            _pHalfSpace->Boundary.Normal = {
                _NormalX / _Magnitude,
                _NormalY / _Magnitude,
                _NormalZ / _Magnitude
            };
        }
        else if (auto _pWrapped = std::get_if<SWrappedVolumeNode>(&Node_.Data))
        {
            auto* _pLower = std::get_if<double>(&_pWrapped->LowerNormalOffset);
            auto* _pUpper = std::get_if<double>(&_pWrapped->UpperNormalOffset);
            if (!_pLower || !_pUpper)
            {
                throw std::invalid_argument(
                    "CADIntent grid-valued wrapped offsets require field editing");
            }
            *_pLower = _ParameterValue(Parameters_, "lowerNormalOffset", *_pLower);
            *_pUpper = _ParameterValue(Parameters_, "upperNormalOffset", *_pUpper);
            if (*_pLower > *_pUpper)
            {
                throw std::invalid_argument(
                    "CADIntent wrapped lower offset must not exceed upper offset");
            }
        }
        else if (auto _pOpening = std::get_if<SOpeningProfileSweepNode>(&Node_.Data))
        {
            if (_pOpening->ProfileField.Keys.empty()
                || _pOpening->ProfileField.Keys.front().Profile.Segments.empty())
            {
                throw std::invalid_argument("CADIntent opening profile has no editable segment");
            }
            auto& _Curve = _pOpening->ProfileField.Keys.front()
                .Profile.Segments.front().Curve;
            auto* _pSegment = std::get_if<iCAX::GeometryData::Segment2>(&_Curve);
            if (!_pSegment)
            {
                throw std::invalid_argument(
                    "CADIntent non-linear opening profiles require curve editing");
            }
            _pSegment->Start.X = _ParameterValue(
                Parameters_, "profileStart", _pSegment->Start.X);
            _pSegment->End.X = _ParameterValue(
                Parameters_, "profileEnd", _pSegment->End.X);
            _pSegment->Start.Y = _ParameterValue(
                Parameters_, "startOffset", _pSegment->Start.Y);
            _pSegment->End.Y = _ParameterValue(
                Parameters_, "endOffset", _pSegment->End.Y);
            if (std::abs(_pSegment->End.X - _pSegment->Start.X) <= 1.0e-12)
            {
                throw std::invalid_argument(
                    "CADIntent opening profile start and end must be distinct");
            }
            const auto _Angle = std::abs(std::atan2(
                _pSegment->End.Y - _pSegment->Start.Y,
                _pSegment->End.X - _pSegment->Start.X));
            _pOpening->ProfileField.Metadata["derivedSemiAngleRadians"] =
                std::to_string(_Angle);
        }
        else
        {
            throw std::invalid_argument(
                "CADIntent selected node has no directly editable scalar parameters");
        }
        Node_.Metadata["parameterEdited"] = "true";
        Node_.Metadata["requiresGeometricReevaluation"] = "true";
    }

    template<class TNodeData_>
    TNodeData_* _FindTubeNodeData(
        IN OUT iCAX::GeometryData::Tube::CTubeNeutralGeometry& Geometry_,
        IN const std::string& strNodeID_)
    {
        const auto _Iter = std::find_if(
            Geometry_.SolidNodes.begin(),
            Geometry_.SolidNodes.end(),
            [&strNodeID_](IN const auto& Node_) { return Node_.ID == strNodeID_; });
        return _Iter == Geometry_.SolidNodes.end()
            ? nullptr
            : std::get_if<TNodeData_>(&_Iter->Data);
    }

    iCAX::GeometryData::Circle2* _FindSingleSectionCircle(
        IN OUT iCAX::GeometryData::Tube::CPlanarRegion2& Section_)
    {
        if (Section_.Boundaries.size() != 1
            || Section_.Boundaries.front().Curves.size() != 1)
        {
            return nullptr;
        }
        return std::get_if<iCAX::GeometryData::Circle2>(
            &Section_.Boundaries.front().Curves.front().Segment.Curve);
    }

    void _SynchronizeOpeningProfileEvaluatedVolume(
        IN OUT iCAX::GeometryData::Tube::CTubeNeutralGeometry& Geometry_,
        IN const std::string& strOpeningNodeID_)
    {
        using namespace iCAX::GeometryData::Tube;
        auto* _pOpening = _FindTubeNodeData<SOpeningProfileSweepNode>(
            Geometry_, strOpeningNodeID_);
        if (!_pOpening) return;
        if (_pOpening->EvaluatedVolumeNodeID.empty())
        {
            throw std::invalid_argument(
                "CADIntent opening profile cannot be rebuilt because it has no evaluated volume");
        }
        auto* _pSource = _FindTubeNodeData<SExtrudedRegionNode>(
            Geometry_, _pOpening->SourceCutNodeID);
        auto* _pEvaluated = _FindTubeNodeData<SBooleanNode>(
            Geometry_, _pOpening->EvaluatedVolumeNodeID);
        if (!_pSource || !_pEvaluated
            || _pEvaluated->Operation != EBooleanOperation::Difference
            || _pEvaluated->Children.size() != 2)
        {
            throw std::invalid_argument(
                "CADIntent only rebuilds a profile whose evaluated volume is envelope minus nominal cut");
        }
        auto* _pEnvelope = _FindTubeNodeData<STaperedRegionNode>(
            Geometry_, _pEvaluated->Children.front());
        auto* _pSourceCircle = _FindSingleSectionCircle(_pSource->Section);
        if (!_pEnvelope || !_pSourceCircle
            || _pOpening->ProfileField.Keys.size() != 1
            || _pOpening->ProfileField.Keys.front().Profile.Segments.size() != 1)
        {
            throw std::invalid_argument(
                "CADIntent current profile evaluator requires one constant circular bevel profile");
        }
        const auto* _pProfile = std::get_if<iCAX::GeometryData::Segment2>(
            &_pOpening->ProfileField.Keys.front().Profile.Segments.front().Curve);
        if (!_pProfile)
        {
            throw std::invalid_argument(
                "CADIntent current profile evaluator requires a linear bevel profile");
        }
        const auto _Length = _pProfile->End.X - _pProfile->Start.X;
        const auto _FirstRadius = _pSourceCircle->Radius + _pProfile->Start.Y;
        const auto _LastRadius = _pSourceCircle->Radius + _pProfile->End.Y;
        if (_Length <= 0.0 || _FirstRadius <= 0.0 || _LastRadius <= 0.0)
        {
            throw std::invalid_argument(
                "CADIntent bevel profile must have increasing stations and positive envelope radii");
        }

        _pEnvelope->Section = _pSource->Section;
        auto* _pEnvelopeCircle = _FindSingleSectionCircle(_pEnvelope->Section);
        if (!_pEnvelopeCircle)
        {
            throw std::runtime_error(
                "CADIntent cannot construct the circular bevel envelope");
        }
        _pEnvelopeCircle->Radius = _FirstRadius;
        _pEnvelope->Frame = _pSource->Frame;
        _pEnvelope->First = _pSource->First + _pProfile->Start.X;
        _pEnvelope->Last = _pEnvelope->First + _Length;
        _pEnvelope->Reference = _pEnvelope->First;
        _pEnvelope->ScaleCenter = _pSourceCircle->Placement.Location;
        _pEnvelope->FirstScale = 1.0;
        _pEnvelope->LastScale = _LastRadius / _FirstRadius;
    }

    std::shared_ptr<iCAX::GeometryData::Tube::CTubeNeutralGeometry>
        _MakeTubeIntentWithParameters(
            IN const iCAX::GeometryData::Tube::CTubeNeutralGeometry& Geometry_,
            IN const std::string& strNodeID_,
            IN const ObjectMap& Parameters_)
    {
        auto _pEdited = std::make_shared<
            iCAX::GeometryData::Tube::CTubeNeutralGeometry>(Geometry_);
        const auto _NodeIter = std::find_if(
            _pEdited->SolidNodes.begin(),
            _pEdited->SolidNodes.end(),
            [&strNodeID_](IN const auto& Node_) {
                return Node_.ID == strNodeID_;
            });
        if (_NodeIter == _pEdited->SolidNodes.end())
        {
            if (!_ApplySectionPrimitiveParameters(
                *_pEdited,
                strNodeID_,
                Parameters_))
            {
                throw std::invalid_argument(
                    "CADIntent node was not found: " + strNodeID_);
            }
        }
        else
        {
            _ApplyNodeParameters(*_NodeIter, Parameters_);
            _SynchronizeOpeningProfileEvaluatedVolume(*_pEdited, strNodeID_);
        }
        return _pEdited;
    }

    std::string _FindTubeIntentEvaluationRoot(
        IN const iCAX::GeometryData::Tube::CTubeNeutralGeometry& Geometry_,
        IN const std::string& strSelectedID_)
    {
        using namespace iCAX::GeometryData::Tube;
        for (const auto& _Node : Geometry_.SolidNodes)
        {
            if (_Node.ID == strSelectedID_) return _Node.ID;
            const auto _ContainsPrimitive = [&strSelectedID_](IN const auto& Extrusion_) {
                return std::any_of(
                    Extrusion_.SectionPrimitives.begin(),
                    Extrusion_.SectionPrimitives.end(),
                    [&strSelectedID_](IN const auto& Primitive_) {
                        return Primitive_.ID == strSelectedID_;
                    });
            };
            if (const auto* _pExtrusion = std::get_if<SExtrudedRegionNode>(
                &_Node.Data))
            {
                if (_ContainsPrimitive(*_pExtrusion)) return _Node.ID;
            }
            else if (const auto* _pTaper = std::get_if<STaperedRegionNode>(
                &_Node.Data))
            {
                if (_ContainsPrimitive(*_pTaper)) return _Node.ID;
            }
        }
        throw std::invalid_argument(
            "CADIntent node was not found: " + strSelectedID_);
    }

    iCAX::OpenCascade::STubeCSGEvaluationOptions _MakeEvaluationOptions(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const iCAX::GeometryData::Tube::CTubeNeutralGeometry& Geometry_)
    {
        using namespace iCAX::GeometryData::Tube;
        iCAX::OpenCascade::STubeCSGEvaluationOptions _Options;
        _Options.Tolerance = 0.001;
        std::set<std::string> _ResourceIDs;
        for (const auto& _Node : Geometry_.SolidNodes)
        {
            if (const auto* _pComposite = std::get_if<SCompositeVolumeNode>(
                &_Node.Data))
            {
                if (!_pComposite->BRepResourceID.empty())
                    _ResourceIDs.insert(_pComposite->BRepResourceID);
            }
            else if (const auto* _pResidual = std::get_if<SResidualBRepNode>(
                &_Node.Data))
            {
                if (!_pResidual->BRepResourceID.empty())
                    _ResourceIDs.insert(_pResidual->BRepResourceID);
            }
        }
        for (const auto& _ResourceID : _ResourceIDs)
        {
            const auto _pBRep = Scene_.Resources().Get<
                iCAX::GeometryData::BRepModel>(_ResourceID);
            if (!_pBRep)
            {
                throw std::runtime_error(
                    "CADIntent referenced BRep resource is unavailable: "
                    + _ResourceID);
            }
            const auto _Rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(*_pBRep);
            if (!_Rebuilt.bOK)
            {
                std::ostringstream _Message;
                _Message << "CADIntent cannot rebuild referenced BRep: "
                    << _ResourceID;
                for (const auto& _Diagnostic : _Rebuilt.Diagnostics)
                    _Message << ": " << _Diagnostic;
                throw std::runtime_error(_Message.str());
            }
            _Options.ExternalBRepShapes.emplace(_ResourceID, _Rebuilt.Shape);
        }
        return _Options;
    }

    void _ClearPreviewMetadata(
        IN OUT iCAX::GeometryData::Tube::CTubeNeutralGeometry& Geometry_)
    {
        const auto _Clear = [](IN OUT auto& Metadata_) {
            Metadata_.erase("previewResourceUrl");
            Metadata_.erase("previewResourceVersion");
            Metadata_.erase("requiresGeometricReevaluation");
        };
        for (auto& _Node : Geometry_.SolidNodes)
        {
            _Clear(_Node.Metadata);
            const auto _ClearPrimitives = [&_Clear](IN OUT auto& Extrusion_) {
                for (auto& _Primitive : Extrusion_.SectionPrimitives)
                    _Clear(_Primitive.Metadata);
            };
            if (auto* _pExtrusion = std::get_if<
                iCAX::GeometryData::Tube::SExtrudedRegionNode>(&_Node.Data))
            {
                _ClearPrimitives(*_pExtrusion);
            }
            else if (auto* _pTaper = std::get_if<
                iCAX::GeometryData::Tube::STaperedRegionNode>(&_Node.Data))
            {
                _ClearPrimitives(*_pTaper);
            }
        }
    }

    std::map<std::string, std::string>* _FindPreviewMetadata(
        IN OUT iCAX::GeometryData::Tube::CTubeNeutralGeometry& Geometry_,
        IN const std::string& strID_)
    {
        for (auto& _Node : Geometry_.SolidNodes)
        {
            if (_Node.ID == strID_) return &_Node.Metadata;
            const auto _FindPrimitive = [&strID_](IN OUT auto& Extrusion_)
                -> std::map<std::string, std::string>* {
                const auto _Iter = std::find_if(
                    Extrusion_.SectionPrimitives.begin(),
                    Extrusion_.SectionPrimitives.end(),
                    [&strID_](IN const auto& Primitive_) {
                        return Primitive_.ID == strID_;
                    });
                return _Iter == Extrusion_.SectionPrimitives.end()
                    ? nullptr
                    : &_Iter->Metadata;
            };
            if (auto* _pExtrusion = std::get_if<
                iCAX::GeometryData::Tube::SExtrudedRegionNode>(&_Node.Data))
            {
                if (auto* _pMetadata = _FindPrimitive(*_pExtrusion))
                    return _pMetadata;
            }
            else if (auto* _pTaper = std::get_if<
                iCAX::GeometryData::Tube::STaperedRegionNode>(&_Node.Data))
            {
                if (auto* _pMetadata = _FindPrimitive(*_pTaper))
                    return _pMetadata;
            }
        }
        return nullptr;
    }

    TopoDS_Shape _MakeFinitePreviewShape(
        IN const iCAX::GeometryData::Tube::CTubeNeutralGeometry& Geometry_,
        IN const std::string& strNodeID_,
        IN const TopoDS_Shape& Shape_,
        IN const iCAX::OpenCascade::STubeCSGEvaluationResult& Evaluation_)
    {
        using iCAX::GeometryData::Tube::SHalfSpaceNode;
        const auto _Node = std::find_if(
            Geometry_.SolidNodes.begin(),
            Geometry_.SolidNodes.end(),
            [&strNodeID_](IN const auto& Node_) {
                return Node_.ID == strNodeID_;
            });
        if (_Node == Geometry_.SolidNodes.end()
            || !std::holds_alternative<SHalfSpaceNode>(_Node->Data))
        {
            return Shape_;
        }
        const auto _Base = Evaluation_.NodeShapes.find(Geometry_.BaseNodeID);
        if (_Base == Evaluation_.NodeShapes.end()) return {};
        BRepAlgoAPI_Common _Common(Shape_, _Base->second);
        _Common.SetFuzzyValue(0.001);
        _Common.Build();
        return _Common.IsDone() ? _Common.Shape() : TopoDS_Shape{};
    }

    void _CommitReevaluatedTubeIntent(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const std::string& strNeutralResourceID_,
        IN const std::shared_ptr<
            iCAX::GeometryData::Tube::CTubeNeutralGeometry>& pGeometry_)
    {
        auto [_pEntity, _pWorkpiece] = _GetActiveWorkpiece(Scene_.Database());
        if (!_pEntity || !_pWorkpiece)
            throw std::invalid_argument("CADIntent requires an active workpiece");

        const auto _Options = _MakeEvaluationOptions(Scene_, *pGeometry_);
        const auto _Evaluation = iCAX::OpenCascade::EvaluateTubeCSG(
            *pGeometry_, _Options);
        if (!_Evaluation.bOK)
        {
            std::ostringstream _Message;
            _Message << "CADIntent 参数无法生成有效结构";
            for (const auto& _Diagnostic : _Evaluation.Diagnostics)
                _Message << ": " << _Diagnostic;
            throw std::runtime_error(_Message.str());
        }

        auto& _Resources = Scene_.Resources();
        auto _SourceBRepID = pGeometry_->Metadata.contains("sourceBRepResourceId")
            ? pGeometry_->Metadata.at("sourceBRepResourceId")
            : _pWorkpiece->GetBRepResourceID();
        if (_SourceBRepID.empty()) _SourceBRepID = _pWorkpiece->GetGeometryResourceID();
        if (_SourceBRepID.empty())
            throw std::runtime_error("CADIntent cannot determine the source BRep resource");

        const auto _NeutralVersion = _NextResourceVersion(
            _Resources, strNeutralResourceID_);
        const auto _VersionSuffix = std::string("v")
            + std::to_string(_NeutralVersion);
        const auto _DisplayName = _pWorkpiece->GetName().empty()
            ? std::string("Tube CAD edit")
            : _pWorkpiece->GetName();
        const auto _EvaluatedBRepID = _StoreIntentBRep(
            Scene_,
            _SourceBRepID,
            "tube.csg.evaluated." + _VersionSuffix,
            _DisplayName + " evaluated " + _VersionSuffix,
            _Evaluation.Shape,
            _Options.Tolerance);
        const auto _pEvaluatedBRep = _Resources.Get<
            iCAX::GeometryData::BRepModel>(_EvaluatedBRepID);
        if (!_pEvaluatedBRep)
            throw std::runtime_error("CADIntent did not publish the evaluated BRep");
        const auto _FrontendGeometry =
            iCAX::RenderInteraction::EnsureFrontendGeometryResource(
                _Resources,
                _EvaluatedBRepID,
                iCAX::Render::ERenderGeometryKind::Mesh);

        const auto _TopologyID = iCAX::CAM::MakeTopologyResourceID(
            _Resources, _EvaluatedBRepID);
        const auto _TopologyVersion = _NextResourceVersion(
            _Resources, _TopologyID);
        auto _pTopology = _MakeTopologyResourceFromBRep(
            *_pEvaluatedBRep,
            _DisplayName,
            _TopologyVersion);
        _pTopology->Metadata["editState"] = std::string("Reevaluated");
        _pTopology->Metadata["brepResourceId"] = _EvaluatedBRepID;
        auto _TopologyInfo = _MakeResourceInfo(
            _TopologyID,
            _DisplayName + " evaluated topology",
            "topology",
            iCAX::Resource::EResourcePersistenceMode::Embedded,
            _TopologyVersion);
        _TopologyInfo.Metadata["brepResourceId"] = _EvaluatedBRepID;
        _TopologyInfo.Dependencies.push_back({
            _EvaluatedBRepID,
            _Resources.GetVersion(_EvaluatedBRepID)
        });
        _Resources.Set<iCAX::CAM::CTopologyResource>(
            _TopologyID, _pTopology, _TopologyInfo);

        _ClearPreviewMetadata(*pGeometry_);
        const auto _PublishPreviews = [&](IN const auto& Shapes_) {
            for (const auto& [_ID, _RawShape] : Shapes_)
            {
                const auto _Shape = _MakeFinitePreviewShape(
                    *pGeometry_, _ID, _RawShape, _Evaluation);
                if (_Shape.IsNull()) continue;
                const auto _PreviewBRepID = _StoreIntentBRep(
                    Scene_,
                    _EvaluatedBRepID,
                    "tube.csg.preview." + _ID,
                    _DisplayName + " construction " + _ID,
                    _Shape,
                    _Options.Tolerance);
                if (_PreviewBRepID.empty()) continue;
                const auto _Preview =
                    iCAX::RenderInteraction::EnsureFrontendGeometryResource(
                        _Resources,
                        _PreviewBRepID,
                        iCAX::Render::ERenderGeometryKind::Mesh);
                if (auto* _pMetadata = _FindPreviewMetadata(*pGeometry_, _ID))
                {
                    (*_pMetadata)["previewResourceUrl"] = _Preview.URL;
                    (*_pMetadata)["previewResourceVersion"] =
                        std::to_string(_Preview.nVersion);
                    (*_pMetadata)["previewKind"] = _ID == pGeometry_->BaseNodeID
                        ? "base"
                        : "construction-body";
                }
            }
        };
        _PublishPreviews(_Evaluation.NodeShapes);
        _PublishPreviews(_Evaluation.SectionPrimitiveShapes);
        pGeometry_->Metadata["evaluatedBRepResourceId"] = _EvaluatedBRepID;
        pGeometry_->Metadata["evaluationDiagnosticCount"] =
            std::to_string(_Evaluation.Diagnostics.size());
        _StoreEditedTubeIntentResource(
            Scene_, strNeutralResourceID_, pGeometry_, true);

        auto _pRender = _GetOrAddEntityComponent<
            iCAX::RenderInteraction::CRenderInstanceComponent>(_pEntity);
        auto _Undo = Scene_.Database().BeginUndoCommand(
            "Rebuild tube CAD intent geometry");
        _SetStringProperty(
            _pWorkpiece,
            iCAX::CAM::CWorkpieceComponent::PropertyName_GeometryResourceID,
            _EvaluatedBRepID);
        _SetStringProperty(
            _pWorkpiece,
            iCAX::CAM::CWorkpieceComponent::PropertyName_ModelResourceID,
            _EvaluatedBRepID);
        _SetStringProperty(
            _pWorkpiece,
            iCAX::CAM::CWorkpieceComponent::PropertyName_BRepResourceID,
            _EvaluatedBRepID);
        _SetStringProperty(
            _pWorkpiece,
            iCAX::CAM::CWorkpieceComponent::PropertyName_TopologyResourceID,
            _TopologyID);
        _SetUInt64Property(
            _pWorkpiece,
            iCAX::CAM::CWorkpieceComponent::PropertyName_TopologyVersion,
            _TopologyVersion);
        _SetUInt64Property(
            _pWorkpiece,
            iCAX::CAM::CWorkpieceComponent::PropertyName_GeometryRevision,
            _pWorkpiece->GetGeometryRevision() + 1ull);
        _SetStringProperty(
            _pWorkpiece,
            iCAX::CAM::CWorkpieceComponent::PropertyName_EditState,
            "Current");
        _SetStringProperty(
            _pRender,
            iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_GeometryResourceID,
            _FrontendGeometry.URL);
        const auto _Base = std::find_if(
            pGeometry_->SolidNodes.begin(),
            pGeometry_->SolidNodes.end(),
            [&pGeometry_](IN const auto& Node_) {
                return Node_.ID == pGeometry_->BaseNodeID;
            });
        if (_Base != pGeometry_->SolidNodes.end())
        {
            if (const auto* _pExtrusion = std::get_if<
                iCAX::GeometryData::Tube::SExtrudedRegionNode>(&_Base->Data))
            {
                _SetDoubleProperty(
                    _pWorkpiece,
                    iCAX::CAM::CWorkpieceComponent::PropertyName_Length,
                    _pExtrusion->Last - _pExtrusion->First);
            }
        }
        _Undo->End();
    }
}

Interaction::CInvocationResult HandleRecognizeCADIntent(
    IN const Interaction::CInvocation&,
    IN const Application::IApplicationContext&,
    IN Product::IProductContext*,
    IN Project::IProjectContext*,
    IN Project::ISceneContext* pScene_)
{
    auto& _Scene = _RequireSceneContext(pScene_);
    const auto _Intent = _RecognizeTubeIntentResource(_Scene);
    if (_Intent.Geometry->RecognitionStatus
        == iCAX::GeometryData::Tube::ERecognitionStatus::Failed)
    {
        throw std::runtime_error("CADIntent recognition did not produce an editable CSG");
    }
    return _MakeWorkpieceListResponse(_Scene);
}

Interaction::CInvocationResult HandleOpenCADIntentEditorScene(
    IN const Interaction::CInvocation& Request_,
    IN const Application::IApplicationContext& ApplicationContext_,
    IN Product::IProductContext* pProductContext_,
    IN Project::IProjectContext* pProjectContext_,
    IN Project::ISceneContext* pScene_)
{
    auto& _ParentScene = _RequireConcreteScene(_RequireSceneContext(pScene_));
    auto& _Product = _RequireProductContext(pProductContext_);
    auto& _ProjectContext = _RequireProjectContext(pProjectContext_);
    if (!_ParentScene.IsMainScene())
    {
        throw std::invalid_argument(
            "CADIntent.OpenEditorScene must be invoked on the main scene");
    }

    const auto _Payload = _DecodeObjectPayload(Request_);
    auto _WorkpieceIDText = _GetOptionalString(_Payload, "workpieceEntityId");
    auto [_pActiveEntity, _pActiveWorkpiece] = _GetActiveWorkpiece(
        _ParentScene.Database());
    auto _pSourceEntity = _pActiveEntity;
    auto _pSourceWorkpiece = _pActiveWorkpiece;
    if (!_WorkpieceIDText.empty())
    {
        const auto _WorkpieceID = _ParseRequiredUuid(
            _WorkpieceIDText, "workpieceEntityId");
        _pSourceEntity = _ParentScene.Database().GetEntity(_WorkpieceID);
        _pSourceWorkpiece = _GetComponent<iCAX::CAM::CWorkpieceComponent>(
            _pSourceEntity);
    }
    if (!_pSourceEntity || !_pSourceWorkpiece)
    {
        throw std::invalid_argument(
            "CADIntent.OpenEditorScene requires an existing workpiece");
    }
    if (_pSourceWorkpiece->GetSourcePath().empty())
    {
        throw std::invalid_argument(
            "CADIntent.OpenEditorScene requires a workpiece source path");
    }

    const auto _PartName = _pSourceWorkpiece->GetName().empty()
        ? _GetDisplayNameFromPath(_pSourceWorkpiece->GetSourcePath())
        : _pSourceWorkpiece->GetName();
    iCAX::Project::CProjectSceneCreateInfo _CreateInfo;
    _CreateInfo.SceneName = _PartName + "编辑视图";
    _CreateInfo.StartupComponent = iCAX::CAM::CCamSceneBootstrapComponent::S_ClassName;
    const auto& _ProductDefinition = _Product.GetDefinition();
    _CreateInfo.bEnablePDOHub = _ProductDefinition.bEnablePDOHub;
    _CreateInfo.PDOHubCreateInfo = _ProductDefinition.PDOHubCreateInfo;
    auto _pEditorScene = _ParentScene.OpenChildScene(std::move(_CreateInfo));

    try
    {
        const auto _Imported = _ImportCadModel(
            *_pEditorScene,
            _Product,
            _pSourceWorkpiece->GetSourcePath(),
            _GetOptionalDouble(_Payload, "tolerance", 0.001));

        ObjectMap _Instantiate;
        _Instantiate["sourcePath"] = _pSourceWorkpiece->GetSourcePath();
        _Instantiate["name"] = _PartName;
        _Instantiate["quantity"] = 1ull;
        _Instantiate["geometryResourceUrl"] = _Imported.BRepResourceID;
        _Instantiate["modelResourceId"] = _Imported.ModelResourceID;
        _Instantiate["brepResourceId"] = _Imported.BRepResourceID;
        _Instantiate["topologyResourceId"] = _Imported.TopologyResourceID;
        _Instantiate["topologyVersion"] =
            static_cast<unsigned long long>(_Imported.nTopologyVersion);
        _Instantiate["tubeNeutralGeometryResourceId"] =
            _Imported.TubeNeutralGeometryResourceID;
        _Instantiate["sectionTypeId"] = _Imported.SectionTypeID;
        _Instantiate["sectionParameters"] = _Imported.SectionParameters;
        _Instantiate["length"] = _Imported.dLength;
        const auto _InstantiateRequest = _MakeLocalInvocation(
            "Workpiece", "Instantiate", _Instantiate);
        const auto _InstantiateResponse = HandleInstantiateWorkpiece(
            _InstantiateRequest,
            ApplicationContext_,
            pProductContext_,
            &_ProjectContext,
            _pEditorScene.get());
        if (!_InstantiateResponse.IsOK())
        {
            throw std::runtime_error(
                _InstantiateResponse.strError.empty()
                    ? "Cannot instantiate workpiece in CAD editor scene"
                    : _InstantiateResponse.strError);
        }

        (void)_RecognizeTubeIntentResource(*_pEditorScene);
        _pEditorScene->Start();
    }
    catch (...)
    {
        (void)_ParentScene.Project().CloseScene(_pEditorScene->GetSceneID());
        throw;
    }

    ObjectMap _Result;
    _Result["editorScene"] = _MakeEditorScenePayload(*_pEditorScene);
    _Result["sourceWorkpieceEntityId"] = _pSourceEntity->GetID();
    _Result["workpieceName"] = _PartName;
    return _MakeResponse(Variant(_Result));
}

Interaction::CInvocationResult HandleCloseCADIntentEditorScene(
    IN const Interaction::CInvocation& Request_,
    IN const Application::IApplicationContext&,
    IN Product::IProductContext*,
    IN Project::IProjectContext*,
    IN Project::ISceneContext* pScene_)
{
    auto& _ParentScene = _RequireConcreteScene(_RequireSceneContext(pScene_));
    if (!_ParentScene.IsMainScene())
    {
        throw std::invalid_argument(
            "CADIntent.CloseEditorScene must be invoked on the main scene");
    }
    const auto _Payload = _DecodeObjectPayload(Request_);
    const auto _SceneIDText = _GetOptionalString(_Payload, "sceneId");
    if (_SceneIDText.empty())
    {
        throw std::invalid_argument(
            "CADIntent.CloseEditorScene requires sceneId");
    }
    const auto _SceneID = _ParseRequiredUuid(_SceneIDText, "sceneId");
    const auto _pTarget = _ParentScene.Project().GetScene(_SceneID);
    if (!_pTarget
        || _pTarget->GetParentSceneID() != _ParentScene.GetSceneID()
        || !_pTarget->IsTransientScene())
    {
        throw std::invalid_argument(
            "CADIntent.CloseEditorScene target is not a child editor scene");
    }
    const auto _Closed = _ParentScene.Project().CloseScene(_SceneID);
    ObjectMap _Result;
    _Result["closed"] = _Closed;
    _Result["sceneId"] = _SceneID;
    return _MakeResponse(Variant(_Result));
}

Interaction::CInvocationResult HandlePreviewCADIntentParameters(
    IN const Interaction::CInvocation& Request_,
    IN const Application::IApplicationContext&,
    IN Product::IProductContext*,
    IN Project::IProjectContext*,
    IN Project::ISceneContext* pScene_)
{
    using namespace iCAX::GeometryData::Tube;
    auto& _Scene = _RequireSceneContext(pScene_);
    const auto _Payload = _DecodeObjectPayload(Request_);
    const auto _NodeID = _GetOptionalString(_Payload, "nodeId");
    if (_NodeID.empty())
    {
        throw std::invalid_argument(
            "CADIntent.PreviewParameters requires nodeId");
    }
    const auto _ParameterIter = _Payload.find("parameters");
    if (_ParameterIter == _Payload.end()
        || !_ParameterIter->second.Is<ObjectMap>())
    {
        throw std::invalid_argument(
            "CADIntent.PreviewParameters requires parameters object");
    }

    auto _Intent = _RequireTubeIntentResource(_Scene);
    auto _pPreviewGeometry = _MakeTubeIntentWithParameters(
        *_Intent.Geometry,
        _NodeID,
        _ParameterIter->second.To<ObjectMap>());
    _pPreviewGeometry->RootNodeID = _FindTubeIntentEvaluationRoot(
        *_pPreviewGeometry,
        _NodeID);
    auto _Options = _MakeEvaluationOptions(_Scene, *_pPreviewGeometry);
    _Options.EvaluateAllConstructionBodies = false;
    auto _Evaluation = iCAX::OpenCascade::EvaluateTubeCSG(
        *_pPreviewGeometry,
        _Options);
    if (!_Evaluation.bOK)
    {
        std::ostringstream _Message;
        _Message << "CADIntent 临时参数无法生成刀具预览";
        for (const auto& _Diagnostic : _Evaluation.Diagnostics)
            _Message << ": " << _Diagnostic;
        throw std::runtime_error(_Message.str());
    }

    auto _ShapeIter = _Evaluation.NodeShapes.find(_NodeID);
    TopoDS_Shape _PreviewShape;
    if (_ShapeIter != _Evaluation.NodeShapes.end())
        _PreviewShape = _ShapeIter->second;
    else
    {
        const auto _PrimitiveShape =
            _Evaluation.SectionPrimitiveShapes.find(_NodeID);
        if (_PrimitiveShape != _Evaluation.SectionPrimitiveShapes.end())
            _PreviewShape = _PrimitiveShape->second;
    }
    if (_PreviewShape.IsNull())
    {
        throw std::runtime_error(
            "CADIntent selected construction body did not evaluate: "
            + _NodeID);
    }

    const auto _SelectedNode = std::find_if(
        _pPreviewGeometry->SolidNodes.begin(),
        _pPreviewGeometry->SolidNodes.end(),
        [&_NodeID](IN const auto& Node_) { return Node_.ID == _NodeID; });
    if (_SelectedNode != _pPreviewGeometry->SolidNodes.end()
        && std::holds_alternative<SHalfSpaceNode>(_SelectedNode->Data)
        && !_Evaluation.NodeShapes.contains(_pPreviewGeometry->BaseNodeID))
    {
        auto _BaseGeometry = *_pPreviewGeometry;
        _BaseGeometry.RootNodeID = _BaseGeometry.BaseNodeID;
        auto _BaseEvaluation = iCAX::OpenCascade::EvaluateTubeCSG(
            _BaseGeometry,
            _Options);
        if (_BaseEvaluation.bOK)
        {
            const auto _BaseShape = _BaseEvaluation.NodeShapes.find(
                _BaseGeometry.BaseNodeID);
            if (_BaseShape != _BaseEvaluation.NodeShapes.end())
                _Evaluation.NodeShapes.emplace(
                    _BaseGeometry.BaseNodeID,
                    _BaseShape->second);
        }
    }
    _PreviewShape = _MakeFinitePreviewShape(
        *_pPreviewGeometry,
        _NodeID,
        _PreviewShape,
        _Evaluation);
    if (_PreviewShape.IsNull())
    {
        throw std::runtime_error(
            "CADIntent selected construction body has no finite preview: "
            + _NodeID);
    }

    auto [_pEntity, _pWorkpiece] = _GetActiveWorkpiece(_Scene.Database());
    if (!_pEntity || !_pWorkpiece)
        throw std::invalid_argument("CADIntent requires an active workpiece");
    auto _SourceBRepID = _pWorkpiece->GetBRepResourceID();
    if (_SourceBRepID.empty())
        _SourceBRepID = _pWorkpiece->GetGeometryResourceID();
    if (_SourceBRepID.empty())
        throw std::runtime_error("CADIntent live preview has no source BRep");

    const auto _PreviewBRepID = _StoreIntentBRep(
        _Scene,
        _SourceBRepID,
        "tube.csg.live-preview." + _NodeID,
        (_pWorkpiece->GetName().empty()
            ? std::string("Tube CAD tool")
            : _pWorkpiece->GetName()) + " live preview " + _NodeID,
        _PreviewShape,
        _Options.Tolerance);
    const auto _Preview =
        iCAX::RenderInteraction::EnsureFrontendGeometryResource(
            _Scene.Resources(),
            _PreviewBRepID,
            iCAX::Render::ERenderGeometryKind::Mesh);

    ObjectMap _Result;
    _Result["nodeId"] = _NodeID;
    _Result["previewResourceUrl"] = _Preview.URL;
    _Result["previewResourceVersion"] = _Preview.nVersion;
    _Result["committed"] = false;
    return _MakeResponse(Variant(_Result));
}

Interaction::CInvocationResult HandleSetCADIntentParameters(
    IN const Interaction::CInvocation& Request_,
    IN const Application::IApplicationContext&,
    IN Product::IProductContext*,
    IN Project::IProjectContext*,
    IN Project::ISceneContext* pScene_)
{
    auto& _Scene = _RequireSceneContext(pScene_);
    const auto _Payload = _DecodeObjectPayload(Request_);
    const auto _NodeID = _GetOptionalString(_Payload, "nodeId");
    if (_NodeID.empty())
    {
        throw std::invalid_argument("CADIntent.SetParameters requires nodeId");
    }
    const auto _ParameterIter = _Payload.find("parameters");
    if (_ParameterIter == _Payload.end() || !_ParameterIter->second.Is<ObjectMap>())
    {
        throw std::invalid_argument("CADIntent.SetParameters requires parameters object");
    }
    auto _Intent = _RequireTubeIntentResource(_Scene);
    auto _pEdited = _MakeTubeIntentWithParameters(
        *_Intent.Geometry,
        _NodeID,
        _ParameterIter->second.To<ObjectMap>());
    _CommitReevaluatedTubeIntent(
        _Scene,
        _Intent.ResourceID,
        _pEdited);
    return _MakeWorkpieceListResponse(_Scene);
}

Interaction::CInvocationResult HandleAddCADIntentTool(
    IN const Interaction::CInvocation& Request_,
    IN const Application::IApplicationContext&,
    IN Product::IProductContext*,
    IN Project::IProjectContext*,
    IN Project::ISceneContext* pScene_)
{
    using namespace iCAX::GeometryData;
    using namespace iCAX::GeometryData::Tube;
    auto& _Scene = _RequireSceneContext(pScene_);
    if (_Scene.IsMainScene())
    {
        throw std::invalid_argument(
            "CADIntent.AddTool is only available in an editor scene");
    }
    const auto _Payload = _DecodeObjectPayload(Request_);
    const auto _Kind = _GetOptionalString(_Payload, "kind");
    if (_Kind.empty())
    {
        throw std::invalid_argument("CADIntent.AddTool requires kind");
    }
    auto _Intent = _RequireTubeIntentResource(_Scene);
    auto _pEdited = std::make_shared<CTubeNeutralGeometry>(*_Intent.Geometry);
    const auto _BaseIter = std::find_if(
        _pEdited->SolidNodes.begin(),
        _pEdited->SolidNodes.end(),
        [&_pEdited](IN const auto& Node_) {
            return Node_.ID == _pEdited->BaseNodeID;
        });
    if (_BaseIter == _pEdited->SolidNodes.end())
    {
        throw std::runtime_error("CADIntent.AddTool cannot find base node");
    }
    const auto* _pBase = std::get_if<SExtrudedRegionNode>(&_BaseIter->Data);
    if (!_pBase)
    {
        throw std::invalid_argument(
            "CADIntent.AddTool currently requires an extruded base");
    }

    const auto _NodeID = _NextBuiltInToolNodeID(*_pEdited);
    SSolidNode _Tool;
    _Tool.ID = _NodeID;
    _Tool.Metadata["source"] = "user-built-in-tool";
    _Tool.Metadata["builtInToolKind"] = _Kind;
    auto _Role = EFeatureMaterialRole::Penetration;

    if (_Kind == "circular-penetration"
        || _Kind == "rectangular-penetration")
    {
        const auto _Circular = _Kind == "circular-penetration";
        SExtrudedRegionNode _Extrusion;
        _Extrusion.Section = _MakeBuiltInToolSection(
            _Circular,
            _Circular ? 20.0 : 24.0,
            _Circular ? 20.0 : 16.0);
        _Extrusion.SectionPrimitives.push_back(
            _MakeBuiltInSectionPrimitive(
                _NodeID,
                _Circular,
                _Circular ? 20.0 : 24.0,
                _Circular ? 20.0 : 16.0));
        _Extrusion.Frame = _MakeDefaultToolFrame(*_pBase);
        const auto _Span = std::max(100.0, 2.0 * (_pBase->Last - _pBase->First));
        _Extrusion.First = -0.5 * _Span;
        _Extrusion.Last = 0.5 * _Span;
        _Tool.Label = _Circular ? "新增圆形贯切" : "新增矩形贯切";
        _Tool.Data = std::move(_Extrusion);
    }
    else if (_Kind == "tapered-penetration")
    {
        STaperedRegionNode _Taper;
        _Taper.Section = _MakeBuiltInToolSection(true, 20.0, 20.0);
        _Taper.SectionPrimitives.push_back(
            _MakeBuiltInSectionPrimitive(_NodeID, true, 20.0, 20.0));
        _Taper.Frame = _MakeDefaultToolFrame(*_pBase);
        const auto _Span = std::max(100.0, 2.0 * (_pBase->Last - _pBase->First));
        _Taper.First = -0.5 * _Span;
        _Taper.Last = 0.5 * _Span;
        _Taper.Reference = 0.0;
        _Taper.FirstScale = 0.65;
        _Taper.LastScale = 1.35;
        _Tool.Label = "新增锥形贯切";
        _Tool.Data = std::move(_Taper);
    }
    else if (_Kind == "half-space")
    {
        SHalfSpaceNode _HalfSpace;
        _HalfSpace.Boundary.Location = {
            _pBase->Frame.Location.X
                + _pBase->Last * _pBase->Frame.ZDirection.X,
            _pBase->Frame.Location.Y
                + _pBase->Last * _pBase->Frame.ZDirection.Y,
            _pBase->Frame.Location.Z
                + _pBase->Last * _pBase->Frame.ZDirection.Z
        };
        _HalfSpace.Boundary.Normal = _pBase->Frame.ZDirection;
        _HalfSpace.Boundary.XDirection = _pBase->Frame.XDirection;
        _HalfSpace.KeepNegativeSide = false;
        _Tool.Label = "新增平面截断";
        _Tool.Data = std::move(_HalfSpace);
        _Role = EFeatureMaterialRole::Truncation;
    }
    else if (_Kind == "wrapped-normal")
    {
        if (_pBase->SideAtlases.empty())
        {
            throw std::invalid_argument(
                "CADIntent.AddTool requires a base side atlas for wrapped-normal");
        }
        SWrappedVolumeNode _Wrapped;
        _Wrapped.Support.ExtrusionNodeID = _pEdited->BaseNodeID;
        _Wrapped.Support.LoopID = _pBase->SideAtlases.front().LoopID;
        _Wrapped.UVRegion = _MakeBuiltInToolSection(false, 24.0, 24.0);
        _Wrapped.LowerNormalOffset = -5.0;
        _Wrapped.UpperNormalOffset = 5.0;
        _Tool.Label = "新增 UV 法向截断";
        _Tool.Data = std::move(_Wrapped);
        _Role = EFeatureMaterialRole::Truncation;
    }
    else if (_Kind == "bevel")
    {
        SOpeningProfileSweepNode _Bevel;
        _Bevel.SourceCutNodeID = _pEdited->RootNodeID;
        _Bevel.ProfileField.ContourFirst = 0.0;
        _Bevel.ProfileField.ContourLast = 1.0;
        SOpeningProfileKey _Key;
        _Key.ContourParameter.Value = 0.0;
        CurveSegment2 _ProfileSegment;
        _ProfileSegment.Curve = Segment2{ { 0.0, 0.0 }, { 3.0, 2.0 } };
        _ProfileSegment.Range = { 0.0, 1.0, false };
        _Key.Profile.Segments.push_back(std::move(_ProfileSegment));
        _Key.Profile.Closed = false;
        _Key.Confidence = 1.0;
        _Bevel.ProfileField.Keys.push_back(std::move(_Key));
        _Bevel.ProfileField.Metadata["derivedSemiAngleRadians"] =
            std::to_string(std::atan2(2.0, 3.0));
        _Bevel.Observability = EParameterObservability::Observed;
        _Tool.Label = "新增局部坡口";
        _Tool.Data = std::move(_Bevel);
        _Role = EFeatureMaterialRole::BoundaryProfileModifier;
    }
    else
    {
        throw std::invalid_argument(
            "CADIntent.AddTool does not support kind: " + _Kind);
    }

    _Tool.Relations = _MakeBuiltInToolRelations(_Role);
    _pEdited->SolidNodes.push_back(std::move(_Tool));

    SSolidNode _Boolean;
    _Boolean.ID = _NodeID + "/result";
    _Boolean.Label = "编辑结果";
    SBooleanNode _Difference;
    _Difference.Operation = EBooleanOperation::Difference;
    _Difference.Children = { _pEdited->RootNodeID, _NodeID };
    _Boolean.Data = std::move(_Difference);
    _pEdited->SolidNodes.push_back(std::move(_Boolean));
    _pEdited->RootNodeID = _NodeID + "/result";
    _StoreEditedTubeIntentResource(_Scene, _Intent.ResourceID, _pEdited);

    auto _ListResponse = _MakeWorkpieceListResponse(_Scene);
    const std::string _Text(
        _ListResponse.Payload.begin(), _ListResponse.Payload.end());
    auto _ListPayload = iCAX::Data::VariantSerializer::Deserialize(_Text);
    auto _Result = _ListPayload.Is<ObjectMap>()
        ? _ListPayload.To<ObjectMap>()
        : ObjectMap{};
    _Result["addedNodeId"] = _NodeID;
    return _MakeResponse(Variant(_Result));
}

Interaction::CInvocationResult HandleSelectCADIntentInterpretation(
    IN const Interaction::CInvocation& Request_,
    IN const Application::IApplicationContext&,
    IN Product::IProductContext*,
    IN Project::IProjectContext*,
    IN Project::ISceneContext* pScene_)
{
    auto& _Scene = _RequireSceneContext(pScene_);
    const auto _Payload = _DecodeObjectPayload(Request_);
    const auto _NodeID = _GetOptionalString(_Payload, "nodeId");
    const auto _CandidateID = _GetOptionalString(_Payload, "candidateId");
    if (_NodeID.empty() || _CandidateID.empty())
    {
        throw std::invalid_argument(
            "CADIntent.SelectInterpretation requires nodeId and candidateId");
    }
    auto _Intent = _RequireTubeIntentResource(_Scene);
    auto _pEdited = std::make_shared<
        iCAX::GeometryData::Tube::CTubeNeutralGeometry>(*_Intent.Geometry);
    const auto _NodeIter = std::find_if(
        _pEdited->SolidNodes.begin(),
        _pEdited->SolidNodes.end(),
        [&_NodeID](IN const auto& Node_) { return Node_.ID == _NodeID; });
    if (_NodeIter == _pEdited->SolidNodes.end())
    {
        throw std::invalid_argument("CADIntent alternative node was not found: " + _NodeID);
    }
    auto* _pAlternative = std::get_if<
        iCAX::GeometryData::Tube::SAlternativeNode>(&_NodeIter->Data);
    if (!_pAlternative)
    {
        throw std::invalid_argument("CADIntent selected node is not an Alternative");
    }
    const auto _CandidateIter = std::find_if(
        _pAlternative->Candidates.begin(),
        _pAlternative->Candidates.end(),
        [&_CandidateID](IN const auto& Candidate_) {
            return Candidate_.ID == _CandidateID;
        });
    if (_CandidateIter == _pAlternative->Candidates.end())
    {
        throw std::invalid_argument(
            "CADIntent candidate was not found: " + _CandidateID);
    }
    _pAlternative->SelectedCandidateID = _CandidateID;
    _pAlternative->SelectionSource =
        iCAX::GeometryData::Tube::EAlternativeSelectionSource::User;
    _NodeIter->Metadata["interpretationSelected"] = "true";
    _StoreEditedTubeIntentResource(
        _Scene,
        _Intent.ResourceID,
        _pEdited);
    return _MakeWorkpieceListResponse(_Scene);
}
} // namespace iCAX::CAM::SDO
