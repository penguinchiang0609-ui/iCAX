// Included in the private issuer namespace. Export receipts describe files;
// only the existing signed authorization ledger decides issuance eligibility.
std::string PathText(const std::filesystem::path& path) {
    const auto text=path.u8string();return {text.begin(),text.end()};
}
std::filesystem::path TextPath(std::string_view text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()),text.size()));
}
bool MatchesRecordedAuthorization(const std::filesystem::path& path,const Configuration& config,const Record& record,
    std::string_view artifact={},std::span<const unsigned char> requiredCertificate={}) noexcept {
    try {
        const auto attributes=GetFileAttributesW(path.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return false;
        const auto bytes=ReadFileBounded(path,static_cast<DWORD>(MaxAuthorizationFileBytes));
        if(!artifact.empty()&&Hex(Hash(bytes))!=artifact)return false;
        const bool original=requiredCertificate.empty()||EqualBytes(requiredCertificate,record.certificate);
        if(EqualBytes(bytes,record.package))return original;
        if(IsUpgradePackage(bytes)||IsReissuePackage(bytes)) {
            const auto transition=VerifyAuthorizationTransition(bytes,config.issuerId,config.publicKey,*config.product);
            return !requiredCertificate.empty()&&EqualBytes(transition.certificate,requiredCertificate);
        }
        if(!IsAuthorizationBundle(bytes))return false;
        VerifyAuthorizationBundleForProduct(bytes,*config.product,config.issuerId,config.publicKey);
        const auto bundle=ParseAuthorizationBundle(bytes);const auto& slot=FindBundleSlot(bundle,config.product->id);
        if(slot.chain.front()!=record.package)return false;
        if(original)return true;
        for(std::size_t i=1;i<slot.chain.size();++i)
            if(EqualBytes(VerifyAuthorizationTransition(slot.chain[i],config.issuerId,config.publicKey,*config.product).certificate,requiredCertificate))return true;
        return false;
    } catch (...) {return false;}
}
struct ExportReceipt final {
    std::string requestId,artifact,certificateDigest;
    std::filesystem::path output;
    std::uint64_t exportedAt{};
};
ExportReceipt ReadExportReceipt(const std::filesystem::path& path,const Configuration& config) {
    const auto raw=ReadFileBounded(path,65536);Require(raw.size()>64,"Invalid export receipt");
    const auto body=std::span(raw).first(raw.size()-64);VerifySignature(config.publicKey,body,std::span(raw).last(64));Reader r(body);
    Require(std::memcmp(r.Take(8).data(),"TDEXP001",8)==0&&r.Text(128)==config.issuerId,"Unknown export receipt");
    ExportReceipt receipt;receipt.requestId=r.Text(64);receipt.artifact=r.Text(64);
    const auto bytes=r.Field(32768);const std::string output(bytes.begin(),bytes.end());
    Require(output.find('\0')==std::string::npos,"Invalid export path");receipt.output=TextPath(output);
    receipt.certificateDigest=r.Text(64);receipt.exportedAt=r.U64();
    Require(r.End()&&IsDigest(receipt.requestId)&&IsDigest(receipt.artifact)&&IsDigest(receipt.certificateDigest)&&receipt.output.is_absolute()
        &&receipt.exportedAt>0,"Invalid export receipt fields");return receipt;
}
std::string RememberExport(const Configuration& config,const crypto::SigningKey& key,std::string_view requestId,
    const std::filesystem::path& output,std::span<const unsigned char> bytes) noexcept {
    try {
        Bytes leaf(bytes.begin(),bytes.end());
        if(IsAuthorizationBundle(leaf))leaf=FindBundleSlot(ParseAuthorizationBundle(leaf),config.product->id).chain.back();
        Bytes certificate;
        if(IsUpgradePackage(leaf)||IsReissuePackage(leaf))certificate=VerifyAuthorizationTransition(leaf,config.issuerId,config.publicKey,*config.product).certificate;
        else for(const auto& record:ReadLedger(config))if(record.package==leaf){certificate=record.certificate;break;}
        Require(!certificate.empty(),"Export has no recorded signed certificate");
        const auto parsed=VerifyCertificateForProduct(certificate,*config.product,config.issuerId,config.publicKey);
        Bytes body{'T','D','E','X','P','0','0','1'};AppendText(body,config.issuerId);AppendText(body,requestId);
        AppendText(body,Hex(Hash(bytes)));AppendText(body,PathText(std::filesystem::absolute(output).lexically_normal()));
        AppendText(body,Hex(parsed.bodyDigest));
        const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        Require(now>0,"Invalid signing computer time");Append64(body,static_cast<std::uint64_t>(now));
        const auto signature=key.Sign(body);body.insert(body.end(),signature.begin(),signature.end());
        const auto folder=config.folder/L"native-exports";std::filesystem::create_directory(folder);const auto id=NewId();
        const auto path=folder/(std::wstring(id.begin(),id.end())+L".tdexport");WriteNewFile(path,body);return {};
    } catch (...) {return "授权文件已生成，但保存导出文件名失败；请保留该文件，稍后检查签发库目录是否可写。";}
}
std::filesystem::path FindNearbyAuthorization(const Configuration& config,const Record& record,
    const std::vector<std::filesystem::path>& roots,std::span<const unsigned char> requiredCertificate={},bool includeCache=false) noexcept {
    try {
        std::vector<std::pair<std::filesystem::path,unsigned>> pending;
        for(const auto& root:roots)if(!root.empty()&&root!=root.root_path())pending.emplace_back(root,0);
        std::vector<std::filesystem::path> visited;std::size_t candidates{};
        for(std::size_t index=0;index<pending.size()&&visited.size()<128&&candidates<512;++index) {
            const auto [folder,depth]=pending[index];std::error_code error;
            const auto absolute=std::filesystem::weakly_canonical(folder,error);if(error)continue;
            if(!includeCache&&(absolute==config.folder||absolute.filename()==L"native-bundles"))continue;
            if(std::find(visited.begin(),visited.end(),absolute)!=visited.end())continue;
            const auto attributes=GetFileAttributesW(absolute.c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT)||!(attributes&FILE_ATTRIBUTE_DIRECTORY))continue;
            visited.push_back(absolute);
            for(std::filesystem::directory_iterator it(absolute,std::filesystem::directory_options::skip_permission_denied,error),end;
                !error&&it!=end&&candidates<512;it.increment(error)) {
                const auto path=it->path();const auto flags=GetFileAttributesW(path.c_str());
                if(flags==INVALID_FILE_ATTRIBUTES||(flags&FILE_ATTRIBUTE_REPARSE_POINT))continue;
                if(flags&FILE_ATTRIBUTE_DIRECTORY) {if(depth<2&&pending.size()<512)pending.emplace_back(path,depth+1);continue;}
                if(_wcsicmp(path.extension().c_str(),L".tdact")!=0)continue;
                ++candidates;if(MatchesRecordedAuthorization(path,config,record,{},requiredCertificate))return path;
            }
        }
    } catch (...) {}
    return {};
}
IssuanceHistory PreviousIssuance(const Configuration& config,const Record& record,const std::filesystem::path& source,
    std::span<const unsigned char> currentCertificate={}) {
    if(currentCertificate.empty())currentCertificate=record.certificate;
    const auto certificate=VerifyCertificateForProduct(currentCertificate,*config.product,config.issuerId,config.publicKey);
    IssuanceHistory history;history.licenseId=certificate.licenseId;history.requestId=certificate.requestId;
    history.customer=record.policy.customer;history.issuedAt=certificate.issuedAt;
    std::optional<ExportReceipt> earliest;const auto folder=config.folder/L"native-exports";
    std::error_code error;std::size_t count{};
    for(std::filesystem::directory_iterator it(folder,std::filesystem::directory_options::skip_permission_denied,error),end;
        !error&&it!=end&&count<4096;it.increment(error)) {
        if(it->path().extension()!=L".tdexport")continue;++count;
        try {auto receipt=ReadExportReceipt(it->path(),config);if(receipt.requestId==certificate.requestId&&receipt.certificateDigest==Hex(certificate.bodyDigest)
            &&(!earliest||receipt.exportedAt<earliest->exportedAt))earliest=std::move(receipt);} catch (...) {}
    }
    if(earliest) {
        history.outputPath=earliest->output;history.outputExists=MatchesRecordedAuthorization(history.outputPath,config,record,earliest->artifact,currentCertificate);
        if(history.outputExists)history.recoveryPath=history.outputPath;
    }
    const auto parent=std::filesystem::absolute(source).parent_path();
    if(history.recoveryPath.empty()) {
        auto nearby=FindNearbyAuthorization(config,record,{parent,parent.parent_path()},currentCertificate);
        if(!nearby.empty()) {
            history.recoveryPath=nearby;
            if(history.outputPath.empty()){history.outputPath=nearby;history.outputExists=true;}
        }
    }
    // A verified cache may restore the original authorization, but its digest
    // filename must never be presented as the customer's prior export name.
    if(history.recoveryPath.empty())history.recoveryPath=FindNearbyAuthorization(config,record,{config.folder/L"native-bundles"},currentCertificate,true);
    return history;
}
const Record* LatestDeviceRecord(const Configuration& config,const std::vector<Record>& records,std::string_view deviceHash,
    const std::vector<UpgradeRecord>& upgrades={}) {
    const Record* latest{};std::uint64_t issued{};
    for(const auto& record:records)if(record.deviceHash==deviceHash) {
        auto certificate=VerifyCertificateForProduct(record.certificate,*config.product,config.issuerId,config.publicKey);
        for(std::size_t depth=0;depth<=upgrades.size();++depth) {
            const auto next=std::find_if(upgrades.begin(),upgrades.end(),[&](const auto& item){return item.base.bodyDigest==certificate.bodyDigest;});
            if(next==upgrades.end())break;certificate=next->next;
        }
        if(!latest||certificate.issuedAt>issued){latest=&record;issued=certificate.issuedAt;}
    }
    return latest;
}
