#include "pch.h"
#include "ComponentModelLibrary.h"
#include "FinalGeometryMeasurement.h"
#include "OpenCascadeResourceImport/OpenCascadeNeutralModelEvaluator.h"
#include "TemplateRuntime/StandardJsonCodec.h"
#include "TemplateRuntime/TemplateCodec.h"
#include <BRepTools.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRep_Builder.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <Bnd_Box.hxx>
#include <IGESControl_Reader.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <StepData_StepModel.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Compound.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
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
        double CSGNumber(const Variant& Value_, const std::string& Path_)
        {
            double _Result = 0;
            if (Value_.Is<double>()) _Result = Value_.To<double>();
            else if (Value_.Is<float>()) _Result = static_cast<double>(Value_.To<float>());
            else if (Value_.Is<int>()) _Result = static_cast<double>(Value_.To<int>());
            else if (Value_.Is<unsigned int>()) _Result = static_cast<double>(Value_.To<unsigned int>());
            else if (Value_.Is<long long>()) _Result = static_cast<double>(Value_.To<long long>());
            else if (Value_.Is<unsigned long long>()) _Result = static_cast<double>(Value_.To<unsigned long long>());
            else throw std::invalid_argument("CSG 数值字段无效：" + Path_);
            if (!std::isfinite(_Result)) throw std::invalid_argument("CSG 数值必须是有限值：" + Path_);
            return _Result;
        }
        double CSGNumber(const ObjectMap& Values_, const std::string& Key_, const std::string& Path_,
            double Default_, bool Required_ = true)
        {
            const auto _It = Values_.find(Key_);
            if (_It == Values_.end())
            {
                if (Required_) throw std::invalid_argument("CSG 缺少数值字段：" + Path_);
                return Default_;
            }
            return CSGNumber(_It->second, Path_);
        }
        ObjectMap CSGObject(const ObjectMap& Values_, const std::string& Key_, const std::string& Path_)
        {
            const auto _It = Values_.find(Key_);
            if (_It == Values_.end() || !_It->second.Is<ObjectMap>())
                throw std::invalid_argument("CSG 缺少对象字段：" + Path_);
            return _It->second.To<ObjectMap>();
        }
        std::string CSGText(const ObjectMap& Values_, const std::string& Key_, const std::string& Path_,
            std::size_t Maximum_ = 120)
        {
            const auto _Value = Text(Values_, Key_);
            if (_Value.empty() || _Value.size() > Maximum_ || _Value.find('\0') != std::string::npos)
                throw std::invalid_argument("CSG 文本字段无效：" + Path_);
            return _Value;
        }
        double BoundedCSGNumber(const ObjectMap& Values_, const std::string& Key_, const std::string& Path_,
            double Minimum_, double Maximum_, double Default_ = 0, bool Required_ = true)
        {
            const auto _Value = CSGNumber(Values_, Key_, Path_, Default_, Required_);
            if (_Value < Minimum_ || _Value > Maximum_)
                throw std::invalid_argument("CSG 数值超出范围：" + Path_);
            return _Value;
        }
        TopoDS_Shape BuildCSGProfileExtrusion(const ObjectMap& Profile_, const double Height_)
        {
            if (!std::isfinite(Height_) || Height_ <= 1.0e-6)
                throw std::invalid_argument("CSG 拉伸高度无效");
            const auto _Contours = Profile_.find("contours");
            if (_Contours == Profile_.end() || !_Contours->second.Is<VariantArray>())
                throw std::invalid_argument("CSG 拉伸截面缺少轮廓");
            const auto _ContourValues = _Contours->second.To<VariantArray>();
            if (_ContourValues.empty() || _ContourValues.size() > 1000)
                throw std::invalid_argument("CSG 拉伸截面轮廓数量无效");
            const VariantArray _Origin{ Variant(0.0), Variant(0.0), Variant(0.0) };
            const VariantArray _XAxis{ Variant(1.0), Variant(0.0), Variant(0.0) };
            const VariantArray _YAxis{ Variant(0.0), Variant(1.0), Variant(0.0) };
            const VariantArray _Vector{ Variant(0.0), Variant(0.0), Variant(Height_) };
            const ObjectMap _Document{
                { "schema", std::string("icax.neutral-model") }, { "schemaVersion", 1ull },
                { "template", ObjectMap{
                    { "id", std::string("icax.component-csg-profile-extrusion") },
                    { "version", std::string("1.0.0") },
                    { "packageDigest", Text(Profile_, "contentDigest", Text(Profile_, "kind", "profile")) }
                } },
                { "geometry", VariantArray{
                    ObjectMap{
                        { "key", std::string("profile") }, { "operator", std::string("profile2d") },
                        { "arguments", ObjectMap{
                            { "placement", ObjectMap{
                                { "origin", _Origin }, { "xAxis", _XAxis }, { "yAxis", _YAxis }
                            } },
                            { "contours", _ContourValues }
                        } }
                    },
                    ObjectMap{
                        { "key", std::string("solid") }, { "operator", std::string("extrude") },
                        { "inputs", VariantArray{ Variant(std::string("profile")) } },
                        { "arguments", ObjectMap{ { "vector", _Vector } } }
                    }
                } }
            };
            const auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(Variant(_Document));
            return iCAX::OpenCascade::EvaluateNeutralModel(_Model, { "solid" }).At("solid");
        }
        ObjectMap NormalizeCSGProfile(const ObjectMap& Profile_)
        {
            const auto _SourceType = CSGText(Profile_, "sourceType", "profile.sourceType", 24);
            if (_SourceType != "parametric" && _SourceType != "fixed")
                throw std::invalid_argument("CSG 拉伸截面类型必须是程式或定式");
            const auto _Name = CSGText(Profile_, "name", "profile.name", 240);
            const auto _Key = CSGText(Profile_, "key", "profile.key", 240);
            const auto _Snapshot = CSGObject(Profile_, "snapshot", "profile.snapshot");
            if (Text(_Snapshot, "schema") != "icax.imported-tube-profile")
                throw std::invalid_argument("CSG 拉伸截面格式不受支持");
            const auto _Version = _Snapshot.find("schemaVersion");
            if (_Version == _Snapshot.end() || CSGNumber(_Version->second, "profile.schemaVersion") != 1)
                throw std::invalid_argument("CSG 拉伸截面版本不受支持");
            const auto _Kind = CSGText(_Snapshot, "kind", "profile.kind", 40);
            if (_Kind != "imported-dxf" && _Kind != "parametric-package")
                throw std::invalid_argument("CSG 拉伸截面来源不受支持");
            (void)BoundedCSGNumber(_Snapshot, "width", "profile.width", .001, 100000);
            (void)BoundedCSGNumber(_Snapshot, "depth", "profile.depth", .001, 100000);
            if (BuildCSGProfileExtrusion(_Snapshot, 1.0).IsNull())
                throw std::invalid_argument("CSG 拉伸截面无法生成实体");

            ObjectMap _Result{
                { "sourceType", _SourceType }, { "key", _Key }, { "name", _Name },
                { "snapshot", _Snapshot }
            };
            if (const auto _Parameters = Profile_.find("parameters"); _Parameters != Profile_.end())
            {
                if (!_Parameters->second.Is<ObjectMap>() || _Parameters->second.To<ObjectMap>().size() > 128)
                    throw std::invalid_argument("CSG 程式截面参数无效");
                _Result["parameters"] = _Parameters->second;
            }
            else _Result["parameters"] = ObjectMap{};
            if (const auto _Definitions = Profile_.find("parameterDefinitions"); _Definitions != Profile_.end())
            {
                if (!_Definitions->second.Is<VariantArray>() || _Definitions->second.To<VariantArray>().size() > 128)
                    throw std::invalid_argument("CSG 程式截面参数定义无效");
                _Result["parameterDefinitions"] = _Definitions->second;
            }
            else _Result["parameterDefinitions"] = VariantArray{};
            if (const auto _Reference = Profile_.find("profileRef"); _Reference != Profile_.end())
            {
                if (!_Reference->second.Is<ObjectMap>()) throw std::invalid_argument("CSG 程式截面引用无效");
                _Result["profileRef"] = _Reference->second;
            }
            return _Result;
        }
        ObjectMap NormalizeComponentCSG(const ObjectMap& Definition_)
        {
            if (Text(Definition_, "schema") != "icax.component-csg"
                || Text(Definition_, "evaluation") != "left-fold")
                throw std::invalid_argument("配件 CSG 定义或求值方式不受支持");
            const auto _Schema = Definition_.find("schemaVersion");
            if (_Schema == Definition_.end() || CSGNumber(_Schema->second, "schemaVersion") != 1)
                throw std::invalid_argument("配件 CSG 版本不受支持");
            const auto _FeaturesIt = Definition_.find("features");
            if (_FeaturesIt == Definition_.end() || !_FeaturesIt->second.Is<VariantArray>())
                throw std::invalid_argument("配件 CSG 必须包含基础体数组");
            const auto _Features = _FeaturesIt->second.To<VariantArray>();
            if (_Features.empty() || _Features.size() > 64)
                throw std::invalid_argument("配件 CSG 必须包含 1 至 64 个基础体");

            VariantArray _CanonicalFeatures;
            std::set<std::string> _IDs;
            for (std::size_t _Index = 0; _Index < _Features.size(); ++_Index)
            {
                if (!_Features[_Index].Is<ObjectMap>()) throw std::invalid_argument("配件 CSG 基础体必须是对象");
                const auto _Feature = _Features[_Index].To<ObjectMap>();
                const auto _ID = CSGText(_Feature, "id", "features.id", 120);
                ValidateID(_ID);
                if (!_IDs.insert(_ID).second) throw std::invalid_argument("配件 CSG 节点标识不能重复");
                const auto _Name = CSGText(_Feature, "name", "features.name", 240);
                const auto _Primitive = CSGText(_Feature, "primitive", "features.primitive", 24);
                const auto _Operation = CSGText(_Feature, "operation", "features.operation", 24);
                if (_Primitive != "extrusion" && _Primitive != "box" && _Primitive != "cylinder" && _Primitive != "sphere" && _Primitive != "cone")
                    throw std::invalid_argument("配件 CSG 包含不支持的基础体：" + _Primitive);
                if ((_Index == 0 && _Operation != "base") || (_Index > 0 && _Operation != "union"
                    && _Operation != "difference" && _Operation != "intersection"))
                    throw std::invalid_argument("配件 CSG 布尔运算顺序无效");
                const auto _SourceParameters = CSGObject(_Feature, "parameters", "features.parameters");
                ObjectMap _Parameters;
                ObjectMap _Profile;
                if (_Primitive == "extrusion")
                {
                    _Parameters["height"] = BoundedCSGNumber(_SourceParameters, "height", "extrusion.height", .01, 100000);
                    _Profile = NormalizeCSGProfile(CSGObject(_Feature, "profile", "features.profile"));
                }
                else if (_Primitive == "box")
                {
                    _Parameters["width"] = BoundedCSGNumber(_SourceParameters, "width", "box.width", .01, 100000);
                    _Parameters["depth"] = BoundedCSGNumber(_SourceParameters, "depth", "box.depth", .01, 100000);
                    _Parameters["height"] = BoundedCSGNumber(_SourceParameters, "height", "box.height", .01, 100000);
                }
                else if (_Primitive == "cylinder")
                {
                    _Parameters["radius"] = BoundedCSGNumber(_SourceParameters, "radius", "cylinder.radius", .01, 100000);
                    _Parameters["height"] = BoundedCSGNumber(_SourceParameters, "height", "cylinder.height", .01, 100000);
                }
                else if (_Primitive == "sphere")
                    _Parameters["radius"] = BoundedCSGNumber(_SourceParameters, "radius", "sphere.radius", .01, 100000);
                else
                {
                    const auto _Bottom = BoundedCSGNumber(_SourceParameters, "bottomRadius", "cone.bottomRadius", 0, 100000);
                    const auto _Top = BoundedCSGNumber(_SourceParameters, "topRadius", "cone.topRadius", 0, 100000);
                    if (_Bottom <= 0 && _Top <= 0) throw std::invalid_argument("圆锥至少需要一个非零半径");
                    if (std::abs(_Bottom - _Top) < 1e-9) throw std::invalid_argument("圆锥上下半径不能相同，请改用圆柱体");
                    _Parameters["bottomRadius"] = _Bottom;
                    _Parameters["topRadius"] = _Top;
                    _Parameters["height"] = BoundedCSGNumber(_SourceParameters, "height", "cone.height", .01, 100000);
                }
                const auto _Transform = CSGObject(_Feature, "transform", "features.transform");
                const auto _SourcePosition = CSGObject(_Transform, "position", "features.transform.position");
                const auto _SourceRotation = CSGObject(_Transform, "rotation", "features.transform.rotation");
                ObjectMap _Position, _Rotation;
                for (const auto* _Axis : { "x", "y", "z" })
                {
                    _Position[_Axis] = BoundedCSGNumber(_SourcePosition, _Axis,
                        std::string("position.") + _Axis, -1000000, 1000000, 0, false);
                    _Rotation[_Axis] = BoundedCSGNumber(_SourceRotation, _Axis,
                        std::string("rotation.") + _Axis, -36000, 36000, 0, false);
                }
                ObjectMap _CanonicalFeature{
                    { "id", _ID }, { "name", _Name }, { "primitive", _Primitive }, { "operation", _Operation },
                    { "parameters", std::move(_Parameters) }, { "transform", ObjectMap{
                        { "position", std::move(_Position) }, { "rotation", std::move(_Rotation) } } }
                };
                if (_Primitive == "extrusion") _CanonicalFeature["profile"] = std::move(_Profile);
                _CanonicalFeatures.emplace_back(std::move(_CanonicalFeature));
            }
            return ObjectMap{
                { "schema", std::string("icax.component-csg") }, { "schemaVersion", 1 },
                { "evaluation", std::string("left-fold") }, { "features", std::move(_CanonicalFeatures) }
            };
        }
        TopoDS_Shape EvaluateComponentCSG(const ObjectMap& Definition_)
        {
            const auto _Features = Definition_.at("features").To<VariantArray>();
            TopoDS_Shape _Result;
            for (std::size_t _Index = 0; _Index < _Features.size(); ++_Index)
            {
                const auto _Feature = _Features[_Index].To<ObjectMap>();
                const auto _Primitive = Text(_Feature, "primitive");
                const auto _Parameters = Object(_Feature, "parameters");
                TopoDS_Shape _Shape;
                if (_Primitive == "extrusion")
                    _Shape = BuildCSGProfileExtrusion(Object(_Feature, "profile").at("snapshot").To<ObjectMap>(),
                        CSGNumber(_Parameters.at("height"), "height"));
                else if (_Primitive == "box") _Shape = BRepPrimAPI_MakeBox(
                    gp_Pnt(-CSGNumber(_Parameters.at("width"), "width") / 2,
                        -CSGNumber(_Parameters.at("depth"), "depth") / 2, 0),
                    CSGNumber(_Parameters.at("width"), "width"),
                    CSGNumber(_Parameters.at("depth"), "depth"),
                    CSGNumber(_Parameters.at("height"), "height")).Shape();
                else if (_Primitive == "cylinder") _Shape = BRepPrimAPI_MakeCylinder(
                    gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)),
                    CSGNumber(_Parameters.at("radius"), "radius"),
                    CSGNumber(_Parameters.at("height"), "height")).Shape();
                else if (_Primitive == "sphere")
                {
                    const auto _Radius = CSGNumber(_Parameters.at("radius"), "radius");
                    _Shape = BRepPrimAPI_MakeSphere(gp_Pnt(0, 0, _Radius), _Radius).Shape();
                }
                else _Shape = BRepPrimAPI_MakeCone(
                    gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)),
                    CSGNumber(_Parameters.at("bottomRadius"), "bottomRadius"),
                    CSGNumber(_Parameters.at("topRadius"), "topRadius"),
                    CSGNumber(_Parameters.at("height"), "height")).Shape();

                const auto _Transform = Object(_Feature, "transform");
                const auto _Rotation = Object(_Transform, "rotation");
                for (const auto& [_Axis, _Direction] : std::array<std::pair<const char*, gp_Dir>, 3>{ {
                    { "x", gp_Dir(1, 0, 0) }, { "y", gp_Dir(0, 1, 0) }, { "z", gp_Dir(0, 0, 1) } } })
                {
                    const auto _Degrees = CSGNumber(_Rotation.at(_Axis), std::string("rotation.") + _Axis);
                    if (std::abs(_Degrees) < 1e-12) continue;
                    gp_Trsf _Rotate;
                    _Rotate.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), _Direction), _Degrees * std::acos(-1.0) / 180.0);
                    _Shape = BRepBuilderAPI_Transform(_Shape, _Rotate, true).Shape();
                }
                const auto _Position = Object(_Transform, "position");
                gp_Trsf _Move;
                _Move.SetTranslation(gp_Vec(CSGNumber(_Position.at("x"), "position.x"),
                    CSGNumber(_Position.at("y"), "position.y"), CSGNumber(_Position.at("z"), "position.z")));
                _Shape = BRepBuilderAPI_Transform(_Shape, _Move, true).Shape();

                if (_Index == 0) _Result = _Shape;
                else if (Text(_Feature, "operation") == "union")
                {
                    BRepAlgoAPI_Fuse _Builder(_Result, _Shape); _Builder.Build();
                    if (!_Builder.IsDone()) throw std::runtime_error("CSG 合并运算失败");
                    _Result = _Builder.Shape();
                }
                else if (Text(_Feature, "operation") == "difference")
                {
                    BRepAlgoAPI_Cut _Builder(_Result, _Shape); _Builder.Build();
                    if (!_Builder.IsDone()) throw std::runtime_error("CSG 减去运算失败");
                    _Result = _Builder.Shape();
                }
                else
                {
                    BRepAlgoAPI_Common _Builder(_Result, _Shape); _Builder.Build();
                    if (!_Builder.IsDone()) throw std::runtime_error("CSG 相交运算失败");
                    _Result = _Builder.Shape();
                }
                if (_Result.IsNull() || !TopExp_Explorer(_Result, TopAbs_SOLID).More())
                    throw std::invalid_argument("CSG 第 " + std::to_string(_Index + 1) + " 步产生了空实体");
            }
            if (!BRepCheck_Analyzer(_Result).IsValid()) throw std::invalid_argument("CSG 运算产生了无效实体");
            return _Result;
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
            else if (_Extension == ".iges" || _Extension == ".igs")
            {
                IGESControl_Reader _Reader;
                if (_Reader.ReadFile(PathText(Source_).c_str()) != IFSelect_RetDone || _Reader.TransferRoots() <= 0)
                    throw std::invalid_argument("无法读取 IGES 三维实体");
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

        ObjectMap ManufacturingPartSnapshotFromFile(const std::filesystem::path& Source_)
        {
            auto _Extension = Source_.extension().string();
            std::transform(_Extension.begin(), _Extension.end(), _Extension.begin(), [](unsigned char C_) {
                return static_cast<char>(std::tolower(C_));
            });
            if (_Extension != ".step" && _Extension != ".stp"
                && _Extension != ".iges" && _Extension != ".igs")
                throw std::invalid_argument("请导入 STEP / STP / IGES / IGS 三维实体文件");

            // Linear-part normalization chooses the measured main body axis,
            // then centers the section and puts the stock direction on +X.
            // This is the same coordinate contract used by the nesting solver.
            auto _Shape = NormalizeLinearPartForManufacturing(ShapeFromFile(Source_));
            Bnd_Box _Box;
            BRepBndLib::Add(_Shape, _Box, false);
            _Box.SetGap(0);
            if (_Box.IsVoid() || _Box.IsOpen())
                throw std::invalid_argument("导入零件的几何包络无效");
            double _X0, _Y0, _Z0, _X1, _Y1, _Z1;
            _Box.Get(_X0, _Y0, _Z0, _X1, _Y1, _Z1);
            const auto _BRep = SerializeShape(_Shape);
            return ObjectMap{
                { "schema", std::string("icax.manufacturing-part-import") },
                { "schemaVersion", 1 },
                { "name", PathText(Source_.stem()) },
                { "unit", std::string("mm") },
                { "sourceFileName", PathText(Source_.filename()) },
                { "sourceFormat", _Extension == ".iges" || _Extension == ".igs"
                    ? std::string("iges") : std::string("step") },
                { "brep", _BRep },
                { "geometryDigest", Digest(_BRep) },
                { "bounds", ObjectMap{
                    { "width", _X1 - _X0 }, { "depth", _Y1 - _Y0 }, { "height", _Z1 - _Z0 },
                    { "min", VariantArray{ _X0, _Y0, _Z0 } },
                    { "max", VariantArray{ _X1, _Y1, _Z1 } } } }
            };
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

        // A uniquely created directory keeps the writer away from any existing
        // user file. It lives beside the target, so final publication is a
        // same-volume atomic move. Never recursively remove a user directory.
        class CComponentStepTemporary final
        {
        public:
            explicit CComponentStepTemporary(const std::filesystem::path& Parent_)
            {
                for (int _Attempt = 0; _Attempt < 8; ++_Attempt)
                {
                    auto _Candidate = Parent_ / (".icax-component-step-" +
                        iCAX::Data::to_string(iCAX::Data::GenerateNewUUID()));
                    if (std::filesystem::create_directory(_Candidate))
                    {
                        Directory = std::move(_Candidate);
                        File = Directory / "model.step";
                        return;
                    }
                }
                throw std::runtime_error("无法创建配件 STEP 临时文件夹");
            }
            ~CComponentStepTemporary()
            {
                std::error_code _Ignored;
                std::filesystem::remove(File, _Ignored);
                std::filesystem::remove(Directory, _Ignored);
            }
            CComponentStepTemporary(const CComponentStepTemporary&) = delete;
            CComponentStepTemporary& operator=(const CComponentStepTemporary&) = delete;
            std::filesystem::path Directory;
            std::filesystem::path File;
        };

        std::filesystem::path ComponentStepTarget(const std::filesystem::path& Requested_)
        {
            if (Requested_.empty() || Requested_.native().find(L'\0') != std::wstring::npos)
                throw std::invalid_argument("配件 STEP 导出路径无效");
            const auto _Name = Requested_.filename().native();
            auto _Extension = Requested_.extension().string();
            std::transform(_Extension.begin(), _Extension.end(), _Extension.begin(), [](unsigned char C_) {
                return static_cast<char>(std::tolower(C_));
            });
            if (_Extension != ".step" || _Name.empty()
                || _Name.find_first_of(L"<>:\"/\\|?*") != std::wstring::npos
                || std::any_of(_Name.begin(), _Name.end(), [](wchar_t C_) { return C_ < 32; }))
                throw std::invalid_argument("配件导出文件必须使用有效的 .step 文件名");
            auto _Stem = Requested_.stem().wstring();
            const auto _Dot = _Stem.find(L'.');
            if (_Dot != std::wstring::npos) _Stem.resize(_Dot);
            std::transform(_Stem.begin(), _Stem.end(), _Stem.begin(), [](wchar_t C_) {
                return static_cast<wchar_t>(C_ >= L'a' && C_ <= L'z' ? C_ - L'a' + L'A' : C_);
            });
            if (_Stem == L"CON" || _Stem == L"PRN" || _Stem == L"AUX" || _Stem == L"NUL"
                || (_Stem.size() == 4 && (_Stem.starts_with(L"COM") || _Stem.starts_with(L"LPT"))
                    && _Stem[3] >= L'1' && _Stem[3] <= L'9'))
                throw std::invalid_argument("配件 STEP 文件名不能使用系统保留名称");
            const auto _Absolute = std::filesystem::absolute(Requested_);
            if (!std::filesystem::is_directory(_Absolute.parent_path()))
                throw std::invalid_argument("配件 STEP 导出文件夹不存在");
            const auto _Target = std::filesystem::canonical(_Absolute.parent_path()) / _Absolute.filename();
            std::error_code _Error;
            const auto _Status = std::filesystem::symlink_status(_Target, _Error);
            if (_Error && _Error != std::errc::no_such_file_or_directory)
                throw std::runtime_error("无法检查配件 STEP 目标文件：" + _Error.message());
            // symlink_status also rejects a dangling link, not just live files.
            if (std::filesystem::exists(_Status))
                throw std::invalid_argument("配件 STEP 目标已存在，已停止以避免覆盖");
            return _Target;
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

    TopoDS_Shape ImportMachiningAssemblyFile(const std::filesystem::path& Source_)
    {
        auto _Extension = Source_.extension().string();
        std::transform(_Extension.begin(), _Extension.end(), _Extension.begin(), [](unsigned char C_) {
            return static_cast<char>(std::tolower(C_));
        });
        if (_Extension != ".step" && _Extension != ".stp" && _Extension != ".iges" && _Extension != ".igs")
            throw std::invalid_argument("加工模型仅支持 STEP / STP / IGES / IGS 文件");
        return ShapeFromFile(Source_);
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

    ObjectMap CreateComponentCSGModelSnapshot(const ObjectMap& Definition_, const ObjectMap& Metadata_)
    {
        const auto _Definition = NormalizeComponentCSG(Definition_);
        const auto _Shape = EvaluateComponentCSG(_Definition);
        Bnd_Box _Box;
        BRepBndLib::Add(_Shape, _Box, false);
        _Box.SetGap(0);
        if (_Box.IsVoid() || _Box.IsOpen()) throw std::invalid_argument("CSG 配件的几何包络无效");
        double _X0, _Y0, _Z0, _X1, _Y1, _Z1;
        _Box.Get(_X0, _Y0, _Z0, _X1, _Y1, _Z1);
        const auto _BRep = SerializeShape(_Shape);
        ObjectMap _Snapshot{
            { "schema", std::string("icax.component-model") }, { "schemaVersion", 1 },
            { "name", std::string("未命名配件") }, { "category", std::string("自制配件") },
            { "sourcing", std::string("made") }, { "material", std::string() },
            { "description", std::string() }, { "unit", std::string("mm") },
            { "modelType", std::string("csg") }, { "sourceFormat", std::string("csg") },
            { "sourceFileName", std::string("CSG 绘制模型") }, { "csgDefinition", _Definition },
            { "brep", _BRep }, { "geometryDigest", Digest(_BRep) },
            { "bounds", ObjectMap{ { "width", _X1 - _X0 }, { "depth", _Y1 - _Y0 },
                { "height", _Z1 - _Z0 }, { "min", VariantArray{ _X0, _Y0, _Z0 } },
                { "max", VariantArray{ _X1, _Y1, _Z1 } } } }
        };
        UpdateComponentModelMetadata(_Snapshot, Metadata_);
        // CSG-created models are always self-manufactured library assets.
        _Snapshot["sourcing"] = std::string("made");
        return _Snapshot;
    }

    ObjectMap ImportManufacturingPartFile(const std::filesystem::path& Source_)
    {
        return ManufacturingPartSnapshotFromFile(Source_);
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

    void ExportComponentModelStep(const ObjectMap& Snapshot_, const std::filesystem::path& TargetPath_)
    {
        const auto _Target = ComponentStepTarget(TargetPath_);
        if (Text(Snapshot_, "unit", "mm") != "mm")
            throw std::invalid_argument("配件 STEP 导出要求毫米单位的模型快照");
        // Read only the frozen BRep, never the import path, bounds or display mesh.
        const auto _Shape = ComponentModelShape(Snapshot_);
        if (Snapshot_.contains("geometryDigest")
            && Text(Snapshot_, "geometryDigest") != Digest(Text(Snapshot_, "brep")))
            throw std::invalid_argument("配件模型快照校验失败");
        STEPControl_Writer _Writer;
        // Per-writer settings avoid changing global OCCT import/export units.
        _Writer.Model()->SetLocalLengthUnit(1.0);
        DESTEP_Parameters _Parameters;
        _Parameters.WriteUnit = UnitsMethods_LengthUnit_Millimeter;
        _Parameters.WriteTessellated = DESTEP_Parameters::RWMode_Tessellated_Off;
        if (_Writer.Transfer(_Shape, STEPControl_AsIs, _Parameters) != IFSelect_RetDone)
            throw std::runtime_error("配件真实实体无法转换为 STEP");

        const CComponentStepTemporary _Temporary(_Target.parent_path());
        {
            // filesystem::path keeps Chinese/Unicode directory names intact.
            std::ofstream _Stream(_Temporary.File, std::ios::binary | std::ios::trunc);
            if (!_Stream || _Writer.WriteStream(_Stream) != IFSelect_RetDone)
                throw std::runtime_error("无法写入配件 STEP 文件");
            _Stream.flush();
            if (!_Stream) throw std::runtime_error("配件 STEP 文件写入不完整");
            _Stream.close();
            if (!_Stream) throw std::runtime_error("无法完成配件 STEP 文件写入");
        }
        if (std::filesystem::file_size(_Temporary.File) == 0)
            throw std::runtime_error("配件 STEP 导出产生了空文件");
        // No MOVEFILE_REPLACE_EXISTING: even a competing export that creates
        // the target after our existence check cannot be overwritten.
        if (!::MoveFileExW(_Temporary.File.c_str(), _Target.c_str(), MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("无法发布配件 STEP 文件；目标可能已存在或文件夹不可写（系统错误 "
                + std::to_string(::GetLastError()) + "）");
    }

    ObjectMap ComponentModelSummary(const ObjectMap& Snapshot_, const std::string& Scope_,
        const std::string& ID_, std::uint64_t Revision_)
    {
        ObjectMap _Summary;
        for (const auto* _Key : { "name", "category", "sourcing", "material", "description", "unit",
            "sourceFileName", "bounds", "geometryDigest", "importTranslation", "templateId", "templateName",
            "modelType", "sourceFormat", "csgDefinition" })
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

    ObjectMap LoadTemplateComponentModel(const std::filesystem::path& TemplateDirectory_,
        const ObjectMap& DescriptorExtensions_, const std::string& TemplateID_,
        const std::string& TemplateName_, const std::string& ID_)
    {
        ValidateID(ID_);
        if (TemplateID_.empty()) throw std::invalid_argument("模板配件必须指定所属模板");
        const auto _Resources = Object(DescriptorExtensions_, "modelResources");
        const auto _It = _Resources.find(ID_);
        if (_It == _Resources.end() || !_It->second.Is<ObjectMap>())
            throw std::invalid_argument("模板没有声明此配件资源：" + ID_);
        const auto _Metadata = _It->second.To<ObjectMap>();
        auto _Snapshot = SnapshotFromFile(ConfinedPath(TemplateDirectory_, Text(_Metadata, "path")), _Metadata, false);
        _Snapshot["sourceReference"] = "template:" + ID_;
        _Snapshot["templateId"] = TemplateID_;
        _Snapshot["templateName"] = TemplateName_.empty() ? TemplateID_ : TemplateName_;
        return _Snapshot;
    }

    VariantArray ListTemplateComponentModelSummaries(const std::filesystem::path& TemplateDirectory_,
        const ObjectMap& DescriptorExtensions_, const std::string& TemplateID_, const std::string& TemplateName_)
    {
        VariantArray _Models;
        const auto _Resources = Object(DescriptorExtensions_, "modelResources");
        if (_Resources.size() > 64) throw std::invalid_argument("模板配件模型种类超过 64 种");
        for (const auto& [_ID, _Metadata] : _Resources)
            _Models.emplace_back(ComponentModelSummary(LoadTemplateComponentModel(
                TemplateDirectory_, DescriptorExtensions_, TemplateID_, TemplateName_, _ID), "template", _ID));
        return _Models;
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
                const auto _Template = Object(Document_, "template");
                _Snapshot = LoadTemplateComponentModel(TemplateDirectory_, DescriptorExtensions_,
                    Text(_Template, "id"), Text(_Template, "id"), _Key);
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
