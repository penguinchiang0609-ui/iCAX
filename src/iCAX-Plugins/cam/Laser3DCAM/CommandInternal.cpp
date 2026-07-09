#include "pch.h"
#include "CommandInternal.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <initializer_list>

namespace iCAX::CAM::Commands::Internal
{
    std::vector<uint8_t> _EncodePayload(IN const Variant& Payload_)
    {
        const auto _Text = iCAX::Data::VariantSerializer::Serialize(Payload_);
        return std::vector<uint8_t>(_Text.begin(), _Text.end());
    }

    Variant _DecodePayload(IN const std::vector<uint8_t>& Payload_)
    {
        if (Payload_.empty())
        {
            return Variant(ObjectMap{});
        }

        const std::string _Text(Payload_.begin(), Payload_.end());
        return iCAX::Data::VariantSerializer::Deserialize(_Text);
    }

    ObjectMap _DecodeObjectPayload(IN const iCAX::Command::CCommandRequest& Request_)
    {
        auto _Payload = _DecodePayload(Request_.Payload);
        if (!_Payload.Is<ObjectMap>())
        {
            throw std::invalid_argument("Cam command payload must be an object");
        }
        return _Payload.To<ObjectMap>();
    }

    iCAX::Command::CCommandResponse _MakeResponse(IN const Variant& Payload_)
    {
        iCAX::Command::CCommandResponse _Response;
        _Response.nStatus = iCAX::Command::ECommandStatusCode::Ok;
        _Response.Payload = _EncodePayload(Payload_);
        return _Response;
    }

    iCAX::Project::ISceneContext& _RequireSceneContext(IN iCAX::Project::ISceneContext* pSceneContext_)
    {
        if (!pSceneContext_)
        {
            throw std::invalid_argument("Cam command must be sent to a scene mailbox");
        }
        return *pSceneContext_;
    }

    iCAX::Product::IProductContext& _RequireProductContext(IN iCAX::Product::IProductContext* pProductContext_)
    {
        if (!pProductContext_)
        {
            throw std::invalid_argument("Cam command requires product context");
        }
        return *pProductContext_;
    }

    iCAX::Project::IProjectContext& _RequireProjectContext(IN iCAX::Project::IProjectContext* pProjectContext_)
    {
        if (!pProjectContext_)
        {
            throw std::invalid_argument("Cam command requires project context");
        }
        return *pProjectContext_;
    }

    unsigned long long _ToUInt64(IN const Variant& Value_, IN const std::string& strFieldName_)
    {
        if (Value_.Is<unsigned long long>())
        {
            return Value_.To<unsigned long long>();
        }
        if (Value_.Is<long long>())
        {
            const auto _Value = Value_.To<long long>();
            if (_Value < 0)
            {
                throw std::invalid_argument("Cam payload field cannot be negative: " + strFieldName_);
            }
            return static_cast<unsigned long long>(_Value);
        }
        if (Value_.Is<unsigned int>())
        {
            return static_cast<unsigned long long>(Value_.To<unsigned int>());
        }
        if (Value_.Is<int>())
        {
            const auto _Value = Value_.To<int>();
            if (_Value < 0)
            {
                throw std::invalid_argument("Cam payload field cannot be negative: " + strFieldName_);
            }
            return static_cast<unsigned long long>(_Value);
        }
        if (Value_.Is<double>())
        {
            const auto _Value = Value_.To<double>();
            if (_Value < 0.0)
            {
                throw std::invalid_argument("Cam payload field cannot be negative: " + strFieldName_);
            }
            return static_cast<unsigned long long>(_Value);
        }
        if (Value_.Is<std::string>())
        {
            return std::stoull(Value_.To<std::string>());
        }
        throw std::invalid_argument("Cam payload field must be numeric: " + strFieldName_);
    }

