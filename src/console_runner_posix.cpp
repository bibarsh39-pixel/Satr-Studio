#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fstream>
#include <iostream>
#include <chrono>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <vector>
#include "run_session.hpp"
int main(int argc,char** argv){
    if(argc<4)return 2;
    if(std::string(argv[1])=="--session")return satr::ConsoleSession(satr::ToWide(argv[2]),satr::ToWide(argv[3]));
    if(chdir(argv[2])!=0){std::cerr<<"Cannot open working directory: "<<strerror(errno)<<"\n";return 2;}
    setpgid(0,0);std::ofstream(std::string(argv[3])+".pid")<<getpgrp();
    std::cout<<"SATR STUDIO / RUN\n\n"<<std::flush;
    bool noPause=false;int first=4;if(first<argc&&std::string(argv[first])=="--no-pause"){noPause=true;++first;}if(first<argc&&std::string(argv[first])=="--")++first;
    std::vector<char*> args{argv[1]};for(int i=first;i<argc;++i)args.push_back(argv[i]);args.push_back(nullptr);
    auto start=std::chrono::steady_clock::now();pid_t child=fork();int code=127;
    if(child==0){execv(argv[1],args.data());perror("Cannot start program");_exit(127);}
    if(child>0){int status=0;while(waitpid(child,&status,0)<0&&errno==EINTR){}code=WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);}
    auto temporary=std::string(argv[3])+".tmp";{std::ofstream result(temporary);result<<code<<"\n";}std::rename(temporary.c_str(),argv[3]);
    double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::cout<<"\n----------------------------------------\nExit code: "<<code<<" | "<<seconds<<" seconds\n";
    if(!noPause&&isatty(STDIN_FILENO)){std::cout<<"Press Enter to close this window."<<std::flush;std::string line;std::getline(std::cin,line);}
    std::remove((std::string(argv[3])+".pid").c_str());return code;
}
