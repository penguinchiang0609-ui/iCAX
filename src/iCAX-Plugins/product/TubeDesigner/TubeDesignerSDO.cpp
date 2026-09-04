#include "pch.h"

#include "SecurityWindow1Template.h"
#include "SecurityWindow2Template.h"
#include "SecurityWindow3Template.h"
#include "SingleFaceSecurityWindowTemplate.h"
#include "PartListXlsxExporter.h"
#include "FinalGeometryMeasurement.h"
#include "TubeDesignerComponents.h"

#include "ApplicationContext/IApplicationContext.h"
#include "Data/VariantSerializer.h"
#include "Database/IEntity.h"
#include "Database/IRepository.h"
#include "GeometryData/GeometryData.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepReader.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepBuilder.h"
#include "OpenCascadeResourceImport/OpenCascadeNeutralModelEvaluator.h"
#include "ProductContext/IProductContext.h"
#include "ProjectContext/IProjectContext.h"
#include "ProjectContext/ISceneContext.h"
#include "RenderData/RenderData.h"
#include "RenderInteraction/RenderInteraction.h"
#include "RenderInteraction/RenderInteractionComponents.h"
#include "Resources/ResourceInfo.h"
#include "Resources/ResourceLibrary.h"
#include "SDO/SDO.h"
#include "SDO/SDORegistrationCatalog.h"
#include "Transform/Transform.h"
#include "TemplateRuntime/PythonTemplateHost.h"
#include "TemplateRuntime/StandardJsonCodec.h"
#include "TemplateRuntime/TemplateCodec.h"

#ifdef M_PI
#undef M_PI
#endif
#ifdef M_PI_2
#undef M_PI_2
#endif

#include <array>
#include <fstream>
#include <mutex>
#include <numeric>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

