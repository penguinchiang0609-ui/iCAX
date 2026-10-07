// Real CEF lifecycle regression. The harness only closes windows and observes
// descendants belonging to its own EXE; it never terminates a process.
#include <windows.h>
#include <tlhelp32.h>
#include <CefUIContainer/CefUIContainer.h>
#include <UIContainer/UIContainer.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#ifndef ICAX_CEF_SHUTDOWN_FACTORY
#define ICAX_CEF_SHUTDOWN_FACTORY 0
#endif

using namespace iCAX::Frontend;
using iCAX::Frontend::Cef::CCefUIContainer;
namespace {
const auto began = std::chrono::steady_clock::now();
std::ofstream evidence;
void event(const char* name, const std::string& detail = {}) {
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-began).count();
    std::ostringstream line;
    line << "{\"event\":" << std::quoted(name) << ",\"pid\":" << GetCurrentProcessId()
         << ",\"threadId\":" << GetCurrentThreadId() << ",\"milliseconds\":" << milliseconds
         << ",\"detail\":" << std::quoted(detail) << "}";
    if(evidence) { evidence << line.str() << std::endl; evidence.flush(); }
    std::cout << line.str() << std::endl;
}
class MockBridge final : public IFrontendBridge {
    iCAX::Tasks::TaskSchedulerPtr scheduler = std::make_shared<iCAX::Tasks::EventLoopTaskScheduler>();
    iCAX::Coroutines::CCoroutineRuntime coroutines{scheduler};
public:
    bool IsAttached() const override { return true; }
    std::string GetApplicationChannelIDText() const override { return "cef-close-fixture"; }
    std::string RegisterProductChannel(const std::string&) override { return "fixture-product"; }
    std::string RegisterSceneChannel(const std::string&,const std::string&) override { return "fixture-scene"; }
    void PostSDOFrame(const CFrontendSDOFrame&) override {}
    std::vector<CFrontendSDOFrame> PollSDOFrames() override { return {}; }
    void SetSDOFrameHandler(FrontendSDOFrameHandler) override {}
    CFrontendResourceResponse RequestResource(const CFrontendResourceRequest&) override { return {}; }
    iCAX::Tasks::TaskSchedulerPtr GetFrontTaskScheduler() const override { return scheduler; }
    std::size_t RunFrontTasks() override { return 0; }
    void PauseFrontCoroutine(const iCAX::Coroutines::CCoroutineHandleBase&) override {}
    void ResumeFrontCoroutine(const iCAX::Coroutines::CCoroutineHandleBase&) override {}
    void CancelFrontCoroutine(const iCAX::Coroutines::CCoroutineHandleBase&) override {}
    bool IsFrontCoroutineRunning(const iCAX::Coroutines::CCoroutineHandleBase&) const override { return false; }
    std::size_t RunFrontCoroutines() override { return 0; }
protected:
    iCAX::Coroutines::CCoroutineRuntime& FrontCoroutineRuntime() override { return coroutines; }
};
std::wstring imagePath(HANDLE process) {
    std::wstring text(32768,L'\0'); DWORD length=static_cast<DWORD>(text.size());
    if(!QueryFullProcessImageNameW(process,0,text.data(),&length)) return {};
    text.resize(length); return text;
}
struct Child { DWORD id; HANDLE handle; };
std::vector<Child> ownedChildren(const std::wstring& executable) {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot observe owned process tree");
    std::map<DWORD,DWORD> parents;
    PROCESSENTRY32W row{sizeof(row)};
    if(Process32FirstW(snapshot,&row)) do { parents[row.th32ProcessID]=row.th32ParentProcessID; } while(Process32NextW(snapshot,&row));
    CloseHandle(snapshot);
    std::set<DWORD> descendants{GetCurrentProcessId()};
    bool changed=true;
    while(changed) { changed=false; for(const auto& [id,parent]:parents) if(descendants.contains(parent) && !descendants.contains(id)) {
        descendants.insert(id); changed=true;
    } }
    std::vector<Child> children;
    for(const auto id:descendants) {
        if(id==GetCurrentProcessId()) continue;
        const auto handle=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,id);
        if(!handle) continue;
        // Console hosts can also be descendants of this console fixture. Only
        // the fixture EXE is configured as a CEF subprocess; observe that set.
        if(_wcsicmp(imagePath(handle).c_str(),executable.c_str())!=0) { CloseHandle(handle); continue; }
        children.push_back({id,handle}); event("owned_child_observed",std::to_string(id));
    }
    return children;
}
std::size_t alive(const std::vector<Child>& children) {
    std::size_t count=0;
    for(const auto& child:children) if(WaitForSingleObject(child.handle,0)==WAIT_TIMEOUT) ++count;
    return count;
}
HWND ownedWindow() {
    HWND found=nullptr;
    EnumWindows([](HWND window,LPARAM result)->BOOL {
        DWORD id=0; GetWindowThreadProcessId(window,&id);
        if(id==GetCurrentProcessId() && GetWindow(window,GW_OWNER)==nullptr) {
            wchar_t name[256]{}; GetClassNameW(window,name,256);
            wchar_t title[256]{}; GetWindowTextW(window,title,256);
            if(std::wstring(name)==L"CefBrowserWindow" && std::wstring(title)==L"CefCloseFixture") {
                *reinterpret_cast<HWND*>(result)=window; return FALSE;
            }
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&found));
    return found;
}
std::vector<HWND> ownedBrowserWindows() {
    std::vector<HWND> result;
    EnumWindows([](HWND window,LPARAM data)->BOOL {
        DWORD id=0; GetWindowThreadProcessId(window,&id);
        if(id==GetCurrentProcessId()) {
            wchar_t name[256]{}; GetClassNameW(window,name,256);
            wchar_t title[256]{}; GetWindowTextW(window,title,256);
            if(std::wstring(name)==L"CefBrowserWindow" &&
                (std::wstring(title)==L"CefCloseFixture" || std::wstring(title)==L"CefClosePopup"))
                reinterpret_cast<std::vector<HWND>*>(data)->push_back(window);
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&result));
    return result;
}
}

int wmain(int argc,wchar_t** argv) {
    const auto subprocess=CCefUIContainer::ExecuteSubProcess(GetModuleHandleW(nullptr));
    if(subprocess>=0) return subprocess;
    if(argc<3) return 2;
    const std::wstring requestedMode=argv[1];
    if(requestedMode!=L"window" && requestedMode!=L"stop" && requestedMode!=L"immediate" && requestedMode!=L"popup") return 2;
    const std::string mode(requestedMode.begin(),requestedMode.end());
    const auto directory=std::filesystem::absolute(argv[2]);
    std::filesystem::create_directories(directory);
    evidence.open(directory/"events.jsonl");
    std::wstring executable(32768,L'\0'); executable.resize(GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size())));
    std::vector<Child> children;
    bool runtimeStarted=false, shutdownCalled=false;
    try {
        std::ofstream page(directory/"fixture.html");
        page << "<!doctype html><meta charset='utf-8'><title>CefCloseFixture</title><p>Isolated browser close fixture</p>";
        if(mode=="popup") {
            std::ofstream(directory/"popup.html") << "<!doctype html><title>CefClosePopup</title><p>Owned popup</p>";
            page << "<script>window.addEventListener('load',()=>window.open('popup.html','fixture-popup','width=420,height=300'))</script>";
        }
        page.close();
        MockBridge bridge;
        CUIContainerConfig config;
        config.ContainerType="cef"; config.pFrontendBridge=&bridge;
        config.WebPageRoot=directory.string();
        auto url=directory.generic_string()+"/fixture.html";
        config.StartURL="file:///"+url;
        config.Properties={{"browserSubprocessPath",std::filesystem::path(executable).string()},
            {"cachePath",(directory/"profile/cache").string()},{"userDataPath",(directory/"profile").string()},
            {"logFile",(directory/"cef.log").string()},{"disableGpu","true"},{"sdoPollIntervalMS","10"}};
        CCefUIContainer container;
        container.SetConfig(config);
        event("start_enter",mode);
        container.Start(); runtimeStarted=true;
        event("start_return");
        HWND window=nullptr;
        if(mode!="immediate") {
            const auto windowDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            while(!(window=ownedWindow()) && std::chrono::steady_clock::now()<windowDeadline) std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if(!window) throw std::runtime_error("Owned native CEF browser window did not appear");
            ShowWindow(window,SW_HIDE);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            children=ownedChildren(executable);
            if(children.empty()) throw std::runtime_error("No actual CEF descendants observed");
        }
        std::vector<HWND> popups;
        if(mode=="popup") {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
            while(std::chrono::steady_clock::now()<deadline) {
                popups=ownedBrowserWindows();
                for(const auto item:popups) ShowWindow(item,SW_HIDE);
                if(popups.size()>=2) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            if(popups.size()<2) throw std::runtime_error("Actual CEF popup did not open");
            event("popup_observed",std::to_string(popups.size()));
        }
        if(mode=="window" || mode=="popup") {
            DWORD owner=0; GetWindowThreadProcessId(window,&owner);
            if(owner!=GetCurrentProcessId()) throw std::runtime_error("Window ownership changed; refusing to close");
            event("browser_close_requested");
            PostMessageW(window,WM_CLOSE,0,0);
            event("wait_for_exit_enter"); container.WaitForExit(); event("wait_for_exit_return");
        }
        event("stop_enter"); container.Stop(); event("stop_return",IsWindow(window)?"browser-window-alive":"browser-window-closed");
        if(container.IsRunning()) throw std::runtime_error("Container remained running after Stop");
        if(mode=="immediate") children=ownedChildren(executable);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        event("children_before_shutdown",std::to_string(alive(children)));
#if ICAX_CEF_SHUTDOWN_FACTORY
        bool wrongThreadRejected=false;
        std::thread wrongThread([&] {
            try { CUIContainerFactory::ShutdownRuntime(config); }
            catch(const std::exception& error) { wrongThreadRejected=true; event("wrong_thread_shutdown_rejected",error.what()); }
        }); wrongThread.join();
        if(!wrongThreadRejected) throw std::runtime_error("Wrong-thread runtime shutdown was not rejected");
        event("shutdown_enter","factory-main-thread");
        CUIContainerFactory::ShutdownRuntime(config);
#else
        event("baseline_shutdown_not_invoked","Stop returned without explicit runtime shutdown");
        event("shutdown_enter","explicit-cleanup-main-thread");
        CCefUIContainer::ShutdownRuntime();
#endif
        shutdownCalled=true; event("shutdown_return");
        // OnBeforeClose precedes native window destruction. Shutdown is the
        // completion boundary for the window and browser process assertions.
        if(IsWindow(window)) throw std::runtime_error("Owned browser window remained after runtime shutdown");
        for(const auto popup:popups) if(IsWindow(popup)) throw std::runtime_error("Owned popup remained after runtime shutdown");
        event("windows_destroyed");
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        while(alive(children) && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        for(const auto& child:children) {
            DWORD code=STILL_ACTIVE; GetExitCodeProcess(child.handle,&code);
            event("child_exit",std::to_string(child.id)+":"+std::to_string(code));
        }
        if(alive(children)) throw std::runtime_error("Owned CEF children did not normally exit after ShutdownRuntime");
        event("passed");
        for(const auto& child:children) CloseHandle(child.handle);
        return 0;
    } catch(const std::exception& error) {
        event("failed",error.what());
        // Only clean up this harness's runtime, from the initialization thread.
        if(runtimeStarted && !shutdownCalled) {
            try { CCefUIContainer::ShutdownRuntime(); event("cleanup_shutdown_return"); }
            catch(const std::exception& cleanup) { event("cleanup_error",cleanup.what()); }
        }
        for(const auto& child:children) CloseHandle(child.handle);
        return 1;
    }
}
