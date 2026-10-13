#include "ghostwave/core.hpp"
#include <windows.h>
#include <wx/init.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace gw;
namespace {
Json results=Json::array();
fs::path evidence;
void require(bool ok,const std::string& message) { if(!ok) throw std::runtime_error(message); }
template<class F> void test(const std::string& name,F action) {
    try { action(); results.push_back({{"name",name},{"passed",true}}); }
    catch(const std::exception& error) { results.push_back({{"name",name},{"passed",false},{"error",error.what()}}); std::cerr<<name<<": "<<error.what()<<'\n'; }
}
template<class F> void rejects(F action) { bool rejected=false; try { action(); } catch(const std::exception&) { rejected=true; } require(rejected,"Invalid input was accepted"); }
template<class T> Bytes bytes(const std::vector<T>& values) { Bytes out(values.size()*sizeof(T)); if(!out.empty()) std::memcpy(out.data(),values.data(),out.size()); return out; }
void equal_bytes(const std::string& name,const Bytes& actual,const Bytes& expected) {
    if(actual==expected) return;
    auto end=std::min(actual.size(),expected.size()); size_t first=0,different=0;
    while(first<end&&actual[first]==expected[first]) ++first;
    for(size_t i=0;i<end;++i) different+=actual[i]!=expected[i];
    write_atomic(evidence/fs::u8path(name+".actual.bin"),actual);
    throw std::runtime_error("Byte mismatch at "+std::to_string(first)+", differing bytes "+std::to_string(different)+", sizes "+std::to_string(actual.size())+" and "+std::to_string(expected.size()));
}
HttpResponse response(const Json& json,unsigned status=200) { auto text=json.dump(); return {status,Bytes(text.begin(),text.end()),"application/json"}; }
Transport offline=[](const HttpRequest&,const std::atomic_bool*)->HttpResponse { throw std::runtime_error("Network is disabled in tests"); };
}
int main(int argc,char** argv) {
    wxInitializer init; if(!init||argc!=2) return 2;
    auto ref=fs::absolute(fs::u8path(argv[1])); evidence=ref.parent_path()/"native"; fs::create_directories(evidence);
    auto home=evidence/"home"; fs::create_directories(home); SetEnvironmentVariableW(L"USERPROFILE",home.c_str());
    fs::current_path(evidence);
    Profile defaults; defaults.path=home/"test.sn";
    test("profile compatibility",[&] {
        auto original=read_bytes(ref/"legacy-profile.sn"); auto profile=Profile::decode(original);
        require(profile.data==read_json(ref/"legacy-profile.json"),"Legacy profile fields changed");
        auto native=profile.encode(); require(Profile::decode(native).data==profile.data,"Native profile roundtrip failed");
        write_atomic(evidence/"native-profile.sn",native);
        require(std::string(native.begin(),native.begin()+16)==std::string("GHOSTWAVE_SN_V1\0",16),"Profile header changed");
        require(std::search(native.begin(),native.end(),std::begin("compatibility_secret"),std::end("compatibility_secret")-1)==native.end(),"Plaintext token leaked");
        auto tampered=native; tampered[40]^=1; rejects([&]{Profile::decode(tampered);});
        tampered=native; tampered[0]^=1; rejects([&]{Profile::decode(tampered);});
        profile.path=home/"roundtrip.sn"; profile.save(); profile.data["language"]="en"; profile.save();
        require(Profile::load(profile.path).data==profile.data,"Saved profile differs");
        write_atomic(profile.path,Bytes{1,2,3}); auto recovered=Profile::load(profile.path);
        require(recovered.recovered,"Backup was not recovered"); rejects([&]{recovered.save();});
        require(read_bytes(profile.path)==Bytes({1,2,3}),"Corrupt original was overwritten"); recovered.save(home/"recovered.sn");
    });
    test("random state compatibility",[&] {
        RandomState random(0); random.restore(read_json(ref/"random-state.json")); std::vector<double> values(10001);
        for(auto& value:values) value=random.normal(); equal_bytes("normal",bytes(values),read_bytes(ref/"normal.bin"));
        require(random.state()==read_json(ref/"normal-state.json"),"Gaussian cache or MT state differs");
        for(auto& value:values) value=-.2+.5*random.uniform(); equal_bytes("uniform",bytes(values),read_bytes(ref/"uniform.bin"));
        require(random.state()==read_json(ref/"uniform-state.json"),"Uniform MT state differs");
    });
    for(const auto& item:read_json(ref/"audio-cases.json")) {
        auto name=item.at("name").get<std::string>();
        test("audio "+name,[&,item,name] {
            auto stage=item.at("stage").get<std::string>(); Audio data;
            if(stage=="load") data=read_audio(ref/fs::u8path(item.at("input").get<std::string>()));
            else {
                auto raw=read_bytes(ref/fs::u8path(item.at("input").get<std::string>())); require(raw.size()%sizeof(float)==0,"Invalid input fixture");
                data.rate=item.at("rate"); data.channels=item.at("channels"); data.samples.resize(raw.size()/sizeof(float)); std::memcpy(data.samples.data(),raw.data(),raw.size());
                RandomState random(0); random.restore(item.at("random_state"));
                data=audio_stage(stage,std::move(data),item.at("options"),random);
                require(random.state()==item.at("expected_random_state"),"Audio random consumption differs");
            }
            require(data.rate==item.at("rate")&&data.channels==item.at("channels"),"Audio format differs");
            equal_bytes(name,bytes(data.samples),read_bytes(ref/fs::u8path(item.at("output").get<std::string>())));
        });
    }
    size_t lyric_index=0;
    for(const auto& item:read_json(ref/"lyrics-cases.json")) {
        auto name="lyrics "+std::to_string(lyric_index++);
        test(name,[&,item] {
            auto profile=defaults; profile.merge(item.at("profile"));
            auto result=process_lyrics(item.at("input"),profile,nullptr,offline);
            require(result.text==item.at("output"),"Lyrics differ: "+result.text.substr(0,180)+" | expected "+item.at("output").get<std::string>().substr(0,180));
            require(result.stats==item.at("stats"),"Lyrics statistics differ: "+result.stats.dump()+" | expected "+item.at("stats").dump());
            if(!item.at("audit").is_null()) for(auto it=item.at("audit").begin();it!=item.at("audit").end();++it) require(result.audit.at(it.key())==it.value(),"Lyrics audit differs: "+it.key());
        });
    }
    for(const auto& item:read_json(ref/"syllables.json")) test("syllables "+item.at("input").get<std::string>(),[item] {
        require(syllabify(item.at("input"))==item.at("syllabify"),"Syllabification differs"); require(count_syllables(item.at("input"))==item.at("count"),"Syllable count differs");
    });
    for(const auto& item:read_json(ref/"pipeline.json")) {
        auto name=item.at("output").get<std::string>();
        test("export "+name,[&,item,name] {
            AudioOptions options; options.values=item.at("options"); RandomState random(0); random.restore(item.at("random_state"));
            auto output=evidence/fs::u8path(name);
            auto result=process_audio(ref/fs::u8path(item.at("input").get<std::string>()),output,options,defaults,{},nullptr,&random,offline);
            require(result.success,result.error); require(random.state()==item.at("expected_random_state"),"Pipeline random consumption differs");
            equal_bytes(name,read_bytes(output),read_bytes(ref/fs::u8path(name)));
        });
    }
    test("slicing compatibility",[&] {
        auto parts=slice_audio(ref/"slice-source.wav",evidence/"slices",3.5); auto names=read_json(ref/"slices.json"); require(parts.size()==names.size(),"Slice count differs");
        for(size_t i=0;i<parts.size();++i) { require(parts[i].filename().u8string()==names[i],"Slice filename differs"); equal_bytes("slice"+std::to_string(i),read_bytes(parts[i]),read_bytes(ref/"slices"/fs::u8path(names[i].get<std::string>()))); }
    });
    test("loud remainder compatibility",[&] {
        auto parts=slice_audio(ref/"slice-loud.wav",evidence/"slices-loud",3.5); auto names=read_json(ref/"slices-loud.json"); require(parts.size()==names.size(),"Loud slice count differs");
        for(size_t i=0;i<parts.size();++i) equal_bytes("slice-loud"+std::to_string(i),read_bytes(parts[i]),read_bytes(ref/"slices-loud"/fs::u8path(names[i].get<std::string>())));
    });
    test("audio boundaries and cancellation",[&] {
        auto empty=evidence/"empty.wav"; write_atomic(empty,{}); rejects([&]{read_audio(empty);});
        Audio data{44100,1,{2.0f,-2.0f,0.0f}}; RandomState random(1); auto limited=audio_stage("limit",data,AudioOptions().values,random);
        for(float value:limited.samples) require(std::isfinite(value)&&std::abs(value)<1,"Limiter failed");
        auto clean=data; clean.samples={.1f,.2f,.3f}; auto options=AudioOptions().values; options["jitter_intensity_ms"]=0;
        require(audio_stage("jitter",clean,options,random).samples==clean.samples,"Zero jitter changed samples");
        std::atomic_bool cancel{true}; auto target=evidence/"preserved.wav"; write_atomic(target,Bytes{1,2,3,4});
        auto result=process_audio(ref/"mono.wav",target,AudioOptions(),defaults,{},&cancel,&random,offline);
        require(!result.success&&read_bytes(target)==Bytes({1,2,3,4}),"Cancellation destroyed output");
        auto failed_target=evidence/"preserved.mp3"; write_atomic(failed_target,Bytes{4,3,2,1});
        rejects([&]{export_audio(failed_target,clean,"invalid",true);});
        require(read_bytes(failed_target)==Bytes({4,3,2,1}),"Failed export destroyed output");
        rejects([&]{slice_audio(ref/"mono.wav",evidence/"bad",NAN);});
    });
    test("CLI validation",[&] {
        bool gui=false; auto launch=[&]{gui=true;return 0;};
        require(command_line({"--version"},launch)==0&&!gui,"Version launched GUI");
        require(command_line({"--tempo","nan"},launch)!=0,"NaN accepted");
        require(command_line({"--unknown"},launch)!=0,"Unknown option accepted");
        require(command_line({"--pitch"},launch)!=0,"Missing argument accepted");
    });
    test("updater and changelog",[&] {
        require(normalize_version("v1.6.2-beta")==std::vector<int>({1,6,2}),"Version parser differs");
        require(normalize_version("2.0")>normalize_version("1.9.99"),"Version comparison failed");
        require(parse_changelog("Version: 2.0.0\nDate: 2026-10-13\nWhat's new:\nNative application")["version"]=="2.0.0","Changelog parser failed");
        Json release={{"draft",true},{"prerelease",false},{"published_at","2026-10-13"},{"tag_name","v3.0.0"},{"html_url","https://github.com/starkrush123/GhostWave-Studio/releases"},{"assets",Json::array()}};
        require(!check_update([&](const HttpRequest& request,const std::atomic_bool*) { require(request.url.find(repository)!=std::string::npos,"Wrong updater origin");return response(release); })["has_update"].get<bool>(),"Draft accepted as update");
        release["draft"]=false; release["prerelease"]=true; require(!check_update([&](const HttpRequest&,const std::atomic_bool*){return response(release);})["has_update"].get<bool>(),"Prerelease accepted");
        require(!check_update(offline)["error_message"].get<std::string>().empty(),"Update error hidden");
    });
    test("cloud and support contracts",[&] {
        auto profile=defaults; profile.path=home/"support.sn"; profile.data["cloud_provider"]="custom"; profile.data["cloud_endpoint_url"]="https://example.invalid/audio";
        auto source=read_bytes(ref/"mono.wav");
        auto separated=cloud_stems(ref/"mono.wav",profile,{},nullptr,[&](const HttpRequest& request,const std::atomic_bool*) {
            require(request.method=="POST"&&request.body.find("name=\"audio\"")!=std::string::npos,"Multipart contract changed"); return HttpResponse{200,source,"audio/wav"};
        }); require(separated.frames()==44117,"Cloud audio format changed");
        auto ticket=create_ticket(profile,"Test title","Test description","Support",true,[&](const HttpRequest& request,const std::atomic_bool*) {
            auto body=Json::parse(request.body); require(body.at("ticket_key").get<std::string>().size()==32,"Ticket credential invalid"); require(body.at("description").get<std::string>().find("Native C++17")!=std::string::npos,"Diagnostics missing"); return response({{"success",true},{"ticket_id","fixture"}},201);
        }); require(profile.data["user_tickets"][0]==ticket,"Ticket not saved");
        fetch_ticket(ticket,[&](const HttpRequest& request,const std::atomic_bool*) { require(request.url.find("id=fixture&key=")!=std::string::npos,"Ticket authentication changed");return response({{"success",true},{"messages",Json::array()}}); });
        reply_ticket(ticket,"Hello",[&](const HttpRequest& request,const std::atomic_bool*) { require(Json::parse(request.body)["message"]=="Hello","Reply changed");return response({{"success",true}}); });
        rejects([&]{create_ticket(profile,"","Body","Support",false,offline);}); rejects([&]{reply_ticket(ticket,"",offline);});
        require(diagnostics().find("compatibility_secret")==std::string::npos,"Diagnostics exposed a credential");
    });
    size_t failed=0; for(const auto& result:results) failed+=!result.at("passed").get<bool>();
    Json report={{"tests",results.size()},{"failures",failed},{"byte_parity_required",true},{"results",results}};
    auto encoded=report.dump(2); write_atomic(ref.parent_path()/"native-results.json",Bytes(encoded.begin(),encoded.end()));
    auto smoke=defaults; smoke.path=home/"smoke.sn"; smoke.data["first_run"]=false; smoke.data["check_updates_on_startup"]=false; smoke.data["language"]="en"; smoke.save();
    std::cout<<results.size()<<" checks, "<<failed<<" failures\n"; return failed?1:0;
}
