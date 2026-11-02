// Disposable, product-owned derivatives of immutable source resources. Cache
// files preserve every supplied collision-mesh bit; they never replace a failed
// read with an empty collider or change the source's collision accuracy.
#include <bit>
#include <span>
#include <filesystem>
#include <fstream>
#include <mutex>
namespace {
namespace CollisionMeshDisk {
    using Mesh=iCAX::GeometryServices::CollisionMesh;
    using Bytes=std::vector<std::uint8_t>;
    using Digest=std::array<std::uint8_t,32>;
    constexpr std::array<std::uint8_t,8> Magic{'I','C','C','M','E','S','H','1'};
    constexpr std::uint32_t Schema=1;
    constexpr std::uint64_t MaximumFileBytes=256ull*1024*1024;
    constexpr std::uint64_t MaximumDirectoryBytes=2ull*1024*1024*1024;
    constexpr std::size_t MaximumFiles=512;
    constexpr std::size_t HeaderBytes=8+4+64+8+32;
    constexpr const wchar_t* Extension=L".icaxcm1";

    bool Hash(std::span<const std::uint8_t> bytes,Digest& digest){
        if(bytes.size()>std::numeric_limits<ULONG>::max())return false;
        return BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,
            const_cast<PUCHAR>(bytes.data()),static_cast<ULONG>(bytes.size()),digest.data(),static_cast<ULONG>(digest.size()))>=0;
    }
    Text Hex(const Digest& digest){
        constexpr char digits[]="0123456789abcdef";Text result;result.reserve(64);
        for(const auto byte:digest){result.push_back(digits[byte>>4]);result.push_back(digits[byte&15]);}return result;
    }
    bool ValidKey(const Text& key){return key.size()==64&&std::all_of(key.begin(),key.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
    std::optional<std::filesystem::path> EnvironmentPath(const wchar_t* name){
        const auto count=GetEnvironmentVariableW(name,nullptr,0);if(!count||count>32768)return std::nullopt;
        std::wstring value(count,L'\0');const auto written=GetEnvironmentVariableW(name,value.data(),count);
        if(!written||written>=count)return std::nullopt;value.resize(written);
        auto path=std::filesystem::path(value);if(!path.is_absolute())return std::nullopt;return path.lexically_normal();
    }
    std::optional<std::filesystem::path> Directory(){
        if(const auto overridden=EnvironmentPath(L"ICAX_CAM_COLLISION_CACHE_DIR"))return overridden;
        const auto local=EnvironmentPath(L"LOCALAPPDATA");if(!local)return std::nullopt;
        return *local/L"iCAX"/L"CollisionMeshes"/L"v1";
    }
    Text GeometryBuild(){
        static std::mutex mutex;static Text build;
        std::lock_guard lock(mutex);if(!build.empty())return build;
        try{
            // The mesher is outside this product DLL. A new geometry build must
            // not silently reuse triangulations from its previous implementation.
            const auto module=GetModuleHandleW(L"OpenCascadeResourceImport.dll");if(!module)return {};
            std::wstring name(32768,L'\0');const auto count=GetModuleFileNameW(module,name.data(),static_cast<DWORD>(name.size()));
            if(!count||count>=name.size())return {};name.resize(count);
            const std::filesystem::path path(name);const auto size=std::filesystem::file_size(path);
            if(!size||size>MaximumFileBytes)return {};
            Bytes bytes(static_cast<std::size_t>(size));std::ifstream file(path,std::ios::binary);
            if(!file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())))return {};
            Digest digest{};if(Hash(bytes,digest))build=Hex(digest);return build;
        }catch(...){return {};}
    }
    void U32(Bytes& bytes,std::uint32_t value){for(int shift=0;shift<32;shift+=8)bytes.push_back(static_cast<std::uint8_t>(value>>shift));}
    void U64(Bytes& bytes,std::uint64_t value){for(int shift=0;shift<64;shift+=8)bytes.push_back(static_cast<std::uint8_t>(value>>shift));}
    struct Reader {
        std::span<const std::uint8_t> Bytes;std::size_t Offset=0;
        void Require(bool condition)const{if(!condition)throw std::runtime_error("CAM_COLLISION_CACHE_INVALID");}
        std::uint32_t U32(){Require(Offset<=Bytes.size()&&Bytes.size()-Offset>=4);std::uint32_t value=0;for(int shift=0;shift<32;shift+=8)value|=static_cast<std::uint32_t>(Bytes[Offset++])<<shift;return value;}
        std::uint64_t U64(){Require(Offset<=Bytes.size()&&Bytes.size()-Offset>=8);std::uint64_t value=0;for(int shift=0;shift<64;shift+=8)value|=static_cast<std::uint64_t>(Bytes[Offset++])<<shift;return value;}
        std::uint64_t Count(std::uint64_t stride){const auto count=U64();Require(stride&&count<=(Bytes.size()-Offset)/stride);return count;}
        double Real(){const auto value=std::bit_cast<double>(U64());Require(std::isfinite(value));return value;}
    };
    bool ValidMesh(const Mesh& mesh){
        if(mesh.Vertices.empty()||mesh.Triangles.empty()||mesh.Vertices.size()>std::numeric_limits<unsigned>::max()
            ||mesh.Triangles.size()>std::numeric_limits<unsigned>::max()||mesh.ClosedVolumes.size()>1000000||!std::isfinite(mesh.Padding)||mesh.Padding<0||mesh.Padding>1e9)return false;
        for(const auto& vertex:mesh.Vertices)for(double value:vertex)if(!std::isfinite(value))return false;
        for(const auto& triangle:mesh.Triangles)for(auto index:triangle)if(index>=mesh.Vertices.size())return false;
        for(const auto& volume:mesh.ClosedVolumes){if(volume.empty()||volume.size()>mesh.Triangles.size())return false;for(auto index:volume)if(index>=mesh.Triangles.size())return false;}
        return true;
    }
    bool CanAdd(std::uint64_t& bytes,std::uint64_t count,std::uint64_t stride){
        if(bytes>MaximumFileBytes||count>(MaximumFileBytes-bytes)/stride)return false;bytes+=count*stride;return true;
    }
    std::optional<Bytes> Encode(const Mesh& mesh){
        if(!ValidMesh(mesh))return std::nullopt;
        std::uint64_t bytes=32;
        if(!CanAdd(bytes,mesh.Vertices.size(),24)||!CanAdd(bytes,mesh.Triangles.size(),12)||!CanAdd(bytes,mesh.ClosedVolumes.size(),8))return std::nullopt;
        for(const auto& volume:mesh.ClosedVolumes)if(!CanAdd(bytes,volume.size(),4))return std::nullopt;
        if(bytes>MaximumFileBytes-HeaderBytes)return std::nullopt;
        Bytes result;result.reserve(static_cast<std::size_t>(bytes));
        U64(result,mesh.Vertices.size());for(const auto& vertex:mesh.Vertices)for(double value:vertex)U64(result,std::bit_cast<std::uint64_t>(value));
        U64(result,mesh.Triangles.size());for(const auto& triangle:mesh.Triangles)for(auto index:triangle)U32(result,index);
        U64(result,std::bit_cast<std::uint64_t>(mesh.Padding));U64(result,mesh.ClosedVolumes.size());
        for(const auto& volume:mesh.ClosedVolumes){U64(result,volume.size());for(auto index:volume)U32(result,index);}return result;
    }
    std::shared_ptr<const Mesh> Decode(std::span<const std::uint8_t> bytes){
        Reader reader{bytes};auto mesh=std::make_shared<Mesh>();
        const auto vertices=reader.Count(24);reader.Require(vertices>0&&vertices<=std::numeric_limits<unsigned>::max());mesh->Vertices.resize(static_cast<std::size_t>(vertices));
        for(auto& vertex:mesh->Vertices)for(auto& value:vertex)value=reader.Real();
        const auto triangles=reader.Count(12);reader.Require(triangles>0&&triangles<=std::numeric_limits<unsigned>::max());mesh->Triangles.resize(static_cast<std::size_t>(triangles));
        for(auto& triangle:mesh->Triangles)for(auto& index:triangle){index=reader.U32();reader.Require(index<vertices);}
        mesh->Padding=reader.Real();reader.Require(mesh->Padding>=0&&mesh->Padding<=1e9);
        const auto volumes=reader.Count(8);reader.Require(volumes<=1000000);mesh->ClosedVolumes.resize(static_cast<std::size_t>(volumes));
        for(auto& volume:mesh->ClosedVolumes){const auto indices=reader.Count(4);reader.Require(indices>0&&indices<=triangles);volume.resize(static_cast<std::size_t>(indices));for(auto& index:volume){index=reader.U32();reader.Require(index<triangles);}}
        reader.Require(reader.Offset==bytes.size());return mesh;
    }
    bool OwnedFile(const std::filesystem::path& path){
        if(path.extension()!=Extension)return false;const auto stem=path.stem().string();if(!ValidKey(stem))return false;
        std::ifstream file(path,std::ios::binary);std::array<std::uint8_t,76> prefix{};
        if(!file.read(reinterpret_cast<char*>(prefix.data()),prefix.size()))return false;
        if(!std::equal(Magic.begin(),Magic.end(),prefix.begin()))return false;Reader reader{prefix};reader.Offset=8;
        return reader.U32()==Schema&&std::equal(stem.begin(),stem.end(),prefix.begin()+12);
    }
    void Trim(const std::filesystem::path& directory,const std::filesystem::path& protectedPath){
        struct Item{std::filesystem::path Path;std::uint64_t Bytes;std::filesystem::file_time_type Used;};std::vector<Item> items;std::uint64_t total=0;
        std::error_code error;for(std::filesystem::directory_iterator iterator(directory,error),end;!error&&iterator!=end;iterator.increment(error)){
            const auto& entry=*iterator;const auto status=entry.symlink_status(error);if(error)break;
            if(status.type()!=std::filesystem::file_type::regular||!OwnedFile(entry.path()))continue;
            const auto size=entry.file_size(error);if(error)break;const auto used=entry.last_write_time(error);if(error)break;
            if(total>std::numeric_limits<std::uint64_t>::max()-size)return;total+=size;items.push_back({entry.path(),size,used});
        }
        if(error||(items.size()<=MaximumFiles&&total<=MaximumDirectoryBytes))return;
        std::sort(items.begin(),items.end(),[](const auto& a,const auto& b){return a.Used<b.Used;});auto count=items.size();
        for(const auto& item:items){if(count<=MaximumFiles&&total<=MaximumDirectoryBytes)break;if(item.Path==protectedPath)continue;
            // The iterator is nonrecursive and only our exact filename/header
            // format is eligible; unrelated user files and symlinks stay intact.
            if(std::filesystem::remove(item.Path,error)){total-=item.Bytes;--count;}error.clear();}
    }
}
    Text CollisionMeshDiskKey(Scene& scene,const Map& reference,const iCAX::GeometryData::BRepModel& geometry,double deflection){
        try{
            if(!std::isfinite(deflection)||deflection<=0)return {};
            const auto build=CollisionMeshDisk::GeometryBuild();if(build.empty())return {};
            const auto source=ReadWireRef(WireRef(reference));const auto info=scene.Resources().GetInfo(source.URL,source.nVersion);if(!info)return {};
            // ResourceInfo::ContentHash may be absent until the first save,
            // or describe the imported source file rather than this BRep body.
            // Canonical body bytes keep an unchanged source stable across save
            // and reopen, and also reject unversioned custom-resource changes.
            const auto canonical=iCAX::GeometryData::Persistence::Serialize(geometry);CollisionMeshDisk::Digest sourceHash{};
            if(!CollisionMeshDisk::Hash(canonical,sourceHash))return {};const auto contentHash=CollisionMeshDisk::Hex(sourceHash);
            const auto identity=iCAX::Data::VariantSerializer::Serialize(Map{{"algorithm",Text("TriangleMeshV1")},{"cacheSchema",Text("1")},{"productBuild",Text(__DATE__ " " __TIME__)},
                {"geometryBuild",build},{"source",WireRef(reference)},{"sourceContentHash",contentHash},{"deflectionBits",std::to_string(std::bit_cast<std::uint64_t>(deflection))}});
            CollisionMeshDisk::Digest hash{};const std::span<const std::uint8_t> bytes{reinterpret_cast<const std::uint8_t*>(identity.data()),identity.size()};
            return CollisionMeshDisk::Hash(bytes,hash)?CollisionMeshDisk::Hex(hash):Text{};
        }catch(...){return {};}
    }
    std::shared_ptr<const iCAX::GeometryServices::CollisionMesh> ReadCollisionMeshDiskCache(const Text& key){
        try{
            if(!CollisionMeshDisk::ValidKey(key))return {};const auto directory=CollisionMeshDisk::Directory();if(!directory)return {};
            const auto path=*directory/(std::wstring(key.begin(),key.end())+CollisionMeshDisk::Extension);
            std::error_code error;if(std::filesystem::symlink_status(path,error).type()!=std::filesystem::file_type::regular||error)return {};
            const auto size=std::filesystem::file_size(path);if(size<CollisionMeshDisk::HeaderBytes||size>CollisionMeshDisk::MaximumFileBytes)return {};
            CollisionMeshDisk::Bytes bytes(static_cast<std::size_t>(size));std::ifstream file(path,std::ios::binary);
            if(!file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())))return {};
            if(!std::equal(CollisionMeshDisk::Magic.begin(),CollisionMeshDisk::Magic.end(),bytes.begin()))return {};
            CollisionMeshDisk::Reader reader{bytes};reader.Offset=8;if(reader.U32()!=CollisionMeshDisk::Schema||!std::equal(key.begin(),key.end(),bytes.begin()+12))return {};reader.Offset+=64;
            const auto payloadBytes=reader.U64();if(payloadBytes!=size-CollisionMeshDisk::HeaderBytes)return {};
            CollisionMeshDisk::Digest expected{};std::copy_n(bytes.begin()+reader.Offset,expected.size(),expected.begin());reader.Offset+=expected.size();
            const std::span<const std::uint8_t> payload{bytes.data()+reader.Offset,static_cast<std::size_t>(payloadBytes)};CollisionMeshDisk::Digest actual{};
            if(!CollisionMeshDisk::Hash(payload,actual)||actual!=expected)return {};auto mesh=CollisionMeshDisk::Decode(payload);
            std::filesystem::last_write_time(path,std::filesystem::file_time_type::clock::now(),error);return mesh;
        }catch(...){return {};}
    }
    void WriteCollisionMeshDiskCache(const Text& key,const iCAX::GeometryServices::CollisionMesh& mesh){
        std::filesystem::path temporary;
        try{
            if(!CollisionMeshDisk::ValidKey(key))return;const auto directory=CollisionMeshDisk::Directory();if(!directory)return;
            const auto payload=CollisionMeshDisk::Encode(mesh);if(!payload)return;CollisionMeshDisk::Digest checksum{};if(!CollisionMeshDisk::Hash(*payload,checksum))return;
            std::filesystem::create_directories(*directory);const auto path=*directory/(std::wstring(key.begin(),key.end())+CollisionMeshDisk::Extension);
            const auto id=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());temporary=*directory/(std::wstring(key.begin(),key.end())+L"."+std::wstring(id.begin(),id.end())+L".tmp");
            CollisionMeshDisk::Bytes header;header.insert(header.end(),CollisionMeshDisk::Magic.begin(),CollisionMeshDisk::Magic.end());CollisionMeshDisk::U32(header,CollisionMeshDisk::Schema);
            header.insert(header.end(),key.begin(),key.end());CollisionMeshDisk::U64(header,payload->size());header.insert(header.end(),checksum.begin(),checksum.end());
            {std::ofstream file(temporary,std::ios::binary|std::ios::trunc);file.exceptions(std::ios::badbit|std::ios::failbit);file.write(reinterpret_cast<const char*>(header.data()),static_cast<std::streamsize>(header.size()));file.write(reinterpret_cast<const char*>(payload->data()),static_cast<std::streamsize>(payload->size()));file.close();}
            // An atomic same-directory replacement also repairs a corrupt prior
            // entry, while readers see either one complete file or the other.
            if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("CAM_COLLISION_CACHE_REPLACE_FAILED");
            temporary.clear();CollisionMeshDisk::Trim(*directory,path);
        }catch(...){if(!temporary.empty()){std::error_code error;std::filesystem::remove(temporary,error);}}
    }
}
