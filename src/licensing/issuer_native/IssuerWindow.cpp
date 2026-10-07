#include "IssuerWindow.h"
#include "IssuerCore.h"
#include "IssuerSettings.h"
#include "ProductCatalog.h"
#include "LicenseBundle.h"
#include "LicenseFiles.h"
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <array>
#include <cwctype>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <thread>

namespace tube::license::issuer_native {
namespace {
enum : int { Choose = 100, Issue, Options, Configuration, OpenFolder,
    SaveConfiguration, BrowseStore, FeatureBase = 200, Days = 300,
    Customer, StorePath, Password, Remember, ProductChoice, ConvertPermanent, IncludeProduct };
constexpr UINT Completed = WM_APP + 1;
void Wipe(Bytes& bytes) { SecureZeroMemory(bytes.data(), bytes.size()); bytes.clear(); }
std::wstring ErrorText(const std::exception& error) {
    try { return Utf8ToWide(error.what()); }
    catch (...) { return L"操作失败，请检查申请或授权文件和签发设置。"; }
}
std::wstring TextOf(HWND control) {
    const int count = GetWindowTextLengthW(control);
    std::wstring value(static_cast<std::size_t>(count) + 1, L'\0');
    GetWindowTextW(control, value.data(), count + 1);
    value.resize(static_cast<std::size_t>(count));
    return value;
}
std::filesystem::path NextOutput(const std::filesystem::path& request, bool upgrade) {
    const auto extension = L".tdact";
    auto candidate = request;
    candidate.replace_extension(extension);
    for (unsigned int suffix = 1; std::filesystem::exists(candidate); ++suffix) {
        Require(suffix < 10000, "Too many existing authorization files");
        candidate = request.parent_path() / (request.stem().wstring() + (upgrade ? L"-升级-" : L"-")
            + std::to_wstring(suffix) + extension);
    }
    return candidate;
}
struct Services {
    std::function<Settings(const ProductDescriptor&)> loadSettings = LoadSettings;
    std::function<bool(const std::filesystem::path&)> isBundle = [](const auto& path) {
        return IsAuthorizationBundle(ReadFileBounded(path, static_cast<DWORD>(MaxAuthorizationFileBytes)));
    };
    std::function<RequestInfo(const ProductDescriptor&, const std::filesystem::path&, const std::filesystem::path&)> inspect = InspectRequest;
    std::function<AuthorizationInfo(const ProductDescriptor&, const std::filesystem::path&, const std::filesystem::path&)> inspectAuthorization = InspectAuthorization;
    std::function<BundleInfo(const std::filesystem::path&, std::span<const BundleStoreRef>)> inspectBundle =
        [](const auto& path, auto stores) {
            const auto bytes = ReadFileBounded(path, static_cast<DWORD>(MaxAuthorizationFileBytes));
            const auto bundle = ParseAuthorizationBundle(bytes);
            std::vector<BundleStoreRef> selected;
            for (const auto& slot : bundle.slots) {
                const auto found = std::find_if(stores.begin(), stores.end(), [&slot](const auto& store) {
                    return store.product && store.product->id == slot.productId;
                });
                Require(found != stores.end(), "授权文件包含尚未注册的产品，请使用包含该产品的签发器");
                selected.push_back(*found);
            }
            return InspectBundle(bytes, selected);
        };
    std::function<GenerateResult(std::span<const BundleInput>, const std::optional<std::filesystem::path>&,
        const std::filesystem::path&)> generateBundle =
        [](auto inputs, const auto& base, const auto& output) { return GenerateBundle(inputs, base, output); };
};
struct Completion {
    std::uint64_t token{};
    bool generation{};
    std::optional<RequestInfo> request;
    std::optional<AuthorizationInfo> authorization;
    std::optional<BundleInfo> bundle;
    std::optional<GenerateResult> result;
    std::wstring error;
};
class Window final {
    HINSTANCE instance_{};
    HWND window_{}, heading_{}, subtitle_{}, file_{}, summary_{}, status_{}, output_{};
    HWND choose_{}, issue_{}, options_{}, configuration_{}, folder_{}, productLabel_{}, productChoice_{}, convertPermanent_{}, included_{};
    HWND daysLabel_{}, days_{}, customerLabel_{}, customer_{};
    HWND storeLabel_{}, store_{}, passwordLabel_{}, password_{}, remember_{}, browse_{}, save_{};
    std::vector<HWND> features_;
    const ProductDescriptor* product_{&TubeDesignerProduct};
    ProductDescriptor syntheticProduct_{TubeDesignerProduct};
    struct Draft {
        const ProductDescriptor* product{};
        Settings settings;
        std::optional<RequestInfo> request;
        std::optional<AuthorizationInfo> authorization;
        std::filesystem::path source;
        std::uint32_t features{};
        std::wstring days{L"30"};
        bool included{}, convert{}, retained{};
    };
    std::vector<Draft> drafts_;
    std::optional<std::filesystem::path> baseBundlePath_;
    HFONT normal_{}, title_{};
    Settings settings_;
    Services services_;
    std::optional<RequestInfo> request_;
    std::optional<AuthorizationInfo> authorization_;
    std::filesystem::path requestPath_, outputPath_;
    std::jthread worker_;
    std::uint64_t token_{};
    bool busy_{}, expandedOptions_{}, expandedConfiguration_{}, testing_{}, readinessStatus_{};
    int scale_{96};
    int OptionsHeight() const {
        std::size_t parents{}, maxChildren{};
        for (const auto& descriptor : product_->features) {
            if (descriptor.parent != 0) continue;
            ++parents;
            std::size_t children{};
            for (const auto& child : product_->features)
                if (child.parent == static_cast<std::uint32_t>(descriptor.feature)) ++children;
            maxChildren = std::max(maxChildren, children);
        }
        return static_cast<int>(((parents + 3) / 4) * (maxChildren + 1) * 32 + 88);
    }
    int Px(int value) const { return MulDiv(value, scale_, 96); }
    HWND Control(const wchar_t* type, const wchar_t* text, DWORD style, int id = 0) {
        auto control = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style,
            0, 0, 1, 1, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        Require(control != nullptr, "Cannot create issuer control");
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(normal_), TRUE);
        return control;
    }
    HWND Button(const wchar_t* text, int id, DWORD style = BS_PUSHBUTTON) {
        return Control(L"BUTTON", text, WS_TABSTOP | style, id);
    }
    HWND Edit(const wchar_t* text, int id, DWORD style = 0) {
        auto control = Control(L"EDIT", text, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | style, id);
        SendMessageW(control, EM_SETLIMITTEXT, 4096, 0);
        return control;
    }
    void Move(HWND control, int x, int y, int width, int height, bool visible = true) {
        ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
        MoveWindow(control, Px(x), Px(y), Px(width), Px(height), TRUE);
    }
    void SetStatus(const std::wstring& text) {
        readinessStatus_ = false;
        SetWindowTextW(status_, text.c_str());
    }
    void EnableFeatures() {
        const auto retained = authorization_ ? authorization_->policy.features : 0;
        for (std::size_t index = 0; index < product_->features.size(); ++index)
            if ((retained & static_cast<std::uint32_t>(product_->features[index].feature)) != 0)
                SendMessageW(features_[index], BM_SETCHECK, BST_CHECKED, 0);
        const auto selected = Features();
        for (std::size_t index = 0; index < product_->features.size(); ++index) {
            const auto parent = product_->features[index].parent;
            const bool enabled = parent == 0 || (selected & parent) == parent;
            if (!enabled) SendMessageW(features_[index], BM_SETCHECK, BST_UNCHECKED, 0);
            const bool fixed = (retained & static_cast<std::uint32_t>(product_->features[index].feature)) != 0;
            EnableWindow(features_[index], !busy_ && enabled && !fixed);
        }
    }
    std::wstring GenerationBlocker() const {
        bool included{};
        for (const auto& draft : drafts_) {
            if (!draft.included) continue;
            included = true;
            const auto label = Utf8ToWide(draft.product->label) + L"：";
            if (!draft.request && !draft.authorization) return label + L"请先选择申请或原授权文件。";
            if (draft.settings.password.empty()) return label + L"签发口令未就绪。请展开“签发设置”，填写口令后保存。";
            if (!IsValidFeatureSet(*draft.product, draft.features)) return label + L"请在“授权选项”中至少选择一个页面及所需功能。";
        }
        return included ? L"" : L"请至少勾选一个产品的“本次包含”。";
    }
    void EnableActions(bool explain = false) {
        EnableFeatures();
        EnableWindow(choose_, !busy_);
        if (!busy_) SaveDraft();
        std::size_t count{};
        for (const auto& draft : drafts_) {
            if (!draft.included) continue;
            ++count;
        }
        const auto blocker = GenerationBlocker();
        EnableWindow(issue_, !busy_ && blocker.empty());
        if (!busy_ && (explain || readinessStatus_)) {
            if (!blocker.empty()) {
                SetStatus(blocker); readinessStatus_ = true;
            } else if (readinessStatus_) {
                SetStatus(baseBundlePath_ || authorization_ ? L"签发条件已就绪，可以生成升级文件。"
                    : L"签发条件已就绪，可以生成授权文件。");
            }
        }
        SetWindowTextW(subtitle_, (L"选择各产品的申请或旧授权；本次包含 " + std::to_wstring(count) + L" 个产品，生成一个文件。").c_str());
        EnableWindow(productChoice_, !busy_);
        EnableWindow(included_, !busy_ && !CurrentDraft().retained);
        EnableWindow(convertPermanent_, !busy_);
        for (const auto control : {options_, configuration_, days_, customer_, store_, password_, remember_, browse_, save_})
            EnableWindow(control, !busy_);
        EnableWindow(days_, !busy_ && request_.has_value() && request_->isTrial);
        EnableWindow(customer_, !busy_ && !authorization_ && !baseBundlePath_);
        EnableWindow(folder_, !busy_ && !outputPath_.empty());
    }
    void ResizeForOptions() {
        RECT current{}; GetClientRect(window_, &current);
        RECT size{0, 0, current.right, Px(342 + (expandedOptions_ ? OptionsHeight() : 0) + (expandedConfiguration_ ? 140 : 0))};
        AdjustWindowRectEx(&size, static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_STYLE)), FALSE, 0);
        MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
        GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor);
        RECT position{}; GetWindowRect(window_, &position);
        const int width = size.right - size.left, height = size.bottom - size.top;
        const int x = std::max(monitor.rcWork.left, std::min(position.left, monitor.rcWork.right - width));
        const int y = std::max(monitor.rcWork.top, std::min(position.top, monitor.rcWork.bottom - height));
        SetWindowPos(window_, nullptr, x, y, width, height, SWP_NOZORDER);
        Layout();
    }
    void Layout() {
        RECT rect{}; GetClientRect(window_, &rect);
        const int width = MulDiv(rect.right, 96, scale_);
        Move(heading_, 24, 18, width - 340, 32);
        Move(productLabel_, width - 322, 26, 42, 24);
        Move(productChoice_, width - 278, 22, 154, 180);
        Move(included_, width - 114, 24, 90, 26);
        Move(subtitle_, 24, 57, width - 48, 22);
        Move(choose_, 24, 96, 146, 36);
        Move(file_, 184, 98, width - 208, 32);
        Move(summary_, 24, 145, width - 48, 24);
        Move(issue_, 24, 185, 184, 42);
        Move(status_, 226, 182, width - 250, 65);
        Move(output_, 24, 257, width - 48, 30, !outputPath_.empty());
        Move(options_, 24, 304, 116, 30);
        Move(configuration_, 150, 304, 116, 30);
        Move(folder_, width - 140, 304, 116, 30);
        int y = 351;
        const int columnWidth = (width - 48) / 4;
        int parentIndex{}, maxRows{};
        for (const auto& parent : product_->features) {
            if (parent.parent != 0) continue;
            int children{};
            for (const auto& child : product_->features)
                if (child.parent == static_cast<std::uint32_t>(parent.feature)) ++children;
            maxRows = std::max(maxRows, children + 1);
        }
        for (const auto& parent : product_->features) {
            if (parent.parent != 0) continue;
            const int column = parentIndex % 4, group = parentIndex / 4;
            int row{};
            for (std::size_t index = 0; index < product_->features.size(); ++index) {
                const auto& descriptor = product_->features[index];
                if (descriptor.feature != parent.feature && descriptor.parent != static_cast<std::uint32_t>(parent.feature)) continue;
                const bool child = descriptor.parent != 0;
                Move(features_[index], 24 + column * columnWidth + (child ? 12 : 0),
                    y + (group * maxRows + row++) * 32, columnWidth - (child ? 12 : 0), 26, expandedOptions_);
            }
            ++parentIndex;
        }
        const int detailY = y + ((parentIndex + 3) / 4) * maxRows * 32 + 16;
        Move(customerLabel_, 24, detailY, 90, 26, expandedOptions_);
        Move(customer_, 116, detailY - 2, 300, 28, expandedOptions_);
        Move(daysLabel_, 442, detailY, 90, 26, expandedOptions_);
        Move(days_, 534, detailY - 2, 80, 28, expandedOptions_);
        Move(convertPermanent_, 24, detailY + 34, width - 48, 26,
            expandedOptions_ && authorization_ && authorization_->policy.kind == Kind::Trial);
        if (expandedOptions_) y += OptionsHeight();
        Move(storeLabel_, 24, y + 2, 68, 26, expandedConfiguration_);
        Move(store_, 94, y, width - 228, 28, expandedConfiguration_);
        Move(browse_, width - 122, y, 98, 28, expandedConfiguration_);
        Move(passwordLabel_, 24, y + 40, 68, 26, expandedConfiguration_);
        Move(password_, 94, y + 38, 250, 28, expandedConfiguration_);
        Move(remember_, 356, y + 40, width - 380, 26, expandedConfiguration_);
        Move(save_, 94, y + 80, 132, 30, expandedConfiguration_);
        SetWindowTextW(options_, expandedOptions_ ? L"授权选项 ▴" : L"授权选项 ▾");
        SetWindowTextW(configuration_, expandedConfiguration_ ? L"签发设置 ▴" : L"签发设置 ▾");
    }
    void CreateControls();
    void CreateFeatures();
    void SelectProduct();
    Draft& CurrentDraft() {
        for (auto& draft : drafts_) if (draft.product == product_) return draft;
        throw std::runtime_error("Selected product is unavailable");
    }
    void SaveDraft();
    void LoadDraft();
    void RefreshSavedCredential();
    void Command(int id);
    void SelectRequestFile();
    void BrowseSigningStore();
    void SaveConfigurationNow();
    void StartGenerate();
    void Accept(std::unique_ptr<Completion> completion);
    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