    double _ToDouble(IN const Variant& Value_, IN const std::string& strFieldName_)
    {
        if (Value_.Is<double>())
        {
            return Value_.To<double>();
        }
        if (Value_.Is<float>())
        {
            return static_cast<double>(Value_.To<float>());
        }
        if (Value_.Is<unsigned long long>())
        {
            return static_cast<double>(Value_.To<unsigned long long>());
        }
        if (Value_.Is<long long>())
        {
            return static_cast<double>(Value_.To<long long>());
        }
        if (Value_.Is<unsigned int>())
        {
            return static_cast<double>(Value_.To<unsigned int>());
        }
        if (Value_.Is<int>())
        {
            return static_cast<double>(Value_.To<int>());
        }
        if (Value_.Is<std::string>())
        {
            return std::stod(Value_.To<std::string>());
        }
        throw std::invalid_argument("Cam payload field must be numeric: " + strFieldName_);
    }

    std::string _GetOptionalString(
        IN const ObjectMap& Payload_,
        IN const std::string& strName_,
        IN const std::string& strDefault_)
    {
        auto _Iter = Payload_.find(strName_);
        if (_Iter == Payload_.end() || _Iter->second.Is<std::monostate>())
        {
            return strDefault_;
        }
        if (!_Iter->second.Is<std::string>())
        {
            throw std::invalid_argument("Cam payload field must be a string: " + strName_);
        }
        return _Iter->second.To<std::string>();
    }

    unsigned long long _GetOptionalUInt64(
        IN const ObjectMap& Payload_,
        IN const std::string& strName_,
        IN unsigned long long nDefault_)
    {
        auto _Iter = Payload_.find(strName_);
        if (_Iter == Payload_.end() || _Iter->second.Is<std::monostate>())
        {
            return nDefault_;
        }
        return _ToUInt64(_Iter->second, strName_);
    }

    double _GetOptionalDouble(
        IN const ObjectMap& Payload_,
        IN const std::string& strName_,
        IN double dDefault_)
    {
        auto _Iter = Payload_.find(strName_);
        if (_Iter == Payload_.end() || _Iter->second.Is<std::monostate>())
        {
            return dDefault_;
        }
        return _ToDouble(_Iter->second, strName_);
    }

    VariantArray _GetOptionalVariantArray(
        IN const ObjectMap& Payload_,
        IN const std::string& strName_,
        IN const VariantArray& Default_)
    {
        auto _Iter = Payload_.find(strName_);
        if (_Iter == Payload_.end() || _Iter->second.Is<std::monostate>())
        {
            return Default_;
        }
        if (!_Iter->second.Is<VariantArray>())
        {
            throw std::invalid_argument("Cam payload field must be an array: " + strName_);
        }
        return _Iter->second.To<VariantArray>();
    }

    std::shared_ptr<iCAX::Database::CComponentBase> _GetOrCreateComponent(
        IN iCAX::Database::IRepository& Repository_,
        IN const std::string& strComponentClass_)
    {
        auto _pMetaEntity = Repository_.GetMetaEntity();
        if (!_pMetaEntity)
        {
            throw std::runtime_error("Cam command requires repository meta entity");
        }

        auto _pComponent = _pMetaEntity->GetComponent(strComponentClass_);
        if (_pComponent)
        {
            return _pComponent;
        }

        std::string _strError;
        if (!_pMetaEntity->AddComponent(strComponentClass_, _pComponent, _strError) || !_pComponent)
        {
            throw std::runtime_error(_strError.empty() ? "Cam component attach failed: " + strComponentClass_ : _strError);
        }
        return _pComponent;
    }


    bool _SetStringProperty(
        IN const std::shared_ptr<iCAX::Database::CComponentBase>& pComponent_,
        IN const std::string& strPropertyName_,
        IN const std::string& strValue_)
    {
        std::string _strError;
        if (!pComponent_->SetProperty(strPropertyName_, iCAX::Data::PropertyValue(strValue_), _strError))
        {
            throw std::runtime_error(_strError.empty() ? "Cam property set failed: " + strPropertyName_ : _strError);
        }
        return true;
    }

