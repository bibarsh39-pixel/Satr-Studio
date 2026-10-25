#pragma once
#include "process.hpp"
#include <fstream>
#include "run_session.hpp"
namespace satr {
class ExternalConsole {
    Process terminal_;
    std::filesystem::path result_;
    bool finished_=false,session_=false;
    std::wstring request_;std::string requestId_;unsigned sequence_=0;
    DWORD code_=0;
public:
    ~ExternalConsole(){Stop();}
    bool Active()const{return terminal_.Active();}
    bool HasWindow()const{return Active();}
    bool Finished()const{return finished_;}
    DWORD ExitCode()const{return code_;}
    void Stop(){
        if(!result_.empty()){
            std::ifstream pidfile(result_.string()+".pid");long group=0;pidfile>>group;
            if(group>1&&group!=getpgrp())::kill(-group,SIGKILL);
            std::error_code ec;std::filesystem::remove(result_.string()+".pid",ec);
        }
        terminal_.StopAndWait();std::error_code ec;if(!request_.empty())std::filesystem::remove(FsPath(request_),ec);
    }
    bool Start(const std::wstring& runner,const std::wstring& program,const std::wstring& cwd,const std::wstring& result,std::string& error,bool noPause=false,const std::vector<std::wstring>& programArgs={}){
        terminal_.Poll();auto id=std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+"-"+std::to_string(++sequence_);
        if(!noPause&&session_&&Active()&&finished_){
            if(!WriteRunRequest(request_,id,program,cwd,programArgs)){error="Cannot send run request";return false;}
            requestId_=id;finished_=false;code_=0;return true;
        }
        Stop();terminal_.Poll();session_=!noPause;finished_=false;code_=0;result_=FsPath(result);request_=result+L".request";requestId_=id;std::error_code ec;std::filesystem::remove(result_,ec);
        std::vector<std::wstring> args={L"-T",L"Satr Studio — Run",L"-fa",L"Monospace",L"-fs",L"12",L"-bg",L"#111827",L"-fg",L"#e5e7eb",L"-e",runner,program,cwd,result};
        if(noPause)args.push_back(L"--no-pause");
        args.push_back(L"--");args.insert(args.end(),programArgs.begin(),programArgs.end());
        if(session_){
            if(!WriteRunRequest(request_,requestId_,program,cwd,programArgs)){error="Cannot create run request";return false;}
            args={L"-T",L"Satr Studio — Run",L"-fa",L"Monospace",L"-fs",L"12",L"-bg",L"#111827",L"-fg",L"#e5e7eb",L"-e",runner,L"--session",request_,result};
        }
        if(!terminal_.Start(L"/usr/bin/xterm",args,cwd,error)){error="Cannot open terminal. Install xterm. "+error;return false;}
        terminal_.EndInput();return true;
    }
    void Poll(){
        terminal_.Poll();if(finished_)return;
        if(session_){if(SessionResult(WidePath(result_),requestId_,code_)){finished_=true;return;}if(!Active()){code_=terminal_.ExitCode();Stop();finished_=true;}return;}
        std::ifstream file(result_);unsigned long value;if(file>>value){code_=value;finished_=true;return;}
        if(!Active()){code_=terminal_.ExitCode();finished_=true;}
    }
};
}