namespace
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::PropertyValue;
    using iCAX::Data::Variant;
    using iCAX::Data::VariantArray;
    using namespace iCAX::TubeDesigner;
    constexpr double kPreviewRollRadians = 1.57079632679489661923;

    ObjectMap DecodeObjectPayload(const iCAX::Interaction::CInvocation& Request_)
    {
        if (Request_.Payload.empty()) return {};
        const std::string _Text(Request_.Payload.begin(), Request_.Payload.end());
        const auto _Payload = iCAX::Data::VariantSerializer::Deserialize(_Text);
        if (!_Payload.Is<ObjectMap>())
        {
            throw std::invalid_argument("TubeDesigner payload must be an object");
        }
        return _Payload.To<ObjectMap>();
    }

    iCAX::Interaction::CInvocationResult MakeResponse(const Variant& Payload_)
    {
        const auto _Text = iCAX::Data::VariantSerializer::Serialize(Payload_);
        iCAX::Interaction::CInvocationResult _Result;
        _Result.nStatus = iCAX::Interaction::EInvocationStatus::Ok;
        _Result.Payload.assign(_Text.begin(), _Text.end());
        return _Result;
    }

    void ReportExportProgress(
        const iCAX::Interaction::CInvocation& Request_,
        const std::string& Phase_,
        const std::uint64_t Completed_,
        const std::uint64_t Total_,
        const std::string& Message_)
    {
        ObjectMap _Report;
        _Report["kind"] = std::string("export");
        _Report["phase"] = Phase_;
        _Report["completed"] = static_cast<unsigned long long>(Completed_);
        _Report["total"] = static_cast<unsigned long long>(Total_);
        _Report["message"] = Message_;
        const auto _Text = iCAX::Data::VariantSerializer::Serialize(Variant(_Report));
        const iCAX::Interaction::SDOPayload _Payload(_Text.begin(), _Text.end());
        Request_.Report(_Payload);
    }

    std::string UuidToString(const iCAX::Data::uuid& ID_)
    {
        return ID_.is_nil() ? std::string() : iCAX::Data::to_string(ID_);
    }

    double ToDouble(const Variant& Value_, const std::string& Name_)
    {
        if (Value_.Is<double>()) return Value_.To<double>();
        if (Value_.Is<float>()) return static_cast<double>(Value_.To<float>());
        if (Value_.Is<unsigned long long>()) return static_cast<double>(Value_.To<unsigned long long>());
        if (Value_.Is<long long>()) return static_cast<double>(Value_.To<long long>());
        if (Value_.Is<unsigned int>()) return static_cast<double>(Value_.To<unsigned int>());
        if (Value_.Is<int>()) return static_cast<double>(Value_.To<int>());
        if (Value_.Is<std::string>()) return std::stod(Value_.To<std::string>());
        throw std::invalid_argument("TubeDesigner field must be numeric: " + Name_);
    }

    std::string GetString(const ObjectMap& Payload_, const std::string& Name_, const std::string& Default_ = {})
    {
        const auto _Iterator = Payload_.find(Name_);
        if (_Iterator == Payload_.end() || _Iterator->second.Is<std::monostate>()) return Default_;
        if (!_Iterator->second.Is<std::string>())
        {
            throw std::invalid_argument("TubeDesigner field must be a string: " + Name_);
        }
        return _Iterator->second.To<std::string>();
    }

    double GetDouble(const ObjectMap& Payload_, const std::string& Name_, double Default_)
    {
        const auto _Iterator = Payload_.find(Name_);
        return _Iterator == Payload_.end() || _Iterator->second.Is<std::monostate>()
            ? Default_
            : ToDouble(_Iterator->second, Name_);
    }

    std::uint64_t GetUInt64(const ObjectMap& Payload_, const std::string& Name_, std::uint64_t Default_)
    {
        const auto _Value = GetDouble(Payload_, Name_, static_cast<double>(Default_));
        if (!std::isfinite(_Value)
            || _Value < 0.0
            || std::floor(_Value) != _Value
            || _Value >= static_cast<double>(std::numeric_limits<std::uint64_t>::max()))
        {
            throw std::invalid_argument("TubeDesigner field must be a non-negative integer: " + Name_);
        }
        return static_cast<std::uint64_t>(_Value);
    }

    template <typename TComponent>
    std::shared_ptr<TComponent> GetComponent(const std::shared_ptr<iCAX::Database::IEntity>& Entity_)
    {
        return Entity_
            ? std::dynamic_pointer_cast<TComponent>(Entity_->GetComponent(TComponent::S_ClassName))
            : nullptr;
    }

    template <typename TComponent>
    std::vector<std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<TComponent>>> Collect(
        iCAX::Database::IRepository& Repository_)
    {
        std::vector<std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<TComponent>>> _Result;
        for (auto& _Entity : Repository_.FilterEntities([](const auto& Candidate_) {
            return Candidate_ && Candidate_->HasComponent(TComponent::S_ClassName);
        }))
        {
            if (auto _Component = GetComponent<TComponent>(_Entity))
            {
                _Result.emplace_back(_Entity, std::move(_Component));
            }
        }
        return _Result;
    }

    std::vector<iCAX::Data::uuid> CollectProductDesignIDs(
        iCAX::Database::IRepository& Repository_,
        const iCAX::Data::uuid& ProductID_)
    {
        std::vector<iCAX::Data::uuid> _IDs;
        if (const auto _Product = Repository_.GetEntity(ProductID_);
            GetComponent<CProductInstanceComponent>(_Product))
        {
            _IDs.push_back(ProductID_);
        }
        for (const auto& [_Entity, _Member] : Collect<CAssemblyMemberComponent>(Repository_))
        {
            if (_Member->GetProductID() == ProductID_) _IDs.push_back(_Entity->GetID());
        }
        for (const auto& [_Entity, _Part] : Collect<CManufacturingPartComponent>(Repository_))
        {
            if (_Part->GetProductID() == ProductID_) _IDs.push_back(_Entity->GetID());
        }
        for (const auto& [_Entity, _Joint] : Collect<CJointIntentComponent>(Repository_))
        {
            if (_Joint->GetProductID() == ProductID_) _IDs.push_back(_Entity->GetID());
        }
        for (const auto& [_Entity, _Run] : Collect<CGenerationRunComponent>(Repository_))
        {
            if (_Run->GetProductID() == ProductID_) _IDs.push_back(_Entity->GetID());
        }
        std::sort(_IDs.begin(), _IDs.end());
        _IDs.erase(std::unique(_IDs.begin(), _IDs.end()), _IDs.end());
        return _IDs;
    }

    iCAX::Data::uuid ParseRequiredUuid(const std::string& Value_, const std::string& Field_)
    {
        const auto _Parsed = iCAX::Data::uuid::from_string(Value_);
        if (!_Parsed || _Parsed->is_nil())
        {
            throw std::invalid_argument("TubeDesigner field must be a uuid: " + Field_);
        }
        return *_Parsed;
    }

    STubeProfile ReadProfile(const ObjectMap& Payload_, const std::string& Prefix_, const STubeProfile& Default_)
    {
        STubeProfile _Result = Default_;
        _Result.Type = GetString(Payload_, Prefix_ + "ProfileType", Default_.Type);
        _Result.Width = GetDouble(Payload_, Prefix_ + "Width", Default_.Width);
        _Result.Depth = GetDouble(Payload_, Prefix_ + "Depth", Default_.Depth);
        _Result.WallThickness = GetDouble(Payload_, Prefix_ + "WallThickness", Default_.WallThickness);
        _Result.CornerRadius = GetDouble(Payload_, Prefix_ + "CornerRadius", Default_.CornerRadius);
        return _Result;
    }

    SSecurityWindow1Parameters ReadParameters(const ObjectMap& Payload_)
    {
        SSecurityWindow1Parameters _Result;
        _Result.ProductCode = GetString(Payload_, "productCode", _Result.ProductCode);
        _Result.Height = GetDouble(Payload_, "height", _Result.Height);
        _Result.Width = GetDouble(Payload_, "width", _Result.Width);
        _Result.HorizontalCount = GetUInt64(Payload_, "horizontalCount", _Result.HorizontalCount);
        _Result.MiddleVerticalCount = GetUInt64(Payload_, "middleVerticalCount", _Result.MiddleVerticalCount);
        _Result.FirstHorizontalTopOffset = GetDouble(Payload_, "firstHorizontalTopOffset", _Result.FirstHorizontalTopOffset);
        _Result.LastHorizontalBottomOffset = GetDouble(Payload_, "lastHorizontalBottomOffset", _Result.LastHorizontalBottomOffset);
        _Result.HorizontalBranchReserve = GetDouble(Payload_, "horizontalBranchReserve", _Result.HorizontalBranchReserve);
        _Result.VerticalBranchReserve = GetDouble(Payload_, "verticalBranchReserve", _Result.VerticalBranchReserve);
        _Result.AssemblyClearance = GetDouble(Payload_, "assemblyClearance", _Result.AssemblyClearance);
        _Result.Frame = ReadProfile(Payload_, "frame", _Result.Frame);
        _Result.Vertical = ReadProfile(Payload_, "vertical", _Result.Vertical);
        _Result.Horizontal = ReadProfile(Payload_, "horizontal", _Result.Horizontal);
        return _Result;
    }

    ObjectMap MakeParameterMap(const SSecurityWindow1Parameters& Parameters_)
    {
        ObjectMap _Result;
        _Result["productCode"] = Parameters_.ProductCode;
        _Result["height"] = Parameters_.Height;
        _Result["width"] = Parameters_.Width;
        _Result["horizontalCount"] = static_cast<unsigned long long>(Parameters_.HorizontalCount);
        _Result["middleVerticalCount"] = static_cast<unsigned long long>(Parameters_.MiddleVerticalCount);
        _Result["firstHorizontalTopOffset"] = Parameters_.FirstHorizontalTopOffset;
        _Result["lastHorizontalBottomOffset"] = Parameters_.LastHorizontalBottomOffset;
        _Result["horizontalBranchReserve"] = Parameters_.HorizontalBranchReserve;
        _Result["verticalBranchReserve"] = Parameters_.VerticalBranchReserve;
        _Result["assemblyClearance"] = Parameters_.AssemblyClearance;
        _Result["frameProfileType"] = Parameters_.Frame.Type;
        _Result["frameWidth"] = Parameters_.Frame.Width;
        _Result["frameDepth"] = Parameters_.Frame.Depth;
        _Result["frameWallThickness"] = Parameters_.Frame.WallThickness;
        _Result["frameCornerRadius"] = Parameters_.Frame.CornerRadius;
        _Result["verticalProfileType"] = Parameters_.Vertical.Type;
        _Result["verticalWidth"] = Parameters_.Vertical.Width;
        _Result["verticalDepth"] = Parameters_.Vertical.Depth;
        _Result["verticalWallThickness"] = Parameters_.Vertical.WallThickness;
        _Result["verticalCornerRadius"] = Parameters_.Vertical.CornerRadius;
        _Result["horizontalProfileType"] = Parameters_.Horizontal.Type;
        _Result["horizontalWidth"] = Parameters_.Horizontal.Width;
        _Result["horizontalDepth"] = Parameters_.Horizontal.Depth;
        _Result["horizontalWallThickness"] = Parameters_.Horizontal.WallThickness;
        _Result["horizontalCornerRadius"] = Parameters_.Horizontal.CornerRadius;
        return _Result;
    }

    SSecurityWindow2Parameters ReadSecurityWindow2Parameters(const ObjectMap& Payload_)
    {
        SSecurityWindow2Parameters _Result;
        _Result.ProductCode = GetString(Payload_, "productCode", _Result.ProductCode);
        _Result.Height = GetDouble(Payload_, "height", _Result.Height);
        _Result.Width = GetDouble(Payload_, "width", _Result.Width);
        _Result.HandleBottom = GetDouble(Payload_, "handleBottom", _Result.HandleBottom);
        _Result.HandleHeight = GetDouble(Payload_, "handleHeight", _Result.HandleHeight);
        _Result.HandleWidth = GetDouble(Payload_, "handleWidth", _Result.HandleWidth);
        _Result.HandleHorizontalCount = GetUInt64(Payload_, "handleHorizontalCount", _Result.HandleHorizontalCount);
        _Result.TopHorizontalCount = GetUInt64(Payload_, "topHorizontalCount", _Result.TopHorizontalCount);
        _Result.BottomHorizontalCount = GetUInt64(Payload_, "bottomHorizontalCount", _Result.BottomHorizontalCount);
        _Result.HorizontalBranchReserve = GetDouble(Payload_, "horizontalBranchReserve", _Result.HorizontalBranchReserve);
        _Result.VerticalBranchReserve = GetDouble(Payload_, "verticalBranchReserve", _Result.VerticalBranchReserve);
        _Result.AssemblyClearance = GetDouble(Payload_, "assemblyClearance", _Result.AssemblyClearance);
        _Result.Frame = ReadProfile(Payload_, "frame", _Result.Frame);
        _Result.Vertical = ReadProfile(Payload_, "vertical", _Result.Vertical);
        _Result.Horizontal = ReadProfile(Payload_, "horizontal", _Result.Horizontal);
        return _Result;
    }

    ObjectMap MakeParameterMap(const SSecurityWindow2Parameters& Parameters_)
    {
        ObjectMap _Result;
        _Result["productCode"] = Parameters_.ProductCode;
        _Result["height"] = Parameters_.Height;
        _Result["width"] = Parameters_.Width;
        _Result["handleBottom"] = Parameters_.HandleBottom;
        _Result["handleHeight"] = Parameters_.HandleHeight;
        _Result["handleWidth"] = Parameters_.HandleWidth;
        _Result["handleHorizontalCount"] = static_cast<unsigned long long>(Parameters_.HandleHorizontalCount);
        _Result["topHorizontalCount"] = static_cast<unsigned long long>(Parameters_.TopHorizontalCount);
        _Result["bottomHorizontalCount"] = static_cast<unsigned long long>(Parameters_.BottomHorizontalCount);
        _Result["horizontalBranchReserve"] = Parameters_.HorizontalBranchReserve;
        _Result["verticalBranchReserve"] = Parameters_.VerticalBranchReserve;
        _Result["assemblyClearance"] = Parameters_.AssemblyClearance;
        _Result["frameProfileType"] = Parameters_.Frame.Type;
        _Result["frameWidth"] = Parameters_.Frame.Width;
        _Result["frameDepth"] = Parameters_.Frame.Depth;
        _Result["frameWallThickness"] = Parameters_.Frame.WallThickness;
        _Result["frameCornerRadius"] = Parameters_.Frame.CornerRadius;
        _Result["verticalProfileType"] = Parameters_.Vertical.Type;
        _Result["verticalWidth"] = Parameters_.Vertical.Width;
        _Result["verticalDepth"] = Parameters_.Vertical.Depth;
        _Result["verticalWallThickness"] = Parameters_.Vertical.WallThickness;
        _Result["verticalCornerRadius"] = Parameters_.Vertical.CornerRadius;
        _Result["horizontalProfileType"] = Parameters_.Horizontal.Type;
        _Result["horizontalWidth"] = Parameters_.Horizontal.Width;
        _Result["horizontalDepth"] = Parameters_.Horizontal.Depth;
        _Result["horizontalWallThickness"] = Parameters_.Horizontal.WallThickness;
        _Result["horizontalCornerRadius"] = Parameters_.Horizontal.CornerRadius;
        return _Result;
    }

    SSecurityWindow3Parameters ReadSecurityWindow3Parameters(const ObjectMap& Payload_)
    {
        SSecurityWindow3Parameters _Result;
        _Result.ProductCode = GetString(Payload_, "productCode", _Result.ProductCode);
        _Result.Height = GetDouble(Payload_, "height", _Result.Height);
        _Result.Width = GetDouble(Payload_, "width", _Result.Width);
        _Result.FrameJoinType = GetString(Payload_, "frameJoinType", _Result.FrameJoinType);
        _Result.FrameButtWrapMode = GetString(Payload_, "frameButtWrapMode", _Result.FrameButtWrapMode);
        _Result.VGrooveStyle = GetString(Payload_, "vGrooveStyle", _Result.VGrooveStyle);
        _Result.VGrooveBottomDistance = GetDouble(Payload_, "vGrooveBottomDistance", _Result.VGrooveBottomDistance);
        _Result.VGrooveRadius = GetDouble(Payload_, "vGrooveRadius", _Result.VGrooveRadius);
        _Result.VGrooveKFactor = GetDouble(Payload_, "vGrooveKFactor", _Result.VGrooveKFactor);
        _Result.VGrooveMaleFemale = GetString(Payload_, "vGrooveMaleFemale", "否") == "是";
        _Result.VGrooveBottomCut = GetString(Payload_, "vGrooveBottomCut", "否") == "是";
        _Result.VGrooveReliefHole = GetString(Payload_, "vGrooveReliefHole", "否") == "是";
        _Result.VGrooveWallOvercut = GetString(Payload_, "vGrooveWallOvercut", "否") == "是";
        _Result.VGrooveReliefDiameter = GetDouble(Payload_, "vGrooveReliefDiameter", _Result.VGrooveReliefDiameter);
        _Result.VGrooveReliefNoThrough = GetString(Payload_, "vGrooveReliefNoThrough", "否") == "是";
        _Result.HorizontalCount = GetUInt64(Payload_, "horizontalCount", _Result.HorizontalCount);
        _Result.MiddleVerticalCount = GetUInt64(Payload_, "middleVerticalCount", _Result.MiddleVerticalCount);
        _Result.FirstHorizontalTopOffset = GetDouble(Payload_, "firstHorizontalTopOffset", _Result.FirstHorizontalTopOffset);
        _Result.LastHorizontalBottomOffset = GetDouble(Payload_, "lastHorizontalBottomOffset", _Result.LastHorizontalBottomOffset);
        _Result.HorizontalBranchReserve = GetDouble(Payload_, "horizontalBranchReserve", _Result.HorizontalBranchReserve);
        _Result.VerticalBranchReserve = GetDouble(Payload_, "verticalBranchReserve", _Result.VerticalBranchReserve);
        _Result.AssemblyClearance = GetDouble(Payload_, "assemblyClearance", _Result.AssemblyClearance);
        _Result.Frame = ReadProfile(Payload_, "frame", _Result.Frame);
        _Result.Vertical = ReadProfile(Payload_, "vertical", _Result.Vertical);
        _Result.Horizontal = ReadProfile(Payload_, "horizontal", _Result.Horizontal);
        return _Result;
    }

    ObjectMap MakeParameterMap(const SSecurityWindow3Parameters& Parameters_)
    {
        ObjectMap _Result;
        _Result["productCode"] = Parameters_.ProductCode;
        _Result["height"] = Parameters_.Height;
        _Result["width"] = Parameters_.Width;
        _Result["frameJoinType"] = Parameters_.FrameJoinType;
        _Result["frameButtWrapMode"] = Parameters_.FrameButtWrapMode;
        _Result["vGrooveStyle"] = Parameters_.VGrooveStyle;
        _Result["vGrooveBottomDistance"] = Parameters_.VGrooveBottomDistance;
        _Result["vGrooveRadius"] = Parameters_.VGrooveRadius;
        _Result["vGrooveKFactor"] = Parameters_.VGrooveKFactor;
        _Result["vGrooveMaleFemale"] = std::string(Parameters_.VGrooveMaleFemale ? "是" : "否");
        _Result["vGrooveBottomCut"] = std::string(Parameters_.VGrooveBottomCut ? "是" : "否");
        _Result["vGrooveReliefHole"] = std::string(Parameters_.VGrooveReliefHole ? "是" : "否");
        _Result["vGrooveWallOvercut"] = std::string(Parameters_.VGrooveWallOvercut ? "是" : "否");
        _Result["vGrooveReliefDiameter"] = Parameters_.VGrooveReliefDiameter;
        _Result["vGrooveReliefNoThrough"] = std::string(Parameters_.VGrooveReliefNoThrough ? "是" : "否");
        _Result["horizontalCount"] = static_cast<unsigned long long>(Parameters_.HorizontalCount);
        _Result["middleVerticalCount"] = static_cast<unsigned long long>(Parameters_.MiddleVerticalCount);
        _Result["firstHorizontalTopOffset"] = Parameters_.FirstHorizontalTopOffset;
        _Result["lastHorizontalBottomOffset"] = Parameters_.LastHorizontalBottomOffset;
        _Result["horizontalBranchReserve"] = Parameters_.HorizontalBranchReserve;
        _Result["verticalBranchReserve"] = Parameters_.VerticalBranchReserve;
        _Result["assemblyClearance"] = Parameters_.AssemblyClearance;
        _Result["frameProfileType"] = Parameters_.Frame.Type;
        _Result["frameWidth"] = Parameters_.Frame.Width;
        _Result["frameDepth"] = Parameters_.Frame.Depth;
        _Result["frameWallThickness"] = Parameters_.Frame.WallThickness;
        _Result["frameCornerRadius"] = Parameters_.Frame.CornerRadius;
        _Result["verticalProfileType"] = Parameters_.Vertical.Type;
        _Result["verticalWidth"] = Parameters_.Vertical.Width;
        _Result["verticalDepth"] = Parameters_.Vertical.Depth;
        _Result["verticalWallThickness"] = Parameters_.Vertical.WallThickness;
        _Result["verticalCornerRadius"] = Parameters_.Vertical.CornerRadius;
        _Result["horizontalProfileType"] = Parameters_.Horizontal.Type;
        _Result["horizontalWidth"] = Parameters_.Horizontal.Width;
        _Result["horizontalDepth"] = Parameters_.Horizontal.Depth;
        _Result["horizontalWallThickness"] = Parameters_.Horizontal.WallThickness;
        _Result["horizontalCornerRadius"] = Parameters_.Horizontal.CornerRadius;
        return _Result;
    }

    void DecodeTubeCornerProcess(
        const ObjectMap& Payload_, const std::string& ParameterName_,
        std::string& JoinType_, std::string& VGrooveStyle_)
    {
        JoinType_ = GetString(Payload_, ParameterName_, JoinType_);
        constexpr std::string_view _VGroovePrefix = "v_groove_90:";
        if (JoinType_.starts_with(_VGroovePrefix))
        {
            VGrooveStyle_ = JoinType_.substr(_VGroovePrefix.size());
            JoinType_ = "v_groove_90";
        }
    }

    std::string EncodeTubeCornerProcess(
        const std::string& JoinType_, const std::string& VGrooveStyle_)
    {
        return JoinType_ == "v_groove_90"
            ? JoinType_ + ":" + VGrooveStyle_
            : JoinType_;
    }

    void ReadTubeCornerProcess(
        const ObjectMap& Payload_, const std::string& JoinParameterName_,
        const std::string& ButtWrapParameterName_, const std::string& VGrooveStyleParameterName_,
        STubeCornerProcessParameters& Process_)
    {
        Process_.ButtWrapMode = GetString(
            Payload_, ButtWrapParameterName_, Process_.ButtWrapMode);
        Process_.VGrooveStyle = GetString(
            Payload_, VGrooveStyleParameterName_, Process_.VGrooveStyle);
        DecodeTubeCornerProcess(
            Payload_, JoinParameterName_, Process_.JoinType, Process_.VGrooveStyle);
    }

    SSingleFaceSecurityWindowParameters ReadSingleFaceSecurityWindowParameters(
        const ObjectMap& Payload_)
    {
        SSingleFaceSecurityWindowParameters _Result;
        _Result.FrameLayout = GetString(Payload_, "frameLayout", _Result.FrameLayout);
        _Result.AccessDoorEnabled = GetString(Payload_, "accessDoorEnabled", "否") == "是";
        _Result.ProductCode = GetString(Payload_, "productCode", _Result.ProductCode);
        _Result.Height = GetDouble(Payload_, "height", _Result.Height);
        _Result.Width = GetDouble(Payload_, "width", _Result.Width);
        _Result.HorizontalCount = GetUInt64(Payload_, "horizontalCount", _Result.HorizontalCount);
        _Result.MiddleVerticalCount = GetUInt64(Payload_, "middleVerticalCount", _Result.MiddleVerticalCount);
        _Result.FirstHorizontalTopOffset = GetDouble(Payload_, "firstHorizontalTopOffset", _Result.FirstHorizontalTopOffset);
        _Result.LastHorizontalBottomOffset = GetDouble(Payload_, "lastHorizontalBottomOffset", _Result.LastHorizontalBottomOffset);
        _Result.DoorLeft = GetDouble(Payload_, "doorLeft", _Result.DoorLeft);
        _Result.DoorBottom = GetDouble(Payload_, "doorBottom", _Result.DoorBottom);
        _Result.DoorHeight = GetDouble(Payload_, "doorHeight", _Result.DoorHeight);
        _Result.DoorWidth = GetDouble(Payload_, "doorWidth", _Result.DoorWidth);
        _Result.DoorGap = GetDouble(Payload_, "doorGap", _Result.DoorGap);
        _Result.DoorHingeSide = GetString(Payload_, "doorHingeSide", _Result.DoorHingeSide);
        _Result.DoorHingeCount = GetUInt64(Payload_, "doorHingeCount", _Result.DoorHingeCount);
        _Result.DoorHorizontalCount = GetUInt64(Payload_, "doorHorizontalCount", _Result.DoorHorizontalCount);
        _Result.DoorVerticalCount = GetUInt64(Payload_, "doorVerticalCount", _Result.DoorVerticalCount);
        auto& _OuterProcess = _Result.OuterFrameProcess;
        ReadTubeCornerProcess(Payload_, "frameJoinType", "frameButtWrapMode",
            "vGrooveStyle", _OuterProcess);
        _OuterProcess.VGrooveBottomDistance = GetDouble(
            Payload_, "vGrooveBottomDistance", _OuterProcess.VGrooveBottomDistance);
        _OuterProcess.VGrooveRadius = GetDouble(
            Payload_, "vGrooveRadius", _OuterProcess.VGrooveRadius);
        _OuterProcess.VGrooveKFactor = GetDouble(
            Payload_, "vGrooveKFactor", _OuterProcess.VGrooveKFactor);
        _OuterProcess.VGrooveMaleFemale = GetString(Payload_, "vGrooveMaleFemale", "否") == "是";
        _OuterProcess.VGrooveBottomCut = GetString(Payload_, "vGrooveBottomCut", "否") == "是";
        _OuterProcess.VGrooveReliefHole = GetString(Payload_, "vGrooveReliefHole", "否") == "是";
        _OuterProcess.VGrooveWallOvercut = GetString(Payload_, "vGrooveWallOvercut", "否") == "是";
        _OuterProcess.VGrooveReliefDiameter = GetDouble(
            Payload_, "vGrooveReliefDiameter", _OuterProcess.VGrooveReliefDiameter);
        _OuterProcess.VGrooveReliefNoThrough = GetString(
            Payload_, "vGrooveReliefNoThrough", "否") == "是";

        // The current product exposes one set of detailed V-groove dimensions.
        // Each consumer still owns its process object, so later templates can
        // override per-corner details without changing the data model.
        _Result.DoorFrameProcess = _OuterProcess;
        ReadTubeCornerProcess(Payload_, "doorFrameJoinType", "doorFrameButtWrapMode",
            "doorFrameVGrooveStyle", _Result.DoorFrameProcess);
        _Result.DoorLeafFrameProcess = _OuterProcess;
        ReadTubeCornerProcess(Payload_, "doorLeafFrameJoinType", "doorLeafFrameButtWrapMode",
            "doorLeafFrameVGrooveStyle", _Result.DoorLeafFrameProcess);
        _Result.HorizontalBranchReserve = GetDouble(Payload_, "horizontalBranchReserve", _Result.HorizontalBranchReserve);
        _Result.VerticalBranchReserve = GetDouble(Payload_, "verticalBranchReserve", _Result.VerticalBranchReserve);
        _Result.AssemblyClearance = GetDouble(Payload_, "assemblyClearance", _Result.AssemblyClearance);
        _Result.Frame = ReadProfile(Payload_, "frame", _Result.Frame);
        _Result.Vertical = ReadProfile(Payload_, "vertical", _Result.Vertical);
        _Result.Horizontal = ReadProfile(Payload_, "horizontal", _Result.Horizontal);
        _Result.DoorFrame = ReadProfile(Payload_, "doorFrame", _Result.DoorFrame);
        _Result.DoorLeafFrame = ReadProfile(Payload_, "doorLeafFrame", _Result.DoorLeafFrame);
        _Result.DoorVertical = ReadProfile(Payload_, "doorVertical", _Result.DoorVertical);
        _Result.DoorHorizontal = ReadProfile(Payload_, "doorHorizontal", _Result.DoorHorizontal);
        return _Result;
    }

    ObjectMap MakeParameterMap(const SSingleFaceSecurityWindowParameters& Parameters_)
    {
        ObjectMap _Result;
        _Result["frameLayout"] = Parameters_.FrameLayout;
        _Result["accessDoorEnabled"] = std::string(Parameters_.AccessDoorEnabled ? "是" : "否");
        _Result["productCode"] = Parameters_.ProductCode;
        _Result["height"] = Parameters_.Height;
        _Result["width"] = Parameters_.Width;
        _Result["horizontalCount"] = static_cast<unsigned long long>(Parameters_.HorizontalCount);
        _Result["middleVerticalCount"] = static_cast<unsigned long long>(Parameters_.MiddleVerticalCount);
        _Result["firstHorizontalTopOffset"] = Parameters_.FirstHorizontalTopOffset;
        _Result["lastHorizontalBottomOffset"] = Parameters_.LastHorizontalBottomOffset;
        _Result["doorLeft"] = Parameters_.DoorLeft;
        _Result["doorBottom"] = Parameters_.DoorBottom;
        _Result["doorHeight"] = Parameters_.DoorHeight;
        _Result["doorWidth"] = Parameters_.DoorWidth;
        _Result["doorGap"] = Parameters_.DoorGap;
        _Result["doorHingeSide"] = Parameters_.DoorHingeSide;
        _Result["doorHingeCount"] = static_cast<unsigned long long>(Parameters_.DoorHingeCount);
        _Result["doorHorizontalCount"] = static_cast<unsigned long long>(Parameters_.DoorHorizontalCount);
        _Result["doorVerticalCount"] = static_cast<unsigned long long>(Parameters_.DoorVerticalCount);
        _Result["frameJoinType"] = EncodeTubeCornerProcess(
            Parameters_.OuterFrameProcess.JoinType, Parameters_.OuterFrameProcess.VGrooveStyle);
        _Result["frameButtWrapMode"] = Parameters_.OuterFrameProcess.ButtWrapMode;
        _Result["vGrooveStyle"] = Parameters_.OuterFrameProcess.VGrooveStyle;
        _Result["doorFrameJoinType"] = EncodeTubeCornerProcess(
            Parameters_.DoorFrameProcess.JoinType, Parameters_.DoorFrameProcess.VGrooveStyle);
        _Result["doorFrameButtWrapMode"] = Parameters_.DoorFrameProcess.ButtWrapMode;
        _Result["doorFrameVGrooveStyle"] = Parameters_.DoorFrameProcess.VGrooveStyle;
        _Result["doorLeafFrameJoinType"] = EncodeTubeCornerProcess(
            Parameters_.DoorLeafFrameProcess.JoinType, Parameters_.DoorLeafFrameProcess.VGrooveStyle);
        _Result["doorLeafFrameButtWrapMode"] = Parameters_.DoorLeafFrameProcess.ButtWrapMode;
        _Result["doorLeafFrameVGrooveStyle"] = Parameters_.DoorLeafFrameProcess.VGrooveStyle;
        _Result["vGrooveBottomDistance"] = Parameters_.OuterFrameProcess.VGrooveBottomDistance;
        _Result["vGrooveRadius"] = Parameters_.OuterFrameProcess.VGrooveRadius;
        _Result["vGrooveKFactor"] = Parameters_.OuterFrameProcess.VGrooveKFactor;
        _Result["vGrooveMaleFemale"] = std::string(Parameters_.OuterFrameProcess.VGrooveMaleFemale ? "是" : "否");
        _Result["vGrooveBottomCut"] = std::string(Parameters_.OuterFrameProcess.VGrooveBottomCut ? "是" : "否");
        _Result["vGrooveReliefHole"] = std::string(Parameters_.OuterFrameProcess.VGrooveReliefHole ? "是" : "否");
        _Result["vGrooveWallOvercut"] = std::string(Parameters_.OuterFrameProcess.VGrooveWallOvercut ? "是" : "否");
        _Result["vGrooveReliefDiameter"] = Parameters_.OuterFrameProcess.VGrooveReliefDiameter;
        _Result["vGrooveReliefNoThrough"] = std::string(Parameters_.OuterFrameProcess.VGrooveReliefNoThrough ? "是" : "否");
        _Result["horizontalBranchReserve"] = Parameters_.HorizontalBranchReserve;
        _Result["verticalBranchReserve"] = Parameters_.VerticalBranchReserve;
        _Result["assemblyClearance"] = Parameters_.AssemblyClearance;
        _Result["frameProfileType"] = Parameters_.Frame.Type;
        _Result["frameWidth"] = Parameters_.Frame.Width;
        _Result["frameDepth"] = Parameters_.Frame.Depth;
        _Result["frameWallThickness"] = Parameters_.Frame.WallThickness;
        _Result["frameCornerRadius"] = Parameters_.Frame.CornerRadius;
        _Result["verticalProfileType"] = Parameters_.Vertical.Type;
        _Result["verticalWidth"] = Parameters_.Vertical.Width;
        _Result["verticalDepth"] = Parameters_.Vertical.Depth;
        _Result["verticalWallThickness"] = Parameters_.Vertical.WallThickness;
        _Result["verticalCornerRadius"] = Parameters_.Vertical.CornerRadius;
        _Result["horizontalProfileType"] = Parameters_.Horizontal.Type;
        _Result["horizontalWidth"] = Parameters_.Horizontal.Width;
        _Result["horizontalDepth"] = Parameters_.Horizontal.Depth;
        _Result["horizontalWallThickness"] = Parameters_.Horizontal.WallThickness;
        _Result["horizontalCornerRadius"] = Parameters_.Horizontal.CornerRadius;
        _Result["doorFrameProfileType"] = Parameters_.DoorFrame.Type;
        _Result["doorFrameWidth"] = Parameters_.DoorFrame.Width;
        _Result["doorFrameDepth"] = Parameters_.DoorFrame.Depth;
        _Result["doorFrameWallThickness"] = Parameters_.DoorFrame.WallThickness;
        _Result["doorFrameCornerRadius"] = Parameters_.DoorFrame.CornerRadius;
        _Result["doorLeafFrameProfileType"] = Parameters_.DoorLeafFrame.Type;
        _Result["doorLeafFrameWidth"] = Parameters_.DoorLeafFrame.Width;
        _Result["doorLeafFrameDepth"] = Parameters_.DoorLeafFrame.Depth;
        _Result["doorLeafFrameWallThickness"] = Parameters_.DoorLeafFrame.WallThickness;
        _Result["doorLeafFrameCornerRadius"] = Parameters_.DoorLeafFrame.CornerRadius;
        _Result["doorVerticalProfileType"] = Parameters_.DoorVertical.Type;
        _Result["doorVerticalWidth"] = Parameters_.DoorVertical.Width;
        _Result["doorVerticalDepth"] = Parameters_.DoorVertical.Depth;
        _Result["doorVerticalWallThickness"] = Parameters_.DoorVertical.WallThickness;
        _Result["doorVerticalCornerRadius"] = Parameters_.DoorVertical.CornerRadius;
        _Result["doorHorizontalProfileType"] = Parameters_.DoorHorizontal.Type;
        _Result["doorHorizontalWidth"] = Parameters_.DoorHorizontal.Width;
        _Result["doorHorizontalDepth"] = Parameters_.DoorHorizontal.Depth;
        _Result["doorHorizontalWallThickness"] = Parameters_.DoorHorizontal.WallThickness;
        _Result["doorHorizontalCornerRadius"] = Parameters_.DoorHorizontal.CornerRadius;
        return _Result;
    }

    struct STemplateEvaluation final
    {
        SGeneratedProduct Product;
        ObjectMap Parameters;
    };

    struct SPythonTemplatePackage final
    {
        iCAX::TemplateRuntime::STemplateDescriptor Descriptor;
        std::filesystem::path DescriptorPath;
        std::filesystem::path ScriptPath;
    };

    struct SNeutralTemplateEvaluation final
    {
        iCAX::TemplateRuntime::STemplateDescriptor Descriptor;
        iCAX::TemplateRuntime::SNeutralModel Model;
        ObjectMap Document;
        ObjectMap Parameters;
    };

    std::string PathToUTF8(const std::filesystem::path& Path_)
    {
        const auto _Text = Path_.u8string();
        return { _Text.begin(), _Text.end() };
    }

    std::string ReadTextFile(const std::filesystem::path& Path_)
    {
        std::ifstream _Stream(Path_, std::ios::binary);
        if (!_Stream) throw std::runtime_error("failed to open template file: " + Path_.string());
        return { std::istreambuf_iterator<char>(_Stream), std::istreambuf_iterator<char>() };
    }

    std::string ContentDigest(const std::string& Descriptor_, const std::string& Script_)
    {
        std::uint64_t _Hash = 14695981039346656037ull;
        const auto _Consume = [&_Hash](const std::string& Text_) {
            for (const auto _Byte : Text_)
            {
                _Hash ^= static_cast<unsigned char>(_Byte);
                _Hash *= 1099511628211ull;
            }
        };
        _Consume(Descriptor_);
        _Hash ^= 0xffu;
        _Hash *= 1099511628211ull;
        _Consume(Script_);
        std::ostringstream _Result;
        _Result << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << _Hash;
        return _Result.str();
    }

    void AppendAncestors(
        std::vector<std::filesystem::path>& Roots_, const std::filesystem::path& Start_)
    {
        if (Start_.empty()) return;
        std::error_code _Error;
        auto _Path = std::filesystem::absolute(Start_, _Error);
        if (_Error) _Path = Start_;
        for (; !_Path.empty(); _Path = _Path.parent_path())
        {
            if (std::find(Roots_.begin(), Roots_.end(), _Path) == Roots_.end())
                Roots_.push_back(_Path);
            if (_Path == _Path.parent_path()) break;
        }
    }

    std::vector<std::filesystem::path> RuntimeSearchRoots(
        const iCAX::Application::IApplicationContext& ApplicationContext_)
    {
        std::vector<std::filesystem::path> _Roots;
        if (!ApplicationContext_.GetPaths().InstallDirectory.empty())
            AppendAncestors(_Roots, ApplicationContext_.GetPaths().InstallDirectory);
        AppendAncestors(_Roots, std::filesystem::current_path());
        AppendAncestors(_Roots, std::filesystem::path(__FILE__).parent_path());
        return _Roots;
    }

    std::filesystem::path ResolveRuntimeFile(
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        const std::vector<std::filesystem::path>& RelativeCandidates_,
        const std::string& strDescription_)
    {
        std::error_code _Error;
        for (const auto& _Root : RuntimeSearchRoots(ApplicationContext_))
        {
            for (const auto& _Relative : RelativeCandidates_)
            {
                const auto _Candidate = _Root / _Relative;
                if (std::filesystem::is_regular_file(_Candidate, _Error))
                    return std::filesystem::weakly_canonical(_Candidate, _Error);
                _Error.clear();
            }
        }
        throw std::runtime_error(strDescription_ + " was not found in the application installation");
    }

    std::filesystem::path EnvironmentPath(const wchar_t* pName_)
    {
        const auto _Size = GetEnvironmentVariableW(pName_, nullptr, 0);
        if (_Size == 0) return {};
        std::wstring _Value(_Size, L'\0');
        const auto _Written = GetEnvironmentVariableW(pName_, _Value.data(), _Size);
        if (_Written == 0 || _Written >= _Size) return {};
        _Value.resize(_Written);
        return std::filesystem::path(_Value);
    }

    struct SPythonTemplateRegistration final
    {
        const char* ID;
        const char* Directory;
    };

    constexpr std::array<SPythonTemplateRegistration, 4> kPythonTemplates{
        SPythonTemplateRegistration{ "single-face-security-window", "single_face_security_window" },
        SPythonTemplateRegistration{ "two-face-security-window", "two_face_security_window" },
        SPythonTemplateRegistration{ "three-face-security-window", "three_face_security_window" },
        SPythonTemplateRegistration{ "five-face-security-window", "five_face_security_window" }
    };

    const SPythonTemplateRegistration* FindPythonTemplateRegistration(const std::string& ID_)
    {
        const auto _Iterator = std::find_if(
            kPythonTemplates.begin(), kPythonTemplates.end(),
            [&ID_](const auto& Registration_) { return ID_ == Registration_.ID; });
        return _Iterator == kPythonTemplates.end() ? nullptr : &*_Iterator;
    }

    bool IsPythonTemplate(const std::string& ID_)
    {
        return FindPythonTemplateRegistration(ID_) != nullptr;
    }

    SPythonTemplatePackage LoadPythonTemplatePackage(
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        const std::string& TemplateID_)
    {
        const auto* _Registration = FindPythonTemplateRegistration(TemplateID_);
        if (!_Registration)
            throw std::invalid_argument("unsupported Python template: " + TemplateID_);
        const auto _RelativeDescriptor = std::filesystem::path("apps/tube-designer/templates")
            / _Registration->Directory / "template.json";
        const auto _SourceDescriptor = std::filesystem::path("src") / _RelativeDescriptor;
        const auto _DescriptorPath = ResolveRuntimeFile(ApplicationContext_, {
            _RelativeDescriptor, _SourceDescriptor
        }, "TubeDesigner Python template descriptor");
        const auto _ScriptPath = _DescriptorPath.parent_path() / "template.py";
        if (!std::filesystem::is_regular_file(_ScriptPath))
            throw std::runtime_error("TubeDesigner Python template script was not found");
        const auto _DescriptorText = ReadTextFile(_DescriptorPath);
        const auto _ScriptText = ReadTextFile(_ScriptPath);
        auto _Descriptor = iCAX::TemplateRuntime::CTemplateCodec::ParseDescriptor(
            iCAX::TemplateRuntime::CStandardJsonCodec::Parse(_DescriptorText));
        if (_Descriptor.ID != TemplateID_)
            throw std::runtime_error("TubeDesigner Python template descriptor ID does not match its registration");
        std::string _PackageScriptText = _ScriptText;
        if (TemplateID_ != "single-face-security-window")
        {
            const auto _SharedScriptPath = _DescriptorPath.parent_path().parent_path()
                / "_shared" / "multi_face_security_window.py";
            if (!std::filesystem::is_regular_file(_SharedScriptPath))
                throw std::runtime_error("TubeDesigner multi-face shared template script was not found");
            _PackageScriptText += "\n\xffshared-script\xff\n" + ReadTextFile(_SharedScriptPath);
        }
        _Descriptor.PackageDigest = ContentDigest(_DescriptorText, _PackageScriptText);
        return { std::move(_Descriptor), _DescriptorPath, _ScriptPath };
    }

    std::filesystem::path ResolvePythonExecutable(
        const iCAX::Application::IApplicationContext& ApplicationContext_)
    {
        const auto _Configured = EnvironmentPath(L"ICAX_PYTHON_EXECUTABLE");
        if (!_Configured.empty())
        {
            if (!std::filesystem::is_regular_file(_Configured))
                throw std::runtime_error("ICAX_PYTHON_EXECUTABLE does not point to a file");
            return std::filesystem::weakly_canonical(_Configured);
        }
        return ResolveRuntimeFile(ApplicationContext_, {
            "runtime/python/python.exe",
            "python/python.exe",
            "src/runtime/python/python.exe"
        }, "iCAX Python runtime (set ICAX_PYTHON_EXECUTABLE to configure it)");
    }

    ObjectMap InvokePythonTemplate(
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        const ObjectMap& Request_)
    {
        static std::mutex _Mutex;
        static std::unique_ptr<iCAX::TemplateRuntime::CPythonTemplateHost> _Host;
        static std::filesystem::path _Python;
        static std::filesystem::path _Worker;
        std::scoped_lock _Lock(_Mutex);
        const auto _ResolvedPython = ResolvePythonExecutable(ApplicationContext_);
        const auto _ResolvedWorker = ResolveRuntimeFile(ApplicationContext_, {
            "iCAX-Engine/framework/TemplateRuntime/python/icax_template_worker.py",
            "src/iCAX-Engine/framework/TemplateRuntime/python/icax_template_worker.py",
            "TemplateRuntime/python/icax_template_worker.py"
        }, "iCAX Python template worker");
        if (!_Host || _ResolvedPython != _Python || _ResolvedWorker != _Worker)
        {
            _Host = std::make_unique<iCAX::TemplateRuntime::CPythonTemplateHost>(
                iCAX::TemplateRuntime::SPythonTemplateHostOptions{
                    _ResolvedPython, _ResolvedWorker, _ResolvedWorker.parent_path()
                });
            _Python = _ResolvedPython;
            _Worker = _ResolvedWorker;
        }
        return _Host->Invoke(Request_);
    }

    SNeutralTemplateEvaluation EvaluateNeutralTemplate(
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        const ObjectMap& Payload_)
    {
        const auto _RequestedTemplateID = GetString(Payload_, "templateId", "single-face-security-window");
        auto _Package = LoadPythonTemplatePackage(ApplicationContext_, _RequestedTemplateID);
        const auto _TemplateID = GetString(Payload_, "templateId", _Package.Descriptor.ID);
        const auto _TemplateVersion = GetString(Payload_, "templateVersion", _Package.Descriptor.Version);
        if (_TemplateID != _Package.Descriptor.ID || _TemplateVersion != _Package.Descriptor.Version)
            throw std::invalid_argument("requested Python template identity does not match its descriptor");

        ObjectMap _Values;
        for (const auto& _Definition : _Package.Descriptor.Parameters)
        {
            if (const auto _Iterator = Payload_.find(_Definition.Key); _Iterator != Payload_.end())
                _Values.emplace(_Definition.Key, _Iterator->second);
        }
        auto _Parameters = iCAX::TemplateRuntime::CTemplateCodec::ValidateAndNormalizeParameters(
            _Package.Descriptor, _Values);
        const auto _Request = iCAX::TemplateRuntime::CTemplateCodec::MakeEvaluationRequest(
            _Package.Descriptor, _Parameters, PathToUTF8(_Package.ScriptPath));
        auto _Document = InvokePythonTemplate(ApplicationContext_, _Request);
        auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(Variant(_Document));
        if (_Model.TemplateID != _Package.Descriptor.ID
            || _Model.TemplateVersion != _Package.Descriptor.Version
            || _Model.PackageDigest != _Package.Descriptor.PackageDigest)
        {
            throw std::runtime_error("Python template returned a model with a mismatched identity");
        }
        if (_Model.Parameters != _Parameters)
            throw std::runtime_error("Python template changed the normalized parameter values");
        return {
            std::move(_Package.Descriptor), std::move(_Model),
            std::move(_Document), std::move(_Parameters)
        };
    }

    STemplateEvaluation EvaluateTemplate(const ObjectMap& Payload_)
    {
        const auto _TemplateID = GetString(Payload_, "templateId", "security-window-1");
        const auto _TemplateVersion = GetString(Payload_, "templateVersion", "1.0.0");
        if (_TemplateVersion != "1.0.0")
        {
            throw std::invalid_argument("unsupported TubeDesigner template version: " + _TemplateVersion);
        }
        if (_TemplateID == "security-window-1")
        {
            const auto _Parameters = ReadParameters(Payload_);
            return { GenerateSecurityWindow1(_Parameters), MakeParameterMap(_Parameters) };
        }
        if (_TemplateID == "security-window-2")
        {
            const auto _Parameters = ReadSecurityWindow2Parameters(Payload_);
            return { GenerateSecurityWindow2(_Parameters), MakeParameterMap(_Parameters) };
        }
        if (_TemplateID == "security-window-3")
        {
            const auto _Parameters = ReadSecurityWindow3Parameters(Payload_);
            return { GenerateSecurityWindow3(_Parameters), MakeParameterMap(_Parameters) };
        }
        throw std::invalid_argument("unsupported TubeDesigner template: " + _TemplateID);
    }

    ObjectMap ParameterField(
        const std::string& Name_, const std::string& Label_, const std::string& Group_,
        const Variant& Default_, const std::string& Type_ = "number",
        const Variant& Minimum_ = Variant(), const Variant& Maximum_ = Variant())
    {
        ObjectMap _Field;
        _Field["name"] = Name_;
        _Field["label"] = Label_;
        _Field["group"] = Group_;
        _Field["type"] = Type_;
        _Field["defaultValue"] = Default_;
        if (!Minimum_.Is<std::monostate>()) _Field["min"] = Minimum_;
        if (!Maximum_.Is<std::monostate>()) _Field["max"] = Maximum_;
        return _Field;
    }

    ObjectMap ChoiceField(
        const std::string& Name_, const std::string& Label_, const std::string& Group_,
        const std::string& Default_,
        const std::vector<std::pair<std::string, std::string>>& Choices_)
    {
        auto _Field = ParameterField(Name_, Label_, Group_, Default_, "select");
        VariantArray _Options;
        for (const auto& [_Value, _Label] : Choices_)
        {
            ObjectMap _Option;
            _Option["value"] = _Value;
            _Option["label"] = _Label;
            _Options.emplace_back(_Option);
        }
        _Field["options"] = _Options;
        return _Field;
    }

    ObjectMap VisibleWhen(
        ObjectMap Field_, const std::vector<std::pair<std::string, Variant>>& Conditions_)
    {
        VariantArray _Conditions;
        for (const auto& [_Name, _Value] : Conditions_)
        {
            ObjectMap _Condition;
            _Condition["name"] = _Name;
            _Condition["value"] = _Value;
            _Conditions.emplace_back(_Condition);
        }
        if (_Conditions.size() == 1)
        {
            Field_["visibleWhen"] = _Conditions.front();
        }
        else
        {
            ObjectMap _All;
            _All["all"] = _Conditions;
            Field_["visibleWhen"] = _All;
        }
        return Field_;
    }

    ObjectMap VisibleWhenAllAny(
        ObjectMap Field_,
        const std::vector<std::pair<std::string, Variant>>& Required_,
        const std::vector<std::pair<std::string, Variant>>& Alternatives_)
    {
        VariantArray _All;
        for (const auto& [_Name, _Value] : Required_)
        {
            ObjectMap _Condition;
            _Condition["name"] = _Name;
            _Condition["value"] = _Value;
            _All.emplace_back(_Condition);
        }
        VariantArray _AnyConditions;
        for (const auto& [_Name, _Value] : Alternatives_)
        {
            ObjectMap _Condition;
            _Condition["name"] = _Name;
            _Condition["value"] = _Value;
            _AnyConditions.emplace_back(_Condition);
        }
        ObjectMap _Any;
        _Any["any"] = _AnyConditions;
        _All.emplace_back(_Any);
        ObjectMap _Visibility;
        _Visibility["all"] = _All;
        Field_["visibleWhen"] = _Visibility;
        return Field_;
    }

    ObjectMap VisibleWhenAllAnyGroups(
        ObjectMap Field_,
        const std::vector<std::vector<std::pair<std::string, Variant>>>& Groups_)
    {
        VariantArray _All;
        for (const auto& _Group : Groups_)
        {
            VariantArray _AnyConditions;
            for (const auto& [_Name, _Value] : _Group)
            {
                ObjectMap _Condition;
                _Condition["name"] = _Name;
                _Condition["value"] = _Value;
                _AnyConditions.emplace_back(_Condition);
            }
            ObjectMap _Any;
            _Any["any"] = _AnyConditions;
            _All.emplace_back(_Any);
        }
        ObjectMap _Visibility;
        _Visibility["all"] = _All;
        Field_["visibleWhen"] = _Visibility;
        return Field_;
    }

    ObjectMap VisibleWhenAnyAll(
        ObjectMap Field_,
        const std::vector<std::vector<std::pair<std::string, Variant>>>& Groups_)
    {
        VariantArray _Any;
        for (const auto& _Group : Groups_)
        {
            VariantArray _AllConditions;
            for (const auto& [_Name, _Value] : _Group)
            {
                ObjectMap _Condition;
                _Condition["name"] = _Name;
                _Condition["value"] = _Value;
                _AllConditions.emplace_back(_Condition);
            }
            ObjectMap _All;
            _All["all"] = _AllConditions;
            _Any.emplace_back(_All);
        }
        ObjectMap _Visibility;
        _Visibility["any"] = _Any;
        Field_["visibleWhen"] = _Visibility;
        return Field_;
    }

    VariantArray CommonParameterSchema(const std::string& DefaultProductCode_)
    {
        VariantArray _Fields;
        _Fields.emplace_back(ParameterField("productCode", "产品编号", "基本尺寸", DefaultProductCode_, "text"));
        _Fields.emplace_back(ParameterField("height", "产品高度 L (mm)", "基本尺寸", 1800.0, "number", 1.0));
        _Fields.emplace_back(ParameterField("width", "产品宽度 D (mm)", "基本尺寸", 1200.0, "number", 1.0));
        _Fields.emplace_back(ParameterField("frameProfileType", "截面类型", "外框管", std::string("rect"), "readonly"));
        _Fields.emplace_back(ParameterField("frameWidth", "截面宽度 (mm)", "外框管", 50.0, "number", 1.0));
        _Fields.emplace_back(ParameterField("frameDepth", "截面深度 (mm)", "外框管", 50.0, "number", 1.0));
        _Fields.emplace_back(ParameterField("frameWallThickness", "壁厚 (mm)", "外框管", 2.0, "number", 0.1));
        _Fields.emplace_back(ParameterField("verticalProfileType", "截面类型", "竖管", std::string("rect"), "readonly"));
        _Fields.emplace_back(ParameterField("verticalWidth", "截面宽度 (mm)", "竖管", 25.0, "number", 1.0));
        _Fields.emplace_back(ParameterField("verticalDepth", "截面深度 (mm)", "竖管", 25.0, "number", 1.0));
        _Fields.emplace_back(ParameterField("verticalWallThickness", "壁厚 (mm)", "竖管", 1.5, "number", 0.1));
        _Fields.emplace_back(ParameterField("horizontalProfileType", "截面类型", "横管", std::string("rect"), "readonly"));
        _Fields.emplace_back(ParameterField("horizontalWidth", "截面宽度 (mm)", "横管", 35.0, "number", 1.0));
        _Fields.emplace_back(ParameterField("horizontalDepth", "截面深度 (mm)", "横管", 35.0, "number", 1.0));
        _Fields.emplace_back(ParameterField("horizontalWallThickness", "壁厚 (mm)", "横管", 1.5, "number", 0.1));
        _Fields.emplace_back(ParameterField("horizontalBranchReserve", "横支管安装预留 (mm)", "装配", -1.0));
        _Fields.emplace_back(ParameterField("verticalBranchReserve", "竖支管安装预留 (mm)", "装配", -1.0));
        _Fields.emplace_back(ParameterField("assemblyClearance", "开孔装配间隙 (mm)", "装配", 0.1, "number", 0.0));
        return _Fields;
    }

    VariantArray SingleFaceParameterSchema()
    {
        VariantArray _Fields;
        _Fields.emplace_back(ChoiceField("frameLayout", "外框布置", "结构", "left_right", {
            { "left_right", "仅左右边框" },
            { "top_bottom", "仅上下边框" },
            { "four_sides", "四边边框" }
        }));
        _Fields.emplace_back(ChoiceField("accessDoorEnabled", "检修门", "结构", "否", {
            { "否", "无检修门" }, { "是", "带矩形检修门" }
        }));
        _Fields.emplace_back(ParameterField("productCode", "产品编号", "基本尺寸", std::string("TD-SF-001"), "text"));
        _Fields.emplace_back(ParameterField("height", "产品高度 L (mm)", "基本尺寸", 1800.0, "number", 1.0));
        _Fields.emplace_back(ParameterField("width", "产品宽度 D (mm)", "基本尺寸", 1200.0, "number", 1.0));
        _Fields.emplace_back(ParameterField("horizontalCount", "主横杆数量", "主格栅", 4ull, "integer", 0ull, 100ull));
        _Fields.emplace_back(ParameterField("middleVerticalCount", "主竖杆数量", "主格栅", 1ull, "integer", 0ull, 100ull));
        _Fields.emplace_back(ParameterField("firstHorizontalTopOffset", "首横杆距顶部 (mm)", "主格栅", 200.0, "number", 0.0));
        _Fields.emplace_back(ParameterField("lastHorizontalBottomOffset", "末横杆距底部 (mm)", "主格栅", 200.0, "number", 0.0));

        const std::vector<std::pair<std::string, Variant>> _DoorEnabled{
            { "accessDoorEnabled", Variant(std::string("是")) }
        };
        _Fields.emplace_back(VisibleWhen(ParameterField("doorLeft", "门框左边距 (mm)", "检修门位置", 450.0, "number", 0.0), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ParameterField("doorBottom", "门框底边距 (mm)", "检修门位置", 700.0, "number", 0.0), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ParameterField("doorWidth", "门框宽度 (mm)", "检修门位置", 300.0, "number", 1.0), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ParameterField("doorHeight", "门框高度 (mm)", "检修门位置", 360.0, "number", 1.0), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ParameterField("doorGap", "门扇与门框间隙 (mm)", "检修门装配", 6.0, "number", 0.0), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ChoiceField("doorHingeSide", "铰链侧", "检修门装配", "left", {
            { "left", "左侧" }, { "right", "右侧" }
        }), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ParameterField("doorHingeCount", "铰链数量", "检修门装配", 2ull, "integer", 1ull, 10ull), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ParameterField("doorHorizontalCount", "门内横杆数量", "门扇格栅", 1ull, "integer", 0ull, 100ull), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ParameterField("doorVerticalCount", "门内竖杆数量", "门扇格栅", 1ull, "integer", 0ull, 100ull), _DoorEnabled));

        const std::vector<std::pair<std::string, std::string>> _TubeCornerProcesses{
            { "v_groove_90:sharp_v", "连续折弯 · 尖角 V 槽" },
            { "v_groove_90:rounded_v", "连续折弯 · 圆角 V 槽" },
            { "v_groove_90:left_arc", "连续折弯 · 左圆弧 V 槽" },
            { "v_groove_90:right_arc", "连续折弯 · 右圆弧 V 槽" },
            { "miter_45", "45° 斜拼（四根管）" },
            { "butt_90", "90° 直拼（四根管）" }
        };
        _Fields.emplace_back(VisibleWhen(ChoiceField(
            "frameJoinType", "大外框连接工艺", "大外框工艺",
            "v_groove_90:sharp_v", _TubeCornerProcesses), {
                { "frameLayout", Variant(std::string("four_sides")) }
            }));
        _Fields.emplace_back(VisibleWhen(ChoiceField(
            "frameButtWrapMode", "直拼包边方向", "大外框工艺", "side_wraps_horizontal", {
            { "side_wraps_horizontal", "左右框包上下框" },
            { "horizontal_wraps_side", "上下框包左右框" }
        }), {
            { "frameLayout", Variant(std::string("four_sides")) },
            { "frameJoinType", Variant(std::string("butt_90")) }
        }));
        _Fields.emplace_back(VisibleWhen(ChoiceField(
            "doorFrameJoinType", "固定门框连接工艺", "固定门框工艺",
            "v_groove_90:sharp_v", _TubeCornerProcesses), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ChoiceField(
            "doorFrameButtWrapMode", "直拼包边方向", "固定门框工艺", "side_wraps_horizontal", {
                { "side_wraps_horizontal", "左右框包上下框" },
                { "horizontal_wraps_side", "上下框包左右框" }
            }), {
                { "accessDoorEnabled", Variant(std::string("是")) },
                { "doorFrameJoinType", Variant(std::string("butt_90")) }
            }));
        _Fields.emplace_back(VisibleWhen(ChoiceField(
            "doorLeafFrameJoinType", "活动门扇框连接工艺", "活动门扇框工艺",
            "v_groove_90:sharp_v", _TubeCornerProcesses), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ChoiceField(
            "doorLeafFrameButtWrapMode", "直拼包边方向", "活动门扇框工艺", "side_wraps_horizontal", {
                { "side_wraps_horizontal", "左右框包上下框" },
                { "horizontal_wraps_side", "上下框包左右框" }
            }), {
                { "accessDoorEnabled", Variant(std::string("是")) },
                { "doorLeafFrameJoinType", Variant(std::string("butt_90")) }
            }));

        const std::array<std::string, 4> _VGrooveProcessValues{
            "v_groove_90:sharp_v", "v_groove_90:rounded_v",
            "v_groove_90:left_arc", "v_groove_90:right_arc"
        };
        std::vector<std::vector<std::pair<std::string, Variant>>> _AnyVGroove;
        for (const auto& _Process : _VGrooveProcessValues)
        {
            _AnyVGroove.push_back({
                { "frameLayout", Variant(std::string("four_sides")) },
                { "frameJoinType", Variant(_Process) }
            });
            _AnyVGroove.push_back({
                { "accessDoorEnabled", Variant(std::string("是")) },
                { "doorFrameJoinType", Variant(_Process) }
            });
            _AnyVGroove.push_back({
                { "accessDoorEnabled", Variant(std::string("是")) },
                { "doorLeafFrameJoinType", Variant(_Process) }
            });
        }
        const std::vector<std::vector<std::pair<std::string, Variant>>> _AnyRoundedVGroove{
            {
                { "frameLayout", Variant(std::string("four_sides")) },
                { "frameJoinType", Variant(std::string("v_groove_90:rounded_v")) }
            },
            {
                { "accessDoorEnabled", Variant(std::string("是")) },
                { "doorFrameJoinType", Variant(std::string("v_groove_90:rounded_v")) }
            },
            {
                { "accessDoorEnabled", Variant(std::string("是")) },
                { "doorLeafFrameJoinType", Variant(std::string("v_groove_90:rounded_v")) }
            }
        };
        const std::vector<std::vector<std::pair<std::string, Variant>>> _AnySharpVGroove{
            {
                { "frameLayout", Variant(std::string("four_sides")) },
                { "frameJoinType", Variant(std::string("v_groove_90:sharp_v")) }
            },
            {
                { "accessDoorEnabled", Variant(std::string("是")) },
                { "doorFrameJoinType", Variant(std::string("v_groove_90:sharp_v")) }
            },
            {
                { "accessDoorEnabled", Variant(std::string("是")) },
                { "doorLeafFrameJoinType", Variant(std::string("v_groove_90:sharp_v")) }
            }
        };
        _Fields.emplace_back(VisibleWhenAnyAll(
            ParameterField("vGrooveBottomDistance", "槽底距外侧底面 (mm)", "管材转角 V 槽参数", 2.0, "number", 0.0),
            _AnyVGroove));
        _Fields.emplace_back(VisibleWhenAnyAll(
            ParameterField("vGrooveRadius", "圆角 V 槽 R 半径 (mm)", "管材转角 V 槽参数", 18.0, "number", 0.0),
            _AnyRoundedVGroove));
        _Fields.emplace_back(VisibleWhenAnyAll(
            ParameterField("vGrooveKFactor", "展开 K 因子", "管材转角 V 槽参数", 0.62, "number", 0.0, 1.0),
            _AnyVGroove));
        _Fields.emplace_back(VisibleWhenAnyAll(
            ChoiceField("vGrooveMaleFemale", "斜切公母", "管材转角 V 槽附加工艺", "否", { { "否", "否" }, { "是", "是" } }),
            _AnyVGroove));
        _Fields.emplace_back(VisibleWhenAnyAll(
            ChoiceField("vGrooveBottomCut", "底部切除", "管材转角 V 槽附加工艺", "否", { { "否", "否" }, { "是", "是" } }),
            _AnySharpVGroove));
        _Fields.emplace_back(VisibleWhenAnyAll(
            ChoiceField("vGrooveReliefHole", "释放孔", "管材转角 V 槽附加工艺", "否", { { "否", "否" }, { "是", "是" } }),
            _AnySharpVGroove));
        _Fields.emplace_back(VisibleWhenAnyAll(
            ChoiceField("vGrooveWallOvercut", "壁厚过切", "管材转角 V 槽附加工艺", "否", { { "否", "否" }, { "是", "是" } }),
            _AnySharpVGroove));
        auto _AnySharpVGrooveWithRelief = _AnySharpVGroove;
        for (auto& _Group : _AnySharpVGrooveWithRelief)
            _Group.emplace_back("vGrooveReliefHole", Variant(std::string("是")));
        _Fields.emplace_back(VisibleWhenAnyAll(
            ParameterField("vGrooveReliefDiameter", "释放孔直径 (mm)", "管材转角 V 槽附加工艺", 10.0, "number", 0.0),
            _AnySharpVGrooveWithRelief));
        _Fields.emplace_back(VisibleWhenAnyAll(
            ChoiceField("vGrooveReliefNoThrough", "释放孔不过切", "管材转角 V 槽附加工艺", "否", { { "否", "否" }, { "是", "是" } }),
            _AnySharpVGrooveWithRelief));

        _Fields.emplace_back(ParameterField("frameProfileType", "截面类型", "大外框矩形管", std::string("rect"), "readonly"));
        _Fields.emplace_back(ParameterField("frameWidth", "截面宽度 (mm)", "大外框矩形管", 50.0, "number", 1.0));
        _Fields.emplace_back(ParameterField("frameDepth", "截面深度 (mm)", "大外框矩形管", 50.0, "number", 1.0));
        _Fields.emplace_back(ParameterField("frameCornerRadius", "外圆角 R (mm)", "大外框矩形管", 5.0, "number", 0.0));
        _Fields.emplace_back(ParameterField("frameWallThickness", "壁厚 (mm)", "大外框矩形管", 2.0, "number", 0.1));
        _Fields.emplace_back(ParameterField("horizontalProfileType", "截面类型", "主横杆矩形管", std::string("rect"), "readonly"));
        _Fields.emplace_back(ParameterField("horizontalWidth", "截面宽度 (mm)", "主横杆矩形管", 35.0, "number", 1.0));
        _Fields.emplace_back(ParameterField("horizontalDepth", "截面深度 (mm)", "主横杆矩形管", 35.0, "number", 1.0));
        _Fields.emplace_back(ParameterField("horizontalCornerRadius", "外圆角 R (mm)", "主横杆矩形管", 3.5, "number", 0.0));
        _Fields.emplace_back(ParameterField("horizontalWallThickness", "壁厚 (mm)", "主横杆矩形管", 1.5, "number", 0.1));
        _Fields.emplace_back(ParameterField("verticalProfileType", "截面类型", "主竖杆圆管", std::string("round"), "readonly"));
        _Fields.emplace_back(ParameterField("verticalWidth", "外径 (mm)", "主竖杆圆管", 25.0, "number", 1.0));
        _Fields.emplace_back(ParameterField("verticalWallThickness", "壁厚 (mm)", "主竖杆圆管", 1.5, "number", 0.1));

        _Fields.emplace_back(VisibleWhen(ParameterField("doorFrameProfileType", "截面类型", "固定门框矩形管", std::string("rect"), "readonly"), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ParameterField("doorFrameWidth", "截面宽度 (mm)", "固定门框矩形管", 35.0, "number", 1.0), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ParameterField("doorFrameDepth", "截面深度 (mm)", "固定门框矩形管", 35.0, "number", 1.0), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ParameterField("doorFrameCornerRadius", "外圆角 R (mm)", "固定门框矩形管", 3.5, "number", 0.0), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ParameterField("doorFrameWallThickness", "壁厚 (mm)", "固定门框矩形管", 1.5, "number", 0.1), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ParameterField("doorLeafFrameProfileType", "截面类型", "活动门扇框矩形管", std::string("rect"), "readonly"), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ParameterField("doorLeafFrameWidth", "截面宽度 (mm)", "活动门扇框矩形管", 25.0, "number", 1.0), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ParameterField("doorLeafFrameDepth", "截面深度 (mm)", "活动门扇框矩形管", 25.0, "number", 1.0), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ParameterField("doorLeafFrameCornerRadius", "外圆角 R (mm)", "活动门扇框矩形管", 2.5, "number", 0.0), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ParameterField("doorLeafFrameWallThickness", "壁厚 (mm)", "活动门扇框矩形管", 1.5, "number", 0.1), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ParameterField("doorHorizontalProfileType", "截面类型", "门内横杆矩形管", std::string("rect"), "readonly"), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ParameterField("doorHorizontalWidth", "截面宽度 (mm)", "门内横杆矩形管", 20.0, "number", 1.0), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ParameterField("doorHorizontalDepth", "截面深度 (mm)", "门内横杆矩形管", 20.0, "number", 1.0), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ParameterField("doorHorizontalCornerRadius", "外圆角 R (mm)", "门内横杆矩形管", 2.0, "number", 0.0), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ParameterField("doorHorizontalWallThickness", "壁厚 (mm)", "门内横杆矩形管", 1.2, "number", 0.1), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ParameterField("doorVerticalProfileType", "截面类型", "门内竖杆圆管", std::string("round"), "readonly"), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ParameterField("doorVerticalWidth", "外径 (mm)", "门内竖杆圆管", 16.0, "number", 1.0), _DoorEnabled));
        _Fields.emplace_back(VisibleWhen(ParameterField("doorVerticalWallThickness", "壁厚 (mm)", "门内竖杆圆管", 1.2, "number", 0.1), _DoorEnabled));
        _Fields.emplace_back(ParameterField("horizontalBranchReserve", "横杆安装预留 (mm)", "装配", -1.0));
        _Fields.emplace_back(ParameterField("verticalBranchReserve", "竖杆安装预留 (mm)", "装配", -1.0));
        _Fields.emplace_back(ParameterField("assemblyClearance", "穿杆开孔间隙 (mm)", "装配", 0.1, "number", 0.0));
        return _Fields;
    }

    VariantArray TemplateCatalog(
        const iCAX::Application::IApplicationContext& ApplicationContext_)
    {
        VariantArray _Templates;
        for (const auto& _Registration : kPythonTemplates)
        {
            try
            {
                const auto _Package = LoadPythonTemplatePackage(ApplicationContext_, _Registration.ID);
                _Templates.emplace_back(iCAX::TemplateRuntime::CTemplateCodec::MakePresentationDescriptor(
                    _Package.Descriptor, "zh-CN"));
            }
            catch (const std::exception& Error_)
            {
                ObjectMap _Unavailable;
                _Unavailable["id"] = std::string(_Registration.ID);
                _Unavailable["version"] = std::string();
                _Unavailable["name"] = std::string(_Registration.ID);
                _Unavailable["available"] = false;
                _Unavailable["status"] = std::string("模板包不可用：") + Error_.what();
                _Unavailable["parameters"] = VariantArray{};
                _Templates.emplace_back(_Unavailable);
            }
        }

        auto _OneFields = CommonParameterSchema("TD-001");
        _OneFields.emplace(_OneFields.begin() + 3, ParameterField("horizontalCount", "横管数量", "基本尺寸", 4ull, "integer", 0ull, 100ull));
        _OneFields.emplace(_OneFields.begin() + 4, ParameterField("middleVerticalCount", "中间竖管数量", "基本尺寸", 1ull, "integer", 0ull, 10ull));
        _OneFields.emplace(_OneFields.begin() + 5, ParameterField("firstHorizontalTopOffset", "首横管距顶部 (mm)", "基本尺寸", 200.0, "number", 0.0));
        _OneFields.emplace(_OneFields.begin() + 6, ParameterField("lastHorizontalBottomOffset", "末横管距底部 (mm)", "基本尺寸", 200.0, "number", 0.0));
        ObjectMap _One;
        _One["id"] = std::string("security-window-1");
        _One["version"] = std::string("1.0.0");
        _One["name"] = std::string("防盗窗 1 号 - 无把手不封闭");
        _One["available"] = true;
        _One["parameters"] = _OneFields;
        _Templates.emplace_back(_One);

        auto _TwoFields = CommonParameterSchema("TD-002");
        _TwoFields.emplace(_TwoFields.begin() + 3, ParameterField("handleBottom", "把手底部 Z1 (mm)", "把手区域", 700.0, "number", 0.0));
        _TwoFields.emplace(_TwoFields.begin() + 4, ParameterField("handleHeight", "把手高度 Z2 (mm)", "把手区域", 360.0, "number", 1.0));
        _TwoFields.emplace(_TwoFields.begin() + 5, ParameterField("handleWidth", "把手宽度 W1 (mm)", "把手区域", 300.0, "number", 1.0));
        _TwoFields.emplace(_TwoFields.begin() + 6, ParameterField("handleHorizontalCount", "把手区域横管数量", "把手区域", 1ull, "integer", 0ull, 100ull));
        _TwoFields.emplace(_TwoFields.begin() + 7, ParameterField("topHorizontalCount", "把手上方横管数量", "把手区域", 2ull, "integer", 0ull, 100ull));
        _TwoFields.emplace(_TwoFields.begin() + 8, ParameterField("bottomHorizontalCount", "把手下方横管数量", "把手区域", 2ull, "integer", 0ull, 100ull));
        ObjectMap _Two;
        _Two["id"] = std::string("security-window-2");
        _Two["version"] = std::string("1.0.0");
        _Two["name"] = std::string("防盗窗 2 号 - 有把手不封闭");
        _Two["available"] = true;
        _Two["parameters"] = _TwoFields;
        _Templates.emplace_back(_Two);

        auto _ThreeFields = CommonParameterSchema("TD-003");
        _ThreeFields.emplace(_ThreeFields.begin() + 3, ParameterField("horizontalCount", "中间横管数量", "内部管材", 4ull, "integer", 0ull, 100ull));
        _ThreeFields.emplace(_ThreeFields.begin() + 4, ParameterField("middleVerticalCount", "中间竖管数量", "内部管材", 1ull, "integer", 0ull, 10ull));
        _ThreeFields.emplace(_ThreeFields.begin() + 5, ParameterField("firstHorizontalTopOffset", "首横管距顶部 (mm)", "内部管材", 200.0, "number", 0.0));
        _ThreeFields.emplace(_ThreeFields.begin() + 6, ParameterField("lastHorizontalBottomOffset", "末横管距底部 (mm)", "内部管材", 200.0, "number", 0.0));
        _ThreeFields.emplace(_ThreeFields.begin() + 7, ChoiceField(
            "frameJoinType", "外框拼接方式", "外框工艺", "v_groove_90", {
                { "v_groove_90", "90° V 槽折弯（连续一根管）" },
                { "miter_45", "45° 斜拼（四根管）" },
                { "butt_90", "90° 直拼（四根管）" }
            }));
        _ThreeFields.emplace(_ThreeFields.begin() + 8, VisibleWhen(ChoiceField(
            "frameButtWrapMode", "直拼包边方向", "外框工艺", "side_wraps_horizontal", {
                { "side_wraps_horizontal", "左右框包上下框" },
                { "horizontal_wraps_side", "上下框包左右框" }
            }), { { "frameJoinType", Variant(std::string("butt_90")) } }));
        _ThreeFields.emplace(_ThreeFields.begin() + 9, VisibleWhen(ChoiceField(
            "vGrooveStyle", "V 槽类型", "V 槽参数", "sharp_v", {
                { "sharp_v", "尖角 V 槽" }, { "rounded_v", "圆角 V 槽" },
                { "left_arc", "左圆弧 V 槽" }, { "right_arc", "右圆弧 V 槽" }
            }), { { "frameJoinType", Variant(std::string("v_groove_90")) } }));
        _ThreeFields.emplace(_ThreeFields.begin() + 10, VisibleWhen(ParameterField("vGrooveBottomDistance", "槽底距外侧底面 (mm)", "V 槽参数", 2.0, "number", 0.0), { { "frameJoinType", Variant(std::string("v_groove_90")) } }));
        _ThreeFields.emplace(_ThreeFields.begin() + 11, VisibleWhen(ParameterField("vGrooveRadius", "圆角/释放半径 (mm)", "V 槽参数", 18.0, "number", 0.0), { { "frameJoinType", Variant(std::string("v_groove_90")) } }));
        _ThreeFields.emplace(_ThreeFields.begin() + 12, VisibleWhen(ParameterField("vGrooveKFactor", "展开 K 因子", "V 槽参数", 0.62, "number", 0.0, 1.0), { { "frameJoinType", Variant(std::string("v_groove_90")) } }));
        _ThreeFields.emplace(_ThreeFields.begin() + 13, VisibleWhen(ChoiceField("vGrooveMaleFemale", "斜切公母", "V 槽附加工艺", "否", { { "否", "否" }, { "是", "是" } }), { { "frameJoinType", Variant(std::string("v_groove_90")) } }));
        _ThreeFields.emplace(_ThreeFields.begin() + 14, VisibleWhen(ChoiceField("vGrooveBottomCut", "底部切除", "V 槽附加工艺", "否", { { "否", "否" }, { "是", "是" } }), { { "frameJoinType", Variant(std::string("v_groove_90")) }, { "vGrooveStyle", Variant(std::string("sharp_v")) } }));
        _ThreeFields.emplace(_ThreeFields.begin() + 15, VisibleWhen(ChoiceField("vGrooveReliefHole", "释放孔", "V 槽附加工艺", "否", { { "否", "否" }, { "是", "是" } }), { { "frameJoinType", Variant(std::string("v_groove_90")) }, { "vGrooveStyle", Variant(std::string("sharp_v")) } }));
        _ThreeFields.emplace(_ThreeFields.begin() + 16, VisibleWhen(ChoiceField("vGrooveWallOvercut", "壁厚过切", "V 槽附加工艺", "否", { { "否", "否" }, { "是", "是" } }), { { "frameJoinType", Variant(std::string("v_groove_90")) }, { "vGrooveStyle", Variant(std::string("sharp_v")) } }));
        _ThreeFields.emplace(_ThreeFields.begin() + 17, VisibleWhen(ParameterField("vGrooveReliefDiameter", "释放孔直径 (mm)", "V 槽附加工艺", 10.0, "number", 0.0), { { "frameJoinType", Variant(std::string("v_groove_90")) }, { "vGrooveStyle", Variant(std::string("sharp_v")) }, { "vGrooveReliefHole", Variant(std::string("是")) } }));
        _ThreeFields.emplace(_ThreeFields.begin() + 18, VisibleWhen(ChoiceField("vGrooveReliefNoThrough", "释放孔不过切", "V 槽附加工艺", "否", { { "否", "否" }, { "是", "是" } }), { { "frameJoinType", Variant(std::string("v_groove_90")) }, { "vGrooveStyle", Variant(std::string("sharp_v")) }, { "vGrooveReliefHole", Variant(std::string("是")) } }));
        ObjectMap _Three;
        _Three["id"] = std::string("security-window-3");
        _Three["version"] = std::string("1.0.0");
        _Three["name"] = std::string("防盗窗 3 号 - 四边封闭/V槽折弯");
        _Three["available"] = true;
        _Three["parameters"] = _ThreeFields;
        _Templates.emplace_back(_Three);
        return _Templates;
    }

    TopoDS_Shape CutShape(
        const TopoDS_Shape& Target_, const TopoDS_Shape& Cutter_, const std::string& Operation_)
    {
        BRepAlgoAPI_Cut _Cut(Target_, Cutter_);
        if (!_Cut.IsDone() || _Cut.Shape().IsNull())
        {
            throw std::runtime_error("TubeDesigner boolean cut failed: " + Operation_);
        }
        return _Cut.Shape();
    }

    TopoDS_Edge MakeLineEdge(const gp_Pnt& Start_, const gp_Pnt& End_)
    {
        return BRepBuilderAPI_MakeEdge(Start_, End_).Edge();
    }

    TopoDS_Edge MakeArcEdge(
        const gp_Pnt& Start_, const gp_Pnt& Middle_, const gp_Pnt& End_)
    {
        GC_MakeArcOfCircle _Arc(Start_, Middle_, End_);
        if (!_Arc.IsDone()) throw std::runtime_error("TubeDesigner failed to build cutter arc");
        return BRepBuilderAPI_MakeEdge(_Arc.Value()).Edge();
    }

    TopoDS_Shape MakeXyPolygonPrism(
        const std::vector<gp_Pnt>& Points_, double StartZ_, double EndZ_)
    {
        if (Points_.size() < 3 || std::abs(EndZ_ - StartZ_) <= 0.001)
            throw std::invalid_argument("TubeDesigner polygon prism dimensions are invalid");
        BRepBuilderAPI_MakePolygon _Polygon;
        for (const auto& _Point : Points_)
            _Polygon.Add(gp_Pnt(_Point.X(), _Point.Y(), StartZ_));
        _Polygon.Close();
        if (!_Polygon.IsDone()) throw std::runtime_error("TubeDesigner failed to build polygon wire");
        BRepBuilderAPI_MakeFace _Face(_Polygon.Wire(), true);
        if (!_Face.IsDone()) throw std::runtime_error("TubeDesigner failed to build polygon face");
        BRepPrimAPI_MakePrism _Prism(
            _Face.Face(), gp_Vec(0.0, 0.0, EndZ_ - StartZ_), false, true);
        if (!_Prism.IsDone()) throw std::runtime_error("TubeDesigner failed to build polygon prism");
        return _Prism.Shape();
    }

    TopoDS_Shape MakeXyProfilePrism(
        const std::vector<TopoDS_Edge>& Edges_, double StartZ_, double EndZ_)
    {
        if (Edges_.empty() || std::abs(EndZ_ - StartZ_) <= 0.001)
            throw std::invalid_argument("TubeDesigner profile prism dimensions are invalid");
        BRepBuilderAPI_MakeWire _Wire;
        for (const auto& _Edge : Edges_) _Wire.Add(_Edge);
        if (!_Wire.IsDone()) throw std::runtime_error("TubeDesigner failed to build profile wire");
        BRepBuilderAPI_MakeFace _Face(_Wire.Wire(), true);
        if (!_Face.IsDone()) throw std::runtime_error("TubeDesigner failed to build profile face");
        BRepPrimAPI_MakePrism _Prism(
            _Face.Face(), gp_Vec(0.0, 0.0, EndZ_ - StartZ_), false, true);
        if (!_Prism.IsDone()) throw std::runtime_error("TubeDesigner failed to build profile prism");
        return _Prism.Shape();
    }

    TopoDS_Shape MakeRoundedRectanglePrism(
        double MinX_, double MinY_, double MaxX_, double MaxY_, double Radius_,
        double StartZ_, double EndZ_)
    {
        if (MaxX_ <= MinX_ || MaxY_ <= MinY_ || EndZ_ <= StartZ_)
            throw std::invalid_argument("TubeDesigner rounded rectangle bounds are invalid");
        const auto _MaximumRadius = std::min(MaxX_ - MinX_, MaxY_ - MinY_) / 2.0 - 0.001;
        const auto _Radius = Radius_ <= 0.001
            ? 0.0 : std::clamp(Radius_, 0.001, std::max(0.001, _MaximumRadius));
        if (_Radius <= 0.001)
        {
            return BRepPrimAPI_MakeBox(
                gp_Pnt(MinX_, MinY_, StartZ_),
                MaxX_ - MinX_, MaxY_ - MinY_, EndZ_ - StartZ_).Shape();
        }
        const auto _Diagonal = _Radius / std::sqrt(2.0);
        const gp_Pnt _BottomLeft(MinX_ + _Radius, MinY_, StartZ_);
        const gp_Pnt _BottomRight(MaxX_ - _Radius, MinY_, StartZ_);
        const gp_Pnt _RightBottom(MaxX_, MinY_ + _Radius, StartZ_);
        const gp_Pnt _RightTop(MaxX_, MaxY_ - _Radius, StartZ_);
        const gp_Pnt _TopRight(MaxX_ - _Radius, MaxY_, StartZ_);
        const gp_Pnt _TopLeft(MinX_ + _Radius, MaxY_, StartZ_);
        const gp_Pnt _LeftTop(MinX_, MaxY_ - _Radius, StartZ_);
        const gp_Pnt _LeftBottom(MinX_, MinY_ + _Radius, StartZ_);
        return MakeXyProfilePrism({
            MakeLineEdge(_BottomLeft, _BottomRight),
            MakeArcEdge(_BottomRight, gp_Pnt(MaxX_ - _Radius + _Diagonal, MinY_ + _Radius - _Diagonal, StartZ_), _RightBottom),
            MakeLineEdge(_RightBottom, _RightTop),
            MakeArcEdge(_RightTop, gp_Pnt(MaxX_ - _Radius + _Diagonal, MaxY_ - _Radius + _Diagonal, StartZ_), _TopRight),
            MakeLineEdge(_TopRight, _TopLeft),
            MakeArcEdge(_TopLeft, gp_Pnt(MinX_ + _Radius - _Diagonal, MaxY_ - _Radius + _Diagonal, StartZ_), _LeftTop),
            MakeLineEdge(_LeftTop, _LeftBottom),
            MakeArcEdge(_LeftBottom, gp_Pnt(MinX_ + _Radius - _Diagonal, MinY_ + _Radius - _Diagonal, StartZ_), _BottomLeft)
        }, StartZ_, EndZ_);
    }

    TopoDS_Shape MakeOrientedRoundedPrism(
        const SGeneratedPart& Part_, double Width_, double Depth_,
        double Radius_, double EndExtension_)
    {
        const auto _Length = Part_.Length + EndExtension_ * 2.0;
        TopoDS_Shape _Local;
        gp_Trsf _Rotation;
        gp_Trsf _Translation;
        if (Part_.IsVertical())
        {
            _Local = MakeRoundedRectanglePrism(
                -Width_ / 2.0, -Depth_ / 2.0,
                Width_ / 2.0, Depth_ / 2.0, Radius_, 0.0, _Length);
            _Rotation.SetRotation(
                gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(1.0, 0.0, 0.0)),
                -1.57079632679489661923);
            _Translation.SetTranslation(gp_Vec(
                (Part_.X1 + Part_.X2) / 2.0,
                std::min(Part_.Y1, Part_.Y2) - EndExtension_, 0.0));
        }
        else
        {
            _Local = MakeRoundedRectanglePrism(
                -Depth_ / 2.0, -Width_ / 2.0,
                Depth_ / 2.0, Width_ / 2.0, Radius_, 0.0, _Length);
            _Rotation.SetRotation(
                gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 1.0, 0.0)),
                1.57079632679489661923);
            _Translation.SetTranslation(gp_Vec(
                std::min(Part_.X1, Part_.X2) - EndExtension_,
                (Part_.Y1 + Part_.Y2) / 2.0, 0.0));
        }
        const auto _Rotated = BRepBuilderAPI_Transform(_Local, _Rotation, true).Shape();
        return BRepBuilderAPI_Transform(_Rotated, _Translation, true).Shape();
    }

    TopoDS_Shape MakeTubeShape(const SGeneratedPart& Part_, double Extra_ = 0.0, bool Hollow_ = true)
    {
        if (Part_.Profile.Type == "round")
        {
            const auto _OuterRadius = Part_.Profile.Width / 2.0 + Extra_;
            const auto _InnerRadius = Part_.Profile.Width / 2.0 - Part_.Profile.WallThickness;
            if (_OuterRadius <= 0.0 || (Hollow_ && _InnerRadius <= 0.0))
                throw std::invalid_argument("TubeDesigner round tube dimensions are invalid");
            const auto _Epsilon = 1.0;
            const auto _Direction = Part_.IsVertical() ? gp_Dir(0.0, 1.0, 0.0) : gp_Dir(1.0, 0.0, 0.0);
            const auto _OuterOrigin = Part_.IsVertical()
                ? gp_Pnt((Part_.X1 + Part_.X2) / 2.0, std::min(Part_.Y1, Part_.Y2) - Extra_, 0.0)
                : gp_Pnt(std::min(Part_.X1, Part_.X2) - Extra_, (Part_.Y1 + Part_.Y2) / 2.0, 0.0);
            auto _Outer = BRepPrimAPI_MakeCylinder(
                gp_Ax2(_OuterOrigin, _Direction), _OuterRadius, Part_.Length + Extra_ * 2.0).Shape();
            if (!Hollow_) return _Outer;
            const auto _InnerOrigin = Part_.IsVertical()
                ? gp_Pnt((Part_.X1 + Part_.X2) / 2.0, std::min(Part_.Y1, Part_.Y2) - _Epsilon, 0.0)
                : gp_Pnt(std::min(Part_.X1, Part_.X2) - _Epsilon, (Part_.Y1 + Part_.Y2) / 2.0, 0.0);
            const auto _Inner = BRepPrimAPI_MakeCylinder(
                gp_Ax2(_InnerOrigin, _Direction), _InnerRadius, Part_.Length + _Epsilon * 2.0).Shape();
            BRepAlgoAPI_Cut _Cut(_Outer, _Inner);
            if (!_Cut.IsDone()) throw std::runtime_error("TubeDesigner failed to build hollow round tube");
            return _Cut.Shape();
        }
        if (Part_.Profile.Type != "rect")
            throw std::invalid_argument("TubeDesigner tube profile type is unsupported: " + Part_.Profile.Type);
        const auto _Width = Part_.Profile.Width + Extra_ * 2.0;
        const auto _Depth = Part_.Profile.Depth + Extra_ * 2.0;
        const auto _Epsilon = 1.0;
        const auto _OuterRadius = std::max(0.0, Part_.Profile.CornerRadius + Extra_);
        auto _Outer = MakeOrientedRoundedPrism(Part_, _Width, _Depth, _OuterRadius, Extra_);
        if (!Hollow_) return _Outer;
        const auto _InnerWidth = Part_.Profile.Width - Part_.Profile.WallThickness * 2.0;
        const auto _InnerDepth = Part_.Profile.Depth - Part_.Profile.WallThickness * 2.0;
        const auto _InnerRadius = std::max(0.0,
            Part_.Profile.CornerRadius - Part_.Profile.WallThickness);
        const auto _Inner = MakeOrientedRoundedPrism(
            Part_, _InnerWidth, _InnerDepth, _InnerRadius, _Epsilon);
        BRepAlgoAPI_Cut _Cut(_Outer, _Inner);
        if (!_Cut.IsDone()) throw std::runtime_error("TubeDesigner failed to build hollow tube");
        return _Cut.Shape();
    }

    TopoDS_Shape MakeMiterCutter(const SGeneratedPart& Part_, bool AtStart_)
    {
        const auto _Width = std::max(Part_.Profile.Width, 0.1);
        const auto _HalfWidth = _Width / 2.0;
        auto _MinX = std::min(Part_.X1, Part_.X2);
        auto _MaxX = std::max(Part_.X1, Part_.X2);
        auto _MinY = std::min(Part_.Y1, Part_.Y2);
        auto _MaxY = std::max(Part_.Y1, Part_.Y2);
        if (Part_.IsVertical())
        {
            const auto _CenterX = (Part_.X1 + Part_.X2) / 2.0;
            _MinX = _CenterX - _HalfWidth;
            _MaxX = _CenterX + _HalfWidth;
        }
        else
        {
            const auto _CenterY = (Part_.Y1 + Part_.Y2) / 2.0;
            _MinY = _CenterY - _HalfWidth;
            _MaxY = _CenterY + _HalfWidth;
        }

        gp_Pnt _LineStart;
        gp_Pnt _LineEnd;
        bool _RemovePositive = false;
        if (Part_.Role.ends_with("left-frame") && AtStart_)
            { _LineStart = gp_Pnt(_MinX, _MinY, 0); _LineEnd = gp_Pnt(_MaxX, _MinY + _Width, 0); _RemovePositive = false; }
        else if (Part_.Role.ends_with("left-frame"))
            { _LineStart = gp_Pnt(_MinX, _MaxY, 0); _LineEnd = gp_Pnt(_MaxX, _MaxY - _Width, 0); _RemovePositive = true; }
        else if (Part_.Role.ends_with("right-frame") && AtStart_)
            { _LineStart = gp_Pnt(_MaxX, _MinY, 0); _LineEnd = gp_Pnt(_MinX, _MinY + _Width, 0); _RemovePositive = true; }
        else if (Part_.Role.ends_with("right-frame"))
            { _LineStart = gp_Pnt(_MaxX, _MaxY, 0); _LineEnd = gp_Pnt(_MinX, _MaxY - _Width, 0); _RemovePositive = false; }
        else if (Part_.Role.ends_with("bottom-frame") && AtStart_)
            { _LineStart = gp_Pnt(_MinX, _MinY, 0); _LineEnd = gp_Pnt(_MinX + _Width, _MaxY, 0); _RemovePositive = true; }
        else if (Part_.Role.ends_with("bottom-frame"))
            { _LineStart = gp_Pnt(_MaxX, _MinY, 0); _LineEnd = gp_Pnt(_MaxX - _Width, _MaxY, 0); _RemovePositive = false; }
        else if (Part_.Role.ends_with("top-frame") && AtStart_)
            { _LineStart = gp_Pnt(_MinX, _MaxY, 0); _LineEnd = gp_Pnt(_MinX + _Width, _MinY, 0); _RemovePositive = false; }
        else if (Part_.Role.ends_with("top-frame"))
            { _LineStart = gp_Pnt(_MaxX, _MaxY, 0); _LineEnd = gp_Pnt(_MaxX - _Width, _MinY, 0); _RemovePositive = true; }
        else throw std::invalid_argument("TubeDesigner miter cut requires a frame part");

        const auto _DX = _LineEnd.X() - _LineStart.X();
        const auto _DY = _LineEnd.Y() - _LineStart.Y();
        const auto _Length = std::hypot(_DX, _DY);
        auto _NormalX = -_DY / _Length;
        auto _NormalY = _DX / _Length;
        if (!_RemovePositive) { _NormalX = -_NormalX; _NormalY = -_NormalY; }
        const auto _UX = _DX / _Length;
        const auto _UY = _DY / _Length;
        const auto _Oversize = std::max(
            std::max({ Part_.Length, Part_.Profile.Width, Part_.Profile.Depth }) * 4.0, 500.0);
        const auto _Z = std::max(Part_.Profile.Depth * 4.0, 200.0);
        const gp_Pnt _A(_LineStart.X() - _UX * _Oversize, _LineStart.Y() - _UY * _Oversize, 0);
        const gp_Pnt _B(_LineEnd.X() + _UX * _Oversize, _LineEnd.Y() + _UY * _Oversize, 0);
        const gp_Pnt _C(_B.X() + _NormalX * _Oversize, _B.Y() + _NormalY * _Oversize, 0);
        const gp_Pnt _D(_A.X() + _NormalX * _Oversize, _A.Y() + _NormalY * _Oversize, 0);
        return MakeXyPolygonPrism({ _A, _B, _C, _D }, -_Z, _Z);
    }

    TopoDS_Shape ApplyEndCuts(const SGeneratedPart& Part_, TopoDS_Shape Shape_)
    {
        if (Part_.StartCut == "miter-45")
            Shape_ = CutShape(Shape_, MakeMiterCutter(Part_, true), Part_.PartNumber + " start miter");
        if (Part_.EndCut == "miter-45")
            Shape_ = CutShape(Shape_, MakeMiterCutter(Part_, false), Part_.PartNumber + " end miter");
        return Shape_;
    }

    TopoDS_Shape MakeFoldedFramePreviewShape(const SGeneratedPart& Part_)
    {
        const auto& _Geometry = *Part_.PreviewGeometry;
        auto _FrameBand = CutShape(
            MakeRoundedRectanglePrism(
                _Geometry.OuterMinX, _Geometry.OuterMinY,
                _Geometry.OuterMaxX, _Geometry.OuterMaxY,
                _Geometry.RoundedOuterCornerRadius,
                -_Geometry.HalfDepth, _Geometry.HalfDepth),
            MakeRoundedRectanglePrism(
                _Geometry.OpeningMinX, _Geometry.OpeningMinY,
                _Geometry.OpeningMaxX, _Geometry.OpeningMaxY,
                _Geometry.RoundedInnerCornerRadius,
                -_Geometry.HalfDepth - 2.0, _Geometry.HalfDepth + 2.0),
            Part_.PartNumber + " preview opening");
        auto _CavityRing = CutShape(
            MakeRoundedRectanglePrism(
                _Geometry.OuterMinX + _Geometry.WallThickness,
                _Geometry.OuterMinY + _Geometry.WallThickness,
                _Geometry.OuterMaxX - _Geometry.WallThickness,
                _Geometry.OuterMaxY - _Geometry.WallThickness,
                _Geometry.RoundedOuterCornerRadius > 0.0
                    ? std::max(_Geometry.RoundedOuterCornerRadius - _Geometry.WallThickness, 0.1) : 0.0,
                -_Geometry.HalfDepth + _Geometry.WallThickness,
                _Geometry.HalfDepth - _Geometry.WallThickness),
            MakeRoundedRectanglePrism(
                _Geometry.OpeningMinX - _Geometry.WallThickness,
                _Geometry.OpeningMinY - _Geometry.WallThickness,
                _Geometry.OpeningMaxX + _Geometry.WallThickness,
                _Geometry.OpeningMaxY + _Geometry.WallThickness,
                _Geometry.RoundedInnerCornerRadius > 0.0
                    ? _Geometry.RoundedInnerCornerRadius + _Geometry.WallThickness : 0.0,
                -_Geometry.HalfDepth + _Geometry.WallThickness - 2.0,
                _Geometry.HalfDepth - _Geometry.WallThickness + 2.0),
            Part_.PartNumber + " preview cavity opening");
        return CutShape(_FrameBand, _CavityRing, Part_.PartNumber + " hollow preview frame");
    }

    TopoDS_Shape MakeManufacturingCutterShape(
        const SGeneratedPart& Part_, const SGeneratedPart::SManufacturingCutter& Cutter_)
    {
        if (Cutter_.Kind == "xy-polygon-prism")
        {
            std::vector<gp_Pnt> _Points;
            _Points.reserve(Cutter_.Points.size());
            for (const auto& _Point : Cutter_.Points) _Points.emplace_back(_Point.X, _Point.Y, 0.0);
            return MakeXyPolygonPrism(_Points, Cutter_.StartZ, Cutter_.EndZ);
        }
        if (Cutter_.Kind == "xy-profile-prism")
        {
            std::vector<TopoDS_Edge> _Edges;
            _Edges.reserve(Cutter_.Edges.size());
            for (const auto& _Edge : Cutter_.Edges)
            {
                const gp_Pnt _Start(_Edge.Start.X, _Edge.Start.Y, Cutter_.StartZ);
                const gp_Pnt _End(_Edge.End.X, _Edge.End.Y, Cutter_.StartZ);
                _Edges.push_back(_Edge.Kind == "arc-3pt"
                    ? MakeArcEdge(_Start, gp_Pnt(_Edge.Middle.X, _Edge.Middle.Y, Cutter_.StartZ), _End)
                    : MakeLineEdge(_Start, _End));
            }
            return MakeXyProfilePrism(_Edges, Cutter_.StartZ, Cutter_.EndZ);
        }
        if (Cutter_.Kind == "z-cylinder")
        {
            if (Cutter_.Radius <= 0.0 || Cutter_.EndZ <= Cutter_.StartZ)
                throw std::invalid_argument("TubeDesigner cylinder cutter dimensions are invalid");
            return BRepPrimAPI_MakeCylinder(
                gp_Ax2(gp_Pnt(Cutter_.Center.X, Cutter_.Center.Y, Cutter_.StartZ), gp_Dir(0, 0, 1)),
                Cutter_.Radius, Cutter_.EndZ - Cutter_.StartZ).Shape();
        }
        throw std::invalid_argument("TubeDesigner manufacturing cutter kind is unsupported: " + Cutter_.Kind);
    }

    TopoDS_Shape MakePreviewShape(const SGeneratedPart& Part_)
    {
        if (Part_.PreviewGeometry)
        {
            if (Part_.PreviewGeometry->Kind != "folded-rect-frame")
                throw std::invalid_argument("TubeDesigner preview geometry kind is unsupported");
            return MakeFoldedFramePreviewShape(Part_);
        }
        return ApplyEndCuts(Part_, MakeTubeShape(Part_));
    }

    TopoDS_Shape MakeManufacturingShape(const SGeneratedPart& Part_)
    {
        if (!Part_.ManufacturingGeometry) return ApplyEndCuts(Part_, MakeTubeShape(Part_));
        const auto& _Geometry = *Part_.ManufacturingGeometry;
        if (_Geometry.Kind != "tube-with-cutters" || _Geometry.BaseLength <= 0.0)
            throw std::invalid_argument("TubeDesigner manufacturing geometry is invalid");
        auto _Base = Part_;
        _Base.X1 = 0.0;
        _Base.Y1 = 0.0;
        _Base.X2 = _Geometry.BaseLength;
        _Base.Y2 = 0.0;
        _Base.Length = _Geometry.BaseLength;
        _Base.StartCut = "square";
        _Base.EndCut = "square";
        _Base.PreviewGeometry.reset();
        _Base.ManufacturingGeometry.reset();
        auto _Shape = MakeTubeShape(_Base);
        for (const auto& _Cutter : _Geometry.Cutters)
        {
            _Shape = CutShape(
                _Shape, MakeManufacturingCutterShape(Part_, _Cutter),
                Part_.PartNumber + " " + _Cutter.Label);
        }
        return _Shape;
    }

    iCAX::Resource::CResourceReference StoreBRep(
        iCAX::Project::ISceneContext& Scene_,
        const std::string& StableName_,
        const std::string& DisplayName_,
        const TopoDS_Shape& Shape_)
    {
        auto& _Resources = Scene_.Resources();
        const auto _ResourceID = _Resources.MakeNamedResourceURL(StableName_);
        auto _BRep = std::make_shared<iCAX::GeometryData::BRepModel>(
            iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(
                Shape_, DisplayName_, _ResourceID, 0.001));
        iCAX::Resource::CResourceInfo _Info;
        _Info.Name = DisplayName_;
        _Info.ResourceTypeID = iCAX::GeometryData::BRepModel::kResourceTypeName;
        _Info.Persistence = iCAX::Resource::EResourcePersistenceMode::Embedded;
        _Info.Metadata["source"] = "tube-designer";
        iCAX::Resource::CResourceInfo _StoredInfo;
        const auto _Mutation = _Resources.PutVersioned<iCAX::GeometryData::BRepModel>(
            _ResourceID, std::move(_BRep), _Info,
            iCAX::Resource::EResourceVersionCondition::None, 0, &_StoredInfo);
        if (_Mutation == iCAX::Resource::EResourceMutationResult::PreconditionFailed
            || _StoredInfo.nVersion == 0)
        {
            throw std::runtime_error("TubeDesigner failed to commit a BRep resource version");
        }
        return { _ResourceID, _StoredInfo.nVersion };
    }

    iCAX::Resource::CResourceReference EnsureDesignerMaterial(iCAX::Project::ISceneContext& Scene_)
    {
        auto& _Resources = Scene_.Resources();
        const auto _ResourceID = _Resources.MakeNamedResourceURL("tube-designer/material/product");
        if (_Resources.GetVersion(_ResourceID) == 0)
        {
            auto _Material = std::make_shared<iCAX::Render::SRenderMaterialData>();
            _Material->nDataVersion = 1;
            _Material->nColorRGBA = 0x89B8C9FFu;
            _Material->nAmbientRGBA = 0x89B8C9FFu;
            _Material->nSpecularRGBA = 0x304858FFu;
            _Material->nEmissiveRGBA = 0x000000FFu;
            _Material->nLineWidth = 1.0f;
            iCAX::Resource::CResourceInfo _Info;
            _Info.Name = "TubeDesigner product material";
            _Info.ResourceTypeID = iCAX::Render::SRenderMaterialData::kResourceTypeName;
            _Info.Persistence = iCAX::Resource::EResourcePersistenceMode::Embedded;
            iCAX::Resource::CResourceInfo _StoredInfo;
            const auto _Mutation = _Resources.PutVersioned<iCAX::Render::SRenderMaterialData>(
                _ResourceID, std::move(_Material), _Info,
                iCAX::Resource::EResourceVersionCondition::MustNotExist, 0, &_StoredInfo);
            if (_Mutation == iCAX::Resource::EResourceMutationResult::PreconditionFailed)
            {
                throw std::runtime_error("TubeDesigner material resource identity changed concurrently");
            }
        }
        return iCAX::RenderInteraction::EnsureFrontendMaterialResource(_Resources, _ResourceID);
    }

    iCAX::Data::uuid MakeStableEntityID(
        const iCAX::Data::uuid& ProductID_,
        const std::string& Kind_,
        const std::string& StableKey_)
    {
        iCAX::Data::uuid_name_generator _Generator(ProductID_);
        return _Generator("tube-designer/" + Kind_ + "/" + StableKey_);
    }

    std::vector<std::string> MakeStablePartKeys(const SGeneratedProduct& Product_)
    {
        std::map<std::string, std::uint64_t> _RoleOrdinals;
        std::vector<std::string> _Keys;
        _Keys.reserve(Product_.Parts.size());
        for (const auto& _Part : Product_.Parts)
        {
            const auto _Ordinal = ++_RoleOrdinals[_Part.Role];
            _Keys.push_back(_Part.Role + "/" + std::to_string(_Ordinal));
        }
        return _Keys;
    }

    std::string MakePreviewMemberStableKey(
        const SGeneratedPart& Part_, const std::string& PartStableKey_)
    {
        return Part_.PreviewAssemblyKey.empty()
            ? PartStableKey_
            : "assembly/" + Part_.PreviewAssemblyKey;
    }

    std::string MakeSafePathSegment(const std::string& Value_, const std::string& Fallback_)
    {
        std::string _SafeName;
        _SafeName.reserve(Value_.size());
        const std::string _Invalid = "<>:\"/\\|?*";
        for (const auto _Character : Value_)
        {
            const auto _Byte = static_cast<unsigned char>(_Character);
            _SafeName.push_back(_Byte < 32 || _Invalid.find(_Character) != std::string::npos
                ? '_'
                : _Character);
        }
        while (!_SafeName.empty()
            && (_SafeName.back() == '.' || _SafeName.back() == ' '))
        {
            _SafeName.pop_back();
        }
        if (_SafeName.empty()) _SafeName = Fallback_;
        if (_SafeName.size() > 180) _SafeName.resize(180);
        return _SafeName;
    }

    std::string MakeStepFileName(const std::string& PartNumber_)
    {
        return MakeSafePathSegment(PartNumber_, "part") + ".step";
    }

    std::string MakeManufacturingPartNumber(
        const std::string& InstanceName_,
        const std::string& CategoryName_,
        std::uint64_t nCategoryOrdinal_,
        std::uint64_t nSegmentOrdinal_ = 0)
    {
        std::ostringstream _Number;
        _Number << InstanceName_ << '-' << CategoryName_ << '-'
            << std::setw(2) << std::setfill('0') << nCategoryOrdinal_;
        if (nSegmentOrdinal_ > 0)
        {
            _Number << "-段" << std::setw(2) << std::setfill('0') << nSegmentOrdinal_;
        }
        return MakeSafePathSegment(_Number.str(), "part");
    }

    std::filesystem::path Utf8Path(const std::string& Value_)
    {
        const std::u8string _Text(
            reinterpret_cast<const char8_t*>(Value_.data()), Value_.size());
        return std::filesystem::path(_Text);
    }

    std::string Utf8PathText(const std::filesystem::path& Value_)
    {
        const auto _Text = Value_.u8string();
        return { reinterpret_cast<const char*>(_Text.data()), _Text.size() };
    }

    struct SPreparedPart final
    {
        SGeneratedPart Generated;
        std::string StableKey;
        iCAX::Data::uuid MemberID;
    };

    struct SPreparedPreviewMember final
    {
        std::string StableKey;
        iCAX::Data::uuid MemberID;
        std::string MemberType;
        std::string Role;
        std::string Name;
        std::uint64_t MemberIndex = 0;
        std::vector<std::size_t> PartOffsets;
        iCAX::Resource::CResourceReference PreviewResource;
        iCAX::Resource::CResourceReference FrontendGeometryResource;
    };

    struct SPreparedJoint final
    {
        SGeneratedJoint Generated;
        iCAX::Data::uuid JointID;
    };

    STubeProfile OptionalTubeProfile(const ObjectMap& Properties_);

    struct SResolvedPartPresentation final
    {
        std::string Name;
        STubeProfile Profile;
    };

    SResolvedPartPresentation ResolvePartPresentation(
        iCAX::Database::IRepository& Repository_,
        const CManufacturingPartComponent& Part_)
    {
        SResolvedPartPresentation _Result{
            Part_.GetRole().empty() || Part_.GetRole() == "main"
                ? Part_.GetPartNumber() : Part_.GetRole(),
            { Part_.GetProfileType(), Part_.GetSectionWidth(), Part_.GetSectionDepth(),
                Part_.GetWallThickness(), Part_.GetCornerRadius() }
        };
        const auto _StoredProfile = OptionalTubeProfile(Part_.GetItemProperties());
        if (_Result.Profile.Type.empty()) _Result.Profile.Type = _StoredProfile.Type;
        if (_Result.Profile.Width <= 0.0) _Result.Profile.Width = _StoredProfile.Width;
        if (_Result.Profile.Depth <= 0.0) _Result.Profile.Depth = _StoredProfile.Depth;
        if (_Result.Profile.WallThickness <= 0.0)
            _Result.Profile.WallThickness = _StoredProfile.WallThickness;
        if (_Result.Profile.CornerRadius <= 0.0)
            _Result.Profile.CornerRadius = _StoredProfile.CornerRadius;

        if (const auto _MemberEntity = Repository_.GetEntity(Part_.GetSourceMemberID());
            const auto _Member = GetComponent<CAssemblyMemberComponent>(_MemberEntity))
        {
            if (!_Member->GetName().empty()) _Result.Name = _Member->GetName();
            if (_Result.Profile.Type.empty()) _Result.Profile.Type = _Member->GetProfileType();
            if (_Result.Profile.Width <= 0.0) _Result.Profile.Width = _Member->GetSectionWidth();
            if (_Result.Profile.Depth <= 0.0) _Result.Profile.Depth = _Member->GetSectionDepth();
            if (_Result.Profile.WallThickness <= 0.0)
                _Result.Profile.WallThickness = _Member->GetWallThickness();
            if (_Result.Profile.CornerRadius <= 0.0)
                _Result.Profile.CornerRadius = _Member->GetCornerRadius();
        }
        return _Result;
    }

    void QueueUpsertComponent(
        iCAX::Database::ITransaction& Transaction_,
        const std::shared_ptr<iCAX::Database::IEntity>& ExistingEntity_,
        const iCAX::Data::uuid& EntityID_,
        const std::string& ComponentClass_,
        const iCAX::Data::PropertySet& Properties_ = {})
    {
        if (ExistingEntity_ && ExistingEntity_->HasComponent(ComponentClass_))
        {
            Transaction_.ModifyComponent(EntityID_, ComponentClass_, Properties_);
        }
        else
        {
            Transaction_.AttachComponent(EntityID_, ComponentClass_, Properties_);
        }
    }

    ObjectMap BuildSnapshot(
        iCAX::Project::ISceneContext& Scene_,
        const iCAX::Application::IApplicationContext& ApplicationContext_)
    {
        auto& _Repository = Scene_.Database();
        ObjectMap _Designer;

        _Designer["templates"] = TemplateCatalog(ApplicationContext_);

        const auto _Meta = _Repository.GetMetaEntity();
        const auto _Root = GetComponent<CTubeDesignerRootComponent>(_Meta);
        const auto _ActiveProductID = _Root
            ? _Root->GetActiveProductID()
            : iCAX::Data::uuid();
        _Designer["activeProductId"] = UuidToString(_ActiveProductID);

        auto _Products = Collect<CProductInstanceComponent>(_Repository);
        std::sort(
            _Products.begin(),
            _Products.end(),
            [](const auto& Left_, const auto& Right_) {
                if (Left_.second->GetCreatedAt() != Right_.second->GetCreatedAt())
                {
                    return Left_.second->GetCreatedAt() > Right_.second->GetCreatedAt();
                }
                return Left_.second->GetName() < Right_.second->GetName();
            });
        VariantArray _Instances;
        for (const auto& [_Entity, _Product] : _Products)
        {
            const auto _ProductID = _Entity->GetID();
            const auto _GenerationRunID = _Product->GetActiveGenerationRunID();
            std::uint64_t _MemberCount = 0;
            std::uint64_t _PartCount = 0;
            std::uint64_t _ExpectedPartCount = 0;
            for (const auto& [_MemberEntity, _Member] : Collect<CAssemblyMemberComponent>(_Repository))
            {
                if (_Member->GetProductID() == _ProductID) ++_MemberCount;
            }
            for (const auto& [_PartEntity, _Part] : Collect<CManufacturingPartComponent>(_Repository))
            {
                if (_Part->GetProductID() == _ProductID
                    && !_GenerationRunID.is_nil()
                    && _Part->GetGenerationRunID() == _GenerationRunID) ++_PartCount;
            }
            if (!_GenerationRunID.is_nil())
            {
                if (const auto _Run = GetComponent<CGenerationRunComponent>(
                    _Repository.GetEntity(_GenerationRunID)))
                {
                    _ExpectedPartCount = _Run->GetPartCount();
                }
            }
            ObjectMap _Instance;
            _Instance["entityId"] = UuidToString(_ProductID);
            _Instance["productCode"] = _Product->GetProductCode();
            _Instance["name"] = _Product->GetName();
            _Instance["createdAt"] = _Product->GetCreatedAt();
            _Instance["templateId"] = _Product->GetTemplateID();
            _Instance["templateVersion"] = _Product->GetTemplateVersion();
            _Instance["status"] = _Product->GetStatus();
            _Instance["parameters"] = _Product->GetParameters();
            _Instance["activeGenerationRunId"] = UuidToString(_GenerationRunID);
            _Instance["memberCount"] = _MemberCount;
            _Instance["partCount"] = _PartCount;
            _Instance["expectedPartCount"] = _ExpectedPartCount;
            _Instance["hasDisassembly"] = _ExpectedPartCount > 0 && _PartCount == _ExpectedPartCount;
            _Instance["active"] = !_ActiveProductID.is_nil() && _ProductID == _ActiveProductID;
            _Instances.emplace_back(_Instance);
        }
        _Designer["instances"] = _Instances;

        std::shared_ptr<iCAX::Database::IEntity> _ActiveProductEntity;
        std::shared_ptr<CProductInstanceComponent> _ActiveProduct;
        for (const auto& [_Entity, _Product] : _Products)
        {
            if (!_ActiveProductID.is_nil() && _Entity->GetID() == _ActiveProductID)
            {
                _ActiveProductEntity = _Entity;
                _ActiveProduct = _Product;
                break;
            }
        }

        if (_ActiveProductEntity && _ActiveProduct)
        {
            ObjectMap _Product;
            _Product["entityId"] = UuidToString(_ActiveProductEntity->GetID());
            _Product["productCode"] = _ActiveProduct->GetProductCode();
            _Product["name"] = _ActiveProduct->GetName();
            _Product["createdAt"] = _ActiveProduct->GetCreatedAt();
            _Product["templateId"] = _ActiveProduct->GetTemplateID();
            _Product["templateVersion"] = _ActiveProduct->GetTemplateVersion();
            _Product["status"] = _ActiveProduct->GetStatus();
            _Product["parameters"] = _ActiveProduct->GetParameters();
            _Product["activeGenerationRunId"] = UuidToString(
                _ActiveProduct->GetActiveGenerationRunID());
            _Designer["product"] = _Product;
        }

        VariantArray _Members;
        for (const auto& [_Entity, _Member] : Collect<CAssemblyMemberComponent>(_Repository))
        {
            if (_ActiveProductID.is_nil() || _Member->GetProductID() != _ActiveProductID) continue;
            ObjectMap _Item;
            _Item["entityId"] = UuidToString(_Entity->GetID());
            _Item["manufacturingPartId"] = UuidToString(_Member->GetManufacturingPartID());
            _Item["index"] = _Member->GetMemberIndex();
            _Item["role"] = _Member->GetRole();
            _Item["name"] = _Member->GetName();
            _Item["memberType"] = _Member->GetMemberType();
            _Item["childPartCount"] = _Member->GetChildPartCount();
            _Item["profileType"] = _Member->GetProfileType();
            _Item["sectionWidth"] = _Member->GetSectionWidth();
            _Item["sectionDepth"] = _Member->GetSectionDepth();
            _Item["wallThickness"] = _Member->GetWallThickness();
            _Item["cornerRadius"] = _Member->GetCornerRadius();
            _Item["length"] = _Member->GetLength();
            _Item["stableKey"] = _Member->GetStableKey();
            _Item["previewGeometryResourceId"] = _Member->GetPreviewGeometryResourceID();
            _Item["previewGeometryResourceVersion"] = _Member->GetPreviewGeometryResourceVersion();
            _Item["properties"] = _Member->GetItemProperties();
            _Members.emplace_back(_Item);
        }
        _Designer["members"] = _Members;

        VariantArray _Parts;
        for (const auto& [_Entity, _Part] : Collect<CManufacturingPartComponent>(_Repository))
        {
            if (_ActiveProductID.is_nil()
                || !_ActiveProduct
                || _Part->GetProductID() != _ActiveProductID
                || _Part->GetGenerationRunID() != _ActiveProduct->GetActiveGenerationRunID()) continue;
            const auto _Presentation = ResolvePartPresentation(_Repository, *_Part);
            ObjectMap _Item;
            _Item["entityId"] = UuidToString(_Entity->GetID());
            _Item["sourceMemberId"] = UuidToString(_Part->GetSourceMemberID());
            _Item["index"] = _Part->GetPartIndex();
            _Item["partNumber"] = _Part->GetPartNumber();
            _Item["name"] = _Presentation.Name;
            _Item["role"] = _Part->GetRole();
            _Item["quantity"] = _Part->GetQuantity();
            _Item["profileType"] = _Presentation.Profile.Type;
            _Item["sectionWidth"] = _Presentation.Profile.Width;
            _Item["sectionDepth"] = _Presentation.Profile.Depth;
            _Item["wallThickness"] = _Presentation.Profile.WallThickness;
            _Item["cornerRadius"] = _Presentation.Profile.CornerRadius;
            _Item["length"] = _Part->GetLength();
            _Item["stableKey"] = _Part->GetStableKey();
            _Item["manufacturingGeometryResourceId"] = _Part->GetManufacturingGeometryResourceID();
            _Item["manufacturingGeometryResourceVersion"] = _Part->GetManufacturingGeometryResourceVersion();
            _Item["thumbnailGeometryResourceId"] = _Part->GetThumbnailGeometryResourceID();
            _Item["thumbnailGeometryResourceVersion"] = _Part->GetThumbnailGeometryResourceVersion();
            _Item["fileName"] = _Part->GetFileName();
            _Item["status"] = _Part->GetStatus();
            _Item["properties"] = _Part->GetItemProperties();
            _Parts.emplace_back(_Item);
        }
        _Designer["parts"] = _Parts;

        VariantArray _ManufacturingGroups;
        for (const auto& [_ProductEntity, _Product] : _Products)
        {
            const auto _ProductID = _ProductEntity->GetID();
            const auto _GenerationRunID = _Product->GetActiveGenerationRunID();
            if (_GenerationRunID.is_nil()) continue;
            const auto _Run = GetComponent<CGenerationRunComponent>(
                _Repository.GetEntity(_GenerationRunID));
            if (!_Run || _Run->GetStatus() != "Succeeded") continue;
            auto _ProductParts = Collect<CManufacturingPartComponent>(_Repository);
            _ProductParts.erase(
                std::remove_if(
                    _ProductParts.begin(),
                    _ProductParts.end(),
                    [&_ProductID, &_GenerationRunID](const auto& Item_) {
                        return Item_.second->GetProductID() != _ProductID
                            || Item_.second->GetGenerationRunID() != _GenerationRunID;
                    }),
                _ProductParts.end());
            if (_ProductParts.empty()
                || _ProductParts.size() != static_cast<std::size_t>(_Run->GetPartCount())) continue;
            std::sort(
                _ProductParts.begin(),
                _ProductParts.end(),
                [](const auto& Left_, const auto& Right_) {
                    return Left_.second->GetPartIndex() < Right_.second->GetPartIndex();
                });
            VariantArray _GroupParts;
            for (const auto& [_PartEntity, _Part] : _ProductParts)
            {
                const auto _Presentation = ResolvePartPresentation(_Repository, *_Part);
                ObjectMap _Item;
                _Item["entityId"] = UuidToString(_PartEntity->GetID());
                _Item["sourceMemberId"] = UuidToString(_Part->GetSourceMemberID());
                _Item["index"] = _Part->GetPartIndex();
                _Item["partNumber"] = _Part->GetPartNumber();
                _Item["name"] = _Presentation.Name;
                _Item["role"] = _Part->GetRole();
                _Item["quantity"] = _Part->GetQuantity();
                _Item["profileType"] = _Presentation.Profile.Type;
                _Item["sectionWidth"] = _Presentation.Profile.Width;
                _Item["sectionDepth"] = _Presentation.Profile.Depth;
                _Item["wallThickness"] = _Presentation.Profile.WallThickness;
                _Item["cornerRadius"] = _Presentation.Profile.CornerRadius;
                _Item["length"] = _Part->GetLength();
                _Item["stableKey"] = _Part->GetStableKey();
                _Item["manufacturingGeometryResourceId"] = _Part->GetManufacturingGeometryResourceID();
                _Item["manufacturingGeometryResourceVersion"] = _Part->GetManufacturingGeometryResourceVersion();
                _Item["thumbnailGeometryResourceId"] = _Part->GetThumbnailGeometryResourceID();
                _Item["thumbnailGeometryResourceVersion"] = _Part->GetThumbnailGeometryResourceVersion();
                _Item["fileName"] = _Part->GetFileName();
                _Item["status"] = _Part->GetStatus();
                _Item["properties"] = _Part->GetItemProperties();
                _GroupParts.emplace_back(_Item);
            }
            ObjectMap _Group;
            _Group["productEntityId"] = UuidToString(_ProductID);
            _Group["generationRunId"] = UuidToString(_GenerationRunID);
            _Group["name"] = _Product->GetName();
            _Group["productCode"] = _Product->GetProductCode();
            _Group["templateId"] = _Product->GetTemplateID();
            _Group["templateVersion"] = _Product->GetTemplateVersion();
            _Group["parameters"] = _Product->GetParameters();
            _Group["parts"] = _GroupParts;
            _ManufacturingGroups.emplace_back(_Group);
        }
        _Designer["manufacturingGroups"] = _ManufacturingGroups;

        VariantArray _Joints;
        for (const auto& [_Entity, _Joint] : Collect<CJointIntentComponent>(_Repository))
        {
            if (_ActiveProductID.is_nil() || _Joint->GetProductID() != _ActiveProductID) continue;
            ObjectMap _Item;
            _Item["entityId"] = UuidToString(_Entity->GetID());
            _Item["targetMemberId"] = UuidToString(_Joint->GetTargetMemberID());
            _Item["insertedMemberId"] = UuidToString(_Joint->GetInsertedMemberID());
            _Item["mode"] = _Joint->GetMode();
            _Item["clearance"] = _Joint->GetClearance();
            _Item["x"] = _Joint->GetX();
            _Item["y"] = _Joint->GetY();
            _Joints.emplace_back(_Item);
        }
        _Designer["joints"] = _Joints;

        for (const auto& [_Entity, _Run] : Collect<CGenerationRunComponent>(_Repository))
        {
            if (_ActiveProductID.is_nil() || _Run->GetProductID() != _ActiveProductID) continue;
            if (_ActiveProduct
                && !_ActiveProduct->GetActiveGenerationRunID().is_nil()
                && _Entity->GetID() != _ActiveProduct->GetActiveGenerationRunID()) continue;
            ObjectMap _Item;
            _Item["entityId"] = UuidToString(_Entity->GetID());
            _Item["templateId"] = _Run->GetTemplateID();
            _Item["templateVersion"] = _Run->GetTemplateVersion();
            _Item["status"] = _Run->GetStatus();
            _Item["partCount"] = _Run->GetPartCount();
            _Item["issueCount"] = _Run->GetIssueCount();
            _Item["packageDigest"] = _Run->GetPackageDigest();
            _Item["hasNeutralModel"] = !_Run->GetNeutralModel().empty();
            _Designer["generationRun"] = _Item;
            break;
        }

        ObjectMap _Response;
        _Response["tubeDesigner"] = _Designer;
        return _Response;
    }

    iCAX::Interaction::CInvocationResult HandleList(
        const iCAX::Interaction::CInvocation&,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_) throw std::invalid_argument("TubeDesigner.List requires a scene");
        return MakeResponse(Variant(BuildSnapshot(*Scene_, ApplicationContext_)));
    }

    iCAX::Interaction::CInvocationResult HandleActivateProduct(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_) throw std::invalid_argument("TubeDesigner.ActivateProduct requires a scene");
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _ProductID = ParseRequiredUuid(
            GetString(_Payload, "productEntityId"), "productEntityId");
        auto& _Repository = Scene_->Database();
        const auto _ProductEntity = _Repository.GetEntity(_ProductID);
        if (!GetComponent<CProductInstanceComponent>(_ProductEntity))
        {
            throw std::invalid_argument("TubeDesigner product instance does not exist");
        }
        const auto _Meta = _Repository.GetMetaEntity();
        if (!_Meta) throw std::runtime_error("TubeDesigner requires repository meta entity");
        const auto _Root = GetComponent<CTubeDesignerRootComponent>(_Meta);
        if (_Root && _Root->GetActiveProductID() == _ProductID)
        {
            return MakeResponse(Variant(BuildSnapshot(*Scene_, ApplicationContext_)));
        }

        auto& _Transaction = _Repository.BeginTransaction("Select TubeDesigner product instance");
        bool _CommitStarted = false;
        try
        {
            const iCAX::Data::PropertySet _RootProperties{
                { CTubeDesignerRootComponent::PropertyName_ActiveProductID, PropertyValue(_ProductID) }
            };
            if (_Root)
            {
                _Transaction.ModifyComponent(
                    _Meta->GetID(),
                    CTubeDesignerRootComponent::S_ClassName,
                    _RootProperties);
            }
            else
            {
                _Transaction.AttachComponent(
                    _Meta->GetID(),
                    CTubeDesignerRootComponent::S_ClassName,
                    _RootProperties);
            }
            std::string _Error;
            _CommitStarted = true;
            if (!_Repository.CommitTransaction(_Transaction, _Error))
            {
                throw std::runtime_error(_Error.empty()
                    ? "TubeDesigner failed to select product instance"
                    : _Error);
            }
        }
        catch (...)
        {
            if (!_CommitStarted)
            {
                try { _Repository.CancelTransaction(_Transaction); }
                catch (...) {}
            }
            throw;
        }
        return MakeResponse(Variant(BuildSnapshot(*Scene_, ApplicationContext_)));
    }

    const iCAX::TemplateRuntime::SOutputSet* FindOutputSet(
        const iCAX::TemplateRuntime::SNeutralModel& Model_, const std::string& strPurpose_)
    {
        const auto _Iterator = std::find_if(
            Model_.Outputs.begin(), Model_.Outputs.end(),
            [&strPurpose_](const auto& Output_) { return Output_.Purpose == strPurpose_; });
        return _Iterator == Model_.Outputs.end() ? nullptr : &*_Iterator;
    }

    const iCAX::TemplateRuntime::SModelItem& FindModelItem(
        const iCAX::TemplateRuntime::SNeutralModel& Model_, const std::string& strKey_)
    {
        const auto _Iterator = std::find_if(
            Model_.Items.begin(), Model_.Items.end(),
            [&strKey_](const auto& Item_) { return Item_.Key == strKey_; });
        if (_Iterator == Model_.Items.end())
            throw std::invalid_argument("neutral model output references a missing item: " + strKey_);
        return *_Iterator;
    }

    std::string OptionalPropertyString(
        const ObjectMap& Properties_, const std::string& strName_, const std::string& strDefault_ = {})
    {
        const auto _Iterator = Properties_.find(strName_);
        if (_Iterator == Properties_.end() || !_Iterator->second.Is<std::string>()) return strDefault_;
        return _Iterator->second.To<std::string>();
    }

    double OptionalPropertyNumber(
        const ObjectMap& Properties_, const std::string& strName_, double dDefault_ = 0.0)
    {
        const auto _Iterator = Properties_.find(strName_);
        if (_Iterator == Properties_.end()) return dDefault_;
        try { return ToDouble(_Iterator->second, strName_); }
        catch (...) { return dDefault_; }
    }

    std::uint64_t OptionalPropertyUInt64(
        const ObjectMap& Properties_, const std::string& strName_, std::uint64_t nDefault_ = 0)
    {
        const auto _Iterator = Properties_.find(strName_);
        if (_Iterator == Properties_.end()) return nDefault_;
        try
        {
            const auto _Value = ToDouble(_Iterator->second, strName_);
            if (_Value < 0.0 || std::floor(_Value) != _Value) return nDefault_;
            return static_cast<std::uint64_t>(_Value);
        }
        catch (...) { return nDefault_; }
    }

    STubeProfile OptionalTubeProfile(const ObjectMap& Properties_)
    {
        STubeProfile _Profile;
        _Profile.Type.clear();
        const auto _Iterator = Properties_.find("tubeDesigner.profile");
        if (_Iterator == Properties_.end() || !_Iterator->second.Is<ObjectMap>()) return _Profile;
        const auto _Properties = _Iterator->second.To<ObjectMap>();
        _Profile.Type = OptionalPropertyString(_Properties, "kind");
        _Profile.Width = OptionalPropertyNumber(_Properties, "width");
        _Profile.Depth = OptionalPropertyNumber(_Properties, "depth");
        _Profile.WallThickness = OptionalPropertyNumber(_Properties, "wallThickness");
        _Profile.CornerRadius = OptionalPropertyNumber(_Properties, "cornerRadius");
        return _Profile;
    }

    std::string ResolveProductCode(const SNeutralTemplateEvaluation& Evaluation_)
    {
        const auto _Identity = Evaluation_.Descriptor.Extensions.find("productIdentity");
        if (_Identity != Evaluation_.Descriptor.Extensions.end() && _Identity->second.Is<ObjectMap>())
        {
            const auto _IdentityObject = _Identity->second.To<ObjectMap>();
            const auto _CodeParameter = _IdentityObject.find("codeParameter");
            if (_CodeParameter != _IdentityObject.end() && _CodeParameter->second.Is<std::string>())
            {
                const auto _Value = Evaluation_.Parameters.find(_CodeParameter->second.To<std::string>());
                if (_Value != Evaluation_.Parameters.end() && _Value->second.Is<std::string>())
                    return _Value->second.To<std::string>();
            }
        }
        return Evaluation_.Descriptor.ID;
    }

    struct SNeutralPreviewMember final
    {
        const iCAX::TemplateRuntime::SModelItem* Item = nullptr;
        iCAX::Data::uuid EntityID;
        iCAX::Resource::CResourceReference PreviewResource;
        iCAX::Resource::CResourceReference FrontendGeometryResource;
        std::uint64_t Index = 0;
    };

    iCAX::Interaction::CInvocationResult GenerateNeutralPreview(
        const ObjectMap& Payload_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Project::ISceneContext& Scene_)
    {
        const auto _Evaluation = EvaluateNeutralTemplate(ApplicationContext_, Payload_);
        const auto _Geometry = iCAX::OpenCascade::EvaluateNeutralModel(_Evaluation.Model);
        const auto _DisplayOutput = FindOutputSet(_Evaluation.Model, "display");
        if (!_DisplayOutput || _DisplayOutput->ItemKeys.empty())
            throw std::runtime_error("Python template returned no display output items");
        const auto _ExportOutput = FindOutputSet(_Evaluation.Model, "export");
        if (!_ExportOutput || _ExportOutput->ItemKeys.empty())
            throw std::runtime_error("Python template returned no export output items");

        auto& _Repository = Scene_.Database();
        const auto _Meta = _Repository.GetMetaEntity();
        if (!_Meta) throw std::runtime_error("TubeDesigner requires repository meta entity");
        const auto _Root = GetComponent<CTubeDesignerRootComponent>(_Meta);
        std::shared_ptr<iCAX::Database::IEntity> _ExistingProductEntity;
        const auto _RequestedProductID = GetString(Payload_, "productEntityId");
        if (!_RequestedProductID.empty())
        {
            const auto _ExistingID = ParseRequiredUuid(_RequestedProductID, "productEntityId");
            _ExistingProductEntity = _Repository.GetEntity(_ExistingID);
            if (!GetComponent<CProductInstanceComponent>(_ExistingProductEntity))
                throw std::invalid_argument("TubeDesigner product instance does not exist");
        }
        const auto _ExistingProduct = GetComponent<CProductInstanceComponent>(_ExistingProductEntity);
        const auto _ProductID = _ExistingProductEntity
            ? _ExistingProductEntity->GetID() : iCAX::Data::GenerateNewUUID();
        const auto _RunID = iCAX::Data::GenerateNewUUID();
        const auto _ProductCode = ResolveProductCode(_Evaluation);
        const auto _InstanceName = GetString(
            Payload_, "instanceName",
            _ExistingProduct ? _ExistingProduct->GetName()
                : _Evaluation.Descriptor.DisplayName.Resolve("zh-CN") + " " + _ProductCode);
        const auto _CreatedAt = GetString(
            Payload_, "createdAt", _ExistingProduct ? _ExistingProduct->GetCreatedAt() : std::string());

        const auto _MaterialResource = EnsureDesignerMaterial(Scene_);
        std::vector<SNeutralPreviewMember> _Members;
        _Members.reserve(_DisplayOutput->ItemKeys.size());
        std::uint64_t _Index = 0;
        for (const auto& _ItemKey : _DisplayOutput->ItemKeys)
        {
            const auto& _Item = FindModelItem(_Evaluation.Model, _ItemKey);
            const auto _Representation = _Item.Representations.find("display");
            if (_Representation == _Item.Representations.end())
                throw std::runtime_error("neutral model item has no display representation: " + _Item.Key);
            const auto _EntityID = MakeStableEntityID(_ProductID, "member", _Item.Key);
            const auto _StablePrefix = "tube-designer/product/" + UuidToString(_ProductID)
                + "/item/" + _Item.Key;
            const auto _Name = _Item.DisplayName.Resolve("zh-CN");
            const auto _Preview = StoreBRep(
                Scene_, _StablePrefix + "/preview", _Name + " preview",
                _Geometry.At(_Representation->second));
            const auto _Frontend = iCAX::RenderInteraction::EnsureFrontendGeometryResource(
                Scene_.Resources(), _Preview.URL, iCAX::Render::ERenderGeometryKind::Mesh);
            _Members.push_back({ &_Item, _EntityID, _Preview, _Frontend, ++_Index });
        }

        const auto _ExistingIDs = CollectProductDesignIDs(_Repository, _ProductID);
        std::vector<iCAX::Data::uuid> _DesiredIDs{ _ProductID, _RunID };
        for (const auto& _Member : _Members) _DesiredIDs.push_back(_Member.EntityID);

        auto _Undo = _Repository.BeginUndoCommand("Generate neutral template preview");
        auto& _Transaction = _Repository.BeginTransaction("Update neutral template preview");
        bool _CommitStarted = false;
        try
        {
            for (const auto& _ID : _ExistingIDs)
            {
                if (std::find(_DesiredIDs.begin(), _DesiredIDs.end(), _ID) == _DesiredIDs.end())
                    _Transaction.DisposeEntity(_ID);
            }
            if (!_ExistingProductEntity) _Transaction.CreateEntity(_ProductID);
            QueueUpsertComponent(
                _Transaction, _ExistingProductEntity, _ProductID,
                CProductInstanceComponent::S_ClassName, {
                    { CProductInstanceComponent::PropertyName_ProductCode, PropertyValue(_ProductCode) },
                    { CProductInstanceComponent::PropertyName_Name, PropertyValue(_InstanceName) },
                    { CProductInstanceComponent::PropertyName_CreatedAt, PropertyValue(_CreatedAt) },
                    { CProductInstanceComponent::PropertyName_TemplateID, PropertyValue(_Evaluation.Descriptor.ID) },
                    { CProductInstanceComponent::PropertyName_TemplateVersion, PropertyValue(_Evaluation.Descriptor.Version) },
                    { CProductInstanceComponent::PropertyName_Parameters, PropertyValue(_Evaluation.Parameters) },
                    { CProductInstanceComponent::PropertyName_ActiveGenerationRunID, PropertyValue(_RunID) }
                });

            _Transaction.CreateEntity(_RunID);
            _Transaction.AttachComponent(_RunID, CGenerationRunComponent::S_ClassName, {
                { CGenerationRunComponent::PropertyName_ProductID, PropertyValue(_ProductID) },
                { CGenerationRunComponent::PropertyName_TemplateID, PropertyValue(_Evaluation.Descriptor.ID) },
                { CGenerationRunComponent::PropertyName_TemplateVersion, PropertyValue(_Evaluation.Descriptor.Version) },
                { CGenerationRunComponent::PropertyName_PartCount, PropertyValue(static_cast<unsigned long long>(_ExportOutput->ItemKeys.size())) },
                { CGenerationRunComponent::PropertyName_IssueCount, PropertyValue(static_cast<unsigned long long>(_Evaluation.Model.Diagnostics.size())) },
                { CGenerationRunComponent::PropertyName_PackageDigest, PropertyValue(_Evaluation.Descriptor.PackageDigest) },
                { CGenerationRunComponent::PropertyName_NeutralModel, PropertyValue(_Evaluation.Document) }
            });

            for (const auto& _Prepared : _Members)
            {
                const auto& _Item = *_Prepared.Item;
                const auto _Profile = OptionalTubeProfile(_Item.Properties);
                const auto _ExistingMember = _Repository.GetEntity(_Prepared.EntityID);
                if (!_ExistingMember) _Transaction.CreateEntity(_Prepared.EntityID);
                QueueUpsertComponent(
                    _Transaction, _ExistingMember, _Prepared.EntityID,
                    CAssemblyMemberComponent::S_ClassName, {
                        { CAssemblyMemberComponent::PropertyName_ProductID, PropertyValue(_ProductID) },
                        { CAssemblyMemberComponent::PropertyName_ManufacturingPartID, PropertyValue(iCAX::Data::uuid()) },
                        { CAssemblyMemberComponent::PropertyName_MemberIndex, PropertyValue(static_cast<unsigned long long>(_Prepared.Index)) },
                        { CAssemblyMemberComponent::PropertyName_StableKey, PropertyValue(_Item.Key) },
                        { CAssemblyMemberComponent::PropertyName_MemberType, PropertyValue(_Item.Children.empty() ? std::string("part") : std::string("assembly")) },
                        { CAssemblyMemberComponent::PropertyName_ChildPartCount, PropertyValue(static_cast<unsigned long long>(std::max<std::size_t>(1, _Item.Children.size()))) },
                        { CAssemblyMemberComponent::PropertyName_Role, PropertyValue(OptionalPropertyString(_Item.Properties, "group")) },
                        { CAssemblyMemberComponent::PropertyName_Name, PropertyValue(_Item.DisplayName.Resolve("zh-CN")) },
                        { CAssemblyMemberComponent::PropertyName_ProfileType, PropertyValue(_Profile.Type) },
                        { CAssemblyMemberComponent::PropertyName_SectionWidth, PropertyValue(_Profile.Width) },
                        { CAssemblyMemberComponent::PropertyName_SectionDepth, PropertyValue(_Profile.Depth) },
                        { CAssemblyMemberComponent::PropertyName_WallThickness, PropertyValue(_Profile.WallThickness) },
                        { CAssemblyMemberComponent::PropertyName_CornerRadius, PropertyValue(_Profile.CornerRadius) },
                        { CAssemblyMemberComponent::PropertyName_Length, PropertyValue(OptionalPropertyNumber(_Item.Properties, "length")) },
                        { CAssemblyMemberComponent::PropertyName_X1, PropertyValue(0.0) },
                        { CAssemblyMemberComponent::PropertyName_Y1, PropertyValue(0.0) },
                        { CAssemblyMemberComponent::PropertyName_X2, PropertyValue(0.0) },
                        { CAssemblyMemberComponent::PropertyName_Y2, PropertyValue(0.0) },
                        { CAssemblyMemberComponent::PropertyName_PreviewGeometryResourceID, PropertyValue(_Prepared.PreviewResource.URL) },
                        { CAssemblyMemberComponent::PropertyName_PreviewGeometryResourceVersion, PropertyValue(_Prepared.PreviewResource.nVersion) },
                        { CAssemblyMemberComponent::PropertyName_ItemProperties, PropertyValue(_Item.Properties) }
                    });
                QueueUpsertComponent(
                    _Transaction, _ExistingMember, _Prepared.EntityID,
                    iCAX::RenderInteraction::CRenderInstanceComponent::S_ClassName, {
                        { iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_GeometryResourceID, PropertyValue(_Prepared.FrontendGeometryResource.URL) },
                        { iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_GeometryResourceVersion, PropertyValue(_Prepared.FrontendGeometryResource.nVersion) },
                        { iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_MaterialResourceID, PropertyValue(_MaterialResource.URL) },
                        { iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_MaterialResourceVersion, PropertyValue(_MaterialResource.nVersion) }
                    });
                QueueUpsertComponent(
                    _Transaction, _ExistingMember, _Prepared.EntityID,
                    iCAX::Transform::CTransformComponent::S_ClassName, {
                        { iCAX::Transform::CTransformComponent::PropertyName_RollRadians, PropertyValue(0.0) }
                    });
            }

            const iCAX::Data::PropertySet _RootProperties{
                { CTubeDesignerRootComponent::PropertyName_ActiveProductID, PropertyValue(_ProductID) }
            };
            if (_Root) _Transaction.ModifyComponent(
                _Meta->GetID(), CTubeDesignerRootComponent::S_ClassName, _RootProperties);
            else _Transaction.AttachComponent(
                _Meta->GetID(), CTubeDesignerRootComponent::S_ClassName, _RootProperties);

            std::string _Error;
            _CommitStarted = true;
            if (!_Repository.CommitTransaction(_Transaction, _Error))
                throw std::runtime_error(_Error.empty()
                    ? "TubeDesigner failed to commit neutral template preview" : _Error);
        }
        catch (...)
        {
            if (!_CommitStarted)
            {
                try { _Repository.CancelTransaction(_Transaction); }
                catch (...) {}
            }
            throw;
        }
        _Undo->End();
        return MakeResponse(Variant(BuildSnapshot(Scene_, ApplicationContext_)));
    }

    iCAX::Interaction::CInvocationResult HandleGeneratePreview(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_) throw std::invalid_argument("TubeDesigner.GeneratePreview requires a scene");
        const auto _Payload = DecodeObjectPayload(Request_);
        if (IsPythonTemplate(GetString(_Payload, "templateId")))
            return GenerateNeutralPreview(_Payload, ApplicationContext_, *Scene_);
        const auto _Evaluation = EvaluateTemplate(_Payload);
        const auto& _Generated = _Evaluation.Product;
        auto& _Repository = Scene_->Database();
        const auto _Meta = _Repository.GetMetaEntity();
        if (!_Meta) throw std::runtime_error("TubeDesigner requires repository meta entity");
        const auto _Root = GetComponent<CTubeDesignerRootComponent>(_Meta);
        std::shared_ptr<iCAX::Database::IEntity> _ExistingProductEntity;
        const auto _RequestedProductID = GetString(_Payload, "productEntityId");
        if (!_RequestedProductID.empty())
        {
            const auto _ProductID = ParseRequiredUuid(_RequestedProductID, "productEntityId");
            _ExistingProductEntity = _Repository.GetEntity(_ProductID);
            if (!GetComponent<CProductInstanceComponent>(_ExistingProductEntity))
            {
                throw std::invalid_argument("TubeDesigner product instance does not exist");
            }
        }
        const auto _ExistingProduct = GetComponent<CProductInstanceComponent>(_ExistingProductEntity);
        const auto _ProductID = _ExistingProductEntity
            ? _ExistingProductEntity->GetID()
            : iCAX::Data::GenerateNewUUID();
        const auto _RunID = iCAX::Data::GenerateNewUUID();
        const auto _InstanceName = GetString(
            _Payload,
            "instanceName",
            _ExistingProduct ? _ExistingProduct->GetName() : "防盗窗 " + _Generated.ProductCode);
        const auto _CreatedAt = GetString(
            _Payload,
            "createdAt",
            _ExistingProduct ? _ExistingProduct->GetCreatedAt() : std::string());

        std::vector<TopoDS_Shape> _PreviewShapes;
        _PreviewShapes.reserve(_Generated.Parts.size());
        for (const auto& _Part : _Generated.Parts)
        {
            _PreviewShapes.push_back(MakePreviewShape(_Part));
        }

        const auto _MaterialResource = EnsureDesignerMaterial(*Scene_);
        const auto _StablePartKeys = MakeStablePartKeys(_Generated);
        std::vector<SPreparedPart> _PreparedParts;
        _PreparedParts.reserve(_Generated.Parts.size());
        std::vector<SPreparedPreviewMember> _PreparedPreviewMembers;
        std::map<std::string, std::size_t> _PreviewMemberOffsets;
        for (std::size_t _Index = 0; _Index < _Generated.Parts.size(); ++_Index)
        {
            const auto& _GeneratedPart = _Generated.Parts[_Index];
            const auto& _StableKey = _StablePartKeys.at(_Index);
            const auto _PreviewMemberStableKey = MakePreviewMemberStableKey(
                _GeneratedPart, _StableKey);
            const auto _MemberID = MakeStableEntityID(
                _ProductID, "member", _PreviewMemberStableKey);
            _PreparedParts.push_back({
                _GeneratedPart,
                _StableKey,
                _MemberID
            });

            const auto [_Iterator, _Inserted] = _PreviewMemberOffsets.emplace(
                _PreviewMemberStableKey, _PreparedPreviewMembers.size());
            if (_Inserted)
            {
                _PreparedPreviewMembers.push_back({
                    _PreviewMemberStableKey,
                    _MemberID,
                    _GeneratedPart.PreviewAssemblyKey.empty() ? "part" : "assembly",
                    _GeneratedPart.PreviewAssemblyRole.empty()
                        ? _GeneratedPart.Role : _GeneratedPart.PreviewAssemblyRole,
                    _GeneratedPart.PreviewAssemblyName.empty()
                        ? _GeneratedPart.PartNumber : _GeneratedPart.PreviewAssemblyName,
                    _GeneratedPart.Index,
                    {},
                    {},
                    {}
                });
            }
            auto& _PreviewMember = _PreparedPreviewMembers.at(_Iterator->second);
            if (_PreviewMember.MemberID != _MemberID)
                throw std::runtime_error("TubeDesigner preview assembly key resolved to inconsistent IDs");
            _PreviewMember.PartOffsets.push_back(_Index);
        }

        for (auto& _PreviewMember : _PreparedPreviewMembers)
        {
            TopoDS_Shape _PreviewShape;
            if (_PreviewMember.PartOffsets.size() == 1 && _PreviewMember.MemberType == "part")
            {
                _PreviewShape = _PreviewShapes.at(_PreviewMember.PartOffsets.front());
            }
            else
            {
                TopoDS_Compound _Compound;
                BRep_Builder _Builder;
                _Builder.MakeCompound(_Compound);
                for (const auto _PartOffset : _PreviewMember.PartOffsets)
                    _Builder.Add(_Compound, _PreviewShapes.at(_PartOffset));
                _PreviewShape = _Compound;
            }
            const auto _StablePrefix = "tube-designer/product/" + UuidToString(_ProductID)
                + (_PreviewMember.MemberType == "part" ? "/part/" : "/")
                + _PreviewMember.StableKey;
            _PreviewMember.PreviewResource = StoreBRep(
                *Scene_, _StablePrefix + "/preview", _PreviewMember.Name + " preview", _PreviewShape);
            _PreviewMember.FrontendGeometryResource =
                iCAX::RenderInteraction::EnsureFrontendGeometryResource(
                    Scene_->Resources(), _PreviewMember.PreviewResource.URL,
                    iCAX::Render::ERenderGeometryKind::Mesh);
        }

        std::vector<SPreparedJoint> _PreparedJoints;
        _PreparedJoints.reserve(_Generated.Joints.size());
        for (const auto& _GeneratedJoint : _Generated.Joints)
        {
            const auto& _Target = _PreparedParts.at(
                static_cast<std::size_t>(_GeneratedJoint.TargetPartIndex - 1));
            const auto& _Inserted = _PreparedParts.at(
                static_cast<std::size_t>(_GeneratedJoint.InsertedPartIndex - 1));
            if (_Target.MemberID == _Inserted.MemberID) continue;
            _PreparedJoints.push_back({
                _GeneratedJoint,
                MakeStableEntityID(
                    _ProductID,
                    "joint",
                    _GeneratedJoint.StableKey.empty()
                        ? _Target.StableKey + "/" + _Inserted.StableKey
                        : _GeneratedJoint.StableKey)
            });
        }

        const auto _ExistingIDs = CollectProductDesignIDs(_Repository, _ProductID);
        std::vector<iCAX::Data::uuid> _DesiredIDs{ _ProductID, _RunID };
        for (const auto& _Prepared : _PreparedPreviewMembers)
        {
            _DesiredIDs.push_back(_Prepared.MemberID);
        }
        for (const auto& _Prepared : _PreparedJoints)
        {
            _DesiredIDs.push_back(_Prepared.JointID);
        }

        auto _Undo = _Repository.BeginUndoCommand("Generate TubeDesigner preview");
        auto& _Transaction = _Repository.BeginTransaction("Update TubeDesigner preview");
        bool _CommitStarted = false;
        try
        {
            for (const auto& _ID : _ExistingIDs)
            {
                if (std::find(_DesiredIDs.begin(), _DesiredIDs.end(), _ID) == _DesiredIDs.end())
                {
                    _Transaction.DisposeEntity(_ID);
                }
            }

            if (!_ExistingProductEntity) _Transaction.CreateEntity(_ProductID);
            QueueUpsertComponent(_Transaction, _ExistingProductEntity, _ProductID, CProductInstanceComponent::S_ClassName, {
                { CProductInstanceComponent::PropertyName_ProductCode, PropertyValue(_Generated.ProductCode) },
                { CProductInstanceComponent::PropertyName_Name, PropertyValue(_InstanceName) },
                { CProductInstanceComponent::PropertyName_CreatedAt, PropertyValue(_CreatedAt) },
                { CProductInstanceComponent::PropertyName_TemplateID, PropertyValue(_Generated.TemplateID) },
                { CProductInstanceComponent::PropertyName_TemplateVersion, PropertyValue(_Generated.TemplateVersion) },
                { CProductInstanceComponent::PropertyName_Parameters, PropertyValue(_Evaluation.Parameters) },
                { CProductInstanceComponent::PropertyName_ActiveGenerationRunID, PropertyValue(_RunID) }
            });

            _Transaction.CreateEntity(_RunID);
            _Transaction.AttachComponent(_RunID, CGenerationRunComponent::S_ClassName, {
                { CGenerationRunComponent::PropertyName_ProductID, PropertyValue(_ProductID) },
                { CGenerationRunComponent::PropertyName_TemplateID, PropertyValue(_Generated.TemplateID) },
                { CGenerationRunComponent::PropertyName_TemplateVersion, PropertyValue(_Generated.TemplateVersion) },
                { CGenerationRunComponent::PropertyName_PartCount, PropertyValue(static_cast<unsigned long long>(_Generated.Parts.size())) }
            });

            for (const auto& _Prepared : _PreparedPreviewMembers)
            {
                const auto& _Part = _Generated.Parts.at(_Prepared.PartOffsets.front());
                auto _CombinedLength = 0.0;
                for (const auto _PartOffset : _Prepared.PartOffsets)
                    _CombinedLength += _Generated.Parts.at(_PartOffset).Length;
                const auto _IsAssembly = _Prepared.MemberType == "assembly";
                const auto _ExistingMember = _Repository.GetEntity(_Prepared.MemberID);
                if (!_ExistingMember) _Transaction.CreateEntity(_Prepared.MemberID);
                QueueUpsertComponent(_Transaction, _ExistingMember, _Prepared.MemberID, CAssemblyMemberComponent::S_ClassName, {
                    { CAssemblyMemberComponent::PropertyName_ProductID, PropertyValue(_ProductID) },
                    { CAssemblyMemberComponent::PropertyName_ManufacturingPartID, PropertyValue(iCAX::Data::uuid()) },
                    { CAssemblyMemberComponent::PropertyName_MemberIndex, PropertyValue(static_cast<unsigned long long>(_Prepared.MemberIndex)) },
                    { CAssemblyMemberComponent::PropertyName_StableKey, PropertyValue(_Prepared.StableKey) },
                    { CAssemblyMemberComponent::PropertyName_MemberType, PropertyValue(_Prepared.MemberType) },
                    { CAssemblyMemberComponent::PropertyName_ChildPartCount, PropertyValue(static_cast<unsigned long long>(_Prepared.PartOffsets.size())) },
                    { CAssemblyMemberComponent::PropertyName_Role, PropertyValue(_Prepared.Role) },
                    { CAssemblyMemberComponent::PropertyName_Name, PropertyValue(_Prepared.Name) },
                    { CAssemblyMemberComponent::PropertyName_ProfileType, PropertyValue(_IsAssembly ? std::string("assembly") : _Part.Profile.Type) },
                    { CAssemblyMemberComponent::PropertyName_SectionWidth, PropertyValue(_IsAssembly ? 0.0 : _Part.Profile.Width) },
                    { CAssemblyMemberComponent::PropertyName_SectionDepth, PropertyValue(_IsAssembly ? 0.0 : _Part.Profile.Depth) },
                    { CAssemblyMemberComponent::PropertyName_WallThickness, PropertyValue(_IsAssembly ? 0.0 : _Part.Profile.WallThickness) },
                    { CAssemblyMemberComponent::PropertyName_CornerRadius, PropertyValue(_IsAssembly ? 0.0 : _Part.Profile.CornerRadius) },
                    { CAssemblyMemberComponent::PropertyName_Length, PropertyValue(_CombinedLength) },
                    { CAssemblyMemberComponent::PropertyName_X1, PropertyValue(_Part.X1) },
                    { CAssemblyMemberComponent::PropertyName_Y1, PropertyValue(_Part.Y1) },
                    { CAssemblyMemberComponent::PropertyName_X2, PropertyValue(_Part.X2) },
                    { CAssemblyMemberComponent::PropertyName_Y2, PropertyValue(_Part.Y2) },
                    { CAssemblyMemberComponent::PropertyName_PreviewGeometryResourceID, PropertyValue(_Prepared.PreviewResource.URL) },
                    { CAssemblyMemberComponent::PropertyName_PreviewGeometryResourceVersion, PropertyValue(_Prepared.PreviewResource.nVersion) }
                });
                QueueUpsertComponent(
                    _Transaction,
                    _ExistingMember,
                    _Prepared.MemberID,
                    iCAX::RenderInteraction::CRenderInstanceComponent::S_ClassName,
                    {
                        { iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_GeometryResourceID, PropertyValue(_Prepared.FrontendGeometryResource.URL) },
                        { iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_GeometryResourceVersion, PropertyValue(_Prepared.FrontendGeometryResource.nVersion) },
                        { iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_MaterialResourceID, PropertyValue(_MaterialResource.URL) },
                        { iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_MaterialResourceVersion, PropertyValue(_MaterialResource.nVersion) }
                    });
                QueueUpsertComponent(
                    _Transaction,
                    _ExistingMember,
                    _Prepared.MemberID,
                    iCAX::Transform::CTransformComponent::S_ClassName,
                    {
                        { iCAX::Transform::CTransformComponent::PropertyName_RollRadians, PropertyValue(kPreviewRollRadians) }
                    });

            }

            for (const auto& _PreparedJoint : _PreparedJoints)
            {
                const auto& _GeneratedJoint = _PreparedJoint.Generated;
                const auto& _Target = _PreparedParts.at(
                    static_cast<std::size_t>(_GeneratedJoint.TargetPartIndex - 1));
                const auto& _Inserted = _PreparedParts.at(
                    static_cast<std::size_t>(_GeneratedJoint.InsertedPartIndex - 1));
                const auto _ExistingJoint = _Repository.GetEntity(_PreparedJoint.JointID);
                if (!_ExistingJoint) _Transaction.CreateEntity(_PreparedJoint.JointID);
                QueueUpsertComponent(_Transaction, _ExistingJoint, _PreparedJoint.JointID, CJointIntentComponent::S_ClassName, {
                    { CJointIntentComponent::PropertyName_ProductID, PropertyValue(_ProductID) },
                    { CJointIntentComponent::PropertyName_TargetMemberID, PropertyValue(_Target.MemberID) },
                    { CJointIntentComponent::PropertyName_InsertedMemberID, PropertyValue(_Inserted.MemberID) },
                    { CJointIntentComponent::PropertyName_Mode, PropertyValue(_GeneratedJoint.Mode) },
                    { CJointIntentComponent::PropertyName_Clearance, PropertyValue(_GeneratedJoint.Clearance) },
                    { CJointIntentComponent::PropertyName_X, PropertyValue(_GeneratedJoint.X) },
                    { CJointIntentComponent::PropertyName_Y, PropertyValue(_GeneratedJoint.Y) }
                });
            }

            const iCAX::Data::PropertySet _RootProperties{
                { CTubeDesignerRootComponent::PropertyName_ActiveProductID, PropertyValue(_ProductID) }
            };
            if (_Root)
            {
                _Transaction.ModifyComponent(
                    _Meta->GetID(),
                    CTubeDesignerRootComponent::S_ClassName,
                    _RootProperties);
            }
            else
            {
                _Transaction.AttachComponent(
                    _Meta->GetID(),
                    CTubeDesignerRootComponent::S_ClassName,
                    _RootProperties);
            }

            std::string _Error;
            _CommitStarted = true;
            if (!_Repository.CommitTransaction(_Transaction, _Error))
            {
                throw std::runtime_error(_Error.empty()
                    ? "TubeDesigner failed to commit generated product"
                    : _Error);
            }
        }
        catch (...)
        {
            if (!_CommitStarted)
            {
                try { _Repository.CancelTransaction(_Transaction); }
                catch (...) {}
            }
            throw;
        }

        _Undo->End();
        return MakeResponse(Variant(BuildSnapshot(*Scene_, ApplicationContext_)));
    }

    struct SPreparedManufacturingPart final
    {
        std::uint64_t Index = 0;
        std::string StableKey;
        std::string PartNumber;
        std::string Role;
        std::uint64_t Quantity = 1;
        std::string ProfileType;
        double SectionWidth = 0.0;
        double SectionDepth = 0.0;
        double WallThickness = 0.0;
        double CornerRadius = 0.0;
        double Length = 0.0;
        ObjectMap ItemProperties;
        iCAX::Data::uuid MemberID;
        iCAX::Data::uuid PartID;
        iCAX::Resource::CResourceReference ManufacturingResource;
        iCAX::Resource::CResourceReference ThumbnailResource;
        std::string FileName;
    };

    struct SPreparedProductDisassembly final
    {
        iCAX::Data::uuid ProductID;
        iCAX::Data::uuid GenerationRunID;
        std::vector<SPreparedManufacturingPart> Parts;
    };

    SPreparedProductDisassembly PrepareNeutralModelDisassembly(
        iCAX::Project::ISceneContext& Scene_,
        const iCAX::Data::uuid& ProductID_,
        const iCAX::Data::uuid& GenerationRunID_,
        const CProductInstanceComponent& Product_,
        const CGenerationRunComponent& Run_)
    {
        const auto _Document = Run_.GetNeutralModel();
        if (_Document.empty())
            throw std::invalid_argument("generation run has no stored neutral model");
        const auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(Variant(_Document));
        if (_Model.TemplateID != Product_.GetTemplateID()
            || _Model.TemplateVersion != Product_.GetTemplateVersion()
            || (!Run_.GetPackageDigest().empty() && _Model.PackageDigest != Run_.GetPackageDigest()))
        {
            throw std::runtime_error("stored neutral model identity does not match its generation run");
        }
        const auto _Output = FindOutputSet(_Model, "export");
        if (!_Output || _Output->ItemKeys.empty())
            throw std::runtime_error("stored neutral model has no export output");
        const auto _Geometry = iCAX::OpenCascade::EvaluateNeutralModel(_Model);

        SPreparedProductDisassembly _Prepared{ ProductID_, GenerationRunID_, {} };
        _Prepared.Parts.reserve(_Output->ItemKeys.size());
        std::uint64_t _Index = 0;
        std::map<std::string, std::uint64_t> _CategoryOrdinals;
        for (const auto& _ItemKey : _Output->ItemKeys)
        {
            const auto& _Item = FindModelItem(_Model, _ItemKey);
            const auto _Representation = _Item.Representations.find("export");
            if (_Representation == _Item.Representations.end())
                throw std::runtime_error("neutral model item has no export representation: " + _Item.Key);
            ++_Index;
            auto _ItemProperties = _Item.Properties;
            const auto _CategoryKey = OptionalPropertyString(
                _ItemProperties, "manufacturing.categoryKey", _Item.Key);
            const auto _CategoryName = OptionalPropertyString(
                _ItemProperties, "manufacturing.categoryName", _Item.DisplayName.Resolve("zh-CN"));
            const auto _CategoryOrdinal = ++_CategoryOrdinals[_CategoryKey];
            const auto _PartNumber = MakeManufacturingPartNumber(
                Product_.GetName(), _CategoryName.empty() ? "零件" : _CategoryName,
                _CategoryOrdinal);
            _ItemProperties["manufacturing.categoryKey"] = _CategoryKey;
            _ItemProperties["manufacturing.categoryName"] = _CategoryName.empty()
                ? std::string("零件")
                : _CategoryName;
            _ItemProperties["manufacturing.logicalPartKey"] = _Item.Key;
            _ItemProperties["manufacturing.segmentIndex"] = 1ull;
            _ItemProperties["manufacturing.segmentCount"] = 1ull;
            const auto _StablePrefix = "tube-designer/product/" + UuidToString(ProductID_)
                + "/item/" + _Item.Key;
            const auto _Profile = OptionalTubeProfile(_ItemProperties);
            const auto _Resource = StoreBRep(
                Scene_, _StablePrefix + "/manufacturing",
                _PartNumber + " manufacturing", _Geometry.At(_Representation->second));
            const auto _Thumbnail = iCAX::RenderInteraction::EnsureFrontendGeometryResource(
                Scene_.Resources(), _Resource.URL, iCAX::Render::ERenderGeometryKind::Mesh);
            _Prepared.Parts.push_back({
                _Index,
                _Item.Key,
                _PartNumber,
                OptionalPropertyString(_ItemProperties, "group"),
                std::max<std::uint64_t>(1, OptionalPropertyUInt64(_ItemProperties, "quantity", 1)),
                _Profile.Type,
                _Profile.Width,
                _Profile.Depth,
                _Profile.WallThickness,
                _Profile.CornerRadius,
                OptionalPropertyNumber(_ItemProperties, "length"),
                _ItemProperties,
                MakeStableEntityID(ProductID_, "member", _Item.Key),
                MakeStableEntityID(ProductID_, "manufacturing-part", _Item.Key),
                _Resource,
                _Thumbnail,
                MakeStepFileName(_PartNumber)
            });
        }
        if (_Prepared.Parts.size() != static_cast<std::size_t>(Run_.GetPartCount()))
            throw std::runtime_error("stored neutral model export count does not match its generation run");
        return _Prepared;
    }

    SPreparedProductDisassembly PrepareProductDisassembly(
        iCAX::Project::ISceneContext& Scene_,
        const iCAX::Data::uuid& ProductID_)
    {
        auto& _Repository = Scene_.Database();
        const auto _ProductEntity = _Repository.GetEntity(ProductID_);
        const auto _Product = GetComponent<CProductInstanceComponent>(_ProductEntity);
        if (!_Product) throw std::invalid_argument("TubeDesigner product instance does not exist");
        const auto _GenerationRunID = _Product->GetActiveGenerationRunID();
        if (_GenerationRunID.is_nil())
        {
            throw std::runtime_error("TubeDesigner product instance has no committed preview");
        }

        const auto _Run = GetComponent<CGenerationRunComponent>(
            _Repository.GetEntity(_GenerationRunID));
        if (!_Run) throw std::runtime_error("TubeDesigner product generation run does not exist");
        if (!_Run->GetNeutralModel().empty())
        {
            return PrepareNeutralModelDisassembly(
                Scene_, ProductID_, _GenerationRunID, *_Product, *_Run);
        }

        auto _TemplatePayload = _Product->GetParameters();
        _TemplatePayload["templateId"] = _Product->GetTemplateID();
        _TemplatePayload["templateVersion"] = _Product->GetTemplateVersion();
        const auto _Evaluation = EvaluateTemplate(_TemplatePayload);
        const auto& _Generated = _Evaluation.Product;
        std::vector<TopoDS_Shape> _ManufacturingShapes;
        _ManufacturingShapes.reserve(_Generated.Parts.size());
        for (const auto& _Part : _Generated.Parts)
        {
            _ManufacturingShapes.push_back(MakeManufacturingShape(_Part));
        }
        for (const auto& _Joint : _Generated.Joints)
        {
            const auto _Target = static_cast<std::size_t>(_Joint.TargetPartIndex - 1);
            const auto _Inserted = static_cast<std::size_t>(_Joint.InsertedPartIndex - 1);
            if (_Target >= _ManufacturingShapes.size() || _Inserted >= _Generated.Parts.size())
            {
                throw std::runtime_error("TubeDesigner generated an invalid joint reference");
            }
            if (_Joint.Mode != "through") continue;
            if (_Generated.Parts[_Target].ManufacturingGeometry)
            {
                // Assembly openings are positioned on the folded preview. The unfolded
                // V-groove tube has its own explicit manufacturing geometry and must not
                // be cut using folded XY coordinates.
                continue;
            }
            const auto _Cutter = MakeTubeShape(
                _Generated.Parts[_Inserted], _Joint.Clearance, false);
            BRepAlgoAPI_Cut _Cut(_ManufacturingShapes[_Target], _Cutter);
            if (!_Cut.IsDone()) throw std::runtime_error("TubeDesigner joint boolean failed");
            _ManufacturingShapes[_Target] = _Cut.Shape();
        }

        const auto _StableKeys = MakeStablePartKeys(_Generated);
        SPreparedProductDisassembly _Prepared{ ProductID_, _GenerationRunID, {} };
        _Prepared.Parts.reserve(_Generated.Parts.size());
        for (std::size_t _Index = 0; _Index < _Generated.Parts.size(); ++_Index)
        {
            const auto& _Part = _Generated.Parts[_Index];
            const auto& _StableKey = _StableKeys[_Index];
            const auto _StablePrefix = "tube-designer/product/" + UuidToString(ProductID_)
                + "/part/" + _StableKey;
            const auto _Resource = StoreBRep(
                Scene_, _StablePrefix + "/manufacturing",
                _Part.PartNumber + " manufacturing", _ManufacturingShapes[_Index]);
            const auto _Thumbnail = iCAX::RenderInteraction::EnsureFrontendGeometryResource(
                Scene_.Resources(), _Resource.URL,
                iCAX::Render::ERenderGeometryKind::Mesh);
            _Prepared.Parts.push_back({
                _Part.Index,
                _StableKey,
                _Part.PartNumber,
                _Part.Role,
                1,
                _Part.Profile.Type,
                _Part.Profile.Width,
                _Part.Profile.Depth,
                _Part.Profile.WallThickness,
                _Part.Profile.CornerRadius,
                _Part.Length,
                {},
                MakeStableEntityID(ProductID_, "member",
                    MakePreviewMemberStableKey(_Part, _StableKey)),
                MakeStableEntityID(ProductID_, "manufacturing-part", _StableKey),
                _Resource,
                _Thumbnail,
                MakeStepFileName(_Part.PartNumber)
            });
        }
        return _Prepared;
    }

    iCAX::Interaction::CInvocationResult HandleDisassemble(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_) throw std::invalid_argument("TubeDesigner.Disassemble requires a scene");
        const auto _Payload = DecodeObjectPayload(Request_);
        auto& _Repository = Scene_->Database();
        const auto _Meta = _Repository.GetMetaEntity();
        const auto _Root = GetComponent<CTubeDesignerRootComponent>(_Meta);
        std::vector<iCAX::Data::uuid> _ProductIDs;
        if (const auto _Selection = _Payload.find("productEntityIds"); _Selection != _Payload.end())
        {
            if (!_Selection->second.Is<VariantArray>())
            {
                throw std::invalid_argument("productEntityIds must be an array");
            }
            for (const auto& _Item : _Selection->second.To<VariantArray>())
            {
                if (!_Item.Is<std::string>())
                {
                    throw std::invalid_argument("productEntityIds must contain uuid strings");
                }
                const auto _ProductID = ParseRequiredUuid(_Item.To<std::string>(), "productEntityIds");
                if (std::find(_ProductIDs.begin(), _ProductIDs.end(), _ProductID) == _ProductIDs.end())
                {
                    _ProductIDs.push_back(_ProductID);
                }
            }
        }
        if (_ProductIDs.empty() && _Root && !_Root->GetActiveProductID().is_nil())
        {
            _ProductIDs.push_back(_Root->GetActiveProductID());
        }
        if (_ProductIDs.empty())
        {
            throw std::invalid_argument("select at least one TubeDesigner product instance");
        }

        std::vector<SPreparedProductDisassembly> _PreparedProducts;
        _PreparedProducts.reserve(_ProductIDs.size());
        for (const auto& _ProductID : _ProductIDs)
        {
            _PreparedProducts.push_back(PrepareProductDisassembly(*Scene_, _ProductID));
        }

        auto _Undo = _Repository.BeginUndoCommand("Disassemble TubeDesigner product instances");
        auto& _Transaction = _Repository.BeginTransaction("Create TubeDesigner manufacturing groups");
        bool _CommitStarted = false;
        try
        {
            for (const auto& _PreparedProduct : _PreparedProducts)
            {
                std::vector<iCAX::Data::uuid> _DesiredPartIDs;
                _DesiredPartIDs.reserve(_PreparedProduct.Parts.size());
                for (const auto& _Prepared : _PreparedProduct.Parts)
                {
                    _DesiredPartIDs.push_back(_Prepared.PartID);
                }
                for (const auto& [_Entity, _Part] : Collect<CManufacturingPartComponent>(_Repository))
                {
                    if (_Part->GetProductID() == _PreparedProduct.ProductID
                        && std::find(_DesiredPartIDs.begin(), _DesiredPartIDs.end(), _Entity->GetID())
                            == _DesiredPartIDs.end())
                    {
                        _Transaction.DisposeEntity(_Entity->GetID());
                    }
                }
                for (const auto& _Prepared : _PreparedProduct.Parts)
                {
                    const auto _ExistingPart = _Repository.GetEntity(_Prepared.PartID);
                    if (!_ExistingPart) _Transaction.CreateEntity(_Prepared.PartID);
                    QueueUpsertComponent(
                        _Transaction, _ExistingPart, _Prepared.PartID,
                        CManufacturingPartComponent::S_ClassName, {
                            { CManufacturingPartComponent::PropertyName_ProductID, PropertyValue(_PreparedProduct.ProductID) },
                            { CManufacturingPartComponent::PropertyName_SourceMemberID, PropertyValue(_Prepared.MemberID) },
                            { CManufacturingPartComponent::PropertyName_GenerationRunID, PropertyValue(_PreparedProduct.GenerationRunID) },
                            { CManufacturingPartComponent::PropertyName_PartIndex, PropertyValue(static_cast<unsigned long long>(_Prepared.Index)) },
                            { CManufacturingPartComponent::PropertyName_StableKey, PropertyValue(_Prepared.StableKey) },
                            { CManufacturingPartComponent::PropertyName_PartNumber, PropertyValue(_Prepared.PartNumber) },
                            { CManufacturingPartComponent::PropertyName_Role, PropertyValue(_Prepared.Role) },
                            { CManufacturingPartComponent::PropertyName_Quantity, PropertyValue(static_cast<unsigned long long>(_Prepared.Quantity)) },
                            { CManufacturingPartComponent::PropertyName_ProfileType, PropertyValue(_Prepared.ProfileType) },
                            { CManufacturingPartComponent::PropertyName_SectionWidth, PropertyValue(_Prepared.SectionWidth) },
                            { CManufacturingPartComponent::PropertyName_SectionDepth, PropertyValue(_Prepared.SectionDepth) },
                            { CManufacturingPartComponent::PropertyName_WallThickness, PropertyValue(_Prepared.WallThickness) },
                            { CManufacturingPartComponent::PropertyName_CornerRadius, PropertyValue(_Prepared.CornerRadius) },
                            { CManufacturingPartComponent::PropertyName_Length, PropertyValue(_Prepared.Length) },
                            { CManufacturingPartComponent::PropertyName_ManufacturingGeometryResourceID, PropertyValue(_Prepared.ManufacturingResource.URL) },
                            { CManufacturingPartComponent::PropertyName_ManufacturingGeometryResourceVersion, PropertyValue(_Prepared.ManufacturingResource.nVersion) },
                            { CManufacturingPartComponent::PropertyName_ThumbnailGeometryResourceID, PropertyValue(_Prepared.ThumbnailResource.URL) },
                            { CManufacturingPartComponent::PropertyName_ThumbnailGeometryResourceVersion, PropertyValue(_Prepared.ThumbnailResource.nVersion) },
                            { CManufacturingPartComponent::PropertyName_FileName, PropertyValue(_Prepared.FileName) },
                            { CManufacturingPartComponent::PropertyName_ItemProperties, PropertyValue(_Prepared.ItemProperties) }
                        });
                    const auto _Member = _Repository.GetEntity(_Prepared.MemberID);
                    if (_Member && _Member->HasComponent(CAssemblyMemberComponent::S_ClassName))
                    {
                        _Transaction.ModifyComponent(
                            _Prepared.MemberID, CAssemblyMemberComponent::S_ClassName, {
                            { CAssemblyMemberComponent::PropertyName_ManufacturingPartID, PropertyValue(
                                std::count_if(
                                    _PreparedProduct.Parts.begin(), _PreparedProduct.Parts.end(),
                                    [&_Prepared](const auto& Item_) {
                                        return Item_.MemberID == _Prepared.MemberID;
                                    }) == 1
                                    ? _Prepared.PartID
                                    : iCAX::Data::uuid()) }
                            });
                    }
                }
            }

            std::string _Error;
            _CommitStarted = true;
            if (!_Repository.CommitTransaction(_Transaction, _Error))
            {
                throw std::runtime_error(_Error.empty()
                    ? "TubeDesigner failed to commit disassembly"
                    : _Error);
            }
        }
        catch (...)
        {
            if (!_CommitStarted)
            {
                try { _Repository.CancelTransaction(_Transaction); }
                catch (...) {}
            }
            throw;
        }
        _Undo->End();
        return MakeResponse(Variant(BuildSnapshot(*Scene_, ApplicationContext_)));
    }

    iCAX::Interaction::CInvocationResult HandleMeasurePartGeometry(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_) throw std::invalid_argument("TubeDesigner.MeasurePartGeometry requires a scene");
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _PartID = ParseRequiredUuid(
            GetString(_Payload, "partEntityId"), "partEntityId");
        auto& _Repository = Scene_->Database();
        const auto _PartEntity = _Repository.GetEntity(_PartID);
        const auto _Part = GetComponent<CManufacturingPartComponent>(_PartEntity);
        if (!_Part)
        {
            throw std::invalid_argument("manufacturing part does not exist");
        }
        const auto _Product = GetComponent<CProductInstanceComponent>(
            _Repository.GetEntity(_Part->GetProductID()));
        if (!_Product
            || _Product->GetActiveGenerationRunID() != _Part->GetGenerationRunID())
        {
            throw std::runtime_error("manufacturing part is stale");
        }

        const auto _ResourceID = _Part->GetManufacturingGeometryResourceID();
        const auto _ResourceVersion = _Part->GetManufacturingGeometryResourceVersion();
        const auto _ExpectedVersion = GetUInt64(
            _Payload, "resourceVersion", _ResourceVersion);
        if (_ResourceID.empty() || _ResourceVersion == 0)
        {
            throw std::runtime_error("manufacturing part has no final BRep resource");
        }
        if (_ExpectedVersion != _ResourceVersion)
        {
            throw std::runtime_error("manufacturing geometry version changed; reopen the part");
        }

        const auto _CacheKey = _ResourceID + "@" + std::to_string(_ResourceVersion);
        static std::mutex _CacheMutex;
        static std::map<std::string, ObjectMap> _Cache;
        {
            const std::lock_guard _Lock(_CacheMutex);
            const auto _Cached = _Cache.find(_CacheKey);
            if (_Cached != _Cache.end())
            {
                return MakeResponse(Variant(_Cached->second));
            }
        }

        const auto _BRep = Scene_->Resources().Get<iCAX::GeometryData::BRepModel>(
            _ResourceID, _ResourceVersion);
        if (!_BRep)
        {
            throw std::runtime_error("final BRep resource version is not available");
        }
        const auto _Rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(*_BRep);
        if (!_Rebuilt.bOK || _Rebuilt.Shape.IsNull())
        {
            std::ostringstream _Message;
            _Message << "无法读取最终零件几何";
            for (const auto& _Diagnostic : _Rebuilt.Diagnostics)
            {
                _Message << ": " << _Diagnostic;
            }
            throw std::runtime_error(_Message.str());
        }
        auto _Measurement = MeasureFinalPartGeometry(
            _Rebuilt.Shape, _ResourceID, _ResourceVersion);
        _Measurement["partEntityId"] = UuidToString(_PartID);
        {
            const std::lock_guard _Lock(_CacheMutex);
            _Cache[_CacheKey] = _Measurement;
        }
        return MakeResponse(Variant(_Measurement));
    }

    iCAX::Interaction::CInvocationResult HandleExportSelected(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_) throw std::invalid_argument("TubeDesigner.ExportSelected requires a scene");
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _TargetDirectory = GetString(_Payload, "targetDirectory");
        const auto _ExpectedGenerationRunID = GetString(_Payload, "generationRunId");
        std::set<std::string> _SelectedPartIDs;
        const auto _Selection = _Payload.find("partEntityIds");
        const auto _HasSelection = _Selection != _Payload.end();
        if (_HasSelection)
        {
            if (!_Selection->second.Is<VariantArray>())
            {
                throw std::invalid_argument("partEntityIds must be an array");
            }
            for (const auto& _Item : _Selection->second.To<VariantArray>())
            {
                if (_Item.Is<std::string>()) _SelectedPartIDs.insert(_Item.To<std::string>());
            }
        }
        if (_TargetDirectory.empty()) throw std::invalid_argument("targetDirectory is required");
        if (_HasSelection && _SelectedPartIDs.empty())
        {
            throw std::invalid_argument("select at least one manufacturing part");
        }

        auto& _Repository = Scene_->Database();
        std::vector<std::tuple<
            std::shared_ptr<iCAX::Database::IEntity>,
            std::shared_ptr<CProductInstanceComponent>,
            std::vector<std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<CManufacturingPartComponent>>>>> _Groups;
        std::set<std::string> _MatchedPartIDs;
        for (const auto& [_ProductEntity, _Product] : Collect<CProductInstanceComponent>(_Repository))
        {
            const auto _GenerationRunID = _Product->GetActiveGenerationRunID();
            if (_GenerationRunID.is_nil()) continue;
            if (!_ExpectedGenerationRunID.empty()
                && UuidToString(_GenerationRunID) != _ExpectedGenerationRunID)
            {
                continue;
            }
            auto _Parts = Collect<CManufacturingPartComponent>(_Repository);
            _Parts.erase(
                std::remove_if(
                    _Parts.begin(),
                    _Parts.end(),
                    [&_ProductEntity, &_GenerationRunID, &_HasSelection, &_SelectedPartIDs](const auto& Item_) {
                        if (Item_.second->GetProductID() != _ProductEntity->GetID()
                            || Item_.second->GetGenerationRunID() != _GenerationRunID)
                        {
                            return true;
                        }
                        return _HasSelection
                            && !_SelectedPartIDs.contains(UuidToString(Item_.first->GetID()));
                    }),
                _Parts.end());
            if (_Parts.empty()) continue;
            std::sort(
                _Parts.begin(),
                _Parts.end(),
                [](const auto& Left_, const auto& Right_) {
                    return Left_.second->GetPartIndex() < Right_.second->GetPartIndex();
                });
            for (const auto& [_PartEntity, _Part] : _Parts)
            {
                _MatchedPartIDs.insert(UuidToString(_PartEntity->GetID()));
            }
            _Groups.emplace_back(_ProductEntity, _Product, std::move(_Parts));
        }
        if (_HasSelection && _MatchedPartIDs != _SelectedPartIDs)
        {
            throw std::runtime_error("TubeDesigner export selection contains stale manufacturing parts");
        }
        if (_Groups.empty())
        {
            throw std::invalid_argument("select at least one current manufacturing part");
        }

        const auto _TotalPartCount = static_cast<std::uint64_t>(std::accumulate(
            _Groups.begin(), _Groups.end(), std::size_t{ 0 },
            [](const std::size_t Count_, const auto& Group_) {
                return Count_ + std::get<2>(Group_).size();
            }));
        ReportExportProgress(Request_, "preparing", 0, _TotalPartCount, "正在准备 STEP 导出");

        const auto _TargetRoot = Utf8Path(_TargetDirectory);
        std::filesystem::create_directories(_TargetRoot);
        VariantArray _Files;
        VariantArray _ExportedGroups;
        std::vector<SPartListRow> _PartListRows;
        std::set<std::string> _FolderNames;
        std::uint64_t _ProductIndex = 0;
        std::uint64_t _CompletedPartCount = 0;
        for (const auto& [_ProductEntity, _Product, _Parts] : _Groups)
        {
            ++_ProductIndex;
            auto _FolderName = MakeSafePathSegment(_Product->GetName(), _Product->GetProductCode());
            if (_FolderNames.contains(_FolderName))
            {
                _FolderName += "_" + UuidToString(_ProductEntity->GetID()).substr(0, 8);
            }
            _FolderNames.insert(_FolderName);
            const auto _ProductDirectory = _TargetRoot / Utf8Path(_FolderName);
            std::filesystem::create_directories(_ProductDirectory);
            VariantArray _GroupFiles;
            for (const auto& [_PartEntity, _Part] : _Parts)
            {
                const auto _TargetPath = Utf8PathText(_ProductDirectory / Utf8Path(_Part->GetFileName()));
                const auto _Result = Scene_->Resources().Export<iCAX::GeometryData::BRepModel>(
                    _Part->GetManufacturingGeometryResourceID(),
                    _TargetPath,
                    { { "resourceVersion", std::to_string(_Part->GetManufacturingGeometryResourceVersion()) } },
                    "cad.step");
                if (!_Result.IsOK())
                {
                    throw std::runtime_error(_Result.Error.empty() ? "TubeDesigner STEP export failed" : _Result.Error);
                }
                _Files.emplace_back(_TargetPath);
                _GroupFiles.emplace_back(_TargetPath);
                ++_CompletedPartCount;
                ReportExportProgress(
                    Request_, "exporting", _CompletedPartCount, _TotalPartCount,
                    "已导出 " + _Part->GetFileName());

                const auto _Presentation = ResolvePartPresentation(_Repository, *_Part);
                _PartListRows.push_back({
                    _ProductIndex,
                    _Product->GetName(),
                    _Product->GetProductCode(),
                    _Part->GetPartIndex(),
                    _Part->GetPartNumber(),
                    _Presentation.Name,
                    _Presentation.Profile.Type,
                    _Presentation.Profile.Width,
                    _Presentation.Profile.Depth,
                    _Presentation.Profile.WallThickness,
                    _Presentation.Profile.CornerRadius,
                    _Part->GetLength(),
                    _Part->GetQuantity(),
                    _Part->GetFileName(),
                    Utf8PathText(Utf8Path(_FolderName) / Utf8Path(_Part->GetFileName()))
                });
            }
            ObjectMap _GroupResult;
            _GroupResult["productEntityId"] = UuidToString(_ProductEntity->GetID());
            _GroupResult["name"] = _Product->GetName();
            _GroupResult["directory"] = Utf8PathText(_ProductDirectory);
            _GroupResult["exportedFiles"] = _GroupFiles;
            _GroupResult["exportedCount"] = static_cast<unsigned long long>(_GroupFiles.size());
            _ExportedGroups.emplace_back(_GroupResult);
        }
        const auto _PartListPath = _TargetRoot / Utf8Path("零件清单.xlsx");
        ReportExportProgress(
            Request_, "workbook", _CompletedPartCount, _TotalPartCount,
            "正在生成零件清单.xlsx");
        WritePartListWorkbook(_PartListPath, _PartListRows);
        ReportExportProgress(
            Request_, "completed", _CompletedPartCount, _TotalPartCount,
            "STEP 与 Excel 文件已经写入目标目录");
        ObjectMap _Result;
        _Result["exportedFiles"] = _Files;
        _Result["exportedCount"] = static_cast<unsigned long long>(_Files.size());
        _Result["exportedGroups"] = _ExportedGroups;
        _Result["partListFile"] = Utf8PathText(_PartListPath);
        return MakeResponse(Variant(_Result));
    }

    class CTubeDesignerSDO final : public iCAX::Interaction::CSDO
    {
    public:
        CTubeDesignerSDO() : CSDO("TubeDesigner")
        {
            ExposeMethod("List", &HandleList);
            ExposeMethod("ActivateProduct", &HandleActivateProduct);
            ExposeMethod("GeneratePreview", &HandleGeneratePreview);
            ExposeMethod("Generate", &HandleGeneratePreview);
            ExposeMethod("Disassemble", &HandleDisassemble);
            ExposeMethod("DisassembleSelected", &HandleDisassemble);
            ExposeMethod("MeasurePartGeometry", &HandleMeasurePartGeometry);
            ExposeMethod("ExportSelected", &HandleExportSelected);
            ExposeMethod("ExportAll", &HandleExportSelected);
        }
    };

    static_assert(iCAX::Interaction::IsStatelessSDOType<CTubeDesignerSDO>);
}

ICAX_REGISTER_SDO(CTubeDesignerSDO)
