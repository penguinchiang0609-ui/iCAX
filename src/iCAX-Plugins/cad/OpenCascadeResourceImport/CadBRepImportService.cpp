#include "pch.h"
#include "CadBRepImportService.h"
#include "OpenCascadeBRepReader.h"
#include "OpenCascadeCancellation.h"
#include "OpenCascadeIgesSession.h"
#include "OpenCascadeVisibleImport.h"
#include "OpenCascadeTopologyPreparation.h"
#include "Services/ServicesHelper.h"
#include <BRepCheck_Analyzer.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <IGESData_IGESModel.hxx>
#include <Message_ProgressIndicator.hxx>
#include <RWStl.hxx>
#include <RWObj_TriangulationReader.hxx>
#include <bcrypt.h>
#include <charconv>
#include <cmath>
#include <limits>
#include "GeometryData/BRepPersistence.h"
#include <deque>
#include <memory>
#include <mutex>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "TKDEOBJ.lib")

namespace
{
    using iCAX::OpenCascade::COpenCascadeCancellationScope;

    struct CExactImportSnapshots final
    {
        struct Entry {
            std::filesystem::path Path;
            std::shared_ptr<const iCAX::CAD::SCadBRepImportResult> Result;
            std::size_t PayloadBytes=0;
        };
        std::mutex Mutex;
        std::deque<Entry> Entries;
        std::size_t PayloadBytes=0;
        static constexpr std::size_t Budget=64ull*1024*1024;
    };
    CExactImportSnapshots& ExactImportSnapshots()
    {
        static CExactImportSnapshots snapshots;return snapshots;
    }
    std::shared_ptr<const iCAX::CAD::SCadBRepImportResult> FindExactImport(
        const std::filesystem::path& path,const iCAX::CAD::SCadBRepImportResult& source)
    {
        auto& snapshots=ExactImportSnapshots();std::lock_guard lock(snapshots.Mutex);
        for(auto it=snapshots.Entries.begin();it!=snapshots.Entries.end();++it) {
            if(it->Path==path&&it->Result->SourceSha256==source.SourceSha256
                &&it->Result->SourceBytes==source.SourceBytes) {
                auto entry=*it;snapshots.Entries.erase(it);snapshots.Entries.push_front(entry);
                return entry.Result;
            }
        }
        return {};
    }
    void RememberExactImport(const std::filesystem::path& path,const iCAX::CAD::SCadBRepImportResult& result)
    {
        const auto bytes=iCAX::GeometryData::Persistence::Serialize(result.Geometry).size()+result.SourceBytes.size();
        if(bytes>CExactImportSnapshots::Budget)return;
        auto snapshot=std::make_shared<const iCAX::CAD::SCadBRepImportResult>(result);
        auto& snapshots=ExactImportSnapshots();std::lock_guard lock(snapshots.Mutex);
        for(auto it=snapshots.Entries.begin();it!=snapshots.Entries.end();) {
            if(it->Path==path){snapshots.PayloadBytes-=it->PayloadBytes;it=snapshots.Entries.erase(it);}
            else ++it;
        }
        while(!snapshots.Entries.empty()&&(snapshots.Entries.size()>=2
            ||snapshots.PayloadBytes+bytes>CExactImportSnapshots::Budget)) {
            snapshots.PayloadBytes-=snapshots.Entries.back().PayloadBytes;snapshots.Entries.pop_back();
        }
        snapshots.Entries.push_front({path,std::move(snapshot),bytes});snapshots.PayloadBytes+=bytes;
    }

    std::string Utf8(const std::filesystem::path& path)
    {
        const auto value = path.u8string();
        return {reinterpret_cast<const char*>(value.data()), value.size()};
    }

