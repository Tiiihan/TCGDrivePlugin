#include "filesystem_bridge.h"

#include "../api/i_drive_client.h"
#include "../api/stub_drive_client.h"
#include "../gui/gui_manager.h"   
#include "../storage/config_manager.h"
#include "../utils/logger.h"
#include "../utils/string_utils.h"

#include <QString>

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace {

bool panelInUkrainian() {
    return ConfigManager::instance().language() == L"uk";
}

std::wstring signInEntry() {
    return panelInUkrainian() ? L"[Увійти в Google Drive]"
                              : L"[Sign in to Google Drive]";
}
std::wstring settingsEntry() {
    return panelInUkrainian() ? L"[Налаштування]" : L"[Settings]";
}
std::wstring signOutEntry() {
    return panelInUkrainian() ? L"[Вийти]" : L"[Sign out]";
}
std::wstring refreshEntry() {
    return panelInUkrainian() ? L"[Оновити]" : L"[Refresh]";
}
std::wstring sharedWithMeEntry() {
    return panelInUkrainian() ? L"[Спільне зі мною]" : L"[Shared with me]";
}

constexpr const char* kSharedWithMeSentinel = "__shared_with_me__";

constexpr std::int64_t kEpochOffset100ns = 116444736000000000LL;

FILETIME chronoToFileTime(std::chrono::system_clock::time_point tp) {
    FILETIME ft{};
    if (tp.time_since_epoch().count() == 0) return ft;
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        tp.time_since_epoch()).count();
    const std::int64_t winTicks = ns / 100 + kEpochOffset100ns;
    ft.dwLowDateTime  = static_cast<DWORD>(winTicks & 0xFFFFFFFFu);
    ft.dwHighDateTime = static_cast<DWORD>(static_cast<std::uint64_t>(winTicks) >> 32);
    return ft;
}

std::string  narrow(const std::wstring& w) { return StringUtils::toUtf8(w); }
std::wstring widen (const std::string&  s) { return StringUtils::fromUtf8(s); }

std::wstring toLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}

bool isRoot(const std::wstring& path) {
    if (path.empty()) return true;
    for (wchar_t c : path) if (c != L'\\' && c != L'/') return false;
    return true;
}

std::wstring canonicalPath(const std::wstring& path) {
    std::wstring out = path;
    std::replace(out.begin(), out.end(), L'/', L'\\');
    while (out.size() > 1 && out.back() == L'\\') out.pop_back();
    return out;
}

bool isRootChild(const std::wstring& path, const std::wstring& leaf) {
    return canonicalPath(path) == (L"\\" + leaf);
}

bool matchesEntry(const std::wstring& path, const wchar_t* en, const wchar_t* uk) {
    const std::wstring c = canonicalPath(path);
    return c == (std::wstring(L"\\") + en) || c == (std::wstring(L"\\") + uk);
}
bool isSignInPath(const std::wstring& p) {
    return matchesEntry(p, L"[Sign in to Google Drive]", L"[Увійти в Google Drive]");
}
bool isSettingsPath(const std::wstring& p) {
    return matchesEntry(p, L"[Settings]", L"[Налаштування]");
}
bool isSignOutPath(const std::wstring& p) {
    return matchesEntry(p, L"[Sign out]", L"[Вийти]");
}
bool isRefreshPath(const std::wstring& p) {
    return matchesEntry(p, L"[Refresh]", L"[Оновити]");
}
bool isSharedWithMePath(const std::wstring& p) {
    return matchesEntry(p, L"[Shared with me]", L"[Спільне зі мною]");
}

std::wstring drivePathFor(const std::wstring& path) {
    const std::wstring canonical = canonicalPath(path);
    const std::wstring signInRoot = std::wstring(L"\\") + signInEntry();
    if (canonical == signInRoot) return L"\\";
    const std::wstring signInPrefix = signInRoot + L"\\";
    if (canonical.rfind(signInPrefix, 0) == 0) {
        return canonical.substr(signInRoot.size());
    }
    return path;
}

DriveFile pseudoEntry(const std::wstring& name, bool isFolder = false) {
    DriveFile f;
    f.id = narrow(name);
    f.name = narrow(name);
    f.size = 0;
    f.isFolder = isFolder;
    return f;
}

