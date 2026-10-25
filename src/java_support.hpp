#pragma once
#include "platform.hpp"
#include "build_cache.hpp"
#include "diagnostics.hpp"
#include <regex>
#include <atomic>

namespace satr {
namespace java_detail {
namespace fs = std::filesystem;
using Json = nlohmann::json;
inline std::string Read(const fs::path& file, uintmax_t limit=64*1024*1024) {
    auto size=fs::file_size(file); if(size>limit)throw std::runtime_error("Java file exceeds cache limit");
    std::ifstream in(file,std::ios::binary);std::string bytes(static_cast<size_t>(size),'\0');
    if(!in || (size&&!in.read(bytes.data(),static_cast<std::streamsize>(size))))throw std::runtime_error("Cannot read Java file");
    return bytes;
}
// Ignore comments and literals when finding a package or a top-level main class.
inline std::vector<std::string> Tokens(const std::string& source) {
    std::vector<std::string> result;
    for(size_t i=0;i<source.size();) {
        unsigned char c=source[i];
        if(std::isspace(c)){++i;continue;}
        if(source.compare(i,2,"//")==0){auto end=source.find('\n',i+2);i=end==std::string::npos?source.size():end;continue;}
        if(source.compare(i,2,"/*")==0){auto end=source.find("*/",i+2);i=end==std::string::npos?source.size():end+2;continue;}
        if(c=='"'||c=='\'') {
            bool block=source.compare(i,3,"\"\"\"")==0;char quote=c;i+=block?3:1;
            while(i<source.size()){if(source[i]=='\\'){i=std::min(source.size(),i+2);continue;}if(block?source.compare(i,3,"\"\"\"")==0:source[i]==quote){i+=block?3:1;break;}++i;}continue;
        }
        if(std::isalnum(c)||c=='_'||c=='$'||c>=128){auto start=i++;while(i<source.size()){unsigned char ch=source[i];if(!(std::isalnum(ch)||ch=='_'||ch=='$'||ch>=128))break;++i;}result.push_back(source.substr(start,i-start));}
        else result.push_back(source.substr(i++,1));
    }
    return result;
}
inline std::pair<std::string,std::string> Names(const std::string& text,const std::string& filename) {
    auto tokens=Tokens(text);std::string package,current,pending,selected;std::vector<std::string> candidates;int depth=0;size_t start=0;
    for(size_t i=0;i<tokens.size();++i){const auto& token=tokens[i];
        if(depth==0&&token=="package"){for(++i;i<tokens.size()&&tokens[i]!=";";++i)package+=tokens[i];continue;}
        if(depth==0&&(token=="class"||token=="interface"||token=="enum"||token=="record")&&i+1<tokens.size())pending=tokens[i+1];
        if(token=="{"){if(depth==0){current=pending;pending.clear();}++depth;start=i+1;continue;}
        if(token=="}"){--depth;start=i+1;if(depth==0)current.clear();continue;}
        if(token==";")start=i+1;
        if(depth==1&&!current.empty()&&token=="main"&&i+1<tokens.size()&&tokens[i+1]=="("){
            bool pub=false,stat=false,v=false;for(size_t j=start;j<i;++j){pub|=tokens[j]=="public";stat|=tokens[j]=="static";v|=tokens[j]=="void";}
            if(pub&&stat&&v){candidates.push_back(current);if(current==filename)selected=current;}
        }
    }
    if(selected.empty())selected=candidates.size()==1?candidates.front():filename;
    return {package,package.empty()?selected:package+"."+selected};
}
inline std::string Snapshot(const fs::path& root,bool classesOnly=false) {
    // A bounded, content-based scan catches additions/deletions and same-size edits.
    // Any incomplete scan disables reuse; javac still gets a normal fresh build.
    std::vector<fs::path> paths;size_t visited=0;uintmax_t total=0;
    for(const auto& entry:fs::recursive_directory_iterator(root)){
        if(++visited>8192||entry.is_symlink())throw std::runtime_error("Java cache scan is incomplete");
        if(!entry.is_regular_file())continue;
        auto ext=entry.path().extension().u8string();
        if(ext!=".class"&&(classesOnly||(ext!=".java"&&ext!=".jar")))continue;
        total+=entry.file_size();if(total>64*1024*1024)throw std::runtime_error("Java cache input limit");paths.push_back(entry.path());
    }
    std::sort(paths.begin(),paths.end());std::string key;
    for(const auto& path:paths)key+=path.lexically_relative(root).generic_u8string()+"\n"+Sha256(Read(path))+"\n";
    return Sha256(key);
}
inline bool SafeClassName(const std::string& name){
    if(name.empty()||name.find('\\')!=std::string::npos||name.find(':')!=std::string::npos)return false;
    auto path=fs::u8path(name);if(path.is_absolute()||path.extension()!=".class")return false;
    for(const auto& part:path)if(part==".."||part==".")return false;
    return true;
}
inline std::wstring ArgumentFile(const fs::path& path,const std::vector<std::wstring>& args){
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    for(const auto& arg:args){out<<'"';for(char c:ToUtf8(arg)){switch(c){case '\\':out<<"\\\\";break;case '"':out<<"\\\"";break;case '\n':out<<"\\n";break;case '\r':out<<"\\r";break;case '\t':out<<"\\t";break;default:out<<c;}}out<<"\"\n";}
    out.close();if(!out)throw std::runtime_error("Cannot write Java argument file");
#ifdef _WIN32
    // The native JDK launcher can lose non-ANSI command-line paths. javac reads
    // UTF-8 @files after the VM starts; only the @file itself needs a short path.
    std::wstring shortPath(32768,L'\0');auto size=GetShortPathNameW(path.c_str(),shortPath.data(),static_cast<DWORD>(shortPath.size()));
    if(size>0&&size<shortPath.size()){shortPath.resize(size);return L"@"+shortPath;}
#endif
    return L"@"+WidePath(path);
}
#ifdef _WIN32
inline std::string FileUrl(const fs::path& path,bool directory){
    auto bytes=fs::absolute(path).generic_u8string();std::string url="file:///";
    if(!bytes.empty()&&bytes[0]=='/')url="file:";
    const char* hex="0123456789ABCDEF";
    for(unsigned char c:bytes){if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='/'||c==':'||c=='-'||c=='_'||c=='.'||c=='~')url+=c;else {url+='%';url+=hex[c>>4];url+=hex[c&15];}}
    if(directory&&url.back()!='/')url+='/';return url;
}
inline void LaunchJar(const fs::path& path,const std::string& mainClass,const std::vector<fs::path>& classpath){
    // Standard JAR manifest class paths use URL-escaped UTF-8. This avoids the
    // Windows java launcher's ANSI decoding of @file class paths, without a
    // custom class loader, reflection wrapper or an extra compiler process.
    std::string manifest="Manifest-Version: 1.0\r\n";
    auto attribute=[&](const std::string& name,const std::string& value){std::string line=name+": "+value;size_t at=0;while(at<line.size()){size_t length=std::min<size_t>(at?70:71,line.size()-at);if(at)manifest+=' ';manifest+=line.substr(at,length);manifest+="\r\n";at+=length;}};
    attribute("Main-Class",mainClass);std::string urls;
    for(const auto& item:classpath){if(!urls.empty())urls+=' ';urls+=FileUrl(item,fs::is_directory(item));}attribute("Class-Path",urls);manifest+="\r\n";
    const std::string name="META-INF/MANIFEST.MF";uint32_t crc=0xffffffff;
    for(unsigned char c:manifest){crc^=c;for(int i=0;i<8;++i)crc=(crc>>1)^(0xedb88320u&uint32_t(-int(crc&1)));}crc^=0xffffffff;
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    auto u16=[&](uint16_t n){out.put(char(n));out.put(char(n>>8));};auto u32=[&](uint32_t n){u16(uint16_t(n));u16(uint16_t(n>>16));};
    u32(0x04034b50);u16(20);u16(0);u16(0);u16(0);u16(0);u32(crc);u32(uint32_t(manifest.size()));u32(uint32_t(manifest.size()));u16(uint16_t(name.size()));u16(0);out<<name<<manifest;
    auto central=uint32_t(out.tellp());u32(0x02014b50);u16(20);u16(20);u16(0);u16(0);u16(0);u16(0);u32(crc);u32(uint32_t(manifest.size()));u32(uint32_t(manifest.size()));u16(uint16_t(name.size()));u16(0);u16(0);u16(0);u16(0);u32(0);u32(0);out<<name;
    auto length=uint32_t(out.tellp())-central;u32(0x06054b50);u16(0);u16(0);u16(1);u16(1);u32(length);u32(central);u16(0);out.close();if(!out)throw std::runtime_error("Cannot write Java launch manifest");
}
#endif
}

