#include "pch.h"
#include "../../../licensing/include/LicenseRuntime.h"

#include "PartListXlsxExporter.h"
#include "ComponentModelLibrary.h"
#include "FinalGeometryMeasurement.h"
#include "NestingAdapter.h"
#include "NestingResultExporter.h"
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
#include "ApplicationContext/UserDataStore.h"
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

#include <fstream>
#include <mutex>
#include <numeric>
#include <unordered_set>
#include <TopoDS_Shape.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Trsf.hxx>

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

    std::filesystem::path ResolveSystemProfileRoot(
        const iCAX::Application::IApplicationContext& ApplicationContext_)
    {
        return ResolveRuntimeDirectory(ApplicationContext_, {
            "apps/tube-designer/templates/_shared/profiles",
            "src/apps/tube-designer/templates/_shared/profiles"
        }, "TubeDesigner system profile directory");
    }

    std::filesystem::path ResolveComponentModelRoot(
        const iCAX::Application::IApplicationContext& ApplicationContext_)
    {
        return ResolveRuntimeDirectory(ApplicationContext_, {
            "apps/tube-designer/models", "src/apps/tube-designer/models"
        }, "TubeDesigner component model directory");
    }

    std::vector<std::filesystem::path> DiscoverPythonTemplateDirectories(
        const iCAX::Application::IApplicationContext& ApplicationContext_)
    {
        const auto _TemplateRoot = ResolveTemplateRoot(ApplicationContext_);
        std::vector<std::filesystem::path> _Directories;
        std::error_code _Error;
        for (std::filesystem::directory_iterator _Iterator(_TemplateRoot, _Error), _End;
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

    std::string SharedTemplatePackageText(const std::filesystem::path& TemplateRoot_)
    {
        const auto _SharedRoot = TemplateRoot_ / "_shared";
        std::error_code _Error;
        if (!std::filesystem::is_directory(_SharedRoot, _Error)) return {};
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
        return _Content;
    }

    SPythonTemplatePackage LoadPythonTemplatePackageFromDirectory(
        const std::filesystem::path& Directory_,
        const std::filesystem::path& TemplateRoot_)
    {
        const auto _DescriptorPath = Directory_ / "template.json";
        const auto _ScriptPath = Directory_ / "template.py";
        const auto _DescriptorText = ReadTextFile(_DescriptorPath);
        const auto _ScriptText = ReadTextFile(_ScriptPath);
        auto _Descriptor = iCAX::TemplateRuntime::CTemplateCodec::ParseDescriptor(
            iCAX::TemplateRuntime::CStandardJsonCodec::Parse(_DescriptorText));
        _Descriptor.PackageDigest = ContentDigest(
            _DescriptorText, _ScriptText + SharedTemplatePackageText(TemplateRoot_));
        return { std::move(_Descriptor), _DescriptorPath, _ScriptPath };
    }

    SPythonTemplatePackage LoadPythonTemplatePackage(
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        const std::string& TemplateID_)
    {
        const auto _TemplateRoot = ResolveTemplateRoot(ApplicationContext_);
        for (const auto& _Directory : DiscoverPythonTemplateDirectories(ApplicationContext_))
        {
            auto _Package = LoadPythonTemplatePackageFromDirectory(_Directory, _TemplateRoot);
            if (_Package.Descriptor.ID == TemplateID_) return _Package;
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
        const auto _TemplateRoot = ResolveTemplateRoot(ApplicationContext_);
        for (const auto& _Directory : DiscoverPythonTemplateDirectories(ApplicationContext_))
        {
            try
            {
                const auto _Package = LoadPythonTemplatePackageFromDirectory(
                    _Directory, _TemplateRoot);
                _Templates.emplace_back(iCAX::TemplateRuntime::CTemplateCodec::MakePresentationDescriptor(
                    _Package.Descriptor, "zh-CN"));
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

        return _Templates;
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
                // BRep 曲线和曲面仍按原始解析几何保存；该参数只控制随资源
                // 携带的显示三角网格。0.025 对应约 0.25 mm 的显示偏差，避免
                // 圆/样条拉伸体因 0.01 mm 级过密网格阻塞界面数十秒。
                Shape_, DisplayName_, _ResourceID, 0.025));
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
        if (_Name.ends_with(".step")) _Name.resize(_Name.size() - 5);
        else if (_Name.ends_with(".stp")) _Name.resize(_Name.size() - 4);
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

    ObjectMap OptionalProfileProperties(const ObjectMap& Properties_);

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
        BRepBndLib::Add(Shape_, _Box, false);
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
        SResolvedPartPresentation _Result{
            MakePartNameFromFileName(Part_.GetFileName(), Part_.GetPartNumber()),
            OptionalProfileProperties(Part_.GetItemProperties())
        };

        if (const auto _MemberEntity = Repository_.GetEntity(Part_.GetSourceMemberID());
            const auto _Member = GetComponent<CAssemblyMemberComponent>(_MemberEntity))
        {
            if (_Result.Profile.empty())
                _Result.Profile = OptionalProfileProperties(_Member->GetItemProperties());
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
        _Designer["nestingSettings"] = _Root ? _Root->GetNestingSettings() : ObjectMap();

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
            _Item["profile"] = OptionalProfileProperties(_Member->GetItemProperties());
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

    constexpr const char* kCustomerFeatureID = "customer";
    constexpr const char* kCustomerRecordType = "profile";
    constexpr const char* kTemplateFeatureID = "template";
    constexpr const char* kParameterPresetRecordType = "parameter-preset";
    constexpr const char* kProfileFeatureID = "profile";
    constexpr const char* kImportedProfileRecordType = "imported-dxf";
    constexpr const char* kParametricProfileRecordType = "parametric-package";
    constexpr const char* kProductSubjectType = "product";
    constexpr const char* kProfileDefinitionSubjectType = "profile-definition";
    constexpr const char* kTemplateSubjectType = "template-definition";
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
        auto _Models = ListComponentModelSummaries(ResolveComponentModelRoot(ApplicationContext_), _Store.get());
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
            &Store_, _Scope, _ID);
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
        auto _Profile = ResolveStoredProfileSnapshot(
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

    VariantArray ListSystemProfileRecords(
        const iCAX::Application::IApplicationContext& ApplicationContext_)
    {
        VariantArray _Items;
        for (const auto& _Package : LoadSystemProfilePackages(ApplicationContext_))
            _Items.emplace_back(MakeSystemProfilePayload(_Package));
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
        ObjectMap _Response;
        _Response["customers"] = ListUserDataRecords(
            *_Store, kCustomerFeatureID, kCustomerRecordType);
        _Response["parameterPresets"] = ListUserDataRecords(
            *_Store, kTemplateFeatureID, kParameterPresetRecordType, true);
        _Response["profiles"] = ListProfileUserDataRecords(*_Store);
        _Response["systemProfiles"] = ListSystemProfileRecords(ApplicationContext_);
        _Response["templateProfiles"] = ListTemplateProfileRecords(ApplicationContext_);
        _Response["profileId"] = std::string("local-default");
        return MakeResponse(Variant(_Response));
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
        if (_Extension != ".icaxprofile" && _Extension != ".zip")
            throw std::invalid_argument("TubeDesigner requires an .icaxprofile package");
        const auto _Password = GetString(_Request, "password");
        if (_Password.size() > 256)
            throw std::invalid_argument("TubeDesigner profile package password is too long");
        auto _Package = ImportProfilePackage(ApplicationContext_, _SourcePath, _Password);
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
                && _Kind != "circle" && _Kind != "arc" && _Kind != "spline"
                && _Kind != "freehand" && _Kind != "text")
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
        const auto _Member = GetComponent<CAssemblyMemberComponent>(_Repository.GetEntity(_MemberID));
        if (!_Member || _Member->GetProductID() != _ProductID)
            throw std::invalid_argument("TubeDesigner sketch target member does not belong to the product");

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

        auto _Undo = _Repository.BeginUndoCommand("Save TubeDesigner side sketch");
        auto& _Transaction = _Repository.BeginTransaction("Update TubeDesigner side sketch");
        bool _CommitStarted = false;
        try
        {
            _Transaction.ModifyComponent(
                _ProductID, CProductInstanceComponent::S_ClassName, {
                    { CProductInstanceComponent::PropertyName_Sketches, PropertyValue(_Sketches) }
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

    ObjectMap OptionalProfileProperties(const ObjectMap& Properties_)
    {
        const auto _Iterator = Properties_.find("tubeDesigner.profile");
        if (_Iterator == Properties_.end() || !_Iterator->second.Is<ObjectMap>()) return {};
        return _Iterator->second.To<ObjectMap>();
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
        iCAX::Product::IProductContext* ProductContext_,
        iCAX::Project::ISceneContext& Scene_)
    {
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
            const auto _Representation = _Item.Representations.find("result");
            if (_Representation == _Item.Representations.end())
                throw std::runtime_error("neutral model item has no result representation: " + _Item.Key);
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
        return GenerateNeutralPreview(_Payload, ApplicationContext_, ProductContext_, *Scene_);
    }

    struct SPreparedManufacturingPart final
    {
        std::uint64_t Index = 0;
        std::string StableKey;
        std::string PartNumber;
        std::string Role;
        std::uint64_t Quantity = 1;
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
        ObjectMap ManufacturingModel;
    };

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
            const auto _ManufacturingShape = NormalizeManufacturingShape(
                _Geometry.At(_Representation->second), _ItemProperties);
            if (_PartKind == "accessory") _ItemProperties["manufacturing.modelBounds"] = ShapeBounds(_ManufacturingShape);
            const auto _Resource = StoreBRep(
                Scene_, _StablePrefix + "/manufacturing",
                _PartNumber + " manufacturing", _ManufacturingShape);
            const auto _Thumbnail = iCAX::RenderInteraction::EnsureFrontendGeometryResource(
                Scene_.Resources(), _Resource.URL, iCAX::Render::ERenderGeometryKind::Mesh);
            _Prepared.Parts.push_back({
                _Index,
                _Item.Key,
                _PartNumber,
                OptionalPropertyString(_ItemProperties, "group"),
                std::max<std::uint64_t>(1, OptionalPropertyUInt64(_ItemProperties, "quantity", 1)),
                OptionalPropertyNumber(_ItemProperties, "length"),
                _ItemProperties,
                _MemberID,
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

        std::vector<SPreparedProductDisassembly> _PreparedProducts;
        _PreparedProducts.reserve(_ProductIDs.size());
        for (const auto& _ProductID : _ProductIDs)
        {
            _PreparedProducts.push_back(PrepareProductDisassembly(ApplicationContext_, *Scene_, _ProductID));
        }

        auto _Undo = _Repository.BeginUndoCommand("Disassemble TubeDesigner product instances");
        auto& _Transaction = _Repository.BeginTransaction("Create TubeDesigner manufacturing groups");
        bool _CommitStarted = false;
        try
        {
            for (const auto& _PreparedProduct : _PreparedProducts)
            {
                _Transaction.ModifyComponent(
                    _PreparedProduct.GenerationRunID, CGenerationRunComponent::S_ClassName, {
                        { CGenerationRunComponent::PropertyName_ManufacturingModel,
                            PropertyValue(_PreparedProduct.ManufacturingModel) }
                    });
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
        auto _FinalShape = _Rebuilt.Shape;
        auto _RecoveredFromGeneration = false;
        if (!_Rebuilt.bOK || _FinalShape.IsNull())
        {
            try
            {
                _FinalShape = RecoverManufacturingShapeFromGeneration(
                    _Repository, *_Part);
                _RecoveredFromGeneration = !_FinalShape.IsNull();
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
        const auto _Kind = ManufacturingPartKind(_Part->GetItemProperties());
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
                    _Part->GetQuantity(),
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

    struct SNestingGeometrySummary final
    {
        double EnvelopeLength = 0.0;
        SLinearNestingGeometry LinearGeometry;
    };

    SNestingGeometrySummary NestingGeometrySummary(
        iCAX::Project::ISceneContext& Scene_, const CManufacturingPartComponent& Part_)
    {
        const auto _ResourceID = Part_.GetManufacturingGeometryResourceID();
        const auto _Version = Part_.GetManufacturingGeometryResourceVersion();
        if (_ResourceID.empty() || _Version == 0)
            throw std::invalid_argument("零件缺少最终制造几何，请回到产品重新拆单");
        const auto _Key = _ResourceID + "@" + std::to_string(_Version);
        static std::mutex _Mutex;
        static std::map<std::string, SNestingGeometrySummary> _Measurements;
        {
            const std::lock_guard _Lock(_Mutex);
            if (const auto _Found = _Measurements.find(_Key); _Found != _Measurements.end())
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
        SNestingGeometrySummary _Result{ _Length, MeasureLinearNestingGeometry(_Shape) };
        {
            const std::lock_guard _Lock(_Mutex);
            if (_Measurements.size() > 10000) _Measurements.clear();
            _Measurements[_Key] = _Result;
        }
        _Result.EnvelopeLength = std::max(Part_.GetLength(), _Result.EnvelopeLength);
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

    std::string NestingPlaneKey(const double GradientY_, const double GradientZ_)
    {
        if (!std::isfinite(GradientY_) || !std::isfinite(GradientZ_)) return {};
        constexpr double _Units = 100000.0;
        return std::to_string(std::llround(GradientY_ * _Units)) + ":"
            + std::to_string(std::llround(GradientZ_ * _Units));
    }

    SPlanarNestingEnd TransformedNestingEnd(
        const SPlanarNestingEnd& Source_, const bool Reversed_, const double Rotation_)
    {
        auto _GradientY = Source_.GradientY;
        auto _GradientZ = Source_.GradientZ;
        if (Reversed_) _GradientZ = -_GradientZ;
        const auto _Cosine = std::cos(Rotation_);
        const auto _Sine = std::sin(Rotation_);
        return {
            Source_.IsPlanar,
            Source_.Projection,
            _Cosine * _GradientY - _Sine * _GradientZ,
            _Sine * _GradientY + _Cosine * _GradientZ
        };
    }

    std::vector<SNestingVariant> BuildNestingVariants(
        const ObjectMap& Profile_, const SLinearNestingGeometry& Geometry_,
        const double EnvelopeLength_)
    {
        constexpr double _Tolerance = 0.021;
        constexpr double _Pi = 3.14159265358979323846;
        if (!Geometry_.IsReliable || !ProfileAllowsNestingRotations(Profile_)
            || std::abs(Geometry_.EnvelopeLength - EnvelopeLength_) > _Tolerance
            || (Geometry_.Left.Projection <= _Tolerance
                && Geometry_.Right.Projection <= _Tolerance))
            return {};

        std::vector<SNestingVariant> _Variants;
        // A half-turn around the stock axis is safe for every centrally symmetric
        // section regardless of its normalized roll. End-for-end reversal would
        // additionally require proving a specific section mirror axis, so it is
        // intentionally not guessed here.
        for (const auto _Reversed : { false })
        {
            for (const auto _Rotation : { 0.0, _Pi })
            {
                const auto _LeftSource = _Reversed ? Geometry_.Right : Geometry_.Left;
                const auto _RightSource = _Reversed ? Geometry_.Left : Geometry_.Right;
                const auto _Left = TransformedNestingEnd(_LeftSource, _Reversed, _Rotation);
                const auto _Right = TransformedNestingEnd(_RightSource, _Reversed, _Rotation);
                const auto _MakeEnd = [&] (const SPlanarNestingEnd& Source_) {
                    SNestingEnd _End;
                    // Retain a geometric tolerance between nominally matching planes.
                    _End.Projection = std::max(0.0, Source_.Projection - _Tolerance);
                    _End.NestingPlane = NestingPlaneKey(Source_.GradientY, Source_.GradientZ);
                    _End.AllowTrapezoidNesting = Source_.IsPlanar
                        && _End.Projection > 0.0 && !_End.NestingPlane.empty();
                    return _End;
                };
                SNestingVariant _Variant;
                _Variant.ID = std::string(_Reversed ? "reverse" : "forward")
                    + (_Rotation == 0.0 ? "-0" : "-180");
                _Variant.EnvelopeLength = EnvelopeLength_;
                _Variant.MaterialLength = std::clamp(
                    Geometry_.MaterialEquivalentLength, 0.01, EnvelopeLength_);
                _Variant.LeftEnd = _MakeEnd(_Left);
                _Variant.RightEnd = _MakeEnd(_Right);
                _Variant.Reversed = _Reversed;
                _Variant.RotationRadians = _Rotation;
                _Variants.push_back(std::move(_Variant));
            }
        }
        return _Variants;
    }

    std::optional<const SNestingVariant*> FindNestingVariant(
        const SNestingPart& Part_, const std::string& ID_,
        const bool Reversed_, const double Rotation_)
    {
        if (Part_.Variants.empty())
            return ID_ == "default" && !Reversed_ && std::abs(Rotation_) <= 1.0e-9
                ? std::optional<const SNestingVariant*>{ nullptr } : std::nullopt;
        const auto _Found = std::ranges::find_if(Part_.Variants, [&](const auto& Variant_) {
            return Variant_.ID == ID_ && Variant_.Reversed == Reversed_
                && std::abs(Variant_.RotationRadians - Rotation_) <= 1.0e-9;
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
                const auto _VariantID = GetRequiredText(_Placement, "variantId", 160);
                const auto _Variant = FindNestingVariant(
                    *_PartFound->second, _VariantID, _Reversed, _Rotation);
                if (!_Variant) throw std::invalid_argument("锁定结果中的零件姿态已改变，请取消锁定后重新排样");
                auto _ExpectedGap = _Index ? QuantizedNestingLength(Gap_, true) : 0.0;
                bool _DidNest = false;
                if (_Index && _PreviousVariant && *_PreviousVariant && *_Variant)
                {
                    const auto& _PreviousEndDescriptor = (*_PreviousVariant)->RightEnd;
                    const auto& _NextEndDescriptor = (*_Variant)->LeftEnd;
                    if (_PreviousEndDescriptor.AllowTrapezoidNesting
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
                    || !std::isfinite(_Rotation) || !std::isfinite(_GapBefore)
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

    iCAX::Interaction::CInvocationResult HandleNest(
        const iCAX::Interaction::CInvocation& Request_,
        const iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
        iCAX::Project::ISceneContext* Scene_)
    {
        if (!Scene_) throw std::invalid_argument("TubeDesigner.Nest requires a scene");
        tube::license::Enforce<401, tube::license::Feature::Production>();
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
            throw std::invalid_argument("单次排样需为 1 至 2000 件零件、至多 1000 行母材");
        const auto _Parameters = GetRequiredObject(_Payload, "parameters");
        const auto _Gap = GetDouble(_Parameters, "partGap", 0.0);
        if (!std::isfinite(_Gap) || _Gap < 0.0 || _Gap > 1000000.0)
            throw std::invalid_argument("零件间距必须为 0 至 1000000 mm 的有限数值");
        std::vector<SNestingPart> _Parts;
        std::vector<SNestingStock> _Stocks;
        std::set<std::string> _PartIDs, _StockIDs;
        std::size_t _TotalParts = 0;
        auto& _Repository = Scene_->Database();
        for (const auto& _Item : _PartItems)
        {
            if (!_Item.Is<ObjectMap>()) throw std::invalid_argument("排样零件项必须是对象");
            const auto _PartData = _Item.To<ObjectMap>();
            const auto _PartID = ParseRequiredUuid(GetRequiredText(_PartData, "partEntityId"), "partEntityId");
            const auto _ID = UuidToString(_PartID);
            if (!_PartIDs.insert(_ID).second) throw std::invalid_argument("排样零件重复");
            const auto _Part = GetComponent<CManufacturingPartComponent>(_Repository.GetEntity(_PartID));
            if (!_Part) throw std::invalid_argument("排样零件不存在或不是拆单后的制造零件");
            if (!IsTubeManufacturingPart(_Part->GetItemProperties()))
                throw std::invalid_argument("板件或配件不参与管材排样，请在零件模块单独导出");
            const auto _Product = GetComponent<CProductInstanceComponent>(_Repository.GetEntity(_Part->GetProductID()));
            if (!_Product || _Product->GetActiveGenerationRunID() != _Part->GetGenerationRunID())
                throw std::invalid_argument("零件已过期，请回到产品重新拆单后排样");
            const auto _Quantity = GetUInt64(_PartData, "quantity", _Part->GetQuantity());
            if (_Quantity == 0 || _Quantity > _Part->GetQuantity() || _Quantity > 2000 - _TotalParts)
                throw std::invalid_argument("排样数量超出零件清单数量或单次 2000 件限制");
            _TotalParts += static_cast<std::size_t>(_Quantity);
            if (!std::isfinite(_Part->GetLength()) || _Part->GetLength() <= 0.0)
                throw std::invalid_argument("零件清单中的长度无效，请重新拆单");
            const auto _ProfileKey = GetRequiredText(_PartData, "profileKey", 65536);
            const auto _Presentation = ResolvePartPresentation(_Repository, *_Part);
            if (!MatchesNestingProfileKey(_ProfileKey, _Presentation.Profile, _ID))
                throw std::invalid_argument("零件截面数据已改变或分组不匹配，请刷新零件清单后重试");
            const auto _Geometry = NestingGeometrySummary(*Scene_, *_Part);
            _Parts.push_back({
                _ID,
                _ProfileKey,
                _Geometry.EnvelopeLength,
                static_cast<std::size_t>(_Quantity),
                BuildNestingVariants(
                    _Presentation.Profile, _Geometry.LinearGeometry, _Geometry.EnvelopeLength)
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
        return MakeResponse(Variant(SolveManufacturingNestingWithLocks(_Parts, _Stocks, _Gap, _LockedPlans)));
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
            for (const auto* _Field : { "start", "end", "length", "gapBefore", "rotationRadians" })
            {
                const auto _Value = GetDouble(_L, _Field, -1.0);
                if (!std::isfinite(_Value) || std::abs(_Value - GetDouble(_R, _Field, -2.0)) > 1e-6) _Changed();
            }
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
        const auto _Solved = HandleNest(_SolveRequest, Application_, Product_, Project_, Scene_);
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
            _ExportPlan.Name = "母材 " + std::to_string(_Found->second.second);
            _ExportPlan.StockLength = GetDouble(_Plan, "stockLength", 0);
            _ExportPlan.UsedLength = GetDouble(_Plan, "usedLength", 0);
            _ExportPlan.RemainingLength = GetDouble(_Plan, "remainingLength", 0);
            _ExportPlan.PartLength = GetDouble(_Plan, "partLength", 0);
            for (const auto& _PlacementValue : _Plan.at("placements").To<VariantArray>())
            {
                const auto _Placement = _PlacementValue.To<ObjectMap>();
                const auto _PartID = GetString(_Placement, "partId");
                _ExportPlan.Placements.push_back({
                    _PartID,
                    GetDouble(_Placement, "start", 0),
                    GetDouble(_Placement, "end", 0),
                    _Placement.at("reversed").To<bool>(),
                    GetDouble(_Placement, "gapBefore", 0),
                    _Placement.at("nestedWithPrevious").To<bool>(),
                    GetDouble(_Placement, "rotationRadians", 0),
                    GetString(_Placement, "variantId", "default")
                });
                if (_PartIDs.insert(_PartID).second)
                {
                    const auto _Part = GetComponent<CManufacturingPartComponent>(
                        _Repository.GetEntity(ParseRequiredUuid(_PartID, "partId")));
                    if (!_Part) throw std::invalid_argument("导出的制造零件不存在");
                    const auto _Presentation = ResolvePartPresentation(_Repository, *_Part);
                    _Parts.push_back({ _PartID, _Presentation.Name, _Part->GetPartNumber(),
                        NestingManufacturingShape(*Scene_, *_Part) });
                }
                if (_ExportPlan.Profile.empty())
                {
                    const auto _Part = GetComponent<CManufacturingPartComponent>(
                        _Repository.GetEntity(ParseRequiredUuid(_PartID, "partId")));
                    const auto _Profile = ResolvePartPresentation(_Repository, *_Part).Profile;
                    _ExportPlan.Profile = GetString(_Profile, "displayName") + " " + GetString(_Profile, "specification");
                }
            }
            _Plans.push_back(std::move(_ExportPlan));
        }
        const auto _Gap = GetDouble(GetRequiredObject(_Original, "parameters"), "partGap", 0);
        return MakeResponse(Variant(ExportNestingResults(Utf8Path(_Directory), _Plans, _Parts, _Gap)));
    }

    class CTubeDesignerSDO final : public iCAX::Interaction::CSDO
    {
    public:
        CTubeDesignerSDO() : CSDO("TubeDesigner")
        {
            ExposeMethod("List", &HandleList);
            ExposeMethod("ListUserData", &HandleListUserData);
            ExposeMethod("ListComponentModels", &HandleListComponentModels);
            ExposeMethod("ImportComponentModel", &HandleImportComponentModel);
            ExposeMethod("UpdateComponentModel", &HandleUpdateComponentModel);
            ExposeMethod("DeleteComponentModel", &HandleDeleteComponentModel);
            ExposeMethod("GenerateComponentModelPreview", &HandleGenerateComponentModelPreview);
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
            ExposeMethod("SaveSketch", &HandleSaveSketch);
            ExposeMethod("GeneratePreview", &HandleGeneratePreview);
            ExposeMethod("Generate", &HandleGeneratePreview);
            ExposeMethod("Disassemble", &HandleDisassemble);
            ExposeMethod("DisassembleSelected", &HandleDisassemble);
            ExposeMethod("MeasurePartGeometry", &HandleMeasurePartGeometry);
            ExposeMethod("Nest", &HandleNest);
            ExposeMethod("SaveNestingSettings", &HandleSaveNestingSettings);
            ExposeMethod("ExportNesting", &HandleExportNesting);
            ExposeMethod("ExportSelected", &HandleExportSelected);
            ExposeMethod("ExportAll", &HandleExportSelected);
        }
    };

    static_assert(iCAX::Interaction::IsStatelessSDOType<CTubeDesignerSDO>);
}

ICAX_REGISTER_SDO(CTubeDesignerSDO)
