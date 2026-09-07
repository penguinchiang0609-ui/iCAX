#include "pch.h"
#include "ComponentModelLibrary.h"
#include "OpenCascadeResourceImport/OpenCascadeNeutralModelEvaluator.h"
#include "TemplateRuntime/StandardJsonCodec.h"
#include "TemplateRuntime/TemplateCodec.h"
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <STEPControl_Reader.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Trsf.hxx>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <iomanip>
#include <set>
#include <sstream>

namespace iCAX::TubeDesigner
{
    namespace
    {
        using namespace iCAX::Data;
        constexpr std::size_t kMaximumModelBytes = 32u * 1024u * 1024u;
        constexpr std::size_t kMaximumSnapshotBytes = 64u * 1024u * 1024u;
        std::string Text(const ObjectMap& Values_, const std::string& Key_, const std::string& Default_ = {})
        {
            const auto _It = Values_.find(Key_);
            if (_It == Values_.end()) return Default_;
            if (!_It->second.Is<std::string>()) throw std::invalid_argument("配件字段必须是文本：" + Key_);
            return _It->second.To<std::string>();
        }
        ObjectMap Object(const ObjectMap& Values_, const std::string& Key_)
        {
            const auto _It = Values_.find(Key_);
            if (_It == Values_.end()) return {};
            if (!_It->second.Is<ObjectMap>()) throw std::invalid_argument("配件对象字段无效：" + Key_);
            return _It->second.To<ObjectMap>();
        }
        std::filesystem::path Utf8Path(const std::string& Value_)
        {
            return std::filesystem::path(std::u8string(
                reinterpret_cast<const char8_t*>(Value_.data()), Value_.size()));
        }
        std::string PathText(const std::filesystem::path& Path_)
        {
            const auto _Text = Path_.u8string();
            return { _Text.begin(), _Text.end() };
        }
        std::string ReadText(const std::filesystem::path& Path_)
        {
            if (!std::filesystem::is_regular_file(Path_) || std::filesystem::file_size(Path_) > kMaximumModelBytes)
                throw std::invalid_argument("配件模型不存在或超过 32 MiB 大小限制");
            std::ifstream _Stream(Path_, std::ios::binary);
            if (!_Stream) throw std::runtime_error("无法打开配件模型文件");
            return { std::istreambuf_iterator<char>(_Stream), std::istreambuf_iterator<char>() };
        }
        ObjectMap ReadObject(const std::filesystem::path& Path_)
        {
            const auto _Document = iCAX::TemplateRuntime::CStandardJsonCodec::Parse(ReadText(Path_));
            if (!_Document.Is<ObjectMap>()) throw std::invalid_argument("配件资源描述必须是 JSON 对象");
            return _Document.To<ObjectMap>();
        }
        void ValidateID(const std::string& ID_)
        {
            if (ID_.empty() || ID_.size() > 120 || ID_ == "." || ID_ == ".."
                || !std::all_of(ID_.begin(), ID_.end(), [](unsigned char C_) {
                    return std::isalnum(C_) || C_ == '-' || C_ == '_' || C_ == '.';
                })) throw std::invalid_argument("配件资源标识无效");
        }
        std::filesystem::path ConfinedPath(const std::filesystem::path& Root_, const std::string& Relative_)
        {
            const auto _Relative = Utf8Path(Relative_);
            if (_Relative.empty() || _Relative.is_absolute() || _Relative.has_root_name() || _Relative.has_root_directory()
                || Relative_.find(':') != std::string::npos || Relative_.find('\\') != std::string::npos)
                throw std::invalid_argument("模板配件资源必须使用包内相对路径");
            for (const auto& _Part : _Relative)
                if (_Part == "..") throw std::invalid_argument("模板配件资源不得越过包目录");
            const auto _Root = std::filesystem::canonical(Root_);
            const auto _Target = std::filesystem::canonical(_Root / _Relative);
            const auto _Within = _Target.lexically_relative(_Root);
            if (_Within.empty() || _Within.is_absolute() || *_Within.begin() == "..")
                throw std::invalid_argument("模板配件资源不得通过链接越过包目录");
            return _Target;
        }
        std::string Digest(const std::string& Text_)
        {
            std::uint64_t _Hash = 14695981039346656037ull;
            for (unsigned char _Byte : Text_) { _Hash ^= _Byte; _Hash *= 1099511628211ull; }
            std::ostringstream _Out;
            _Out << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << _Hash;
            return _Out.str();
        }
        std::string SerializeShape(const TopoDS_Shape& Shape_)
        {
            std::ostringstream _Out;
            BRepTools::Write(Shape_, _Out);
            auto _Text = _Out.str();
            if (_Text.empty() || _Text.size() > kMaximumModelBytes)
                throw std::invalid_argument("配件实体超过 32 MiB 大小限制");
            return _Text;
        }
        TopoDS_Shape ShapeFromFile(const std::filesystem::path& Source_)
        {
            if (!std::filesystem::is_regular_file(Source_) || std::filesystem::file_size(Source_) > kMaximumModelBytes)
                throw std::invalid_argument("配件模型不存在或超过 32 MiB 大小限制");
            auto _Extension = Source_.extension().string();
            std::transform(_Extension.begin(), _Extension.end(), _Extension.begin(), [](unsigned char C_) {
                return static_cast<char>(std::tolower(C_));
            });
            TopoDS_Shape _Shape;
            if (_Extension == ".step" || _Extension == ".stp")
            {
                STEPControl_Reader _Reader;
                if (_Reader.ReadFile(PathText(Source_).c_str()) != IFSelect_RetDone || _Reader.TransferRoots() <= 0)
                    throw std::invalid_argument("无法读取 STEP 配件实体");
                _Shape = _Reader.OneShape();
            }
            else if (_Extension == ".brep")
            {
                return ComponentModelShape({ { "brep", ReadText(Source_) } });
            }
            else if (_Extension == ".json")
            {
                const auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(ReadObject(Source_));
                if (_Model.LengthUnit != "mm") throw std::invalid_argument("配件几何 JSON 必须使用毫米单位");
                if (_Model.Geometry.size() > 2000) throw std::invalid_argument("配件模型的几何节点过多");
                std::vector<std::string> _Roots;
                for (const auto& _Output : _Model.Outputs)
                    for (const auto& _Key : _Output.ItemKeys)
                    {
                        const auto _Item = std::find_if(_Model.Items.begin(), _Model.Items.end(), [&](const auto& Item_) {
                            return Item_.Key == _Key;
                        });
                        if (_Item == _Model.Items.end() || !_Item->Representations.contains(_Output.Purpose))
                            throw std::invalid_argument("配件输出引用缺失的几何");
                        const auto& _Root = _Item->Representations.at(_Output.Purpose);
                        if (std::find(_Roots.begin(), _Roots.end(), _Root) == _Roots.end()) _Roots.push_back(_Root);
                    }
                if (_Roots.empty()) throw std::invalid_argument("配件几何文件没有声明输出实体");
                const auto _Evaluation = iCAX::OpenCascade::EvaluateNeutralModel(_Model, _Roots);
                if (_Roots.size() == 1) _Shape = _Evaluation.At(_Roots.front());
                else
                {
                    BRep_Builder _Builder;
                    TopoDS_Compound _Compound;
                    _Builder.MakeCompound(_Compound);
                    for (const auto& _Root : _Roots) _Builder.Add(_Compound, _Evaluation.At(_Root));
                    _Shape = _Compound;
                }
            }
            else throw std::invalid_argument("配件仅支持 STEP、STP、BREP 或模板内置几何 JSON");
            if (_Shape.IsNull()) throw std::invalid_argument("配件模型没有实体");
            // Reuse the generic resource validator so imports and template
            // resources obey exactly the same shape validity contract.
            return ComponentModelShape({ { "brep", SerializeShape(_Shape) } });
        }
        ObjectMap SnapshotFromFile(const std::filesystem::path& Source_, const ObjectMap& Metadata_, bool Normalize_)
        {
            auto _Shape = ShapeFromFile(Source_);
            Bnd_Box _Box;
            BRepBndLib::Add(_Shape, _Box, false);
            _Box.SetGap(0);
            double _X0, _Y0, _Z0, _X1, _Y1, _Z1;
            _Box.Get(_X0, _Y0, _Z0, _X1, _Y1, _Z1);
            VariantArray _Offset{ 0.0, 0.0, 0.0 };
            if (Normalize_)
            {
                const auto _X = -(_X0 + _X1) / 2;
                const auto _Y = -(_Y0 + _Y1) / 2;
                const auto _Z = -_Z0;
                gp_Trsf _Placement;
                _Placement.SetTranslation(gp_Vec(_X, _Y, _Z));
                _Shape = _Shape.Moved(TopLoc_Location(_Placement));
                _Offset = { _X, _Y, _Z };
                _X0 += _X; _X1 += _X; _Y0 += _Y; _Y1 += _Y; _Z1 += _Z; _Z0 = 0;
            }
            const auto _BRep = SerializeShape(_Shape);
            ObjectMap _Snapshot{
                { "schema", std::string("icax.component-model") }, { "schemaVersion", 1 },
                { "name", PathText(Source_.stem()) }, { "category", std::string("其他配件") },
                { "sourcing", std::string("purchased") }, { "material", std::string() },
                { "description", std::string() }, { "unit", std::string("mm") },
                { "sourceFileName", PathText(Source_.filename()) },
                { "brep", _BRep }, { "geometryDigest", Digest(_BRep) },
                { "importTranslation", _Offset },
                { "bounds", ObjectMap{ { "width", _X1 - _X0 }, { "depth", _Y1 - _Y0 },
                    { "height", _Z1 - _Z0 }, { "min", VariantArray{ _X0, _Y0, _Z0 } },
                    { "max", VariantArray{ _X1, _Y1, _Z1 } } } }
            };
            UpdateComponentModelMetadata(_Snapshot, Metadata_);
            return _Snapshot;
        }
    }

