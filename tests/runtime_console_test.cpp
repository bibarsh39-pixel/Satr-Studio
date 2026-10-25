#include "../src/external_console.hpp"
#include "../src/java_support.hpp"
#include "../src/process.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
int Test(const std::wstring& app,const std::wstring& scratch){
    auto root=satr::FsPath(scratch)/std::filesystem::u8path("scripts & (ملفات)");std::filesystem::create_directories(root);
    std::ofstream(root/"test.py")<<"import sys,os\nassert sys.stdin.isatty() and sys.stdout.isatty()\nassert sys.argv[1:] == ['hello world', 'a;$()']\nopen('python-console.txt','w').write('PY_CONSOLE_OK')\nprint('PY_CONSOLE_OK')\nsys.exit(7)\n";
    std::ofstream(root/"test.js")<<"const assert=require('node:assert/strict');assert.ok(process.stdin.isTTY && process.stdout.isTTY);assert.deepEqual(process.argv.slice(2),['hello world','a;$()']);require('node:fs').writeFileSync('node-console.txt','NODE_CONSOLE_OK');console.log('NODE_CONSOLE_OK');process.exitCode=7;\n";
#ifdef _WIN32
    auto runner=app+L"/SatrRunner.exe",python=app+L"/toolchain/python-3.14.9/python.exe",node=app+L"/toolchain/node-v24.22.1-win-x64/node.exe";
#else
    auto runner=app+L"/SatrRunner",python=std::wstring(L"/usr/bin/python3"),node=app+L"/toolchain/node-v24.22.1-linux-x64/bin/node";
#endif
    satr::ExternalConsole console;std::string error;
    for(bool py:{true,false}){
        std::vector<std::wstring> args=py?std::vector<std::wstring>{L"-u",L"-X",L"utf8",app+L"/setup/run_python.py",satr::WidePath(root/"test.py")}:std::vector<std::wstring>{satr::WidePath(root/"test.js")};
        args.push_back(L"hello world");args.push_back(L"a;$()");
        if(!console.Start(runner,py?python:node,satr::WidePath(root),satr::WidePath(root/"result.txt"),error,true,args)){std::cerr<<error;return 1;}
        auto until=GetTickCount64()+15000;
        while(!console.Finished()&&GetTickCount64()<until){console.Poll();std::this_thread::sleep_for(std::chrono::milliseconds(10));}
        std::ifstream input(root/(py?"python-console.txt":"node-console.txt"));std::string actual;input>>actual;
        bool ok=console.Finished()&&console.ExitCode()==7&&actual==(py?"PY_CONSOLE_OK":"NODE_CONSOLE_OK");console.Stop();
        std::cout<<(ok?"PASS ":"FAIL ")<<(py?"Python":"JavaScript")<<" external tty, Unicode source path, literal arguments, exit status\n";if(!ok)return 1;
    }
    auto source=root/"JavaConsole.java";
    std::ofstream(source)<<"import java.nio.file.*; public class JavaConsole {public static void main(String[] args) throws Exception {if(System.console()==null)throw new AssertionError(\"No interactive console\");if(args.length!=2||!args[0].equals(\"hello world\")||!args[1].equals(\"a;$()\"))throw new AssertionError(\"Arguments changed\");Files.writeString(Path.of(\"java-console.txt\"),\"JAVA_CONSOLE_OK\");System.out.println(\"JAVA_CONSOLE_OK\");System.exit(7);}}";
#ifdef _WIN32
    auto jdk=satr::FsPath(app)/"toolchain/java-21/jdk-21.0.12.1+1",javac=jdk/"bin/javac.exe",java=jdk/"bin/java.exe";
#else
    auto jdk=std::filesystem::path("/usr/lib/jvm/java-21-openjdk-amd64"),javac=jdk/"bin/javac",java=jdk/"bin/java";
#endif
    auto plan=satr::PrepareJava(source,javac,java,satr::FsPath(scratch)/"profile",false,"");satr::Process compiler;
    if(!compiler.Start(satr::WidePath(javac),plan.compileArgs,satr::WidePath(root),error)){std::cerr<<error;return 1;}
    auto until=GetTickCount64()+30000;std::string compilerOutput;
    while(compiler.Active()&&GetTickCount64()<until){compilerOutput+=compiler.Poll();std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    if(compiler.Active()||compiler.ExitCode()!=0){compiler.Stop();std::cerr<<compilerOutput;return 1;}
    plan.runArgs.push_back(L"hello world");plan.runArgs.push_back(L"a;$()");
    if(!console.Start(runner,satr::WidePath(java),satr::WidePath(root),satr::WidePath(root/"java-result.txt"),error,true,plan.runArgs)){std::cerr<<error;return 1;}
    until=GetTickCount64()+15000;while(!console.Finished()&&GetTickCount64()<until){console.Poll();std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    std::ifstream input(root/"java-console.txt");std::string actual;input>>actual;bool ok=console.Finished()&&console.ExitCode()==7&&actual=="JAVA_CONSOLE_OK";console.Stop();
    std::cout<<(ok?"PASS ":"FAIL ")<<"Java external tty, Unicode source path, literal arguments, exit status\n";if(!ok)return 1;
    return 0;
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){return argc==3?Test(argv[1],argv[2]):2;}
#else
int main(int argc,char** argv){return argc==3?Test(satr::ToWide(argv[1]),satr::ToWide(argv[2])):2;}
#endif
