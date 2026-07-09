#include "pch.h"
#include "SDFMachineParser.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <unknwn.h>
#include <objidl.h>
#include <xmllite.h>

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

    ObjectMap _MakeVisualOrCollisionPayload(
        IN const SXmlNode& Node_,
        IN const std::string& strLinkName_,
        IN bool bVisual_)
    {
        ObjectMap _Payload;
        _Payload["name"] = _Attr(Node_, "name");
        _Payload["linkName"] = strLinkName_;
        _Payload["pose"] = _ReadPose(Node_);
        _Payload["castShadows"] = _ParseBoolText(_ChildText(Node_, "cast_shadows", "1"), true);
        _Payload["transparency"] = _ReadDoubleChild(Node_, "transparency", 0.0);

        if (const auto* _pGeometry = _FirstChild(Node_, "geometry"))
        {
            _Payload["geometry"] = _MakeGeometryPayload(*_pGeometry);
        }
        if (bVisual_)
        {
            if (const auto* _pMaterial = _FirstChild(Node_, "material"))
            {
                _Payload["material"] = _MakeMaterialPayload(*_pMaterial);
            }
        }
        return _Payload;
    }

    ObjectMap _MakeAxisPayload(IN const SXmlNode& AxisNode_)
    {
        ObjectMap _Axis;
        _Axis["xyz"] = _ReadDoubleVectorChild(AxisNode_, "xyz", 3, { 0.0, 0.0, 1.0 });
        _Axis["useParentModelFrame"] = _ParseBoolText(_ChildText(AxisNode_, "use_parent_model_frame"), false);
        _Axis["expressedIn"] = _Attr(AxisNode_, "expressed_in");

        if (const auto* _pLimit = _FirstChild(AxisNode_, "limit"))
        {
            ObjectMap _Limit;
            _Limit["lower"] = _ReadDoubleChild(*_pLimit, "lower", 0.0);
            _Limit["upper"] = _ReadDoubleChild(*_pLimit, "upper", 0.0);
            _Limit["effort"] = _ReadDoubleChild(*_pLimit, "effort", 0.0);
            _Limit["velocity"] = _ReadDoubleChild(*_pLimit, "velocity", 0.0);
            _Axis["limit"] = _Limit;
        }

        if (const auto* _pDynamics = _FirstChild(AxisNode_, "dynamics"))
        {
            ObjectMap _Dynamics;
            _Dynamics["damping"] = _ReadDoubleChild(*_pDynamics, "damping", 0.0);
            _Dynamics["friction"] = _ReadDoubleChild(*_pDynamics, "friction", 0.0);
            _Dynamics["springReference"] = _ReadDoubleChild(*_pDynamics, "spring_reference", 0.0);
            _Dynamics["springStiffness"] = _ReadDoubleChild(*_pDynamics, "spring_stiffness", 0.0);
            _Axis["dynamics"] = _Dynamics;
        }
        return _Axis;
    }

    ObjectMap _MakeJointPayload(IN const SXmlNode& JointNode_)
    {
        ObjectMap _Joint;
        _Joint["name"] = _Attr(JointNode_, "name");
        _Joint["type"] = _Attr(JointNode_, "type");
        _Joint["pose"] = _ReadPose(JointNode_);
        _Joint["parent"] = _ChildText(JointNode_, "parent");
        _Joint["child"] = _ChildText(JointNode_, "child");
        if (const auto* _pAxis = _FirstChild(JointNode_, "axis"))
        {
            _Joint["axis"] = _MakeAxisPayload(*_pAxis);
        }
        if (const auto* _pAxis2 = _FirstChild(JointNode_, "axis2"))
        {
            _Joint["axis2"] = _MakeAxisPayload(*_pAxis2);
        }
        return _Joint;
    }

    ObjectMap _MakeLinkPayload(
        IN const SXmlNode& LinkNode_,
        IN OUT VariantArray& Visuals_,
        IN OUT VariantArray& Collisions_,
        IN OUT VariantArray& Materials_)
    {
        const auto _LinkName = _Attr(LinkNode_, "name");
        ObjectMap _Link;
        _Link["name"] = _LinkName;
        _Link["pose"] = _ReadPose(LinkNode_);
        _Link["selfCollide"] = _ParseBoolText(_ChildText(LinkNode_, "self_collide"), false);
        _Link["gravity"] = _ParseBoolText(_ChildText(LinkNode_, "gravity", "1"), true);
        _Link["kinematic"] = _ParseBoolText(_ChildText(LinkNode_, "kinematic"), false);

        if (const auto* _pInertial = _FirstChild(LinkNode_, "inertial"))
        {
            _Link["inertial"] = _MakeInertialPayload(*_pInertial);
        }

        VariantArray _LinkVisuals;
        for (const auto* _pVisual : _Children(LinkNode_, "visual"))
        {
            auto _Visual = _MakeVisualOrCollisionPayload(*_pVisual, _LinkName, true);
            _LinkVisuals.emplace_back(_Visual);
            Visuals_.emplace_back(_Visual);
            if (_Visual.find("material") != _Visual.end())
            {
                ObjectMap _Material = _Visual["material"].To<ObjectMap>();
                _Material["visualName"] = _Visual["name"];
                _Material["linkName"] = _LinkName;
                Materials_.emplace_back(_Material);
            }
        }
        _Link["visuals"] = _LinkVisuals;

        VariantArray _LinkCollisions;
        for (const auto* _pCollision : _Children(LinkNode_, "collision"))
        {
            auto _Collision = _MakeVisualOrCollisionPayload(*_pCollision, _LinkName, false);
            _LinkCollisions.emplace_back(_Collision);
            Collisions_.emplace_back(_Collision);
        }
        _Link["collisions"] = _LinkCollisions;
        return _Link;
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
}

bool iCAX::CAM::ParseSDFMachineDocument(
    IN const std::string& strSourcePath_,
    OUT SSDFMachineDocument& Document_,
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
        SSDFMachineDocument _Result;
        _Result.SDFVersion = _Attr(*_pSDF, "version");
        _Result.ModelName = _Attr(_ModelNode, "name");
        _Result.bStaticModel = _ParseBoolText(_ChildText(_ModelNode, "static"), false);

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
        for (const auto* _pLink : _Children(_ModelNode, "link"))
        {
            _Result.Links.emplace_back(_MakeLinkPayload(*_pLink, _Result.Visuals, _Result.Collisions, _Result.Materials));
        }
        for (const auto* _pJoint : _Children(_ModelNode, "joint"))
        {
            _Result.Joints.emplace_back(_MakeJointPayload(*_pJoint));
        }

        Document_ = std::move(_Result);
        strError_.clear();
        return true;
    }
    catch (const std::exception& _Error)
    {
        strError_ = _Error.what();
        return false;
    }
}
