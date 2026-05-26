#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

struct HttpResponse {
    long                                         statusCode = 0;  // HTTP-статус
    std::string                                  body;            // тіло відповіді
    std::unordered_map<std::string, std::string> headers; 
    std::string                                  error;           // транспортна помилка
    bool                                         aborted = false; // скасовано користувачем

    bool ok() const {                                             // 2xx без помилок
        return error.empty() && statusCode >= 200 && statusCode < 300;
    }
};

class HttpClient {
public:
    using ProgressCallback = std::function<bool(std::int64_t, std::int64_t)>;

    HttpClient();
    ~HttpClient();

    HttpClient(const HttpClient&)            = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    HttpResponse request(const std::string&              method,
                         const std::string&              url,
                         const std::vector<std::string>& headers,
                         const std::string&              body);

    HttpResponse get  (const std::string& url, const std::vector<std::string>& headers = {});
    HttpResponse post (const std::string& url, const std::string& body,
                       const std::vector<std::string>& headers = {});
    HttpResponse put  (const std::string& url, const std::string& body,
                       const std::vector<std::string>& headers = {});
    HttpResponse patch(const std::string& url, const std::string& body,
                       const std::vector<std::string>& headers = {});
    HttpResponse del  (const std::string& url, const std::vector<std::string>& headers = {});

    HttpResponse downloadToFile(const std::string&              url,
                                const std::string&              destPath,
                                ProgressCallback                progress,
                                const std::vector<std::string>& headers = {});


    HttpResponse uploadFromFile(const std::string&              method,
                                const std::string&              url,
                                const std::string&              srcPath,
                                std::int64_t                    offset,
                                std::int64_t                    length,
                                ProgressCallback                progress,
                                const std::vector<std::string>& headers = {});

private:
    std::string m_userAgent = "TCGDrivePlugin/1.0";
    int         m_timeoutSeconds = 30;
};
