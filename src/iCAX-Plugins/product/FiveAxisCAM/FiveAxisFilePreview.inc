// File chooser reads leave document entities, revisions and undo unchanged;
// wire previews are published as runtime query resources.
namespace
{
    std::filesystem::path PickerPath(const Text& text)
    {
        if (text.size() > 32767 || text.find('\0') != Text::npos)
            throw std::invalid_argument("文件路径无效");
        return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
    }
    Text PickerPathText(const std::filesystem::path& path)
    {
        const auto text = path.generic_u8string();
        return Text(reinterpret_cast<const char*>(text.data()), text.size());
    }
    bool PickerCadFile(const std::filesystem::path& path)
    {
        auto extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension == ".step" || extension == ".stp" || extension == ".iges" || extension == ".igs";
    }
    Text PickerModified(const std::filesystem::path& path)
    {
        return std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::filesystem::last_write_time(path).time_since_epoch()).count());
    }
}
#include "FiveAxisFilePreviewCache.inc"

Map ListWorkpieceFiles(Scene&, const Map& request)
{
    const auto fileType=request.contains("fileType")?String(request,"fileType"):Text("cad");
    if(fileType!="cad"&&fileType!="cutting-head")throw std::invalid_argument("不支持的文件类型");
    const auto accepted=[&](const std::filesystem::path& path){
        if(PickerCadFile(path))return true;
        if(fileType!="cutting-head")return false;
        auto extension=path.extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        return extension==".urdf"||extension==".sdf"||extension==".stl"||extension==".obj";
    };
    const auto text = request.contains("directory") ? String(request, "directory") : Text();
    std::error_code error;
    const auto directory = std::filesystem::canonical(text.empty() ? std::filesystem::current_path() : PickerPath(text), error);
    if (error || !std::filesystem::is_directory(directory)) throw std::invalid_argument("文件目录不存在或无法访问");
    Array entries;
    for (const auto& entry : std::filesystem::directory_iterator(directory, std::filesystem::directory_options::skip_permission_denied))
    {
        const bool folder = entry.is_directory(error);
        if (error) { error.clear(); continue; }
        if (!folder && (!entry.is_regular_file(error) || !accepted(entry.path()))) { error.clear(); continue; }
        Map row{{"name", PickerPathText(entry.path().filename())}, {"path", PickerPathText(entry.path())}, {"directory", folder}};
        if (!folder)
        {
            row["sizeBytes"] = static_cast<unsigned long long>(entry.file_size());
            row["lastModifiedNs"] = PickerModified(entry.path());
        }
        entries.emplace_back(std::move(row));
    }
    return {{"directory", PickerPathText(directory)}, {"parentDirectory", directory == directory.root_path() ? Text() : PickerPathText(directory.parent_path())}, {"entries", std::move(entries)}};
}

