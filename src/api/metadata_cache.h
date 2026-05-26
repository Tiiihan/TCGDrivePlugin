#pragma once

#include <chrono>
#include <cstddef>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

#include "drive_models.h"

class MetadataCache {
public:
    static constexpr std::size_t          kDefaultCapacity = 200;
    static constexpr std::chrono::seconds kDefaultTtl{60};

    explicit MetadataCache(std::size_t          capacity = kDefaultCapacity,
                           std::chrono::seconds ttl      = kDefaultTtl);
    ~MetadataCache();

    void setTtl(std::chrono::seconds ttl);

    bool getFile(const std::string& fileId, DriveFile& out);
    void putFile(const std::string& fileId, DriveFile  file);
    void invalidateFile(const std::string& fileId);

    bool getList(const std::string& folderId, DriveList& out);
    void putList(const std::string& folderId, DriveList  list);
    void invalidateList(const std::string& folderId);

    void clear();

private:
    template <typename Value>
    struct Bucket {
        struct Entry {
            Value                                   value;
            std::chrono::steady_clock::time_point   insertedAt;
            typename std::list<std::string>::iterator lruIt;
        };

        std::list<std::string>                      lru;     // front = MRU
        std::unordered_map<std::string, Entry>      map;
        mutable std::mutex                          mu;
    };

    template <typename Value>
    bool getImpl(Bucket<Value>& b, const std::string& key, Value& out);

    template <typename Value>
    void putImpl(Bucket<Value>& b, const std::string& key, Value v);

    template <typename Value>
    void invalidateImpl(Bucket<Value>& b, const std::string& key);

    std::size_t          m_capacity;
    std::chrono::seconds m_ttl;

    Bucket<DriveFile>  m_files;
    Bucket<DriveList>  m_lists;
};
