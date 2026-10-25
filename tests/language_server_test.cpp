#include "../src/language_server.hpp"
#include <iostream>
#include <filesystem>
#include <fstream>
using satr::LspJson;
int wmain(int argc,wchar_t** argv){
    if(argc!=3&&argc!=4)return 2;
    satr::LanguageServer server;std::string error;
    std::wstring compiler=argv[1],root=argv[2];auto clangd=std::filesystem::path(compiler).parent_path()/"clangd.exe";
    if(argc==4)clangd=argv[3];
    if(!server.Start(clangd.wstring(),compiler,root,error)){std::cerr<<error;return 1;}
    auto begun=GetTickCount64();while(!server.Ready()&&!server.Failed()&&GetTickCount64()-begun<15000){server.Poll();Sleep(10);}
    if(!server.Ready()){std::cerr<<"initialize failed";return 1;}
    auto utf8=[](const std::wstring& value){int n=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);std::string out(n,0);WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),out.data(),n,nullptr,nullptr);return out;};
    auto file=utf8(root+L"\\semantic.cpp"),driver=utf8(compiler);int failures=0;
    int checks=0;
    auto check=[&](const std::string& source,const std::string& wanted,const std::string& label,bool c=false){
        bool complete=false,found=false;LspJson reply;
        int line=static_cast<int>(std::count(source.begin(),source.end(),'\n'));auto last=source.rfind('\n');int column=static_cast<int>(last==std::string::npos?source.size():source.size()-last-1);
        auto sourceFile=c?utf8(root+L"\\لغة c.c"):file;auto sourceDriver=c?utf8((std::filesystem::path(compiler).parent_path()/"gcc.exe").wstring()):driver;
        server.Complete(sourceFile,source,{sourceDriver,c?"-std=c17":"-std=c++17",sourceFile},utf8(root),line,column,[&](const LspJson& result){reply=result;complete=true;});
        auto start=GetTickCount64();while(!complete&&!server.Failed()&&GetTickCount64()-start<10000){server.Poll();Sleep(10);}
        auto items=reply.is_array()?reply:reply.is_object()?reply.value("items",LspJson::array()):LspJson::array();
        for(const auto& item:items){if(item.value("filterText",item.value("label",""))==wanted){found=true;std::cout<<item.dump()<<'\n';}}
        ++checks;std::cout<<(found?"PASS ":"FAIL ")<<label<<'\n';if(!found){++failures;std::cout<<reply.dump().substr(0,5000)<<'\n';}
    };
    check("#include <cstdio>\nint main(){ std::pri","printf","standard_library_printf");
    check("#include <vector>\nint main(){std::vector<int> values;values.pu","push_back","typed_vector_member");
    check("struct Planet { int orbitDays; void rotate(int speed); };\nint main(){Planet earth;earth.ro","rotate","user_defined_class_member");
    check("#include <cstdio>\nint main(){const char* message=\"مرحبا\";std::pri","printf","utf8_cursor_after_arabic_text");
    check("#include <stdio.h>\nint main(void){pri","printf","c17_unicode_source_path",true);
    std::ofstream(std::filesystem::path(root)/"library.hpp")<<"namespace my_lib {double magnitude(double value);}\n";
    check("#include \"library.hpp\"\nint main(){my_lib::mag","magnitude","user_header_library_function");
    server.Stop();std::cout<<checks<<" semantic checks, "<<failures<<" failed\n";return failures?1:0;
}
