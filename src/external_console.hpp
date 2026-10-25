#pragma once
#ifndef _WIN32
#include "external_console_posix.hpp"
#else
#pragma once
#include "process.hpp"
#include <cstdlib>
#include <future>
#include <memory>
#include "run_session.hpp"
namespace satr {
// The visible runner and its program share an owned job and a real console.
// Paths are arguments to CreateProcessW, never shell commands.
class ExternalConsole {
    HANDLE process_=nullptr,job_=nullptr;
    std::wstring result_;
    bool finished_=false,session_=false,prepared_=false;
    std::wstring request_;std::string requestId_;unsigned sequence_=0;
    DWORD code_=0;
    std::future<std::unique_ptr<ExternalConsole>> preparation_;
    void AdoptPreparation(){
        if(!preparation_.valid())return;
        try{auto ready=preparation_.get();if(!ready||!ready->Active())return;
            std::swap(process_,ready->process_);std::swap(job_,ready->job_);
            std::swap(result_,ready->result_);std::swap(request_,ready->request_);std::swap(requestId_,ready->requestId_);
            std::swap(finished_,ready->finished_);std::swap(session_,ready->session_);std::swap(prepared_,ready->prepared_);std::swap(code_,ready->code_);
        }catch(...){} // Prewarming is optional; normal Run is always available.
    }
public:
    ~ExternalConsole(){Stop();}
    bool Active()const{return process_&&WaitForSingleObject(process_,0)==WAIT_TIMEOUT;}
    bool HasWindow()const{return Active()&&!prepared_;}
    bool Preparing()const{return preparation_.valid();}
    bool Prepared()const{return Active()&&prepared_;}
    bool Finished()const{return finished_;}
    DWORD ExitCode()const{return code_;}
    void Stop(){if(preparation_.valid()){try{preparation_.get().reset();}catch(...){}}if(!request_.empty())DeleteFileW(request_.c_str());if(job_){TerminateJobObject(job_,130);CloseHandle(job_);job_=nullptr;}if(process_){CloseHandle(process_);process_=nullptr;}prepared_=false;}
    void PrepareAsync(const std::wstring& runner,const std::wstring& cwd,const std::wstring& result){
        if(Active()||preparation_.valid())return;
        Stop();
        try{preparation_=std::async(std::launch::async,[runner,cwd,result]{
            auto ready=std::make_unique<ExternalConsole>();std::string error;
            if(!ready->Start(runner,L"",cwd,result,error,false,{},true))return std::unique_ptr<ExternalConsole>();
            auto until=GetTickCount64()+1000;
            while(ready->Active()&&GetTickCount64()<until){
                auto response=ReadSessionFile(result);
                if(response.is_object()&&response.value("prepared",false))return ready;
                Sleep(10);
            }
            return std::unique_ptr<ExternalConsole>();
        });}catch(...){}
    }
    bool Start(const std::wstring& runner,const std::wstring& program,const std::wstring& cwd,const std::wstring& result,std::string& error,bool noPause=false,const std::vector<std::wstring>& args={},bool prepareOnly=false){
        AdoptPreparation();
        auto id=std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+"-"+std::to_string(++sequence_);
        if(!noPause&&session_&&Active()&&finished_){
            AllowSetForegroundWindow(GetProcessId(process_));
            if(!WriteRunRequest(request_,id,program,cwd,args)){error="Cannot send run request";return false;}
            requestId_=id;finished_=false;prepared_=false;code_=STILL_ACTIVE;return true;
        }
        Stop();session_=!noPause;finished_=false;code_=STILL_ACTIVE;result_=result;request_=result+L".request";requestId_=id;DeleteFileW(result.c_str());
        job_=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!job_||!SetInformationJobObject(job_,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){error="Cannot create console job";Stop();return false;}
        std::wstring command=QuoteArgument(runner)+L" "+QuoteArgument(program)+L" "+QuoteArgument(cwd)+L" "+QuoteArgument(result)+(noPause?L" --no-pause":L"");
        command+=L" --";for(const auto& arg:args)command+=L" "+QuoteArgument(arg);
        if(session_){
            if(prepareOnly)DeleteFileW(request_.c_str());
            else if(!WriteRunRequest(request_,requestId_,program,cwd,args)){error="Cannot create run request";Stop();return false;}
            command=QuoteArgument(runner)+(prepareOnly?L" --prepared-session ":L" --session ")+QuoteArgument(request_)+L" "+QuoteArgument(result_);
        }
        prepared_=prepareOnly;finished_=prepareOnly;
        STARTUPINFOW start{};start.cb=sizeof(start);start.dwFlags=STARTF_USESHOWWINDOW;start.wShowWindow=prepareOnly?SW_HIDE:SW_SHOWNORMAL;PROCESS_INFORMATION pi{};
        if(!CreateProcessW(runner.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NEW_CONSOLE|CREATE_SUSPENDED,nullptr,cwd.c_str(),&start,&pi)){error="Cannot open console: "+std::to_string(GetLastError());Stop();return false;}
        if(!AssignProcessToJobObject(job_,pi.hProcess)){error="Cannot own console process";TerminateProcess(pi.hProcess,1);CloseHandle(pi.hProcess);CloseHandle(pi.hThread);Stop();return false;}
        process_=pi.hProcess;
        if(ResumeThread(pi.hThread)==static_cast<DWORD>(-1)){error="Cannot start console process";CloseHandle(pi.hThread);Stop();return false;}
        CloseHandle(pi.hThread);return true;
    }
    void Poll(){
        if(preparation_.valid()&&preparation_.wait_for(std::chrono::seconds(0))==std::future_status::ready)AdoptPreparation();
        if(finished_||!process_)return;
        if(session_){if(SessionResult(result_,requestId_,code_)){finished_=true;return;}if(!Active()){GetExitCodeProcess(process_,&code_);finished_=true;}return;}
        HANDLE file=CreateFileW(result_.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file!=INVALID_HANDLE_VALUE){char text[32]{};DWORD count=0;bool read=ReadFile(file,text,sizeof(text)-1,&count,nullptr)!=0;CloseHandle(file);if(read&&count){code_=static_cast<DWORD>(std::strtoul(text,nullptr,10));finished_=true;return;}}
        if(!Active()){GetExitCodeProcess(process_,&code_);finished_=true;}
    }
};
}

#endif