    void UpdateComponentModelMetadata(ObjectMap& Snapshot_, const ObjectMap& Metadata_)
    {
        ObjectMap _Updates;
        for (const auto* _Key : { "name", "category", "sourcing", "material", "description" })
        {
            if (!Metadata_.contains(_Key)) continue;
            auto _Value = Text(Metadata_, _Key);
            const auto _Maximum = std::string(_Key) == "description" ? 4000u : 240u;
            if (_Value.size() > _Maximum || _Value.find('\0') != std::string::npos)
                throw std::invalid_argument("配件文本过长或包含无效字符");
            if ((std::string(_Key) == "name" || std::string(_Key) == "category")
                && _Value.find_first_not_of(" \t\r\n") == std::string::npos)
                throw std::invalid_argument("配件名称和分类不能为空");
            if (std::string(_Key) == "sourcing" && _Value != "made" && _Value != "purchased")
                throw std::invalid_argument("配件供料方式只能是自制或外购");
            _Updates[_Key] = std::move(_Value);
        }
        for (auto& [_Key, _Value] : _Updates) Snapshot_[_Key] = std::move(_Value);
    }

    ObjectMap ImportComponentModelFile(const std::filesystem::path& Source_, const ObjectMap& Metadata_)
    {
        auto _Extension = Source_.extension().string();
        std::transform(_Extension.begin(), _Extension.end(), _Extension.begin(), [](unsigned char C_) {
            return static_cast<char>(std::tolower(C_));
        });
        if (_Extension != ".step" && _Extension != ".stp" && _Extension != ".brep")
            throw std::invalid_argument("请导入 STEP / STP / BREP 三维实体文件");
        return SnapshotFromFile(Source_, Metadata_, true);
    }

