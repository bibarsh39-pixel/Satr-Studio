#include <windows.h>
#include <cstdio>
#include <string>
#include "process.hpp"
#include "run_messages.hpp"
#include "run_session.hpp"
static void SaveResult(const std::wstring& path,DWORD code){
    auto temporary=path+L".tmp";HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;
    std::string text=std::to_string(code);DWORD written=0;bool okay=WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)&&written==text.size();CloseHandle(file);
    if(okay)MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
int wmain(int argc,wchar_t** argv){
    if(argc<4)return 2;
    if(std::wstring(argv[1])==L"--session")return satr::ConsoleSession(argv[2],argv[3]);
    if(std::wstring(argv[1])==L"--prepared-session")return satr::ConsoleSession(argv[2],argv[3],true);
    SetConsoleTitleW(L"Satr Studio | Program output");SetConsoleOutputCP(CP_UTF8);SetConsoleCP(CP_UTF8);
    HANDLE output=GetStdHandle(STD_OUTPUT_HANDLE);SetConsoleTextAttribute(output,FOREGROUND_GREEN|FOREGROUND_BLUE|FOREGROUND_INTENSITY);
    std::puts("  Satr Studio  /  Run\n  ------------------------------------------------------------\n");SetConsoleTextAttribute(output,7);std::fflush(stdout);
    std::wstring command=satr::QuoteArgument(argv[1]);STARTUPINFOW start{};start.cb=sizeof(start);PROCESS_INFORMATION child{};DWORD code=1;ULONGLONG begun=GetTickCount64();
    bool noPause=false;int first=4;
    if(first<argc&&std::wstring(argv[first])==L"--no-pause"){noPause=true;++first;}
    if(first<argc&&std::wstring(argv[first])==L"--")++first;
    for(int i=first;i<argc;++i)command+=L" "+satr::QuoteArgument(argv[i]);
    if(CreateProcessW(argv[1],command.data(),nullptr,nullptr,TRUE,0,nullptr,argv[2],&start,&child)){
        CloseHandle(child.hThread);WaitForSingleObject(child.hProcess,INFINITE);GetExitCodeProcess(child.hProcess,&code);CloseHandle(child.hProcess);
    }else{code=GetLastError();std::printf("Could not start program. Windows error: %lu\n",code);}
    SetConsoleTextAttribute(output,FOREGROUND_GREEN|FOREGROUND_BLUE|FOREGROUND_INTENSITY);
    std::printf("\n\n  ------------------------------------------------------------\n  Exit code: %lu (0x%08lX)  |  %.3f seconds\n",code,code,(GetTickCount64()-begun)/1000.0);SetConsoleTextAttribute(output,7);
    std::printf("  %s\n",satr::RunMessage(code));
    if(!noPause){
        // Keys left over from program input must not dismiss the result window.
        HANDLE input=CreateFileW(L"CONIN$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        if(input!=INVALID_HANDLE_VALUE)FlushConsoleInputBuffer(input);
        std::fflush(stdout);DWORD written=0;const wchar_t* prompt=L"  Press any key to close this window.\r\n";
        WriteConsoleW(output,prompt,static_cast<DWORD>(wcslen(prompt)),&written,nullptr);SaveResult(argv[3],code);
        // Read the actual console buffer: a GUI-launched process may have no
        // usable CRT conio input stream even though its Windows console exists.
        if(input!=INVALID_HANDLE_VALUE){
            INPUT_RECORD event{};DWORD count=0;
            while(ReadConsoleInputW(input,&event,1,&count)){
                if(count&&event.EventType==KEY_EVENT&&event.Event.KeyEvent.bKeyDown){
                    WORD key=event.Event.KeyEvent.wVirtualKeyCode;
                    if(key!=VK_SHIFT&&key!=VK_CONTROL&&key!=VK_MENU)break;
                }
            }
            CloseHandle(input);
        }
    }else SaveResult(argv[3],code);
    return static_cast<int>(code);
}
