#include "../src/process.hpp"
#include "../src/repair.hpp"
#include "../src/language_server.hpp"
#include <fstream>
#include <iostream>
#include <thread>
#include <stdexcept>
using namespace satr;
static int checks=0;
static void Check(bool ok,const char* name){++checks;std::cout<<(ok?"PASS ":"FAIL ")<<name<<std::endl;if(!ok)throw std::runtime_error(name);}
static std::string Wait(Process& p,int limit=10000){std::string out;auto end=GetTickCount64()+limit;while(p.Active()&&GetTickCount64()<end){out+=p.Poll();usleep(2000);}if(p.Active())throw std::runtime_error("timeout");return out;}
int main(int argc,char** argv){
    if(argc>1){std::string mode=argv[1];
        if(mode=="--wait"){sleep(60);return 0;}
        if(mode=="--echo"){std::string line;while(std::getline(std::cin,line))std::cout<<line<<"\n";std::cerr<<"ERR\n";return 7;}
        if(mode=="--flood"){for(int i=0;i<512;++i)std::cout<<std::string(4096,'F');std::cout<<"END";return 0;}
        if(mode=="--tree"){pid_t pid=fork();if(pid==0){sleep(60);_exit(0);}std::cout<<pid<<std::endl;sleep(60);return 0;}
        if(mode=="--args"){for(int i=2;i<argc;++i)std::cout<<argv[i]<<"\n";return 0;}
    }
    try{
        auto self=ToWide(std::filesystem::canonical("/proc/self/exe").string());std::string error;
        Process p;Check(!p.Start(L"/missing-satr-executable",{},L"/tmp",error)&&!error.empty(),"exec failure reported immediately");
        Check(p.Start(self,{L"--args",L"hello world",L"$(touch /tmp/satr-injection)",L"ملف عربي",L"quote\"test"},L"/tmp",error),"argv process starts");p.EndInput();
        auto out=Wait(p);Check(out=="hello world\n$(touch /tmp/satr-injection)\nملف عربي\nquote\"test\n","Unicode and metacharacters preserved literally");Check(!std::filesystem::exists("/tmp/satr-injection"),"no command interpolation");
        Check(p.Start(self,{L"--echo"},L"/tmp",error),"interactive child starts");Check(p.Send("hello\n"),"nonblocking input accepted");p.EndInput();out=Wait(p);Check(out.find("hello\n")!=std::string::npos&&out.find("ERR")!=std::string::npos&&p.ExitCode()==7,"stdin EOF output stderr and exit code");
        p.Start(self,{L"--flood"},L"/tmp",error);p.EndInput();out=Wait(p);Check(out.size()==512*4096+3&&out.substr(out.size()-3)=="END","large output drained without truncation");
        p.Start(self,{L"--wait"},L"/tmp",error);auto t=GetTickCount64();bool full=false;for(int i=0;i<100;++i)if(!p.Send(std::string(4096,'x'))){full=true;break;}Check(full&&GetTickCount64()-t<200,"blocked stdin bounded and nonblocking");p.Stop();Wait(p);Check(p.ExitCode()==137,"Stop terminates child");
        p.Start(self,{L"--tree"},L"/tmp",error);out.clear();while(out.find('\n')==std::string::npos){out+=p.Poll();usleep(2000);}auto child=std::stoi(out);p.Stop();Wait(p);usleep(100000);std::ifstream stat("/proc/"+std::to_string(child)+"/stat");std::string state;std::getline(stat,state);Check(!stat||state.find(") Z ")!=std::string::npos,"Stop terminates descendant process");
        std::filesystem::path directory=std::filesystem::u8path("/tmp/satr-test/ملفات تجربة");std::filesystem::create_directories(directory);
        std::ofstream(directory/"main.c")<<"#include <stdio.h>\nint main(void){int n;if(scanf(\"%d\",&n)!=1)return 2;printf(\"%d\\n\",n*2);return 0;}\n";
        Check(p.Start(L"/usr/bin/gcc",{L"main.c",L"-o",L"program"},WidePath(directory),error),"C compilation in Unicode working directory");p.EndInput();out=Wait(p);Check(p.ExitCode()==0,"C source compiles");
        p.Start(WidePath(directory/"program"),{},WidePath(directory),error);p.Send("21\n");p.EndInput();Check(Wait(p)=="42\n"&&p.ExitCode()==0,"compiled program executes with input");
        RepairConfig config;config.compiler=L"/usr/bin/gcc";config.c=true;config.allowAI=false;config.directory=WidePath(directory);config.scratch=WidePath(directory/"repair");RepairTask task;
        Check(task.Start(config,"#include <stdio.h>\nint main(void){int count=4;printf(\"%d\",coutn);return 0;}\n"),"compiler repair starts");t=GetTickCount64();while(task.Busy()&&GetTickCount64()-t<15000){task.Poll();usleep(2000);}Check(task.Status()==RepairTask::State::Ready&&task.Candidate().find(",count)")!=std::string::npos,"compiler typo repair verified on Linux");
        std::cout<<"All "<<checks<<" checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