    bool _SetUInt64Property(
        IN const std::shared_ptr<iCAX::Database::CComponentBase>& pComponent_,
        IN const std::string& strPropertyName_,
        IN unsigned long long nValue_)
    {
        std::string _strError;
        if (!pComponent_->SetProperty(strPropertyName_, iCAX::Data::PropertyValue(nValue_), _strError))
        {
            throw std::runtime_error(_strError.empty() ? "Cam property set failed: " + strPropertyName_ : _strError);
        }
        return true;
    }

    bool _SetBoolProperty(
        IN const std::shared_ptr<iCAX::Database::CComponentBase>& pComponent_,
        IN const std::string& strPropertyName_,
        IN bool bValue_)
    {
        std::string _strError;
        if (!pComponent_->SetProperty(strPropertyName_, iCAX::Data::PropertyValue(bValue_), _strError))
        {
            throw std::runtime_error(_strError.empty() ? "Cam property set failed: " + strPropertyName_ : _strError);
        }
        return true;
    }

    bool _SetUuidProperty(
        IN const std::shared_ptr<iCAX::Database::CComponentBase>& pComponent_,
        IN const std::string& strPropertyName_,
        IN const iCAX::Data::uuid& Value_)
    {
        std::string _strError;
        if (!pComponent_->SetProperty(strPropertyName_, iCAX::Data::PropertyValue(Value_), _strError))
        {
            throw std::runtime_error(_strError.empty() ? "Cam property set failed: " + strPropertyName_ : _strError);
        }
        return true;
    }

    bool _SetDoubleProperty(
        IN const std::shared_ptr<iCAX::Database::CComponentBase>& pComponent_,
        IN const std::string& strPropertyName_,
        IN double dValue_)
    {
        std::string _strError;
        if (!pComponent_->SetProperty(strPropertyName_, iCAX::Data::PropertyValue(dValue_), _strError))
        {
            throw std::runtime_error(_strError.empty() ? "Cam property set failed: " + strPropertyName_ : _strError);
        }
        return true;
    }

    bool _SetVariantArrayProperty(
        IN const std::shared_ptr<iCAX::Database::CComponentBase>& pComponent_,
        IN const std::string& strPropertyName_,
        IN const VariantArray& Value_)
    {
        std::string _strError;
        if (!pComponent_->SetProperty(strPropertyName_, iCAX::Data::PropertyValue(Value_), _strError))
        {
            throw std::runtime_error(_strError.empty() ? "Cam property set failed: " + strPropertyName_ : _strError);
        }
        return true;
    }

    std::string _GetObjectString(
        IN const ObjectMap& Object_,
        IN const std::string& strName_,
        IN const std::string& strDefault_)
    {
        auto _Iter = Object_.find(strName_);
        if (_Iter == Object_.end() || _Iter->second.Is<std::monostate>())
        {
            return strDefault_;
        }
        if (!_Iter->second.Is<std::string>())
        {
            return strDefault_;
        }
        return _Iter->second.To<std::string>();
    }

    unsigned long long _GetObjectUInt64(
        IN const ObjectMap& Object_,
        IN const std::string& strName_,
        IN unsigned long long nDefault_)
    {
        auto _Iter = Object_.find(strName_);
        if (_Iter == Object_.end() || _Iter->second.Is<std::monostate>())
        {
            return nDefault_;
        }
        return _ToUInt64(_Iter->second, strName_);
    }

    double _GetObjectDouble(
        IN const ObjectMap& Object_,
        IN const std::string& strName_,
        IN double nDefault_)
    {
        auto _Iter = Object_.find(strName_);
        if (_Iter == Object_.end() || _Iter->second.Is<std::monostate>())
        {
            return nDefault_;
        }
        return _ToDouble(_Iter->second, strName_);
    }

