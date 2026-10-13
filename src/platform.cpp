#include "ghostwave/core.hpp"
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace gw {
namespace {
void wincheck(bool ok, const char* operation) {
    if (!ok) throw std::runtime_error(std::string(operation) + " failed with Windows error " + std::to_string(GetLastError()));
}
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h = INVALID_HANDLE_VALUE) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
};
struct Internet {
    HINTERNET value;
    explicit Internet(HINTERNET h) : value(h) { wincheck(h != nullptr, "Network operation"); }
    ~Internet() { WinHttpCloseHandle(value); }
};
std::wstring quote(const std::wstring& arg) {
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (auto c : arg) {
        if (c == L'\\') { ++backslashes; continue; }
        out.append(backslashes * (c == L'"' ? 2 : 1), L'\\');
        if (c == L'"') out += L'\\';
        out += c;
        backslashes = 0;
    }
    out.append(backslashes * 2, L'\\');
    return out + L'"';
}
std::wstring header(HINTERNET request, DWORD field) {
    DWORD length = 0;
    WinHttpQueryHeaders(request, field, nullptr, nullptr, &length, nullptr);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return {};
    std::wstring result(length / sizeof(wchar_t), L'\0');
    wincheck(WinHttpQueryHeaders(request, field, nullptr, result.data(), &length, nullptr), "Read response header");
    result.resize(length / sizeof(wchar_t));
    while (!result.empty() && result.back() == L'\0') result.pop_back();
    return result;
}
}
std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    wincheck(n > 0, "Decode UTF8");
    std::wstring result(n, L'\0');
    wincheck(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), n) == n, "Decode UTF8");
    return result;
}
std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    wincheck(n > 0, "Encode UTF8");
    std::string result(n, '\0');
    wincheck(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), n, nullptr, nullptr) == n, "Encode UTF8");
    return result;
}
fs::path app_dir() {
    std::wstring path(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    wincheck(n > 0 && n < path.size(), "Find application folder");
    path.resize(n);
    return fs::path(path).parent_path();
}
fs::path user_dir() {
    std::wstring value(32768, L'\0');
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", value.data(), static_cast<DWORD>(value.size()));
    wincheck(n > 0 && n < value.size(), "Find user folder");
    value.resize(n);
    return fs::path(value);
}
Bytes read_bytes(const fs::path& path, size_t limit) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot read file: " + path.u8string());
    const auto size = file.tellg();
    if (size < 0 || static_cast<uint64_t>(size) > limit) throw std::runtime_error("File exceeds allowed size");
    Bytes bytes(static_cast<size_t>(size));
    file.seekg(0);
    if (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) throw std::runtime_error("Incomplete file read");
    return bytes;
}
void write_atomic(const fs::path& path, const Bytes& bytes) {
    auto absolute = fs::absolute(path);
    fs::create_directories(absolute.parent_path());
    TempFile temporary(".tmp", absolute.parent_path());
    {
        Handle file(CreateFileW(temporary.path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        wincheck(file.value != INVALID_HANDLE_VALUE, "Create temporary file");
        size_t offset = 0;
        while (offset < bytes.size()) {
            DWORD written = 0;
            DWORD count = static_cast<DWORD>(std::min<size_t>(bytes.size() - offset, 1024 * 1024));
            wincheck(WriteFile(file.value, bytes.data() + offset, count, &written, nullptr) && written > 0, "Write temporary file");
            offset += written;
        }
        wincheck(FlushFileBuffers(file.value), "Flush temporary file");
    }
    wincheck(MoveFileExW(temporary.path.c_str(), absolute.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH), "Replace destination");
}
Json read_json(const fs::path& path) {
    auto bytes = read_bytes(path, 16 * 1024 * 1024);
    return Json::parse(bytes.begin(), bytes.end());
}
void cancelled(const std::atomic_bool* cancel) {
    if (cancel && cancel->load()) throw std::runtime_error("Cancelled");
}
std::string random_hex(size_t count) {
    Bytes random(count);
    if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(count), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) throw std::runtime_error("Secure random generation failed");
    std::string out;
    for (auto b : random) { out += "0123456789abcdef"[b >> 4]; out += "0123456789abcdef"[b & 15]; }
    return out;
}
std::string base64(const Bytes& bytes, bool url) {
    DWORD n = 0;
    const DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
    wincheck(CryptBinaryToStringW(bytes.data(), static_cast<DWORD>(bytes.size()), flags, nullptr, &n), "Encode base64");
    std::wstring text(n, L'\0');
    wincheck(CryptBinaryToStringW(bytes.data(), static_cast<DWORD>(bytes.size()), flags, text.data(), &n), "Encode base64");
    while (!text.empty() && text.back() == L'\0') text.pop_back();
    auto result = utf8(text);
    if (url) { std::replace(result.begin(), result.end(), '+', '-'); std::replace(result.begin(), result.end(), '/', '_'); }
    return result;
}
Bytes unbase64(std::string text, bool url) {
    if (url) { std::replace(text.begin(), text.end(), '-', '+'); std::replace(text.begin(), text.end(), '_', '/'); }
    auto input = wide(text);
    DWORD size = 0;
    wincheck(CryptStringToBinaryW(input.c_str(), static_cast<DWORD>(input.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, nullptr, &size, nullptr, nullptr), "Decode base64");
    Bytes result(size);
    wincheck(CryptStringToBinaryW(input.c_str(), static_cast<DWORD>(input.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, result.data(), &size, nullptr, nullptr), "Decode base64");
    result.resize(size);
    return result;
}
Bytes sha256(const Bytes& bytes) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) throw std::runtime_error("Hash initialization failed");
    Bytes result(32);
    NTSTATUS status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    if (status >= 0) status = BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), 0);
    if (status >= 0) status = BCryptFinishHash(hash, result.data(), 32, 0);
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) throw std::runtime_error("Hash computation failed");
    return result;
}
TempFile::TempFile(const std::string& extension, fs::path directory) : path(directory / fs::u8path("ghostwave_" + random_hex(12) + extension)) {}
TempFile::~TempFile() { std::error_code error; fs::remove(path, error); }
fs::path ffmpeg_path() {
    auto bundled = app_dir() / "ffmpeg.exe";
    if (fs::is_regular_file(bundled)) return bundled;
    throw std::runtime_error("Bundled FFmpeg is missing");
}
ProcessResult run_process(const std::vector<std::wstring>& args, int timeout_seconds, const std::atomic_bool* cancel) {
    if (args.empty()) throw std::runtime_error("Empty process command");
    static std::mutex spawn_mutex;
    std::lock_guard<std::mutex> lock(spawn_mutex);
    cancelled(cancel);
    TempFile stdout_file(".out"), stderr_file(".err");
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle output(CreateFileW(stdout_file.path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr));
    Handle error(CreateFileW(stderr_file.path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr));
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr));
    wincheck(output.value != INVALID_HANDLE_VALUE && error.value != INVALID_HANDLE_VALUE && input.value != INVALID_HANDLE_VALUE, "Prepare media process");
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input.value; startup.hStdOutput = output.value; startup.hStdError = error.value;
    PROCESS_INFORMATION process{};
    std::wstring command;
    for (const auto& arg : args) { if (!command.empty()) command += L' '; command += quote(arg); }
    wincheck(CreateProcessW(args[0].c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process), "Start media process");
    Handle process_handle(process.hProcess), thread(process.hThread);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
    bool interrupted = false;
    while (WaitForSingleObject(process_handle.value, 50) == WAIT_TIMEOUT) {
        if ((cancel && cancel->load()) || std::chrono::steady_clock::now() >= deadline) {
            TerminateProcess(process_handle.value, 1);
            WaitForSingleObject(process_handle.value, INFINITE);
            interrupted = true;
            break;
        }
    }
    DWORD code = 1;
    wincheck(GetExitCodeProcess(process_handle.value, &code), "Read process status");
    CloseHandle(output.value); output.value = INVALID_HANDLE_VALUE;
    CloseHandle(error.value); error.value = INVALID_HANDLE_VALUE;
    if (interrupted) { cancelled(cancel); throw std::runtime_error("Media process timed out"); }
    auto errors = read_bytes(stderr_file.path, 16 * 1024 * 1024);
    return {static_cast<int>(code), read_bytes(stdout_file.path), std::string(errors.begin(), errors.end())};
}
HttpResponse http(const HttpRequest& original, const std::atomic_bool* cancel) {
    auto request = original;
    std::wstring original_host;
    for (int redirects = 0; redirects < 6; ++redirects) {
        cancelled(cancel);
        auto url = wide(request.url);
        URL_COMPONENTS parts{}; parts.dwStructSize = sizeof(parts);
        parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        wincheck(WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), ICU_REJECT_USERPWD, &parts), "Parse endpoint");
        if (parts.nScheme != INTERNET_SCHEME_HTTPS || !parts.dwHostNameLength) throw std::runtime_error("Endpoint must use HTTPS");
        std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        if (original_host.empty()) original_host = host;
        std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
        if (path.empty()) path = L"/";
        if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        Internet session(WinHttpOpen(L"GhostWaveStudio/2.0.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        int timeout = std::clamp(request.timeout_seconds, 1, 600) * 1000;
        wincheck(WinHttpSetTimeouts(session.value, timeout, timeout, timeout, timeout), "Set network timeout");
        Internet connection(WinHttpConnect(session.value, host.c_str(), parts.nPort, 0));
        Internet operation(WinHttpOpenRequest(connection.value, wide(request.method).c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        wincheck(WinHttpSetOption(operation.value, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy)), "Set redirect policy");
        std::wstring headers;
        for (const auto& field : request.headers) {
            if (field.first.find_first_of("\r\n:") != std::string::npos || field.second.find_first_of("\r\n") != std::string::npos) throw std::runtime_error("Invalid request header");
            if (_stricmp(field.first.c_str(), "Authorization") == 0 && _wcsicmp(host.c_str(), original_host.c_str()) != 0) continue;
            headers += wide(field.first + ": " + field.second + "\r\n");
        }
        if (request.body.size() > 512 * 1024 * 1024) throw std::runtime_error("Upload exceeds allowed size");
        wincheck(WinHttpSendRequest(operation.value, headers.c_str(), static_cast<DWORD>(headers.size()), request.body.empty() ? WINHTTP_NO_REQUEST_DATA : request.body.data(), static_cast<DWORD>(request.body.size()), static_cast<DWORD>(request.body.size()), 0), "Send request");
        cancelled(cancel);
        wincheck(WinHttpReceiveResponse(operation.value, nullptr), "Receive response");
        HttpResponse response;
        DWORD n = sizeof(response.status);
        wincheck(WinHttpQueryHeaders(operation.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &response.status, &n, nullptr), "Read HTTP status");
        if (response.status == 301 || response.status == 302 || response.status == 303 || response.status == 307 || response.status == 308) {
            auto location = utf8(header(operation.value, WINHTTP_QUERY_LOCATION));
            if (location.empty()) throw std::runtime_error("Missing redirect address");
            if (location.front() == '/') location = "https://" + utf8(host) + location;
            request.url = location;
            if (response.status == 303 || ((response.status == 301 || response.status == 302) && request.method == "POST")) { request.method = "GET"; request.body.clear(); }
            continue;
        }
        response.content_type = utf8(header(operation.value, WINHTTP_QUERY_CONTENT_TYPE));
        uint64_t expected_size = 0;
        auto length_header = header(operation.value, WINHTTP_QUERY_CONTENT_LENGTH);
        if (!length_header.empty()) expected_size = std::stoull(length_header);
        if (expected_size > 512 * 1024 * 1024) throw std::runtime_error("Response exceeds allowed size");
        unsigned char buffer[65536];
        for (;;) {
            cancelled(cancel);
            DWORD count = 0;
            wincheck(WinHttpReadData(operation.value, buffer, sizeof(buffer), &count), "Read response");
            if (!count) break;
            if (response.body.size() + count > 512 * 1024 * 1024) throw std::runtime_error("Response exceeds allowed size");
            response.body.insert(response.body.end(), buffer, buffer + count);
            if (request.progress) request.progress(expected_size ? static_cast<int>(100 * response.body.size() / expected_size) : 0, "Downloading portable update.");
        }
        return response;
    }
    throw std::runtime_error("Too many redirects");
}
Json response_json(const HttpResponse& response) {
    if (response.status < 200 || response.status >= 300) throw std::runtime_error("HTTP " + std::to_string(response.status));
    return Json::parse(response.body.begin(), response.body.end());
}
std::string url_encode(const std::string& value) {
    std::string result;
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') result += c;
        else { result += '%'; result += "0123456789ABCDEF"[c >> 4]; result += "0123456789ABCDEF"[c & 15]; }
    }
    return result;
}
}
