#include "ghostwave/core.hpp"
#include <windows.h>
#include <shellapi.h>
#include <wx/wx.h>
#include <wx/access.h>
#include <wx/clipbrd.h>
#include <wx/dnd.h>
#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <wx/spinctrl.h>
#include <chrono>
#include <algorithm>
#include <array>
#include <set>
#include <sstream>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace {
using namespace gw;
Json catalog;
wxString t(const std::string& key) { return wxString::FromUTF8(catalog.at(key).get<std::string>()); }
std::string text(wxTextCtrl* control) { return control->GetValue().ToStdString(wxConvUTF8); }
void named(wxWindow* control,const wxString& name) { auto value=name; value.Replace("&",""); control->SetName(value); }
wxTextCtrl* field(wxWindow* parent,wxSizer* sizer,const char* key,const wxString& value={},long style=0) {
    sizer->Add(new wxStaticText(parent,wxID_ANY,t(key)),0,wxTOP|wxBOTTOM,4);
    auto control=new wxTextCtrl(parent,wxID_ANY,value,wxDefaultPosition,wxDefaultSize,style);
    named(control,t(key)); sizer->Add(control,(style&wxTE_MULTILINE)?1:0,wxEXPAND|wxBOTTOM,8); return control;
}
wxButton* button(wxWindow* parent,wxSizer* sizer,const char* key,int id=wxID_ANY) {
    auto control=new wxButton(parent,id,t(key)); named(control,t(key)); sizer->Add(control,0,wxALL,4); return control;
}
void read_dialog(wxWindow* parent,const char* title,const std::string& content) {
    wxDialog dialog(parent,wxID_ANY,t(title),wxDefaultPosition,wxSize(620,440),wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER);
    auto sizer=new wxBoxSizer(wxVERTICAL);
    auto area=field(&dialog,sizer,"result",wxString::FromUTF8(content),wxTE_MULTILINE|wxTE_READONLY);
    auto row=new wxBoxSizer(wxHORIZONTAL);
    auto copy=button(&dialog,row,"copy"); button(&dialog,row,"close",wxID_CANCEL);
    copy->Bind(wxEVT_BUTTON,[area](wxCommandEvent&) { if(wxTheClipboard->Open()) { wxTheClipboard->SetData(new wxTextDataObject(area->GetValue())); wxTheClipboard->Close(); } });
    sizer->Add(row,0,wxALIGN_RIGHT); dialog.SetSizer(sizer); area->SetFocus(); dialog.ShowModal();
}
struct Job {
    std::atomic_bool done{false},cancel{false};
    std::mutex mutex;
    int percent=0;
    std::string status,error,output;
    Json json;
    std::optional<Profile> profile;
    Progress progress() { return [this](int pct,const std::string& message) { std::lock_guard<std::mutex> lock(mutex); percent=pct; status=message; }; }
};
enum { OpenAudio=wxID_HIGHEST+1,OpenLyrics,SaveLyrics,ImportProfile,ExportProfile,Advanced,StemSettings,LyricsSettings,Blacklist,Slice,Audit,Updates,Changelog,Support,Guide,Protocol,About,English,Indonesian,Process };
class Frame;
class AudioDrop : public wxFileDropTarget {
    Frame* frame;
public:
    explicit AudioDrop(Frame* parent):frame(parent) {}
    bool OnDropFiles(wxCoord,wxCoord,const wxArrayString& files) override;
};
class Frame : public wxFrame {
    Profile profile;
    wxNotebook* tabs;
    wxTextCtrl *input,*output,*lyrics_input,*lyrics_output;
    wxChoice *preset,*format,*mode;
    wxStaticText* status;
    wxGauge* gauge;
    wxButton* cancel_button;
    wxTimer timer;
    std::thread worker;
    std::unique_ptr<Job> job;
    std::function<void(Job&)> finish;
    bool closing=false;
    int last_progress=-1;
    std::vector<std::string> preset_names={"Complete Sanitization (Recommended)","Balanced Audio Quality","Room Acoustic Simulation","Frequency & Phase Shift","Vocal Reduction Only","Cloud API Stem Separation"};
    void report_error(const std::string& error) { wxMessageBox(wxString::FromUTF8(error),t("error"),wxOK|wxICON_ERROR,this); }
    void state(bool busy) {
        tabs->Enable(!busy); cancel_button->Enable(busy);
        for(size_t i=0;i<GetMenuBar()->GetMenuCount();++i) GetMenuBar()->EnableTop(i,!busy);
        if(busy) cancel_button->SetFocus();
    }
    void start(std::function<void(Job&)> task,std::function<void(Job&)> completion={}) {
        if(job) return;
        job=std::make_unique<Job>(); finish=std::move(completion); state(true); last_progress=-1; gauge->SetValue(0); status->SetLabel(t("working"));
        auto current=job.get();
        worker=std::thread([current,task=std::move(task)] { try { task(*current); } catch(const std::exception& error) { current->error=error.what(); } catch(...) { current->error="Processing failed."; } current->done.store(true); });
    }
    void tick(wxTimerEvent&) {
        if(!job) return;
        { std::lock_guard<std::mutex> lock(job->mutex); int percent=std::clamp(job->percent,0,100); gauge->SetValue(percent);
          if(percent/5!=last_progress/5) { auto message=t("working"); if(message.EndsWith(".")) message.RemoveLast(); status->SetLabel(wxString::Format("%s %d%%.",message,percent)); last_progress=percent; wxAccessible::NotifyEvent(wxACC_EVENT_OBJECT_NAMECHANGE,status,wxOBJID_CLIENT,0); } }
        if(!job->done.load()) return;
        worker.join(); auto complete=std::move(job); auto callback=std::move(finish); state(false);
        if(closing) { Close(); return; }
        if(!complete->error.empty()) { status->SetLabel(complete->cancel.load()?t("cancelled"):t("error")); if(!complete->cancel.load()) report_error(complete->error); }
        else { gauge->SetValue(100); status->SetLabel(t("done")); if(callback) try { callback(*complete); } catch(const std::exception& error) { report_error(error.what()); } }
        wxAccessible::NotifyEvent(wxACC_EVENT_OBJECT_NAMECHANGE,status,wxOBJID_CLIENT,0);
        if(!job) (tabs->GetSelection()==0?input:lyrics_input)->SetFocus();
    }
    void sync() {
        profile.data["export_format"]=format->GetSelection()==0?"wav":format->GetSelection()==1?"320k":"192k";
        const char* modes[]={"scramble","hybrid","phonetic","semantic","cloud"}; profile.data["lyrics_cloak_mode"]=modes[mode->GetSelection()];
        profile.data["default_preset"]=preset_names[preset->GetSelection()];
    }
    void restore() {
        auto selected=std::find(preset_names.begin(),preset_names.end(),profile.data["default_preset"].get<std::string>());
        preset->SetSelection(selected==preset_names.end()?0:static_cast<int>(selected-preset_names.begin()));
        std::string container=profile.data["export_format"]; format->SetSelection(container=="wav"?0:container=="192k"?2:1);
        std::string strategy=profile.data["lyrics_cloak_mode"]; int choice=0;
        const char* modes[]={"scramble","hybrid","phonetic","semantic","cloud"}; for(int i=0;i<5;++i) if(strategy==modes[i]) choice=i; mode->SetSelection(choice);
    }
    void choose_preset() {
        int index=preset->GetSelection();
        double pitches[]={2.5,1.5,1.0,2.0,0.0,2.0},tempos[]={0.940,0.970,0.980,0.950,1.0,0.950};
        profile.data["pitch_shift_semitones"]=pitches[index]; profile.data["tempo_factor"]=tempos[index]; profile.data["trim_duration"]=false;
        const std::vector<std::pair<const char*,std::array<bool,6>>> flags={
            {"enable_micro_chrono_jitter",{true,true,false,true,false,true}},{"enable_bode_freq_shifter",{true,true,false,true,false,true}},
            {"enable_adversarial_peaks",{true,false,false,true,false,false}},{"enable_phase_dispersion",{true,true,true,true,false,true}},
            {"enable_reamping_room",{true,true,true,false,false,true}},{"enable_vocal_cut",{true,true,true,true,true,false}},
            {"inject_preamble",{true,false,true,false,false,true}}};
        for(const auto& flag:flags) profile.data[flag.first]=flag.second[index];
        if(index==0||index==1||index==5) format->SetSelection(0);
        if(index==0) profile.data["max_duration_seconds"]=24.0; else if(index==1) profile.data["max_duration_seconds"]=28.0;
        sync();
    }
    void process() {
        sync();
        if(!profile.recovered) profile.save();
        const auto cfg=profile;
        if(tabs->GetSelection()==1) {
            const auto original=text(lyrics_input);
            start([cfg,original](Job& job) { auto result=process_lyrics(original,cfg,&job.cancel); job.output=result.text; job.json=result.audit; job.json["stats"]=result.stats; job.json["warnings"]=result.warnings; },[this](Job& job) { lyrics_output->SetValue(wxString::FromUTF8(job.output)); read_dialog(this,"audit",job.json.dump(2)+"\n\n"+t("audit_note").ToStdString(wxConvUTF8)); });
            return;
        }
        auto source=fs::u8path(text(input)); if(!fs::is_regular_file(source)) { report_error(t("select_audio").ToStdString(wxConvUTF8)); return; }
        auto destination=fs::u8path(text(output));
        if(destination.empty()) {
            const auto extension=format->GetSelection()==0?".wav":".mp3";
            wxFileDialog dialog(this,t("output"),source.parent_path().wstring(),wxString::FromUTF8(source.stem().u8string()+"_suno_ready"+extension),format->GetSelection()==0?"WAV files (*.wav)|*.wav":"MP3 files (*.mp3)|*.mp3",wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
            if(dialog.ShowModal()!=wxID_OK) return; destination=fs::path(dialog.GetPath().ToStdWstring()); output->SetValue(dialog.GetPath());
        } else if(fs::exists(destination)) {
            wxFileDialog dialog(this,t("output"),destination.parent_path().wstring(),destination.filename().wstring(),"Audio files|*.wav;*.mp3",wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
            if(dialog.ShowModal()!=wxID_OK) return; destination=fs::path(dialog.GetPath().ToStdWstring()); output->SetValue(dialog.GetPath());
        }
        auto options=AudioOptions::preset(preset->GetSelection(),cfg);
        start([cfg,source,destination,options](Job& job) { auto result=process_audio(source,destination,options,cfg,job.progress(),&job.cancel); if(!result.success) throw std::runtime_error(result.error); job.json=result.audit; },[this](Job& job) { read_dialog(this,"audit",job.json.dump(2)+"\n\n"+t("audit_note").ToStdString(wxConvUTF8)); });
    }
    void advanced() {
        sync(); wxDialog dialog(this,wxID_ANY,t("advanced"),wxDefaultPosition,wxSize(500,620),wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER);
        auto outer=new wxBoxSizer(wxVERTICAL); auto scroller=new wxScrolledWindow(&dialog); scroller->SetScrollRate(0,12); auto sizer=new wxBoxSizer(wxVERTICAL);
        std::vector<std::pair<std::string,wxCheckBox*>> checks;
        for(const auto& key:{"enable_micro_chrono_jitter","enable_bode_freq_shifter","enable_adversarial_peaks","enable_phase_dispersion","enable_reamping_room","enable_vocal_cut","enable_formant_scrambler","inject_preamble","apply_eq_filters","apply_dither","trim_duration","strip_metadata","filter_profanity","strip_celebrities","normalize_unicode","format_tags","check_limits","enable_lyrics_cloaking","lyrics_preserve_syllables","lyrics_add_vibrato_glides","lyrics_break_ngrams","check_updates_on_startup"}) {
            auto box=new wxCheckBox(scroller,wxID_ANY,t(key)); named(box,t(key)); box->SetValue(profile.data[key]); sizer->Add(box,0,wxALL,4); checks.push_back({key,box});
        }
        std::vector<std::pair<std::string,wxSpinCtrlDouble*>> numbers;
        auto number=[&](const char* key,const char* label,double minimum,double maximum,double increment) {
            sizer->Add(new wxStaticText(scroller,wxID_ANY,t(label)),0,wxTOP,6);
            auto control=new wxSpinCtrlDouble(scroller,wxID_ANY,{},wxDefaultPosition,wxDefaultSize,wxSP_ARROW_KEYS,minimum,maximum,profile.data[key].get<double>(),increment); control->SetDigits(3); named(control,t(label)); sizer->Add(control,0,wxEXPAND|wxBOTTOM,6); numbers.push_back({key,control});
        };
        number("pitch_shift_semitones","pitch",-48,48,0.1); number("tempo_factor","tempo",0.1,4,0.01); number("bode_shift_hz","bode",-20000,20000,0.5); number("max_duration_seconds","duration",1,3600,1); number("slicer_chunk_duration","chunk",3,3600,1); number("lyrics_adlib_frequency","adlib_frequency",0,1,0.1);
        scroller->SetSizer(sizer); outer->Add(scroller,1,wxEXPAND|wxALL,12); auto row=new wxBoxSizer(wxHORIZONTAL); button(&dialog,row,"save",wxID_OK); button(&dialog,row,"cancel",wxID_CANCEL); outer->Add(row,0,wxALIGN_RIGHT); dialog.SetSizer(outer);
        if(dialog.ShowModal()==wxID_OK) { for(const auto& check:checks) profile.data[check.first]=check.second->GetValue(); for(const auto& number:numbers) profile.data[number.first]=number.second->GetValue(); profile.save(); }
    }
    void credentials(bool lyrics) {
        wxDialog dialog(this,wxID_ANY,t(lyrics?"lyrics_settings":"stem_settings"),wxDefaultPosition,wxSize(490,500),wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER);
        auto sizer=new wxBoxSizer(wxVERTICAL); std::string prefix=lyrics?"lyrics_cloud_":"cloud_";
        sizer->Add(new wxStaticText(&dialog,wxID_ANY,t("provider")),0,wxALL,4);
        wxArrayString choices=lyrics?wxArrayString{"groq","openrouter","openai","custom"}:wxArrayString{"replicate","huggingface","custom"};
        auto provider=new wxChoice(&dialog,wxID_ANY,wxDefaultPosition,wxDefaultSize,choices); provider->SetStringSelection(wxString::FromUTF8(profile.data[prefix+"provider"].get<std::string>())); named(provider,t("provider")); sizer->Add(provider,0,wxEXPAND|wxALL,4);
        auto token=field(&dialog,sizer,"token",wxString::FromUTF8(profile.data[prefix+"api_token"].get<std::string>()),wxTE_PASSWORD);
        auto reveal=new wxCheckBox(&dialog,wxID_ANY,t("show_token")); named(reveal,t("show_token")); sizer->Add(reveal,0,wxBOTTOM,8);
        reveal->Bind(wxEVT_CHECKBOX,[token](wxCommandEvent& event) { SendMessageW(reinterpret_cast<HWND>(token->GetHandle()),EM_SETPASSWORDCHAR,event.IsChecked()?0:L'*',0); });
        auto endpoint=field(&dialog,sizer,"endpoint",wxString::FromUTF8(profile.data[prefix+"endpoint_url"].get<std::string>()));
        auto model=field(&dialog,sizer,"model",wxString::FromUTF8(profile.data[prefix+(lyrics?"model":"model_version")].get<std::string>()));
        wxSpinCtrl* timeout=nullptr;
        if(!lyrics) { sizer->Add(new wxStaticText(&dialog,wxID_ANY,t("timeout")),0,wxBOTTOM,4); timeout=new wxSpinCtrl(&dialog,wxID_ANY,{},wxDefaultPosition,wxDefaultSize,wxSP_ARROW_KEYS,1,600,profile.data["cloud_timeout_seconds"].get<int>()); named(timeout,t("timeout")); sizer->Add(timeout,0,wxEXPAND|wxBOTTOM,8); }
        auto row=new wxBoxSizer(wxHORIZONTAL); button(&dialog,row,"save",wxID_OK); button(&dialog,row,"cancel",wxID_CANCEL); sizer->Add(row,0,wxALIGN_RIGHT); dialog.SetSizer(sizer);
        if(dialog.ShowModal()==wxID_OK) { profile.data[prefix+"provider"]=provider->GetStringSelection().ToStdString(wxConvUTF8); profile.data[prefix+"api_token"]=text(token); profile.data[prefix+"endpoint_url"]=text(endpoint); profile.data[prefix+(lyrics?"model":"model_version")]=text(model); if(timeout)profile.data["cloud_timeout_seconds"]=timeout->GetValue(); profile.save(); }
    }
    void blacklist() {
        wxDialog dialog(this,wxID_ANY,t("blacklist"),wxDefaultPosition,wxSize(460,440),wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER);
        auto sizer=new wxBoxSizer(wxVERTICAL); sizer->Add(new wxStaticText(&dialog,wxID_ANY,t("blacklist_description")),0,wxALL,8);
        auto names=profile.data["custom_celebrity_blacklist"];
        if(names.empty()) names=read_json(app_dir()/"resources/lyrics.json")["DEFAULT_CELEBRITY_BLACKLIST"];
        std::string content; for(const auto& name:names) { if(!content.empty()) content+='\n'; content+=name.get<std::string>(); }
        auto control=field(&dialog,sizer,"blacklist",wxString::FromUTF8(content),wxTE_MULTILINE);
        auto row=new wxBoxSizer(wxHORIZONTAL); button(&dialog,row,"save",wxID_OK); button(&dialog,row,"cancel",wxID_CANCEL); sizer->Add(row,0,wxALIGN_RIGHT); dialog.SetSizer(sizer);
        if(dialog.ShowModal()==wxID_OK) { Json saved=Json::array(); std::istringstream stream(text(control)); std::string line; std::set<std::string> unique; while(std::getline(stream,line)) { auto value=wxString::FromUTF8(line).Trim().Trim(false).ToStdString(wxConvUTF8); if(!value.empty()&&unique.insert(value).second) saved.push_back(value); } profile.data["custom_celebrity_blacklist"]=saved; profile.save(); }
    }
    void tickets() {
        wxArrayString choices; choices.Add(t("new_ticket")); for(const auto& ticket:profile.data["user_tickets"]) choices.Add(wxString::FromUTF8(ticket.value("title",std::string("Ticket"))));
        wxSingleChoiceDialog choose(this,t("support"),t("support"),choices); if(choose.ShowModal()!=wxID_OK) return;
        if(choose.GetSelection()==0) {
            wxDialog dialog(this,wxID_ANY,t("new_ticket"),wxDefaultPosition,wxSize(500,450),wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER);
            auto sizer=new wxBoxSizer(wxVERTICAL); auto title=field(&dialog,sizer,"title"); auto description=field(&dialog,sizer,"description",{},wxTE_MULTILINE); auto category=field(&dialog,sizer,"category",t("support"));
            auto details=new wxCheckBox(&dialog,wxID_ANY,t("diagnostics")); details->SetValue(true); named(details,t("diagnostics")); sizer->Add(details,0,wxALL,4);
            auto row=new wxBoxSizer(wxHORIZONTAL); button(&dialog,row,"submit",wxID_OK); button(&dialog,row,"cancel",wxID_CANCEL); sizer->Add(row,0,wxALIGN_RIGHT); dialog.SetSizer(sizer);
            if(dialog.ShowModal()!=wxID_OK) return;
            auto cfg=profile; auto heading=text(title),body=text(description),kind=text(category); bool attach=details->GetValue();
            start([cfg,heading,body,kind,attach](Job& job) mutable { job.json=create_ticket(cfg,heading,body,kind,attach,http,&job.cancel); job.profile=cfg; },[this](Job& job) { profile=*job.profile; wxMessageBox(t("ticket_sent"),t("support"),wxOK,this); });
        } else {
            auto ticket=profile.data["user_tickets"][choose.GetSelection()-1];
            start([ticket](Job& job) { job.json=fetch_ticket(ticket,http,&job.cancel); },[this,ticket](Job& job) {
                read_dialog(this,"my_tickets",job.json.dump(2));
                wxTextEntryDialog reply(this,t("message"),t("reply")); if(reply.ShowModal()!=wxID_OK||reply.GetValue().empty()) return;
                auto message=reply.GetValue().ToStdString(wxConvUTF8);
                start([ticket,message](Job& job) { job.json=reply_ticket(ticket,message,http,&job.cancel); },[this](Job&) { wxMessageBox(t("ticket_sent"),t("support"),wxOK,this); });
            });
        }
    }
    void update(bool quiet=false) {
        start([](Job& job) { job.json=check_update(http,&job.cancel); },[this,quiet](Job& job) {
            if(!job.json["error_message"].get<std::string>().empty()) { if(!quiet)report_error(job.json["error_message"]); return; }
            if(!job.json["has_update"].get<bool>()) { if(!quiet)wxMessageBox(t("up_to_date"),t("updates"),wxOK,this); return; }
            read_dialog(this,"changelog",job.json.value("release_notes",std::string()));
            if(wxMessageBox(t("update_prompt"),t("updates"),wxYES_NO|wxICON_QUESTION,this)!=wxYES) return;
            auto info=job.json;
            start([info](Job& job) { job.output=download_update(info,job.progress(),&job.cancel).u8string(); },[this](Job& job) { wxMessageBox(t("downloaded"),t("updates"),wxOK,this); ShellExecuteW(nullptr,L"open",wide(job.output).c_str(),nullptr,nullptr,SW_SHOWNORMAL); });
        });
    }
    void menu(int id,const std::function<void()>& action) { Bind(wxEVT_MENU,[this,action](wxCommandEvent&) { if(job) return; try { action(); } catch(const std::exception& error) { report_error(error.what()); } },id); }
    void change_language(const char* language) {
        sync(); profile.data["language"]=language; profile.save(); catalog=read_json(app_dir()/fs::u8path(std::string("resources/ui-")+language+".json"));
        auto replacement=new Frame(profile); replacement->input->SetValue(input->GetValue()); replacement->output->SetValue(output->GetValue());
        replacement->lyrics_input->SetValue(lyrics_input->GetValue()); replacement->lyrics_output->SetValue(lyrics_output->GetValue()); replacement->tabs->SetSelection(tabs->GetSelection());
        replacement->Show(); wxTheApp->SetTopWindow(replacement); Destroy();
    }
public:
    explicit Frame(Profile cfg):wxFrame(nullptr,wxID_ANY,"GhostWave Studio",wxDefaultPosition,wxSize(780,600)),profile(std::move(cfg)),timer(this) {
        SetMinSize(wxSize(600,460)); named(this,"GhostWave Studio");
        auto menu_bar=new wxMenuBar; auto files=new wxMenu; auto tools=new wxMenu; auto languages=new wxMenu; auto help=new wxMenu;
        for(const auto& item:std::vector<std::pair<int,const char*>>{{OpenAudio,"open_audio"},{OpenLyrics,"open_lyrics"},{SaveLyrics,"save_lyrics"},{ImportProfile,"import_profile"},{ExportProfile,"export_profile"},{wxID_EXIT,"exit"}}) files->Append(item.first,t(item.second)+(item.first==OpenAudio?"\tCtrl+O":""));
        for(const auto& item:{std::pair{Advanced,"advanced"},{StemSettings,"stem_settings"},{LyricsSettings,"lyrics_settings"},{Blacklist,"blacklist"},{Slice,"slice"},{Audit,"audit"}}) tools->Append(item.first,t(item.second));
        languages->Append(English,"English"); languages->Append(Indonesian,"Bahasa Indonesia");
        for(const auto& item:{std::pair{Updates,"updates"},{Changelog,"changelog"},{Support,"support"},{Guide,"guide"},{Protocol,"protocol"},{About,"about"}}) help->Append(item.first,t(item.second)+(item.first==Guide?"\tF1":item.first==Changelog?"\tF2":""));
        menu_bar->Append(files,t("file")); menu_bar->Append(tools,t("tools")); menu_bar->Append(languages,t("language")); menu_bar->Append(help,t("help")); SetMenuBar(menu_bar);
        auto panel=new wxPanel(this); auto sizer=new wxBoxSizer(wxVERTICAL); tabs=new wxNotebook(panel,wxID_ANY); named(tabs,t("mode"));
        auto audio=new wxPanel(tabs); auto audio_sizer=new wxBoxSizer(wxVERTICAL);
        audio_sizer->Add(new wxStaticText(audio,wxID_ANY,t("audio_description")),0,wxALL,8);
        input=field(audio,audio_sizer,"input"); auto browse_row=new wxBoxSizer(wxHORIZONTAL); auto browse=button(audio,browse_row,"browse"); audio_sizer->Add(browse_row);
        audio_sizer->Add(new wxStaticText(audio,wxID_ANY,t("preset")),0,wxBOTTOM,4); wxArrayString presets; for(int i=0;i<6;++i) presets.Add(t("preset"+std::to_string(i))); preset=new wxChoice(audio,wxID_ANY,wxDefaultPosition,wxDefaultSize,presets); named(preset,t("preset")); audio_sizer->Add(preset,0,wxEXPAND|wxBOTTOM,8);
        audio_sizer->Add(new wxStaticText(audio,wxID_ANY,t("format")),0,wxBOTTOM,4); format=new wxChoice(audio,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxArrayString{"WAV","MP3 320 kbps","MP3 192 kbps"}); named(format,t("format")); audio_sizer->Add(format,0,wxEXPAND|wxBOTTOM,8);
        output=field(audio,audio_sizer,"output"); auto audio_buttons=new wxBoxSizer(wxHORIZONTAL); auto process_audio_button=button(audio,audio_buttons,"process"); auto advanced_button=button(audio,audio_buttons,"advanced"); audio_sizer->AddStretchSpacer(); audio_sizer->Add(audio_buttons,0,wxALIGN_RIGHT); audio->SetSizer(audio_sizer);
        auto lyrics=new wxPanel(tabs); auto lyrics_sizer=new wxBoxSizer(wxVERTICAL); lyrics_sizer->Add(new wxStaticText(lyrics,wxID_ANY,t("lyrics_description")),0,wxALL,8);
        lyrics_input=field(lyrics,lyrics_sizer,"source",{},wxTE_MULTILINE); lyrics_sizer->Add(new wxStaticText(lyrics,wxID_ANY,t("mode")),0,wxBOTTOM,4);
        mode=new wxChoice(lyrics,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxArrayString{t("scramble"),t("hybrid"),t("phonetic"),t("semantic"),t("cloud")}); named(mode,t("mode")); lyrics_sizer->Add(mode,0,wxEXPAND|wxBOTTOM,8);
        lyrics_output=field(lyrics,lyrics_sizer,"result",{},wxTE_MULTILINE|wxTE_READONLY); auto lyrics_buttons=new wxBoxSizer(wxHORIZONTAL); auto process_lyrics_button=button(lyrics,lyrics_buttons,"process_lyrics"); auto copy_button=button(lyrics,lyrics_buttons,"copy"); lyrics_sizer->Add(lyrics_buttons,0,wxALIGN_RIGHT); lyrics->SetSizer(lyrics_sizer);
        tabs->AddPage(audio,t("audio")); tabs->AddPage(lyrics,t("lyrics")); sizer->Add(tabs,1,wxEXPAND|wxALL,10);
        gauge=new wxGauge(panel,wxID_ANY,100); named(gauge,t("progress")); sizer->Add(gauge,0,wxEXPAND|wxLEFT|wxRIGHT,12);
        auto bottom=new wxBoxSizer(wxHORIZONTAL); status=new wxStaticText(panel,wxID_ANY,t("ready")); named(status,t("status")); bottom->Add(status,1,wxALIGN_CENTER_VERTICAL|wxALL,8); cancel_button=button(panel,bottom,"cancel"); cancel_button->Enable(false); sizer->Add(bottom,0,wxEXPAND); panel->SetSizer(sizer);
        restore(); input->SetDropTarget(new AudioDrop(this));
        browse->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){open_audio();}); preset->Bind(wxEVT_CHOICE,[this](wxCommandEvent&){choose_preset();});
        for(auto control:{process_audio_button,process_lyrics_button}) control->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){try { process(); } catch(const std::exception& error) {report_error(error.what());}});
        advanced_button->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){try { advanced(); } catch(const std::exception& error) {report_error(error.what());}});
        copy_button->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){if(wxTheClipboard->Open()){wxTheClipboard->SetData(new wxTextDataObject(lyrics_output->GetValue()));wxTheClipboard->Close();}});
        cancel_button->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){if(job)job->cancel=true;});
        Bind(wxEVT_TIMER,&Frame::tick,this); timer.Start(200);
        Bind(wxEVT_CLOSE_WINDOW,[this](wxCloseEvent& event){if(job){closing=true;job->cancel=true;if(event.CanVeto())event.Veto();else{worker.join();job.reset();event.Skip();}}else event.Skip();});
        Bind(wxEVT_CHAR_HOOK,[this](wxKeyEvent& event){if(event.GetKeyCode()==WXK_ESCAPE&&job)job->cancel=true;else event.Skip();});
        wxAcceleratorEntry accelerators[1]; accelerators[0].Set(wxACCEL_CTRL,WXK_RETURN,Process); SetAcceleratorTable(wxAcceleratorTable(1,accelerators));
        menu(Process,[this]{process();}); menu(OpenAudio,[this]{open_audio();}); menu(wxID_EXIT,[this]{Close();}); menu(Advanced,[this]{advanced();}); menu(StemSettings,[this]{credentials(false);}); menu(LyricsSettings,[this]{credentials(true);}); menu(Blacklist,[this]{blacklist();}); menu(Support,[this]{tickets();}); menu(Updates,[this]{update();});
        menu(English,[this]{change_language("en");}); menu(Indonesian,[this]{change_language("id");});
        menu(Guide,[this]{read_dialog(this,"guide",t("guide_text").ToStdString(wxConvUTF8));}); menu(Protocol,[this]{read_dialog(this,"protocol",t("protocol_text").ToStdString(wxConvUTF8));}); menu(About,[this]{read_dialog(this,"about",std::string("GhostWave Studio ")+version+"\n\n"+t("about_text").ToStdString(wxConvUTF8));});
        menu(Changelog,[this]{start([](Job& job){job.output=changelog(true,http,&job.cancel);},[this](Job& job){read_dialog(this,"changelog",job.output);});});
        menu(ExportProfile,[this]{sync();wxFileDialog dialog(this,t("export_profile"),{},"ghostwave.sn","Profiles (*.sn)|*.sn",wxFD_SAVE|wxFD_OVERWRITE_PROMPT);if(dialog.ShowModal()==wxID_OK)profile.save(fs::path(dialog.GetPath().ToStdWstring()));});
        menu(ImportProfile,[this]{wxFileDialog dialog(this,t("import_profile"),{},{} ,"Profiles (*.sn)|*.sn",wxFD_OPEN|wxFD_FILE_MUST_EXIST);if(dialog.ShowModal()==wxID_OK){auto imported=Profile::decode(read_bytes(fs::path(dialog.GetPath().ToStdWstring())));imported.path=profile.path;imported.save();profile=std::move(imported);restore();}});
        menu(OpenLyrics,[this]{wxFileDialog dialog(this,t("open_lyrics"),{},{} ,"Text files (*.txt)|*.txt",wxFD_OPEN|wxFD_FILE_MUST_EXIST);if(dialog.ShowModal()==wxID_OK){auto bytes=read_bytes(fs::path(dialog.GetPath().ToStdWstring()),16*1024*1024);lyrics_input->SetValue(wxString::FromUTF8(std::string(bytes.begin(),bytes.end())));tabs->SetSelection(1);}});
        menu(SaveLyrics,[this]{wxFileDialog dialog(this,t("save_lyrics"),{},"lyrics.txt","Text files (*.txt)|*.txt",wxFD_SAVE|wxFD_OVERWRITE_PROMPT);if(dialog.ShowModal()==wxID_OK){auto content=text(lyrics_output);write_atomic(fs::path(dialog.GetPath().ToStdWstring()),Bytes(content.begin(),content.end()));}});
        menu(Slice,[this]{auto source=fs::u8path(text(input));wxDirDialog dialog(this,t("slice"));if(dialog.ShowModal()!=wxID_OK)return;auto directory=fs::path(dialog.GetPath().ToStdWstring());double duration=profile.data["slicer_chunk_duration"];start([source,directory,duration](Job& job){for(const auto& file:slice_audio(source,directory,duration,&job.cancel))job.output+=file.u8string()+"\n";},[this](Job& job){read_dialog(this,"slice",job.output);});});
        menu(Audit,[this]{auto source=fs::u8path(text(input)),destination=fs::u8path(text(output));start([source,destination](Job& job){job.json=audit_audio(source,destination,&job.cancel);},[this](Job& job){read_dialog(this,"audit",job.json.dump(2)+"\n\n"+t("audit_note").ToStdString(wxConvUTF8));});});
        input->SetFocus();
        if(profile.recovered) wxMessageBox(t("recovered"),t("import_profile"),wxOK|wxICON_INFORMATION,this);
    }
    ~Frame() override { timer.Stop();if(job)job->cancel=true;if(worker.joinable())worker.join(); }
    void startup() { if(profile.data["check_updates_on_startup"].get<bool>()) update(true); }
    void open_audio() { if(job)return;wxFileDialog dialog(this,t("open_audio"),{},{} ,"Audio files|*.wav;*.mp3;*.flac;*.ogg;*.m4a;*.aac;*.wma|All files|*.*",wxFD_OPEN|wxFD_FILE_MUST_EXIST);if(dialog.ShowModal()==wxID_OK)set_audio(dialog.GetPath()); }
    void set_audio(const wxString& path) { input->SetValue(path);output->Clear();tabs->SetSelection(0);auto file=fs::path(path.ToStdWstring());start([file](Job& job){job.json=inspect_audio(file);},[this](Job& job){status->SetLabel(wxString::Format("%.1f s, %d Hz, %d",job.json["duration_seconds"].get<double>(),job.json["sample_rate"].get<int>(),job.json["channels"].get<int>()));}); }
};
bool AudioDrop::OnDropFiles(wxCoord,wxCoord,const wxArrayString& files) { if(files.empty())return false;frame->set_audio(files[0]);return true; }
class App : public wxApp {
public:
    bool OnInit() override {
        try {
            auto profile=Profile::load();
            auto lang=profile.data["language"].get<std::string>(); if(lang!="en"&&lang!="id")lang="en";
            catalog=read_json(app_dir()/fs::u8path("resources/ui-"+lang+".json"));
            if(profile.data["first_run"].get<bool>()) {
                wxSingleChoiceDialog dialog(nullptr,t("language"),"GhostWave Studio",wxArrayString{"English","Bahasa Indonesia"});dialog.SetSelection(lang=="id"?1:0);
                if(dialog.ShowModal()==wxID_OK) { lang=dialog.GetSelection()==1?"id":"en";profile.data["language"]=lang;catalog=read_json(app_dir()/fs::u8path("resources/ui-"+lang+".json")); }
                profile.data["first_run"]=false; if(!profile.recovered)profile.save();
            }
            auto frame=new Frame(std::move(profile));frame->Show();SetTopWindow(frame);frame->startup();return true;
        } catch(const std::exception& error) { wxMessageBox(wxString::FromUTF8(error.what()),"GhostWave Studio",wxOK|wxICON_ERROR);return false; }
    }
};
}
wxIMPLEMENT_APP_NO_MAIN(App);
int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,LPSTR command,int show) {
    int count=0;auto arguments=CommandLineToArgvW(GetCommandLineW(),&count);
    std::vector<std::string> args;for(int i=1;i<count;++i)args.push_back(gw::utf8(arguments[i]));LocalFree(arguments);
    return gw::command_line(args,[&]{return wxEntry(instance,previous,command,show);});
}