public:
    explicit Window(HINSTANCE instance, bool testing = false) : instance_(instance), testing_(testing) {
        for (const auto& product : ProductCatalog) {
            Draft draft; draft.product = &product; draft.features = KnownProductFeatures(product);
            draft.included = drafts_.empty();
            if (!testing_) draft.settings = LoadSettings(product);
            else { draft.settings.message = L"界面测试"; draft.settings.password = {'t','e','s','t'}; }
            drafts_.push_back(std::move(draft));
        }
        if (testing_) {
            services_.loadSettings = [](const auto&) { return Settings{}; };
            syntheticProduct_.id = "icax.synthetic-ui"; syntheticProduct_.label = "测试产品";
            Draft draft; draft.product = &syntheticProduct_; draft.features = KnownProductFeatures(syntheticProduct_);
            draft.settings.message = L"界面测试"; draft.settings.password = {'t','e','s','t'};
            drafts_.push_back(std::move(draft));
        }
        product_ = drafts_.front().product; settings_ = drafts_.front().settings;
    }
    ~Window() {
        if (worker_.joinable()) worker_.join();
        if (window_ && IsWindow(window_)) DestroyWindow(window_);
        Wipe(settings_.password);
        for (auto& draft : drafts_) Wipe(draft.settings.password);
        if (normal_) DeleteObject(normal_);
        if (title_) DeleteObject(title_);
    }
    void Create(int show);
    std::uint32_t Features() const {
        std::uint32_t result{};
        for (std::size_t index = 0; index < product_->features.size(); ++index)
            if (features_[index] && SendMessageW(features_[index], BM_GETCHECK, 0, 0) == BST_CHECKED)
                result |= static_cast<std::uint32_t>(product_->features[index].feature);
        return result;
    }
    void InspectSelection(const std::filesystem::path& path);
    void CheckSelection(const std::filesystem::path& path, const std::filesystem::path& report);
    bool SelfTest();
    void Capture(const std::filesystem::path& path, int section = 0);
    HWND Handle() const { return window_; }
};
}
}