std::wstring formatSize(std::int64_t bytes) {
    if (bytes < 0) bytes = 0;
    wchar_t buf[32];
    if (bytes >= 1LL << 30)
        std::swprintf(buf, 32, L"%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    else if (bytes >= 1LL << 20)
        std::swprintf(buf, 32, L"%.0f MB", bytes / (1024.0 * 1024.0));
    else
        std::swprintf(buf, 32, L"%lld KB", static_cast<long long>(bytes >> 10));
    return buf;
}

std::wstring quotaEntryName(const DriveQuota& q) {
    const bool uk = panelInUkrainian();
    const std::wstring prefix = uk ? L"[Сховище: " : L"[Storage: ";
    if (q.limitBytes <= 0)
        return prefix + formatSize(q.usageBytes)
             + (uk ? L" використано]" : L" used]");
    const std::int64_t freeBytes = q.limitBytes - q.usageBytes;
    return prefix + formatSize(freeBytes > 0 ? freeBytes : 0)
         + (uk ? L" вільно з " : L" free of ")
         + formatSize(q.limitBytes) + L"]";
}

} 

FilesystemBridge& FilesystemBridge::instance() {
    static FilesystemBridge s_instance;
    return s_instance;
}

FilesystemBridge::FilesystemBridge() {
    m_drive = std::make_shared<StubDriveClient>();
}

FilesystemBridge::~FilesystemBridge() = default;

void FilesystemBridge::initialize(int            pluginNr,
                                  tProgressProcW progressProc,
                                  tLogProcW      logProc,
                                  tRequestProcW  requestProc) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pluginNr     = pluginNr;
    m_progressProc = progressProc;
    m_logProc      = logProc;
    m_requestProc  = requestProc;
    Logger::instance().info("FilesystemBridge initialized, pluginNr="
                            + std::to_string(pluginNr));
}

void FilesystemBridge::setCryptCallback(tCryptProcW cryptProc, int cryptoNr, int flags) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cryptProc  = cryptProc;
    m_cryptoNr   = cryptoNr;
    m_cryptFlags = flags;
}

void FilesystemBridge::setDriveClient(std::shared_ptr<IGoogleDriveClient> client) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_drive = std::move(client);
    m_pathToId.clear();
}

void FilesystemBridge::setTokenSource(TokenSource tokenSource) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tokenSource = std::move(tokenSource);
}

void FilesystemBridge::setAuthTrigger(AuthTrigger trigger) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_authTrigger = std::move(trigger);
}

void FilesystemBridge::setSettingsTrigger(SettingsTrigger trigger) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_settingsTrigger = std::move(trigger);
}

void FilesystemBridge::setLogoutTrigger(LogoutTrigger trigger) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_logoutTrigger = std::move(trigger);
}

void FilesystemBridge::onAccountChanged() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pathToId.clear();
}

void FilesystemBridge::setCacheTtl(int seconds) {
    auto drive = driveSnapshot();
    if (drive) drive->setCacheTtl(std::chrono::seconds(seconds));
}

void FilesystemBridge::refreshCache() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pathToId.clear();
    }
    auto drive = driveSnapshot();
    if (drive) drive->clearCache();
    Logger::instance().info("FilesystemBridge: cache refreshed");
}

std::shared_ptr<IGoogleDriveClient> FilesystemBridge::driveSnapshot() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_drive;
}

std::string FilesystemBridge::tokenSnapshot() {
    TokenSource ts;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ts = m_tokenSource;
    }
    if (!ts) return {};
    try { return ts(); }
    catch (const std::exception& e) {
        Logger::instance().error(std::string("token source threw: ") + e.what());
        return {};
    }
}

HANDLE FilesystemBridge::generateHandle() {
    while (true) {
        auto raw = m_nextHandleId.fetch_add(1, std::memory_order_relaxed);
        if (raw == 0) continue;
        HANDLE h = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(raw));
        if (h != INVALID_HANDLE_VALUE) return h;
    }
}