    std::string _UuidToString(IN const iCAX::Data::uuid& ID_)
    {
        return ID_.is_nil() ? std::string() : iCAX::Data::to_string(ID_);
    }

    iCAX::Data::uuid _ParseRequiredUuid(IN const std::string& strValue_, IN const std::string& strFieldName_)
    {
        auto _Parsed = iCAX::Data::uuid::from_string(strValue_);
        if (!_Parsed.has_value() || _Parsed->is_nil())
        {
            throw std::invalid_argument("Cam payload field must be a valid uuid: " + strFieldName_);
        }
        return *_Parsed;
    }

    VariantArray _NormalizeNumericArray(
        IN const ObjectMap& Object_,
        IN const std::string& strFieldName_,
        IN size_t nExpectedSize_)
    {
        auto _Iter = Object_.find(strFieldName_);
        if (_Iter == Object_.end() || !_Iter->second.Is<VariantArray>())
        {
            throw std::invalid_argument("Cam pose sample field must be an array: " + strFieldName_);
        }

        const auto _Input = _Iter->second.To<VariantArray>();
        if (_Input.size() != nExpectedSize_)
        {
            throw std::invalid_argument("Cam pose sample field has invalid size: " + strFieldName_);
        }

        VariantArray _Result;
        _Result.reserve(nExpectedSize_);
        for (size_t _Index = 0; _Index < _Input.size(); ++_Index)
        {
            _Result.emplace_back(_ToDouble(_Input[_Index], strFieldName_ + "[" + std::to_string(_Index) + "]"));
        }
        return _Result;
    }

    iCAX::GeometryData::Direction3 _ReadDirection3(
        IN const ObjectMap& Object_,
        IN const std::string& strFieldName_)
    {
        const auto _Values = _NormalizeNumericArray(Object_, strFieldName_, 3);
        iCAX::GeometryData::Direction3 _Direction;
        _Direction.X = _Values[0].To<double>();
        _Direction.Y = _Values[1].To<double>();
        _Direction.Z = _Values[2].To<double>();
        return _Direction;
    }

    VariantArray _MakeStringArray(IN std::initializer_list<const char*> Values_)
    {
        VariantArray _Result;
        _Result.reserve(Values_.size());
        for (const auto* _Value : Values_)
        {
            _Result.emplace_back(std::string(_Value));
        }
        return _Result;
    }

    VariantArray _MakeDoubleArray(IN std::initializer_list<double> Values_)
    {
        VariantArray _Result;
        _Result.reserve(Values_.size());
        for (const auto _Value : Values_)
        {
            _Result.emplace_back(_Value);
        }
        return _Result;
    }

    std::vector<double> _VariantArrayToDoubles(IN const VariantArray& Values_, IN const std::string& strFieldName_)
    {
        std::vector<double> _Result;
        _Result.reserve(Values_.size());
        for (size_t _Index = 0; _Index < Values_.size(); ++_Index)
        {
            _Result.emplace_back(_ToDouble(Values_[_Index], strFieldName_ + "[" + std::to_string(_Index) + "]"));
        }
        return _Result;
    }

    VariantArray _DoublesToVariantArray(IN const std::vector<double>& Values_)
    {
        VariantArray _Result;
        _Result.reserve(Values_.size());
        for (const auto _Value : Values_)
        {
            _Result.emplace_back(_Value);
        }
        return _Result;
    }

    VariantArray _NormalizeDoubleArray(
        IN const VariantArray& Values_,
        IN const std::string& strFieldName_,
        IN size_t nExpectedSize_)
    {
        if (Values_.size() != nExpectedSize_)
        {
            throw std::invalid_argument("Cam payload field has invalid array size: " + strFieldName_);
        }
        return _DoublesToVariantArray(_VariantArrayToDoubles(Values_, strFieldName_));
    }

