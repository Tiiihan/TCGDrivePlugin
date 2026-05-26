#include "webview2_auth_window.h"
#include "../utils/logger.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>      
#include <shlobj.h>        

#include <WebView2.h>
#include <wrl/client.h>    
#include <wrl/event.h>      

#include <string>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

std::string percentDecode(const std::wstring& in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        const wchar_t c = in[i];
        if (c == L'+') {
            out += ' ';
        } else if (c == L'%' && i + 2 < in.size()) {
            auto hexVal = [](wchar_t ch) -> int {
                if (ch >= L'0' && ch <= L'9') return ch - L'0';
                if (ch >= L'a' && ch <= L'f') return 10 + (ch - L'a');
                if (ch >= L'A' && ch <= L'F') return 10 + (ch - L'A');
                return -1;
            };
            const int hi = hexVal(in[i + 1]);
            const int lo = hexVal(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
            if (c < 128) out += static_cast<char>(c);
        } else if (c < 128) {
            out += static_cast<char>(c);
        }
    }
    return out;
}

std::string queryParam(const std::wstring& url, const std::string& key) {
    const auto q = url.find(L'?');
    if (q == std::wstring::npos) return {};

    std::wstring qs = url.substr(q + 1);

    const auto frag = qs.find(L'#');
    if (frag != std::wstring::npos) qs.resize(frag);

    const std::wstring wkey(key.begin(), key.end());
    std::size_t i = 0;
    while (i < qs.size()) {
        const auto eq  = qs.find(L'=', i);
        auto       amp = qs.find(L'&', i);
        if (amp == std::wstring::npos) amp = qs.size();

        if (eq != std::wstring::npos && eq < amp) {
            if (qs.substr(i, eq - i) == wkey) {
                return percentDecode(qs.substr(eq + 1, amp - eq - 1));
            }
        }
        i = amp + 1;
    }
    return {};
}

constexpr const wchar_t* kClassName      = L"TCGDriveOAuth2Window";
constexpr UINT_PTR       kTimeoutTimer   = 1u;
constexpr UINT_PTR       kCancelPollTimer = 2u;

struct WndState {
    std::string               redirectUriPrefix;
    WebView2Result            result;
    bool                      done     = false;
    const std::atomic<bool>*  cancelFlag = nullptr;
    ComPtr<ICoreWebView2Controller> controller;
};

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }

    auto* s = reinterpret_cast<WndState*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {

    case WM_SIZE:
        if (s && s->controller) {
            RECT r{};
            GetClientRect(hwnd, &r);
            s->controller->put_Bounds(r);
        }
        return 0;

    case WM_CLOSE:
        if (s && !s->done) {
            s->result.error = "user_cancelled";
            s->done         = true;
        }
        KillTimer(hwnd, kTimeoutTimer);
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_TIMER:
        if (s && !s->done) {
            if (wParam == kTimeoutTimer) {
                s->result.timedOut = true;
                s->result.error    = "timeout";
                s->done            = true;
                KillTimer(hwnd, kTimeoutTimer);
                KillTimer(hwnd, kCancelPollTimer);
                Logger::instance().warn("WebView2AuthWindow: sign-in timed out");
                DestroyWindow(hwnd);
            } else if (wParam == kCancelPollTimer
                       && s->cancelFlag && s->cancelFlag->load()) {
                // Another thread asked us to abort (e.g. the user pressed
                // Cancel in the Qt auth dialog).
                s->result.error = "user_cancelled";
                s->done         = true;
                KillTimer(hwnd, kTimeoutTimer);
                KillTimer(hwnd, kCancelPollTimer);
                Logger::instance().info("WebView2AuthWindow: cancelled on request");
                DestroyWindow(hwnd);
            }
        }
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void ensureWindowClassRegistered() {
    static bool registered = false;
    if (registered) return;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    registered = true;
}

} 

std::wstring WebView2AuthWindow::userDataPath() {
    wchar_t buf[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, buf);
    return std::wstring(buf) + L"\\TCGDrivePlugin\\WebView2Profile";
}

// ---------------------------------------------------------------------------
// WebView2AuthWindow::show
// ---------------------------------------------------------------------------

