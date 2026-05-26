#include "http_client.h"

#include "../utils/logger.h"
#include "../utils/string_utils.h"

#include <curl/curl.h>

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <mutex>

namespace {

void initCurlOnce() {
    static std::once_flag flag;
    std::call_once(flag, []() {
        ::curl_global_init(CURL_GLOBAL_DEFAULT);
    });
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s) {
    return StringUtils::trim(s);
}

size_t writeStringCb(void* ptr, size_t size, size_t nmemb, void* userp) {
    auto* out = static_cast<std::string*>(userp);
    out->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

size_t writeFileCb(void* ptr, size_t size, size_t nmemb, void* userp) {
    FILE* fp = static_cast<FILE*>(userp);
    return std::fwrite(ptr, size, nmemb, fp);
}

size_t headerCb(char* buffer, size_t size, size_t nitems, void* userp) {
    auto* hdrs = static_cast<std::unordered_map<std::string, std::string>*>(userp);
    const size_t total = size * nitems;
    std::string line(buffer, total);
    auto colon = line.find(':');
    if (colon != std::string::npos) {
        std::string key   = toLower(trim(line.substr(0, colon)));
        std::string value = trim(line.substr(colon + 1));
        if (!key.empty()) (*hdrs)[key] = std::move(value);
    }
    return total;
}

struct ProgressCtx {
    HttpClient::ProgressCallback cb;
    std::int64_t                 hint = 0; 
};

int xferInfoCb(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
               curl_off_t ultotal, curl_off_t ulnow) {
    auto* ctx = static_cast<ProgressCtx*>(clientp);
    if (!ctx || !ctx->cb) return 0;

    std::int64_t now   = std::max<std::int64_t>(dlnow, ulnow);
    std::int64_t total = std::max<std::int64_t>(dltotal, ultotal);
    if (total == 0 && ctx->hint > 0) total = ctx->hint;
    return ctx->cb(now, total) ? 0 : 1;
}

struct ReadCtx {
    FILE*        fp        = nullptr;
    std::int64_t remaining = 0;
};

size_t readFileCb(char* buffer, size_t size, size_t nitems, void* userp) {
    auto* ctx = static_cast<ReadCtx*>(userp);
    if (!ctx || !ctx->fp || ctx->remaining <= 0) return 0;
    size_t want = size * nitems;
    if (static_cast<std::int64_t>(want) > ctx->remaining)
        want = static_cast<size_t>(ctx->remaining);
    size_t got = std::fread(buffer, 1, want, ctx->fp);
    ctx->remaining -= static_cast<std::int64_t>(got);
    return got;
}

} 

HttpClient::HttpClient()  { initCurlOnce(); }
HttpClient::~HttpClient() = default;

HttpResponse HttpClient::request(const std::string&              method,
                                 const std::string&              url,
                                 const std::vector<std::string>& headers,
                                 const std::string&              body) {
    HttpResponse rep;
    CURL* curl = ::curl_easy_init();
    if (!curl) { rep.error = "curl_easy_init failed"; return rep; }

    curl_slist* hlist = nullptr;
    for (auto& h : headers) hlist = ::curl_slist_append(hlist, h.c_str());

    ::curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    ::curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    ::curl_easy_setopt(curl, CURLOPT_USERAGENT, m_userAgent.c_str());
    ::curl_easy_setopt(curl, CURLOPT_TIMEOUT,        static_cast<long>(m_timeoutSeconds));
    ::curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    ::curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    ::curl_easy_setopt(curl, CURLOPT_NOSIGNAL,       1L);

    if (!body.empty() || method == "POST" || method == "PUT" || method == "PATCH") {
        ::curl_easy_setopt(curl, CURLOPT_POSTFIELDS,    body.data());
        ::curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }
    if (hlist) ::curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hlist);

    ::curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  writeStringCb);
    ::curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &rep.body);
    ::curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCb);
    ::curl_easy_setopt(curl, CURLOPT_HEADERDATA,     &rep.headers);

    CURLcode rc = ::curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        rep.error   = ::curl_easy_strerror(rc);
        rep.aborted = (rc == CURLE_ABORTED_BY_CALLBACK);
        Logger::instance().warn("HttpClient " + method + " " + url + " transport: " + rep.error);
    } else {
        ::curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &rep.statusCode);
    }

    if (hlist) ::curl_slist_free_all(hlist);
    ::curl_easy_cleanup(curl);
    return rep;
}

HttpResponse HttpClient::get  (const std::string& url, const std::vector<std::string>& h) { return request("GET",    url, h, {}); }
HttpResponse HttpClient::post (const std::string& url, const std::string& body, const std::vector<std::string>& h) { return request("POST",  url, h, body); }
HttpResponse HttpClient::put  (const std::string& url, const std::string& body, const std::vector<std::string>& h) { return request("PUT",   url, h, body); }
HttpResponse HttpClient::patch(const std::string& url, const std::string& body, const std::vector<std::string>& h) { return request("PATCH", url, h, body); }
HttpResponse HttpClient::del  (const std::string& url, const std::vector<std::string>& h) { return request("DELETE", url, h, {}); }

