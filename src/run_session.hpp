#pragma once
#include "process.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <chrono>
#include <thread>
#include <vector>
#include <cstdio>
#ifndef _WIN32
#include <sys/wait.h>
#include <poll.h>
#include <termios.h>
#include <signal.h>
#endif
namespace satr {
inline bool WriteSessionFile(const std::wstring& path,const nlohmann::json& value){
    try {
    auto destination=FsPath(path),temporary=destination;temporary+=".tmp";
    auto bytes=value.dump();if(bytes.size()>131072)return false;
    {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out.write(bytes.data(),bytes.size());out.close();if(!out)return false;}
#ifdef _WIN32
    // Requests/results are transient IPC, not durable user documents. Same-volume
    // replacement remains atomic without forcing a disk flush on every Run.
    for(unsigned attempt=0;attempt<8;++attempt){
        if(MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING))return true;
        auto error=GetLastError();if(error!=ERROR_SHARING_VIOLATION&&error!=ERROR_ACCESS_DENIED)break;
        Sleep(2); // A scanner or external reader may briefly hold the old file.
    }
    return false;
#else
    std::error_code ec;std::filesystem::permissions(temporary,std::filesystem::perms::owner_read|std::filesystem::perms::owner_write,std::filesystem::perm_options::replace,ec);if(ec)return false;
    std::filesystem::rename(temporary,destination,ec);return !ec;
#endif
    } catch(...){return false;}
}
inline nlohmann::json ReadSessionFile(const std::wstring& path){
    try{
#ifdef _WIN32
        HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return {};
        LARGE_INTEGER size{};if(!GetFileSizeEx(file,&size)||size.QuadPart<0||size.QuadPart>131072){CloseHandle(file);return {};}
        std::string bytes(static_cast<size_t>(size.QuadPart),'\0');DWORD count=0;
        bool read=ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)!=0;CloseHandle(file);
        if(!read||count!=bytes.size())return {};
#else
        std::ifstream stream(FsPath(path),std::ios::binary|std::ios::ate);auto size=stream.tellg();if(size<0||size>131072)return {};
        stream.seekg(0);std::string bytes(static_cast<size_t>(size),'\0');if(!stream.read(bytes.data(),bytes.size()))return {};
#endif
        return nlohmann::json::parse(bytes,nullptr,false);
    }catch(...){return {};}
}
inline bool WriteRunRequest(const std::wstring& request,const std::string& id,const std::wstring& executable,const std::wstring& cwd,const std::vector<std::wstring>& arguments){
    std::vector<std::string> args;for(const auto& arg:arguments)args.push_back(ToUtf8(arg));
    return WriteSessionFile(request,{{"id",id},{"program",ToUtf8(executable)},{"cwd",ToUtf8(cwd)},{"args",args}});
}
inline bool SessionResult(const std::wstring& result,const std::string& id,DWORD& code){
    try{auto value=ReadSessionFile(result);if(!value.is_object()||value.value("id","")!=id||!value.contains("code")||!value["code"].is_number_unsigned())return false;
    code=value["code"].get<DWORD>();return true;}catch(...){return false;}
}
inline int ConsoleSession(const std::wstring& request,const std::wstring& result,bool prepared=false){
    std::string previous;
#ifdef _WIN32
    SetConsoleTitleW(L"Satr Studio | Program output");SetConsoleOutputCP(CP_UTF8);SetConsoleCP(CP_UTF8);
    auto input=GetStdHandle(STD_INPUT_HANDLE),output=GetStdHandle(STD_OUTPUT_HANDLE);DWORD inputMode=0,outputMode=0;GetConsoleMode(input,&inputMode);GetConsoleMode(output,&outputMode);
    if(prepared){
        // Only prewarm a real classic console window. Pseudoconsole hosts use
        // the ordinary launch path instead of leaving a program invisible.
        RECT rect{};auto window=GetConsoleWindow();
        if(!window||!GetWindowRect(window,&rect)||rect.right<=rect.left||rect.bottom<=rect.top)return 0;
        ShowWindow(window,SW_HIDE); // Consume STARTUPINFO's initial show state.
        if(!WriteSessionFile(result,{{"prepared",true}}))return 2;
    }
    struct Changes {HANDLE handle=INVALID_HANDLE_VALUE;~Changes(){if(handle!=INVALID_HANDLE_VALUE)FindCloseChangeNotification(handle);}} changes;
    changes.handle=FindFirstChangeNotificationW(FsPath(request).parent_path().c_str(),FALSE,FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_LAST_WRITE);
#else
    (void)prepared;
    setpgid(0,0);signal(SIGTTOU,SIG_IGN);
    termios original{};bool hasTermios=tcgetattr(STDIN_FILENO,&original)==0;
#endif
    for(;;){
        auto message=ReadSessionFile(request);bool ready=false;std::string id,program,cwd;std::vector<std::string> args;
        try{if(message.is_object()){id=message.at("id").get<std::string>();program=message.at("program").get<std::string>();cwd=message.at("cwd").get<std::string>();args=message.at("args").get<std::vector<std::string>>();
            ready=!id.empty()&&id.size()<=80&&id!=previous&&!program.empty()&&args.size()<=256&&program.find('\0')==std::string::npos&&cwd.find('\0')==std::string::npos;
            for(const auto& arg:args)if(arg.size()>32768||arg.find('\0')!=std::string::npos)ready=false;}}
        catch(...){ready=false;}
        if(!ready){
#ifdef _WIN32
            INPUT_RECORD event{};DWORD count=0;while(PeekConsoleInputW(input,&event,1,&count)&&count){ReadConsoleInputW(input,&event,1,&count);if(event.EventType==KEY_EVENT&&event.Event.KeyEvent.bKeyDown&&(event.Event.KeyEvent.wVirtualKeyCode==VK_RETURN||event.Event.KeyEvent.wVirtualKeyCode==VK_ESCAPE))return 0;}
#else
            pollfd p{STDIN_FILENO,POLLIN,0};if(poll(&p,1,0)>0){char buffer[256];auto count=read(STDIN_FILENO,buffer,sizeof(buffer));if(count<=0)return 0;for(ssize_t i=0;i<count;++i)if(buffer[i]=='\n'||buffer[i]==27)return 0;}
#endif
#ifdef _WIN32
            if(changes.handle!=INVALID_HANDLE_VALUE){
                HANDLE events[]={changes.handle,input};
                auto signaled=WaitForMultipleObjects(2,events,FALSE,250);
                if(signaled==WAIT_OBJECT_0)FindNextChangeNotification(changes.handle);
                if(signaled==WAIT_FAILED)std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }else std::this_thread::sleep_for(std::chrono::milliseconds(16));
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
#endif
            continue;
        }
        previous=id;DWORD code=127;auto requestBegan=std::chrono::steady_clock::now();
#ifdef _WIN32
        if(prepared){ShowWindow(GetConsoleWindow(),SW_SHOWNORMAL);SetForegroundWindow(GetConsoleWindow());prepared=false;}
        SetConsoleMode(input,inputMode);SetConsoleMode(output,outputMode);FlushConsoleInputBuffer(input);SetConsoleTextAttribute(output,7);
        // Keep earlier runs in scrollback, like a terminal. Clearing the whole
        // console buffer costs more than starting many small programs.
#else
        if(hasTermios)tcsetattr(STDIN_FILENO,TCSANOW,&original);
        tcflush(STDIN_FILENO,TCIFLUSH);
#endif
        std::puts("\nSatr Studio / Run\n----------------------------------------");std::fflush(stdout);
        auto begun=std::chrono::steady_clock::now();
#ifdef _WIN32
        auto native=ToWide(program),working=ToWide(cwd);auto command=QuoteArgument(native);for(const auto& arg:args)command+=L" "+QuoteArgument(ToWide(arg));
        HANDLE job=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        STARTUPINFOW start{};start.cb=sizeof(start);PROCESS_INFORMATION child{};
        if(job&&SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))&&CreateProcessW(native.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_SUSPENDED,nullptr,working.c_str(),&start,&child)){
            if(AssignProcessToJobObject(job,child.hProcess)&&ResumeThread(child.hThread)!=DWORD(-1)){WaitForSingleObject(child.hProcess,INFINITE);GetExitCodeProcess(child.hProcess,&code);}else{code=GetLastError();TerminateProcess(child.hProcess,code);}
            CloseHandle(child.hThread);CloseHandle(child.hProcess);
        }else code=GetLastError();
        if(job)CloseHandle(job);
        SetConsoleMode(input,inputMode);SetConsoleMode(output,outputMode);SetConsoleTextAttribute(output,7);FlushConsoleInputBuffer(input);
