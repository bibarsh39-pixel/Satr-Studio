#pragma once
#include <wx/string.h>
#include <string>
#include <string_view>
#include <unordered_map>
namespace ui {
inline std::string language="ar";
struct Message {const char* ar;const char* en;const char* fr;};
inline const Message messages[]={
#include "ui_messages.inc"
};
inline void SetLanguage(const std::string& code){language=(code=="en"||code=="fr")?code:"ar";}
inline bool Arabic(){return language=="ar";}
inline std::string Translate(const std::string& source){
    if(Arabic())return source;
    static const auto index=[] {std::unordered_map<std::string_view,const Message*> value;for(const auto& m:messages)value.emplace(m.ar,&m);return value;}();
    auto it=index.find(source);if(it==index.end()){
        // UI status prefixes carry tool names or OS errors as plain data.
        const Message* prefix=nullptr;size_t length=0;
        for(const auto& m:messages){std::string_view key(m.ar);if(key.size()>length&&key.back()==' '&&source.compare(0,key.size(),key)==0){prefix=&m;length=key.size();}}
        if(prefix)return std::string(language=="fr"?prefix->fr:prefix->en)+Translate(source.substr(length));
        auto progress=source.find(" — ");if(progress!=std::string::npos)return Translate(source.substr(0,progress))+source.substr(progress);
        const std::string ready=" جاهز";if(source.size()>ready.size()&&source.compare(source.size()-ready.size(),ready.size(),ready)==0)return Translate(source.substr(0,source.size()-ready.size()))+Translate(ready);
        return source;
    }
    return language=="fr"?it->second->fr:it->second->en;
}
inline wxString Text(const std::string& source){auto value=Translate(source);return wxString::FromUTF8(value.data(),value.size());}
// Used only for UI labels, never for text controls, editor contents or paths.
inline wxString Relabel(const wxString& current,const std::string& oldLanguage){
    auto bytes=std::string(current.utf8_str());
    for(const auto& m:messages){const char* old=oldLanguage=="fr"?m.fr:oldLanguage=="en"?m.en:m.ar;if(bytes==old)return Text(m.ar);}
    return current;
}
}
