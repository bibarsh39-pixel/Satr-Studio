#include "../src/native_runtime.hpp"
#include "../src/process.hpp"
#include <iostream>
#include <thread>
namespace fs=std::filesystem;
static int failed=0,checks=0;
void check(bool okay,const char* name){++checks;if(!okay)++failed;std::cout<<(okay?"PASS ":"FAIL ")<<name<<std::endl;}
int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 2;
    auto root=fs::absolute(argv[2])/std::to_string(GetCurrentProcessId());fs::create_directories(root);
    auto source=root/L"toolchain",stage=root/L"runtime & (اختبار)";fs::create_directories(source);fs::create_directories(stage);
    satr::NativeRuntime runtime;
    check(!runtime.Prepare(root/"missing-toolchain"/"g++.exe",stage),"missing_runtime_selects_static_fallback");
    for(auto name:{"libstdc++-6.dll","libgcc_s_seh-1.dll","libwinpthread-1.dll"})std::ofstream(source/name)<<"original runtime";
    check(runtime.Prepare(source/"g++.exe",stage)&&satr::CacheRead(stage/"libstdc++-6.dll")=="original runtime","runtime_copied_beside_program_in_unicode_path");
    auto before=fs::last_write_time(stage/"libstdc++-6.dll");
    check(runtime.Prepare(source/"g++.exe",stage)&&fs::last_write_time(stage/"libstdc++-6.dll")==before,"unchanged_runtime_not_rewritten");
    std::ofstream(stage/"libstdc++-6.dll")<<"corrupt";
    check(runtime.Prepare(source/"g++.exe",stage)&&satr::CacheRead(stage/"libstdc++-6.dll")=="original runtime","modified_destination_repaired");
    std::ofstream(source/"libstdc++-6.dll")<<"new runtime";
    check(runtime.Prepare(source/"g++.exe",stage)&&satr::CacheRead(stage/"libstdc++-6.dll")=="new runtime","changed_toolchain_runtime_refreshed");
    // Replace the synthetic test data with the real selected MinGW runtime.
    auto compiler=fs::absolute(argv[1]);
    check(runtime.Prepare(compiler,stage),"real_selected_compiler_runtime_prepared");
    auto code=root/"probe.cpp",linked=root/"probe.exe",program=stage/L"probe.exe";
    std::ofstream(code)<<R"CPP(
#include <windows.h>
#include <iostream>
#include <filesystem>
#include <thread>
#include <stdexcept>
int main(){
 int value=0;std::thread work([&]{value=42;});work.join();
 try{throw std::runtime_error("expected");}catch(const std::exception& e){if(std::string(e.what())!="expected")return 4;}
 wchar_t executable[32768];GetModuleFileNameW(nullptr,executable,32768);
 auto folder=std::filesystem::path(executable).parent_path();
 for(auto name:{L"libstdc++-6.dll",L"libgcc_s_seh-1.dll",L"libwinpthread-1.dll"}){
  wchar_t path[32768];auto module=GetModuleHandleW(name);
  if(!module||!GetModuleFileNameW(module,path,32768)||!std::filesystem::equivalent(folder,std::filesystem::path(path).parent_path()))return 5;
 }
 std::cout<<"PRIVATE_RUNTIME_OK:"<<value;return value==42?0:6;
})CPP";
    satr::Process process;std::string error;
    bool start=process.Start(satr::WidePath(compiler),{satr::WidePath(code),L"-std=c++17",L"-g0",L"-O0",L"-s",L"-o",satr::WidePath(linked)},satr::WidePath(root),error);
    std::string output;auto until=GetTickCount64()+60000;
    while(start&&process.Active()&&GetTickCount64()<until){output+=process.Poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    check(start&&!process.Active()&&process.ExitCode()==0,"dynamic_probe_builds");
    if(process.Active())process.Stop();
    if(start&&process.ExitCode()==0){
        fs::copy_file(linked,program,fs::copy_options::overwrite_existing);
        auto oldPath=std::wstring(32768,L'\0');auto count=GetEnvironmentVariableW(L"PATH",oldPath.data(),static_cast<DWORD>(oldPath.size()));oldPath.resize(count);
        SetEnvironmentVariableW(L"PATH",L"C:\\Windows\\System32;C:\\Windows");output.clear();
        start=process.Start(satr::WidePath(program),{},satr::WidePath(root),error);until=GetTickCount64()+15000;
        while(start&&process.Active()&&GetTickCount64()<until){output+=process.Poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        SetEnvironmentVariableW(L"PATH",oldPath.c_str());
        check(start&&!process.Active()&&process.ExitCode()==0&&output=="PRIVATE_RUNTIME_OK:42","threads_exceptions_and_exact_private_dlls_without_toolchain_path");
    }else std::cerr<<output<<error;
    std::cout<<checks<<" checks, "<<failed<<" failed\n";return failed?1:0;
}
