#include "pch.h"
#include "../../../licensing/include/LicenseRuntime.h"

#include "PartListXlsxExporter.h"
#include "ComponentModelLibrary.h"
#include "FinalGeometryMeasurement.h"
#include "NestingAdapter.h"
#include "NestingResultExporter.h"
#include "MachiningToolpaths.h"
#include "TubeDesignerComponents.h"
#include "ProductionQuantity.h"
#include "PunchGeometry.h"
#include "PartDrawingDefinition.h"
#include "BRepTubeUnfoldingService.h"
#include "ExtrusionRecognition/ExtrusionRecognitionService.h"

#include "ApplicationContext/IApplicationContext.h"
#include "Data/VariantSerializer.h"
#include "Database/IEntity.h"
#include "Database/IRepository.h"
#include "GeometryData/GeometryData.h"
#include "GeometryData/BRepPersistence.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepReader.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepBuilder.h"
#include "OpenCascadeResourceImport/OpenCascadeNeutralModelEvaluator.h"
#include "OpenCascadeResourceImport/OpenCascadeTaskExecution.h"
#include "ProductContext/IProductContext.h"
#include "ApplicationContext/UserDataStore.h"
#include "ProjectContext/IProjectContext.h"
#include "ProjectContext/ISceneContext.h"
#include "RenderData/RenderData.h"
#include "RenderInteraction/RenderInteraction.h"
#include "RenderInteraction/RenderInteractionComponents.h"
#include "Resources/ResourceInfo.h"
#include "Resources/ResourceLibrary.h"
#include "SDO/SDO.h"
#include "Services/ServiceProvider.h"
#include "SDO/SDORegistrationCatalog.h"
#include "Task/Task.h"
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

#include <atomic>
#include <fstream>
#include <mutex>
#include <numeric>
#include <set>
#include <unordered_set>
#include <TopoDS_Shape.hxx>
#include <TopExp_Explorer.hxx>
#include <Bnd_Box.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRep_Builder.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

namespace
{
    // The installed package tree is immutable application content.  Personal
    // packages live next to the product's existing Product.Data under the
    // platform user-data root, so reinstalling the application cannot remove
    // them.  Both roots use the same package categories below.
    constexpr const char* kTubeDesignerProductID = "icax.tube-designer";

    constexpr std::size_t kMaximumNestingPartInstances = 1'000'000;
    using iCAX::Data::ObjectMap;
    using iCAX::Data::PropertyValue;
    using iCAX::Data::Variant;
    using iCAX::Data::VariantArray;
    using namespace iCAX::TubeDesigner;
    using iCAX::ExtrusionRecognition::SRecognitionResult;
    using iCAX::GeometryData::Transform3;
    constexpr double kPreviewRollRadians = 1.57079632679489661923;
    constexpr double kPunchPi = 3.14159265358979323846;

    struct SPreparedManufacturingPart final
    {
        std::uint64_t Index = 0;
        std::string StableKey;
        std::string PartNumber;
        std::string Role;
        std::uint64_t Quantity = 1;
        double Length = 0.0;
        ObjectMap ItemProperties;
        ObjectMap TubeProfile;
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
        ObjectMap ManufacturingModel;
    };

    // Manufacturing output is an operation result, not project data. Keep the
    // latest disassembly in process memory. A nesting task may persist light
    // references to these parts; their geometry is regenerated from the
    // product recipe when a saved project is reopened.
    struct STransientSceneState final
    {
        std::map<std::string, SPreparedProductDisassembly> Products;
    };

    std::mutex gTransientDisassemblyMutex;
    std::map<iCAX::Data::uuid, STransientSceneState> gTransientDisassembly;
    std::mutex gPartMeasurementCacheMutex;
    std::map<std::string, ObjectMap> gPartMeasurementCache;
    std::map<std::string, ObjectMap> gPartUnfoldingCache;

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

    std::optional<ObjectMap> FindProductSideSketch(
        const CProductInstanceComponent& Product_,
        const iCAX::Data::uuid& MemberID_,
        const std::string& StableKey_)
    {
        const auto _Sketches = Product_.GetSketches();
        const auto _SideIt = _Sketches.find("side");
        if (_SideIt == _Sketches.end() || !_SideIt->second.Is<ObjectMap>()) return std::nullopt;
        const auto _Side = _SideIt->second.To<ObjectMap>();
        auto _SketchIt = _Side.find(UuidToString(MemberID_));
        if (_SketchIt == _Side.end() && !StableKey_.empty())
        {
            _SketchIt = std::find_if(_Side.begin(), _Side.end(), [&](const auto& Entry_) {
                return Entry_.second.Is<ObjectMap>()
                    && GetString(Entry_.second.To<ObjectMap>(), "targetMemberKey") == StableKey_;
            });
        }
        if (_SketchIt == _Side.end() || !_SketchIt->second.Is<ObjectMap>()) return std::nullopt;
        return _SketchIt->second.To<ObjectMap>();
    }

    std::string TrimText(const std::string& Text_)
    {
        const auto _First = std::find_if_not(Text_.begin(), Text_.end(), [](const unsigned char Character_) {
            return std::isspace(Character_) != 0;
        });
        const auto _Last = std::find_if_not(Text_.rbegin(), Text_.rend(), [](const unsigned char Character_) {
            return std::isspace(Character_) != 0;
        }).base();
        return _First < _Last ? std::string(_First, _Last) : std::string();
    }

    std::string GetRequiredText(
        const ObjectMap& Payload_, const std::string& Name_, const std::size_t MaximumLength_ = 160)
    {
        const auto _Value = TrimText(GetString(Payload_, Name_));
        if (_Value.empty()) throw std::invalid_argument("TubeDesigner field cannot be empty: " + Name_);
        if (_Value.size() > MaximumLength_)
            throw std::invalid_argument("TubeDesigner field is too long: " + Name_);
        return _Value;
    }

    ObjectMap GetRequiredObject(const ObjectMap& Payload_, const std::string& Name_)
    {
        const auto _Iterator = Payload_.find(Name_);
        if (_Iterator == Payload_.end() || !_Iterator->second.Is<ObjectMap>())
            throw std::invalid_argument("TubeDesigner field must be an object: " + Name_);
        return _Iterator->second.To<ObjectMap>();
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

    std::string EncodeTemplateAssetBase64(const std::string& Bytes_)
    {
        static constexpr char _Alphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string _Result;
        _Result.reserve(((Bytes_.size() + 2) / 3) * 4);
        for (std::size_t _Index = 0; _Index < Bytes_.size(); _Index += 3)
        {
            const auto _A = static_cast<unsigned char>(Bytes_[_Index]);
            const auto _B = _Index + 1 < Bytes_.size()
                ? static_cast<unsigned char>(Bytes_[_Index + 1]) : 0;
            const auto _C = _Index + 2 < Bytes_.size()
                ? static_cast<unsigned char>(Bytes_[_Index + 2]) : 0;
            const auto _Value = (static_cast<unsigned int>(_A) << 16)
                | (static_cast<unsigned int>(_B) << 8)
                | static_cast<unsigned int>(_C);
            _Result.push_back(_Alphabet[(_Value >> 18) & 0x3f]);
            _Result.push_back(_Alphabet[(_Value >> 12) & 0x3f]);
            _Result.push_back(_Index + 1 < Bytes_.size()
                ? _Alphabet[(_Value >> 6) & 0x3f] : '=');
            _Result.push_back(_Index + 2 < Bytes_.size()
                ? _Alphabet[_Value & 0x3f] : '=');
        }
        return _Result;
    }

    std::string DecodeTemplateAssetBase64(const std::string& Text_)
    {
        if (Text_.size() % 4 != 0) throw std::invalid_argument("模板资源不是有效 Base64");
        const auto _Value = [](const char Character_) -> int {
            if (Character_ >= 'A' && Character_ <= 'Z') return Character_ - 'A';
            if (Character_ >= 'a' && Character_ <= 'z') return Character_ - 'a' + 26;
            if (Character_ >= '0' && Character_ <= '9') return Character_ - '0' + 52;
            if (Character_ == '+') return 62;
            if (Character_ == '/') return 63;
            return -1;
        };
        std::string _Result;
        _Result.reserve((Text_.size() / 4) * 3);
        for (std::size_t _Index = 0; _Index < Text_.size(); _Index += 4)
        {
            const auto _A = _Value(Text_[_Index]);
            const auto _B = _Value(Text_[_Index + 1]);
            const auto _C = Text_[_Index + 2] == '=' ? 0 : _Value(Text_[_Index + 2]);
            const auto _D = Text_[_Index + 3] == '=' ? 0 : _Value(Text_[_Index + 3]);
            if (_A < 0 || _B < 0 || _C < 0 || _D < 0
                || (Text_[_Index + 2] == '=' && Text_[_Index + 3] != '='))
                throw std::invalid_argument("模板资源不是有效 Base64");
            const auto _Bits = (static_cast<unsigned int>(_A) << 18)
                | (static_cast<unsigned int>(_B) << 12)
                | (static_cast<unsigned int>(_C) << 6)
                | static_cast<unsigned int>(_D);
            _Result.push_back(static_cast<char>((_Bits >> 16) & 0xff));
            if (Text_[_Index + 2] != '=') _Result.push_back(static_cast<char>((_Bits >> 8) & 0xff));
            if (Text_[_Index + 3] != '=') _Result.push_back(static_cast<char>(_Bits & 0xff));
        }
        return _Result;
    }

    void WriteTemplateFile(const std::filesystem::path& Path_, const std::string& Bytes_)
    {
        std::ofstream _Stream(Path_, std::ios::binary | std::ios::trunc);
        if (!_Stream) throw std::runtime_error("无法写入模板文件：" + Path_.string());
        _Stream.write(Bytes_.data(), static_cast<std::streamsize>(Bytes_.size()));
        if (!_Stream) throw std::runtime_error("无法完成模板文件写入：" + Path_.string());
    }

    std::filesystem::path ResolveTemplateAssetPath(
        const std::filesystem::path& PackageRoot_, const std::string& RelativePath_)
    {
        if (RelativePath_.empty()) return {};
        const auto _Relative = std::filesystem::path(
            std::u8string(RelativePath_.begin(), RelativePath_.end()));
        if (_Relative.is_absolute() || _Relative.has_root_name() || _Relative.has_root_directory())
            throw std::invalid_argument("模板资源路径必须是相对路径");
        for (const auto& _Part : _Relative)
            if (_Part == "..")
                throw std::invalid_argument("模板资源路径不得越过模板目录");
        std::error_code _Error;
        const auto _Root = std::filesystem::weakly_canonical(PackageRoot_, _Error);
        if (_Error) throw std::runtime_error("模板资源目录无效：" + _Error.message());
        const auto _Path = std::filesystem::weakly_canonical(_Root / _Relative, _Error);
        if (_Error) throw std::runtime_error("模板资源路径无效：" + _Error.message());
        const auto _RelativeToRoot = std::filesystem::relative(_Path, _Root, _Error);
        if (_Error || _RelativeToRoot.empty() || _RelativeToRoot == ".."
            || _RelativeToRoot.string().starts_with(".." + std::string(1, std::filesystem::path::preferred_separator)))
            throw std::invalid_argument("模板资源路径不得越过模板目录");
        return _Path;
    }

    void AttachTemplateCatalogAssets(
        ObjectMap& Presentation_, const std::filesystem::path& PackageRoot_)
    {
        auto _ExtensionsIt = Presentation_.find("extensions");
        if (_ExtensionsIt == Presentation_.end() || !_ExtensionsIt->second.Is<ObjectMap>()) return;
        auto _Extensions = _ExtensionsIt->second.To<ObjectMap>();
        auto _CatalogIt = _Extensions.find("catalog");
        if (_CatalogIt == _Extensions.end() || !_CatalogIt->second.Is<ObjectMap>()) return;
        auto _Catalog = _CatalogIt->second.To<ObjectMap>();
        ObjectMap _AssetData;
        for (const auto* _Kind : { "icon", "schematic" })
        {
            const auto _DeclaredPath = GetString(_Catalog, _Kind,
                std::string("resource/") + _Kind + ".svg");
            const auto _AssetPath = ResolveTemplateAssetPath(PackageRoot_, _DeclaredPath);
            std::error_code _Error;
            if (!std::filesystem::is_regular_file(_AssetPath, _Error))
            {
                if (!GetString(_Catalog, _Kind).empty())
                    throw std::runtime_error("模板声明的资源不存在：" + _DeclaredPath);
                continue;
            }
            const auto _Bytes = ReadTextFile(_AssetPath);
            _AssetData[_Kind] = std::string("data:image/svg+xml;base64,")
                + EncodeTemplateAssetBase64(_Bytes);
        }
        if (!_AssetData.empty())
        {
            _Catalog["assetData"] = std::move(_AssetData);
            _Extensions["catalog"] = std::move(_Catalog);
            Presentation_["extensions"] = std::move(_Extensions);
        }
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

    std::filesystem::path ResolveRuntimeDirectory(
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
                if (std::filesystem::is_directory(_Candidate, _Error))
                    return std::filesystem::weakly_canonical(_Candidate, _Error);
                _Error.clear();
            }
        }
        throw std::runtime_error(strDescription_ + " was not found in the application installation");
    }

    std::filesystem::path ResolveTemplateRoot(
        const iCAX::Application::IApplicationContext& ApplicationContext_)
    {
        return ResolveRuntimeDirectory(ApplicationContext_, {
            "apps/tube-designer/templates",
            "src/apps/tube-designer/templates"
        }, "TubeDesigner template directory");
    }

    std::filesystem::path ResolveUserTemplateRoot(
        const iCAX::Application::IApplicationContext& ApplicationContext_)
    {
        const auto& _UserData = ApplicationContext_.GetPaths().UserDataDirectory;
        if (_UserData.empty()) return {};
        const std::u8string _UserDataUTF8(
            reinterpret_cast<const char8_t*>(_UserData.data()), _UserData.size());
        return std::filesystem::path(_UserDataUTF8)
            / kTubeDesignerProductID / "template";
    }

    std::filesystem::path ResolveTemplateAssetPath(
        const std::filesystem::path& PackageRoot_, const std::string& RelativePath_);

    std::filesystem::path MaterializeUserProductTemplate(
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        const ObjectMap& Package_)
    {
        const auto _Descriptor = GetRequiredObject(Package_, "descriptor");
        const auto _TemplateID = GetRequiredText(_Descriptor, "id", 240);
        const auto _IDPath = std::filesystem::path(
            std::u8string(_TemplateID.begin(), _TemplateID.end()));
        if (_IDPath.has_root_name() || _IDPath.has_root_directory()
            || _IDPath.filename() != _IDPath || _TemplateID == "." || _TemplateID == "..")
            throw std::invalid_argument("产品模板 ID 不能作为用户模板目录名");
        const auto _Root = ResolveUserTemplateRoot(ApplicationContext_);
        if (_Root.empty()) throw std::runtime_error("用户模板目录不可用");
        const auto _ProductRoot = _Root / "product";
        std::error_code _Error;
        std::filesystem::create_directories(_ProductRoot, _Error);
        if (_Error) throw std::runtime_error("无法创建用户产品模板目录：" + _Error.message());
        const auto _Target = _ProductRoot / _IDPath;
        if (std::filesystem::exists(_Target, _Error))
            throw std::invalid_argument("用户产品模板已存在：" + _TemplateID);
        const auto _Staging = _ProductRoot / ("." + _TemplateID + ".importing");
        std::filesystem::remove_all(_Staging, _Error);
        _Error.clear();
        std::filesystem::create_directories(_Staging, _Error);
        if (_Error) throw std::runtime_error("无法创建用户产品模板暂存目录：" + _Error.message());
        try
        {
            WriteTemplateFile(_Staging / "template.json",
                iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(Variant(_Descriptor)));
            const auto _ScriptSource = GetString(Package_, "scriptSource");
            if (_ScriptSource.empty()) throw std::invalid_argument("产品模板脚本不能为空");
            WriteTemplateFile(_Staging / "template.py", _ScriptSource);
            if (const auto _ResourcesIt = Package_.find("resources");
                _ResourcesIt != Package_.end())
            {
                if (!_ResourcesIt->second.Is<ObjectMap>())
                    throw std::invalid_argument("产品模板资源不是对象");
                for (const auto& [_Name, _Encoded] : _ResourcesIt->second.To<ObjectMap>())
                {
                    if (!_Encoded.Is<std::string>())
                        throw std::invalid_argument("产品模板资源不是 Base64 文本：" + _Name);
                    const auto _Path = ResolveTemplateAssetPath(_Staging, _Name);
                    std::filesystem::create_directories(_Path.parent_path(), _Error);
                    if (_Error) throw std::runtime_error("无法创建产品模板资源目录：" + _Error.message());
                    WriteTemplateFile(_Path, DecodeTemplateAssetBase64(_Encoded.To<std::string>()));
                }
            }
            std::filesystem::rename(_Staging, _Target, _Error);
            if (_Error) throw std::runtime_error("无法提交用户产品模板：" + _Error.message());
        }
        catch (...)
        {
            std::error_code _CleanupError;
            std::filesystem::remove_all(_Staging, _CleanupError);
            throw;
        }
        return _Target;
    }

    std::filesystem::path ResolveSystemProfileRoot(
        const iCAX::Application::IApplicationContext& ApplicationContext_)
    {
        return ResolveRuntimeDirectory(ApplicationContext_, {
            "apps/tube-designer/templates/profile",
            "src/apps/tube-designer/templates/profile"
        }, "TubeDesigner system profile directory");
    }

    std::filesystem::path ResolveUserProfileRoot(
        const iCAX::Application::IApplicationContext& ApplicationContext_)
    {
        const auto _Root = ResolveUserTemplateRoot(ApplicationContext_);
        return _Root.empty() ? std::filesystem::path{} : _Root / "profile";
    }

    std::filesystem::path ResolveUserMoldRoot(
        const iCAX::Application::IApplicationContext& ApplicationContext_)
    {
        const auto _Root = ResolveUserTemplateRoot(ApplicationContext_);
        return _Root.empty() ? std::filesystem::path{} : _Root / "mold";
    }

    std::filesystem::path ResolveComponentModelRoot(
        const iCAX::Application::IApplicationContext& ApplicationContext_)
    {
        return ResolveRuntimeDirectory(ApplicationContext_, {
            "apps/tube-designer/templates/accessory",
            "src/apps/tube-designer/templates/accessory"
        }, "TubeDesigner component model directory");
    }

    std::filesystem::path ResolveUserComponentModelRoot(
        const iCAX::Application::IApplicationContext& ApplicationContext_)
    {
        const auto _Root = ResolveUserTemplateRoot(ApplicationContext_);
        return _Root.empty() ? std::filesystem::path{} : _Root / "accessory";
    }

    void EnsureUserTemplateSharedRuntime(
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        const std::filesystem::path& UserRoot_)
    {
        if (UserRoot_.empty() || !std::filesystem::is_directory(UserRoot_)) return;
        const auto _SystemShared = ResolveTemplateRoot(ApplicationContext_) / "_shared";
        const auto _UserShared = UserRoot_ / "_shared";
        if (!std::filesystem::is_directory(_SystemShared)) return;
        std::error_code _Error;
        std::filesystem::create_directories(_UserShared, _Error);
        _Error.clear();
        std::filesystem::copy(_SystemShared, _UserShared,
            std::filesystem::copy_options::recursive
                | std::filesystem::copy_options::overwrite_existing, _Error);
        if (_Error)
            throw std::runtime_error("TubeDesigner failed to refresh user template runtime: " + _Error.message());
    }

    std::vector<std::filesystem::path> DiscoverPythonTemplateDirectoriesAt(
        const std::filesystem::path& TemplateRoot_, bool Required_)
    {
        const auto _ProductRoot = TemplateRoot_ / "product";
        std::vector<std::filesystem::path> _Directories;
        std::error_code _Error;
        if (!std::filesystem::is_directory(_ProductRoot, _Error))
        {
            // The user template root is optional.  A fresh installation has no
            // %LOCALAPPDATA%/iCAX/icax.tube-designer/template/product directory
            // yet, which must mean "no user packages", not an enumeration error.
            if (Required_)
                throw std::runtime_error("TubeDesigner failed to enumerate template packages");
            return _Directories;
        }
        for (std::filesystem::directory_iterator _Iterator(_ProductRoot, _Error), _End;
            !_Error && _Iterator != _End; _Iterator.increment(_Error))
        {
            if (!_Iterator->is_directory(_Error))
            {
                _Error.clear();
                continue;
            }
            const auto _Directory = _Iterator->path();
            const auto _Name = _Directory.filename().string();
            if (_Name.empty() || _Name.front() == '_') continue;
            if (std::filesystem::is_regular_file(_Directory / "template.json", _Error)
                && std::filesystem::is_regular_file(_Directory / "template.py", _Error))
            {
                _Directories.push_back(_Directory);
            }
            _Error.clear();
        }
        if (_Error)
            throw std::runtime_error("TubeDesigner failed to enumerate template packages");
        std::sort(_Directories.begin(), _Directories.end());
        return _Directories;
    }

    std::vector<std::filesystem::path> DiscoverPythonTemplateDirectories(
        const iCAX::Application::IApplicationContext& ApplicationContext_)
    {
        return DiscoverPythonTemplateDirectoriesAt(ResolveTemplateRoot(ApplicationContext_), true);
    }

    std::vector<std::filesystem::path> DiscoverUserPythonTemplateDirectories(
        const iCAX::Application::IApplicationContext& ApplicationContext_)
    {
        const auto _Root = ResolveUserTemplateRoot(ApplicationContext_);
        EnsureUserTemplateSharedRuntime(ApplicationContext_, _Root);
        return _Root.empty() ? std::vector<std::filesystem::path>{}
            : DiscoverPythonTemplateDirectoriesAt(_Root, false);
    }

    const std::string& SharedTemplatePackageText(const std::filesystem::path& TemplateRoot_)
    {
        const auto _SharedRoot = TemplateRoot_ / "_shared";
        const auto _CacheKey = PathToUTF8(std::filesystem::weakly_canonical(TemplateRoot_));
        static std::mutex _CacheMutex;
        static std::map<std::string, std::string> _Cache;
        {
            const std::lock_guard _Lock(_CacheMutex);
            if (const auto _It = _Cache.find(_CacheKey); _It != _Cache.end())
                return _It->second;
        }

        std::error_code _Error;
        if (!std::filesystem::is_directory(_SharedRoot, _Error))
        {
            const std::lock_guard _Lock(_CacheMutex);
            return _Cache.emplace(_CacheKey, std::string{}).first->second;
        }
        std::vector<std::filesystem::path> _Files;
        for (std::filesystem::recursive_directory_iterator _Iterator(_SharedRoot, _Error), _End;
            !_Error && _Iterator != _End; _Iterator.increment(_Error))
        {
            if (_Iterator->is_regular_file(_Error)
                && (_Iterator->path().extension() == ".py"
                    || _Iterator->path().extension() == ".json"))
            {
                _Files.push_back(_Iterator->path());
            }
            _Error.clear();
        }
        if (_Error) throw std::runtime_error("TubeDesigner failed to enumerate shared template files");
        std::sort(_Files.begin(), _Files.end());
        std::string _Content;
        for (const auto& _File : _Files)
        {
            _Content += "\n\xffshared-file\xff";
            _Content += PathToUTF8(std::filesystem::relative(_File, TemplateRoot_));
            _Content += "\n";
            _Content += ReadTextFile(_File);
        }
        const std::lock_guard _Lock(_CacheMutex);
        return _Cache.emplace(_CacheKey, std::move(_Content)).first->second;
    }

    std::string LocalTemplatePackageText(
        const std::filesystem::path& Directory_, const std::filesystem::path& TemplateRoot_)
    {
        std::error_code _Error;
        std::vector<std::filesystem::path> _Files;
        for (std::filesystem::recursive_directory_iterator _Iterator(Directory_, _Error), _End;
            !_Error && _Iterator != _End; _Iterator.increment(_Error))
        {
            const auto _Relative = _Iterator->path().lexically_relative(Directory_);
            const bool _InCache = std::find(_Relative.begin(), _Relative.end(),
                std::filesystem::path("__pycache__")) != _Relative.end();
            if (_Iterator->is_regular_file(_Error)
                && _Iterator->path().filename() != "template.json"
                && _Iterator->path().filename() != "template.py"
                && !_InCache)
                _Files.push_back(_Iterator->path());
            _Error.clear();
        }
        if (_Error) throw std::runtime_error("TubeDesigner failed to enumerate local template resources");
        std::sort(_Files.begin(), _Files.end());
        std::string _Content;
        for (const auto& _File : _Files)
        {
            _Content += "\n\xfflocal-file\xff";
            _Content += PathToUTF8(std::filesystem::relative(_File, TemplateRoot_));
            _Content += "\n";
            _Content += ReadTextFile(_File);
        }
        return _Content;
    }

    SPythonTemplatePackage LoadPythonTemplatePackageFromDirectory(
        const std::filesystem::path& Directory_,
        const std::filesystem::path& TemplateRoot_,
        const std::filesystem::path& LocalPackageRoot_ = {})
    {
        const auto _DescriptorPath = Directory_ / "template.json";
        const auto _ScriptPath = Directory_ / "template.py";
        const auto _DescriptorText = ReadTextFile(_DescriptorPath);
        const auto _ScriptText = ReadTextFile(_ScriptPath);
        auto _Descriptor = iCAX::TemplateRuntime::CTemplateCodec::ParseDescriptor(
            iCAX::TemplateRuntime::CStandardJsonCodec::Parse(_DescriptorText));
        const auto _LocalRoot = LocalPackageRoot_.empty() ? TemplateRoot_ : LocalPackageRoot_;
        _Descriptor.PackageDigest = ContentDigest(
            _DescriptorText, _ScriptText + SharedTemplatePackageText(TemplateRoot_)
                + LocalTemplatePackageText(Directory_, _LocalRoot));
        return { std::move(_Descriptor), _DescriptorPath, _ScriptPath };
    }

    SPythonTemplatePackage LoadPythonTemplatePackage(
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        const std::string& TemplateID_)
    {
        const auto _TemplateRoot = ResolveTemplateRoot(ApplicationContext_);
        for (const auto& _Directory : DiscoverPythonTemplateDirectories(ApplicationContext_))
        {
            // Identify the package from its descriptor before reading and
            // hashing the scripts/resources of unrelated packages.
            const auto _DescriptorText = ReadTextFile(_Directory / "template.json");
            const auto _Descriptor = iCAX::TemplateRuntime::CTemplateCodec::ParseDescriptor(
                iCAX::TemplateRuntime::CStandardJsonCodec::Parse(_DescriptorText));
            if (_Descriptor.ID == TemplateID_)
                return LoadPythonTemplatePackageFromDirectory(_Directory, _TemplateRoot);
        }
        const auto _UserRoot = ResolveUserTemplateRoot(ApplicationContext_);
        for (const auto& _Directory : DiscoverUserPythonTemplateDirectories(ApplicationContext_))
        {
            const auto _DescriptorText = ReadTextFile(_Directory / "template.json");
            const auto _Descriptor = iCAX::TemplateRuntime::CTemplateCodec::ParseDescriptor(
                iCAX::TemplateRuntime::CStandardJsonCodec::Parse(_DescriptorText));
            if (_Descriptor.ID == TemplateID_)
                return LoadPythonTemplatePackageFromDirectory(_Directory, _TemplateRoot, _UserRoot);
        }
        throw std::invalid_argument("unsupported Python template: " + TemplateID_);
    }

    bool IsPythonTemplate(
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        const std::string& TemplateID_)
    {
        try
        {
            (void)LoadPythonTemplatePackage(ApplicationContext_, TemplateID_);
            return true;
        }
        catch (const std::invalid_argument&)
        {
            return false;
        }
    }

    std::filesystem::path ResolveEmbeddedPythonRuntimeLibrary(
        const iCAX::Application::IApplicationContext& ApplicationContext_)
    {
        return ResolveRuntimeFile(ApplicationContext_, {
            "runtime/python/python312.dll",
            "python/python312.dll",
            "src/x64/Debug/runtime/python/python312.dll",
            "src/x64/Release/runtime/python/python312.dll"
        }, "iCAX embedded Python runtime");
    }

    ObjectMap InvokePythonTemplate(
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        const ObjectMap& Request_)
    {
        static std::mutex _Mutex;
        static std::unique_ptr<iCAX::TemplateRuntime::CPythonTemplateHost> _Host;
        static std::filesystem::path _RuntimeLibrary;
        static std::filesystem::path _Worker;
        std::scoped_lock _Lock(_Mutex);
        const auto _ResolvedRuntimeLibrary = ResolveEmbeddedPythonRuntimeLibrary(ApplicationContext_);
        const auto _ResolvedWorker = ResolveRuntimeFile(ApplicationContext_, {
            "runtime/template-python/icax_template_worker.py",
            "iCAX-Engine/framework/TemplateRuntime/python/icax_template_worker.py",
            "src/iCAX-Engine/framework/TemplateRuntime/python/icax_template_worker.py",
            "TemplateRuntime/python/icax_template_worker.py"
        }, "iCAX Python template worker");
        if (!_Host || _ResolvedRuntimeLibrary != _RuntimeLibrary || _ResolvedWorker != _Worker)
        {
            _Host = std::make_unique<iCAX::TemplateRuntime::CPythonTemplateHost>(
                iCAX::TemplateRuntime::SPythonTemplateHostOptions{
                    _ResolvedRuntimeLibrary, _ResolvedWorker, _ResolvedWorker.parent_path()
                });
            _RuntimeLibrary = _ResolvedRuntimeLibrary;
            _Worker = _ResolvedWorker;
        }
        return _Host->Invoke(Request_);
    }

    SNeutralTemplateEvaluation EvaluateNeutralTemplate(
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        const ObjectMap& Payload_,
        const std::string& GeometryPurpose_,
        const std::string& ExpectedPackageDigest_ = {},
        iCAX::Application::IProductUserDataStore* ComponentStore_ = nullptr,
        const ObjectMap* FrozenComponents_ = nullptr)
    {
        const auto _RequestedTemplateID = GetString(Payload_, "templateId", "single-face-security-window");
        auto _Package = LoadPythonTemplatePackage(ApplicationContext_, _RequestedTemplateID);
        const auto _TemplateID = GetString(Payload_, "templateId", _Package.Descriptor.ID);
        const auto _TemplateVersion = GetString(Payload_, "templateVersion", _Package.Descriptor.Version);
        if (!ExpectedPackageDigest_.empty()
            && (ExpectedPackageDigest_ != _Package.Descriptor.PackageDigest
                || _TemplateID != _Package.Descriptor.ID || _TemplateVersion != _Package.Descriptor.Version))
            throw std::runtime_error("模板已变化，请重新生成产品后再拆单");
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
        if (const auto _Overrides = Payload_.find("tubeDesignerProfileOverrides");
            _Overrides != Payload_.end())
        {
            if (!_Overrides->second.Is<ObjectMap>())
                throw std::invalid_argument("tubeDesignerProfileOverrides must be an object");
            const auto _OverrideValues = _Overrides->second.To<ObjectMap>();
            if (_OverrideValues.size() > 64)
                throw std::invalid_argument("tubeDesignerProfileOverrides has too many entries");
            for (const auto& [_Prefix, _Value] : _OverrideValues)
            {
                if (!_Value.Is<ObjectMap>()) throw std::invalid_argument("管型快照必须是对象");
                const auto _Profile = _Value.To<ObjectMap>();
                if (GetString(_Profile, "profileScope") != "template") continue;
                if (GetString(_Profile, "templateId") != _Package.Descriptor.ID)
                    throw std::invalid_argument("模板自带管型只能用于所属模板");
                const auto _ResourcesIt = _Package.Descriptor.Extensions.find("profileResources");
                if (_ResourcesIt == _Package.Descriptor.Extensions.end() || !_ResourcesIt->second.Is<ObjectMap>()
                    || !_ResourcesIt->second.To<ObjectMap>().contains(GetString(_Profile, "profileDefinitionId")))
                    throw std::invalid_argument("当前模板没有声明此管型资源");
            }
            _Parameters["tubeDesignerProfileOverrides"] = _OverrideValues;
        }
        auto _Request = iCAX::TemplateRuntime::CTemplateCodec::MakeEvaluationRequest(
            _Package.Descriptor, _Parameters, PathToUTF8(_Package.ScriptPath));
        auto _Context = _Request.at("context").To<ObjectMap>();
        _Context["geometryPurpose"] = GeometryPurpose_;
        _Request["context"] = std::move(_Context);
        auto _Document = InvokePythonTemplate(ApplicationContext_, _Request);
        const auto _GeometryNodes = _Document.at("geometry").To<VariantArray>();
        const bool _HasResource = _Package.Descriptor.Extensions.contains("modelResources")
            || std::any_of(_GeometryNodes.begin(), _GeometryNodes.end(), [](const auto& Node_) {
                return Node_.Is<ObjectMap>() && GetString(Node_.To<ObjectMap>(), "operator") == "resource";
            });
        if (_HasResource)
            ResolveTemplateComponentResources(_Document, _Package.DescriptorPath.parent_path(),
                _Package.Descriptor.Extensions, ResolveComponentModelRoot(ApplicationContext_),
                ResolveUserComponentModelRoot(ApplicationContext_),
                ComponentStore_, FrozenComponents_);
        auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(Variant(_Document));
        if (_Model.TemplateID != _Package.Descriptor.ID
            || _Model.TemplateVersion != _Package.Descriptor.Version
            || _Model.PackageDigest != _Package.Descriptor.PackageDigest)
        {
            throw std::runtime_error("Python template returned a model with a mismatched identity");
        }
        if (_Model.Parameters != _Parameters)
            throw std::runtime_error("Python template changed the normalized parameter values");
        if (GetString(_Model.Extensions, "tubeDesigner.geometryPurpose") != GeometryPurpose_
            || _Model.Outputs.size() != 1 || _Model.Outputs.front().Purpose != "result")
            throw std::runtime_error("Python template must return one neutral result for the requested geometry");
        for (const auto& _Item : _Model.Items)
        {
            if (_Item.Representations.size() != 1 || !_Item.Representations.contains("result"))
                throw std::runtime_error("Python template returned alternative geometry representations");
        }
        return {
            std::move(_Package.Descriptor), std::move(_Model),
            std::move(_Document), std::move(_Parameters)
        };
    }

    VariantArray TemplateCatalog(
        const iCAX::Application::IApplicationContext& ApplicationContext_)
    {
        VariantArray _Templates;
        const auto _Append = [&](const std::vector<std::filesystem::path>& _Directories,
            const std::string& _LibraryScope) {
        for (const auto& _Directory : _Directories)
        {
            try
            {
                // The startup snapshot only needs the catalog card. Do not
                // load scripts, shared files, or full parameter presentation
                // until a template is actually selected.
                const auto _DescriptorText = ReadTextFile(_Directory / "template.json");
                const auto _Descriptor = iCAX::TemplateRuntime::CTemplateCodec::ParseDescriptor(
                    iCAX::TemplateRuntime::CStandardJsonCodec::Parse(_DescriptorText));
                // The list is loaded during startup and is used for choosing a
                // template. Keep it small; the full descriptor is hydrated by
                // the web UI only after the response arrives.
                ObjectMap _Presentation;
                _Presentation["id"] = _Descriptor.ID;
                _Presentation["version"] = _Descriptor.Version;
                _Presentation["name"] = _Descriptor.DisplayName.Resolve("zh-CN");
                _Presentation["description"] = _Descriptor.Description;
                _Presentation["available"] = true;
                _Presentation["libraryScope"] = _LibraryScope;
                _Presentation["ownerScope"] = _LibraryScope;
                if (const auto _Catalog = _Descriptor.Extensions.find("catalog");
                    _Catalog != _Descriptor.Extensions.end())
                    _Presentation["extensions"] = ObjectMap{{ "catalog", _Catalog->second }};
                AttachTemplateCatalogAssets(_Presentation, _Directory);
                _Templates.emplace_back(std::move(_Presentation));
            }
            catch (const std::exception& Error_)
            {
                ObjectMap _Unavailable;
                _Unavailable["id"] = _Directory.filename().string();
                _Unavailable["version"] = std::string();
                _Unavailable["name"] = _Directory.filename().string();
                _Unavailable["available"] = false;
                _Unavailable["status"] = std::string("模板包不可用：") + Error_.what();
                _Unavailable["parameters"] = VariantArray{};
                _Templates.emplace_back(_Unavailable);
            }
        }
        };
        _Append(DiscoverPythonTemplateDirectories(ApplicationContext_), "system");
        _Append(DiscoverUserPythonTemplateDirectories(ApplicationContext_), "user");

        return _Templates;
    }

    iCAX::Resource::CResourceReference StorePreparedBRep(
        iCAX::Project::ISceneContext& Scene_,
        const std::string& ResourceID_,
        const std::string& DisplayName_,
        iCAX::GeometryData::BRepModel&& Model_)
    {
        auto& _Resources = Scene_.Resources();
        auto _BRep = std::make_shared<iCAX::GeometryData::BRepModel>(std::move(Model_));
        iCAX::Resource::CResourceInfo _Info;
        _Info.Name = DisplayName_;
        _Info.ResourceTypeID = iCAX::GeometryData::BRepModel::kResourceTypeName;
        _Info.Persistence = iCAX::Resource::EResourcePersistenceMode::RuntimeOnly;
        _Info.nSchemaVersion = 1;
        _Info.Metadata["source"] = "tube-designer";
        iCAX::Resource::CResourceInfo _StoredInfo;
        const auto _Mutation = _Resources.PutVersioned<iCAX::GeometryData::BRepModel>(
            ResourceID_, std::move(_BRep), _Info,
            iCAX::Resource::EResourceVersionCondition::None, 0, &_StoredInfo);
        if (_Mutation == iCAX::Resource::EResourceMutationResult::PreconditionFailed
            || _StoredInfo.nVersion == 0)
        {
            throw std::runtime_error("TubeDesigner failed to commit a BRep resource version");
        }
        return { ResourceID_, _StoredInfo.nVersion };
    }

    // Defined with the punch-wizard helpers below.  The side-sketch save
    // handler also uses the same task-backed conversion and versioned BRep
    // storage path, so keep one implementation for both features.
    iCAX::Resource::CResourceReference StorePunchBRep(
        iCAX::Project::ISceneContext& Scene_, const std::string& Key_,
        const std::string& Name_, const TopoDS_Shape& Shape_,
        const iCAX::Interaction::CInvocation& Request_, bool ReuseIdentical_);

    iCAX::Resource::CResourceReference StoreBRep(
        iCAX::Project::ISceneContext& Scene_,
        const std::string& StableName_,
        const std::string& DisplayName_,
        const TopoDS_Shape& Shape_)
    {
        const auto _ResourceID = Scene_.Resources().MakeNamedResourceURL(StableName_);
        // Tolerance controls display triangulation only, not analytic BRep data.
        return StorePreparedBRep(Scene_, _ResourceID, DisplayName_,
            iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(Shape_, DisplayName_, _ResourceID, 0.025));
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
            _Info.Persistence = iCAX::Resource::EResourcePersistenceMode::RuntimeOnly;
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

    iCAX::Resource::CResourceReference EnsureComponentCSGOperandMaterial(
        iCAX::Project::ISceneContext& Scene_)
    {
        auto& _Resources = Scene_.Resources();
        const auto _ResourceID = _Resources.MakeNamedResourceURL(
            "tube-designer/material/component-csg-operand");
        if (_Resources.GetVersion(_ResourceID) == 0)
        {
            auto _Material = std::make_shared<iCAX::Render::SRenderMaterialData>();
            _Material->nDataVersion = 1;
            // The selected operand stays visible even when a difference removes
            // it from the result. Alpha is carried in the low RGBA byte.
            _Material->nColorRGBA = 0xF0A23D66u;
            _Material->nAmbientRGBA = 0xF0A23D66u;
            _Material->nSpecularRGBA = 0x6B421AFFu;
            _Material->nEmissiveRGBA = 0x1A0A00FFu;
            _Material->nLineWidth = 1.0f;
            iCAX::Resource::CResourceInfo _Info;
            _Info.Name = "TubeDesigner selected CSG operand material";
            _Info.ResourceTypeID = iCAX::Render::SRenderMaterialData::kResourceTypeName;
            _Info.Persistence = iCAX::Resource::EResourcePersistenceMode::RuntimeOnly;
            iCAX::Resource::CResourceInfo _StoredInfo;
            const auto _Mutation = _Resources.PutVersioned<iCAX::Render::SRenderMaterialData>(
                _ResourceID, std::move(_Material), _Info,
                iCAX::Resource::EResourceVersionCondition::MustNotExist, 0, &_StoredInfo);
            if (_Mutation == iCAX::Resource::EResourceMutationResult::PreconditionFailed)
                throw std::runtime_error("TubeDesigner CSG operand material changed concurrently");
        }
        return iCAX::RenderInteraction::EnsureFrontendMaterialResource(_Resources, _ResourceID);
    }

    iCAX::Resource::CResourceReference EnsurePunchPreviewBlankMaterial(
        iCAX::Project::ISceneContext& Scene_)
    {
        auto& _Resources = Scene_.Resources();
        const auto _ResourceID = _Resources.MakeNamedResourceURL(
            "tube-designer/material/punch-preview-blank");
        if (_Resources.GetVersion(_ResourceID) == 0)
        {
            auto _Material = std::make_shared<iCAX::Render::SRenderMaterialData>();
            _Material->nDataVersion = 1;
            // Keep the blank readable while the orange cutter remains visible
            // through the wall. Alpha is carried in the low RGBA byte.
            _Material->nColorRGBA = 0x78AFC5B8u;
            _Material->nAmbientRGBA = 0x78AFC5B8u;
            _Material->nSpecularRGBA = 0xDCEEF5FFu;
            _Material->nEmissiveRGBA = 0x000000FFu;
            _Material->nLineWidth = 1.0f;
            iCAX::Resource::CResourceInfo _Info;
            _Info.Name = "TubeDesigner punch preview blank material";
            _Info.ResourceTypeID = iCAX::Render::SRenderMaterialData::kResourceTypeName;
            _Info.Persistence = iCAX::Resource::EResourcePersistenceMode::RuntimeOnly;
            iCAX::Resource::CResourceInfo _StoredInfo;
            const auto _Mutation = _Resources.PutVersioned<iCAX::Render::SRenderMaterialData>(
                _ResourceID, std::move(_Material), _Info,
                iCAX::Resource::EResourceVersionCondition::MustNotExist, 0, &_StoredInfo);
            if (_Mutation == iCAX::Resource::EResourceMutationResult::PreconditionFailed)
                throw std::runtime_error("TubeDesigner punch preview blank material changed concurrently");
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

    std::string MakePartNameFromFileName(
        const std::string& FileName_, const std::string& Fallback_)
    {
        auto _Name = FileName_;
        auto _Lower = _Name;
        std::transform(_Lower.begin(), _Lower.end(), _Lower.begin(), [](unsigned char C_) {
            return static_cast<char>(std::tolower(C_));
        });
        for (const auto* _Extension : { ".step", ".stp", ".iges", ".igs" })
            if (_Lower.ends_with(_Extension))
            {
                _Name.resize(_Name.size() - std::string(_Extension).size());
                break;
            }
        return _Name.empty() ? Fallback_ : _Name;
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

    std::filesystem::path MakeUniqueExportPath(
        const std::filesystem::path& Directory_,
        const std::string& BaseName_,
        const std::string& Extension_)
    {
        const auto _SafeBase = MakeSafePathSegment(BaseName_, "profile");
        auto _Candidate = Directory_ / Utf8Path(_SafeBase + Extension_);
        for (std::uint64_t _Index = 2; std::filesystem::exists(_Candidate); ++_Index)
        {
            if (_Index > 10000)
                throw std::runtime_error("TubeDesigner could not choose a unique export file name");
            _Candidate = Directory_ / Utf8Path(
                _SafeBase + "_" + std::to_string(_Index) + Extension_);
        }
        return _Candidate;
    }

    std::string ProfileNumberText(const double Value_)
    {
        std::ostringstream _Text;
        _Text << std::fixed << std::setprecision(3) << Value_;
        auto _Result = _Text.str();
        while (_Result.size() > 1 && _Result.back() == '0') _Result.pop_back();
        if (!_Result.empty() && _Result.back() == '.') _Result.pop_back();
        return _Result;
    }

    std::string TubeProfileDisplayName(const std::string& TypeID_)
    {
        if (TypeID_ == "round") return "圆管";
        if (TypeID_ == "rect") return "矩形管";
        if (TypeID_ == "ellipse") return "椭圆管";
        if (TypeID_ == "flat-oval") return "腰圆管";
        if (TypeID_ == "angle") return "角钢";
        if (TypeID_ == "channel") return "槽钢";
        if (TypeID_ == "i-section") return "工字钢";
        if (TypeID_ == "t-section") return "T 型钢";
        if (TypeID_ == "z-section") return "Z 型钢";
        if (TypeID_ == "polygon") return "多边形管";
        return TypeID_.empty() ? "异型管" : TypeID_;
    }

    std::string TubeProfileSpecification(
        const std::string& TypeID_, const ObjectMap& Parameters_)
    {
        const auto _Width = GetDouble(Parameters_, "width", 0.0);
        const auto _Depth = GetDouble(Parameters_, "depth", 0.0);
        const auto _Wall = GetDouble(Parameters_, "wallThickness", 0.0);
        const auto _Radius = GetDouble(Parameters_, "cornerRadius", 0.0);
        if (TypeID_ == "round" && _Width > 0.0)
            return "Ø" + ProfileNumberText(_Width) + (_Wall > 0.0
                ? " × 壁厚 " + ProfileNumberText(_Wall) : "");
        if (TypeID_ == "rect" && _Width > 0.0 && _Depth > 0.0)
            return ProfileNumberText(_Width) + " × " + ProfileNumberText(_Depth)
                + (_Radius > 0.0 ? " × R" + ProfileNumberText(_Radius) : "")
                + (_Wall > 0.0 ? " × 壁厚 " + ProfileNumberText(_Wall) : "");
        if (_Width > 0.0 && _Depth > 0.0)
            return ProfileNumberText(_Width) + " × " + ProfileNumberText(_Depth)
                + (_Wall > 0.0 ? " × 壁厚 " + ProfileNumberText(_Wall) : "");
        return TubeProfileDisplayName(TypeID_);
    }

    ObjectMap TransformPayload(const Transform3& Transform_)
    {
        VariantArray _Rows;
        for (const auto& _Row : Transform_.Matrix.Values)
        {
            VariantArray _Values;
            for (const auto _Value : _Row) _Values.emplace_back(_Value);
            _Rows.emplace_back(std::move(_Values));
        }
        return {
            { "schema", std::string("icax.transform3") },
            { "schemaVersion", 1ull },
            { "matrix", std::move(_Rows) },
            { "axis", std::string("+X") },
            { "origin", VariantArray{ 0.0, 0.0, 0.0 } }
        };
    }

    ObjectMap IdentityTransformPayload()
    {
        return TransformPayload(Transform3{});
    }

    ObjectMap SectionContoursPayload(
        const iCAX::ExtrusionRecognition::SSectionSnapshot& Section_)
    {
        VariantArray _Loops;
        _Loops.reserve(Section_.Loops.size());
        for (std::size_t _Index = 0; _Index < Section_.Loops.size(); ++_Index)
        {
            const auto& _Loop = Section_.Loops[_Index];
            VariantArray _Points;
            _Points.reserve(_Loop.Points.size());
            for (const auto& _Point : _Loop.Points)
                _Points.emplace_back(VariantArray{ _Point.X, _Point.Y });
            _Loops.emplace_back(ObjectMap{
                { "id", _Loop.ID.empty()
                    ? (_Index == 0 ? std::string("outer") : "inner-" + std::to_string(_Index))
                    : _Loop.ID },
                { "inner", _Loop.bInner },
                { "points", std::move(_Points) }
            });
        }
        return {
            { "schema", std::string("icax.section-contours.v1") },
            { "schemaVersion", 1ull },
            { "coordinateSystem", std::string("YOZ") },
            { "loops", std::move(_Loops) }
        };
    }

    bool IsPointContourArray(const VariantArray& Contours_)
    {
        if (Contours_.empty()) return true;
        for (const auto& _Contour : Contours_)
        {
            if (!_Contour.Is<ObjectMap>()) return false;
            const auto& _Object = _Contour.To<ObjectMap>();
            const auto _Points = _Object.find("points");
            if (_Points == _Object.end() || !_Points->second.Is<VariantArray>())
                return false;
        }
        return true;
    }

    ObjectMap ProfileContoursPayload(const ObjectMap& Profile_)
    {
        const auto _Found = Profile_.find("contours");
        if (_Found != Profile_.end() && _Found->second.Is<VariantArray>())
        {
            const auto& _Contours = _Found->second.To<VariantArray>();
            if (!IsPointContourArray(_Contours))
            {
                return {
                    { "schema", std::string("icax.tube-profile-contours.v1") },
                    { "schemaVersion", 1ull },
                    { "coordinateSystem", std::string("YOZ") },
                    { "representation", std::string("parametric") },
                    { "definitions", _Contours }
                };
            }
            return {
                { "schema", std::string("icax.section-contours.v1") },
                { "schemaVersion", 1ull },
                { "coordinateSystem", std::string("YOZ") },
                { "representation", std::string("points") },
                { "loops", _Contours }
            };
        }
        return {
            { "schema", std::string("icax.section-contours.v1") },
            { "schemaVersion", 1ull },
            { "coordinateSystem", std::string("YOZ") },
            { "representation", std::string("points") },
            { "loops", VariantArray() }
        };
    }

    ObjectMap TubeProfileParameterPayload(const ObjectMap& Profile_)
    {
        auto _Parameters = Profile_;
        _Parameters.erase("schema");
        _Parameters.erase("schemaVersion");
        _Parameters.erase("id");
        _Parameters.erase("packageVersion");
        _Parameters.erase("kind");
        _Parameters.erase("displayName");
        _Parameters.erase("specification");
        _Parameters.erase("contours");
        return _Parameters;
    }

    iCAX::Data::PropertySet TubeProfileComponentProperties(
        const ObjectMap& Profile_, const std::string& Source_)
    {
        const auto _TypeID = GetString(Profile_, "id", GetString(Profile_, "kind"));
        return {
            { CTubeProfileComponent::PropertyName_TypeID, PropertyValue(_TypeID) },
            { CTubeProfileComponent::PropertyName_DisplayName, PropertyValue(
                GetString(Profile_, "displayName", TubeProfileDisplayName(_TypeID))) },
            { CTubeProfileComponent::PropertyName_Specification, PropertyValue(
                GetString(Profile_, "specification", TubeProfileSpecification(_TypeID, Profile_))) },
            { CTubeProfileComponent::PropertyName_Source, PropertyValue(Source_) },
            { CTubeProfileComponent::PropertyName_Parameters, PropertyValue(
                TubeProfileParameterPayload(Profile_)) },
            { CTubeProfileComponent::PropertyName_Contours, PropertyValue(ProfileContoursPayload(Profile_)) },
            { CTubeProfileComponent::PropertyName_Transform, PropertyValue(IdentityTransformPayload()) }
        };
    }

    ObjectMap ProfileSnapshotFromProfile(
        const ObjectMap& Profile_, const std::string& Source_)
    {
        const auto _TypeID = GetString(Profile_, "id", GetString(Profile_, "kind"));
        auto _Result = TubeProfileParameterPayload(Profile_);
        _Result["schema"] = std::string("icax.tube-profile");
        _Result["schemaVersion"] = 1ull;
        _Result["id"] = _TypeID;
        _Result["kind"] = GetString(Profile_, "kind", _TypeID);
        _Result["displayName"] = GetString(Profile_, "displayName", TubeProfileDisplayName(_TypeID));
        _Result["specification"] = GetString(
            Profile_, "specification", TubeProfileSpecification(_TypeID, Profile_));
        _Result["source"] = Source_;
        _Result["parameters"] = TubeProfileParameterPayload(Profile_);
        const auto _ContourPayload = ProfileContoursPayload(Profile_);
        _Result["contours"] = _ContourPayload.contains("loops")
            ? _ContourPayload.at("loops")
            : (_ContourPayload.contains("definitions")
                ? _ContourPayload.at("definitions") : VariantArray());
        _Result["placement"] = IdentityTransformPayload();
        return _Result;
    }

    iCAX::Data::PropertySet TubeProfileComponentProperties(
        const SRecognitionResult& Recognition_, const std::string& Source_)
    {
        const auto _TypeID = Recognition_.SectionTypeID.empty()
            ? std::string("irregular") : Recognition_.SectionTypeID;
        const auto _DisplayName = TubeProfileDisplayName(_TypeID);
        const auto _Specification = Recognition_.SectionTypeID.empty()
            ? std::string("异型管")
            : TubeProfileSpecification(_TypeID, Recognition_.SectionParameters);
        return {
            { CTubeProfileComponent::PropertyName_TypeID, PropertyValue(_TypeID) },
            { CTubeProfileComponent::PropertyName_DisplayName, PropertyValue(_DisplayName) },
            { CTubeProfileComponent::PropertyName_Specification, PropertyValue(_Specification) },
            { CTubeProfileComponent::PropertyName_Source, PropertyValue(Source_) },
            { CTubeProfileComponent::PropertyName_Parameters, PropertyValue(Recognition_.SectionParameters) },
            { CTubeProfileComponent::PropertyName_Contours, PropertyValue(SectionContoursPayload(Recognition_.Section)) },
            { CTubeProfileComponent::PropertyName_Transform, PropertyValue(TransformPayload(Recognition_.TRSF)) }
        };
    }

    iCAX::Data::PropertySet TubeProfileComponentPropertiesFromSnapshot(
        const ObjectMap& Profile_, const std::string& SourceOverride_ = {})
    {
        const auto _TypeID = GetString(Profile_, "id", GetString(Profile_, "kind", "irregular"));
        const auto _Parameters = Profile_.contains("parameters")
            && Profile_.at("parameters").Is<ObjectMap>()
            ? Profile_.at("parameters").To<ObjectMap>()
            : TubeProfileParameterPayload(Profile_);
        const auto _Source = SourceOverride_.empty()
            ? GetString(Profile_, "source") : SourceOverride_;
        ObjectMap _ContourInput;
        if (const auto _ContourValue = Profile_.find("contours");
            _ContourValue != Profile_.end())
        {
            _ContourInput["contours"] = _ContourValue->second;
        }
        const auto _Contours = _ContourInput.empty()
            ? ProfileContoursPayload(Profile_) : ProfileContoursPayload(_ContourInput);
        const auto _Transform = Profile_.contains("placement")
            && Profile_.at("placement").Is<ObjectMap>()
            ? Profile_.at("placement").To<ObjectMap>() : IdentityTransformPayload();
        return {
            { CTubeProfileComponent::PropertyName_TypeID, PropertyValue(_TypeID) },
            { CTubeProfileComponent::PropertyName_DisplayName, PropertyValue(
                GetString(Profile_, "displayName", TubeProfileDisplayName(_TypeID))) },
            { CTubeProfileComponent::PropertyName_Specification, PropertyValue(
                GetString(Profile_, "specification", TubeProfileSpecification(_TypeID, _Parameters))) },
            { CTubeProfileComponent::PropertyName_Source, PropertyValue(_Source) },
            { CTubeProfileComponent::PropertyName_Parameters, PropertyValue(_Parameters) },
            { CTubeProfileComponent::PropertyName_Contours, PropertyValue(_Contours) },
            { CTubeProfileComponent::PropertyName_Transform, PropertyValue(_Transform) }
        };
    }

    ObjectMap ProfileSnapshot(const CTubeProfileComponent& Component_)
    {
        auto _Result = Component_.GetParameters();
        _Result["schema"] = std::string("icax.tube-profile");
        _Result["schemaVersion"] = 1ull;
        _Result["id"] = Component_.GetTypeID();
        _Result["kind"] = Component_.GetTypeID();
        _Result["displayName"] = Component_.GetDisplayName();
        _Result["specification"] = Component_.GetSpecification();
        _Result["source"] = Component_.GetSource();
        _Result["parameters"] = Component_.GetParameters();
        _Result["contours"] = Component_.GetContours().contains("loops")
            ? Component_.GetContours().at("loops")
            : (Component_.GetContours().contains("definitions")
                ? Component_.GetContours().at("definitions") : VariantArray());
        _Result["placement"] = Component_.GetTransform();
        return _Result;
    }

    ObjectMap ProfileSnapshot(const SRecognitionResult& Recognition_, const std::string& Source_)
    {
        const auto _TypeID = Recognition_.SectionTypeID.empty()
            ? std::string("irregular") : Recognition_.SectionTypeID;
        auto _Result = Recognition_.SectionParameters;
        _Result["schema"] = std::string("icax.tube-profile");
        _Result["schemaVersion"] = 1ull;
        _Result["id"] = _TypeID;
        _Result["kind"] = _TypeID;
        _Result["displayName"] = TubeProfileDisplayName(_TypeID);
        _Result["specification"] = Recognition_.SectionTypeID.empty()
            ? std::string("异型管")
            : TubeProfileSpecification(_TypeID, Recognition_.SectionParameters);
        _Result["source"] = Source_;
        _Result["parameters"] = Recognition_.SectionParameters;
        _Result["contours"] = SectionContoursPayload(Recognition_.Section).at("loops");
        _Result["placement"] = TransformPayload(Recognition_.TRSF);
        return _Result;
    }

    ObjectMap OptionalProfileProperties(const CTubeProfileComponent* Component_)
    {
        return Component_ ? ProfileSnapshot(*Component_) : ObjectMap{};
    }

    std::string ManufacturingPartKind(const ObjectMap& Properties_)
    {
        return GetString(Properties_, "manufacturing.partKind",
            GetString(Properties_, "manufacturing.materialCategory", "tube"));
    }

    std::string ManufacturingKindName(const std::string& Kind_)
    {
        if (Kind_ == "plate") return "板件";
        if (Kind_ == "glass") return "玻璃";
        if (Kind_ == "accessory") return "配件";
        return "管材";
    }

    ObjectMap ShapeBounds(const TopoDS_Shape& Shape_)
    {
        Bnd_Box _Box;
        BRepBndLib::AddOptimal(Shape_, _Box, false, false);
        _Box.SetGap(0);
        if (_Box.IsVoid() || _Box.IsOpen()) throw std::invalid_argument("零件实体包络无效");
        double _X0, _Y0, _Z0, _X1, _Y1, _Z1;
        _Box.Get(_X0, _Y0, _Z0, _X1, _Y1, _Z1);
        return { { "width", _X1 - _X0 }, { "depth", _Y1 - _Y0 }, { "height", _Z1 - _Z0 },
            { "min", VariantArray{ _X0, _Y0, _Z0 } }, { "max", VariantArray{ _X1, _Y1, _Z1 } } };
    }

    TopoDS_Shape NormalizeManufacturingShape(const TopoDS_Shape& Shape_, const ObjectMap& Properties_)
    {
        if (ManufacturingPartKind(Properties_) != "accessory")
            return NormalizeLinearPartForManufacturing(Shape_);
        const auto _Bounds = ShapeBounds(Shape_);
        const auto _Min = _Bounds.at("min").To<VariantArray>();
        const auto _Max = _Bounds.at("max").To<VariantArray>();
        gp_Trsf _Placement;
        _Placement.SetTranslation(gp_Vec(-(_Min[0].To<double>() + _Max[0].To<double>()) / 2,
            -(_Min[1].To<double>() + _Max[1].To<double>()) / 2, -_Min[2].To<double>()));
        return Shape_.Moved(TopLoc_Location(_Placement));
    }

    ObjectMap ManufacturingPlate(const ObjectMap& Properties_)
    {
        const auto _Found = Properties_.find("manufacturing.plate");
        return _Found != Properties_.end() && _Found->second.Is<ObjectMap>()
            ? _Found->second.To<ObjectMap>() : ObjectMap{};
    }

    std::string PlateSpecification(const ObjectMap& Plate_)
    {
        std::ostringstream _Text;
        _Text << GetDouble(Plate_, "width", 0) << " × " << GetDouble(Plate_, "height", 0)
            << " × " << GetDouble(Plate_, "thickness", 0) << " mm";
        return _Text.str();
    }

    std::string ComponentSpecification(const ObjectMap& Properties_)
    {
        const auto _It = Properties_.find("manufacturing.modelBounds");
        if (_It == Properties_.end() || !_It->second.Is<ObjectMap>())
            return GetString(Properties_, "manufacturing.modelName");
        const auto _Bounds = _It->second.To<ObjectMap>();
        std::ostringstream _Text;
        _Text << GetString(Properties_, "manufacturing.modelName") << " "
            << GetDouble(_Bounds, "width", 0) << " × " << GetDouble(_Bounds, "depth", 0)
            << " × " << GetDouble(_Bounds, "height", 0) << " mm";
        return _Text.str();
    }

    struct SResolvedPartPresentation final
    {
        std::string Name;
        ObjectMap Profile;
    };

    SResolvedPartPresentation ResolvePartPresentation(
        iCAX::Database::IRepository& Repository_,
        const CManufacturingPartComponent& Part_)
    {
        const auto _PartProfile = GetComponent<CTubeProfileComponent>(Part_.GetEntity());
        SResolvedPartPresentation _Result{
            TrimText(Part_.GetName()).empty()
                ? MakePartNameFromFileName(Part_.GetFileName(), Part_.GetPartNumber())
                : TrimText(Part_.GetName()),
            OptionalProfileProperties(_PartProfile.get())
        };

        if (const auto _MemberEntity = Repository_.GetEntity(Part_.GetSourceMemberID());
            const auto _Member = GetComponent<CAssemblyMemberComponent>(_MemberEntity))
        {
            if (_Result.Profile.empty())
                _Result.Profile = OptionalProfileProperties(
                    GetComponent<CTubeProfileComponent>(_MemberEntity).get());
        }
        return _Result;
    }

    ObjectMap PartPropertiesSnapshot(
        const CManufacturingPartComponent& Part_)
    {
        auto _Properties = Part_.GetItemProperties();
        const auto _Entity = Part_.GetEntity();
        if (!_Entity) return _Properties;
        if (const auto _Drawing = GetComponent<CPartDrawingComponent>(_Entity);
            _Drawing && !_Drawing->GetDefinition().empty())
        {
            _Properties["tubeDesigner.partDrawing"] = _Drawing->GetDefinition();
            const auto& _Definition = _Drawing->GetDefinition();
            if (const auto _Ends = _Definition.find("baseEndProcess");
                _Ends != _Definition.end() && _Ends->second.Is<ObjectMap>())
                _Properties["tubeDesigner.endProcess"] = _Ends->second;
        }
        if (const auto _Punch = GetComponent<CPunchWizardComponent>(_Entity);
            _Punch && !_Punch->GetDefinition().empty())
        {
            _Properties["tubeDesigner.punchWizard"] = _Punch->GetDefinition();
            const auto& _Definition = _Punch->GetDefinition();
            if (const auto _Ends = _Definition.find("baseEndProcess");
                _Ends != _Definition.end() && _Ends->second.Is<ObjectMap>())
                _Properties["tubeDesigner.endProcess"] = _Ends->second;
        }
        if (const auto _Source = GetComponent<CNestingSourceComponent>(_Entity))
        {
            _Properties["nesting.source"] = ObjectMap{
                {"kind", std::string("product-disassembly")},
                {"sourceProductEntityId", UuidToString(_Source->GetSourceProductID())},
                {"sourceGenerationRunId", UuidToString(_Source->GetSourceGenerationRunID())},
                {"sourcePartEntityId", UuidToString(_Source->GetSourcePartEntityID())},
                {"sourceProductName", _Source->GetSourceProductName()},
                {"sourceProductCode", _Source->GetSourceProductCode()},
                {"sourceStableKey", _Source->GetSourceStableKey()}
            };
        }
        return _Properties;
    }

    std::uint64_t EffectiveManufacturingQuantity(
        const CManufacturingPartComponent& Part_, const std::uint64_t InstanceQuantity_)
    {
        const auto _Override = Part_.GetQuantityOverride();
        return _Override == 0
            ? ProductionQuantity(Part_.GetQuantity(), InstanceQuantity_)
            : ValidateInstanceQuantity(_Override);
    }

    std::optional<SPreparedManufacturingPart> FindTransientPart(
        const iCAX::Project::ISceneContext& Scene_, const iCAX::Data::uuid& PartID_)
    {
        const std::lock_guard _Lock(gTransientDisassemblyMutex);
        const auto _Scene = gTransientDisassembly.find(Scene_.GetSceneID());
        if (_Scene == gTransientDisassembly.end()) return std::nullopt;
        for (const auto& [_ProductID, _Prepared] : _Scene->second.Products)
            for (const auto& _Part : _Prepared.Parts)
                if (_Part.PartID == PartID_) return _Part;
        return std::nullopt;
    }

    std::vector<SPreparedProductDisassembly> CopyTransientDisassembly(
        const iCAX::Project::ISceneContext& Scene_)
    {
        const std::lock_guard _Lock(gTransientDisassemblyMutex);
        const auto _Scene = gTransientDisassembly.find(Scene_.GetSceneID());
        if (_Scene == gTransientDisassembly.end()) return {};

        // Runtime-only BRep resources are deliberately not written to the
        // project file.  If the same project is reopened in this process, the
        // persisted scene ID may be the same but its resource library is new;
        // in that case the old in-memory disassembly must not reappear.
        for (const auto& [_ProductID, _Prepared] : _Scene->second.Products)
            for (const auto& _Part : _Prepared.Parts)
                if (_Part.ManufacturingResource.URL.empty()
                    || !Scene_.Resources().Get<iCAX::GeometryData::BRepModel>(
                        _Part.ManufacturingResource.URL,
                        _Part.ManufacturingResource.nVersion))
                {
                    gTransientDisassembly.erase(_Scene);
                    return {};
                }
        std::vector<SPreparedProductDisassembly> _Result;
        _Result.reserve(_Scene->second.Products.size());
        for (const auto& [_ID, _Prepared] : _Scene->second.Products) _Result.push_back(_Prepared);
        return _Result;
    }

    struct STransientPartSource final
    {
        iCAX::Data::uuid ProductID;
        iCAX::Data::uuid GenerationRunID;
        SPreparedManufacturingPart Part;
    };

    std::optional<STransientPartSource> FindTransientPartSource(
        const std::vector<SPreparedProductDisassembly>& Prepared_,
        const iCAX::Data::uuid& PartID_)
    {
        for (const auto& _Product : Prepared_)
            for (const auto& _Part : _Product.Parts)
                if (_Part.PartID == PartID_)
                    return STransientPartSource{
                        _Product.ProductID, _Product.GenerationRunID, _Part };
        return std::nullopt;
    }

    bool IsProductNestingReference(const ObjectMap& Reference_)
    {
        return GetString(Reference_, "source") == "product"
            || !GetString(Reference_, "productEntityId").empty();
    }

    ObjectMap MakeProductNestingReference(
        const SPreparedProductDisassembly& Product_,
        const SPreparedManufacturingPart& Part_)
    {
        return ObjectMap{
            {"partEntityId", UuidToString(Part_.PartID)},
            {"generationRunId", UuidToString(Product_.GenerationRunID)},
            {"productEntityId", UuidToString(Product_.ProductID)},
            {"stableKey", Part_.StableKey},
            {"source", std::string("product")}
        };
    }

    std::set<std::string> LinkedNestingPartIDs(
        iCAX::Project::ISceneContext& Scene_)
    {
        std::set<std::string> _Result;
        const auto _Root = GetComponent<CTubeDesignerRootComponent>(
            Scene_.Database().GetMetaEntity());
        const auto _Task = _Root ? _Root->GetNestingTask() : ObjectMap();
        const auto _References = _Task.find("parts");
        if (_References == _Task.end() || !_References->second.Is<VariantArray>())
            return _Result;
        for (const auto& _Value : _References->second.To<VariantArray>())
        {
            if (!_Value.Is<ObjectMap>()) continue;
            const auto _Reference = _Value.To<ObjectMap>();
            if (IsProductNestingReference(_Reference))
            {
                const auto _ID = GetString(_Reference, "partEntityId");
                const auto _ProductIDText = GetString(_Reference, "productEntityId");
                const auto _GenerationRunIDText = GetString(_Reference, "generationRunId");
                if (_ID.empty() || _ProductIDText.empty() || _GenerationRunIDText.empty()) continue;
                const auto _Product = GetComponent<CProductInstanceComponent>(
                    Scene_.Database().GetEntity(
                        ParseRequiredUuid(_ProductIDText, "productEntityId")));
                if (_Product && UuidToString(_Product->GetActiveGenerationRunID()) == _GenerationRunIDText)
                    _Result.insert(_ID);
            }
        }
        return _Result;
    }

    void ReleaseTransientParts(
        iCAX::Project::ISceneContext& Scene_,
        const std::set<std::string>& SelectedPartIDs_)
    {
        const auto _LinkedPartIDs = LinkedNestingPartIDs(Scene_);
        std::set<std::string> _ResourceIDs;
        {
            const std::lock_guard _Lock(gTransientDisassemblyMutex);
            const auto _Scene = gTransientDisassembly.find(Scene_.GetSceneID());
            if (_Scene == gTransientDisassembly.end()) return;
            for (auto _Product = _Scene->second.Products.begin();
                _Product != _Scene->second.Products.end();)
            {
                auto& _Parts = _Product->second.Parts;
                _Parts.erase(std::remove_if(_Parts.begin(), _Parts.end(), [&](const auto& _Part) {
                    const auto _ID = UuidToString(_Part.PartID);
                    const auto _Selected = SelectedPartIDs_.empty()
                        || SelectedPartIDs_.contains(_ID);
                    if (!_Selected || _LinkedPartIDs.contains(_ID)) return false;
                    if (!_Part.ManufacturingResource.URL.empty())
                        _ResourceIDs.insert(_Part.ManufacturingResource.URL);
                    if (!_Part.ThumbnailResource.URL.empty())
                        _ResourceIDs.insert(_Part.ThumbnailResource.URL);
                    return true;
                }), _Parts.end());
                if (_Parts.empty()) _Product = _Scene->second.Products.erase(_Product);
                else ++_Product;
            }
            if (_Scene->second.Products.empty()) gTransientDisassembly.erase(_Scene);
        }
        // Unload the complete dependency set first so no retained derived
        // resource can keep a source BRep resident while records are removed.
        for (const auto& _ResourceID : _ResourceIDs)
            (void)Scene_.Resources().Unload(_ResourceID);
        for (const auto& _ResourceID : _ResourceIDs)
            (void)Scene_.Resources().Delete(_ResourceID);
        {
            const std::lock_guard _Lock(gPartMeasurementCacheMutex);
            for (auto _Cached = gPartMeasurementCache.begin();
                _Cached != gPartMeasurementCache.end();)
            {
                const auto _Separator = _Cached->first.rfind('@');
                const auto _ResourceID = _Separator == std::string::npos
                    ? _Cached->first : _Cached->first.substr(0, _Separator);
                if (_ResourceIDs.contains(_ResourceID))
                    _Cached = gPartMeasurementCache.erase(_Cached);
                else ++_Cached;
            }
            for (auto _Cached = gPartUnfoldingCache.begin();
                _Cached != gPartUnfoldingCache.end();)
            {
                const auto _Separator = _Cached->first.rfind('@');
                const auto _ResourceID = _Separator == std::string::npos
                    ? _Cached->first : _Cached->first.substr(0, _Separator);
                if (_ResourceIDs.contains(_ResourceID))
                    _Cached = gPartUnfoldingCache.erase(_Cached);
                else ++_Cached;
            }
        }
    }

    ObjectMap MakeTransientPartSnapshot(
        const SPreparedManufacturingPart& Part_, const CProductInstanceComponent& Product_)
    {
        return ObjectMap{
            {"entityId", UuidToString(Part_.PartID)},
            {"sourceMemberId", UuidToString(Part_.MemberID)},
            {"index", Part_.Index}, {"partNumber", Part_.PartNumber},
            {"name", MakePartNameFromFileName(Part_.FileName, Part_.PartNumber)},
            {"role", Part_.Role}, {"quantity", ProductionQuantity(Part_.Quantity, Product_.GetQuantity())},
            {"unitQuantity", Part_.Quantity}, {"instanceQuantity", Product_.GetQuantity()},
            {"profile", Part_.TubeProfile}, {"partKind", ManufacturingPartKind(Part_.ItemProperties)},
            {"plate", ManufacturingPlate(Part_.ItemProperties)}, {"length", Part_.Length},
            {"stableKey", Part_.StableKey}, {"fileName", Part_.FileName},
            {"status", std::string("Ready")}, {"transient", true},
            {"properties", Part_.ItemProperties},
            {"manufacturingGeometryResourceId", Part_.ManufacturingResource.URL},
            {"manufacturingGeometryResourceVersion", Part_.ManufacturingResource.nVersion},
            {"thumbnailGeometryResourceId", Part_.ThumbnailResource.URL},
            {"thumbnailGeometryResourceVersion", Part_.ThumbnailResource.nVersion}
        };
    }

    ObjectMap MakeTransientGroupSnapshot(
        const SPreparedProductDisassembly& Prepared_, const CProductInstanceComponent& Product_)
    {
        VariantArray _Parts;
        _Parts.reserve(Prepared_.Parts.size());
        for (const auto& _Part : Prepared_.Parts)
            _Parts.emplace_back(MakeTransientPartSnapshot(_Part, Product_));
        return ObjectMap{
            {"productEntityId", UuidToString(Prepared_.ProductID)},
            {"generationRunId", UuidToString(Prepared_.GenerationRunID)},
            {"name", Product_.GetName()}, {"productCode", Product_.GetProductCode()},
            {"quantity", Product_.GetQuantity()}, {"templateId", Product_.GetTemplateID()},
            {"templateVersion", Product_.GetTemplateVersion()}, {"parameters", Product_.GetParameters()},
            {"parts", std::move(_Parts)}, {"transient", true}
        };
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

    void RestoreDesignerResources(iCAX::Project::ISceneContext& Scene_,
        const iCAX::Application::IApplicationContext& ApplicationContext_, bool Manufacturing_ = false);
    SPreparedProductDisassembly PrepareProductDisassembly(
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Project::ISceneContext& Scene_, const iCAX::Data::uuid& ProductID_);
    void RestoreLinkedNestingParts(iCAX::Project::ISceneContext& Scene_,
        const iCAX::Application::IApplicationContext& ApplicationContext_);
    void MigrateLegacyNestingTask(iCAX::Project::ISceneContext& Scene_,
        const iCAX::Application::IApplicationContext& Application_);
    void SyncNestingPersistence(iCAX::Project::ISceneContext& Scene_);

    bool IsIndependentNestingPart(const CManufacturingPartComponent& Part_)
    {
        return Part_.GetProductID().is_nil() && Part_.GetItemProperties().contains("nesting.snapshot");
    }

    void RemoveLegacyProductManufacturingParts(iCAX::Project::ISceneContext& Scene_)
    {
        auto& _DB = Scene_.Database();
        std::vector<iCAX::Data::uuid> _PartIDs;
        std::set<iCAX::Data::uuid> _GenerationRunIDs;
        for (const auto& [_Entity, _Part] : Collect<CManufacturingPartComponent>(_DB))
        {
            if (IsIndependentNestingPart(*_Part)) continue;
            _PartIDs.push_back(_Entity->GetID());
            if (!_Part->GetGenerationRunID().is_nil())
                _GenerationRunIDs.insert(_Part->GetGenerationRunID());
        }
        if (_PartIDs.empty()) return;

        auto& _Transaction = _DB.BeginTransaction("Migrate transient manufacturing results");
        bool _Committing = false;
        try
        {
            for (const auto& _PartID : _PartIDs)
            {
                for (const auto& [_MemberEntity, _Member] : Collect<CAssemblyMemberComponent>(_DB))
                    if (_Member->GetManufacturingPartID() == _PartID)
                        _Transaction.ModifyComponent(
                            _MemberEntity->GetID(), CAssemblyMemberComponent::S_ClassName,
                            {{CAssemblyMemberComponent::PropertyName_ManufacturingPartID,
                                PropertyValue(iCAX::Data::uuid())}});
                _Transaction.DisposeEntity(_PartID);
            }
            for (const auto& _RunID : _GenerationRunIDs)
                if (GetComponent<CGenerationRunComponent>(_DB.GetEntity(_RunID)))
                    _Transaction.ModifyComponent(
                        _RunID, CGenerationRunComponent::S_ClassName,
                        {{CGenerationRunComponent::PropertyName_ManufacturingModel,
                            PropertyValue(ObjectMap())}});
            std::string _Error;
            _Committing = true;
            if (!_DB.CommitTransaction(_Transaction, _Error))
                throw std::runtime_error(_Error.empty()
                    ? "清理旧版拆单结果失败" : _Error);
        }
        catch (...)
        {
            if (!_Committing) { try { _DB.CancelTransaction(_Transaction); } catch (...) {} }
            throw;
        }
    }

    void RestoreNestingResources(iCAX::Project::ISceneContext& Scene_)
    {
        for (const auto& [_Entity, _Part] : Collect<CManufacturingPartComponent>(Scene_.Database()))
            if (IsIndependentNestingPart(*_Part))
            {
                if (!Scene_.Resources().Get<iCAX::GeometryData::BRepModel>(
                    _Part->GetManufacturingGeometryResourceID(), _Part->GetManufacturingGeometryResourceVersion()))
                    throw std::runtime_error("下料快照的独立几何资源缺失");
                (void)iCAX::RenderInteraction::EnsureFrontendGeometryResource(Scene_.Resources(),
                    _Part->GetManufacturingGeometryResourceID(), iCAX::Render::ERenderGeometryKind::Mesh);
            }
    }

    ObjectMap BuildSnapshot(
        iCAX::Project::ISceneContext& Scene_,
        const iCAX::Application::IApplicationContext& ApplicationContext_, bool RestoreSources_ = true)
    {
        if (RestoreSources_) RestoreDesignerResources(Scene_, ApplicationContext_);
        else RestoreNestingResources(Scene_);
        MigrateLegacyNestingTask(Scene_, ApplicationContext_);
        RemoveLegacyProductManufacturingParts(Scene_);
        RestoreLinkedNestingParts(Scene_, ApplicationContext_);
        SyncNestingPersistence(Scene_);
        auto& _Repository = Scene_.Database();
        const auto _TransientDisassembly = CopyTransientDisassembly(Scene_);
        const auto _FindTransientProduct = [&](const iCAX::Data::uuid& ProductID_,
            const iCAX::Data::uuid& GenerationRunID_) -> const SPreparedProductDisassembly* {
            for (const auto& _Prepared : _TransientDisassembly)
                if (_Prepared.ProductID == ProductID_
                    && (GenerationRunID_.is_nil() || _Prepared.GenerationRunID == GenerationRunID_))
                    return &_Prepared;
            return nullptr;
        };
        ObjectMap _Designer;

        _Designer["templates"] = TemplateCatalog(ApplicationContext_);

        const auto _Meta = _Repository.GetMetaEntity();
        const auto _Root = GetComponent<CTubeDesignerRootComponent>(_Meta);
        const auto _ActiveProductID = _Root
            ? _Root->GetActiveProductID()
            : iCAX::Data::uuid();
        _Designer["activeProductId"] = UuidToString(_ActiveProductID);
        _Designer["nestingSettings"] = _Root ? _Root->GetNestingSettings() : ObjectMap();
        _Designer["nestingTask"] = _Root ? _Root->GetNestingTask() : ObjectMap();
        _Designer["machiningTask"] = _Root ? _Root->GetMachiningTask() : ObjectMap();

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
            if (const auto* _Transient = _FindTransientProduct(_ProductID, _GenerationRunID))
                _PartCount = static_cast<std::uint64_t>(_Transient->Parts.size());
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
            _Instance["quantity"] = _Product->GetQuantity();
            _Instance["createdAt"] = _Product->GetCreatedAt();
            _Instance["templateId"] = _Product->GetTemplateID();
            _Instance["templateVersion"] = _Product->GetTemplateVersion();
            _Instance["status"] = _Product->GetStatus();
            _Instance["parameters"] = _Product->GetParameters();
            _Instance["sketches"] = _Product->GetSketches();
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
            _Product["quantity"] = _ActiveProduct->GetQuantity();
            _Product["createdAt"] = _ActiveProduct->GetCreatedAt();
            _Product["templateId"] = _ActiveProduct->GetTemplateID();
            _Product["templateVersion"] = _ActiveProduct->GetTemplateVersion();
            _Product["status"] = _ActiveProduct->GetStatus();
            _Product["parameters"] = _ActiveProduct->GetParameters();
            _Product["sketches"] = _ActiveProduct->GetSketches();
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
            _Item["profile"] = OptionalProfileProperties(
                GetComponent<CTubeProfileComponent>(_Entity).get());
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
            _Item["quantity"] = EffectiveManufacturingQuantity(*_Part, _ActiveProduct->GetQuantity());
            _Item["unitQuantity"] = _Part->GetQuantity();
            _Item["instanceQuantity"] = _ActiveProduct->GetQuantity();
            _Item["profile"] = _Presentation.Profile;
            _Item["partKind"] = ManufacturingPartKind(_Part->GetItemProperties());
            _Item["plate"] = ManufacturingPlate(_Part->GetItemProperties());
            _Item["length"] = _Part->GetLength();
            _Item["stableKey"] = _Part->GetStableKey();
            _Item["manufacturingGeometryResourceId"] = _Part->GetManufacturingGeometryResourceID();
            _Item["manufacturingGeometryResourceVersion"] = _Part->GetManufacturingGeometryResourceVersion();
            _Item["thumbnailGeometryResourceId"] = _Part->GetThumbnailGeometryResourceID();
            _Item["thumbnailGeometryResourceVersion"] = _Part->GetThumbnailGeometryResourceVersion();
            _Item["fileName"] = _Part->GetFileName();
            _Item["status"] = _Part->GetStatus();
            _Item["properties"] = PartPropertiesSnapshot(*_Part);
            _Parts.emplace_back(_Item);
        }
        if (_ActiveProduct)
            if (const auto* _Transient = _FindTransientProduct(
                    _ActiveProductID, _ActiveProduct->GetActiveGenerationRunID()))
                for (const auto& _Prepared : _Transient->Parts)
                    _Parts.emplace_back(MakeTransientPartSnapshot(_Prepared, *_ActiveProduct));
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
            // Keep a group visible when the user deliberately removes one of
            // its manufacturing parts.  The instance-level hasDisassembly
            // flag still records that the generated set is incomplete, while
            // the remaining editable rows stay available in the parts list.
            if (_ProductParts.empty()) continue;
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
                _Item["quantity"] = EffectiveManufacturingQuantity(*_Part, _Product->GetQuantity());
                _Item["unitQuantity"] = _Part->GetQuantity();
                _Item["instanceQuantity"] = _Product->GetQuantity();
                _Item["profile"] = _Presentation.Profile;
                _Item["partKind"] = ManufacturingPartKind(_Part->GetItemProperties());
                _Item["plate"] = ManufacturingPlate(_Part->GetItemProperties());
                _Item["length"] = _Part->GetLength();
                _Item["stableKey"] = _Part->GetStableKey();
                _Item["manufacturingGeometryResourceId"] = _Part->GetManufacturingGeometryResourceID();
                _Item["manufacturingGeometryResourceVersion"] = _Part->GetManufacturingGeometryResourceVersion();
                _Item["thumbnailGeometryResourceId"] = _Part->GetThumbnailGeometryResourceID();
                _Item["thumbnailGeometryResourceVersion"] = _Part->GetThumbnailGeometryResourceVersion();
                _Item["fileName"] = _Part->GetFileName();
                _Item["status"] = _Part->GetStatus();
                _Item["properties"] = PartPropertiesSnapshot(*_Part);
                _GroupParts.emplace_back(_Item);
            }
            ObjectMap _Group;
            _Group["productEntityId"] = UuidToString(_ProductID);
            _Group["generationRunId"] = UuidToString(_GenerationRunID);
            _Group["name"] = _Product->GetName();
            _Group["quantity"] = _Product->GetQuantity();
            _Group["productCode"] = _Product->GetProductCode();
            _Group["templateId"] = _Product->GetTemplateID();
            _Group["templateVersion"] = _Product->GetTemplateVersion();
            _Group["parameters"] = _Product->GetParameters();
            _Group["parts"] = _GroupParts;
            _ManufacturingGroups.emplace_back(_Group);
        }
        for (const auto& _Prepared : _TransientDisassembly)
        {
            for (const auto& [_ProductEntity, _Product] : _Products)
            {
                if (_ProductEntity->GetID() != _Prepared.ProductID
                    || _Product->GetActiveGenerationRunID() != _Prepared.GenerationRunID)
                    continue;
                _ManufacturingGroups.emplace_back(
                    MakeTransientGroupSnapshot(_Prepared, *_Product));
                break;
            }
        }
        _Designer["manufacturingGroups"] = _ManufacturingGroups;

        // These entities own their geometry and production counts. Product IDs
        // and generation recipes are deliberately not required to display them.
        std::map<std::string, ObjectMap> _NestingGroups;
        for (const auto& [_Entity, _Part] : Collect<CManufacturingPartComponent>(_Repository))
        {
            if (!IsIndependentNestingPart(*_Part)) continue;
            const auto _Snapshot = _Part->GetItemProperties().at("nesting.snapshot").To<ObjectMap>();
            const auto _GroupID = UuidToString(_Part->GetGenerationRunID());
            auto& _Group = _NestingGroups[_GroupID];
            if (_Group.empty())
            {
                _Group = _Snapshot;
                _Group["productEntityId"] = _GroupID; // Independent batch identity, not a product reference.
                _Group["generationRunId"] = _GroupID;
                _Group["parts"] = VariantArray();
            }
            const auto _Presentation = ResolvePartPresentation(_Repository, *_Part);
            ObjectMap _Item{
                {"entityId", UuidToString(_Entity->GetID())}, {"index", _Part->GetPartIndex()},
                {"partNumber", _Part->GetPartNumber()}, {"name", _Presentation.Name},
                {"quantity", _Part->GetQuantity()}, {"instanceQuantity", 1ull},
                {"profile", _Presentation.Profile}, {"partKind", ManufacturingPartKind(_Part->GetItemProperties())},
                {"length", _Part->GetLength()}, {"role", _Part->GetRole()}, {"fileName", _Part->GetFileName()},
                {"properties", PartPropertiesSnapshot(*_Part)}, {"status", _Part->GetStatus()},
                {"manufacturingGeometryResourceId", _Part->GetManufacturingGeometryResourceID()},
                {"manufacturingGeometryResourceVersion", _Part->GetManufacturingGeometryResourceVersion()},
                {"thumbnailGeometryResourceId", _Part->GetThumbnailGeometryResourceID()},
                {"thumbnailGeometryResourceVersion", _Part->GetThumbnailGeometryResourceVersion()}
            };
            auto _GroupParts = _Group.at("parts").To<VariantArray>();
            _GroupParts.emplace_back(std::move(_Item));
            _Group["parts"] = std::move(_GroupParts);
        }
        if (_Root)
        {
            const auto _Task = _Root->GetNestingTask();
            const auto _References = _Task.find("parts");
            std::map<std::string, std::map<std::string, std::string>> _LinkedPartIDs;
            if (_References != _Task.end() && _References->second.Is<VariantArray>())
            {
                for (const auto& _Value : _References->second.To<VariantArray>())
                {
                    if (!_Value.Is<ObjectMap>()) continue;
                    const auto _Reference = _Value.To<ObjectMap>();
                    if (!IsProductNestingReference(_Reference)) continue;
                    const auto _ProductID = GetString(_Reference, "productEntityId");
                    const auto _PartID = GetString(_Reference, "partEntityId");
                    const auto _GenerationRunID = GetString(_Reference, "generationRunId");
                    if (!_ProductID.empty() && !_PartID.empty() && !_GenerationRunID.empty())
                        _LinkedPartIDs[_ProductID][_PartID] = _GenerationRunID;
                }
            }
            for (const auto& _Prepared : _TransientDisassembly)
            {
                const auto _ProductID = UuidToString(_Prepared.ProductID);
                const auto _Linked = _LinkedPartIDs.find(_ProductID);
                if (_Linked == _LinkedPartIDs.end()) continue;
                for (const auto& [_ProductEntity, _Product] : _Products)
                {
                    if (_ProductEntity->GetID() != _Prepared.ProductID
                        || _Product->GetActiveGenerationRunID() != _Prepared.GenerationRunID)
                        continue;
                    auto _Group = MakeTransientGroupSnapshot(_Prepared, *_Product);
                    VariantArray _Parts;
                    for (const auto& _Part : _Prepared.Parts)
                    {
                        const auto _PartReference = _Linked->second.find(UuidToString(_Part.PartID));
                        if (_PartReference == _Linked->second.end()
                            || _PartReference->second != UuidToString(_Prepared.GenerationRunID))
                            continue;
                        auto _Item = MakeTransientPartSnapshot(_Part, *_Product);
                        _Item["linkedNesting"] = true;
                        _Parts.emplace_back(std::move(_Item));
                    }
                    if (_Parts.empty()) break;
                    _Group["parts"] = std::move(_Parts);
                    _Group["linkedNesting"] = true;
                    _NestingGroups["product:" + _ProductID] = std::move(_Group);
                    break;
                }
            }
        }
        VariantArray _IndependentGroups;
        for (auto& [_ID, _Group] : _NestingGroups)
        {
            auto _GroupParts = _Group.at("parts").To<VariantArray>();
            std::sort(_GroupParts.begin(), _GroupParts.end(), [](const auto& A_, const auto& B_) {
                return GetUInt64(A_.To<ObjectMap>(), "index", 0) < GetUInt64(B_.To<ObjectMap>(), "index", 0);
            });
            _Group["parts"] = std::move(_GroupParts);
            _IndependentGroups.emplace_back(std::move(_Group));
        }
        _Designer["nestingGroups"] = std::move(_IndependentGroups);

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
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_) throw std::invalid_argument("TubeDesigner.List requires a scene");
        const auto _Payload = DecodeObjectPayload(Request_);
        if (const auto _It = _Payload.find("nestingOnly"); _It != _Payload.end() && _It->second.Is<bool>() && _It->second.To<bool>())
            return MakeResponse(Variant(BuildSnapshot(*Scene_, ApplicationContext_, false)));
        if (const auto _It = _Payload.find("includeManufacturingGeometry"); _It != _Payload.end() && _It->second.Is<bool>() && _It->second.To<bool>())
            RestoreDesignerResources(*Scene_, ApplicationContext_, true);
        return MakeResponse(Variant(BuildSnapshot(*Scene_, ApplicationContext_)));
    }

    iCAX::Interaction::CInvocationResult HandleGetTemplateDescriptor(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext*)
    {
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _TemplateID = GetRequiredText(_Payload, "templateId", 240);
        const auto _Package = LoadPythonTemplatePackage(ApplicationContext_, _TemplateID);
        auto _Presentation = iCAX::TemplateRuntime::CTemplateCodec::MakePresentationDescriptor(
            _Package.Descriptor, "zh-CN");
        _Presentation["packageDigest"] = _Package.Descriptor.PackageDigest;
        _Presentation["available"] = true;
        _Presentation["descriptorLoaded"] = true;
        AttachTemplateCatalogAssets(_Presentation, _Package.DescriptorPath.parent_path());
        return MakeResponse(ObjectMap{{ "template", std::move(_Presentation) }});
    }

    constexpr const char* kCustomerFeatureID = "customer";
    constexpr const char* kCustomerRecordType = "profile";
    constexpr const char* kTemplateFeatureID = "template";
    constexpr const char* kParameterPresetRecordType = "parameter-preset";
    constexpr const char* kProductTemplateFeatureID = "product-template";
    constexpr const char* kProductTemplateRecordType = "product-template";
    constexpr const char* kProfileFeatureID = "profile";
    constexpr const char* kImportedProfileRecordType = "imported-dxf";
    constexpr const char* kParametricProfileRecordType = "parametric-package";
    constexpr const char* kPunchToolFeatureID = "punch-tool";
    constexpr const char* kFixedPunchToolRecordType = "fixed-geometry";
    constexpr const char* kParametricPunchToolRecordType = "parametric-package";
    constexpr const char* kProductSubjectType = "product";
    constexpr const char* kProfileDefinitionSubjectType = "profile-definition";
    constexpr const char* kTemplateSubjectType = "template-definition";
    constexpr const char* kProductTemplateSubjectType = "product-template-definition";
    constexpr const char* kCustomerRelationType = "customer";

    struct SProfileReference final
    {
        std::string Scope;
        std::string ID;
        std::string TemplateID;
    };

    bool IsSystemProfileID(const std::string& Value_)
    {
        if (Value_.empty() || Value_.size() > 80
            || Value_.front() < 'a' || Value_.front() > 'z')
        {
            return false;
        }
        return std::all_of(Value_.begin() + 1, Value_.end(), [](const unsigned char Character_)
        {
            return (Character_ >= 'a' && Character_ <= 'z')
                || (Character_ >= '0' && Character_ <= '9')
                || Character_ == '_' || Character_ == '-';
        });
    }

    SProfileReference ParseProfileReference(const ObjectMap& Request_)
    {
        const auto _Reference = GetRequiredObject(Request_, "profileRef");
        const auto _Scope = GetRequiredText(_Reference, "scope", 16);
        const auto _ID = GetRequiredText(_Reference, "id", 240);
        if (_Scope == "user")
        {
            return { _Scope, UuidToString(ParseRequiredUuid(_ID, "profileRef.id")) };
        }
        if (_Scope == "system")
        {
            if (!IsSystemProfileID(_ID))
                throw std::invalid_argument("TubeDesigner system profile ID is invalid");
            return { _Scope, _ID };
        }
        if (_Scope == "template")
        {
            if (!IsSystemProfileID(_ID)) throw std::invalid_argument("模板管型资源 ID 无效");
            return { _Scope, _ID, GetRequiredText(_Reference, "templateId", 240) };
        }
        throw std::invalid_argument("TubeDesigner profileRef.scope must be system, template or user");
    }

    ObjectMap ProfileReferencePayload(const SProfileReference& Reference_)
    {
        ObjectMap _Result{ { "scope", Reference_.Scope }, { "id", Reference_.ID } };
        if (Reference_.Scope == "template") _Result["templateId"] = Reference_.TemplateID;
        return _Result;
    }

    std::string ProfileResourceIdentity(const SProfileReference& Reference_)
    {
        return Reference_.Scope + "/" + (Reference_.Scope == "template" ? Reference_.TemplateID + "/" : "") + Reference_.ID;
    }

    TopoDS_Shape BuildProfileExtrusion(
        const ObjectMap& Profile_,
        const double Length_)
    {
        if (!std::isfinite(Length_) || Length_ <= 1.0e-6)
            throw std::invalid_argument("tube profile extrusion length is invalid");
        const auto _Kind = GetString(Profile_, "kind");
        const auto _Contours = Profile_.find("contours");
        if (_Contours == Profile_.end() || !_Contours->second.Is<VariantArray>())
            throw std::invalid_argument("imported tube profile requires contours");
        const auto _ContourValues = _Contours->second.To<VariantArray>();
        const VariantArray _Origin{ Variant(0.0), Variant(0.0), Variant(0.0) };
        const VariantArray _XAxis{ Variant(1.0), Variant(0.0), Variant(0.0) };
        const VariantArray _YAxis{ Variant(0.0), Variant(1.0), Variant(0.0) };
        const VariantArray _Vector{ Variant(0.0), Variant(0.0), Variant(Length_) };
        const ObjectMap _Document{
            { "schema", std::string("icax.neutral-model") },
            { "schemaVersion", 1ull },
            { "template", ObjectMap{
                { "id", std::string("icax.tube-profile-extrusion") },
                { "version", std::string("1.0.0") },
                { "packageDigest", GetString(Profile_, "contentDigest", _Kind) }
            } },
            { "geometry", VariantArray{
                ObjectMap{
                    { "key", std::string("profile") },
                    { "operator", std::string("profile2d") },
                    { "arguments", ObjectMap{
                        { "placement", ObjectMap{
                            { "origin", _Origin }, { "xAxis", _XAxis }, { "yAxis", _YAxis }
                        } },
                        { "contours", _ContourValues }
                    } }
                },
                ObjectMap{
                    { "key", std::string("solid") },
                    { "operator", std::string("extrude") },
                    { "inputs", VariantArray{ Variant(std::string("profile")) } },
                    { "arguments", ObjectMap{ { "vector", _Vector } } }
                }
            } }
        };
        const auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(
            Variant(_Document));
        return iCAX::OpenCascade::EvaluateNeutralModel(_Model, { "solid" }).At("solid");
    }

    void ValidateImportedProfileDefinition(const ObjectMap& Profile_)
    {
        if (GetString(Profile_, "schema") != "icax.imported-tube-profile"
            || GetUInt64(Profile_, "schemaVersion", 0) != 1)
        {
            throw std::invalid_argument("unsupported imported tube profile schema");
        }
        const auto _Kind = GetString(Profile_, "kind");
        if (_Kind != "imported-dxf" && _Kind != "parametric-package")
            throw std::invalid_argument("imported tube profile kind is not supported");
        const auto _Width = GetDouble(Profile_, "width", 0.0);
        const auto _Depth = GetDouble(Profile_, "depth", 0.0);
        if (!std::isfinite(_Width) || !std::isfinite(_Depth)
            || _Width <= 1.0e-6 || _Depth <= 1.0e-6)
        {
            throw std::invalid_argument("imported tube profile bounds are invalid");
        }
        const auto _Contours = Profile_.find("contours");
        if (_Contours == Profile_.end() || !_Contours->second.Is<VariantArray>())
            throw std::invalid_argument("imported tube profile requires contours");
        const auto _ContourValues = _Contours->second.To<VariantArray>();
        if (_ContourValues.empty() || _ContourValues.size() > 1000)
            throw std::invalid_argument("imported tube profile contour count is invalid");

        if (BuildProfileExtrusion(Profile_, 1.0).IsNull())
            throw std::runtime_error("tube profile validation produced no solid");
    }

    ObjectMap ImportDxfProfile(
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        const std::string& SourcePath_)
    {
        const auto _ScriptPath = ResolveRuntimeFile(ApplicationContext_, {
            "apps/tube-designer/templates/_shared/dxf_profile_importer.py",
            "src/apps/tube-designer/templates/_shared/dxf_profile_importer.py"
        }, "TubeDesigner DXF profile importer");
        const auto _ScriptText = ReadTextFile(_ScriptPath);
        ObjectMap _Request;
        _Request["protocol"] = std::string("icax.template-runtime");
        _Request["protocolVersion"] = 1ull;
        _Request["operation"] = std::string("evaluate");
        _Request["templatePath"] = PathToUTF8(_ScriptPath);
        _Request["template"] = ObjectMap{
            { "id", std::string("icax.dxf-profile-importer") },
            { "version", std::string("1.0.0") },
            { "packageDigest", ContentDigest("dxf-profile-importer", _ScriptText) }
        };
        _Request["parameters"] = ObjectMap{ { "sourcePath", SourcePath_ } };
        _Request["context"] = ObjectMap{
            { "coordinateSystem", std::string("right-handed-x-width-y-depth") },
            { "lengthUnit", std::string("mm") }
        };
        const auto _Result = InvokePythonTemplate(ApplicationContext_, _Request);
        auto _Profile = GetRequiredObject(_Result, "profile");
        ValidateImportedProfileDefinition(_Profile);
        return _Profile;
    }

    ObjectMap InvokeProfilePackageRuntime(
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        const ObjectMap& Parameters_)
    {
        const auto _ScriptPath = ResolveRuntimeFile(ApplicationContext_, {
            "apps/tube-designer/templates/_shared/profile_package_runtime.py",
            "src/apps/tube-designer/templates/_shared/profile_package_runtime.py"
        }, "TubeDesigner profile package runtime");
        const auto _ScriptText = ReadTextFile(_ScriptPath);
        ObjectMap _Request;
        _Request["protocol"] = std::string("icax.template-runtime");
        _Request["protocolVersion"] = 1ull;
        _Request["operation"] = std::string("evaluate");
        _Request["templatePath"] = PathToUTF8(_ScriptPath);
        _Request["template"] = ObjectMap{
            { "id", std::string("icax.profile-package-runtime") },
            { "version", std::string("1.0.0") },
            { "packageDigest", ContentDigest("profile-package-runtime", _ScriptText) }
        };
        _Request["parameters"] = Parameters_;
        _Request["context"] = ObjectMap{
            { "coordinateSystem", std::string("right-handed-x-width-y-depth") },
            { "lengthUnit", std::string("mm") }
        };
        return InvokePythonTemplate(ApplicationContext_, _Request);
    }

    ObjectMap InvokeProductTemplatePackageRuntime(
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        const ObjectMap& Parameters_)
    {
        const auto _ScriptPath = ResolveRuntimeFile(ApplicationContext_, {
            "apps/tube-designer/templates/_shared/product_template_package_runtime.py",
            "src/apps/tube-designer/templates/_shared/product_template_package_runtime.py"
        }, "TubeDesigner product template package runtime");
        const auto _ScriptText = ReadTextFile(_ScriptPath);
        ObjectMap _Request;
        _Request["protocol"] = std::string("icax.template-runtime");
        _Request["protocolVersion"] = 1ull;
        _Request["operation"] = std::string("evaluate");
        _Request["templatePath"] = PathToUTF8(_ScriptPath);
        _Request["template"] = ObjectMap{
            { "id", std::string("icax.product-template-package-runtime") },
            { "version", std::string("1.0.0") },
            { "packageDigest", ContentDigest("product-template-package-runtime", _ScriptText) }
        };
        _Request["parameters"] = Parameters_;
        _Request["context"] = ObjectMap{
            { "coordinateSystem", std::string("right-handed-x-width-y-depth") },
            { "lengthUnit", std::string("mm") }
        };
        return InvokePythonTemplate(ApplicationContext_, _Request);
    }

    ObjectMap InvokePunchToolPackageRuntime(
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        const ObjectMap& Parameters_)
    {
        const auto _ScriptPath = ResolveRuntimeFile(ApplicationContext_, {
            "apps/tube-designer/templates/_shared/punch_tool_package_runtime.py",
            "src/apps/tube-designer/templates/_shared/punch_tool_package_runtime.py"
        }, "TubeDesigner punch tool package runtime");
        const auto _ScriptText = ReadTextFile(_ScriptPath);
        return InvokePythonTemplate(ApplicationContext_, ObjectMap{
            { "protocol", std::string("icax.template-runtime") },
            { "protocolVersion", 1ull }, { "operation", std::string("evaluate") },
            { "templatePath", PathToUTF8(_ScriptPath) },
            { "template", ObjectMap{
                { "id", std::string("icax.punch-tool-package-runtime") },
                { "version", std::string("1.0.0") },
                { "packageDigest", ContentDigest("punch-tool-package-runtime", _ScriptText) }
            } },
            { "parameters", Parameters_ },
            { "context", ObjectMap{{ "lengthUnit", std::string("mm") }} }
        });
    }

    ObjectMap ExportProfileDxf(
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        const ObjectMap& Profile_,
        const std::string& TargetPath_)
    {
        const auto _ScriptPath = ResolveRuntimeFile(ApplicationContext_, {
            "apps/tube-designer/templates/_shared/profile_export_runtime.py",
            "src/apps/tube-designer/templates/_shared/profile_export_runtime.py"
        }, "TubeDesigner profile DXF exporter");
        const auto _ScriptText = ReadTextFile(_ScriptPath);
        ObjectMap _Request;
        _Request["protocol"] = std::string("icax.template-runtime");
        _Request["protocolVersion"] = 1ull;
        _Request["operation"] = std::string("evaluate");
        _Request["templatePath"] = PathToUTF8(_ScriptPath);
        _Request["template"] = ObjectMap{
            { "id", std::string("icax.profile-export-runtime") },
            { "version", std::string("1.0.0") },
            { "packageDigest", ContentDigest("profile-export-runtime", _ScriptText) }
        };
        _Request["parameters"] = ObjectMap{
            { "profile", Profile_ }, { "targetPath", TargetPath_ }
        };
        _Request["context"] = ObjectMap{
            { "coordinateSystem", std::string("right-handed-x-width-y-depth") },
            { "lengthUnit", std::string("mm") }
        };
        return InvokePythonTemplate(ApplicationContext_, _Request);
    }

    void ValidateProfilePackageRecord(const ObjectMap& Package_)
    {
        if (GetString(Package_, "schema") != "icax.tube-profile-package-record"
            || GetUInt64(Package_, "schemaVersion", 0) != 1
            || GetString(Package_, "kind") != "parametric-package")
        {
            throw std::invalid_argument("unsupported tube profile package schema");
        }
        const auto _Descriptor = GetRequiredObject(Package_, "descriptor");
        if (GetString(_Descriptor, "schema") != "icax.tube-profile-descriptor"
            || GetUInt64(_Descriptor, "schemaVersion", 0) != 2
            || GetString(_Descriptor, "id").empty()
            || GetString(_Descriptor, "version").empty())
        {
            throw std::invalid_argument("tube profile package descriptor is invalid");
        }
        if (GetString(Package_, "scriptSource").empty())
            throw std::invalid_argument("tube profile package script is empty");
        (void)GetRequiredObject(Package_, "defaultParameters");
        const auto _Preview = GetRequiredObject(Package_, "previewProfile");
        ValidateImportedProfileDefinition(_Preview);
    }

    ObjectMap ImportProfilePackage(
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        const std::string& SourcePath_,
        const std::string& Password_)
    {
        ObjectMap _Parameters;
        _Parameters["action"] = std::string("inspect");
        _Parameters["sourcePath"] = SourcePath_;
        _Parameters["password"] = Password_;
        const auto _Result = InvokeProfilePackageRuntime(ApplicationContext_, _Parameters);
        auto _Package = GetRequiredObject(_Result, "package");
        ValidateProfilePackageRecord(_Package);
        return _Package;
    }

    ObjectMap EvaluateProfilePackage(
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        const ObjectMap& Package_,
        const ObjectMap& Values_)
    {
        ValidateProfilePackageRecord(Package_);
        ObjectMap _Parameters;
        _Parameters["action"] = std::string("evaluate");
        _Parameters["descriptor"] = GetRequiredObject(Package_, "descriptor");
        _Parameters["scriptSource"] = GetString(Package_, "scriptSource");
        _Parameters["packageDigest"] = GetString(Package_, "packageDigest");
        _Parameters["sourceFileName"] = GetString(Package_, "sourceFileName");
        _Parameters["values"] = Values_;
        const auto _Result = InvokeProfilePackageRuntime(ApplicationContext_, _Parameters);
        auto _Profile = GetRequiredObject(_Result, "profile");
        ValidateImportedProfileDefinition(_Profile);
        return _Profile;
    }

    std::vector<ObjectMap> LoadSystemProfilePackages(
        const iCAX::Application::IApplicationContext& ApplicationContext_)
    {
        ObjectMap _Parameters;
        _Parameters["action"] = std::string("list-system");
        _Parameters["profileRoot"] = PathToUTF8(
            ResolveSystemProfileRoot(ApplicationContext_));
        const auto _Result = InvokeProfilePackageRuntime(ApplicationContext_, _Parameters);
        const auto _Packages = _Result.find("systemProfiles");
        if (_Packages == _Result.end() || !_Packages->second.Is<VariantArray>())
            throw std::runtime_error("TubeDesigner system profile catalog returned no packages");

        std::vector<ObjectMap> _ResultPackages;
        for (const auto& _Value : _Packages->second.To<VariantArray>())
        {
            if (!_Value.Is<ObjectMap>())
                throw std::runtime_error("TubeDesigner system profile catalog contains an invalid package");
            auto _Package = _Value.To<ObjectMap>();
            ValidateProfilePackageRecord(_Package);
            _ResultPackages.emplace_back(std::move(_Package));
        }
        return _ResultPackages;
    }

    std::vector<ObjectMap> LoadUserProfilePackages(
        const iCAX::Application::IApplicationContext& ApplicationContext_)
    {
        const auto _Root = ResolveUserProfileRoot(ApplicationContext_);
        if (_Root.empty() || !std::filesystem::is_directory(_Root)) return {};
        const auto _Result = InvokeProfilePackageRuntime(ApplicationContext_, ObjectMap{
            { "action", std::string("list-user") },
            { "profileRoot", PathToUTF8(_Root) }
        });
        const auto _Packages = _Result.find("userProfiles");
        if (_Packages == _Result.end() || !_Packages->second.Is<VariantArray>())
            throw std::runtime_error("TubeDesigner user profile catalog returned no packages");
        std::vector<ObjectMap> _ResultPackages;
        for (const auto& _Value : _Packages->second.To<VariantArray>())
        {
            if (!_Value.Is<ObjectMap>())
                throw std::runtime_error("TubeDesigner user profile catalog contains an invalid package");
            auto _Package = _Value.To<ObjectMap>();
            ValidateProfilePackageRecord(_Package);
            _ResultPackages.emplace_back(std::move(_Package));
        }
        return _ResultPackages;
    }

    ObjectMap EvaluateSystemProfile(
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        const std::string& ProfileID_,
        const ObjectMap& Values_)
    {
        ObjectMap _Parameters;
        _Parameters["action"] = std::string("evaluate-system");
        _Parameters["profileRoot"] = PathToUTF8(
            ResolveSystemProfileRoot(ApplicationContext_));
        _Parameters["systemProfileId"] = ProfileID_;
        _Parameters["values"] = Values_;
        const auto _Result = InvokeProfilePackageRuntime(ApplicationContext_, _Parameters);
        auto _Profile = GetRequiredObject(_Result, "profile");
        ValidateImportedProfileDefinition(_Profile);
        return _Profile;
    }

    ObjectMap EvaluateUserProfile(
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        const std::string& ProfileID_, const ObjectMap& Values_)
    {
        const auto _Root = ResolveUserProfileRoot(ApplicationContext_);
        if (_Root.empty() || !std::filesystem::is_directory(_Root))
            throw std::invalid_argument("用户管型不存在：" + ProfileID_);
        const auto _Result = InvokeProfilePackageRuntime(ApplicationContext_, ObjectMap{
            { "action", std::string("evaluate-user") },
            { "profileRoot", PathToUTF8(_Root) },
            { "userProfileId", ProfileID_ },
            { "values", Values_ }
        });
        auto _Profile = GetRequiredObject(_Result, "profile");
        ValidateImportedProfileDefinition(_Profile);
        return _Profile;
    }

    std::shared_ptr<iCAX::Application::IProductUserDataStore> GetUserDataStore(
        iCAX::Product::IProductContext* ProductContext_)
    {
        if (!ProductContext_)
            throw std::runtime_error("TubeDesigner user data requires a product context");
        auto _Store = ProductContext_->GetUserDataStore();
        if (!_Store) throw std::runtime_error("TubeDesigner user data store is not available");
        if (_Store->GetProductID() != ProductContext_->GetProductID())
            throw std::logic_error("Product user data store identity mismatch");
        return _Store;
    }

    std::uint64_t ComponentExpectedRevision(const ObjectMap& Request_)
    {
        const auto _Revision = GetDouble(Request_, "expectedRevision", -1);
        if (!std::isfinite(_Revision) || _Revision < 1 || _Revision > 9007199254740991.0
            || std::floor(_Revision) != _Revision)
            throw std::invalid_argument("配件版本无效，无法完成操作");
        return static_cast<std::uint64_t>(_Revision);
    }

    iCAX::Interaction::CInvocationResult HandleListComponentModels(
        const iCAX::Interaction::CInvocation&,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext*)
    {
        auto _Store = GetUserDataStore(ProductContext_);
        auto _Models = ListComponentModelSummaries(ResolveComponentModelRoot(ApplicationContext_), _Store.get(),
            ResolveUserComponentModelRoot(ApplicationContext_));
        const auto _TemplateRoot = ResolveTemplateRoot(ApplicationContext_);
        for (const auto& _Directory : DiscoverPythonTemplateDirectories(ApplicationContext_))
        {
            const auto _Package = LoadPythonTemplatePackageFromDirectory(_Directory, _TemplateRoot);
            auto _TemplateModels = ListTemplateComponentModelSummaries(_Directory, _Package.Descriptor.Extensions,
                _Package.Descriptor.ID, _Package.Descriptor.DisplayName.Resolve());
            _Models.insert(_Models.end(), _TemplateModels.begin(), _TemplateModels.end());
        }
        return MakeResponse(ObjectMap{ { "models", std::move(_Models) } });
    }

    iCAX::Interaction::CInvocationResult HandleImportComponentModel(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext*)
    {
        tube::license::Enforce<110, tube::license::Feature::Design>();
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _Path = Utf8Path(GetRequiredText(_Payload, "sourcePath", 32767));
        auto _Store = GetUserDataStore(ProductContext_);
        auto _Snapshot = ImportComponentModelFile(_Path, _Payload);
        iCAX::Application::CProductUserDataRecord _Record;
        _Record.FeatureID = kComponentModelFeature;
        _Record.RecordType = kComponentModelRecordType;
        _Record.SubjectType = "product";
        _Record.SubjectID = ProductContext_->GetProductID();
        _Record.RecordID = UuidToString(iCAX::Data::GenerateNewUUID());
        _Record.Payload = _Snapshot;
        const auto _Saved = _Store->Put(_Record, 0);
        return MakeResponse(ObjectMap{ { "model", ComponentModelSummary(_Snapshot,
            "user", _Saved.RecordID, _Saved.Revision) } });
    }

    iCAX::Interaction::CInvocationResult HandleCreateComponentCSGModel(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext*)
    {
        tube::license::Enforce<113, tube::license::Feature::Design>();
        const auto _Payload = DecodeObjectPayload(Request_);
        auto _Snapshot = CreateComponentCSGModelSnapshot(
            GetRequiredObject(_Payload, "csgDefinition"), _Payload);
        iCAX::Application::CProductUserDataRecord _Record;
        _Record.FeatureID = kComponentModelFeature;
        _Record.RecordType = kComponentModelRecordType;
        _Record.SubjectType = "product";
        _Record.SubjectID = ProductContext_->GetProductID();
        _Record.RecordID = UuidToString(iCAX::Data::GenerateNewUUID());
        _Record.Payload = _Snapshot;
        const auto _Saved = GetUserDataStore(ProductContext_)->Put(_Record, 0);
        return MakeResponse(ObjectMap{ { "model", ComponentModelSummary(_Snapshot,
            "user", _Saved.RecordID, _Saved.Revision) } });
    }

    iCAX::Interaction::CInvocationResult HandleUpdateComponentCSGModel(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext*)
    {
        tube::license::Enforce<114, tube::license::Feature::Design>();
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _ID = UuidToString(ParseRequiredUuid(GetRequiredText(_Payload, "id"), "id"));
        auto _Store = GetUserDataStore(ProductContext_);
        auto _Record = _Store->Get(kComponentModelFeature, kComponentModelRecordType, _ID);
        if (!_Record || !_Record->Payload.Is<ObjectMap>()) throw std::invalid_argument("配件模型不存在");
        const auto _Previous = _Record->Payload.To<ObjectMap>();
        if (GetString(_Previous, "modelType") != "csg" || !_Previous.contains("csgDefinition"))
            throw std::invalid_argument("导入模型没有可编辑的 CSG 构造树");
        auto _Snapshot = CreateComponentCSGModelSnapshot(
            GetRequiredObject(_Payload, "csgDefinition"), _Payload);
        _Record->Payload = _Snapshot;
        const auto _Saved = _Store->Put(*_Record, ComponentExpectedRevision(_Payload));
        return MakeResponse(ObjectMap{ { "model", ComponentModelSummary(_Snapshot,
            "user", _ID, _Saved.Revision) } });
    }

    iCAX::Interaction::CInvocationResult HandleUpdateComponentModel(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext*)
    {
        tube::license::Enforce<111, tube::license::Feature::Design>();
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _ID = UuidToString(ParseRequiredUuid(GetRequiredText(_Payload, "id"), "id"));
        auto _Store = GetUserDataStore(ProductContext_);
        auto _Record = _Store->Get(kComponentModelFeature, kComponentModelRecordType, _ID);
        if (!_Record || !_Record->Payload.Is<ObjectMap>()) throw std::invalid_argument("配件模型不存在");
        auto _Snapshot = _Record->Payload.To<ObjectMap>();
        UpdateComponentModelMetadata(_Snapshot, _Payload);
        _Record->Payload = _Snapshot;
        const auto _Saved = _Store->Put(*_Record, ComponentExpectedRevision(_Payload));
        return MakeResponse(ObjectMap{ { "model", ComponentModelSummary(_Snapshot,
            "user", _ID, _Saved.Revision) } });
    }

    iCAX::Interaction::CInvocationResult HandleDeleteComponentModel(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext*)
    {
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _ID = UuidToString(ParseRequiredUuid(GetRequiredText(_Payload, "id"), "id"));
        const auto _Deleted = GetUserDataStore(ProductContext_)->Delete(kComponentModelFeature,
            kComponentModelRecordType, _ID, ComponentExpectedRevision(_Payload));
        return MakeResponse(ObjectMap{ { "deleted", _Deleted } });
    }

    ObjectMap ResolveComponentModelRequest(
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Application::IProductUserDataStore& Store_, const ObjectMap& Payload_)
    {
        const auto _Scope = GetRequiredText(Payload_, "scope", 16);
        const auto _ID = GetRequiredText(Payload_, "id", 120);
        if (_Scope == "template")
        {
            const auto _TemplateID = GetRequiredText(Payload_, "templateId", 240);
            const auto _Package = LoadPythonTemplatePackage(ApplicationContext_, _TemplateID);
            return LoadTemplateComponentModel(_Package.DescriptorPath.parent_path(),
                _Package.Descriptor.Extensions, _TemplateID, _Package.Descriptor.DisplayName.Resolve(), _ID);
        }
        return ResolveComponentModelSnapshot(ResolveComponentModelRoot(ApplicationContext_),
            &Store_, _Scope, _ID, ResolveUserComponentModelRoot(ApplicationContext_));
    }

    iCAX::Interaction::CInvocationResult HandleExportComponentModel(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext*)
    {
        tube::license::Enforce<302, tube::license::Feature::StepExport>();
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _Snapshot = ResolveComponentModelRequest(ApplicationContext_,
            *GetUserDataStore(ProductContext_), _Payload);
        const auto _TargetDirectory = Utf8Path(GetRequiredText(_Payload, "targetDirectory", 32767));
        if (!_TargetDirectory.is_absolute() || !std::filesystem::is_directory(_TargetDirectory))
            throw std::invalid_argument("请选择存在的配件 STEP 导出目录");
        const auto _Name = GetString(_Snapshot, "name", "配件");
        const auto _TargetPath = MakeUniqueExportPath(_TargetDirectory, _Name, ".step");
        ExportComponentModelStep(_Snapshot, _TargetPath);
        ObjectMap _Response{
            { "path", Utf8PathText(_TargetPath) }, { "format", std::string("step") },
            { "name", _Name }, { "scope", GetRequiredText(_Payload, "scope", 16) },
            { "id", GetRequiredText(_Payload, "id", 120) }
        };
        if (GetString(_Payload, "scope") == "template")
            _Response["templateId"] = GetRequiredText(_Payload, "templateId", 240);
        return MakeResponse(Variant(_Response));
    }

    iCAX::Interaction::CInvocationResult HandleGenerateComponentModelPreview(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext* Scene_)
    {
        tube::license::Enforce<112, tube::license::Feature::Design>();
        if (!Scene_) throw std::invalid_argument("配件预览需要当前项目场景");
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _Scope = GetRequiredText(_Payload, "scope", 16);
        const auto _ID = GetRequiredText(_Payload, "id", 120);
        auto _Store = GetUserDataStore(ProductContext_);
        const auto _Snapshot = ResolveComponentModelRequest(ApplicationContext_, *_Store, _Payload);
        std::string _ResourceIdentity = _Scope + "/" + _ID;
        if (_Scope == "template")
        {
            const auto _TemplateID = GetRequiredText(_Payload, "templateId", 240);
            _ResourceIdentity = _Scope + "/" + _TemplateID + "/" + _ID;
        }
        const auto _Name = GetString(_Snapshot, "name");
        const auto _Resource = StoreBRep(*Scene_, "tube-designer/component-preview/" + _ResourceIdentity,
            _Name, ComponentModelShape(_Snapshot));
        const auto _Geometry = iCAX::RenderInteraction::EnsureFrontendGeometryResource(
            Scene_->Resources(), _Resource.URL, iCAX::Render::ERenderGeometryKind::Mesh);
        const auto _Material = EnsureDesignerMaterial(*Scene_);
        return MakeResponse(ObjectMap{
            { "model", ComponentModelSummary(_Snapshot, _Scope, _ID,
                static_cast<std::uint64_t>(GetDouble(_Snapshot, "revision", 0))) },
            { "geometryResourceId", _Geometry.URL },
            { "geometryResourceVersion", static_cast<unsigned long long>(_Geometry.nVersion) },
            { "materialResourceId", _Material.URL },
            { "materialResourceVersion", static_cast<unsigned long long>(_Material.nVersion) },
            { "brepResourceId", _Resource.URL },
            { "brepResourceVersion", static_cast<unsigned long long>(_Resource.nVersion) }
        });
    }

    iCAX::Interaction::CInvocationResult HandleGenerateComponentCSGPreview(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext* Scene_)
    {
        tube::license::Enforce<115, tube::license::Feature::Design>();
        if (!Scene_) throw std::invalid_argument("CSG 实时预览需要当前项目场景");
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _Definition = GetRequiredObject(_Payload, "csgDefinition");
        ObjectMap _Response;
        const auto _SelectedID = GetString(_Payload, "selectedFeatureId");
        if (!_SelectedID.empty())
        {
            for (const auto& _Value : _Definition.at("features").To<VariantArray>())
            {
                if (!_Value.Is<ObjectMap>()) continue;
                auto _Feature = _Value.To<ObjectMap>();
                if (GetString(_Feature, "id") != _SelectedID) continue;
                _Feature["operation"] = std::string("base");
                const ObjectMap _OperandDefinition{
                    { "schema", std::string("icax.component-csg") }, { "schemaVersion", 1 },
                    { "evaluation", std::string("left-fold") },
                    { "features", VariantArray{ std::move(_Feature) } }
                };
                const auto _OperandSnapshot = CreateComponentCSGModelSnapshot(_OperandDefinition, {});
                const auto _OperandResource = StoreBRep(*Scene_,
                    "tube-designer/component-csg-live/selected-operand", "CSG 当前基础体",
                    ComponentModelShape(_OperandSnapshot));
                const auto _OperandGeometry = iCAX::RenderInteraction::EnsureFrontendGeometryResource(
                    Scene_->Resources(), _OperandResource.URL, iCAX::Render::ERenderGeometryKind::Mesh);
                const auto _OperandMaterial = EnsureComponentCSGOperandMaterial(*Scene_);
                _Response["operandGeometryResourceId"] = _OperandGeometry.URL;
                _Response["operandGeometryResourceVersion"] =
                    static_cast<unsigned long long>(_OperandGeometry.nVersion);
                _Response["operandMaterialResourceId"] = _OperandMaterial.URL;
                _Response["operandMaterialResourceVersion"] =
                    static_cast<unsigned long long>(_OperandMaterial.nVersion);
                break;
            }
        }

        try
        {
            const auto _Snapshot = CreateComponentCSGModelSnapshot(_Definition, {});
            const auto _CanonicalDefinition = GetRequiredObject(_Snapshot, "csgDefinition");
            const auto _ResultResource = StoreBRep(*Scene_,
                "tube-designer/component-csg-live/result", "CSG 实时结果",
                ComponentModelShape(_Snapshot));
            const auto _ResultGeometry = iCAX::RenderInteraction::EnsureFrontendGeometryResource(
                Scene_->Resources(), _ResultResource.URL, iCAX::Render::ERenderGeometryKind::Mesh);
            const auto _ResultMaterial = EnsureDesignerMaterial(*Scene_);
            _Response["valid"] = true;
            _Response["geometryResourceId"] = _ResultGeometry.URL;
            _Response["geometryResourceVersion"] =
                static_cast<unsigned long long>(_ResultGeometry.nVersion);
            _Response["materialResourceId"] = _ResultMaterial.URL;
            _Response["materialResourceVersion"] =
                static_cast<unsigned long long>(_ResultMaterial.nVersion);
            _Response["brepResourceId"] = _ResultResource.URL;
            _Response["brepResourceVersion"] =
                static_cast<unsigned long long>(_ResultResource.nVersion);
            _Response["bounds"] = _Snapshot.at("bounds");
            _Response["csgDefinition"] = std::move(_CanonicalDefinition);
        }
        catch (const std::exception& _Error)
        {
            // A valid operand may produce an empty intersection or remove the
            // whole result. Return its preview so the user can move it back into
            // place instead of hiding the very object that needs correction.
            if (!_Response.contains("operandGeometryResourceId")) throw;
            _Response["valid"] = false;
            _Response["resultError"] = std::string(_Error.what());
        }
        return MakeResponse(std::move(_Response));
    }

    std::optional<iCAX::Application::CProductUserDataRecord> FindProfileRecord(
        iCAX::Application::IProductUserDataStore& Store_,
        const std::string& RecordID_)
    {
        for (const auto* _RecordType : {
            kImportedProfileRecordType, kParametricProfileRecordType })
        {
            if (auto _Record = Store_.Get(kProfileFeatureID, _RecordType, RecordID_))
                return _Record;
        }
        return std::nullopt;
    }

    ObjectMap ResolveStoredProfileSnapshot(
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Application::IProductUserDataStore& Store_,
        const std::string& RecordID_,
        const ObjectMap& Parameters_)
    {
        const auto _Record = FindProfileRecord(Store_, RecordID_);
        if (!_Record)
            throw std::invalid_argument("TubeDesigner profile does not exist");
        if (!_Record->Payload.Is<ObjectMap>())
            throw std::runtime_error("TubeDesigner profile payload is invalid");

        auto _Stored = _Record->Payload.To<ObjectMap>();
        ObjectMap _Profile;
        if (_Record->RecordType == kParametricProfileRecordType)
        {
            const auto _Values = Parameters_.empty()
                ? GetRequiredObject(_Stored, "defaultParameters")
                : Parameters_;
            _Profile = EvaluateProfilePackage(ApplicationContext_, _Stored, _Values);
        }
        else
        {
            _Profile = std::move(_Stored);
            ValidateImportedProfileDefinition(_Profile);
        }
        _Profile["name"] = GetString(
            _Record->Payload.To<ObjectMap>(), "name", GetString(_Profile, "name"));
        _Profile["savedProfileId"] = RecordID_;
        return _Profile;
    }

    ObjectMap ResolveProfileSnapshot(
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Application::IProductUserDataStore& Store_,
        const SProfileReference& Reference_,
        const ObjectMap& Parameters_)
    {
        if (Reference_.Scope == "system")
        {
            auto _Profile = EvaluateSystemProfile(
                ApplicationContext_, Reference_.ID, Parameters_);
            _Profile["profileScope"] = std::string("system");
            _Profile["profileDefinitionId"] = Reference_.ID;
            return _Profile;
        }
        if (Reference_.Scope == "template")
        {
            const auto _Package = LoadPythonTemplatePackage(ApplicationContext_, Reference_.TemplateID);
            const auto _Resources = GetRequiredObject(_Package.Descriptor.Extensions, "profileResources");
            const auto _Result = InvokeProfilePackageRuntime(ApplicationContext_, ObjectMap{
                { "action", std::string("evaluate-template") },
                { "templateId", _Package.Descriptor.ID },
                { "templateName", _Package.Descriptor.DisplayName.Resolve() },
                { "templateDirectory", PathToUTF8(_Package.DescriptorPath.parent_path()) },
                { "profileResources", _Resources }, { "profileId", Reference_.ID }, { "values", Parameters_ }
            });
            auto _Profile = GetRequiredObject(_Result, "profile");
            ValidateImportedProfileDefinition(_Profile);
            _Profile["profileScope"] = std::string("template");
            _Profile["templateId"] = Reference_.TemplateID;
            _Profile["profileDefinitionId"] = Reference_.ID;
            _Profile.erase("savedProfileId");
            return _Profile;
        }
        ObjectMap _Profile;
        const auto _UserProfileRoot = ResolveUserProfileRoot(ApplicationContext_);
        const bool _HasFilesystemProfile = !_UserProfileRoot.empty()
            && std::filesystem::is_directory(_UserProfileRoot / Reference_.ID);
        if (_HasFilesystemProfile)
            _Profile = EvaluateUserProfile(ApplicationContext_, Reference_.ID, Parameters_);
        else
            _Profile = ResolveStoredProfileSnapshot(
                ApplicationContext_, Store_, Reference_.ID, Parameters_);
        _Profile["profileScope"] = std::string("user");
        return _Profile;
    }

    ObjectMap MakeUserDataPayload(const iCAX::Application::CProductUserDataRecord& Record_)
    {
        ObjectMap _Payload = Record_.Payload.Is<ObjectMap>()
            ? Record_.Payload.To<ObjectMap>()
            : ObjectMap();
        _Payload["id"] = Record_.RecordID;
        _Payload["ownerScope"] = Record_.OwnerScope;
        _Payload["revision"] = static_cast<unsigned long long>(Record_.Revision);
        _Payload["createdAt"] = Record_.CreatedAt;
        _Payload["updatedAt"] = Record_.UpdatedAt;
        return _Payload;
    }

    ObjectMap MakeProfileUserDataPayload(
        const iCAX::Application::CProductUserDataRecord& Record_)
    {
        auto _Payload = MakeUserDataPayload(Record_);
        _Payload["profileType"] = Record_.RecordType;
        _Payload["profileScope"] = std::string("user");
        _Payload["profileRef"] = ObjectMap{
            { "scope", std::string("user") }, { "id", Record_.RecordID }
        };
        if (Record_.RecordType == kParametricProfileRecordType)
            _Payload.erase("scriptSource");
        return _Payload;
    }

    ObjectMap MakeSystemProfilePayload(const ObjectMap& Package_)
    {
        auto _Payload = Package_;
        _Payload.erase("scriptSource");
        const auto _Descriptor = GetRequiredObject(Package_, "descriptor");
        const auto _ProfileID = GetRequiredText(_Descriptor, "id", 80);
        _Payload["id"] = _ProfileID;
        _Payload["profileType"] = std::string(kParametricProfileRecordType);
        _Payload["profileScope"] = std::string("system");
        _Payload["profileRef"] = ObjectMap{
            { "scope", std::string("system") }, { "id", _ProfileID }
        };
        _Payload["ownerScope"] = std::string("system");
        _Payload["revision"] = 0ull;
        _Payload["capabilities"] = ObjectMap{
            { "editParameters", true },
            { "preview", true },
            { "export", true },
            { "rename", false },
            { "delete", false }
        };
        return _Payload;
    }

    ObjectMap MakeUserProfilePackagePayload(const ObjectMap& Package_)
    {
        auto _Payload = Package_;
        _Payload.erase("scriptSource");
        const auto _Descriptor = GetRequiredObject(Package_, "descriptor");
        const auto _ProfileID = GetRequiredText(_Descriptor, "id", 80);
        _Payload["id"] = _ProfileID;
        _Payload["profileType"] = std::string(kParametricProfileRecordType);
        _Payload["profileScope"] = std::string("user");
        _Payload["libraryScope"] = std::string("user");
        _Payload["profileRef"] = ObjectMap{
            { "scope", std::string("user") }, { "id", _ProfileID }
        };
        _Payload["ownerScope"] = std::string("user");
        _Payload["revision"] = 0ull;
        _Payload["capabilities"] = ObjectMap{
            { "editParameters", true }, { "preview", true }, { "export", true },
            { "rename", false }, { "delete", false }
        };
        return _Payload;
    }

    ObjectMap MakeParameterPresetPayload(
        const iCAX::Application::CProductUserDataRecord& Record_)
    {
        auto _Payload = MakeUserDataPayload(Record_);
        _Payload["templateId"] = Record_.SubjectID;
        std::string _CustomerID;
        for (const auto& _Link : Record_.Links)
        {
            if (_Link.RelationType == kCustomerRelationType
                && _Link.TargetKind == "user-record"
                && _Link.TargetFeatureID == kCustomerFeatureID
                && _Link.TargetType == kCustomerRecordType)
            {
                _CustomerID = _Link.TargetID;
                break;
            }
        }
        _Payload["customerId"] = _CustomerID;
        return _Payload;
    }

    ObjectMap MakeProductTemplatePayload(
        const iCAX::Application::CProductUserDataRecord& Record_)
    {
        auto _Payload = MakeUserDataPayload(Record_);
        _Payload["templateType"] = std::string(kProductTemplateRecordType);
        _Payload["templateScope"] = std::string("personal");
        _Payload["sourceFormat"] = std::string("itpt");
        _Payload["capabilities"] = ObjectMap{
            { "export", true }, { "rename", false }, { "delete", true }
        };
        if (const auto _Descriptor = _Payload.find("descriptor");
            _Descriptor != _Payload.end() && _Descriptor->second.Is<ObjectMap>())
        {
            const auto& _Value = _Descriptor->second.To<ObjectMap>();
            _Payload["templateId"] = GetString(_Value, "id");
            _Payload["version"] = GetString(_Value, "version");
            const auto _CustomName = GetString(_Payload, "name");
            _Payload["displayName"] = !_CustomName.empty()
                ? Variant(_CustomName)
                : (_Value.contains("displayName")
                    ? _Value.at("displayName") : Variant(std::string("产品模板")));
        }
        _Payload.erase("scriptSource");
        _Payload.erase("resources");
        return _Payload;
    }

    std::string ProductTemplateLocalizedText(
        const Variant& Value_, const std::string& Default_)
    {
        if (Value_.Is<std::string>()) return Value_.To<std::string>();
        if (Value_.Is<ObjectMap>())
        {
            const auto& _Localized = Value_.To<ObjectMap>();
            return GetString(_Localized, "zh-CN",
                GetString(_Localized, "zh", GetString(_Localized, "en-US", Default_)));
        }
        return Default_;
    }

    ObjectMap LegacyProductTemplatePresetDescriptor(
        const ObjectMap& Descriptor_, const ObjectMap& Preset_, const std::string& TemplateID_)
    {
        const auto _PresetID = GetString(Preset_, "id");
        if (_PresetID.empty()) throw std::invalid_argument("旧产品模板款式缺少 ID");
        auto _Result = Descriptor_;
        auto _NewID = TemplateID_ + "-" + _PresetID;
        if (_NewID.size() > 240) _NewID.resize(240);
        _Result["id"] = _NewID;
        if (const auto _Name = Preset_.find("displayName"); _Name != Preset_.end())
            _Result["displayName"] = _Name->second;

        ObjectMap _Extensions;
        if (const auto _It = _Result.find("extensions");
            _It != _Result.end() && _It->second.Is<ObjectMap>())
            _Extensions = _It->second.To<ObjectMap>();
        ObjectMap _Catalog;
        if (const auto _It = _Extensions.find("catalog");
            _It != _Extensions.end() && _It->second.Is<ObjectMap>())
            _Catalog = _It->second.To<ObjectMap>();
        _Catalog.erase("presets");
        if (!_Catalog.contains("categoryPath"))
        {
            VariantArray _CategoryPath;
            const auto _OriginalName = ProductTemplateLocalizedText(
                Descriptor_.contains("displayName") ? Descriptor_.at("displayName") : Variant(), "");
            std::size_t _Start = 0;
            while (_Start < _OriginalName.size())
            {
                const auto _End = _OriginalName.find('/', _Start);
                const auto _Part = _OriginalName.substr(
                    _Start, _End == std::string::npos ? std::string::npos : _End - _Start);
                if (!_Part.empty()) _CategoryPath.emplace_back(_Part);
                if (_End == std::string::npos || _CategoryPath.size() >= 2) break;
                _Start = _End + 1;
            }
            if (_CategoryPath.empty())
                _CategoryPath.emplace_back(std::string("其他产品"));
            _Catalog["categoryPath"] = std::move(_CategoryPath);
        }
        _Extensions["catalog"] = std::move(_Catalog);
        _Result["extensions"] = std::move(_Extensions);

        const auto _PresetParameters = Preset_.find("parameters");
        if (_PresetParameters != Preset_.end() && _PresetParameters->second.Is<ObjectMap>())
        {
            if (const auto _Definitions = _Result.find("parameters");
                _Definitions != _Result.end() && _Definitions->second.Is<VariantArray>())
            {
                VariantArray _Updated;
                for (const auto& _Value : _Definitions->second.To<VariantArray>())
                {
                    if (!_Value.Is<ObjectMap>())
                    {
                        _Updated.emplace_back(_Value);
                        continue;
                    }
                    auto _Definition = _Value.To<ObjectMap>();
                    const auto _Key = GetString(_Definition, "key");
                    const auto _Override = _PresetParameters->second.To<ObjectMap>().find(_Key);
                    if (_Override != _PresetParameters->second.To<ObjectMap>().end())
                        _Definition["defaultValue"] = _Override->second;
                    _Updated.emplace_back(std::move(_Definition));
                }
                _Result["parameters"] = std::move(_Updated);
            }
        }
        return _Result;
    }

    std::size_t MigrateLegacyProductTemplateRecords(
        iCAX::Application::IProductUserDataStore& Store_)
    {
        iCAX::Application::CProductUserDataQuery _Query;
        _Query.FeatureID = kProductTemplateFeatureID;
        _Query.RecordType = kProductTemplateRecordType;
        _Query.OwnerScope = "personal";
        std::size_t _Migrated = 0;
        const auto _Records = Store_.List(_Query);
        std::set<std::string> _ExistingMigrationKeys;
        for (const auto& _Record : _Records)
        {
            if (!_Record.Payload.Is<ObjectMap>()) continue;
            const auto& _Payload = _Record.Payload.To<ObjectMap>();
            const auto _Migration = _Payload.find("legacyMigration");
            if (_Migration == _Payload.end() || !_Migration->second.Is<ObjectMap>()) continue;
            const auto& _Info = _Migration->second.To<ObjectMap>();
            const auto _Source = GetString(_Info, "sourceRecordId");
            const auto _Preset = GetString(_Info, "sourcePresetId");
            if (!_Source.empty() && !_Preset.empty())
                _ExistingMigrationKeys.insert(_Source + "\n" + _Preset);
        }
        for (const auto& _Record : _Records)
        {
            if (!_Record.Payload.Is<ObjectMap>()) continue;
            const auto& _Payload = _Record.Payload.To<ObjectMap>();
            const auto _DescriptorIt = _Payload.find("descriptor");
            if (_DescriptorIt == _Payload.end() || !_DescriptorIt->second.Is<ObjectMap>()) continue;
            const auto& _Descriptor = _DescriptorIt->second.To<ObjectMap>();
            const auto _ExtensionsIt = _Descriptor.find("extensions");
            if (_ExtensionsIt == _Descriptor.end() || !_ExtensionsIt->second.Is<ObjectMap>()) continue;
            const auto& _Extensions = _ExtensionsIt->second.To<ObjectMap>();
            const auto _CatalogIt = _Extensions.find("catalog");
            if (_CatalogIt == _Extensions.end() || !_CatalogIt->second.Is<ObjectMap>()) continue;
            const auto& _Catalog = _CatalogIt->second.To<ObjectMap>();
            const auto _PresetsIt = _Catalog.find("presets");
            if (_PresetsIt == _Catalog.end() || !_PresetsIt->second.Is<VariantArray>()) continue;

            const auto& _Presets = _PresetsIt->second.To<VariantArray>();
            std::vector<std::pair<ObjectMap, ObjectMap>> _Converted;
            _Converted.reserve(_Presets.size());
            for (const auto& _Preset : _Presets)
            {
                if (!_Preset.Is<ObjectMap>()) continue;
                const auto& _PresetObject = _Preset.To<ObjectMap>();
                if (GetString(_PresetObject, "id").empty()) continue;
                _Converted.emplace_back(
                    LegacyProductTemplatePresetDescriptor(_Descriptor, _PresetObject,
                        GetString(_Descriptor, "id")), _PresetObject);
            }
            if (_Converted.empty()) continue;

            try
            {
                for (std::size_t _Index = 1; _Index < _Converted.size(); ++_Index)
                {
                    auto _Child = _Record;
                    const auto _PresetID = GetString(_Converted[_Index].second, "id");
                    const auto _MigrationKey = _Record.RecordID + "\n" + _PresetID;
                    if (_ExistingMigrationKeys.contains(_MigrationKey)) continue;
                    _Child.RecordID = UuidToString(iCAX::Data::GenerateNewUUID());
                    _Child.SubjectID = GetString(_Converted[_Index].first, "id");
                    auto _ChildPayload = _Payload;
                    _ChildPayload["descriptor"] = _Converted[_Index].first;
                    _ChildPayload["name"] = ProductTemplateLocalizedText(
                        _Converted[_Index].first.at("displayName"), _Child.SubjectID);
                    _ChildPayload["legacyMigration"] = ObjectMap{
                        { "sourceRecordId", _Record.RecordID },
                        { "sourcePresetId", _PresetID },
                        { "version", 1ull }
                    };
                    _Child.Payload = Variant(std::move(_ChildPayload));
                    _Child.Revision = 0;
                    _Child.CreatedAt.clear();
                    _Child.UpdatedAt.clear();
                    Store_.Put(_Child, 0);
                    _ExistingMigrationKeys.insert(_MigrationKey);
                }

                auto _Updated = _Record;
                _Updated.SubjectID = GetString(_Converted.front().first, "id");
                auto _UpdatedPayload = _Payload;
                _UpdatedPayload["descriptor"] = _Converted.front().first;
                _UpdatedPayload["name"] = ProductTemplateLocalizedText(
                    _Converted.front().first.at("displayName"), _Updated.SubjectID);
                _UpdatedPayload["legacyMigration"] = ObjectMap{
                    { "sourceRecordId", _Record.RecordID },
                    { "sourcePresetId", GetString(_Converted.front().second, "id") },
                    { "version", 1ull }
                };
                _Updated.Payload = Variant(std::move(_UpdatedPayload));
                Store_.Put(_Updated, _Record.Revision);
                ++_Migrated;
            }
            catch (const std::exception&)
            {
                // Migration is best-effort. A stale revision or malformed old
                // record must not prevent the application from starting; the
                // untouched legacy record remains available for retry/export.
            }
        }
        return _Migrated;
    }

    VariantArray ListUserDataRecords(
        iCAX::Application::IProductUserDataStore& Store_,
        const std::string& FeatureID_,
        const std::string& RecordType_,
        const bool bParameterPreset_ = false)
    {
        iCAX::Application::CProductUserDataQuery _Query;
        _Query.FeatureID = FeatureID_;
        _Query.RecordType = RecordType_;
        VariantArray _Items;
        for (const auto& _Record : Store_.List(_Query))
        {
            _Items.emplace_back(bParameterPreset_
                ? MakeParameterPresetPayload(_Record)
                : MakeUserDataPayload(_Record));
        }
        return _Items;
    }

    VariantArray ListProfileUserDataRecords(
        iCAX::Application::IProductUserDataStore& Store_)
    {
        VariantArray _Items;
        for (const auto* _RecordType : {
            kImportedProfileRecordType, kParametricProfileRecordType })
        {
            iCAX::Application::CProductUserDataQuery _Query;
            _Query.FeatureID = kProfileFeatureID;
            _Query.RecordType = _RecordType;
            for (const auto& _Record : Store_.List(_Query))
                _Items.emplace_back(MakeProfileUserDataPayload(_Record));
        }
        return _Items;
    }

    ObjectMap MakePunchToolUserDataPayload(
        const iCAX::Application::CProductUserDataRecord& Record_)
    {
        auto _Payload = Record_.Payload.Is<ObjectMap>()
            ? Record_.Payload.To<ObjectMap>() : ObjectMap();
        const auto _Descriptor = _Payload.find("descriptor");
        if (_Descriptor == _Payload.end() || !_Descriptor->second.Is<ObjectMap>())
            throw std::runtime_error("TubeDesigner punch tool payload is invalid");
        const auto& _Definition = _Descriptor->second.To<ObjectMap>();
        _Payload["id"] = GetRequiredText(_Definition, "id", 80);
        _Payload["displayName"] = GetString(_Definition, "displayName", "未命名模具");
        _Payload["name"] = GetString(_Definition, "displayName", "未命名模具");
        _Payload["kind"] = GetString(_Definition, "kind", "programmatic");
        _Payload["target"] = GetString(_Definition, "target", "side");
        _Payload["category"] = GetString(_Definition, "category",
            GetString(_Definition, "target") == "end" ? "端面" : "孔型");
        _Payload["version"] = GetString(_Definition, "version", "1.0.0");
        _Payload["parameters"] = _Definition.contains("parameters")
            ? _Definition.at("parameters") : VariantArray{};
        if (!_Payload.contains("defaultParameters"))
            _Payload["defaultParameters"] = ObjectMap();
        if (_Payload.contains("packageDigest") && !_Payload.contains("digest"))
            _Payload["digest"] = _Payload.at("packageDigest");
        _Payload["libraryScope"] = std::string("user");
        _Payload["toolScope"] = std::string("user");
        _Payload["toolRecordType"] = Record_.RecordType;
        _Payload["recordId"] = Record_.RecordID;
        _Payload["ownerScope"] = Record_.OwnerScope;
        _Payload["revision"] = static_cast<unsigned long long>(Record_.Revision);
        _Payload["createdAt"] = Record_.CreatedAt;
        _Payload["updatedAt"] = Record_.UpdatedAt;
        _Payload["capabilities"] = ObjectMap{
            { "rename", true }, { "delete", true }, { "editParameters", false }, { "preview", true }
        };
        return _Payload;
    }

    VariantArray ListPunchToolUserDataRecords(
        iCAX::Application::IProductUserDataStore& Store_)
    {
        VariantArray _Items;
        for (const auto* _RecordType : { kFixedPunchToolRecordType, kParametricPunchToolRecordType })
        {
            iCAX::Application::CProductUserDataQuery _Query;
            _Query.FeatureID = kPunchToolFeatureID;
            _Query.RecordType = _RecordType;
            for (const auto& _Record : Store_.List(_Query))
                _Items.emplace_back(MakePunchToolUserDataPayload(_Record));
        }
        return _Items;
    }

    VariantArray ListProductTemplateRecords(
        iCAX::Application::IProductUserDataStore& Store_)
    {
        iCAX::Application::CProductUserDataQuery _Query;
        _Query.FeatureID = kProductTemplateFeatureID;
        _Query.RecordType = kProductTemplateRecordType;
        VariantArray _Items;
        for (const auto& _Record : Store_.List(_Query))
            _Items.emplace_back(MakeProductTemplatePayload(_Record));
        return _Items;
    }

    VariantArray ListSystemProfileRecords(
        const iCAX::Application::IApplicationContext& ApplicationContext_)
    {
        VariantArray _Items;
        for (const auto& _Package : LoadSystemProfilePackages(ApplicationContext_))
            _Items.emplace_back(MakeSystemProfilePayload(_Package));
        return _Items;
    }

    VariantArray ListUserProfilePackageRecords(
        const iCAX::Application::IApplicationContext& ApplicationContext_)
    {
        VariantArray _Items;
        for (const auto& _Package : LoadUserProfilePackages(ApplicationContext_))
            _Items.emplace_back(MakeUserProfilePackagePayload(_Package));
        return _Items;
    }

    VariantArray ListTemplateProfileRecords(
        const iCAX::Application::IApplicationContext& ApplicationContext_)
    {
        VariantArray _Templates;
        const auto _Root = ResolveTemplateRoot(ApplicationContext_);
        for (const auto& _Directory : DiscoverPythonTemplateDirectories(ApplicationContext_))
        {
            const auto _Package = LoadPythonTemplatePackageFromDirectory(_Directory, _Root);
            const auto _It = _Package.Descriptor.Extensions.find("profileResources");
            if (_It == _Package.Descriptor.Extensions.end()) continue;
            const auto _Resources = GetRequiredObject(_Package.Descriptor.Extensions, "profileResources");
            if (_Resources.empty()) continue;
            _Templates.emplace_back(ObjectMap{
                { "templateId", _Package.Descriptor.ID },
                { "templateName", _Package.Descriptor.DisplayName.Resolve() },
                { "templateDirectory", PathToUTF8(_Directory) }, { "profileResources", _Resources }
            });
        }
        if (_Templates.empty()) return {};
        const auto _Result = InvokeProfilePackageRuntime(ApplicationContext_, ObjectMap{
            { "action", std::string("list-template") }, { "templates", std::move(_Templates) }
        });
        const auto _It = _Result.find("templateProfiles");
        if (_It == _Result.end() || !_It->second.Is<VariantArray>())
            throw std::runtime_error("模板管型目录返回的数据无效");
        VariantArray _Items;
        for (const auto& _Value : _It->second.To<VariantArray>())
        {
            auto _Package = _Value.To<ObjectMap>();
            ValidateProfilePackageRecord(_Package);
            const auto _ID = GetRequiredText(_Package, "id", 80);
            const auto _TemplateID = GetRequiredText(_Package, "templateId", 240);
            _Package.erase("scriptSource");
            _Package["profileType"] = std::string(kParametricProfileRecordType);
            _Package["libraryScope"] = std::string("template");
            _Package["profileScope"] = std::string("template");
            _Package["ownerScope"] = std::string("template");
            _Package["profileRef"] = ProfileReferencePayload({ "template", _ID, _TemplateID });
            _Package["revision"] = 0ull;
            _Package["capabilities"] = ObjectMap{
                { "editParameters", true }, { "preview", true }, { "export", true },
                { "rename", false }, { "delete", false }
            };
            _Items.emplace_back(std::move(_Package));
        }
        return _Items;
    }

    iCAX::Interaction::CInvocationResult HandleListUserData(
        const iCAX::Interaction::CInvocation&,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext*)
    {
        auto _Store = GetUserDataStore(ProductContext_);
        // Upgrade legacy personal product-template records before exposing the
        // catalogue. Old records may contain many catalog presets; each preset
        // becomes its own record while preserving the original record ID for
        // the first converted template.
        (void)MigrateLegacyProductTemplateRecords(*_Store);
        ObjectMap _Response;
        _Response["customers"] = ListUserDataRecords(
            *_Store, kCustomerFeatureID, kCustomerRecordType);
        _Response["parameterPresets"] = ListUserDataRecords(
            *_Store, kTemplateFeatureID, kParameterPresetRecordType, true);
        _Response["profiles"] = ListProfileUserDataRecords(*_Store);
        {
            auto _UserProfiles = _Response["profiles"].To<VariantArray>();
            const auto _FilesystemProfiles = ListUserProfilePackageRecords(ApplicationContext_);
            _UserProfiles.insert(_UserProfiles.end(), _FilesystemProfiles.begin(), _FilesystemProfiles.end());
            _Response["profiles"] = std::move(_UserProfiles);
        }
        // Tool definitions are kept as a separate library from tube profiles.
        // Punching, drawing and product disassembly only persist references to
        // these definitions; the definition itself belongs to this library.
        _Response["punchTools"] = ListPunchToolUserDataRecords(*_Store);
        _Response["productTemplates"] = ListProductTemplateRecords(*_Store);
        _Response["systemProfiles"] = ListSystemProfileRecords(ApplicationContext_);
        _Response["templateProfiles"] = ListTemplateProfileRecords(ApplicationContext_);
        _Response["profileId"] = std::string("local-default");
        return MakeResponse(Variant(_Response));
    }

    iCAX::Interaction::CInvocationResult HandleImportProductTemplatePackage(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext*)
    {
        const auto _Request = DecodeObjectPayload(Request_);
        const auto _SourcePath = GetRequiredText(_Request, "sourcePath", 32767);
        auto _Extension = Utf8Path(_SourcePath).extension().string();
        std::transform(_Extension.begin(), _Extension.end(), _Extension.begin(), [](unsigned char Value_) {
            return static_cast<char>(std::tolower(Value_));
        });
        if (_Extension != ".itpt")
            throw std::invalid_argument("产品模板必须使用 .itpt 压缩包");
        auto _Runtime = InvokeProductTemplatePackageRuntime(ApplicationContext_, ObjectMap{
            { "action", std::string("inspect") }, { "sourcePath", _SourcePath }
        });
        auto _Package = GetRequiredObject(_Runtime, "package");
        const auto _Descriptor = GetRequiredObject(_Package, "descriptor");
        const auto _TemplateID = GetRequiredText(_Descriptor, "id", 240);
        const auto _Name = [&]() {
            if (const auto _DisplayName = _Descriptor.find("displayName");
                _DisplayName != _Descriptor.end())
            {
                if (_DisplayName->second.Is<std::string>())
                    return _DisplayName->second.To<std::string>();
                if (_DisplayName->second.Is<ObjectMap>())
                {
                    const auto& _Localized = _DisplayName->second.To<ObjectMap>();
                    return GetString(_Localized, "zh-CN",
                        GetString(_Localized, "zh", GetString(_Localized, "en-US", _TemplateID)));
                }
            }
            return _TemplateID;
        }();
        _Package["name"] = _Name;
        _Package["sourceFormat"] = std::string("itpt");
        MaterializeUserProductTemplate(ApplicationContext_, _Package);
        // The executable template is now a normal directory package under the
        // user data root. Keep the user-data record lightweight; startup and
        // evaluation read template.json/template.py/resource directly.
        _Package.erase("scriptSource");
        _Package.erase("resources");
        _Package["storage"] = std::string("filesystem");
        iCAX::Application::CProductUserDataRecord _Record;
        _Record.FeatureID = kProductTemplateFeatureID;
        _Record.RecordType = kProductTemplateRecordType;
        _Record.SubjectType = kProductTemplateSubjectType;
        _Record.SubjectID = _TemplateID;
        _Record.RecordID = UuidToString(iCAX::Data::GenerateNewUUID());
        _Record.OwnerScope = "personal";
        _Record.Payload = Variant(_Package);
        const auto _Saved = GetUserDataStore(ProductContext_)->Put(_Record, 0);
        return MakeResponse(ObjectMap{{ "template", MakeProductTemplatePayload(_Saved) }});
    }

    iCAX::Interaction::CInvocationResult HandleCreateProductTemplate(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext*)
    {
        const auto _Request = DecodeObjectPayload(Request_);
        const auto _BaseID = GetRequiredText(_Request, "baseTemplateId", 240);
        const auto _Name = GetRequiredText(_Request, "name", 120);
        auto _Package = [&]() {
            auto _Loaded = LoadPythonTemplatePackage(ApplicationContext_, _BaseID);
            auto _Runtime = InvokeProductTemplatePackageRuntime(ApplicationContext_, ObjectMap{
                { "action", std::string("inspect-directory") },
                { "sourceDirectory", PathToUTF8(_Loaded.DescriptorPath.parent_path()) }
            });
            ObjectMap _Result = GetRequiredObject(_Runtime, "package");
            _Result["sourceFormat"] = std::string("itpt");
            _Result["baseTemplateId"] = _BaseID;
            _Result["name"] = _Name;
            _Result["description"] = TrimText(GetString(_Request, "description"));
            return _Result;
        }();
        MaterializeUserProductTemplate(ApplicationContext_, _Package);
        _Package.erase("scriptSource");
        _Package.erase("resources");
        _Package["storage"] = std::string("filesystem");
        // Keep the executable descriptor unchanged; the personal display name
        // is metadata only, so exporting the package remains a valid template.
        iCAX::Application::CProductUserDataRecord _Record;
        _Record.FeatureID = kProductTemplateFeatureID;
        _Record.RecordType = kProductTemplateRecordType;
        _Record.SubjectType = kProductTemplateSubjectType;
        _Record.SubjectID = _BaseID;
        _Record.RecordID = UuidToString(iCAX::Data::GenerateNewUUID());
        _Record.OwnerScope = "personal";
        _Record.Payload = Variant(_Package);
        const auto _Saved = GetUserDataStore(ProductContext_)->Put(_Record, 0);
        return MakeResponse(ObjectMap{{ "template", MakeProductTemplatePayload(_Saved) }});
    }

    iCAX::Interaction::CInvocationResult HandleExportProductTemplatePackage(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext*)
    {
        const auto _Request = DecodeObjectPayload(Request_);
        const auto _Scope = GetString(_Request, "scope", "builtin");
        const auto _ID = GetRequiredText(_Request, "id", 240);
        const auto _TargetPath = GetRequiredText(_Request, "targetPath", 32767);
        ObjectMap _Package;
        std::string _SourceDirectory;
        if (_Scope == "personal")
        {
            const auto _RecordID = UuidToString(ParseRequiredUuid(_ID, "id"));
            auto _Record = GetUserDataStore(ProductContext_)->Get(
                kProductTemplateFeatureID, kProductTemplateRecordType, _RecordID);
            if (!_Record || !_Record->Payload.Is<ObjectMap>())
                throw std::invalid_argument("产品模板记录不存在");
            const auto _Stored = _Record->Payload.To<ObjectMap>();
            const auto _StoredDescriptor = GetRequiredObject(_Stored, "descriptor");
            const auto _TemplateID = GetRequiredText(_StoredDescriptor, "id", 240);
            const auto _Loaded = LoadPythonTemplatePackage(ApplicationContext_, _TemplateID);
            const auto _Runtime = InvokeProductTemplatePackageRuntime(ApplicationContext_, ObjectMap{
                { "action", std::string("inspect-directory") },
                { "sourceDirectory", PathToUTF8(_Loaded.DescriptorPath.parent_path()) }
            });
            _Package = GetRequiredObject(_Runtime, "package");
            _SourceDirectory = PathToUTF8(_Loaded.DescriptorPath.parent_path());
        }
        else if (_Scope == "builtin")
        {
            const auto _Loaded = LoadPythonTemplatePackage(ApplicationContext_, _ID);
            _Package["descriptor"] = iCAX::TemplateRuntime::CStandardJsonCodec::Parse(
                ReadTextFile(_Loaded.DescriptorPath));
            _Package["scriptSource"] = ReadTextFile(_Loaded.ScriptPath);
            _SourceDirectory = PathToUTF8(_Loaded.DescriptorPath.parent_path());
        }
        else throw std::invalid_argument("产品模板来源无效");
        ObjectMap _ExportRequest{
            { "action", std::string("export") },
            { "descriptor", GetRequiredObject(_Package, "descriptor") },
            { "scriptSource", GetString(_Package, "scriptSource") },
            { "targetPath", _TargetPath }
        };
        if (const auto _Resources = _Package.find("resources");
            _Resources != _Package.end())
            _ExportRequest["resources"] = _Resources->second;
        if (!_SourceDirectory.empty()) _ExportRequest["sourceDirectory"] = _SourceDirectory;
        const auto _Result = InvokeProductTemplatePackageRuntime(ApplicationContext_, _ExportRequest);
        ObjectMap _Response = _Result;
        _Response["format"] = std::string("itpt");
        return MakeResponse(Variant(_Response));
    }

    iCAX::Interaction::CInvocationResult HandleDeleteProductTemplate(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext*)
    {
        const auto _Request = DecodeObjectPayload(Request_);
        const auto _ID = UuidToString(ParseRequiredUuid(GetRequiredText(_Request, "id"), "id"));
        auto _Store = GetUserDataStore(ProductContext_);
        std::string _TemplateID;
        if (const auto _Record = _Store->Get(kProductTemplateFeatureID, kProductTemplateRecordType, _ID);
            _Record && _Record->Payload.Is<ObjectMap>())
        {
            if (const auto _Descriptor = _Record->Payload.To<ObjectMap>().find("descriptor");
                _Descriptor != _Record->Payload.To<ObjectMap>().end() && _Descriptor->second.Is<ObjectMap>())
                _TemplateID = GetString(_Descriptor->second.To<ObjectMap>(), "id");
        }
        const auto _Deleted = _Store->Delete(
            kProductTemplateFeatureID, kProductTemplateRecordType, _ID,
            GetUInt64(_Request, "revision", 0));
        if (_Deleted && !_TemplateID.empty())
        {
            const auto _Root = ResolveUserTemplateRoot(ApplicationContext_);
            if (!_Root.empty())
            {
                std::error_code _Error;
                std::filesystem::remove_all(
                    _Root / "product" / std::filesystem::path(
                        std::u8string(_TemplateID.begin(), _TemplateID.end())), _Error);
                if (_Error) throw std::runtime_error("无法删除用户产品模板文件：" + _Error.message());
            }
        }
        return MakeResponse(ObjectMap{{ "deleted", _Deleted }});
    }

    iCAX::Interaction::CInvocationResult HandleImportProfileDxf(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext*)
    {
        tube::license::Enforce<113, tube::license::Feature::Design>();
        const auto _Request = DecodeObjectPayload(Request_);
        const auto _SourcePath = GetRequiredText(_Request, "sourcePath", 32767);
        const auto _Extension = [&]() {
            auto _Value = Utf8Path(_SourcePath).extension().string();
            std::transform(_Value.begin(), _Value.end(), _Value.begin(), [](const unsigned char Value_) {
                return static_cast<char>(std::tolower(Value_));
            });
            return _Value;
        }();
        if (_Extension != ".dxf") throw std::invalid_argument("TubeDesigner requires a .dxf file");
        ObjectMap _Response;
        _Response["profile"] = ImportDxfProfile(ApplicationContext_, _SourcePath);
        return MakeResponse(Variant(_Response));
    }

    iCAX::Interaction::CInvocationResult HandleImportProfilePackage(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext*)
    {
        tube::license::Enforce<114, tube::license::Feature::Design>();
        const auto _Request = DecodeObjectPayload(Request_);
        const auto _SourcePath = GetRequiredText(_Request, "sourcePath", 32767);
        const auto _Extension = [&]() {
            auto _Value = Utf8Path(_SourcePath).extension().string();
            std::transform(_Value.begin(), _Value.end(), _Value.begin(), [](const unsigned char Value_) {
                return static_cast<char>(std::tolower(Value_));
            });
            return _Value;
        }();
        if (_Extension != ".ittt")
            throw std::invalid_argument("TubeDesigner requires an .ittt package");
        auto _Package = ImportProfilePackage(ApplicationContext_, _SourcePath, "");
        const auto _Descriptor = GetRequiredObject(_Package, "descriptor");

        iCAX::Application::CProductUserDataRecord _Record;
        _Record.FeatureID = kProfileFeatureID;
        _Record.RecordType = kParametricProfileRecordType;
        _Record.SubjectType = kProfileDefinitionSubjectType;
        _Record.SubjectID = GetRequiredText(_Descriptor, "id", 80);
        _Record.RecordID = UuidToString(iCAX::Data::GenerateNewUUID());
        _Record.OwnerScope = "personal";
        _Record.Payload = Variant(_Package);
        const auto _Saved = GetUserDataStore(ProductContext_)->Put(_Record, 0);
        ObjectMap _Response;
        _Response["profile"] = MakeProfileUserDataPayload(_Saved);
        return MakeResponse(Variant(_Response));
    }

    iCAX::Interaction::CInvocationResult HandleEvaluateProfilePackage(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext*)
    {
        tube::license::Enforce<115, tube::license::Feature::Design>();
        const auto _Request = DecodeObjectPayload(Request_);
        if (_Request.contains("profileRef"))
        {
            const auto _Reference = ParseProfileReference(_Request);
            const auto _Profile = ResolveProfileSnapshot(ApplicationContext_, *GetUserDataStore(ProductContext_),
                _Reference, GetRequiredObject(_Request, "parameters"));
            return MakeResponse(ObjectMap{ { "profile", _Profile }, { "profileRef", ProfileReferencePayload(_Reference) } });
        }
        const auto _RecordID = UuidToString(ParseRequiredUuid(
            GetRequiredText(_Request, "id"), "id"));
        auto _Store = GetUserDataStore(ProductContext_);
        const auto _Record = _Store->Get(
            kProfileFeatureID, kParametricProfileRecordType, _RecordID);
        if (!_Record)
            throw std::invalid_argument("TubeDesigner parametric profile does not exist");
        auto _Package = _Record->Payload.Is<ObjectMap>()
            ? _Record->Payload.To<ObjectMap>()
            : ObjectMap();
        const auto _Values = GetRequiredObject(_Request, "parameters");
        auto _Profile = EvaluateProfilePackage(ApplicationContext_, _Package, _Values);
        _Profile["name"] = GetString(_Package, "name", GetString(_Profile, "name"));
        _Profile["savedProfileId"] = _RecordID;
        ObjectMap _Response;
        _Response["profile"] = _Profile;
        return MakeResponse(Variant(_Response));
    }

    iCAX::Interaction::CInvocationResult HandleGenerateProfilePreview(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        tube::license::Enforce<116, tube::license::Feature::Design>();
        if (!Scene_)
            throw std::invalid_argument("TubeDesigner.GenerateProfilePreview requires a scene");
        const auto _Request = DecodeObjectPayload(Request_);
        const auto _ProfileReference = ParseProfileReference(_Request);
        const auto _Length = GetDouble(_Request, "length", 1000.0);
        if (!std::isfinite(_Length) || _Length < 1.0 || _Length > 100000.0)
            throw std::invalid_argument("profile preview length must be between 1 and 100000 mm");
        ObjectMap _Parameters;
        if (const auto _Iterator = _Request.find("parameters");
            _Iterator != _Request.end() && !_Iterator->second.Is<std::monostate>())
        {
            if (!_Iterator->second.Is<ObjectMap>())
                throw std::invalid_argument("profile preview parameters must be an object");
            _Parameters = _Iterator->second.To<ObjectMap>();
        }
        auto _Profile = ResolveProfileSnapshot(
            ApplicationContext_, *GetUserDataStore(ProductContext_),
            _ProfileReference, _Parameters);
        const auto _Name = GetString(_Profile, "name", "管型");
        const auto _Shape = BuildProfileExtrusion(_Profile, _Length);
        if (_Shape.IsNull())
            throw std::runtime_error("tube profile preview produced no solid");
        const auto _BRep = StoreBRep(
            *Scene_, "tube-designer/profile-preview/" + ProfileResourceIdentity(_ProfileReference),
            _Name + " preview", _Shape);
        const auto _Geometry = iCAX::RenderInteraction::EnsureFrontendGeometryResource(
            Scene_->Resources(), _BRep.URL, iCAX::Render::ERenderGeometryKind::Mesh);
        const auto _Material = EnsureDesignerMaterial(*Scene_);

        ObjectMap _Response;
        _Response["profile"] = _Profile;
        _Response["profileRef"] = ProfileReferencePayload(_ProfileReference);
        _Response["length"] = _Length;
        _Response["geometryResourceId"] = _Geometry.URL;
        _Response["geometryResourceVersion"] = static_cast<unsigned long long>(_Geometry.nVersion);
        _Response["materialResourceId"] = _Material.URL;
        _Response["materialResourceVersion"] = static_cast<unsigned long long>(_Material.nVersion);
        _Response["brepResourceId"] = _BRep.URL;
        _Response["brepResourceVersion"] = static_cast<unsigned long long>(_BRep.nVersion);
        return MakeResponse(Variant(_Response));
    }

    iCAX::Interaction::CInvocationResult HandleExportProfile(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_)
            throw std::invalid_argument("TubeDesigner.ExportProfile requires a scene");
        const auto _Request = DecodeObjectPayload(Request_);
        const auto _ProfileReference = ParseProfileReference(_Request);
        const auto _Format = GetRequiredText(_Request, "format", 16);
        if (_Format != "dxf" && _Format != "step")
            throw std::invalid_argument("profile export format must be dxf or step");
        if (_Format == "step") tube::license::Enforce<303, tube::license::Feature::StepExport>();
        else tube::license::Enforce<103, tube::license::Feature::Design>();
        const auto _Length = GetDouble(_Request, "length", 1000.0);
        if (!std::isfinite(_Length) || _Length < 1.0 || _Length > 100000.0)
            throw std::invalid_argument("profile export length must be between 1 and 100000 mm");
        ObjectMap _Parameters;
        if (const auto _Iterator = _Request.find("parameters");
            _Iterator != _Request.end() && !_Iterator->second.Is<std::monostate>())
        {
            if (!_Iterator->second.Is<ObjectMap>())
                throw std::invalid_argument("profile export parameters must be an object");
            _Parameters = _Iterator->second.To<ObjectMap>();
        }
        auto _Profile = ResolveProfileSnapshot(
            ApplicationContext_, *GetUserDataStore(ProductContext_),
            _ProfileReference, _Parameters);
        const auto _Name = GetString(_Profile, "name", "管型");
        const auto _TargetRoot = Utf8Path(GetRequiredText(_Request, "targetDirectory", 32767));
        std::filesystem::create_directories(_TargetRoot);

        std::filesystem::path _TargetPath;
        if (_Format == "dxf")
        {
            _TargetPath = MakeUniqueExportPath(_TargetRoot, _Name, ".dxf");
            const auto _RuntimeResult = ExportProfileDxf(
                ApplicationContext_, _Profile, Utf8PathText(_TargetPath));
            if (GetString(_RuntimeResult, "path").empty())
                throw std::runtime_error("TubeDesigner DXF export returned no output path");
        }
        else
        {
            std::ostringstream _LengthText;
            _LengthText << std::setprecision(12) << _Length;
            _TargetPath = MakeUniqueExportPath(
                _TargetRoot, _Name + "_L" + _LengthText.str() + "mm", ".step");
            const auto _Shape = BuildProfileExtrusion(_Profile, _Length);
            if (_Shape.IsNull())
                throw std::runtime_error("tube profile STEP export produced no solid");
            const auto _BRep = StoreBRep(
                *Scene_, "tube-designer/profile-export/" + ProfileResourceIdentity(_ProfileReference),
                _Name + " export", _Shape);
            const auto _Result = Scene_->Resources().Export<iCAX::GeometryData::BRepModel>(
                _BRep.URL,
                Utf8PathText(_TargetPath),
                { { "resourceVersion", std::to_string(_BRep.nVersion) } },
                "cad.step");
            if (!_Result.IsOK())
                throw std::runtime_error(
                    _Result.Error.empty() ? "TubeDesigner STEP export failed" : _Result.Error);
        }

        ObjectMap _Response;
        _Response["format"] = _Format;
        _Response["path"] = Utf8PathText(_TargetPath);
        _Response["profileName"] = _Name;
        _Response["profileRef"] = ProfileReferencePayload(_ProfileReference);
        _Response["length"] = _Length;
        return MakeResponse(Variant(_Response));
    }

    iCAX::Interaction::CInvocationResult HandleUpdateProfilePackage(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext*)
    {
        tube::license::Enforce<117, tube::license::Feature::Design>();
        const auto _Request = DecodeObjectPayload(Request_);
        const auto _RecordID = UuidToString(ParseRequiredUuid(
            GetRequiredText(_Request, "id"), "id"));
        auto _Store = GetUserDataStore(ProductContext_);
        const auto _Existing = _Store->Get(
            kProfileFeatureID, kParametricProfileRecordType, _RecordID);
        if (!_Existing)
            throw std::invalid_argument("TubeDesigner parametric profile does not exist");
        auto _Record = *_Existing;
        auto _Package = _Record.Payload.Is<ObjectMap>()
            ? _Record.Payload.To<ObjectMap>()
            : ObjectMap();
        const auto _Values = GetRequiredObject(_Request, "parameters");
        auto _Preview = EvaluateProfilePackage(ApplicationContext_, _Package, _Values);
        const auto _Name = GetRequiredText(_Request, "name", 120);
        _Preview["name"] = _Name;
        _Package["name"] = _Name;
        _Package["defaultParameters"] = GetRequiredObject(_Preview, "parameters");
        _Package["previewProfile"] = _Preview;
        _Record.Payload = Variant(_Package);
        const auto _Saved = _Store->Put(
            _Record, GetUInt64(_Request, "revision", 0));
        ObjectMap _Response;
        _Response["profile"] = MakeProfileUserDataPayload(_Saved);
        return MakeResponse(Variant(_Response));
    }

    iCAX::Interaction::CInvocationResult HandleSaveImportedProfile(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext*)
    {
        tube::license::Enforce<118, tube::license::Feature::Design>();
        const auto _Request = DecodeObjectPayload(Request_);
        const auto _RequestedID = TrimText(GetString(_Request, "id"));
        const auto _RecordID = _RequestedID.empty()
            ? UuidToString(iCAX::Data::GenerateNewUUID())
            : UuidToString(ParseRequiredUuid(_RequestedID, "id"));
        auto _Payload = GetRequiredObject(_Request, "profile");
        if (GetString(_Payload, "kind") != "imported-dxf")
            throw std::invalid_argument(
                "SaveImportedProfile only accepts a frozen DXF profile");
        _Payload["name"] = GetRequiredText(_Request, "name", 120);
        ValidateImportedProfileDefinition(_Payload);

        iCAX::Application::CProductUserDataRecord _Record;
        _Record.FeatureID = kProfileFeatureID;
        _Record.RecordType = kImportedProfileRecordType;
        _Record.SubjectType = kProductSubjectType;
        _Record.SubjectID = ProductContext_->GetProductID();
        _Record.RecordID = _RecordID;
        _Record.OwnerScope = "personal";
        _Record.Payload = Variant(_Payload);
        const auto _Saved = GetUserDataStore(ProductContext_)->Put(
            _Record, GetUInt64(_Request, "revision", 0));
        ObjectMap _Response;
        _Response["profile"] = MakeProfileUserDataPayload(_Saved);
        return MakeResponse(Variant(_Response));
    }

    iCAX::Interaction::CInvocationResult HandleRenameImportedProfile(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext*)
    {
        const auto _Request = DecodeObjectPayload(Request_);
        const auto _RecordID = UuidToString(ParseRequiredUuid(
            GetRequiredText(_Request, "id"), "id"));
        auto _Store = GetUserDataStore(ProductContext_);
        const auto _Existing = FindProfileRecord(*_Store, _RecordID);
        if (!_Existing)
            throw std::invalid_argument("TubeDesigner profile does not exist");

        auto _Record = *_Existing;
        auto _Payload = _Record.Payload.Is<ObjectMap>()
            ? _Record.Payload.To<ObjectMap>()
            : ObjectMap();
        _Payload["name"] = GetRequiredText(_Request, "name", 120);
        _Record.Payload = Variant(_Payload);
        const auto _Saved = _Store->Put(
            _Record, GetUInt64(_Request, "revision", 0));
        ObjectMap _Response;
        _Response["profile"] = MakeProfileUserDataPayload(_Saved);
        return MakeResponse(Variant(_Response));
    }

    iCAX::Interaction::CInvocationResult HandleSaveCustomer(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext*)
    {
        const auto _Request = DecodeObjectPayload(Request_);
        const auto _RequestedID = TrimText(GetString(_Request, "id"));
        const auto _RecordID = _RequestedID.empty()
            ? UuidToString(iCAX::Data::GenerateNewUUID())
            : UuidToString(ParseRequiredUuid(_RequestedID, "id"));
        ObjectMap _Payload;
        _Payload["name"] = GetRequiredText(_Request, "name");
        _Payload["notes"] = TrimText(GetString(_Request, "notes"));

        iCAX::Application::CProductUserDataRecord _Record;
        _Record.FeatureID = kCustomerFeatureID;
        _Record.RecordType = kCustomerRecordType;
        _Record.SubjectType = kProductSubjectType;
        _Record.SubjectID = ProductContext_->GetProductID();
        _Record.RecordID = _RecordID;
        _Record.OwnerScope = "personal";
        _Record.Payload = Variant(_Payload);
        auto _Saved = GetUserDataStore(ProductContext_)->Put(
            _Record, GetUInt64(_Request, "revision", 0));
        ObjectMap _Response;
        _Response["customer"] = MakeUserDataPayload(_Saved);
        return MakeResponse(Variant(_Response));
    }

    iCAX::Interaction::CInvocationResult HandleSaveParameterPreset(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext*)
    {
        const auto _Request = DecodeObjectPayload(Request_);
        const auto _RequestedID = TrimText(GetString(_Request, "id"));
        const auto _RecordID = _RequestedID.empty()
            ? UuidToString(iCAX::Data::GenerateNewUUID())
            : UuidToString(ParseRequiredUuid(_RequestedID, "id"));
        const auto _TemplateID = GetRequiredText(_Request, "templateId", 240);
        auto _CustomerID = TrimText(GetString(_Request, "customerId"));
        auto _Store = GetUserDataStore(ProductContext_);
        if (!_CustomerID.empty())
        {
            _CustomerID = UuidToString(ParseRequiredUuid(_CustomerID, "customerId"));
            if (!_Store->Get(kCustomerFeatureID, kCustomerRecordType, _CustomerID))
                throw std::invalid_argument("TubeDesigner customer does not exist");
        }

        ObjectMap _Payload;
        _Payload["name"] = GetRequiredText(_Request, "name");
        _Payload["templateVersion"] = TrimText(GetString(_Request, "templateVersion"));
        _Payload["values"] = GetRequiredObject(_Request, "values");

        iCAX::Application::CProductUserDataRecord _Record;
        _Record.FeatureID = kTemplateFeatureID;
        _Record.RecordType = kParameterPresetRecordType;
        _Record.SubjectType = kTemplateSubjectType;
        _Record.SubjectID = _TemplateID;
        _Record.RecordID = _RecordID;
        _Record.OwnerScope = "personal";
        _Record.Payload = Variant(_Payload);
        if (!_CustomerID.empty())
        {
            iCAX::Application::CUserDataLink _CustomerLink;
            _CustomerLink.RelationType = kCustomerRelationType;
            _CustomerLink.TargetKind = "user-record";
            _CustomerLink.TargetFeatureID = kCustomerFeatureID;
            _CustomerLink.TargetType = kCustomerRecordType;
            _CustomerLink.TargetID = _CustomerID;
            _Record.Links.emplace_back(std::move(_CustomerLink));
        }
        auto _Saved = _Store->Put(_Record, GetUInt64(_Request, "revision", 0));
        ObjectMap _Response;
        _Response["parameterPreset"] = MakeParameterPresetPayload(_Saved);
        return MakeResponse(Variant(_Response));
    }

    iCAX::Interaction::CInvocationResult DeleteUserDataRecord(
        const iCAX::Interaction::CInvocation& Request_,
        iCAX::Product::IProductContext* ProductContext_,
        const std::string& FeatureID_,
        const std::string& RecordType_,
        const std::string& ResultName_)
    {
        const auto _Request = DecodeObjectPayload(Request_);
        const auto _RecordID = UuidToString(ParseRequiredUuid(
            GetRequiredText(_Request, "id"), "id"));
        const auto _Deleted = GetUserDataStore(ProductContext_)->Delete(
            FeatureID_,
            RecordType_,
            _RecordID,
            GetUInt64(_Request, "revision", 0));
        ObjectMap _Response;
        _Response[ResultName_] = _RecordID;
        _Response["deleted"] = _Deleted;
        return MakeResponse(Variant(_Response));
    }

    iCAX::Interaction::CInvocationResult HandleDeleteCustomer(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext*)
    {
        return DeleteUserDataRecord(
            Request_, ProductContext_, kCustomerFeatureID, kCustomerRecordType, "customerId");
    }

    iCAX::Interaction::CInvocationResult HandleDeleteParameterPreset(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext*)
    {
        return DeleteUserDataRecord(
            Request_,
            ProductContext_,
            kTemplateFeatureID,
            kParameterPresetRecordType,
            "parameterPresetId");
    }

    iCAX::Interaction::CInvocationResult HandleDeleteImportedProfile(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext*)
    {
        return DeleteUserDataRecord(
            Request_, ProductContext_, kProfileFeatureID, kImportedProfileRecordType, "profileId");
    }

    iCAX::Interaction::CInvocationResult HandleDeleteProfile(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext*)
    {
        const auto _Request = DecodeObjectPayload(Request_);
        const auto _RecordID = UuidToString(ParseRequiredUuid(
            GetRequiredText(_Request, "id"), "id"));
        auto _Store = GetUserDataStore(ProductContext_);
        const auto _Existing = FindProfileRecord(*_Store, _RecordID);
        if (!_Existing)
            throw std::invalid_argument("TubeDesigner profile does not exist");
        const auto _Deleted = _Store->Delete(
            kProfileFeatureID,
            _Existing->RecordType,
            _RecordID,
            GetUInt64(_Request, "revision", 0));
        ObjectMap _Response;
        _Response["profileId"] = _RecordID;
        _Response["deleted"] = _Deleted;
        return MakeResponse(Variant(_Response));
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

    iCAX::Interaction::CInvocationResult HandleSetInstanceQuantity(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext*, iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_) throw std::invalid_argument("SetInstanceQuantity requires a scene");
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _ID = ParseRequiredUuid(GetString(_Payload, "productEntityId"), "productEntityId");
        const auto _Quantity = ValidateInstanceQuantity(GetUInt64(_Payload, "quantity", 0));
        auto& _DB = Scene_->Database();
        const auto _Product = GetComponent<CProductInstanceComponent>(_DB.GetEntity(_ID));
        if (!_Product) throw std::invalid_argument("产品实例不存在");
        if (_Product->GetQuantity() == _Quantity)
            return MakeResponse(Variant(BuildSnapshot(*Scene_, ApplicationContext_)));
        for (const auto& [_Entity, _Part] : Collect<CManufacturingPartComponent>(_DB))
            if (_Part->GetProductID() == _ID && _Part->GetQuantityOverride() == 0)
                (void)ProductionQuantity(_Part->GetQuantity(), _Quantity);

        auto _Undo = _DB.BeginUndoCommand("Change product instance quantity");
        auto& _Transaction = _DB.BeginTransaction("Change product instance quantity");
        bool _Committing = false;
        try
        {
            _Transaction.ModifyComponent(_ID, CProductInstanceComponent::S_ClassName,
                {{CProductInstanceComponent::PropertyName_Quantity, PropertyValue(static_cast<unsigned long long>(_Quantity))}});
            const auto _Meta = _DB.GetMetaEntity();
            const auto _Root = GetComponent<CTubeDesignerRootComponent>(_Meta);
            if (_Root)
            {
                auto _Task = _Root->GetNestingTask();
                bool _HasLinkedParts = false;
                if (const auto _References = _Task.find("parts");
                    _References != _Task.end() && _References->second.Is<VariantArray>())
                    for (const auto& _Value : _References->second.To<VariantArray>())
                        if (_Value.Is<ObjectMap>()
                            && GetString(_Value.To<ObjectMap>(), "productEntityId") == UuidToString(_ID))
                        {
                            _HasLinkedParts = true;
                            break;
                        }
                if (_HasLinkedParts)
                {
                    _Task["request"] = ObjectMap();
                    _Task["result"] = ObjectMap();
                    _Task["revision"] = UuidToString(iCAX::Data::GenerateNewUUID());
                    _Transaction.ModifyComponent(
                        _Meta->GetID(), CTubeDesignerRootComponent::S_ClassName,
                        {{CTubeDesignerRootComponent::PropertyName_NestingTask,
                            PropertyValue(_Task)}});
                }
            }
            std::string _Error;
            _Committing = true;
            if (!_DB.CommitTransaction(_Transaction, _Error)) throw std::runtime_error(_Error);
        }
        catch (...)
        {
            if (!_Committing) { try { _DB.CancelTransaction(_Transaction); } catch (...) {} }
            throw;
        }
        _Undo->End();
        return MakeResponse(Variant(BuildSnapshot(*Scene_, ApplicationContext_)));
    }

    iCAX::Interaction::CInvocationResult HandleUpdateManufacturingPart(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext*, iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_ || Request_.Payload.size() > 64 * 1024)
            throw std::invalid_argument("Invalid manufacturing part update request");
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _PartID = ParseRequiredUuid(GetString(_Payload, "partEntityId"), "partEntityId");
        auto& _DB = Scene_->Database();
        const auto _PartEntity = _DB.GetEntity(_PartID);
        const auto _Part = GetComponent<CManufacturingPartComponent>(_PartEntity);
        if (!_Part) throw std::invalid_argument("制造零件不存在");
        const auto _Independent = IsIndependentNestingPart(*_Part);
        if (!_Independent)
            throw std::invalid_argument("产品拆单零件是临时数据，请进入下料区后再修改");

        iCAX::Data::PropertySet _Changes;
        if (_Payload.contains("name"))
        {
            const auto _Name = TrimText(GetString(_Payload, "name"));
            if (_Name.size() > 160 || _Name.find('\0') != std::string::npos)
                throw std::invalid_argument("零件名称不能超过 160 个字符");
            _Changes[CManufacturingPartComponent::PropertyName_Name] = PropertyValue(_Name);
        }
        if (_Payload.contains("material"))
        {
            const auto _Material = TrimText(GetString(_Payload, "material"));
            if (_Material.size() > 240 || _Material.find('\0') != std::string::npos)
                throw std::invalid_argument("材料名称不能超过 240 个字符");
            auto _ItemProperties = _Part->GetItemProperties();
            _ItemProperties["manufacturing.material"] = _Material;
            _Changes[CManufacturingPartComponent::PropertyName_ItemProperties] =
                PropertyValue(_ItemProperties);
        }
        if (_Payload.contains("quantity"))
        {
            const auto _Quantity = ValidateInstanceQuantity(GetUInt64(_Payload, "quantity", 0));
            _Changes[CManufacturingPartComponent::PropertyName_Quantity] =
                PropertyValue(static_cast<unsigned long long>(_Quantity));
            _Changes[CManufacturingPartComponent::PropertyName_QuantityOverride] =
                PropertyValue(0ull);
        }
        if (_Changes.empty()) throw std::invalid_argument("没有可保存的零件修改");

        auto _Undo = _DB.BeginUndoCommand("Update manufacturing part");
        auto& _Transaction = _DB.BeginTransaction("Update manufacturing part");
        bool _Committing = false;
        try
        {
            _Transaction.ModifyComponent(_PartID, CManufacturingPartComponent::S_ClassName, _Changes);
            if (_Independent && _Payload.contains("quantity"))
            {
                const auto _Meta = _DB.GetMetaEntity();
                const auto _Root = GetComponent<CTubeDesignerRootComponent>(_Meta);
                if (_Root)
                {
                    auto _Task = _Root->GetNestingTask();
                    _Task["request"] = ObjectMap();
                    _Task["result"] = ObjectMap();
                    _Task["revision"] = UuidToString(iCAX::Data::GenerateNewUUID());
                    _Transaction.ModifyComponent(
                        _Meta->GetID(), CTubeDesignerRootComponent::S_ClassName,
                        {{CTubeDesignerRootComponent::PropertyName_NestingTask, PropertyValue(_Task)}});
                }
            }
            std::string _Error;
            _Committing = true;
            if (!_DB.CommitTransaction(_Transaction, _Error))
                throw std::runtime_error(_Error.empty() ? "保存零件修改失败" : _Error);
        }
        catch (...)
        {
            if (!_Committing) { try { _DB.CancelTransaction(_Transaction); } catch (...) {} }
            throw;
        }
        _Undo->End();
        return MakeResponse(Variant(BuildSnapshot(*Scene_, ApplicationContext_)));
    }

    iCAX::Interaction::CInvocationResult HandleDeleteManufacturingPart(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext*, iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_ || Request_.Payload.size() > 256 * 1024)
            throw std::invalid_argument("Invalid manufacturing part deletion request");
        const auto _Payload = DecodeObjectPayload(Request_);
        std::vector<iCAX::Data::uuid> _IDs;
        if (const auto _Found = _Payload.find("partEntityIds");
            _Found != _Payload.end())
        {
            if (!_Found->second.Is<VariantArray>())
                throw std::invalid_argument("partEntityIds must be an array");
            for (const auto& _Value : _Found->second.To<VariantArray>())
                _IDs.push_back(ParseRequiredUuid(_Value.To<std::string>(), "partEntityId"));
        }
        else
        {
            _IDs.push_back(ParseRequiredUuid(GetString(_Payload, "partEntityId"), "partEntityId"));
        }
        std::sort(_IDs.begin(), _IDs.end());
        _IDs.erase(std::unique(_IDs.begin(), _IDs.end()), _IDs.end());
        if (_IDs.empty()) throw std::invalid_argument("请选择要删除的零件");

        auto& _DB = Scene_->Database();
        for (const auto& _ID : _IDs)
        {
            const auto _Part = GetComponent<CManufacturingPartComponent>(_DB.GetEntity(_ID));
            if (!_Part) throw std::invalid_argument("制造零件不存在");
            if (IsIndependentNestingPart(*_Part))
                throw std::invalid_argument("下料区零件请使用下料页面的删除操作");
            const auto _Product = GetComponent<CProductInstanceComponent>(_DB.GetEntity(_Part->GetProductID()));
            if (!_Product || _Product->GetActiveGenerationRunID() != _Part->GetGenerationRunID())
                throw std::invalid_argument("只能删除当前拆单结果中的零件");
        }

        auto _Undo = _DB.BeginUndoCommand("Delete manufacturing parts");
        auto& _Transaction = _DB.BeginTransaction("Delete manufacturing parts");
        bool _Committing = false;
        try
        {
            for (const auto& _ID : _IDs)
            {
                for (const auto& [_MemberEntity, _Member] : Collect<CAssemblyMemberComponent>(_DB))
                    if (_Member->GetManufacturingPartID() == _ID)
                        _Transaction.ModifyComponent(
                            _MemberEntity->GetID(), CAssemblyMemberComponent::S_ClassName,
                            {{CAssemblyMemberComponent::PropertyName_ManufacturingPartID, PropertyValue(iCAX::Data::uuid())}});
                _Transaction.DisposeEntity(_ID);
            }
            std::string _Error;
            _Committing = true;
            if (!_DB.CommitTransaction(_Transaction, _Error))
                throw std::runtime_error(_Error.empty() ? "删除零件失败" : _Error);
        }
        catch (...)
        {
            if (!_Committing) { try { _DB.CancelTransaction(_Transaction); } catch (...) {} }
            throw;
        }
        _Undo->End();
        return MakeResponse(Variant(BuildSnapshot(*Scene_, ApplicationContext_)));
    }

    double RequiredSketchNumber(const ObjectMap& Entity_, const std::string& Name_)
    {
        const auto _Iterator = Entity_.find(Name_);
        if (_Iterator == Entity_.end())
            throw std::invalid_argument("TubeDesigner sketch entity is missing " + Name_);
        const auto _Value = ToDouble(_Iterator->second, Name_);
        if (!std::isfinite(_Value))
            throw std::invalid_argument("TubeDesigner sketch entity has a non-finite " + Name_);
        return _Value;
    }

    void ValidateSketchCoordinate(
        const double Value_, const double Maximum_, const std::string& Name_)
    {
        const auto _Tolerance = std::max(1.0e-6, Maximum_ * 1.0e-9);
        if (Value_ < -_Tolerance || Value_ > Maximum_ + _Tolerance)
            throw std::invalid_argument("TubeDesigner sketch entity is outside the member bounds: " + Name_);
    }

    std::pair<double, double> ValidateSketchPoint(
        const Variant& Value_, const double Length_, const double FaceHeight_,
        const std::string& Name_)
    {
        if (!Value_.Is<VariantArray>())
            throw std::invalid_argument("TubeDesigner sketch point must be an array: " + Name_);
        const auto _Coordinates = Value_.To<VariantArray>();
        if (_Coordinates.size() != 2)
            throw std::invalid_argument("TubeDesigner sketch point must contain x and y: " + Name_);
        const auto _X = ToDouble(_Coordinates[0], Name_ + ".x");
        const auto _Y = ToDouble(_Coordinates[1], Name_ + ".y");
        if (!std::isfinite(_X) || !std::isfinite(_Y))
            throw std::invalid_argument("TubeDesigner sketch point must be finite: " + Name_);
        ValidateSketchCoordinate(_X, Length_, Name_ + ".x");
        ValidateSketchCoordinate(_Y, FaceHeight_, Name_ + ".y");
        return { _X, _Y };
    }

    ObjectMap ValidateSideSketch(const ObjectMap& Sketch_, const double ExpectedLength_)
    {
        if (GetString(Sketch_, "schema") != "icax.tube-sketch"
            || GetUInt64(Sketch_, "schemaVersion", 0) != 1)
        {
            throw std::invalid_argument("unsupported TubeDesigner sketch schema");
        }
        if (GetString(Sketch_, "kind") != "side")
            throw std::invalid_argument("TubeDesigner project sketches must be side sketches");
        const auto _Entities = Sketch_.find("entities");
        if (_Entities == Sketch_.end() || !_Entities->second.Is<VariantArray>())
            throw std::invalid_argument("TubeDesigner sketch requires an entity array");
        const auto _EntityValues = _Entities->second.To<VariantArray>();
        if (_EntityValues.size() > 5000)
            throw std::invalid_argument("TubeDesigner sketch entity count is invalid");
        const auto _Length = GetDouble(Sketch_, "length", 0.0);
        const auto _FaceHeight = GetDouble(Sketch_, "faceHeight", 0.0);
        if (!std::isfinite(_Length) || _Length <= 0.0 || _Length > 1.0e7
            || !std::isfinite(_FaceHeight) || _FaceHeight <= 0.0 || _FaceHeight > 1.0e6)
        {
            throw std::invalid_argument("TubeDesigner side sketch bounds are invalid");
        }
        const auto _LengthTolerance = std::max(1.0e-6, ExpectedLength_ * 1.0e-7);
        if (!std::isfinite(ExpectedLength_) || ExpectedLength_ <= 0.0
            || std::abs(_Length - ExpectedLength_) > _LengthTolerance)
        {
            throw std::invalid_argument("TubeDesigner side sketch does not match the current member length");
        }
        if (GetString(Sketch_, "unit") != "mm")
            throw std::invalid_argument("TubeDesigner side sketch unit must be mm");

        std::unordered_set<std::string> _IDs;
        std::size_t _PointCount = 0;
        for (const auto& _Value : _EntityValues)
        {
            if (!_Value.Is<ObjectMap>())
                throw std::invalid_argument("TubeDesigner sketch entities must be objects");
            const auto _Entity = _Value.To<ObjectMap>();
            const auto _Kind = GetRequiredText(_Entity, "kind", 32);
            if (_Kind != "line" && _Kind != "polyline" && _Kind != "rectangle"
                && _Kind != "circle" && _Kind != "ellipse" && _Kind != "circleArc"
                && _Kind != "ellipseArc" && _Kind != "path" && _Kind != "arc"
                && _Kind != "spline" && _Kind != "freehand" && _Kind != "text")
            {
                throw std::invalid_argument("TubeDesigner sketch entity kind is not supported");
            }
            const auto _ID = GetRequiredText(_Entity, "id", 160);
            if (!_IDs.insert(_ID).second)
                throw std::invalid_argument("TubeDesigner sketch entity IDs must be unique");

            if (_Kind == "line")
            {
                const auto _X1 = RequiredSketchNumber(_Entity, "x1");
                const auto _Y1 = RequiredSketchNumber(_Entity, "y1");
                const auto _X2 = RequiredSketchNumber(_Entity, "x2");
                const auto _Y2 = RequiredSketchNumber(_Entity, "y2");
                ValidateSketchCoordinate(_X1, _Length, "x1");
                ValidateSketchCoordinate(_X2, _Length, "x2");
                ValidateSketchCoordinate(_Y1, _FaceHeight, "y1");
                ValidateSketchCoordinate(_Y2, _FaceHeight, "y2");
                if (std::hypot(_X2 - _X1, _Y2 - _Y1) <= 1.0e-6)
                    throw std::invalid_argument("TubeDesigner sketch line cannot have zero length");
            }
            else if (_Kind == "rectangle")
            {
                const auto _X = RequiredSketchNumber(_Entity, "x");
                const auto _Y = RequiredSketchNumber(_Entity, "y");
                const auto _Width = RequiredSketchNumber(_Entity, "width");
                const auto _Height = RequiredSketchNumber(_Entity, "height");
                if (_Width <= 1.0e-6 || _Height <= 1.0e-6)
                    throw std::invalid_argument("TubeDesigner sketch rectangle dimensions must be positive");
                ValidateSketchCoordinate(_X, _Length, "x");
                ValidateSketchCoordinate(_Y, _FaceHeight, "y");
                ValidateSketchCoordinate(_X + _Width, _Length, "x + width");
                ValidateSketchCoordinate(_Y + _Height, _FaceHeight, "y + height");
            }
            else if (_Kind == "circle")
            {
                const auto _X = RequiredSketchNumber(_Entity, "cx");
                const auto _Y = RequiredSketchNumber(_Entity, "cy");
                const auto _Radius = RequiredSketchNumber(_Entity, "radius");
                if (_Radius <= 1.0e-6)
                    throw std::invalid_argument("TubeDesigner sketch circle radius must be positive");
                ValidateSketchCoordinate(_X - _Radius, _Length, "cx - radius");
                ValidateSketchCoordinate(_X + _Radius, _Length, "cx + radius");
                ValidateSketchCoordinate(_Y - _Radius, _FaceHeight, "cy - radius");
                ValidateSketchCoordinate(_Y + _Radius, _FaceHeight, "cy + radius");
            }
            else if (_Kind == "ellipse")
            {
                const auto _X = RequiredSketchNumber(_Entity, "cx");
                const auto _Y = RequiredSketchNumber(_Entity, "cy");
                const auto _RadiusX = RequiredSketchNumber(_Entity, "radiusX");
                const auto _RadiusY = RequiredSketchNumber(_Entity, "radiusY");
                const auto _Rotation = GetDouble(_Entity, "rotation", 0.0);
                if (_RadiusX <= 1.0e-6 || _RadiusY <= 1.0e-6)
                    throw std::invalid_argument("TubeDesigner sketch ellipse radii must be positive");
                const auto _Cosine = std::cos(_Rotation);
                const auto _Sine = std::sin(_Rotation);
                const auto _ExtentX = std::hypot(_RadiusX * _Cosine, _RadiusY * _Sine);
                const auto _ExtentY = std::hypot(_RadiusX * _Sine, _RadiusY * _Cosine);
                ValidateSketchCoordinate(_X - _ExtentX, _Length, "cx - ellipse extent");
                ValidateSketchCoordinate(_X + _ExtentX, _Length, "cx + ellipse extent");
                ValidateSketchCoordinate(_Y - _ExtentY, _FaceHeight, "cy - ellipse extent");
                ValidateSketchCoordinate(_Y + _ExtentY, _FaceHeight, "cy + ellipse extent");
            }
            else if (_Kind == "circleArc" || _Kind == "ellipseArc")
            {
                const auto _X = RequiredSketchNumber(_Entity, "cx");
                const auto _Y = RequiredSketchNumber(_Entity, "cy");
                const auto _RadiusX = _Kind == "circleArc"
                    ? RequiredSketchNumber(_Entity, "radius")
                    : RequiredSketchNumber(_Entity, "radiusX");
                const auto _RadiusY = _Kind == "circleArc"
                    ? _RadiusX : RequiredSketchNumber(_Entity, "radiusY");
                const auto _Sweep = RequiredSketchNumber(_Entity, "sweep");
                if (_RadiusX <= 1.0e-6 || _RadiusY <= 1.0e-6
                    || std::abs(_Sweep) <= 1.0e-9)
                    throw std::invalid_argument("TubeDesigner sketch arc parameters are invalid");
                // The conservative bounding box is intentional: an arc may
                // later be edited into a full loop, so validation must not
                // accept a point which can never be mapped to the rectangle.
                const auto _Extent = std::max(_RadiusX, _RadiusY);
                ValidateSketchCoordinate(_X - _Extent, _Length, "cx - arc extent");
                ValidateSketchCoordinate(_X + _Extent, _Length, "cx + arc extent");
                ValidateSketchCoordinate(_Y - _Extent, _FaceHeight, "cy - arc extent");
                ValidateSketchCoordinate(_Y + _Extent, _FaceHeight, "cy + arc extent");
            }
            else if (_Kind == "path")
            {
                const auto _Segments = _Entity.find("segments");
                if (_Segments == _Entity.end() || !_Segments->second.Is<VariantArray>()
                    || _Segments->second.To<VariantArray>().empty()
                    || _Segments->second.To<VariantArray>().size() > 10000)
                    throw std::invalid_argument("TubeDesigner sketch path requires segments");
                for (const auto& _SegmentValue : _Segments->second.To<VariantArray>())
                {
                    if (!_SegmentValue.Is<ObjectMap>())
                        throw std::invalid_argument("TubeDesigner sketch path segments must be objects");
                    const auto _Segment = _SegmentValue.To<ObjectMap>();
                    const auto _SegmentKind = GetRequiredText(_Segment, "kind", 32);
                    if (_SegmentKind == "line")
                    {
                        const auto _X1 = RequiredSketchNumber(_Segment, "x1");
                        const auto _Y1 = RequiredSketchNumber(_Segment, "y1");
                        const auto _X2 = RequiredSketchNumber(_Segment, "x2");
                        const auto _Y2 = RequiredSketchNumber(_Segment, "y2");
                        ValidateSketchCoordinate(_X1, _Length, "path.x1");
                        ValidateSketchCoordinate(_X2, _Length, "path.x2");
                        ValidateSketchCoordinate(_Y1, _FaceHeight, "path.y1");
                        ValidateSketchCoordinate(_Y2, _FaceHeight, "path.y2");
                    }
                    else if (_SegmentKind == "bezier")
                    {
                        const auto _Points = _Segment.find("points");
                        if (_Points == _Segment.end() || !_Points->second.Is<VariantArray>()
                            || _Points->second.To<VariantArray>().size() < 2)
                            throw std::invalid_argument("TubeDesigner sketch bezier segment requires points");
                        for (const auto& _Point : _Points->second.To<VariantArray>())
                            (void)ValidateSketchPoint(_Point, _Length, _FaceHeight, "path.bezier");
                    }
                    else if (_SegmentKind == "circleArc" || _SegmentKind == "ellipseArc")
                    {
                        const auto _X = RequiredSketchNumber(_Segment, "cx");
                        const auto _Y = RequiredSketchNumber(_Segment, "cy");
                        const auto _RadiusX = _SegmentKind == "circleArc"
                            ? RequiredSketchNumber(_Segment, "radius")
                            : RequiredSketchNumber(_Segment, "radiusX");
                        const auto _RadiusY = _SegmentKind == "circleArc"
                            ? _RadiusX : RequiredSketchNumber(_Segment, "radiusY");
                        const auto _Start = RequiredSketchNumber(_Segment, "startAngle");
                        const auto _Sweep = RequiredSketchNumber(_Segment, "sweep");
                        if (_RadiusX <= 1.0e-6 || _RadiusY <= 1.0e-6
                            || std::abs(_Sweep) <= 1.0e-9)
                            throw std::invalid_argument("TubeDesigner sketch path arc parameters are invalid");
                        (void)_Start;
                        const auto _Extent = std::max(_RadiusX, _RadiusY);
                        ValidateSketchCoordinate(_X - _Extent, _Length, "path arc x");
                        ValidateSketchCoordinate(_X + _Extent, _Length, "path arc x");
                        ValidateSketchCoordinate(_Y - _Extent, _FaceHeight, "path arc y");
                        ValidateSketchCoordinate(_Y + _Extent, _FaceHeight, "path arc y");
                    }
                    else
                    {
                        throw std::invalid_argument("TubeDesigner sketch path segment kind is not supported");
                    }
                }
            }
            else if (_Kind == "text")
            {
                ValidateSketchCoordinate(RequiredSketchNumber(_Entity, "x"), _Length, "x");
                ValidateSketchCoordinate(RequiredSketchNumber(_Entity, "y"), _FaceHeight, "y");
                (void)GetRequiredText(_Entity, "value", 2000);
            }
            else
            {
                const auto _Points = _Entity.find("points");
                if (_Points == _Entity.end() || !_Points->second.Is<VariantArray>())
                    throw std::invalid_argument("TubeDesigner sketch curve requires points");
                const auto _Values = _Points->second.To<VariantArray>();
                const auto _Minimum = (_Kind == "arc" || _Kind == "spline") ? 3ull : 2ull;
                if (_Values.size() < _Minimum || _Values.size() > 10000)
                    throw std::invalid_argument("TubeDesigner sketch curve point count is invalid");
                _PointCount += _Values.size();
                if (_PointCount > 100000)
                    throw std::invalid_argument("TubeDesigner sketch contains too many curve points");
                auto _Previous = ValidateSketchPoint(_Values.front(), _Length, _FaceHeight, "points[0]");
                double _CurveLength = 0.0;
                for (std::size_t _Index = 1; _Index < _Values.size(); ++_Index)
                {
                    const auto _Current = ValidateSketchPoint(_Values[_Index], _Length, _FaceHeight,
                        "points[" + std::to_string(_Index) + "]");
                    _CurveLength += std::hypot(_Current.first - _Previous.first, _Current.second - _Previous.second);
                    _Previous = _Current;
                }
                if (_CurveLength <= 1.0e-6)
                    throw std::invalid_argument("TubeDesigner sketch curve cannot have zero length");
            }

            if (const auto _Closed = _Entity.find("closed");
                _Closed != _Entity.end() && !_Closed->second.Is<bool>())
            {
                throw std::invalid_argument("TubeDesigner sketch closed flag must be boolean");
            }
        }
        return Sketch_;
    }

    iCAX::Interaction::CInvocationResult HandleSaveSketch(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_) throw std::invalid_argument("TubeDesigner.SaveSketch requires a scene");
        tube::license::Enforce<102, tube::license::Feature::Design>();
        const auto _Payload = DecodeObjectPayload(Request_);
        auto& _Repository = Scene_->Database();
        const auto _Meta = _Repository.GetMetaEntity();
        const auto _Root = GetComponent<CTubeDesignerRootComponent>(_Meta);
        const auto _RequestedProductID = GetString(_Payload, "productEntityId");
        const auto _ProductID = !_RequestedProductID.empty()
            ? ParseRequiredUuid(_RequestedProductID, "productEntityId")
            : (_Root ? _Root->GetActiveProductID() : iCAX::Data::uuid());
        if (_ProductID.is_nil()) throw std::invalid_argument("select a TubeDesigner product instance");
        const auto _ProductEntity = _Repository.GetEntity(_ProductID);
        const auto _Product = GetComponent<CProductInstanceComponent>(_ProductEntity);
        if (!_Product) throw std::invalid_argument("TubeDesigner product instance does not exist");
        const auto _MemberID = ParseRequiredUuid(
            GetString(_Payload, "targetMemberId"), "targetMemberId");
        const auto _MemberEntity = _Repository.GetEntity(_MemberID);
        const auto _Member = GetComponent<CAssemblyMemberComponent>(_MemberEntity);
        if (!_Member || _Member->GetProductID() != _ProductID)
            throw std::invalid_argument("TubeDesigner sketch target member does not belong to the product");
        if (!IsTubeManufacturingPart(_Member->GetItemProperties()))
            throw std::invalid_argument("二维包覆只支持管材零件");

        auto _Sketch = ValidateSideSketch(GetRequiredObject(_Payload, "sketch"), _Member->GetLength());
        _Sketch["targetMemberId"] = UuidToString(_MemberID);
        _Sketch["targetMemberKey"] = _Member->GetStableKey();
        auto _Sketches = _Product->GetSketches();
        _Sketches["schema"] = std::string("icax.tube-sketch-set");
        _Sketches["schemaVersion"] = 1ull;
        ObjectMap _Side;
        if (const auto _Existing = _Sketches.find("side");
            _Existing != _Sketches.end() && _Existing->second.Is<ObjectMap>())
        {
            _Side = _Existing->second.To<ObjectMap>();
        }
        for (auto _Iterator = _Side.begin(); _Iterator != _Side.end();)
        {
            const auto _MatchesID = _Iterator->first == UuidToString(_MemberID);
            const auto _MatchesStableKey = !_Member->GetStableKey().empty()
                && _Iterator->second.Is<ObjectMap>()
                && GetString(_Iterator->second.To<ObjectMap>(), "targetMemberKey") == _Member->GetStableKey();
            _Iterator = _MatchesID || _MatchesStableKey ? _Side.erase(_Iterator) : std::next(_Iterator);
        }
        if (!_Sketch.at("entities").To<VariantArray>().empty())
            _Side[UuidToString(_MemberID)] = _Sketch;
        if (_Side.empty()) _Sketches.erase("side");
        else _Sketches["side"] = _Side;

        // Update the visible product member in the same save operation.  The
        // product sketch remains the source of truth, while this member keeps
        // a frozen pre-sketch BRep so replacing/removing a trajectory never
        // cuts the previous result a second time.
        const auto _PreviewResourceID = _Member->GetPreviewGeometryResourceID();
        const auto _PreviewResourceVersion = _Member->GetPreviewGeometryResourceVersion();
        if (_PreviewResourceID.empty() || _PreviewResourceVersion == 0)
            throw std::runtime_error("产品管件没有可用的预览几何");
        const auto _CurrentPreview = Scene_->Resources().Get<iCAX::GeometryData::BRepModel>(
            _PreviewResourceID, _PreviewResourceVersion);
        if (!_CurrentPreview)
            throw std::runtime_error("产品管件预览几何不可用，请重新生成产品");

        const auto _Removing = _Sketch.at("entities").To<VariantArray>().empty();
        auto _MemberProperties = _Member->GetItemProperties();
        auto _BaseResourceID = GetString(
            _MemberProperties, "tubeDesigner.sideSketchBaseResourceId");
        auto _BaseResourceVersion = GetUInt64(
            _MemberProperties, "tubeDesigner.sideSketchBaseResourceVersion", 0);
        if (_BaseResourceID.empty() || _BaseResourceVersion == 0)
        {
            _BaseResourceID = Scene_->Resources().MakeNamedResourceURL(
                "tube-designer/product/" + UuidToString(_ProductID)
                + "/item/" + _Member->GetStableKey() + "/side-sketch-base");
            const auto _Base = StorePreparedBRep(
                *Scene_, _BaseResourceID, "产品二维包覆基准",
                iCAX::GeometryData::BRepModel(*_CurrentPreview));
            _BaseResourceID = _Base.URL;
            _BaseResourceVersion = _Base.nVersion;
        }
        const auto _BaseBRep = Scene_->Resources().Get<iCAX::GeometryData::BRepModel>(
            _BaseResourceID, _BaseResourceVersion);
        if (!_BaseBRep)
            throw std::runtime_error("产品二维包覆基准几何版本不可用，请重新生成产品");
        const auto _BaseBuild = iCAX::OpenCascade::BuildOpenCascadeShape(*_BaseBRep);
        if (!_BaseBuild.bOK || _BaseBuild.Shape.IsNull())
            throw std::runtime_error("无法读取产品二维包覆基准几何");

        TopoDS_Shape _PreviewShape = _BaseBuild.Shape;
        STubeSideSketchResult _ApplyResult;
        if (!_Removing)
        {
            _ApplyResult = ApplyTubeSideSketch(_BaseBuild.Shape, _Sketch);
            if (!_ApplyResult.bOK || _ApplyResult.Shape.IsNull())
                throw std::runtime_error(_ApplyResult.Diagnostic.empty()
                    ? "产品侧面草图没有生成有效的切除结果" : _ApplyResult.Diagnostic);
            _PreviewShape = _ApplyResult.Shape;
        }
        _MemberProperties["tubeDesigner.sideSketchBaseResourceId"] = _BaseResourceID;
        _MemberProperties["tubeDesigner.sideSketchBaseResourceVersion"] = _BaseResourceVersion;
        if (_Removing)
        {
            _MemberProperties.erase("tubeDesigner.sideSketchApplyMethod");
            _MemberProperties.erase("tubeDesigner.sideSketchDiagnostic");
            _MemberProperties.erase("tubeDesigner.sideSketchClosedLoopCount");
            _MemberProperties.erase("tubeDesigner.sideSketchOpenTrajectoryCount");
        }
        else
        {
            _MemberProperties["tubeDesigner.sideSketchApplyMethod"] = _ApplyResult.Method;
            _MemberProperties["tubeDesigner.sideSketchDiagnostic"] = _ApplyResult.Diagnostic;
            _MemberProperties["tubeDesigner.sideSketchClosedLoopCount"] =
                static_cast<unsigned long long>(_ApplyResult.ClosedLoopCount);
            _MemberProperties["tubeDesigner.sideSketchOpenTrajectoryCount"] =
                static_cast<unsigned long long>(_ApplyResult.OpenTrajectoryCount);
        }
        const auto _NewPreviewResource = StorePunchBRep(
            *Scene_, _PreviewResourceID, _Member->GetName(), _PreviewShape, Request_, false);
        const auto _NewPreviewMesh = iCAX::RenderInteraction::EnsureFrontendGeometryResource(
            Scene_->Resources(), _NewPreviewResource.URL, iCAX::Render::ERenderGeometryKind::Mesh);

        auto _Undo = _Repository.BeginUndoCommand("Save TubeDesigner side sketch");
        auto& _Transaction = _Repository.BeginTransaction("Update TubeDesigner side sketch");
        bool _CommitStarted = false;
        try
        {
            _Transaction.ModifyComponent(
                _ProductID, CProductInstanceComponent::S_ClassName, {
                    { CProductInstanceComponent::PropertyName_Sketches, PropertyValue(_Sketches) }
                });
            QueueUpsertComponent(
                _Transaction, _MemberEntity, _MemberID,
                CAssemblyMemberComponent::S_ClassName, {
                    { CAssemblyMemberComponent::PropertyName_PreviewGeometryResourceID,
                        PropertyValue(_NewPreviewResource.URL) },
                    { CAssemblyMemberComponent::PropertyName_PreviewGeometryResourceVersion,
                        PropertyValue(_NewPreviewResource.nVersion) },
                    { CAssemblyMemberComponent::PropertyName_ItemProperties,
                        PropertyValue(_MemberProperties) }
                });
            QueueUpsertComponent(
                _Transaction, _MemberEntity, _MemberID,
                iCAX::RenderInteraction::CRenderInstanceComponent::S_ClassName, {
                    { iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_GeometryResourceID,
                        PropertyValue(_NewPreviewMesh.URL) },
                    { iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_GeometryResourceVersion,
                        PropertyValue(_NewPreviewMesh.nVersion) }
                });
            std::string _Error;
            _CommitStarted = true;
            if (!_Repository.CommitTransaction(_Transaction, _Error))
                throw std::runtime_error(_Error.empty()
                    ? "TubeDesigner failed to save side sketch" : _Error);
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

    iCAX::Interaction::CInvocationResult HandleSavePartSketch(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_ || Request_.Payload.size() > 2 * 1024 * 1024)
            throw std::invalid_argument("Invalid manufacturing part sketch request");
        tube::license::Enforce<403, tube::license::Feature::Production>();
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _PartID = ParseRequiredUuid(
            GetString(_Payload, "partEntityId"), "partEntityId");
        auto& _Repository = Scene_->Database();
        const auto _Part = GetComponent<CManufacturingPartComponent>(
            _Repository.GetEntity(_PartID));
        if (!_Part || !IsIndependentNestingPart(*_Part))
            throw std::invalid_argument("只能直接保存下料区中的独立零件草图");
        if (!IsTubeManufacturingPart(_Part->GetItemProperties()))
            throw std::invalid_argument("二维包覆只支持管材零件");
        const auto _ExpectedVersion = GetUInt64(
            _Payload, "resourceVersion", _Part->GetManufacturingGeometryResourceVersion());
        if (_ExpectedVersion != _Part->GetManufacturingGeometryResourceVersion())
            throw std::runtime_error("零件几何已经变化，请重新打开二维草图");

        RestoreNestingResources(*Scene_);
        auto _Sketch = ValidateSideSketch(
            GetRequiredObject(_Payload, "sketch"), _Part->GetLength());
        _Sketch["targetPartId"] = UuidToString(_PartID);
        auto _ItemProperties = _Part->GetItemProperties();

        const auto _ResourceID = _Part->GetManufacturingGeometryResourceID();
        const auto _ResourceVersion = _Part->GetManufacturingGeometryResourceVersion();
        if (_ResourceID.empty() || _ResourceVersion == 0)
            throw std::runtime_error("零件没有可用的最终 BRep 几何");
        const auto _CurrentBRep = Scene_->Resources().Get<iCAX::GeometryData::BRepModel>(
            _ResourceID, _ResourceVersion);
        if (!_CurrentBRep)
            throw std::runtime_error("零件最终 BRep 几何不可用，请重新加入下料");

        // Always rebuild from the frozen pre-sketch shape.  A later edit is a
        // replacement of the trajectory, not another cut on top of the last
        // result; this is the same immutable-base rule used by the punch
        // wizard.
        auto _BaseResourceID = GetString(
            _ItemProperties, "tubeDesigner.sideSketchBaseResourceId");
        auto _BaseResourceVersion = GetUInt64(
            _ItemProperties, "tubeDesigner.sideSketchBaseResourceVersion", 0);
        if (_BaseResourceID.empty() || _BaseResourceVersion == 0)
        {
            _BaseResourceID = Scene_->Resources().MakeNamedResourceURL(
                "tube-designer/nesting/side-sketch-base/" + UuidToString(_PartID));
            const auto _Base = StorePreparedBRep(
                *Scene_, _BaseResourceID, "二维包覆编辑基准",
                iCAX::GeometryData::BRepModel(*_CurrentBRep));
            _BaseResourceID = _Base.URL;
            _BaseResourceVersion = _Base.nVersion;
        }
        const auto _BaseBRep = Scene_->Resources().Get<iCAX::GeometryData::BRepModel>(
            _BaseResourceID, _BaseResourceVersion);
        if (!_BaseBRep)
            throw std::runtime_error("二维包覆基准几何版本不可用，请重新加入下料");
        const auto _BaseBuild = iCAX::OpenCascade::BuildOpenCascadeShape(*_BaseBRep);
        if (!_BaseBuild.bOK || _BaseBuild.Shape.IsNull())
            throw std::runtime_error("无法读取二维包覆基准几何");

        const auto _Removing = _Sketch.at("entities").To<VariantArray>().empty();
        TopoDS_Shape _Shape = _BaseBuild.Shape;
        STubeSideSketchResult _ApplyResult;
        if (!_Removing)
        {
            _ApplyResult = ApplyTubeSideSketch(_BaseBuild.Shape, _Sketch);
            if (!_ApplyResult.bOK || _ApplyResult.Shape.IsNull())
                throw std::runtime_error(_ApplyResult.Diagnostic.empty()
                    ? "二维包覆没有生成有效的切除结果" : _ApplyResult.Diagnostic);
            _Shape = _ApplyResult.Shape;
        }
        if (_Removing)
            _ItemProperties.erase("tubeDesigner.sideSketch");
        else
            _ItemProperties["tubeDesigner.sideSketch"] = _Sketch;
        _ItemProperties["tubeDesigner.sideSketchBaseResourceId"] = _BaseResourceID;
        _ItemProperties["tubeDesigner.sideSketchBaseResourceVersion"] = _BaseResourceVersion;
        if (!_Removing)
        {
            _ItemProperties["tubeDesigner.sideSketchApplyMethod"] = _ApplyResult.Method;
            _ItemProperties["tubeDesigner.sideSketchDiagnostic"] = _ApplyResult.Diagnostic;
            _ItemProperties["tubeDesigner.sideSketchClosedLoopCount"] =
                static_cast<unsigned long long>(_ApplyResult.ClosedLoopCount);
            _ItemProperties["tubeDesigner.sideSketchOpenTrajectoryCount"] =
                static_cast<unsigned long long>(_ApplyResult.OpenTrajectoryCount);
        }

        const auto _PresentationName = _Part->GetName().empty()
            ? _Part->GetPartNumber() : _Part->GetName();
        const auto _NewResource = StorePunchBRep(
            *Scene_, _ResourceID, _PresentationName, _Shape, Request_, false);
        ReportExportProgress(Request_, "mesh", 0, 1, "正在生成显示网格");
        const auto _Thumbnail = iCAX::RenderInteraction::EnsureFrontendGeometryResource(
            Scene_->Resources(), _NewResource.URL, iCAX::Render::ERenderGeometryKind::Mesh);
        const auto _Measurement = MeasureFinalPartGeometry(
            _Shape, _NewResource.URL, _NewResource.nVersion);
        const auto _EnvelopeLength = GetDouble(ShapeBounds(_Shape), "width", _Part->GetLength());
        auto _UpdatedProperties = _ItemProperties;
        auto _UpdatedMeasurement = _Measurement;
        _UpdatedMeasurement["nestingEnvelopeLength"] = _EnvelopeLength;
        _UpdatedMeasurement["normalizedAxis"] = std::string("+X");
        _UpdatedProperties["manufacturing.geometryMeasurement"] = _UpdatedMeasurement;

        auto _Undo = _Repository.BeginUndoCommand("Save manufacturing part side sketch");
        auto& _Transaction = _Repository.BeginTransaction(
            "Update manufacturing part side sketch");
        bool _CommitStarted = false;
        try
        {
            _Transaction.ModifyComponent(
                _PartID, CManufacturingPartComponent::S_ClassName, {
                    { CManufacturingPartComponent::PropertyName_Length,
                        PropertyValue(_EnvelopeLength) },
                    { CManufacturingPartComponent::PropertyName_ManufacturingGeometryResourceID,
                        PropertyValue(_NewResource.URL) },
                    { CManufacturingPartComponent::PropertyName_ManufacturingGeometryResourceVersion,
                        PropertyValue(_NewResource.nVersion) },
                    { CManufacturingPartComponent::PropertyName_ThumbnailGeometryResourceID,
                        PropertyValue(_Thumbnail.URL) },
                    { CManufacturingPartComponent::PropertyName_ThumbnailGeometryResourceVersion,
                        PropertyValue(_Thumbnail.nVersion) },
                    { CManufacturingPartComponent::PropertyName_ItemProperties,
                        PropertyValue(_UpdatedProperties) },
                    { CManufacturingPartComponent::PropertyName_Status,
                        PropertyValue(std::string("Ready")) }
                });
            const auto _Meta = _Repository.GetMetaEntity();
            const auto _Root = GetComponent<CTubeDesignerRootComponent>(_Meta);
            if (_Root)
            {
                auto _Task = _Root->GetNestingTask();
                _Task["request"] = ObjectMap();
                _Task["result"] = ObjectMap();
                _Task["revision"] = UuidToString(iCAX::Data::GenerateNewUUID());
                _Transaction.ModifyComponent(
                    _Meta->GetID(), CTubeDesignerRootComponent::S_ClassName, {
                        { CTubeDesignerRootComponent::PropertyName_NestingTask,
                            PropertyValue(_Task) }
                    });
            }
            std::string _Error;
            _CommitStarted = true;
            if (!_Repository.CommitTransaction(_Transaction, _Error))
                throw std::runtime_error(_Error.empty()
                    ? "保存零件二维草图失败" : _Error);
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
        SyncNestingPersistence(*Scene_);
        return MakeResponse(Variant(BuildSnapshot(*Scene_, ApplicationContext_, false)));
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

    iCAX::OpenCascade::SNeutralModelEvaluation EvaluateOutputGeometry(
        const iCAX::TemplateRuntime::SNeutralModel& Model_,
        const iCAX::TemplateRuntime::SOutputSet& Output_)
    {
        std::vector<std::string> _GeometryKeys;
        _GeometryKeys.reserve(Output_.ItemKeys.size());
        for (const auto& _ItemKey : Output_.ItemKeys)
        {
            const auto& _Item = FindModelItem(Model_, _ItemKey);
            const auto _Representation = _Item.Representations.find(Output_.Purpose);
            if (_Representation == _Item.Representations.end())
                throw std::runtime_error("neutral model item has no " + Output_.Purpose
                    + " representation: " + _Item.Key);
            _GeometryKeys.push_back(_Representation->second);
        }
        // Generic output dependency evaluation, retained for legacy stored documents.
        // New requests already contain only the geometry selected by Python.
        return iCAX::OpenCascade::EvaluateNeutralModel(Model_, _GeometryKeys);
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

    std::string PunchLowerAscii(std::string Value_)
    {
        std::ranges::transform(Value_, Value_.begin(), [](const unsigned char Value) {
            return static_cast<char>(std::tolower(Value));
        });
        return Value_;
    }

    ObjectMap PunchFeatureSnapshot(const SPunchFeature& Feature_)
    {
        VariantArray _Contour;
        for (const auto& _Point : Feature_.Contour) _Contour.emplace_back(VariantArray{_Point[0], _Point[1]});
        ObjectMap _Result{
            { "id", Feature_.ID },
            { "enabled", Feature_.Enabled },
            { "reference", Feature_.Reference },
            { "endDatum", Feature_.EndDatum },
            { "opposite", Feature_.Opposite },
            { "through", Feature_.Through },
            { "reverse", Feature_.Reverse },
            { "allowOpen", Feature_.AllowOpen },
            { "cornerRadius", Feature_.CornerRadius },
            { "rowCount", Feature_.RowCount },
            { "rowPitch", Feature_.RowPitch },
            { "contour", _Contour },
            { "type", Feature_.Type },
            { "face", Feature_.Face },
            { "station", Feature_.Station },
            { "offset", Feature_.Offset },
            { "diameter", Feature_.Diameter },
            { "spanAlong", Feature_.SpanAlong },
            { "spanAcross", Feature_.SpanAcross },
            { "rotation", Feature_.Rotation },
            { "arrayCount", static_cast<unsigned long long>(Feature_.ArrayCount) },
            { "arrayPitch", Feature_.ArrayPitch },
        };
        for (const auto& [_Key, _Value] : Feature_.TemplateData) if (_Key != "toolSnapshot") _Result[_Key] = _Value;
        if (!Feature_.LayoutDatum.empty()) _Result["layoutDatum"] = Feature_.LayoutDatum;
        if(Feature_.HasArrayGroups) {
            VariantArray _Transforms;
            for(const auto& _Matrix:Feature_.ArrayTransforms){VariantArray _Row;for(double _Value:_Matrix)_Row.emplace_back(_Value);_Transforms.emplace_back(_Row);}
            _Result["arrayTransforms"]=_Transforms;_Result["arrayCandidateCount"]=Feature_.ArrayCandidateCount;
        }
        const auto _Offsets = [](const std::vector<double>& _Values) {
            VariantArray _Result;
            for (const auto _Value : _Values) _Result.emplace_back(_Value);
            return _Result;
        };
        if (!Feature_.ArrayOffsets.empty()) _Result["arrayOffsets"] = _Offsets(Feature_.ArrayOffsets);
        if (!Feature_.RowOffsets.empty()) _Result["rowOffsets"] = _Offsets(Feature_.RowOffsets);
        if (!Feature_.SkippedInstances.empty()) {
            VariantArray _Skipped;
            for (const auto& _ID : Feature_.SkippedInstances) _Skipped.emplace_back(_ID);
            _Result["skippedInstances"] = _Skipped;
        }
        return _Result;
    }

    std::vector<SPunchFeature> ReadPunchFeatureArray(const Variant& Value_, const std::string& Field_)
    {
        if (!Value_.Is<VariantArray>())
            throw std::invalid_argument("TubeDesigner field must be an array: " + Field_);
        const auto _Values = Value_.To<VariantArray>();
        if (_Values.size() > 256)
            throw std::invalid_argument("冲孔特征最多支持 256 条");
        std::vector<SPunchFeature> _Features;
        _Features.reserve(_Values.size());
        for (const auto& _Value : _Values)
        {
            if (!_Value.Is<ObjectMap>())
                throw std::invalid_argument("冲孔特征必须是对象");
            const auto _Object = _Value.To<ObjectMap>();
            SPunchFeature _Feature;
            _Feature.HasArrayGroups=_Object.contains("arrayGroups");
            for (const auto& _Key : {"toolRef", "toolParameters", "toolSnapshot", "frozenTool", "frozenCut", "section"}) {
                if (const auto _It = _Object.find(_Key); _It != _Object.end()) {
                    if (!_It->second.Is<ObjectMap>()) throw std::invalid_argument("刀具模板参数须为对象");
                    _Feature.TemplateData[_Key] = _It->second;
                }
            }
            for (const auto& _Key : {"toolLabel","toolKind","toolTarget","recordKind","depthMode","distributionMode",
                "centerMode","fillAlign","spacingSequence","positionList","skipInstancesText","rowDistributionMode","layoutReference"})
                if (const auto _It = _Object.find(_Key); _It != _Object.end() && _It->second.Is<std::string>())
                    _Feature.TemplateData[_Key] = _It->second;
            // Persist the editable rule, not just its expanded offsets. UI-only
            // summaries and arbitrary unrecognised geometry are not retained.
            for (const auto& _Key : {"headMargin","tailMargin","centerOffset","centerFirstOffset","maxSpacing",
                "rowStartAngle","rowEndAngle"})
                if (const auto _It = _Object.find(_Key); _It != _Object.end())
                    _Feature.TemplateData[_Key] = _It->second;
            const auto _Bool = [&](const std::string& _Key, bool _Default) {
                const auto _It = _Object.find(_Key);
                if (_It == _Object.end()) return _Default;
                if (!_It->second.Is<bool>()) throw std::invalid_argument("孔开关须为布尔值: " + _Key);
                return _It->second.To<bool>();
            };
            _Feature.ID = GetString(_Object, "id", std::to_string(_Features.size() + 1));
            _Feature.Enabled = _Bool("enabled", true);
            _Feature.Opposite = _Bool("opposite", false);
            _Feature.Through = _Bool("through", false);
            _Feature.Reverse = _Bool("reverse", false);
            _Feature.AllowOpen = _Bool("allowOpen", false);
            _Feature.Reference = GetString(_Object, "reference", "start");
            _Feature.EndDatum = GetString(_Object, "endDatum", "long");
            _Feature.LayoutDatum = GetString(_Object, "layoutDatum", "");
            _Feature.CornerRadius = GetDouble(_Object, "cornerRadius", 0.0);
            _Feature.RowCount = _Feature.HasArrayGroups?1:GetUInt64(_Object, "rowCount", 1);
            _Feature.RowPitch = _Feature.HasArrayGroups?0:GetDouble(_Object, "rowPitch", 0.0);
            if (const auto _It = _Object.find("contour"); _It != _Object.end()) {
                if (!_It->second.Is<VariantArray>()) throw std::invalid_argument("自定义孔轮廓须为坐标数组");
                for (const auto& _Point : _It->second.To<VariantArray>()) {
                    if (!_Point.Is<VariantArray>()) throw std::invalid_argument("孔轮廓坐标无效");
                    const auto _XY = _Point.To<VariantArray>();
                    if (_XY.size() != 2) throw std::invalid_argument("孔轮廓须为二维坐标");
                    _Feature.Contour.push_back({ToDouble(_XY[0], "x"), ToDouble(_XY[1], "y")});
                }
            }
            _Feature.Type = PunchLowerAscii(GetString(_Object, "type", "circle"));
            _Feature.Face = PunchLowerAscii(GetString(_Object, "face", "top"));
            _Feature.Station = GetDouble(_Object, "station", 0.0);
            _Feature.Offset = GetDouble(_Object, "offset", 0.0);
            _Feature.Diameter = GetDouble(_Object, "diameter", 10.0);
            _Feature.SpanAlong = GetDouble(_Object, "spanAlong", 30.0);
            _Feature.SpanAcross = GetDouble(_Object, "spanAcross", 10.0);
            _Feature.Rotation = GetDouble(_Object, "rotation", 0.0);
            _Feature.ArrayCount = _Feature.HasArrayGroups?1:GetUInt64(_Object, "arrayCount", 1);
            _Feature.ArrayPitch = _Feature.HasArrayGroups?0:GetDouble(_Object, "arrayPitch", 0.0);
            if(_Feature.HasArrayGroups) {
                if(!_Object.at("arrayGroups").Is<VariantArray>())throw std::invalid_argument("阵列组须为数组");
                const auto _Groups=_Object.at("arrayGroups").To<VariantArray>();
                if(_Groups.size()>1000)throw std::invalid_argument("阵列组最多为 1000 组");
                VariantArray _Saved;std::set<std::string> _GroupIDs;
                for(const auto& _GroupValue:_Groups) {
                    if(!_GroupValue.Is<ObjectMap>())throw std::invalid_argument("阵列组须为对象");
                    const auto _Group=_GroupValue.To<ObjectMap>();ObjectMap _Rule;
                    for(const auto* _Key:{"id","enabled","type","axis","count","spacing","direction","distributionMode","headMargin","tailMargin",
                        "centerOffset","centerMode","centerFirstOffset","fillAlign","maxSpacing","spacingSequence","positionList",
                        "angleMode","startAngle","endAngle","angleStep","origin"})
                        if(_Group.contains(_Key))_Rule[_Key]=_Group.at(_Key);
                    const auto _ID=GetString(_Rule,"id");
                    if(_ID.empty()||!_GroupIDs.insert(_ID).second)throw std::invalid_argument("阵列组 ID 不可为空或重复");
                    _Saved.emplace_back(_Rule);
                }
                _Feature.TemplateData["arrayGroups"]=_Saved;
                if(const auto _It=_Object.find("arraySkips");_It!=_Object.end()) {
                    if(!_It->second.Is<VariantArray>()||_It->second.To<VariantArray>().size()>1000)throw std::invalid_argument("阵列跳过规则须为最多 1000 项的数组");
                    for(const auto& _PatternValue:_It->second.To<VariantArray>()) {
                        if(!_PatternValue.Is<ObjectMap>())throw std::invalid_argument("阵列跳过规则须为组索引对象");
                        for(const auto& [_ID,_Index]:_PatternValue.To<ObjectMap>()) {
                            const double _N=ToDouble(_Index,"阵列跳过索引");
                            if(_ID.empty()||!std::isfinite(_N)||_N<0||_N>=1000||std::floor(_N)!=_N)throw std::invalid_argument("阵列跳过索引无效");
                        }
                    }
                    _Feature.TemplateData["arraySkips"]=_It->second;
                }
                _Feature.ArrayCandidateCount=GetUInt64(_Object,"arrayCandidateCount",1);
                if(const auto _It=_Object.find("arrayTransforms");_It!=_Object.end()) {
                    if(!_It->second.Is<VariantArray>())throw std::invalid_argument("阵列变换须为矩阵数组");
                    const auto _Rows=_It->second.To<VariantArray>();if(_Rows.size()>1000)throw std::invalid_argument("阵列变换最多为 1000 个");
                    for(const auto& _Row:_Rows) {
                        if(!_Row.Is<VariantArray>()||_Row.To<VariantArray>().size()!=16)throw std::invalid_argument("阵列变换须为 16 项行主序矩阵");
                        const auto _Values=_Row.To<VariantArray>();std::array<double,16> _Matrix;
                        for(std::size_t _I=0;_I<16;++_I)_Matrix[_I]=ToDouble(_Values[_I],"阵列变换");
                        _Feature.ArrayTransforms.push_back(_Matrix);
                    }
                }
            } else if(_Object.contains("arrayTransforms")||_Object.contains("arrayCandidateCount"))
                throw std::invalid_argument("显式阵列变换须携带阵列组规则");
            const auto _ReadOffsets = [&](const std::string& _Key, std::vector<double>& _Result) {
                const auto _It = _Object.find(_Key);
                if (_It == _Object.end()) return;
                if (!_It->second.Is<VariantArray>()) throw std::invalid_argument("孔偏移序列须为数组: " + _Key);
                const auto _Values = _It->second.To<VariantArray>();
                if (_Values.empty() || _Values.size() > 1000)
                    throw std::invalid_argument("显式孔偏移序列须包含 1 至 1000 个位置: " + _Key);
                for (const auto& _Value : _Values) _Result.push_back(ToDouble(_Value, _Key));
            };
            if(!_Feature.HasArrayGroups){_ReadOffsets("arrayOffsets", _Feature.ArrayOffsets);_ReadOffsets("rowOffsets", _Feature.RowOffsets);}
            if (const auto _It = _Object.find("skippedInstances"); !_Feature.HasArrayGroups&&_It != _Object.end()) {
                if (!_It->second.Is<VariantArray>()) throw std::invalid_argument("跳过孔索引须为数组");
                const auto _Skipped = _It->second.To<VariantArray>();
                if (_Skipped.size() > 1000) throw std::invalid_argument("跳过孔索引最多为 1000 个");
                for (const auto& _ID : _Skipped) {
                    if (!_ID.Is<std::string>()) throw std::invalid_argument("跳过孔索引须为零基行:列字符串");
                    _Feature.SkippedInstances.push_back(_ID.To<std::string>());
                }
            }
            ValidatePunchFeatureLayout(_Feature);
            _Features.push_back(std::move(_Feature));
        }
        return _Features;
    }

    std::vector<SPunchFeature> ReadPunchFeatures(const ObjectMap& Object_, const std::string& Field_)
    {
        const auto _Found = Object_.find(Field_);
        if (_Found == Object_.end()) return {};
        return ReadPunchFeatureArray(_Found->second, Field_);
    }

    SPunchEnds ReadPunchEnds(const ObjectMap& Payload_)
    {
        SPunchEnds _Ends;
        const auto _It = Payload_.find("ends");
        if (_It == Payload_.end()) return _Ends;
        if (!_It->second.Is<ObjectMap>()) throw std::invalid_argument("端部参数须为对象");
        const auto _Object = _It->second.To<ObjectMap>();
        const auto _Read = [&](const std::string& _Key, SPunchEnd& _End) {
            const auto _Found = _Object.find(_Key);
            if (_Found == _Object.end()) return;
            if (!_Found->second.Is<ObjectMap>()) throw std::invalid_argument("端部参数无效");
            const auto _Value = _Found->second.To<ObjectMap>();
            for (const auto& _Name : {"toolRef", "toolParameters", "toolSnapshot", "frozenTool", "frozenCut", "section"}) {
                if (const auto _Entry = _Value.find(_Name); _Entry != _Value.end()) {
                    if (!_Entry->second.Is<ObjectMap>()) throw std::invalid_argument("端部刀具参数须为对象");
                    _End.TemplateData[_Name] = _Entry->second;
                }
            }
            for (const auto& _Name : {"toolLabel","toolKind"})
                if (const auto _Entry = _Value.find(_Name); _Entry != _Value.end() && _Entry->second.Is<std::string>())
                    _End.TemplateData[_Name] = _Entry->second;
            _End.Type = GetString(_Value, "type", "keep"); _End.Datum = GetString(_Value, "datum", "long");
            _End.Trim = GetDouble(_Value, "trim", 0); _End.Angle = GetDouble(_Value, "angle", 45);
            _End.Rotation = GetDouble(_Value, "rotation", 0); _End.Diameter = GetDouble(_Value, "diameter", 40);
            _End.Offset = GetDouble(_Value, "offset", 0); _End.Clearance = GetDouble(_Value, "clearance", 0);
        };
        _Read("start", _Ends.Start); _Read("end", _Ends.End);
        return _Ends;
    }

    ObjectMap PunchEndsSnapshot(const SPunchEnds& Ends_)
    {
        const auto _Save = [](const SPunchEnd& _End) -> ObjectMap {
            ObjectMap _Result{{"type",_End.Type},{"datum",_End.Datum},{"trim",_End.Trim},{"angle",_End.Angle},
                {"rotation",_End.Rotation},{"diameter",_End.Diameter},{"offset",_End.Offset},{"clearance",_End.Clearance}};
            for (const auto& [_Key, _Value] : _End.TemplateData) if (_Key != "toolSnapshot") _Result[_Key] = _Value;
            return _Result;
        };
        return {{"start",_Save(Ends_.Start)},{"end",_Save(Ends_.End)}};
    }

    VariantArray PunchOperationChain(const std::vector<SPunchFeature>& Features_, const SPunchEnds& Ends_)
    {
        VariantArray _Operations;
        for (const auto& [_Key,_End] : {std::pair{"start",&Ends_.Start},std::pair{"end",&Ends_.End}})
            if (_End->Type != "keep") _Operations.emplace_back(ObjectMap{{"operator",std::string("subtract")},
                {"target",std::string("end")},{"key",std::string(_Key)}});
        for (std::size_t _Index=0; _Index<Features_.size(); ++_Index)
            _Operations.emplace_back(ObjectMap{{"operator",std::string("subtract")},{"target",std::string("side")},
                {"key",Features_[_Index].ID},{"featureIndex",static_cast<unsigned long long>(_Index)}});
        return _Operations;
    }

    ObjectMap InvokePunchToolRuntime(const iCAX::Application::IApplicationContext& ApplicationContext_, const ObjectMap& Parameters_)
    {
        const auto _Path = ResolveRuntimeFile(ApplicationContext_, {
            "apps/tube-designer/templates/_shared/punch_tool_runtime.py",
            "src/apps/tube-designer/templates/_shared/punch_tool_runtime.py"
        }, "TubeDesigner punch tool runtime");
        return InvokePythonTemplate(ApplicationContext_, {
            {"protocol",std::string("icax.template-runtime")},{"protocolVersion",1ull},
            {"operation",std::string("evaluate")},{"templatePath",PathToUTF8(_Path)},
            {"template",ObjectMap{{"id",std::string("icax.punch-tool-runtime")},{"version",std::string("1.0.0")},
                {"packageDigest",ContentDigest("punch-tool-runtime",ReadTextFile(_Path))}}},
            {"parameters",Parameters_},{"context",ObjectMap{{"lengthUnit",std::string("mm")}}}
        });
    }

    iCAX::Resource::CResourceReference StorePunchBRep(
        iCAX::Project::ISceneContext& Scene_, const std::string& Key_, const std::string& Name_,
        const TopoDS_Shape& Shape_, const iCAX::Interaction::CInvocation& Request_, bool ReuseIdentical_ = false)
    {
        // Creation/preview callers pass a stable logical name. Apply callers
        // pass the already-resolved resource URL so the same identity can
        // receive its next immutable version. Never hash a full resource URL
        // as if it were a new name.
        const auto _URL = Key_.rfind("resource://", 0) == 0
            ? Key_ : Scene_.Resources().MakeNamedResourceURL(Key_);
        ReportExportProgress(Request_,"convert",0,1,"正在转换实体数据");
        auto _BRep = iCAX::Tasks::Run([&] {
            return iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(Shape_,Name_,_URL,0.025);
        }, iCAX::OpenCascade::detail::GeometryTaskScheduler()).Result();
        if(ReuseIdentical_) {
            const auto _Version=Scene_.Resources().GetVersion(_URL);
            const auto _Existing=Scene_.Resources().Get<iCAX::GeometryData::BRepModel>(_URL,_Version);
            if(_Existing&&iCAX::GeometryData::Persistence::Serialize(*_Existing)==iCAX::GeometryData::Persistence::Serialize(_BRep))
                return {_URL,_Version};
        }
        ReportExportProgress(Request_,"convert",1,1,"正在写入实体资源");
        return StorePreparedBRep(Scene_,_URL,Name_,std::move(_BRep));
    }

    ObjectMap PunchEndProcess(const SPunchEnds& Ends_, ObjectMap Base_)
    {
        const auto _Apply = [&](const SPunchEnd& _End, const std::string& _Key) {
            if (_End.Type == "keep") return;
            if (!_End.TemplateData.empty()) {
                const auto _Descriptor = GetRequiredObject(GetRequiredObject(_End.TemplateData,"toolSnapshot"),"descriptor");
                Base_[_Key] = GetString(_Descriptor,"processCode",GetString(_Descriptor,"displayName"));
            } else Base_[_Key] = _End.Type;
        };
        _Apply(Ends_.Start,"startCut"); _Apply(Ends_.End,"endCut");
        return Base_;
    }

    TopoDS_Shape PunchSnapshotShape(const ObjectMap& Snapshot_, bool& IsProfile_)
    {
        const auto _Geometry = GetRequiredObject(Snapshot_, "geometry");
        IsProfile_ = GetString(_Geometry, "mode") == "profile";
        if (IsProfile_) {
            const auto _Extrusion = BuildProfileExtrusion(ObjectMap{{"contours",_Geometry.at("contours")}},1);
            // Select the original XY face; the cutter will be extruded to actual wall depth.
            for (TopExp_Explorer _Face(_Extrusion,TopAbs_FACE); _Face.More(); _Face.Next()) {
                const auto _Box = ShapeBounds(_Face.Current());
                const auto _Min = _Box.at("min").To<VariantArray>();
                const auto _Max = _Box.at("max").To<VariantArray>();
                if (std::abs(ToDouble(_Min[2],"z"))<1e-6 && std::abs(ToDouble(_Max[2],"z"))<1e-6)
                    return _Face.Current().Oriented(TopAbs_FORWARD);
            }
            throw std::invalid_argument("无法构造刀具截面");
        }
        const auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Geometry.at("model"));
        const auto _Key = GetString(_Geometry,"outputKey");
        return iCAX::OpenCascade::EvaluateNeutralModel(_Model,{_Key}).At(_Key);
    }

    TopoDS_Shape BuildPunchBlank(const ObjectMap& Profile_, double Length_)
    {
        gp_Trsf _Frame;
        // Profile width -> Y, depth -> Z, extrusion -> X (no inferred principal axis).
        _Frame.SetValues(0,0,1,0, 1,0,0,0, 0,1,0,0);
        auto _Shape = BRepBuilderAPI_Transform(BuildProfileExtrusion(Profile_,Length_),_Frame,true).Shape();
        const auto _Box = ShapeBounds(_Shape);
        const auto _Min = _Box.at("min").To<VariantArray>(), _Max = _Box.at("max").To<VariantArray>();
        gp_Trsf _Center;
        _Center.SetTranslation(gp_Vec(-ToDouble(_Min[0],"x"),
            -(ToDouble(_Min[1],"y")+ToDouble(_Max[1],"y"))/2,
            -(ToDouble(_Min[2],"z")+ToDouble(_Max[2],"z"))/2));
        return BRepBuilderAPI_Transform(_Shape,_Center,true).Shape();
    }

    TopoDS_Shape EvaluatePunch(const TopoDS_Shape& Base_, std::vector<SPunchFeature>& Features_,
        SPunchEnds& Ends_, const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_, iCAX::Project::ISceneContext& Scene_,
        const ObjectMap& Original_ = {}, bool Rebased_ = false,
        std::vector<SPunchCut>* PreviewCuts_ = nullptr, SPunchGeometryStatistics* Statistics_ = nullptr,
        SPunchPreviewDiagnostics* PreviewDiagnostics_ = nullptr, bool ToolsOnly_ = false, double NominalWallThickness_ = 0,
        const VariantArray& UserTools_ = {})
    {
        ReportExportProgress(Request_, "punch", 0, 1, "正在运行刀具模板");
        VariantArray _Features;
        for (const auto& _Feature : Features_) _Features.emplace_back(PunchFeatureSnapshot(_Feature));
        ObjectMap _PunchParameters{
            {"action",std::string("prepare")},{"features",_Features},{"ends",PunchEndsSnapshot(Ends_)},{"bounds",ShapeBounds(Base_)},
            {"original",Original_},{"rebased",Rebased_},{"userTools",UserTools_}
        };
        const auto _UserMoldRoot = ResolveUserMoldRoot(ApplicationContext_);
        if (!_UserMoldRoot.empty() && std::filesystem::is_directory(_UserMoldRoot))
            _PunchParameters["userToolRoot"] = PathToUTF8(_UserMoldRoot);
        const auto _Prepared = InvokePunchToolRuntime(ApplicationContext_, _PunchParameters);
        Features_ = ReadPunchFeatures(_Prepared,"features");
        Ends_ = ReadPunchEnds(_Prepared);
        std::map<std::string,std::shared_ptr<const iCAX::GeometryData::BRepModel>> _Frozen;
        const auto _LoadFrozen = [&](const std::string& _Key, const ObjectMap& _Data) {
            if (!_Data.contains("toolSnapshot") || GetString(GetRequiredObject(_Data,"toolSnapshot"),"resolution")!="frozen") return;
            const auto _Ref = GetRequiredObject(_Data,"frozenCut");
            const auto _BRep = Scene_.Resources().Get<iCAX::GeometryData::BRepModel>(GetString(_Ref,"url"),GetUInt64(_Ref,"version",0));
            if (!_BRep) throw std::invalid_argument("固化刀具体资源缺失；已有零件可查看，删除此节点或恢复原版刀具后才能重算");
            _Frozen[_Key] = _BRep;
        };
        for (const auto& _Feature : Features_) if (_Feature.Enabled
            && _Feature.SkippedInstances.size() != _Feature.ArrayCount * _Feature.RowCount)
            _LoadFrozen("side:"+_Feature.ID,_Feature.TemplateData);
        _LoadFrozen("end:start",Ends_.Start.TemplateData);_LoadFrozen("end:end",Ends_.End.TemplateData);
        std::vector<SPunchCut> _Cuts;
        // Geometry work uses the project's task library; database/resource commits stay on the host thread.
        TopoDS_Shape _Result;
        try {
        _Result = iCAX::Tasks::Run([&] {
            const auto _FrozenShape = [&](const std::string& _Key) -> TopoDS_Shape {
                const auto _It=_Frozen.find(_Key);if(_It==_Frozen.end())return {};
                const auto _Rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(*_It->second);
                if(!_Rebuilt.bOK || _Rebuilt.Shape.IsNull())throw std::invalid_argument("无法恢复固化刀具体");
                return _Rebuilt.Shape;
            };
            for (auto& _Feature : Features_) {
                if (!_Feature.Enabled) continue;
                if(PreviewDiagnostics_) {
                    PreviewDiagnostics_->FailureStage="prepare";PreviewDiagnostics_->FailureTarget="feature";
                    PreviewDiagnostics_->FailureKey=_Feature.ID;PreviewDiagnostics_->FailureIndex=&_Feature-Features_.data();
                }
                _Feature.FrozenCut=_FrozenShape("side:"+_Feature.ID);
                if(!_Feature.FrozenCut.IsNull()) {
                    _Feature.FrozenCutInstanceCount=GetUInt64(GetRequiredObject(_Feature.TemplateData,"frozenCut"),"instanceCount",0);
                    continue;
                }
                // A freshly evaluated group may now have all candidates skipped;
                // never leave its previous, non-empty frozen cutter attached.
                _Feature.TemplateData.erase("frozenCut");
                if (_Feature.SkippedInstances.size() == _Feature.ArrayCount * _Feature.RowCount) continue;
                _Feature.ToolShape = PunchSnapshotShape(GetRequiredObject(_Feature.TemplateData,"toolSnapshot"),_Feature.ToolIsProfile);
                _Feature.Type = GetString(GetRequiredObject(_Feature.TemplateData,"toolRef"),"id");
                const auto _CoordinateSpace = GetString(GetRequiredObject(GetRequiredObject(_Feature.TemplateData,"toolSnapshot"),"geometry"),"coordinateSpace");
                _Feature.ToolInPartCoordinates = _CoordinateSpace=="part" || _CoordinateSpace=="part-local";
                _Feature.ToolInPartLocalCoordinates = _CoordinateSpace=="part-local";
                if(!_Feature.ToolInPartCoordinates) PreparePunchToolFootprint(_Feature);
            }
            for (auto* _End : {&Ends_.Start, &Ends_.End}) if (_End->TemplateData.contains("toolSnapshot")) {
                if(PreviewDiagnostics_) {
                    PreviewDiagnostics_->FailureStage="prepare";PreviewDiagnostics_->FailureTarget="end";
                    PreviewDiagnostics_->FailureKey=_End==&Ends_.Start?"start":"end";PreviewDiagnostics_->FailureIndex=-1;
                }
                const auto _Snapshot = GetRequiredObject(_End->TemplateData,"toolSnapshot");
                bool _IsProfile = false;
                _End->ToolShape = _FrozenShape(_End==&Ends_.Start?"end:start":"end:end");
                const bool _FixedPlacement=!_End->ToolShape.IsNull();
                if(!_FixedPlacement) _End->ToolShape = PunchSnapshotShape(_Snapshot,_IsProfile);
                const auto _Geometry = GetRequiredObject(_Snapshot,"geometry");
                if (const auto _Contact = _Geometry.find("requireEndContact"); _Contact != _Geometry.end()) {
                    if (!_Contact->second.Is<bool>()) throw std::invalid_argument("端部刀具接触检查标记无效");
                    _End->RequireEndContact = _Contact->second.To<bool>();
                }
                if (const auto _Retained = _Geometry.find("requireRetainedEndMaterial"); _Retained != _Geometry.end()) {
                    if (!_Retained->second.Is<bool>()) throw std::invalid_argument("端部刀具保留材料检查标记无效");
                    _End->RequireRetainedEndMaterial = _Retained->second.To<bool>();
                }
                const auto _Space = GetString(_Geometry,"coordinateSpace","end-local");
                if (_Space == "end-local" && !_FixedPlacement) {
                    const auto _Box = ShapeBounds(Base_);
                    const auto _Min = _Box.at("min").To<VariantArray>(), _Max = _Box.at("max").To<VariantArray>();
                    const bool _Start = _End == &Ends_.Start;
                    const double _Sign = _Start ? 1 : -1, _R = _End->Rotation*kPunchPi/180;
                    gp_Trsf _Placement;
                    _Placement.SetValues(_Sign,0,0,ToDouble((_Start?_Min:_Max)[0],"x")+_Sign*_End->Trim,
                        0,std::cos(_R),-_Sign*std::sin(_R),(ToDouble(_Min[1],"y")+ToDouble(_Max[1],"y"))/2,
                        0,std::sin(_R),_Sign*std::cos(_R),(ToDouble(_Min[2],"z")+ToDouble(_Max[2],"z"))/2);
                    _End->ToolShape = BRepBuilderAPI_Transform(_End->ToolShape,_Placement,true).Shape();
                } else if (_Space != "part" && _Space != "end-local") throw std::invalid_argument("端部刀具坐标空间无效");
                if (const auto _Datum = _Geometry.find("datumCenter"); _Datum != _Geometry.end() && !_Datum->second.Is<std::monostate>()) {
                    _End->HasDatumCenter = true; _End->DatumCenter = ToDouble(_Datum->second,"datumCenter");
                    if (_Space == "end-local") {
                        const auto _Box = ShapeBounds(Base_);
                        const bool _Start = _End == &Ends_.Start;
                        const auto _Origin = _Box.at(_Start?"min":"max").To<VariantArray>();
                        _End->DatumCenter = ToDouble(_Origin[0],"x")+(_Start?1:-1)*(_End->Trim+_End->DatumCenter);
                    }
                }
            }
            if(ToolsOnly_) {
                _Cuts=BuildPunchToolPlacements(Base_,Features_,Ends_,NominalWallThickness_,Statistics_,PreviewDiagnostics_);
                return Base_;
            }
            return BuildPunchGeometry(Base_, Features_, Ends_, [&](std::size_t _Done, std::size_t _Total, const std::string& _Message) {
                ReportExportProgress(Request_, "punch", _Done, _Total, _Message);
            }, &_Cuts, Statistics_, PreviewDiagnostics_);
        }, iCAX::OpenCascade::detail::GeometryTaskScheduler()).Result();
        } catch(...) {
            if(PreviewCuts_) *PreviewCuts_=_Cuts;
            throw;
        }
        if (PreviewCuts_) *PreviewCuts_ = _Cuts;
        if(ToolsOnly_)return _Result; // Placement previews never create frozen manufacturing cutters.
        for (const auto& _Cut : _Cuts) {
            if(_Frozen.contains(_Cut.Target+":"+_Cut.Key)) continue; // Keep exact embedded reference when degraded.
            ObjectMap* _Data=nullptr;
            if(_Cut.Target=="end") _Data=&(_Cut.Key=="start"?Ends_.Start:Ends_.End).TemplateData;
            else for(auto& _Feature:Features_) if(_Feature.ID==_Cut.Key){_Data=&_Feature.TemplateData;break;}
            if(!_Data) throw std::logic_error("刀具节点记录不匹配");
            const auto _Ref=StorePunchBRep(Scene_,"tube-designer/punch-cut/"+UuidToString(iCAX::Data::GenerateNewUUID()),
                GetString(*_Data,"toolLabel","固化刀具"),_Cut.Shape,Request_);
            (*_Data)["frozenCut"]=ObjectMap{{"url",_Ref.URL},{"version",_Ref.nVersion},{"coordinateSpace",std::string("part")},
                {"instanceCount",static_cast<unsigned long long>(_Cut.InstanceCount)}};
        }
        return _Result;
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
        ObjectMap ItemProperties;
        ObjectMap TubeProfile;
    };

    iCAX::Interaction::CInvocationResult GenerateNeutralPreview(
        const ObjectMap& Payload_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::ISceneContext& Scene_)
    {
        // Reject malformed quantities before doing any geometric work.
        if (Payload_.contains("instanceQuantity"))
            ValidateInstanceQuantity(GetUInt64(Payload_, "instanceQuantity", 1));
        auto _ComponentStore = ProductContext_ ? GetUserDataStore(ProductContext_) : nullptr;
        const auto _Evaluation = EvaluateNeutralTemplate(ApplicationContext_, Payload_, "display",
            {}, _ComponentStore.get());
        const auto _DisplayOutput = FindOutputSet(_Evaluation.Model, "result");
        if (!_DisplayOutput || _DisplayOutput->ItemKeys.empty())
            throw std::runtime_error("Python template returned no display output items");
        const auto _PartCount = OptionalPropertyUInt64(
            _Evaluation.Model.Extensions, "tubeDesigner.manufacturingPartCount");
        if (_PartCount == 0)
            throw std::runtime_error("Python template returned no manufacturing item count");
        // Execute the entire received document. Python, not the native evaluator,
        // decides which operations belong in this request's neutral expression.
        const auto _Geometry = iCAX::OpenCascade::EvaluateNeutralModel(_Evaluation.Model);

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
        const auto _InstanceQuantity = ValidateInstanceQuantity(GetUInt64(Payload_, "instanceQuantity",
            _ExistingProduct ? _ExistingProduct->GetQuantity() : 1));
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
        std::vector<iCAX::OpenCascade::SBRepConversionInput> _Conversions;
        _Conversions.reserve(_DisplayOutput->ItemKeys.size());
        std::uint64_t _Index = 0;
        for (const auto& _ItemKey : _DisplayOutput->ItemKeys)
        {
            const auto& _Item = FindModelItem(_Evaluation.Model, _ItemKey);
            const auto _Representation = _Item.Representations.find("result");
            if (_Representation == _Item.Representations.end())
                throw std::runtime_error("neutral model item has no result representation: " + _Item.Key);
            const auto _EntityID = MakeStableEntityID(_ProductID, "member", _Item.Key);
            const auto _StablePrefix = "tube-designer/product/" + UuidToString(_ProductID)
                + "/item/" + _Item.Key;
            const auto _Name = _Item.DisplayName.Resolve("zh-CN");
            auto _PreviewShape = _Geometry.At(_Representation->second);
            auto _MemberProperties = _Item.Properties;
            ObjectMap _TubeProfile;
            if (const auto _Profile = _Item.Properties.find("tubeDesigner.profile");
                _Profile != _Item.Properties.end() && _Profile->second.Is<ObjectMap>())
            {
                _TubeProfile = ProfileSnapshotFromProfile(
                    _Profile->second.To<ObjectMap>(), "product-generation");
                _MemberProperties.erase("tubeDesigner.profile");
            }
            if (_ExistingProduct && IsTubeManufacturingPart(_Item.Properties))
            {
                if (const auto _Sketch = FindProductSideSketch(
                    *_ExistingProduct, _EntityID, _Item.Key))
                {
                    const auto _BaseResourceID = Scene_.Resources().MakeNamedResourceURL(
                        _StablePrefix + "/side-sketch-base");
                    const auto _Base = StorePreparedBRep(
                        Scene_, _BaseResourceID, "产品二维包覆基准",
                        iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(
                            _PreviewShape, "产品二维包覆基准", _BaseResourceID, 0.025));
                    _MemberProperties["tubeDesigner.sideSketchBaseResourceId"] = _Base.URL;
                    _MemberProperties["tubeDesigner.sideSketchBaseResourceVersion"] = _Base.nVersion;
                    const auto _Applied = ApplyTubeSideSketch(_PreviewShape, *_Sketch);
                    if (!_Applied.bOK || _Applied.Shape.IsNull())
                        throw std::runtime_error(_Applied.Diagnostic.empty()
                            ? "产品侧面草图没有生成有效的切除结果" : _Applied.Diagnostic);
                    _PreviewShape = _Applied.Shape;
                    _MemberProperties["tubeDesigner.sideSketchApplyMethod"] = _Applied.Method;
                    _MemberProperties["tubeDesigner.sideSketchDiagnostic"] = _Applied.Diagnostic;
                    _MemberProperties["tubeDesigner.sideSketchClosedLoopCount"] =
                        static_cast<unsigned long long>(_Applied.ClosedLoopCount);
                    _MemberProperties["tubeDesigner.sideSketchOpenTrajectoryCount"] =
                        static_cast<unsigned long long>(_Applied.OpenTrajectoryCount);
                }
            }
            _Conversions.push_back({ _PreviewShape, _Name + " preview",
                Scene_.Resources().MakeNamedResourceURL(_StablePrefix + "/preview") });
            _Members.push_back({ &_Item, _EntityID, {}, {}, ++_Index,
                std::move(_MemberProperties), std::move(_TubeProfile) });
        }
        auto _Converted = iCAX::OpenCascade::ConvertOpenCascadeShapesToBRep(_Conversions, 0.025);
        // Workers touch only private OCC shapes and in-memory BRep models. All
        // resource publication stays on this scene-owning invocation thread.
        for (std::size_t _MemberIndex = 0; _MemberIndex < _Members.size(); ++_MemberIndex)
        {
            auto& _Member = _Members[_MemberIndex];
            const auto& _Input = _Conversions[_MemberIndex];
            _Member.PreviewResource = StorePreparedBRep(
                Scene_, _Input.SourceID, _Input.DisplayName, std::move(_Converted[_MemberIndex]));
            _Member.FrontendGeometryResource = iCAX::RenderInteraction::EnsureFrontendGeometryResource(
                Scene_.Resources(), _Member.PreviewResource.URL, iCAX::Render::ERenderGeometryKind::Mesh);
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
                    { CProductInstanceComponent::PropertyName_Quantity, PropertyValue(static_cast<unsigned long long>(_InstanceQuantity)) },
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
                { CGenerationRunComponent::PropertyName_PartCount, PropertyValue(static_cast<unsigned long long>(_PartCount)) },
                { CGenerationRunComponent::PropertyName_IssueCount, PropertyValue(static_cast<unsigned long long>(_Evaluation.Model.Diagnostics.size())) },
                { CGenerationRunComponent::PropertyName_PackageDigest, PropertyValue(_Evaluation.Descriptor.PackageDigest) },
                { CGenerationRunComponent::PropertyName_NeutralModel, PropertyValue(_Evaluation.Document) }
            });

            for (const auto& _Prepared : _Members)
            {
                const auto& _Item = *_Prepared.Item;
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
                        { CAssemblyMemberComponent::PropertyName_Length, PropertyValue(OptionalPropertyNumber(_Item.Properties, "length")) },
                        { CAssemblyMemberComponent::PropertyName_X1, PropertyValue(0.0) },
                        { CAssemblyMemberComponent::PropertyName_Y1, PropertyValue(0.0) },
                        { CAssemblyMemberComponent::PropertyName_X2, PropertyValue(0.0) },
                        { CAssemblyMemberComponent::PropertyName_Y2, PropertyValue(0.0) },
                         { CAssemblyMemberComponent::PropertyName_PreviewGeometryResourceID, PropertyValue(_Prepared.PreviewResource.URL) },
                         { CAssemblyMemberComponent::PropertyName_PreviewGeometryResourceVersion, PropertyValue(_Prepared.PreviewResource.nVersion) },
                         { CAssemblyMemberComponent::PropertyName_ItemProperties, PropertyValue(_Prepared.ItemProperties) }
                     });
                if (!_Prepared.TubeProfile.empty())
                    QueueUpsertComponent(
                        _Transaction, _ExistingMember, _Prepared.EntityID,
                        CTubeProfileComponent::S_ClassName,
                        TubeProfileComponentPropertiesFromSnapshot(_Prepared.TubeProfile));
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
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_) throw std::invalid_argument("TubeDesigner.GeneratePreview requires a scene");
        tube::license::Enforce<101, tube::license::Feature::Design>();
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _TemplateID = GetString(_Payload, "templateId");
        if (!IsPythonTemplate(ApplicationContext_, _TemplateID))
            throw std::invalid_argument("unsupported TubeDesigner template: " + _TemplateID);
        auto _Result = GenerateNeutralPreview(_Payload, ApplicationContext_, ProductContext_, *Scene_);
        ReleaseTransientParts(*Scene_, {});
        SyncNestingPersistence(*Scene_);
        return _Result;
    }

    iCAX::Interaction::CInvocationResult HandleGenerateProductTemplatePreview(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_)
            throw std::invalid_argument("TubeDesigner.GenerateProductTemplatePreview requires a scene");
        tube::license::Enforce<101, tube::license::Feature::Design>();
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _TemplateID = GetRequiredText(_Payload, "templateId", 256);
        if (_TemplateID.empty())
            throw std::invalid_argument("产品模板预览缺少模板 ID");
        if (const auto _It = _Payload.find("parameters");
            _It != _Payload.end() && !_It->second.Is<ObjectMap>())
            throw std::invalid_argument("产品模板预览参数必须是对象");

        ObjectMap _EvaluationPayload{{"templateId", _TemplateID}};
        if (const auto _It = _Payload.find("templateVersion"); _It != _Payload.end())
            _EvaluationPayload["templateVersion"] = _It->second;
        if (const auto _It = _Payload.find("parameters"); _It != _Payload.end()
            && _It->second.Is<ObjectMap>())
        {
            for (const auto& [_Key, _Value] : _It->second.To<ObjectMap>())
                _EvaluationPayload[_Key] = _Value;
        }
        auto _ComponentStore = ProductContext_ ? GetUserDataStore(ProductContext_) : nullptr;
        const auto _Evaluation = EvaluateNeutralTemplate(
            ApplicationContext_, _EvaluationPayload, "display", {},
            _ComponentStore.get());
        const auto _Output = FindOutputSet(_Evaluation.Model, "result");
        if (!_Output || _Output->ItemKeys.empty())
            throw std::runtime_error("产品模板没有可预览的几何结果");
        const auto _Geometry = iCAX::OpenCascade::EvaluateNeutralModel(_Evaluation.Model);
        const auto _Material = EnsureDesignerMaterial(*Scene_);
        VariantArray _Items;
        _Items.reserve(_Output->ItemKeys.size());
        std::uint64_t _Index = 0;
        for (const auto& _ItemKey : _Output->ItemKeys)
        {
            const auto& _Item = FindModelItem(_Evaluation.Model, _ItemKey);
            const auto _Representation = _Item.Representations.find("result");
            if (_Representation == _Item.Representations.end())
                throw std::runtime_error("产品模板预览项没有 result 几何: " + _Item.Key);
            const auto _Shape = _Geometry.At(_Representation->second);
            if (_Shape.IsNull()) continue;
            const auto _Name = _Item.DisplayName.Resolve("zh-CN");
            const auto _BRep = StoreBRep(
                *Scene_, "tube-designer/template-preview/" + _Evaluation.Descriptor.ID + "/" + _Item.Key,
                _Name + " preview", _Shape);
            const auto _Mesh = iCAX::RenderInteraction::EnsureFrontendGeometryResource(
                Scene_->Resources(), _BRep.URL, iCAX::Render::ERenderGeometryKind::Mesh);
            _Items.emplace_back(ObjectMap{
                {"entityId", std::string("template-preview-") + _Evaluation.Descriptor.ID + "-" + std::to_string(++_Index)},
                {"key", _Item.Key},
                {"name", _Name},
                {"geometry", ObjectMap{{"url", _Mesh.URL}, {"version", _Mesh.nVersion}}},
                {"bounds", ShapeBounds(_Shape)},
            });
        }
        if (_Items.empty())
            throw std::runtime_error("产品模板没有有效的预览实体");
        return MakeResponse(Variant(ObjectMap{
            {"templateId", _Evaluation.Descriptor.ID},
            {"templateVersion", _Evaluation.Descriptor.Version},
            {"parameters", _Evaluation.Parameters},
            {"items", std::move(_Items)},
            {"material", ObjectMap{{"url", _Material.URL}, {"version", _Material.nVersion}}},
        }));
    }

    const iCAX::TemplateRuntime::SOutputSet& ManufacturingOutput(
        const iCAX::TemplateRuntime::SNeutralModel& Model_)
    {
        const auto _Purpose = GetString(Model_.Extensions, "tubeDesigner.geometryPurpose");
        // Only explicitly manufactured results or legacy export representations
        // are production inputs. A display result can never be a recovery source.
        const auto _Output = _Purpose == "manufacturing"
            ? FindOutputSet(Model_, "result")
            : (_Purpose.empty() ? FindOutputSet(Model_, "export") : nullptr);
        if (!_Output || _Output->ItemKeys.empty())
            throw std::runtime_error("generation run has no manufacturing neutral expression");
        return *_Output;
    }

    void ValidateGenerationModelIdentity(
        const iCAX::TemplateRuntime::SNeutralModel& Model_,
        const CGenerationRunComponent& Run_)
    {
        if (Model_.TemplateID != Run_.GetTemplateID()
            || Model_.TemplateVersion != Run_.GetTemplateVersion()
            || (!Run_.GetPackageDigest().empty() && Model_.PackageDigest != Run_.GetPackageDigest()))
            throw std::runtime_error("stored neutral model identity does not match its generation run");
    }

    SPreparedProductDisassembly PrepareNeutralModelDisassembly(
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Project::ISceneContext& Scene_,
        const iCAX::Data::uuid& ProductID_,
        const iCAX::Data::uuid& GenerationRunID_,
        const CProductInstanceComponent& Product_,
        const CGenerationRunComponent& Run_)
    {
        const auto _PreviewDocument = Run_.GetNeutralModel();
        if (_PreviewDocument.empty())
            throw std::invalid_argument("generation run has no stored neutral model");
        const auto _PreviewModel = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(
            Variant(_PreviewDocument));
        ValidateGenerationModelIdentity(_PreviewModel, Run_);
        if (Run_.GetProductID() != ProductID_
            || _PreviewModel.TemplateID != Product_.GetTemplateID()
            || _PreviewModel.TemplateVersion != Product_.GetTemplateVersion())
        {
            throw std::runtime_error("stored neutral model identity does not match its generation run");
        }
        auto _Document = Run_.GetManufacturingModel();
        if (_Document.empty())
        {
            if (GetString(_PreviewModel.Extensions, "tubeDesigner.geometryPurpose") == "display")
            {
                // Reproduce the committed design, not current editor values. The
                // package must still match; never silently manufacture a new design.
                auto _Payload = _PreviewModel.Parameters;
                _Payload["templateId"] = _PreviewModel.TemplateID;
                _Payload["templateVersion"] = _PreviewModel.TemplateVersion;
                const auto _SnapshotsIt = _PreviewModel.Extensions.find(kResolvedComponentModels);
                const auto _FrozenComponents = _SnapshotsIt != _PreviewModel.Extensions.end()
                    && _SnapshotsIt->second.Is<ObjectMap>() ? _SnapshotsIt->second.To<ObjectMap>() : ObjectMap();
                _Document = EvaluateNeutralTemplate(
                    ApplicationContext_, _Payload, "manufacturing", _PreviewModel.PackageDigest,
                    nullptr, &_FrozenComponents).Document;
            }
            else
            {
                // Previously saved combined documents already contain the exact
                // production graph, so they do not require the original template.
                _Document = _PreviewDocument;
            }
        }
        const auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(Variant(_Document));
        ValidateGenerationModelIdentity(_Model, Run_);
        if (_Model.Parameters != _PreviewModel.Parameters)
            throw std::runtime_error("manufacturing parameters do not match the committed preview");
        const auto& _Output = ManufacturingOutput(_Model);
        if (_Output.ItemKeys.size() != static_cast<std::size_t>(Run_.GetPartCount()))
            throw std::runtime_error("manufacturing item count does not match its generation run");
        const auto _Geometry = _Output.Purpose == "result"
            ? iCAX::OpenCascade::EvaluateNeutralModel(_Model)
            : EvaluateOutputGeometry(_Model, _Output);

        SPreparedProductDisassembly _Prepared{ ProductID_, GenerationRunID_, {}, std::move(_Document) };
        _Prepared.Parts.reserve(_Output.ItemKeys.size());
        std::vector<iCAX::OpenCascade::SBRepConversionInput> _Conversions;
        _Conversions.reserve(_Output.ItemKeys.size());
        std::uint64_t _Index = 0;
        std::map<std::string, std::uint64_t> _CategoryOrdinals;
        for (const auto& _ItemKey : _Output.ItemKeys)
        {
            const auto& _Item = FindModelItem(_Model, _ItemKey);
            const auto _Representation = _Item.Representations.find(_Output.Purpose);
            if (_Representation == _Item.Representations.end())
                throw std::runtime_error("neutral model item has no manufacturing representation: " + _Item.Key);
            ++_Index;
            auto _ItemProperties = _Item.Properties;
            const auto _PartKind = ManufacturingPartKind(_ItemProperties);
            ObjectMap _TubeProfile;
            if (const auto _Profile = _ItemProperties.find("tubeDesigner.profile");
                _Profile != _ItemProperties.end() && _Profile->second.Is<ObjectMap>())
            {
                _TubeProfile = ProfileSnapshotFromProfile(
                    _Profile->second.To<ObjectMap>(), "product-generation");
                _ItemProperties.erase("tubeDesigner.profile");
            }
            if (_PartKind == "plate" || _PartKind == "glass")
            {
                const auto _Plate = ManufacturingPlate(_ItemProperties);
                const auto _Width = GetDouble(_Plate, "width", 0);
                const auto _Height = GetDouble(_Plate, "height", 0);
                const auto _Thickness = GetDouble(_Plate, "thickness", 0);
                if (!std::isfinite(_Width) || !std::isfinite(_Height) || !std::isfinite(_Thickness)
                    || _Width <= 0 || _Height <= 0 || _Thickness <= 0
                    || _Thickness >= std::min(_Width, _Height))
                    throw std::invalid_argument("板件宽、高、厚度无效，无法拆单");
                _ItemProperties["length"] = std::max(_Width, _Height);
            }
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
            _ItemProperties["manufacturing.coordinateSystem"] = std::string(
                _PartKind == "plate" || _PartKind == "glass" ? "plate-local" : "part-local");
            _ItemProperties["manufacturing.origin"] = VariantArray{ 0.0, 0.0, 0.0 };
            if (_PartKind != "accessory")
                _ItemProperties["manufacturing.lengthAxis"] = VariantArray{ 1.0, 0.0, 0.0 };
            else
            {
                _ItemProperties["length"] = 0.0;
                _ItemProperties.erase("manufacturing.lengthAxis");
            }
            if (!_ItemProperties.contains("manufacturing.sourcing"))
                _ItemProperties["manufacturing.sourcing"] = std::string(
                    _PartKind == "accessory" || _PartKind == "glass" ? "purchased" : "made");
            const auto _MemberID = MakeStableEntityID(ProductID_, "member", _Item.Key);
            const auto _Sketches = Product_.GetSketches();
            if (const auto _SideSketches = _Sketches.find("side");
                _SideSketches != _Sketches.end() && _SideSketches->second.Is<ObjectMap>())
            {
                const auto _Side = _SideSketches->second.To<ObjectMap>();
                auto _Sketch = _Side.find(UuidToString(_MemberID));
                if (_Sketch == _Side.end())
                {
                    _Sketch = std::find_if(_Side.begin(), _Side.end(), [&](const auto& Entry_) {
                        return Entry_.second.Is<ObjectMap>()
                            && GetString(Entry_.second.To<ObjectMap>(), "targetMemberKey") == _Item.Key;
                    });
                }
                if (_Sketch != _Side.end() && _Sketch->second.Is<ObjectMap>())
                {
                    _ItemProperties["tubeDesigner.sideSketch"] = _Sketch->second;
                }
            }
            const auto _StablePrefix = "tube-designer/product/" + UuidToString(ProductID_)
                + "/item/" + _Item.Key;
            auto _ManufacturingShape = NormalizeManufacturingShape(
                _Geometry.At(_Representation->second), _ItemProperties);
            if (const auto _Sketch = _ItemProperties.find("tubeDesigner.sideSketch");
                _Sketch != _ItemProperties.end() && _Sketch->second.Is<ObjectMap>())
            {
                const auto _Applied = ApplyTubeSideSketch(
                    _ManufacturingShape, _Sketch->second.To<ObjectMap>());
                if (!_Applied.bOK || _Applied.Shape.IsNull())
                    throw std::runtime_error(_Applied.Diagnostic.empty()
                        ? "产品侧面草图没有生成有效的切除结果" : _Applied.Diagnostic);
                _ManufacturingShape = _Applied.Shape;
                _ItemProperties["tubeDesigner.sideSketchApplyMethod"] = _Applied.Method;
                _ItemProperties["tubeDesigner.sideSketchDiagnostic"] = _Applied.Diagnostic;
                _ItemProperties["tubeDesigner.sideSketchClosedLoopCount"] =
                    static_cast<unsigned long long>(_Applied.ClosedLoopCount);
                _ItemProperties["tubeDesigner.sideSketchOpenTrajectoryCount"] =
                    static_cast<unsigned long long>(_Applied.OpenTrajectoryCount);
            }
            if (_PartKind == "accessory") _ItemProperties["manufacturing.modelBounds"] = ShapeBounds(_ManufacturingShape);
            _Conversions.push_back({ _ManufacturingShape, _PartNumber + " manufacturing",
                Scene_.Resources().MakeNamedResourceURL(_StablePrefix + "/manufacturing") });
            _Prepared.Parts.push_back({
                _Index,
                _Item.Key,
                _PartNumber,
                OptionalPropertyString(_ItemProperties, "group"),
                std::max<std::uint64_t>(1, OptionalPropertyUInt64(_ItemProperties, "quantity", 1)),
                 OptionalPropertyNumber(_ItemProperties, "length"),
                 _ItemProperties,
                 _TubeProfile,
                 _MemberID,
                MakeStableEntityID(ProductID_, "manufacturing-part", _Item.Key),
                {},
                {},
                MakeStepFileName(_PartNumber)
            });
        }
        if (_Prepared.Parts.size() != static_cast<std::size_t>(Run_.GetPartCount()))
            throw std::runtime_error("stored neutral model export count does not match its generation run");
        auto _Converted = iCAX::OpenCascade::ConvertOpenCascadeShapesToBRep(_Conversions, 0.025);
        for (std::size_t _PartIndex = 0; _PartIndex < _Prepared.Parts.size(); ++_PartIndex)
        {
            auto& _Part = _Prepared.Parts[_PartIndex];
            const auto& _Input = _Conversions[_PartIndex];
            _Part.ManufacturingResource = StorePreparedBRep(
                Scene_, _Input.SourceID, _Input.DisplayName, std::move(_Converted[_PartIndex]));
            _Part.ThumbnailResource = iCAX::RenderInteraction::EnsureFrontendGeometryResource(
                Scene_.Resources(), _Part.ManufacturingResource.URL, iCAX::Render::ERenderGeometryKind::Mesh);
        }
        return _Prepared;
    }

    SPreparedProductDisassembly PrepareProductDisassembly(
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Project::ISceneContext& Scene_,
        const iCAX::Data::uuid& ProductID_)
    {
        auto& _Repository = Scene_.Database();
        const auto _ProductEntity = _Repository.GetEntity(ProductID_);
        const auto _Product = GetComponent<CProductInstanceComponent>(_ProductEntity);
        if (!_Product) throw std::invalid_argument("TubeDesigner product instance does not exist");
        const auto _GenerationRunID = _Product->GetActiveGenerationRunID();
        if (_GenerationRunID.is_nil())
            throw std::runtime_error("TubeDesigner product instance has no committed preview");

        const auto _Run = GetComponent<CGenerationRunComponent>(
            _Repository.GetEntity(_GenerationRunID));
        if (!_Run) throw std::runtime_error("TubeDesigner product generation run does not exist");
        if (_Run->GetNeutralModel().empty())
            throw std::invalid_argument("legacy C++ template products are no longer supported");
        return PrepareNeutralModelDisassembly(
            ApplicationContext_, Scene_, ProductID_, _GenerationRunID, *_Product, *_Run);
    }

    void RestoreLinkedNestingParts(
        iCAX::Project::ISceneContext& Scene_,
        const iCAX::Application::IApplicationContext& ApplicationContext_)
    {
        auto& _Repository = Scene_.Database();
        const auto _Root = GetComponent<CTubeDesignerRootComponent>(_Repository.GetMetaEntity());
        const auto _Task = _Root ? _Root->GetNestingTask() : ObjectMap();
        const auto _References = _Task.find("parts");
        if (_References == _Task.end() || !_References->second.Is<VariantArray>()) return;

        std::map<iCAX::Data::uuid, iCAX::Data::uuid> _RequiredProducts;
        for (const auto& _Value : _References->second.To<VariantArray>())
        {
            if (!_Value.Is<ObjectMap>()) continue;
            const auto _Reference = _Value.To<ObjectMap>();
            if (!IsProductNestingReference(_Reference)) continue;
            const auto _ProductID = ParseRequiredUuid(
                GetRequiredText(_Reference, "productEntityId"), "productEntityId");
            const auto _GenerationRunID = ParseRequiredUuid(
                GetRequiredText(_Reference, "generationRunId"), "generationRunId");
            _RequiredProducts[_ProductID] = _GenerationRunID;
        }
        if (_RequiredProducts.empty()) return;

        auto _PreparedProducts = CopyTransientDisassembly(Scene_);
        for (const auto& [_ProductID, _GenerationRunID] : _RequiredProducts)
        {
            const auto _Product = GetComponent<CProductInstanceComponent>(
                _Repository.GetEntity(_ProductID));
            if (!_Product || _Product->GetActiveGenerationRunID() != _GenerationRunID) continue;
            const auto _Available = std::find_if(
                _PreparedProducts.begin(), _PreparedProducts.end(),
                [&](const auto& _Prepared) {
                    return _Prepared.ProductID == _ProductID
                        && _Prepared.GenerationRunID == _GenerationRunID;
                });
            if (_Available != _PreparedProducts.end()) continue;

            auto _Prepared = PrepareProductDisassembly(
                ApplicationContext_, Scene_, _ProductID);
            {
                const std::lock_guard _Lock(gTransientDisassemblyMutex);
                gTransientDisassembly[Scene_.GetSceneID()].Products[
                    UuidToString(_ProductID)] = _Prepared;
            }
            _PreparedProducts.push_back(std::move(_Prepared));
        }
    }


    void RestoreExactBRep(iCAX::Project::ISceneContext& scene, const std::string& url,
        std::uint64_t version, iCAX::GeometryData::BRepModel model)
    {
        if (url.empty() || version == 0) throw std::runtime_error("Invalid saved geometry reference");
        iCAX::Resource::CResourceInfo info;
        info.ResourceTypeID = iCAX::GeometryData::BRepModel::kResourceTypeName;
        info.nSchemaVersion = 1; info.nVersion = version;
        info.Persistence = iCAX::Resource::EResourcePersistenceMode::RuntimeOnly;
        info.Metadata["source"] = "tube-designer";
        scene.Resources().Set(url, std::make_shared<iCAX::GeometryData::BRepModel>(std::move(model)), info);
    }

    void SyncNestingPersistence(iCAX::Project::ISceneContext& scene)
    {
        auto& db = scene.Database();
        std::set<std::string> embedded;
        const auto machiningRoot = GetComponent<CTubeDesignerRootComponent>(db.GetMetaEntity());
        const auto machining = machiningRoot ? machiningRoot->GetMachiningTask() : ObjectMap();
        if (machining.contains("jobs") && machining.at("jobs").Is<VariantArray>())
            for (const auto& value : machining.at("jobs").To<VariantArray>())
                if (value.Is<ObjectMap>()) embedded.insert(GetString(value.To<ObjectMap>(), "resourceId"));
        for (const auto& [entity, part] : Collect<CManufacturingPartComponent>(db))
            if (IsIndependentNestingPart(*part)) {
                embedded.insert(part->GetManufacturingGeometryResourceID());
                // A side-sketch edit is always rebuilt from this immutable
                // pre-sketch BRep.  Persist the base together with the final
                // part, otherwise reopening the project would make the next
                // edit either compound the cut or lose the edit base.
                embedded.insert(GetString(
                    part->GetItemProperties(), "tubeDesigner.sideSketchBaseResourceId"));
                const auto addConfigResources = [&embedded](const ObjectMap& value) {
                    embedded.insert(GetString(value, "baseResourceId"));
                    const auto addCut=[&](const ObjectMap& data) {
                        const auto cut=data.find("frozenCut");
                        if(cut!=data.end() && cut->second.Is<ObjectMap>())embedded.insert(GetString(cut->second.To<ObjectMap>(),"url"));
                    };
                    for(const auto& feature:ReadPunchFeatures(value,"features"))addCut(feature.TemplateData);
                    const auto ends=ReadPunchEnds(value);addCut(ends.Start.TemplateData);addCut(ends.End.TemplateData);
                };
                if (const auto _Drawing = GetComponent<CPartDrawingComponent>(entity))
                    if (!_Drawing->GetDefinition().empty()) addConfigResources(_Drawing->GetDefinition());
                if (const auto _Punch = GetComponent<CPunchWizardComponent>(entity))
                    if (!_Punch->GetDefinition().empty()) addConfigResources(_Punch->GetDefinition());
            }
        for (const auto& [entity, member] : Collect<CAssemblyMemberComponent>(db))
            embedded.insert(GetString(
                member->GetItemProperties(), "tubeDesigner.sideSketchBaseResourceId"));
        for (auto info : scene.Resources().GetInfos()) {
            if (info.ResourceTypeID != iCAX::GeometryData::BRepModel::kResourceTypeName
                || info.Metadata["source"] != "tube-designer") continue;
            const auto mode = embedded.contains(info.Key.Source)
                ? iCAX::Resource::EResourcePersistenceMode::Embedded : iCAX::Resource::EResourcePersistenceMode::RuntimeOnly;
            if (info.Persistence != mode) {
                info.Persistence = mode; info.nSchemaVersion = 1;
                if (!scene.Resources().UpdateInfo(info.Key.Source, info)) throw std::runtime_error("Cannot update drawing resource policy");
            }
        }
    }

    void RestoreDesignerResources(iCAX::Project::ISceneContext& scene,
        const iCAX::Application::IApplicationContext& application, bool manufacturing)
    {
        auto& db = scene.Database();
        auto& resources = scene.Resources();
        RestoreNestingResources(scene);
        const auto root = GetComponent<CTubeDesignerRootComponent>(db.GetMetaEntity());
        const auto task = root ? root->GetNestingTask() : ObjectMap();
        ObjectMap frozen;
        if (const auto it = task.find("manufacturingModels"); it != task.end() && it->second.Is<ObjectMap>()) frozen = it->second.To<ObjectMap>();
        for (const auto& [productEntity, product] : Collect<CProductInstanceComponent>(db)) {
            const auto run = GetComponent<CGenerationRunComponent>(db.GetEntity(product->GetActiveGenerationRunID()));
            if (!run || run->GetNeutralModel().empty()) continue;
            const auto savedModel = frozen.find(UuidToString(product->GetActiveGenerationRunID()));
            if (run->GetManufacturingModel().empty() && savedModel != frozen.end()) {
                if (!run->SetManufacturingModel(savedModel->second.To<ObjectMap>())) throw std::runtime_error("Cannot restore manufacturing recipe");
            }
            const auto members = Collect<CAssemblyMemberComponent>(db);
            bool needsDisplay = false;
            for (const auto& [entity, member] : members)
                if (member->GetProductID() == productEntity->GetID()
                    && !resources.Get<iCAX::GeometryData::BRepModel>(member->GetPreviewGeometryResourceID(), member->GetPreviewGeometryResourceVersion())) needsDisplay = true;
            if (needsDisplay) {
                const auto model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(Variant(run->GetNeutralModel()));
                ValidateGenerationModelIdentity(model, *run);
                const auto geometry = iCAX::OpenCascade::EvaluateNeutralModel(model);
                for (const auto& [entity, member] : members) {
                    if (member->GetProductID() != productEntity->GetID()) continue;
                    const auto& item = FindModelItem(model, member->GetStableKey());
                    const auto representation = item.Representations.find("result");
                    if (representation == item.Representations.end()) throw std::runtime_error("Saved display recipe has no result");
                    auto restoredShape = geometry.At(representation->second);
                    if (IsTubeManufacturingPart(item.Properties))
                        if (const auto sketch = FindProductSideSketch(
                            *product, entity->GetID(), member->GetStableKey()))
                        {
                            const auto applied = ApplyTubeSideSketch(restoredShape, *sketch);
                            if (!applied.bOK || applied.Shape.IsNull())
                                throw std::runtime_error(applied.Diagnostic.empty()
                                    ? "无法恢复产品侧面草图" : applied.Diagnostic);
                            restoredShape = applied.Shape;
                        }
                    RestoreExactBRep(scene, member->GetPreviewGeometryResourceID(), member->GetPreviewGeometryResourceVersion(),
                        iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(restoredShape, member->GetName(), member->GetPreviewGeometryResourceID(), 0.025));
                }
            }
            // Render buffers are derived, even when the source BRep was restored.
            if (needsDisplay) {
                (void)EnsureDesignerMaterial(scene);
                for (const auto& [entity, member] : members)
                    if (member->GetProductID() == productEntity->GetID())
                        (void)iCAX::RenderInteraction::EnsureFrontendGeometryResource(resources, member->GetPreviewGeometryResourceID(), iCAX::Render::ERenderGeometryKind::Mesh);
            }
            if (!manufacturing) continue;
            bool needsParts = false;
            const auto parts = Collect<CManufacturingPartComponent>(db);
            for (const auto& [entity, part] : parts)
                if (part->GetProductID() == productEntity->GetID() && part->GetGenerationRunID() == product->GetActiveGenerationRunID()
                    && !resources.Get<iCAX::GeometryData::BRepModel>(part->GetManufacturingGeometryResourceID(), part->GetManufacturingGeometryResourceVersion())) needsParts = true;
            if (needsParts) {
                const auto prepared = PrepareProductDisassembly(application, scene, productEntity->GetID());
                if (run->GetManufacturingModel().empty() && !run->SetManufacturingModel(prepared.ManufacturingModel))
                    throw std::runtime_error("Cannot cache manufacturing recipe");
                for (const auto& [entity, part] : parts) {
                    if (part->GetProductID() != productEntity->GetID() || part->GetGenerationRunID() != product->GetActiveGenerationRunID()) continue;
                    const auto current = resources.Get<iCAX::GeometryData::BRepModel>(part->GetManufacturingGeometryResourceID());
                    if (!current) throw std::runtime_error("Cannot rebuild manufacturing geometry");
                    if (resources.GetVersion(part->GetManufacturingGeometryResourceID()) != part->GetManufacturingGeometryResourceVersion())
                        RestoreExactBRep(scene, part->GetManufacturingGeometryResourceID(), part->GetManufacturingGeometryResourceVersion(), *current);
                }
            }
            for (const auto& [entity, part] : parts)
                if (part->GetProductID() == productEntity->GetID() && part->GetGenerationRunID() == product->GetActiveGenerationRunID())
                    (void)iCAX::RenderInteraction::EnsureFrontendGeometryResource(resources, part->GetManufacturingGeometryResourceID(), iCAX::Render::ERenderGeometryKind::Mesh);
        }
        SyncNestingPersistence(scene);
    }

    void SaveNestingTask(iCAX::Project::ISceneContext& scene, const VariantArray& selected,
        const ObjectMap& request = {}, const ObjectMap& result = {})
    {
        auto& db = scene.Database();
        const auto meta = db.GetMetaEntity();
        if (!meta) throw std::runtime_error("Missing project root");
        const auto root = GetComponent<CTubeDesignerRootComponent>(meta);
        const auto prepared = CopyTransientDisassembly(scene);
        VariantArray references;
        std::set<std::string> referencedIds;
        const auto addReference = [&](ObjectMap reference) {
            const auto id = GetString(reference, "partEntityId");
            if (!id.empty() && referencedIds.insert(id).second)
                references.emplace_back(std::move(reference));
        };

        // Product-linked parts are membership of the nesting area, not merely
        // the currently checked solver input. Keep all valid links, including
        // plate/accessory rows that are shown but excluded from tube nesting.
        const auto oldTask = root ? root->GetNestingTask() : ObjectMap();
        if (const auto oldReferences = oldTask.find("parts");
            oldReferences != oldTask.end() && oldReferences->second.Is<VariantArray>())
        {
            for (const auto& value : oldReferences->second.To<VariantArray>())
            {
                if (!value.Is<ObjectMap>()) continue;
                const auto reference = value.To<ObjectMap>();
                if (!IsProductNestingReference(reference)) continue;
                const auto id = GetString(reference, "partEntityId");
                if (id.empty()) continue;
                const auto source = FindTransientPartSource(
                    prepared, ParseRequiredUuid(id, "partEntityId"));
                const auto product = source
                    ? GetComponent<CProductInstanceComponent>(db.GetEntity(source->ProductID))
                    : nullptr;
                if (source && product
                    && product->GetActiveGenerationRunID() == source->GenerationRunID
                    && GetString(reference, "generationRunId")
                        == UuidToString(source->GenerationRunID))
                    addReference(reference);
            }
        }
        for (const auto& value : selected) {
            if (!value.Is<std::string>() && !value.Is<ObjectMap>())
                throw std::invalid_argument("下料零件项格式无效");
            const auto id = value.Is<std::string>() ? value.To<std::string>()
                : GetRequiredText(value.To<ObjectMap>(), "partEntityId");
            const auto partId = ParseRequiredUuid(id, "partEntityId");
            const auto part = GetComponent<CManufacturingPartComponent>(db.GetEntity(partId));
            if (part && IsIndependentNestingPart(*part))
            {
                if (!IsTubeManufacturingPart(part->GetItemProperties()))
                    throw std::invalid_argument("板件或配件不能加入管材排样");
                addReference(ObjectMap{{"partEntityId", id},
                    {"generationRunId", UuidToString(part->GetGenerationRunID())}});
                continue;
            }
            const auto source = FindTransientPartSource(prepared, partId);
            const auto product = source
                ? GetComponent<CProductInstanceComponent>(db.GetEntity(source->ProductID))
                : nullptr;
            if (!source || !product
                || product->GetActiveGenerationRunID() != source->GenerationRunID
                || !IsTubeManufacturingPart(source->Part.ItemProperties)
                || !referencedIds.contains(id))
                throw std::invalid_argument("排样零件没有关联到当前下料区");
        }
        ObjectMap task{{"parts", references}, {"manufacturingModels", ObjectMap()}, {"request", request}, {"result", result},
            {"revision", UuidToString(iCAX::Data::GenerateNewUUID())}};
        auto& transaction = db.BeginTransaction("Save TubeDesigner nesting task");
        bool committing = false;
        try {
            QueueUpsertComponent(transaction, meta, meta->GetID(), CTubeDesignerRootComponent::S_ClassName,
                {{CTubeDesignerRootComponent::PropertyName_NestingTask, PropertyValue(task)}});
            std::string error; committing = true;
            if (!db.CommitTransaction(transaction, error)) throw std::runtime_error(error);
        } catch (...) { if (!committing) { try { db.CancelTransaction(transaction); } catch (...) {} } throw; }
        SyncNestingPersistence(scene);
    }

    VariantArray StageIndependentNestingParts(iCAX::Project::ISceneContext& Scene_, const VariantArray& SourceIDs_)
    {
        auto& _DB = Scene_.Database();
        const auto _Meta = _DB.GetMetaEntity();
        if (!_Meta) throw std::runtime_error("Missing project root");
        struct SStageSource final
        {
            iCAX::Data::uuid ID;
            iCAX::Data::uuid ProductID;
            iCAX::Data::uuid GenerationRunID;
            std::string Name;
            std::string ProductName;
            std::string ProductCode;
            std::uint64_t ProductQuantity = 1;
            std::uint64_t Index = 0;
            std::string StableKey;
            std::string PartNumber;
            std::string Role;
            std::uint64_t Quantity = 1;
            double Length = 0.0;
            ObjectMap ItemProperties;
            ObjectMap TubeProfile;
            ObjectMap PartDrawingDefinition;
            ObjectMap PunchWizardDefinition;
            std::string FileName;
            std::string ManufacturingResourceID;
            std::uint64_t ManufacturingResourceVersion = 0;
        };
        std::map<iCAX::Data::uuid, iCAX::Data::uuid> _Batches;
        std::vector<std::pair<iCAX::Data::uuid, PropertySet>> _Copies;
        std::map<iCAX::Data::uuid, std::pair<std::string, ObjectMap>> _CopyDomainComponents;
        std::map<iCAX::Data::uuid, PropertySet> _CopyProfileComponents;
        std::map<iCAX::Data::uuid, PropertySet> _CopySourceComponents;
        VariantArray _References, _CreatedIDs;
        for (const auto& [_Entity, _Part] : Collect<CManufacturingPartComponent>(_DB))
            if (IsIndependentNestingPart(*_Part))
                _References.emplace_back(ObjectMap{{"partEntityId", UuidToString(_Entity->GetID())},
                    {"generationRunId", UuidToString(_Part->GetGenerationRunID())}});
        const auto _Transient = CopyTransientDisassembly(Scene_);
        const auto _FindTransient = [&](const iCAX::Data::uuid& ID_) -> const SPreparedManufacturingPart* {
            for (const auto& _Prepared : _Transient)
                for (const auto& _Part : _Prepared.Parts)
                    if (_Part.PartID == ID_) return &_Part;
            return nullptr;
        };
        std::vector<SStageSource> _Sources;
        std::set<std::string> _Seen;
        // Validate every source before publishing any copies.
        for (const auto& _Value : SourceIDs_)
        {
            const auto _Text = _Value.To<std::string>();
            if (!_Seen.insert(_Text).second) throw std::invalid_argument("下料零件重复");
            const auto _ID = ParseRequiredUuid(_Text, "partEntityId");
            const auto _Part = GetComponent<CManufacturingPartComponent>(_DB.GetEntity(_ID));
            if (_Part && IsIndependentNestingPart(*_Part))
                throw std::invalid_argument("该零件已经在下料区");
            SStageSource _Source;
            _Source.ID = _ID;
            if (_Part)
            {
                const auto _Product = GetComponent<CProductInstanceComponent>(_DB.GetEntity(_Part->GetProductID()));
                if (!_Product || _Product->GetActiveGenerationRunID() != _Part->GetGenerationRunID())
                    throw std::invalid_argument("零件已过期，请重新拆单");
                _Source.ProductID = _Part->GetProductID();
                _Source.GenerationRunID = _Part->GetGenerationRunID();
                _Source.Name = ResolvePartPresentation(_DB, *_Part).Name;
                _Source.ProductName = _Product->GetName();
                _Source.ProductCode = _Product->GetProductCode();
                _Source.ProductQuantity = _Product->GetQuantity();
                _Source.Index = _Part->GetPartIndex();
                _Source.StableKey = _Part->GetStableKey();
                _Source.PartNumber = _Part->GetPartNumber();
                _Source.Role = _Part->GetRole();
                _Source.Quantity = EffectiveManufacturingQuantity(*_Part, _Product->GetQuantity());
                _Source.Length = _Part->GetLength();
                _Source.ItemProperties = _Part->GetItemProperties();
                if (const auto _TubeProfile = GetComponent<CTubeProfileComponent>(_Part->GetEntity()))
                    _Source.TubeProfile = ProfileSnapshot(*_TubeProfile);
                else
                    throw std::invalid_argument("制造零件缺少管型组件，请重新生成拆单结果");
                if (const auto _Drawing = GetComponent<CPartDrawingComponent>(_Part->GetEntity());
                    _Drawing && !_Drawing->GetDefinition().empty())
                    _Source.PartDrawingDefinition = _Drawing->GetDefinition();
                if (const auto _Punch = GetComponent<CPunchWizardComponent>(_Part->GetEntity());
                    _Punch && !_Punch->GetDefinition().empty())
                    _Source.PunchWizardDefinition = _Punch->GetDefinition();
                _Source.FileName = _Part->GetFileName();
                _Source.ManufacturingResourceID = _Part->GetManufacturingGeometryResourceID();
                _Source.ManufacturingResourceVersion = _Part->GetManufacturingGeometryResourceVersion();
            }
            else if (const auto* _TransientPart = _FindTransient(_ID))
            {
                // The prepared record is keyed by its product in the transient
                // cache; resolve it below instead of requiring a DB part entity.
                const SPreparedProductDisassembly* _PreparedProduct = nullptr;
                for (const auto& _Candidate : _Transient)
                    if (std::find_if(_Candidate.Parts.begin(), _Candidate.Parts.end(),
                        [&](const auto& _CandidatePart) { return _CandidatePart.PartID == _ID; }) != _Candidate.Parts.end())
                    { _PreparedProduct = &_Candidate; break; }
                const auto _RealProduct = _PreparedProduct
                    ? GetComponent<CProductInstanceComponent>(_DB.GetEntity(_PreparedProduct->ProductID)) : nullptr;
                if (!_PreparedProduct || !_RealProduct
                    || _RealProduct->GetActiveGenerationRunID() != _PreparedProduct->GenerationRunID)
                    throw std::invalid_argument("临时拆单结果已过期，请重新拆单");
                _Source.ProductID = _PreparedProduct->ProductID;
                _Source.GenerationRunID = _PreparedProduct->GenerationRunID;
                _Source.Name = MakePartNameFromFileName(_TransientPart->FileName, _TransientPart->PartNumber);
                _Source.ProductName = _RealProduct->GetName();
                _Source.ProductCode = _RealProduct->GetProductCode();
                _Source.ProductQuantity = _RealProduct->GetQuantity();
                _Source.Index = _TransientPart->Index;
                _Source.StableKey = _TransientPart->StableKey;
                _Source.PartNumber = _TransientPart->PartNumber;
                _Source.Role = _TransientPart->Role;
                _Source.Quantity = ProductionQuantity(_TransientPart->Quantity, _RealProduct->GetQuantity());
                _Source.Length = _TransientPart->Length;
                _Source.ItemProperties = _TransientPart->ItemProperties;
                _Source.TubeProfile = _TransientPart->TubeProfile;
                _Source.FileName = _TransientPart->FileName;
                _Source.ManufacturingResourceID = _TransientPart->ManufacturingResource.URL;
                _Source.ManufacturingResourceVersion = _TransientPart->ManufacturingResource.nVersion;
            }
            else throw std::invalid_argument("请选择当前拆单结果中的制造零件");
            _Sources.push_back(std::move(_Source));
        }
        if (SourceIDs_.empty()) throw std::invalid_argument("请选择要加入下料的零件");
        for (const auto& _Source : _Sources)
        {
            auto& _Batch = _Batches[_Source.ProductID];
            if (_Batch.is_nil()) _Batch = iCAX::Data::GenerateNewUUID();
            const auto _ID = iCAX::Data::GenerateNewUUID();
            const auto _Geometry = Scene_.Resources().Get<iCAX::GeometryData::BRepModel>(
                _Source.ManufacturingResourceID, _Source.ManufacturingResourceVersion);
            if (!_Geometry) throw std::runtime_error("无法复制下料零件的几何资源");
            const auto _URL = Scene_.Resources().MakeNamedResourceURL("tube-designer/nesting/" + UuidToString(_ID));
            const auto _Resource = StorePreparedBRep(Scene_, _URL, _Source.Name, iCAX::GeometryData::BRepModel(*_Geometry));
            const auto _Thumbnail = iCAX::RenderInteraction::EnsureFrontendGeometryResource(
                Scene_.Resources(), _Resource.URL, iCAX::Render::ERenderGeometryKind::Mesh);
            PropertySet _Properties{
                {CManufacturingPartComponent::PropertyName_PartIndex, PropertyValue(_Source.Index)},
                {CManufacturingPartComponent::PropertyName_StableKey, PropertyValue(_Source.StableKey)},
                {CManufacturingPartComponent::PropertyName_PartNumber, PropertyValue(_Source.PartNumber)},
                {CManufacturingPartComponent::PropertyName_Name, PropertyValue(_Source.Name)},
                {CManufacturingPartComponent::PropertyName_Role, PropertyValue(_Source.Role)},
                {CManufacturingPartComponent::PropertyName_Length, PropertyValue(_Source.Length)},
                {CManufacturingPartComponent::PropertyName_FileName, PropertyValue(_Source.FileName)},
                {CManufacturingPartComponent::PropertyName_Status, PropertyValue(std::string("Ready"))}
            };
            auto _ItemProperties = _Source.ItemProperties;
            _ItemProperties.erase("nesting.source");
            _ItemProperties["nesting.snapshot"] = ObjectMap{{"name", _Source.ProductName},
                {"productCode", _Source.ProductCode}, {"quantity", _Source.ProductQuantity},
                {"source", std::string("product-disassembly")}};
            _Properties[CManufacturingPartComponent::PropertyName_ProductID] = iCAX::Data::uuid();
            _Properties[CManufacturingPartComponent::PropertyName_SourceMemberID] = iCAX::Data::uuid();
            _Properties[CManufacturingPartComponent::PropertyName_GenerationRunID] = _Batch;
            _Properties[CManufacturingPartComponent::PropertyName_Quantity] = _Source.Quantity;
            _Properties[CManufacturingPartComponent::PropertyName_QuantityOverride] = 0ull;
            _Properties[CManufacturingPartComponent::PropertyName_ItemProperties] = _ItemProperties;
            _Properties[CManufacturingPartComponent::PropertyName_ManufacturingGeometryResourceID] = _Resource.URL;
            _Properties[CManufacturingPartComponent::PropertyName_ManufacturingGeometryResourceVersion] = _Resource.nVersion;
            _Properties[CManufacturingPartComponent::PropertyName_ThumbnailGeometryResourceID] = _Thumbnail.URL;
            _Properties[CManufacturingPartComponent::PropertyName_ThumbnailGeometryResourceVersion] = _Thumbnail.nVersion;
            _Copies.emplace_back(_ID, std::move(_Properties));
            if (!_Source.TubeProfile.empty())
                _CopyProfileComponents.emplace(
                    _ID, TubeProfileComponentPropertiesFromSnapshot(_Source.TubeProfile));
            if (!_Source.PartDrawingDefinition.empty())
                _CopyDomainComponents[_ID] = std::make_pair(
                    CPartDrawingComponent::S_ClassName, _Source.PartDrawingDefinition);
            else if (!_Source.PunchWizardDefinition.empty())
                _CopyDomainComponents[_ID] = std::make_pair(
                    CPunchWizardComponent::S_ClassName, _Source.PunchWizardDefinition);
            if (!_Source.ProductID.is_nil())
                _CopySourceComponents.emplace(_ID, PropertySet{
                    {CNestingSourceComponent::PropertyName_SourceProductID, PropertyValue(_Source.ProductID)},
                    {CNestingSourceComponent::PropertyName_SourceGenerationRunID, PropertyValue(_Source.GenerationRunID)},
                    {CNestingSourceComponent::PropertyName_SourcePartEntityID, PropertyValue(_Source.ID)},
                    {CNestingSourceComponent::PropertyName_SourceProductName, PropertyValue(_Source.ProductName)},
                    {CNestingSourceComponent::PropertyName_SourceProductCode, PropertyValue(_Source.ProductCode)},
                    {CNestingSourceComponent::PropertyName_SourceStableKey, PropertyValue(_Source.StableKey)}
                });
            _CreatedIDs.emplace_back(UuidToString(_ID));
            _References.emplace_back(ObjectMap{{"partEntityId", UuidToString(_ID)}, {"generationRunId", UuidToString(_Batch)}});
        }
        const ObjectMap _Task{{"parts", _References}, {"request", ObjectMap()}, {"result", ObjectMap()},
            {"revision", UuidToString(iCAX::Data::GenerateNewUUID())}};
        auto _Undo = _DB.BeginUndoCommand("Add independent nesting parts");
        auto& _Transaction = _DB.BeginTransaction("Add independent nesting parts");
        bool _Committing = false;
        try
        {
            for (const auto& [_ID, _Properties] : _Copies)
            {
                _Transaction.CreateEntity(_ID);
                _Transaction.AttachComponent(_ID, CManufacturingPartComponent::S_ClassName, _Properties);
                if (const auto _Profile = _CopyProfileComponents.find(_ID);
                    _Profile != _CopyProfileComponents.end())
                    _Transaction.AttachComponent(
                        _ID, CTubeProfileComponent::S_ClassName, _Profile->second);
                if (const auto _Domain = _CopyDomainComponents.find(_ID);
                    _Domain != _CopyDomainComponents.end())
                    _Transaction.AttachComponent(_ID, _Domain->second.first,
                        {{ _Domain->second.first == CPartDrawingComponent::S_ClassName
                            ? CPartDrawingComponent::PropertyName_Definition
                            : CPunchWizardComponent::PropertyName_Definition,
                            PropertyValue(_Domain->second.second) }});
                if (const auto _Source = _CopySourceComponents.find(_ID);
                    _Source != _CopySourceComponents.end())
                    _Transaction.AttachComponent(_ID, CNestingSourceComponent::S_ClassName,
                        _Source->second);
            }
            QueueUpsertComponent(_Transaction, _Meta, _Meta->GetID(), CTubeDesignerRootComponent::S_ClassName,
                {{CTubeDesignerRootComponent::PropertyName_NestingTask, PropertyValue(_Task)}});
            std::string _Error; _Committing = true;
            if (!_DB.CommitTransaction(_Transaction, _Error)) throw std::runtime_error(_Error);
        }
        catch (...) { if (!_Committing) { try { _DB.CancelTransaction(_Transaction); } catch (...) {} } throw; }
        _Undo->End();
        SyncNestingPersistence(Scene_);
        return _CreatedIDs;
    }

    VariantArray LinkProductNestingParts(
        iCAX::Project::ISceneContext& Scene_, const VariantArray& SourceIDs_)
    {
        if (SourceIDs_.empty()) throw std::invalid_argument("请选择要加入下料的零件");
        auto& _DB = Scene_.Database();
        const auto _Meta = _DB.GetMetaEntity();
        if (!_Meta) throw std::runtime_error("Missing project root");
        const auto _Root = GetComponent<CTubeDesignerRootComponent>(_Meta);
        const auto _PreparedProducts = CopyTransientDisassembly(Scene_);

        VariantArray _References;
        std::set<std::string> _ReferencedIDs;
        const auto _AddReference = [&](ObjectMap Reference_) {
            const auto _ID = GetString(Reference_, "partEntityId");
            if (!_ID.empty() && _ReferencedIDs.insert(_ID).second)
                _References.emplace_back(std::move(Reference_));
        };
        for (const auto& [_Entity, _Part] : Collect<CManufacturingPartComponent>(_DB))
            if (IsIndependentNestingPart(*_Part))
                _AddReference(ObjectMap{{"partEntityId", UuidToString(_Entity->GetID())},
                    {"generationRunId", UuidToString(_Part->GetGenerationRunID())}});

        const auto _OldTask = _Root ? _Root->GetNestingTask() : ObjectMap();
        if (const auto _OldReferences = _OldTask.find("parts");
            _OldReferences != _OldTask.end() && _OldReferences->second.Is<VariantArray>())
        {
            for (const auto& _Value : _OldReferences->second.To<VariantArray>())
            {
                if (!_Value.Is<ObjectMap>()) continue;
                const auto _Reference = _Value.To<ObjectMap>();
                if (!IsProductNestingReference(_Reference)) continue;
                const auto _IDText = GetString(_Reference, "partEntityId");
                if (_IDText.empty()) continue;
                const auto _Source = FindTransientPartSource(
                    _PreparedProducts, ParseRequiredUuid(_IDText, "partEntityId"));
                if (!_Source) continue;
                const auto _Product = GetComponent<CProductInstanceComponent>(
                    _DB.GetEntity(_Source->ProductID));
                if (_Product && _Product->GetActiveGenerationRunID() == _Source->GenerationRunID)
                    if (GetString(_Reference, "generationRunId")
                        == UuidToString(_Source->GenerationRunID))
                        _AddReference(_Reference);
            }
        }

        VariantArray _LinkedIDs;
        std::set<std::string> _Selection;
        for (const auto& _Value : SourceIDs_)
        {
            if (!_Value.Is<std::string>())
                throw std::invalid_argument("下料零件编号必须是字符串");
            const auto _IDText = _Value.To<std::string>();
            if (!_Selection.insert(_IDText).second)
                throw std::invalid_argument("下料零件重复");
            const auto _PartID = ParseRequiredUuid(_IDText, "partEntityId");
            if (const auto _Persistent = GetComponent<CManufacturingPartComponent>(
                    _DB.GetEntity(_PartID));
                _Persistent && IsIndependentNestingPart(*_Persistent))
                throw std::invalid_argument("该零件已经在下料区");

            const SPreparedProductDisassembly* _PreparedProduct = nullptr;
            const SPreparedManufacturingPart* _PreparedPart = nullptr;
            for (const auto& _Candidate : _PreparedProducts)
            {
                const auto _Part = std::find_if(
                    _Candidate.Parts.begin(), _Candidate.Parts.end(),
                    [&](const auto& _Item) { return _Item.PartID == _PartID; });
                if (_Part == _Candidate.Parts.end()) continue;
                _PreparedProduct = &_Candidate;
                _PreparedPart = &*_Part;
                break;
            }
            const auto _Product = _PreparedProduct
                ? GetComponent<CProductInstanceComponent>(
                    _DB.GetEntity(_PreparedProduct->ProductID)) : nullptr;
            if (!_PreparedProduct || !_PreparedPart || !_Product
                || _Product->GetActiveGenerationRunID() != _PreparedProduct->GenerationRunID)
                throw std::invalid_argument("请选择当前拆单结果中的制造零件");
            _AddReference(MakeProductNestingReference(*_PreparedProduct, *_PreparedPart));
            _LinkedIDs.emplace_back(_IDText);
        }

        const ObjectMap _Task{{"parts", _References}, {"manufacturingModels", ObjectMap()},
            {"request", ObjectMap()}, {"result", ObjectMap()},
            {"revision", UuidToString(iCAX::Data::GenerateNewUUID())}};
        auto _Undo = _DB.BeginUndoCommand("Link product parts to nesting");
        auto& _Transaction = _DB.BeginTransaction("Link product parts to nesting");
        bool _Committing = false;
        try
        {
            QueueUpsertComponent(_Transaction, _Meta, _Meta->GetID(),
                CTubeDesignerRootComponent::S_ClassName,
                {{CTubeDesignerRootComponent::PropertyName_NestingTask,
                    PropertyValue(_Task)}});
            std::string _Error;
            _Committing = true;
            if (!_DB.CommitTransaction(_Transaction, _Error))
                throw std::runtime_error(_Error.empty() ? "关联下料零件失败" : _Error);
        }
        catch (...)
        {
            if (!_Committing) { try { _DB.CancelTransaction(_Transaction); } catch (...) {} }
            throw;
        }
        _Undo->End();
        return _LinkedIDs;
    }

    void MigrateLegacyNestingTask(iCAX::Project::ISceneContext& Scene_,
        const iCAX::Application::IApplicationContext& Application_)
    {
        auto& _DB = Scene_.Database();
        const auto _Root = GetComponent<CTubeDesignerRootComponent>(_DB.GetMetaEntity());
        const auto _Task = _Root ? _Root->GetNestingTask() : ObjectMap();
        const auto _Refs = _Task.find("parts");
        if (_Refs == _Task.end() || !_Refs->second.Is<VariantArray>()) return;
        VariantArray _Sources;
        for (const auto& _Value : _Refs->second.To<VariantArray>())
        {
            const auto _ID = GetString(_Value.To<ObjectMap>(), "partEntityId");
            const auto _Part = GetComponent<CManufacturingPartComponent>(_DB.GetEntity(ParseRequiredUuid(_ID, "partEntityId")));
            if (_Part && !IsIndependentNestingPart(*_Part)) _Sources.emplace_back(_ID);
        }
        if (_Sources.empty()) return;
        RestoreDesignerResources(Scene_, Application_, true);
        const auto _NewIDs = StageIndependentNestingParts(Scene_, _Sources);
        std::map<std::string, std::string> _IDMap;
        for (std::size_t _Index = 0; _Index < _Sources.size(); ++_Index)
            _IDMap[_Sources[_Index].To<std::string>()] = _NewIDs[_Index].To<std::string>();
        const auto _Remap = [&_IDMap](const auto& Self_, const Variant& Value_) -> Variant {
            if (Value_.Is<std::string>())
            {
                const auto _Text = Value_.To<std::string>();
                const auto _Found = _IDMap.find(_Text);
                return _Found == _IDMap.end() ? Value_ : Variant(_Found->second);
            }
            if (Value_.Is<VariantArray>())
            {
                auto _Items = Value_.To<VariantArray>();
                for (auto& _Item : _Items) _Item = Self_(Self_, _Item);
                return _Items;
            }
            if (Value_.Is<ObjectMap>())
            {
                auto _Items = Value_.To<ObjectMap>();
                for (auto& [_Key, _Item] : _Items) _Item = Self_(Self_, _Item);
                return _Items;
            }
            return Value_;
        };
        auto _Request = _Task.contains("request") ? _Remap(_Remap, _Task.at("request")).To<ObjectMap>() : ObjectMap();
        if (_Request.contains("parts"))
        {
            auto _Parts = _Request.at("parts").To<VariantArray>();
            for (auto& _Value : _Parts) { auto _Part = _Value.To<ObjectMap>(); _Part.erase("instanceQuantity"); _Value = _Part; }
            _Request["parts"] = _Parts;
        }
        const auto _Result = _Task.contains("result") ? _Remap(_Remap, _Task.at("result")).To<ObjectMap>() : ObjectMap();
        const auto _Selected = _Remap(_Remap, _Refs->second).To<VariantArray>();
        SaveNestingTask(Scene_, _Selected, _Request, _Result);
    }

    iCAX::Interaction::CInvocationResult HandleStageNestingParts(
        const iCAX::Interaction::CInvocation& request, const iCAX::Application::IApplicationContext& application,
        iCAX::Product::IProductContext*, iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext* scene)
    {
        if (!scene || request.Payload.size() > 1024 * 1024) throw std::invalid_argument("Invalid nesting task request");
        tube::license::Enforce<403, tube::license::Feature::Production>();
        const auto payload = DecodeObjectPayload(request);
        const auto found = payload.find("partEntityIds");
        if (found == payload.end() || !found->second.Is<VariantArray>() || found->second.To<VariantArray>().size() > 2000)
            throw std::invalid_argument("请选择至多 2000 种下料零件");
        RestoreLinkedNestingParts(*scene, application);
        // Entering the nesting area takes a snapshot of the selected source
        // parts.  From this point on nesting owns those copies; product
        // disassembly generations are only an input source and must not keep a
        // lifecycle link that can later make an otherwise valid nesting row
        // look stale during export.
        const auto ids = StageIndependentNestingParts(*scene, found->second.To<VariantArray>());
        auto response = BuildSnapshot(*scene, application, false);
        response["staged"] = true;
        response["partEntityIds"] = ids;
        return MakeResponse(response);
    }

    std::string ImportedDimensionText(const double Value_)
    {
        std::ostringstream _Text;
        _Text << std::fixed << std::setprecision(3) << Value_;
        auto _Result = _Text.str();
        while (_Result.size() > 1 && _Result.back() == '0') _Result.pop_back();
        if (!_Result.empty() && _Result.back() == '.') _Result.pop_back();
        return _Result;
    }

    iCAX::Interaction::CInvocationResult HandleImportNestingPart(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext*, iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_ || Request_.Payload.size() > 1024 * 1024)
            throw std::invalid_argument("导入下料零件请求无效");
        tube::license::Enforce<403, tube::license::Feature::Production>();
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _SourcePath = Utf8Path(GetRequiredText(_Payload, "sourcePath", 32767));
        const auto _Snapshot = ImportManufacturingPartFile(_SourcePath);
        auto _Shape = ComponentModelShape(_Snapshot);
        const auto _ImportedBRep = iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(
            _Shape, GetString(_Snapshot, "name", "导入管材"),
            GetString(_Snapshot, "geometryDigest"), 0.001);
        const auto _Fitters = iCAX::ExtrusionRecognition::DiscoverPythonSectionFitters(
            PathToUTF8(ResolveSystemProfileRoot(ApplicationContext_)));
        iCAX::ExtrusionRecognition::CExtrusionRecognitionService _RecognitionService;
        const auto _Recognition = _RecognitionService.RecognizePythonFitters(
            _ImportedBRep, _Fitters);
        if (_Recognition.Status != iCAX::ExtrusionRecognition::ERecognitionStatus::Success
            && _Recognition.Status != iCAX::ExtrusionRecognition::ERecognitionStatus::SectionTypeNotMatched)
        {
            throw std::invalid_argument(
                "导入实体不是可识别的线性拉伸管材："
                + (_Recognition.Diagnostics.empty()
                    ? std::string("无法提取主方向或截面轮廓")
                    : _Recognition.Diagnostics.back()));
        }
        const auto _Length = _Recognition.dLength;
        const auto _SectionWidth = _Recognition.Section.Bounds.Max.X
            - _Recognition.Section.Bounds.Min.X;
        const auto _SectionHeight = _Recognition.Section.Bounds.Max.Y
            - _Recognition.Section.Bounds.Min.Y;
        if (!std::isfinite(_Length) || _Length <= 0.02
            || !std::isfinite(_SectionWidth) || !std::isfinite(_SectionHeight)
            || _SectionWidth <= 0.02 || _SectionHeight <= 0.02
            || _Length <= std::max(_SectionWidth, _SectionHeight) * 1.1)
            throw std::invalid_argument("导入实体不是可用于直管排样的线性零件");

        const auto _SourceFormat = GetString(_Snapshot, "sourceFormat", "step");
        const auto _Profile = ProfileSnapshot(_Recognition, "geometry-import:" + _SourceFormat);
        const auto _Normalized = iCAX::OpenCascade::BuildOpenCascadeShape(
            _Recognition.NormalizedGeometry);
        if (!_Normalized.bOK || _Normalized.Shape.IsNull())
            throw std::runtime_error("无法生成管型识别后的标准化实体");
        _Shape = _Normalized.Shape;
        auto _Name = TrimText(GetString(_Payload, "name"));
        if (_Name.empty())
            _Name = MakePartNameFromFileName(GetString(_Snapshot, "sourceFileName"), "导入零件");
        if (_Name.empty() || _Name.size() > 160 || _Name.find('\0') != std::string::npos)
            throw std::invalid_argument("导入零件名称不能为空且不能超过 160 个字符");
        const auto _Material = TrimText(GetString(_Payload, "material"));
        if (_Material.size() > 240 || _Material.find('\0') != std::string::npos)
            throw std::invalid_argument("材料名称过长或包含无效字符");
        const auto _Quantity = ValidateInstanceQuantity(GetUInt64(_Payload, "quantity", 1));

        const auto _PartID = iCAX::Data::GenerateNewUUID();
        const auto _BatchID = iCAX::Data::GenerateNewUUID();
        const auto _PartNumber = MakeSafePathSegment(
            _Name + "-" + UuidToString(_PartID).substr(0, 8), "imported-part");
        const auto _FileName = MakeStepFileName(_PartNumber);
        std::uint64_t _NextIndex = 1;
        auto& _DB = Scene_->Database();
        for (const auto& [_Entity, _Part] : Collect<CManufacturingPartComponent>(_DB))
            if (IsIndependentNestingPart(*_Part) && _Part->GetPartIndex() >= _NextIndex
                && _Part->GetPartIndex() < (std::numeric_limits<std::uint64_t>::max)())
                _NextIndex = _Part->GetPartIndex() + 1;

        const auto _Resource = StoreBRep(*Scene_,
            "tube-designer/nesting/import/" + UuidToString(_PartID), _Name, _Shape);
        const auto _Thumbnail = iCAX::RenderInteraction::EnsureFrontendGeometryResource(
            Scene_->Resources(), _Resource.URL, iCAX::Render::ERenderGeometryKind::Mesh);
        const auto _Bounds = ShapeBounds(_Shape);
        const auto _EnvelopeLength = GetDouble(_Bounds, "width", 0.0);
        if (!std::isfinite(_EnvelopeLength) || _EnvelopeLength <= 0.02)
            throw std::invalid_argument("导入零件的轴向包络长度无效");
        auto _Measurement = MeasureFinalPartGeometry(_Shape, _Resource.URL, _Resource.nVersion);
        _Measurement["nestingEnvelopeLength"] = _EnvelopeLength;
        _Measurement["normalizedAxis"] = std::string("+X");

        ObjectMap _ItemProperties{
            { "manufacturing.partKind", std::string("tube") },
            { "manufacturing.materialCategory", std::string("tube") },
            { "manufacturing.sourcing", std::string("made") },
            { "manufacturing.process", std::string("straight-cut") },
            { "manufacturing.requiresBending", false },
            { "manufacturing.material", _Material },
            { "manufacturing.categoryName", std::string("导入零件") },
            { "manufacturing.imported", true },
            { "manufacturing.import.sourceFileName", GetString(_Snapshot, "sourceFileName") },
            { "manufacturing.import.sourceFormat", _SourceFormat },
            { "manufacturing.import.geometryDigest", GetString(_Snapshot, "geometryDigest") },
            { "manufacturing.geometryMeasurement", _Measurement },
            { "nesting.snapshot", ObjectMap{
                { "name", _Name },
                { "quantity", 1ull },
                { "source", std::string("direct-import") },
                { "sourceFileName", GetString(_Snapshot, "sourceFileName") },
                { "sourceFormat", _SourceFormat } } }
        };
        iCAX::Data::PropertySet _Properties{
            { CManufacturingPartComponent::PropertyName_ProductID, PropertyValue(iCAX::Data::uuid()) },
            { CManufacturingPartComponent::PropertyName_SourceMemberID, PropertyValue(iCAX::Data::uuid()) },
            { CManufacturingPartComponent::PropertyName_GenerationRunID, PropertyValue(_BatchID) },
            { CManufacturingPartComponent::PropertyName_PartIndex, PropertyValue(_NextIndex) },
            { CManufacturingPartComponent::PropertyName_StableKey, PropertyValue("imported/" + UuidToString(_PartID)) },
            { CManufacturingPartComponent::PropertyName_PartNumber, PropertyValue(_PartNumber) },
            { CManufacturingPartComponent::PropertyName_Name, PropertyValue(_Name) },
            { CManufacturingPartComponent::PropertyName_Role, PropertyValue(std::string("导入零件")) },
            { CManufacturingPartComponent::PropertyName_Quantity, PropertyValue(_Quantity) },
            { CManufacturingPartComponent::PropertyName_QuantityOverride, PropertyValue(0ull) },
            { CManufacturingPartComponent::PropertyName_Length, PropertyValue(std::max(_Length, _EnvelopeLength)) },
            { CManufacturingPartComponent::PropertyName_ManufacturingGeometryResourceID, PropertyValue(_Resource.URL) },
            { CManufacturingPartComponent::PropertyName_ManufacturingGeometryResourceVersion, PropertyValue(_Resource.nVersion) },
            { CManufacturingPartComponent::PropertyName_ThumbnailGeometryResourceID, PropertyValue(_Thumbnail.URL) },
            { CManufacturingPartComponent::PropertyName_ThumbnailGeometryResourceVersion, PropertyValue(_Thumbnail.nVersion) },
            { CManufacturingPartComponent::PropertyName_FileName, PropertyValue(_FileName) },
            { CManufacturingPartComponent::PropertyName_Status, PropertyValue(std::string("Ready")) },
            { CManufacturingPartComponent::PropertyName_ItemProperties, PropertyValue(_ItemProperties) }
        };

        VariantArray _References;
        for (const auto& [_Entity, _Part] : Collect<CManufacturingPartComponent>(_DB))
            if (IsIndependentNestingPart(*_Part))
                _References.emplace_back(ObjectMap{
                    { "partEntityId", UuidToString(_Entity->GetID()) },
                    { "generationRunId", UuidToString(_Part->GetGenerationRunID()) } });
        _References.emplace_back(ObjectMap{
            { "partEntityId", UuidToString(_PartID) },
            { "generationRunId", UuidToString(_BatchID) } });
        const ObjectMap _Task{
            { "parts", _References }, { "request", ObjectMap() }, { "result", ObjectMap() },
            { "revision", UuidToString(iCAX::Data::GenerateNewUUID()) } };

        auto _Undo = _DB.BeginUndoCommand("Import nesting part");
        auto& _Transaction = _DB.BeginTransaction("Import nesting part");
        bool _Committing = false;
        try
        {
            _Transaction.CreateEntity(_PartID);
            _Transaction.AttachComponent(_PartID, CManufacturingPartComponent::S_ClassName, _Properties);
            _Transaction.AttachComponent(
                _PartID,
                CTubeProfileComponent::S_ClassName,
                TubeProfileComponentPropertiesFromSnapshot(_Profile));
            const auto _Meta = _DB.GetMetaEntity();
            if (!_Meta) throw std::runtime_error("Missing project root");
            QueueUpsertComponent(_Transaction, _Meta, _Meta->GetID(), CTubeDesignerRootComponent::S_ClassName,
                {{ CTubeDesignerRootComponent::PropertyName_NestingTask, PropertyValue(_Task) }});
            std::string _Error;
            _Committing = true;
            if (!_DB.CommitTransaction(_Transaction, _Error))
                throw std::runtime_error(_Error.empty() ? "导入下料零件失败" : _Error);
        }
        catch (...)
        {
            if (!_Committing) { try { _DB.CancelTransaction(_Transaction); } catch (...) {} }
            throw;
        }
        _Undo->End();
        SyncNestingPersistence(*Scene_);
        auto _Response = BuildSnapshot(*Scene_, ApplicationContext_, false);
        _Response["partEntityId"] = UuidToString(_PartID);
        _Response["profile"] = _Profile;
        _Response["recognition"] = _Measurement;
        _Response["sourceFileName"] = GetString(_Snapshot, "sourceFileName");
        return MakeResponse(_Response);
    }

    iCAX::Interaction::CInvocationResult HandleDeleteNestingParts(
        const iCAX::Interaction::CInvocation& Request_, const iCAX::Application::IApplicationContext& Application_,
        iCAX::Product::IProductContext*, iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_ || Request_.Payload.size() > 1024 * 1024) throw std::invalid_argument("Invalid nesting deletion request");
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _Found = _Payload.find("partEntityIds");
        if (_Found == _Payload.end() || !_Found->second.Is<VariantArray>()) throw std::invalid_argument("请选择下料零件");
        RestoreLinkedNestingParts(*Scene_, Application_);
        auto& _DB = Scene_->Database();
        std::set<iCAX::Data::uuid> _IDs;
        std::set<std::string> _IDTexts;
        const auto _LinkedPartIDs = LinkedNestingPartIDs(*Scene_);
        for (const auto& _Value : _Found->second.To<VariantArray>())
        {
            const auto _ID = ParseRequiredUuid(_Value.To<std::string>(), "partEntityId");
            const auto _Part = GetComponent<CManufacturingPartComponent>(_DB.GetEntity(_ID));
            const auto _IDText = UuidToString(_ID);
            if (_Part && IsIndependentNestingPart(*_Part)) _IDs.insert(_ID);
            else if (!_LinkedPartIDs.contains(_IDText))
                throw std::invalid_argument("只能删除下料区中的零件");
            _IDTexts.insert(_IDText);
        }
        if (_IDTexts.empty()) throw std::invalid_argument("请选择下料零件");
        VariantArray _References;
        for (const auto& [_Entity, _Part] : Collect<CManufacturingPartComponent>(_DB))
            if (IsIndependentNestingPart(*_Part) && !_IDs.contains(_Entity->GetID()))
                _References.emplace_back(ObjectMap{{"partEntityId", UuidToString(_Entity->GetID())},
                    {"generationRunId", UuidToString(_Part->GetGenerationRunID())}});
        const auto _Root = GetComponent<CTubeDesignerRootComponent>(_DB.GetMetaEntity());
        const auto _OldTask = _Root ? _Root->GetNestingTask() : ObjectMap();
        if (const auto _OldReferences = _OldTask.find("parts");
            _OldReferences != _OldTask.end() && _OldReferences->second.Is<VariantArray>())
            for (const auto& _Value : _OldReferences->second.To<VariantArray>())
            {
                if (!_Value.Is<ObjectMap>()) continue;
                const auto _Reference = _Value.To<ObjectMap>();
                const auto _ID = GetString(_Reference, "partEntityId");
                if (IsProductNestingReference(_Reference)
                    && _LinkedPartIDs.contains(_ID) && !_IDTexts.contains(_ID))
                    _References.emplace_back(_Reference);
            }
        ObjectMap _Task{{"parts", _References}, {"request", ObjectMap()}, {"result", ObjectMap()},
            {"revision", UuidToString(iCAX::Data::GenerateNewUUID())}};
        const auto _Meta = _DB.GetMetaEntity();
        auto _Undo = _DB.BeginUndoCommand("Delete independent nesting parts");
        auto& _Transaction = _DB.BeginTransaction("Delete independent nesting parts");
        bool _Committing = false;
        try
        {
            for (const auto& _ID : _IDs) _Transaction.DisposeEntity(_ID);
            QueueUpsertComponent(_Transaction, _Meta, _Meta->GetID(), CTubeDesignerRootComponent::S_ClassName,
                {{CTubeDesignerRootComponent::PropertyName_NestingTask, PropertyValue(_Task)}});
            std::string _Error; _Committing = true;
            if (!_DB.CommitTransaction(_Transaction, _Error)) throw std::runtime_error(_Error);
        }
        catch (...) { if (!_Committing) { try { _DB.CancelTransaction(_Transaction); } catch (...) {} } throw; }
        _Undo->End();
        ReleaseTransientParts(*Scene_, _IDTexts);
        SyncNestingPersistence(*Scene_);
        return MakeResponse(Variant(BuildSnapshot(*Scene_, Application_, false)));
    }

    TopoDS_Shape RecoverManufacturingShapeFromGeneration(
        iCAX::Database::IRepository& Repository_,
        const CManufacturingPartComponent& Part_)
    {
        const auto _Run = GetComponent<CGenerationRunComponent>(
            Repository_.GetEntity(Part_.GetGenerationRunID()));
        if (!_Run || _Run->GetProductID() != Part_.GetProductID())
            throw std::runtime_error("generation run has no neutral model for BRep recovery");
        const auto _Document = _Run->GetManufacturingModel().empty()
            ? _Run->GetNeutralModel() : _Run->GetManufacturingModel();
        const auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(
            Variant(_Document));
        ValidateGenerationModelIdentity(_Model, *_Run);
        if (!_Run->GetManufacturingModel().empty())
        {
            const auto _PreviewModel = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(
                Variant(_Run->GetNeutralModel()));
            ValidateGenerationModelIdentity(_PreviewModel, *_Run);
            if (_Model.Parameters != _PreviewModel.Parameters)
                throw std::runtime_error("manufacturing recovery parameters do not match the generation run");
        }
        const auto& _Output = ManufacturingOutput(_Model);
        if (std::find(_Output.ItemKeys.begin(), _Output.ItemKeys.end(), Part_.GetStableKey())
            == _Output.ItemKeys.end())
            throw std::runtime_error("manufacturing item is not part of its generation result");
        const auto& _Item = FindModelItem(_Model, Part_.GetStableKey());
        const auto _Representation = _Item.Representations.find(_Output.Purpose);
        if (_Representation == _Item.Representations.end())
            throw std::runtime_error("manufacturing item has no result representation");
        const auto _Geometry = iCAX::OpenCascade::EvaluateNeutralModel(
            _Model, { _Representation->second });
        return NormalizeManufacturingShape(_Geometry.At(_Representation->second), _Item.Properties);
    }

    iCAX::Interaction::CInvocationResult HandleReleaseTransientParts(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext*, iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_ || Request_.Payload.size() > 1024 * 1024)
            throw std::invalid_argument("Invalid transient part release request");
        const auto _Payload = DecodeObjectPayload(Request_);
        std::set<std::string> _PartIDs;
        if (const auto _Selection = _Payload.find("partEntityIds");
            _Selection != _Payload.end())
        {
            if (!_Selection->second.Is<VariantArray>())
                throw std::invalid_argument("partEntityIds must be an array");
            for (const auto& _Value : _Selection->second.To<VariantArray>())
                if (_Value.Is<std::string>()) _PartIDs.insert(_Value.To<std::string>());
        }
        ReleaseTransientParts(*Scene_, _PartIDs);
        return MakeResponse(Variant(BuildSnapshot(*Scene_, ApplicationContext_, false)));
    }

    iCAX::Interaction::CInvocationResult HandleDisassemble(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_) throw std::invalid_argument("TubeDesigner.Disassemble requires a scene");
        tube::license::Enforce<201, tube::license::Feature::Breakdown>();
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

        // A new temporary list supersedes previous export-only lists. Keep
        // only parts already referenced by nesting before preparing this one.
        ReleaseTransientParts(*Scene_, {});
        const auto _ExistingPrepared = CopyTransientDisassembly(*Scene_);
        std::vector<SPreparedProductDisassembly> _PreparedProducts;
        _PreparedProducts.reserve(_ProductIDs.size());
        for (const auto& _ProductID : _ProductIDs)
        {
            const auto _Product = GetComponent<CProductInstanceComponent>(
                _Repository.GetEntity(_ProductID));
            const auto _Run = _Product ? GetComponent<CGenerationRunComponent>(
                _Repository.GetEntity(_Product->GetActiveGenerationRunID())) : nullptr;
            const auto _Existing = std::find_if(
                _ExistingPrepared.begin(), _ExistingPrepared.end(),
                [&](const auto& _Prepared) {
                    return _Product && _Prepared.ProductID == _ProductID
                        && _Prepared.GenerationRunID == _Product->GetActiveGenerationRunID()
                        && _Run
                        && _Prepared.Parts.size() == static_cast<std::size_t>(_Run->GetPartCount());
                });
            _PreparedProducts.push_back(_Existing != _ExistingPrepared.end()
                ? *_Existing
                : PrepareProductDisassembly(ApplicationContext_, *Scene_, _ProductID));
        }

        {
            const std::lock_guard _Lock(gTransientDisassemblyMutex);
            auto& _State = gTransientDisassembly[Scene_->GetSceneID()];
            for (auto& _Prepared : _PreparedProducts)
                _State.Products[UuidToString(_Prepared.ProductID)] = std::move(_Prepared);
        }
        return MakeResponse(Variant(BuildSnapshot(*Scene_, ApplicationContext_)));
    }

    iCAX::Interaction::CInvocationResult HandleMeasurePartGeometry(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
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
        const auto _TransientPart = FindTransientPart(*Scene_, _PartID);
        if (!_Part && !_TransientPart)
        {
            throw std::invalid_argument("manufacturing part does not exist");
        }
        const auto _Independent = _Part && IsIndependentNestingPart(*_Part);
        if (_Independent) RestoreNestingResources(*Scene_);
        else if (!_TransientPart) RestoreDesignerResources(*Scene_, ApplicationContext_, true);
        const auto _Product = _Part ? GetComponent<CProductInstanceComponent>(
            _Repository.GetEntity(_Part->GetProductID())) : nullptr;
        if (!_Independent && !_TransientPart && (!_Product
            || _Product->GetActiveGenerationRunID() != _Part->GetGenerationRunID()))
        {
            throw std::runtime_error("manufacturing part is stale");
        }

        const auto _ResourceID = _Part
            ? _Part->GetManufacturingGeometryResourceID() : _TransientPart->ManufacturingResource.URL;
        const auto _ResourceVersion = _Part
            ? _Part->GetManufacturingGeometryResourceVersion() : _TransientPart->ManufacturingResource.nVersion;
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
        {
            const std::lock_guard _Lock(gPartMeasurementCacheMutex);
            const auto _Cached = gPartMeasurementCache.find(_CacheKey);
            if (_Cached != gPartMeasurementCache.end())
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
        auto _FinalShape = _Rebuilt.Shape;
        auto _RecoveredFromGeneration = false;
        if (!_Rebuilt.bOK || _FinalShape.IsNull())
        {
            try
            {
                if (_Part)
                {
                    _FinalShape = RecoverManufacturingShapeFromGeneration(
                        _Repository, *_Part);
                    _RecoveredFromGeneration = !_FinalShape.IsNull();
                }
            }
            catch (const std::exception& _RecoveryError)
            {
                std::ostringstream _Message;
                _Message << "无法读取最终零件几何";
                for (const auto& _Diagnostic : _Rebuilt.Diagnostics)
                {
                    _Message << ": " << _Diagnostic;
                }
                _Message << ": recovery failed: " << _RecoveryError.what();
                throw std::runtime_error(_Message.str());
            }
        }
        if (_FinalShape.IsNull())
        {
            throw std::runtime_error("无法读取最终零件几何: recovered shape is empty");
        }
        const auto _Properties = _Part ? _Part->GetItemProperties() : _TransientPart->ItemProperties;
        const auto _Kind = ManufacturingPartKind(_Properties);
        ObjectMap _Measurement;
        if (_Kind == "accessory")
            _Measurement = { { "source", std::string("final-brep") }, { "available", true },
                { "partKind", _Kind }, { "length", 0.0 }, { "bounds", ShapeBounds(_FinalShape) },
                { "features", VariantArray{} }, { "resourceId", _ResourceID },
                { "resourceVersion", static_cast<unsigned long long>(_ResourceVersion) } };
        else
        {
            _Measurement = _Kind == "plate" || _Kind == "glass"
                ? MeasureFinalPlateGeometry(_FinalShape, _ResourceID, _ResourceVersion)
                : MeasureFinalPartGeometry(_FinalShape, _ResourceID, _ResourceVersion);
            if (_Kind == "glass") _Measurement["partKind"] = _Kind;
        }
        _Measurement["partEntityId"] = UuidToString(_PartID);
        _Measurement["recoveredFromGeneration"] = _RecoveredFromGeneration;
        {
            const std::lock_guard _Lock(gPartMeasurementCacheMutex);
            if (gPartMeasurementCache.size() > 10000) gPartMeasurementCache.clear();
            gPartMeasurementCache[_CacheKey] = _Measurement;
        }
        return MakeResponse(Variant(_Measurement));
    }

    iCAX::Interaction::CInvocationResult HandleUnfoldPartBRep(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_)
            throw std::invalid_argument("TubeDesigner.UnfoldPartBRep requires a scene");
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _PartID = ParseRequiredUuid(
            GetString(_Payload, "partEntityId"), "partEntityId");
        auto& _Repository = Scene_->Database();
        const auto _PartEntity = _Repository.GetEntity(_PartID);
        const auto _Part = GetComponent<CManufacturingPartComponent>(_PartEntity);
        const auto _TransientPart = FindTransientPart(*Scene_, _PartID);
        if (!_Part && !_TransientPart)
            throw std::invalid_argument("manufacturing part does not exist");

        const auto _Independent = _Part && IsIndependentNestingPart(*_Part);
        if (_Independent) RestoreNestingResources(*Scene_);
        else if (!_TransientPart) RestoreDesignerResources(*Scene_, ApplicationContext_, true);
        const auto _Product = _Part ? GetComponent<CProductInstanceComponent>(
            _Repository.GetEntity(_Part->GetProductID())) : nullptr;
        if (!_Independent && !_TransientPart && (!_Product
            || _Product->GetActiveGenerationRunID() != _Part->GetGenerationRunID()))
        {
            throw std::runtime_error("manufacturing part is stale");
        }

        const auto _ResourceID = _Part
            ? _Part->GetManufacturingGeometryResourceID()
            : _TransientPart->ManufacturingResource.URL;
        const auto _ResourceVersion = _Part
            ? _Part->GetManufacturingGeometryResourceVersion()
            : _TransientPart->ManufacturingResource.nVersion;
        const auto _ExpectedVersion = GetUInt64(
            _Payload, "resourceVersion", _ResourceVersion);
        if (_ResourceID.empty() || _ResourceVersion == 0)
            throw std::runtime_error("manufacturing part has no final BRep resource");
        if (_ExpectedVersion != _ResourceVersion)
            throw std::runtime_error("manufacturing geometry version changed; reopen the part");

        const auto _CacheKey = _ResourceID + "@" + std::to_string(_ResourceVersion);
        {
            const std::lock_guard _Lock(gPartMeasurementCacheMutex);
            const auto _Cached = gPartUnfoldingCache.find(_CacheKey);
            if (_Cached != gPartUnfoldingCache.end())
            {
                auto _Response = _Cached->second;
                _Response["partEntityId"] = UuidToString(_PartID);
                _Response["resourceId"] = _ResourceID;
                _Response["resourceVersion"] =
                    static_cast<unsigned long long>(_ResourceVersion);
                return MakeResponse(Variant(std::move(_Response)));
            }
        }

        const auto _BRep = Scene_->Resources().Get<iCAX::GeometryData::BRepModel>(
            _ResourceID, _ResourceVersion);
        if (!_BRep)
            throw std::runtime_error("final BRep resource version is not available");
        const auto _Rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(*_BRep);
        auto _Shape = _Rebuilt.Shape;
        if (!_Rebuilt.bOK || _Shape.IsNull())
        {
            if (_Part)
                _Shape = RecoverManufacturingShapeFromGeneration(_Repository, *_Part);
            if (_Shape.IsNull())
                throw std::runtime_error("无法读取最终零件几何以生成二维展开");
        }

        const auto _Service = Scene_->Services().Resolve<IBRepTubeUnfoldingService>();
        if (!_Service)
            throw std::runtime_error("BRep 展开服务未初始化");
        const auto _Unfolded = _Service->Unfold(_Shape);
        auto _Response = SerializeTubeBRepUnfolding(_Unfolded);
        _Response["partEntityId"] = UuidToString(_PartID);
        _Response["resourceId"] = _ResourceID;
        _Response["resourceVersion"] =
            static_cast<unsigned long long>(_ResourceVersion);
        if (!_Unfolded.bOK)
            throw std::runtime_error(_Unfolded.Diagnostic.empty()
                ? "无法从最终 BRep 生成二维展开" : _Unfolded.Diagnostic);

        {
            const std::lock_guard _Lock(gPartMeasurementCacheMutex);
            if (gPartUnfoldingCache.size() > 256) gPartUnfoldingCache.clear();
            auto _CachedResponse = _Response;
            _CachedResponse.erase("partEntityId");
            _CachedResponse.erase("resourceId");
            _CachedResponse.erase("resourceVersion");
            gPartUnfoldingCache[_CacheKey] = std::move(_CachedResponse);
        }
        return MakeResponse(Variant(std::move(_Response)));
    }

    iCAX::Interaction::CInvocationResult HandleGetPunchTools(
        const iCAX::Interaction::CInvocation&,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext* ProductContext_, iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext*)
    {
        ObjectMap _Parameters{{"action",std::string("catalogue")}};
        VariantArray _TemplateSources;
        for (const auto& _Directory : DiscoverPythonTemplateDirectories(ApplicationContext_))
        {
            const auto _ToolRoot = _Directory / "punch-tools";
            std::error_code _Error;
            if (!std::filesystem::is_directory(_ToolRoot, _Error)) continue;
            _TemplateSources.emplace_back(ObjectMap{
                {"scope",std::string("template")},
                {"root",PathToUTF8(_ToolRoot)},
                {"templateId",_Directory.filename().string()},
                {"templateName",_Directory.filename().string()}
            });
        }
        const auto _UserMoldRoot = ResolveUserMoldRoot(ApplicationContext_);
        if (!_UserMoldRoot.empty() && std::filesystem::is_directory(_UserMoldRoot))
            _TemplateSources.emplace_back(ObjectMap{
                { "scope", std::string("user") },
                { "root", PathToUTF8(_UserMoldRoot) }
            });
        if (!_TemplateSources.empty()) _Parameters["catalogueSources"] = std::move(_TemplateSources);
        auto _Response = InvokePunchToolRuntime(ApplicationContext_,std::move(_Parameters));
        if (ProductContext_)
            _Response["tools"] = [&] {
                auto _Tools = _Response.at("tools").To<VariantArray>();
                for (const auto& _Value : ListPunchToolUserDataRecords(*GetUserDataStore(ProductContext_)))
                    _Tools.emplace_back(_Value);
                return _Tools;
            }();
        return MakeResponse(std::move(_Response));
    }

    std::string NewPunchToolDefinitionID()
    {
        auto _ID = UuidToString(iCAX::Data::GenerateNewUUID());
        std::erase_if(_ID, [](const unsigned char _Character) {
            return !((_Character >= 'a' && _Character <= 'z')
                || (_Character >= 'A' && _Character <= 'Z')
                || (_Character >= '0' && _Character <= '9'));
        });
        return "user-" + _ID;
    }

    iCAX::Application::CProductUserDataRecord SavePunchToolUserRecord(
        iCAX::Product::IProductContext* ProductContext_, const ObjectMap& Request_,
        ObjectMap Descriptor_, ObjectMap Geometry_ = {}, std::string ScriptSource_ = {},
        std::string PackageDigest_ = {}, ObjectMap Resources_ = {})
    {
        if (!ProductContext_) throw std::runtime_error("模具保存需要产品上下文");
        auto _Store = GetUserDataStore(ProductContext_);
        const auto _ID = GetString(Descriptor_, "id").empty()
            ? NewPunchToolDefinitionID() : GetRequiredText(Descriptor_, "id", 80);
        Descriptor_["id"] = _ID;
        Descriptor_["schema"] = std::string("icax.punch-tool");
        Descriptor_["schemaVersion"] = 1ull;
        Descriptor_["displayName"] = GetRequiredText(Request_, "name", 120);
        Descriptor_["version"] = GetString(Descriptor_, "version", "1.0.0");
        Descriptor_["kind"] = GetString(Descriptor_, "kind", "fixed");
        Descriptor_["target"] = GetString(Descriptor_, "target", "side");
        Descriptor_["category"] = GetString(Request_, "category",
            GetString(Descriptor_, "category", GetString(Descriptor_, "target") == "end" ? "端面" : "孔型"));
        if (GetString(Descriptor_, "kind") != "fixed" && GetString(Descriptor_, "kind") != "programmatic")
            throw std::invalid_argument("模具类型只能是程式或定式");
        if (GetString(Descriptor_, "target") != "side" && GetString(Descriptor_, "target") != "end"
            && GetString(Descriptor_, "target") != "part")
            throw std::invalid_argument("模具适用位置无效");
        if (GetString(Descriptor_, "kind") == "fixed") {
            Descriptor_["parameters"] = VariantArray{};
            if (Geometry_.empty()) throw std::invalid_argument("定式模具必须包含闭合截面");
        } else if (ScriptSource_.empty()) {
            throw std::invalid_argument("程式模具必须包含 tool.py");
        }
        iCAX::Application::CProductUserDataRecord _Record;
        _Record.FeatureID = kPunchToolFeatureID;
        _Record.RecordType = GetString(Descriptor_, "kind") == "fixed"
            ? kFixedPunchToolRecordType : kParametricPunchToolRecordType;
        _Record.SubjectType = "punch-tool-definition";
        _Record.SubjectID = _ID;
        _Record.RecordID = GetString(Request_, "recordId");
        if (_Record.RecordID.empty()) _Record.RecordID = UuidToString(iCAX::Data::GenerateNewUUID());
        _Record.OwnerScope = "personal";
        ObjectMap _Payload{{"descriptor", Descriptor_}};
        if (!Geometry_.empty()) _Payload["geometry"] = Geometry_;
        if (!ScriptSource_.empty()) _Payload["scriptSource"] = ScriptSource_;
        if (!PackageDigest_.empty()) _Payload["packageDigest"] = PackageDigest_;
        if (!Resources_.empty()) _Payload["resources"] = std::move(Resources_);
        _Record.Payload = Variant(std::move(_Payload));
        return _Store->Put(_Record, GetUInt64(Request_, "revision", 0));
    }

    iCAX::Interaction::CInvocationResult HandleSavePunchTool(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext*)
    {
        tube::license::Enforce<119, tube::license::Feature::Design>();
        const auto _Request = DecodeObjectPayload(Request_);
        const auto _Kind = GetString(_Request, "kind", "fixed");
        ObjectMap _Descriptor = _Request.contains("descriptor")
            ? GetRequiredObject(_Request, "descriptor") : ObjectMap();
        _Descriptor["kind"] = _Kind;
        _Descriptor["target"] = GetString(_Request, "target", "side");
        ObjectMap _Geometry;
        if (_Request.contains("geometry")) _Geometry = GetRequiredObject(_Request, "geometry");
        ObjectMap _Resources;
        if (_Request.contains("resources")) _Resources = GetRequiredObject(_Request, "resources");
        const auto _Record = SavePunchToolUserRecord(ProductContext_, _Request, std::move(_Descriptor),
            std::move(_Geometry), GetString(_Request, "scriptSource"), GetString(_Request, "packageDigest"),
            std::move(_Resources));
        return MakeResponse(ObjectMap{{ "tool", MakePunchToolUserDataPayload(_Record) }});
    }

    iCAX::Interaction::CInvocationResult HandleImportPunchToolPackage(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext*)
    {
        tube::license::Enforce<120, tube::license::Feature::Design>();
        const auto _Request = DecodeObjectPayload(Request_);
        const auto _SourcePath = GetRequiredText(_Request, "sourcePath", 32767);
        auto _Extension = Utf8Path(_SourcePath).extension().string();
        std::transform(_Extension.begin(), _Extension.end(), _Extension.begin(), [](const unsigned char _Value) {
            return static_cast<char>(std::tolower(_Value));
        });
        if (_Extension != ".itmt") throw std::invalid_argument("程式模具必须使用 .itmt 压缩包");
        auto _Runtime = InvokePunchToolPackageRuntime(ApplicationContext_, ObjectMap{
            { "action", std::string("inspect") }, { "sourcePath", _SourcePath }
        });
        const auto _Package = GetRequiredObject(_Runtime, "package");
        const auto _Descriptor = GetRequiredObject(_Package, "descriptor");
        ObjectMap _RequestWithName = _Request;
        _RequestWithName["name"] = GetString(_Descriptor, "displayName", "程式模具");
        _RequestWithName["kind"] = std::string("programmatic");
        _RequestWithName["target"] = GetString(_Descriptor, "target", "side");
        auto _UserDescriptor = _Descriptor;
        _UserDescriptor["sourceId"] = GetString(_Descriptor, "id");
        _UserDescriptor.erase("id");
        ObjectMap _Resources;
        if (_Package.contains("resources")) _Resources = GetRequiredObject(_Package, "resources");
        auto _Record = SavePunchToolUserRecord(ProductContext_, _RequestWithName, _UserDescriptor,
            {}, GetString(_Package, "scriptSource"), GetString(_Package, "packageDigest"),
            std::move(_Resources));
        return MakeResponse(ObjectMap{{ "tool", MakePunchToolUserDataPayload(_Record) }});
    }

    iCAX::Interaction::CInvocationResult HandleGetPartDrawingTools(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext* ProductContext_, iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext*)
    {
        // Part drawing and the punch wizard consume the same mould catalogue,
        // including template-provided resources. Keep one source of truth so
        // a mould added for one entry point is immediately visible in the other.
        return HandleGetPunchTools(Request_, ApplicationContext_, ProductContext_, nullptr, nullptr);
    }

    ObjectMap ReadEditableDrawingConfig(const CManufacturingPartComponent& Part_, bool PartDrawing_)
    {
        const auto _Entity = Part_.GetEntity();
        if (_Entity)
        {
            if (PartDrawing_)
            {
                if (const auto _Component = GetComponent<CPartDrawingComponent>(_Entity);
                    _Component && !_Component->GetDefinition().empty())
                    return _Component->GetDefinition();
            }
            else
            {
                if (const auto _Component = GetComponent<CPunchWizardComponent>(_Entity);
                    _Component && !_Component->GetDefinition().empty())
                    return _Component->GetDefinition();
            }
        }
        return {};
    }

    ObjectMap ReadPartDrawing(const ObjectMap& Payload_)
    {
        const auto _Drawing = GetRequiredObject(Payload_,"drawing");
        const auto _Section = GetRequiredObject(_Drawing,"section");
        const auto _Profile = GetRequiredObject(_Section,"profile");
        ValidateImportedProfileDefinition(_Profile);
        const auto _Length=GetDouble(_Drawing,"length",0);
        if(!std::isfinite(_Length)||_Length<1||_Length>100000)throw std::invalid_argument("主管长度须为 1 至 100000 mm");
        return ObjectMap{{"schemaVersion",1ull},{"length",_Length},{"section",_Section}};
    }

    bool PartDrawingRebased(const ObjectMap& Existing_, const ObjectMap& Drawing_)
    {
        if(Drawing_.empty())return false;
        if(!Existing_.contains("drawing"))throw std::invalid_argument("此零件没有可编辑的主管定义");
        return PartDrawingGeometrySignature(GetRequiredObject(Existing_,"drawing"))!=PartDrawingGeometrySignature(Drawing_);
    }

    ObjectMap ResolvePunchBlankProfile(const ObjectMap& Payload_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,iCAX::Product::IProductContext* ProductContext_)
    {
        if(Payload_.contains("drawing"))return GetRequiredObject(GetRequiredObject(ReadPartDrawing(Payload_),"section"),"profile");
        ObjectMap _Parameters;
        if(Payload_.contains("parameters"))_Parameters=GetRequiredObject(Payload_,"parameters");
        return ResolveProfileSnapshot(ApplicationContext_,*GetUserDataStore(ProductContext_),ParseProfileReference(Payload_),_Parameters);
    }

    iCAX::Interaction::CInvocationResult PreviewEditableDrawingTools(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext* Scene_, bool PartDrawing_)
    {
        if (!Scene_ || Request_.Payload.size() > 16 * 1024 * 1024) throw std::invalid_argument("预览请求无效");
        const auto _Payload = DecodeObjectPayload(Request_);
        // UI preview is always placement-only. The explicit diagnostic flag is
        // reserved for native regression probes, never emitted by product UI.
        const auto _DiagnosticFlag=_Payload.find("diagnosticBooleanPreview");
        const bool _ToolsOnly=PartDrawing_||_DiagnosticFlag==_Payload.end()||!_DiagnosticFlag->second.Is<bool>()||!_DiagnosticFlag->second.To<bool>();
        const std::string _PreviewPrefix=PartDrawing_?"tube-designer/part-drawing-preview":"tube-designer/punch-preview";
        TopoDS_Shape _Base;
        ObjectMap _Original;
        const auto _Drawing=_Payload.contains("drawing")?ReadPartDrawing(_Payload):ObjectMap();
        bool _Rebased=false;
        const auto _PartText = GetString(_Payload, "partEntityId");
        if (!_PartText.empty()) {
            const auto _Part = GetComponent<CManufacturingPartComponent>(Scene_->Database().GetEntity(ParseRequiredUuid(_PartText, "partEntityId")));
            if (!_Part || !IsIndependentNestingPart(*_Part) || !IsTubeManufacturingPart(_Part->GetItemProperties()))
                throw std::invalid_argument("预览只支持独立下料管材");
            if (GetUInt64(_Payload, "resourceVersion", _Part->GetManufacturingGeometryResourceVersion()) != _Part->GetManufacturingGeometryResourceVersion()
                || GetString(_Payload,"resourceId",_Part->GetManufacturingGeometryResourceID()) != _Part->GetManufacturingGeometryResourceID())
                throw std::invalid_argument("零件已改变，请重新打开向导");
            const auto _Properties = _Part->GetItemProperties();
            const auto _Config=ReadEditableDrawingConfig(*_Part,PartDrawing_);
            _Original = _Config;
            const auto _BRep = Scene_->Resources().Get<iCAX::GeometryData::BRepModel>(
                GetString(_Config, "baseResourceId", _Part->GetManufacturingGeometryResourceID()),
                GetUInt64(_Config, "baseResourceVersion", _Part->GetManufacturingGeometryResourceVersion()));
            if (!_BRep) throw std::invalid_argument("原始编辑基准缺失，无法可靠预览；请重新导入原始零件");
            const auto _Rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(*_BRep);
            if (!_Rebuilt.bOK) throw std::runtime_error("无法恢复基准实体");
            _Base = _Rebuilt.Shape;
            _Rebased=PartDrawingRebased(_Original,_Drawing);
            if(_Rebased)_Base=BuildPunchBlank(GetRequiredObject(GetRequiredObject(_Drawing,"section"),"profile"),GetDouble(_Drawing,"length",0));
        } else {
            const double _Length = GetDouble(_Drawing.empty()?_Payload:_Drawing, "length", 0);
            if (!std::isfinite(_Length) || _Length < 1 || _Length > 100000) throw std::invalid_argument("长度须为 1 至 100000 mm");
            ObjectMap _Parameters;
            if (const auto _It = _Payload.find("parameters"); _It != _Payload.end()) {
                if (!_It->second.Is<ObjectMap>()) throw std::invalid_argument("管型参数无效");
                _Parameters = _It->second.To<ObjectMap>();
            }
            const auto _Profile = ResolvePunchBlankProfile(_Payload,ApplicationContext_,ProductContext_);
            _Base = BuildPunchBlank(_Profile, _Length);
        }
        auto _Features = ReadPunchFeatures(_Payload, "features");
        auto _Ends = ReadPunchEnds(_Payload);
        std::vector<SPunchCut> _PreviewCuts;
        SPunchGeometryStatistics _Statistics;
        SPunchPreviewDiagnostics _Diagnostics;
        _Diagnostics.AllowDisconnectedResults=!_ToolsOnly;
        const double _NominalWall=_Drawing.empty()?0:GetDouble(GetRequiredObject(GetRequiredObject(_Drawing,"section"),"profile"),"wallThickness",0);
        const auto _UserTools = ProductContext_ ? ListPunchToolUserDataRecords(*GetUserDataStore(ProductContext_)) : VariantArray{};
        TopoDS_Shape _Shape;
        std::string _ResultError;
        try {
            _Shape = EvaluatePunch(_Base, _Features, _Ends, Request_, ApplicationContext_, *Scene_,
                _Original, _Rebased, &_PreviewCuts, &_Statistics, &_Diagnostics, _ToolsOnly, _NominalWall, _UserTools);
        } catch(const std::exception& _Error) {
            // A failed boolean result is not a successful part. Preview alone
            // publishes its current blank/tools so the user can see what failed;
            // Add/Apply retain the original throwing, single-solid contract.
            _ResultError=_Error.what();
        }
        const bool _PreviewComputed=_ResultError.empty()&&_Diagnostics.ResultComputed;
        const bool _ResultValid=_PreviewComputed&&_Diagnostics.SolidCount==1;
        const bool _HasResultGeometry=_PreviewComputed&&_Diagnostics.SolidCount>0&&!_Shape.IsNull();
        ReportExportProgress(Request_, "mesh", 0, 1, "正在生成三维预览");
        const auto _BaseResource = StorePunchBRep(*Scene_, _PreviewPrefix+"/base",
            "主管预览（未保存）", _Base, Request_, true);
        const auto _BaseMesh = iCAX::RenderInteraction::EnsureFrontendGeometryResource(
            Scene_->Resources(), _BaseResource.URL, iCAX::Render::ERenderGeometryKind::Mesh);
        const auto _BaseMaterial = EnsurePunchPreviewBlankMaterial(*Scene_);
        ObjectMap _Response{{"toolsOnly",_ToolsOnly},{"previewToolsComplete",_Diagnostics.ToolsComplete},
            {"baseGeometry", ObjectMap{{"url",_BaseMesh.URL},{"version",_BaseMesh.nVersion}}},
            {"baseMaterial", ObjectMap{{"url",_BaseMaterial.URL},{"version",_BaseMaterial.nVersion}}},
            {"bounds",ShapeBounds(_HasResultGeometry?_Shape:_Base)},{"baseBounds",ShapeBounds(_Base)},
            {"length",GetDouble(ShapeBounds(_HasResultGeometry?_Shape:_Base),"width",0)}};
        if(!_ToolsOnly){_Response["resultValid"]=_ResultValid;_Response["previewComputed"]=_PreviewComputed;}
        if(_HasResultGeometry) {
            const auto _Resource=StorePunchBRep(*Scene_,_PreviewPrefix,"加工预览（未保存）",_Shape,Request_,true);
            const auto _Mesh=iCAX::RenderInteraction::EnsureFrontendGeometryResource(Scene_->Resources(),_Resource.URL,iCAX::Render::ERenderGeometryKind::Mesh);
            _Response["geometry"]=ObjectMap{{"url",_Mesh.URL},{"version",_Mesh.nVersion}};
        }
        if(!_ToolsOnly&&_PreviewComputed) {
            _Response["solidCount"]=static_cast<unsigned long long>(_Diagnostics.SolidCount);
            if(!_ResultValid)_Response["resultWarning"]=std::string("当前加工后剩余 ")+std::to_string(_Diagnostics.SolidCount)+
                " 段，可继续编辑；最终确认时须保留 1 段";
        } else if(!_ResultError.empty()) {
            _Response["resultError"]=_ResultError.empty()?std::string("未生成有效加工实体"):_ResultError;
            _Response["failureStage"]=_Diagnostics.FailureStage;
            ObjectMap _Target{{"target",_Diagnostics.FailureTarget}};
            if(!_Diagnostics.FailureKey.empty())_Target["key"]=_Diagnostics.FailureKey;
            if(_Diagnostics.FailureIndex>=0)_Target["index"]=static_cast<unsigned long long>(_Diagnostics.FailureIndex);
            _Response["failureTarget"]=_Target;
        }
        VariantArray _ToolPreviews;
        BRep_Builder _DisplayBuilder;TopoDS_Compound _CutterGroup;_DisplayBuilder.MakeCompound(_CutterGroup);
        const auto _ToolMaterial=EnsureComponentCSGOperandMaterial(*Scene_);
        for(const auto& _Cut:_PreviewCuts) {
            const auto _Clipped=iCAX::Tasks::Run([&]{return _ToolsOnly&&_Cut.Target!="end"?_Cut.Shape:BuildPunchToolPreview(_Base,{_Cut});},
                iCAX::OpenCascade::detail::GeometryTaskScheduler()).Result();
            if(!TopExp_Explorer(_Clipped,TopAbs_FACE).More())continue;
            _DisplayBuilder.Add(_CutterGroup,_Clipped);
            const auto _Resource=StorePunchBRep(*Scene_,_PreviewPrefix+"/tool/"+_Cut.Target+"/"+_Cut.Key,
                "当前刀具局部预览（未保存）",_Clipped,Request_,true);
            const auto _Mesh=iCAX::RenderInteraction::EnsureFrontendGeometryResource(Scene_->Resources(),_Resource.URL,iCAX::Render::ERenderGeometryKind::Mesh);
            _ToolPreviews.emplace_back(ObjectMap{{"target",std::string(_Cut.Target=="end"?"end":"feature")},{"key",_Cut.Key},
                {"geometry",ObjectMap{{"url",_Mesh.URL},{"version",_Mesh.nVersion}}}});
        }
        _Response["toolPreviews"]=_ToolPreviews;
        if(TopExp_Explorer(_CutterGroup,TopAbs_FACE).More()) {
            const auto _ToolResource = StorePunchBRep(*Scene_, _PreviewPrefix+"/tools",
                "冲孔刀具拉伸体（未保存）", _CutterGroup, Request_, true);
            const auto _ToolMesh = iCAX::RenderInteraction::EnsureFrontendGeometryResource(
                Scene_->Resources(), _ToolResource.URL, iCAX::Render::ERenderGeometryKind::Mesh);
            _Response["toolGeometry"] = ObjectMap{{"url",_ToolMesh.URL},{"version",_ToolMesh.nVersion}};
            _Response["toolMaterial"] = ObjectMap{{"url",_ToolMaterial.URL},{"version",_ToolMaterial.nVersion}};
        }
        _Response["toolGroupCount"]=static_cast<unsigned long long>(_PreviewCuts.size());
        _Response["toolCount"]=static_cast<unsigned long long>(_Statistics.AppliedCount+_Statistics.EndToolCount);
        _Response["candidateToolCount"]=static_cast<unsigned long long>(_Statistics.CandidateCount);
        _Response["skippedToolCount"]=static_cast<unsigned long long>(_Statistics.SkippedCount);
        if(!_ToolsOnly){_Response["outsideToolCount"]=static_cast<unsigned long long>(_Statistics.OutsideCount);_Response["appliedPunchToolCount"]=static_cast<unsigned long long>(_Statistics.AppliedCount);}
        else _Response["placedToolCount"]=static_cast<unsigned long long>(_Statistics.AppliedCount+_Statistics.EndToolCount);
        _Response["endToolCount"]=static_cast<unsigned long long>(_Statistics.EndToolCount);
        _Response["toolCountExact"]=_Statistics.CountsExact&&_Diagnostics.ToolsComplete;
        _Response["toolDisplayClipped"]=true;
        return MakeResponse(std::move(_Response));
    }

    iCAX::Interaction::CInvocationResult HandleAddNestingStandardPart(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_ || Request_.Payload.size() > 16 * 1024 * 1024)
            throw std::invalid_argument("添加标准零件请求无效");
        tube::license::Enforce<403, tube::license::Feature::Production>();
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _Length = GetDouble(_Payload, "length", 0.0);
        if (!std::isfinite(_Length) || _Length < 1.0 || _Length > 100000.0)
            throw std::invalid_argument("零件长度必须在 1 到 100000 mm 之间");
        ObjectMap _Parameters;
        if (const auto _Iterator = _Payload.find("parameters");
            _Iterator != _Payload.end() && !_Iterator->second.Is<std::monostate>())
        {
            if (!_Iterator->second.Is<ObjectMap>())
                throw std::invalid_argument("管型参数必须是对象");
            _Parameters = _Iterator->second.To<ObjectMap>();
        }

        ObjectMap _Profile, _ProfileReference;
        if (_Payload.contains("profile"))
        {
            if (_Payload.contains("profileRef"))
                throw std::invalid_argument("请选择管型库管型或本地 DXF 中的一种来源");
            _Profile = GetRequiredObject(_Payload, "profile");
            if (GetString(_Profile, "kind") != kImportedProfileRecordType
                || GetString(_Profile, "profileScope") == "template")
                throw std::invalid_argument("本地截面必须是导入的 DXF 定式管型");
            if (!_Parameters.empty())
                throw std::invalid_argument("DXF 定式管型不支持程式参数");
            ValidateImportedProfileDefinition(_Profile);
        }
        else
        {
            const auto _Reference = ParseProfileReference(_Payload);
            if (_Reference.Scope == "template")
                throw std::invalid_argument("标准零件只能使用系统或我的管型，不能使用模板管型");
            _ProfileReference = ProfileReferencePayload(_Reference);
            _Profile = ResolveProfileSnapshot(ApplicationContext_, *GetUserDataStore(ProductContext_),
                _Reference, _Parameters);
        }
        const auto _Shape = BuildPunchBlank(_Profile, _Length);
        if (_Shape.IsNull() || !BRepCheck_Analyzer(_Shape).IsValid())
            throw std::runtime_error("无法从所选管型生成有效的标准零件实体");

        auto _ProfileName = TrimText(GetString(_Profile, "name", "管型"));
        if (_ProfileName.empty()) _ProfileName = "管型";
        if (_ProfileName.size() > 120)
        {
            auto _End = std::size_t(120);
            while (_End > 0 && (static_cast<unsigned char>(_ProfileName[_End]) & 0xc0) == 0x80) --_End;
            _ProfileName.resize(_End);
        }
        const auto _Name = _ProfileName + " × " + ImportedDimensionText(_Length) + " mm";
        const auto _Quantity = ValidateInstanceQuantity(GetUInt64(_Payload, "quantity", 1));
        const auto _PartID = iCAX::Data::GenerateNewUUID();
        const auto _BatchID = iCAX::Data::GenerateNewUUID();
        const auto _PartNumber = MakeSafePathSegment(
            _Name + "-" + UuidToString(_PartID).substr(0, 8), "standard-part");
        auto& _DB = Scene_->Database();
        std::uint64_t _NextIndex = 1;
        for (const auto& [_Entity, _Part] : Collect<CManufacturingPartComponent>(_DB))
            if (IsIndependentNestingPart(*_Part) && _Part->GetPartIndex() >= _NextIndex
                && _Part->GetPartIndex() < (std::numeric_limits<std::uint64_t>::max)())
                _NextIndex = _Part->GetPartIndex() + 1;

        const auto _Resource = StoreBRep(*Scene_,
            "tube-designer/nesting/standard/" + UuidToString(_PartID), _Name, _Shape);
        const auto _Thumbnail = iCAX::RenderInteraction::EnsureFrontendGeometryResource(
            Scene_->Resources(), _Resource.URL, iCAX::Render::ERenderGeometryKind::Mesh);
        auto _Measurement = MeasureFinalPartGeometry(_Shape, _Resource.URL, _Resource.nVersion);
        _Measurement["nestingEnvelopeLength"] = _Length;
        _Measurement["normalizedAxis"] = std::string("+X");
        ObjectMap _StandardPart{
            { "schemaVersion", 1ull }, { "profile", _Profile },
            { "profileRef", _ProfileReference }, { "parameters", _Parameters }, { "length", _Length },
            { "source", std::string(_ProfileReference.empty() ? "local-dxf" : "profile-library") }
        };
        ObjectMap _ItemProperties{
            { "tubeDesigner.standardPart", std::move(_StandardPart) },
            { "tubeDesigner.endProcess", ObjectMap{
                { "startCut", std::string("square") }, { "endCut", std::string("square") } } },
            { "manufacturing.partKind", std::string("tube") },
            { "manufacturing.materialCategory", std::string("tube") },
            { "manufacturing.sourcing", std::string("made") },
            { "manufacturing.process", std::string("straight-cut") },
            { "manufacturing.requiresBending", false },
            { "manufacturing.categoryName", std::string("标准零件") },
            { "manufacturing.geometryMeasurement", _Measurement },
            { "nesting.snapshot", ObjectMap{
                { "name", _Name }, { "quantity", _Quantity }, { "source", std::string("standard-part") },
                { "profileRef", _ProfileReference }, { "length", _Length } } }
        };
        iCAX::Data::PropertySet _Properties{
            { CManufacturingPartComponent::PropertyName_ProductID, PropertyValue(iCAX::Data::uuid()) },
            { CManufacturingPartComponent::PropertyName_SourceMemberID, PropertyValue(iCAX::Data::uuid()) },
            { CManufacturingPartComponent::PropertyName_GenerationRunID, PropertyValue(_BatchID) },
            { CManufacturingPartComponent::PropertyName_PartIndex, PropertyValue(_NextIndex) },
            { CManufacturingPartComponent::PropertyName_StableKey, PropertyValue("standard/" + UuidToString(_PartID)) },
            { CManufacturingPartComponent::PropertyName_PartNumber, PropertyValue(_PartNumber) },
            { CManufacturingPartComponent::PropertyName_Name, PropertyValue(_Name) },
            { CManufacturingPartComponent::PropertyName_Role, PropertyValue(std::string("标准零件")) },
            { CManufacturingPartComponent::PropertyName_Quantity, PropertyValue(_Quantity) },
            { CManufacturingPartComponent::PropertyName_QuantityOverride, PropertyValue(0ull) },
            { CManufacturingPartComponent::PropertyName_Length, PropertyValue(_Length) },
            { CManufacturingPartComponent::PropertyName_ManufacturingGeometryResourceID, PropertyValue(_Resource.URL) },
            { CManufacturingPartComponent::PropertyName_ManufacturingGeometryResourceVersion, PropertyValue(_Resource.nVersion) },
            { CManufacturingPartComponent::PropertyName_ThumbnailGeometryResourceID, PropertyValue(_Thumbnail.URL) },
            { CManufacturingPartComponent::PropertyName_ThumbnailGeometryResourceVersion, PropertyValue(_Thumbnail.nVersion) },
            { CManufacturingPartComponent::PropertyName_FileName, PropertyValue(MakeStepFileName(_PartNumber)) },
            { CManufacturingPartComponent::PropertyName_Status, PropertyValue(std::string("Ready")) },
            { CManufacturingPartComponent::PropertyName_ItemProperties, PropertyValue(_ItemProperties) }
        };

        const auto _Meta = _DB.GetMetaEntity();
        if (!_Meta) throw std::runtime_error("Missing project root");
        const auto _Root = GetComponent<CTubeDesignerRootComponent>(_Meta);
        const auto _OldTask = _Root ? _Root->GetNestingTask() : ObjectMap();
        VariantArray _References;
        if (const auto _OldReferences = _OldTask.find("parts");
            _OldReferences != _OldTask.end() && _OldReferences->second.Is<VariantArray>())
            for (const auto& _Value : _OldReferences->second.To<VariantArray>())
                if (_Value.Is<ObjectMap>() && IsProductNestingReference(_Value.To<ObjectMap>()))
                    _References.emplace_back(_Value);
        for (const auto& [_Entity, _Part] : Collect<CManufacturingPartComponent>(_DB))
            if (IsIndependentNestingPart(*_Part))
                _References.emplace_back(ObjectMap{
                    { "partEntityId", UuidToString(_Entity->GetID()) },
                    { "generationRunId", UuidToString(_Part->GetGenerationRunID()) } });
        _References.emplace_back(ObjectMap{
            { "partEntityId", UuidToString(_PartID) },
            { "generationRunId", UuidToString(_BatchID) } });
        const ObjectMap _Task{
            { "parts", _References }, { "request", ObjectMap() }, { "result", ObjectMap() },
            { "revision", UuidToString(iCAX::Data::GenerateNewUUID()) } };

        auto _Undo = _DB.BeginUndoCommand("Add nesting standard part");
        auto& _Transaction = _DB.BeginTransaction("Add nesting standard part");
        bool _Committing = false;
        try
        {
            _Transaction.CreateEntity(_PartID);
            _Transaction.AttachComponent(_PartID, CManufacturingPartComponent::S_ClassName, _Properties);
            _Transaction.AttachComponent(
                _PartID,
                CTubeProfileComponent::S_ClassName,
                TubeProfileComponentProperties(_Profile, "standard-part"));
            QueueUpsertComponent(_Transaction, _Meta, _Meta->GetID(), CTubeDesignerRootComponent::S_ClassName,
                {{ CTubeDesignerRootComponent::PropertyName_NestingTask, PropertyValue(_Task) }});
            std::string _Error;
            _Committing = true;
            if (!_DB.CommitTransaction(_Transaction, _Error))
                throw std::runtime_error(_Error.empty() ? "添加标准零件失败" : _Error);
        }
        catch (...)
        {
            if (!_Committing) { try { _DB.CancelTransaction(_Transaction); } catch (...) {} }
            throw;
        }
        _Undo->End();
        SyncNestingPersistence(*Scene_);
        auto _Response = BuildSnapshot(*Scene_, ApplicationContext_, false);
        _Response["partEntityId"] = UuidToString(_PartID);
        _Response["profile"] = _Profile;
        _Response["profileRef"] = _ProfileReference;
        return MakeResponse(_Response);
    }

    iCAX::Interaction::CInvocationResult CreateEditableDrawingPart(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_,bool PartDrawing_)
    {
        if (!Scene_ || Request_.Payload.size() > 16 * 1024 * 1024)
            throw std::invalid_argument("添加冲孔件请求无效");
        tube::license::Enforce<403, tube::license::Feature::Production>();
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _Drawing=_Payload.contains("drawing")?ReadPartDrawing(_Payload):ObjectMap();
        const auto _ProfileReference = _Drawing.empty()?ProfileReferencePayload(ParseProfileReference(_Payload)):ObjectMap();
        const auto _Length = GetDouble(_Drawing.empty()?_Payload:_Drawing, "length", 0.0);
        if (!std::isfinite(_Length) || _Length < 1.0 || _Length > 100000.0)
            throw std::invalid_argument("成品长度必须在 1 到 100000 mm 之间");
        ObjectMap _Parameters;
        if (const auto _Iterator = _Payload.find("parameters");
            _Iterator != _Payload.end() && !_Iterator->second.Is<std::monostate>())
        {
            if (!_Iterator->second.Is<ObjectMap>())
                throw std::invalid_argument("管型参数必须是对象");
            _Parameters = _Iterator->second.To<ObjectMap>();
        }
        auto _IncomingFeatures = ReadPunchFeatures(_Payload, "features");
        auto _Ends = ReadPunchEnds(_Payload);

        auto _Profile = ResolvePunchBlankProfile(_Payload,ApplicationContext_,ProductContext_);
        const auto _BaseShape = BuildPunchBlank(_Profile, _Length);
        if (_BaseShape.IsNull() || !BRepCheck_Analyzer(_BaseShape).IsValid())
            throw std::runtime_error("无法从所选管型生成有效的直管实体");
        const auto _UserTools = ProductContext_ ? ListPunchToolUserDataRecords(*GetUserDataStore(ProductContext_)) : VariantArray{};
        SPunchPreviewDiagnostics _ResultState;_ResultState.AllowDisconnectedResults=PartDrawing_;
        const auto _Shape = EvaluatePunch(_BaseShape, _IncomingFeatures, _Ends, Request_, ApplicationContext_, *Scene_,{},false,nullptr,nullptr,PartDrawing_?&_ResultState:nullptr,false,0,_UserTools);
        if(PartDrawing_&&_ResultState.SolidCount==0)throw std::invalid_argument("三维零件没有剩余实体，无法创建；请调整刀具");

        auto _Name = TrimText(GetString(_Payload, "name"));
        if (_Name.empty()) _Name = GetString(_Profile, "name", "管型") + (PartDrawing_?"三维零件":"冲孔件");
        if (_Name.empty() || _Name.size() > 160 || _Name.find('\0') != std::string::npos)
            throw std::invalid_argument("冲孔件名称不能为空且不能超过 160 个字符");
        const auto _Material = TrimText(GetString(_Payload, "material"));
        if (_Material.size() > 240 || _Material.find('\0') != std::string::npos)
            throw std::invalid_argument("材料名称过长或包含无效字符");
        const auto _Quantity = ValidateInstanceQuantity(GetUInt64(_Payload, "quantity", 1));

        const auto _PartID = iCAX::Data::GenerateNewUUID();
        const auto _BatchID = iCAX::Data::GenerateNewUUID();
        const auto _PartNumber = MakeSafePathSegment(
            _Name + "-" + UuidToString(_PartID).substr(0, 8), "punch-part");
        const auto _FileName = MakeStepFileName(_PartNumber);
        std::uint64_t _NextIndex = 1;
        auto& _DB = Scene_->Database();
        for (const auto& [_Entity, _Part] : Collect<CManufacturingPartComponent>(_DB))
            if (IsIndependentNestingPart(*_Part) && _Part->GetPartIndex() >= _NextIndex
                && _Part->GetPartIndex() < (std::numeric_limits<std::uint64_t>::max)())
                _NextIndex = _Part->GetPartIndex() + 1;

        const auto _BaseResource = StorePunchBRep(*Scene_,
            "tube-designer/nesting/punch-base/" + UuidToString(_PartID),
            _Name + " 冲孔基准", _BaseShape, Request_);
        const auto _Resource = StorePunchBRep(*Scene_,
            "tube-designer/nesting/punch/" + UuidToString(_PartID), _Name, _Shape, Request_);
        ReportExportProgress(Request_,"mesh",0,1,"正在生成显示网格");
        const auto _Thumbnail = iCAX::RenderInteraction::EnsureFrontendGeometryResource(
            Scene_->Resources(), _Resource.URL, iCAX::Render::ERenderGeometryKind::Mesh);
        const auto _Bounds = ShapeBounds(_Shape);
        const auto _EnvelopeLength = GetDouble(_Bounds, "width", 0.0);
        if (!std::isfinite(_EnvelopeLength) || _EnvelopeLength <= 0.02)
            throw std::invalid_argument("冲孔件的轴向包络长度无效");
        auto _Measurement = MeasureFinalPartGeometry(_Shape, _Resource.URL, _Resource.nVersion);
        _Measurement["nestingEnvelopeLength"] = _EnvelopeLength;
        _Measurement["normalizedAxis"] = std::string("+X");

        VariantArray _SavedFeatures;
        _SavedFeatures.reserve(_IncomingFeatures.size());
        for (const auto& _Feature : _IncomingFeatures)
            _SavedFeatures.emplace_back(PunchFeatureSnapshot(_Feature));
        ObjectMap _PunchConfig{
            { "schema", std::string(PartDrawing_?"icax.tube-designer.part-drawing":"icax.tube-designer.punch-wizard") },
            { "schemaVersion", 4ull },
            { "baseResourceId", _BaseResource.URL },
            { "baseResourceVersion", _BaseResource.nVersion },
            { "baseLength", _Length },
            { "baseBounds", ShapeBounds(_BaseShape) },
            { "baseEndProcess", ObjectMap{{"startCut",std::string("square")},{"endCut",std::string("square")}} },
            { "operations", PunchOperationChain(_IncomingFeatures, _Ends) },
            { "ends", PunchEndsSnapshot(_Ends) },
            { "features", std::move(_SavedFeatures) },
        };
        if(!_Drawing.empty())_PunchConfig["drawing"]=_Drawing;
        ObjectMap _ItemProperties{
            { "manufacturing.partKind", std::string("tube") },
            { "manufacturing.materialCategory", std::string("tube") },
            { "manufacturing.sourcing", std::string("made") },
            { "manufacturing.process", std::string(PartDrawing_?"part-drawing":"punch") },
            { "manufacturing.requiresBending", false },
            { "manufacturing.material", _Material },
            { "manufacturing.categoryName", std::string(PartDrawing_?"三维绘制零件":"冲孔件") },
            { "manufacturing.punchPart", !PartDrawing_ },
            { "manufacturing.geometryMeasurement", _Measurement },
            { "nesting.snapshot", ObjectMap{
                { "name", _Name },
                { "quantity", _Quantity },
                { "source", std::string(PartDrawing_?"part-drawing":"punch-wizard") },
                { "profileRef", _ProfileReference },
                { "length", _EnvelopeLength } } }
        };
        iCAX::Data::PropertySet _Properties{
            { CManufacturingPartComponent::PropertyName_ProductID, PropertyValue(iCAX::Data::uuid()) },
            { CManufacturingPartComponent::PropertyName_SourceMemberID, PropertyValue(iCAX::Data::uuid()) },
            { CManufacturingPartComponent::PropertyName_GenerationRunID, PropertyValue(_BatchID) },
            { CManufacturingPartComponent::PropertyName_PartIndex, PropertyValue(_NextIndex) },
            { CManufacturingPartComponent::PropertyName_StableKey, PropertyValue("punch/" + UuidToString(_PartID)) },
            { CManufacturingPartComponent::PropertyName_PartNumber, PropertyValue(_PartNumber) },
            { CManufacturingPartComponent::PropertyName_Name, PropertyValue(_Name) },
            { CManufacturingPartComponent::PropertyName_Role, PropertyValue(std::string(PartDrawing_?"三维绘制零件":"冲孔件")) },
            { CManufacturingPartComponent::PropertyName_Quantity, PropertyValue(_Quantity) },
            { CManufacturingPartComponent::PropertyName_QuantityOverride, PropertyValue(0ull) },
            { CManufacturingPartComponent::PropertyName_Length, PropertyValue(_EnvelopeLength) },
            { CManufacturingPartComponent::PropertyName_ManufacturingGeometryResourceID, PropertyValue(_Resource.URL) },
            { CManufacturingPartComponent::PropertyName_ManufacturingGeometryResourceVersion, PropertyValue(_Resource.nVersion) },
            { CManufacturingPartComponent::PropertyName_ThumbnailGeometryResourceID, PropertyValue(_Thumbnail.URL) },
            { CManufacturingPartComponent::PropertyName_ThumbnailGeometryResourceVersion, PropertyValue(_Thumbnail.nVersion) },
            { CManufacturingPartComponent::PropertyName_FileName, PropertyValue(_FileName) },
            { CManufacturingPartComponent::PropertyName_Status, PropertyValue(std::string("Ready")) },
            { CManufacturingPartComponent::PropertyName_ItemProperties, PropertyValue(_ItemProperties) }
        };

        VariantArray _References;
        for (const auto& [_Entity, _Part] : Collect<CManufacturingPartComponent>(_DB))
            if (IsIndependentNestingPart(*_Part))
                _References.emplace_back(ObjectMap{
                    { "partEntityId", UuidToString(_Entity->GetID()) },
                    { "generationRunId", UuidToString(_Part->GetGenerationRunID()) } });
        _References.emplace_back(ObjectMap{
            { "partEntityId", UuidToString(_PartID) },
            { "generationRunId", UuidToString(_BatchID) } });
        const ObjectMap _Task{
            { "parts", _References }, { "request", ObjectMap() }, { "result", ObjectMap() },
            { "revision", UuidToString(iCAX::Data::GenerateNewUUID()) } };

        auto _Undo = _DB.BeginUndoCommand("Add nesting punch part");
        auto& _Transaction = _DB.BeginTransaction("Add nesting punch part");
        bool _Committing = false;
        try
        {
            _Transaction.CreateEntity(_PartID);
            _Transaction.AttachComponent(_PartID, CManufacturingPartComponent::S_ClassName, _Properties);
            _Transaction.AttachComponent(
                _PartID,
                CTubeProfileComponent::S_ClassName,
                TubeProfileComponentProperties(_Profile, PartDrawing_ ? "part-drawing" : "punch-wizard"));
            _Transaction.AttachComponent(
                _PartID,
                PartDrawing_ ? CPartDrawingComponent::S_ClassName : CPunchWizardComponent::S_ClassName,
                {{ PartDrawing_ ? CPartDrawingComponent::PropertyName_Definition
                    : CPunchWizardComponent::PropertyName_Definition,
                    PropertyValue(_PunchConfig) }});
            const auto _Meta = _DB.GetMetaEntity();
            if (!_Meta) throw std::runtime_error("Missing project root");
            QueueUpsertComponent(_Transaction, _Meta, _Meta->GetID(), CTubeDesignerRootComponent::S_ClassName,
                {{ CTubeDesignerRootComponent::PropertyName_NestingTask, PropertyValue(_Task) }});
            std::string _Error;
            _Committing = true;
            if (!_DB.CommitTransaction(_Transaction, _Error))
                throw std::runtime_error(_Error.empty() ? "添加冲孔件失败" : _Error);
        }
        catch (...)
        {
            if (!_Committing) { try { _DB.CancelTransaction(_Transaction); } catch (...) {} }
            throw;
        }
        _Undo->End();
        SyncNestingPersistence(*Scene_);
        auto _Response = BuildSnapshot(*Scene_, ApplicationContext_, false);
        _Response["partEntityId"] = UuidToString(_PartID);
        _Response["profile"] = _Profile;
        _Response["profileRef"] = _ProfileReference;
        _Response["recognition"] = _Measurement;
        return MakeResponse(_Response);
    }

    iCAX::Interaction::CInvocationResult ApplyEditableDrawingPart(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_,bool PartDrawing_)
    {
        if (!Scene_ || Request_.Payload.size() > 16 * 1024 * 1024)
            throw std::invalid_argument("Invalid punch wizard request");
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _PartID = ParseRequiredUuid(
            GetString(_Payload, "partEntityId"), "partEntityId");
        auto& _DB = Scene_->Database();
        const auto _Part = GetComponent<CManufacturingPartComponent>(_DB.GetEntity(_PartID));
        if (!_Part || !IsIndependentNestingPart(*_Part))
            throw std::invalid_argument("冲孔向导只能修改下料区的独立零件，请先加入下料");
        if (!IsTubeManufacturingPart(_Part->GetItemProperties()))
            throw std::invalid_argument("冲孔向导只支持管材零件");

        RestoreNestingResources(*Scene_);
        const auto _ResourceID = _Part->GetManufacturingGeometryResourceID();
        const auto _ResourceVersion = _Part->GetManufacturingGeometryResourceVersion();
        const auto _ExpectedVersion = GetUInt64(
            _Payload, "resourceVersion", _ResourceVersion);
        if (_ResourceID.empty() || _ResourceVersion == 0)
            throw std::runtime_error("零件没有可用的最终 BRep 几何");
        if (_ExpectedVersion != _ResourceVersion || GetString(_Payload,"resourceId",_ResourceID) != _ResourceID)
            throw std::runtime_error("零件几何已变化，请关闭向导后重新打开");

        auto _IncomingFeatures = ReadPunchFeatures(_Payload, "features");
        const auto _Properties = _Part->GetItemProperties();
        const auto _ExistingConfig=ReadEditableDrawingConfig(*_Part,PartDrawing_);
        const bool _HasExistingConfig=!_ExistingConfig.empty();
        const auto _Drawing=_Payload.contains("drawing")?ReadPartDrawing(_Payload)
            :(_ExistingConfig.contains("drawing")?GetRequiredObject(_ExistingConfig,"drawing"):ObjectMap());
        const bool _Rebased=PartDrawingRebased(_ExistingConfig,_Drawing);
        ObjectMap _BaseEndProcess;
        if (const auto _It = _ExistingConfig.find("baseEndProcess"); _It != _ExistingConfig.end() && _It->second.Is<ObjectMap>())
            _BaseEndProcess = _It->second.To<ObjectMap>();

        auto _BaseResourceID = GetString(_ExistingConfig, "baseResourceId", _ResourceID);
        auto _BaseResourceVersion = GetUInt64(_ExistingConfig, "baseResourceVersion", 0);
        if (_BaseResourceID.empty() || _BaseResourceVersion == 0)
        {
            if (_HasExistingConfig && !_ExistingConfig.empty())
                throw std::runtime_error("旧冲孔记录缺少原始基准，无法可靠修改或移除刀具；请重新导入原始零件");
            _BaseResourceID = _ResourceID;
            _BaseResourceVersion = _ResourceVersion;
        }
        auto _Ends = ReadPunchEnds(_Payload);

        const auto _BaseBRep = Scene_->Resources().Get<iCAX::GeometryData::BRepModel>(
            _BaseResourceID, _BaseResourceVersion);
        if (!_BaseBRep)
            throw std::runtime_error("冲孔基准几何版本不可用，请重新加入下料");
        const auto _Rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(*_BaseBRep);
        if (!_Rebuilt.bOK || _Rebuilt.Shape.IsNull())
            throw std::runtime_error("无法读取冲孔基准几何");
        const auto _BaseShape=_Rebased?BuildPunchBlank(GetRequiredObject(GetRequiredObject(_Drawing,"section"),"profile"),GetDouble(_Drawing,"length",0)):_Rebuilt.Shape;
        const auto _UserTools = ProductContext_ ? ListPunchToolUserDataRecords(*GetUserDataStore(ProductContext_)) : VariantArray{};
        SPunchPreviewDiagnostics _ResultState;_ResultState.AllowDisconnectedResults=PartDrawing_;
        const auto _Shape = EvaluatePunch(_BaseShape, _IncomingFeatures, _Ends, Request_, ApplicationContext_, *Scene_, _ExistingConfig,_Rebased,nullptr,nullptr,PartDrawing_?&_ResultState:nullptr,false,0,_UserTools);
        if(PartDrawing_&&_ResultState.SolidCount==0)throw std::invalid_argument("三维零件没有剩余实体，无法保存；请调整刀具");
        if(_Rebased) {
            const auto _Base=StorePunchBRep(*Scene_,"tube-designer/nesting/drawing-base/"+UuidToString(iCAX::Data::GenerateNewUUID()),"三维绘制主管",_BaseShape,Request_);
            _BaseResourceID=_Base.URL;_BaseResourceVersion=_Base.nVersion;
        }

        // Freeze legacy/current-version references into a separately addressable base.
        // The finished part itself keeps its logical resource ID; each Apply publishes
        // a new immutable version on that ID so consumers (including annotations and
        // derived display meshes) can observe a normal version transition.
        if (_BaseResourceID == _ResourceID) {
            const auto _Base = StorePreparedBRep(*Scene_, Scene_->Resources().MakeNamedResourceURL(
                "tube-designer/nesting/punch-base/" + UuidToString(_PartID)),
                "冲孔编辑基准", iCAX::GeometryData::BRepModel(*_BaseBRep));
            _BaseResourceID = _Base.URL;
            _BaseResourceVersion = _Base.nVersion;
        }

        const auto _PresentationName = _Part->GetName().empty()
            ? _Part->GetPartNumber() : _Part->GetName();
        const auto _NewResource = StorePunchBRep(*Scene_,
            _ResourceID, _PresentationName, _Shape, Request_);
        ReportExportProgress(Request_,"mesh",0,1,"正在生成显示网格");
        const auto _Thumbnail = iCAX::RenderInteraction::EnsureFrontendGeometryResource(
            Scene_->Resources(), _NewResource.URL, iCAX::Render::ERenderGeometryKind::Mesh);
        VariantArray _SavedFeatures;
        _SavedFeatures.reserve(_IncomingFeatures.size());
        for (const auto& _Feature : _IncomingFeatures)
            _SavedFeatures.emplace_back(PunchFeatureSnapshot(_Feature));
        ObjectMap _PunchConfig{
            { "schema", std::string(PartDrawing_?"icax.tube-designer.part-drawing":"icax.tube-designer.punch-wizard") },
            { "schemaVersion", 4ull },
            { "baseResourceId", _BaseResourceID },
            { "baseResourceVersion", _BaseResourceVersion },
            { "baseLength", GetDouble(ShapeBounds(_BaseShape), "width", _Part->GetLength()) },
            { "baseBounds", ShapeBounds(_BaseShape) },
            { "baseEndProcess", _BaseEndProcess },
            { "operations", PunchOperationChain(_IncomingFeatures, _Ends) },
            { "ends", PunchEndsSnapshot(_Ends) },
            { "features", std::move(_SavedFeatures) },
        };
        auto _UpdatedProperties = _Properties;
        iCAX::Data::PropertySet _TubeProfileChanges;
        if(!_Drawing.empty()) {
            _PunchConfig["drawing"]=_Drawing;
            _TubeProfileChanges = TubeProfileComponentProperties(
                GetRequiredObject(GetRequiredObject(_Drawing, "section"), "profile"),
                PartDrawing_ ? "part-drawing" : "punch-wizard");
        }
        // Recipe data belongs to the domain component, not the generic
        // manufacturing-part property bag.  Keep only manufacturing-wide
        // metadata here (profile/material/kind/measurement).
        _UpdatedProperties.erase("tubeDesigner.partDrawing");
        _UpdatedProperties.erase("tubeDesigner.punchWizard");
        _UpdatedProperties.erase("tubeDesigner.endProcess");
        auto _Measurement = MeasureFinalPartGeometry(_Shape, _NewResource.URL, _NewResource.nVersion);
        const auto _EnvelopeLength = GetDouble(ShapeBounds(_Shape), "width", 0);
        _Measurement["nestingEnvelopeLength"] = _EnvelopeLength;
        _Measurement["normalizedAxis"] = std::string("+X");
        _UpdatedProperties["manufacturing.geometryMeasurement"] = _Measurement;

        auto _Undo = _DB.BeginUndoCommand("Apply tube punch wizard");
        auto& _Transaction = _DB.BeginTransaction("Apply tube punch wizard");
        bool _Committing = false;
        try
        {
            _Transaction.ModifyComponent(
                _PartID, CManufacturingPartComponent::S_ClassName, {
                    { CManufacturingPartComponent::PropertyName_Length, PropertyValue(_EnvelopeLength) },
                    { CManufacturingPartComponent::PropertyName_ManufacturingGeometryResourceID, PropertyValue(_NewResource.URL) },
                    { CManufacturingPartComponent::PropertyName_ManufacturingGeometryResourceVersion,
                        PropertyValue(_NewResource.nVersion) },
                    { CManufacturingPartComponent::PropertyName_ThumbnailGeometryResourceID,
                        PropertyValue(_Thumbnail.URL) },
                    { CManufacturingPartComponent::PropertyName_ThumbnailGeometryResourceVersion,
                        PropertyValue(_Thumbnail.nVersion) },
                    { CManufacturingPartComponent::PropertyName_ItemProperties,
                        PropertyValue(_UpdatedProperties) },
                    { CManufacturingPartComponent::PropertyName_Status,
                        PropertyValue(std::string("Ready")) },
                });
            if (!_TubeProfileChanges.empty())
                _Transaction.ModifyComponent(
                    _PartID, CTubeProfileComponent::S_ClassName, _TubeProfileChanges);
            const auto _ComponentClass = PartDrawing_
                ? CPartDrawingComponent::S_ClassName : CPunchWizardComponent::S_ClassName;
            const auto _DefinitionProperty = PartDrawing_
                ? CPartDrawingComponent::PropertyName_Definition : CPunchWizardComponent::PropertyName_Definition;
            const bool _HasDomainComponent = PartDrawing_
                ? static_cast<bool>(GetComponent<CPartDrawingComponent>(_Part->GetEntity()))
                : static_cast<bool>(GetComponent<CPunchWizardComponent>(_Part->GetEntity()));
            if (_HasDomainComponent)
            {
                _Transaction.ModifyComponent(_PartID, _ComponentClass,
                    {{ _DefinitionProperty, PropertyValue(_PunchConfig) }});
            }
            else
            {
                _Transaction.AttachComponent(_PartID, _ComponentClass,
                    {{ _DefinitionProperty, PropertyValue(_PunchConfig) }});
            }
            const auto _Meta = _DB.GetMetaEntity();
            const auto _Root = GetComponent<CTubeDesignerRootComponent>(_Meta);
            if (_Root)
            {
                auto _Task = _Root->GetNestingTask();
                _Task["request"] = ObjectMap();
                _Task["result"] = ObjectMap();
                _Task["revision"] = UuidToString(iCAX::Data::GenerateNewUUID());
                _Transaction.ModifyComponent(
                    _Meta->GetID(), CTubeDesignerRootComponent::S_ClassName,
                    {{ CTubeDesignerRootComponent::PropertyName_NestingTask,
                        PropertyValue(_Task) }});
            }
            std::string _Error;
            _Committing = true;
            if (!_DB.CommitTransaction(_Transaction, _Error))
                throw std::runtime_error(_Error.empty() ? "保存冲孔结果失败" : _Error);
        }
        catch (...)
        {
            if (!_Committing)
            {
                try { _DB.CancelTransaction(_Transaction); }
                catch (...) {}
            }
            throw;
        }
        _Undo->End();
        return MakeResponse(Variant(BuildSnapshot(*Scene_, ApplicationContext_)));
    }

    iCAX::Interaction::CInvocationResult HandlePreviewPunchWizard(const iCAX::Interaction::CInvocation& r,
        const iCAX::Application::IApplicationContext& a,iCAX::Product::IProductContext* p,iCAX::Project::IProjectContext* j,iCAX::Project::ISceneContext* s)
    { return PreviewEditableDrawingTools(r,a,p,j,s,false); }
    iCAX::Interaction::CInvocationResult HandlePreviewPartDrawing(const iCAX::Interaction::CInvocation& r,
        const iCAX::Application::IApplicationContext& a,iCAX::Product::IProductContext* p,iCAX::Project::IProjectContext* j,iCAX::Project::ISceneContext* s)
    { return PreviewEditableDrawingTools(r,a,p,j,s,true); }
    iCAX::Interaction::CInvocationResult HandleAddNestingPunchPart(const iCAX::Interaction::CInvocation& r,
        const iCAX::Application::IApplicationContext& a,iCAX::Product::IProductContext* p,iCAX::Project::IProjectContext* j,iCAX::Project::ISceneContext* s)
    { return CreateEditableDrawingPart(r,a,p,j,s,false); }
    iCAX::Interaction::CInvocationResult HandleAddPartDrawing(const iCAX::Interaction::CInvocation& r,
        const iCAX::Application::IApplicationContext& a,iCAX::Product::IProductContext* p,iCAX::Project::IProjectContext* j,iCAX::Project::ISceneContext* s)
    { return CreateEditableDrawingPart(r,a,p,j,s,true); }
    iCAX::Interaction::CInvocationResult HandleApplyPunchWizard(const iCAX::Interaction::CInvocation& r,
        const iCAX::Application::IApplicationContext& a,iCAX::Product::IProductContext* p,iCAX::Project::IProjectContext* j,iCAX::Project::ISceneContext* s)
    { return ApplyEditableDrawingPart(r,a,p,j,s,false); }
    iCAX::Interaction::CInvocationResult HandleApplyPartDrawing(const iCAX::Interaction::CInvocation& r,
        const iCAX::Application::IApplicationContext& a,iCAX::Product::IProductContext* p,iCAX::Project::IProjectContext* j,iCAX::Project::ISceneContext* s)
    { return ApplyEditableDrawingPart(r,a,p,j,s,true); }

    iCAX::Interaction::CInvocationResult ExportTransientDisassembly(
        const iCAX::Interaction::CInvocation& Request_,
        iCAX::Project::ISceneContext& Scene_,
        const std::vector<SPreparedProductDisassembly>& PreparedProducts_,
        const std::set<std::string>& SelectedPartIDs_)
    {
        auto& _Repository = Scene_.Database();
        std::vector<std::pair<const SPreparedProductDisassembly*, std::shared_ptr<CProductInstanceComponent>>> _Groups;
        std::set<std::string> _MatchedPartIDs;
        for (const auto& _Prepared : PreparedProducts_)
        {
            const auto _Product = GetComponent<CProductInstanceComponent>(
                _Repository.GetEntity(_Prepared.ProductID));
            if (!_Product || _Product->GetActiveGenerationRunID() != _Prepared.GenerationRunID)
                continue;
            for (const auto& _Part : _Prepared.Parts)
            {
                const auto _ID = UuidToString(_Part.PartID);
                if (SelectedPartIDs_.empty() || SelectedPartIDs_.contains(_ID))
                    _MatchedPartIDs.insert(_ID);
            }
            _Groups.emplace_back(&_Prepared, _Product);
        }
        if (!SelectedPartIDs_.empty() && _MatchedPartIDs != SelectedPartIDs_)
            throw std::runtime_error("TubeDesigner export selection contains stale transient parts");
        if (_Groups.empty())
            throw std::invalid_argument("当前没有可导出的临时拆单结果，请先重新拆单");

        std::uint64_t _TotalPartCount = 0;
        for (const auto& [_Prepared, _Product] : _Groups)
            for (const auto& _Part : _Prepared->Parts)
                if (SelectedPartIDs_.empty() || SelectedPartIDs_.contains(UuidToString(_Part.PartID)))
                    ++_TotalPartCount;
        ReportExportProgress(Request_, "preparing", 0, _TotalPartCount, "正在准备 STEP 导出");

        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _TargetDirectory = GetString(_Payload, "targetDirectory");
        const auto _TargetRoot = Utf8Path(_TargetDirectory);
        std::filesystem::create_directories(_TargetRoot);
        VariantArray _Files;
        VariantArray _ExportedGroups;
        std::vector<SPartListRow> _PartListRows;
        std::set<std::string> _FolderNames;
        std::uint64_t _ProductIndex = 0;
        std::uint64_t _CompletedPartCount = 0;
        for (const auto& [_Prepared, _Product] : _Groups)
        {
            ++_ProductIndex;
            auto _FolderName = MakeSafePathSegment(_Product->GetName(), _Product->GetProductCode())
                + InstanceQuantitySuffix(_Product->GetQuantity());
            if (_FolderNames.contains(_FolderName))
                _FolderName = MakeSafePathSegment(_Product->GetName(), _Product->GetProductCode())
                    + "_" + UuidToString(_Prepared->ProductID).substr(0, 8)
                    + InstanceQuantitySuffix(_Product->GetQuantity());
            _FolderNames.insert(_FolderName);
            const auto _ProductDirectory = _TargetRoot / Utf8Path(_FolderName);
            std::filesystem::create_directories(_ProductDirectory);
            VariantArray _GroupFiles;
            std::uint64_t _GroupCount = 0;
            for (const auto& _Part : _Prepared->Parts)
            {
                if (!SelectedPartIDs_.empty() && !SelectedPartIDs_.contains(UuidToString(_Part.PartID)))
                    continue;
                const auto _TargetPath = Utf8PathText(_ProductDirectory / Utf8Path(_Part.FileName));
                const auto _Result = Scene_.Resources().Export<iCAX::GeometryData::BRepModel>(
                    _Part.ManufacturingResource.URL, _TargetPath,
                    { { "resourceVersion", std::to_string(_Part.ManufacturingResource.nVersion) } },
                    "cad.step");
                if (!_Result.IsOK())
                    throw std::runtime_error(_Result.Error.empty() ? "TubeDesigner STEP export failed" : _Result.Error);
                _Files.emplace_back(_TargetPath);
                _GroupFiles.emplace_back(_TargetPath);
                ++_GroupCount;
                ++_CompletedPartCount;
                ReportExportProgress(Request_, "exporting", _CompletedPartCount, _TotalPartCount,
                    "已导出 " + _Part.FileName);

                const auto _Properties = _Part.ItemProperties;
                const auto _Profile = _Part.TubeProfile;
                const auto _Kind = ManufacturingPartKind(_Properties);
                const auto _Plate = ManufacturingPlate(_Properties);
                _PartListRows.push_back({
                    _ProductIndex,
                    _Product->GetName(),
                    _Product->GetProductCode(),
                    _Part.Index,
                    _Part.PartNumber,
                    MakePartNameFromFileName(_Part.FileName, _Part.PartNumber),
                    OptionalPropertyString(_Profile, "displayName", ManufacturingKindName(_Kind)),
                    _Kind == "plate" || _Kind == "glass" ? PlateSpecification(_Plate)
                        : (_Kind == "accessory" ? ComponentSpecification(_Properties)
                            : OptionalPropertyString(_Profile, "specification")),
                    _Part.Length,
                    ProductionQuantity(_Part.Quantity, _Product->GetQuantity()),
                    _Part.FileName,
                    Utf8PathText(Utf8Path(_FolderName) / Utf8Path(_Part.FileName)),
                    _Kind,
                    GetDouble(_Plate, "width", 0), GetDouble(_Plate, "height", 0), GetDouble(_Plate, "thickness", 0),
                    GetString(_Properties, "manufacturing.material"),
                    GetString(_Properties, "manufacturing.sourcing",
                        _Kind == "glass" || _Kind == "accessory" ? "purchased" : "made"),
                    GetString(_Properties, "manufacturing.process")
                });
            }
            if (_GroupCount == 0) continue;
            ObjectMap _GroupResult;
            _GroupResult["productEntityId"] = UuidToString(_Prepared->ProductID);
            _GroupResult["name"] = _Product->GetName();
            _GroupResult["quantity"] = _Product->GetQuantity();
            _GroupResult["directory"] = Utf8PathText(_ProductDirectory);
            _GroupResult["exportedFiles"] = _GroupFiles;
            _GroupResult["exportedCount"] = static_cast<unsigned long long>(_GroupFiles.size());
            _ExportedGroups.emplace_back(_GroupResult);
        }
        const auto _PartListPath = _TargetRoot / Utf8Path("零件清单.xlsx");
        ReportExportProgress(Request_, "workbook", _CompletedPartCount, _TotalPartCount,
            "正在生成零件清单.xlsx");
        WritePartListWorkbook(_PartListPath, _PartListRows);
        ReportExportProgress(Request_, "completed", _CompletedPartCount, _TotalPartCount,
            "STEP 与 Excel 文件已经写入目标目录");
        ObjectMap _Result;
        _Result["exportedFiles"] = _Files;
        _Result["exportedCount"] = static_cast<unsigned long long>(_Files.size());
        _Result["exportedGroups"] = _ExportedGroups;
        _Result["partListFile"] = Utf8PathText(_PartListPath);
        return MakeResponse(Variant(_Result));
    }

    // Standalone drawing and punch parts are persisted as independent nesting
    // parts (ProductID is nil).  They do not belong to a product disassembly,
    // so the product-only exporter must not treat their IDs as stale.  Export
    // them using the same STEP + Excel contract as product parts, grouped by
    // their independent batch/generation run.
    iCAX::Interaction::CInvocationResult ExportIndependentNestingParts(
        const iCAX::Interaction::CInvocation& Request_,
        iCAX::Project::ISceneContext& Scene_,
        const std::set<std::string>& SelectedPartIDs_)
    {
        struct SGroup final
        {
            std::string ID;
            ObjectMap Snapshot;
            std::vector<std::pair<std::shared_ptr<iCAX::Database::IEntity>,
                std::shared_ptr<CManufacturingPartComponent>>> Parts;
        };

        auto& _Repository = Scene_.Database();
        RestoreNestingResources(Scene_);
        std::map<std::string, SGroup> _Groups;
        std::set<std::string> _MatchedPartIDs;
        for (const auto& [_Entity, _Part] : Collect<CManufacturingPartComponent>(_Repository))
        {
            if (!IsIndependentNestingPart(*_Part)) continue;
            const auto _ID = UuidToString(_Entity->GetID());
            if (!SelectedPartIDs_.empty() && !SelectedPartIDs_.contains(_ID)) continue;
            const auto _GroupID = UuidToString(_Part->GetGenerationRunID());
            auto& _Group = _Groups[_GroupID];
            _Group.ID = _GroupID;
            if (_Group.Snapshot.empty())
                _Group.Snapshot = _Part->GetItemProperties().at("nesting.snapshot").To<ObjectMap>();
            _Group.Parts.emplace_back(_Entity, _Part);
            _MatchedPartIDs.insert(_ID);
        }
        if (!SelectedPartIDs_.empty() && _MatchedPartIDs != SelectedPartIDs_)
            throw std::runtime_error("TubeDesigner export selection contains stale manufacturing parts");
        if (_Groups.empty())
            throw std::invalid_argument("select at least one current manufacturing part");

        std::uint64_t _TotalPartCount = 0;
        for (const auto& [_ID, _Group] : _Groups)
            _TotalPartCount += static_cast<std::uint64_t>(_Group.Parts.size());
        ReportExportProgress(Request_, "preparing", 0, _TotalPartCount, "正在准备 STEP 导出");

        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _TargetDirectory = GetString(_Payload, "targetDirectory");
        const auto _TargetRoot = Utf8Path(_TargetDirectory);
        std::filesystem::create_directories(_TargetRoot);
        VariantArray _Files;
        VariantArray _ExportedGroups;
        std::vector<SPartListRow> _PartListRows;
        std::set<std::string> _FolderNames;
        std::uint64_t _ProductIndex = 0;
        std::uint64_t _CompletedPartCount = 0;
        for (auto& [_GroupID, _Group] : _Groups)
        {
            ++_ProductIndex;
            const auto _Name = GetString(_Group.Snapshot, "name", "独立零件");
            const auto _Source = GetString(_Group.Snapshot, "source", "nesting");
            const auto _ProductCode = GetString(_Group.Snapshot, "productCode", _Source);
            const auto _GroupQuantity = GetUInt64(_Group.Snapshot, "quantity", 1);
            auto _FolderName = MakeSafePathSegment(_Name, _ProductCode)
                + InstanceQuantitySuffix(_GroupQuantity);
            if (_FolderNames.contains(_FolderName))
                _FolderName = MakeSafePathSegment(_Name, _ProductCode)
                    + "_" + (_GroupID.size() > 8 ? _GroupID.substr(0, 8) : _GroupID)
                    + InstanceQuantitySuffix(_GroupQuantity);
            _FolderNames.insert(_FolderName);
            const auto _ProductDirectory = _TargetRoot / Utf8Path(_FolderName);
            std::filesystem::create_directories(_ProductDirectory);
            VariantArray _GroupFiles;
            std::sort(_Group.Parts.begin(), _Group.Parts.end(), [](const auto& Left_, const auto& Right_) {
                return Left_.second->GetPartIndex() < Right_.second->GetPartIndex();
            });
            for (const auto& [_PartEntity, _Part] : _Group.Parts)
            {
                const auto _TargetPath = Utf8PathText(_ProductDirectory / Utf8Path(_Part->GetFileName()));
                const auto _Result = Scene_.Resources().Export<iCAX::GeometryData::BRepModel>(
                    _Part->GetManufacturingGeometryResourceID(), _TargetPath,
                    { { "resourceVersion", std::to_string(_Part->GetManufacturingGeometryResourceVersion()) } },
                    "cad.step");
                if (!_Result.IsOK())
                    throw std::runtime_error(_Result.Error.empty() ? "TubeDesigner STEP export failed" : _Result.Error);
                _Files.emplace_back(_TargetPath);
                _GroupFiles.emplace_back(_TargetPath);
                ++_CompletedPartCount;
                ReportExportProgress(Request_, "exporting", _CompletedPartCount, _TotalPartCount,
                    "已导出 " + _Part->GetFileName());

                const auto _Presentation = ResolvePartPresentation(_Repository, *_Part);
                const auto _Properties = _Part->GetItemProperties();
                const auto _Profile = _Presentation.Profile;
                const auto _Kind = ManufacturingPartKind(_Properties);
                const auto _Plate = ManufacturingPlate(_Properties);
                _PartListRows.push_back({
                    _ProductIndex,
                    _Name,
                    _ProductCode,
                    _Part->GetPartIndex(),
                    _Part->GetPartNumber(),
                    _Presentation.Name,
                    OptionalPropertyString(_Profile, "displayName", ManufacturingKindName(_Kind)),
                    _Kind == "plate" || _Kind == "glass" ? PlateSpecification(_Plate)
                        : (_Kind == "accessory" ? ComponentSpecification(_Properties)
                            : OptionalPropertyString(_Profile, "specification")),
                    _Part->GetLength(),
                    EffectiveManufacturingQuantity(*_Part, 1),
                    _Part->GetFileName(),
                    Utf8PathText(Utf8Path(_FolderName) / Utf8Path(_Part->GetFileName())),
                    _Kind,
                    GetDouble(_Plate, "width", 0), GetDouble(_Plate, "height", 0), GetDouble(_Plate, "thickness", 0),
                    GetString(_Properties, "manufacturing.material"),
                    GetString(_Properties, "manufacturing.sourcing",
                        _Kind == "glass" || _Kind == "accessory" ? "purchased" : "made"),
                    GetString(_Properties, "manufacturing.process")
                });
            }
            ObjectMap _GroupResult;
            _GroupResult["productEntityId"] = _GroupID;
            _GroupResult["generationRunId"] = _GroupID;
            _GroupResult["name"] = _Name;
            _GroupResult["quantity"] = _GroupQuantity;
            _GroupResult["productCode"] = _ProductCode;
            _GroupResult["directory"] = Utf8PathText(_ProductDirectory);
            _GroupResult["exportedFiles"] = _GroupFiles;
            _GroupResult["exportedCount"] = static_cast<unsigned long long>(_GroupFiles.size());
            _ExportedGroups.emplace_back(_GroupResult);
        }
        const auto _PartListPath = _TargetRoot / Utf8Path("零件清单.xlsx");
        ReportExportProgress(Request_, "workbook", _CompletedPartCount, _TotalPartCount,
            "正在生成零件清单.xlsx");
        WritePartListWorkbook(_PartListPath, _PartListRows);
        ReportExportProgress(Request_, "completed", _CompletedPartCount, _TotalPartCount,
            "STEP 与 Excel 文件已经写入目标目录");
        ObjectMap _Result;
        _Result["exportedFiles"] = _Files;
        _Result["exportedCount"] = static_cast<unsigned long long>(_Files.size());
        _Result["exportedGroups"] = _ExportedGroups;
        _Result["partListFile"] = Utf8PathText(_PartListPath);
        return MakeResponse(Variant(_Result));
    }

    iCAX::Interaction::CInvocationResult HandleExportSelected(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_) throw std::invalid_argument("TubeDesigner.ExportSelected requires a scene");
        tube::license::Enforce<301, tube::license::Feature::StepExport>();
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

        const auto _TransientDisassembly = CopyTransientDisassembly(*Scene_);
        bool _HasTransientSelection = !_TransientDisassembly.empty() && !_HasSelection;
        if (_HasSelection)
        {
            for (const auto& _Prepared : _TransientDisassembly)
                for (const auto& _Part : _Prepared.Parts)
                    if (_SelectedPartIDs.contains(UuidToString(_Part.PartID)))
                        _HasTransientSelection = true;
        }
        if (_HasTransientSelection)
        {
            const auto _Result = ExportTransientDisassembly(
                Request_, *Scene_, _TransientDisassembly, _SelectedPartIDs);
            // The direct-export session has fulfilled its only purpose.
            // Release all export-only geometry immediately; parts linked into
            // nesting remain resident regardless of the export selection.
            ReleaseTransientParts(*Scene_, {});
            auto _Response = iCAX::Data::VariantSerializer::Deserialize(
                std::string(_Result.Payload.begin(), _Result.Payload.end())).To<ObjectMap>();
            const auto _Snapshot = BuildSnapshot(*Scene_, ApplicationContext_, false);
            _Response["tubeDesigner"] = _Snapshot.at("tubeDesigner");
            return MakeResponse(Variant(_Response));
        }

        // Drawing/punch wizard parts are independent persisted nesting parts,
        // not members of a product generation.  When the selection consists
        // solely of those parts, export them directly instead of sending their
        // IDs through the product-generation matcher (which would report them
        // as stale manufacturing parts).
        if (_HasSelection)
        {
            auto& _Repository = Scene_->Database();
            std::set<std::string> _IndependentSelection;
            for (const auto& [_Entity, _Part] : Collect<CManufacturingPartComponent>(_Repository))
                if (IsIndependentNestingPart(*_Part))
                {
                    const auto _ID = UuidToString(_Entity->GetID());
                    if (_SelectedPartIDs.contains(_ID)) _IndependentSelection.insert(_ID);
                }
            if (!_IndependentSelection.empty() && _IndependentSelection == _SelectedPartIDs)
            {
                const auto _Result = ExportIndependentNestingParts(
                    Request_, *Scene_, _SelectedPartIDs);
                auto _Response = iCAX::Data::VariantSerializer::Deserialize(
                    std::string(_Result.Payload.begin(), _Result.Payload.end())).To<ObjectMap>();
                const auto _Snapshot = BuildSnapshot(*Scene_, ApplicationContext_, false);
                _Response["tubeDesigner"] = _Snapshot.at("tubeDesigner");
                return MakeResponse(Variant(_Response));
            }
        }

        RestoreDesignerResources(*Scene_, ApplicationContext_, true);
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
            auto _FolderName = MakeSafePathSegment(_Product->GetName(), _Product->GetProductCode())
                + InstanceQuantitySuffix(_Product->GetQuantity());
            if (_FolderNames.contains(_FolderName))
            {
                _FolderName = MakeSafePathSegment(_Product->GetName(), _Product->GetProductCode())
                    + "_" + UuidToString(_ProductEntity->GetID()).substr(0, 8)
                    + InstanceQuantitySuffix(_Product->GetQuantity());
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
                const auto _Properties = _Part->GetItemProperties();
                const auto _Kind = ManufacturingPartKind(_Properties);
                const auto _Plate = ManufacturingPlate(_Properties);
                _PartListRows.push_back({
                    _ProductIndex,
                    _Product->GetName(),
                    _Product->GetProductCode(),
                    _Part->GetPartIndex(),
                    _Part->GetPartNumber(),
                    _Presentation.Name,
                    OptionalPropertyString(_Presentation.Profile, "displayName", ManufacturingKindName(_Kind)),
                    _Kind == "plate" || _Kind == "glass" ? PlateSpecification(_Plate)
                        : (_Kind == "accessory" ? ComponentSpecification(_Properties)
                            : OptionalPropertyString(_Presentation.Profile, "specification")),
                    _Part->GetLength(),
                    EffectiveManufacturingQuantity(*_Part, _Product->GetQuantity()),
                    _Part->GetFileName(),
                    Utf8PathText(Utf8Path(_FolderName) / Utf8Path(_Part->GetFileName())),
                    _Kind,
                    GetDouble(_Plate, "width", 0), GetDouble(_Plate, "height", 0), GetDouble(_Plate, "thickness", 0),
                    GetString(_Properties, "manufacturing.material"),
                    GetString(_Properties, "manufacturing.sourcing",
                        _Kind == "glass" || _Kind == "accessory" ? "purchased" : "made"),
                    GetString(_Properties, "manufacturing.process")
                });
            }
            ObjectMap _GroupResult;
            _GroupResult["productEntityId"] = UuidToString(_ProductEntity->GetID());
            _GroupResult["name"] = _Product->GetName();
            _GroupResult["quantity"] = _Product->GetQuantity();
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

    iCAX::Interaction::CInvocationResult HandleSaveNestingSettings(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_) throw std::invalid_argument("TubeDesigner.SaveNestingSettings requires a scene");
        if (Request_.Payload.size() > 8 * 1024 * 1024)
            throw std::invalid_argument("母材设置过大，请减少母材行数");
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _Settings = NormalizeNestingSettings(GetRequiredObject(_Payload, "settings"));
        auto& _Repository = Scene_->Database();
        const auto _Meta = _Repository.GetMetaEntity();
        if (!_Meta) throw std::runtime_error("TubeDesigner requires repository meta entity");
        const auto _Root = GetComponent<CTubeDesignerRootComponent>(_Meta);
        if (_Root && _Root->GetNestingSettings() == _Settings)
            return MakeResponse(Variant(ObjectMap{ { "settings", _Settings } }));
        auto _Undo = _Repository.BeginUndoCommand("Save TubeDesigner nesting settings");
        auto& _Transaction = _Repository.BeginTransaction("Update TubeDesigner nesting settings");
        bool _CommitStarted = false;
        try
        {
            QueueUpsertComponent(
                _Transaction, _Meta, _Meta->GetID(), CTubeDesignerRootComponent::S_ClassName,
                { { CTubeDesignerRootComponent::PropertyName_NestingSettings, PropertyValue(_Settings) } });
            std::string _Error;
            _CommitStarted = true;
            if (!_Repository.CommitTransaction(_Transaction, _Error))
                throw std::runtime_error(_Error.empty() ? "保存排样设置失败" : _Error);
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
        return MakeResponse(Variant(ObjectMap{ { "settings", _Settings } }));
    }

    TopoDS_Shape NestingManufacturingShape(
        iCAX::Project::ISceneContext& Scene_, const CManufacturingPartComponent& Part_)
    {
        const auto _BRep = Scene_.Resources().Get<iCAX::GeometryData::BRepModel>(
            Part_.GetManufacturingGeometryResourceID(), Part_.GetManufacturingGeometryResourceVersion());
        if (!_BRep) throw std::invalid_argument("零件的最终制造几何资源不可用，请重新拆单");
        const auto _Rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(*_BRep);
        auto _Shape = _Rebuilt.Shape;
        if (!_Rebuilt.bOK || _Shape.IsNull())
            _Shape = RecoverManufacturingShapeFromGeneration(Scene_.Database(), Part_);
        if (_Shape.IsNull()) throw std::runtime_error("无法读取零件最终几何，已停止排样以避免截短零件");
        return _Shape;
    }

    TopoDS_Shape NestingManufacturingShape(
        iCAX::Project::ISceneContext& Scene_, const SPreparedManufacturingPart& Part_)
    {
        const auto _BRep = Scene_.Resources().Get<iCAX::GeometryData::BRepModel>(
            Part_.ManufacturingResource.URL, Part_.ManufacturingResource.nVersion);
        if (!_BRep) throw std::invalid_argument("零件的临时制造几何已释放，请重新导入下料");
        const auto _Rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(*_BRep);
        if (!_Rebuilt.bOK || _Rebuilt.Shape.IsNull())
            throw std::runtime_error("无法读取零件最终几何，已停止排样以避免截短零件");
        return _Rebuilt.Shape;
    }

    struct SNestingGeometrySummary final
    {
        double EnvelopeLength = 0.0;
        SLinearNestingGeometry LinearGeometry;
        std::array<double, 3> LocalCenter{ 0.0, 0.0, 0.0 };
        iCAX::TubeNesting::CutLineFeatureCode LeftCutLineFeature;
        iCAX::TubeNesting::CutLineFeatureCode RightCutLineFeature;
    };

    bool HasNestingCutLineFeatures(const SNestingGeometrySummary& Summary_)
    {
        return iCAX::TubeNesting::IsValidCutLineFeature(Summary_.LeftCutLineFeature)
            && iCAX::TubeNesting::IsValidCutLineFeature(Summary_.RightCutLineFeature)
            && Summary_.LeftCutLineFeature.Period == Summary_.RightCutLineFeature.Period;
    }

    struct SNestingGeometryCache final
    {
        std::mutex Mutex;
        std::map<std::string, SNestingGeometrySummary> Measurements;
        std::set<std::string> PendingFeatureJobs;
        std::shared_ptr<iCAX::Tasks::ThreadPoolTaskScheduler> Scheduler =
            std::make_shared<iCAX::Tasks::ThreadPoolTaskScheduler>(1);
        std::atomic_bool Stopping = false;

        ~SNestingGeometryCache()
        {
            Stopping.store(true, std::memory_order_release);
            Scheduler->Shutdown();
        }
    };

    SNestingGeometryCache& NestingGeometryCache()
    {
        static SNestingGeometryCache _Cache;
        return _Cache;
    }

    void QueueNestingCutLineFeatureGeneration(std::string Key_, TopoDS_Shape Shape_)
    {
        auto& _Cache = NestingGeometryCache();
        {
            const std::lock_guard _Lock(_Cache.Mutex);
            const auto _Found = _Cache.Measurements.find(Key_);
            if (_Found == _Cache.Measurements.end()
                || HasNestingCutLineFeatures(_Found->second)
                || !_Cache.PendingFeatureJobs.insert(Key_).second)
                return;
        }

        // Full side unfolding is useful for the precise cut-line matcher, but
        // it is not a prerequisite for nesting.  It can take about 91 seconds
        // for the 82 five-face parts, so keep it off the SDO request path and
        // publish it into the same resource-version cache when it is ready.
        const auto _PendingKey = Key_;
        try
        {
            (void)iCAX::Tasks::Run([Key_ = std::move(Key_), Shape_ = std::move(Shape_)] {
                STubeUnfoldedEndFeatureCodes _Features;
                try
                {
                    auto& _Cache = NestingGeometryCache();
                    if (!_Cache.Stopping.load(std::memory_order_acquire))
                    {
                        STubeBRepUnfoldingOptions _Options;
                        _Options.StationCount = 5;
                        _Options.SamplesPerCurve = 33;
                        _Options.MaximumOutputPoints = 20000;
                        _Options.IncludeInnerSurfaces = false;
                        const auto _Unfolded = UnfoldTubeBRepSurface(Shape_, _Options);
                        if (_Unfolded.bOK)
                            _Features = EncodeTubeUnfoldedEndFeatures(_Unfolded, 64);
                    }
                }
                catch (const std::exception&)
                {
                    // Knife-plane summaries remain the authoritative fast fallback
                    // when an individual side cannot be unfolded.
                }
                catch (...)
                {
                    // Keep a failed background feature job from affecting nesting.
                }

                auto& _Cache = NestingGeometryCache();
                const std::lock_guard _Lock(_Cache.Mutex);
                if (_Features.bOK)
                {
                    const auto _Found = _Cache.Measurements.find(Key_);
                    if (_Found != _Cache.Measurements.end())
                    {
                        _Found->second.LeftCutLineFeature = _Features.Left;
                        _Found->second.RightCutLineFeature = _Features.Right;
                    }
                }
                _Cache.PendingFeatureJobs.erase(Key_);
            }, _Cache.Scheduler);
        }
        catch (...)
        {
            const std::lock_guard _Lock(_Cache.Mutex);
            _Cache.PendingFeatureJobs.erase(_PendingKey);
        }
    }

    SNestingGeometrySummary NestingGeometrySummary(
        iCAX::Project::ISceneContext& Scene_, const CManufacturingPartComponent& Part_)
    {
        const auto _ResourceID = Part_.GetManufacturingGeometryResourceID();
        const auto _Version = Part_.GetManufacturingGeometryResourceVersion();
        if (_ResourceID.empty() || _Version == 0)
            throw std::invalid_argument("零件缺少最终制造几何，请回到产品重新拆单");
        const auto _Key = _ResourceID + "@" + std::to_string(_Version);
        auto& _Cache = NestingGeometryCache();
        {
            const std::lock_guard _Lock(_Cache.Mutex);
            if (const auto _Found = _Cache.Measurements.find(_Key);
                _Found != _Cache.Measurements.end())
            {
                auto _Result = _Found->second;
                _Result.EnvelopeLength = std::max(Part_.GetLength(), _Result.EnvelopeLength);
                return _Result;
            }
        }
        const auto _Shape = NestingManufacturingShape(Scene_, Part_);
        // Disassembly normalizes manufacturing geometry to +X with a centered box.
        Bnd_Box _Box;
        BRepBndLib::AddOptimal(_Shape, _Box, false, false);
        _Box.SetGap(0.0);
        if (_Box.IsVoid() || _Box.IsWhole())
            throw std::runtime_error("零件轴向包围长度无效，无法安全排样");
        double _X0, _Y0, _Z0, _X1, _Y1, _Z1;
        _Box.Get(_X0, _Y0, _Z0, _X1, _Y1, _Z1);
        const auto _Length = _X1 - _X0;
        if (!std::isfinite(_Length) || _Length <= 0.0)
            throw std::runtime_error("零件轴向包围长度必须大于 0");
        SNestingGeometrySummary _Result{
            _Length, MeasureLinearNestingGeometry(_Shape),
            { (_X0 + _X1) * 0.5, (_Y0 + _Y1) * 0.5, (_Z0 + _Z1) * 0.5 }, {}, {}
        };
        if (_Result.LinearGeometry.IsReliable)
        {
            _Result.EnvelopeLength = _Result.LinearGeometry.EnvelopeLength;
            _Result.LocalCenter[0] = (_Result.LinearGeometry.AxialMinimum
                + _Result.LinearGeometry.AxialMaximum) * 0.5;
        }
        {
            const std::lock_guard _Lock(_Cache.Mutex);
            if (_Cache.Measurements.size() > 10000) _Cache.Measurements.clear();
            _Cache.Measurements[_Key] = _Result;
        }
        QueueNestingCutLineFeatureGeneration(_Key, _Shape);
        _Result.EnvelopeLength = std::max(Part_.GetLength(), _Result.EnvelopeLength);
        return _Result;
    }

    SNestingGeometrySummary NestingGeometrySummary(
        iCAX::Project::ISceneContext& Scene_, const SPreparedManufacturingPart& Part_)
    {
        const auto _ResourceID = Part_.ManufacturingResource.URL;
        const auto _Version = Part_.ManufacturingResource.nVersion;
        if (_ResourceID.empty() || _Version == 0)
            throw std::invalid_argument("零件缺少最终制造几何，请重新导入下料");
        const auto _Key = _ResourceID + "@" + std::to_string(_Version);
        auto& _Cache = NestingGeometryCache();
        {
            const std::lock_guard _Lock(_Cache.Mutex);
            if (const auto _Found = _Cache.Measurements.find(_Key);
                _Found != _Cache.Measurements.end())
            {
                auto _Result = _Found->second;
                _Result.EnvelopeLength = std::max(Part_.Length, _Result.EnvelopeLength);
                return _Result;
            }
        }
        const auto _Shape = NestingManufacturingShape(Scene_, Part_);
        Bnd_Box _Box;
        BRepBndLib::AddOptimal(_Shape, _Box, false, false);
        _Box.SetGap(0.0);
        if (_Box.IsVoid() || _Box.IsWhole())
            throw std::runtime_error("零件轴向包围长度无效，无法安全排样");
        double _X0, _Y0, _Z0, _X1, _Y1, _Z1;
        _Box.Get(_X0, _Y0, _Z0, _X1, _Y1, _Z1);
        const auto _Length = _X1 - _X0;
        if (!std::isfinite(_Length) || _Length <= 0.0)
            throw std::runtime_error("零件轴向包围长度必须大于 0");
        SNestingGeometrySummary _Result{
            _Length, MeasureLinearNestingGeometry(_Shape),
            { (_X0 + _X1) * 0.5, (_Y0 + _Y1) * 0.5, (_Z0 + _Z1) * 0.5 }, {}, {}
        };
        if (_Result.LinearGeometry.IsReliable)
        {
            _Result.EnvelopeLength = _Result.LinearGeometry.EnvelopeLength;
            _Result.LocalCenter[0] = (_Result.LinearGeometry.AxialMinimum
                + _Result.LinearGeometry.AxialMaximum) * 0.5;
        }
        {
            const std::lock_guard _Lock(_Cache.Mutex);
            if (_Cache.Measurements.size() > 10000) _Cache.Measurements.clear();
            _Cache.Measurements[_Key] = _Result;
        }
        QueueNestingCutLineFeatureGeneration(_Key, _Shape);
        _Result.EnvelopeLength = std::max(Part_.Length, _Result.EnvelopeLength);
        return _Result;
    }

    std::string LowerAscii(std::string Value_)
    {
        std::ranges::transform(Value_, Value_.begin(), [](const unsigned char Value_) {
            return static_cast<char>(std::tolower(Value_));
        });
        return Value_;
    }

    bool ProfileAllowsNestingRotations(const ObjectMap& Profile_)
    {
        const auto _Identity = LowerAscii(GetString(Profile_, "id") + " " + GetString(Profile_, "kind"));
        if (_Identity.find("round") != std::string::npos
            || _Identity.find("circle") != std::string::npos
            || _Identity.find("ellipse") != std::string::npos
            || _Identity.find("oval") != std::string::npos
            || _Identity.find("rect") != std::string::npos
            || _Identity.find("square") != std::string::npos)
            return true;
        if (_Identity.find("polygon") == std::string::npos) return false;
        const auto _Parameters = Profile_.find("parameters");
        if (_Parameters == Profile_.end() || !_Parameters->second.Is<ObjectMap>()) return false;
        const auto _Values = _Parameters->second.To<ObjectMap>();
        const auto _SideCount = GetDouble(_Values, "sideCount", 0.0);
        return std::isfinite(_SideCount) && _SideCount >= 4.0
            && std::floor(_SideCount) == _SideCount
            && static_cast<long long>(_SideCount) % 2 == 0;
    }

    std::vector<SNestingVariant> BuildNestingVariants(
        const ObjectMap& Profile_, const SLinearNestingGeometry& Geometry_,
        const double EnvelopeLength_,
        const iCAX::TubeNesting::CutLineFeatureCode& LeftFeature_,
        const iCAX::TubeNesting::CutLineFeatureCode& RightFeature_)
    {
        auto _Variants = BuildKnifePlaneNestingVariants(
            Geometry_, EnvelopeLength_, ProfileAllowsNestingRotations(Profile_));
        const bool _HasFeatures = iCAX::TubeNesting::IsValidCutLineFeature(LeftFeature_)
            && iCAX::TubeNesting::IsValidCutLineFeature(RightFeature_)
            && LeftFeature_.Period == RightFeature_.Period;
        if (_Variants.empty() && _HasFeatures)
        {
            SNestingVariant _Fallback;
            _Fallback.ID = "forward-0";
            _Fallback.EnvelopeLength = EnvelopeLength_;
            _Fallback.MaterialLength = EnvelopeLength_;
            _Variants.push_back(std::move(_Fallback));
        }
        if (_HasFeatures)
        {
            for (auto& _Variant : _Variants)
            {
                _Variant.LeftEnd.Feature = LeftFeature_;
                _Variant.RightEnd.Feature = RightFeature_;

                // The feature samples use the section's unrolled U origin.
                // Keep the existing physical rotation as the source of truth,
                // and carry its circumferential displacement as a compact phase
                // value for the adjacency matcher.  This avoids duplicating a
                // rotated sample array for every variant.
                const long double _TwoPi = 2.0L * static_cast<long double>(kPunchPi);
                long double _Phase = static_cast<long double>(LeftFeature_.Period)
                    * static_cast<long double>(_Variant.RotationRadians) / _TwoPi;
                _Phase = std::fmod(_Phase, static_cast<long double>(LeftFeature_.Period));
                if (_Phase < 0.0L) _Phase += static_cast<long double>(LeftFeature_.Period);
                if (std::isfinite(static_cast<double>(_Phase)))
                    _Variant.PhaseOffset = static_cast<double>(std::llround(_Phase));
            }
        }
        return _Variants;
    }

    std::optional<const SNestingVariant*> FindNestingVariant(
        const SNestingPart& Part_, const std::string& ID_,
        const bool Reversed_, const double Rotation_, const double PhaseOffset_ = 0.0)
    {
        if (Part_.Variants.empty())
            return ID_ == "default" && !Reversed_ && std::abs(Rotation_) <= 1.0e-9
                && std::abs(PhaseOffset_) <= 1.0e-9
                ? std::optional<const SNestingVariant*>{ nullptr } : std::nullopt;
        const auto _Found = std::ranges::find_if(Part_.Variants, [&](const auto& Variant_) {
            return Variant_.ID == ID_ && Variant_.Reversed == Reversed_
                && std::abs(Variant_.RotationRadians - Rotation_) <= 1.0e-9
                && std::abs(Variant_.PhaseOffset - PhaseOffset_) <= 0.011;
        });
        return _Found == Part_.Variants.end()
            ? std::nullopt : std::optional<const SNestingVariant*>{ &*_Found };
    }

    double QuantizedNestingLength(const double Value_, const bool RoundUp_)
    {
        return (RoundUp_ ? std::ceil(Value_ * 100.0 - 1.0e-6)
                         : std::floor(Value_ * 100.0 + 1.0e-6)) / 100.0;
    }

    ObjectMap SolveManufacturingNestingWithLocks(
        const std::vector<SNestingPart>& Parts_, const std::vector<SNestingStock>& Stocks_,
        const double Gap_, const VariantArray& LockedValues_)
    {
        if (LockedValues_.empty()) return SolveManufacturingNesting(Parts_, Stocks_, Gap_);
        constexpr double _Tolerance = 0.011;
        std::map<std::string, const SNestingPart*> _PartsByID;
        std::map<std::string, const SNestingStock*> _StocksByID;
        for (const auto& _Part : Parts_) _PartsByID.emplace(_Part.ID, &_Part);
        for (const auto& _Stock : Stocks_) _StocksByID.emplace(_Stock.ID, &_Stock);
        std::map<std::string, std::size_t> _LockedPartCounts, _LockedStockCounts;
        std::set<std::string> _PlanIDs, _InstanceIDs;
        VariantArray _LockedPlans;
        for (const auto& _Value : LockedValues_)
        {
            if (!_Value.Is<ObjectMap>()) throw std::invalid_argument("锁定的排样结果必须是对象");
            auto _Plan = _Value.To<ObjectMap>();
            const auto _PlanID = GetRequiredText(_Plan, "id", 320);
            const auto _StockID = GetRequiredText(_Plan, "stockTypeId", 160);
            const auto _ProfileKey = GetRequiredText(_Plan, "profileKey", 65536);
            if (!_PlanIDs.insert(_PlanID).second) throw std::invalid_argument("锁定的排样结果编号重复");
            const auto _StockFound = _StocksByID.find(_StockID);
            if (_StockFound == _StocksByID.end() || _StockFound->second->ProfileKey != _ProfileKey)
                throw std::invalid_argument("锁定结果使用的母材已改变，请取消锁定后重新排样");
            const auto& _Stock = *_StockFound->second;
            const auto _StockLength = GetDouble(_Plan, "stockLength", -1.0);
            if (!std::isfinite(_StockLength) || std::abs(_StockLength - _Stock.Length) > _Tolerance)
                throw std::invalid_argument("锁定结果的母材长度已改变，请取消锁定后重新排样");
            const auto _PlacementField = _Plan.find("placements");
            if (_PlacementField == _Plan.end() || !_PlacementField->second.Is<VariantArray>())
                throw std::invalid_argument("锁定结果缺少零件排布");
            auto _Placements = _PlacementField->second.To<VariantArray>();
            if (_Placements.empty()) throw std::invalid_argument("空排样结果不能锁定");
            double _PreviousEnd = 0.0, _PartLength = 0.0;
            std::optional<const SNestingVariant*> _PreviousVariant;
            for (std::size_t _Index = 0; _Index < _Placements.size(); ++_Index)
            {
                if (!_Placements[_Index].Is<ObjectMap>())
                    throw std::invalid_argument("锁定结果中的零件排布必须是对象");
                auto _Placement = _Placements[_Index].To<ObjectMap>();
                const auto _PartID = GetRequiredText(_Placement, "partId", 160);
                const auto _InstanceID = GetRequiredText(_Placement, "instanceId", 320);
                const auto _PartFound = _PartsByID.find(_PartID);
                if (_PartFound == _PartsByID.end() || _PartFound->second->ProfileKey != _ProfileKey)
                    throw std::invalid_argument("锁定结果中的零件已改变，请取消锁定后重新排样");
                if (!_InstanceIDs.insert(_InstanceID).second)
                    throw std::invalid_argument("锁定结果中存在重复的零件实例");
                const auto _Start = GetDouble(_Placement, "start", -1.0);
                const auto _End = GetDouble(_Placement, "end", -1.0);
                const auto _Length = GetDouble(_Placement, "length", _End - _Start);
                const auto _ReversedField = _Placement.find("reversed");
                const auto _NestedField = _Placement.find("nestedWithPrevious");
                if (_ReversedField == _Placement.end() || !_ReversedField->second.Is<bool>()
                    || _NestedField == _Placement.end() || !_NestedField->second.Is<bool>())
                    throw std::invalid_argument("锁定结果中的零件方向或梯形套切标记无效");
                const auto _Reversed = _ReversedField->second.To<bool>();
                const auto _Rotation = GetDouble(_Placement, "rotationRadians", 0.0);
                const auto _PhaseOffset = GetDouble(_Placement, "phaseOffset", 0.0);
                const auto _VariantID = GetRequiredText(_Placement, "variantId", 160);
                if (!std::isfinite(_PhaseOffset) || _PhaseOffset < 0.0
                    || _PhaseOffset > 1000000.0)
                    throw std::invalid_argument("锁定结果中的相位偏移无效");
                const auto _Variant = FindNestingVariant(
                    *_PartFound->second, _VariantID, _Reversed, _Rotation, _PhaseOffset);
                if (!_Variant) throw std::invalid_argument("锁定结果中的零件姿态已改变，请取消锁定后重新排样");
                const auto _BaseGap = _Index ? QuantizedNestingLength(Gap_, true) : 0.0;
                auto _ExpectedGap = _BaseGap;
                bool _DidNest = false;
                if (_Index && _PreviousVariant && *_PreviousVariant && *_Variant)
                {
                    const auto& _PreviousEndDescriptor = (*_PreviousVariant)->RightEnd;
                    const auto& _NextEndDescriptor = (*_Variant)->LeftEnd;
                    const auto& _TailFeature = _PreviousEndDescriptor.Feature;
                    const auto& _HeadFeature = _NextEndDescriptor.Feature;
                    if (iCAX::TubeNesting::IsValidCutLineFeature(_TailFeature)
                        && iCAX::TubeNesting::IsValidCutLineFeature(_HeadFeature)
                        && _TailFeature.Period == _HeadFeature.Period)
                    {
                        const auto _RelativePhase = static_cast<iCAX::TubeNesting::Length>(
                            std::llround(((*_Variant)->PhaseOffset
                                - (*_PreviousVariant)->PhaseOffset) * 100.0));
                        const auto _Match = iCAX::TubeNesting::EvaluateCutLineMatch(
                            _TailFeature, _HeadFeature, _RelativePhase);
                        if (_Match.Valid)
                        {
                            _ExpectedGap += static_cast<double>(_Match.RequiredSeparation) / 100.0;
                            _DidNest = _ExpectedGap < _BaseGap - _Tolerance;
                        }
                    }
                    else if (_PreviousEndDescriptor.AllowTrapezoidNesting
                        && _NextEndDescriptor.AllowTrapezoidNesting
                        && _PreviousEndDescriptor.NestingPlane == _NextEndDescriptor.NestingPlane)
                    {
                        const auto _Overlap = std::min(
                            QuantizedNestingLength(_PreviousEndDescriptor.Projection, false),
                            QuantizedNestingLength(_NextEndDescriptor.Projection, false));
                        if (_Overlap > 0.0)
                        {
                            _ExpectedGap -= _Overlap;
                            _DidNest = true;
                        }
                    }
                }
                const auto _GapBefore = GetDouble(_Placement, "gapBefore", std::numeric_limits<double>::quiet_NaN());
                const auto _Nested = _NestedField->second.To<bool>();
                if (!std::isfinite(_Start) || !std::isfinite(_End) || !std::isfinite(_Length)
                    || !std::isfinite(_Rotation) || !std::isfinite(_PhaseOffset)
                    || _PhaseOffset < 0.0 || !std::isfinite(_GapBefore)
                    || _Start < 0.0 || _End <= _Start
                    || _End - _Start < _PartFound->second->Length - _Tolerance
                    || std::abs(_Length - (_End - _Start)) > _Tolerance
                    || std::abs(_GapBefore - _ExpectedGap) > _Tolerance
                    || std::abs(_Start - (_PreviousEnd + _ExpectedGap)) > _Tolerance
                    || _Nested != _DidNest
                    || _End > _StockLength + _Tolerance)
                    throw std::invalid_argument("锁定结果中的零件位置、长度或间距无效");
                ++_LockedPartCounts[_PartID];
                _PartLength += *_Variant
                    ? std::min(
                        QuantizedNestingLength(_PartFound->second->Length, true),
                        QuantizedNestingLength((*_Variant)->MaterialLength, true))
                    : QuantizedNestingLength(_PartFound->second->Length, true);
                _PreviousEnd = _End;
                _PreviousVariant = *_Variant;
                _Placement["length"] = _End - _Start;
                _Placement["gapBefore"] = _ExpectedGap;
                _Placement["nestedWithPrevious"] = _DidNest;
                _Placement["variantId"] = _VariantID;
                _Placement["rotationRadians"] = _Rotation;
                _Placement["phaseOffset"] = _PhaseOffset;
                _Placement["trsf"] = MakeNestingPlacementTransform(
                    _PartFound->second->LocalCenter, _Start, _End, _Reversed, _Rotation);
                _Placements[_Index] = std::move(_Placement);
            }
            const auto _UsedLength = GetDouble(_Plan, "usedLength", -1.0);
            const auto _RemainingLength = GetDouble(_Plan, "remainingLength", -1.0);
            if (!std::isfinite(_UsedLength) || !std::isfinite(_RemainingLength)
                || std::abs(_UsedLength - _PreviousEnd) > _Tolerance
                || std::abs(_StockLength - _UsedLength - _RemainingLength) > _Tolerance)
                throw std::invalid_argument("锁定结果的已用长度或余料无效");
            ++_LockedStockCounts[_StockID];
            _Plan["placements"] = std::move(_Placements);
            _Plan["stockLength"] = _StockLength;
            _Plan["usedLength"] = _UsedLength;
            _Plan["remainingLength"] = _RemainingLength;
            _Plan["partLength"] = _PartLength;
            _Plan["partCount"] = static_cast<unsigned long long>(
                _Plan.at("placements").To<VariantArray>().size());
            _Plan["utilization"] = _StockLength > 0.0 ? _PartLength / _StockLength : 0.0;
            _LockedPlans.emplace_back(std::move(_Plan));
        }
        for (const auto& _Part : Parts_)
            if (_LockedPartCounts[_Part.ID] > _Part.Quantity)
                throw std::invalid_argument("锁定结果使用的零件数量超过当前清单");
        for (const auto& _Stock : Stocks_)
            if (_Stock.Quantity >= 0 && _LockedStockCounts[_Stock.ID] > static_cast<std::size_t>(_Stock.Quantity))
                throw std::invalid_argument("锁定结果使用的母材数量超过当前库存");

        auto _RemainingParts = Parts_;
        for (auto& _Part : _RemainingParts) _Part.Quantity -= _LockedPartCounts[_Part.ID];
        std::erase_if(_RemainingParts, [](const auto& _Part) { return _Part.Quantity == 0; });
        auto _RemainingStocks = Stocks_;
        for (auto& _Stock : _RemainingStocks)
            if (_Stock.Quantity >= 0) _Stock.Quantity -= static_cast<std::int64_t>(_LockedStockCounts[_Stock.ID]);

        ObjectMap _Solved = _RemainingParts.empty()
            ? ObjectMap{
                { "status", std::string("feasible") }, { "plans", VariantArray{} },
                { "unplaced", VariantArray{} }, { "diagnostics", VariantArray{} },
                { "metrics", ObjectMap{} } }
            : SolveManufacturingNesting(_RemainingParts, _RemainingStocks, Gap_);
        auto _FreshPlans = _Solved.at("plans").To<VariantArray>();
        std::map<std::string, std::size_t> _FreshPlanOrdinals, _FreshPartOrdinals;
        for (auto& _Value : _FreshPlans)
        {
            auto _Plan = _Value.To<ObjectMap>();
            auto _PlanID = GetRequiredText(_Plan, "id", 320);
            if (_PlanIDs.contains(_PlanID))
            {
                const auto _StockID = GetRequiredText(_Plan, "stockTypeId", 160);
                do { _PlanID = _StockID + "#rerun-" + std::to_string(++_FreshPlanOrdinals[_StockID]); }
                while (_PlanIDs.contains(_PlanID));
                _Plan["id"] = _PlanID;
            }
            _PlanIDs.insert(_PlanID);
            auto _Placements = _Plan.at("placements").To<VariantArray>();
            for (auto& _PlacementValue : _Placements)
            {
                auto _Placement = _PlacementValue.To<ObjectMap>();
                auto _InstanceID = GetRequiredText(_Placement, "instanceId", 320);
                if (_InstanceIDs.contains(_InstanceID))
                {
                    const auto _PartID = GetRequiredText(_Placement, "partId", 160);
                    do { _InstanceID = _PartID + "#rerun-" + std::to_string(++_FreshPartOrdinals[_PartID]); }
                    while (_InstanceIDs.contains(_InstanceID));
                    _Placement["instanceId"] = _InstanceID;
                }
                _InstanceIDs.insert(_InstanceID);
                _PlacementValue = std::move(_Placement);
            }
            _Plan["placements"] = std::move(_Placements);
            _Value = std::move(_Plan);
        }
        VariantArray _Plans = std::move(_LockedPlans);
        _Plans.insert(_Plans.end(), _FreshPlans.begin(), _FreshPlans.end());
        auto _Unplaced = _Solved.at("unplaced").To<VariantArray>();
        auto _Diagnostics = _Solved.at("diagnostics").To<VariantArray>();
        _Diagnostics.insert(_Diagnostics.begin(), std::string("已固定锁定的排样结果，仅对其余零件和母材重新求解。"));
        std::size_t _PlacedCount = 0;
        double _TotalStock = 0.0, _PartLength = 0.0, _UsedLength = 0.0;
        for (const auto& _Value : _Plans)
        {
            const auto _Plan = _Value.To<ObjectMap>();
            _PlacedCount += _Plan.at("placements").To<VariantArray>().size();
            _TotalStock += GetDouble(_Plan, "stockLength", 0.0);
            _PartLength += GetDouble(_Plan, "partLength", 0.0);
            _UsedLength += GetDouble(_Plan, "usedLength", 0.0);
        }
        std::size_t _UnplacedCount = 0, _RequestedCount = 0;
        for (const auto& _Part : Parts_) _RequestedCount += _Part.Quantity;
        for (const auto& _Value : _Unplaced)
            _UnplacedCount += static_cast<std::size_t>(GetUInt64(_Value.To<ObjectMap>(), "quantity", 0));
        const auto _SolvedMetrics = _Solved.at("metrics").To<ObjectMap>();
        const auto _Status = !_Unplaced.empty() ? (_Plans.empty() ? "infeasible" : "partial") : "feasible";
        return {
            { "status", std::string(_Status) }, { "plans", std::move(_Plans) },
            { "unplaced", std::move(_Unplaced) }, { "diagnostics", std::move(_Diagnostics) },
            { "metrics", ObjectMap{
                { "requestedPartCount", static_cast<unsigned long long>(_RequestedCount) },
                { "placedPartCount", static_cast<unsigned long long>(_PlacedCount) },
                { "unplacedPartCount", static_cast<unsigned long long>(_UnplacedCount) },
                { "stockCount", static_cast<unsigned long long>(_PlanIDs.size()) },
                { "lockedStockCount", static_cast<unsigned long long>(LockedValues_.size()) },
                { "totalStockLength", _TotalStock }, { "partLength", _PartLength },
                { "usedLength", _UsedLength }, { "remainingLength", _TotalStock - _UsedLength },
                { "gapLength", _UsedLength - _PartLength },
                { "utilization", _TotalStock > 0.0 ? _PartLength / _TotalStock : 0.0 },
                { "searchNodes", GetUInt64(_SolvedMetrics, "searchNodes", 0) }, { "partGap", Gap_ }
            } }
        };
    }

    iCAX::Interaction::CInvocationResult SolveNestingRequest(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_, bool PersistTask_)
    {
        if (!Scene_) throw std::invalid_argument("TubeDesigner.Nest requires a scene");
        tube::license::Enforce<401, tube::license::Feature::Production>();
        RestoreNestingResources(*Scene_);
        RestoreLinkedNestingParts(*Scene_, ApplicationContext_);
        if (Request_.Payload.size() > 8 * 1024 * 1024)
            throw std::invalid_argument("排样请求过大，请分批排样");
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _PartField = _Payload.find("parts");
        const auto _StockField = _Payload.find("stocks");
        if (_PartField == _Payload.end() || !_PartField->second.Is<VariantArray>()
            || _StockField == _Payload.end() || !_StockField->second.Is<VariantArray>())
            throw std::invalid_argument("排样必须提供零件列表和母材列表");
        const auto _PartItems = _PartField->second.To<VariantArray>();
        const auto _StockItems = _StockField->second.To<VariantArray>();
        VariantArray _LockedPlans;
        if (const auto _LockedField = _Payload.find("lockedPlans"); _LockedField != _Payload.end())
        {
            if (!_LockedField->second.Is<VariantArray>())
                throw std::invalid_argument("锁定排样结果必须是列表");
            _LockedPlans = _LockedField->second.To<VariantArray>();
            if (_LockedPlans.size() > 2000)
                throw std::invalid_argument("锁定排样结果不能超过 2000 根母材");
        }
        if (_PartItems.empty() || _PartItems.size() > 2000 || _StockItems.size() > 1000)
            throw std::invalid_argument("单次排样需为 1 至 2000 种零件、至多 1000 行母材");
        const auto _Parameters = GetRequiredObject(_Payload, "parameters");
        const auto _Gap = GetDouble(_Parameters, "partGap", 0.0);
        if (!std::isfinite(_Gap) || _Gap < 0.0 || _Gap > 1000000.0)
            throw std::invalid_argument("零件间距必须为 0 至 1000000 mm 的有限数值");
        std::vector<SNestingPart> _Parts;
        std::vector<SNestingExportPart> _BackgroundParts;
        std::vector<SNestingStock> _Stocks;
        std::set<std::string> _PartIDs, _StockIDs;
        std::size_t _TotalParts = 0;
        auto& _Repository = Scene_->Database();
        const auto _PreparedProducts = CopyTransientDisassembly(*Scene_);
        const auto _LinkedPartIDs = LinkedNestingPartIDs(*Scene_);
        for (const auto& _Item : _PartItems)
        {
            if (!_Item.Is<ObjectMap>()) throw std::invalid_argument("排样零件项必须是对象");
            const auto _PartData = _Item.To<ObjectMap>();
            const auto _PartID = ParseRequiredUuid(GetRequiredText(_PartData, "partEntityId"), "partEntityId");
            const auto _ID = UuidToString(_PartID);
            if (!_PartIDs.insert(_ID).second) throw std::invalid_argument("排样零件重复");
            const auto _Part = GetComponent<CManufacturingPartComponent>(_Repository.GetEntity(_PartID));
            const auto _Transient = FindTransientPartSource(_PreparedProducts, _PartID);
            ObjectMap _Profile;
            std::string _Name, _PartNumber, _ResourceID;
            std::uint64_t _ResourceVersion = 0;
            std::uint64_t _InstanceQuantity = 1;
            std::uint64_t _ProductionQuantity = 0;
            double _NominalLength = 0.0;
            SNestingGeometrySummary _Geometry;
            if (_Part && IsIndependentNestingPart(*_Part))
            {
                if (!IsTubeManufacturingPart(_Part->GetItemProperties()))
                    throw std::invalid_argument("板件或配件不参与管材排样，请在零件模块单独导出");
                const auto _Presentation = ResolvePartPresentation(_Repository, *_Part);
                _Profile = _Presentation.Profile;
                _Name = _Presentation.Name;
                _PartNumber = _Part->GetPartNumber();
                _NominalLength = _Part->GetLength();
                _ProductionQuantity = EffectiveManufacturingQuantity(*_Part, 1);
                _ResourceID = _Part->GetManufacturingGeometryResourceID();
                _ResourceVersion = _Part->GetManufacturingGeometryResourceVersion();
                _Geometry = NestingGeometrySummary(*Scene_, *_Part);
            }
            else
            {
                if (!_Transient || !_LinkedPartIDs.contains(_ID))
                    throw std::invalid_argument("请先将零件加入下料区，再执行排样");
                const auto _Product = GetComponent<CProductInstanceComponent>(
                    _Repository.GetEntity(_Transient->ProductID));
                if (!_Product
                    || _Product->GetActiveGenerationRunID() != _Transient->GenerationRunID)
                    throw std::invalid_argument("零件已过期，请回到产品重新导入下料");
                if (!IsTubeManufacturingPart(_Transient->Part.ItemProperties))
                    throw std::invalid_argument("板件或配件不参与管材排样，请在零件模块单独导出");
                _Profile = _Transient->Part.TubeProfile;
                _Name = MakePartNameFromFileName(
                    _Transient->Part.FileName, _Transient->Part.PartNumber);
                _PartNumber = _Transient->Part.PartNumber;
                _NominalLength = _Transient->Part.Length;
                _InstanceQuantity = _Product->GetQuantity();
                _ProductionQuantity = ProductionQuantity(
                    _Transient->Part.Quantity, _InstanceQuantity);
                _ResourceID = _Transient->Part.ManufacturingResource.URL;
                _ResourceVersion = _Transient->Part.ManufacturingResource.nVersion;
                _Geometry = NestingGeometrySummary(*Scene_, _Transient->Part);
            }
            if (GetUInt64(_PartData, "instanceQuantity", 1) != _InstanceQuantity)
                throw std::invalid_argument("实例数量已变化，请按当前数量重新排样");
            const auto _Quantity = GetUInt64(_PartData, "quantity", _ProductionQuantity);
            if (_Quantity == 0 || _Quantity > _ProductionQuantity
                || _Quantity > kMaximumNestingPartInstances - _TotalParts)
                throw std::invalid_argument("排样数量超出零件清单数量或单次 1000000 件限制");
            _TotalParts += static_cast<std::size_t>(_Quantity);
            if (!std::isfinite(_NominalLength) || _NominalLength <= 0.0)
                throw std::invalid_argument("零件清单中的长度无效，请重新拆单");
            const auto _ProfileKey = GetRequiredText(_PartData, "profileKey", 65536);
            if (!MatchesNestingProfileKey(_ProfileKey, _Profile, _ID))
                throw std::invalid_argument("零件截面数据已改变或分组不匹配，请刷新零件清单后重试");
            _Parts.push_back({
                _ID,
                _ProfileKey,
                _Geometry.EnvelopeLength,
                static_cast<std::size_t>(_Quantity),
                BuildNestingVariants(
                    _Profile, _Geometry.LinearGeometry, _Geometry.EnvelopeLength,
                    _Geometry.LeftCutLineFeature, _Geometry.RightCutLineFeature),
                _Geometry.LocalCenter
            });
            if (PersistTask_)
                _BackgroundParts.push_back({
                    _ID, _Name, _PartNumber, {}, _ResourceID, _ResourceVersion,
                    Scene_->Resources().Get<iCAX::GeometryData::BRepModel>(_ResourceID, _ResourceVersion)
                });
        }
        for (const auto& _Item : _StockItems)
        {
            if (!_Item.Is<ObjectMap>()) throw std::invalid_argument("母材项必须是对象");
            const auto _Data = _Item.To<ObjectMap>();
            const auto _ID = GetRequiredText(_Data, "id");
            if (!_StockIDs.insert(_ID).second) throw std::invalid_argument("母材编号重复");
            const auto _Quantity = GetDouble(_Data, "quantity", 0);
            if (!std::isfinite(_Quantity) || std::floor(_Quantity) != _Quantity
                || _Quantity < -1.0 || _Quantity > 1000000.0)
                throw std::invalid_argument("母材数量必须为 -1（不限量）、0（停用）或正整数");
            if (_Quantity == 0) continue;
            _Stocks.push_back({ _ID, GetRequiredText(_Data, "profileKey", 65536),
                GetDouble(_Data, "length", 0.0), static_cast<std::int64_t>(_Quantity) });
        }
        const auto _Result = SolveManufacturingNestingWithLocks(_Parts, _Stocks, _Gap, _LockedPlans);
        if (PersistTask_)
        {
            SaveNestingTask(*Scene_, _PartItems, _Payload, _Result);
            std::vector<SNestingExportPlan> _BackgroundPlans;
            for (const auto& _PlanValue : _Result.at("plans").To<VariantArray>())
            {
                const auto _Plan = _PlanValue.To<ObjectMap>();
                SNestingExportPlan _ExportPlan;
                _ExportPlan.ID = GetRequiredText(_Plan, "id");
                _ExportPlan.ProfileKey = GetRequiredText(_Plan, "profileKey", 65536);
                _ExportPlan.StockLength = GetDouble(_Plan, "stockLength", 0);
                _ExportPlan.UsedLength = GetDouble(_Plan, "usedLength", 0);
                _ExportPlan.RemainingLength = GetDouble(_Plan, "remainingLength", 0);
                _ExportPlan.PartLength = GetDouble(_Plan, "partLength", 0);
                for (const auto& _PlacementValue : _Plan.at("placements").To<VariantArray>())
                {
                    const auto _Placement = _PlacementValue.To<ObjectMap>();
                    SNestingExportPlacement _ExportPlacement{
                        GetString(_Placement, "partId"), GetDouble(_Placement, "start", 0),
                        GetDouble(_Placement, "end", 0), _Placement.at("reversed").To<bool>(),
                        GetDouble(_Placement, "gapBefore", 0),
                        _Placement.at("nestedWithPrevious").To<bool>(),
                        GetDouble(_Placement, "rotationRadians", 0),
                        GetString(_Placement, "variantId", "default")
                    };
                    if (const auto _Transform = _Placement.find("trsf"); _Transform != _Placement.end()
                        && _Transform->second.Is<VariantArray>())
                    {
                        const auto _Values = _Transform->second.To<VariantArray>();
                        if (_Values.size() != _ExportPlacement.Transform.size())
                            throw std::invalid_argument("排样结果中的 TRSF 维数无效");
                        for (std::size_t _Index = 0; _Index < _Values.size(); ++_Index)
                            _ExportPlacement.Transform[_Index] = ToDouble(_Values[_Index], "trsf");
                        _ExportPlacement.HasTransform = true;
                    }
                    _ExportPlan.Placements.push_back(std::move(_ExportPlacement));
                }
                _BackgroundPlans.push_back(std::move(_ExportPlan));
            }
            QueueNestingResultResources(
                UuidToString(Scene_->GetSceneID()), _BackgroundPlans, _BackgroundParts, _Gap);
        }
        return MakeResponse(Variant(_Result));
    }

    iCAX::Interaction::CInvocationResult HandleNest(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& Application_,
        iCAX::Product::IProductContext* Product_,
        iCAX::Project::IProjectContext* Project_,
        iCAX::Project::ISceneContext* Scene_)
    {
        return SolveNestingRequest(Request_, Application_, Product_, Project_, Scene_, true);
    }

    void ValidateExportPlanSnapshot(const ObjectMap& Submitted_, const ObjectMap& Verified_)
    {
        const auto _Changed = [] { throw std::invalid_argument("排样结果或零件数据已改变，请重新排样后再导出"); };
        for (const auto* _Field : { "id", "stockTypeId", "profileKey" })
            if (GetString(Submitted_, _Field) != GetString(Verified_, _Field)) _Changed();
        for (const auto* _Field : { "stockLength", "usedLength", "remainingLength", "partLength" })
        {
            const auto _Value = GetDouble(Submitted_, _Field, -1.0);
            if (!std::isfinite(_Value) || std::abs(_Value - GetDouble(Verified_, _Field, -2.0)) > 1e-6) _Changed();
        }
        const auto _Submitted = Submitted_.find("placements");
        if (_Submitted == Submitted_.end() || !_Submitted->second.Is<VariantArray>()) _Changed();
        const auto _Left = _Submitted->second.To<VariantArray>();
        const auto _Right = Verified_.at("placements").To<VariantArray>();
        if (_Left.size() != _Right.size()) _Changed();
        for (std::size_t _Index = 0; _Index < _Left.size(); ++_Index)
        {
            if (!_Left[_Index].Is<ObjectMap>()) _Changed();
            const auto _L = _Left[_Index].To<ObjectMap>();
            const auto _R = _Right[_Index].To<ObjectMap>();
            for (const auto* _Field : { "partId", "instanceId", "variantId" })
                if (GetString(_L, _Field) != GetString(_R, _Field)) _Changed();
            for (const auto* _Field : {
                "start", "end", "length", "gapBefore", "rotationRadians" })
            {
                const auto _Value = GetDouble(_L, _Field, -1.0);
                if (!std::isfinite(_Value) || std::abs(_Value - GetDouble(_R, _Field, -2.0)) > 1e-6) _Changed();
            }
            // phaseOffset was added after the original export contract.  Old
            // submitted plans omit it and are equivalent to phase zero.
            const auto _SubmittedPhase = GetDouble(_L, "phaseOffset", 0.0);
            if (!std::isfinite(_SubmittedPhase)
                || std::abs(_SubmittedPhase - GetDouble(_R, "phaseOffset", 0.0)) > 1e-6)
                _Changed();
            const auto _Reversed = _L.find("reversed");
            if (_Reversed == _L.end() || !_Reversed->second.Is<bool>()
                || _Reversed->second.To<bool>() != _R.at("reversed").To<bool>()) _Changed();
            const auto _Nested = _L.find("nestedWithPrevious");
            if (_Nested == _L.end() || !_Nested->second.Is<bool>()
                || _Nested->second.To<bool>() != _R.at("nestedWithPrevious").To<bool>()) _Changed();
        }
    }

    iCAX::Interaction::CInvocationResult HandleExportNesting(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& Application_,
        iCAX::Product::IProductContext* Product_,
        iCAX::Project::IProjectContext* Project_,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_) throw std::invalid_argument("TubeDesigner.ExportNesting requires a scene");
        tube::license::Enforce<402, tube::license::Feature::Production>();
        if (Request_.Payload.size() > 16 * 1024 * 1024)
            throw std::invalid_argument("导出请求过大，请分批导出");
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _Directory = GetRequiredText(_Payload, "targetDirectory", 32768);
        const auto _PlansField = _Payload.find("plans");
        if (_PlansField == _Payload.end() || !_PlansField->second.Is<VariantArray>())
            throw std::invalid_argument("请选择需要导出的排样结果");
        const auto _SelectedPlans = _PlansField->second.To<VariantArray>();
        if (_SelectedPlans.empty() || _SelectedPlans.size() > 2000)
            throw std::invalid_argument("请选择 1 至 2000 根母材的排样结果");

        // Revalidate authoritative entities and recompute the original request before
        // exporting. A modified browser result cannot move unrelated/stale geometry.
        const auto _Original = GetRequiredObject(_Payload, "request");
        auto _SolveRequest = Request_;
        const auto _OriginalText = iCAX::Data::VariantSerializer::Serialize(Variant(_Original));
        _SolveRequest.Payload.assign(_OriginalText.begin(), _OriginalText.end());
        const auto _Solved = SolveNestingRequest(_SolveRequest, Application_, Product_, Project_, Scene_, false);
        const auto _Verified = iCAX::Data::VariantSerializer::Deserialize(
            std::string(_Solved.Payload.begin(), _Solved.Payload.end())).To<ObjectMap>();
        std::map<std::string, std::pair<ObjectMap, std::size_t>> _VerifiedPlans;
        std::size_t _Ordinal = 0;
        for (const auto& _Value : _Verified.at("plans").To<VariantArray>())
        {
            const auto _Plan = _Value.To<ObjectMap>();
            _VerifiedPlans.emplace(GetString(_Plan, "id"), std::make_pair(_Plan, ++_Ordinal));
        }
        std::vector<SNestingExportPlan> _Plans;
        std::vector<SNestingExportPart> _Parts;
        std::set<std::string> _PlanIDs, _PartIDs;
        auto& _Repository = Scene_->Database();
        const auto _PreparedProducts = CopyTransientDisassembly(*Scene_);
        const auto _LinkedPartIDs = LinkedNestingPartIDs(*Scene_);
        for (const auto& _Value : _SelectedPlans)
        {
            if (!_Value.Is<ObjectMap>()) throw std::invalid_argument("排样导出项必须是对象");
            const auto _Submitted = _Value.To<ObjectMap>();
            const auto _ID = GetRequiredText(_Submitted, "id");
            const auto _Found = _VerifiedPlans.find(_ID);
            if (!_PlanIDs.insert(_ID).second || _Found == _VerifiedPlans.end())
                throw std::invalid_argument("选中的排样结果不存在或重复，请重新排样");
            const auto& _Plan = _Found->second.first;
            ValidateExportPlanSnapshot(_Submitted, _Plan);
            SNestingExportPlan _ExportPlan;
            _ExportPlan.ID = _ID;
            _ExportPlan.ProfileKey = GetRequiredText(_Plan, "profileKey");
            _ExportPlan.Name = "母材 " + std::to_string(_Found->second.second);
            _ExportPlan.StockLength = GetDouble(_Plan, "stockLength", 0);
            _ExportPlan.UsedLength = GetDouble(_Plan, "usedLength", 0);
            _ExportPlan.RemainingLength = GetDouble(_Plan, "remainingLength", 0);
            _ExportPlan.PartLength = GetDouble(_Plan, "partLength", 0);
            for (const auto& _PlacementValue : _Plan.at("placements").To<VariantArray>())
            {
                const auto _Placement = _PlacementValue.To<ObjectMap>();
                const auto _PartID = GetString(_Placement, "partId");
                SNestingExportPlacement _ExportPlacement{
                    _PartID,
                    GetDouble(_Placement, "start", 0),
                    GetDouble(_Placement, "end", 0),
                    _Placement.at("reversed").To<bool>(),
                    GetDouble(_Placement, "gapBefore", 0),
                    _Placement.at("nestedWithPrevious").To<bool>(),
                    GetDouble(_Placement, "rotationRadians", 0),
                    GetString(_Placement, "variantId", "default")
                };
                if (const auto _Transform = _Placement.find("trsf"); _Transform != _Placement.end()
                    && _Transform->second.Is<VariantArray>())
                {
                    const auto _Values = _Transform->second.To<VariantArray>();
                    if (_Values.size() != _ExportPlacement.Transform.size())
                        throw std::invalid_argument("排样结果中的 TRSF 维数无效");
                    for (std::size_t _Index = 0; _Index < _Values.size(); ++_Index)
                        _ExportPlacement.Transform[_Index] = ToDouble(_Values[_Index], "trsf");
                    _ExportPlacement.HasTransform = true;
                }
                _ExportPlan.Placements.push_back(std::move(_ExportPlacement));
                const auto _ParsedPartID = ParseRequiredUuid(_PartID, "partId");
                const auto _Part = GetComponent<CManufacturingPartComponent>(
                    _Repository.GetEntity(_ParsedPartID));
                const auto _Transient = FindTransientPartSource(
                    _PreparedProducts, _ParsedPartID);
                ObjectMap _Profile;
                if (_Part && IsIndependentNestingPart(*_Part))
                {
                    const auto _Presentation = ResolvePartPresentation(_Repository, *_Part);
                    _Profile = _Presentation.Profile;
                    if (_PartIDs.insert(_PartID).second)
                        _Parts.push_back({ _PartID, _Presentation.Name, _Part->GetPartNumber(),
                            NestingManufacturingShape(*Scene_, *_Part),
                            _Part->GetManufacturingGeometryResourceID(),
                            _Part->GetManufacturingGeometryResourceVersion() });
                }
                else if (_Transient && _LinkedPartIDs.contains(_PartID))
                {
                    _Profile = _Transient->Part.TubeProfile;
                    if (_PartIDs.insert(_PartID).second)
                        _Parts.push_back({ _PartID,
                            MakePartNameFromFileName(
                                _Transient->Part.FileName, _Transient->Part.PartNumber),
                            _Transient->Part.PartNumber,
                            NestingManufacturingShape(*Scene_, _Transient->Part),
                            _Transient->Part.ManufacturingResource.URL,
                            _Transient->Part.ManufacturingResource.nVersion });
                }
                else throw std::invalid_argument("导出的制造零件不存在或已失效");
                if (_ExportPlan.Profile.empty())
                {
                    _ExportPlan.Profile = GetString(_Profile, "displayName") + " " + GetString(_Profile, "specification");
                }
            }
            _Plans.push_back(std::move(_ExportPlan));
        }
        const auto _Gap = GetDouble(GetRequiredObject(_Original, "parameters"), "partGap", 0);
        return MakeResponse(Variant(ExportNestingResults(
            Utf8Path(_Directory), _Plans, _Parts, _Gap, UuidToString(Scene_->GetSceneID()))));
    }

    TopoDS_Shape MachiningSourceShape(iCAX::Project::ISceneContext& Scene_, const ObjectMap& Job_, const ObjectMap& Nesting_)
    {
        if (GetString(Job_, "sourceKind") == "cad")
        {
            const auto _Model = Scene_.Resources().Get<iCAX::GeometryData::BRepModel>(
                GetRequiredText(Job_, "resourceId"), GetUInt64(Job_, "resourceVersion", 0));
            if (!_Model) throw std::invalid_argument("原始 CAD 资源不可用");
            const auto _Built = iCAX::OpenCascade::BuildOpenCascadeShape(*_Model);
            if (!_Built.bOK || _Built.Shape.IsNull()) throw std::invalid_argument("无法还原原始 CAD 实体");
            return _Built.Shape;
        }
        if (GetString(Job_, "sourceRevision") != GetString(Nesting_, "revision"))
            throw std::invalid_argument("原排样已变化，请重新接收排样结果；独立刀路仍然保留");
        const auto _Result = GetRequiredObject(Nesting_, "result");
        ObjectMap _Plan;
        for (const auto& _Value : _Result.at("plans").To<VariantArray>())
            if (GetString(_Value.To<ObjectMap>(), "id") == GetString(Job_, "planId")) _Plan = _Value.To<ObjectMap>();
        if (_Plan.empty()) throw std::invalid_argument("原排样结果不存在");
        SNestingExportPlan _Export;
        _Export.ID = GetString(_Plan, "id");
        _Export.ProfileKey = GetString(_Plan, "profileKey");
        _Export.StockLength = GetDouble(_Plan, "stockLength", 0);
        _Export.UsedLength = GetDouble(_Plan, "usedLength", 0);
        _Export.RemainingLength = GetDouble(_Plan, "remainingLength", 0);
        _Export.PartLength = GetDouble(_Plan, "partLength", 0);
        std::vector<SNestingExportPart> _Parts;
        std::set<std::string> _PartIDs;
        const auto _Prepared = CopyTransientDisassembly(Scene_);
        const auto _Linked = LinkedNestingPartIDs(Scene_);
        for (const auto& _Value : _Plan.at("placements").To<VariantArray>())
        {
            const auto _Placement = _Value.To<ObjectMap>();
            const auto _ID = GetRequiredText(_Placement, "partId");
            SNestingExportPlacement _Placed{ _ID, GetDouble(_Placement, "start", 0), GetDouble(_Placement, "end", 0),
                _Placement.at("reversed").To<bool>(), GetDouble(_Placement, "gapBefore", 0),
                _Placement.at("nestedWithPrevious").To<bool>(), GetDouble(_Placement, "rotationRadians", 0), GetString(_Placement, "variantId", "default") };
            if (_Placement.contains("trsf") && _Placement.at("trsf").Is<VariantArray>())
            {
                const auto _Values = _Placement.at("trsf").To<VariantArray>();
                if (_Values.size() != 16) throw std::invalid_argument("排样坐标无效");
                for (std::size_t _Index = 0; _Index < 16; ++_Index) _Placed.Transform[_Index] = ToDouble(_Values[_Index], "trsf");
                _Placed.HasTransform = true;
            }
            _Export.Placements.push_back(std::move(_Placed));
            if (!_PartIDs.insert(_ID).second) continue;
            const auto _UUID = ParseRequiredUuid(_ID, "partId");
            const auto _Part = GetComponent<CManufacturingPartComponent>(Scene_.Database().GetEntity(_UUID));
            const auto _Transient = FindTransientPartSource(_Prepared, _UUID);
            if (_Part && IsIndependentNestingPart(*_Part))
                _Parts.push_back({ _ID, _ID, _ID, NestingManufacturingShape(Scene_, *_Part) });
            else if (_Transient && _Linked.contains(_ID))
                _Parts.push_back({ _ID, _ID, _ID, NestingManufacturingShape(Scene_, _Transient->Part) });
            else throw std::invalid_argument("排样零件几何已失效");
        }
        const auto _Request = GetRequiredObject(Nesting_, "request");
        const auto _Gap = GetDouble(GetRequiredObject(_Request, "parameters"), "partGap", 0);
        // Identical BRep/placements to STEP export, without a disk round trip.
        return BuildNestingExportCompound(_Export, _Parts, _Gap);
    }

    // Preparation only: keep machining data separate from nesting and never
    // accept client-authored geometry/placements as authoritative nesting output.
    iCAX::Interaction::CInvocationResult HandleMachiningData(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext*, iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_ || Request_.Payload.size() > 64 * 1024 * 1024)
            throw std::invalid_argument("加工数据请求无效");
        tube::license::Enforce<403, tube::license::Feature::Production>();
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _Action = GetRequiredText(_Payload, "action");
        auto& _DB = Scene_->Database();
        const auto _Meta = _DB.GetMetaEntity();
        if (!_Meta) throw std::runtime_error("Missing project root");
        const auto _Root = GetComponent<CTubeDesignerRootComponent>(_Meta);
        auto _Task = _Root ? _Root->GetMachiningTask() : ObjectMap();
        const auto _Nesting = _Root ? _Root->GetNestingTask() : ObjectMap();
        auto _Jobs = _Task.contains("jobs") && _Task.at("jobs").Is<VariantArray>()
            ? _Task.at("jobs").To<VariantArray>() : VariantArray();
        if (_Action == "preview")
        {
            const auto _ID = GetRequiredText(_Payload, "id");
            for (const auto& _Value : _Jobs)
            {
                const auto _Job = _Value.To<ObjectMap>();
                if (GetString(_Job, "id") != _ID) continue;
                const auto _URL = GetRequiredText(_Job, "resourceId");
                const auto _Mesh = iCAX::RenderInteraction::EnsureFrontendGeometryResource(
                    Scene_->Resources(), _URL, iCAX::Render::ERenderGeometryKind::Mesh);
                return MakeResponse(ObjectMap{ { "geometryResourceId", _Mesh.URL },
                    { "geometryResourceVersion", _Mesh.nVersion } });
            }
            throw std::invalid_argument("加工模型不存在");
        }
        if (GetString(_Payload, "expectedRevision") != GetString(_Task, "revision"))
            throw std::invalid_argument("加工清单已变化，请刷新后重试");
        std::string _SelectedID;
        std::string _NewResource;
        if (_Action == "analyze" || _Action == "save-paths")
        {
            const auto _IDs = _Payload.at("jobIds").To<VariantArray>();
            if (_IDs.empty() || _IDs.size() > 2000) throw std::invalid_argument("请选择 1 至 2000 项加工数据");
            const bool _Confirmed = _Payload.contains("confirmReplace") && _Payload.at("confirmReplace").Is<bool>()
                && _Payload.at("confirmReplace").To<bool>();
            if (_Action == "save-paths" && _IDs.size() != 1) throw std::invalid_argument("一次只能编辑一项加工数据");
            if (_Action == "analyze") RestoreLinkedNestingParts(*Scene_, ApplicationContext_);
            std::set<std::string> _Seen;
            for (const auto& _IDValue : _IDs)
            {
                const auto _ID = _IDValue.To<std::string>();
                if (!_Seen.insert(_ID).second) throw std::invalid_argument("加工记录重复");
                bool _Found = false;
                for (auto& _JobValue : _Jobs)
                {
                    auto _Job = _JobValue.To<ObjectMap>();
                    if (GetString(_Job, "id") != _ID) continue;
                    _Found = true;
                    if (_Action == "analyze")
                    {
                        if (_Job.contains("analysis") && !_Confirmed)
                            throw std::invalid_argument("重新分析会丢弃此前全部刀路编辑，请先确认");
                        const auto _Service = Scene_->Services().Resolve<iCAX::CAM::IBRepToCamPathService>();
                        if (!_Service) throw std::runtime_error("BRep 到 CamPath 解析服务未就绪");
                        iCAX::CAM::CamPathAnalysis _Parsed;
                        if (GetString(_Job, "sourceKind") == "cad")
                        {
                            const auto _BRep = Scene_->Resources().Get<iCAX::GeometryData::BRepModel>(
                                GetRequiredText(_Job, "resourceId"), GetUInt64(_Job, "resourceVersion", 0));
                            if (!_BRep) throw std::invalid_argument("原始 CAD 资源不可用");
                            _Parsed = _Service->Analyze(*_BRep);
                        }
                        else
                        {
                            const auto _Shape = MachiningSourceShape(*Scene_, _Job, _Nesting);
                            const auto _BRep = iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(_Shape, "machining-input", "", 0.025);
                            _Parsed = _Service->Analyze(_BRep);
                        }
                        auto _Analysis = SerializeMachiningToolpaths(_Parsed);
                        _Analysis["revision"] = UuidToString(iCAX::Data::GenerateNewUUID());
                        _Job["analysis"] = std::move(_Analysis);
                    }
                    else
                    {
                        auto _Analysis = _Job.contains("analysis") ? GetRequiredObject(_Job, "analysis") : ObjectMap();
                        if (GetString(_Payload, "analysisRevision") != GetString(_Analysis, "revision"))
                            throw std::invalid_argument("刀路已变化，请重新打开编辑");
                        _Analysis["paths"] = ValidateMachiningToolpaths(_Payload.at("paths").To<VariantArray>());
                        _Analysis["independent"] = true;
                        _Analysis["schemaVersion"] = 1;
                        _Analysis["unit"] = std::string("mm");
                        _Analysis["revision"] = UuidToString(iCAX::Data::GenerateNewUUID());
                        _Job["analysis"] = std::move(_Analysis);
                    }
                    _SelectedID = _ID;
                    _JobValue = std::move(_Job);
                    break;
                }
                if (!_Found) throw std::invalid_argument("加工记录不存在");
            }
        }
        else if (_Action == "link")
        {
            const auto _Selected = _Payload.find("planIds");
            if (_Selected == _Payload.end() || !_Selected->second.Is<VariantArray>())
                throw std::invalid_argument("请选择排样结果");
            const auto _Result = GetRequiredObject(_Nesting, "result");
            const auto _Plans = _Result.find("plans");
            if (_Plans == _Result.end() || !_Plans->second.Is<VariantArray>())
                throw std::invalid_argument("没有可用于加工的排样结果");
            const auto _Revision = GetRequiredText(_Nesting, "revision");
            if (GetString(_Payload, "nestingRevision") != _Revision)
                throw std::invalid_argument("排样结果已变化，请重新选择");
            for (const auto& _IDValue : _Selected->second.To<VariantArray>())
            {
                const auto _PlanID = _IDValue.To<std::string>();
                bool _Found = false;
                for (const auto& _PlanValue : _Plans->second.To<VariantArray>())
                {
                    const auto _Plan = _PlanValue.To<ObjectMap>();
                    if (GetString(_Plan, "id") != _PlanID) continue;
                    _Found = true;
                    for (const auto& _Existing : _Jobs)
                    {
                        const auto _Job = _Existing.To<ObjectMap>();
                        if (GetString(_Job, "sourceKind") == "nesting"
                            && GetString(_Job, "planId") == _PlanID
                            && GetString(_Job, "sourceRevision") == _Revision)
                        { _SelectedID = GetString(_Job, "id"); break; }
                    }
                    if (_SelectedID.empty())
                    {
                        _SelectedID = UuidToString(iCAX::Data::GenerateNewUUID());
                        _Jobs.emplace_back(ObjectMap{ { "id", _SelectedID },
                            { "sourceKind", std::string("nesting") }, { "planId", _PlanID },
                            { "sourceRevision", _Revision } });
                    }
                    break;
                }
                if (!_Found) throw std::invalid_argument("所选排样结果不存在");
                // The next selected bar must be resolved independently.
                _Task["activeJobId"] = _SelectedID;
                _SelectedID.clear();
            }
            if (_Selected->second.To<VariantArray>().empty()) throw std::invalid_argument("请选择排样结果");
            _SelectedID = GetString(_Task, "activeJobId");
        }
        else if (_Action == "import")
        {
            const auto _Path = Utf8Path(GetRequiredText(_Payload, "sourcePath", 32767));
            const auto _Shape = ImportMachiningAssemblyFile(_Path);
            _SelectedID = UuidToString(iCAX::Data::GenerateNewUUID());
            const auto _Name = Utf8PathText(_Path.stem());
            const auto _Resource = StoreBRep(*Scene_, "tube-designer/machining/import/" + _SelectedID, _Name, _Shape);
            _NewResource = _Resource.URL;
            unsigned long long _SolidCount = 0;
            for (TopExp_Explorer _It(_Shape, TopAbs_SOLID); _It.More(); _It.Next()) ++_SolidCount;
            _Jobs.emplace_back(ObjectMap{ { "id", _SelectedID }, { "sourceKind", std::string("cad") },
                { "name", _Name }, { "sourceFileName", Utf8PathText(_Path.filename()) },
                { "resourceId", _Resource.URL }, { "resourceVersion", _Resource.nVersion },
                { "solidCount", _SolidCount }, { "bounds", ShapeBounds(_Shape) },
                { "unit", std::string("mm") }, { "coordinatePolicy", std::string("preserve-assembly") } });
        }
        else if (_Action == "remove")
        {
            const auto _ID = GetRequiredText(_Payload, "id");
            const auto _Before = _Jobs.size();
            std::erase_if(_Jobs, [&](const Variant& _Value) { return GetString(_Value.To<ObjectMap>(), "id") == _ID; });
            if (_Before == _Jobs.size()) throw std::invalid_argument("加工记录不存在");
            _SelectedID = _Jobs.empty() ? std::string() : GetString(_Jobs.front().To<ObjectMap>(), "id");
        }
        else throw std::invalid_argument("未知加工数据操作");
        _Task["schemaVersion"] = 1;
        _Task["jobs"] = std::move(_Jobs);
        _Task["activeJobId"] = _SelectedID;
        _Task["revision"] = UuidToString(iCAX::Data::GenerateNewUUID());
        auto _Undo = _DB.BeginUndoCommand("Update machining data");
        auto& _Transaction = _DB.BeginTransaction("Update machining data");
        bool _Committing = false;
        try
        {
            QueueUpsertComponent(_Transaction, _Meta, _Meta->GetID(), CTubeDesignerRootComponent::S_ClassName,
                {{ CTubeDesignerRootComponent::PropertyName_MachiningTask, PropertyValue(_Task) }});
            std::string _Error;
            _Committing = true;
            if (!_DB.CommitTransaction(_Transaction, _Error)) throw std::runtime_error(_Error);
        }
        catch (...)
        {
            if (!_Committing) { try { _DB.CancelTransaction(_Transaction); } catch (...) {} }
            if (!_NewResource.empty()) { Scene_->Resources().Unload(_NewResource); Scene_->Resources().Delete(_NewResource); }
            throw;
        }
        _Undo->End();
        SyncNestingPersistence(*Scene_);
        return MakeResponse(ObjectMap{ { "machiningTask", _Task }, { "nestingTask", _Nesting }, { "jobId", _SelectedID } });
    }

    class CTubeDesignerSDO final : public iCAX::Interaction::CSDO
    {
    public:
        CTubeDesignerSDO() : CSDO("TubeDesigner")
        {
            ExposeMethod("List", &HandleList);
            ExposeMethod("GetTemplateDescriptor", &HandleGetTemplateDescriptor);
            ExposeMethod("MachiningData", &HandleMachiningData);
            ExposeMethod("ListUserData", &HandleListUserData);
            ExposeMethod("ImportProductTemplatePackage", &HandleImportProductTemplatePackage);
            ExposeMethod("CreateProductTemplate", &HandleCreateProductTemplate);
            ExposeMethod("ExportProductTemplatePackage", &HandleExportProductTemplatePackage);
            ExposeMethod("DeleteProductTemplate", &HandleDeleteProductTemplate);
            ExposeMethod("ListComponentModels", &HandleListComponentModels);
            ExposeMethod("ImportComponentModel", &HandleImportComponentModel);
            ExposeMethod("CreateComponentCSGModel", &HandleCreateComponentCSGModel);
            ExposeMethod("UpdateComponentCSGModel", &HandleUpdateComponentCSGModel);
            ExposeMethod("UpdateComponentModel", &HandleUpdateComponentModel);
            ExposeMethod("DeleteComponentModel", &HandleDeleteComponentModel);
            ExposeMethod("GenerateComponentModelPreview", &HandleGenerateComponentModelPreview);
            ExposeMethod("GenerateComponentCSGPreview", &HandleGenerateComponentCSGPreview);
            ExposeMethod("ExportComponentModel", &HandleExportComponentModel);
            ExposeMethod("SaveCustomer", &HandleSaveCustomer);
            ExposeMethod("DeleteCustomer", &HandleDeleteCustomer);
            ExposeMethod("SaveParameterPreset", &HandleSaveParameterPreset);
            ExposeMethod("DeleteParameterPreset", &HandleDeleteParameterPreset);
            ExposeMethod("ImportProfileDxf", &HandleImportProfileDxf);
            ExposeMethod("ImportProfilePackage", &HandleImportProfilePackage);
            ExposeMethod("EvaluateProfilePackage", &HandleEvaluateProfilePackage);
            ExposeMethod("GenerateProfilePreview", &HandleGenerateProfilePreview);
            ExposeMethod("ExportProfile", &HandleExportProfile);
            ExposeMethod("UpdateProfilePackage", &HandleUpdateProfilePackage);
            ExposeMethod("SaveImportedProfile", &HandleSaveImportedProfile);
            ExposeMethod("RenameImportedProfile", &HandleRenameImportedProfile);
            ExposeMethod("RenameProfile", &HandleRenameImportedProfile);
            ExposeMethod("DeleteImportedProfile", &HandleDeleteImportedProfile);
            ExposeMethod("DeleteProfile", &HandleDeleteProfile);
            ExposeMethod("ActivateProduct", &HandleActivateProduct);
            ExposeMethod("SetInstanceQuantity", &HandleSetInstanceQuantity);
            ExposeMethod("UpdateManufacturingPart", &HandleUpdateManufacturingPart);
            ExposeMethod("DeleteManufacturingPart", &HandleDeleteManufacturingPart);
            ExposeMethod("SaveSketch", &HandleSaveSketch);
            ExposeMethod("SavePartSketch", &HandleSavePartSketch);
            ExposeMethod("GeneratePreview", &HandleGeneratePreview);
            ExposeMethod("GenerateProductTemplatePreview", &HandleGenerateProductTemplatePreview);
            ExposeMethod("Generate", &HandleGeneratePreview);
            ExposeMethod("Disassemble", &HandleDisassemble);
            ExposeMethod("DisassembleSelected", &HandleDisassemble);
            ExposeMethod("ReleaseTransientParts", &HandleReleaseTransientParts);
            ExposeMethod("MeasurePartGeometry", &HandleMeasurePartGeometry);
            ExposeMethod("UnfoldPartBRep", &HandleUnfoldPartBRep);
            ExposeMethod("AddNestingStandardPart", &HandleAddNestingStandardPart);
            ExposeMethod("AddNestingPunchPart", &HandleAddNestingPunchPart);
            ExposeMethod("ApplyPunchWizard", &HandleApplyPunchWizard);
            ExposeMethod("PreviewPunchWizard", &HandlePreviewPunchWizard);
            ExposeMethod("GetPunchTools", &HandleGetPunchTools);
            ExposeMethod("SavePunchTool", &HandleSavePunchTool);
            ExposeMethod("ImportPunchToolPackage", &HandleImportPunchToolPackage);
            ExposeMethod("GetPartDrawingTools", &HandleGetPartDrawingTools);
            ExposeMethod("PreviewPartDrawing", &HandlePreviewPartDrawing);
            ExposeMethod("AddPartDrawing", &HandleAddPartDrawing);
            ExposeMethod("ApplyPartDrawing", &HandleApplyPartDrawing);
            ExposeMethod("Nest", &HandleNest);
            ExposeMethod("StageNestingParts", &HandleStageNestingParts);
            ExposeMethod("ImportNestingPart", &HandleImportNestingPart);
            ExposeMethod("DeleteNestingParts", &HandleDeleteNestingParts);
            ExposeMethod("SaveNestingSettings", &HandleSaveNestingSettings);
            ExposeMethod("ExportNesting", &HandleExportNesting);
            ExposeMethod("ExportSelected", &HandleExportSelected);
            ExposeMethod("ExportAll", &HandleExportSelected);
        }
    };

    static_assert(iCAX::Interaction::IsStatelessSDOType<CTubeDesignerSDO>);
}

ICAX_REGISTER_SDO(CTubeDesignerSDO)
