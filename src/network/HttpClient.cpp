#include "HttpClient.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#endif

HttpClient::HttpClient() = default;
HttpClient::~HttpClient() = default;

std::string HttpClient::urlEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else if (c == ' ') {
            escaped << "%20";
        } else {
            escaped << '%' << std::setw(2) << static_cast<int>(c);
        }
    }

    return escaped.str();
}

#ifdef _WIN32
static HttpResponse executeWinHttp(
    const std::string& url,
    bool isPost,
    const std::string& postData,
    const std::string& contentType,
    const std::map<std::string, std::string>& headers
) {
    HttpResponse resp;

    // Convert URL to wide string
    int wUrlLen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, NULL, 0);
    if (wUrlLen <= 0) {
        resp.errorMessage = "Invalid URL encoding";
        return resp;
    }
    std::wstring wUrl(wUrlLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, &wUrl[0], wUrlLen);

    // Crack URL
    URL_COMPONENTS urlComp;
    std::memset(&urlComp, 0, sizeof(urlComp));
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = static_cast<DWORD>(-1);
    urlComp.dwHostNameLength = static_cast<DWORD>(-1);
    urlComp.dwUrlPathLength = static_cast<DWORD>(-1);
    urlComp.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(wUrl.c_str(), static_cast<DWORD>(wUrl.length()), 0, &urlComp)) {
        resp.errorMessage = "Failed to parse URL";
        return resp;
    }

    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength + urlComp.dwExtraInfoLength);
    INTERNET_PORT port = urlComp.nPort;
    bool isHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);

    HINTERNET hSession = WinHttpOpen(
        L"SlothPlayer/2.0 (Windows)",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!hSession) {
        resp.errorMessage = "Failed to initialize WinHTTP session";
        return resp;
    }

    WinHttpSetTimeouts(hSession, 5000, 5000, 10000, 10000);

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        resp.errorMessage = "Failed to connect to host";
        return resp;
    }

    DWORD flags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        isPost ? L"POST" : L"GET",
        path.empty() ? L"/" : path.c_str(),
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags
    );
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        resp.errorMessage = "Failed to open HTTP request";
        return resp;
    }

    // Set auto-redirect
    DWORD redirectOption = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectOption, sizeof(redirectOption));

    // Headers
    std::wstring headerStr;
    if (isPost && !contentType.empty()) {
        headerStr += L"Content-Type: ";
        int wLen = MultiByteToWideChar(CP_UTF8, 0, contentType.c_str(), -1, NULL, 0);
        std::wstring wct(wLen, 0);
        MultiByteToWideChar(CP_UTF8, 0, contentType.c_str(), -1, &wct[0], wLen);
        headerStr += wct;
        headerStr += L"\r\n";
    }

    for (const auto& [k, v] : headers) {
        int wkLen = MultiByteToWideChar(CP_UTF8, 0, k.c_str(), -1, NULL, 0);
        std::wstring wk(wkLen, 0);
        MultiByteToWideChar(CP_UTF8, 0, k.c_str(), -1, &wk[0], wkLen);

        int wvLen = MultiByteToWideChar(CP_UTF8, 0, v.c_str(), -1, NULL, 0);
        std::wstring wv(wvLen, 0);
        MultiByteToWideChar(CP_UTF8, 0, v.c_str(), -1, &wv[0], wvLen);

        headerStr += wk + L": " + wv + L"\r\n";
    }

    if (!headerStr.empty()) {
        WinHttpAddRequestHeaders(hRequest, headerStr.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    // Send Request
    LPVOID optionalData = isPost && !postData.empty() ? const_cast<char*>(postData.data()) : WINHTTP_NO_REQUEST_DATA;
    DWORD optionalLen = isPost ? static_cast<DWORD>(postData.size()) : 0;

    BOOL sendOk = WinHttpSendRequest(
        hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        optionalData,
        optionalLen,
        optionalLen,
        0
    );

    if (!sendOk || !WinHttpReceiveResponse(hRequest, NULL)) {
        resp.errorMessage = "HTTP request transmission or response receive failed";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    // Read status code
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (WinHttpQueryHeaders(
        hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusSize,
        WINHTTP_NO_HEADER_INDEX
    )) {
        resp.statusCode = static_cast<int>(statusCode);
    }

    // Read Response Stream
    DWORD dwSize = 0;
    std::vector<uint8_t> buffer;
    while (WinHttpQueryDataAvailable(hRequest, &dwSize) && dwSize > 0) {
        std::vector<uint8_t> chunk(dwSize);
        DWORD dwDownloaded = 0;
        if (WinHttpReadData(hRequest, chunk.data(), dwSize, &dwDownloaded) && dwDownloaded > 0) {
            buffer.insert(buffer.end(), chunk.begin(), chunk.begin() + dwDownloaded);
        } else {
            break;
        }
    }

    resp.binaryData = std::move(buffer);
    if (!resp.binaryData.empty()) {
        resp.body = std::string(reinterpret_cast<char*>(resp.binaryData.data()), resp.binaryData.size());
    }

    resp.success = (resp.statusCode >= 200 && resp.statusCode < 300);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return resp;
}
#endif

HttpResponse HttpClient::get(const std::string& url, const std::map<std::string, std::string>& headers) {
#ifdef _WIN32
    return executeWinHttp(url, false, "", "", headers);
#else
    HttpResponse r;
    r.errorMessage = "Not supported on non-Windows platform";
    return r;
#endif
}

HttpResponse HttpClient::post(
    const std::string& url,
    const std::string& postData,
    const std::string& contentType,
    const std::map<std::string, std::string>& headers
) {
#ifdef _WIN32
    return executeWinHttp(url, true, postData, contentType, headers);
#else
    HttpResponse r;
    r.errorMessage = "Not supported on non-Windows platform";
    return r;
#endif
}

void HttpClient::getAsync(
    const std::string& url,
    std::function<void(const HttpResponse&)> callback,
    const std::map<std::string, std::string>& headers
) {
    std::thread([url, callback, headers]() {
        HttpResponse res = get(url, headers);
        if (callback) {
            callback(res);
        }
    }).detach();
}

void HttpClient::postAsync(
    const std::string& url,
    const std::string& postData,
    std::function<void(const HttpResponse&)> callback,
    const std::string& contentType,
    const std::map<std::string, std::string>& headers
) {
    std::thread([url, postData, callback, contentType, headers]() {
        HttpResponse res = post(url, postData, contentType, headers);
        if (callback) {
            callback(res);
        }
    }).detach();
}
