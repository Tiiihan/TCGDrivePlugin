#include "drive_client.h"

#include "../storage/config_manager.h"
#include "../utils/logger.h"
#include "../utils/string_utils.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

namespace {

using nlohmann::json;

constexpr const char* kFilesEndpoint     = "https://www.googleapis.com/drive/v3/files";
constexpr const char* kUploadEndpoint    = "https://www.googleapis.com/upload/drive/v3/files";
constexpr const char* kDrivesEndpoint    = "https://www.googleapis.com/drive/v3/drives";
constexpr const char* kFolderMime        = "application/vnd.google-apps.folder";
constexpr const char* kGoogleDocsMime    = "application/vnd.google-apps.document";
constexpr const char* kGoogleSheetsMime  = "application/vnd.google-apps.spreadsheet";
constexpr const char* kGoogleSlidesMime  = "application/vnd.google-apps.presentation";

constexpr std::int64_t kResumableChunkBytes = 8LL * 1024 * 1024;

constexpr const char* kListFields =
    "nextPageToken,files(id,name,mimeType,parents,size,modifiedTime,createdTime,"
    "trashed,md5Checksum,webViewLink,shared)";
constexpr const char* kFileFields =
    "id,name,mimeType,parents,size,modifiedTime,createdTime,trashed,md5Checksum,"
    "webViewLink,shared";


bool endsWithAsciiInsensitive(const std::string& value, const std::string& suffix) {
    if (suffix.size() > value.size()) return false;
    auto it = value.end() - static_cast<std::ptrdiff_t>(suffix.size());
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        unsigned char a = static_cast<unsigned char>(*(it + static_cast<std::ptrdiff_t>(i)));
        unsigned char b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}

bool isGoogleNativeMime(const std::string& mime) {
    return mime.rfind("application/vnd.google-apps.", 0) == 0
        && mime != kFolderMime;
}

std::string exportExtensionForMime(const std::string& mime) {
    if (mime == kGoogleDocsMime) {
        return ConfigManager::instance().exportDocsAsDocx() ? ".docx" : ".pdf";
    }
    if (mime == kGoogleSheetsMime) {
        return ConfigManager::instance().exportSheetsAsXlsx() ? ".xlsx" : ".pdf";
    }
    if (mime == kGoogleSlidesMime) return ".pptx";
    return ".pdf";
}

std::string exportMimeForDriveMime(const std::string& mime) {
    if (mime == kGoogleDocsMime) {
        return ConfigManager::instance().exportDocsAsDocx()
            ? "application/vnd.openxmlformats-officedocument.wordprocessingml.document"
            : "application/pdf";
    }
    if (mime == kGoogleSheetsMime) {
        return ConfigManager::instance().exportSheetsAsXlsx()
            ? "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"
            : "application/pdf";
    }
    if (mime == kGoogleSlidesMime) {
        return "application/vnd.openxmlformats-officedocument.presentationml.presentation";
    }
    return "application/pdf";
}

std::string withExportExtension(const std::string& name, const std::string& mime) {
    if (!isGoogleNativeMime(mime)) return name;
    const std::string ext = exportExtensionForMime(mime);
    return endsWithAsciiInsensitive(name, ext) ? name : name + ext;
}


std::chrono::system_clock::time_point parseRfc3339(const std::string& s) {
    if (s.size() < 19) return {};
    std::tm tm{};
    int yyyy = 0, mm = 0, dd = 0, hh = 0, mi = 0, ss = 0;
    if (::sscanf_s(s.c_str(), "%d-%d-%dT%d:%d:%d",
                   &yyyy, &mm, &dd, &hh, &mi, &ss) != 6) {
        return {};
    }
    tm.tm_year = yyyy - 1900;
    tm.tm_mon  = mm - 1;
    tm.tm_mday = dd;
    tm.tm_hour = hh;
    tm.tm_min  = mi;
    tm.tm_sec  = ss;
    std::time_t t = ::_mkgmtime(&tm);
    if (t == static_cast<std::time_t>(-1)) return {};
    return std::chrono::system_clock::from_time_t(t);
}


DriveFile parseDriveFile(const json& j) {
    DriveFile f;
    f.id          = j.value("id",          std::string{});
    f.name        = j.value("name",        std::string{});
    f.mimeType    = j.value("mimeType",    std::string{});
    f.isFolder    = (f.mimeType == kFolderMime);
    f.name        = withExportExtension(f.name, f.mimeType);
    f.trashed     = j.value("trashed",     false);
    f.shared      = j.value("shared",      false);
    f.md5Checksum = j.value("md5Checksum", std::string{});
    f.webViewLink = j.value("webViewLink", std::string{});

    if (j.contains("parents") && j["parents"].is_array()) {
        for (auto& p : j["parents"]) {
            if (p.is_string()) f.parents.push_back(p.get<std::string>());
        }
    }

    if (j.contains("size")) {
        try {
            f.size = std::stoll(j["size"].is_string()
                                ? j["size"].get<std::string>()
                                : std::to_string(j["size"].get<std::int64_t>()));
        } catch (...) { f.size = 0; }
    }

    if (j.contains("modifiedTime") && j["modifiedTime"].is_string())
        f.modifiedTime = parseRfc3339(j["modifiedTime"].get<std::string>());
    if (j.contains("createdTime") && j["createdTime"].is_string())
        f.createdTime  = parseRfc3339(j["createdTime"].get<std::string>());

    return f;
}

DriveFolder parseDriveFolder(const json& j) {
    DriveFolder d;
    d.id   = j.value("id",   std::string{});
    d.name = j.value("name", std::string{});
    if (j.contains("createdTime") && j["createdTime"].is_string())
        d.createdTime = parseRfc3339(j["createdTime"].get<std::string>());
    return d;
}

std::filesystem::path fsPath(const std::string& utf8) {
    return std::filesystem::path(StringUtils::fromUtf8(utf8));
}

std::int64_t fileSize(const std::string& utf8Path) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(fsPath(utf8Path), ec);
    if (ec) return -1;
    return static_cast<std::int64_t>(sz);
}

} 

