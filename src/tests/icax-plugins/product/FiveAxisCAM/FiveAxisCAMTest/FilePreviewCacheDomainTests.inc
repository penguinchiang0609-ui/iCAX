namespace {
struct PreviewCacheSandbox {
    std::filesystem::path Root,Temp;std::wstring Previous;
    PreviewCacheSandbox(){Temp=std::filesystem::temp_directory_path();const auto id=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());Root=Temp/("icax-preview-cache-test-"+id);std::filesystem::create_directories(Root);const auto count=GetEnvironmentVariableW(L"ICAX_CAM_PREVIEW_CACHE_DIR",nullptr,0);if(count){Previous.resize(count);GetEnvironmentVariableW(L"ICAX_CAM_PREVIEW_CACHE_DIR",Previous.data(),count);Previous.resize(count-1);}SetEnvironmentVariableW(L"ICAX_CAM_PREVIEW_CACHE_DIR",Root.c_str());}
    ~PreviewCacheSandbox(){SetEnvironmentVariableW(L"ICAX_CAM_PREVIEW_CACHE_DIR",Previous.empty()?nullptr:Previous.c_str());std::error_code error;if(Root.parent_path()==Temp&&Root.filename().wstring().starts_with(L"icax-preview-cache-test-"))std::filesystem::remove_all(Root,error);}
    std::vector<std::filesystem::path> Entries()const{std::vector<std::filesystem::path> result;for(const auto& e:std::filesystem::directory_iterator(Root))if(e.path().extension()==L".icaxfp1")result.push_back(e.path());return result;}
};
Map CachedPreview(const File& source){Scene scene;const auto initial=Read(scene),task=Invoke(&scene,"CamWorkpiece","PreviewFile",PreviewRequest(source));const auto result=WaitPreview(scene,task);if(result.at("status").To<Text>()!="Completed")throw std::runtime_error(iCAX::Data::VariantSerializer::Serialize(result));EXPECT_EQ(Read(scene),initial);EXPECT_FALSE(scene.Database().Undo());Invoke(&scene,"CamWorkpiece","ReleaseFilePreview",PreviewIdentity(task));return result.at("result").To<Map>();}
}
TEST(FiveAxisFilePreviewCache, CrossSceneHitAndFileModificationUseSystemCacheWithoutChangingCad){
    PreviewCacheSandbox cache;File source;const auto first=CachedPreview(source);EXPECT_FALSE(first.at("previewCache").To<Map>().at("hit").To<bool>());ASSERT_EQ(cache.Entries().size(),1u);
    const auto second=CachedPreview(source);EXPECT_TRUE(second.at("previewCache").To<Map>().at("hit").To<bool>());EXPECT_EQ(first.at("wireframe"),second.at("wireframe"));
    {std::ofstream stream(source.Path,std::ios::binary|std::ios::app);stream<<"\n";}
    const auto changed=CachedPreview(source);EXPECT_FALSE(changed.at("previewCache").To<Map>().at("hit").To<bool>());EXPECT_EQ(changed.at("wireframe"),first.at("wireframe"));RecordProperty("cacheDirectory",cache.Root.string());
}
TEST(FiveAxisFilePreviewCache, CorruptionRebuildsAndOldEntriesRemainReusable){
    PreviewCacheSandbox cache;File source;const auto first=CachedPreview(source);auto entry=cache.Entries().front();{std::fstream file(entry,std::ios::binary|std::ios::in|std::ios::out);file.seekp(-1,std::ios::end);file.put('\x7f');}
    EXPECT_FALSE(CachedPreview(source).at("previewCache").To<Map>().at("hit").To<bool>());EXPECT_TRUE(CachedPreview(source).at("previewCache").To<Map>().at("hit").To<bool>());
    entry=cache.Entries().front();std::filesystem::last_write_time(entry,std::filesystem::file_time_type::clock::now()-std::chrono::hours(24*80));const auto old=CachedPreview(source);EXPECT_TRUE(old.at("previewCache").To<Map>().at("hit").To<bool>());EXPECT_EQ(old.at("wireframe"),first.at("wireframe"));
}
TEST(FiveAxisFilePreviewCache, CountCapacityAndLruEvictionStayBoundedAndIgnoreOtherFiles){
    PreviewCacheSandbox cache;File source;CachedPreview(source);const auto original=cache.Entries().front();const auto old=std::filesystem::file_time_type::clock::now()-std::chrono::hours(1);
    for(unsigned i=0;i<55;++i){auto name=std::string(64,'0');const auto suffix=std::to_string(i+1);name.replace(64-suffix.size(),suffix.size(),suffix);const auto copy=cache.Root/(name+".icaxfp1");std::filesystem::copy_file(original,copy);std::filesystem::last_write_time(copy,old+std::chrono::seconds(i));}
    {std::ofstream other(cache.Root/"keep-this.txt");other<<"unrelated";}
    EXPECT_TRUE(CachedPreview(source).at("previewCache").To<Map>().at("hit").To<bool>());EXPECT_LE(cache.Entries().size(),50u);EXPECT_TRUE(std::filesystem::exists(original));EXPECT_TRUE(std::filesystem::exists(cache.Root/"keep-this.txt"));EXPECT_FALSE(std::filesystem::exists(cache.Root/(std::string(63,'0')+"1.icaxfp1")));
    for(unsigned i=0;i<6;++i){auto name=std::string(64,'a');name[63]='a'+static_cast<char>(i);const auto copy=cache.Root/(name+".icaxfp1");std::filesystem::copy_file(original,copy);std::filesystem::resize_file(copy,25ull*1024*1024);std::filesystem::last_write_time(copy,old+std::chrono::seconds(i));}
    EXPECT_TRUE(CachedPreview(source).at("previewCache").To<Map>().at("hit").To<bool>());std::uintmax_t total=0;for(const auto& path:cache.Entries())total+=std::filesystem::file_size(path);EXPECT_LE(total,128ull*1024*1024);EXPECT_LE(cache.Entries().size(),50u);
}
TEST(FiveAxisFilePreviewCache, CacheDirectoryAndWriteFailuresDoNotFailPreview){
    PreviewCacheSandbox cache;File source;const auto blocked=cache.Root/L"not-a-directory";{std::ofstream file(blocked);file<<"blocked";}SetEnvironmentVariableW(L"ICAX_CAM_PREVIEW_CACHE_DIR",blocked.c_str());
    const auto uncached=CachedPreview(source);EXPECT_FALSE(uncached.at("previewCache").To<Map>().at("hit").To<bool>());EXPECT_FALSE(uncached.at("wireframe").To<Map>().at("polylines").To<Array>().empty());
    SetEnvironmentVariableW(L"ICAX_CAM_PREVIEW_CACHE_DIR",cache.Root.c_str());CachedPreview(source);const auto entry=cache.Entries().front();{std::ofstream file(entry,std::ios::binary|std::ios::trunc);file<<"damaged cache";}
    struct Reset {std::filesystem::path Path;~Reset(){SetFileAttributesW(Path.c_str(),FILE_ATTRIBUTE_NORMAL);}} reset{entry};ASSERT_TRUE(SetFileAttributesW(entry.c_str(),FILE_ATTRIBUTE_READONLY));
    const auto rebuilt=CachedPreview(source);EXPECT_FALSE(rebuilt.at("previewCache").To<Map>().at("hit").To<bool>());EXPECT_EQ(rebuilt.at("wireframe"),uncached.at("wireframe"));
    for(const auto& item:std::filesystem::directory_iterator(cache.Root))EXPECT_NE(item.path().extension(),L".tmp");
}
TEST(FiveAxisFilePreviewCache, UnevictableEntriesSkipCachingWithoutExceedingTheCountLimit){
    PreviewCacheSandbox cache;File source;CachedPreview(source);const auto original=cache.Entries().front();for(unsigned i=0;i<49;++i){auto name=std::string(64,'0');const auto suffix=std::to_string(i+1);name.replace(64-suffix.size(),suffix.size(),suffix);std::filesystem::copy_file(original,cache.Root/(name+".icaxfp1"));}
    struct ResetAll{PreviewCacheSandbox& Cache;~ResetAll(){for(const auto& p:Cache.Entries())SetFileAttributesW(p.c_str(),FILE_ATTRIBUTE_NORMAL);}} reset{cache};for(const auto& p:cache.Entries())ASSERT_TRUE(SetFileAttributesW(p.c_str(),FILE_ATTRIBUTE_READONLY));
    File another;const auto result=CachedPreview(another);EXPECT_FALSE(result.at("previewCache").To<Map>().at("hit").To<bool>());EXPECT_EQ(cache.Entries().size(),50u);EXPECT_FALSE(result.at("wireframe").To<Map>().at("polylines").To<Array>().empty());
}
TEST(FiveAxisFilePreviewCache, DISABLED_RestartProbe){
    const auto count=GetEnvironmentVariableW(L"ICAX_PREVIEW_CACHE_PROBE_FILE",nullptr,0);ASSERT_GT(count,1u);std::wstring source(count,L'\0');GetEnvironmentVariableW(L"ICAX_PREVIEW_CACHE_PROBE_FILE",source.data(),count);source.resize(count-1);const auto path=std::filesystem::path(source).generic_u8string();Scene scene;const auto task=Invoke(&scene,"CamWorkpiece","PreviewFile",{{"sourcePath",Text(reinterpret_cast<const char*>(path.data()),path.size())},{"previewKey",Text("restart-cache-probe")}});const auto result=WaitPreview(scene,task);ASSERT_EQ(result.at("status").To<Text>(),"Completed");const auto value=result.at("result").To<Map>();std::cout<<"PREVIEW_CACHE_PROBE "<<iCAX::Data::VariantSerializer::Serialize(value.at("previewCache"))<<std::endl;
}
