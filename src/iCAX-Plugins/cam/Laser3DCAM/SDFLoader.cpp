#include "pch.h"
#include "SDFLoader.h"

#include <initializer_list>

#pragma comment(lib, "xmllite.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::Variant;
    using iCAX::Data::VariantArray;

    struct SXmlNode final
    {
        std::string Name;
        std::string Text;
        std::map<std::string, std::string> Attributes;
        std::vector<std::shared_ptr<SXmlNode>> Children;
    };

    std::string _Narrow(IN const wchar_t* pText_)
    {
        if (!pText_)
        {
            return {};
        }
        const int _Length = WideCharToMultiByte(CP_UTF8, 0, pText_, -1, nullptr, 0, nullptr, nullptr);
        if (_Length <= 1)
        {
            return {};
        }
        std::string _Result(static_cast<size_t>(_Length - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, pText_, -1, _Result.data(), _Length, nullptr, nullptr);
        return _Result;
    }

    std::string _Trim(IN std::string Text_)
    {
        auto _IsSpace = [](unsigned char ch_) {
            return std::isspace(ch_) != 0;
        };
        Text_.erase(Text_.begin(), std::find_if_not(Text_.begin(), Text_.end(), _IsSpace));
        Text_.erase(std::find_if_not(Text_.rbegin(), Text_.rend(), _IsSpace).base(), Text_.end());
        return Text_;
    }

    std::vector<unsigned char> _ReadFileBytes(IN const std::string& strSourcePath_)
    {
        std::ifstream _File(strSourcePath_, std::ios::binary);
        if (!_File)
        {
            throw std::runtime_error("SDF file cannot be opened: " + strSourcePath_);
        }
        return std::vector<unsigned char>(
            std::istreambuf_iterator<char>(_File),
            std::istreambuf_iterator<char>());
    }

    struct CComReleaser final
    {
        IUnknown* pValue = nullptr;
        ~CComReleaser()
        {
            if (pValue)
            {
                pValue->Release();
            }
        }

        template <typename T>
        T** Put()
        {
            return reinterpret_cast<T**>(&pValue);
        }

        template <typename T>
        T* Get() const
        {
            return static_cast<T*>(pValue);
        }
    };

    void _CheckHR(IN HRESULT hr_, IN const std::string& strMessage_)
    {
        if (FAILED(hr_))
        {
            std::ostringstream _Stream;
            _Stream << strMessage_ << " HRESULT=0x" << std::hex << static_cast<unsigned long>(hr_);
            throw std::runtime_error(_Stream.str());
        }
    }

    std::shared_ptr<SXmlNode> _ParseXml(IN const std::string& strSourcePath_)
    {
        auto _Bytes = _ReadFileBytes(strSourcePath_);
        if (_Bytes.empty())
        {
            throw std::runtime_error("SDF file is empty: " + strSourcePath_);
        }

        HGLOBAL _hGlobal = GlobalAlloc(GMEM_MOVEABLE, _Bytes.size());
        if (!_hGlobal)
        {
            throw std::runtime_error("Failed to allocate SDF read buffer");
        }

        void* _pBuffer = GlobalLock(_hGlobal);
        if (!_pBuffer)
        {
            GlobalFree(_hGlobal);
            throw std::runtime_error("Failed to lock SDF read buffer");
        }
        std::memcpy(_pBuffer, _Bytes.data(), _Bytes.size());
        GlobalUnlock(_hGlobal);

        CComReleaser _Stream;
        _CheckHR(CreateStreamOnHGlobal(_hGlobal, TRUE, _Stream.Put<IStream>()), "CreateStreamOnHGlobal failed");

        CComReleaser _ReaderHolder;
        _CheckHR(CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void**>(_ReaderHolder.Put<IXmlReader>()), nullptr), "CreateXmlReader failed");
        auto* _pReader = _ReaderHolder.Get<IXmlReader>();
        _CheckHR(_pReader->SetInput(_Stream.Get<IStream>()), "XmlReader SetInput failed");
        (void)_pReader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit);

        auto _pRoot = std::make_shared<SXmlNode>();
        _pRoot->Name = "#document";
        std::vector<std::shared_ptr<SXmlNode>> _Stack;
        _Stack.push_back(_pRoot);

        XmlNodeType _NodeType = XmlNodeType_None;
        while (S_OK == _pReader->Read(&_NodeType))
        {
            if (_NodeType == XmlNodeType_Element)
            {
                const wchar_t* _pName = nullptr;
                _CheckHR(_pReader->GetLocalName(&_pName, nullptr), "XmlReader GetLocalName failed");
                auto _pNode = std::make_shared<SXmlNode>();
                _pNode->Name = _Narrow(_pName);

                if (S_OK == _pReader->MoveToFirstAttribute())
                {
                    do
                    {
                        const wchar_t* _pAttrName = nullptr;
                        const wchar_t* _pAttrValue = nullptr;
                        _CheckHR(_pReader->GetLocalName(&_pAttrName, nullptr), "XmlReader GetLocalName attribute failed");
                        _CheckHR(_pReader->GetValue(&_pAttrValue, nullptr), "XmlReader GetValue attribute failed");
                        _pNode->Attributes[_Narrow(_pAttrName)] = _Narrow(_pAttrValue);
                    } while (S_OK == _pReader->MoveToNextAttribute());
                    _CheckHR(_pReader->MoveToElement(), "XmlReader MoveToElement failed");
                }

                _Stack.back()->Children.push_back(_pNode);
                if (!_pReader->IsEmptyElement())
                {
                    _Stack.push_back(_pNode);
                }
                continue;
            }

            if (_NodeType == XmlNodeType_EndElement)
            {
                if (_Stack.size() > 1)
                {
                    _Stack.pop_back();
                }
                continue;
            }

            if (_NodeType == XmlNodeType_Text || _NodeType == XmlNodeType_CDATA || _NodeType == XmlNodeType_Whitespace)
            {
                const wchar_t* _pValue = nullptr;
                _CheckHR(_pReader->GetValue(&_pValue, nullptr), "XmlReader GetValue text failed");
                if (!_Stack.empty())
                {
                    _Stack.back()->Text += _Narrow(_pValue);
                }
            }
        }

        if (_pRoot->Children.empty())
        {
            throw std::runtime_error("SDF file does not contain an XML root element");
        }
        return _pRoot;
    }

    const SXmlNode* _FirstChild(IN const SXmlNode& Node_, IN const std::string& strName_)
    {
        for (const auto& _pChild : Node_.Children)
        {
            if (_pChild && _pChild->Name == strName_)
            {
                return _pChild.get();
            }
        }
        return nullptr;
    }

    std::vector<const SXmlNode*> _Children(IN const SXmlNode& Node_, IN const std::string& strName_)
    {
        std::vector<const SXmlNode*> _Result;
        for (const auto& _pChild : Node_.Children)
        {
            if (_pChild && _pChild->Name == strName_)
            {
                _Result.push_back(_pChild.get());
            }
        }
        return _Result;
    }

    std::string _Attr(IN const SXmlNode& Node_, IN const std::string& strName_, IN const std::string& strDefault_ = {})
    {
        auto _Iter = Node_.Attributes.find(strName_);
        return _Iter == Node_.Attributes.end() ? strDefault_ : _Iter->second;
    }

    std::string _ChildText(IN const SXmlNode& Node_, IN const std::string& strName_, IN const std::string& strDefault_ = {})
    {
        auto _pChild = _FirstChild(Node_, strName_);
        return _pChild ? _Trim(_pChild->Text) : strDefault_;
    }

    std::string _ChildTextAny(
        IN const SXmlNode& Node_,
        IN std::initializer_list<const char*> Names_,
        IN const std::string& strDefault_ = {})
    {
        for (const auto* _pName : Names_)
        {
            if (!_pName)
            {
                continue;
            }

            auto _Value = _ChildText(Node_, _pName);
            if (!_Value.empty())
            {
                return _Value;
            }
        }
        return strDefault_;
    }

    std::string _FindBaseColorTextureURI(IN const SXmlNode& Node_)
    {
        auto _ToLower = [](std::string Text_) {
            std::transform(Text_.begin(), Text_.end(), Text_.begin(), [](unsigned char ch_) {
                return static_cast<char>(std::tolower(ch_));
            });
            return Text_;
        };

        const auto _Name = _ToLower(Node_.Name);
        if (_Name == "script")
        {
            return {};
        }

        const bool _bTextureField = _Name == "albedo_map"
            || _Name == "diffuse_map"
            || _Name == "base_color_map"
            || _Name == "basecolor_map"
            || _Name == "color_map"
            || _Name == "texture";
        if (_bTextureField)
        {
            auto _Text = _Trim(Node_.Text);
            if (!_Text.empty())
            {
                return _Text;
            }
            for (const auto& _ChildName : { "uri", "filename", "file", "diffuse" })
            {
                _Text = _ChildText(Node_, _ChildName);
                if (!_Text.empty())
                {
                    return _Text;
                }
            }
        }

        for (const auto& _pChild : Node_.Children)
        {
            if (!_pChild)
            {
                continue;
            }
            auto _TextureURI = _FindBaseColorTextureURI(*_pChild);
            if (!_TextureURI.empty())
            {
                return _TextureURI;
            }
        }
        return {};
    }

    bool _ParseBoolText(IN const std::string& strText_, IN bool bDefault_ = false)
    {
        auto _Text = _Trim(strText_);
        std::transform(_Text.begin(), _Text.end(), _Text.begin(), [](unsigned char ch_) {
            return static_cast<char>(std::tolower(ch_));
        });
        if (_Text == "1" || _Text == "true")
        {
            return true;
        }
        if (_Text == "0" || _Text == "false")
        {
            return false;
        }
        return bDefault_;
    }

    std::vector<double> _ParseDoubles(IN const std::string& strText_)
    {
        std::istringstream _Stream(strText_);
        std::vector<double> _Values;
        double _Value = 0.0;
        while (_Stream >> _Value)
        {
            _Values.push_back(_Value);
        }
        return _Values;
    }

    VariantArray _MakeDoubleArray(IN const std::vector<double>& Values_, IN size_t nSize_, IN const std::vector<double>& Default_)
    {
        VariantArray _Result;
        _Result.reserve(nSize_);
        for (size_t _Index = 0; _Index < nSize_; ++_Index)
        {
            const auto _Value = _Index < Values_.size()
                ? Values_[_Index]
                : (_Index < Default_.size() ? Default_[_Index] : 0.0);
            _Result.emplace_back(_Value);
        }
        return _Result;
    }

    VariantArray _ReadPose(IN const SXmlNode& Node_)
    {
        return _MakeDoubleArray(_ParseDoubles(_ChildText(Node_, "pose")), 6, { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 });
    }

    VariantArray _ReadDoubleVectorChild(IN const SXmlNode& Node_, IN const std::string& strName_, IN size_t nSize_, IN const std::vector<double>& Default_)
    {
        return _MakeDoubleArray(_ParseDoubles(_ChildText(Node_, strName_)), nSize_, Default_);
    }

    VariantArray _ReadDoubleVectorChildAny(
        IN const SXmlNode& Node_,
        IN std::initializer_list<const char*> Names_,
        IN size_t nSize_,
        IN const std::vector<double>& Default_)
    {
        for (const auto* _pName : Names_)
        {
            if (!_pName)
            {
                continue;
            }

            auto _Values = _ParseDoubles(_ChildText(Node_, _pName));
            if (!_Values.empty())
            {
                return _MakeDoubleArray(_Values, nSize_, Default_);
            }
        }
        return _MakeDoubleArray({}, nSize_, Default_);
    }

    VariantArray _ScaleDoubleVector(IN const VariantArray& Values_, IN double dScale_)
    {
        VariantArray _Result;
        _Result.reserve(Values_.size());
        for (const auto& _Value : Values_)
        {
            if (_Value.Is<double>())
            {
                _Result.emplace_back(_Value.To<double>() * dScale_);
            }
            else if (_Value.Is<float>())
            {
                _Result.emplace_back(static_cast<double>(_Value.To<float>()) * dScale_);
            }
            else if (_Value.Is<int>())
            {
                _Result.emplace_back(static_cast<double>(_Value.To<int>()) * dScale_);
            }
            else if (_Value.Is<unsigned long long>())
            {
                _Result.emplace_back(static_cast<double>(_Value.To<unsigned long long>()) * dScale_);
            }
            else
            {
                _Result.emplace_back(0.0);
            }
        }
        return _Result;
    }

    VariantArray _NormalizeDirection(IN const VariantArray& Values_)
    {
        std::vector<double> _Direction;
        _Direction.reserve(Values_.size());
        for (const auto& _Value : Values_)
        {
            _Direction.emplace_back(_Value.Is<double>() ? _Value.To<double>() : 0.0);
        }
        _Direction.resize(3, 0.0);

        const auto _Length = std::sqrt(
            _Direction[0] * _Direction[0]
            + _Direction[1] * _Direction[1]
            + _Direction[2] * _Direction[2]);
        if (_Length <= 0.0)
        {
            return _MakeDoubleArray({ 0.0, 0.0, -1.0 }, 3, {});
        }
        return _MakeDoubleArray({
            _Direction[0] / _Length,
            _Direction[1] / _Length,
            _Direction[2] / _Length
        }, 3, {});
    }

    double _ReadDoubleChild(IN const SXmlNode& Node_, IN const std::string& strName_, IN double dDefault_ = 0.0)
    {
        const auto _Values = _ParseDoubles(_ChildText(Node_, strName_));
        return _Values.empty() ? dDefault_ : _Values.front();
    }

    ObjectMap _MakeGeometryPayload(IN const SXmlNode& GeometryNode_)
    {
        ObjectMap _Geometry;

        if (const auto* _pBox = _FirstChild(GeometryNode_, "box"))
        {
            _Geometry["type"] = std::string("box");
            _Geometry["size"] = _ReadDoubleVectorChild(*_pBox, "size", 3, { 1.0, 1.0, 1.0 });
            return _Geometry;
        }
        if (const auto* _pCylinder = _FirstChild(GeometryNode_, "cylinder"))
        {
            _Geometry["type"] = std::string("cylinder");
            _Geometry["radius"] = _ReadDoubleChild(*_pCylinder, "radius", 0.5);
            _Geometry["length"] = _ReadDoubleChild(*_pCylinder, "length", 1.0);
            return _Geometry;
        }
        if (const auto* _pSphere = _FirstChild(GeometryNode_, "sphere"))
        {
            _Geometry["type"] = std::string("sphere");
            _Geometry["radius"] = _ReadDoubleChild(*_pSphere, "radius", 0.5);
            return _Geometry;
        }
        if (const auto* _pPlane = _FirstChild(GeometryNode_, "plane"))
        {
            _Geometry["type"] = std::string("plane");
            _Geometry["normal"] = _ReadDoubleVectorChild(*_pPlane, "normal", 3, { 0.0, 0.0, 1.0 });
            _Geometry["size"] = _ReadDoubleVectorChild(*_pPlane, "size", 2, { 1.0, 1.0 });
            return _Geometry;
        }
        if (const auto* _pMesh = _FirstChild(GeometryNode_, "mesh"))
        {
            _Geometry["type"] = std::string("mesh");
            _Geometry["uri"] = _ChildText(*_pMesh, "uri");
            _Geometry["scale"] = _ReadDoubleVectorChild(*_pMesh, "scale", 3, { 1.0, 1.0, 1.0 });
            return _Geometry;
        }

        _Geometry["type"] = GeometryNode_.Children.empty() ? std::string("unknown") : GeometryNode_.Children.front()->Name;
        return _Geometry;
    }

    ObjectMap _MakeMaterialPayload(IN const SXmlNode& MaterialNode_)
    {
        ObjectMap _Material;
        _Material["ambient"] = _ReadDoubleVectorChild(MaterialNode_, "ambient", 4, { 0.65, 0.65, 0.65, 1.0 });
        _Material["diffuse"] = _ReadDoubleVectorChild(MaterialNode_, "diffuse", 4, { 0.65, 0.65, 0.65, 1.0 });
        _Material["specular"] = _ReadDoubleVectorChild(MaterialNode_, "specular", 4, { 0.0, 0.0, 0.0, 1.0 });
        _Material["emissive"] = _ReadDoubleVectorChild(MaterialNode_, "emissive", 4, { 0.0, 0.0, 0.0, 1.0 });
        _Material["lighting"] = _ParseBoolText(_ChildText(MaterialNode_, "lighting", "1"), true);
        if (auto _TextureURI = _FindBaseColorTextureURI(MaterialNode_); !_TextureURI.empty())
        {
            _Material["baseColorTextureUri"] = _TextureURI;
        }

        if (const auto* _pScript = _FirstChild(MaterialNode_, "script"))
        {
            ObjectMap _Script;
            VariantArray _Uris;
            for (const auto* _pUri : _Children(*_pScript, "uri"))
            {
                _Uris.emplace_back(_Trim(_pUri->Text));
            }
            _Script["uris"] = _Uris;
            _Script["name"] = _ChildText(*_pScript, "name");
            _Material["script"] = _Script;
        }
        return _Material;
    }

    ObjectMap _MakeInertialPayload(IN const SXmlNode& InertialNode_)
    {
        ObjectMap _Inertial;
        _Inertial["pose"] = _ReadPose(InertialNode_);
        _Inertial["mass"] = _ReadDoubleChild(InertialNode_, "mass", 0.0);
        if (const auto* _pInertia = _FirstChild(InertialNode_, "inertia"))
        {
            ObjectMap _Tensor;
            for (const auto& _Name : { "ixx", "ixy", "ixz", "iyy", "iyz", "izz" })
            {
                _Tensor[_Name] = _ReadDoubleChild(*_pInertia, _Name, 0.0);
            }
            _Inertial["inertia"] = _Tensor;
        }
        return _Inertial;
    }

    ObjectMap _MakeIncludePayload(IN const SXmlNode& IncludeNode_)
    {
        ObjectMap _Include;
        _Include["uri"] = _ChildText(IncludeNode_, "uri");
        _Include["name"] = _ChildText(IncludeNode_, "name");
        _Include["pose"] = _ReadPose(IncludeNode_);
        return _Include;
    }

    ObjectMap _MakeFramePayload(IN const SXmlNode& FrameNode_)
    {
        ObjectMap _Frame;
        _Frame["name"] = _Attr(FrameNode_, "name");
        _Frame["attachedTo"] = _Attr(FrameNode_, "attached_to");
        _Frame["pose"] = _ReadPose(FrameNode_);
        return _Frame;
    }

    ObjectMap _MakePluginPayload(IN const SXmlNode& PluginNode_)
    {
        ObjectMap _Plugin;
        _Plugin["name"] = _Attr(PluginNode_, "name");
        _Plugin["filename"] = _Attr(PluginNode_, "filename");
        _Plugin["childCount"] = static_cast<unsigned long long>(PluginNode_.Children.size());
        for (const auto& _pChild : PluginNode_.Children)
        {
            if (!_pChild || _pChild->Name.empty())
            {
                continue;
            }

            const auto _Text = _Trim(_pChild->Text);
            if (_Text.empty())
            {
                _Plugin[_pChild->Name] = std::string();
                continue;
            }

            const auto _Values = _ParseDoubles(_Text);
            _Plugin[_pChild->Name] = _Values.size() == 1 ? Variant(_Values.front()) : Variant(_Text);
        }
        return _Plugin;
    }

    const SXmlNode& _ResolveModelNode(IN const SXmlNode& Document_)
    {
        const auto* _pSDF = _FirstChild(Document_, "sdf");
        if (!_pSDF)
        {
            throw std::runtime_error("SDF XML root <sdf> is missing");
        }
        const auto* _pModel = _FirstChild(*_pSDF, "model");
        if (!_pModel)
        {
            throw std::runtime_error("SDF <model> is missing");
        }
        return *_pModel;
    }

    std::string _ToLower(IN std::string Text_)
    {
        std::transform(Text_.begin(), Text_.end(), Text_.begin(), [](unsigned char ch_) {
            return static_cast<char>(std::tolower(ch_));
        });
        return Text_;
    }

    std::string _MakeElementID(
        IN const std::string& strKind_,
        IN const std::string& strName_,
        IN size_t nIndex_,
        IN const std::string& strParentName_ = {})
    {
        auto _Name = strName_.empty() ? std::to_string(nIndex_ + 1) : strName_;
        if (!strParentName_.empty())
        {
            _Name = strParentName_ + "/" + _Name;
        }
        return strKind_ + ":" + _Name;
    }

    iCAX::CAM::MachinePose6 _ReadPose6(IN const SXmlNode& Node_)
    {
        const auto _Values = _ParseDoubles(_ChildText(Node_, "pose"));
        iCAX::CAM::MachinePose6 _Pose{ 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
        for (size_t _Index = 0; _Index < _Pose.size() && _Index < _Values.size(); ++_Index)
        {
            _Pose[_Index] = _Values[_Index];
        }
        return _Pose;
    }

    bool _IsMovableJoint(IN const std::string& strJointType_)
    {
        return !strJointType_.empty() && strJointType_ != "fixed";
    }

    std::pair<double, double> _DefaultJointLimits(IN const std::string& strJointType_)
    {
        if (strJointType_ == "prismatic")
        {
            return { -1000.0, 1000.0 };
        }
        if (strJointType_ == "continuous")
        {
            return { -6.283185307179586, 6.283185307179586 };
        }
        return { -3.141592653589793, 3.141592653589793 };
    }

    iCAX::CAM::SMachineAxisData _ReadAxisData(IN const SXmlNode* pAxisNode_)
    {
        iCAX::CAM::SMachineAxisData _Axis;
        if (!pAxisNode_)
        {
            return _Axis;
        }

        _Axis.bValid = true;
        _Axis.XYZ = _ReadDoubleVectorChild(*pAxisNode_, "xyz", 3, { 0.0, 0.0, 1.0 });
        _Axis.bUseParentModelFrame = _ParseBoolText(_ChildText(*pAxisNode_, "use_parent_model_frame"), false);
        _Axis.ExpressedIn = _Attr(*pAxisNode_, "expressed_in");

        if (const auto* _pLimit = _FirstChild(*pAxisNode_, "limit"))
        {
            _Axis.dLowerLimit = _ReadDoubleChild(*_pLimit, "lower", 0.0);
            _Axis.dUpperLimit = _ReadDoubleChild(*_pLimit, "upper", 0.0);
            _Axis.dEffort = _ReadDoubleChild(*_pLimit, "effort", 0.0);
            _Axis.dVelocity = _ReadDoubleChild(*_pLimit, "velocity", 0.0);
        }

        if (const auto* _pDynamics = _FirstChild(*pAxisNode_, "dynamics"))
        {
            _Axis.dDamping = _ReadDoubleChild(*_pDynamics, "damping", 0.0);
            _Axis.dFriction = _ReadDoubleChild(*_pDynamics, "friction", 0.0);
        }
        return _Axis;
    }

    iCAX::CAM::SMachineToolData _ReadToolData(IN const SXmlNode& LinkNode_, IN const std::string& strDefaultName_)
    {
        auto _IsToolPlugin = [](IN const SXmlNode& PluginNode_) {
            const auto _Name = _ToLower(_Attr(PluginNode_, "name"));
            const auto _FileName = _ToLower(_Attr(PluginNode_, "filename"));
            return _FileName == "icax.machine.tool"
                || _FileName == "icax.tool.tcp"
                || _Name == "icax_machine_tool"
                || _Name == "icax_tool_tcp"
                || _Name.find("tool") != std::string::npos
                || _Name.find("tcp") != std::string::npos;
        };

        for (const auto* _pPlugin : _Children(LinkNode_, "plugin"))
        {
            if (!_pPlugin || !_IsToolPlugin(*_pPlugin))
            {
                continue;
            }

            iCAX::CAM::SMachineToolData _Tool;
            _Tool.bValid = true;
            _Tool.Name = _ChildTextAny(*_pPlugin, { "tool_name", "toolName", "name" }, strDefaultName_);
            if (_Tool.Name.empty())
            {
                _Tool.Name = strDefaultName_;
            }
            _Tool.ToolType = _ChildTextAny(*_pPlugin, { "tool_type", "toolType", "type" }, "laser_cutting_head");

            _Tool.TcpLocalPosition = _ReadDoubleVectorChildAny(
                *_pPlugin,
                { "tcp_position_mm", "tcpLocalPositionMm", "tcp_mm", "tcp" },
                3,
                { 0.0, 0.0, 0.0 });
            if (!_ChildTextAny(*_pPlugin, { "tcp_position_m", "tcpLocalPositionM", "tcp_m" }).empty())
            {
                _Tool.TcpLocalPosition = _ScaleDoubleVector(
                    _ReadDoubleVectorChildAny(*_pPlugin, { "tcp_position_m", "tcpLocalPositionM", "tcp_m" }, 3, { 0.0, 0.0, 0.0 }),
                    1000.0);
            }

            _Tool.BeamLocalDirection = _NormalizeDirection(_ReadDoubleVectorChildAny(
                *_pPlugin,
                { "beam_direction", "beamDirection", "beam_local_direction", "beamLocalDirection" },
                3,
                { 0.0, 0.0, -1.0 }));
            return _Tool;
        }

        return {};
    }

    void _AppendElement(
        IN OUT iCAX::CAM::CMachineDescriptionResource& Description_,
        IN const std::string& strElementID_,
        IN const std::string& strParentElementID_,
        IN const std::string& strElementKind_,
        IN const std::string& strName_,
        IN const std::string& strOriginalName_,
        IN const iCAX::CAM::MachinePose6& Pose_,
        IN uint64_t nSourceIndex_)
    {
        iCAX::CAM::SMachineElementData _Element;
        _Element.ElementID = strElementID_;
        _Element.ParentElementID = strParentElementID_;
        _Element.ElementKind = strElementKind_;
        _Element.Name = strName_;
        _Element.OriginalName = strOriginalName_;
        _Element.Pose = Pose_;
        _Element.nSourceIndex = nSourceIndex_;
        Description_.Elements.emplace_back(std::move(_Element));
    }

    iCAX::CAM::SMachineElementData* _FindElementByID(
        IN OUT iCAX::CAM::CMachineDescriptionResource& Description_,
        IN const std::string& strElementID_)
    {
        for (auto& _Element : Description_.Elements)
        {
            if (_Element.ElementID == strElementID_)
            {
                return &_Element;
            }
        }
        return nullptr;
    }
}