Map PreviewWorkpieceFile(Scene& scene, const Map& request)
{
    if (scene.IsStopRequested()) throw std::runtime_error("CAM_SCENE_CLOSING");
    const auto runtime = Runtime(scene);
    const auto key = String(request, "previewKey");
    if (key.empty() || key.size() > 256) throw std::invalid_argument("CAM_PREVIEW_KEY_INVALID");
    std::error_code error;
    const auto path = std::filesystem::canonical(PickerPath(String(request, "sourcePath")), error);
    if (error || !std::filesystem::is_regular_file(path)) throw std::invalid_argument("模型文件不存在或无法访问");
    if (!PickerCadFile(path)) throw std::invalid_argument("请选择 STEP 或 IGES 模型");
    const auto size = std::filesystem::file_size(path);
    const auto modified = PickerModified(path);
    const auto importer = scene.Services().Resolve<iCAX::CAD::ICadBRepImportService>();
    const auto display = scene.Services().Resolve<iCAX::GeometryServices::IBRepDisplayService>();
    if (!importer || !display) throw std::runtime_error("模型预览服务不可用");
    if (const auto old = runtime->FilePreviews.find(key); old != runtime->FilePreviews.end()) {
        old->second.Cancelled->store(true);
        DiscardQueryDocument(scene,ID(),"file-preview:"+old->second.TaskID);
        runtime->FilePreviews.erase(old);
    }
    if (runtime->FilePreviews.size() >= 8) throw std::runtime_error("CAM_PREVIEW_SESSION_LIMIT");
    FilePreviewTask task;
    const auto cancelled = task.Cancelled;
    // All parsing/evaluation and thumbnail construction run off the Scene thread.
    // Capture neutral services and values only; a closed scene never stays alive.
    task.Worker = iCAX::Tasks::Run([path, size, modified, importer, display, cancelled] {
        Text cacheKey;try{cacheKey=FilePreviewCache::Key(path,size,modified);}catch(...){}
        if(cancelled->load())throw iCAX::Tasks::TaskCanceledException();
        if(const auto cached=FilePreviewCache::Read(cacheKey)){
            if(cancelled->load())throw iCAX::Tasks::TaskCanceledException();
            if(size!=std::filesystem::file_size(path)||modified!=PickerModified(path))throw std::invalid_argument("文件在预览期间已更改，请重新选择");
            return std::make_shared<Map>(Map{{"sourcePath",PickerPathText(path)},{"sourceFileName",PickerPathText(path.filename())},{"sizeBytes",static_cast<unsigned long long>(size)},{"lastModifiedNs",modified},{"wireframe",*cached},{"previewCache",Map{{"hit",true},{"tier",Text("Disk")}}}});
        }
        iCAX::CAD::SCadBRepImportRequest input;
        input.SourcePath = path;
        input.DisplayDeflectionMm = 0.5;
        input.IsCancelled = [cancelled] { return cancelled->load(); };
        const auto model = importer->Load(input);
        iCAX::GeometryServices::BRepDisplayOptions options;
        options.Deflection = 0.03;
        options.MaxPoints = 1250000;
        options.IsCancelled = input.IsCancelled;
        const auto edges = display->BuildEdges(model.Geometry, options);
        if (edges.Edges.size() > 50000) throw std::invalid_argument("模型边线过多，无法预览");
        Array lines;
        unsigned long long pointCount = 0;
        for (const auto& edge : edges.Edges)
        {
            if (cancelled->load()) throw iCAX::Tasks::TaskCanceledException();
            if (edge.Points.size() < 2) continue;
            // The thumbnail has a bounded display budget; the imported CAD remains exact.
            const auto count = std::min<std::size_t>(edge.Points.size(), 256);
            Array coordinates;
            coordinates.reserve(count * 3);
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto& p = edge.Points[i * (edge.Points.size() - 1) / (count - 1)];
                coordinates.emplace_back(p.X); coordinates.emplace_back(p.Y); coordinates.emplace_back(p.Z);
            }
            pointCount += count;
            if (pointCount > 1250000) throw std::invalid_argument("模型边线过多，无法预览");
            lines.emplace_back(std::move(coordinates));
        }
        if (lines.empty()) throw std::invalid_argument("模型没有可预览的边线");
        if (size != std::filesystem::file_size(path) || modified != PickerModified(path))
            throw std::invalid_argument("文件在预览期间已更改，请重新选择");
        const auto edgeCount = static_cast<unsigned long long>(lines.size());
        Map wireframe{{"polylines",std::move(lines)},{"edgeCount",edgeCount},{"pointCount",pointCount}};
        if(cancelled->load())throw iCAX::Tasks::TaskCanceledException();FilePreviewCache::Write(cacheKey,wireframe);
        return std::make_shared<Map>(Map{{"sourcePath", PickerPathText(path)}, {"sourceFileName", PickerPathText(path.filename())},
            {"sizeBytes", static_cast<unsigned long long>(size)}, {"lastModifiedNs", modified},
            {"wireframe",std::move(wireframe)},{"previewCache",Map{{"hit",false},{"tier",Text("Generated")}}}});
    });
    const auto id = task.TaskID;
    runtime->FilePreviews.emplace(key, std::move(task));
    return {{"epoch",runtime->Epoch},{"taskId",id},{"status",Text("Running")}};
}

Map QueryWorkpieceFilePreview(Scene& scene, const Map& request)
{
    const auto runtime = Runtime(scene);
    if (String(request,"epoch") != runtime->Epoch) throw std::invalid_argument("CAM_STALE_EPOCH");
    const auto id = String(request,"taskId");
    const auto found = std::find_if(runtime->FilePreviews.begin(),runtime->FilePreviews.end(),
        [&](const auto& entry){return entry.second.TaskID == id;});
    if (found == runtime->FilePreviews.end()) throw std::invalid_argument("CAM_PREVIEW_NOT_FOUND");
    auto& task = found->second;
    if (task.Status == "Running" && task.Worker.IsCompleted()) {
        try { task.Result = std::move(*task.Worker.Result()); task.Status = "Completed"; }
        catch (const iCAX::Tasks::TaskCanceledException&) { task.Status = "Cancelled"; }
        catch (const std::exception& error) { task.Status = "Failed"; task.Error = error.what(); }
        task.Worker = {};
    }
    if(task.Status=="Completed"&&task.Result.contains("wireframe")){
        const auto wireframe=task.Result.at("wireframe").To<Map>();const auto ref=PublishQueryDocument(scene,ID(),"file-preview:"+id,id,Map{{"wireframe",wireframe}});
        task.Result.erase("wireframe");task.Result["wireframeResourceRef"]=WireRef(StoredRef(ref));task.Result["edgeCount"]=wireframe.at("edgeCount");task.Result["pointCount"]=wireframe.at("pointCount");
    }
    return {{"epoch",runtime->Epoch},{"taskId",id},{"status",task.Status},{"error",task.Error},{"result",task.Result}};
}

Map ReleaseWorkpieceFilePreview(Scene& scene, const Map& request)
{
    const auto runtime = Runtime(scene);
    if (String(request,"epoch") != runtime->Epoch) throw std::invalid_argument("CAM_STALE_EPOCH");
    const auto id = String(request,"taskId");
    const auto found = std::find_if(runtime->FilePreviews.begin(),runtime->FilePreviews.end(),
        [&](const auto& entry){return entry.second.TaskID == id;});
    if (found == runtime->FilePreviews.end()) return {{"released",false}};
    found->second.Cancelled->store(true);
    DiscardQueryDocument(scene,ID(),"file-preview:"+id);
    runtime->FilePreviews.erase(found);
    return {{"released",true}};
}
