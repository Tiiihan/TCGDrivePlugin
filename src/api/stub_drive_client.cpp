#include "stub_drive_client.h"

#include "../utils/logger.h"

#include <chrono>

namespace {
constexpr const char* kFolderMime = "application/vnd.google-apps.folder";
}

StubDriveClient::StubDriveClient() {
    Logger::instance().info("StubDriveClient created");
}

StubDriveClient::~StubDriveClient() = default;

DriveList StubDriveClient::listFiles(const std::string& folderId,
                                     const std::string& /*token*/) {
    Logger::instance().debug("Stub::listFiles folderId=" + folderId);
    DriveList out;
    if (folderId == "root" || folderId.empty()) {
        DriveFile f;
        f.id           = "stub-placeholder";
        f.name         = "Sign in to populate";
        f.mimeType     = kFolderMime;
        f.isFolder     = true;
        f.parents      = { "root" };
        f.modifiedTime = std::chrono::system_clock::now();
        f.createdTime  = f.modifiedTime;
        out.files.push_back(std::move(f));
    }
    return out;
}

DriveFile StubDriveClient::getFileMetadata(const std::string& fileId,
                                           const std::string& /*token*/) {
    Logger::instance().debug("Stub::getFileMetadata id=" + fileId);
    return {};
}

bool StubDriveClient::downloadFile(const std::string& fileId,
                                   const std::string& localPath,
                                   ProgressCallback   /*cb*/,
                                   const std::string& /*token*/) {
    Logger::instance().info("Stub::downloadFile " + fileId
                            + " -> " + localPath + " (not implemented)");
    return false;
}

bool StubDriveClient::uploadFile(const std::string& localPath,
                                 const std::string& parentId,
                                 const std::string& /*targetName*/,
                                 ProgressCallback   /*cb*/,
                                 const std::string& /*token*/) {
    Logger::instance().info("Stub::uploadFile " + localPath
                            + " -> parent=" + parentId + " (not implemented)");
    return false;
}

bool StubDriveClient::deleteFile(const std::string& fileId,
                                 const std::string& /*token*/) {
    Logger::instance().info("Stub::deleteFile " + fileId + " (not implemented)");
    return false;
}

bool StubDriveClient::createFolder(const std::string& name,
                                   const std::string& parentId,
                                   const std::string& /*token*/) {
    Logger::instance().info("Stub::createFolder " + name
                            + " in " + parentId + " (not implemented)");
    return false;
}

bool StubDriveClient::renameFile(const std::string& fileId,
                                 const std::string& newName,
                                 const std::string& /*token*/) {
    Logger::instance().info("Stub::renameFile " + fileId
                            + " -> " + newName + " (not implemented)");
    return false;
}

DriveList StubDriveClient::listSharedDrives(const std::string& /*token*/) {
    Logger::instance().debug("Stub::listSharedDrives");
    return {};
}