namespace tube::license::issuer_native {
namespace {
void Window::CreateControls() {
    heading_ = Control(L"STATIC", L"iCAX 授权签发器", SS_LEFT);
    SendMessageW(heading_, WM_SETFONT, reinterpret_cast<WPARAM>(title_), TRUE);
    productLabel_ = Control(L"STATIC", L"产品", SS_LEFT);
    productChoice_ = Control(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, ProductChoice);
    for (const auto& draft : drafts_)
        SendMessageW(productChoice_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Utf8ToWide(draft.product->label).c_str()));
    SendMessageW(productChoice_, CB_SETCURSEL, 0, 0);
    included_ = Button(L"本次包含", IncludeProduct, BS_AUTOCHECKBOX);
    SendMessageW(included_, BM_SETCHECK, BST_CHECKED, 0);
    subtitle_ = Control(L"STATIC", L"选择客户申请首次授权，或选择原授权文件追加功能。", SS_LEFT);
    choose_ = Button(L"选择申请或授权", Choose);
    file_ = Edit(L"尚未选择申请或授权文件", 0, ES_READONLY);
    SendMessageW(file_, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"尚未选择申请或授权文件"));
    summary_ = Control(L"STATIC", settings_.message.c_str(), SS_LEFT);
    issue_ = Button(L"生成授权文件", Issue, BS_DEFPUSHBUTTON);
    status_ = Control(L"STATIC", L"", SS_LEFT);
    output_ = Edit(L"", 0, ES_READONLY);
    options_ = Button(L"授权选项 ▾", Options);
    configuration_ = Button(L"签发设置 ▾", Configuration);
    folder_ = Button(L"打开文件夹", OpenFolder);
    CreateFeatures();
    convertPermanent_ = Button(L"转为正式授权（保留已有功能）", ConvertPermanent, BS_AUTOCHECKBOX);
    customerLabel_ = Control(L"STATIC", L"客户（可选）", SS_LEFT);
    customer_ = Edit(L"", Customer);
    SendMessageW(customer_, EM_SETLIMITTEXT, 128, 0);
    daysLabel_ = Control(L"STATIC", L"试用天数", SS_LEFT);
    days_ = Edit(L"30", Days, ES_NUMBER);
    SendMessageW(days_, EM_SETLIMITTEXT, 2, 0);
    storeLabel_ = Control(L"STATIC", L"签发库", SS_LEFT);
    store_ = Edit(settings_.storeDirectory.c_str(), StorePath);
    browse_ = Button(L"选择目录", BrowseStore);
    passwordLabel_ = Control(L"STATIC", L"口令", SS_LEFT);
    password_ = Edit(L"", Password, ES_PASSWORD);
    SendMessageW(password_, EM_SETCUEBANNER, FALSE,
        reinterpret_cast<LPARAM>(L"留空使用已保存口令"));
    remember_ = Button(L"本机记住口令", Remember, BS_AUTOCHECKBOX);
    SendMessageW(remember_, BM_SETCHECK, BST_CHECKED, 0);
    save_ = Button(L"保存签发设置", SaveConfiguration);
    Layout(); EnableActions();
}
void Window::CreateFeatures() {
    for (const auto control : features_) DestroyWindow(control);
    features_.clear();
    for (std::size_t index = 0; index < product_->features.size(); ++index) {
        const auto label = Utf8ToWide(product_->features[index].label);
        auto control = Button(label.c_str(), FeatureBase + static_cast<int>(index), BS_AUTOCHECKBOX);
        SendMessageW(control, BM_SETCHECK, BST_CHECKED, 0);
        features_.push_back(control);
    }
}
void Window::SelectProduct() {
    if (busy_) return;
    const auto selected = SendMessageW(productChoice_, CB_GETCURSEL, 0, 0);
    Require(selected >= 0 && static_cast<std::size_t>(selected) < drafts_.size(), "请选择有效产品");
    const auto* product = drafts_[static_cast<std::size_t>(selected)].product;
    if (product->id == product_->id) return;
    SaveDraft(); product_ = product; ++token_;
    LoadDraft(); ResizeForOptions(); EnableActions(true);
}
void Window::SaveDraft() {
    if (!included_) return;
    auto& draft = CurrentDraft();
    Wipe(draft.settings.password); draft.settings = settings_;
    draft.request = request_; draft.authorization = authorization_; draft.source = requestPath_;
    draft.features = Features(); draft.days = TextOf(days_);
    draft.included = draft.retained || SendMessageW(included_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    draft.convert = SendMessageW(convertPermanent_, BM_GETCHECK, 0, 0) == BST_CHECKED;
}
void Window::LoadDraft() {
    const auto& draft = CurrentDraft();
    Wipe(settings_.password); settings_ = draft.settings;
    request_ = draft.request; authorization_ = draft.authorization; requestPath_ = draft.source;
    CreateFeatures();
    for (std::size_t index = 0; index < product_->features.size(); ++index)
        SendMessageW(features_[index], BM_SETCHECK,
            (draft.features & static_cast<std::uint32_t>(product_->features[index].feature)) ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(included_, BM_SETCHECK, draft.included ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(convertPermanent_, BM_SETCHECK, draft.convert ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(file_, draft.source.empty() ? L"尚未选择申请或授权文件" : draft.source.c_str());
    SetWindowTextW(days_, draft.days.c_str()); SetWindowTextW(store_, settings_.storeDirectory.c_str());
    SetWindowTextW(password_, L"");
    SetWindowTextW(issue_, baseBundlePath_ || authorization_ ? L"生成升级文件" : L"生成授权文件");
    SetWindowTextW(summary_, authorization_ ? L"原授权已读取 · 已有功能保留"
        : (request_ ? L"申请已验证 · 已加入本次授权" : settings_.message.c_str()));
    SetStatus(L""); Layout();
}
void Window::RefreshSavedCredential() {
    if (!settings_.password.empty()) return;
    auto saved = services_.loadSettings(*product_);
    // Only refresh the selected product's existing vault; an unrelated saved
    // configuration must not silently replace the operator's selection.
    const auto sameVault = [&saved](const std::filesystem::path& selected) {
        if (selected.empty()) return true;
        std::error_code selectedError, savedError;
        const auto selectedPath = std::filesystem::weakly_canonical(selected, selectedError);
        const auto savedPath = std::filesystem::weakly_canonical(saved.storeDirectory, savedError);
        return !selectedError && !savedError && selectedPath == savedPath;
    };
    if (!saved.password.empty() && !saved.storeDirectory.empty()
        && sameVault(settings_.storeDirectory) && sameVault(std::filesystem::path(TextOf(store_)))) {
        settings_ = saved;
        SetWindowTextW(store_, settings_.storeDirectory.c_str());
    }
    Wipe(saved.password);
}
void Window::Create(int show) {
    scale_ = static_cast<int>(GetDpiForSystem());
    normal_ = CreateFontW(-Px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    title_ = CreateFontW(-Px(22), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    Require(normal_ && title_, "Cannot create issuer fonts");
    WNDCLASSEXW type{}; type.cbSize = sizeof(type); type.lpfnWndProc = Procedure;
    type.hInstance = instance_; type.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    type.hIcon = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(101), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
    type.hIconSm = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(101), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    type.lpszClassName = L"iCAX.NativeIssuer";
    if (!RegisterClassExW(&type)) Require(GetLastError() == ERROR_CLASS_ALREADY_EXISTS, "Cannot register issuer window");
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT size{0, 0, Px(760), Px(342)}; AdjustWindowRectEx(&size, style, FALSE, 0);
    RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int width = size.right - size.left, height = size.bottom - size.top;
    window_ = CreateWindowExW(0, type.lpszClassName, L"iCAX 授权签发器", style,
        work.left + (work.right - work.left - width) / 2,
        work.top + (work.bottom - work.top - height) / 2,
        width, height, nullptr, nullptr, instance_, this);
    Require(window_ != nullptr, "Cannot create issuer window");
    if (show != SW_HIDE) { ShowWindow(window_, show); UpdateWindow(window_); }
}
void Window::SelectRequestFile() {
    std::wstring buffer(32768, L'\0');
    OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = window_;
    dialog.lpstrTitle = L"选择客户申请或原授权文件";
    dialog.lpstrFilter = L"申请及授权文件\0*.tdreq;*.tdact\0申请文件 (*.tdreq)\0*.tdreq\0授权文件 (*.tdact)\0*.tdact\0\0";
    dialog.lpstrFile = buffer.data(); dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog)) InspectSelection(std::filesystem::path(buffer.c_str()));
}
void Window::InspectSelection(const std::filesystem::path& path) {
    if (busy_) return;
    auto extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t value) { return static_cast<wchar_t>(towlower(value)); });
    Require(!CurrentDraft().retained || extension != L".tdreq", "已有产品请修改权限；新增产品请先切换产品再选择申请。");
    RefreshSavedCredential();
    SaveDraft();
    if (authorization_) SetWindowTextW(days_, L"30");
    requestPath_ = path; request_.reset(); authorization_.reset(); outputPath_.clear();
    SendMessageW(convertPermanent_, BM_SETCHECK, BST_UNCHECKED, 0);
    SetWindowTextW(issue_, L"生成授权文件");
    SetWindowTextW(file_, path.c_str());
    SetWindowTextW(summary_, L"正在验证申请或授权文件…");
    SetStatus(L""); busy_ = true; EnableActions(); Layout();
    const auto token = ++token_;
    const auto handle = window_;
    const auto store = settings_.storeDirectory;
    const auto inspect = services_.inspect;
    const auto inspectAuthorization = services_.inspectAuthorization;
    const auto inspectBundle = services_.inspectBundle;
    const auto isBundle = services_.isBundle;
    std::vector<BundleStoreRef> stores;
    for (const auto& draft : drafts_) stores.push_back({draft.product, draft.settings.storeDirectory});
    const auto product = product_;
    worker_ = std::jthread([token, handle, path, store, inspect, inspectAuthorization, inspectBundle, isBundle, stores, product] {
        auto completion = std::make_unique<Completion>(); completion->token = token;
        try {
            auto extension = path.extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](wchar_t value) { return static_cast<wchar_t>(towlower(value)); });
            if (extension == L".tdreq") completion->request = inspect(*product, path, store);
            else {
                Require(extension == L".tdact", "请选择 .tdreq 申请文件或 .tdact 授权文件");
                const bool multi = isBundle(path);
                if (multi) completion->bundle = inspectBundle(path, stores);
                else completion->authorization = inspectAuthorization(*product, path, store);
            }
        }
        catch (const std::exception& error) { completion->error = ErrorText(error); }
        auto raw = completion.release();
        if (!PostMessageW(handle, Completed, 0, reinterpret_cast<LPARAM>(raw))) delete raw;
    });
}
void Window::StartGenerate() {
    if (busy_) return;
    try {
        SaveDraft();
        std::string customer;
        for (const auto& draft : drafts_)
            if (draft.included && draft.authorization) {
                if (customer.empty()) customer = draft.authorization->policy.customer;
                Require(customer == draft.authorization->policy.customer, "所选授权的客户不一致");
            }
        if (customer.empty()) customer = WideToUtf8(TextOf(customer_));
        std::vector<BundleInput> inputs;
        const bool upgrade = baseBundlePath_.has_value()
            || std::any_of(drafts_.begin(), drafts_.end(), [](const auto& draft) { return draft.included && draft.authorization; });
        for (const auto& draft : drafts_) {
            if (!draft.included) continue;
            Require(draft.request || draft.authorization, "请为本次包含的每个产品选择申请或原授权");
            Require(!draft.settings.password.empty(), "请先保存各产品的签发口令");
            Policy policy = draft.authorization ? draft.authorization->policy : Policy{};
            policy.features = draft.features;
            Require(IsValidFeatureSet(*draft.product, policy.features), "请为每个产品选择页面及所需功能");
            if (customer.empty()) customer = WideToUtf8(draft.source.stem().wstring());
            if (draft.authorization) {
                Require((policy.features & draft.authorization->policy.features) == draft.authorization->policy.features,
                    "升级必须保留原有授权功能");
                if (policy.kind == Kind::Trial && draft.convert) { policy.kind = Kind::Permanent; policy.days = 30; }
            } else {
                policy.customer = customer;
                policy.kind = draft.request->isTrial ? Kind::Trial : Kind::Permanent;
                if (draft.request->isTrial) {
                    Require(!draft.days.empty() && draft.days.size() <= 2
                        && std::all_of(draft.days.begin(), draft.days.end(), [](wchar_t value) { return value >= L'0' && value <= L'9'; }),
                        "试用天数应为 1 至 30 天");
                    policy.days = static_cast<std::uint32_t>(std::stoul(draft.days));
                    Require(policy.days >= 1 && policy.days <= 30, "试用天数应为 1 至 30 天");
                }
            }
            inputs.push_back({draft.product, draft.settings.storeDirectory, draft.settings.password, draft.source, policy});
        }
        Require(!inputs.empty(), "请至少包含一个产品");
        const auto base = baseBundlePath_;
        const auto output = NextOutput(base ? *base : inputs.front().sourcePath, upgrade);
        busy_ = true; EnableActions(); SetStatus(L"正在生成整份授权文件…");
        const auto token = ++token_;
        const auto handle = window_;
        const auto generate = services_.generateBundle;
        worker_ = std::jthread([token, handle, base, output, generate, inputs = std::move(inputs)]() mutable {
            auto completion = std::make_unique<Completion>(); completion->token = token; completion->generation = true;
            try {
                completion->result = generate(inputs, base, output);
            } catch (const std::exception& error) { completion->error = ErrorText(error); }
            for (auto& input : inputs) Wipe(input.password);
            auto raw = completion.release();
            if (!PostMessageW(handle, Completed, 0, reinterpret_cast<LPARAM>(raw))) delete raw;
        });
    } catch (const std::exception& error) {
        busy_ = false; SetStatus(L"未生成：" + ErrorText(error)); EnableActions();
    }
}
void Window::Accept(std::unique_ptr<Completion> completion) {
    if (!completion || completion->token != token_) return;
    busy_ = false;
    if (!completion->error.empty()) {
        if (!completion->generation) {
            if (CurrentDraft().retained) LoadDraft();
            else {
                request_.reset(); authorization_.reset();
                SetWindowTextW(summary_, L"文件尚未通过验证，请检查签发设置或选择有效文件。");
            }
        }
        SetStatus((completion->generation ? L"生成失败：" : L"读取失败：") + completion->error);
    } else if (completion->bundle) {
        const auto& info = *completion->bundle;
        Require(!info.products.empty(), "授权文件没有可用产品");
        for (const auto& authorization : info.products)
            Require(std::any_of(drafts_.begin(), drafts_.end(), [&authorization](const auto& draft) {
                return draft.product->id == authorization.product;
            }), "授权文件包含尚未注册的产品，请使用包含该产品的签发器");
        baseBundlePath_ = requestPath_;
        for (auto& draft : drafts_) {
            draft.request.reset(); draft.authorization.reset(); draft.source.clear();
            draft.included = false; draft.retained = false; draft.convert = false;
            draft.features = KnownProductFeatures(*draft.product); draft.days = L"30";
        }
        for (const auto& authorization : info.products)
            for (auto& draft : drafts_)
                if (draft.product->id == authorization.product) {
                    draft.authorization = authorization; draft.source = *baseBundlePath_;
                    draft.features = authorization.policy.features;
                    draft.days = std::to_wstring(authorization.policy.days);
                    draft.included = true; draft.retained = true;
                }
        if (!CurrentDraft().included)
            for (std::size_t index = 0; index < drafts_.size(); ++index)
                if (drafts_[index].included) {
                    product_ = drafts_[index].product;
                    SendMessageW(productChoice_, CB_SETCURSEL, index, 0); break;
                }
        SetWindowTextW(customer_, Utf8ToWide(info.customer).c_str());
        LoadDraft(); expandedOptions_ = true; expandedConfiguration_ = false; ResizeForOptions();
        SetStatus(L"整份授权已读取。切换产品追加功能，或添加其他产品的申请；已有产品全部保留。");
    } else if (completion->request) {
        RefreshSavedCredential();
        request_ = completion->request;
        SendMessageW(included_, BM_SETCHECK, BST_CHECKED, 0);
        SetWindowTextW(issue_, baseBundlePath_ ? L"生成升级文件" : L"生成授权文件");
        SetWindowTextW(summary_, request_->isTrial ? L"试用申请 · 验证通过"
            : L"正式授权申请 · 验证通过");
        SetStatus(L"已读取申请。点击“生成授权文件”即可。");
    } else if (completion->authorization) {
        authorization_ = completion->authorization;
        SendMessageW(included_, BM_SETCHECK, BST_CHECKED, 0);
        const auto& policy = authorization_->policy;
        for (std::size_t index = 0; index < product_->features.size(); ++index)
            SendMessageW(features_[index], BM_SETCHECK,
                (policy.features & static_cast<std::uint32_t>(product_->features[index].feature)) != 0
                    ? BST_CHECKED : BST_UNCHECKED, 0);
        SetWindowTextW(customer_, Utf8ToWide(policy.customer).c_str());
        SetWindowTextW(days_, std::to_wstring(policy.days).c_str());
        SetWindowTextW(issue_, L"生成升级文件");
        SetWindowTextW(summary_, policy.kind == Kind::Trial
            ? L"试用授权 · 验证通过 · 升级保留原试用期限"
            : L"正式授权 · 验证通过 · 原功能保留");
        expandedOptions_ = true; expandedConfiguration_ = false; ResizeForOptions();
        SetStatus(authorization_->hasNewerAuthorization
            ? L"已有后续授权，请选择最新文件继续升级；相同选项可重新导出已有升级。"
            : L"原有功能已锁定。勾选新增功能后生成新授权文件，无需重新申请。");
    } else if (completion->result) {
        outputPath_ = completion->result->outputPath;
        SetWindowTextW(output_, outputPath_.c_str());
        SetStatus(completion->result->reexported ? L"已重新导出整份授权文件。\r\n客户在各产品的“关于”页导入同一文件。"
            : L"整份授权文件已生成。\r\n客户在各产品的“关于”页导入同一文件。");
    }
    EnableActions(completion->error.empty() && !completion->generation); Layout();
}
void Window::BrowseSigningStore() {
    BROWSEINFOW dialog{}; dialog.hwndOwner = window_;
    dialog.lpszTitle = L"选择签发库目录";
    dialog.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    auto selected = SHBrowseForFolderW(&dialog);
    if (!selected) return;
    std::array<wchar_t, MAX_PATH> path{};
    if (SHGetPathFromIDListW(selected, path.data())) SetWindowTextW(store_, path.data());
    CoTaskMemFree(selected);
}
void Window::SaveConfigurationNow() {
    auto entered = TextOf(password_);
    std::string encoded;
    Bytes newPassword;
    try {
        const std::filesystem::path directory(TextOf(store_));
        encoded = WideToUtf8(entered);
        if (!encoded.empty()) newPassword.assign(encoded.begin(), encoded.end());
        else {
            Require(std::filesystem::weakly_canonical(directory)
                == std::filesystem::weakly_canonical(settings_.storeDirectory), "更换签发库时请填写口令");
            newPassword = settings_.password;
        }
        Require(!newPassword.empty(), "请填写签发口令");
        const bool remember = SendMessageW(remember_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        SaveSettings(*product_, directory, newPassword, remember);
        Wipe(settings_.password); settings_.password = std::move(newPassword);
        settings_.storeDirectory = std::filesystem::weakly_canonical(directory);
        settings_.remembered = remember;
        SetWindowTextW(password_, L"");
        SetStatus(L"签发设置已保存。");
        if (!requestPath_.empty()) InspectSelection(requestPath_);
        else SetWindowTextW(summary_, L"签发配置已就绪，选择申请或授权文件即可。");
    } catch (const std::exception& error) { SetStatus(L"设置未保存：" + ErrorText(error)); }
    SecureZeroMemory(entered.data(), entered.size() * sizeof(wchar_t));
    SecureZeroMemory(encoded.data(), encoded.size()); Wipe(newPassword); EnableActions();
}
void Window::Command(int id) {
    if (busy_) return;
    if (id >= FeatureBase && id < FeatureBase + static_cast<int>(features_.size())) {
        EnableActions(true); return;
    }
    switch (id) {
    case ProductChoice: SelectProduct(); break;
    case IncludeProduct:
        if (CurrentDraft().retained) SendMessageW(included_, BM_SETCHECK, BST_CHECKED, 0);
        EnableActions(true); break;
    case ConvertPermanent: EnableActions(true); break;
    case Choose: SelectRequestFile(); break;
    case Issue: StartGenerate(); break;
    case Options:
        expandedOptions_ = !expandedOptions_;
        if (expandedOptions_) expandedConfiguration_ = false;
        ResizeForOptions(); break;
    case Configuration:
        expandedConfiguration_ = !expandedConfiguration_;
        if (expandedConfiguration_) expandedOptions_ = false;
        ResizeForOptions(); break;
    case BrowseStore: BrowseSigningStore(); break;
    case SaveConfiguration: SaveConfigurationNow(); break;
    case OpenFolder:
        if (!outputPath_.empty()) ShellExecuteW(window_, L"open", outputPath_.parent_path().c_str(),
            nullptr, nullptr, SW_SHOWNORMAL);
        break;
    default: break;
    }
}
LRESULT CALLBACK Window::Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto self = reinterpret_cast<Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wparam, lparam);
    try {
        switch (message) {
        case WM_CREATE: self->CreateControls(); return 0;
        case WM_COMMAND:
            if (HIWORD(wparam) == BN_CLICKED || HIWORD(wparam) == CBN_SELCHANGE) self->Command(LOWORD(wparam));
            return 0;
        case WM_SIZE: if (self->heading_) self->Layout(); return 0;
        case Completed: self->Accept(std::unique_ptr<Completion>(reinterpret_cast<Completion*>(lparam))); return 0;
        case WM_CTLCOLORSTATIC: {
            auto dc = reinterpret_cast<HDC>(wparam);
            SetTextColor(dc, RGB(32, 52, 65)); SetBkColor(dc, RGB(255, 255, 255));
            return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
        }
        case WM_CLOSE:
            if (self->busy_) { self->SetStatus(L"正在处理，请稍候再关闭。"); return 0; }
            DestroyWindow(window); return 0;
        case WM_DESTROY: self->window_ = nullptr; PostQuitMessage(0); return 0;
        default: break;
        }
    } catch (const std::exception& error) {
        if (self->status_) {
            self->busy_ = false;
            self->SetStatus(L"操作未完成：" + ErrorText(error));
            self->EnableActions();
        }
        if (message == WM_CREATE) return -1;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
bool Window::SelfTest() {
    Require(testing_, "Window test must not access production settings");
    Require(Features() == KnownProductFeatures(*product_), "Default permissions are incomplete");
    Require(SendMessageW(productChoice_, CB_GETCOUNT, 0, 0) == static_cast<LRESULT>(drafts_.size()),
        "Product selector must use registered drafts");
    Require(!IsWindowEnabled(issue_), "Generation must require a verified request");
    auto pump = [this] {
        const auto started = GetTickCount64();
        while (busy_) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                if (message.message != WM_QUIT) { TranslateMessage(&message); DispatchMessageW(&message); }
            Require(GetTickCount64() - started < 5000, "Window worker timed out");
            Sleep(1);
        }
        if (worker_.joinable()) worker_.join();
    };
    const auto otherIndex = drafts_.size() - 1;
    auto select = [this](std::size_t index) {
        SendMessageW(productChoice_, CB_SETCURSEL, index, 0); Command(ProductChoice);
    };
    services_.isBundle = [](const auto& path) { return path.stem() == L"multi-ui"; };
    SendMessageW(features_[0], BM_SETCHECK, BST_UNCHECKED, 0); Command(FeatureBase);
    for (std::size_t index = 0; index < product_->features.size(); ++index)
        if (product_->features[index].parent == 1) {
            Require(!IsWindowEnabled(features_[index]), "Child permission must be disabled");
            Require(SendMessageW(features_[index], BM_GETCHECK, 0, 0) == BST_UNCHECKED, "Disabled child must clear");
        }
    SendMessageW(features_[0], BM_SETCHECK, BST_CHECKED, 0); Command(FeatureBase);
    const auto selected = Features();
    Command(Options); Command(Configuration);
    Require(!expandedOptions_ && expandedConfiguration_ && Features() == selected, "Folding must preserve permissions");
    services_.inspect = [](const auto&, const auto&, const auto&) -> RequestInfo { throw std::runtime_error("invalid request"); };
    InspectSelection(L"first-ui.tdreq"); pump();
    Require(!request_ && !IsWindowEnabled(issue_), "Invalid request must not enable signing");
    auto stale = std::make_unique<Completion>(); stale->token = token_ - 1; stale->request = RequestInfo{};
    Accept(std::move(stale)); Require(!request_, "Stale result must not change selection");
    services_.inspect = [](const auto& product, const auto&, const auto&) {
        RequestInfo request; request.isTrial = product.id == TubeDesignerProduct.id; return request;
    };
    InspectSelection(L"first-ui.tdreq"); pump();
    Require(request_ && IsWindowEnabled(issue_) && IsWindowEnabled(days_), "Verified trial must enable signing");
    const auto configured = settings_;
    Wipe(settings_.password); EnableActions(true);
    Require(request_ && !IsWindowEnabled(issue_) && TextOf(status_).find(L"签发口令未就绪") != std::wstring::npos
        && TextOf(status_).find(L"点击") == std::wstring::npos,
        "Verified request without credential must explain the disabled action");
    InspectSelection(L"first-ui.tdreq"); pump();
    Require(request_ && !IsWindowEnabled(issue_) && TextOf(status_).find(L"签发口令未就绪") != std::wstring::npos,
        "Successful validation must not hide missing credential");
    services_.loadSettings = [configured](const auto&) {
        auto restored = configured; restored.storeDirectory = L"ui-vault"; return restored;
    };
    settings_.storeDirectory = L"another-ui-vault";
    InspectSelection(L"first-ui.tdreq"); pump();
    Require(!IsWindowEnabled(issue_) && settings_.password.empty() && settings_.storeDirectory == L"another-ui-vault",
        "Credential refresh must not change the selected vault");
    settings_.storeDirectory = L"ui-vault";
    SetWindowTextW(store_, L"unsaved-ui-vault");
    InspectSelection(L"first-ui.tdreq"); pump();
    Require(!IsWindowEnabled(issue_) && settings_.password.empty() && TextOf(store_) == L"unsaved-ui-vault",
        "Credential refresh must preserve an unsaved vault edit");
    SetWindowTextW(store_, L"ui-vault");
    InspectSelection(L"first-ui.tdreq"); pump();
    Require(IsWindowEnabled(issue_) && settings_.password == configured.password && CurrentDraft().settings.password == configured.password
        && TextOf(status_).find(L"签发口令未就绪") == std::wstring::npos,
        "Saved credential refresh must restore signing readiness for the same vault");
    Wipe(settings_.password);
    std::size_t credentialReads{};
    services_.loadSettings = [configured, &credentialReads](const auto&) {
        if (++credentialReads == 1) return Settings{};
        auto restored = configured; restored.storeDirectory = L"ui-vault"; return restored;
    };
    InspectSelection(L"first-ui.tdreq"); pump();
    Require(credentialReads == 2 && IsWindowEnabled(issue_) && !settings_.password.empty(),
        "Validation completion must recheck unavailable saved credential");
    services_.loadSettings = [](const auto&) { return Settings{}; };
    for (const auto control : features_) SendMessageW(control, BM_SETCHECK, BST_UNCHECKED, 0);
    EnableActions(true);
    Require(!IsWindowEnabled(issue_) && TextOf(status_).find(L"至少选择一个页面") != std::wstring::npos,
        "Empty permission selection must explain the disabled action");
    for (std::size_t index = 0; index < product_->features.size(); ++index)
        SendMessageW(features_[index], BM_SETCHECK,
            (selected & static_cast<std::uint32_t>(product_->features[index].feature)) ? BST_CHECKED : BST_UNCHECKED, 0);
    EnableActions(true);
    Require(IsWindowEnabled(issue_) && TextOf(status_).find(L"已就绪") != std::wstring::npos,
        "Repairing a readiness blocker must update both the button and prompt");
    SendMessageW(included_, BM_SETCHECK, BST_UNCHECKED, 0); Command(IncludeProduct);
    Require(!IsWindowEnabled(issue_) && TextOf(status_).find(L"本次包含") != std::wstring::npos,
        "No included products must explain the disabled action");
    SendMessageW(included_, BM_SETCHECK, BST_CHECKED, 0); Command(IncludeProduct);
    Require(IsWindowEnabled(issue_) && TextOf(status_).find(L"已就绪") != std::wstring::npos,
        "Including a valid product must restore readiness");
    std::vector<Policy> generated;
    std::vector<std::string> products;
    std::optional<std::filesystem::path> generatedBase;
    services_.generateBundle = [&generated, &products, &generatedBase](auto inputs, const auto& base, const auto& output) {
        generated.clear(); products.clear(); generatedBase = base;
        for (const auto& input : inputs) {
            Require(input.password == Bytes({'t','e','s','t'}), "Worker credential mismatch");
            generated.push_back(input.policy); products.emplace_back(input.product->id);
        }
        Require(output.extension() == L".tdact", "Output extension must be tdact");
        GenerateResult result; result.outputPath = output; return result;
    };
    SetWindowTextW(days_, L"7x"); Command(Issue);
    Require(!busy_ && generated.empty(), "Malformed trial days must not reach signer");
    SetWindowTextW(customer_, L"界面测试"); SetWindowTextW(days_, L"7");
    select(otherIndex);
    Require(!request_ && !CurrentDraft().included && drafts_[0].request && drafts_[0].days == L"7"
        && drafts_[0].features == selected, "Switching must preserve each product draft");
    InspectSelection(L"second-ui.tdreq"); pump();
    Require(CurrentDraft().included && IsWindowEnabled(issue_), "Verified second product must join bundle");
    Command(Issue);
    Require(busy_ && !IsWindowEnabled(issue_) && !IsWindowEnabled(choose_) && !IsWindowEnabled(productChoice_),
        "Busy worker must lock competing actions");
    SendMessageW(window_, WM_CLOSE, 0, 0); Require(IsWindow(window_), "Busy worker must keep window alive");
    pump();
    Require(generated.size() == 2 && !generatedBase && products[0] == TubeDesignerProduct.id
        && products[1] == syntheticProduct_.id && generated[0].kind == Kind::Trial && generated[0].days == 7
        && generated[1].kind == Kind::Permanent && generated[0].features == selected
        && generated[0].customer == WideToUtf8(L"界面测试") && generated[1].customer == generated[0].customer,
        "One generation must contain both product-specific policies and one customer");
    Require(!outputPath_.empty() && IsWindowEnabled(folder_), "Output must be visible");
    SendMessageW(included_, BM_SETCHECK, BST_UNCHECKED, 0); Command(IncludeProduct);
    Command(Issue); pump(); Require(generated.size() == 1, "Unchecked draft must not be signed");
    select(0); Require(request_ && TextOf(days_) == L"7" && Features() == selected, "Original draft must restore controls");
    Policy original; original.customer = WideToUtf8(L"原授权客户"); original.kind = Kind::Trial;
    original.features = RequiredFeatures(Feature::ProductDesign); original.days = 7; original.maxMajor = 2;
    services_.inspectAuthorization = [original](const auto& product, const auto&, const auto&) {
        AuthorizationInfo result; result.product = std::string(product.id); result.policy = original; return result;
    };
    InspectSelection(L"single-ui.TDACT"); pump();
    Require(authorization_ && !request_ && Features() == original.features && !IsWindowEnabled(customer_)
        && !IsWindowEnabled(days_) && expandedOptions_ && IsWindowEnabled(issue_),
        "Existing single authorization must support wrapping and preserve identity");
    std::size_t design{}, breakdown{};
    for (std::size_t index = 0; index < product_->features.size(); ++index) {
        if (product_->features[index].feature == Feature::ProductDesign) design = index;
        if (product_->features[index].feature == Feature::ProductBreakdown) breakdown = index;
    }
    Require(!IsWindowEnabled(features_[design]) && IsWindowEnabled(features_[breakdown]), "Existing rights must lock");
    SendMessageW(features_[design], BM_SETCHECK, BST_UNCHECKED, 0); Command(FeatureBase + static_cast<int>(design));
    Require(Features() == original.features, "Existing rights cannot be removed");
    SendMessageW(features_[breakdown], BM_SETCHECK, BST_CHECKED, 0); Command(FeatureBase + static_cast<int>(breakdown));
    SetWindowTextW(customer_, L"另一客户"); SetWindowTextW(days_, L"30"); Command(Issue); pump();
    Require(generated.size() == 1 && generated[0].customer == original.customer && generated[0].days == original.days
        && generated[0].maxMajor == original.maxMajor && generated[0].kind == Kind::Trial
        && generated[0].features == (original.features | static_cast<std::uint32_t>(Feature::ProductBreakdown)),
        "Single authorization upgrade must preserve prior policy");
    services_.inspectBundle = [this, original](const auto&, auto stores) {
        Require(stores.size() == drafts_.size(), "Every registered store must be available for inspection");
        BundleInfo bundle; bundle.bundleId = "synthetic-ui-bundle"; bundle.customer = original.customer;
        for (const auto* draft : {&drafts_.front(), &drafts_.back()}) {
            AuthorizationInfo item; item.product = std::string(draft->product->id); item.policy = original;
            if (draft->product != &TubeDesignerProduct) item.policy.kind = Kind::Permanent;
            bundle.products.push_back(item);
        }
        return bundle;
    };
    InspectSelection(L"multi-ui.tdact"); pump();
    Require(baseBundlePath_ && drafts_[0].retained && drafts_[otherIndex].retained && drafts_[otherIndex].included
        && !IsWindowEnabled(included_) && Features() == original.features, "Whole bundle must retain every product");
    SendMessageW(included_, BM_SETCHECK, BST_UNCHECKED, 0); Command(IncludeProduct);
    Require(CurrentDraft().included && SendMessageW(included_, BM_GETCHECK, 0, 0) == BST_CHECKED,
        "Existing product cannot be dropped from upgrade");
    SendMessageW(convertPermanent_, BM_SETCHECK, BST_CHECKED, 0); Command(ConvertPermanent);
    select(otherIndex); Require(authorization_ && Features() == original.features, "Bundle product switch must restore exact rights");
    Command(Issue); pump();
    Require(generated.size() == 2 && generatedBase && generatedBase->stem() == L"multi-ui"
        && generated[0].kind == Kind::Permanent && generated[0].features == original.features
        && generated[1].kind == Kind::Permanent && generated[1].features == original.features,
        "Paid upgrade must preserve unrelated product and sign full bundle");
    services_.inspectAuthorization = [](const auto&, const auto&, const auto&) -> AuthorizationInfo {
        throw std::runtime_error("invalid authorization");
    };
    InspectSelection(L"bad-ui.tdact"); pump();
    Require(authorization_ && CurrentDraft().retained && CurrentDraft().source.stem() == L"multi-ui"
        && IsWindowEnabled(issue_), "Failed replacement must preserve loaded bundle");
    bool blocked{};
    try { InspectSelection(L"wrong-ui.tdreq"); } catch (const std::exception&) { blocked = true; }
    Require(blocked && !busy_ && authorization_, "Existing product cannot be replaced by a new request");
    return true;
}
void Window::CheckSelection(const std::filesystem::path& path, const std::filesystem::path& report) {
    InspectSelection(path);
    const auto started = GetTickCount64();
    while (busy_) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            if (message.message != WM_QUIT) { TranslateMessage(&message); DispatchMessageW(&message); }
        Require(GetTickCount64() - started < 30000, "Selection check timed out");
        Sleep(1);
    }
    if (worker_.joinable()) worker_.join();
    std::ofstream output(report, std::ios::binary | std::ios::trunc);
    output << "requestVerified=" << request_.has_value()
        << "\nauthorizationVerified=" << authorization_.has_value()
        << "\ncredentialAvailable=" << !settings_.password.empty()
        << "\nfeatures=" << Features()
        << "\nfeaturesValid=" << IsValidFeatureSet(*product_, Features())
        << "\ngenerateEnabled=" << (IsWindowEnabled(issue_) != FALSE)
        << "\nsummary=" << WideToUtf8(TextOf(summary_))
        << "\nstatus=" << WideToUtf8(TextOf(status_)) << '\n';
    Require(static_cast<bool>(output), "Cannot save selection check");
}
void Window::Capture(const std::filesystem::path& path, int section) {
    if (section == 1) Command(Options);
    if (section == 2) Command(Configuration);
    if (section == 3) {
        Require(testing_, "Upgrade capture must use synthetic settings");
        services_.isBundle = [](const auto&) { return false; };
        services_.inspectAuthorization = [](const auto&, const auto&, const auto&) {
            AuthorizationInfo result; result.policy.customer = WideToUtf8(L"示例客户");
            result.policy.features = RequiredFeatures(Feature::ProductDesign);
            result.policy.kind = Kind::Trial; result.policy.days = 7; return result;
        };
        InspectSelection(L"示例客户-原授权.tdact");
        while (busy_) {
            MSG pending{};
            while (PeekMessageW(&pending, nullptr, 0, 0, PM_REMOVE)) {
                if (pending.message != WM_QUIT) { TranslateMessage(&pending); DispatchMessageW(&pending); }
            }
            Sleep(1);
        }
        if (worker_.joinable()) worker_.join();
    }
    ShowWindow(window_, SW_SHOWNORMAL);
    // The first call can honor a hidden STARTUPINFO supplied by the test runner.
    ShowWindow(window_, SW_SHOWNORMAL); UpdateWindow(window_);
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
    RECT rect{}; GetWindowRect(window_, &rect);
    const int width = rect.right - rect.left, height = rect.bottom - rect.top;
    auto source = GetDC(window_); auto target = CreateCompatibleDC(source);
    auto bitmap = CreateCompatibleBitmap(source, width, height);
    auto previous = SelectObject(target, bitmap);
    const BOOL okay = PrintWindow(window_, target, 0);
    SendMessageW(window_, WM_PRINT, reinterpret_cast<WPARAM>(target),
        PRF_NONCLIENT | PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
    SelectObject(target, previous);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    Bytes pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    const int rows = GetDIBits(target, bitmap, 0, static_cast<UINT>(height), pixels.data(), &info, DIB_RGB_COLORS);
    DeleteObject(bitmap); DeleteDC(target); ReleaseDC(window_, source);
    Require(okay && rows == height, "Cannot capture issuer window");
    BITMAPFILEHEADER header{}; header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
    header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.size());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(&header), sizeof(header));
    output.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    output.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    Require(static_cast<bool>(output), "Cannot save issuer window capture");
}
}
int RunIssuerWindow(HINSTANCE instance, int show) {
    int count{}; auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::wstring> options;
    if (arguments) {
        for (int index = 1; index < count; ++index) options.emplace_back(arguments[index]);
        LocalFree(arguments);
    }
    if (options.size() == 1 && options[0] == L"--check-settings") {
        auto settings = LoadSettings(TubeDesignerProduct);
        const bool ready = !settings.storeDirectory.empty() && !settings.password.empty();
        Wipe(settings.password); return ready ? 0 : 1;
    }
    const bool testing = options.size() == 1 && options[0] == L"--self-test";
    const bool capture = (options.size() == 2 || options.size() == 3) && options[0] == L"--capture-ui";
    const bool checkSelection = options.size() == 3 && options[0] == L"--check-selection";
    Require(options.empty() || testing || capture || checkSelection, "Unknown issuer argument");
    const bool syntheticCapture = capture && options.size() == 3 && options[2] == L"upgrade";
    Window window(instance, testing || syntheticCapture); window.Create(testing || checkSelection ? SW_HIDE : show);
    if (testing) return window.SelfTest() ? 0 : 1;
    if (checkSelection) { window.CheckSelection(options[1], options[2]); return 0; }
    if (capture) {
        int section{};
        if (options.size() == 3) {
            Require(options[2] == L"options" || options[2] == L"configuration" || options[2] == L"upgrade", "Unknown capture section");
            section = options[2] == L"options" ? 1 : (options[2] == L"configuration" ? 2 : 3);
        }
        window.Capture(options[1], section); return 0;
    }
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window.Handle(), &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    return static_cast<int>(message.wParam);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    INITCOMMONCONTROLSEX controls{}; controls.dwSize = sizeof(controls); controls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int result{};
    try { result = tube::license::issuer_native::RunIssuerWindow(instance, show); }
    catch (const std::exception& error) {
        OutputDebugStringA(error.what()); result = 1;
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
    return result;
}
