#pragma once

#include "i_drive_client.h"

class StubDriveClient : public IGoogleDriveClient {
public:
    StubDriveClient();
    ~StubDriveClient() override;

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
    DriveList listSharedDrives(const std::string& token) override;
};