void FilesystemBridge::populateFindData(const DriveFile& src, WIN32_FIND_DATAW& dst) const {
    ZeroMemory(&dst, sizeof(dst));
    const std::wstring wname = widen(src.name);
    const std::size_t copyLen = std::min<std::size_t>(wname.size(), MAX_PATH - 1);
    std::wmemcpy(dst.cFileName, wname.c_str(), copyLen);
    dst.cFileName[copyLen] = L'\0';

    if (src.isFolder) {
        dst.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
    } else {
        dst.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
        dst.nFileSizeLow  = static_cast<DWORD>(src.size & 0xFFFFFFFFull);
        dst.nFileSizeHigh = static_cast<DWORD>(static_cast<std::uint64_t>(src.size) >> 32);
    }

    dst.ftCreationTime   = chronoToFileTime(src.createdTime);
    dst.ftLastWriteTime  = chronoToFileTime(src.modifiedTime);
    dst.ftLastAccessTime = dst.ftLastWriteTime;
}

bool FilesystemBridge::reportProgress(const std::wstring& source,
                                      const std::wstring& target,
                                      int                 percent) {
    tProgressProcW cb = nullptr;
    int            pluginNr = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cb       = m_progressProc;
        pluginNr = m_pluginNr;
    }
    if (!cb) return true;
    auto srcBuf = source;
    auto tgtBuf = target;
    return cb(pluginNr, srcBuf.data(), tgtBuf.data(), percent) == 0;
}

void FilesystemBridge::splitParentAndName(const std::wstring& path,
                                          std::wstring&       parent,
                                          std::wstring&       name) {
    auto pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        parent = L"";
        name   = path;
    } else {
        parent = path.substr(0, pos);
        name   = path.substr(pos + 1);
    }
    if (parent.empty()) parent = L"\\"; // canonicalise root to "\\"
}

void FilesystemBridge::logToTC(int msgType, const std::wstring& message) {
    tLogProcW cb       = nullptr;
    int       pluginNr = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cb       = m_logProc;
        pluginNr = m_pluginNr;
    }
    if (!cb) return;
    std::wstring buf = message;     // tLogProcW takes a mutable WCHAR*
    cb(pluginNr, msgType, buf.data());
}

void FilesystemBridge::showMessageToTC(const std::wstring& text) {
    tRequestProcW cb       = nullptr;
    int           pluginNr = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cb       = m_requestProc;
        pluginNr = m_pluginNr;
    }
    if (!cb) return;
    std::wstring title = L"Google Drive";
    std::wstring body  = text;
    cb(pluginNr, RT_MsgOK, title.data(), body.data(), nullptr, 0);
}

std::string FilesystemBridge::resolveToId(const std::wstring& path) {
    const std::wstring drivePath = drivePathFor(path);
    if (isRoot(drivePath)) return "root";

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_pathToId.find(drivePath);
        if (it != m_pathToId.end()) return it->second;
    }

    auto drive = driveSnapshot();
    if (!drive) return {};
    const std::string token = tokenSnapshot();

    std::string current = "root";
    auto segments = StringUtils::splitPath(drivePath);
    for (const auto& seg : segments) {
        const std::wstring needle = toLower(seg);

        DriveList listing = (current == kSharedWithMeSentinel)
            ? drive->listSharedWithMe(token)
            : drive->listFiles(current, token);

        bool found = false;
        for (const auto& f : listing.files) {
            if (toLower(widen(f.name)) == needle) {
                current = f.id;
                found = true;
                break;
            }
        }

        if (!found && current == "root") {
            DriveList sds = drive->listSharedDrives(token);
            for (const auto& sd : sds.drives) {
                if (toLower(widen(sd.name)) == needle) {
                    current = sd.id;
                    found = true;
                    break;
                }
            }
            if (!found
                && (needle == toLower(std::wstring(L"[Shared with me]"))
                    || needle == toLower(std::wstring(L"[Спільне зі мною]")))) {
                current = kSharedWithMeSentinel;
                found = true;
            }
        }

        if (!found) {
            Logger::instance().debug("resolveToId: segment not found '"
                                     + narrow(seg) + "' in path "
                                     + narrow(drivePath));
            return {};
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pathToId[drivePath] = current;
    }
    return current;
}