    struct CSourceHandle final
    {
        HANDLE Value = INVALID_HANDLE_VALUE;
        explicit CSourceHandle(const std::filesystem::path& path)
            : Value(::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr))
        {
            if (Value == INVALID_HANDLE_VALUE)
                throw std::runtime_error("CAD_SOURCE_UNAVAILABLE: cannot open or freeze source file");
        }
        ~CSourceHandle() { ::CloseHandle(Value); }
        CSourceHandle(const CSourceHandle&) = delete;
        CSourceHandle& operator=(const CSourceHandle&) = delete;
    };

    std::vector<std::uint8_t> ReadFrozenSource(HANDLE handle, std::uint64_t limit)
    {
        LARGE_INTEGER size{};
        if (!::GetFileSizeEx(handle, &size) || size.QuadPart <= 0
            || static_cast<std::uint64_t>(size.QuadPart) > limit)
            throw std::invalid_argument("CAD_SOURCE_SIZE: source is empty or exceeds import budget");
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size.QuadPart));
        std::size_t offset = 0;
        while (offset < bytes.size())
        {
            COpenCascadeCancellationScope::ThrowIfCancellationRequested();
            const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(1024 * 1024, bytes.size() - offset));
            DWORD read = 0;
            if (!::ReadFile(handle, bytes.data() + offset, requested, &read, nullptr) || !read)
                throw std::runtime_error("CAD_SOURCE_READ_FAILED: incomplete source snapshot");
            offset += read;
        }
        return bytes;
    }

    std::string Sha256(const std::vector<std::uint8_t>& bytes)
    {
        BCRYPT_ALG_HANDLE provider = nullptr;
        if (BCryptOpenAlgorithmProvider(&provider, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            throw std::runtime_error("CAD_SOURCE_HASH_FAILED: SHA-256 provider unavailable");
        std::array<unsigned char, 32> digest{};
        const auto status = BCryptHash(provider, nullptr, 0,
            const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()),
            digest.data(), static_cast<ULONG>(digest.size()));
        BCryptCloseAlgorithmProvider(provider, 0);
        if (status < 0) throw std::runtime_error("CAD_SOURCE_HASH_FAILED: SHA-256 failed");
        std::ostringstream text;
        text << std::hex << std::setfill('0');
        for (const auto value : digest) text << std::setw(2) << static_cast<unsigned int>(value);
        return text.str();
    }

    class CImportProgress final : public Message_ProgressIndicator
    {
    public:
        explicit CImportProgress(std::function<bool()> cancelled) : m_Cancelled(std::move(cancelled)) {}
    protected:
        bool UserBreak() override { return m_Cancelled && m_Cancelled(); }
        void Show(const Message_ProgressScope&, bool) override {}
    private:
        const std::function<bool()> m_Cancelled;
    };

    // OCCT repairs IGES unit flags 0/3 from the unit name and even defaults an
    // absent name to inches. Inspect the source declaration before that repair;
    // manufacturing import must not silently invent a physical scale.
    int DeclaredIgesUnit(const std::vector<std::uint8_t>& bytes)
    {
        std::string globals;
        std::istringstream source(std::string(bytes.begin(), bytes.end()));
        for (std::string line; std::getline(source, line);)
        {
            if (line.size() > 72 && line[72] == 'G')
            {
                globals.append(line, 0, 72);
                if (globals.size() > 1024 * 1024)
                    throw std::invalid_argument("CAD_IGES_HEADER: global section exceeds budget");
            }
            else if (!globals.empty()) break;
        }
        if (globals.empty())
            throw std::invalid_argument("CAD_IGES_HEADER: missing IGES global section; source is not a readable IGES document");
        auto fail = [] { throw std::invalid_argument("CAD_UNIT_REQUIRED: IGES requires an explicit standard length unit"); };
        std::size_t cursor = 0;
        char separator = ',';
        auto field = [&]() -> std::string {
            while (cursor < globals.size() && globals[cursor] == ' ') ++cursor;
            const auto start = cursor;
            while (cursor < globals.size() && std::isdigit(static_cast<unsigned char>(globals[cursor]))) ++cursor;
            if (cursor > start && cursor < globals.size() && (globals[cursor] == 'H' || globals[cursor] == 'h'))
            {
                std::size_t length = 0;
                if (std::from_chars(globals.data() + start, globals.data() + cursor, length).ec != std::errc()) fail();
                ++cursor;
                if (length > globals.size() - cursor) fail();
                auto result = globals.substr(cursor, length);
                cursor += length;
                while (cursor < globals.size() && globals[cursor] == ' ') ++cursor;
                return result;
            }
            cursor = start;
            while (cursor < globals.size() && globals[cursor] != separator) ++cursor;
            auto result = globals.substr(start, cursor - start);
            while (!result.empty() && result.back() == ' ') result.pop_back();
            return result;
        };
        auto delimiter = field();
        if (!delimiter.empty())
        {
            if (delimiter.size() != 1) fail();
            separator = delimiter.front();
        }
        std::string unit;
        for (int index = 2; index <= 14; ++index)
        {
            if (cursor >= globals.size() || globals[cursor++] != separator) fail();
            unit = field();
        }
        int flag = 0;
        const auto parsed = std::from_chars(unit.data(), unit.data() + unit.size(), flag);
        if (parsed.ec != std::errc() || parsed.ptr != unit.data() + unit.size()
            || flag < 1 || flag > 11 || flag == 3) fail();
        return flag;
    }

    template<class TReader>
    TopoDS_Shape Transfer(TReader& reader, iCAX::CAD::SCadBRepImportResult& result)
    {
        COpenCascadeCancellationScope::ThrowIfCancellationRequested();
        const auto expectedRoots = reader.NbRootsForTransfer();
        occ::handle<Message_ProgressIndicator> progress = new CImportProgress(COpenCascadeCancellationScope::Capture());
        const auto transferred = reader.TransferRoots(progress->Start());
        COpenCascadeCancellationScope::ThrowIfCancellationRequested();
        if (expectedRoots <= 0 || transferred != expectedRoots)
            throw std::runtime_error("CAD_TRANSFER_INCOMPLETE: transferred " + std::to_string(transferred)
                + " of " + std::to_string(expectedRoots) + " source roots");
        result.TransferredRoots = static_cast<std::uint32_t>(transferred);
        auto shape = reader.OneShape();
        if (shape.IsNull()) throw std::runtime_error("CAD_EMPTY_GEOMETRY: no geometry was transferred");
        return shape;
    }

    class CCadBRepImportService final : public iCAX::CAD::ICadBRepImportService
    {
        AUTO_REGIST_SERVICE(iCAX::CAD::ICadBRepImportService, CCadBRepImportService)
    public:
        void OnLoad() override {}
        void OnUnload() override {}

        iCAX::CAD::SCadBRepImportResult Load(const iCAX::CAD::SCadBRepImportRequest& request) const override
        {
            if (!request.SourcePath.is_absolute())
                throw std::invalid_argument("CAD_SOURCE_PATH: an absolute source path is required");
            if (!std::isfinite(request.DisplayDeflectionMm) || request.DisplayDeflectionMm <= 0
                || !request.MaximumSourceBytes || request.MaximumSourceBytes > std::numeric_limits<ULONG>::max())
                throw std::invalid_argument("CAD_IMPORT_OPTIONS: invalid deflection or byte budget");
            auto extension = Utf8(request.SourcePath.extension());
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const bool step = extension == ".step" || extension == ".stp";
            const bool mesh = extension == ".stl" || extension == ".obj";
            if (!step && !mesh && extension != ".iges" && extension != ".igs")
                throw std::invalid_argument("CAD_FORMAT_UNSUPPORTED: expected STEP, IGES, STL or OBJ");

            COpenCascadeCancellationScope cancellation(request.IsCancelled);
            COpenCascadeCancellationScope::ThrowIfCancellationRequested();
            // Deny writes/deletion until parsing has ended, including IGES whose
            // work library only implements file input in the pinned OCCT version.
            CSourceHandle source(request.SourcePath);
            iCAX::CAD::SCadBRepImportResult result;
            result.SourceBytes = ReadFrozenSource(source.Value, request.MaximumSourceBytes);
            result.SourceSha256 = Sha256(result.SourceBytes);
            result.DisplayName = Utf8(request.SourcePath.filename());
            if (mesh) {
                // Mesh files are neutral display geometry, never a fabricated
                // surface/solid BRep. Their unitless coordinates are kept in mm.
                occ::handle<Poly_Triangulation> triangulation;
                const auto path=Utf8(request.SourcePath);
                try {
                    if(extension==".stl")triangulation=RWStl::ReadFile(path.c_str());
                    else {
                        RWObj_TriangulationReader reader;reader.SetCreateShapes(false);
                        reader.SetMemoryLimit(request.MaximumSourceBytes*4);
                        std::istringstream input(std::string(result.SourceBytes.begin(),result.SourceBytes.end()));
                        if(!reader.Read(input,TCollection_AsciiString(path.c_str()),Message_ProgressRange()))
                            throw std::runtime_error("CAD_MESH_READ_FAILED: OBJ parsing failed");
                        triangulation=reader.GetTriangulation();
                    }
                    COpenCascadeCancellationScope::ThrowIfCancellationRequested();
                    if(triangulation.IsNull()||!triangulation->NbNodes()||!triangulation->NbTriangles())
                        throw std::runtime_error("CAD_EMPTY_GEOMETRY: no mesh triangles");
                    iCAX::GeometryData::Triangulation3Record record;record.Id=1;record.Metadata.Name=result.DisplayName;
                    auto& geometry=record.Geometry;
                    const auto largest=std::numeric_limits<double>::max();
                    result.Bounds.Min={largest,largest,largest};result.Bounds.Max={-largest,-largest,-largest};
                    for(int i=1;i<=triangulation->NbNodes();++i){
                        if((i&4095)==0)COpenCascadeCancellationScope::ThrowIfCancellationRequested();
                        const auto point=triangulation->Node(i);const double x=point.X(),y=point.Y(),z=point.Z();
                        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))throw std::runtime_error("CAD_MESH_POINT_INVALID");
                        geometry.Vertices.push_back({x,y,z});
                        result.Bounds.Min.X=std::min(result.Bounds.Min.X,x);result.Bounds.Max.X=std::max(result.Bounds.Max.X,x);
                        result.Bounds.Min.Y=std::min(result.Bounds.Min.Y,y);result.Bounds.Max.Y=std::max(result.Bounds.Max.Y,y);
                        result.Bounds.Min.Z=std::min(result.Bounds.Min.Z,z);result.Bounds.Max.Z=std::max(result.Bounds.Max.Z,z);
                    }
                    for(int i=1;i<=triangulation->NbTriangles();++i){int a,b,c;triangulation->Triangle(i).Get(a,b,c);geometry.Triangles.push_back({static_cast<std::uint32_t>(a-1),static_cast<std::uint32_t>(b-1),static_cast<std::uint32_t>(c-1)});}
                    result.Geometry.Triangulations3.push_back(std::move(record));result.Bounds.Empty=false;
                    result.FormatId=extension==".stl"?"geometry.stl":"geometry.obj";result.SourceLengthUnits={"unitless"};result.TransferredRoots=1;
                    result.Diagnostics.emplace_back("Mesh coordinates interpreted as mm; no surface or solid topology is present.");
                    return result;
                }catch(const Standard_Failure& error){throw std::runtime_error(std::string("CAD_MESH_READ_FAILED: ")+error.what());}
            }
            result.FormatId = step ? "cad.step" : "cad.iges";
            // File selection and import read the same source. Every call still
            // freezes and compares ALL current bytes under the source handle;
            // timestamps are not a validity proof. Returned models are copies.
            if(!request.IncludeDisplayMesh) {
                if(const auto snapshot=FindExactImport(request.SourcePath,result)) {
                    auto reused=*snapshot;
                    COpenCascadeCancellationScope::ThrowIfCancellationRequested();
                    return reused;
                }
            }
            const auto path = Utf8(request.SourcePath);
            TopoDS_Shape shape;
            const char* stage = "read";
            try
            {
                if (step)
                {
                    const auto parameters = iCAX::OpenCascade::CaptureStepReadParameters();
                    STEPCAFControl_Reader caf;
                    auto& reader = caf.ChangeReader();
                    std::istringstream stream(std::string(result.SourceBytes.begin(), result.SourceBytes.end()));
                    if (reader.ReadStream(path.c_str(), parameters, stream) != IFSelect_RetDone)
                        throw std::runtime_error("CAD_PARSE_FAILED: STEP parse failed");
                    NCollection_Sequence<TCollection_AsciiString> lengths, angles, solidAngles;
                    reader.FileUnits(lengths, angles, solidAngles);
                    for (const auto& name : lengths)
                    {
                        if (name.IsEmpty()) throw std::invalid_argument("CAD_UNIT_REQUIRED: missing STEP length unit");
                        result.SourceLengthUnits.emplace_back(name.ToCString());
                    }
                    if (result.SourceLengthUnits.empty())
                        throw std::invalid_argument("CAD_UNIT_REQUIRED: STEP length unit is not declared");
                    reader.SetSystemLengthUnit(1.0); // OCCT system length unit in millimetres.
                    stage = "STEP transfer";
                    occ::handle<Message_ProgressIndicator> progress = new CImportProgress(COpenCascadeCancellationScope::Capture());
                    const auto transferred = iCAX::OpenCascade::TransferVisibleStep(caf, progress->Start());
                    shape = transferred.Shape;
                    result.TransferredRoots = transferred.TransferredRoots;
                    if (transferred.HiddenLabels)
                        result.Diagnostics.emplace_back("CAD_SOURCE_VISIBILITY_FILTERED: ignored "
                            + std::to_string(transferred.HiddenLabels) + " hidden STEP objects");
                }
                else
                {
                    const auto declaredUnit = DeclaredIgesUnit(result.SourceBytes);
                    const auto session = iCAX::OpenCascade::AcquireIgesSession();
                    IGESControl_Reader reader;
                    reader.SetReadVisible(true);
                    if (reader.ReadFile(path.c_str()) != IFSelect_RetDone || reader.IGESModel().IsNull())
                        throw std::runtime_error("CAD_PARSE_FAILED: IGES parse failed");
                    auto& global = reader.IGESModel()->ChangeGlobalSection();
                    const auto flag = global.UnitFlag();
                    // Flag 3 is an arbitrary user unit: the name alone is not a scale.
                    if (flag != declaredUnit)
                        throw std::invalid_argument("CAD_UNIT_REQUIRED: IGES length unit has no known scale");
                    global.SetCascadeUnit(1.0);
                    if (!std::isfinite(global.UnitValue()) || global.UnitValue() <= 0
                        || !std::isfinite(global.Scale()) || global.Scale() <= 0)
                        throw std::invalid_argument("CAD_UNIT_REQUIRED: invalid IGES length unit or scale");
                    result.SourceLengthUnits.emplace_back(global.UnitName().IsNull()
                        ? "IGES:" + std::to_string(flag) : global.UnitName()->ToCString());
                    if (reader.NbRootsForTransfer() <= 0)
                        throw std::runtime_error("CAD_EMPTY_VISIBLE_GEOMETRY: source contains no visible geometry");
                    shape = Transfer(reader, result);
                }
                COpenCascadeCancellationScope::ThrowIfCancellationRequested();
                Bnd_Box bounds;
                stage = "bounds";
                BRepBndLib::AddOptimal(shape, bounds, false, false);
                if (bounds.IsVoid() || bounds.IsOpen())
                    throw std::runtime_error("CAD_BOUNDS_INVALID: expected finite geometry bounds");
                bounds.Get(result.Bounds.Min.X, result.Bounds.Min.Y, result.Bounds.Min.Z,
                           result.Bounds.Max.X, result.Bounds.Max.Y, result.Bounds.Max.Z);
                result.Bounds.Empty = false;
                stage = "BRep validation";
                result.IsValidBRep = BRepCheck_Analyzer(shape).IsValid();
                result.SourceIsValidBRep = result.IsValidBRep;
                if(!result.IsValidBRep) {
                    const auto prepared=iCAX::OpenCascade::topology_detail::Prepare(shape);
                    result.Diagnostics.insert(result.Diagnostics.end(),prepared.Diagnostics.begin(),prepared.Diagnostics.end());
                    if(prepared.Valid){shape=prepared.Shape;result.IsValidBRep=true;}
                }
                if (!result.IsValidBRep)
                    throw std::runtime_error("CAD_INVALID_BREP: source requires explicit geometry preparation");
                stage = "neutral BRep conversion";
                result.Geometry = request.IncludeDisplayMesh
                    ? iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(
                        shape, result.DisplayName, result.SourceSha256, request.DisplayDeflectionMm)
                    : iCAX::OpenCascade::ConvertOpenCascadeShapeToBRepWithDisplay(
                        shape, result.DisplayName, result.SourceSha256,
                        [](const TopoDS_Face&, std::uint64_t) { return iCAX::GeometryData::Triangulation3{}; });
                COpenCascadeCancellationScope::ThrowIfCancellationRequested();
                if (result.Geometry.Faces.empty())
                    throw std::runtime_error("CAD_EMPTY_GEOMETRY: no surface or solid geometry");
                if(!request.IncludeDisplayMesh)RememberExactImport(request.SourcePath,result);
                return result;
            }
            catch (const Standard_Failure& error)
            {
                COpenCascadeCancellationScope::ThrowIfCancellationRequested();
                throw std::runtime_error(std::string("CAD_KERNEL_FAILED: ") + stage + ": " + error.what());
            }
        }
    };
}