DriveClient::DriveClient()
    : m_http (std::make_shared<HttpClient>()),
      m_cache(std::make_shared<MetadataCache>()) {}

DriveClient::DriveClient(std::shared_ptr<HttpClient>    http,
                         std::shared_ptr<MetadataCache> cache)
    : m_http (std::move(http)),
      m_cache(std::move(cache)) {
    if (!m_http)  m_http  = std::make_shared<HttpClient>();
    if (!m_cache) m_cache = std::make_shared<MetadataCache>();
}

DriveClient::~DriveClient() = default;

void DriveClient::setCacheTtl(std::chrono::seconds ttl) {
    m_cache->setTtl(ttl);
}

void DriveClient::clearCache() {
    m_cache->clear();
    std::lock_guard<std::mutex> lock(m_quotaMutex);
    m_quotaCachedAt = {};   // force the next getQuota() to refetch
}

DriveQuota DriveClient::getQuota(const std::string& token) {
    {
        std::lock_guard<std::mutex> lock(m_quotaMutex);
        if (m_quotaCachedAt.time_since_epoch().count() != 0
            && std::chrono::steady_clock::now() - m_quotaCachedAt < kQuotaTtl) {
            return m_cachedQuota;
        }
    }

    constexpr const char* kAboutEndpoint =
        "https://www.googleapis.com/drive/v3/about?fields=storageQuota";

    HttpResponse rep = callWithRetry(
        [&]() { return m_http->get(kAboutEndpoint, authHeaders(token)); },
        "getQuota", /*maxRetries=*/0, /*maxBackoffSec=*/0);

    if (!rep.ok()) return {};
    try {
        json j = json::parse(rep.body);
        if (!j.contains("storageQuota")) return {};
        const auto& sq = j["storageQuota"];
        DriveQuota q;
        if (sq.contains("limit") && sq["limit"].is_string())
            q.limitBytes = std::stoll(sq["limit"].get<std::string>());
        if (sq.contains("usage") && sq["usage"].is_string())
            q.usageBytes = std::stoll(sq["usage"].get<std::string>());
        {
            std::lock_guard<std::mutex> lock(m_quotaMutex);
            m_cachedQuota   = q;
            m_quotaCachedAt = std::chrono::steady_clock::now();
        }
        return q;
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("getQuota parse: ") + e.what());
        return {};
    }
}

std::vector<std::string> DriveClient::authHeaders(const std::string& token) {
    return { "Authorization: Bearer " + token };
}

std::vector<std::string> DriveClient::jsonAuthHeaders(const std::string& token) {
    return {
        "Authorization: Bearer " + token,
        "Content-Type: application/json; charset=UTF-8",
    };
}

