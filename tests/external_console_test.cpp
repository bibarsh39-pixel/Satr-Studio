// Integration checks for the real Windows console and its owned process tree.
// Build with -std=c++17 -municode -static. Args: SatrRunner.exe scratch-directory.
#include "../src/external_console.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdio>
namespace fs=std::filesystem;
static std::wstring Self(){wchar_t path[32768];DWORD n=GetModuleFileNameW(nullptr,path,32768);return {path,n};}
static int Helper(){
    std::string mode;std::ifstream("mode.txt")>>mode;
    if(mode=="tree"){
        auto self=Self(),command=satr::QuoteArgument(self)+L" --leaf";STARTUPINFOW start{};start.cb=sizeof(start);PROCESS_INFORMATION child{};
        if(!CreateProcessW(self.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&start,&child))return 91;
        std::ofstream("pids.txt")<<GetCurrentProcessId()<<' '<<child.dwProcessId;
        CloseHandle(child.hThread);CloseHandle(child.hProcess);Sleep(INFINITE);return 0;
    }
    DWORD a=0,b=0;
    if(!GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE),&a)||!GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE),&b))return 92;
    if(mode=="input"){
        // Supply input only to this test program's own newly allocated console.
        INPUT_RECORD keys[3]{};const wchar_t chars[]={L'2',L'1',L'\r'};
        for(int i=0;i<3;++i){keys[i].EventType=KEY_EVENT;keys[i].Event.KeyEvent.bKeyDown=TRUE;keys[i].Event.KeyEvent.wRepeatCount=1;keys[i].Event.KeyEvent.uChar.UnicodeChar=chars[i];}
        DWORD count=0;if(!WriteConsoleInputW(GetStdHandle(STD_INPUT_HANDLE),keys,3,&count)||count!=3)return 93;
        int n=0;if(std::scanf("%d",&n)!=1)return 94;
        return n==21?42:95;
    }
    std::ofstream("visible.txt")<<(IsWindowVisible(GetConsoleWindow())?1:0);
    std::puts("Satr external console integration check");return 23;
}
static int checks=0,failures=0;
static void Check(bool okay,const char* name){++checks;if(!okay)++failures;std::cout<<(okay?"PASS ":"FAIL ")<<name<<std::endl;}
static bool Finish(satr::ExternalConsole& run){auto start=GetTickCount64();while(GetTickCount64()-start<10000){run.Poll();if(run.Finished())return true;Sleep(20);}return false;}
int wmain(int argc,wchar_t** argv){
    if(argc==2&&std::wstring(argv[1])==L"--leaf"){Sleep(INFINITE);return 0;}
    if(argc==1)return Helper();
    if(argc!=3)return 2;
    auto root=fs::absolute(argv[2])/L"console & (اختبار)";fs::create_directories(root);
    auto program=root/L"program & (sample).exe",result=root/L"exit result.txt";
    fs::copy_file(Self(),program,fs::copy_options::overwrite_existing);
    auto runner=fs::absolute(argv[1]).wstring();satr::ExternalConsole run;std::string error;
    run.PrepareAsync(runner,root.wstring(),result.wstring());
    auto prepareStarted=GetTickCount64();
    while(run.Preparing()&&GetTickCount64()-prepareStarted<3000){run.Poll();Sleep(10);}
    Check(run.Prepared()&&!run.HasWindow(),"prewarmed_console_is_ready_and_hidden");
    std::ofstream(root/L"mode.txt")<<"quick";
    Check(run.Start(runner,program.wstring(),root.wstring(),result.wstring(),error),"launch_with_unicode_spaces_shell_characters");
    Check(Finish(run)&&run.ExitCode()==23,"actual_console_and_nonzero_exit_code");
    int visible=0;std::ifstream(root/L"visible.txt")>>visible;Check(visible==1,"prepared_console_becomes_visible_on_run");
    Sleep(700);Check(run.Active(),"console_stays_open_after_program_exit");
    std::ofstream(root/L"mode.txt")<<"input";
    auto reusedStarted=GetTickCount64();
    Check(run.Start(runner,program.wstring(),root.wstring(),result.wstring(),error)&&Finish(run)&&run.ExitCode()==42,"reused_console_interactive_stdin_fresh_exit_status");
    std::cout<<"Reused console request to result: "<<(GetTickCount64()-reusedStarted)<<" ms\n";
    Check(run.Active(),"reused_console_still_open");run.Stop();Check(!run.Active(),"close_completed_console");
    std::ofstream(root/L"mode.txt")<<"input";
    Check(run.Start(runner,program.wstring(),root.wstring(),result.wstring(),error,true)&&Finish(run)&&run.ExitCode()==42,"interactive_stdin_and_runner_reuse");run.Stop();
    std::ofstream(root/L"mode.txt")<<"tree";
    Check(run.Start(runner,program.wstring(),root.wstring(),result.wstring(),error),"launch_long_running_program");
    DWORD parent=0,child=0;auto began=GetTickCount64();
    while(GetTickCount64()-began<5000){std::ifstream(root/L"pids.txt")>>parent>>child;if(child)break;Sleep(20);}
    HANDLE parentHandle=parent?OpenProcess(SYNCHRONIZE,FALSE,parent):nullptr,childHandle=child?OpenProcess(SYNCHRONIZE,FALSE,child):nullptr;
    run.Stop();Check(parentHandle&&WaitForSingleObject(parentHandle,3000)==WAIT_OBJECT_0,"stop_running_external_program");
    Check(childHandle&&WaitForSingleObject(childHandle,3000)==WAIT_OBJECT_0,"stop_external_descendant");
    if(parentHandle)CloseHandle(parentHandle);
    if(childHandle)CloseHandle(childHandle);
    Check(!run.Start((root/L"missing.exe").wstring(),program.wstring(),root.wstring(),result.wstring(),error)&&!error.empty(),"missing_runner_reports_failure");
    std::cout<<checks<<" checks, "<<failures<<" failed\n";return failures?1:0;
}
