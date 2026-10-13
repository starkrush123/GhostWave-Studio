#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace gw {
namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;
using Bytes = std::vector<unsigned char>;
using Progress = std::function<void(int, const std::string&)>;
inline constexpr char version[] = "2.0.0";
inline constexpr char repository[] = "starkrush123/GhostWave-Studio";

fs::path app_dir();
fs::path user_dir();
Bytes read_bytes(const fs::path& path, size_t limit = 512 * 1024 * 1024);
void write_atomic(const fs::path& path, const Bytes& bytes);
Json read_json(const fs::path& path);
void cancelled(const std::atomic_bool* cancel);
std::wstring wide(const std::string& text);
std::string utf8(const std::wstring& text);
std::string random_hex(size_t bytes);
std::string base64(const Bytes& bytes, bool url = false);
Bytes unbase64(std::string text, bool url = false);
Bytes sha256(const Bytes& bytes);
struct TempFile {
    fs::path path;
    explicit TempFile(const std::string& extension, fs::path directory = fs::temp_directory_path());
    ~TempFile();
};
struct ProcessResult { int code; Bytes out; std::string error; };
ProcessResult run_process(const std::vector<std::wstring>& args, int timeout_seconds = 60,
                          const std::atomic_bool* cancel = nullptr);
fs::path ffmpeg_path();

struct HttpRequest {
    std::string method = "GET", url, body;
    std::vector<std::pair<std::string, std::string>> headers;
    int timeout_seconds = 30;
    Progress progress;
};
struct HttpResponse { unsigned status = 0; Bytes body; std::string content_type; };
using Transport = std::function<HttpResponse(const HttpRequest&, const std::atomic_bool*)>;
HttpResponse http(const HttpRequest& request, const std::atomic_bool* cancel = nullptr);
Json response_json(const HttpResponse& response);
std::string url_encode(const std::string& value);

struct Profile {
    Json data;
    fs::path path;
    bool recovered = false;
    Profile();
    static fs::path resolve(fs::path explicit_path = {});
    static Profile load(fs::path explicit_path = {});
    static Profile decode(const Bytes& bytes);
    Bytes encode() const;
    void save(fs::path destination = {});
    void merge(const Json& values);
};

struct RandomState {
    uint32_t key[624]{};
    size_t position = 624;
    bool has_gaussian = false;
    double gaussian = 0;
    explicit RandomState(uint32_t seed);
    uint32_t next();
    double uniform();
    double normal();
    void restore(const Json& state);
    Json state() const;
};
struct Audio {
    int rate = 44100, channels = 1;
    std::vector<float> samples;
    size_t frames() const { return samples.size() / channels; }
};
struct AudioOptions {
    Json values;
    AudioOptions();
    static AudioOptions preset(int index, const Profile& profile);
};
Audio read_audio(const fs::path& path, int target_rate = 44100, bool remove_dc = true);
Audio resample_audio(Audio data, int rate);
void write_pcm(const fs::path& path, const Audio& data);
void export_audio(const fs::path& path, Audio data, const std::string& bitrate = "wav",
                  bool strip_metadata = true, const std::atomic_bool* cancel = nullptr);
Audio audio_stage(const std::string& stage, Audio data, const Json& options, RandomState& random,
                  const std::atomic_bool* cancel = nullptr);
struct AudioResult {
    bool success = false;
    double original_duration = 0, final_duration = 0;
    std::string error;
    Json audit;
};
AudioResult process_audio(const fs::path& input, const fs::path& output, const AudioOptions& options,
                          const Profile& profile, Progress progress = {},
                          const std::atomic_bool* cancel = nullptr, RandomState* random = nullptr,
                          Transport transport = http);
std::vector<fs::path> slice_audio(const fs::path& input, const fs::path& directory, double seconds,
                                const std::atomic_bool* cancel = nullptr);
Json inspect_audio(const fs::path& input);
Json audit_audio(const fs::path& original, const fs::path& output,
                 const std::atomic_bool* cancel = nullptr);
Audio cloud_stems(const fs::path& input, const Profile& profile, Progress progress,
                  const std::atomic_bool* cancel, Transport transport = http);

std::string normalize_unicode(const std::string& text);
std::string syllabify(const std::string& word);
int count_syllables(const std::string& word);
std::string syllabify_lines(const std::string& text);
Json audit_lyrics(const std::string& original, const std::string& result);
struct LyricsResult {
    std::string text;
    std::vector<std::string> changes, warnings;
    Json stats, audit;
};
LyricsResult process_lyrics(const std::string& text, const Profile& profile,
                           const std::atomic_bool* cancel = nullptr, Transport transport = http);
std::vector<int> normalize_version(const std::string& value);
Json parse_changelog(const std::string& text);
Json check_update(Transport transport = http, const std::atomic_bool* cancel = nullptr);
std::string changelog(bool online, Transport transport = http, const std::atomic_bool* cancel = nullptr);
fs::path download_update(const Json& info, Progress progress, const std::atomic_bool* cancel = nullptr);
std::string diagnostics();
Json create_ticket(Profile& profile, const std::string& title, const std::string& description,
                   const std::string& category, bool include_diagnostics, Transport transport = http,
                   const std::atomic_bool* cancel = nullptr);
Json fetch_ticket(const Json& ticket, Transport transport = http, const std::atomic_bool* cancel = nullptr);
Json reply_ticket(const Json& ticket, const std::string& message, Transport transport = http,
                  const std::atomic_bool* cancel = nullptr);
int command_line(const std::vector<std::string>& args, const std::function<int()>& launch_gui);
}