HANDLE FilesystemBridge::findFirst(const std::wstring& path, WIN32_FIND_DATAW& findData) {
    Logger::instance().debug("Bridge::findFirst " + narrow(path));

    auto drive = driveSnapshot();
    if (!drive) { SetLastError(ERROR_NOT_READY); return INVALID_HANDLE_VALUE; }

    const bool signInPath = isSignInPath(path);
    const bool hasToken = !tokenSnapshot().empty();
    if (!hasToken) {
        if (signInPath) {
            AuthTrigger trigger;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                trigger = m_authTrigger;
            }
            if (trigger) {
                Logger::instance().info("Bridge::findFirst: explicit sign-in requested");
                trigger();
            }
            if (tokenSnapshot().empty()) {
                SetLastError(ERROR_ACCESS_DENIED);
                return INVALID_HANDLE_VALUE;
            }
            logToTC(MSGTYPE_CONNECT, L"Google Drive");
        } else
        if (isRoot(path)) {
            auto ctx = std::make_unique<FindContext>();
            ctx->path = path;
            ctx->entries.push_back(pseudoEntry(signInEntry()));
            ctx->entries.push_back(pseudoEntry(settingsEntry()));

            populateFindData(ctx->entries[0], findData);
            ctx->cursor = 1;

            HANDLE id = generateHandle();
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_findHandles.emplace(id, std::move(ctx));
            }
            return id;
        }

        SetLastError(ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }

    const std::wstring listingPath = drivePathFor(path);

    if (isSharedWithMePath(listingPath)) {
        const std::string tok = tokenSnapshot();
        DriveList listing = drive->listSharedWithMe(tok);
        auto ctx = std::make_unique<FindContext>();
        ctx->path    = listingPath;
        ctx->entries = std::move(listing.files);
        if (ctx->entries.empty()) {
            SetLastError(ERROR_NO_MORE_FILES);
            return INVALID_HANDLE_VALUE;
        }
        populateFindData(ctx->entries[0], findData);
        ctx->cursor = 1;
        HANDLE id = generateHandle();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_findHandles.emplace(id, std::move(ctx));
        }
        return id;
    }

    const std::string folderId = resolveToId(listingPath);
    if (folderId.empty()) {
        const long st = drive->lastHttpStatus();
        if (st == 401 || st == 403) {
            Logger::instance().warn("Bridge::findFirst: access denied (HTTP "
                                    + std::to_string(st) + ") — re-authentication required");
            SetLastError(ERROR_ACCESS_DENIED);
        } else if (st == 0) {
            Logger::instance().warn("Bridge::findFirst: no response from Google Drive "
                                    "(network error)");
            SetLastError(ERROR_NOT_CONNECTED);
        } else {
            SetLastError(ERROR_PATH_NOT_FOUND);
        }
        return INVALID_HANDLE_VALUE;
    }

    const std::string token   = tokenSnapshot();
    DriveList         listing = drive->listFiles(folderId, token);

    auto ctx = std::make_unique<FindContext>();
    ctx->path    = listingPath;
    ctx->entries = std::move(listing.files);
    if (isRoot(listingPath)) {
        const DriveQuota quota = drive->getQuota(token);
        const std::wstring quotaName = quotaEntryName(quota);
        ctx->entries.insert(ctx->entries.begin(), pseudoEntry(quotaName));
        ctx->entries.insert(ctx->entries.begin(), pseudoEntry(refreshEntry()));
        ctx->entries.insert(ctx->entries.begin(), pseudoEntry(signOutEntry()));
        ctx->entries.insert(ctx->entries.begin(), pseudoEntry(settingsEntry()));

        ctx->entries.push_back(pseudoEntry(sharedWithMeEntry(), true));

        DriveList sharedDrives = drive->listSharedDrives(token);
        for (const auto& sd : sharedDrives.drives) {
            DriveFile f;
            f.id       = sd.id;
            f.name     = sd.name;
            f.isFolder = true;
            f.createdTime = sd.createdTime;
            ctx->entries.push_back(f);
        }
    }

    if (ctx->entries.empty()) {
        SetLastError(ERROR_NO_MORE_FILES);
        return INVALID_HANDLE_VALUE;
    }

    populateFindData(ctx->entries[0], findData);
    ctx->cursor = 1;

    HANDLE id = generateHandle();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_findHandles.emplace(id, std::move(ctx));
    }
    return id;
}

