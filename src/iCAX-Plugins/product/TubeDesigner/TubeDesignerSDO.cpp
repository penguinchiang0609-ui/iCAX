#include "pch.h"

#include "SecurityWindow1Template.h"
#include "TubeDesignerComponents.h"

#include "Data/VariantSerializer.h"
#include "Database/IEntity.h"
#include "Database/IRepository.h"
#include "GeometryData/GeometryData.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepReader.h"
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

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>

namespace
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::PropertyValue;
    using iCAX::Data::Variant;
    using iCAX::Data::VariantArray;
    using namespace iCAX::TubeDesigner;

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

    template <typename TComponent>
    void AppendEntityIDs(iCAX::Database::IRepository& Repository_, std::vector<iCAX::Data::uuid>& IDs_)
    {
        for (const auto& [_Entity, _Component] : Collect<TComponent>(Repository_))
        {
            if (_Entity) IDs_.push_back(_Entity->GetID());
        }
    }

    std::vector<iCAX::Data::uuid> CollectExistingDesignIDs(
        iCAX::Database::IRepository& Repository_)
    {
        std::vector<iCAX::Data::uuid> _IDs;
        AppendEntityIDs<CProductInstanceComponent>(Repository_, _IDs);
        AppendEntityIDs<CAssemblyMemberComponent>(Repository_, _IDs);
        AppendEntityIDs<CManufacturingPartComponent>(Repository_, _IDs);
        AppendEntityIDs<CJointIntentComponent>(Repository_, _IDs);
        AppendEntityIDs<CGenerationRunComponent>(Repository_, _IDs);
        std::sort(_IDs.begin(), _IDs.end());
        _IDs.erase(std::unique(_IDs.begin(), _IDs.end()), _IDs.end());
        return _IDs;
    }

    STubeProfile ReadProfile(const ObjectMap& Payload_, const std::string& Prefix_, const STubeProfile& Default_)
    {
        STubeProfile _Result = Default_;
        _Result.Type = GetString(Payload_, Prefix_ + "ProfileType", Default_.Type);
        _Result.Width = GetDouble(Payload_, Prefix_ + "Width", Default_.Width);
        _Result.Depth = GetDouble(Payload_, Prefix_ + "Depth", Default_.Depth);
        _Result.WallThickness = GetDouble(Payload_, Prefix_ + "WallThickness", Default_.WallThickness);
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
        _Result["verticalProfileType"] = Parameters_.Vertical.Type;
        _Result["verticalWidth"] = Parameters_.Vertical.Width;
        _Result["verticalDepth"] = Parameters_.Vertical.Depth;
        _Result["verticalWallThickness"] = Parameters_.Vertical.WallThickness;
        _Result["horizontalProfileType"] = Parameters_.Horizontal.Type;
        _Result["horizontalWidth"] = Parameters_.Horizontal.Width;
        _Result["horizontalDepth"] = Parameters_.Horizontal.Depth;
        _Result["horizontalWallThickness"] = Parameters_.Horizontal.WallThickness;
        return _Result;
    }

    TopoDS_Shape MakeTubeShape(const SGeneratedPart& Part_, double Extra_ = 0.0, bool Hollow_ = true)
    {
        const auto _Width = Part_.Profile.Width + Extra_ * 2.0;
        const auto _Depth = Part_.Profile.Depth + Extra_ * 2.0;
        const auto _Epsilon = 1.0;
        const auto _MinX = std::min(Part_.X1, Part_.X2);
        const auto _MinY = std::min(Part_.Y1, Part_.Y2);
        const auto _CenterX = (Part_.X1 + Part_.X2) / 2.0;
        const auto _CenterY = (Part_.Y1 + Part_.Y2) / 2.0;

        TopoDS_Shape _Outer;
        TopoDS_Shape _Inner;
        if (Part_.IsVertical())
        {
            _Outer = BRepPrimAPI_MakeBox(
                gp_Pnt(_CenterX - _Width / 2.0, _MinY - Extra_, -_Depth / 2.0),
                _Width, Part_.Length + Extra_ * 2.0, _Depth).Shape();
            if (Hollow_)
            {
                const auto _InnerWidth = Part_.Profile.Width - Part_.Profile.WallThickness * 2.0;
                const auto _InnerDepth = Part_.Profile.Depth - Part_.Profile.WallThickness * 2.0;
                _Inner = BRepPrimAPI_MakeBox(
                    gp_Pnt(_CenterX - _InnerWidth / 2.0, _MinY - _Epsilon, -_InnerDepth / 2.0),
                    _InnerWidth, Part_.Length + _Epsilon * 2.0, _InnerDepth).Shape();
            }
        }
        else
        {
            _Outer = BRepPrimAPI_MakeBox(
                gp_Pnt(_MinX - Extra_, _CenterY - _Width / 2.0, -_Depth / 2.0),
                Part_.Length + Extra_ * 2.0, _Width, _Depth).Shape();
            if (Hollow_)
            {
                const auto _InnerWidth = Part_.Profile.Width - Part_.Profile.WallThickness * 2.0;
                const auto _InnerDepth = Part_.Profile.Depth - Part_.Profile.WallThickness * 2.0;
                _Inner = BRepPrimAPI_MakeBox(
                    gp_Pnt(_MinX - _Epsilon, _CenterY - _InnerWidth / 2.0, -_InnerDepth / 2.0),
                    Part_.Length + _Epsilon * 2.0, _InnerWidth, _InnerDepth).Shape();
            }
        }
        if (!Hollow_) return _Outer;
        BRepAlgoAPI_Cut _Cut(_Outer, _Inner);
        if (!_Cut.IsDone()) throw std::runtime_error("TubeDesigner failed to build hollow tube");
        return _Cut.Shape();
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

    std::string MakeStepFileName(const std::string& PartNumber_)
    {
        std::string _SafeName;
        _SafeName.reserve(PartNumber_.size());
        const std::string _Invalid = "<>:\"/\\|?*";
        for (const auto _Character : PartNumber_)
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
        if (_SafeName.empty()) _SafeName = "part";
        if (_SafeName.size() > 180) _SafeName.resize(180);
        return _SafeName + ".step";
    }

    struct SPreparedPart final
    {
        SGeneratedPart Generated;
        std::string StableKey;
        iCAX::Data::uuid MemberID;
        iCAX::Data::uuid ManufacturingPartID;
        iCAX::Resource::CResourceReference PreviewResource;
        iCAX::Resource::CResourceReference ManufacturingResource;
        iCAX::Resource::CResourceReference FrontendGeometryResource;
        std::string FileName;
    };

    struct SPreparedJoint final
    {
        SGeneratedJoint Generated;
        iCAX::Data::uuid JointID;
    };

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

    ObjectMap BuildSnapshot(iCAX::Project::ISceneContext& Scene_)
    {
        auto& _Repository = Scene_.Database();
        ObjectMap _Designer;

        VariantArray _Templates;
        ObjectMap _Template;
        _Template["id"] = std::string("security-window-1");
        _Template["version"] = std::string("1.0.0");
        _Template["name"] = std::string("防盗窗 1 号 - 无把手不封闭");
        _Template["available"] = true;
        _Templates.emplace_back(_Template);
        _Designer["templates"] = _Templates;

        const auto _Meta = _Repository.GetMetaEntity();
        const auto _Root = GetComponent<CTubeDesignerRootComponent>(_Meta);
        const auto _ActiveProductID = _Root
            ? _Root->GetActiveProductID()
            : iCAX::Data::uuid();
        std::shared_ptr<iCAX::Database::IEntity> _ActiveProductEntity;
        std::shared_ptr<CProductInstanceComponent> _ActiveProduct;
        for (const auto& [_Entity, _Product] : Collect<CProductInstanceComponent>(_Repository))
        {
            if (_ActiveProductID.is_nil() || _Entity->GetID() == _ActiveProductID)
            {
                _ActiveProductEntity = _Entity;
                _ActiveProduct = _Product;
                if (!_ActiveProductID.is_nil()) break;
            }
        }

        if (_ActiveProductEntity && _ActiveProduct)
        {
            ObjectMap _Product;
            _Product["entityId"] = UuidToString(_ActiveProductEntity->GetID());
            _Product["productCode"] = _ActiveProduct->GetProductCode();
            _Product["name"] = _ActiveProduct->GetName();
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
            if (!_ActiveProductID.is_nil() && _Member->GetProductID() != _ActiveProductID) continue;
            ObjectMap _Item;
            _Item["entityId"] = UuidToString(_Entity->GetID());
            _Item["manufacturingPartId"] = UuidToString(_Member->GetManufacturingPartID());
            _Item["index"] = _Member->GetMemberIndex();
            _Item["role"] = _Member->GetRole();
            _Item["name"] = _Member->GetName();
            _Item["profileType"] = _Member->GetProfileType();
            _Item["sectionWidth"] = _Member->GetSectionWidth();
            _Item["sectionDepth"] = _Member->GetSectionDepth();
            _Item["wallThickness"] = _Member->GetWallThickness();
            _Item["length"] = _Member->GetLength();
            _Item["stableKey"] = _Member->GetStableKey();
            _Item["previewGeometryResourceId"] = _Member->GetPreviewGeometryResourceID();
            _Item["previewGeometryResourceVersion"] = _Member->GetPreviewGeometryResourceVersion();
            _Members.emplace_back(_Item);
        }
        _Designer["members"] = _Members;

        VariantArray _Parts;
        for (const auto& [_Entity, _Part] : Collect<CManufacturingPartComponent>(_Repository))
        {
            if (!_ActiveProductID.is_nil() && _Part->GetProductID() != _ActiveProductID) continue;
            ObjectMap _Item;
            _Item["entityId"] = UuidToString(_Entity->GetID());
            _Item["sourceMemberId"] = UuidToString(_Part->GetSourceMemberID());
            _Item["index"] = _Part->GetPartIndex();
            _Item["partNumber"] = _Part->GetPartNumber();
            _Item["role"] = _Part->GetRole();
            _Item["quantity"] = _Part->GetQuantity();
            _Item["length"] = _Part->GetLength();
            _Item["stableKey"] = _Part->GetStableKey();
            _Item["manufacturingGeometryResourceId"] = _Part->GetManufacturingGeometryResourceID();
            _Item["manufacturingGeometryResourceVersion"] = _Part->GetManufacturingGeometryResourceVersion();
            _Item["fileName"] = _Part->GetFileName();
            _Item["status"] = _Part->GetStatus();
            _Parts.emplace_back(_Item);
        }
        _Designer["parts"] = _Parts;

        VariantArray _Joints;
        for (const auto& [_Entity, _Joint] : Collect<CJointIntentComponent>(_Repository))
        {
            if (!_ActiveProductID.is_nil() && _Joint->GetProductID() != _ActiveProductID) continue;
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
            if (!_ActiveProductID.is_nil() && _Run->GetProductID() != _ActiveProductID) continue;
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
            _Designer["generationRun"] = _Item;
            break;
        }

        ObjectMap _Response;
        _Response["tubeDesigner"] = _Designer;
        return _Response;
    }

    iCAX::Interaction::CInvocationResult HandleList(
        const iCAX::Interaction::CInvocation&,
        const iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_) throw std::invalid_argument("TubeDesigner.List requires a scene");
        return MakeResponse(Variant(BuildSnapshot(*Scene_)));
    }

    iCAX::Interaction::CInvocationResult HandleGenerate(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_) throw std::invalid_argument("TubeDesigner.Generate requires a scene");
        const auto _Parameters = ReadParameters(DecodeObjectPayload(Request_));
        const auto _Generated = GenerateSecurityWindow1(_Parameters);
        auto& _Repository = Scene_->Database();
        const auto _Meta = _Repository.GetMetaEntity();
        if (!_Meta) throw std::runtime_error("TubeDesigner requires repository meta entity");
        const auto _Root = GetComponent<CTubeDesignerRootComponent>(_Meta);
        auto _ExistingProductEntity = _Root && !_Root->GetActiveProductID().is_nil()
            ? _Repository.GetEntity(_Root->GetActiveProductID())
            : nullptr;
        if (!_ExistingProductEntity)
        {
            const auto _Products = Collect<CProductInstanceComponent>(_Repository);
            if (!_Products.empty()) _ExistingProductEntity = _Products.front().first;
        }
        const auto _ProductID = _ExistingProductEntity
            ? _ExistingProductEntity->GetID()
            : iCAX::Data::GenerateNewUUID();
        const auto _RunID = iCAX::Data::GenerateNewUUID();

        std::vector<TopoDS_Shape> _PreviewShapes;
        std::vector<TopoDS_Shape> _ManufacturingShapes;
        _PreviewShapes.reserve(_Generated.Parts.size());
        _ManufacturingShapes.reserve(_Generated.Parts.size());
        for (const auto& _Part : _Generated.Parts)
        {
            auto _Shape = MakeTubeShape(_Part);
            _PreviewShapes.push_back(_Shape);
            _ManufacturingShapes.push_back(std::move(_Shape));
        }
        for (const auto& _Joint : _Generated.Joints)
        {
            const auto _Target = static_cast<std::size_t>(_Joint.TargetPartIndex - 1);
            const auto _Inserted = static_cast<std::size_t>(_Joint.InsertedPartIndex - 1);
            if (_Target >= _ManufacturingShapes.size() || _Inserted >= _Generated.Parts.size())
            {
                throw std::runtime_error("TubeDesigner generated an invalid joint reference");
            }
            const auto _Cutter = MakeTubeShape(_Generated.Parts[_Inserted], _Joint.Clearance, false);
            BRepAlgoAPI_Cut _Cut(_ManufacturingShapes[_Target], _Cutter);
            if (!_Cut.IsDone()) throw std::runtime_error("TubeDesigner joint boolean failed");
            _ManufacturingShapes[_Target] = _Cut.Shape();
        }

        const auto _MaterialResource = EnsureDesignerMaterial(*Scene_);
        const auto _StablePartKeys = MakeStablePartKeys(_Generated);
        std::vector<SPreparedPart> _PreparedParts;
        _PreparedParts.reserve(_Generated.Parts.size());
        for (std::size_t _Index = 0; _Index < _Generated.Parts.size(); ++_Index)
        {
            const auto& _GeneratedPart = _Generated.Parts[_Index];
            const auto& _StableKey = _StablePartKeys.at(_Index);
            const auto _StablePrefix = "tube-designer/product/" + UuidToString(_ProductID)
                + "/part/" + _StableKey;
            const auto _PreviewResource = StoreBRep(
                *Scene_, _StablePrefix + "/preview", _GeneratedPart.PartNumber + " preview", _PreviewShapes[_Index]);
            const auto _ManufacturingResource = StoreBRep(
                *Scene_, _StablePrefix + "/manufacturing", _GeneratedPart.PartNumber + " manufacturing", _ManufacturingShapes[_Index]);
            const auto _FrontendGeometry = iCAX::RenderInteraction::EnsureFrontendGeometryResource(
                Scene_->Resources(), _PreviewResource.URL, iCAX::Render::ERenderGeometryKind::Mesh);
            _PreparedParts.push_back({
                _GeneratedPart,
                _StableKey,
                MakeStableEntityID(_ProductID, "member", _StableKey),
                MakeStableEntityID(_ProductID, "manufacturing-part", _StableKey),
                _PreviewResource,
                _ManufacturingResource,
                _FrontendGeometry,
                MakeStepFileName(_GeneratedPart.PartNumber)
            });
        }

        std::vector<SPreparedJoint> _PreparedJoints;
        _PreparedJoints.reserve(_Generated.Joints.size());
        for (const auto& _GeneratedJoint : _Generated.Joints)
        {
            const auto& _Target = _PreparedParts.at(
                static_cast<std::size_t>(_GeneratedJoint.TargetPartIndex - 1));
            const auto& _Inserted = _PreparedParts.at(
                static_cast<std::size_t>(_GeneratedJoint.InsertedPartIndex - 1));
            _PreparedJoints.push_back({
                _GeneratedJoint,
                MakeStableEntityID(
                    _ProductID,
                    "joint",
                    _Target.StableKey + "/" + _Inserted.StableKey)
            });
        }

        const auto _ExistingIDs = CollectExistingDesignIDs(_Repository);
        std::vector<iCAX::Data::uuid> _DesiredIDs{ _ProductID, _RunID };
        for (const auto& _Prepared : _PreparedParts)
        {
            _DesiredIDs.push_back(_Prepared.MemberID);
            _DesiredIDs.push_back(_Prepared.ManufacturingPartID);
        }
        for (const auto& _Prepared : _PreparedJoints)
        {
            _DesiredIDs.push_back(_Prepared.JointID);
        }

        auto _Undo = _Repository.BeginUndoCommand("Generate TubeDesigner product");
        auto& _Transaction = _Repository.BeginTransaction("Update TubeDesigner product");
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
                { CProductInstanceComponent::PropertyName_Name, PropertyValue("防盗窗 " + _Generated.ProductCode) },
                { CProductInstanceComponent::PropertyName_TemplateID, PropertyValue(_Generated.TemplateID) },
                { CProductInstanceComponent::PropertyName_TemplateVersion, PropertyValue(_Generated.TemplateVersion) },
                { CProductInstanceComponent::PropertyName_Parameters, PropertyValue(MakeParameterMap(_Parameters)) },
                { CProductInstanceComponent::PropertyName_ActiveGenerationRunID, PropertyValue(_RunID) }
            });

            _Transaction.CreateEntity(_RunID);
            _Transaction.AttachComponent(_RunID, CGenerationRunComponent::S_ClassName, {
                { CGenerationRunComponent::PropertyName_ProductID, PropertyValue(_ProductID) },
                { CGenerationRunComponent::PropertyName_TemplateID, PropertyValue(_Generated.TemplateID) },
                { CGenerationRunComponent::PropertyName_TemplateVersion, PropertyValue(_Generated.TemplateVersion) },
                { CGenerationRunComponent::PropertyName_PartCount, PropertyValue(static_cast<unsigned long long>(_Generated.Parts.size())) }
            });

            for (const auto& _Prepared : _PreparedParts)
            {
                const auto& _Part = _Prepared.Generated;
                const auto _ExistingMember = _Repository.GetEntity(_Prepared.MemberID);
                if (!_ExistingMember) _Transaction.CreateEntity(_Prepared.MemberID);
                QueueUpsertComponent(_Transaction, _ExistingMember, _Prepared.MemberID, CAssemblyMemberComponent::S_ClassName, {
                    { CAssemblyMemberComponent::PropertyName_ProductID, PropertyValue(_ProductID) },
                    { CAssemblyMemberComponent::PropertyName_ManufacturingPartID, PropertyValue(_Prepared.ManufacturingPartID) },
                    { CAssemblyMemberComponent::PropertyName_MemberIndex, PropertyValue(static_cast<unsigned long long>(_Part.Index)) },
                    { CAssemblyMemberComponent::PropertyName_StableKey, PropertyValue(_Prepared.StableKey) },
                    { CAssemblyMemberComponent::PropertyName_Role, PropertyValue(_Part.Role) },
                    { CAssemblyMemberComponent::PropertyName_Name, PropertyValue(_Part.PartNumber) },
                    { CAssemblyMemberComponent::PropertyName_ProfileType, PropertyValue(_Part.Profile.Type) },
                    { CAssemblyMemberComponent::PropertyName_SectionWidth, PropertyValue(_Part.Profile.Width) },
                    { CAssemblyMemberComponent::PropertyName_SectionDepth, PropertyValue(_Part.Profile.Depth) },
                    { CAssemblyMemberComponent::PropertyName_WallThickness, PropertyValue(_Part.Profile.WallThickness) },
                    { CAssemblyMemberComponent::PropertyName_Length, PropertyValue(_Part.Length) },
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
                if (!_ExistingMember || !_ExistingMember->HasComponent(iCAX::Transform::CTransformComponent::S_ClassName))
                {
                    _Transaction.AttachComponent(
                        _Prepared.MemberID,
                        iCAX::Transform::CTransformComponent::S_ClassName);
                }

                const auto _ExistingManufacturingPart = _Repository.GetEntity(_Prepared.ManufacturingPartID);
                if (!_ExistingManufacturingPart) _Transaction.CreateEntity(_Prepared.ManufacturingPartID);
                QueueUpsertComponent(_Transaction, _ExistingManufacturingPart, _Prepared.ManufacturingPartID, CManufacturingPartComponent::S_ClassName, {
                    { CManufacturingPartComponent::PropertyName_ProductID, PropertyValue(_ProductID) },
                    { CManufacturingPartComponent::PropertyName_SourceMemberID, PropertyValue(_Prepared.MemberID) },
                    { CManufacturingPartComponent::PropertyName_GenerationRunID, PropertyValue(_RunID) },
                    { CManufacturingPartComponent::PropertyName_PartIndex, PropertyValue(static_cast<unsigned long long>(_Part.Index)) },
                    { CManufacturingPartComponent::PropertyName_StableKey, PropertyValue(_Prepared.StableKey) },
                    { CManufacturingPartComponent::PropertyName_PartNumber, PropertyValue(_Part.PartNumber) },
                    { CManufacturingPartComponent::PropertyName_Role, PropertyValue(_Part.Role) },
                    { CManufacturingPartComponent::PropertyName_Length, PropertyValue(_Part.Length) },
                    { CManufacturingPartComponent::PropertyName_ManufacturingGeometryResourceID, PropertyValue(_Prepared.ManufacturingResource.URL) },
                    { CManufacturingPartComponent::PropertyName_ManufacturingGeometryResourceVersion, PropertyValue(_Prepared.ManufacturingResource.nVersion) },
                    { CManufacturingPartComponent::PropertyName_FileName, PropertyValue(_Prepared.FileName) }
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
        return MakeResponse(Variant(BuildSnapshot(*Scene_)));
    }

    iCAX::Interaction::CInvocationResult HandleExportAll(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_) throw std::invalid_argument("TubeDesigner.ExportAll requires a scene");
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _TargetDirectory = GetString(_Payload, "targetDirectory");
        const auto _ExpectedGenerationRunID = GetString(_Payload, "generationRunId");
        if (_TargetDirectory.empty()) throw std::invalid_argument("targetDirectory is required");
        if (_ExpectedGenerationRunID.empty())
        {
            throw std::invalid_argument("generationRunId is required");
        }
        const auto _Meta = Scene_->Database().GetMetaEntity();
        const auto _Root = GetComponent<CTubeDesignerRootComponent>(_Meta);
        if (!_Root || _Root->GetActiveProductID().is_nil())
        {
            throw std::runtime_error("TubeDesigner has no active product to export");
        }
        const auto _ActiveProductID = _Root->GetActiveProductID();
        std::shared_ptr<CProductInstanceComponent> _ActiveProduct;
        for (const auto& [_Entity, _Product] : Collect<CProductInstanceComponent>(Scene_->Database()))
        {
            if (_Entity->GetID() == _ActiveProductID)
            {
                _ActiveProduct = _Product;
                break;
            }
        }
        if (!_ActiveProduct)
        {
            throw std::runtime_error("TubeDesigner active product is missing");
        }
        const auto _ActiveGenerationRunID = _ActiveProduct->GetActiveGenerationRunID();
        if (_ActiveGenerationRunID.is_nil()
            || UuidToString(_ActiveGenerationRunID) != _ExpectedGenerationRunID)
        {
            throw std::runtime_error(
                "TubeDesigner export generation does not match the active committed generation");
        }
        auto _Parts = Collect<CManufacturingPartComponent>(Scene_->Database());
        _Parts.erase(
            std::remove_if(
                _Parts.begin(),
                _Parts.end(),
                [&_ActiveProductID, &_ActiveGenerationRunID](const auto& Item_) {
                    return Item_.second->GetProductID() != _ActiveProductID
                        || Item_.second->GetGenerationRunID() != _ActiveGenerationRunID;
                }),
            _Parts.end());
        std::sort(
            _Parts.begin(),
            _Parts.end(),
            [](const auto& Left_, const auto& Right_) {
                return Left_.second->GetPartIndex() < Right_.second->GetPartIndex();
            });
        std::filesystem::create_directories(std::filesystem::path(_TargetDirectory));
        VariantArray _Files;
        for (const auto& [_Entity, _Part] : _Parts)
        {
            const auto _TargetPath = (std::filesystem::path(_TargetDirectory) / _Part->GetFileName()).string();
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
        }
        ObjectMap _Result;
        _Result["generationRunId"] = _ExpectedGenerationRunID;
        _Result["exportedFiles"] = _Files;
        _Result["exportedCount"] = static_cast<unsigned long long>(_Files.size());
        return MakeResponse(Variant(_Result));
    }

    class CTubeDesignerSDO final : public iCAX::Interaction::CSDO
    {
    public:
        CTubeDesignerSDO() : CSDO("TubeDesigner")
        {
            ExposeMethod("List", &HandleList);
            ExposeMethod("Generate", &HandleGenerate);
            ExposeMethod("ExportAll", &HandleExportAll);
        }
    };

    static_assert(iCAX::Interaction::IsStatelessSDOType<CTubeDesignerSDO>);
}

ICAX_REGISTER_SDO(CTubeDesignerSDO)
