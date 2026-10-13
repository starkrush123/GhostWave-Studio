#include "ghostwave/core.hpp"
#include <windows.h>
#include <wx/wfstream.h>
#include <wx/zipstrm.h>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace gw {
namespace {
std::string trim(std::string value) {
    auto first=value.find_first_not_of(" \t\r\n"),last=value.find_last_not_of(" \t\r\n");
    return first==std::string::npos?"":value.substr(first,last-first+1);
}
std::string lowered(std::string value) { for(auto& c:value) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return value; }
std::string host(const std::string& url) {
    if(url.rfind("https://",0)!=0) throw std::runtime_error("Endpoint must use HTTPS");
    auto end=url.find('/',8); auto result=lowered(url.substr(8,end==std::string::npos?end:end-8));
    if(result.find_first_of("@:#?\\\r\n")!=std::string::npos) throw std::runtime_error("Invalid endpoint authority");
    return result;
}
bool delivery(const std::string& url) {
    auto authority=host(url);
    const std::string suffix=".replicate.delivery";
    return authority=="replicate.delivery" || (authority.size()>suffix.size()&&authority.compare(authority.size()-suffix.size(),suffix.size(),suffix)==0);
}
std::string environment(const wchar_t* name) {
    wchar_t value[32768]; DWORD n=GetEnvironmentVariableW(name,value,32768); return n&&n<32768?utf8(std::wstring(value,n)):"";
}
Audio binary_audio(const Bytes& bytes) {
    if(bytes.size()<44) throw std::runtime_error("Empty or invalid remote audio");
    TempFile temp(".wav"); write_atomic(temp.path,bytes); return read_audio(temp.path,0,false);
}
std::string hex(const Bytes& bytes) { std::ostringstream out; for(auto b:bytes) out<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(b); return out.str(); }
const std::string worker="https://ghostwave-studio-private.hafiyanajah.workers.dev";
Json ticket_response(HttpResponse response) {
    auto data=response_json(response);
    if(!data.is_object()) throw std::runtime_error("Invalid support response");
    if(data.contains("success")&&!data["success"].get<bool>()) throw std::runtime_error(data.value("error",std::string("Support request failed")));
    return data;
}
}
Audio cloud_stems(const fs::path& input,const Profile& profile,Progress progress,const std::atomic_bool* cancel,Transport transport) {
    cancelled(cancel);
    const auto& cfg=profile.data;
    std::string provider=cfg["cloud_provider"],token=cfg["cloud_api_token"],endpoint=cfg["cloud_endpoint_url"];
    int timeout=cfg["cloud_timeout_seconds"]; if(timeout<1||timeout>600) throw std::runtime_error("Invalid cloud timeout");
    auto bytes=read_bytes(input); auto ext=lowered(input.extension().u8string());
    std::string mime=ext==".wav"?"audio/wav":ext==".mp3"?"audio/mpeg":ext==".flac"?"audio/flac":"application/octet-stream";
    Json output;
    if(provider=="replicate") {
        if(token.empty()) token=environment(L"REPLICATE_API_TOKEN");
        if(token.empty()) throw std::runtime_error("Replicate token is required");
        if(bytes.size()>8*1024*1024) {
            TempFile compressed(".mp3");
            auto result=run_process({ffmpeg_path().wstring(),L"-y",L"-i",input.wstring(),L"-b:a",L"192k",L"-ar",L"44100",compressed.path.wstring()},60,cancel);
            if(!result.code&&fs::exists(compressed.path)&&fs::file_size(compressed.path)) { bytes=read_bytes(compressed.path); mime="audio/mpeg"; }
        }
        std::string model=cfg["cloud_model_version"];
        if(model.size()!=64||model.find_first_not_of("0123456789abcdef")!=std::string::npos) model="25a173c086e22410ed5e644737da3273b307586ce47304622790802f530c37a4";
        Json payload={{"version",model},{"input",{{"audio","data:"+mime+";base64,"+base64(bytes)},{"two_stems","vocals"}}}};
        auto response=response_json(transport({"POST","https://api.replicate.com/v1/predictions",payload.dump(),{{"Authorization","Bearer "+token},{"Content-Type","application/json"}},30},cancel));
        auto id=response.at("id").get<std::string>();
        auto poll=response.value("urls",Json::object()).value("get","https://api.replicate.com/v1/predictions/"+url_encode(id));
        if(host(poll)!="api.replicate.com") throw std::runtime_error("Invalid prediction address");
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(timeout);
        try {
            for(;;) {
                cancelled(cancel);
                auto status=response.value("status",std::string("starting"));
                if(status=="succeeded") { output=response.at("output"); break; }
                if(status=="failed"||status=="canceled") throw std::runtime_error("Remote separation failed");
                if(std::chrono::steady_clock::now()>=deadline) throw std::runtime_error("Remote separation timed out");
                if(progress) progress(45,"Waiting for remote separation.");
                for(int i=0;i<30;++i) { cancelled(cancel); std::this_thread::sleep_for(std::chrono::milliseconds(100)); }
                response=response_json(transport({"GET",poll,"",{{"Authorization","Bearer "+token}},15},cancel));
            }
        } catch(...) {
            if(cancel&&cancel->load()) try { transport({"POST","https://api.replicate.com/v1/predictions/"+url_encode(id)+"/cancel","",{{"Authorization","Bearer "+token}},15},nullptr); } catch(...) {}
            throw;
        }
    } else if(provider=="huggingface"||provider=="custom") {
        if(provider=="huggingface") { if(token.empty()) token=environment(L"HF_TOKEN"); if(endpoint.empty()) endpoint="https://api-inference.huggingface.co/models/facebook/demucs"; }
        if(endpoint.empty()) throw std::runtime_error("Custom endpoint is required");
        HttpRequest request; request.method="POST"; request.url=endpoint; request.timeout_seconds=timeout;
        if(!token.empty()) request.headers.push_back({"Authorization","Bearer "+token});
        if(provider=="custom") {
            std::string boundary="GhostWaveBoundary"+random_hex(16),filename=input.filename().u8string();
            for(auto& c:filename) if(c=='"'||c=='\r'||c=='\n'||c=='\\') c='_';
            request.headers.push_back({"Content-Type","multipart/form-data; boundary="+boundary});
            request.body="--"+boundary+"\r\nContent-Disposition: form-data; name=\"audio\"; filename=\""+filename+"\"\r\nContent-Type: "+mime+"\r\n\r\n";
            request.body.append(bytes.begin(),bytes.end()); request.body+="\r\n--"+boundary+"--\r\n";
        } else { request.headers.push_back({"Content-Type",mime}); request.body.assign(bytes.begin(),bytes.end()); }
        auto response=transport(request,cancel);
        if(response.status<200||response.status>=300) throw std::runtime_error("HTTP "+std::to_string(response.status));
        if(response.content_type.find("audio")!=std::string::npos || (response.body.size()>=4&&std::string(response.body.begin(),response.body.begin()+4)=="RIFF")) {
            auto audio=binary_audio(response.body);
            return resample_audio(std::move(audio),44100);
        }
        output=Json::parse(response.body.begin(),response.body.end());
    } else throw std::runtime_error("Unsupported stem provider");
    std::string instrumental,vocal;
    if(output.is_object()) {
        for(auto it=output.begin();it!=output.end();++it) {
            auto key=lowered(it.key()); if(!it.value().is_string()) continue;
            if(key.find("no_vocals")!=std::string::npos||key.find("instrumental")!=std::string::npos||key.find("accompaniment")!=std::string::npos) instrumental=it.value();
            else if(key.find("vocal")!=std::string::npos&&key.find("no_vocal")==std::string::npos) vocal=it.value();
        }
        if(instrumental.empty()&&output.contains("output")&&output["output"].is_string()) instrumental=output["output"];
    } else if(output.is_array()) {
        for(const auto& item:output) if(item.is_string()) { auto value=item.get<std::string>(),low=lowered(value); if(low.find("no_vocals")!=std::string::npos||low.find("instrumental")!=std::string::npos) instrumental=value; else if(low.find("vocal")!=std::string::npos&&low.find("no_vocal")==std::string::npos) vocal=value; }
        if(instrumental.empty()&&!output.empty()&&output[0].is_string()) instrumental=output[0];
    } else if(output.is_string()) instrumental=output;
    if(instrumental.empty()) throw std::runtime_error("Instrumental stem is missing");
    auto download=[&](const std::string& url) {
        HttpRequest request; request.url=url; request.timeout_seconds=60;
        if(provider=="replicate"&&delivery(url)) request.headers.push_back({"Authorization","Bearer "+token});
        auto response=transport(request,cancel);
        if(response.status!=200) throw std::runtime_error("Stem download failed");
        return binary_audio(response.body);
    };
    if(progress) progress(75,"Downloading separated stems.");
    auto audio=download(instrumental);
    if(!vocal.empty()) {
        auto voice=download(vocal);
        if(voice.rate!=audio.rate||voice.channels!=audio.channels) throw std::runtime_error("Remote stems have different formats");
        auto count=std::min(audio.samples.size(),voice.samples.size()); audio.samples.resize(count);
        float gain=static_cast<float>(std::pow(10.0,-18.0/20.0)); for(size_t i=0;i<count;++i) audio.samples[i]+=voice.samples[i]*gain;
    }
    return resample_audio(std::move(audio),44100);
}
std::vector<int> normalize_version(const std::string& value) {
    auto text=trim(value); text.erase(0,text.find_first_not_of("vV")); text=text.substr(0,text.find_first_of("-+"));
    std::vector<int> result; std::istringstream stream(text); std::string part;
    while(std::getline(stream,part,'.')) { std::string digits; for(unsigned char c:part) if(std::isdigit(c)) digits+=c; result.push_back(digits.empty()?0:std::stoi(digits)); }
    while(result.size()<3) result.push_back(0); return result;
}
Json parse_changelog(const std::string& text) {
    Json result={{"version",""},{"date",""},{"installer_url",""},{"portable_url",""},{"notes",""}};
    std::istringstream stream(text); std::string line,notes; bool collecting=false;
    while(std::getline(stream,line)) {
        auto value=trim(line),low=lowered(value),colon=value.find(':');
        if(low.rfind("version:",0)==0) result["version"]=trim(value.substr(colon+1));
        else if(low.rfind("date:",0)==0||low.rfind("release date:",0)==0) result["date"]=trim(value.substr(colon+1));
        else if(low.rfind("portable:",0)==0) result["portable_url"]=trim(value.substr(colon+1));
        else if(low.rfind("installer:",0)==0||low.rfind("download:",0)==0) result["installer_url"]=trim(value.substr(colon+1));
        else { if(low.rfind("what's new:",0)==0||low.rfind("what is new:",0)==0||low.rfind("changelog:",0)==0||low.rfind("changes:",0)==0) collecting=true; if(collecting) { if(!notes.empty()) notes+='\n'; notes+=line; } }
    }
    result["notes"]=notes.empty()?trim(text):trim(notes); return result;
}
Json check_update(Transport transport,const std::atomic_bool* cancel) {
    Json result={{"has_update",false},{"current_version",version},{"latest_version",version},{"error_message",""}};
    try {
        auto response=response_json(transport({"GET",std::string("https://api.github.com/repos/")+repository+"/releases/latest","",{{"Accept","application/vnd.github+json"}},6},cancel));
        if(response.value("draft",true)||response.value("prerelease",true)||!response.contains("published_at")||response["published_at"].is_null()) return result;
        result["latest_version"]=response.at("tag_name"); result["release_notes"]=response.value("body",std::string()); result["html_url"]=response.at("html_url");
        if(normalize_version(response.at("tag_name"))<=normalize_version(version)) return result;
        for(const auto& asset:response.at("assets")) {
            std::string name=asset.at("name"),url=asset.at("browser_download_url");
            if(url.rfind(std::string("https://github.com/")+repository+"/releases/download/",0)!=0) continue;
            if(name.rfind("GhostWaveStudio-",0)==0&&name.find("win64")!=std::string::npos&&name.size()>13&&name.substr(name.size()-13)=="-Portable.zip") { result["asset_name"]=name; result["download_url"]=url; result["asset_size"]=asset.at("size"); }
        }
        if(result.contains("asset_name")) for(const auto& asset:response.at("assets")) if(asset.at("name")==result.at("asset_name").get<std::string>()+".sha256") {
            auto url=asset.at("browser_download_url").get<std::string>();
            if(url.rfind(std::string("https://github.com/")+repository+"/releases/download/",0)==0) result["checksum_url"]=url;
        }
        result["has_update"]=result.contains("download_url")&&result.contains("checksum_url");
    } catch(const std::exception& error) { cancelled(cancel); result["error_message"]=error.what(); }
    return result;
}
std::string changelog(bool online,Transport transport,const std::atomic_bool* cancel) {
    auto cache=user_dir()/".ghostwave_changelog.txt";
    std::string best;
    for(const auto& path:{app_dir()/"changelog.txt",cache}) if(fs::is_regular_file(path)) {
        auto bytes=read_bytes(path,1024*1024); std::string text(bytes.begin(),bytes.end());
        if(best.empty()||normalize_version(parse_changelog(text)["version"])>=normalize_version(parse_changelog(best)["version"])) best=text;
    }
    if(online) try {
        auto response=transport({"GET",std::string("https://raw.githubusercontent.com/")+repository+"/main/changelog.txt","",{},6},cancel);
        if(response.status==200&&response.body.size()<1024*1024) {
            std::string text(response.body.begin(),response.body.end());
            if(normalize_version(parse_changelog(text)["version"])>=normalize_version(parse_changelog(best)["version"])) { write_atomic(cache,response.body); best=text; }
        }
    } catch(...) { cancelled(cancel); }
    return best;
}
fs::path download_update(const Json& info,Progress progress,const std::atomic_bool* cancel) {
    if(!info.value("has_update",false)) throw std::runtime_error("No published portable update");
    auto name=info.at("asset_name").get<std::string>();
    if(fs::u8path(name).filename()!=fs::u8path(name)) throw std::runtime_error("Invalid update filename");
    HttpRequest request; request.url=info.at("download_url"); request.timeout_seconds=60; request.progress=progress;
    auto response=http(request,cancel); if(response.status!=200) throw std::runtime_error("Update download failed");
    auto checksum=http({"GET",info.at("checksum_url"),"",{},15},cancel);
    if(checksum.status!=200) throw std::runtime_error("Update checksum is missing");
    std::string digest(checksum.body.begin(),checksum.body.end());
    if(digest.size()<64||lowered(digest.substr(0,64))!=hex(sha256(response.body))) throw std::runtime_error("Update checksum failed");
    auto folder=user_dir()/"Downloads/GhostWaveStudio"/fs::u8path(random_hex(8)); fs::create_directories(folder);
    try {
        auto archive=folder/fs::u8path(name); write_atomic(archive,response.body);
        wxFileInputStream file(archive.wstring()); wxZipInputStream zip(file);
        size_t entries=0,total=0;
        while(auto entry=std::unique_ptr<wxZipEntry>(zip.GetNextEntry())) {
            cancelled(cancel); if(++entries>1000) throw std::runtime_error("Update archive has too many entries");
            fs::path relative(entry->GetName().ToStdWstring());
            relative=relative.lexically_normal();
            if(relative.is_absolute()||relative.has_root_name()) throw std::runtime_error("Invalid archive path");
            for(const auto& part:relative) if(part==L".."||part.wstring().find(L':')!=std::wstring::npos) throw std::runtime_error("Invalid archive path");
            auto destination=folder/relative;
            if(entry->IsDir()) { fs::create_directories(destination); continue; }
            fs::create_directories(destination.parent_path());
            Bytes content; unsigned char buffer[65536];
            while(zip.CanRead()) { zip.Read(buffer,sizeof(buffer)); auto n=zip.LastRead(); if(!n) break; total+=n; if(total>512*1024*1024) throw std::runtime_error("Expanded update is too large"); content.insert(content.end(),buffer,buffer+n); }
            write_atomic(destination,content);
        }
        auto manifest=read_json(folder/"manifest.json");
        if(manifest.at("architecture")!="x64"||manifest.at("commit").get<std::string>().size()!=40||!fs::is_regular_file(folder/"GhostWaveStudio.exe")) throw std::runtime_error("Invalid portable package");
        return folder;
    } catch(...) { std::error_code error; fs::remove_all(folder,error); throw; }
}
std::string diagnostics() { return std::string("OS: Windows\nRuntime: Native C++17\nApp Version: ")+version+"\nMachine: x86_64"; }
Json create_ticket(Profile& profile,const std::string& title,const std::string& description,const std::string& category,bool include_diagnostics,Transport transport,const std::atomic_bool* cancel) {
    if(trim(title).empty()||trim(description).empty()) throw std::runtime_error("Title and description are required");
    profile.save();
    auto key=random_hex(16),body=trim(description);
    if(include_diagnostics) body+="\n\n### System Diagnostics\n```\n"+diagnostics()+"\n```";
    Json payload={{"title",trim(title)},{"description",body},{"category",trim(category).empty()?"Support":trim(category)},{"client_version",version},{"ticket_key",key}};
    auto result=ticket_response(transport({"POST",worker+"/api/tickets",payload.dump(),{{"Content-Type","application/json"}},15},cancel));
    if(!result.value("success",false)||!result.contains("ticket_id")) throw std::runtime_error("Invalid support response");
    result["ticket_key"]=key; result["category"]=category; result["title"]=trim(title);
    profile.data["user_tickets"].insert(profile.data["user_tickets"].begin(),result); profile.save(); return result;
}
Json fetch_ticket(const Json& ticket,Transport transport,const std::atomic_bool* cancel) {
    auto id=ticket.at("ticket_id").is_string()?ticket.at("ticket_id").get<std::string>():ticket.at("ticket_id").dump();
    return ticket_response(transport({"GET",worker+"/api/tickets?id="+url_encode(id)+"&key="+url_encode(ticket.at("ticket_key")),"",{},15},cancel));
}
Json reply_ticket(const Json& ticket,const std::string& message,Transport transport,const std::atomic_bool* cancel) {
    if(trim(message).empty()) throw std::runtime_error("Reply message is required");
    Json payload={{"ticket_id",ticket.at("ticket_id")},{"ticket_key",ticket.at("ticket_key")},{"message",trim(message)}};
    return ticket_response(transport({"POST",worker+"/api/tickets/reply",payload.dump(),{{"Content-Type","application/json"}},15},cancel));
}
}