WebView2Result WebView2AuthWindow::show(const std::string&       authUrl,
                                        const std::string&       redirectUriPrefix,
                                        int                      timeoutSeconds,
                                        const std::atomic<bool>* cancelFlag) {
    ensureWindowClassRegistered();

    WndState state;
    state.redirectUriPrefix = redirectUriPrefix;
    state.cancelFlag        = cancelFlag;

    HWND hwnd = CreateWindowExW(
        0,
        kClassName,
        L"Google Drive — Sign in",   // em-dash
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        960, 720,
        nullptr, nullptr,
        GetModuleHandleW(nullptr),
        &state);

    if (!hwnd) {
        Logger::instance().error(
            "WebView2AuthWindow: CreateWindowExW failed ("
            + std::to_string(GetLastError()) + ")");
        state.result.error = "window_create_failed";
        return state.result;
    }

    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    if (timeoutSeconds > 0) {
        SetTimer(hwnd, kTimeoutTimer,
                 static_cast<UINT>(timeoutSeconds) * 1000U, nullptr);
    }
    if (cancelFlag) {
        SetTimer(hwnd, kCancelPollTimer, 200U, nullptr);
    }

    const std::wstring wAuthUrl(authUrl.begin(), authUrl.end());

    const std::wstring dataDir = WebView2AuthWindow::userDataPath();
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, dataDir.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [hwnd, &state, wAuthUrl](
            HRESULT                   envHr,
            ICoreWebView2Environment* env) -> HRESULT
        {
            if (FAILED(envHr) || !env) {
                Logger::instance().error(
                    "WebView2AuthWindow: environment creation failed (hr=0x"
                    + []( HRESULT h ){
                        char buf[12];
                        std::snprintf(buf, sizeof(buf), "%08lX",
                                      static_cast<unsigned long>(h));
                        return std::string(buf);
                    }(envHr)
                    + "). Ensure the WebView2 Runtime is installed.");
                state.result.error = "webview2_unavailable";
                state.done         = true;
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return envHr;
            }

            return env->CreateCoreWebView2Controller(
                hwnd,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [hwnd, &state, wAuthUrl](
                    HRESULT                  ctrlHr,
                    ICoreWebView2Controller* ctrl) -> HRESULT
                {
                    if (FAILED(ctrlHr) || !ctrl) {
                        Logger::instance().error(
                            "WebView2AuthWindow: controller creation failed (hr=0x"
                            + []( HRESULT h ){
                                char buf[12];
                                std::snprintf(buf, sizeof(buf), "%08lX",
                                              static_cast<unsigned long>(h));
                                return std::string(buf);
                            }(ctrlHr) + ")");
                        state.result.error = "webview2_ctrl_failed";
                        state.done         = true;
                        PostMessageW(hwnd, WM_CLOSE, 0, 0);
                        return ctrlHr;
                    }

                    state.controller = ctrl;

                    RECT bounds{};
                    GetClientRect(hwnd, &bounds);
                    ctrl->put_Bounds(bounds);
                    ctrl->put_IsVisible(TRUE);

                    ComPtr<ICoreWebView2> webview;
                    ctrl->get_CoreWebView2(&webview);
                    if (!webview) {
                        state.result.error = "webview2_interface_missing";
                        state.done         = true;
                        PostMessageW(hwnd, WM_CLOSE, 0, 0);
                        return E_FAIL;
                    }

                    ComPtr<ICoreWebView2Settings> settings;
                    if (SUCCEEDED(webview->get_Settings(&settings)) && settings) {
                        settings->put_AreDefaultContextMenusEnabled(FALSE);
                        settings->put_IsStatusBarEnabled(FALSE);
                    }

                    const std::string redirectPrefix = state.redirectUriPrefix;
                    webview->add_NavigationStarting(
                        Callback<ICoreWebView2NavigationStartingEventHandler>(
                        [hwnd, &state, redirectPrefix](
                            ICoreWebView2*,
                            ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT
                        {
                            LPWSTR uriRaw = nullptr;
                            args->get_Uri(&uriRaw);
                            if (!uriRaw) return S_OK;

                            const std::wstring uri = uriRaw;
                            CoTaskMemFree(uriRaw);   // always free COM memory

                            const std::wstring wPrefix(redirectPrefix.begin(),
                                                       redirectPrefix.end());
                            if (uri.rfind(wPrefix, 0) != 0) {
                                return S_OK;
                            }

                            args->put_Cancel(TRUE);

                            state.result.code    = queryParam(uri, "code");
                            state.result.state   = queryParam(uri, "state");
                            state.result.error   = queryParam(uri, "error");
                            state.result.success = !state.result.code.empty();
                            state.done           = true;

                            if (state.result.success) {
                                Logger::instance().info(
                                    "WebView2AuthWindow: authorization code received");
                            } else {
                                Logger::instance().warn(
                                    "WebView2AuthWindow: OAuth error — "
                                    + state.result.error);
                            }

                            KillTimer(hwnd, kTimeoutTimer);
                            PostMessageW(hwnd, WM_CLOSE, 0, 0);
                            return S_OK;
                        }).Get(), nullptr);

                    webview->Navigate(wAuthUrl.c_str());
                    Logger::instance().info(
                        "WebView2AuthWindow: navigating to authorization URL");
                    return S_OK;
                }).Get());   
        }).Get());            

    if (FAILED(hr)) {
        Logger::instance().error(
            "WebView2AuthWindow: CreateCoreWebView2EnvironmentWithOptions "
            "failed (hr=0x"
            + [hr]{
                char buf[12];
                std::snprintf(buf, sizeof(buf), "%08lX",
                              static_cast<unsigned long>(hr));
                return std::string(buf);
            }()
            + "). Install the WebView2 Runtime from "
              "https://developer.microsoft.com/microsoft-edge/webview2/");
        state.result.error = "webview2_unavailable";
        DestroyWindow(hwnd);
        return state.result;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (state.controller) {
        state.controller->Close();
        state.controller.Reset();
    }

    return state.result;
}