#else
        int gate[2];if(pipe(gate)==0){pid_t child=fork();
            if(child==0){close(gate[1]);setpgid(0,0);prctl(PR_SET_PDEATHSIG,SIGKILL);char permit;auto count=read(gate[0],&permit,1);close(gate[0]);if(count!=1)_exit(127);signal(SIGTTOU,SIG_DFL);
                if(chdir(cwd.c_str())!=0){perror("Cannot open directory");_exit(127);}std::vector<char*> native{program.data()};for(auto& arg:args)native.push_back(arg.data());native.push_back(nullptr);execv(program.c_str(),native.data());perror("Cannot start program");_exit(127);}
            close(gate[0]);
            if(child>0){setpgid(child,child);std::ofstream(FsPath(result+L".pid"))<<child;tcsetpgrp(STDIN_FILENO,child);char permit=1;(void)write(gate[1],&permit,1);close(gate[1]);
                int status=0;pid_t waited;do{waited=waitpid(child,&status,0);}while(waited<0&&errno==EINTR);
                code=waited==child?(WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status)):127;
                kill(-child,SIGKILL);tcsetpgrp(STDIN_FILENO,getpgrp());
                if(hasTermios)tcsetattr(STDIN_FILENO,TCSANOW,&original);
                std::error_code ec;std::filesystem::remove(FsPath(result+L".pid"),ec);tcflush(STDIN_FILENO,TCIFLUSH);
            }else close(gate[1]);
        }
#endif
        double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begun).count();
        std::printf("\n----------------------------------------\nExit code: %lu | %.3f seconds\nPress Enter to close, or run again from Satr Studio.\n",static_cast<unsigned long>(code),seconds);std::fflush(stdout);
        if(!WriteSessionFile(result,{{"id",id},{"code",static_cast<uint64_t>(code)},{"seconds",seconds},{"consoleSetupSeconds",std::chrono::duration<double>(begun-requestBegan).count()}}))return 2;
    }
}
}