    VariantArray _NormalizeDirectionArray(IN const VariantArray& Values_, IN const std::string& strFieldName_)
    {
        auto _Direction = _VariantArrayToDoubles(_NormalizeDoubleArray(Values_, strFieldName_, 3), strFieldName_);
        const auto _Length = std::sqrt(
            _Direction[0] * _Direction[0]
            + _Direction[1] * _Direction[1]
            + _Direction[2] * _Direction[2]);
        if (_Length <= 0.0)
        {
            throw std::invalid_argument("Cam payload direction cannot be zero: " + strFieldName_);
        }
        return _MakeDoubleArray({ _Direction[0] / _Length, _Direction[1] / _Length, _Direction[2] / _Length });
    }

    iCAX::CAM::SPoseSample _NormalizePoseSample(IN const Variant& Sample_, IN size_t nSampleIndex_)
    {
        if (!Sample_.Is<ObjectMap>())
        {
            throw std::invalid_argument("Cam pose sample must be an object at index: " + std::to_string(nSampleIndex_));
        }

        const auto _Input = Sample_.To<ObjectMap>();
        iCAX::CAM::SPoseSample _Sample;
        _Sample.nSegmentIndex = _GetObjectUInt64(_Input, "segmentIndex");
        _Sample.dSegmentU = _GetObjectDouble(_Input, "segmentU");
        _Sample.BeamDirection = _ReadDirection3(_Input, "beamDirection");
        return _Sample;
    }

    std::vector<iCAX::CAM::SPoseSample> _NormalizePoseSamples(IN const VariantArray& Samples_)
    {
        std::vector<iCAX::CAM::SPoseSample> _Result;
        _Result.reserve(Samples_.size());
        for (size_t _Index = 0; _Index < Samples_.size(); ++_Index)
        {
            _Result.emplace_back(_NormalizePoseSample(Samples_[_Index], _Index));
        }
        return _Result;
    }

    std::string _GetDisplayNameFromPath(IN const std::string& strSourcePath_)
    {
        if (strSourcePath_.empty())
        {
            return {};
        }

        std::filesystem::path _Path(strSourcePath_);
        auto _Name = _Path.filename().string();
        return _Name.empty() ? strSourcePath_ : _Name;
    }

    std::string _ToLowerASCII(IN std::string strText_)
    {
        std::transform(strText_.begin(), strText_.end(), strText_.begin(), [](unsigned char ch_) {
            return static_cast<char>(std::tolower(ch_));
        });
        return strText_;
    }

    bool _IsSupportedWorkpieceModelPath(IN const std::string& strSourcePath_)
    {
        const auto _Extension = _ToLowerASCII(std::filesystem::path(strSourcePath_).extension().string());
        return _Extension == ".step" || _Extension == ".stp" || _Extension == ".igs" || _Extension == ".iges";
    }

    bool _IsSupportedMachineDescriptionPath(IN const std::string& strSourcePath_)
    {
        const auto _Extension = _ToLowerASCII(std::filesystem::path(strSourcePath_).extension().string());
        return _Extension == ".sdf" || _Extension == ".xml";
    }

    iCAX::Resource::CResourceInfo _MakeResourceInfo(
        IN const std::string& strSource_,
        IN const std::string& strName_,
        IN const std::string& strKind_,
        IN iCAX::Resource::EResourcePersistenceMode Persistence_,
        IN uint64_t nVersion_)
    {
        iCAX::Resource::CResourceInfo _Info;
        _Info.Source = strSource_;
        _Info.Name = strName_;
        _Info.Persistence = Persistence_;
        _Info.nVersion = nVersion_;
        _Info.Metadata["kind"] = strKind_;
        return _Info;
    }

    uint64_t _NextResourceVersion(IN iCAX::Resource::CResourceLibrary& Resources_, IN const std::string& strResourceID_)
    {
        const auto _PreviousVersion = Resources_.GetVersion(strResourceID_);
        return _PreviousVersion == 0 ? 1 : _PreviousVersion + 1;
    }

}
