# WebView2 SDK

This directory must contain the Microsoft WebView2 SDK headers and static loader library
before the project can be compiled.

## Required structure

```
third_party/webview2/
├── include/
│   ├── WebView2.h               ← main header
│   └── WebView2EnvironmentOptions.h
└── lib/
    └── x64/
        └── WebView2LoaderStatic.lib   ← static loader (no runtime DLL needed)
```

## How to obtain

### Option A — NuGet (recommended)

```powershell
# From the solution directory:
nuget install Microsoft.Web.WebView2 -OutputDirectory packages
# Then copy:
#   packages\Microsoft.Web.WebView2.x.y.z\build\native\include\  → third_party\webview2\include\
#   packages\Microsoft.Web.WebView2.x.y.z\build\native\x64\      → third_party\webview2\lib\x64\
```

### Option B — Manual download

1. Go to https://developer.microsoft.com/microsoft-edge/webview2/
2. Download the **WebView2 SDK** (not the Runtime).
3. Extract and copy `include/` and `build/native/x64/` into this directory.

## Runtime requirement

The **WebView2 Runtime** must be installed on the end-user machine.

- **Windows 11** — bundled with the OS.
- **Windows 10** — installed automatically with Microsoft Edge (Chromium).
  Manual installer: https://go.microsoft.com/fwlink/p/?LinkId=2124703

If the Runtime is absent at sign-in time, `WebView2AuthWindow::show()` returns
`result.error == "webview2_unavailable"` and the authentication flow fails gracefully
with a log message instructing the user to install the Runtime.
