#pragma once
#include <wx/wx.h>
#include <wx/gauge.h>
#include <wx/stdpaths.h>
#include <nlohmann/json.hpp>
#include "process.hpp"
#include "ui_i18n.hpp"

// One explicit click starts a background, per-user dependency setup. The UI
// parses bounded JSON progress; downloaded packages are never shell commands.
class ToolsSetupDialog : public wxDialog {
    satr::Process process_;
    wxTimer timer_;
    wxStaticText* status_;
    wxGauge* progress_;
    wxButton* cancel_;
    wxTextCtrl* log_;
    std::string pending_;
    bool cancelRequested_=false,cancellable_=true,started_=false;
    nlohmann::json paths_;
    static wxString Txt(const std::string& s){return ui::Text(s);}
    void Tick(wxTimerEvent&){
        pending_+=process_.Poll();
        if(pending_.size()>1024*1024){process_.Stop();cancelRequested_=true;}
        size_t end;
        while((end=pending_.find('\n'))!=std::string::npos){
            auto line=pending_.substr(0,end);pending_.erase(0,end+1);
            auto event=nlohmann::json::parse(line,nullptr,false);
            if(!event.is_object())continue;
            auto message=event.value("message","");if(!message.empty()){status_->SetLabel(Txt(message));status_->Wrap(FromDIP(560));if(log_->GetLastPosition()>32000)log_->Remove(0,16000);log_->AppendText(Txt(message)+"\n");}
            int percent=event.value("percent",-1);if(percent<0)progress_->Pulse();else progress_->SetValue(std::clamp(percent,0,100));
            cancellable_=event.value("cancellable",true);cancel_->Enable(cancellable_);
            if(event.value("kind","")=="complete"&&event.contains("paths"))paths_=event["paths"];
            Layout();
        }
        if(!process_.Active()){
            timer_.Stop();cancellable_=true;cancel_->Enable();
            if(cancelRequested_){EndModal(wxID_CANCEL);return;}
            if(process_.ExitCode()==0&&paths_.is_object()&&paths_.value("ready",false)){EndModal(wxID_OK);return;}
            status_->SetLabel(Txt("لم يكتمل التجهيز. يمكنك إغلاق النافذة والمحاولة مجددًا."));cancel_->SetLabel(Txt("إغلاق"));Layout();
        }
    }
    void Cancel(){
        if(!cancellable_)return;
        if(process_.Active()){cancelRequested_=true;process_.Stop();cancel_->Disable();status_->SetLabel(Txt("جارٍ إلغاء التنزيل…"));}
        else EndModal(wxID_CANCEL);
    }
public:
    const nlohmann::json& Paths()const{return paths_;}
    ToolsSetupDialog(wxWindow* parent,const wxString& script,const wxString& appRoot,const wxString& installRoot,const wxString& cache=wxEmptyString,bool offline=false)
      :wxDialog(parent,wxID_ANY,"Satr Studio — "+Txt("تجهيز أدوات التشغيل"),wxDefaultPosition,parent->FromDIP(wxSize(660,390)),wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER),timer_(this){
        auto* layout=new wxBoxSizer(wxVERTICAL);
        auto* note=new wxStaticText(this,wxID_ANY,Txt("يحمّل الأدوات الناقصة فقط ويضبطها تلقائيًا. قد يطلب ويندوز موافقة لتثبيت Microsoft Visual C++ عند الحاجة."),wxDefaultPosition,wxDefaultSize,wxALIGN_RIGHT);note->Wrap(FromDIP(590));layout->Add(note,0,wxEXPAND|wxALL,FromDIP(16));
        status_=new wxStaticText(this,wxID_ANY,Txt("جارٍ فحص الأدوات…"),wxDefaultPosition,wxDefaultSize,wxALIGN_RIGHT);layout->Add(status_,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,FromDIP(16));
        progress_=new wxGauge(this,wxID_ANY,100);layout->Add(progress_,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,FromDIP(16));
        log_=new wxTextCtrl(this,wxID_ANY,wxEmptyString,wxDefaultPosition,wxDefaultSize,wxTE_MULTILINE|wxTE_READONLY|wxTE_RIGHT);layout->Add(log_,1,wxEXPAND|wxLEFT|wxRIGHT,FromDIP(16));
        cancel_=new wxButton(this,wxID_CANCEL,Txt("إلغاء"));layout->Add(cancel_,0,wxALIGN_RIGHT|wxALL,FromDIP(16));SetSizer(layout);CentreOnParent();
        Bind(wxEVT_TIMER,&ToolsSetupDialog::Tick,this);
        cancel_->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){Cancel();});
        Bind(wxEVT_CLOSE_WINDOW,[this](wxCloseEvent& e){if(!cancellable_){e.Veto();return;}e.Veto();Cancel();});
        CallAfter([this,script,appRoot,installRoot,cache,offline]{
            if(started_)return;
            started_=true;
            wchar_t system[MAX_PATH];GetSystemDirectoryW(system,MAX_PATH);
            std::wstring executable=std::wstring(system)+L"\\WindowsPowerShell\\v1.0\\powershell.exe";
            std::vector<std::wstring> args={L"-NoLogo",L"-NoProfile",L"-NonInteractive",L"-ExecutionPolicy",L"Bypass",L"-File",script.ToStdWstring(),L"-AppRoot",appRoot.ToStdWstring(),L"-InstallRoot",installRoot.ToStdWstring()};
            if(!cache.empty()){args.push_back(L"-CacheRoot");args.push_back(cache.ToStdWstring());}if(offline)args.push_back(L"-Offline");
            std::string error;
            if(!process_.Start(executable,args,appRoot.ToStdWstring(),error)){status_->SetLabel(Txt("تعذر بدء التجهيز: "+error));cancel_->SetLabel(Txt("إغلاق"));return;}
            process_.EndInput();timer_.Start(80);
        });
    }
    ~ToolsSetupDialog(){timer_.Stop();process_.Stop();}
};
