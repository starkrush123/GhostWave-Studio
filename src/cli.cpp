#include "ghostwave/core.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

namespace gw {
int command_line(const std::vector<std::string>& args,const std::function<int()>& launch_gui) {
    try {
        std::map<std::string,std::string> values={{"--format","wav"},{"--preset","max"},{"--pitch","2.5"},{"--tempo","0.94"},{"--bode-hz","8.5"},{"--duration","24"},{"--slice-duration","22"},{"--lyrics-mode","scramble"}};
        const std::set<std::string> valued={"--input","--output","--format","--preset","--pitch","--tempo","--bode-hz","--duration","--config-sn","--slice-duration","--lyrics","--lyrics-out","--lyrics-mode"};
        const std::set<std::string> switches={"--no-jitter","--no-bode","--no-decoy","--no-allpass","--no-room","--no-vocal-cut","--no-preamble","--trim","--no-trim","--audit-only","--slice","--no-adlibs","--check-update","--gui","--no-banner"};
        std::set<std::string> flags;
        for(size_t i=0;i<args.size();++i) {
            auto key=args[i]; if(key=="-i") key="--input"; if(key=="-o") key="--output";
            if(key=="--version"||key=="-v") { std::cout<<"GhostWave Studio v"<<version<<'\n'; return 0; }
            if(key=="--help"||key=="-h") {
                std::cout<<"GhostWave Studio "<<version<<"\nUsage: ghostwave-cli --input FILE --output FILE\n";
                for(const auto& name:valued) std::cout<<name<<" VALUE\n";
                for(const auto& name:switches) std::cout<<name<<'\n'; return 0;
            }
            auto equals=key.find('='); std::string argument;
            if(equals!=std::string::npos) { argument=key.substr(equals+1); key=key.substr(0,equals); }
            if(valued.count(key)) {
                if(equals==std::string::npos) { if(++i==args.size()) throw std::runtime_error("Option requires a value: "+key); argument=args[i]; }
                values[key]=argument;
            } else if(switches.count(key)&&equals==std::string::npos) flags.insert(key);
            else throw std::runtime_error("Unknown option: "+key);
        }
        auto choice=[&](const std::string& key,const std::set<std::string>& choices) { if(!choices.count(values[key])) throw std::runtime_error("Invalid value for "+key); };
        choice("--format",{"wav","mp3"}); choice("--preset",{"max","balanced","cloud"}); choice("--lyrics-mode",{"scramble","hybrid","phonetic","semantic","cloud"});
        auto numeric=[&](const std::string& key) { size_t end=0; double number=std::stod(values[key],&end); if(end!=values[key].size()||!std::isfinite(number)) throw std::runtime_error("Invalid number for "+key); return number; };
        double pitch=numeric("--pitch"),tempo=numeric("--tempo"),bode=numeric("--bode-hz"),duration=numeric("--duration"),slice=numeric("--slice-duration");
        if(tempo<=0||tempo>4||std::abs(pitch)>48||duration<0||duration>3600||std::abs(bode)>20000||slice<0||slice>3600) throw std::runtime_error("Audio parameters exceed supported limits");
        if(flags.count("--check-update")) { std::cout<<check_update().dump(2)<<'\n'; return 0; }
        if(flags.count("--gui")||(!values.count("--input")&&!values.count("--lyrics"))) return launch_gui();
        auto profile=Profile::load(values.count("--config-sn")?fs::u8path(values["--config-sn"]):fs::path());
        if(values.count("--lyrics")) {
            auto bytes=read_bytes(fs::u8path(values["--lyrics"]),16*1024*1024);
            auto lyrics_profile=profile;
            for(const auto* key:{"filter_profanity","strip_celebrities","normalize_unicode","format_tags","check_limits"}) lyrics_profile.data[key]=true;
            lyrics_profile.data["enable_lyrics_cloaking"]=true;
            lyrics_profile.data["lyrics_cloak_mode"]=values["--lyrics-mode"];
            lyrics_profile.data["lyrics_add_vibrato_glides"]=true;
            lyrics_profile.data["lyrics_preserve_syllables"]=true;
            lyrics_profile.data["lyrics_break_ngrams"]=!flags.count("--no-adlibs");
            auto lyrics=process_lyrics(std::string(bytes.begin(),bytes.end()),lyrics_profile);
            if(values.count("--lyrics-out")) write_atomic(fs::u8path(values["--lyrics-out"]),Bytes(lyrics.text.begin(),lyrics.text.end()));
            else std::cout<<lyrics.text<<'\n';
            if(!values.count("--input")) return 0;
        }
        auto input=fs::absolute(fs::u8path(values["--input"]));
        auto output=values.count("--output")?fs::absolute(fs::u8path(values["--output"])):input.parent_path()/fs::u8path(input.stem().u8string()+"_ghostwave_ready."+values["--format"]);
        if(flags.count("--slice")) {
            auto directory=values.count("--output")?output:input.parent_path()/"ghostwave_chunks";
            for(const auto& part:slice_audio(input,directory,slice)) std::cout<<part.u8string()<<'\n'; return 0;
        }
        if(flags.count("--audit-only")) { std::cout<<audit_audio(input,output).dump(2)<<'\n'; return 0; }
        AudioOptions options;
        auto& cfg=options.values;
        cfg["mode"]=values["--preset"]=="cloud"?"cloud_api":"master_evasion";
        cfg["pitch_shift_semitones"]=pitch; cfg["tempo_factor"]=tempo; cfg["bode_shift_hz"]=bode;
        for(const auto& pair:{std::pair{"--no-jitter","enable_micro_chrono_jitter"},{"--no-bode","enable_bode_freq_shifter"},{"--no-decoy","enable_adversarial_peaks"},{"--no-allpass","enable_phase_dispersion"},{"--no-room","enable_reamping_room"},{"--no-vocal-cut","enable_vocal_cut"},{"--no-vocal-cut","enable_formant_scrambler"},{"--no-preamble","inject_preamble"}}) cfg[pair.second]=!flags.count(pair.first);
        cfg["trim_duration"]=flags.count("--trim")&&!flags.count("--no-trim"); cfg["max_duration_seconds"]=duration;
        cfg["strip_metadata"]=true; cfg["output_bitrate"]=values["--format"]=="mp3"?"320k":"wav";
        if(!flags.count("--no-banner")) std::cout<<"GhostWave Studio "<<version<<"\n";
        auto result=process_audio(input,output,options,profile,[](int pct,const std::string& message){std::cout<<pct<<"% "<<message<<'\n';});
        if(!result.success) throw std::runtime_error(result.error);
        std::cout<<output.u8string()<<'\n'<<result.audit.dump(2)<<'\n'; return 0;
    } catch(const std::exception& error) { std::cerr<<"Error: "<<error.what()<<'\n'; return 1; }
}
}
