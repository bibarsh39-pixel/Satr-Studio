#pragma once
#include "completion_edit.hpp"
#include <sstream>
namespace satr {
inline const char* JavaKeywords(){return "abstract assert boolean break byte case catch char class const continue default do double else enum extends final finally float for if implements import instanceof int interface long native new package private protected public record return sealed short static strictfp super switch synchronized this throw throws transient try var void volatile while true false null permits non-sealed";}
inline std::vector<CompletionItem> JavaCompletions(const std::string& prefix){
    std::string words=std::string(JavaKeywords())+" String StringBuilder System out err in Scanner ArrayList LinkedList List Map HashMap Set HashSet Arrays Collections Math Integer Double Long Object Exception RuntimeException IOException println() print() printf() nextInt() nextDouble() nextLine() next() hasNext() hasNextInt() length() size() add() get() set() remove() isEmpty() contains() equals() toString() valueOf() parseInt() parseDouble() sort() max() min() abs() sqrt() pow() close() readLine() append() charAt() substring() compareTo()";
    std::vector<CompletionItem> items;std::istringstream stream(words);std::string word;
    while(stream>>word){bool callable=word.size()>2&&word.substr(word.size()-2)=="()";if(callable)word.resize(word.size()-2);if(word.compare(0,prefix.size(),prefix)!=0)continue;CompletionItem item;item.name=word;item.insert=word;item.kind=callable?CompletionKind::Function:CompletionKind::Word;item.signature=callable?word+"(...)":word;items.push_back(item);}return items;
}

inline std::vector<CompletionItem> ScriptCompletions(const std::string& prefix,bool python){
    const char* words=python?
        "False None True and as assert async await break class continue def del elif else except finally for from global if import in is lambda nonlocal not or pass raise return try while with yield print() input() len() range() int() str() float() list() dict() set() tuple() sum() sorted() enumerate() zip() open() isinstance() super() abs() min() max()":
        "async await break case catch class const continue debugger default delete do else export extends false finally for from function if import in instanceof let new null of return static super switch this throw true try typeof undefined var void while with yield console log() require() setTimeout() setInterval() parseInt() parseFloat() Number() String() Array() Object() Promise() JSON Math";
    std::vector<CompletionItem> items;std::istringstream stream(words);std::string word;
    while(stream>>word){bool callable=word.size()>2&&word.substr(word.size()-2)=="()";if(callable)word.resize(word.size()-2);if(word.compare(0,prefix.size(),prefix)!=0)continue;CompletionItem item;item.name=word;item.insert=word;item.kind=callable?CompletionKind::Function:CompletionKind::Word;item.signature=callable?word+"(...)":word;items.push_back(item);}
    return items;
}
}
