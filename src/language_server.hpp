#pragma once
#include "process.hpp"
#include <nlohmann/json.hpp>
#include <functional>
#include <map>
#include <sstream>
#include <iomanip>
namespace satr {
using LspJson=nlohmann::json;
inline std::string FileUri(std::string path){
    std::replace(path.begin(),path.end(),'\\','/');std::string out=path.size()>1&&path[1]==':'?"file:///":"file://";
    const char* digits="0123456789ABCDEF";
    for(unsigned char c:path){if(std::isalnum(c)||c=='/'||c==':'||c=='-'||c=='_'||c=='.'||c=='~')out+=static_cast<char>(c);else{out+='%';out+=digits[c>>4];out+=digits[c&15];}}return out;
}
// One lazily started, asynchronous clangd process. All bytes stay on this PC.
// GUI code passes exact source versions and discards stale completion replies.
class LanguageServer {
    Process process_;
    std::deque<std::string> outgoing_;
    size_t outgoingBytes_=0,offset_=0;
    std::string incoming_,currentFile_,currentSource_,configuration_;
    std::string diagnostics_;
    bool debug_=false;std::string trace_;
    bool ready_=false,failed_=false;
    int sequence_=1,documentVersion_=0,pendingRequest_=0;
    ULONGLONG started_=0,requestStarted_=0;
    ULONGLONG retryAt_=0;int retries_=0;LspJson lastQuery_;std::function<void(const LspJson&)> retryCallback_;
    std::map<int,std::function<void(const LspJson&)>> callbacks_;
    bool Queue(const LspJson& message){
        auto body=message.dump();auto frame="Content-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
        if(outgoingBytes_+frame.size()>2*1024*1024){failed_=true;process_.Stop();return false;}
        outgoingBytes_+=frame.size();outgoing_.push_back(std::move(frame));return true;
    }
    void Notice(const std::string& method,const LspJson& params){Queue({{"jsonrpc","2.0"},{"method",method},{"params",params}});}
public:
    ~LanguageServer(){process_.Stop();}
    bool Ready()const{return ready_&&!failed_;}
    bool Started()const{return started_!=0;}
    bool Failed()const{return failed_;}
    bool Busy()const{return pendingRequest_!=0||retryAt_!=0;}
    const std::string& Diagnostics()const{return diagnostics_;}
    const std::string& Trace()const{return trace_;}
    void Stop(){process_.Stop();ready_=false;failed_=true;callbacks_.clear();}
    bool Start(const std::wstring& executable,const std::wstring& compiler,const std::wstring& cwd,std::string& error,bool debug=false){
        if(Started())return !failed_;
        started_=GetTickCount64();debug_=debug;
        auto driver=compiler;std::replace(driver.begin(),driver.end(),L'\\',L'/');auto gcc=driver;auto pos=gcc.rfind(L"g++");if(pos!=std::wstring::npos)gcc.replace(pos,3,L"gcc");
        std::vector<std::wstring> args={L"--background-index=false",L"--clang-tidy=false",L"--completion-style=detailed",L"--header-insertion=never",L"--limit-results=80",L"--pch-storage=memory",L"--log=error",L"-j=2",L"--query-driver="+driver+L","+gcc,L"--completion-parse=always"};
        if(debug)args[6]=L"--log=verbose";
        if(!process_.Start(executable,args,cwd,error)){failed_=true;return false;}
        LspJson capabilities={{"offsetEncoding",{"utf-8"}},{"textDocument",{{"completion",{{"completionItem",{{"snippetSupport",true}}}}}}}};
        capabilities["textDocument"]["completion"]["completionItemKind"]["valueSet"]=LspJson::array();for(int i=1;i<=25;++i)capabilities["textDocument"]["completion"]["completionItemKind"]["valueSet"].push_back(i);
        Queue({{"jsonrpc","2.0"},{"id",sequence_++},{"method","initialize"},{"params",{{"processId",GetCurrentProcessId()},{"rootUri",nullptr},{"capabilities",capabilities},{"initializationOptions",{{"fallbackFlags",{"-Wall"}}}}}}});return true;
    }
    void Poll(){
        if(!Started()||failed_)return;
        // Send small chunks into Process's bounded, nonblocking writer queue.
        for(int i=0;i<16&&!outgoing_.empty();++i){auto& first=outgoing_.front();size_t n=std::min<size_t>(4096,first.size()-offset_);if(!process_.Send(first.substr(offset_,n)))break;offset_+=n;outgoingBytes_-=n;if(offset_==first.size()){outgoing_.pop_front();offset_=0;}}
        auto bytes=process_.Poll();if(debug_&&trace_.size()<100000)trace_+=bytes;incoming_+=bytes;
        if(!process_.Active()||incoming_.size()>4*1024*1024||(!ready_&&GetTickCount64()-started_>15000)){Stop();return;}
        for(int messages=0;messages<32;++messages){
            auto start=incoming_.find("Content-Length: ");if(start==std::string::npos){if(incoming_.size()>8192)incoming_.erase(0,incoming_.size()-128);break;}
            if(start)incoming_.erase(0,start);
            auto boundary=incoming_.find("\r\n\r\n");if(boundary==std::string::npos)break;
            size_t length=0;try{length=std::stoul(incoming_.substr(16,boundary-16));}catch(...){incoming_.erase(0,boundary+4);continue;}
            if(length>4*1024*1024){Stop();return;}if(incoming_.size()<boundary+4+length)break;
            auto payload=incoming_.substr(boundary+4,length);incoming_.erase(0,boundary+4+length);
            auto message=LspJson::parse(payload,nullptr,false);if(message.is_discarded()||!message.is_object())continue;
            if(message.value("method","")=="textDocument/publishDiagnostics")diagnostics_=message.dump();
            if(message.contains("id")&&message["id"].is_number_integer()){
                int id=message["id"].get<int>();
                if(id==1){if(!message.contains("result")||message["result"].value("offsetEncoding","")!="utf-8"){Stop();return;}ready_=true;Notice("initialized",LspJson::object());}
                auto callback=callbacks_.find(id);if(callback!=callbacks_.end()){
                    auto fn=std::move(callback->second);callbacks_.erase(callback);if(pendingRequest_==id)pendingRequest_=0;auto result=message.value("result",LspJson());
                    bool empty=result.is_array()?result.empty():result.is_object()&&result.contains("items")&&result["items"].empty();
                    if(empty&&retries_>0){--retries_;retryCallback_=std::move(fn);retryAt_=GetTickCount64()+350;}else fn(result);
                }
            }
        }
        if(pendingRequest_&&GetTickCount64()-requestStarted_>7000){Notice("$/cancelRequest",{{"id",pendingRequest_}});callbacks_.erase(pendingRequest_);pendingRequest_=0;}
        if(retryAt_&&GetTickCount64()>=retryAt_){retryAt_=0;pendingRequest_=sequence_++;requestStarted_=GetTickCount64();callbacks_[pendingRequest_]=std::move(retryCallback_);Queue({{"jsonrpc","2.0"},{"id",pendingRequest_},{"method","textDocument/completion"},{"params",lastQuery_}});}
    }
    bool Complete(const std::string& file,const std::string& source,const std::vector<std::string>& command,const std::string& cwd,int line,int byteColumn,std::function<void(const LspJson&)> callback){
        if(!Ready()||Busy()||source.size()>256*1024)return false;
        auto normalized=file;std::replace(normalized.begin(),normalized.end(),'\\','/');
        LspJson config={{normalized,{{"workingDirectory",cwd},{"compilationCommand",command}}}};auto serialized=config.dump();
        if(configuration_!=serialized){Notice("workspace/didChangeConfiguration",{{"settings",{{"compilationDatabaseChanges",config}}}});configuration_=serialized;}
        auto uri=FileUri(file);
        if(currentFile_!=file){
            if(!currentFile_.empty())Notice("textDocument/didClose",{{"textDocument",{{"uri",FileUri(currentFile_)}}}});
            currentFile_=file;documentVersion_=1;
            bool c=file.size()>2&&file.substr(file.size()-2)==".c";
            Notice("textDocument/didOpen",{{"textDocument",{{"uri",uri},{"languageId",c?"c":"cpp"},{"version",documentVersion_},{"text",source}}}});
        }else if(currentSource_!=source)Notice("textDocument/didChange",{{"textDocument",{{"uri",uri},{"version",++documentVersion_}}},{"contentChanges",LspJson::array({{{"text",source}}})},{"wantDiagnostics",true}});
        currentSource_=source;retries_=4;lastQuery_={{"textDocument",{{"uri",uri}}},{"position",{{"line",line},{"character",byteColumn}}},{"context",{{"triggerKind",1}}}};
        pendingRequest_=sequence_++;requestStarted_=GetTickCount64();callbacks_[pendingRequest_]=std::move(callback);
        return Queue({{"jsonrpc","2.0"},{"id",pendingRequest_},{"method","textDocument/completion"},{"params",lastQuery_}});
    }
};
}