BOOL FilesystemBridge::findNext(HANDLE hdl, WIN32_FIND_DATAW& findData) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_findHandles.find(hdl);
    if (it == m_findHandles.end()) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    FindContext& ctx = *it->second;
    if (ctx.cursor >= ctx.entries.size()) { SetLastError(ERROR_NO_MORE_FILES); return FALSE; }
    populateFindData(ctx.entries[ctx.cursor++], findData);
    return TRUE;
}

int FilesystemBridge::findClose(HANDLE hdl) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_findHandles.erase(hdl);
    return 0;
}

int FilesystemBridge::getFile(const std::wstring& remoteName,
                              const std::wstring& localName,
                              int                 copyFlags,
                              RemoteInfoStruct* /*ri*/) {
    Logger::instance().info("Bridge::getFile remote=" + narrow(remoteName)
                            + " local=" + narrow(localName)
                            + " flags=" + std::to_string(copyFlags));

    if (!(copyFlags & FS_COPYFLAGS_OVERWRITE)
        && GetFileAttributesW(localName.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return FS_FILE_EXISTS;
    }

    auto drive = driveSnapshot();
    if (!drive) return FS_FILE_NOTSUPPORTED;
    const std::string fileId = resolveToId(remoteName);
    if (fileId.empty()) return FS_FILE_NOTFOUND;

    std::wstring parent, leaf;
    splitParentAndName(remoteName, parent, leaf);
    ProgressDialog* dlg = GUIManager::instance().createProgressDialog(
        QString::fromStdWString(leaf), 0);

    const std::string token = tokenSnapshot();
    bool cancelled = false;
    bool ok = drive->downloadFile(fileId, narrow(localName),
        [&](std::int64_t now, std::int64_t total) -> bool {
            GUIManager::instance().updateProgressDialog(dlg, now, total);
            const int pct = (total > 0) ? static_cast<int>((now * 100) / total) : 0;
            const bool tcWantsContinue = reportProgress(remoteName, localName, pct);
            const bool userCancelled =
                GUIManager::instance().isProgressDialogCancelled(dlg);
            if (!tcWantsContinue || userCancelled) {
                cancelled = true;
                return false;
            }
            return true;
        }, token);

    GUIManager::instance().destroyProgressDialog(dlg);
    if (cancelled) return FS_FILE_USERABORT;
    if (!ok) {
        if (drive->lastHttpStatus() == 0) {
            showMessageToTC(panelInUkrainian()
                ? L"Немає з'єднання з Google Drive. Перевірте підключення до Інтернету."
                : L"No connection to Google Drive. Please check your internet connection.");
        }
        logToTC(MSGTYPE_IMPORTANTERROR, L"Download failed: " + remoteName);
        return FS_FILE_READERROR;
    }
    logToTC(MSGTYPE_TRANSFERCOMPLETE, remoteName + L" -> " + localName);
    return FS_FILE_OK;
}

int FilesystemBridge::putFile(const std::wstring& localName,
                              const std::wstring& remoteName,
                              int                 copyFlags) {
    Logger::instance().info("Bridge::putFile local=" + narrow(localName)
                            + " remote=" + narrow(remoteName)
                            + " flags=" + std::to_string(copyFlags));

    auto drive = driveSnapshot();
    if (!drive) return FS_FILE_NOTSUPPORTED;

    std::wstring parent, name;
    splitParentAndName(remoteName, parent, name);
    const std::string parentId = resolveToId(parent);
    if (parentId.empty()) return FS_FILE_WRITEERROR;

    const std::string existingId = resolveToId(remoteName);
    if (!existingId.empty()) {
        if (!(copyFlags & FS_COPYFLAGS_OVERWRITE)) return FS_FILE_EXISTS;
        if (!deleteFile(remoteName)) {
            Logger::instance().warn("putFile: could not remove existing target before overwrite");
            return FS_FILE_WRITEERROR;
        }
    }

    ProgressDialog* dlg = GUIManager::instance().createProgressDialog(
        QString::fromStdWString(name), 0);

    const std::string token = tokenSnapshot();

    try {
        const std::int64_t fileSize =
            static_cast<std::int64_t>(std::filesystem::file_size(localName));
        if (fileSize > 0) {
            const DriveQuota quota = drive->getQuota(token);
            if (quota.limitBytes > 0) {
                const std::int64_t freeBytes = quota.limitBytes - quota.usageBytes;
                if (fileSize > freeBytes) {
                    Logger::instance().warn(
                        "putFile: not enough Google Drive space — need "
                        + narrow(formatSize(fileSize)) + ", free "
                        + narrow(formatSize(freeBytes > 0 ? freeBytes : 0)));
                    GUIManager::instance().destroyProgressDialog(dlg);

                    showMessageToTC(panelInUkrainian()
                        ? L"Недостатньо вільного місця на Google Drive для цього файлу."
                        : L"Not enough Google Drive storage space for this file.");
                    logToTC(MSGTYPE_IMPORTANTERROR,
                            L"Upload failed (quota): " + remoteName);
                    return FS_FILE_WRITEERROR;
                }
            }
        }
    } catch (const std::exception& e) {
        Logger::instance().warn(std::string("putFile: quota check failed: ") + e.what());
    }

    bool cancelled = false;
    bool ok = drive->uploadFile(narrow(localName), parentId, narrow(name),
        [&](std::int64_t now, std::int64_t total) -> bool {
            GUIManager::instance().updateProgressDialog(dlg, now, total);
            const int pct = (total > 0) ? static_cast<int>((now * 100) / total) : 0;
            const bool tcWantsContinue = reportProgress(localName, remoteName, pct);
            const bool userCancelled =
                GUIManager::instance().isProgressDialogCancelled(dlg);
            if (!tcWantsContinue || userCancelled) {
                cancelled = true;
                return false;
            }
            return true;
        }, token);

    GUIManager::instance().destroyProgressDialog(dlg);
    if (cancelled) return FS_FILE_USERABORT;
    if (!ok) {
        if (drive->lastHttpStatus() == 0) {
            showMessageToTC(panelInUkrainian()
                ? L"Немає з'єднання з Google Drive. Перевірте підключення до Інтернету."
                : L"No connection to Google Drive. Please check your internet connection.");
        }
        logToTC(MSGTYPE_IMPORTANTERROR, L"Upload failed: " + remoteName);
        return FS_FILE_WRITEERROR;
    }
    logToTC(MSGTYPE_TRANSFERCOMPLETE, localName + L" -> " + remoteName);
    return FS_FILE_OK;
}

BOOL FilesystemBridge::deleteFile(const std::wstring& remoteName) {
    Logger::instance().info("Bridge::deleteFile " + narrow(remoteName));
    auto drive = driveSnapshot();
    if (!drive) return FALSE;
    const std::string fileId = resolveToId(remoteName);
    if (fileId.empty()) return FALSE;
    const std::string token = tokenSnapshot();
    bool ok = drive->deleteFile(fileId, token);
    if (ok) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pathToId.erase(remoteName);
    }
    return ok ? TRUE : FALSE;
}