    TopoDS_Shape ComponentModelShape(const ObjectMap& Snapshot_)
    {
        iCAX::TemplateRuntime::SNeutralModel _Model;
        iCAX::TemplateRuntime::SGeometryNode _Node;
        _Node.Key = "component";
        _Node.Operator = iCAX::TemplateRuntime::EGeometryOperator::Resource;
        _Node.Arguments["brep"] = Text(Snapshot_, "brep");
        _Model.Geometry.push_back(std::move(_Node));
        return iCAX::OpenCascade::EvaluateNeutralModel(_Model, { "component" }).At("component");
    }

    ObjectMap ComponentModelSummary(const ObjectMap& Snapshot_, const std::string& Scope_,
        const std::string& ID_, std::uint64_t Revision_)
    {
        ObjectMap _Summary;
        for (const auto* _Key : { "name", "category", "sourcing", "material", "description", "unit",
            "sourceFileName", "bounds", "geometryDigest", "importTranslation" })
            if (const auto _It = Snapshot_.find(_Key); _It != Snapshot_.end()) _Summary[_Key] = _It->second;
        _Summary["scope"] = Scope_;
        _Summary["id"] = ID_;
        _Summary["revision"] = static_cast<unsigned long long>(Revision_);
        return _Summary;
    }

    ObjectMap LoadSystemComponentModel(const std::filesystem::path& Root_, const std::string& ID_)
    {
        ValidateID(ID_);
        const auto _Manifest = ConfinedPath(Root_, ID_ + "/model.json");
        const auto _Metadata = ReadObject(_Manifest);
        if (Text(_Metadata, "schema") != "icax.component-model" || Text(_Metadata, "id") != ID_)
            throw std::invalid_argument("系统配件模型描述身份不一致");
        if (!_Metadata.contains("schemaVersion")
            || iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(_Metadata.at("schemaVersion")) != "1")
            throw std::invalid_argument("系统配件模型描述版本不支持");
        const auto _File = ConfinedPath(_Manifest.parent_path(), Text(_Metadata, "geometryFile"));
        auto _Snapshot = SnapshotFromFile(_File, _Metadata, false);
        _Snapshot["sourceReference"] = "system:" + ID_;
        return _Snapshot;
    }

