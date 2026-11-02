// Disposable wireframe derivatives only. Never part of a project or install.
namespace { namespace FilePreviewCache {
using Bytes=std::vector<std::uint8_t>;using Digest=std::array<std::uint8_t,32>;
constexpr std::array<std::uint8_t,8> Magic{'I','C','A','X','F','P','0','1'};
constexpr std::uint64_t MaximumFileBytes=32ull*1024*1024,MaximumBytes=128ull*1024*1024;
constexpr size_t MaximumFiles=50,HeaderBytes=8+64+8+32;
constexpr const wchar_t* Extension=L".icaxfp1";
std::mutex Mutex;
bool Hash(std::span<const std::uint8_t> data,Digest& result){return data.size()<=std::numeric_limits<ULONG>::max()&&BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<PUCHAR>(data.data()),static_cast<ULONG>(data.size()),result.data(),static_cast<ULONG>(result.size()))>=0;}
Text Hex(const Digest& digest){constexpr char symbols[]="0123456789abcdef";Text result;for(auto n:digest){result.push_back(symbols[n>>4]);result.push_back(symbols[n&15]);}return result;}
bool ValidKey(const Text& key){return key.size()==64&&std::all_of(key.begin(),key.end(),[](char c){return c>='0'&&c<='9'||c>='a'&&c<='f';});}
std::optional<std::filesystem::path> EnvironmentDirectory(){const auto length=GetEnvironmentVariableW(L"ICAX_CAM_PREVIEW_CACHE_DIR",nullptr,0);if(!length||length>32768)return {};std::wstring value(length,L'\0');const auto written=GetEnvironmentVariableW(L"ICAX_CAM_PREVIEW_CACHE_DIR",value.data(),length);if(!written||written>=length)return {};value.resize(written);return std::filesystem::path(value);}
bool OutsideInstall(const std::filesystem::path& candidate){
    if(!candidate.is_absolute())return false;std::wstring executable(32768,L'\0');const auto count=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));if(!count||count>=executable.size())return false;executable.resize(count);
    std::error_code error;auto directory=std::filesystem::weakly_canonical(candidate,error);if(error)return false;auto install=std::filesystem::weakly_canonical(std::filesystem::path(executable).parent_path(),error);if(error)return false;
    auto p=directory.generic_wstring(),base=install.generic_wstring();CharLowerBuffW(p.data(),static_cast<DWORD>(p.size()));CharLowerBuffW(base.data(),static_cast<DWORD>(base.size()));if(p==base)return false;if(!base.ends_with(L'/'))base+=L'/';return !p.starts_with(base);
}
std::optional<std::filesystem::path> Directory(){
    if(const auto override=EnvironmentDirectory())return OutsideInstall(*override)?override:std::nullopt;
    PWSTR local=nullptr;std::filesystem::path candidate;
    if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local))&&local)candidate=std::filesystem::path(local)/L"iCAX"/L"Cache"/L"FiveAxisCAM"/L"FilePreviews"/L"v1";if(local)CoTaskMemFree(local);
    if(!candidate.empty()&&OutsideInstall(candidate))return candidate;
    std::error_code error;candidate=std::filesystem::temp_directory_path(error)/L"iCAX"/L"FiveAxisCAM"/L"FilePreviews"/L"v1";return !error&&OutsideInstall(candidate)?std::optional(candidate):std::nullopt;
}
Text Key(const std::filesystem::path& path,std::uintmax_t size,const Text& modified){
    static const Text build=[](){Text result;for(const auto name:{L"OpenCascadeResourceImport.dll",L"OpenCascadeGeometryServices.dll"}){const auto module=GetModuleHandleW(name);if(!module)continue;std::wstring file(32768,L'\0');const auto count=GetModuleFileNameW(module,file.data(),static_cast<DWORD>(file.size()));if(!count||count>=file.size())return Text();file.resize(count);const auto p=std::filesystem::path(file);result+=std::to_string(std::filesystem::file_size(p))+":"+std::to_string(std::filesystem::last_write_time(p).time_since_epoch().count())+";";}return result;}();
    if(build.empty())return {};auto lower=path.generic_wstring();CharLowerBuffW(lower.data(),static_cast<DWORD>(lower.size()));
    const auto identity=Text("wireframe.1|import.5|edge.03|edgePoints256|points1250000|")+build+PickerPathText(std::filesystem::path(lower))+"|"+std::to_string(size)+"|"+modified;
    Digest digest;if(!Hash({reinterpret_cast<const std::uint8_t*>(identity.data()),identity.size()},digest))return {};return Hex(digest);
}
void U64(Bytes& bytes,std::uint64_t n){for(int shift=0;shift<64;shift+=8)bytes.push_back(static_cast<std::uint8_t>(n>>shift));}
struct Reader{std::span<const std::uint8_t> Data;size_t Offset=0;void Require(bool ok){if(!ok)throw std::runtime_error("CAM_PREVIEW_CACHE_INVALID");}std::uint64_t U64(){Require(Offset<=Data.size()&&Data.size()-Offset>=8);std::uint64_t n=0;for(int shift=0;shift<64;shift+=8)n|=static_cast<std::uint64_t>(Data[Offset++])<<shift;return n;}};
std::optional<Bytes> Encode(const Map& wireframe){
    const auto lines=wireframe.at("polylines").To<Array>();if(lines.empty()||lines.size()>50000)return {};Bytes result;U64(result,lines.size());std::uint64_t total=0;
    for(const auto& line:lines){const auto coordinates=line.To<Array>();if(coordinates.size()<6||coordinates.size()%3)return {};total+=coordinates.size()/3;if(total>1250000||coordinates.size()>(MaximumFileBytes-HeaderBytes-result.size()-8)/8)return {};U64(result,coordinates.size());for(const auto& value:coordinates)U64(result,std::bit_cast<std::uint64_t>(FiniteNumber(value,"CAM_PREVIEW_CACHE_INVALID")));}return result;
}
Map Decode(std::span<const std::uint8_t> payload){
    Reader reader{payload};const auto count=reader.U64();reader.Require(count>0&&count<=50000);Array lines;lines.reserve(static_cast<size_t>(count));std::uint64_t points=0;
    for(std::uint64_t i=0;i<count;++i){const auto n=reader.U64();reader.Require(n>=6&&n%3==0&&n<=(payload.size()-reader.Offset)/8);points+=n/3;reader.Require(points<=1250000);Array coordinates;coordinates.reserve(static_cast<size_t>(n));for(std::uint64_t j=0;j<n;++j){const auto value=std::bit_cast<double>(reader.U64());reader.Require(std::isfinite(value));coordinates.emplace_back(value);}lines.emplace_back(std::move(coordinates));}
    reader.Require(reader.Offset==payload.size());return {{"polylines",std::move(lines)},{"edgeCount",count},{"pointCount",points}};
}
bool Owned(const std::filesystem::path& path){return path.extension()==Extension&&ValidKey(path.stem().string());}
bool Trim(const std::filesystem::path& directory,const std::filesystem::path& protect={},std::uintmax_t reserveBytes=0,size_t reserveFiles=0){
    struct Item{std::filesystem::path Path;std::uintmax_t Bytes;std::filesystem::file_time_type Used;};std::vector<Item> items;std::uintmax_t total=0;std::error_code error;
    for(std::filesystem::directory_iterator i(directory,error),end;!error&&i!=end;i.increment(error)){
        const auto& e=*i;if(e.symlink_status(error).type()!=std::filesystem::file_type::regular||error){error.clear();continue;}
        const auto name=e.path().filename().string();const bool temporary=e.path().extension()==L".tmp"&&name.size()==105&&ValidKey(name.substr(0,64))&&name[64]=='.'&&ID::from_string(name.substr(65,36)).has_value();
        if(!Owned(e.path())&&!temporary)continue;if(reserveFiles&&e.path()==protect)continue;const auto size=e.file_size(error);if(error)return false;const auto used=e.last_write_time(error);if(error)return false;
        if(total>std::numeric_limits<std::uintmax_t>::max()-size)return false;total+=size;items.push_back({e.path(),size,used});
    }
    if(error||reserveBytes>MaximumBytes||reserveFiles>MaximumFiles)return false;
    std::sort(items.begin(),items.end(),[](const auto& a,const auto& b){return a.Used<b.Used;});auto count=items.size();for(const auto& item:items){if(total<=MaximumBytes-reserveBytes&&count<=MaximumFiles-reserveFiles)break;if(item.Path==protect)continue;if(std::filesystem::remove(item.Path,error)){total-=item.Bytes;--count;}error.clear();}
    return total<=MaximumBytes-reserveBytes&&count<=MaximumFiles-reserveFiles;
}
std::optional<Map> Read(const Text& key){
    try{std::lock_guard lock(Mutex);if(!ValidKey(key))return {};const auto directory=Directory();if(!directory||!std::filesystem::is_directory(*directory))return {};Trim(*directory);
        const auto file=*directory/(std::wstring(key.begin(),key.end())+Extension);if(std::filesystem::symlink_status(file).type()!=std::filesystem::file_type::regular)return {};const auto size=std::filesystem::file_size(file);if(size<=HeaderBytes||size>MaximumFileBytes)return {};
        Bytes bytes(static_cast<size_t>(size));std::ifstream stream(file,std::ios::binary);if(!stream.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())))return {};
        if(!std::equal(Magic.begin(),Magic.end(),bytes.begin())||!std::equal(key.begin(),key.end(),bytes.begin()+8))return {};Reader reader{bytes,72};const auto n=reader.U64();if(n!=size-HeaderBytes)return {};Digest expected;std::copy_n(bytes.begin()+80,32,expected.begin());Digest actual;const std::span<const std::uint8_t> payload{bytes.data()+HeaderBytes,static_cast<size_t>(n)};
        if(!Hash(payload,actual)||actual!=expected)return {};auto result=Decode(payload);std::error_code error;std::filesystem::last_write_time(file,std::filesystem::file_time_type::clock::now(),error);return result;
    }catch(...){return {};}
}
void Write(const Text& key,const Map& wireframe){
    std::filesystem::path temporary;
    try{const auto payload=Encode(wireframe);if(!payload||!ValidKey(key))return;Digest digest;if(!Hash(*payload,digest))return;std::lock_guard lock(Mutex);const auto directory=Directory();if(!directory)return;
        std::filesystem::create_directories(*directory);const auto path=*directory/(std::wstring(key.begin(),key.end())+Extension);
        // If old entries cannot be removed, skip the new cache rather than
        // exceeding the budget. Neither disk access nor eviction is required
        // for successful preview display.
        if(!Trim(*directory,path,payload->size()+HeaderBytes,1))return;
        const auto id=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());temporary=*directory/(std::wstring(key.begin(),key.end())+L"."+std::wstring(id.begin(),id.end())+L".tmp");
        Bytes header;header.insert(header.end(),Magic.begin(),Magic.end());header.insert(header.end(),key.begin(),key.end());U64(header,payload->size());header.insert(header.end(),digest.begin(),digest.end());
        {std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);stream.exceptions(std::ios::badbit|std::ios::failbit);stream.write(reinterpret_cast<const char*>(header.data()),static_cast<std::streamsize>(header.size()));stream.write(reinterpret_cast<const char*>(payload->data()),static_cast<std::streamsize>(payload->size()));stream.close();}
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("CAM_PREVIEW_CACHE_WRITE_FAILED");temporary.clear();Trim(*directory);
    }catch(...){if(!temporary.empty()){std::error_code error;std::filesystem::remove(temporary,error);}}
}
}}
