#include "../src/process.hpp"
#include <iostream>
#include <filesystem>
int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 2;
    std::filesystem::path app=std::filesystem::absolute(argv[1]),work=std::filesystem::absolute(argv[2]);
    satr::Process p;std::string error;
    wchar_t system[MAX_PATH];GetSystemDirectoryW(system,MAX_PATH);
    std::vector<std::wstring> args={L"-NoLogo",L"-NoProfile",L"-NonInteractive",L"-ExecutionPolicy",L"Bypass",L"-File",(app/L"setup"/L"SetupTools.ps1").wstring(),L"-AppRoot",app.wstring(),L"-InstallRoot",work.wstring(),L"-Offline"};
    if(!p.Start(std::wstring(system)+L"\\WindowsPowerShell\\v1.0\\powershell.exe",args,app.wstring(),error)){std::cout<<error;return 1;}
    p.EndInput();auto begun=GetTickCount64();while(p.Active()&&GetTickCount64()-begun<60000){std::cout<<p.Poll()<<std::flush;Sleep(35);}std::cout<<"\nEXIT "<<p.ExitCode()<<std::endl;return p.ExitCode()==0?0:1;
}