HttpResponse HttpClient::downloadToFile(const std::string&              url,
                                       const std::string&              destPath,
                                       ProgressCallback                progress,
                                       const std::vector<std::string>& headers) {
    HttpResponse rep;
    FILE* fp = nullptr;

    if (::_wfopen_s(&fp, StringUtils::fromUtf8(destPath).c_str(), L"wb") != 0 || !fp) {
        rep.error = "cannot open destination file: " + destPath;
        return rep;
    }

    CURL* curl = ::curl_easy_init();
    if (!curl) { rep.error = "curl_easy_init failed"; std::fclose(fp); return rep; }

    curl_slist* hlist = nullptr;
    for (auto& h : headers) hlist = ::curl_slist_append(hlist, h.c_str());

    ProgressCtx pctx{ std::move(progress), 0 };

    ::curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    ::curl_easy_setopt(curl, CURLOPT_USERAGENT, m_userAgent.c_str());
    ::curl_easy_setopt(curl, CURLOPT_HTTPGET,        1L);
    ::curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    ::curl_easy_setopt(curl, CURLOPT_TIMEOUT,        0L); 
    ::curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    ::curl_easy_setopt(curl, CURLOPT_NOSIGNAL,       1L);

    ::curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    ::curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME,  30L);
    if (hlist) ::curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hlist);

    ::curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  writeFileCb);
    ::curl_easy_setopt(curl, CURLOPT_WRITEDATA,      fp);
    ::curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCb);
    ::curl_easy_setopt(curl, CURLOPT_HEADERDATA,     &rep.headers);

    if (pctx.cb) {
        ::curl_easy_setopt(curl, CURLOPT_NOPROGRESS,       0L);
        ::curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferInfoCb);
        ::curl_easy_setopt(curl, CURLOPT_XFERINFODATA,     &pctx);
    }

    CURLcode rc = ::curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        rep.error   = ::curl_easy_strerror(rc);
        rep.aborted = (rc == CURLE_ABORTED_BY_CALLBACK);
    } else {
        ::curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &rep.statusCode);
    }
    if (hlist) ::curl_slist_free_all(hlist);
    ::curl_easy_cleanup(curl);
    std::fclose(fp);

    if (!rep.ok()) {
        ::_wremove(StringUtils::fromUtf8(destPath).c_str());
    }
    return rep;
}

HttpResponse HttpClient::uploadFromFile(const std::string&              method,
                                       const std::string&              url,
                                       const std::string&              srcPath,
                                       std::int64_t                    offset,
                                       std::int64_t                    length,
                                       ProgressCallback                progress,
                                       const std::vector<std::string>& headers) {
    HttpResponse rep;
    FILE* fp = nullptr;
    if (::_wfopen_s(&fp, StringUtils::fromUtf8(srcPath).c_str(), L"rb") != 0 || !fp) {
        rep.error = "cannot open source file: " + srcPath;
        return rep;
    }
    if (offset > 0) ::_fseeki64(fp, offset, SEEK_SET);

    if (length < 0) {
        ::_fseeki64(fp, 0, SEEK_END);
        std::int64_t end = ::_ftelli64(fp);
        length = end - offset;
        ::_fseeki64(fp, offset, SEEK_SET);
    }

    CURL* curl = ::curl_easy_init();
    if (!curl) { rep.error = "curl_easy_init failed"; std::fclose(fp); return rep; }

    curl_slist* hlist = nullptr;
    for (auto& h : headers) hlist = ::curl_slist_append(hlist, h.c_str());

    ReadCtx     rctx{ fp, length };
    ProgressCtx pctx{ std::move(progress), length };

    ::curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    ::curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    ::curl_easy_setopt(curl, CURLOPT_USERAGENT, m_userAgent.c_str());
    ::curl_easy_setopt(curl, CURLOPT_UPLOAD,         1L);
    ::curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE,
                       static_cast<curl_off_t>(length));
    ::curl_easy_setopt(curl, CURLOPT_READFUNCTION, readFileCb);
    ::curl_easy_setopt(curl, CURLOPT_READDATA,     &rctx);
    ::curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    ::curl_easy_setopt(curl, CURLOPT_TIMEOUT,        0L);
    ::curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    ::curl_easy_setopt(curl, CURLOPT_NOSIGNAL,       1L);
    ::curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    ::curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME,  30L);
    if (hlist) ::curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hlist);

    ::curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  writeStringCb);
    ::curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &rep.body);
    ::curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCb);
    ::curl_easy_setopt(curl, CURLOPT_HEADERDATA,     &rep.headers);

    if (pctx.cb) {
        ::curl_easy_setopt(curl, CURLOPT_NOPROGRESS,       0L);
        ::curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferInfoCb);
        ::curl_easy_setopt(curl, CURLOPT_XFERINFODATA,     &pctx);
    }

    CURLcode rc = ::curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        rep.error   = ::curl_easy_strerror(rc);
        rep.aborted = (rc == CURLE_ABORTED_BY_CALLBACK);
    } else {
        ::curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &rep.statusCode);
    }

    if (hlist) ::curl_slist_free_all(hlist);
    ::curl_easy_cleanup(curl);
    std::fclose(fp);
    return rep;
}