    ObjectMap ResolveComponentModelSnapshot(const std::filesystem::path& SystemRoot_,
        iCAX::Application::IProductUserDataStore* Store_, const std::string& Scope_, const std::string& ID_)
    {
        if (Scope_ == "system") return LoadSystemComponentModel(SystemRoot_, ID_);
        if (Scope_ != "user" || !Store_) throw std::invalid_argument("配件模型来源无效或配件库不可用");
        const auto _Record = Store_->Get(kComponentModelFeature, kComponentModelRecordType, ID_);
        if (!_Record || !_Record->Payload.Is<ObjectMap>()) throw std::invalid_argument("配件模型已删除或不存在");
        auto _Snapshot = _Record->Payload.To<ObjectMap>();
        _Snapshot["revision"] = static_cast<unsigned long long>(_Record->Revision);
        _Snapshot["sourceReference"] = "library:" + ID_;
        return _Snapshot;
    }

    VariantArray ListComponentModelSummaries(const std::filesystem::path& Root_,
        iCAX::Application::IProductUserDataStore* Store_)
    {
        VariantArray _Models;
        std::vector<std::string> _IDs;
        if (std::filesystem::is_directory(Root_))
            for (const auto& _Entry : std::filesystem::directory_iterator(Root_))
                if (_Entry.is_directory() && std::filesystem::is_regular_file(_Entry.path() / "model.json"))
                    _IDs.push_back(PathText(_Entry.path().filename()));
        std::sort(_IDs.begin(), _IDs.end());
        for (const auto& _ID : _IDs)
            _Models.emplace_back(ComponentModelSummary(LoadSystemComponentModel(Root_, _ID), "system", _ID));
        if (Store_)
        {
            iCAX::Application::CProductUserDataQuery _Query;
            _Query.FeatureID = kComponentModelFeature;
            _Query.RecordType = kComponentModelRecordType;
            for (const auto& _Record : Store_->List(_Query))
                if (_Record.Payload.Is<ObjectMap>())
                    _Models.emplace_back(ComponentModelSummary(_Record.Payload.To<ObjectMap>(),
                        "user", _Record.RecordID, _Record.Revision));
        }
        return _Models;
    }

