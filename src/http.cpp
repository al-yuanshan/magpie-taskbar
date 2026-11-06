#include <windows.h>
#include <winhttp.h>
#include <array>
#include <stdexcept>
#include "http.h"

namespace magpie {
namespace {
class InternetHandle {
public:
    explicit InternetHandle(HINTERNET value) : value_(value) {
        if (!value_)
            throw std::runtime_error("WinHTTP 网络错误，错误码：" + std::to_string(GetLastError()));
    }
    ~InternetHandle() { WinHttpCloseHandle(value_); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    operator HINTERNET() const { return value_; }
private:
    HINTERNET value_;
};
void Check(BOOL result) {
    if (!result)
        throw std::runtime_error("WinHTTP 网络错误，错误码：" + std::to_string(GetLastError()));
}
}

std::wstring Wide(const std::string& value)
{
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!size) throw std::runtime_error("响应不是有效的 UTF-8 文本");
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string Utf8(const std::wstring& value)
{
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("配置文本编码无效");
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::string Get(unsigned short port, const std::string& path)
{
    InternetHandle session(WinHttpOpen(L"magpie-taskbar/0.3", WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    Check(WinHttpSetTimeouts(session, 1000, 1000, 1000, 2000));
    InternetHandle connection(WinHttpConnect(session, L"127.0.0.1", port, 0));
    const auto widePath = Wide(path);
    InternetHandle request(WinHttpOpenRequest(connection, L"GET", widePath.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0));
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    Check(WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)));
    Check(WinHttpSendRequest(request, L"Accept: application/json\r\n", static_cast<DWORD>(-1),
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0));
    Check(WinHttpReceiveResponse(request, nullptr));
    DWORD status = 0, statusSize = sizeof(status);
    Check(WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX));
    if (status != 200)
        throw std::runtime_error("HTTP " + std::to_string(status));
    std::array<char, 8192> buffer;
    std::string body;
    const ULONGLONG started = GetTickCount64();
    for (;;) {
        DWORD read = 0;
        Check(WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &read));
        if (!read) break;
        if (body.size() + read > 2 * 1024 * 1024 || GetTickCount64() - started > 5000)
            throw std::runtime_error("Magpie 响应超过大小限制");
        body.append(buffer.data(), read);
    }
    return body;
}
}
