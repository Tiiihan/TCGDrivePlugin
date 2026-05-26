#pragma once

#include <windows.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "fsplugin.h"
#include "../api/drive_models.h"

class IGoogleDriveClient;

class FilesystemBridge {
public:
    using TokenSource = std::function<std::string()>;
    using AuthTrigger     = std::function<void()>;
    using SettingsTrigger = std::function<void()>;
    using LogoutTrigger   = std::function<void()>;

    static FilesystemBridge& instance();

    void initialize(int            pluginNr,
                    tProgressProcW progressProc,
                    tLogProcW      logProc,
                    tRequestProcW  requestProc);

    void setCryptCallback(tCryptProcW cryptProc, int cryptoNr, int flags);
    void setDriveClient (std::shared_ptr<IGoogleDriveClient> client);
    void setTokenSource (TokenSource tokenSource);
    void setAuthTrigger (AuthTrigger trigger);
    void setSettingsTrigger(SettingsTrigger trigger);
    void setLogoutTrigger  (LogoutTrigger   trigger);

    HANDLE findFirst(const std::wstring& path, WIN32_FIND_DATAW& findData);
    BOOL   findNext (HANDLE hdl, WIN32_FIND_DATAW& findData);
    int    findClose(HANDLE hdl);

    int  getFile   (const std::wstring& remoteName,
                    const std::wstring& localName,
                    int copyFlags,
                    RemoteInfoStruct* ri);
    int  putFile   (const std::wstring& localName,
                    const std::wstring& remoteName,
                    int copyFlags);
    BOOL deleteFile(const std::wstring& remoteName);
    BOOL mkDir     (const std::wstring& path);
    BOOL removeDir (const std::wstring& remoteName);
    int  renMovFile(const std::wstring& oldName,
                    const std::wstring& newName,
                    bool move,
                    bool overWrite,
                    RemoteInfoStruct* ri);

    void getDefRootName(char* buf, int maxlen);
    int  backgroundFlags() const;
    void statusInfo(const std::wstring& remoteDir, int infoStartEnd, int infoOperation);
    BOOL disconnect(const std::wstring& disconnectRoot);
    int  executeFile(std::wstring& remoteName, const std::wstring& verb);

    void onAccountChanged();
    void setCacheTtl(int seconds);
    void refreshCache();

private:
    FilesystemBridge();
    ~FilesystemBridge();

    FilesystemBridge(const FilesystemBridge&)            = delete;
    FilesystemBridge& operator=(const FilesystemBridge&) = delete;

    struct FindContext {
        std::wstring           path;
        std::vector<DriveFile> entries;     //елементи каталогу
        std::size_t            cursor = 0;  //позиція ітерації
    };

    std::shared_ptr<IGoogleDriveClient> driveSnapshot();
    std::string                         tokenSnapshot();
    HANDLE                              generateHandle();
    void                                populateFindData(const DriveFile& src,
                                                         WIN32_FIND_DATAW& dst) const;
    bool                                reportProgress(const std::wstring& source,
                                                       const std::wstring& target,
                                                       int                 percent);

    void logToTC(int msgType, const std::wstring& message);
    void showMessageToTC(const std::wstring& text);

    std::string resolveToId(const std::wstring& path);

    static void splitParentAndName(const std::wstring& path,
                                   std::wstring&       parent,
                                   std::wstring&       name);

    mutable std::mutex m_mutex;

    int            m_pluginNr     = 0;
    tProgressProcW m_progressProc = nullptr;
    tLogProcW      m_logProc      = nullptr;
    tRequestProcW  m_requestProc  = nullptr;
    tCryptProcW    m_cryptProc    = nullptr;
    int            m_cryptoNr     = 0;
    int            m_cryptFlags   = 0;

    std::shared_ptr<IGoogleDriveClient>         m_drive;
    TokenSource                                 m_tokenSource;
    AuthTrigger                                 m_authTrigger;
    SettingsTrigger                             m_settingsTrigger;
    LogoutTrigger                               m_logoutTrigger;
    std::unordered_map<std::wstring, std::string> m_pathToId;

    std::unordered_map<HANDLE, std::unique_ptr<FindContext>> m_findHandles;
    std::atomic<std::uintptr_t>                              m_nextHandleId{1};
};
