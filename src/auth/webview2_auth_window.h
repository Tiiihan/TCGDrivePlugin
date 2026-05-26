#pragma once
#include <atomic>
#include <string>

struct WebView2Result {
    bool        success  = false;   
    bool        timedOut = false;  
    std::string code;              
    std::string state;              
    std::string error;             
};

class WebView2AuthWindow {
public:
    static WebView2Result show(const std::string&       authUrl,
                               const std::string&       redirectUriPrefix,
                               int                      timeoutSeconds = 120,
                               const std::atomic<bool>* cancelFlag     = nullptr);

    static std::wstring userDataPath();

private:
    WebView2AuthWindow() = delete;
};
