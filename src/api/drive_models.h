#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

struct DriveFile {
    std::string                           id;               // ідентифікатор файлу
    std::string                           name;             // назва
    std::string                           mimeType;         // MIME-тип
    std::vector<std::string>              parents;          // батьківські папки
    std::int64_t                          size = 0;         // розмір (0 для папок)
    std::chrono::system_clock::time_point modifiedTime{};
    std::chrono::system_clock::time_point createdTime{};
    bool                                  trashed     = false;
    bool                                  isFolder    = false;  // ознака папки
    bool                                  shared      = false;  // спільні файли
    std::string                           md5Checksum;          // контрольна сума
    std::string                           webViewLink;
};

struct DriveFolder {
    std::string                           id;
    std::string                           name;
    std::chrono::system_clock::time_point createdTime{};
};

struct DriveQuota {
    std::int64_t limitBytes = 0;
    std::int64_t usageBytes = 0;
};

struct DriveList {
    std::vector<DriveFile>   files;
    std::vector<DriveFolder> drives;
    std::string              nextPageToken;
};