struct JavaPlan {
    std::filesystem::path source,root,classes,compiler,runtime,cacheFile;
    std::string mainClass,key,inputs,output;
    std::vector<std::wstring> compileArgs,runArgs;
    bool cacheable=false,hit=false;
};

inline JavaPlan PrepareJava(const std::filesystem::path& source,const std::filesystem::path& compiler,
                            const std::filesystem::path& runtime,const std::filesystem::path& dataRoot,
                            bool cacheEnabled,const std::string& environment) {
    namespace fs=std::filesystem;using namespace java_detail;
    JavaPlan p;p.source=source;p.compiler=compiler;p.runtime=runtime;
    auto names=Names(Read(source,8*1024*1024),source.stem().u8string());p.mainClass=names.second;p.root=source.parent_path();
    if(!names.first.empty()){
        std::vector<std::string> parts;std::istringstream stream(names.first);std::string word;while(std::getline(stream,word,'.'))parts.push_back(word);
        auto parent=p.root;bool match=true;for(auto i=parts.rbegin();i!=parts.rend();++i){if(parent.filename().u8string()!=*i){match=false;break;}parent=parent.parent_path();}if(match)p.root=parent;
    }
    auto sourceKey=Sha256(source.u8string());auto buildRoot=dataRoot/"java-build"/("session-"+std::to_string(GetCurrentProcessId()));
    p.classes=buildRoot/sourceKey;p.cacheFile=dataRoot/"java-cache-v1"/(sourceKey+".cache");
    // This is an app-owned directory derived only from a SHA-256 key, never a source path.
    fs::create_directories(buildRoot);if(fs::is_symlink(buildRoot)||fs::is_symlink(p.classes))throw std::runtime_error("Invalid Java build directory");
    fs::remove_all(p.classes);fs::create_directories(p.classes);
#ifndef _WIN32
    fs::permissions(buildRoot,fs::perms::owner_all,fs::perm_options::replace);
#endif
#ifdef _WIN32
    const std::wstring sep=L";";
#else
    const std::wstring sep=L":";
#endif
    std::wstring classpath=WidePath(p.classes)+sep+WidePath(p.root);
    auto lib=p.root/"lib";std::vector<fs::path> jars;if(fs::is_directory(lib))for(const auto& entry:fs::directory_iterator(lib))if(entry.is_regular_file()&&entry.path().extension()==".jar")jars.push_back(entry.path());
    std::sort(jars.begin(),jars.end());for(const auto& jar:jars)classpath+=sep+WidePath(jar);
    p.compileArgs={L"-J-XX:TieredStopAtLevel=1",L"-J-Duser.language=en",L"-J-Duser.country=US",L"-J-Dfile.encoding=UTF-8",L"-encoding",L"UTF-8",L"-proc:none",L"-implicit:class",L"-Xprefer:source",L"-d",WidePath(p.classes),L"-classpath",classpath,L"-sourcepath",WidePath(p.root),WidePath(source)};
    p.runArgs={L"-Dfile.encoding=UTF-8",L"-Dstdout.encoding=UTF-8",L"-Dstderr.encoding=UTF-8",L"-cp",classpath,ToWide(p.mainClass)};
#ifdef _WIN32
    auto launchJar=buildRoot/(sourceKey+".launch.jar");std::vector<fs::path> runClasspath{p.classes,p.root};runClasspath.insert(runClasspath.end(),jars.begin(),jars.end());LaunchJar(launchJar,p.mainClass,runClasspath);
    p.runArgs={L"-Dfile.encoding=UTF-8",L"-Dstdout.encoding=UTF-8",L"-Dstderr.encoding=UTF-8",L"-jar",launchJar.filename().wstring()};
    std::wstring shortJar(32768,L'\0');auto shortSize=GetShortPathNameW(launchJar.c_str(),shortJar.data(),static_cast<DWORD>(shortJar.size()));
    if(shortSize>0&&shortSize<shortJar.size()){shortJar.resize(shortSize);p.runArgs.back()=shortJar;}else p.runArgs.back()=launchJar.wstring();
#endif
    std::vector<std::wstring> compileOptions(p.compileArgs.begin()+4,p.compileArgs.end());
    p.compileArgs.resize(4);p.compileArgs.push_back(L"-J-Dstdout.encoding=UTF-8");p.compileArgs.push_back(L"-J-Dstderr.encoding=UTF-8");p.compileArgs.push_back(ArgumentFile(buildRoot/(sourceKey+".compile.args"),compileOptions));
    p.runArgs={ArgumentFile(buildRoot/(sourceKey+".run.args"),p.runArgs)};
    // Custom JVM/compiler options can refer to files outside the source tree.
    // Compile normally rather than reusing classes with untracked external inputs.
    if(!cacheEnabled||!environment.empty())return p;
    try{
        p.inputs=Snapshot(p.root);auto jdk=fs::canonical(compiler).parent_path().parent_path();
        std::string key="satr-java-v2\n"+source.u8string()+"\n"+p.inputs+"\n"+environment;
        for(const auto& file:{compiler,runtime,jdk/"release",jdk/"lib"/"modules"})key+='\n'+file.u8string()+':'+FileStamp(file);
        p.key=Sha256(key);p.cacheable=true;
        if(!fs::is_regular_file(p.cacheFile))return p;
        auto bytes=Read(p.cacheFile,40*1024*1024);if(bytes.size()<64||Sha256(std::string_view(bytes).substr(64))!=bytes.substr(0,64))return p;
        auto data=Json::from_cbor(bytes.begin()+64,bytes.end());
        if(data.value("version",0)!=1||data.value("key","")!=p.key)return p;
        auto files=data.at("classes");if(!files.is_object()||files.empty()||files.size()>4096)return p;
        for(const auto& entry:files.items())if(!SafeClassName(entry.key())||!entry.value().is_binary())return p;
        for(const auto& entry:files.items()){
            auto path=p.classes/fs::u8path(entry.key());fs::create_directories(path.parent_path());const auto& bytes=entry.value().get_binary();
            std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());out.close();if(!out)throw std::runtime_error("Cannot restore Java class");
        }
        p.output=data.value("output","");p.hit=true;
    }catch(...){p.hit=false;p.cacheable=false;fs::remove_all(p.classes);fs::create_directories(p.classes);}
    return p;
}