BOOL FilesystemBridge::mkDir(const std::wstring& path) {
    Logger::instance().info("Bridge::mkDir " + narrow(path));
    auto drive = driveSnapshot();
    if (!drive) return FALSE;

    std::wstring parent, name;
    splitParentAndName(path, parent, name);
    if (name.empty()) return FALSE;

    const std::string parentId = resolveToId(parent);
    if (parentId.empty()) return FALSE;
    const std::string token = tokenSnapshot();
    bool ok = drive->createFolder(narrow(name), parentId, token);
    if (ok) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pathToId.erase(parent);
    }
    return ok ? TRUE : FALSE;
}

BOOL FilesystemBridge::removeDir(const std::wstring& remoteName) {
    return deleteFile(remoteName);
}

int FilesystemBridge::renMovFile(const std::wstring& oldName,
                                 const std::wstring& newName,
                                 bool                /*move*/,
                                 bool                overWrite,
                                 RemoteInfoStruct* /*ri*/) {
    Logger::instance().info("Bridge::renMovFile " + narrow(oldName)
                            + " -> " + narrow(newName));

    auto drive = driveSnapshot();
    if (!drive) return FS_FILE_NOTSUPPORTED;

    const std::string fileId = resolveToId(oldName);
    if (fileId.empty()) return FS_FILE_NOTFOUND;

    std::wstring oldParent, oldStem, newParent, newStem;
    splitParentAndName(oldName, oldParent, oldStem);
    splitParentAndName(newName, newParent, newStem);

    const std::string token = tokenSnapshot();

    const std::string existingTarget = resolveToId(newName);
    if (!existingTarget.empty() && existingTarget != fileId) {
        if (!overWrite) return FS_FILE_EXISTS;
        if (!deleteFile(newName)) {
            Logger::instance().warn(
                "renMovFile: could not remove existing target before overwrite");
            return FS_FILE_WRITEERROR;
        }
    }

    if (oldParent != newParent) {
        const std::string oldParentId = resolveToId(oldParent);
        const std::string newParentId = resolveToId(newParent);
        if (oldParentId.empty() || newParentId.empty()) return FS_FILE_NOTFOUND;

        bool ok = drive->moveFile(fileId, oldParentId, newParentId, token);
        if (!ok) return FS_FILE_WRITEERROR;

        if (oldStem != newStem)
            drive->renameFile(fileId, narrow(newStem), token);

        std::lock_guard<std::mutex> lock(m_mutex);
        m_pathToId.erase(oldName);
        m_pathToId.erase(oldParent);
        m_pathToId.erase(newParent);
        return FS_FILE_OK;
    }

    bool ok = drive->renameFile(fileId, narrow(newStem), token);
    if (ok) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pathToId.erase(oldName);
        m_pathToId.erase(oldParent);
    }
    return ok ? FS_FILE_OK : FS_FILE_WRITEERROR;
}

