#pragma once
#include "platform.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>
#include <vector>
#include <future>
#include <algorithm>
#include <nlohmann/json.hpp>
#include "content_hash.hpp"
namespace satr {
// Checked persistent cache. The compiler expands every source before comparing it,
// so new/changed headers, conditional includes and time macros stay correct.
inline std::string FileStamp(const std::filesystem::path& path){
    std::error_code ec;auto size=std::filesystem::file_size(path,ec);if(ec)return {};
    auto time=std::filesystem::last_write_time(path,ec);if(ec)return {};
    return std::to_string(size)+":"+std::to_string(time.time_since_epoch().count());
}
inline std::string CacheRead(const std::filesystem::path& path){
    std::error_code ec;auto size=std::filesystem::file_size(path,ec);if(ec||size>16*1024*1024)return {};
    std::ifstream f(path,std::ios::binary);if(!f)return {};
    std::string text(static_cast<size_t>(size),'\0');
    if(size&&!f.read(text.data(),static_cast<std::streamsize>(size)))return {};
    return text;
}
inline bool CacheEligible(const std::string& text){
    // These directives read binary/assembly inputs after preprocessing. Their
    // contents are not represented by the expanded C/C++ text, so compile them.
    return !text.empty()&&text.find("#pragma GCC pch_preprocess")==std::string::npos&&text.find(".incbin")==std::string::npos&&text.find(".include")==std::string::npos;
}
inline std::vector<std::filesystem::path> LinkDependencies(const std::filesystem::path& file){
    auto text=CacheRead(file);auto colon=text.find(": ");if(colon==std::string::npos)return {};
    std::vector<std::filesystem::path> paths;std::string word;
    for(size_t i=colon+2;i<text.size();++i){char c=text[i];
        if(c=='\\'&&i+1<text.size()){
            char next=text[i+1];if(next=='\n'){++i;continue;}
            if(next=='\r'&&i+2<text.size()&&text[i+2]=='\n'){i+=2;continue;}
            if(next==' '||next=='\t'||next=='\\'||next=='#'){word+=next;++i;continue;}
        }
        if(c=='\n'||c=='\r'){if(!word.empty())paths.push_back(std::filesystem::u8path(word));break;}
        if(c==' '||c=='\t'){if(!word.empty()){paths.push_back(std::filesystem::u8path(word));word.clear();}}else word+=c;
    }
    return paths;
}
class BuildCache {
    struct Unit {std::string key,text,output,stamp;};
    std::map<std::wstring,Unit> units_;
    std::string linkKey_;
    std::map<std::filesystem::path,std::string> linkFiles_;
    std::filesystem::path storage_;
    std::future<void> writer_;
public:
    void SetStorage(const std::filesystem::path& path){storage_=path;}
    bool CanRestore(const std::string& key)const{std::error_code ec;return !storage_.empty()&&std::filesystem::is_regular_file(storage_/(Sha256(key)+".cache"),ec);}
    bool Restore(const std::wstring& object,const std::string& key){
        if(HasMatchingUnit(object,key)||storage_.empty())return false;
        try{
            auto path=storage_/(Sha256(key)+".cache");std::error_code ec;auto size=std::filesystem::file_size(path,ec);if(ec||size>40*1024*1024)return false;
            std::ifstream file(path,std::ios::binary);std::string bytes(static_cast<size_t>(size),'\0');if(!file.read(bytes.data(),static_cast<std::streamsize>(bytes.size())))return false;
            if(bytes.size()<64||Sha256(std::string_view(bytes).substr(64))!=bytes.substr(0,64))return false;
            auto data=nlohmann::json::from_cbor(bytes.begin()+64,bytes.end());
            if(data.value("version",0)!=1||data.value("key","")!=key||!data["object"].is_binary())return false;
            auto text=data.at("input").get<std::string>();if(!CacheEligible(text)||text.size()>16*1024*1024)return false;
            const auto& binary=data["object"].get_binary();if(binary.empty()||binary.size()>16*1024*1024)return false;
            auto output=data.at("output").get<std::string>();if(output.size()>4*1024*1024)return false;
            std::ofstream target(FsPath(object),std::ios::binary|std::ios::trunc);target.write(reinterpret_cast<const char*>(binary.data()),static_cast<std::streamsize>(binary.size()));target.close();if(!target)return false;
            units_[object]={key,std::move(text),std::move(output),FileStamp(FsPath(object))};return true;
        }catch(...){return false;}
    }
    bool HasUnit(const std::wstring& object)const{return units_.count(object)!=0;}
    bool HasMatchingUnit(const std::wstring& object,const std::string& key)const{auto found=units_.find(object);return found!=units_.end()&&found->second.key==key;}
    bool UnitHit(const std::wstring& object,const std::string& key,const std::string& text,std::string& output)const{
        auto i=units_.find(object);if(i==units_.end()||!CacheEligible(text))return false;
        const auto& unit=i->second;if(unit.key!=key||unit.text!=text||unit.stamp!=FileStamp(FsPath(object)))return false;
        output=unit.output;return true;
    }
    void Commit(const std::wstring& object,const std::string& key,std::string text,const std::string& output){
        if(!CacheEligible(text))units_.erase(object);else units_[object]={key,std::move(text),output,FileStamp(FsPath(object))};
        if(storage_.empty()||!units_.count(object))return;
        if(writer_.valid()&&writer_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
        auto binary=CacheRead(FsPath(object));if(binary.empty())return;
        auto snapshot=units_.at(object);auto directory=storage_;
        if(snapshot.text.size()>16*1024*1024||snapshot.output.size()>4*1024*1024)return;
        writer_=std::async(std::launch::async,[binary=std::move(binary),snapshot=std::move(snapshot),directory,key]{
        try{
            nlohmann::json data={{"version",1},{"key",key},{"input",snapshot.text},{"output",snapshot.output},{"object",nlohmann::json::binary(std::vector<uint8_t>(binary.begin(),binary.end()))}};
            auto payload=nlohmann::json::to_cbor(data);std::string_view view(reinterpret_cast<const char*>(payload.data()),payload.size());auto digest=Sha256(view);
            std::error_code ec;std::filesystem::create_directories(directory,ec);if(ec)return;
#ifndef _WIN32
            std::filesystem::permissions(directory,std::filesystem::perms::owner_all,std::filesystem::perm_options::replace,ec);if(ec)return;
#endif
            auto path=directory/(Sha256(key)+".cache"),temporary=directory/(Sha256(key)+"."+std::to_string(GetCurrentProcessId())+".part");
            {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out.write(digest.data(),digest.size());out.write(view.data(),static_cast<std::streamsize>(view.size()));out.close();if(!out){std::filesystem::remove(temporary,ec);return;}}
#ifdef _WIN32
            if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))std::filesystem::remove(temporary,ec);
#else
            std::filesystem::rename(temporary,path,ec);if(ec)std::filesystem::remove(temporary,ec);
#endif
            // Only our hash-named cache files are eligible for eviction.
            struct Entry{std::filesystem::path path;std::filesystem::file_time_type time;uintmax_t bytes;};
            std::vector<Entry> entries;uintmax_t total=0;
            for(const auto& entry:std::filesystem::directory_iterator(directory)){
                auto name=entry.path().filename().string();
                if(name.size()!=70||name.substr(64)!=".cache"||name.substr(0,64).find_first_not_of("0123456789abcdef")!=std::string::npos||entry.is_symlink()||!entry.is_regular_file())continue;
                auto size=entry.file_size();total+=size;entries.push_back({entry.path(),entry.last_write_time(),size});
            }
            std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a.time<b.time;});
            auto count=entries.size();for(const auto& entry:entries){if(total<=256*1024*1024&&count<=128)break;if(entry.path==path)continue;if(std::filesystem::remove(entry.path,ec)){total-=entry.bytes;--count;}}
        }catch(...){} // A full disk or corrupt cache never prevents a normal build.
        });
    }
    void Invalidate(const std::wstring& object){units_.erase(object);}
    bool LinkHit(const std::string& key)const{
        if(key!=linkKey_||linkFiles_.empty())return false;
        for(const auto& item:linkFiles_)if(item.second.empty()||item.second!=FileStamp(item.first))return false;
        return true;
    }
    void CommitLink(const std::string& key,const std::filesystem::path& depfile,const std::filesystem::path& binary,const std::filesystem::path& cwd){
        linkFiles_.clear();auto deps=LinkDependencies(depfile);if(deps.empty())return;
        deps.push_back(binary);for(auto file:deps){if(file.is_relative())file=cwd/file;auto stamp=FileStamp(file);if(stamp.empty()){linkFiles_.clear();return;}linkFiles_[file]=stamp;}
        linkKey_=key;
    }
};
}
