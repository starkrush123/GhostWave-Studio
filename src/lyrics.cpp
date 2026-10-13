#include "ghostwave/core.hpp"
#include <utf8proc.h>
#include <wx/regex.h>
#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
#include <stdexcept>

namespace gw {
namespace {
using Text = std::u32string;
Text decode(const std::string& text) {
    Text result;
    size_t i = 0;
    while (i < text.size()) {
        int32_t cp = 0;
        auto n = utf8proc_iterate(reinterpret_cast<const uint8_t*>(text.data() + i), text.size() - i, &cp);
        if (n < 0) throw std::runtime_error("Invalid UTF8 text");
        result += static_cast<char32_t>(cp); i += n;
    }
    return result;
}
std::string encode(const Text& text) {
    std::string result;
    for (auto cp : text) {
        uint8_t buffer[4]; auto n = utf8proc_encode_char(cp, buffer);
        if (n <= 0) throw std::runtime_error("Invalid Unicode character");
        result.append(reinterpret_cast<char*>(buffer), n);
    }
    return result;
}
Text lower(Text text) { for (auto& c : text) c = utf8proc_tolower(c); return text; }
Text upper(Text text) { for (auto& c : text) c = utf8proc_toupper(c); return text; }
bool space(char32_t c) {
    auto category = utf8proc_category(c);
    return (c >= 9 && c <= 13) || (c >= 0x1c && c <= 0x20) || c == 0x85 || category == UTF8PROC_CATEGORY_ZS || category == UTF8PROC_CATEGORY_ZL || category == UTF8PROC_CATEGORY_ZP;
}
bool word(char32_t c) {
    auto category = utf8proc_category(c);
    return c == '_' || (category >= UTF8PROC_CATEGORY_LU && category <= UTF8PROC_CATEGORY_LO) || (category >= UTF8PROC_CATEGORY_ND && category <= UTF8PROC_CATEGORY_NO);
}
Text trim(Text text) {
    size_t first = 0, last = text.size();
    while (first < last && space(text[first])) ++first;
    while (last > first && space(text[last - 1])) --last;
    return text.substr(first, last - first);
}
std::vector<Text> lines(const Text& text) {
    std::vector<Text> result(1);
    for (auto c : text) { if (c == '\n') result.emplace_back(); else result.back() += c; }
    return result;
}
Text join(const std::vector<Text>& values) {
    Text result;
    for (size_t i = 0; i < values.size(); ++i) { if (i) result += '\n'; result += values[i]; }
    return result;
}
std::vector<Text> tokens(const Text& text, bool punctuation = false, bool extended = false) {
    std::vector<Text> result;
    for (size_t i = 0; i < text.size();) {
        auto isword = [extended](char32_t c) { return word(c) || (extended && (c == '\'' || c == '-')); };
        if (isword(text[i])) {
            size_t start = i++; while (i < text.size() && isword(text[i])) ++i;
            result.push_back(text.substr(start, i - start));
        } else { if (punctuation && !space(text[i])) result.push_back(text.substr(i, 1)); ++i; }
    }
    return result;
}
bool ends(const Text& text, const Text& suffix) { return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0; }
bool isupper(const Text& text) { return text != lower(text) && text == upper(text); }
bool istitle(const Text& text) {
    bool previous = false, cased = false;
    for (auto c : text) {
        auto cat = utf8proc_category(c);
        if (cat == UTF8PROC_CATEGORY_LU || cat == UTF8PROC_CATEGORY_LT) { if (previous) return false; previous = cased = true; }
        else if (cat == UTF8PROC_CATEGORY_LL) { if (!previous) return false; previous = cased = true; }
        else previous = false;
    }
    return cased;
}
Text matchcase(const Text& original, Text replacement, bool title_words = false) {
    if (isupper(original)) return upper(replacement);
    if (istitle(original)) {
        replacement = lower(replacement);
        bool start = true;
        for (size_t i = 0; i < replacement.size(); ++i) {
            if (start) replacement[i] = utf8proc_toupper(replacement[i]);
            start = title_words && !(utf8proc_category(replacement[i]) >= UTF8PROC_CATEGORY_LU && utf8proc_category(replacement[i]) <= UTF8PROC_CATEGORY_LO);
        }
    }
    return replacement;
}
const Json& dictionaries() { static const auto data = read_json(app_dir() / "resources/lyrics.json"); return data; }
bool vowel(char32_t c, char32_t next = 0) {
    c = utf8proc_tolower(c); next = utf8proc_tolower(next);
    static const Text vowels = decode(dictionaries()["_CORE_VOWELS"]);
    return vowels.find(c) != Text::npos || (c == 'y' && vowels.find(next) == Text::npos);
}
std::string normalized(const std::string& input, utf8proc_option_t flags) {
    uint8_t* output = nullptr;
    auto n = utf8proc_map(reinterpret_cast<const uint8_t*>(input.data()), input.size(), &output, flags);
    if (n < 0) throw std::runtime_error(utf8proc_errmsg(n));
    std::string result(reinterpret_cast<char*>(output), n); free(output); return result;
}
Text filter(Text text, const Json& dictionary, bool artist, std::vector<std::string>& changes) {
    std::vector<std::pair<Text, Text>> entries;
    if (artist) { for (const auto& name : dictionary) { auto value = decode(name); if (!value.empty()) entries.push_back({lower(value), U"[Artist]"}); } }
    else { for (auto it = dictionary.begin(); it != dictionary.end(); ++it) entries.push_back({decode(it.key()), decode(it.value())}); }
    std::stable_sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });
    auto lowered = lower(text);
    Text result;
    for (size_t i = 0; i < text.size();) {
        bool matched = false;
        for (const auto& entry : entries) {
            size_t end = i + entry.first.size();
            if (end > text.size() || lowered.compare(i, entry.first.size(), entry.first)) continue;
            if ((i > 0 && word(text[i - 1])) == word(text[i])) continue;
            if (word(text[end - 1]) == (end < text.size() && word(text[end]))) continue;
            result += artist ? entry.second : matchcase(text.substr(i, entry.first.size()), entry.second);
            i = end; matched = true; changes.push_back(artist ? "Replaced an artist name." : "Replaced an explicit term."); break;
        }
        if (!matched) result += text[i++];
    }
    return result;
}
Text format_tags(Text text, std::vector<std::string>& changes) {
    std::string tags;
    for (const auto& tag : dictionaries()["RECOGNIZED_SUNO_TAGS"]) { if (!tags.empty()) tags += '|'; tags += wxRegEx::QuoteMeta(wxString::FromUTF8(tag.get<std::string>())).ToStdString(); }
    auto output = lines(text);
    for (auto& line : output) {
        auto raw = encode(trim(line));
        auto test = [&](const std::string& pattern) { wxRegEx regex(wxString::FromUTF8(pattern), wxRE_ICASE); if (!regex.IsValid()) throw std::runtime_error("Invalid structural expression"); return regex.Matches(wxString::FromUTF8(raw)); };
        if (raw.size() < 2) continue;
        const auto original = raw;
        if ((raw.rfind("[[", 0) == 0 || (raw.size() >= 2 && raw.substr(raw.size() - 2) == "]]")) && test(R"(^\[+\s*([a-zA-Z0-9\s:_\-\(\)]+?)\s*\]+$)")) {
            size_t a = raw.find_first_not_of('['), b = raw.find_last_not_of(']'); raw = "[" + encode(trim(decode(raw.substr(a, b - a + 1)))) + "]";
        }
        if (test("^[\\(\\{]\\s*(?:" + tags + ")(?:\\s*[0-9]+)?(?:\\s*:\\s*[^\\)\\}]+)?\\s*[\\)\\}]$")) raw = "[" + encode(trim(decode(raw.substr(1, raw.size() - 2)))) + "]";
        if (test("^(?:" + tags + ")(?:\\s*[0-9]+)?\\s*:?$")) { while (!raw.empty() && raw.back() == ':') raw.pop_back(); raw = "[" + encode(trim(decode(raw))) + "]"; }
        if (test(R"(^\[\s*([a-zA-Z0-9\s:_\-\(\)]+?)\s*\]$)")) {
            auto inside = wxString::FromUTF8(encode(trim(decode(raw.substr(1, raw.size() - 2)))));
            for (const auto& value : dictionaries()["RECOGNIZED_SUNO_TAGS"]) {
                auto tag = value.get<std::string>();
                wxRegEx regex("^(" + wxRegEx::QuoteMeta(wxString::FromUTF8(tag)) + R"()(\s*[0-9]+|\s*:\s*.+|\s*\([^\)]+\))?$)", wxRE_ICASE);
                if (!regex.IsValid()) throw std::runtime_error("Invalid tag expression");
                if (!regex.Matches(inside)) continue;
                auto suffix = encode(trim(decode(regex.GetMatch(inside, 2).ToStdString(wxConvUTF8))));
                if (!suffix.empty() && suffix.front() == ':') suffix = ": " + encode(trim(decode(suffix.substr(suffix.find_first_not_of(':')))));
                else if (!suffix.empty()) suffix = " " + suffix;
                raw = "[" + tag + suffix + "]"; break;
            }
        }
        if (raw != original) { line = decode(raw); changes.push_back("Standardized a structural tag."); }
    }
    return join(output);
}
int line_syllables(const Text& text) {
    auto value = trim(text);
    if (value.empty() || value.front() == '[') return 0;
    int count = 0;
    wxString input = wxString::FromUTF8(encode(value));
    wxRegEx regex(R"(\b[a-zA-Z']+\b)");
    while (regex.Matches(input)) { size_t start, length; regex.GetMatch(&start, &length); count += count_syllables(regex.GetMatch(input).ToStdString(wxConvUTF8)); input = input.Mid(start + length); }
    return count;
}
}
std::string normalize_unicode(const std::string& input) {
    Text out;
    for (auto c : decode(input)) {
        if (Text{0x2019,0x2018,0x201a,0x201b,U'`'}.find(c) != Text::npos) out += '\'';
        else if (Text{0x201c,0x201d,0x201e,0x201f}.find(c) != Text::npos) out += '"';
        else if (Text{0x2014,0x2013,0x2015,0x2212}.find(c) != Text::npos) out += '-';
        else if (c == 0x2026) out += U"...";
        else if (c == 0x2022 || c == 0xb7) out += '*';
        else if (c == 0xa0) out += ' ';
        else if (c != 0x200b && c != 0x200c && c != 0x200d && c != 0xfeff) out += c;
    }
    auto decomposed = decode(normalized(encode(out), static_cast<utf8proc_option_t>(UTF8PROC_STABLE | UTF8PROC_COMPAT | UTF8PROC_DECOMPOSE)));
    out.clear();
    for (auto c : decomposed) if (utf8proc_category(c) != UTF8PROC_CATEGORY_MN) out += c;
    auto composed = decode(normalized(encode(out), static_cast<utf8proc_option_t>(UTF8PROC_STABLE | UTF8PROC_COMPOSE)));
    out.clear();
    for (auto c : composed) if (!(c <= 8 || c == 11 || c == 12 || (c >= 14 && c <= 31) || c == 127)) out += c;
    return encode(out);
}
std::string syllabify(const std::string& input) {
    auto w = decode(input);
    if (w.size() <= 3 || w.find('-') != Text::npos || w.find('\'') != Text::npos) return input;
    size_t vowels = 0;
    for (size_t i = 0; i < w.size(); ++i) vowels += vowel(w[i], i + 1 < w.size() ? w[i + 1] : 0);
    if (vowels < 2) return input;
    Text out;
    auto isdigraph = [&](const Text& text) { auto lower_text = encode(lower(text)); for (const auto& d : dictionaries()["_MULTILINGUAL_DIGRAPHS"]) if (d == lower_text) return true; return false; };
    for (size_t i = 0; i < w.size(); ++i) {
        out += w[i];
        if (i + 2 >= w.size() || !vowel(w[i], w[i + 1])) continue;
        bool c1 = vowel(w[i + 1], w[i + 2]), c2 = vowel(w[i + 2], i + 3 < w.size() ? w[i + 3] : 0);
        if (i + 3 < w.size() && isdigraph(w.substr(i + 1, 2))) {
            bool c3 = vowel(w[i + 3], i + 4 < w.size() ? w[i + 4] : 0);
            if (c3) { out += '-'; out += w.substr(i + 1, 2); i += 2; continue; }
            if (i + 4 < w.size() && vowel(w[i + 4])) { out += w.substr(i + 1, 2); out += '-'; i += 2; continue; }
        }
        if (!c1 && c2) out += '-';
        else if (!c1 && !c2 && i + 3 < w.size() && vowel(w[i + 3])) { out += w[++i]; out += '-'; }
    }
    return encode(out);
}
int count_syllables(const std::string& input) {
    auto w = lower(decode(input));
    const Text punctuation = U".,!?;:'\"()[]{}~";
    while (!w.empty() && punctuation.find(w.front()) != Text::npos) w.erase(w.begin());
    while (!w.empty() && punctuation.find(w.back()) != Text::npos) w.pop_back();
    if (w.empty()) return 0;
    if (w.find('-') != Text::npos) { int n = 0; bool in = false; for (auto c : w) { if (c == '-') in = false; else if (!space(c) && !in) { ++n; in = true; } } if (n) return n; }
    w.erase(std::remove(w.begin(), w.end(), '\''), w.end());
    if (w.size() <= 3) return 1;
    static const Json known = {{"yesterday",3},{"tomorrow",3},{"forever",3},{"everything",3},{"fantasy",3},{"reality",4},{"lonely",2},{"trouble",2},{"troubles",2},{"living",2},{"waiting",2},{"midnight",2},{"streetlights",2},{"people",2},{"silence",2},{"shadow",2},{"shadows",2},{"anywhere",3},{"somebody",3},{"nobody",3},{"everybody",4},{"monstrosity",4},{"silhouette",3},{"scaramouche",3},{"fandango",3},{"galileo",4}};
    if (known.contains(encode(w))) return known[encode(w)];
    Text proc = w;
    if (ends(w,U"e") && !(ends(w,U"le") && w.size() > 2 && Text(U"aeiouy").find(w[w.size() - 3]) == Text::npos)) proc.pop_back();
    int count = 0; bool previous = false;
    for (auto c : proc) { bool current = vowel(c); if (current && !previous) ++count; previous = current; }
    for (const auto& hiatus : {U"eo",U"ia",U"io",U"ua",U"uo",U"ea",U"oa"}) if (w.find(hiatus) != Text::npos) ++count;
    return std::max(1, count);
}
std::string syllabify_lines(const std::string& input) {
    auto output = lines(decode(input));
    if (!output.empty() && output.back().empty()) output.pop_back();
    for (auto& line : output) {
        auto value = trim(line); if (value.empty() || (value.front() == '[' && value.back() == ']')) continue;
        Text out;
        for (size_t i = 0; i < line.size();) {
            if (line[i] == '[') { auto end = line.find(']', i); if (end != Text::npos) { out += line.substr(i, end - i + 1); i = end + 1; continue; } }
            if (word(line[i]) || line[i] == '\'' || line[i] == '-') { size_t start = i++; while (i < line.size() && (word(line[i]) || line[i] == '\'' || line[i] == '-')) ++i; out += decode(syllabify(encode(line.substr(start, i - start)))); }
            else out += line[i++];
        }
        line = out;
    }
    return encode(join(output));
}
Json audit_lyrics(const std::string& original, const std::string& result) {
    auto a = tokens(lower(decode(original))), b = tokens(lower(decode(result)));
    auto grams = [](const std::vector<Text>& words) { std::set<Text> result; for (size_t i = 0; i + 3 < words.size(); ++i) result.insert(words[i] + U" " + words[i+1] + U" " + words[i+2] + U" " + words[i+3]); return result; };
    auto ga = grams(a), gb = grams(b);
    size_t matches = 0; for (const auto& g : ga) matches += gb.count(g);
    std::set<Text> sa(a.begin(), a.end()), sb(b.begin(), b.end()), united = sa;
    united.insert(sb.begin(), sb.end()); size_t intersection = 0; for (const auto& w : sa) intersection += sb.count(w);
    auto content_lines = [](const std::string& text) { std::vector<Text> result; for (auto line : lines(decode(text))) { line = trim(line); if (!line.empty() && line.front() != '[') result.push_back(line); } return result; };
    auto la = content_lines(original), lb = content_lines(result);
    size_t n = std::min(la.size(), lb.size()), cadence = 0;
    Json details = Json::array();
    for (size_t i = 0; i < n; ++i) { auto s1 = line_syllables(la[i]), s2 = line_syllables(lb[i]); cadence += std::abs(s1-s2) <= 1; details.push_back(Json{{"line",i+1},{"original",s1},{"result",s2}}); }
    return {{"original_words",a.size()},{"cloaked_words",b.size()},{"ngram_overlap_pct",ga.empty()?0:static_cast<double>(matches)/ga.size()*100.0},{"token_similarity_pct",united.empty()?0:static_cast<double>(intersection)/united.size()*100.0},{"syllable_accuracy_pct",n?static_cast<double>(cadence)/n*100.0:100.0},{"details",details}};
}
LyricsResult process_lyrics(const std::string& input, const Profile& profile, const std::atomic_bool* cancel, Transport transport) {
    LyricsResult result;
    const auto& cfg = profile.data;
    if (input.empty()) { result.stats = {{"total_characters",0},{"total_lines",0},{"word_count",0},{"estimated_duration","0s"}}; result.changes.push_back("Input text is empty."); return result; }
    auto current = input;
    if (cfg["normalize_unicode"].get<bool>()) { current = normalize_unicode(current); if (current != input) result.changes.push_back("Normalized Unicode punctuation and accents."); }
    auto text = decode(current);
    cancelled(cancel);
    if (cfg["strip_celebrities"].get<bool>()) text = filter(text, cfg["custom_celebrity_blacklist"].empty() ? dictionaries()["DEFAULT_CELEBRITY_BLACKLIST"] : cfg["custom_celebrity_blacklist"], true, result.changes);
    if (cfg["filter_profanity"].get<bool>()) text = filter(text, dictionaries()["PROFANITY_HOMOPHONES"], false, result.changes);
    if (cfg["format_tags"].get<bool>()) text = format_tags(text, result.changes);
    bool remote_done = false;
    std::string mode = cfg["lyrics_cloak_mode"];
    if (cfg["enable_lyrics_cloaking"].get<bool>() && mode == "cloud" && !cfg["lyrics_cloud_api_token"].get<std::string>().empty()) {
        const auto before = encode(text);
        try {
            std::string provider = cfg["lyrics_cloud_provider"], endpoint = cfg["lyrics_cloud_endpoint_url"];
            if (endpoint.empty()) {
                if (provider == "groq") endpoint = "https://api.groq.com/openai/v1/chat/completions";
                else if (provider == "openrouter") endpoint = "https://openrouter.ai/api/v1/chat/completions";
                else if (provider == "openai") endpoint = "https://api.openai.com/v1/chat/completions";
                else throw std::runtime_error("Custom endpoint is required");
            }
            Json payload = {{"model",cfg["lyrics_cloud_model"]},{"messages",Json::array({{{"role","system"},{"content","You are an acoustic and lyric cloaking assistant for signal processing study.\nYour task is to rewrite song lyrics so they will NOT match commercial lyrics databases (0% contiguous 4-word overlap).\nStrict Rules:\n1. Maintain the EXACT syllable count per line (meter and cadence must match for singing).\n2. Preserve the song structure, rhyme scheme, and Suno tags like [Verse], [Chorus], [Bridge].\n3. Do not include preamble, conversational remarks, or markdown code fences; output ONLY the cloaked lyrics."}},{{"role","user"},{"content","Rewrite these lyrics for acoustic study:\n\n"+before}}})},{"temperature",0.6},{"max_tokens",1500}};
            auto response = response_json(transport({"POST",endpoint,payload.dump(),{{"Content-Type","application/json"},{"Authorization","Bearer "+cfg["lyrics_cloud_api_token"].get<std::string>()}},30},cancel));
            auto rewritten = wxString::FromUTF8(encode(trim(decode(response.at("choices").at(0).at("message").at("content")))));
            wxRegEx opening("^```[a-zA-Z]*\\n?"), closing("\\n?```$"); opening.ReplaceFirst(&rewritten, ""); closing.ReplaceFirst(&rewritten, "");
            auto output = trim(decode(rewritten.ToStdString(wxConvUTF8)));
            if (!output.empty() && output != text) { text = output; remote_done = true; result.changes.push_back("Remote rewriting completed."); }
        } catch (...) { cancelled(cancel); result.warnings.push_back("Remote rewriting failed, so offline processing was used."); }
    }
    if (cfg["enable_lyrics_cloaking"].get<bool>() && !remote_done) {
        auto output = lines(text); size_t adlib = 0, line_index = 0;
        for (auto& line : output) {
            cancelled(cancel); ++line_index;
            auto value = trim(line); if (value.empty() || value.front() == '[') continue;
            auto words = tokens(value, true, true);
            for (auto& token : words) {
                if (token.empty() || !(word(token.front()) || token.front() == '\'' || token.front() == '-')) continue;
                auto low = lower(token); auto key = encode(low);
                if (mode == "semantic" && dictionaries()["SYLLABLE_SYNONYMS"].contains(key)) token = matchcase(token, decode(dictionaries()["SYLLABLE_SYNONYMS"][key][1][0]), true);
                else if (mode == "scramble" || mode == "phonetic" || mode == "syllable" || mode == "hybrid") {
                    if (dictionaries()["ACOUSTIC_PHONETIC_DISGUISE"].contains(key)) token = matchcase(token, decode(dictionaries()["ACOUSTIC_PHONETIC_DISGUISE"][key]));
                    else if (ends(low,U"ing") && low.size() > 4) token = token.substr(0,token.size()-3) + U"-in'";
                    else if (ends(low,U"ed") && low.size() > 4 && !ends(low,U"ted") && !ends(low,U"ded")) token = token.substr(0,token.size()-2) + U"'d";
                    else if (low.find(U"ght") != Text::npos && low.rfind(U"ght",0) != 0) { for (const auto& pair : {std::pair{U"ght",U"ite"}, {U"GHT",U"ITE"}}) { size_t position = 0; while ((position=token.find(pair.first,position)) != Text::npos) { token.replace(position,3,pair.second); position+=3; } } }
                    else if (token.size() >= 4) token = matchcase(token, decode(syllabify(encode(token))));
                }
            }
            Text reconstructed;
            for (const auto& token : words) {
                if (!reconstructed.empty() && !(token.size()==1 && Text(U".,!?;:'\")]} ").find(token[0])!=Text::npos) && Text(U"([{ ").find(reconstructed.back())==Text::npos && reconstructed!=U"\"" && !ends(reconstructed,U" \"")) reconstructed += ' ';
                reconstructed += token;
            }
            if (cfg["lyrics_add_vibrato_glides"].get<bool>() && !reconstructed.empty() && reconstructed.back()!=']' && reconstructed.back()!='~' && !ends(reconstructed,U"...")) {
                if (Text(U".!?,").find(reconstructed.back())!=Text::npos) reconstructed.insert(reconstructed.size()-1,1,'~'); else reconstructed += '~';
            }
            if (cfg["lyrics_break_ngrams"].get<bool>() && words.size() >= 4 && (line_index%2==1 || cfg["lyrics_adlib_frequency"].get<double>() >= 0.7)) {
                auto extra = U" " + decode(dictionaries()["ADLIBS"][adlib++%dictionaries()["ADLIBS"].size()]);
                size_t comma = 0; while ((comma=reconstructed.find(',',comma)) != Text::npos && comma+1<reconstructed.size() && utf8proc_category(reconstructed[comma+1])==UTF8PROC_CATEGORY_ND) ++comma;
                if (comma == Text::npos) reconstructed += extra; else reconstructed.insert(comma,extra);
            }
            line = reconstructed;
        }
        text = join(output); result.changes.push_back("Applied the selected lyric transformation.");
    }
    result.text = encode(text); result.audit = audit_lyrics(input,result.text);
    if (cfg["check_limits"].get<bool>()) {
        auto output = lines(text);
        for (size_t i=0;i<output.size();++i) if (output[i].size()>80) result.warnings.push_back("Line "+std::to_string(i+1)+" exceeds 80 characters.");
        if (text.size()>3000) result.warnings.push_back("Lyrics exceed 3000 characters.");
        auto words = tokens(text).size(); int seconds = static_cast<int>(words/130.0*60);
        std::ostringstream duration; if(seconds) duration<<seconds/60<<"m "<<(seconds%60<10?"0":"")<<seconds%60<<'s'; else duration<<"0s";
        result.stats = {{"total_characters",text.size()},{"total_lines",output.size()},{"word_count",words},{"estimated_duration",duration.str()}};
    }
    if(result.changes.empty()) result.changes.push_back("No changes were needed.");
    return result;
}
}
