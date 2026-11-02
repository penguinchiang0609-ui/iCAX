// Engineering values belong to persistent component properties. The envelope
// carries a revision for asynchronous fencing, never a ResourceManager handle.
    Variant EngineeringAssets(const Variant& value,bool stored) {
        if(value.Is<Map>()){
            auto object=value.To<Map>();
            if(const auto ref=iCAX::ProjectFile::TryGetResourceReferenceValue(object))return stored?Variant(object):Variant(Map{{"url",ref->URL},{"version",std::to_string(ref->nVersion)}});
            if(object.contains("url")&&object.contains("version")&&object.at("url").Is<Text>()&&object.at("url").To<Text>().starts_with("resource://")){
                if(stored)return iCAX::ProjectFile::MakeResourceReferenceValue({object.at("url").To<Text>(),std::stoull(object.at("version").To<Text>())});
                return object;
            }
            for(auto& [_,child]:object)child=EngineeringAssets(child,stored);return object;
        }
        if(value.Is<Array>()){auto array=value.To<Array>();for(auto& child:array)child=EngineeringAssets(child,stored);return array;}
        return value;
    }
    Map EngineeringData(const Map& value){return {{"revision",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"value",EngineeringAssets(value,true)}};}
    Map EngineeringBody(const Map& data,const Text& schema={}){
        if(data.empty())return {};
        if(!data.contains("revision")||!data.contains("value"))throw std::invalid_argument("CAM_ENGINEERING_DATA_INVALID");
        auto value=EngineeringAssets(data.at("value"),false).To<Map>();
        if(!schema.empty()&&(!value.contains("schema")||value.at("schema").To<Text>()!=schema||value.at("schemaVersion").To<Text>()!="1"))throw std::invalid_argument("CAM_ENGINEERING_DATA_SCHEMA_MISMATCH");
        return value;
    }
    Map EngineeringIdentity(const Map& data){return data.empty()?Map{}:Map{{"revision",data.at("revision")}};}
    Text EngineeringDigest(const Variant& value){
        const auto bytes=iCAX::Data::VariantSerializer::Serialize(value);if(bytes.size()>std::numeric_limits<ULONG>::max())throw std::invalid_argument("CAM_ENGINEERING_DATA_TOO_LARGE");unsigned char hash[32]{};
        if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),static_cast<ULONG>(bytes.size()),hash,sizeof(hash))<0)throw std::runtime_error("CAM_ENGINEERING_DIGEST_FAILED");
        constexpr char hex[]="0123456789abcdef";Text result;for(auto byte:hash){result+=hex[byte>>4];result+=hex[byte&15];}return result;
    }
    Variant EngineeringSignature(const Map& data){
        if(data.empty())return Map{};const auto digest=EngineeringDigest(data.at("value"));
        return Map{{"revision",data.at("revision")},{"digest",digest}};
    }
    Text EngineeringBytes(std::span<const uint8_t> bytes);
    std::vector<uint8_t> EngineeringBytes(const Text& text);
    Map IntentCurveData(const iCAX::CAM::Intent::CIntentCurveResource& curve){
        iCAX::GeometryData::Persistence::Writer writer;writer.Write(curve.Curve2);writer.Write(curve.Curve3);
        return EngineeringData({{"schema",Text("cam.intent-curve-data")},{"schemaVersion",Text("1")},{"version",std::to_string(curve.Version)},
            {"hasCurve2",curve.HasCurve2},{"hasCurve3",curve.HasCurve3},{"parameterization",curve.Parameterization},{"bytes",EngineeringBytes(writer.Bytes)}});
    }
    std::shared_ptr<const iCAX::CAM::Intent::CIntentCurveResource> ReadIntentCurveData(const Map& data){
        const auto value=EngineeringBody(data,"cam.intent-curve-data");auto curve=std::make_shared<iCAX::CAM::Intent::CIntentCurveResource>();
        const auto bytes=EngineeringBytes(value.at("bytes").To<Text>());iCAX::GeometryData::Persistence::Reader reader(bytes);reader.Read(curve->Curve2);reader.Read(curve->Curve3);if(!reader.End())throw std::invalid_argument("CAM_ENGINEERING_CURVE_INVALID");
        curve->Version=std::stoull(value.at("version").To<Text>());curve->HasCurve2=value.at("hasCurve2").To<bool>();curve->HasCurve3=value.at("hasCurve3").To<bool>();curve->Parameterization=value.at("parameterization").To<Text>();return curve;
    }
    Text EngineeringBytes(std::span<const uint8_t> bytes){
        constexpr char hex[]="0123456789abcdef";Text text(bytes.size()*2,'0');for(std::size_t i=0;i<bytes.size();++i){text[2*i]=hex[bytes[i]>>4];text[2*i+1]=hex[bytes[i]&15];}return text;
    }
    std::vector<uint8_t> EngineeringBytes(const Text& text){
        if(text.size()%2)throw std::runtime_error("CAM_ENGINEERING_BYTES_INVALID");const auto digit=[](char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::runtime_error("CAM_ENGINEERING_BYTES_INVALID");};
        std::vector<uint8_t> bytes(text.size()/2);for(std::size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<uint8_t>(digit(text[2*i])*16+digit(text[2*i+1]));return bytes;
    }
