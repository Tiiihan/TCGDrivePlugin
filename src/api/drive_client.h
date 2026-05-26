#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>

#include "http_client.h"
#include "i_drive_client.h"
#include "metadata_cache.h"

// Concrete Google Drive REST API v3 client.
//
// Endpoint map (all over HTTPS):
//   GET    https://www.googleapis.com/drive/v3/files?q=...&pageSize=...&pageToken=...&fields=...
//   GET    https://www.googleapis.com/drive/v3/files/{fileId}?fields=...
//   GET    https://www.googleapis.com/drive/v3/files/{fileId}?alt=media
//   POST   https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart
//   POST   https://www.googleapis.com/upload/drive/v3/files?uploadType=resumable   (session init)
//   PUT    <session URI from the init response above>                              (chunk upload)
//   POST   https://www.googleapis.com/drive/v3/files                                (createFolder)
//   PATCH  https://www.googleapis.com/drive/v3/files/{fileId}                       (rename)
//   DELETE https://www.googleapis.com/drive/v3/files/{fileId}
//   GET    https://www.googleapis.com/drive/v3/drives?pageSize=...&pageToken=...

class DriveClient : public IGoogleDriveClient {
public:
    DriveClient();
    DriveClient(std::shared_ptr<HttpClient>     http,
                std::shared_ptr<MetadataCache>  cache);
    ~DriveClient() override;

    DriveList listFiles      (const std::string& folderId, const std::string& token) override;
    DriveFile getFileMetadata(const std::string& fileId,   const std::string& token) override;
    bool      downloadFile   (const std::string& fileId,
                              const std::string& localPath,
                              ProgressCallback   cb,
                              const std::string& token) override;
    bool      uploadFile     (const std::string& localPath,
                              const std::string& parentId,
                              const std::string& targetName,
                              ProgressCallback   cb,
                              const std::string& token) override;
    bool      deleteFile     (const std::string& fileId,   const std::string& token) override;
    bool      createFolder   (const std::string& name,
                              const std::string& parentId,
                              const std::string& token) override;
    bool      renameFile     (const std::string& fileId,
                              const std::string& newName,
                              const std::string& token) override;
    bool      moveFile       (const std::string& fileId,
                              const std::string& oldParentId,
                              const std::string& newParentId,
                              const std::string& token) override;
    DriveList listSharedDrives (const std::string& token) override;
    DriveList listSharedWithMe (const std::string& token) override;

    void       setCacheTtl(std::chrono::seconds ttl) override;
    void       clearCache() override;
    DriveQuota getQuota(const std::string& token) override;
    long       lastHttpStatus() const override { return m_lastHttpStatus.load(); }

    static constexpr int kInteractiveRetries    = 2;
    static constexpr int kInteractiveMaxBackoff = 4;   // seconds
    static constexpr int kBackgroundRetries     = 5;
    static constexpr int kBackgroundMaxBackoff  = 16;  // seconds

private:
    HttpResponse callWithRetry(
        const std::function<HttpResponse()>& doRequest,
        const std::string&                   diagLabel,
        int                                  maxRetries    = kInteractiveRetries,
        int                                  maxBackoffSec = kInteractiveMaxBackoff,
        // Optional poll for cooperative cancellation. When supplied, it is
        // checked before each attempt and during the back-off wait, so an
        // offline/stalled retry can be aborted promptly instead of blocking
        // for the whole interval. Returns true to abort.
        const std::function<bool()>&         shouldCancel  = {});

    bool uploadResumable(const std::string& localPath,
                         const std::string& fileName,
                         const std::string& parentId,
                         std::int64_t       size,
                         ProgressCallback   cb,
                         const std::string& token);

    static std::vector<std::string> authHeaders(const std::string& token);
    static std::vector<std::string> jsonAuthHeaders(const std::string& token);

    std::shared_ptr<HttpClient>    m_http;
    std::shared_ptr<MetadataCache> m_cache;

    std::atomic<long>              m_lastHttpStatus{0};

    std::mutex                              m_quotaMutex;
    DriveQuota                              m_cachedQuota;
    std::chrono::steady_clock::time_point   m_quotaCachedAt{};
    static constexpr std::chrono::seconds   kQuotaTtl{30};
};
