#include "pch.h"
#include "URDFLoader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
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

    std::string _ToLower(IN std::string Text_)
    {
        std::transform(Text_.begin(), Text_.end(), Text_.begin(), [](unsigned char ch_) {
            return static_cast<char>(std::tolower(ch_));
        });
        return Text_;
    }

    std::vector<unsigned char> _ReadFileBytes(IN const std::string& strSourcePath_)
    {
        std::ifstream _File(strSourcePath_, std::ios::binary);
        if (!_File)
        {
            throw std::runtime_error("URDF file cannot be opened: " + strSourcePath_);
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
            throw std::runtime_error("URDF file is empty: " + strSourcePath_);
        }

        HGLOBAL _hGlobal = GlobalAlloc(GMEM_MOVEABLE, _Bytes.size());
        if (!_hGlobal)
        {
            throw std::runtime_error("Failed to allocate URDF read buffer");
        }

        void* _pBuffer = GlobalLock(_hGlobal);
        if (!_pBuffer)
        {
            GlobalFree(_hGlobal);
            throw std::runtime_error("Failed to lock URDF read buffer");
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
                _Stack.back()->Text += _Narrow(_pValue);
            }
        }

        if (_pRoot->Children.empty())
        {
            throw std::runtime_error("URDF file does not contain an XML root element");
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

    double _ToDouble(IN const std::string& strText_, IN double dDefault_ = 0.0)
    {
        if (strText_.empty())
        {
            return dDefault_;
        }
        std::istringstream _Stream(strText_);
        double _Value = dDefault_;
        _Stream >> _Value;
        return _Stream.fail() ? dDefault_ : _Value;
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

    iCAX::CAM::MachinePose6 _ReadOriginPose(IN const SXmlNode& Node_)
    {
        iCAX::CAM::MachinePose6 _Pose{ 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
        if (const auto* _pOrigin = _FirstChild(Node_, "origin"))
        {
            const auto _XYZ = _ParseDoubles(_Attr(*_pOrigin, "xyz"));
            const auto _RPY = _ParseDoubles(_Attr(*_pOrigin, "rpy"));
            for (size_t _Index = 0; _Index < 3 && _Index < _XYZ.size(); ++_Index)
            {
                _Pose[_Index] = _XYZ[_Index];
            }
            for (size_t _Index = 0; _Index < 3 && _Index < _RPY.size(); ++_Index)
            {
                _Pose[_Index + 3] = _RPY[_Index];
            }
        }
        return _Pose;
    }

    ObjectMap _MakeGeometryPayload(IN const SXmlNode& GeometryNode_)
    {
        ObjectMap _Geometry;
        if (const auto* _pBox = _FirstChild(GeometryNode_, "box"))
        {
            _Geometry["type"] = std::string("box");
            _Geometry["size"] = _MakeDoubleArray(_ParseDoubles(_Attr(*_pBox, "size")), 3, { 1.0, 1.0, 1.0 });
            return _Geometry;
        }
        if (const auto* _pCylinder = _FirstChild(GeometryNode_, "cylinder"))
        {
            _Geometry["type"] = std::string("cylinder");
            _Geometry["radius"] = _ToDouble(_Attr(*_pCylinder, "radius"), 0.5);
            _Geometry["length"] = _ToDouble(_Attr(*_pCylinder, "length"), 1.0);
            return _Geometry;
        }
        if (const auto* _pSphere = _FirstChild(GeometryNode_, "sphere"))
        {
            _Geometry["type"] = std::string("sphere");
            _Geometry["radius"] = _ToDouble(_Attr(*_pSphere, "radius"), 0.5);
            return _Geometry;
        }
        if (const auto* _pMesh = _FirstChild(GeometryNode_, "mesh"))
        {
            _Geometry["type"] = std::string("mesh");
            _Geometry["uri"] = _Attr(*_pMesh, "filename", _Attr(*_pMesh, "url"));
            _Geometry["scale"] = _MakeDoubleArray(_ParseDoubles(_Attr(*_pMesh, "scale")), 3, { 1.0, 1.0, 1.0 });
            return _Geometry;
        }

        _Geometry["type"] = GeometryNode_.Children.empty() ? std::string("unknown") : GeometryNode_.Children.front()->Name;
        return _Geometry;
    }

    ObjectMap _MakeMaterialPayload(IN const SXmlNode& VisualNode_)
    {
        ObjectMap _Material;
        _Material["ambient"] = _MakeDoubleArray({}, 4, { 0.65, 0.65, 0.65, 1.0 });
        _Material["diffuse"] = _MakeDoubleArray({}, 4, { 0.65, 0.65, 0.65, 1.0 });
        _Material["specular"] = _MakeDoubleArray({}, 4, { 0.0, 0.0, 0.0, 1.0 });
        _Material["emissive"] = _MakeDoubleArray({}, 4, { 0.0, 0.0, 0.0, 1.0 });
        _Material["lighting"] = true;

        const auto* _pMaterial = _FirstChild(VisualNode_, "material");
        if (!_pMaterial)
        {
            return _Material;
        }

        if (const auto* _pColor = _FirstChild(*_pMaterial, "color"))
        {
            auto _Color = _MakeDoubleArray(_ParseDoubles(_Attr(*_pColor, "rgba")), 4, { 0.65, 0.65, 0.65, 1.0 });
            _Material["ambient"] = _Color;
            _Material["diffuse"] = _Color;
        }
        if (const auto* _pTexture = _FirstChild(*_pMaterial, "texture"))
        {
            auto _TextureURI = _Attr(*_pTexture, "filename", _Attr(*_pTexture, "url"));
            if (!_TextureURI.empty())
            {
                _Material["baseColorTextureUri"] = _TextureURI;
            }
        }
        return _Material;
    }

    ObjectMap _MakeInertialPayload(IN const SXmlNode& InertialNode_)
    {
        ObjectMap _Inertial;
        _Inertial["pose"] = _MakeDoubleArray(std::vector<double>{
            _ReadOriginPose(InertialNode_)[0],
            _ReadOriginPose(InertialNode_)[1],
            _ReadOriginPose(InertialNode_)[2],
            _ReadOriginPose(InertialNode_)[3],
            _ReadOriginPose(InertialNode_)[4],
            _ReadOriginPose(InertialNode_)[5]
        }, 6, {});

        if (const auto* _pMass = _FirstChild(InertialNode_, "mass"))
        {
            _Inertial["mass"] = _ToDouble(_Attr(*_pMass, "value"), 0.0);
        }
        else
        {
            _Inertial["mass"] = 0.0;
        }

        if (const auto* _pInertia = _FirstChild(InertialNode_, "inertia"))
        {
            ObjectMap _Tensor;
            for (const auto& _Name : { "ixx", "ixy", "ixz", "iyy", "iyz", "izz" })
            {
                _Tensor[_Name] = _ToDouble(_Attr(*_pInertia, _Name), 0.0);
            }
            _Inertial["inertia"] = _Tensor;
        }
        return _Inertial;
    }

    std::string _MakeElementID(
        IN const std::string& strKind_,
        IN const std::string& strName_,
        IN size_t nIndex_)
    {
        const auto _Name = strName_.empty() ? std::to_string(nIndex_ + 1) : strName_;
        return strKind_ + ":" + _Name;
    }

    iCAX::CAM::SMachineAxisData _ReadAxisData(IN const SXmlNode& JointNode_)
    {
        iCAX::CAM::SMachineAxisData _Axis;
        if (const auto* _pAxis = _FirstChild(JointNode_, "axis"))
        {
            _Axis.bValid = true;
            _Axis.XYZ = _MakeDoubleArray(_ParseDoubles(_Attr(*_pAxis, "xyz")), 3, { 1.0, 0.0, 0.0 });
        }

        if (const auto* _pLimit = _FirstChild(JointNode_, "limit"))
        {
            _Axis.dLowerLimit = _ToDouble(_Attr(*_pLimit, "lower"), 0.0);
            _Axis.dUpperLimit = _ToDouble(_Attr(*_pLimit, "upper"), 0.0);
            _Axis.dEffort = _ToDouble(_Attr(*_pLimit, "effort"), 0.0);
            _Axis.dVelocity = _ToDouble(_Attr(*_pLimit, "velocity"), 0.0);
        }
        if (const auto* _pDynamics = _FirstChild(JointNode_, "dynamics"))
        {
            _Axis.dDamping = _ToDouble(_Attr(*_pDynamics, "damping"), 0.0);
            _Axis.dFriction = _ToDouble(_Attr(*_pDynamics, "friction"), 0.0);
        }
        return _Axis;
    }

    bool _IsMovableJoint(IN const std::string& strJointType_)
    {
        return strJointType_ == "revolute"
            || strJointType_ == "continuous"
            || strJointType_ == "prismatic";
    }

    std::pair<double, double> _DefaultJointLimits(IN const std::string& strJointType_)
    {
        if (strJointType_ == "prismatic")
        {
            return { -1.0, 1.0 };
        }
        if (strJointType_ == "continuous")
        {
            return { -6.283185307179586, 6.283185307179586 };
        }
        if (strJointType_ == "revolute")
        {
            return { -3.141592653589793, 3.141592653589793 };
        }
        return { 0.0, 0.0 };
    }

    std::string _ChildLinkName(IN const SXmlNode& JointNode_, IN const std::string& strChildName_)
    {
        if (const auto* _pChild = _FirstChild(JointNode_, strChildName_))
        {
            return _Attr(*_pChild, "link");
        }
        return {};
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

    const SXmlNode& _ResolveRobotNode(IN const SXmlNode& Document_)
    {
        const auto* _pRobot = _FirstChild(Document_, "robot");
        if (!_pRobot)
        {
            throw std::runtime_error("URDF XML root <robot> is missing");
        }
        return *_pRobot;
    }
}

bool iCAX::CAM::CanLoadURDFMachineDescription(
    IN const std::string& strSourcePath_)
{
    const auto _Extension = _ToLower(std::filesystem::path(strSourcePath_).extension().string());
    return _Extension == ".urdf";
}

bool iCAX::CAM::LoadURDFMachineDescription(
    IN const std::string& strSourcePath_,
    OUT CMachineDescriptionResource& Description_,
    OUT std::string& strError_)
{
    try
    {
        auto _pDocument = _ParseXml(strSourcePath_);
        const auto& _RobotNode = _ResolveRobotNode(*_pDocument);

        CMachineDescriptionResource _Result;
        _Result.SourcePath = strSourcePath_;
        _Result.SourceDirectory = std::filesystem::path(strSourcePath_).parent_path().string();
        _Result.SourceFormat = "URDF";
        _Result.SourceFormatVersion = {};
        _Result.ModelName = _Attr(_RobotNode, "name", "URDF Robot");
        _Result.bStaticModel = false;

        std::unordered_map<std::string, std::string> _LinkElementIDs;
        auto _LinkNodes = _Children(_RobotNode, "link");
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
            _Element.ParentElementID = {};
            _Element.ElementKind = "part";
            _Element.Name = _LinkName;
            _Element.OriginalName = _LinkName;
            _Element.Pose = {};
            _Element.nSourceIndex = static_cast<uint64_t>(_Index);
            if (const auto* _pInertial = _FirstChild(_LinkNode, "inertial"))
            {
                _Element.Inertial = _MakeInertialPayload(*_pInertial);
            }

            _LinkElementIDs[_LinkName] = _Element.ElementID;

            for (const auto* _pVisual : _Children(_LinkNode, "visual"))
            {
                const auto _VisualIndex = _Element.Visuals.size();
                SMachineVisualData _Visual;
                _Visual.OwnerElementID = _Element.ElementID;
                _Visual.Name = _Attr(*_pVisual, "name", "Visual" + std::to_string(_VisualIndex + 1));
                _Visual.VisualID = _MakeElementID("visual", _Visual.Name, _VisualIndex);
                _Visual.Pose = _ReadOriginPose(*_pVisual);
                if (const auto* _pGeometry = _FirstChild(*_pVisual, "geometry"))
                {
                    _Visual.Geometry = _MakeGeometryPayload(*_pGeometry);
                }
                _Visual.Material = _MakeMaterialPayload(*_pVisual);
                _Element.Visuals.emplace_back(std::move(_Visual));
            }

            for (const auto* _pCollision : _Children(_LinkNode, "collision"))
            {
                const auto _CollisionIndex = _Element.Collisions.size();
                SMachineCollisionData _Collision;
                _Collision.OwnerElementID = _Element.ElementID;
                _Collision.Name = _Attr(*_pCollision, "name", "Collision" + std::to_string(_CollisionIndex + 1));
                _Collision.CollisionID = _MakeElementID("collision", _Collision.Name, _CollisionIndex);
                _Collision.Pose = _ReadOriginPose(*_pCollision);
                if (const auto* _pGeometry = _FirstChild(*_pCollision, "geometry"))
                {
                    _Collision.Geometry = _MakeGeometryPayload(*_pGeometry);
                }
                _Element.Collisions.emplace_back(std::move(_Collision));
            }

            _Result.Elements.emplace_back(std::move(_Element));
        }

        std::unordered_set<std::string> _ChildLinks;
        auto _JointNodes = _Children(_RobotNode, "joint");
        for (size_t _Index = 0; _Index < _JointNodes.size(); ++_Index)
        {
            const auto& _JointNode = *_JointNodes[_Index];
            const auto _ParentName = _ChildLinkName(_JointNode, "parent");
            const auto _ChildName = _ChildLinkName(_JointNode, "child");
            const auto _ParentIter = _LinkElementIDs.find(_ParentName);
            const auto _ChildIter = _LinkElementIDs.find(_ChildName);
            if (_ChildIter == _LinkElementIDs.end())
            {
                continue;
            }

            auto* _pChildElement = _FindElementByID(_Result, _ChildIter->second);
            if (!_pChildElement)
            {
                continue;
            }

            const auto _JointType = _ToLower(_Attr(_JointNode, "type", "fixed"));
            SMachineJointToParentData _Joint;
            _Joint.bValid = true;
            _Joint.Name = _Attr(_JointNode, "name", "Joint" + std::to_string(_Index + 1));
            _Joint.OriginalName = _Joint.Name;
            _Joint.OriginalParentName = _ParentName;
            _Joint.OriginalChildName = _ChildName;
            _Joint.JointType = _JointType;
            _Joint.Pose = _ReadOriginPose(_JointNode);
            _Joint.Axis = _ReadAxisData(_JointNode);
            if (_IsMovableJoint(_JointType) && !_Joint.Axis.bValid)
            {
                _Joint.Axis.bValid = true;
                _Joint.Axis.XYZ = _MakeDoubleArray({ 1.0, 0.0, 0.0 }, 3, {});
            }

            auto [_Lower, _Upper] = _DefaultJointLimits(_JointType);
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

            _pChildElement->ParentElementID = _ParentIter == _LinkElementIDs.end() ? std::string() : _ParentIter->second;
            _pChildElement->JointToParent = _Joint;
            _ChildLinks.insert(_ChildName);
        }

        std::string _RootLinkName;
        for (const auto& _Pair : _LinkElementIDs)
        {
            if (_ChildLinks.find(_Pair.first) == _ChildLinks.end())
            {
                _RootLinkName = _Pair.first;
                break;
            }
        }
        if (_RootLinkName.empty() && !_LinkNodes.empty())
        {
            _RootLinkName = _Attr(*_LinkNodes.front(), "name");
        }
        if (_RootLinkName.empty())
        {
            throw std::runtime_error("URDF robot has no link");
        }

        _Result.RootElementID = _LinkElementIDs[_RootLinkName];
        for (auto& _Element : _Result.Elements)
        {
            if (_Element.ElementID.empty())
            {
                continue;
            }
            if (_Element.ElementID != _Result.RootElementID && _Element.ParentElementID.empty())
            {
                _Element.ParentElementID = _Result.RootElementID;
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
