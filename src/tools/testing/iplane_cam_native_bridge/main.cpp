// Test transport only. Product startup, channels, SDO dispatch, tasks, resources
// and persistence all use the unmodified ApplicationRuntime/FrontendBridge.
#include "ApplicationRuntime/ApplicationRuntime.h"
#include "FrontendBridge.h"
#include "Product/ProductManifest.h"
#include "TemplateRuntime/StandardJsonCodec.h"
#include <Windows.h>
#include <crtdbg.h>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>

namespace {
using Map = iCAX::Data::ObjectMap;
using Array = iCAX::Data::VariantArray;
using Variant = iCAX::Data::Variant;
using Json = iCAX::TemplateRuntime::CStandardJsonCodec;
std::string text(const Map& map, const char* key, const std::string& fallback = {}) {
    const auto it = map.find(key); return it == map.end() ? fallback : it->second.To<std::string>();
}
Map object(const Map& map, const char* key) {
    const auto it = map.find(key); return it == map.end() ? Map{} : it->second.To<Map>();
}
uint16_t smallNumber(const Map& map, const char* key) {
    const auto it = map.find(key); if (it == map.end()) return 0;
    const auto& v = it->second;
    const auto n = v.Is<long long>() ? v.To<long long>() : v.Is<int>() ? v.To<int>()
        : v.Is<unsigned long long>() ? static_cast<long long>(v.To<unsigned long long>()) : -1;
    if (n < 0 || n > 65535) throw std::invalid_argument("Invalid small numeric frame value");
    return static_cast<uint16_t>(n);
}
std::string utf8(const std::filesystem::path& path) { const auto s = path.u8string();return {s.begin(), s.end()}; }
std::filesystem::path path(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }
std::string base64(std::span<const uint8_t> bytes) {
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;result.reserve((bytes.size() + 2) / 3 * 4);
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned n = (unsigned(bytes[i]) << 16) | (i + 1 < bytes.size() ? unsigned(bytes[i + 1]) << 8 : 0)
            | (i + 2 < bytes.size() ? bytes[i + 2] : 0);
        result += alphabet[(n >> 18) & 63];result += alphabet[(n >> 12) & 63];
        result += i + 1 < bytes.size() ? alphabet[(n >> 6) & 63] : '=';
        result += i + 2 < bytes.size() ? alphabet[n & 63] : '=';
    }
    return result;
}
}

int main(int argc, char** argv) {
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    std::ios::sync_with_stdio(false);std::cin.tie(nullptr);
    try {
        wchar_t executable[32768]{};
        if (!GetModuleFileNameW(nullptr, executable, 32768)) throw std::runtime_error("Cannot locate bridge executable");
        const auto binary = std::filesystem::path(executable).parent_path();
        auto manifest = binary / "apps/iplane-cam/product.manifest.json";
        auto data = binary / "NativeTestData";
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if ((option == "--manifest" || option == "--data-dir") && i + 1 < argc) {
                (option == "--manifest" ? manifest : data) = std::filesystem::absolute(path(argv[++i]));
            } else throw std::invalid_argument("Unknown or incomplete bridge argument");
        }
        std::filesystem::create_directories(data);
        iCAX::Application::ApplicationRuntimeConfig config;
        config.Descriptor.AppID = "icax";config.Descriptor.AppName = "iPlaneCAM Native Acceptance";
        config.Paths.InstallDirectory = utf8(binary);
        config.Paths.UserDataDirectory = utf8(data);
        config.Paths.UserConfigDirectory = utf8(data / "Profiles");
        config.Paths.TempDirectory = utf8(data / "Temp");
        config.Paths.CacheDirectory = utf8(data / "Cache");
        config.Paths.ResourceVersionDirectory = utf8(data / "ResourceVersions");
        config.Paths.LogDirectory = utf8(data / "Logs");
        config.strApplicationSettingsPath = utf8(data / "Application.Setting");
        config.Products.push_back(iCAX::Product::LoadProductManifest(utf8(manifest)).Definition);
        iCAX::Application::CApplicationRuntime runtime;runtime.SetConfig(config);runtime.Start();
        iCAX::Application::CFrontendBridge bridge;bridge.Attach(runtime);
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty()) continue;
            Variant id;Map response;bool exiting = false;
            try {
                const auto input = Json::Parse(line).To<Map>();
                if (input.contains("id")) id = input.at("id");
                const auto action = text(input, "action");Map result;
                if (action == "application-channel") result = {{"channelId", bridge.GetApplicationChannelIDText()}};
                else if (action == "register-product") result = {{"channelId", bridge.RegisterProductChannel(text(input, "productId"))}};
                else if (action == "register-scene") result = {{"channelId", bridge.RegisterSceneChannel(text(input, "projectId"), text(input, "sceneId"))}};
                else if (action == "post") {
                    const auto source = object(input, "frame");iCAX::Application::CFrontendSDOFrame frame;
                    frame.ChannelID = text(source, "channelId");frame.nCallID = std::stoull(text(source, "callId"));
                    frame.nMethodCode = std::stoull(text(source, "methodCode"));
                    frame.nKind = smallNumber(source, "kind");frame.nStatus = smallNumber(source, "status");
                    frame.PayloadText = text(source, "payloadText");bridge.PostSDOFrame(frame);result = {{"posted", true}};
                } else if (action == "poll") {
                    bridge.RunFrontTasks();bridge.RunFrontCoroutines();Array frames;
                    for (const auto& frame : bridge.PollSDOFrames()) frames.emplace_back(Map{
                        {"channelId", frame.ChannelID}, {"callId", std::to_string(frame.nCallID)},
                        {"methodCode", std::to_string(frame.nMethodCode)}, {"kind", int(frame.nKind)},
                        {"status", int(frame.nStatus)}, {"payloadText", frame.PayloadText}});
                    result = {{"frames", std::move(frames)}};
                } else if (action == "resource") {
                    const auto source = object(input, "request");iCAX::Application::CFrontendResourceRequest request;
                    request.Method = text(source, "method", "GET");request.URL = text(source, "url");
                    for (const auto& [key, value] : object(source, "headers")) request.Headers[key] = value.To<std::string>();
                    const auto resource = bridge.RequestResource(request);Map headers;
                    for (const auto& [key, value] : resource.Headers) headers[key] = value;
                    result = {{"status", int(resource.nStatus)}, {"headers", std::move(headers)}, {"bodyBase64", base64(resource.Body)}};
                } else if (action == "exit") {result = {{"exited", true}};exiting = true;}
                else throw std::invalid_argument("Unknown bridge action: " + action);
                response = {{"id", id}, {"ok", true}, {"result", std::move(result)}};
            } catch (const std::exception& error) {response = {{"id", id}, {"ok", false}, {"error", std::string(error.what())}};}
            std::cout << Json::Serialize(response) << '\n' << std::flush;
            if (exiting) break;
        }
        bridge.Detach();runtime.Stop();return 0;
    } catch (const std::exception& error) {std::cerr << "Native runtime startup failed: " << error.what() << '\n';return 1;}
}
