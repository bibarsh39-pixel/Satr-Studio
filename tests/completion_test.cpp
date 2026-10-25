#include "../src/completion_edit.hpp"
#include <iostream>
#include <cstdlib>
static int failed=0,checks=0;
static void expect(bool value,const char* name){++checks;if(!value){++failed;std::cerr<<"FAIL "<<name<<'\n';}}
static bool has(const std::string& prefix,const std::string& word,const std::string& source="") {auto list=satr::Complete(prefix,source);return std::find(list.begin(),list.end(),word)!=list.end();}
int main(){
    expect(has("print","printf"),"printf prefix");expect(has("sca","scanf"),"scanf prefix");
    expect(has("cons","constexpr"),"C++ keyword");expect(has("co_","co_await"),"coroutine keyword");
    expect(has("_Gen","_Generic"),"C keyword");expect(has("uint","uint64_t"),"stdint type");
    expect(has("vec","vector"),"container");expect(has("make_","make_unique"),"smart pointer");
    expect(has("myV","myVariable","int myVariable=5;"),"document identifier");
    expect(has("hel","helperFunction","int helperFunction(){return 1;}"),"document function");
    expect(!has("PR","printf"),"case sensitivity");expect(satr::Complete("","").empty(),"empty prefix");
    expect(satr::Complete("xyz_nonexistent","").empty(),"no irrelevant suggestions");
    auto list=satr::Complete("pri","printf printf printify");expect(std::count(list.begin(),list.end(),"printf")==1,"deduplication");
    expect(std::is_sorted(list.begin(),list.end()),"ordered suggestions");
    expect(satr::FunctionTips().at("printf").find("format")!=std::string::npos,"printf signature");
    auto printfItem=satr::LocalItem("printf");auto edit=satr::ExpandCompletion(printfItem,"");
    expect(edit.text=="printf()"&&edit.caret==7,"function parentheses and caret");
    edit=satr::ExpandCompletion(printfItem,"(42)");expect(edit.text=="printf"&&edit.caret==7,"reuse existing parentheses");
    edit=satr::ExpandCompletion(printfItem,"  (42)");expect(edit.text=="printf"&&edit.caret==9,"reuse parentheses after spaces");
    edit=satr::ExpandCompletion(printfItem,"",true);expect(edit.text=="printf"&&edit.caret==6,"address of function stays a name");
    edit=satr::ExpandCompletion(satr::LocalItem("while"),"");expect(edit.text=="while","keyword does not become function");
    edit=satr::ExpandCompletion({"value","value","","",satr::CompletionKind::Variable},"");expect(edit.text=="value","variable has no parentheses");
    edit=satr::ExpandCompletion({"compute","compute(${1:int a}, ${2:double b})","","",satr::CompletionKind::Function},"");expect(edit.text=="compute()"&&edit.caret==8,"parameter types are not inserted into source");
    edit=satr::ExpandCompletion({"make_unique","make_unique<${1:typename T}>(${2:args})","","",satr::CompletionKind::Function},"");expect(edit.text=="make_unique<>()"&&edit.caret==12,"template function starts at type parameter");
    edit=satr::ExpandCompletion({"vector","vector<${1:typename T}>","","",satr::CompletionKind::Type},"");expect(edit.text=="vector<>"&&edit.caret==7,"template type placeholder");
    edit=satr::ExpandCompletion(satr::LocalItem("std"),"");expect(edit.text=="std::"&&edit.caret==5,"namespace scope insertion");
    edit=satr::ExpandCompletion(satr::LocalItem("std"),"::cout");expect(edit.text=="std"&&edit.caret==5,"reuse namespace separator");
    satr::CompletionItem header{"stdio.h","stdio.h","","stdio.h",satr::CompletionKind::Header};
    edit=satr::ExpandCompletion(header,"");expect(edit.text=="stdio.h>"&&edit.caret==8,"close angle include");
    edit=satr::ExpandCompletion(header,">");expect(edit.text=="stdio.h"&&edit.caret==8,"reuse include closing bracket");
    edit=satr::ExpandCompletion(header,"",false,'"');expect(edit.text=="stdio.h\"","close quoted include");
    expect(satr::RemoveSnippetFields("name${1:value}$0")=="name","no raw LSP placeholders");
    expect(satr::CFunctions().size()>200,"expanded C callable catalog");
    expect(satr::AvailableWord("printf",true,17),"C printf available");
    expect(!satr::AvailableWord("vector",true,17),"no C++ containers in C catalog");
    expect(!satr::AvailableWord("gets",false,17),"removed gets omitted");
    expect(!satr::AvailableWord("random_shuffle",false,17),"removed random_shuffle omitted");
    expect(!satr::AvailableWord("co_await",false,17)&&satr::AvailableWord("co_await",false,20),"C++20 coroutine filtering");
    expect(!satr::AvailableWord("println",false,20)&&satr::AvailableWord("println",false,23),"C++23 print filtering");
    auto cItems=satr::LocalCompletions("wpri","",true,17);expect(!cItems.empty()&&cItems[0].name=="wprintf"&&cItems[0].kind==satr::CompletionKind::Function,"wide character function completion");
    auto cHeaders=satr::HeaderCompletions("std",true,17);expect(std::any_of(cHeaders.begin(),cHeaders.end(),[](const auto& item){return item.name=="stdio.h";}),"C header suggestions");
    expect(satr::HeaderCompletions("vec",true,17).empty(),"no C++ headers in C");
    expect(!satr::HeaderCompletions("vec",false,17).empty(),"C++ vector header");
    expect(satr::HeaderCompletions("spa",false,17).empty()&&!satr::HeaderCompletions("spa",false,20).empty(),"C++20 header filtering");
    expect(satr::HeaderFor("printf")=="stdio.h"&&satr::HeaderFor("sort")=="algorithm","required header metadata");
    std::cout<<(failed?"FAIL":"PASS")<<": "<<checks<<" completion checks, "<<failed<<" failed\n";return failed?EXIT_FAILURE:EXIT_SUCCESS;
}
