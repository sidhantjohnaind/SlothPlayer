#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <future>
#include <cstdint>

struct HttpResponse {
    int statusCode = 0;
    std::string body;
    std::vector<uint8_t> binaryData;
    bool success = false;
    std::string errorMessage;
};

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    // Synchronous requests
    static HttpResponse get(const std::string& url, const std::map<std::string, std::string>& headers = {});
    static HttpResponse post(const std::string& url, const std::string& postData, const std::string& contentType = "application/json", const std::map<std::string, std::string>& headers = {});

    // Asynchronous requests (runs on background thread, invokes callback on completion)
    static void getAsync(const std::string& url, std::function<void(const HttpResponse&)> callback, const std::map<std::string, std::string>& headers = {});
    static void postAsync(const std::string& url, const std::string& postData, std::function<void(const HttpResponse&)> callback, const std::string& contentType = "application/json", const std::map<std::string, std::string>& headers = {});

    // URL encoder
    static std::string urlEncode(const std::string& value);
};
