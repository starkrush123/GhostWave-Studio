#include "ghostwave/core.hpp"
#include <windows.h>
#include <sndfile.h>
#include <pocketfft_hdronly.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstring>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace gw {
namespace {
constexpr double pi = 3.141592653589793238462643383279502884;
struct SoundFileApi {
    HMODULE library = nullptr;
    decltype(&sf_wchar_open) open = nullptr;
    decltype(&sf_close) close = nullptr;
    decltype(&sf_readf_float) read = nullptr;
    decltype(&sf_writef_float) write = nullptr;
    decltype(&sf_strerror) error = nullptr;
    decltype(&sf_version_string) version_string = nullptr;
    SoundFileApi() {
        library = LoadLibraryExW((app_dir()/"libsndfile_x64.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!library) throw std::runtime_error("Bundled audio library is missing");
        open = reinterpret_cast<decltype(open)>(GetProcAddress(library,"sf_wchar_open"));
        close = reinterpret_cast<decltype(close)>(GetProcAddress(library,"sf_close"));
        read = reinterpret_cast<decltype(read)>(GetProcAddress(library,"sf_readf_float"));
        write = reinterpret_cast<decltype(write)>(GetProcAddress(library,"sf_writef_float"));
        error = reinterpret_cast<decltype(error)>(GetProcAddress(library,"sf_strerror"));
        version_string = reinterpret_cast<decltype(version_string)>(GetProcAddress(library,"sf_version_string"));
        if(!open||!close||!read||!write||!error||!version_string) { FreeLibrary(library); throw std::runtime_error("Audio library exports are incomplete"); }
    }
    ~SoundFileApi() { FreeLibrary(library); }
};
SoundFileApi& sound() { static SoundFileApi api; return api; }
struct SoundFile {
    SNDFILE* file = nullptr;
    SoundFile(const fs::path& path,int mode,SF_INFO& info) { file = sound().open(path.c_str(),mode,&info); if(!file) throw std::runtime_error(std::string("Cannot open audio: ")+sound().error(nullptr)); }
    ~SoundFile() { if(file) sound().close(file); }
};
template<typename T> std::vector<std::complex<T>> rfft(const std::vector<T>& data,size_t n,int channels) {
    std::vector<std::complex<T>> out((n/2+1)*channels);
    pocketfft::r2c<T>({n,static_cast<size_t>(channels)},{static_cast<ptrdiff_t>(sizeof(T)*channels),sizeof(T)},
        {static_cast<ptrdiff_t>(sizeof(std::complex<T>)*channels),sizeof(std::complex<T>)},0,true,data.data(),out.data(),T(1),1);
    return out;
}
template<typename T> std::vector<T> irfft(const std::vector<std::complex<T>>& data,size_t n,int channels) {
    std::vector<T> out(n*channels);
    pocketfft::c2r<T>({n,static_cast<size_t>(channels)},{static_cast<ptrdiff_t>(sizeof(std::complex<T>)*channels),sizeof(std::complex<T>)},
        {static_cast<ptrdiff_t>(sizeof(T)*channels),sizeof(T)},0,false,data.data(),out.data(),T(1)/T(n),1);
    return out;
}
size_t fast_length(size_t n,bool real=false) { return real ? pocketfft::detail::util::good_size_real(n) : pocketfft::detail::util::good_size_cmplx(n); }
template<typename T> std::vector<std::complex<T>> analytic(std::vector<T> data,size_t frames,int channels) {
    const auto n = fast_length(frames);
    std::vector<std::complex<T>> padded(n*channels), transformed(n*channels);
    for(size_t i=0;i<data.size();++i) padded[i] = data[i];
    pocketfft::c2c<T>({n,static_cast<size_t>(channels)},{static_cast<ptrdiff_t>(sizeof(std::complex<T>)*channels),sizeof(std::complex<T>)},
        {static_cast<ptrdiff_t>(sizeof(std::complex<T>)*channels),sizeof(std::complex<T>)},{0},true,padded.data(),transformed.data(),T(1),1);
    for(size_t i=0;i<n;++i) {
        T factor = (i==0 || (n%2==0 && i==n/2)) ? T(1) : (i<(n+1)/2 ? T(2) : T(0));
        for(int c=0;c<channels;++c) transformed[i*channels+c] *= factor;
    }
    pocketfft::c2c<T>({n,static_cast<size_t>(channels)},{static_cast<ptrdiff_t>(sizeof(std::complex<T>)*channels),sizeof(std::complex<T>)},
        {static_cast<ptrdiff_t>(sizeof(std::complex<T>)*channels),sizeof(std::complex<T>)},{0},false,transformed.data(),padded.data(),T(1)/T(n),1);
    padded.resize(frames*channels); return padded;
}
Audio resample(Audio data,size_t frames) {
    if(frames==data.frames()) return data;
    if(!frames||!data.frames()) throw std::runtime_error("Resampling produced empty audio");
    const size_t old = data.frames();
    auto source=rfft(data.samples,old,data.channels);
    std::vector<std::complex<float>> target((frames/2+1)*data.channels);
    const auto n=std::min(old,frames), count=n/2+1;
    std::copy_n(source.begin(),count*data.channels,target.begin());
    if(n%2==0) for(int c=0;c<data.channels;++c) target[(n/2)*data.channels+c] *= frames<old ? 2.0f : 0.5f;
    data.samples=irfft(target,frames,data.channels);
    float ratio=static_cast<float>(static_cast<double>(frames)/old);
    for(auto& value:data.samples) value*=ratio;
    return data;
}
float pairwise_sum(const float* data,size_t n) {
    if(n<8) { float sum=-0.0f; for(size_t i=0;i<n;++i) sum+=data[i]; return sum; }
    if(n<=128) {
        float accum[8]; std::copy_n(data,8,accum);
        size_t i=8;
        for(;i+7<n;i+=8) for(size_t k=0;k<8;++k) accum[k]+=data[i+k];
        float sum=((accum[0]+accum[1])+(accum[2]+accum[3]))+((accum[4]+accum[5])+(accum[6]+accum[7]));
        for(;i<n;++i) sum+=data[i];
        return sum;
    }
    auto half=(n/2)/8*8; return pairwise_sum(data,half)+pairwise_sum(data+half,n-half);
}
float time32(size_t i,size_t n,double end,bool endpoint=false,double start=0) {
    const double step=(end-start)/(endpoint ? n-1 : n);
    return static_cast<float>(static_cast<double>(i)*step+start);
}
std::vector<double> asdouble(const Audio& data) { return {data.samples.begin(),data.samples.end()}; }
std::vector<double> filter(std::vector<double> data,int channels,const Json& sos) {
    for(const auto& row:sos) {
        const auto coefficients=row.get<std::vector<double>>();
        if(coefficients.size()!=6 || coefficients[3]!=1.0) throw std::runtime_error("Invalid filter coefficients");
        for(int c=0;c<channels;++c) {
            double z1=0,z2=0;
            for(size_t i=c;i<data.size();i+=channels) {
                double x=data[i],y=coefficients[0]*x+z1;
                z1=coefficients[1]*x-coefficients[4]*y+z2;
                z2=coefficients[2]*x-coefficients[5]*y;
                data[i]=y;
            }
        }
    }
    return data;
}
const Json& bank(int rate) {
    static const auto coefficients=read_json(app_dir()/"resources/filter-bank.json");
    auto key=std::to_string(rate);
    if(!coefficients.contains(key)) throw std::runtime_error("Unsupported direct DSP sample rate");
    return coefficients.at(key);
}
void assign(Audio& data,const std::vector<double>& values) { data.samples.assign(values.begin(),values.end()); }
Audio limit(Audio data,float threshold) {
    float peak=0; for(auto value:data.samples) peak=std::max(peak,std::abs(value));
    if(peak>threshold) for(auto& value:data.samples) value=std::tanh(value/threshold)*threshold;
    return data;
}
std::string number(double value,int precision) { std::ostringstream out; out<<std::fixed<<std::setprecision(precision)<<value; return out.str(); }
Audio pitch_fallback(const Audio& input,double pitch,double tempo,const std::atomic_bool* cancel) {
    const int nfft=2048,hop=512;
    double factor=1.0/(std::pow(2.0,pitch/12.0)*tempo);
    int hop_a=std::max(1,static_cast<int>(std::nearbyint(hop/factor)));
    const auto frame_count=(static_cast<ptrdiff_t>(input.frames())-nfft)/hop_a;
    const auto target=static_cast<size_t>(std::nearbyint(input.frames()/tempo));
    if(frame_count<=0) return resample(input,target);
    Audio stretched; stretched.channels=input.channels; stretched.rate=input.rate;
    const size_t length=static_cast<size_t>(input.frames()*factor)+nfft;
    stretched.samples.resize(length*input.channels);
    std::vector<float> window(nfft);
    for(int i=0;i<nfft;++i) window[i]=static_cast<float>(0.5-0.5*std::cos(2*pi*i/(nfft-1)));
    for(int c=0;c<input.channels;++c) {
        std::vector<float> out(length),norm(length),phase_acc(nfft/2+1),previous(nfft/2+1);
        for(ptrdiff_t frame=0;frame<frame_count;++frame) {
            cancelled(cancel);
            std::vector<double> segment(nfft);
            for(int i=0;i<nfft;++i) segment[i]=input.samples[(frame*hop_a+i)*input.channels+c]*window[i];
            auto spectrum=rfft(segment,nfft,1);
            for(int k=0;k<=nfft/2;++k) {
                const double phase=std::arg(spectrum[k]),advance=2*pi*hop_a*k/nfft;
                double delta=std::fmod(phase-previous[k]-advance+pi,2*pi); if(delta<0) delta+=2*pi; delta-=pi;
                phase_acc[k]=static_cast<float>(phase_acc[k]+(advance+delta)*(static_cast<double>(hop)/hop_a));
                previous[k]=static_cast<float>(phase);
                spectrum[k]=std::abs(spectrum[k])*std::complex<double>(std::cos(phase_acc[k]),std::sin(phase_acc[k]));
            }
            auto inverse=irfft(spectrum,nfft,1);
            for(int i=0;i<nfft && static_cast<size_t>(frame*hop+i)<length;++i) {
                size_t index=frame*hop+i;
                out[index]=static_cast<float>(out[index]+inverse[i]*window[i]);
                norm[index]+=window[i]*window[i];
            }
        }
        for(size_t i=0;i<length;++i) stretched.samples[i*input.channels+c]=norm[i]>1e-4f ? out[i]/norm[i] : out[i];
    }
    return resample(stretched,target);
}
Audio pitch(Audio data,const Json& options,bool wobble,const std::atomic_bool* cancel) {
    double semitones=options.value("pitch_shift_semitones",2.5),tempo=options.value("tempo_factor",0.94);
    if(tempo<=0 || !std::isfinite(tempo) || !std::isfinite(semitones)) throw std::runtime_error("Invalid pitch or tempo");
    try {
        TempFile in(".wav"),out(".wav"); write_pcm(in.path,data);
        std::string filters="rubberband=pitch="+number(std::pow(2.0,semitones/12.0),6)+":tempo="+number(tempo,6)+":formant=shifted";
        if(wobble) filters+=",vibrato=f="+number(options.value("pitch_wobble_freq",0.22),3)+":d="+number(options.value("pitch_wobble_depth",0.28),3);
        auto result=run_process({ffmpeg_path().wstring(),L"-y",L"-i",in.path.wstring(),L"-af",wide(filters),L"-ar",std::to_wstring(data.rate),out.path.wstring()},60,cancel);
        if(result.code==0 && fs::exists(out.path)) return read_audio(out.path,0,false);
    } catch(...) { cancelled(cancel); }
    return pitch_fallback(data,semitones,tempo,cancel);
}
std::vector<uint32_t> fingerprint(const fs::path& path,const std::atomic_bool* cancel) {
    try {
        auto result=run_process({ffmpeg_path().wstring(),L"-y",L"-i",path.wstring(),L"-fp_format",L"raw",L"-f",L"chromaprint",L"-"},12,cancel);
        if(result.code || result.out.size()<8) return {};
        std::vector<uint32_t> values(result.out.size()/4); std::memcpy(values.data(),result.out.data(),values.size()*4); return values;
    } catch(...) { cancelled(cancel); return {}; }
}
}
RandomState::RandomState(uint32_t seed) {
    key[0]=seed;
    for(uint32_t i=1;i<624;++i) key[i]=1812433253U*(key[i-1]^(key[i-1]>>30))+i;
}
uint32_t RandomState::next() {
    if(position>=624) {
        for(size_t i=0;i<624;++i) { uint32_t value=(key[i]&0x80000000U)|(key[(i+1)%624]&0x7fffffffU); key[i]=key[(i+397)%624]^(value>>1)^((value&1)?0x9908b0dfU:0); }
        position=0;
    }
    uint32_t value=key[position++]; value^=value>>11; value^=(value<<7)&0x9d2c5680U; value^=(value<<15)&0xefc60000U; value^=value>>18; return value;
}
double RandomState::uniform() { uint32_t a=next()>>5,b=next()>>6; return (a*67108864.0+b)*(1.0/9007199254740992.0); }
double RandomState::normal() {
    if(has_gaussian) { has_gaussian=false; return gaussian; }
    double x1,x2,r;
    do { x1=2.0*uniform()-1.0; x2=2.0*uniform()-1.0; r=x1*x1+x2*x2; } while(r>=1.0||r==0.0);
    double scale=std::sqrt(-2.0*std::log(r)/r); gaussian=scale*x1; has_gaussian=true; return scale*x2;
}
void RandomState::restore(const Json& state) {
    if(state.at("algorithm")!="MT19937" || state.at("key").size()!=624) throw std::runtime_error("Invalid random state");
    for(size_t i=0;i<624;++i) key[i]=state.at("key").at(i);
    position=state.at("position"); if(position>624) throw std::runtime_error("Invalid random position");
    has_gaussian=state.at("has_gaussian"); gaussian=state.at("gaussian");
}
Json RandomState::state() const { return {{"algorithm","MT19937"},{"key",std::vector<uint32_t>(key,key+624)},{"position",position},{"has_gaussian",has_gaussian},{"gaussian",gaussian}}; }
AudioOptions::AudioOptions() : values(read_json(app_dir()/"resources/audio-defaults.json")) {}
Audio resample_audio(Audio data,int rate) {
    if(rate<=0||data.rate<=0) throw std::runtime_error("Invalid sample rate");
    if(rate!=data.rate) data=resample(data,static_cast<size_t>(std::nearbyint(data.frames()*static_cast<double>(rate)/data.rate)));
    data.rate=rate; return data;
}
AudioOptions AudioOptions::preset(int index,const Profile& profile) {
    if(index<0||index>5) throw std::runtime_error("Invalid preset");
    AudioOptions options;
    for(const auto& name:{"pitch_shift_semitones","tempo_factor","bode_shift_hz","enable_micro_chrono_jitter","enable_bode_freq_shifter","enable_adversarial_peaks","enable_phase_dispersion","enable_reamping_room","enable_vocal_cut","enable_formant_scrambler","inject_preamble","apply_eq_filters","apply_dither","trim_duration","max_duration_seconds","strip_metadata"}) options.values[name]=profile.data.at(name);
    options.values["mode"]=index==5 ? "cloud_api" : "master_evasion";
    options.values["output_bitrate"]=profile.data["export_format"];
    return options;
}
Audio read_audio(const fs::path& input,int target_rate,bool remove_dc) {
    static thread_local bool decoding=false;
    if(!fs::is_regular_file(input)||!fs::file_size(input)) throw std::runtime_error("Audio file is missing or empty");
    Audio result;
    sound();
    try {
        SF_INFO info{}; SoundFile file(input,SFM_READ,info);
        if(info.frames<=0||info.samplerate<=0||info.channels<=0||info.channels>32 || static_cast<uint64_t>(info.frames)*info.channels>512*1024*1024/sizeof(float)) throw std::runtime_error("Invalid or oversized audio stream");
        result.rate=info.samplerate; result.channels=info.channels; result.samples.resize(static_cast<size_t>(info.frames)*info.channels);
        auto frames=sound().read(file.file,result.samples.data(),info.frames);
        if(frames!=info.frames) throw std::runtime_error("Incomplete audio stream");
    } catch(...) {
        if(decoding) throw;
        TempFile decoded(".wav");
        auto process=run_process({ffmpeg_path().wstring(),L"-y",L"-i",input.wstring(),L"-c:a",L"pcm_s32le",decoded.path.wstring()});
        if(process.code) throw std::runtime_error("Cannot decode audio");
        decoding=true;
        try { result=read_audio(decoded.path,0,false); } catch(...) { decoding=false; throw; }
        decoding=false;
    }
    if(target_rate>0 && target_rate!=result.rate) { result=resample(result,static_cast<size_t>(std::nearbyint(result.frames()*static_cast<double>(target_rate)/result.rate))); result.rate=target_rate; }
    for(auto& sample:result.samples) if(!std::isfinite(sample)) sample=std::isnan(sample)?0.0f:std::signbit(sample)?-1.0f:1.0f;
    if(remove_dc && result.frames()) {
        for(int c=0;c<result.channels;++c) {
            float sum=0;
            if(result.channels==1) sum=pairwise_sum(result.samples.data(),result.frames());
            else for(size_t i=c;i<result.samples.size();i+=result.channels) sum+=result.samples[i];
            float mean=sum/static_cast<float>(result.frames());
            for(size_t i=c;i<result.samples.size();i+=result.channels) result.samples[i]-=mean;
        }
    }
    return result;
}
void write_pcm(const fs::path& path,const Audio& data) {
    if(!data.frames()||data.channels<1||data.rate<1) throw std::runtime_error("Cannot write empty audio");
    SF_INFO info{}; info.samplerate=data.rate; info.channels=data.channels; info.format=SF_FORMAT_WAV|SF_FORMAT_PCM_16;
    SoundFile file(path,SFM_WRITE,info);
    if(sound().write(file.file,data.samples.data(),data.frames())!=static_cast<sf_count_t>(data.frames())) throw std::runtime_error("Incomplete audio write");
    if(sound().close(file.file)!=0) { file.file=nullptr; throw std::runtime_error("Cannot finish audio file"); } file.file=nullptr;
}
void export_audio(const fs::path& path,Audio data,const std::string& bitrate,bool strip_metadata,const std::atomic_bool* cancel) {
    cancelled(cancel);
    auto target=fs::absolute(path); fs::create_directories(target.parent_path());
    data=limit(std::move(data),0.98f);
    for(auto& sample:data.samples) sample=std::clamp(sample,-1.0f,1.0f);
    TempFile temporary(".wav",target.parent_path()),encoded(target.extension().u8string(),target.parent_path());
    write_pcm(temporary.path,data);
    std::vector<std::wstring> command{ffmpeg_path().wstring(),L"-y",L"-i",temporary.path.wstring(),L"-threads",L"4"};
    auto ext=target.extension().u8string(); std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(ext==".mp3") { std::string rate=bitrate; if(rate.empty()||rate=="wav") rate="320k"; else if(rate.back()!='k') rate+='k'; command.insert(command.end(),{L"-c:a",L"libmp3lame",L"-b:a",wide(rate),L"-ar",std::to_wstring(data.rate),L"-preset",L"veryfast"}); }
    else if(ext==".wav") command.insert(command.end(),{L"-c:a",L"pcm_s16le",L"-ar",std::to_wstring(data.rate)});
    else if(ext==".flac") command.insert(command.end(),{L"-c:a",L"flac"});
    if(strip_metadata) command.insert(command.end(),{L"-map_metadata",L"-1",L"-id3v2_version",L"0",L"-write_xing",L"0",L"-fflags",L"+bitexact"});
    command.push_back(encoded.path.wstring());
    auto result=run_process(command,60,cancel);
    if(result.code || !fs::is_regular_file(encoded.path) || !fs::file_size(encoded.path)) throw std::runtime_error("Audio export failed: "+result.error);
    cancelled(cancel);
    if(!MoveFileExW(encoded.path.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Cannot replace output file");
}
Audio audio_stage(const std::string& stage,Audio data,const Json& options,RandomState& random,const std::atomic_bool* cancel) {
    cancelled(cancel);
    const auto n=data.frames(); const int channels=data.channels,sr=data.rate;
    if(!n) return data;
    if(stage=="limit") return limit(std::move(data),static_cast<float>(options.value("threshold",0.95)));
    if(stage=="jitter") {
        double ms=options.value("jitter_intensity_ms",16.0); if(n<=static_cast<size_t>(sr)||ms<=0) return data;
        auto input=data.samples;
        for(size_t i=0;i<n;++i) {
            float t=time32(i,n,static_cast<double>(n)/sr);
            float drift=static_cast<float>(ms/1000.0)*(0.65f*std::sin(static_cast<float>(2*pi*0.22)*t)+0.35f*std::cos(static_cast<float>(2*pi*0.37)*t+0.4f));
            float sample=std::clamp((t+drift)*static_cast<float>(sr),0.0f,static_cast<float>(n-1));
            auto a=static_cast<size_t>(std::floor(sample)),b=std::min(a+1,n-1); float frac=static_cast<float>(static_cast<double>(sample)-a);
            for(int c=0;c<channels;++c) data.samples[i*channels+c]=input[a*channels+c]*(1.0f-frac)+input[b*channels+c]*frac;
        }
    } else if(stage=="vocal" || stage=="scrambler") {
        auto input=asdouble(data);
        if(stage=="vocal") {
            if(channels==1) { auto middle=filter(input,1,bank(sr).at("vocal_mid")); for(size_t i=0;i<input.size();++i) input[i]-=0.92*middle[i]; }
            else {
                auto bass=filter(input,channels,bank(sr).at("bass160")),high=filter(input,channels,bank(sr).at("high160"));
                std::vector<double> output(n*2);
                double energy=0; for(size_t i=0;i<n;++i) { double side=high[i*channels]-high[i*channels+1]; energy+=side*side; }
                bool safeguard=(-energy/(energy+1e-9))<-0.4;
                for(size_t i=0;i<n;++i) { double low=0.5*(bass[i*channels]+bass[i*channels+1]),side=high[i*channels]-high[i*channels+1],center=safeguard?0.15*0.5*(high[i*channels]+high[i*channels+1]):0; output[i*2]=low+(side+center); output[i*2+1]=low+(-side+center); }
                input=std::move(output); data.channels=2;
            }
        } else {
            if(channels>=2) {
                auto bass=filter(input,channels,bank(sr).at("bass180")),high=filter(input,channels,bank(sr).at("high180"));
                std::vector<double> output(n*2); double depth=options.value("vocal_cut_depth",0.92);
                for(size_t i=0;i<n;++i) { double low=0.5*(bass[i*channels]+bass[i*channels+1]); output[i*2]=low+(high[i*channels]-depth*high[i*channels+1]); output[i*2+1]=low+(high[i*channels+1]-depth*high[i*channels]); }
                input=std::move(output); data.channels=2;
            }
            auto formants=filter(input,data.channels,bank(sr).at("formants")); double wet=options.value("ring_mod_wet",0.35);
            for(size_t i=0;i<n;++i) {
                float t=time32(i,n,static_cast<double>(n)/sr),lfo=std::sin(static_cast<float>(2*pi*0.35)*t);
                float carrier=std::cos(static_cast<float>(2*pi)*(static_cast<float>(options.value("ring_mod_carrier_hz",65.0))+25.0f*lfo)*t);
                for(int c=0;c<data.channels;++c) { size_t k=i*data.channels+c; double ring=formants[k]*carrier,smeared=(1.0-wet)*formants[k]+wet*ring; input[k]=input[k]-formants[k]+smeared; }
            }
            auto original=input; auto delay=static_cast<size_t>(std::max(1.0,std::nearbyint(options.value("comb_delay_ms",6.8)/1000.0*sr)));
            for(size_t i=delay;i<n;++i) for(int c=0;c<data.channels;++c) input[i*data.channels+c]+=options.value("comb_feedback",0.32)*original[(i-delay)*data.channels+c];
        }
        assign(data,input);
    } else if(stage=="bode" || stage=="triple") {
        if(stage=="bode") {
            auto hilbert=analytic(data.samples,n,channels); float frequency=static_cast<float>(2*pi*options.value("bode_shift_hz",8.5)),wet=0.85f;
            for(size_t i=0;i<n;++i) { float phase=frequency*time32(i,n,static_cast<double>(n)/sr); std::complex<float> rotation(std::cos(phase),std::sin(phase)); for(int c=0;c<channels;++c) { auto k=i*channels+c; data.samples[k]=(1.0f-wet)*data.samples[k]+wet*(hilbert[k]*rotation).real(); } }
        } else {
            auto bass=filter(asdouble(data),channels,bank(sr).at("bass200"));
            auto middle=analytic(filter(asdouble(data),channels,bank(sr).at("middle")),n,channels),top=analytic(filter(asdouble(data),channels,bank(sr).at("top")),n,channels);
            for(size_t i=0;i<n;++i) {
                float t=time32(i,n,static_cast<double>(n)/sr),pm=static_cast<float>(2*pi*12)*t,pt=static_cast<float>(2*pi*-16)*t;
                std::complex<double> rm(std::cos(pm),std::sin(pm)),rt(std::cos(pt),std::sin(pt));
                for(int c=0;c<channels;++c) { auto k=i*channels+c; data.samples[k]=static_cast<float>(bass[k]+(middle[k]*rm).real()+(top[k]*rt).real()); }
            }
        }
    } else if(stage=="peaks") {
        if(n<2048) return data;
        constexpr size_t fft_size=2048,hop=512,bins=1025;
        const size_t padded=n+2048,frames=(padded-2048+hop-1)/hop+1,total=(frames-1)*hop+2048;
        std::vector<float> window(fft_size); for(size_t i=0;i<fft_size;++i) window[i]=static_cast<float>(0.5-0.5*std::cos(2*pi*i/fft_size));
        int minbin=std::max(1,static_cast<int>(300.0/(sr/2048.0))),maxbin=std::min(1024,static_cast<int>(5000.0/(sr/2048.0)));
        for(int c=0;c<channels;++c) {
            std::vector<float> out(total),norm(total);
            for(size_t frame=0;frame<frames;++frame) {
                cancelled(cancel);
                std::vector<float> segment(fft_size);
                for(size_t i=0;i<fft_size;++i) { ptrdiff_t index=static_cast<ptrdiff_t>(frame*hop+i)-1024; if(index>=0 && static_cast<size_t>(index)<n) segment[i]=data.samples[index*channels+c]*window[i]; }
                auto spectrum=rfft(segment,fft_size,1);
                for(auto& z:spectrum) z*=1.0f/1024.0f;
                if(maxbin>minbin) {
                    int top=minbin; float magnitude=std::abs(spectrum[top]);
                    for(int k=minbin+1;k<maxbin;++k) { float current=std::abs(spectrum[k]); if(current>magnitude) { magnitude=current; top=k; } }
                    int target=std::min(static_cast<int>(bins-1),top+4);
                    float amplitude=magnitude*static_cast<float>(options.value("decoy_peak_intensity",1.15)),phase=std::arg(spectrum[top])+0.45f;
                    spectrum[target]+=amplitude*std::complex<float>(std::cos(phase),std::sin(phase));
                }
                auto inverse=irfft(spectrum,fft_size,1);
                for(size_t i=0;i<fft_size;++i) { inverse[i]*=1024.0f; out[frame*hop+i]+=inverse[i]*window[i]; norm[frame*hop+i]+=window[i]*window[i]; }
            }
            for(size_t i=0;i<n;++i) { const auto index=i+1024; data.samples[i*channels+c]=out[index]/(norm[index]>1e-10f?norm[index]:1.0f); }
        }
    } else if(stage=="phase") {
        assign(data,filter(asdouble(data),channels,bank(sr).at("phase")));
    } else if(stage=="bed") {
        std::vector<float> drone(n);
        for(size_t i=0;i<n;++i) { float t=time32(i,n,static_cast<double>(n)/sr); drone[i]=static_cast<float>(options.value("drone_bed_volume",0.035))*(0.45f*std::sin(static_cast<float>(2*pi*110)*t)+0.35f*std::sin(static_cast<float>(2*pi*164.81)*t)+0.20f*std::sin(static_cast<float>(2*pi*220)*t)); }
        size_t delay=static_cast<size_t>(0.002*sr)%n;
        for(size_t i=0;i<n;++i) for(int c=0;c<channels;++c) { size_t index=c==1 ? (i+n-delay)%n : i; float texture=static_cast<float>(0.007*random.normal()); data.samples[i*channels+c]=data.samples[i*channels+c]+drone[index]+texture; }
    } else if(stage=="padding") {
        double front=options.value("front_padding_seconds",4.0),tail=options.value("tail_padding_seconds",2.0);
        if(front<0 || tail<0 || front>600 || tail>600) throw std::runtime_error("Invalid padding duration");
        size_t f=static_cast<size_t>(front*sr),t=static_cast<size_t>(tail*sr);
        if(!f&&!t) return data;
        std::vector<float> track(f);
        for(size_t i=0;i<f;++i) { float tf=time32(i,f,front),env=time32(i,f,1.0,true,0.05); env=std::pow(env,3.0f); float tone=(0.02f*std::sin(static_cast<float>(2*pi*140)*tf))*env; track[i]=tone+static_cast<float>(0.003*random.normal()); }
        Audio out; out.rate=sr; out.channels=channels; out.samples.resize((f+n+t)*channels);
        size_t delay=f?static_cast<size_t>(0.002*sr)%f:0;
        for(size_t i=0;i<f;++i) for(int c=0;c<channels;++c) out.samples[i*channels+c]=track[c==1?(i+f-delay)%f:i];
        std::copy(data.samples.begin(),data.samples.end(),out.samples.begin()+f*channels); data=std::move(out);
    } else if(stage=="room") {
        const auto length=static_cast<size_t>(0.25*sr); if(!length) return data;
        std::vector<double> impulse(length); impulse[0]=1;
        for(const auto& reflection:{std::pair{0.016,0.45}, {0.027,-0.34}, {0.041,0.28}, {0.059,-0.21}}) { size_t index=static_cast<size_t>(reflection.first*sr); if(index<length) impulse[index]=static_cast<float>(reflection.second); }
        std::vector<double> diffuse(length);
        for(size_t i=0;i<length;++i) diffuse[i]=static_cast<float>(0.08*random.normal())*std::exp(-(static_cast<double>(i)*0.25/length)/0.35);
        diffuse=filter(std::move(diffuse),1,bank(sr).at("air"));
        double peak=0; for(size_t i=0;i<length;++i) { impulse[i]+=diffuse[i]; peak=std::max(peak,std::abs(impulse[i])); }
        for(auto& value:impulse) value/=peak+1e-6;
        std::vector<float> sat=data.samples; for(auto& value:sat) value=std::tanh(value*1.25f)/1.25f;
        const auto size=fast_length(n+length-1,true);
        double wet=options.value("reamping_wet_mix",0.28);
        for(int c=0;c<channels;++c) {
            std::vector<float> source(size); for(size_t i=0;i<n;++i) source[i]=sat[i*channels+c];
            std::vector<double> response(size); size_t delay=c==1?static_cast<size_t>(0.0015*sr)%length:0;
            for(size_t i=0;i<length;++i) response[i]=impulse[(i+length-delay)%length];
            auto a=rfft(source,size,1); auto b=rfft(response,size,1);
            for(size_t i=0;i<b.size();++i) b[i]*=std::complex<double>(a[i].real(),a[i].imag());
            auto convolution=irfft(b,size,1);
            for(size_t i=0;i<n;++i) data.samples[i*channels+c]=static_cast<float>(static_cast<float>(1.0-wet)*sat[i*channels+c]+wet*convolution[i]);
        }
    } else if(stage=="pitch" || stage=="wobble") {
        data=pitch(std::move(data),options,stage=="wobble",cancel);
    } else if(stage=="eq") {
        std::vector<double> output=asdouble(data);
        if(options.value("apply_eq_filters",true)) { if(25.0<sr*0.45) output=filter(std::move(output),channels,bank(sr).at("eq_high")); if(18500.0<sr*0.49) output=filter(std::move(output),channels,bank(sr).at("eq_low")); }
        if(options.value("apply_dither",true)) {
            double amplitude=std::pow(10.0,options.value("dither_level_db",-62.0)/20.0);
            std::vector<double> noise(output.size()); for(auto& value:noise) value=-amplitude+2*amplitude*random.uniform();
            for(size_t i=0;i<output.size();++i) output[i]+=(noise[i]+(-amplitude+2*amplitude*random.uniform()))*0.5;
        }
        assign(data,output);
    } else if(stage=="normalize") {
        float peak=0;
        for(auto& value:data.samples) { if(!std::isfinite(value)) value=std::isnan(value)?0.0f:std::signbit(value)?-1.0f:1.0f; peak=std::max(peak,std::abs(value)); }
        if(peak>0) { float factor=static_cast<float>(std::pow(10.0,options.value("peak_target_db",-0.5)/20.0)/peak); for(auto& value:data.samples) value*=factor; }
    } else throw std::runtime_error("Unknown audio stage: "+stage);
    return data;
}
AudioResult process_audio(const fs::path& input,const fs::path& output,const AudioOptions& options,const Profile& profile,Progress progress,const std::atomic_bool* cancel,RandomState* provided,Transport transport) {
    AudioResult result;
    RandomState generated(static_cast<uint32_t>(std::stoul(random_hex(4),nullptr,16))); auto& random=provided?*provided:generated;
    const auto& cfg=options.values;
    auto log=[&](int pct,const std::string& message) { cancelled(cancel); if(progress) progress(pct,message); };
    try {
        log(5,"Reading audio."); Audio data=read_audio(input); result.original_duration=static_cast<double>(data.frames())/data.rate;
        bool cloud=false; std::string mode=cfg["mode"];
        if(mode=="cloud_api"||mode=="stem") { try { log(18,"Separating remote stems."); data=cloud_stems(input,profile,progress,cancel,transport); cloud=true; } catch(...) { cancelled(cancel); log(25,"Cloud separation failed, so local processing was used."); } }
        auto apply=[&](const char* stage,int pct,const char* message) { log(pct,message); data=audio_stage(stage,std::move(data),cfg,random,cancel); };
        if(!cloud) {
            if(mode=="nuclear_cloak"||mode=="pure_instrumental"||cfg.value("enable_total_vocal_cut",true)) apply("vocal",22,"Processing vocals.");
            else if(cfg.value("enable_vocal_cut",true)||cfg.value("enable_formant_scrambler",true)) { auto settings=cfg; if(!cfg.value("enable_formant_scrambler",true)) settings["ring_mod_wet"]=0; log(22,"Processing vocals."); data=audio_stage("scrambler",std::move(data),settings,random,cancel); }
            if(mode=="nuclear_cloak"||cfg.value("enable_triple_band_bode",true)) apply("triple",36,"Shifting frequency bands.");
            else if(cfg.value("enable_bode_freq_shifter",true)&&cfg.value("bode_shift_hz",8.5)!=0) apply("bode",36,"Shifting frequencies.");
            if(cfg.value("enable_micro_chrono_jitter",true)&&cfg.value("jitter_intensity_ms",16.0)>0) apply("jitter",48,"Applying timing variation.");
            if(cfg.value("enable_adversarial_peaks",true)) apply("peaks",58,"Processing spectral peaks.");
            if(cfg.value("enable_phase_dispersion",true)) apply("phase",66,"Dispersing phase.");
            if(mode=="nuclear_cloak"||cfg.value("enable_continuous_bed",true)) apply("bed",74,"Adding ambient texture.");
            if(cfg.value("enable_reamping_room",true)&&cfg.value("reamping_wet_mix",0.28)>0) apply("room",80,"Simulating room acoustics.");
            if(mode=="nuclear_cloak"||cfg.value("enable_pitch_wobble",true)) apply("wobble",86,"Processing pitch and tempo.");
            else if(std::abs(cfg.value("pitch_shift_semitones",2.5))>0.01||std::abs(cfg.value("tempo_factor",0.94)-1)>0.005) apply("pitch",86,"Processing pitch and tempo.");
        }
        if(cfg.value("front_padding_seconds",4.0)>0||cfg.value("tail_padding_seconds",2.0)>0) apply("padding",90,"Adding front and tail padding.");
        if(cfg.value("apply_eq_filters",true)||cfg.value("apply_dither",true)) apply("eq",92,"Applying filters and dither.");
        if(cfg.value("trim_duration",false)&&cfg.value("max_duration_seconds",28.0)>0) { auto frames=static_cast<size_t>(cfg.value("max_duration_seconds",28.0)*data.rate); if(data.frames()>frames) data.samples.resize(frames*data.channels); }
        apply("normalize",96,"Normalizing the peak level.");
        log(97,"Writing audio."); export_audio(output,data,cfg.value("output_bitrate",std::string("wav")),cfg.value("strip_metadata",true),cancel);
        result.final_duration=static_cast<double>(data.frames())/data.rate; result.audit=audit_audio(input,output,cancel); result.success=true;
        if(progress) progress(100,"Processing completed.");
    } catch(const std::exception& error) { result.error=error.what(); }
    return result;
}
std::vector<fs::path> slice_audio(const fs::path& input,const fs::path& directory,double seconds,const std::atomic_bool* cancel) {
    if(!std::isfinite(seconds)||seconds>3600) throw std::runtime_error("Invalid slice duration");
    auto data=read_audio(input); auto chunk=static_cast<size_t>(std::max(3.0,seconds)*data.rate);
    fs::create_directories(directory);
    std::vector<fs::path> result;
    for(size_t start=0;start<data.frames();start+=chunk) {
        cancelled(cancel); auto end=std::min(start+chunk,data.frames());
        Audio piece; piece.rate=data.rate; piece.channels=data.channels;
        piece.samples.assign(data.samples.begin()+start*data.channels,data.samples.begin()+end*data.channels);
        if(end-start<static_cast<size_t>(3*data.rate)&&!result.empty()) {
            auto previous=read_audio(result.back(),0,false);
            previous.samples.insert(previous.samples.end(),piece.samples.begin(),piece.samples.end());
            export_audio(result.back(),std::move(previous),"wav",true,cancel); break;
        }
        std::ostringstream suffix; suffix<<"_part"<<std::setw(2)<<std::setfill('0')<<result.size()+1<<".wav";
        auto path=directory/fs::u8path(input.stem().u8string()+suffix.str()); export_audio(path,std::move(piece),"wav",true,cancel); result.push_back(path);
    }
    return result;
}
Json inspect_audio(const fs::path& input) {
    auto data=read_audio(input,0,false);
    double duration=static_cast<double>(data.frames())/data.rate;
    return {{"duration_seconds",duration},{"sample_rate",data.rate},{"channels",data.channels},{"file_size_mb",fs::file_size(input)/(1024.0*1024.0)},{"format_name",input.extension().u8string()},{"libsndfile",sound().version_string()}};
}
Json audit_audio(const fs::path& original,const fs::path& output,const std::atomic_bool* cancel) {
    auto a=fingerprint(original,cancel),b=fingerprint(output,cancel);
    double similarity=0; size_t aligned=0,window=std::min<size_t>(30,std::min(a.size(),b.size()));
    if(window>5) for(int offset=-35;offset<35;++offset) {
        size_t first=std::max(0,-offset),second=std::max(0,offset);
        size_t count_a=first<a.size()?std::min(window,a.size()-first):0,count_b=second<b.size()?std::min(window,b.size()-second):0;
        if(count_a!=count_b||count_a<5) continue;
        unsigned difference=0; for(size_t i=0;i<count_a;++i) { auto bits=a[first+i]^b[second+i]; while(bits) { bits&=bits-1; ++difference; } }
        double value=(1.0-static_cast<double>(difference)/(count_a*32))*100;
        if(value>similarity) { similarity=value; aligned=count_a; }
    }
    double drop=0;
    try {
        auto source=read_audio(original),destination=read_audio(output);
        auto vo=filter(asdouble(source),source.channels,bank(44100).at("audit")),vs=filter(asdouble(destination),destination.channels,bank(44100).at("audit"));
        double so=0,ss=0; for(auto v:vo) so+=v*v; for(auto v:vs) ss+=v*v;
        drop=std::clamp(20*std::log10((std::sqrt(ss/vs.size())+1e-9)/(std::sqrt(so/vo.size())+1e-9)),-60.0,20.0);
    } catch(...) { cancelled(cancel); }
    return {{"chromaprint_available",!a.empty()&&!b.empty()},{"similarity_pct",std::nearbyint(similarity*100)/100},{"bit_error_rate_pct",std::nearbyint((100-similarity)*100)/100},{"aligned_frames",aligned},{"vocal_energy_drop_db",std::nearbyint(drop*10)/10}};
}
}