HttpResponse DriveClient::callWithRetry(
    const std::function<HttpResponse()>& doRequest,
    const std::string&                   diagLabel,
    int                                  maxRetries,
    int                                  maxBackoffSec,
    const std::function<bool()>&         shouldCancel) {
    HttpResponse rep;
    int   attempt = 0;
    int   delaySec = 1;
    while (true) {
        if (shouldCancel && shouldCancel()) {
            rep.aborted = true;
            return rep;
        }
        rep = doRequest();
        m_lastHttpStatus.store(rep.error.empty() ? rep.statusCode : 0,
                               std::memory_order_relaxed);

        if (rep.aborted) return rep;

        const bool transportErr = !rep.error.empty();
        const bool retryable    = transportErr
                               || rep.statusCode == 429
                               || (rep.statusCode >= 500 && rep.statusCode < 600);
        if (!retryable) return rep;
        if (attempt >= maxRetries) {
            Logger::instance().error(diagLabel + " gave up after "
                                     + std::to_string(maxRetries) + " retries; "
                                     + (transportErr ? rep.error
                                        : ("HTTP " + std::to_string(rep.statusCode))));
            return rep;
        }

        int wait = delaySec;
        auto it  = rep.headers.find("retry-after");
        if (it != rep.headers.end()) {
            try { wait = std::max(wait, std::stoi(it->second)); }
            catch (...) {}
        }
        wait = std::min(wait, maxBackoffSec);
        Logger::instance().warn(diagLabel + " transient failure ("
                                + (transportErr ? rep.error
                                   : ("HTTP " + std::to_string(rep.statusCode)))
                                + "); sleeping " + std::to_string(wait) + "s");
        for (int slice = 0; slice < wait * 10; ++slice) {
            if (shouldCancel && shouldCancel()) {
                rep.aborted = true;
                return rep;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        ++attempt;
        delaySec = std::min(delaySec * 2, maxBackoffSec);
    }
}

DriveList DriveClient::listFiles(const std::string& folderId,
                                 const std::string& token) {
    DriveList cached;
    if (m_cache->getList(folderId, cached)) {
        return cached;
    }

    DriveList out;
    std::string pageToken;
    do {
        const std::string q = "'" + folderId + "' in parents and trashed = false";
        std::string url = std::string(kFilesEndpoint)
            + "?q="             + StringUtils::urlEncode(q)
            + "&pageSize=1000"
            + "&fields="        + StringUtils::urlEncode(kListFields)
            + "&supportsAllDrives=true&includeItemsFromAllDrives=true";
        if (!pageToken.empty()) {
            url += "&pageToken=" + StringUtils::urlEncode(pageToken);
        }

        HttpResponse rep = callWithRetry(
            [&]() { return m_http->get(url, authHeaders(token)); },
            "listFiles(" + folderId + ")");

        if (!rep.ok()) return {};
        try {
            json j = json::parse(rep.body);
            pageToken = j.value("nextPageToken", std::string{});
            if (j.contains("files") && j["files"].is_array()) {
                for (auto& item : j["files"]) out.files.push_back(parseDriveFile(item));
            }
        } catch (const std::exception& e) {
            Logger::instance().error(std::string("listFiles parse: ") + e.what());
            return {};
        }
    } while (!pageToken.empty());

    m_cache->putList(folderId, out);
    for (auto& f : out.files) m_cache->putFile(f.id, f);
    return out;
}

DriveFile DriveClient::getFileMetadata(const std::string& fileId,
                                       const std::string& token) {
    DriveFile cached;
    if (m_cache->getFile(fileId, cached)) return cached;

    const std::string url = std::string(kFilesEndpoint) + "/" + StringUtils::urlEncode(fileId)
        + "?fields=" + StringUtils::urlEncode(kFileFields)
        + "&supportsAllDrives=true";

    HttpResponse rep = callWithRetry(
        [&]() { return m_http->get(url, authHeaders(token)); },
        "getFileMetadata(" + fileId + ")");

    DriveFile out;
    if (!rep.ok()) return out;
    try {
        out = parseDriveFile(json::parse(rep.body));
        m_cache->putFile(fileId, out);
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("getFileMetadata parse: ") + e.what());
        return {};
    }
    return out;
}

bool DriveClient::downloadFile(const std::string& fileId,
                               const std::string& localPath,
                               ProgressCallback   cb,
                               const std::string& token) {
    DriveFile meta = getFileMetadata(fileId, token);

    std::atomic<std::int64_t> lastNow{0}, lastTotal{0};
    ProgressCallback track = cb
        ? ProgressCallback([cb, &lastNow, &lastTotal](std::int64_t n, std::int64_t t) {
              lastNow.store(n); lastTotal.store(t); return cb(n, t); })
        : ProgressCallback{};
    auto shouldCancel = cb
        ? std::function<bool()>([cb, &lastNow, &lastTotal]() {
              return !cb(lastNow.load(), lastTotal.load()); })
        : std::function<bool()>{};

    if (isGoogleNativeMime(meta.mimeType)) {
        const std::string exportMime = exportMimeForDriveMime(meta.mimeType);
        const std::string url = std::string(kFilesEndpoint) + "/" + StringUtils::urlEncode(fileId)
            + "/export?mimeType=" + StringUtils::urlEncode(exportMime);

        HttpResponse rep = callWithRetry(
            [&]() { return m_http->downloadToFile(url, localPath, track, authHeaders(token)); },
            "exportFile(" + fileId + ")",
            kBackgroundRetries, kBackgroundMaxBackoff, shouldCancel);
        if (!rep.ok()) {
            Logger::instance().error("exportFile HTTP " + std::to_string(rep.statusCode)
                                     + " body=" + rep.body);
        }
        return rep.ok();
    }

    const std::string url = std::string(kFilesEndpoint) + "/" + StringUtils::urlEncode(fileId)
        + "?alt=media&supportsAllDrives=true";

    HttpResponse rep = callWithRetry(
        [&]() { return m_http->downloadToFile(url, localPath, track, authHeaders(token)); },
        "downloadFile(" + fileId + ")",
        kBackgroundRetries, kBackgroundMaxBackoff, shouldCancel);
    return rep.ok();
}

bool DriveClient::uploadFile(const std::string& localPath,
                             const std::string& parentId,
                             const std::string& targetName,
                             ProgressCallback   cb,
                             const std::string& token) {
    const std::int64_t size = fileSize(localPath);
    if (size < 0) {
        Logger::instance().error("uploadFile: cannot stat " + localPath);
        return false;
    }

    std::string fileName = targetName;
    if (fileName.empty())
        fileName = StringUtils::toUtf8(fsPath(localPath).filename().wstring());
    if (fileName.empty()) fileName = "upload.bin";

    const bool ok = uploadResumable(localPath, fileName, parentId, size,
                                    std::move(cb), token);
    if (ok) m_cache->invalidateList(parentId);
    return ok;
}

bool DriveClient::uploadResumable(const std::string& localPath,
                                  const std::string& fileName,
                                  const std::string& parentId,
                                  std::int64_t       size,
                                  ProgressCallback   cb,
                                  const std::string& token) {

    std::atomic<std::int64_t> lastNow{0}, lastTotal{0};
    ProgressCallback track = cb
        ? ProgressCallback([cb, &lastNow, &lastTotal](std::int64_t n, std::int64_t t) {
              lastNow.store(n); lastTotal.store(t); return cb(n, t); })
        : ProgressCallback{};
    auto shouldCancel = cb
        ? std::function<bool()>([cb, &lastNow, &lastTotal]() {
              return !cb(lastNow.load(), lastTotal.load()); })
        : std::function<bool()>{};

    json metadata;
    metadata["name"]    = fileName;
    metadata["parents"] = json::array({ parentId });
    const std::string metaBody = metadata.dump();

    std::vector<std::string> initHeaders = {
        "Authorization: Bearer "             + token,
        "Content-Type: application/json; charset=UTF-8",
        "X-Upload-Content-Type: application/octet-stream",
        "X-Upload-Content-Length: " + std::to_string(size),
    };

    const std::string initUrl = std::string(kUploadEndpoint)
        + "?uploadType=resumable&supportsAllDrives=true&fields=id";

    HttpResponse init = callWithRetry(
        [&]() { return m_http->post(initUrl, metaBody, initHeaders); },
        "uploadResumable.init(" + fileName + ")",
        kBackgroundRetries, kBackgroundMaxBackoff, shouldCancel);
    if (init.aborted) return false;
    if (!init.ok()) {
        Logger::instance().error("resumable init HTTP " + std::to_string(init.statusCode)
                                 + " body=" + init.body);
        return false;
    }

    auto locIt = init.headers.find("location");
    if (locIt == init.headers.end() || locIt->second.empty()) {
        Logger::instance().error("resumable init: missing Location header");
        return false;
    }
    const std::string sessionUri = locIt->second;
    Logger::instance().info("resumable session opened for " + fileName);

    if (size <= kResumableChunkBytes) {
        std::vector<std::string> putHeaders = {
            "Content-Type: application/octet-stream",
            "Content-Length: " + std::to_string(size),
        };
        HttpResponse put = callWithRetry(
            [&]() {
                return m_http->uploadFromFile("PUT", sessionUri, localPath,
                                              /*offset=*/0, size, track, putHeaders);
            },
            "uploadResumable.put(" + fileName + ")",
            kBackgroundRetries, kBackgroundMaxBackoff, shouldCancel);
        if (put.aborted) return false;
        if (!put.ok()) {
            Logger::instance().error("resumable put HTTP " + std::to_string(put.statusCode)
                                     + " body=" + put.body);
            return false;
        }
        return true;
    }

    std::int64_t offset = 0;
    while (offset < size) {
        const std::int64_t chunkLen = std::min<std::int64_t>(kResumableChunkBytes,
                                                             size - offset);
        const std::int64_t chunkEnd = offset + chunkLen - 1;        // inclusive
        const bool         lastChunk = (offset + chunkLen >= size);

        std::vector<std::string> chunkHeaders = {
            "Content-Type: application/octet-stream",
            "Content-Length: " + std::to_string(chunkLen),
            "Content-Range: bytes " + std::to_string(offset) + "-"
                + std::to_string(chunkEnd) + "/" + std::to_string(size),
        };

        const std::int64_t base = offset;
        ProgressCallback chunkCb;
        if (track) {
            chunkCb = [track, base, size](std::int64_t now, std::int64_t) -> bool {
                return track(base + now, size);
            };
        }

        HttpResponse rep = callWithRetry(
            [&]() {
                return m_http->uploadFromFile("PUT", sessionUri, localPath,
                                              offset, chunkLen, chunkCb, chunkHeaders);
            },
            "uploadResumable.chunk(" + fileName + " @" + std::to_string(offset) + ")",
            kBackgroundRetries, kBackgroundMaxBackoff, shouldCancel);

        if (rep.aborted) return false;

        if (lastChunk) {
            if (!rep.ok()) {
                Logger::instance().error("resumable final chunk HTTP "
                                         + std::to_string(rep.statusCode)
                                         + " body=" + rep.body);
                return false;
            }
        } else if (rep.statusCode != 308) {
            Logger::instance().error("resumable chunk expected 308, got "
                                     + std::to_string(rep.statusCode)
                                     + " body=" + rep.body);
            return false;
        }
        offset += chunkLen;
    }
    return true;
}

bool DriveClient::deleteFile(const std::string& fileId,
                             const std::string& token) {
    std::vector<std::string> parents;
    DriveFile cachedMeta;
    if (m_cache->getFile(fileId, cachedMeta)) parents = cachedMeta.parents;

    const std::string url = std::string(kFilesEndpoint) + "/" + StringUtils::urlEncode(fileId)
        + "?supportsAllDrives=true";
    HttpResponse rep = callWithRetry(
        [&]() { return m_http->del(url, authHeaders(token)); },
        "deleteFile(" + fileId + ")");
    if (rep.ok() || rep.statusCode == 204) {
        m_cache->invalidateFile(fileId);
        if (parents.empty()) {
            m_cache->clear();   
        } else {
            for (const auto& p : parents) m_cache->invalidateList(p);
        }
        return true;
    }
    return false;
}

bool DriveClient::createFolder(const std::string& name,
                               const std::string& parentId,
                               const std::string& token) {
    json body;
    body["name"]     = name;
    body["mimeType"] = kFolderMime;
    body["parents"]  = json::array({ parentId });

    const std::string url = std::string(kFilesEndpoint)
        + "?supportsAllDrives=true&fields=id";

    HttpResponse rep = callWithRetry(
        [&]() { return m_http->post(url, body.dump(), jsonAuthHeaders(token)); },
        "createFolder(" + name + ")");
    if (rep.ok()) {
        m_cache->invalidateList(parentId);
        return true;
    }
    Logger::instance().error("createFolder HTTP " + std::to_string(rep.statusCode)
                             + " body=" + rep.body);
    return false;
}

bool DriveClient::renameFile(const std::string& fileId,
                             const std::string& newName,
                             const std::string& token) {
    std::vector<std::string> parents;
    DriveFile cachedMeta;
    if (m_cache->getFile(fileId, cachedMeta)) parents = cachedMeta.parents;

    json body;
    body["name"] = newName;

    const std::string url = std::string(kFilesEndpoint) + "/" + StringUtils::urlEncode(fileId)
        + "?supportsAllDrives=true&fields=id,name";

    HttpResponse rep = callWithRetry(
        [&]() { return m_http->patch(url, body.dump(), jsonAuthHeaders(token)); },
        "renameFile(" + fileId + ")");
    if (rep.ok()) {
        m_cache->invalidateFile(fileId);
        if (parents.empty()) {
            m_cache->clear();   
        } else {
            for (const auto& p : parents) m_cache->invalidateList(p);
        }
        return true;
    }
    Logger::instance().error("renameFile HTTP " + std::to_string(rep.statusCode)
                             + " body=" + rep.body);
    return false;
}

bool DriveClient::moveFile(const std::string& fileId,
                           const std::string& oldParentId,
                           const std::string& newParentId,
                           const std::string& token) {
    const std::string url = std::string(kFilesEndpoint) + "/" + StringUtils::urlEncode(fileId)
        + "?addParents="    + StringUtils::urlEncode(newParentId)
        + "&removeParents=" + StringUtils::urlEncode(oldParentId)
        + "&supportsAllDrives=true&fields=id";

    HttpResponse rep = callWithRetry(
        [&]() { return m_http->patch(url, "{}", jsonAuthHeaders(token)); },
        "moveFile(" + fileId + ")");
    if (rep.ok()) {
        m_cache->invalidateFile(fileId);
        m_cache->invalidateList(oldParentId);
        m_cache->invalidateList(newParentId);
        return true;
    }
    Logger::instance().error("moveFile HTTP " + std::to_string(rep.statusCode)
                             + " body=" + rep.body);
    return false;
}

DriveList DriveClient::listSharedDrives(const std::string& token) {
    DriveList out;
    std::string pageToken;
    do {
        std::string url = std::string(kDrivesEndpoint) + "?pageSize=100";
        if (!pageToken.empty()) {
            url += "&pageToken=" + StringUtils::urlEncode(pageToken);
        }

        HttpResponse rep = callWithRetry(
            [&]() { return m_http->get(url, authHeaders(token)); },
            "listSharedDrives");

        if (!rep.ok()) return {};
        try {
            json j = json::parse(rep.body);
            pageToken = j.value("nextPageToken", std::string{});
            if (j.contains("drives") && j["drives"].is_array()) {
                for (auto& d : j["drives"]) out.drives.push_back(parseDriveFolder(d));
            }
        } catch (const std::exception& e) {
            Logger::instance().error(std::string("listSharedDrives parse: ") + e.what());
            return {};
        }
    } while (!pageToken.empty());
    return out;
}

DriveList DriveClient::listSharedWithMe(const std::string& token) {
    DriveList out;
    std::string pageToken;
    do {
        const std::string q = "sharedWithMe=true and trashed=false";
        std::string url = std::string(kFilesEndpoint)
            + "?q="        + StringUtils::urlEncode(q)
            + "&pageSize=1000"
            + "&fields="   + StringUtils::urlEncode(kListFields)
            + "&supportsAllDrives=true&includeItemsFromAllDrives=true";
        if (!pageToken.empty())
            url += "&pageToken=" + StringUtils::urlEncode(pageToken);

        HttpResponse rep = callWithRetry(
            [&]() { return m_http->get(url, authHeaders(token)); },
            "listSharedWithMe");
        if (!rep.ok()) return {};
        try {
            json j = json::parse(rep.body);
            pageToken = j.value("nextPageToken", std::string{});
            if (j.contains("files") && j["files"].is_array()) {
                for (auto& item : j["files"]) out.files.push_back(parseDriveFile(item));
            }
        } catch (const std::exception& e) {
            Logger::instance().error(std::string("listSharedWithMe parse: ") + e.what());
            return {};
        }
    } while (!pageToken.empty());
    return out;
}