void FilesystemBridge::getDefRootName(char* buf, int maxlen) {
    if (!buf || maxlen <= 0) return;
    static constexpr char kRoot[] = "Google Drive";
    const int copy = std::min<int>(maxlen - 1, static_cast<int>(sizeof(kRoot) - 1));
    std::memcpy(buf, kRoot, copy);
    buf[copy] = '\0';
}

int FilesystemBridge::backgroundFlags() const {
    return 1 | 2; // BG_DOWNLOAD | BG_UPLOAD
}

void FilesystemBridge::statusInfo(const std::wstring& remoteDir,
                                  int                 infoStartEnd,
                                  int                 infoOperation) {
    Logger::instance().debug("Bridge::statusInfo dir=" + narrow(remoteDir)
                             + " start=" + std::to_string(infoStartEnd)
                             + " op="    + std::to_string(infoOperation));
}

BOOL FilesystemBridge::disconnect(const std::wstring& disconnectRoot) {
    Logger::instance().info("Bridge::disconnect " + narrow(disconnectRoot));
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_findHandles.clear();
        m_pathToId.clear();
    }
    logToTC(MSGTYPE_DISCONNECT, L"Google Drive");
    return TRUE;
}

int FilesystemBridge::executeFile(std::wstring& remoteName,
                                  const std::wstring& /*verb*/) {
    Logger::instance().info("Bridge::executeFile " + narrow(remoteName));
    const std::wstring drivePath = drivePathFor(remoteName);

    if (isSignInPath(remoteName)) {
        AuthTrigger trigger;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            trigger = m_authTrigger;
        }
        if (!trigger) return FS_EXEC_ERROR;
        trigger();
        if (!tokenSnapshot().empty()) {
            logToTC(MSGTYPE_CONNECT, L"Google Drive");
        }
        remoteName = L"\\";
        return FS_EXEC_SYMLINK;
    }

    if (isSettingsPath(drivePath)) {
        SettingsTrigger trigger;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            trigger = m_settingsTrigger;
        }
        if (!trigger) return FS_EXEC_ERROR;
        trigger();
        remoteName = L"\\";
        return FS_EXEC_SYMLINK;
    }

    if (isSignOutPath(remoteName)) {
        LogoutTrigger trigger;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            trigger = m_logoutTrigger;
        }
        if (trigger) trigger();
        onAccountChanged();
        logToTC(MSGTYPE_DISCONNECT, L"Google Drive");
        remoteName = L"\\";
        return FS_EXEC_SYMLINK;
    }

    if (isRefreshPath(remoteName)) {
        refreshCache();
        remoteName = L"\\";
        return FS_EXEC_SYMLINK;
    }

    return FS_EXEC_YOURSELF;
}
