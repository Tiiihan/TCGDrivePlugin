#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

#include "drive_models.h"

class IGoogleDriveClient {
public:
    using ProgressCallback = std::function<bool(std::int64_t bytesTransferred,
                                                std::int64_t total)>;

    virtual ~IGoogleDriveClient() = default;

    virtual DriveList listFiles(const std::string& folderId,
                                const std::string& token) = 0;

    virtual DriveFile getFileMetadata(const std::string& fileId,
                                      const std::string& token) = 0;

    virtual bool downloadFile(const std::string& fileId,
                              const std::string& localPath,
                              ProgressCallback   cb,
                              const std::string& token) = 0;

    virtual bool uploadFile(const std::string& localPath,
                            const std::string& parentId,
                            const std::string& targetName,
                            ProgressCallback   cb,
                            const std::string& token) = 0;

    virtual bool deleteFile  (const std::string& fileId,
                              const std::string& token) = 0;
    virtual bool createFolder(const std::string& name,
                              const std::string& parentId,
                              const std::string& token) = 0;
    virtual bool renameFile  (const std::string& fileId,
                              const std::string& newName,
                              const std::string& token) = 0;

    virtual bool moveFile(const std::string& fileId,
                          const std::string& oldParentId,
                          const std::string& newParentId,
                          const std::string& token) { return false; }

    virtual DriveList listSharedDrives(const std::string& token) = 0;

    virtual DriveList listSharedWithMe(const std::string& /*token*/) { return {}; }

    virtual void setCacheTtl(std::chrono::seconds /*ttl*/) {}
    virtual void clearCache() {}

    virtual DriveQuota getQuota(const std::string& /*token*/) { return {}; }

    virtual long lastHttpStatus() const { return 0; }
};