    void ResolveTemplateComponentResources(ObjectMap& Document_, const std::filesystem::path& TemplateDirectory_,
        const ObjectMap& DescriptorExtensions_, const std::filesystem::path& SystemRoot_,
        iCAX::Application::IProductUserDataStore* Store_, const ObjectMap* Frozen_)
    {
        const auto _Resources = Object(DescriptorExtensions_, "modelResources");
        auto _Geometry = Document_.at("geometry").To<VariantArray>();
        std::set<std::string> _References;
        for (const auto& _Value : _Geometry)
        {
            const auto _Node = _Value.To<ObjectMap>();
            if (Text(_Node, "operator") != "resource") continue;
            const auto _Arguments = Object(_Node, "arguments");
            const auto _Reference = Text(_Arguments, "reference");
            if (_Reference.empty()) throw std::invalid_argument("模板配件缺少稳定模型引用");
            _References.insert(_Reference);
        }
        // Freeze declared package resources, including manufacturing-only ones.
        if (!Frozen_)
            for (const auto& [_Key, _Value] : _Resources) _References.insert("template:" + _Key);
        if (_References.empty()) return;
        if (_References.size() > 64) throw std::invalid_argument("单个产品引用的配件模型种类超过 64 种");
        ObjectMap _Snapshots;
        std::size_t _TotalBytes = 0;
        for (const auto& _Reference : _References)
        {
            ObjectMap _Snapshot;
            if (Frozen_)
            {
                const auto _It = Frozen_->find(_Reference);
                if (_It == Frozen_->end() || !_It->second.Is<ObjectMap>())
                    throw std::invalid_argument("缺少产品生成时保存的配件快照，请重新生成产品");
                _Snapshot = _It->second.To<ObjectMap>();
            }
            else if (_Reference.starts_with("template:"))
            {
                const auto _Key = _Reference.substr(9);
                ValidateID(_Key);
                const auto _It = _Resources.find(_Key);
                if (_It == _Resources.end() || !_It->second.Is<ObjectMap>())
                    throw std::invalid_argument("模板没有声明此配件资源：" + _Key);
                const auto _Metadata = _It->second.To<ObjectMap>();
                _Snapshot = SnapshotFromFile(ConfinedPath(TemplateDirectory_, Text(_Metadata, "path")), _Metadata, false);
                _Snapshot["sourceReference"] = _Reference;
            }
            else if (_Reference.starts_with("system:"))
                _Snapshot = ResolveComponentModelSnapshot(SystemRoot_, Store_, "system", _Reference.substr(7));
            else if (_Reference.starts_with("library:"))
                _Snapshot = ResolveComponentModelSnapshot(SystemRoot_, Store_, "user", _Reference.substr(8));
            else throw std::invalid_argument("配件引用必须来自系统库、我的配件库或模板资源");
            const auto _BRep = Text(_Snapshot, "brep");
            _TotalBytes += _BRep.size();
            if (_BRep.empty() || _TotalBytes > kMaximumSnapshotBytes)
                throw std::invalid_argument("产品配件几何为空或累计超过 64 MiB");
            if (Text(_Snapshot, "geometryDigest") != Digest(_BRep))
                throw std::invalid_argument("配件模型快照校验失败");
            _Snapshots[_Reference] = std::move(_Snapshot);
        }
        for (auto& _Value : _Geometry)
        {
            auto _Node = _Value.To<ObjectMap>();
            if (Text(_Node, "operator") != "resource") continue;
            auto _Arguments = Object(_Node, "arguments");
            const auto& _Snapshot = _Snapshots.at(Text(_Arguments, "reference")).To<ObjectMap>();
            _Arguments["brep"] = _Snapshot.at("brep");
            _Arguments["geometryDigest"] = _Snapshot.at("geometryDigest");
            _Node["arguments"] = std::move(_Arguments);
            _Value = std::move(_Node);
        }
        std::map<std::string, ObjectMap> _Nodes;
        for (const auto& _Value : _Geometry)
        {
            auto _Node = _Value.To<ObjectMap>();
            _Nodes.emplace(Text(_Node, "key"), std::move(_Node));
        }
        Document_["geometry"] = std::move(_Geometry);
        auto _Items = Document_.at("items").To<VariantArray>();
        for (auto& _Value : _Items)
        {
            auto _Item = _Value.To<ObjectMap>();
            auto _Properties = Object(_Item, "properties");
            // Derive provenance from the actual geometry as well: an omitted
            // metadata field must never turn a reused solid into tube stock.
            std::set<std::string> _ItemReferences, _Visited;
            std::function<void(const std::string&)> _Visit = [&](const std::string& Key_) {
                if (!_Visited.insert(Key_).second) return;
                const auto _NodeIt = _Nodes.find(Key_);
                if (_NodeIt == _Nodes.end()) return; // Codec reports missing keys.
                const auto& _Node = _NodeIt->second;
                if (Text(_Node, "operator") == "resource")
                    _ItemReferences.insert(Text(Object(_Node, "arguments"), "reference"));
                const auto _Inputs = _Node.find("inputs");
                if (Text(_Node, "operator") == "boolean")
                {
                    // Match BooleanShape's effective dependencies. Explicit
                    // target/tools override inputs and may be the only place
                    // a resource prototype is referenced in a valid graph.
                    const auto _Arguments = Object(_Node, "arguments");
                    const auto _InputKeys = _Inputs == _Node.end() ? VariantArray{}
                        : _Inputs->second.To<VariantArray>();
                    if (_Arguments.contains("target")) _Visit(Text(_Arguments, "target"));
                    else if (!_InputKeys.empty()) _Visit(_InputKeys.front().To<std::string>());
                    if (const auto _Tools = _Arguments.find("tools"); _Tools != _Arguments.end())
                    {
                        for (const auto& _Tool : _Tools->second.To<VariantArray>()) _Visit(_Tool.To<std::string>());
                    }
                    else
                    {
                        for (std::size_t _Index = 1; _Index < _InputKeys.size(); ++_Index)
                            _Visit(_InputKeys[_Index].To<std::string>());
                    }
                    return;
                }
                if (_Inputs != _Node.end())
                    for (const auto& _Input : _Inputs->second.To<VariantArray>()) _Visit(_Input.To<std::string>());
            };
            for (const auto& [_Purpose, _Root] : Object(_Item, "representations")) _Visit(_Root.To<std::string>());
            auto _Reference = Text(_Properties, "manufacturing.modelReference");
            if (_ItemReferences.size() > 1)
                throw std::invalid_argument("一个配件清单项只能引用一种模型，请将装配拆成独立配件项");
            if (!_ItemReferences.empty())
            {
                if (!_Reference.empty() && _Reference != *_ItemReferences.begin())
                    throw std::invalid_argument("配件清单引用与实际几何来源不一致");
                _Reference = *_ItemReferences.begin();
                _Properties["manufacturing.modelReference"] = _Reference;
            }
            if (_Reference.empty()) continue;
            const auto _It = _Snapshots.find(_Reference);
            if (_It == _Snapshots.end()) throw std::invalid_argument("配件清单引用缺少模型快照");
            const auto _Snapshot = _It->second.To<ObjectMap>();
            // A reused imported solid is always an accessory, never a fake tube
            // with a bounding-box length sent to a tube cutting solver.
            _Properties["manufacturing.partKind"] = std::string("accessory");
            _Properties["manufacturing.materialCategory"] = std::string("accessory");
            _Properties["manufacturing.sourcing"] = _Snapshot.at("sourcing");
            _Properties["manufacturing.material"] = _Snapshot.at("material");
            _Properties["manufacturing.modelName"] = _Snapshot.at("name");
            _Properties["manufacturing.modelBounds"] = _Snapshot.at("bounds");
            _Properties["manufacturing.modelDigest"] = _Snapshot.at("geometryDigest");
            _Properties["manufacturing.process"] = Text(_Snapshot, "sourcing") == "purchased"
                ? std::string("purchased") : std::string("separate-fabrication");
            _Properties["length"] = 0.0;
            _Properties.erase("tubeDesigner.profile");
            _Properties.erase("manufacturing.plate");
            _Item["properties"] = std::move(_Properties);
            _Value = std::move(_Item);
        }
        Document_["items"] = std::move(_Items);
        auto _Extensions = Object(Document_, "extensions");
        _Extensions[kResolvedComponentModels] = std::move(_Snapshots);
        Document_["extensions"] = std::move(_Extensions);
    }
}
