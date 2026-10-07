#include "IssuerCore.h"
#include "IssuerCrypto.h"
#include "LicenseFiles.h"
#include "TpmTransport.h"
#include "TrialRuntime.h"
#include "LicenseBundle.h"
#include <wincrypt.h>
#include <algorithm>
#include <chrono>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <variant>

namespace tube::license::issuer_native {
namespace {
std::string Hex(std::span<const unsigned char> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (auto byte : bytes) { result.push_back(digits[byte>>4]); result.push_back(digits[byte&15]); }
    return result;
}
bool IsDigest(std::string_view text) {
    return text.size()==64 && std::all_of(text.begin(),text.end(),[](char c) {
        return (c>='0'&&c<='9')||(c>='a'&&c<='f');
    });
}
bool EqualBytes(std::span<const unsigned char> left,std::span<const unsigned char> right){
    return left.size()==right.size()&&std::equal(left.begin(),left.end(),right.begin());
}
void Identifier(std::string_view value) {
    Require(!value.empty() && value.size()<=128 && std::all_of(value.begin(),value.end(),[](unsigned char c) {
        return c>=33&&c<=126;
    }),"Invalid issuer identifier");
}
using JsonValue=std::variant<std::string,std::uint64_t>;
class ConfigParser final {
    std::string_view raw_;std::size_t offset_{};
    void Space(){while(offset_<raw_.size()&&(raw_[offset_]==' '||raw_[offset_]=='\t'||raw_[offset_]=='\r'||raw_[offset_]=='\n'))++offset_;}
    char Take(){Require(offset_<raw_.size(),"Truncated issuer configuration");return raw_[offset_++];}
    void Expect(char c){Space();Require(Take()==c,"Invalid issuer configuration");}
    std::string String(){
        Expect('"');std::string result;
        while(true){const auto c=static_cast<unsigned char>(Take());if(c=='"')return result;
            Require(c>=32,"Invalid configuration string");
            if(c!='\\')result.push_back(static_cast<char>(c));else{
                const auto e=Take();
                if(e=='"'||e=='\\'||e=='/')result.push_back(e);
                else throw std::runtime_error("Unsupported configuration string escape");
            }
            Require(result.size()<=1024,"Configuration string too large");
        }
    }
    std::uint64_t Number(){
        Space();std::uint64_t value{};const auto start=offset_;
        while(offset_<raw_.size()&&raw_[offset_]>='0'&&raw_[offset_]<='9'){
            const auto digit=static_cast<unsigned>(raw_[offset_++]-'0');
            Require(value<=((std::numeric_limits<std::uint64_t>::max)()-digit)/10,"Configuration number overflow");value=value*10+digit;
        }
        Require(offset_>start&&(offset_-start==1||raw_[start]!='0'),"Invalid configuration number");return value;
    }
public:
    explicit ConfigParser(std::string_view raw):raw_(raw){}
    std::map<std::string,JsonValue> Parse(){
        Expect('{');std::map<std::string,JsonValue> fields;
        while(true){auto name=String();Expect(':');Space();
            JsonValue value=offset_<raw_.size()&&raw_[offset_]=='"'?JsonValue(String()):JsonValue(Number());
            Require(fields.emplace(std::move(name),std::move(value)).second,"Duplicate issuer configuration field");
            Space();const auto separator=Take();if(separator=='}')break;Require(separator==',',"Invalid configuration separator");
        }
        Space();Require(offset_==raw_.size(),"Trailing issuer configuration");return fields;
    }
};
struct Configuration final{std::filesystem::path folder;std::string issuerId;Bytes publicKey;const ProductDescriptor* product;};
Configuration ReadConfiguration(const ProductDescriptor& product,const std::filesystem::path& store){
    Require(std::filesystem::is_directory(store),"Issuer store is not a directory");const auto folder=std::filesystem::canonical(store);
    Require(!std::filesystem::exists(folder/L"BACKUP_INCOMPLETE"),"Cannot use incomplete issuer backup");
    const auto data=ReadFileBounded(folder/L"issuer.json",4096);const std::string text(data.begin(),data.end());const auto fields=ConfigParser(text).Parse();
    Require(fields.size()==3&&fields.contains("format")&&fields.contains("issuer_id")&&fields.contains("public_sha256"),"Invalid issuer configuration fields");
    Require(std::holds_alternative<std::uint64_t>(fields.at("format"))&&std::get<std::uint64_t>(fields.at("format"))==1
        &&std::holds_alternative<std::string>(fields.at("issuer_id"))&&std::holds_alternative<std::string>(fields.at("public_sha256")),"Invalid issuer configuration types");
    const auto id=std::get<std::string>(fields.at("issuer_id"));Identifier(id);auto publicKey=ReadFileBounded(folder/L"issuer-public.blob",72);ValidatePublicBlob(publicKey);
    const auto digest=std::get<std::string>(fields.at("public_sha256"));Require(IsDigest(digest)&&Hex(Hash(publicKey))==digest,"Issuer public key does not match configuration");
    Require(id==product.issuerId&&publicKey.size()==product.publicKey.size()
        &&std::equal(publicKey.begin(),publicKey.end(),product.publicKey.begin()),"Issuer store is not trusted for the selected product");
    Require(std::filesystem::is_regular_file(folder/L"issuer-private.pem"),"Encrypted signing key is missing");return{folder,id,std::move(publicKey),&product};
}
struct Request final{
    RequestInfo info;Digest digest{};Bytes akArea,qualified,ekArea,devicePublic,nvArea;
    std::uint64_t initial{};std::vector<Bytes> certificates;
};
Request ParseRequest(const ProductDescriptor& product,std::span<const unsigned char> raw){
    Require(raw.size()>8&&raw.size()<=65536,"Invalid enrollment size");Reader r(raw);
    const auto magic=r.Take(8);const auto sameMagic=[&](std::string_view value){return value.size()==magic.size()&&std::equal(magic.begin(),magic.end(),value.begin());};
    const bool trial=sameMagic(product.trialRequestFormat);
    Require(trial||sameMagic(product.permanentRequestFormat),"Unknown enrollment format");Require(r.Text(128)==product.id,"Wrong enrollment product");
    const auto challenge=r.Field(32);Require(challenge.size()==32,"Invalid enrollment nonce");Digest nonce{};std::copy(challenge.begin(),challenge.end(),nonce.begin());
    Request request;request.akArea=r.Field(256);request.qualified=r.Field(68);request.ekArea=r.Field(1024);
    // Issuance requires the exact primary template, not merely a restricted key.
    TpmReader ak(request.akArea);Require(ak.U16()==0x23&&ak.U16()==11&&ak.U32()==0x50072,"Unexpected enrollment AK template");
    const auto domain=product.attestationPrimaryDomain;
    const auto expectedPolicy=Hash({reinterpret_cast<const unsigned char*>(domain.data()),domain.size()});const auto policy=ak.Sized(32);
    Require(policy.size()==32&&std::equal(policy.begin(),policy.end(),expectedPolicy.begin()),"Unexpected enrollment AK policy");
    request.devicePublic=AttestationPublicKey(request.akArea);
    Bytes hierarchy;Append32(hierarchy,0x40000001);const auto name=TpmSha256Name(request.akArea);hierarchy.insert(hierarchy.end(),name.begin(),name.end());
    const auto qualifiedDigest=Hash(hierarchy);Bytes qualified{0,11};qualified.insert(qualified.end(),qualifiedDigest.begin(),qualifiedDigest.end());
    Require(request.qualified==qualified,"Enrollment AK qualified name does not match owner primary");crypto::RsaEkPublicBlob(request.ekArea);
    const auto count=r.U32();Require(count>=1&&count<=8,"Missing or excessive EK certificates");
    for(std::uint32_t i=0;i<count;++i)request.certificates.push_back(r.Field(8192));
    const tpm::Evidence evidence{r.Field(1024),r.Field(72)};tpm::VerifyQuote(request.akArea,request.qualified,nonce,evidence);
    if(trial){
        request.nvArea=r.Field(128);request.initial=r.U64();Require(request.nvArea.size()==14&&request.initial>0
            &&request.initial<=(std::numeric_limits<std::uint64_t>::max)()-720,"Invalid trial counter");TrialIndex(request.nvArea);
        const auto attest=r.Field(1024),signature=r.Field(72);const NvBinding binding{request.akArea,request.qualified,request.nvArea,request.initial};
        Require(VerifyNvEvidence(binding,nonce,attest,signature).Value()==request.initial,"Trial initial counter differs from signed evidence");
    }
    Require(r.End(),"Trailing enrollment data");request.digest=Hash(raw);request.info.digest=Hex(request.digest);request.info.isTrial=trial;request.info.deviceHash=Hex(Hash(request.ekArea));return request;
}
struct CertContext final{
    PCCERT_CONTEXT value{};
    explicit CertContext(const Bytes& der):value(CertCreateCertificateContext(X509_ASN_ENCODING,der.data(),static_cast<DWORD>(der.size()))){Require(value!=nullptr,"Invalid manufacturer certificate");}
    ~CertContext(){if(value)CertFreeCertificateContext(value);}
    CertContext(const CertContext&)=delete;
};
struct CertStore final{
    HCERTSTORE value{CertOpenStore(CERT_STORE_PROV_MEMORY,0,0,CERT_STORE_CREATE_NEW_FLAG,nullptr)};
    CertStore(){Require(value!=nullptr,"Cannot create offline certificate store");}
    ~CertStore(){CertCloseStore(value,0);}
    void Add(const Bytes& der){Require(CertAddEncodedCertificateToStore(value,X509_ASN_ENCODING,der.data(),static_cast<DWORD>(der.size()),CERT_STORE_ADD_USE_EXISTING,nullptr),"Invalid manufacturer certificate");}
};
void CheckCa(PCCERT_CONTEXT cert,bool root){
    const auto ext=CertFindExtension(szOID_BASIC_CONSTRAINTS2,cert->pCertInfo->cExtension,cert->pCertInfo->rgExtension);Require(ext!=nullptr,"Manufacturer CA lacks basic constraints");
    CERT_BASIC_CONSTRAINTS2_INFO* basic{};DWORD size{};
    Require(CryptDecodeObjectEx(X509_ASN_ENCODING,X509_BASIC_CONSTRAINTS2,ext->Value.pbData,ext->Value.cbData,CRYPT_DECODE_ALLOC_FLAG,nullptr,&basic,&size),"Invalid CA constraints");
    const bool ca=basic->fCA!=0;LocalFree(basic);Require(ca,"Manufacturer issuer is not a CA");
    BYTE usage{};Require(CertGetIntendedKeyUsage(X509_ASN_ENCODING,cert->pCertInfo,&usage,1)&&(usage&CERT_KEY_CERT_SIGN_KEY_USAGE),"CA lacks certificate signing usage");
    if(root)Require(CertCompareCertificateName(X509_ASN_ENCODING,&cert->pCertInfo->Subject,&cert->pCertInfo->Issuer)
        &&CryptVerifyCertificateSignatureEx(0,X509_ASN_ENCODING,CRYPT_VERIFY_CERT_SIGN_SUBJECT_CERT,const_cast<PCERT_CONTEXT>(cert),CRYPT_VERIFY_CERT_SIGN_ISSUER_CERT,const_cast<PCERT_CONTEXT>(cert),0,nullptr),"Manufacturer root is not self-signed");
}
void LoadCaFolder(CertStore& store,const std::filesystem::path& folder,bool roots){
    Require(std::filesystem::is_directory(folder),"Manufacturer certificate directory is missing");unsigned count{};
    for(const auto& entry:std::filesystem::directory_iterator(folder))if(entry.path().extension()==L".cer"){
        Require(entry.is_regular_file()&&++count<=256,"Invalid or excessive manufacturer certificates");const auto der=ReadFileBounded(entry.path(),16384);
        CertContext cert(der);CheckCa(cert.value,roots);store.Add(der);
    }
    if(roots)Require(count>0,"No independently trusted TPM manufacturer roots are configured");
}
bool MatchesEk(PCCERT_CONTEXT cert,const Bytes& expected){
    BCRYPT_KEY_HANDLE imported{};if(!CryptImportPublicKeyInfoEx2(X509_ASN_ENCODING,&cert->pCertInfo->SubjectPublicKeyInfo,0,nullptr,&imported))return false;
    PublicKey key;key.value=imported;ULONG size{};
    if(BCryptExportKey(key.value,nullptr,BCRYPT_RSAPUBLIC_BLOB,nullptr,0,&size,0)<0||size>1024)return false;
    Bytes blob(size);return BCryptExportKey(key.value,nullptr,BCRYPT_RSAPUBLIC_BLOB,blob.data(),size,&size,0)>=0&&size==blob.size()&&blob==expected;
}
std::string VerifyEk(const Request& request,const Configuration& config){
    CertStore roots,intermediates;LoadCaFolder(roots,config.folder/L"manufacturer-roots",true);LoadCaFolder(intermediates,config.folder/L"intermediates",false);
    const auto expected=crypto::RsaEkPublicBlob(request.ekArea);Bytes leafBytes;
    for(const auto& der:request.certificates){CertContext cert(der);
        if(MatchesEk(cert.value,expected)){Require(leafBytes.empty(),"More than one manufacturer certificate matches the EK");leafBytes=der;}
        else intermediates.Add(der);
    }
    Require(!leafBytes.empty(),"No manufacturer leaf certificate matches the EK");CertContext leaf(leafBytes);
    const auto basicExt=CertFindExtension(szOID_BASIC_CONSTRAINTS2,leaf.value->pCertInfo->cExtension,leaf.value->pCertInfo->rgExtension);Require(basicExt!=nullptr,"EK certificate lacks basic constraints");
    CERT_BASIC_CONSTRAINTS2_INFO* basic{};DWORD size{};
    Require(CryptDecodeObjectEx(X509_ASN_ENCODING,X509_BASIC_CONSTRAINTS2,basicExt->Value.pbData,basicExt->Value.cbData,CRYPT_DECODE_ALLOC_FLAG,nullptr,&basic,&size),"Invalid EK basic constraints");
    const bool isCa=basic->fCA!=0;LocalFree(basic);Require(!isCa,"EK leaf must not be a CA");
    BYTE keyUsage{};Require(CertGetIntendedKeyUsage(X509_ASN_ENCODING,leaf.value->pCertInfo,&keyUsage,1)
        &&(keyUsage&CERT_KEY_ENCIPHERMENT_KEY_USAGE)!=0&&(keyUsage&CERT_KEY_CERT_SIGN_KEY_USAGE)==0,"Invalid EK key usage");
    const auto ekuExt=CertFindExtension(szOID_ENHANCED_KEY_USAGE,leaf.value->pCertInfo->cExtension,leaf.value->pCertInfo->rgExtension);Require(ekuExt!=nullptr,"EK certificate lacks explicit TCG usage");
    CERT_ENHKEY_USAGE* usage{};
    Require(CryptDecodeObjectEx(X509_ASN_ENCODING,X509_ENHANCED_KEY_USAGE,ekuExt->Value.pbData,ekuExt->Value.cbData,CRYPT_DECODE_ALLOC_FLAG,nullptr,&usage,&size),"Invalid EK enhanced usage");
    bool endorsement=false;for(DWORD i=0;i<usage->cUsageIdentifier;++i)if(std::strcmp(usage->rgpszUsageIdentifier[i],"2.23.133.8.1")==0)endorsement=true;
    LocalFree(usage);Require(endorsement,"Certificate is not a TCG endorsement certificate");
    CERT_CHAIN_ENGINE_CONFIG engineConfig{};engineConfig.cbSize=sizeof(engineConfig);engineConfig.hExclusiveRoot=roots.value;
    HCERTCHAINENGINE engine{};Require(CertCreateCertificateChainEngine(&engineConfig,&engine),"Cannot create offline manufacturer trust engine");
    struct EngineClear{HCERTCHAINENGINE value;~EngineClear(){CertFreeCertificateChainEngine(value);}}engineClear{engine};
    CERT_CHAIN_PARA parameters{};parameters.cbSize=sizeof(parameters);parameters.RequestedUsage.dwType=USAGE_MATCH_TYPE_AND;PCCERT_CHAIN_CONTEXT chain{};
    Require(CertGetCertificateChain(engine,leaf.value,nullptr,intermediates.value,&parameters,CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL|CERT_CHAIN_DISABLE_AUTH_ROOT_AUTO_UPDATE,nullptr,&chain),"Cannot build offline EK chain");
    struct ChainClear{PCCERT_CHAIN_CONTEXT value;~ChainClear(){CertFreeCertificateChain(value);}}chainClear{chain};
    Require(chain->TrustStatus.dwErrorStatus==0&&chain->cChain==1&&chain->rgpChain[0]->cElement>=2&&chain->rgpChain[0]->cElement<=7,"EK manufacturer chain is untrusted, incomplete, expired or invalid");
    for(DWORD i=1;i<chain->rgpChain[0]->cElement;++i){
        const auto cert=chain->rgpChain[0]->rgpElement[i]->pCertContext;CheckCa(cert,i+1==chain->rgpChain[0]->cElement);
        const auto ext=CertFindExtension(szOID_ENHANCED_KEY_USAGE,cert->pCertInfo->cExtension,cert->pCertInfo->rgExtension);if(!ext)continue;
        CERT_ENHKEY_USAGE* eku{};DWORD length{};
        Require(CryptDecodeObjectEx(X509_ASN_ENCODING,X509_ENHANCED_KEY_USAGE,ext->Value.pbData,ext->Value.cbData,CRYPT_DECODE_ALLOC_FLAG,nullptr,&eku,&length),"Invalid CA enhanced usage");
        bool allowed=false;for(DWORD j=0;j<eku->cUsageIdentifier;++j){const std::string_view oid(eku->rgpszUsageIdentifier[j]);
            if(oid=="2.23.133.8.12"||oid=="2.23.133.8.1"||oid=="2.5.29.37.0")allowed=true;
        }
        LocalFree(eku);Require(allowed,"Manufacturer CA is not authorized for EK issuance");
    }
    CERT_CHAIN_POLICY_PARA policy{};policy.cbSize=sizeof(policy);CERT_CHAIN_POLICY_STATUS status{};status.cbSize=sizeof(status);
    Require(CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_BASE,chain,&policy,&status)&&status.dwError==0,"Offline EK chain policy rejected");return Hex(Hash(leafBytes));
}
Request ValidatedRequest(const std::filesystem::path& path,const Configuration& config){
    auto request=ParseRequest(*config.product,ReadFileBounded(path,65536));request.info.ekCertificateHash=VerifyEk(request,config);request.info.issuerId=config.issuerId;return request;
}
std::string NewId(){
    GUID id{};Require(SUCCEEDED(CoCreateGuid(&id)),"Cannot create authorization identifier");wchar_t text[40]{};
    Require(StringFromGUID2(id,text,40)==39,"Cannot format authorization identifier");std::string result;
    for(std::size_t i=1;i<37;++i)result.push_back(static_cast<char>(text[i]));
    std::transform(result.begin(),result.end(),result.begin(),[](char c){return c>='A'&&c<='F'?static_cast<char>(c+('a'-'A')):c;});return result;
}
void ValidatePolicy(const ProductDescriptor& product,const Policy& p){
    Require(p.kind==Kind::Permanent||p.kind==Kind::Trial,"Unknown authorization kind");
    Require(!p.customer.empty()&&p.customer.size()<=1024&&p.customer.find_first_not_of(" \t\r\n")!=std::string::npos
        &&p.customer.find('\0')==std::string::npos,"A customer name is required");
    Require(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,p.customer.data(),static_cast<int>(p.customer.size()),nullptr,0)>0,"Customer name is not valid UTF-8");
    Require(IsValidFeatureSet(product,p.features),"Invalid permission set or missing parent page");Require(p.minMajor<=p.maxMajor,"Invalid version range");
    Require(p.days>=1&&p.days<=30,"Trial duration must be 1 to 30 days");
}
bool SamePolicy(const Policy& a,const Policy& b){
    return a.customer==b.customer&&a.kind==b.kind&&a.features==b.features&&a.minMajor==b.minMajor&&a.maxMajor==b.maxMajor&&a.days==b.days;
}
class StoreLock final{
    HANDLE value_{};bool held_{};
public:
    explicit StoreLock(const std::filesystem::path& path){
        auto normalized=path.native();Require(normalized.size()<32768,"Issuer path is too long");
        CharLowerBuffW(normalized.data(),static_cast<DWORD>(normalized.size()));
        const auto digest=Hex(Hash({reinterpret_cast<const unsigned char*>(normalized.data()),normalized.size()*sizeof(wchar_t)}));
        const std::wstring name=L"Global\\TubeDesigner.Issuer.Store."+std::wstring(digest.begin(),digest.end());
        value_=CreateMutexW(nullptr,FALSE,name.c_str());Require(value_!=nullptr,"Cannot lock issuer store");
        const auto wait=WaitForSingleObject(value_,30000);held_=wait==WAIT_OBJECT_0||wait==WAIT_ABANDONED;
        if(!held_){CloseHandle(value_);value_=nullptr;throw std::runtime_error("Issuer store is busy; retry after the other signing operation");}
    }
    ~StoreLock(){if(held_)ReleaseMutex(value_);if(value_)CloseHandle(value_);}
    StoreLock(const StoreLock&)=delete;
};
Bytes CertificateBody(const Configuration& config,const Request& request,const Policy& policy,const std::string& id){
    const auto now=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();Require(now>0,"Invalid signing computer date");
    const auto issued=static_cast<std::uint64_t>(now);Bytes body(config.product->certificateFormat.begin(),config.product->certificateFormat.end());
    AppendText(body,config.issuerId);AppendText(body,id);AppendText(body,request.info.digest);AppendText(body,NewId());AppendText(body,config.product->id);
    Append32(body,static_cast<std::uint32_t>(Strategy::Tpm2));Append32(body,static_cast<std::uint32_t>(policy.kind));
    Append32(body,policy.features);Append32(body,policy.minMajor);Append32(body,policy.maxMajor);Append64(body,issued);
    Append64(body,policy.kind==Kind::Trial?issued:0);Append64(body,policy.kind==Kind::Trial?issued+static_cast<std::uint64_t>(policy.days)*86400:0);
    AppendField(body,request.devicePublic);
    if(policy.kind==Kind::Trial){AppendField(body,request.nvArea);Append64(body,request.initial);Append32(body,3600);}
    return body;
}
struct Record final{
    std::string digest,deviceHash,ekCertificateHash,licenseId;Policy policy;Bytes certificate,package;
};
Bytes EncodeRecord(const Record& record,const Configuration& config,const crypto::SigningKey& key){
    Bytes body{'T','D','L','O','G','0','0','1'};AppendText(body,config.issuerId);AppendText(body,record.digest);AppendText(body,record.deviceHash);
    AppendText(body,record.ekCertificateHash);AppendText(body,record.licenseId);AppendText(body,record.policy.customer);
    Append32(body,static_cast<std::uint32_t>(record.policy.kind));Append32(body,record.policy.features);Append32(body,record.policy.minMajor);
    Append32(body,record.policy.maxMajor);Append32(body,record.policy.days);AppendField(body,record.certificate);AppendField(body,record.package);
    const auto signature=key.Sign(body);body.insert(body.end(),signature.begin(),signature.end());return body;
}
Record DecodeRecord(std::span<const unsigned char> raw,const Configuration& config){
    Require(raw.size()>64&&raw.size()<=32768,"Invalid native issuance record");const auto body=raw.first(raw.size()-64);VerifySignature(config.publicKey,body,raw.last(64));
    Reader r(body);const auto magic=r.Take(8);Require(std::memcmp(magic.data(),"TDLOG001",8)==0&&r.Text(128)==config.issuerId,"Unknown native issuance record");
    Record record;record.digest=r.Text(64);record.deviceHash=r.Text(64);record.ekCertificateHash=r.Text(64);record.licenseId=r.Text(128);
    const auto customer=r.Field(1024);record.policy.customer.assign(customer.begin(),customer.end());
    record.policy.kind=static_cast<Kind>(r.U32());record.policy.features=r.U32();record.policy.minMajor=r.U32();record.policy.maxMajor=r.U32();record.policy.days=r.U32();
    record.certificate=r.Field(8192);record.package=r.Field(16384);
    Require(r.End()&&IsDigest(record.digest)&&IsDigest(record.deviceHash)&&IsDigest(record.ekCertificateHash),"Invalid native issuance metadata");ValidatePolicy(*config.product,record.policy);
    const auto certificate=VerifyCertificateForProduct(record.certificate,*config.product,config.issuerId,config.publicKey);
    Require(certificate.licenseId==record.licenseId&&certificate.requestId==record.digest&&certificate.strategy==Strategy::Tpm2
        &&certificate.kind==record.policy.kind&&certificate.features==record.policy.features&&certificate.minMajor==record.policy.minMajor
        &&certificate.maxMajor==record.policy.maxMajor,"Native issuance record and certificate differ");
    if(certificate.kind==Kind::Trial)Require(certificate.expiresAt-certificate.notBefore==static_cast<std::uint64_t>(record.policy.days)*86400,"Recorded trial duration differs from certificate");
    Require(record.package.size()>64&&record.package.size()<=16384,"Invalid recorded activation package");
    const auto packageBody=std::span(record.package).first(record.package.size()-64);VerifySignature(config.publicKey,packageBody,std::span(record.package).last(64));
    Reader package(packageBody);const auto packageMagic=package.Take(8);Require(std::memcmp(packageMagic.data(),"TDACT001",8)==0,"Unknown recorded activation package");
    const auto digest=package.Field(32),name=package.Field(34),credential=package.Field(1024),secret=package.Field(512),nonce=package.Field(12),encrypted=package.Field(8192);
    Require(package.End()&&digest.size()==32&&Hex(digest)==record.digest&&name.size()==34&&name[0]==0&&name[1]==11
        &&credential.size()==68&&secret.size()==256&&nonce.size()==12&&encrypted.size()==record.certificate.size()+16,"Invalid recorded activation package fields");return record;
}
std::vector<Record> ReadLedger(const Configuration& config){
    // A previous ledger cannot be silently discarded. Current primary stores have
    // no SQLite history; operators must keep using the appropriate tool if it exists.
    Require(!std::filesystem::exists(config.folder/L"issuance.sqlite")&&!std::filesystem::exists(config.folder/L"issuance.sqlite-wal")
        &&!std::filesystem::exists(config.folder/L"issuance.sqlite-journal"),"Existing SQLite issuance history requires its original signing workflow; native signing is refused");
    const auto folder=config.folder/L"native-ledger";std::vector<Record> records;if(!std::filesystem::exists(folder))return records;
    Require(std::filesystem::is_directory(folder),"Native issuance ledger is invalid");
    for(const auto& entry:std::filesystem::directory_iterator(folder))if(entry.path().extension()==L".tdrec"){
        Require(entry.is_regular_file()&&records.size()<100000,"Invalid or excessive native issuance records");
        auto record=DecodeRecord(ReadFileBounded(entry.path(),32768),config);
        Require(entry.path().stem().string()==record.digest,"Native issuance filename differs from signed request");records.push_back(std::move(record));
    }
    return records;
}
void CommitRecord(const Configuration& config,const Record& record,const crypto::SigningKey& key){
    const auto folder=config.folder/L"native-ledger";std::filesystem::create_directory(folder);
    const auto id=NewId();const auto temporary=folder/(std::wstring(id.begin(),id.end())+L".pending");
    const auto destination=folder/(std::wstring(record.digest.begin(),record.digest.end())+L".tdrec");
    try{WriteNewFile(temporary,EncodeRecord(record,config,key));
        Require(MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_WRITE_THROUGH),"Cannot commit native issuance record");
    }catch(...){DeleteFileW(temporary.c_str());throw;}
}
struct UpgradeRecord final {
    Bytes baseCertificate, certificate, package;
    Certificate base, next;
    Policy policy;
};
Bytes EncodeUpgradeRecord(const UpgradeRecord& record,const Configuration& config,const crypto::SigningKey& key){
    Bytes body{'T','D','U','L','O','G','0','1'};AppendText(body,config.issuerId);
    AppendField(body,record.baseCertificate);AppendText(body,record.policy.customer);
    Append32(body,static_cast<std::uint32_t>(record.policy.kind));Append32(body,record.policy.features);
    Append32(body,record.policy.minMajor);Append32(body,record.policy.maxMajor);Append32(body,record.policy.days);
    AppendField(body,record.package);const auto signature=key.Sign(body);body.insert(body.end(),signature.begin(),signature.end());return body;
}
UpgradeRecord DecodeUpgradeRecord(std::span<const unsigned char> raw,const Configuration& config){
    Require(raw.size()>64&&raw.size()<=32768,"Invalid native upgrade record");
    const auto body=raw.first(raw.size()-64);VerifySignature(config.publicKey,body,raw.last(64));Reader r(body);
    Require(std::memcmp(r.Take(8).data(),"TDULOG01",8)==0&&r.Text(128)==config.issuerId,"Unknown native upgrade record");
    UpgradeRecord record;record.baseCertificate=r.Field(MaxCertificateBytes);
    const auto customer=r.Field(1024);record.policy.customer.assign(customer.begin(),customer.end());
    record.policy.kind=static_cast<Kind>(r.U32());record.policy.features=r.U32();record.policy.minMajor=r.U32();
    record.policy.maxMajor=r.U32();record.policy.days=r.U32();record.package=r.Field(MaxActivationBytes);
    Require(r.End(),"Trailing native upgrade record");ValidatePolicy(*config.product,record.policy);
    record.base=VerifyCertificateForProduct(record.baseCertificate,*config.product,config.issuerId,config.publicKey);
    const auto upgrade=VerifyUpgradePackage(record.package,config.issuerId,config.publicKey,*config.product);
    Require(record.base.bodyDigest==upgrade.baseDigest,"Recorded upgrade base differs from signed package");
    record.certificate=upgrade.certificate;record.next=upgrade.parsed;RequireAuthorizationUpgrade(record.base,record.next);
    Require(record.next.kind==record.policy.kind
        &&record.next.features==record.policy.features&&record.next.minMajor==record.policy.minMajor
        &&record.next.maxMajor==record.policy.maxMajor,"Native upgrade policy and certificate differ");
    Require(record.next.kind!=record.base.kind||record.next.features!=record.base.features||record.next.minMajor!=record.base.minMajor
        ||record.next.maxMajor!=record.base.maxMajor,"Upgrade must add permissions or extend the version range");
    if(record.next.kind==Kind::Trial)Require(record.next.expiresAt-record.next.notBefore
        ==static_cast<std::uint64_t>(record.policy.days)*86400,"Recorded upgrade trial duration differs");
    return record;
}
std::vector<UpgradeRecord> ReadUpgrades(const Configuration& config,const std::vector<Record>& initial){
    std::vector<UpgradeRecord> upgrades;const auto folder=config.folder/L"native-upgrades";
    if(!std::filesystem::exists(folder))return upgrades;
    Require(std::filesystem::is_directory(folder),"Native upgrade ledger is invalid");
    std::map<std::string,std::size_t> children;
    for(const auto& entry:std::filesystem::directory_iterator(folder))if(entry.path().extension()==L".tdurec"){
        Require(entry.is_regular_file()&&upgrades.size()<100000,"Invalid or excessive native upgrade records");
        auto record=DecodeUpgradeRecord(ReadFileBounded(entry.path(),32768),config);const auto digest=Hex(record.base.bodyDigest);
        Require(entry.path().stem().string()==digest,"Native upgrade filename differs from signed base");
        Require(children.emplace(digest,upgrades.size()).second,"Duplicate native upgrade successor");upgrades.push_back(std::move(record));
    }
    std::vector<bool> visited(upgrades.size());std::size_t count{};
    for(const auto& record:initial){
        auto certificate=record.certificate;auto policy=record.policy;
        auto digest=Hex(VerifyCertificateForProduct(certificate,*config.product,config.issuerId,config.publicKey).bodyDigest);
        while(children.contains(digest)){
            const auto index=children.at(digest);Require(!visited[index],"Repeated or cyclic native upgrade lineage");
            const auto& next=upgrades[index];Require(next.baseCertificate==certificate,"Native upgrade does not match its recorded predecessor");
            Require(next.policy.customer==policy.customer
                &&(next.policy.kind==Kind::Permanent||next.policy.days==policy.days),
                "Native upgrade changed the customer or trial duration");
            visited[index]=true;++count;certificate=next.certificate;policy=next.policy;digest=Hex(next.next.bodyDigest);
        }
    }
    Require(count==upgrades.size(),"Native upgrade history is missing its original authorization or predecessor");return upgrades;
}
struct ResolvedAuthorization final {
    AuthorizationInfo info;
    Bytes certificate;
    Certificate parsed;
};
ResolvedAuthorization ResolveAuthorizationBytes(std::span<const unsigned char> raw,const Configuration& config,
    const std::vector<Record>& initial,const std::vector<UpgradeRecord>& upgrades){
    Require(raw.size()>0&&raw.size()<=MaxActivationBytes,"Invalid authorization size");Bytes certificate;
    if(IsUpgradePackage(raw)){
        certificate=VerifyUpgradePackage(raw,config.issuerId,config.publicKey,*config.product).certificate;
        Require(std::any_of(upgrades.begin(),upgrades.end(),[&](const auto& record){return EqualBytes(record.package,raw);}),
            "Upgrade package is not recorded in this trusted native issuer store");
    }
    else if(raw.size()>=8&&std::memcmp(raw.data(),"TDACT001",8)==0){
        Require(raw.size()>64,"Invalid activation file size");
        VerifySignature(config.publicKey,std::span(raw).first(raw.size()-64),std::span(raw).last(64));
        for(const auto& record:initial)if(EqualBytes(record.package,raw)){certificate=record.certificate;break;}
        Require(!certificate.empty(),"Activation file is not recorded in this trusted native issuer store");
    }else{VerifyCertificateForProduct(raw,*config.product,config.issuerId,config.publicKey);certificate.assign(raw.begin(),raw.end());}
    ResolvedAuthorization result;result.certificate=certificate;result.parsed=VerifyCertificateForProduct(certificate,*config.product,config.issuerId,config.publicKey);
    const Record* root{};const Policy* policy{};
    for(const auto& record:initial)if(record.certificate==certificate){root=&record;policy=&record.policy;break;}
    if(!root)for(const auto& record:upgrades)if(record.certificate==certificate){
        if(IsUpgradePackage(raw))Require(EqualBytes(record.package,raw),"Upgrade package is not the recorded signed file");
        policy=&record.policy;
        for(const auto& original:initial){const auto c=VerifyCertificateForProduct(original.certificate,*config.product,config.issuerId,config.publicKey);
            if(c.licenseId==result.parsed.licenseId&&c.customerId==result.parsed.customerId&&c.requestId==result.parsed.requestId){root=&original;break;}}
        break;
    }
    Require(root&&policy,"Authorization is not recorded in this trusted native issuer store");
    const auto& c=result.parsed;auto& info=result.info;info.issuerId=c.issuerId;info.licenseId=c.licenseId;
    info.requestId=c.requestId;info.customerId=c.customerId;info.product=c.product;info.deviceHash=root->deviceHash;info.policy=*policy;
    for(const auto& record:upgrades)if(record.base.bodyDigest==c.bodyDigest){info.hasNewerAuthorization=true;break;}
    return result;
}
ResolvedAuthorization ResolveAuthorization(const std::filesystem::path& path,const Configuration& config,
    const std::vector<Record>& initial,const std::vector<UpgradeRecord>& upgrades){
    return ResolveAuthorizationBytes(ReadFileBounded(path,static_cast<DWORD>(MaxActivationBytes)),config,initial,upgrades);
}
Bytes UpgradeCertificateBody(const ProductDescriptor& product,const Certificate& base,const Policy& policy){
    const auto now=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    Require(now>0,"Invalid signing computer date");
    // A trial keeps its original issue date because its absolute period and NV
    // anchor must remain unchanged; no upgrade starts another trial.
    const auto issued=policy.kind==Kind::Trial?base.issuedAt:(std::max)(base.issuedAt,static_cast<std::uint64_t>(now));
    Bytes body(product.certificateFormat.begin(),product.certificateFormat.end());
    for(const auto& field:{base.issuerId,base.licenseId,base.requestId,base.customerId,base.product})AppendText(body,field);
    Append32(body,static_cast<std::uint32_t>(base.strategy));Append32(body,static_cast<std::uint32_t>(policy.kind));
    Append32(body,policy.features);Append32(body,policy.minMajor);Append32(body,policy.maxMajor);Append64(body,issued);
    Append64(body,policy.kind==Kind::Trial?base.notBefore:0);Append64(body,policy.kind==Kind::Trial?base.expiresAt:0);AppendField(body,base.devicePublicKey);
    if(policy.kind==Kind::Trial){AppendField(body,base.trialNvPublic);Append64(body,base.trialInitialCounter);Append32(body,base.trialQuantumSeconds);}
    return body;
}
void CommitUpgradeRecord(const Configuration& config,const UpgradeRecord& record,const crypto::SigningKey& key){
    const auto folder=config.folder/L"native-upgrades";std::filesystem::create_directory(folder);
    const auto id=NewId(),digest=Hex(record.base.bodyDigest);const auto temporary=folder/(std::wstring(id.begin(),id.end())+L".pending");
    const auto destination=folder/(std::wstring(digest.begin(),digest.end())+L".tdurec");
    try{WriteNewFile(temporary,EncodeUpgradeRecord(record,config,key));
        Require(MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_WRITE_THROUGH),"Cannot commit native upgrade record");
    }catch(...){DeleteFileW(temporary.c_str());throw;}
}
void ExportPackage(const std::filesystem::path& path,const Bytes& package){
    bool created=false;
    try{
        FileHandle output(CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));created=true;DWORD written{};
        Require(WriteFile(output.value,package.data(),static_cast<DWORD>(package.size()),&written,nullptr)&&written==package.size()&&FlushFileBuffers(output.value),"Cannot export activation file");
    }catch(...){if(created)DeleteFileW(path.c_str());throw;}
}
std::vector<Bytes> AuthorizationChain(const ResolvedAuthorization& authorization,const Configuration& config,
    const std::vector<Record>& initial,const std::vector<UpgradeRecord>& upgrades){
    for(const auto& root:initial){
        auto certificate=VerifyCertificateForProduct(root.certificate,*config.product,config.issuerId,config.publicKey);
        if(certificate.licenseId!=authorization.parsed.licenseId||certificate.requestId!=authorization.parsed.requestId
            ||certificate.customerId!=authorization.parsed.customerId)continue;
        std::vector<Bytes> chain{root.package};
        for(std::size_t depth=0;depth<=upgrades.size();++depth){
            if(certificate.bodyDigest==authorization.parsed.bodyDigest)return chain;
            const auto next=std::find_if(upgrades.begin(),upgrades.end(),[&](const auto& record){return record.base.bodyDigest==certificate.bodyDigest;});
            Require(next!=upgrades.end(),"Authorization chain is missing a signed predecessor");
            chain.push_back(next->package);certificate=next->next;
        }
    }
    throw std::runtime_error("Authorization has no original activation chain");
}
std::string CustomerReference(std::string_view customer){return Hex(Hash({reinterpret_cast<const unsigned char*>(customer.data()),customer.size()}));}
struct LockedBundleStores final {
    std::vector<Configuration> configurations;
    std::vector<std::unique_ptr<StoreLock>> locks;
    explicit LockedBundleStores(std::span<const BundleStoreRef> stores){
        Require(!stores.empty()&&stores.size()<=64,"Invalid bundle product count");std::vector<std::pair<std::wstring,std::size_t>> order;
        std::map<std::string,bool> products;
        for(const auto& store:stores){Require(store.product!=nullptr,"Bundle product is missing");Identifier(store.product->id);
            Require(products.emplace(std::string(store.product->id),true).second,"Duplicate bundle product");
            configurations.push_back(ReadConfiguration(*store.product,store.storeDirectory));auto name=configurations.back().folder.native();
            CharLowerBuffW(name.data(),static_cast<DWORD>(name.size()));order.emplace_back(std::move(name),configurations.size()-1);
        }
        std::sort(order.begin(),order.end());
        for(std::size_t i=0;i<order.size();++i){Require(i==0||order[i-1].first!=order[i].first,"Products must use independent issuer stores");
            locks.push_back(std::make_unique<StoreLock>(configurations[order[i].second].folder));}
        for(auto& config:configurations)config=ReadConfiguration(*config.product,config.folder);
    }
};
struct BundleState final {
    Configuration config;std::vector<Record> initial;std::vector<UpgradeRecord> upgrades;
    std::unique_ptr<crypto::SigningKey> key;std::optional<Record> issuance;std::optional<UpgradeRecord> upgrade;
    std::optional<Request> request;std::optional<ResolvedAuthorization> authorization;std::vector<Bytes> chain;
    std::string deviceHash,requestId;
    explicit BundleState(Configuration selected):config(std::move(selected)),initial(ReadLedger(config)),upgrades(ReadUpgrades(config,initial)){}
};
ResolvedAuthorization ResolveBundleSlot(const BundleSlot& slot,const AuthorizationBundle& bundle,BundleState& state){
    Require(slot.productId==state.config.product->id&&slot.issuerId==state.config.issuerId,"Bundle slot belongs to another product issuer");
    auto authorization=ResolveAuthorizationBytes(slot.chain.back(),state.config,state.initial,state.upgrades);
    const auto chain=AuthorizationChain(authorization,state.config,state.initial,state.upgrades);
    Require(chain==slot.chain,"Bundle slot does not contain its complete recorded activation chain");
    Require(authorization.info.deviceHash==Hex(bundle.deviceHash),"Bundle products belong to different TPM devices");
    Require(CustomerReference(authorization.info.policy.customer)==bundle.customerReference,"Bundle customer differs from signed issuance history");
    return authorization;
}
void PrepareBundleIssue(BundleState& state,const Policy& policy){
    const auto& request=*state.request;
    for(const auto& record:state.initial)if(record.digest==request.info.digest){
        Require(SamePolicy(record.policy,policy),"This request was already issued with a different policy");
        Require(record.deviceHash==request.info.deviceHash&&record.ekCertificateHash==request.info.ekCertificateHash,"Recorded request device differs");
        const auto certificate=VerifyCertificateForProduct(record.certificate,*state.config.product,state.config.issuerId,state.config.publicKey);
        Require(certificate.devicePublicKey==request.devicePublic,"Recorded certificate device differs");state.chain={record.package};return;
    }
    if(policy.kind==Kind::Trial)for(const auto& record:state.initial)
        Require(record.policy.kind!=Kind::Trial||record.deviceHash!=request.info.deviceHash,"A trial was already issued for this TPM device");
    Record record;record.digest=request.info.digest;record.deviceHash=request.info.deviceHash;record.ekCertificateHash=request.info.ekCertificateHash;
    record.policy=policy;record.licenseId=NewId();record.certificate=CertificateBody(state.config,request,policy,record.licenseId);
    const auto signature=state.key->Sign(record.certificate);record.certificate.insert(record.certificate.end(),signature.begin(),signature.end());
    VerifyCertificateForProduct(record.certificate,*state.config.product,state.config.issuerId,state.config.publicKey);
    record.package=crypto::ActivationPackage(*state.key,request.ekArea,request.akArea,request.digest,record.certificate);
    DecodeRecord(EncodeRecord(record,state.config,*state.key),state.config);state.chain={record.package};state.issuance=std::move(record);
}
void PrepareBundleUpgrade(BundleState& state,const Policy& policy){
    const auto& base=*state.authorization;state.chain=AuthorizationChain(base,state.config,state.initial,state.upgrades);
    if(SamePolicy(base.info.policy,policy))return;
    Require(policy.customer==base.info.policy.customer,"Upgrade must preserve the recorded customer");
    Require(base.parsed.kind!=Kind::Permanent||policy.kind==Kind::Permanent,"A permanent license cannot become a trial");
    Require(policy.kind==Kind::Trial?policy.days==base.info.policy.days:(base.parsed.kind==Kind::Trial?policy.days==30:policy.days==base.info.policy.days),
        "Upgrade must preserve the trial duration");
    for(const auto& record:state.upgrades)if(record.base.bodyDigest==base.parsed.bodyDigest){
        Require(SamePolicy(record.policy,policy),"This authorization was already upgraded; select its latest successor");state.chain.push_back(record.package);return;
    }
    UpgradeRecord record;record.baseCertificate=base.certificate;record.base=base.parsed;record.policy=policy;
    auto candidate=base.parsed;candidate.kind=policy.kind;candidate.features=policy.features;candidate.minMajor=policy.minMajor;candidate.maxMajor=policy.maxMajor;
    if(candidate.kind==Kind::Permanent){candidate.notBefore=0;candidate.expiresAt=0;candidate.trialNvPublic.clear();candidate.trialInitialCounter=0;candidate.trialQuantumSeconds=0;}
    RequireAuthorizationUpgrade(base.parsed,candidate);
    Require(candidate.kind!=base.parsed.kind||candidate.features!=base.parsed.features||candidate.minMajor!=base.parsed.minMajor||candidate.maxMajor!=base.parsed.maxMajor,
        "Upgrade must add permissions or extend the version range");
    record.certificate=UpgradeCertificateBody(*state.config.product,base.parsed,policy);
    const auto signature=state.key->Sign(record.certificate);record.certificate.insert(record.certificate.end(),signature.begin(),signature.end());
    record.next=VerifyCertificateForProduct(record.certificate,*state.config.product,state.config.issuerId,state.config.publicKey);
    record.package=UpgradeBody(base.parsed,record.certificate,state.config.issuerId,state.config.publicKey,*state.config.product);
    const auto packageSignature=state.key->Sign(record.package);record.package.insert(record.package.end(),packageSignature.begin(),packageSignature.end());
    DecodeUpgradeRecord(EncodeUpgradeRecord(record,state.config,*state.key),state.config);state.chain.push_back(record.package);state.upgrade=std::move(record);
}
}
StoreInfo InspectStore(const ProductDescriptor& product,const std::filesystem::path& store){const auto config=ReadConfiguration(product,store);return{config.issuerId,Hex(Hash(config.publicKey))};}
RequestInfo InspectRequest(const ProductDescriptor& product,const std::filesystem::path& request,const std::filesystem::path& store){const auto config=ReadConfiguration(product,store);return ValidatedRequest(request,config).info;}
AuthorizationInfo InspectAuthorization(const ProductDescriptor& product,const std::filesystem::path& authorization,const std::filesystem::path& store){
    const auto config=ReadConfiguration(product,store);StoreLock lock(config.folder);const auto initial=ReadLedger(config);
    const auto upgrades=ReadUpgrades(config,initial);return ResolveAuthorization(authorization,config,initial,upgrades).info;
}
GenerateResult Generate(const ProductDescriptor& product,const std::filesystem::path& store,const std::filesystem::path& path,std::string_view passwordUtf8,const Policy& policy,const std::filesystem::path& output){
    auto config=ReadConfiguration(product,store);StoreLock lock(config.folder);config=ReadConfiguration(product,config.folder);
    const auto request=ValidatedRequest(path,config);ValidatePolicy(product,policy);
    Require(request.info.isTrial==(policy.kind==Kind::Trial),"Authorization type must match permanent or trial enrollment request");
    const auto records=ReadLedger(config);ReadUpgrades(config,records);
    crypto::SigningKey signingKey(config.folder/L"issuer-private.pem",passwordUtf8,config.publicKey);
    for(const auto& record:records)if(record.digest==request.info.digest){
        Require(SamePolicy(record.policy,policy),"This request was already issued with a different policy; use matching settings to re-export or create a new device request");
        Require(record.deviceHash==request.info.deviceHash&&record.ekCertificateHash==request.info.ekCertificateHash,"Recorded request device metadata differs");
        const auto certificate=VerifyCertificateForProduct(record.certificate,*config.product,config.issuerId,config.publicKey);Require(certificate.devicePublicKey==request.devicePublic,"Recorded certificate device differs");
        ExportPackage(output,record.package);return{output,record.licenseId,true};
    }
    if(policy.kind==Kind::Trial)for(const auto& record:records)Require(record.policy.kind!=Kind::Trial||record.deviceHash!=request.info.deviceHash,"A trial was already issued for this TPM device");
    Record record;record.digest=request.info.digest;record.deviceHash=request.info.deviceHash;record.ekCertificateHash=request.info.ekCertificateHash;record.policy=policy;record.licenseId=NewId();
    record.certificate=CertificateBody(config,request,policy,record.licenseId);const auto signature=signingKey.Sign(record.certificate);record.certificate.insert(record.certificate.end(),signature.begin(),signature.end());
    VerifyCertificateForProduct(record.certificate,*config.product,config.issuerId,config.publicKey);record.package=crypto::ActivationPackage(signingKey,request.ekArea,request.akArea,request.digest,record.certificate);
    const auto encoded=EncodeRecord(record,config,signingKey);DecodeRecord(encoded,config);
    CommitRecord(config,record,signingKey);ExportPackage(output,record.package);return{output,record.licenseId,false};
}
GenerateResult GenerateUpgrade(const ProductDescriptor& product,const std::filesystem::path& store,const std::filesystem::path& authorization,
    std::string_view passwordUtf8,const Policy& policy,const std::filesystem::path& output){
    auto config=ReadConfiguration(product,store);StoreLock lock(config.folder);config=ReadConfiguration(product,config.folder);
    const auto initial=ReadLedger(config);const auto upgrades=ReadUpgrades(config,initial);
    const auto base=ResolveAuthorization(authorization,config,initial,upgrades);ValidatePolicy(product,policy);
    Require(policy.customer==base.info.policy.customer,"Upgrade must preserve the recorded customer");
    Require(base.parsed.kind!=Kind::Permanent||policy.kind==Kind::Permanent,"A permanent license cannot become a trial");
    Require(policy.kind==Kind::Trial?policy.days==base.info.policy.days:
        (base.parsed.kind==Kind::Trial?policy.days==30:policy.days==base.info.policy.days),"Upgrade must preserve the trial duration");
    for(const auto& record:upgrades)if(record.base.bodyDigest==base.parsed.bodyDigest){
        Require(SamePolicy(record.policy,policy),"This authorization was already upgraded; select the latest authorization file for further changes");
        // Re-export still verifies the supplied password and matching private key.
        crypto::SigningKey signingKey(config.folder/L"issuer-private.pem",passwordUtf8,config.publicKey);
        ExportPackage(output,record.package);return{output,record.next.licenseId,true};
    }
    UpgradeRecord record;record.baseCertificate=base.certificate;record.base=base.parsed;record.policy=policy;
    auto candidate=base.parsed;candidate.kind=policy.kind;candidate.features=policy.features;candidate.minMajor=policy.minMajor;candidate.maxMajor=policy.maxMajor;
    if(candidate.kind==Kind::Permanent){candidate.notBefore=0;candidate.expiresAt=0;candidate.trialNvPublic.clear();candidate.trialInitialCounter=0;candidate.trialQuantumSeconds=0;}
    RequireAuthorizationUpgrade(base.parsed,candidate);
    Require(candidate.kind!=base.parsed.kind||candidate.features!=base.parsed.features||candidate.minMajor!=base.parsed.minMajor||candidate.maxMajor!=base.parsed.maxMajor,
        "Upgrade must add permissions or extend the version range");
    record.certificate=UpgradeCertificateBody(product,base.parsed,policy);
    crypto::SigningKey signingKey(config.folder/L"issuer-private.pem",passwordUtf8,config.publicKey);
    const auto signature=signingKey.Sign(record.certificate);record.certificate.insert(record.certificate.end(),signature.begin(),signature.end());
    record.next=VerifyCertificateForProduct(record.certificate,*config.product,config.issuerId,config.publicKey);
    record.package=UpgradeBody(base.parsed,record.certificate,config.issuerId,config.publicKey,product);
    const auto packageSignature=signingKey.Sign(record.package);record.package.insert(record.package.end(),packageSignature.begin(),packageSignature.end());
    DecodeUpgradeRecord(EncodeUpgradeRecord(record,config,signingKey),config);
    CommitUpgradeRecord(config,record,signingKey);ExportPackage(output,record.package);return{output,record.next.licenseId,false};
}
BundleInfo InspectBundle(std::span<const unsigned char> bytes,std::span<const BundleStoreRef> stores){
    const auto bundle=ParseAuthorizationBundle(bytes);LockedBundleStores locked(stores);
    Require(bundle.slots.size()==stores.size(),"Every bundle product requires its own trusted issuer store");
    BundleInfo info;info.bundleId=bundle.bundleId;info.deviceHash=Hex(bundle.deviceHash);
    for(auto& config:locked.configurations){
        VerifyAuthorizationBundleForProduct(bytes,*config.product,config.issuerId,config.publicKey);
        BundleState state(config);const auto& slot=FindBundleSlot(bundle,config.product->id);auto authorization=ResolveBundleSlot(slot,bundle,state);
        if(info.customer.empty())info.customer=authorization.info.policy.customer;
        Require(info.customer==authorization.info.policy.customer,"Bundle products belong to different customers");info.products.push_back(std::move(authorization.info));
    }
    return info;
}
BundleInfo InspectBundle(const std::filesystem::path& path,std::span<const BundleStoreRef> stores){
    return InspectBundle(ReadFileBounded(path,static_cast<DWORD>(MaxAuthorizationFileBytes)),stores);
}
GenerateResult GenerateBundle(std::span<const BundleInput> inputs,const std::optional<std::filesystem::path>& baseBundlePath,
    const std::filesystem::path& output){
    Bytes baseBytes;std::optional<AuthorizationBundle> base;
    if(baseBundlePath){baseBytes=ReadFileBounded(*baseBundlePath,static_cast<DWORD>(MaxAuthorizationFileBytes));base=ParseAuthorizationBundle(baseBytes);}
    std::vector<BundleStoreRef> stores;stores.reserve(inputs.size());
    for(const auto& input:inputs)stores.push_back({input.product,input.storeDirectory});
    LockedBundleStores locked(stores);std::vector<BundleState> states;states.reserve(inputs.size());
    std::string customer,deviceHash;
    if(base)for(const auto& slot:base->slots)
        Require(std::any_of(inputs.begin(),inputs.end(),[&](const auto& input){return input.product->id==slot.productId;}),
            "An existing bundle product cannot be removed");
    for(std::size_t index=0;index<inputs.size();++index){
        const auto& input=inputs[index];ValidatePolicy(*input.product,input.policy);
        if(index==0)customer=input.policy.customer;Require(customer==input.policy.customer,"Bundle products must have the same customer");
        states.emplace_back(locked.configurations[index]);auto& state=states.back();
        const auto existing=base?std::find_if(base->slots.begin(),base->slots.end(),[&](const auto& slot){return slot.productId==input.product->id;}):std::vector<BundleSlot>::const_iterator{};
        if(base&&existing!=base->slots.end()){
            VerifyAuthorizationBundleForProduct(baseBytes,*input.product,state.config.issuerId,state.config.publicKey);
            state.authorization=ResolveBundleSlot(*existing,*base,state);
        }else{
            const auto snapshot=ReadFileBounded(input.sourcePath,static_cast<DWORD>(MaxActivationBytes>65536?MaxActivationBytes:65536));
            const auto matches=[&](std::string_view magic){return snapshot.size()>=magic.size()&&EqualBytes(std::span(snapshot).first(magic.size()),
                {reinterpret_cast<const unsigned char*>(magic.data()),magic.size()});};
            if(matches(input.product->permanentRequestFormat)||matches(input.product->trialRequestFormat)){
                state.request=ParseRequest(*input.product,snapshot);state.request->info.ekCertificateHash=VerifyEk(*state.request,state.config);
                state.request->info.issuerId=state.config.issuerId;
                Require(state.request->info.isTrial==(input.policy.kind==Kind::Trial),"Authorization type must match the product enrollment request");
            }else state.authorization=ResolveAuthorizationBytes(snapshot,state.config,state.initial,state.upgrades);
        }
        state.deviceHash=state.request?state.request->info.deviceHash:state.authorization->info.deviceHash;
        state.requestId=state.request?state.request->info.digest:state.authorization->info.requestId;
        if(index==0)deviceHash=state.deviceHash;Require(deviceHash==state.deviceHash,"Bundle products belong to different TPM devices");
    }
    // All source snapshots and policies are validated before any key is opened or
    // record is committed. Opening every key first avoids partial issuance when a
    // later product has a wrong password or a mismatched private key.
    for(std::size_t i=0;i<inputs.size();++i){const auto& password=inputs[i].password;Require(!password.empty(),"A product signing password is required");
        states[i].key=std::make_unique<crypto::SigningKey>(states[i].config.folder/L"issuer-private.pem",
            std::string_view(reinterpret_cast<const char*>(password.data()),password.size()),states[i].config.publicKey);}
    AuthorizationBundle bundle;bundle.customerReference=CustomerReference(customer);Require(IsDigest(deviceHash),"Invalid bundle TPM fingerprint");
    const auto digit=[](char c){return static_cast<unsigned char>(c<='9'?c-'0':c-'a'+10);};
    for(std::size_t i=0;i<bundle.deviceHash.size();++i)bundle.deviceHash[i]=static_cast<unsigned char>((digit(deviceHash[i*2])<<4)|digit(deviceHash[i*2+1]));
    Bytes identity;AppendText(identity,deviceHash);AppendText(identity,bundle.customerReference);
    std::vector<std::size_t> order;for(std::size_t i=0;i<inputs.size();++i)order.push_back(i);
    std::sort(order.begin(),order.end(),[&](auto left,auto right){return inputs[left].product->id<inputs[right].product->id;});
    for(const auto i:order){auto& state=states[i];if(state.request)PrepareBundleIssue(state,inputs[i].policy);else PrepareBundleUpgrade(state,inputs[i].policy);
        AppendText(identity,inputs[i].product->id);AppendText(identity,state.requestId);
        bundle.slots.push_back({std::string(inputs[i].product->id),state.config.issuerId,state.chain,{}});}
    bundle.bundleId=Hex(Hash(identity));const auto manifest=BundleManifest(bundle);const auto manifestDigest=Hex(Hash(manifest));
    const auto first=std::min_element(states.begin(),states.end(),[](const auto& left,const auto& right){return left.config.folder.native()<right.config.folder.native();});
    const auto cacheFolder=first->config.folder/L"native-bundles";
    const auto cache=cacheFolder/(std::wstring(manifestDigest.begin(),manifestDigest.end())+L".tdact");
    if(std::filesystem::exists(cache)){
        const auto saved=ReadFileBounded(cache,static_cast<DWORD>(MaxAuthorizationFileBytes));const auto parsed=ParseAuthorizationBundle(saved);
        Require(BundleManifest(parsed)==manifest,"Signed bundle cache has different contents");
        for(const auto& state:states)VerifyAuthorizationBundleForProduct(saved,*state.config.product,state.config.issuerId,state.config.publicKey);
        ExportPackage(output,saved);return{output,bundle.bundleId,true};
    }
    for(std::size_t i=0;i<order.size();++i)bundle.slots[i].signature=states[order[i]].key->Sign(manifest);
    const auto encoded=EncodeAuthorizationBundle(bundle);
    for(const auto& state:states)VerifyAuthorizationBundleForProduct(encoded,*state.config.product,state.config.issuerId,state.config.publicKey);
    // Store commits are individually durable. If interrupted, retries recover the
    // exact committed product packages and only create still-missing records.
    for(auto& state:states){if(state.issuance)CommitRecord(state.config,*state.issuance,*state.key);
        if(state.upgrade)CommitUpgradeRecord(state.config,*state.upgrade,*state.key);}
    std::filesystem::create_directory(cacheFolder);const auto suffix=NewId();const auto temporary=cacheFolder/(std::wstring(suffix.begin(),suffix.end())+L".pending");
    try{ExportPackage(temporary,encoded);Require(MoveFileExW(temporary.c_str(),cache.c_str(),MOVEFILE_WRITE_THROUGH),"Cannot commit signed authorization bundle cache");}
    catch(...){DeleteFileW(temporary.c_str());throw;}
    ExportPackage(output,encoded);return{output,bundle.bundleId,false};
}
}
