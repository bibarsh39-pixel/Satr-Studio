#pragma once
#include "build_cache.hpp"
#include <array>

namespace satr {
// Place the selected MinGW runtime beside the generated program. The loader
// finds these exact DLLs without changing PATH, the working directory or the
// user's system. If this toolchain cannot supply them, retain static linking.
class NativeRuntime {
    struct Copy { std::string sourceStamp,destinationStamp; };
    std::map<std::filesystem::path,Copy> copies_;
public:
    bool Prepare(const std::filesystem::path& compiler,const std::filesystem::path& directory){
#ifdef _WIN32
        try {
            const std::array<const char*,3> names={"libstdc++-6.dll","libgcc_s_seh-1.dll","libwinpthread-1.dll"};
            auto bin=compiler.parent_path();
            for(auto name:names)if(!std::filesystem::is_regular_file(bin/name))return false;
            for(auto name:names){
                auto source=bin/name,destination=directory/name;
                auto sourceStamp=FileStamp(source),destinationStamp=FileStamp(destination);
                auto key=destination;auto& cached=copies_[key];
                auto identity=WidePath(source)+L"\n"+ToWide(sourceStamp);
                if(!sourceStamp.empty()&&cached.sourceStamp==ToUtf8(identity)&&cached.destinationStamp==destinationStamp&&!destinationStamp.empty())continue;
                auto bytes=CacheRead(source);if(bytes.empty())return false;
                if(CacheRead(destination)!=bytes){
                    auto temporary=destination;temporary+=L".runtime.tmp";
                    std::error_code ec;std::filesystem::copy_file(source,temporary,std::filesystem::copy_options::overwrite_existing,ec);if(ec)return false;
                    if(CacheRead(temporary)!=bytes||sourceStamp!=FileStamp(source)){std::filesystem::remove(temporary,ec);return false;}
                    if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING)){std::filesystem::remove(temporary,ec);return false;}
                }
                cached={ToUtf8(identity),FileStamp(destination)};
            }
            return true;
        }catch(...){return false;}
#else
        (void)compiler;(void)directory;return false;
#endif
    }
};
}
