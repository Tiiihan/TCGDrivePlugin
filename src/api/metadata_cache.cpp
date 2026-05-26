#include "metadata_cache.h"

MetadataCache::MetadataCache(std::size_t capacity, std::chrono::seconds ttl)
    : m_capacity(capacity), m_ttl(ttl) {}

MetadataCache::~MetadataCache() = default;

void MetadataCache::setTtl(std::chrono::seconds ttl) { m_ttl = ttl; }

template <typename Value>
bool MetadataCache::getImpl(Bucket<Value>& b, const std::string& key, Value& out) {
    std::lock_guard<std::mutex> lock(b.mu);
    auto it = b.map.find(key);
    if (it == b.map.end()) return false;

    if (std::chrono::steady_clock::now() - it->second.insertedAt > m_ttl) {
        b.lru.erase(it->second.lruIt);
        b.map.erase(it);
        return false;
    }

    b.lru.erase(it->second.lruIt);
    b.lru.push_front(key);
    it->second.lruIt = b.lru.begin();

    out = it->second.value;
    return true;
}

template <typename Value>
void MetadataCache::putImpl(Bucket<Value>& b, const std::string& key, Value v) {
    std::lock_guard<std::mutex> lock(b.mu);
    auto it = b.map.find(key);
    if (it != b.map.end()) {
        b.lru.erase(it->second.lruIt);
        b.lru.push_front(key);
        it->second.value      = std::move(v);
        it->second.insertedAt = std::chrono::steady_clock::now();
        it->second.lruIt      = b.lru.begin();
        return;
    }

    b.lru.push_front(key);
    auto& e   = b.map[key];
    e.value      = std::move(v);
    e.insertedAt = std::chrono::steady_clock::now();
    e.lruIt      = b.lru.begin();

    while (b.map.size() > m_capacity && !b.lru.empty()) {
        const std::string& victim = b.lru.back();
        b.map.erase(victim);
        b.lru.pop_back();
    }
}

template <typename Value>
void MetadataCache::invalidateImpl(Bucket<Value>& b, const std::string& key) {
    std::lock_guard<std::mutex> lock(b.mu);
    auto it = b.map.find(key);
    if (it == b.map.end()) return;
    b.lru.erase(it->second.lruIt);
    b.map.erase(it);
}

bool MetadataCache::getFile(const std::string& fileId, DriveFile& out) {
    return getImpl(m_files, fileId, out);
}

void MetadataCache::putFile(const std::string& fileId, DriveFile file) {
    putImpl(m_files, fileId, std::move(file));
}

void MetadataCache::invalidateFile(const std::string& fileId) {
    invalidateImpl(m_files, fileId);
}

bool MetadataCache::getList(const std::string& folderId, DriveList& out) {
    return getImpl(m_lists, folderId, out);
}

void MetadataCache::putList(const std::string& folderId, DriveList list) {
    putImpl(m_lists, folderId, std::move(list));
}

void MetadataCache::invalidateList(const std::string& folderId) {
    invalidateImpl(m_lists, folderId);
}

void MetadataCache::clear() {
    {
        std::lock_guard<std::mutex> lock(m_files.mu);
        m_files.map.clear();
        m_files.lru.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_lists.mu);
        m_lists.map.clear();
        m_lists.lru.clear();
    }
}
