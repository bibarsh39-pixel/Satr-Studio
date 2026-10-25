#include "../src/build_cache.hpp"
#include <iostream>
int main(int argc,char** argv){
    if(argc!=3)return 2;
    auto text=satr::CacheRead(std::filesystem::u8path(argv[1]));
    if(text.find("#pragma GCC pch_preprocess")==std::string::npos)return 3;
    auto object=satr::ToWide(argv[2]);satr::BuildCache cache;std::string output;
    cache.Commit(object,"same-compiler-and-flags",text,"");
    if(cache.UnitHit(object,"same-compiler-and-flags",text,output))return 4;
    std::cout<<"PASS real PCH input bypasses reuse even when preprocessed source is identical\n";
}
