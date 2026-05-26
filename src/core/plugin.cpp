#include "plugin.h"

#include "filesystem_bridge.h"
#include "../api/drive_client.h"
#include "../auth/oauth_manager.h"
#include "../gui/gui_manager.h"
#include "../storage/config_manager.h"
#include "../utils/logger.h"
#include "../utils/string_utils.h"

#include <exception>
#include <cstring>
#include <cwchar>
#include <memory>
#include <mutex>
#include <string>

#ifndef BG_DOWNLOAD
#  define BG_DOWNLOAD 1
#endif
#ifndef BG_UPLOAD
#  define BG_UPLOAD   2
#endif

extern "C" int __stdcall FsGetBackgroundFlags(void);

namespace {

struct PluginContext {
    static PluginContext& instance() { static PluginContext c; return c; }
    std::once_flag                initFlag;
    std::shared_ptr<OAuthManager> oauth;
    std::shared_ptr<DriveClient>  drive;
};

LogLevel logLevelFromName(const std::string& name) {
    if (name == "DEBUG")   return LogLevel::Debug;
    if (name == "WARNING") return LogLevel::Warning;
    if (name == "ERROR")   return LogLevel::Error;
    return LogLevel::Info;
}

std::string currentAccessToken(const std::shared_ptr<OAuthManager>& oauth) {
    if (!oauth) return {};
    std::wstring acct = ConfigManager::instance().defaultAccount();
    if (acct.empty()) {
        const auto list = oauth->getAccountList();
        if (!list.empty()) acct = StringUtils::fromUtf8(list.front());
    }
    if (acct.empty()) return {};
    return oauth->getAccessToken(StringUtils::toUtf8(acct));
}

void ensurePluginInitialized() {
    auto& ctx = PluginContext::instance();
    std::call_once(ctx.initFlag, []() {
        auto& cfg = ConfigManager::instance();
        cfg.load();

        Logger::instance().setLevel(logLevelFromName(cfg.logLevelName()));
        Logger::instance().info("TCGDrivePlugin: initializing");

        auto& c = PluginContext::instance();
        c.oauth = std::make_shared<OAuthManager>();
        c.drive = std::make_shared<DriveClient>();

        GUIManager::instance().setOAuthManager(c.oauth.get());

        auto& bridge = FilesystemBridge::instance();
        bridge.setDriveClient(c.drive);
        bridge.setCacheTtl(cfg.cacheTtlSeconds());

        const auto oauth = c.oauth;   
        bridge.setTokenSource([oauth]() -> std::string {
            return currentAccessToken(oauth);
        });
        bridge.setAuthTrigger([oauth]() {
            GUIManager::instance().showAuthDialog(oauth.get());
        });
        bridge.setSettingsTrigger([]() {
            GUIManager::instance().showSettings();
        });
        bridge.setLogoutTrigger([oauth]() {
            std::wstring acct = ConfigManager::instance().defaultAccount();
            if (acct.empty()) {
                const auto list = oauth->getAccountList();
                if (!list.empty()) acct = StringUtils::fromUtf8(list.front());
            }
            if (acct.empty()) return;
            oauth->logout(StringUtils::toUtf8(acct));
            if (ConfigManager::instance().defaultAccount() == acct) {
                ConfigManager::instance().setDefaultAccount(L"");
                ConfigManager::instance().save();
            }
        });

        if (cfg.clientId().empty()
            || cfg.clientId() == StringUtils::fromUtf8(ConfigManager::DEFAULT_CLIENT_ID)) {
            Logger::instance().warn("TCGDrivePlugin: the OAuth client credentials are still the "
                                    "build placeholders — replace DEFAULT_CLIENT_ID / "
                                    "DEFAULT_CLIENT_SECRET in config_manager.h (or set "
                                    "client_id / client_secret in config.ini) before sign-in "
                                    "will work");
        }
        Logger::instance().info("TCGDrivePlugin: initialized");
    });
}


std::string ws2utf8(const wchar_t* w) {
    if (!w) return "<null>";
    if (!*w) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return "";
    std::string out(static_cast<std::size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring utf82ws(const char* s) {
    if (!s) return L"";
    int len = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    if (len <= 1) return L"";
    std::wstring out(static_cast<std::size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_ACP, 0, s, -1, out.data(), len);
    return out;
}

void trace(const char* fn, const std::string& detail = {}) {
    if (detail.empty()) {
        Logger::instance().debug(std::string("WFX ") + fn);
    } else {
        Logger::instance().debug(std::string("WFX ") + fn + ": " + detail);
    }
}

void logException(const char* fn, const std::exception& e) {
    Logger::instance().error(std::string("WFX ") + fn + " threw: " + e.what());
}

void logUnknownException(const char* fn) {
    Logger::instance().error(std::string("WFX ") + fn + " threw unknown exception");
}

bool tryEnsurePluginInitialized(const char* fn) {
    try {
        ensurePluginInitialized();
        return true;
    } catch (const std::exception& e) {
        logException(fn, e);
    } catch (...) {
        logUnknownException(fn);
    }
    return false;
}

} 

extern "C" int __stdcall FsInit(int          PluginNr,
                                tProgressProc /*pProgressProc*/,
                                tLogProc      /*pLogProc*/,
                                tRequestProc  /*pRequestProc*/) {
    tryEnsurePluginInitialized("FsInit");
    trace("FsInit", "pluginNr=" + std::to_string(PluginNr));
    return 0;
}

extern "C" int __stdcall FsInitW(int           PluginNr,
                                 tProgressProcW pProgressProcW,
                                 tLogProcW      pLogProcW,
                                 tRequestProcW  pRequestProcW) {
    try {
        tryEnsurePluginInitialized("FsInitW");
        trace("FsInitW", "pluginNr=" + std::to_string(PluginNr));
        FilesystemBridge::instance().initialize(
            PluginNr, pProgressProcW, pLogProcW, pRequestProcW);
        GUIManager::instance().ensureInitialized();
        return 0;
    } catch (const std::exception& e) {
        logException("FsInitW", e);
    } catch (...) {
        logUnknownException("FsInitW");
    }
    return 0;
}

extern "C" HANDLE __stdcall FsFindFirst(char* Path, WIN32_FIND_DATA* FindData) {
    trace("FsFindFirst", ws2utf8(utf82ws(Path).c_str()));
    if (!Path || !FindData) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    try {
        // The SDK declares FindData as WIN32_FIND_DATA*, which under
        // our UNICODE build is identical to WIN32_FIND_DATAW*. Pass
        // it straight through; only Path needs ANSI->wide conversion.
        return FilesystemBridge::instance().findFirst(utf82ws(Path), *FindData);
    } catch (const std::exception& e) { logException("FsFindFirst", e); }
      catch (...)                     { logUnknownException("FsFindFirst"); }
    SetLastError(ERROR_GEN_FAILURE);
    return INVALID_HANDLE_VALUE;
}

extern "C" HANDLE __stdcall FsFindFirstW(WCHAR* Path, WIN32_FIND_DATAW* FindData) {
    trace("FsFindFirstW", ws2utf8(Path));
    if (!Path || !FindData) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    try {
        return FilesystemBridge::instance().findFirst(Path, *FindData);
    } catch (const std::exception& e) { logException("FsFindFirstW", e); }
      catch (...)                     { logUnknownException("FsFindFirstW"); }
    SetLastError(ERROR_GEN_FAILURE);
    return INVALID_HANDLE_VALUE;
}

extern "C" BOOL __stdcall FsFindNext(HANDLE Hdl, WIN32_FIND_DATA* FindData) {
    trace("FsFindNext");
    if (!FindData) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    try {
        // WIN32_FIND_DATA == WIN32_FIND_DATAW under UNICODE; no
        // down-conversion needed (and impossible — cFileName is wide).
        return FilesystemBridge::instance().findNext(Hdl, *FindData);
    } catch (const std::exception& e) { logException("FsFindNext", e); }
      catch (...)                     { logUnknownException("FsFindNext"); }
    return FALSE;
}

extern "C" BOOL __stdcall FsFindNextW(HANDLE Hdl, WIN32_FIND_DATAW* FindData) {
    trace("FsFindNextW");
    if (!FindData) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    try {
        return FilesystemBridge::instance().findNext(Hdl, *FindData);
    } catch (const std::exception& e) { logException("FsFindNextW", e); }
      catch (...)                     { logUnknownException("FsFindNextW"); }
    return FALSE;
}

extern "C" int __stdcall FsFindClose(HANDLE Hdl) {
    trace("FsFindClose");
    try {
        return FilesystemBridge::instance().findClose(Hdl);
    } catch (const std::exception& e) { logException("FsFindClose", e); }
      catch (...)                     { logUnknownException("FsFindClose"); }
    return 0;
}

// =====================================================================
// FsGetFile / FsPutFile
// Copying between Drive and the local filesystem. May run on a TC
// background-transfer thread once we advertise BG_DOWNLOAD/BG_UPLOAD.
// =====================================================================

extern "C" int __stdcall FsGetFile(char*  RemoteName,
                                   char*  LocalName,
                                   int    CopyFlags,
                                   RemoteInfoStruct* ri) {
    trace("FsGetFile");
    try {
        return FilesystemBridge::instance().getFile(
            utf82ws(RemoteName), utf82ws(LocalName), CopyFlags, ri);
    } catch (const std::exception& e) { logException("FsGetFile", e); }
      catch (...)                     { logUnknownException("FsGetFile"); }
    return FS_FILE_READERROR;
}

extern "C" int __stdcall FsGetFileW(WCHAR* RemoteName,
                                    WCHAR* LocalName,
                                    int    CopyFlags,
                                    RemoteInfoStruct* ri) {
    trace("FsGetFileW", ws2utf8(RemoteName));
    try {
        return FilesystemBridge::instance().getFile(
            RemoteName ? RemoteName : L"",
            LocalName  ? LocalName  : L"",
            CopyFlags, ri);
    } catch (const std::exception& e) { logException("FsGetFileW", e); }
      catch (...)                     { logUnknownException("FsGetFileW"); }
    return FS_FILE_READERROR;
}

extern "C" int __stdcall FsPutFile(char* LocalName, char* RemoteName, int CopyFlags) {
    trace("FsPutFile");
    try {
        return FilesystemBridge::instance().putFile(
            utf82ws(LocalName), utf82ws(RemoteName), CopyFlags);
    } catch (const std::exception& e) { logException("FsPutFile", e); }
      catch (...)                     { logUnknownException("FsPutFile"); }
    return FS_FILE_WRITEERROR;
}

extern "C" int __stdcall FsPutFileW(WCHAR* LocalName, WCHAR* RemoteName, int CopyFlags) {
    trace("FsPutFileW", ws2utf8(RemoteName));
    try {
        return FilesystemBridge::instance().putFile(
            LocalName  ? LocalName  : L"",
            RemoteName ? RemoteName : L"",
            CopyFlags);
    } catch (const std::exception& e) { logException("FsPutFileW", e); }
      catch (...)                     { logUnknownException("FsPutFileW"); }
    return FS_FILE_WRITEERROR;
}

// =====================================================================
// FsDeleteFile / FsMkDir / FsRemoveDir / FsRenMovFile
// =====================================================================

extern "C" BOOL __stdcall FsDeleteFile(char* RemoteName) {
    trace("FsDeleteFile");
    try {
        return FilesystemBridge::instance().deleteFile(utf82ws(RemoteName));
    } catch (const std::exception& e) { logException("FsDeleteFile", e); }
      catch (...)                     { logUnknownException("FsDeleteFile"); }
    return FALSE;
}

extern "C" BOOL __stdcall FsDeleteFileW(WCHAR* RemoteName) {
    trace("FsDeleteFileW", ws2utf8(RemoteName));
    try {
        return FilesystemBridge::instance().deleteFile(RemoteName ? RemoteName : L"");
    } catch (const std::exception& e) { logException("FsDeleteFileW", e); }
      catch (...)                     { logUnknownException("FsDeleteFileW"); }
    return FALSE;
}

extern "C" BOOL __stdcall FsMkDir(char* Path) {
    trace("FsMkDir");
    try {
        return FilesystemBridge::instance().mkDir(utf82ws(Path));
    } catch (const std::exception& e) { logException("FsMkDir", e); }
      catch (...)                     { logUnknownException("FsMkDir"); }
    return FALSE;
}

extern "C" BOOL __stdcall FsMkDirW(WCHAR* Path) {
    trace("FsMkDirW", ws2utf8(Path));
    try {
        return FilesystemBridge::instance().mkDir(Path ? Path : L"");
    } catch (const std::exception& e) { logException("FsMkDirW", e); }
      catch (...)                     { logUnknownException("FsMkDirW"); }
    return FALSE;
}

extern "C" BOOL __stdcall FsRemoveDir(char* RemoteName) {
    trace("FsRemoveDir");
    try {
        return FilesystemBridge::instance().removeDir(utf82ws(RemoteName));
    } catch (const std::exception& e) { logException("FsRemoveDir", e); }
      catch (...)                     { logUnknownException("FsRemoveDir"); }
    return FALSE;
}

extern "C" BOOL __stdcall FsRemoveDirW(WCHAR* RemoteName) {
    trace("FsRemoveDirW", ws2utf8(RemoteName));
    try {
        return FilesystemBridge::instance().removeDir(RemoteName ? RemoteName : L"");
    } catch (const std::exception& e) { logException("FsRemoveDirW", e); }
      catch (...)                     { logUnknownException("FsRemoveDirW"); }
    return FALSE;
}

extern "C" int __stdcall FsRenMovFile(char* OldName, char* NewName,
                                      BOOL Move, BOOL OverWrite,
                                      RemoteInfoStruct* ri) {
    trace("FsRenMovFile");
    try {
        return FilesystemBridge::instance().renMovFile(
            utf82ws(OldName), utf82ws(NewName), Move != FALSE, OverWrite != FALSE, ri);
    } catch (const std::exception& e) { logException("FsRenMovFile", e); }
      catch (...)                     { logUnknownException("FsRenMovFile"); }
    return FS_FILE_WRITEERROR;
}

extern "C" int __stdcall FsRenMovFileW(WCHAR* OldName, WCHAR* NewName,
                                       BOOL Move, BOOL OverWrite,
                                       RemoteInfoStruct* ri) {
    trace("FsRenMovFileW", ws2utf8(OldName) + " -> " + ws2utf8(NewName));
    try {
        return FilesystemBridge::instance().renMovFile(
            OldName ? OldName : L"",
            NewName ? NewName : L"",
            Move != FALSE, OverWrite != FALSE, ri);
    } catch (const std::exception& e) { logException("FsRenMovFileW", e); }
      catch (...)                     { logUnknownException("FsRenMovFileW"); }
    return FS_FILE_WRITEERROR;
}

// =====================================================================
// FsGetDefRootName — the user-visible mount point in TC's
// Network Neighborhood.
// =====================================================================

extern "C" void __stdcall FsGetDefRootName(char* DefRootName, int maxlen) {
    trace("FsGetDefRootName");
    try {
        FilesystemBridge::instance().getDefRootName(DefRootName, maxlen);
    } catch (const std::exception& e) { logException("FsGetDefRootName", e); }
      catch (...)                     { logUnknownException("FsGetDefRootName"); }
}

// =====================================================================
// FsSetCryptCallback — TC hands us a callback for storing/loading
// encrypted secrets in its password manager.
// =====================================================================

extern "C" void __stdcall FsSetCryptCallback(tCryptProc /*pCryptProc*/,
                                             int CryptoNr, int Flags) {
    trace("FsSetCryptCallback",
          "cryptoNr=" + std::to_string(CryptoNr) + " flags=" + std::to_string(Flags));
    // ANSI not stored; FsSetCryptCallbackW is what TC calls on
    // Unicode-capable builds.
}

extern "C" void __stdcall FsSetCryptCallbackW(tCryptProcW pCryptProcW,
                                              int CryptoNr, int Flags) {
    trace("FsSetCryptCallbackW",
          "cryptoNr=" + std::to_string(CryptoNr) + " flags=" + std::to_string(Flags));
    try {
        FilesystemBridge::instance().setCryptCallback(pCryptProcW, CryptoNr, Flags);
    } catch (const std::exception& e) { logException("FsSetCryptCallbackW", e); }
      catch (...)                     { logUnknownException("FsSetCryptCallbackW"); }
}

// =====================================================================
// FsGetBackgroundFlags — tells TC which transfer directions can run
// on its background-transfer pool.
// =====================================================================

extern "C" int __stdcall FsGetBackgroundFlags(void) {
    trace("FsGetBackgroundFlags");
    try {
        return FilesystemBridge::instance().backgroundFlags();
    } catch (const std::exception& e) { logException("FsGetBackgroundFlags", e); }
      catch (...)                     { logUnknownException("FsGetBackgroundFlags"); }
    return BG_DOWNLOAD | BG_UPLOAD;
}

// =====================================================================
// FsStatusInfo — TC notifies the plugin around multi-file ops; useful
// for cache invalidation and progress reporting.
// =====================================================================

extern "C" void __stdcall FsStatusInfo(char* RemoteDir, int InfoStartEnd, int InfoOperation) {
    trace("FsStatusInfo");
    try {
        FilesystemBridge::instance().statusInfo(
            utf82ws(RemoteDir), InfoStartEnd, InfoOperation);
    } catch (...) {}
}

extern "C" void __stdcall FsStatusInfoW(WCHAR* RemoteDir, int InfoStartEnd, int InfoOperation) {
    trace("FsStatusInfoW");
    try {
        FilesystemBridge::instance().statusInfo(
            RemoteDir ? RemoteDir : L"", InfoStartEnd, InfoOperation);
    } catch (...) {}
}

// =====================================================================
// FsDisconnect — user pressed Ctrl+F2 / Disconnect on our plugin root.
// =====================================================================

extern "C" BOOL __stdcall FsDisconnect(char* DisconnectRoot) {
    trace("FsDisconnect");
    try {
        return FilesystemBridge::instance().disconnect(utf82ws(DisconnectRoot));
    } catch (...) {}
    return FALSE;
}

extern "C" BOOL __stdcall FsDisconnectW(WCHAR* DisconnectRoot) {
    trace("FsDisconnectW");
    try {
        return FilesystemBridge::instance().disconnect(DisconnectRoot ? DisconnectRoot : L"");
    } catch (...) {}
    return FALSE;
}

// =====================================================================
// FsExecuteFile — user double-clicked a remote item. Returning
// FS_EXEC_YOURSELF tells TC to download it and let Windows handle
// "open" via shell associations.
// =====================================================================

extern "C" int __stdcall FsExecuteFile(HWND /*MainWin*/, char* RemoteName, char* Verb) {
    trace("FsExecuteFile");
    try {
        std::wstring target = utf82ws(RemoteName);
        const int rc = FilesystemBridge::instance().executeFile(target, utf82ws(Verb));
        if (rc == FS_EXEC_SYMLINK && RemoteName) {
            const std::string redirected = StringUtils::toUtf8(target);
            std::memcpy(RemoteName, redirected.c_str(), redirected.size() + 1);
        }
        return rc;
    } catch (const std::exception& e) { logException("FsExecuteFile", e); }
      catch (...)                     { logUnknownException("FsExecuteFile"); }
    return FS_EXEC_ERROR;
}

extern "C" int __stdcall FsExecuteFileW(HWND /*MainWin*/, WCHAR* RemoteName, WCHAR* Verb) {
    trace("FsExecuteFileW", ws2utf8(RemoteName));
    try {
        std::wstring target = RemoteName ? RemoteName : L"";
        const int rc = FilesystemBridge::instance().executeFile(
            target,
            Verb ? Verb : L"");
        if (rc == FS_EXEC_SYMLINK && RemoteName) {
            std::wmemcpy(RemoteName, target.c_str(), target.size() + 1);
        }
        return rc;
    } catch (const std::exception& e) { logException("FsExecuteFileW", e); }
      catch (...)                     { logUnknownException("FsExecuteFileW"); }
    return FS_EXEC_ERROR;
}

// =====================================================================
// FsSetDefaultParams — TC tells us where its default INI lives. The
// real implementation forwards this to ConfigManager.
// =====================================================================

extern "C" void __stdcall FsSetDefaultParams(FsDefaultParamStruct* dps) {
    trace("FsSetDefaultParams");
    try {
        // Total Commander suggests an INI location for the plugin in
        // dps->DefaultIniName. Hand it to ConfigManager, which adopts it
        // when appropriate (see ConfigManager::adoptIniPath).
        if (dps && dps->DefaultIniName[0] != '\0') {
            ConfigManager::instance().adoptIniPath(utf82ws(dps->DefaultIniName));
        }
    } catch (const std::exception& e) { logException("FsSetDefaultParams", e); }
      catch (...)                     { logUnknownException("FsSetDefaultParams"); }
}

// =====================================================================
// DllMain — minimal. No per-thread bookkeeping needed.
// =====================================================================

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*lpReserved*/) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule); // we don't need
                                                // DLL_THREAD_ATTACH/DETACH
            break;
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