inline bool CommitJava(const JavaPlan& p,const std::string& output){
    namespace fs=std::filesystem;using namespace java_detail;
    if(!p.cacheable)return false;
    try{
        // A source edited externally while javac ran must not produce a reusable cache entry.
        if(Snapshot(p.root)!=p.inputs)return false;
        Json classes=Json::object();uintmax_t total=0;
        for(const auto& entry:fs::recursive_directory_iterator(p.classes)){
            if(entry.is_symlink())return false;
            if(!entry.is_regular_file())continue;
            auto name=entry.path().lexically_relative(p.classes).generic_u8string();if(!SafeClassName(name))return false;
            auto bytes=Read(entry.path());total+=bytes.size();if(total>32*1024*1024||classes.size()>=4096)return false;
            classes[name]=Json::binary(std::vector<uint8_t>(bytes.begin(),bytes.end()));
        }
        if(classes.empty())return false;
        auto payload=Json::to_cbor(Json{{"version",1},{"key",p.key},{"classes",std::move(classes)},{"output",output}});
        std::string_view bytes(reinterpret_cast<const char*>(payload.data()),payload.size());auto digest=Sha256(bytes);
        auto directory=p.cacheFile.parent_path();fs::create_directories(directory);
#ifndef _WIN32
        fs::permissions(directory,fs::perms::owner_all,fs::perm_options::replace);
#endif
        auto temp=p.cacheFile;temp+="."+std::to_string(GetCurrentProcessId())+".part";
        {std::ofstream out(temp,std::ios::binary);out<<digest;out.write(bytes.data(),bytes.size());out.close();if(!out)return false;}
#ifdef _WIN32
        if(!MoveFileExW(temp.c_str(),p.cacheFile.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return false;
#else
        fs::rename(temp,p.cacheFile);
#endif
        struct Entry{fs::path file;fs::file_time_type time;uintmax_t size;};std::vector<Entry> entries;total=0;
        for(const auto& entry:fs::directory_iterator(directory)){
            auto name=entry.path().filename().string();if(name.size()!=70||name.substr(64)!=".cache"||name.substr(0,64).find_first_not_of("0123456789abcdef")!=std::string::npos||entry.is_symlink()||!entry.is_regular_file())continue;
            auto size=entry.file_size();total+=size;entries.push_back({entry.path(),entry.last_write_time(),size});
        }
        std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a.time<b.time;});auto count=entries.size();
        for(const auto& entry:entries){if(total<=128*1024*1024&&count<=64)break;if(entry.file==p.cacheFile)continue;std::error_code ec;if(fs::remove(entry.file,ec)){total-=entry.size;--count;}}
        return true;
    }catch(...){return false;}
}