bool iCAX::CAM::CanLoadSDFMachineDescription(
    IN const std::string& strSourcePath_)
{
    const auto _Extension = _ToLower(std::filesystem::path(strSourcePath_).extension().string());
    return _Extension == ".sdf" || _Extension == ".xml";
}

bool iCAX::CAM::LoadSDFMachineDescription(
    IN const std::string& strSourcePath_,
    OUT CMachineDescriptionResource& Description_,
    OUT std::string& strError_)
{
    try
    {
        auto _pDocument = _ParseXml(strSourcePath_);
        const auto* _pSDF = _FirstChild(*_pDocument, "sdf");
        if (!_pSDF)
        {
            throw std::runtime_error("SDF XML root <sdf> is missing");
        }

        const auto& _ModelNode = _ResolveModelNode(*_pDocument);
        CMachineDescriptionResource _Result;
        _Result.SourcePath = strSourcePath_;
        _Result.SourceDirectory = std::filesystem::path(strSourcePath_).parent_path().string();
        _Result.SourceFormat = "SDF";
        _Result.SourceFormatVersion = _Attr(*_pSDF, "version");
        _Result.ModelName = _Attr(_ModelNode, "name");
        _Result.bStaticModel = _ParseBoolText(_ChildText(_ModelNode, "static"), false);
        _Result.RootElementID = _MakeElementID("model", _Result.ModelName, 0);
        _AppendElement(_Result, _Result.RootElementID, {}, "model", _Result.ModelName, _Result.ModelName, _ReadPose6(_ModelNode), 0);

        for (const auto* _pInclude : _Children(_ModelNode, "include"))
        {
            _Result.Includes.emplace_back(_MakeIncludePayload(*_pInclude));
        }
        for (const auto* _pFrame : _Children(_ModelNode, "frame"))
        {
            _Result.Frames.emplace_back(_MakeFramePayload(*_pFrame));
        }
        for (const auto* _pPlugin : _Children(_ModelNode, "plugin"))
        {
            _Result.Plugins.emplace_back(_MakePluginPayload(*_pPlugin));
        }

        std::unordered_map<std::string, std::string> _LinkElementIDs;
        auto _LinkNodes = _Children(_ModelNode, "link");
        for (size_t _Index = 0; _Index < _LinkNodes.size(); ++_Index)
        {
            const auto& _LinkNode = *_LinkNodes[_Index];
            auto _LinkName = _Attr(_LinkNode, "name");
            if (_LinkName.empty())
            {
                _LinkName = "Link" + std::to_string(_Index + 1);
            }

            SMachineElementData _Element;
            _Element.ElementID = _MakeElementID("part", _LinkName, _Index);
            _Element.ParentElementID = _Result.RootElementID;
            _Element.ElementKind = "part";
            _Element.Name = _LinkName;
            _Element.OriginalName = _LinkName;
            _Element.Pose = _ReadPose6(_LinkNode);
            _Element.bSelfCollide = _ParseBoolText(_ChildText(_LinkNode, "self_collide"), false);
            _Element.bGravity = _ParseBoolText(_ChildText(_LinkNode, "gravity", "1"), true);
            _Element.bKinematic = _ParseBoolText(_ChildText(_LinkNode, "kinematic"), false);
            _Element.nSourceIndex = static_cast<uint64_t>(_Index);
            if (const auto* _pInertial = _FirstChild(_LinkNode, "inertial"))
            {
                _Element.Inertial = _MakeInertialPayload(*_pInertial);
            }
            _Element.Tool = _ReadToolData(_LinkNode, _LinkName);

            _LinkElementIDs[_LinkName] = _Element.ElementID;

            for (const auto* _pVisual : _Children(_LinkNode, "visual"))
            {
                const auto _VisualIndex = _Element.Visuals.size();
                SMachineVisualData _Visual;
                _Visual.OwnerElementID = _Element.ElementID;
                _Visual.Name = _Attr(*_pVisual, "name");
                if (_Visual.Name.empty())
                {
                    _Visual.Name = "Visual" + std::to_string(_VisualIndex + 1);
                }
                _Visual.VisualID = _MakeElementID("visual", _Visual.Name, _VisualIndex, _LinkName);
                _Visual.Pose = _ReadPose6(*_pVisual);
                _Visual.bCastShadows = _ParseBoolText(_ChildText(*_pVisual, "cast_shadows", "1"), true);
                _Visual.dTransparency = _ReadDoubleChild(*_pVisual, "transparency", 0.0);
                if (const auto* _pGeometry = _FirstChild(*_pVisual, "geometry"))
                {
                    _Visual.Geometry = _MakeGeometryPayload(*_pGeometry);
                }
                if (const auto* _pMaterial = _FirstChild(*_pVisual, "material"))
                {
                    _Visual.Material = _MakeMaterialPayload(*_pMaterial);
                }

                _Element.Visuals.emplace_back(std::move(_Visual));
            }

            for (const auto* _pCollision : _Children(_LinkNode, "collision"))
            {
                const auto _CollisionIndex = _Element.Collisions.size();
                SMachineCollisionData _Collision;
                _Collision.OwnerElementID = _Element.ElementID;
                _Collision.Name = _Attr(*_pCollision, "name");
                if (_Collision.Name.empty())
                {
                    _Collision.Name = "Collision" + std::to_string(_CollisionIndex + 1);
                }
                _Collision.CollisionID = _MakeElementID("collision", _Collision.Name, _CollisionIndex, _LinkName);
                _Collision.Pose = _ReadPose6(*_pCollision);
                if (const auto* _pGeometry = _FirstChild(*_pCollision, "geometry"))
                {
                    _Collision.Geometry = _MakeGeometryPayload(*_pGeometry);
                }

                _Element.Collisions.emplace_back(std::move(_Collision));
            }

            _Result.Elements.emplace_back(std::move(_Element));
        }

        auto _JointNodes = _Children(_ModelNode, "joint");
        for (size_t _Index = 0; _Index < _JointNodes.size(); ++_Index)
        {
            const auto& _JointNode = *_JointNodes[_Index];
            SMachineJointToParentData _Joint;
            _Joint.Name = _Attr(_JointNode, "name");
            if (_Joint.Name.empty())
            {
                _Joint.Name = "Joint" + std::to_string(_Index + 1);
            }
            _Joint.bValid = true;
            _Joint.JointType = _Attr(_JointNode, "type");
            _Joint.Pose = _ReadPose6(_JointNode);
            _Joint.OriginalName = _Joint.Name;
            _Joint.OriginalParentName = _ChildText(_JointNode, "parent");
            _Joint.OriginalChildName = _ChildText(_JointNode, "child");
            auto _ParentIter = _LinkElementIDs.find(_Joint.OriginalParentName);
            auto _ChildIter = _LinkElementIDs.find(_Joint.OriginalChildName);
            const auto _ParentElementID = _ParentIter == _LinkElementIDs.end() ? _Result.RootElementID : _ParentIter->second;
            const auto _ChildElementID = _ChildIter == _LinkElementIDs.end() ? std::string() : _ChildIter->second;
            _Joint.Axis = _ReadAxisData(_FirstChild(_JointNode, "axis"));
            _Joint.Axis2 = _ReadAxisData(_FirstChild(_JointNode, "axis2"));
            auto [_Lower, _Upper] = _IsMovableJoint(_Joint.JointType)
                ? _DefaultJointLimits(_Joint.JointType)
                : std::pair<double, double>{ 0.0, 0.0 };
            if (_Joint.Axis.bValid)
            {
                _Lower = _Joint.Axis.dLowerLimit;
                _Upper = _Joint.Axis.dUpperLimit;
            }
            if (_Upper < _Lower)
            {
                std::swap(_Lower, _Upper);
            }
            _Joint.dLowerLimit = _Lower;
            _Joint.dUpperLimit = _Upper;

            if (auto* _pChildElement = _FindElementByID(_Result, _ChildElementID))
            {
                _pChildElement->ParentElementID = _ParentElementID;
                _pChildElement->JointToParent = _Joint;
            }
        }

        Description_ = std::move(_Result);
        strError_.clear();
        return true;
    }
    catch (const std::exception& _Error)
    {
        strError_ = _Error.what();
        return false;
    }
}
