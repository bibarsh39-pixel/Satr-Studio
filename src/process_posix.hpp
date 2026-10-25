#pragma once
#include "platform.hpp"
#include <algorithm>
#include <vector>
#include <deque>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/prctl.h>

namespace satr {
// An argv-only child process. Pipes are nonblocking and bounded; no shell is
// involved. A process group owns compiler children and is stopped on cleanup.
class Process {
    pid_t pid_=-1;
    int output_=-1,input_=-1;
    bool active_=false,ending_=false,reaped_=false;
    DWORD code_=0;
    std::string pending_;
    static void Close(int& fd){if(fd>=0)::close(fd);fd=-1;}
    void Flush(){
        while(input_>=0&&!pending_.empty()){
            auto count=::write(input_,pending_.data(),pending_.size());
            if(count>0)pending_.erase(0,static_cast<size_t>(count));
            else if(count<0&&errno==EINTR)continue;
            else if(count<0&&(errno==EAGAIN||errno==EWOULDBLOCK))break;
            else {Close(input_);pending_.clear();break;}
        }
        if(ending_&&pending_.empty())Close(input_);
    }
    void Cleanup(){
        if(pid_>0){::kill(-pid_,SIGKILL);if(!reaped_){int status;while(::waitpid(pid_,&status,0)<0&&errno==EINTR){}}}
        pid_=-1;active_=false;Close(input_);Close(output_);pending_.clear();
    }
public:
    Process(){::signal(SIGPIPE,SIG_IGN);}
    Process(const Process&)=delete;Process& operator=(const Process&)=delete;
    ~Process(){Cleanup();}
    bool Active()const{return active_;}
    DWORD ExitCode()const{return code_;}
    bool Start(const std::wstring& executable,const std::vector<std::wstring>& args,const std::wstring& cwd,std::string& error){
        if(active_){error="A process is already running";return false;}
        Cleanup();error.clear();ending_=false;reaped_=false;code_=0;
        std::string exe=ToUtf8(executable),directory=ToUtf8(cwd);
        std::vector<std::string> values{exe};for(const auto& arg:args)values.push_back(ToUtf8(arg));
        std::vector<char*> argv;for(auto& value:values)argv.push_back(value.data());argv.push_back(nullptr);
        int in[2]{-1,-1},out[2]{-1,-1},failure[2]{-1,-1};
        if(pipe2(in,O_CLOEXEC)||pipe2(out,O_CLOEXEC)||pipe2(failure,O_CLOEXEC)){
            error=std::strerror(errno);for(int* pair:{in,out,failure}){Close(pair[0]);Close(pair[1]);}return false;
        }
        pid_=fork();
        if(pid_==0){
            ::close(failure[0]);::setpgid(0,0);::prctl(PR_SET_PDEATHSIG,SIGKILL);
            ::signal(SIGPIPE,SIG_DFL);
            int problem=0;
            if((!directory.empty()&&::chdir(directory.c_str())!=0)||dup2(in[0],STDIN_FILENO)<0||dup2(out[1],STDOUT_FILENO)<0||dup2(out[1],STDERR_FILENO)<0)problem=errno;
            ::close(in[0]);::close(in[1]);::close(out[0]);::close(out[1]);
            if(!problem){::execv(exe.c_str(),argv.data());problem=errno;}
            (void)::write(failure[1],&problem,sizeof(problem));::_exit(127);
        }
        Close(in[0]);Close(out[1]);Close(failure[1]);
        if(pid_<0){error=std::strerror(errno);Close(in[1]);Close(out[0]);Close(failure[0]);return false;}
        ::setpgid(pid_,pid_);
        int problem=0;ssize_t count;do{count=::read(failure[0],&problem,sizeof(problem));}while(count<0&&errno==EINTR);Close(failure[0]);
        input_=in[1];output_=out[0];
        if(count>0){error=std::strerror(problem);Cleanup();return false;}
        fcntl(input_,F_SETFL,fcntl(input_,F_GETFL)|O_NONBLOCK);fcntl(output_,F_SETFL,fcntl(output_,F_GETFL)|O_NONBLOCK);
        active_=true;return true;
    }
    std::string Poll(){
        std::string text;if(!active_)return text;Flush();bool exhausted=false;
        for(int i=0;i<16;++i){char buffer[4096];auto count=::read(output_,buffer,sizeof(buffer));
            if(count>0)text.append(buffer,count);else if(count<0&&errno==EINTR){--i;continue;}else{exhausted=true;break;}}
        if(!reaped_){int status=0;auto result=::waitpid(pid_,&status,WNOHANG);if(result==pid_){reaped_=true;code_=WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);}}
        if(reaped_&&exhausted){::kill(-pid_,SIGKILL);pid_=-1;active_=false;Close(input_);Close(output_);pending_.clear();}
        return text;
    }
    bool Send(const std::string& text){if(!active_||ending_||input_<0||text.size()>4096||pending_.size()+text.size()>65536)return false;pending_+=text;Flush();return true;}
    void EndInput(){ending_=true;Flush();}
    void Stop(){if(pid_>0)::kill(-pid_,SIGKILL);Close(input_);pending_.clear();ending_=true;}
    void StopAndWait(){Cleanup();code_=137;ending_=true;}
};
}