inline std::vector<Diagnostic> JavaDiagnostics(const std::string& output){
    std::vector<Diagnostic> result;std::istringstream lines(output);std::string line;
    const std::regex location(R"(^(.+\.java):([0-9]+): (error|warning): (.*)$)");std::smatch match;
    while(std::getline(lines,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(std::regex_match(line,match,location)){
            Diagnostic d;d.file=match[1];d.line=std::stoi(match[2]);d.severity=match[3];d.original=match[4];
            auto message=d.original;
            if(message.find("';' expected")!=std::string::npos)d.summary="فاصلة منقوطة متوقعة";
            else if(message.find("cannot find symbol")!=std::string::npos)d.summary="اسم غير معروف";
            else if(message.find("incompatible types")!=std::string::npos)d.summary="أنواع غير متوافقة";
            else if(message.find("should be declared in a file named")!=std::string::npos)d.summary="اسم الملف لا يطابق اسم الصنف";
            else if(message.find("reached end of file while parsing")!=std::string::npos)d.summary="قوس إغلاق مفقود";
            else d.summary=message;
            d.hint=line;result.push_back(std::move(d));
        }else if(!result.empty()){
            result.back().hint+='\n'+line;
            auto caret=line.find('^');if(caret!=std::string::npos&&line.find_first_not_of(" \t^")==std::string::npos)result.back().column=static_cast<int>(caret)+1;
        }
    }
    return result;
}
}
