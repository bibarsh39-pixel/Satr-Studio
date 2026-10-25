#include <wx/wx.h>
#include <wx/aui/aui.h>
#include <wx/stc/stc.h>
#include <wx/listctrl.h>
#include <wx/treectrl.h>
#include <wx/splitter.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/file.h>
#include <wx/dir.h>
#include <wx/xml/xml.h>
#include <wx/dcbuffer.h>
#include <wx/dcscreen.h>
#include <wx/translation.h>
#include <wx/simplebook.h>
#include <wx/scrolwin.h>
#include <wx/tooltip.h>
#include <future>
#include <wx/weakref.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <set>
#include <deque>
#include <memory>
#include <nlohmann/json.hpp>
#include "diagnostics.hpp"
#include "process.hpp"
#include "completion.hpp"
#include "completion_edit.hpp"
#include "script_completion.hpp"
#include "build_cache.hpp"
#include "native_runtime.hpp"
#include "java_support.hpp"
#include "language_server.hpp"
#include "ui_i18n.hpp"
#include "theme.hpp"
#include "external_console.hpp"
#include "repair.hpp"
#ifdef _WIN32
#include "setup_dialog.hpp"
#endif
#include "platform.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;
static wxString U(const std::string& s) { return wxString::FromUTF8(s.data(), s.size()); }
static wxString T(const std::string& s) { return ui::Text(s); }
static std::string Utf8(const wxString& s) { return std::string(s.utf8_str()); }
static const wxColour Paper=theme::Editor, Background=theme::Soft, Ink=theme::Ink, Muted=theme::Hint, Blue=theme::Blue, Green=theme::Accent, Red=theme::Error;
static wxString ReadUtf8(const wxString& path) {
    if (!wxFileExists(path)) return wxString();
    wxFile file(path); wxString text;
    if (file.IsOpened()) file.ReadAll(&text, wxConvUTF8);
    return text;
}
static bool WriteUtf8(const wxString& path, const wxString& text) {
    wxTempFile file(path);
    if (!file.IsOpened()) return false;
    auto bytes = text.utf8_str();
    return file.Write(bytes.data(), bytes.length()) && file.Commit();
}
static wxString BaseDirectory() { return wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath(); }
static bool EnsureDirectory(const wxString& path,int permissions=wxS_DIR_DEFAULT,int flags=wxPATH_MKDIR_FULL){return wxDirExists(path)||wxFileName::Mkdir(path,permissions,flags);}
static wxString TestDataDirectory;
static bool LanguageSmoke=false;
static wxString UserDataDirectory() { return TestDataDirectory.empty()?wxStandardPaths::Get().GetUserLocalDataDir():TestDataDirectory; }
static wxString ProgramName(const wxString& name) {
#ifdef _WIN32
    return name+".exe";
#else
    return name;
#endif
}
static wxString CCompiler(const wxString& cpp) {
    auto name=wxFileName(cpp).GetFullName();name.Replace("g++","gcc");
    return wxFileName(cpp).GetPath()+"/"+name;
}
static bool SamePath(const wxString& a,const wxString& b) {
#ifdef _WIN32
    return a.CmpNoCase(b)==0;
#else
    return a==b;
#endif
}
static bool NativeSourceFile(const wxString& path) {
    auto extension = wxFileName(path).GetExt().Lower();
    return extension == "c" || extension == "cpp" || extension == "cc" || extension == "cxx";
}
static bool SourceFile(const wxString& path) {
    auto ext=wxFileName(path).GetExt().Lower();
    return NativeSourceFile(path)||ext=="py"||ext=="js"||ext=="mjs"||ext=="cjs"||ext=="java";
}
static wxString Canonical(const wxString& path, const wxString& base = wxEmptyString) {
    wxFileName name(path); name.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE, base); return name.GetFullPath();
}

class Document : public wxStyledTextCtrl {
    std::map<std::string,satr::CompletionItem> completionItems_;
    std::map<std::string,std::string> semanticTips_;
    bool changingCompletion_=false,headerCompletion_=false;
    char headerClose_='>';
    int completionStart_=0;
    bool LiteralAt(int position){
        Colourise(std::max(0,PositionFromLine(LineFromPosition(position))-1),std::min(GetTextLength(),position+1));
        int style=GetStyleAt(std::max(0,position-1));
        if(IsPython())return style==wxSTC_P_COMMENTLINE||style==wxSTC_P_STRING||style==wxSTC_P_CHARACTER||style==wxSTC_P_TRIPLE||style==wxSTC_P_TRIPLEDOUBLE;
        return style==wxSTC_C_COMMENT||style==wxSTC_C_COMMENTLINE||style==wxSTC_C_COMMENTDOC||style==wxSTC_C_STRING||style==wxSTC_C_CHARACTER||style==wxSTC_C_STRINGRAW;
    }
    void Present(const std::vector<satr::CompletionItem>& items,int start){
        if(items.empty())return;
        changingCompletion_=true;auto selected=Utf8(AutoCompGetCurrentText());AutoCompCancel();completionItems_.clear();wxString list;
        for(const auto& item:items){if(item.name.empty()||item.name.find_first_of("\n\r?")!=std::string::npos)continue;if(completionItems_.count(item.name))continue;
            completionItems_[item.name]=item;if(!list.empty())list+='\n';list+=U(item.name)+wxString::Format("?%d",static_cast<int>(item.kind)+1);
        }
        completionStart_=start;AutoCompSetSeparator('\n');AutoCompSetTypeSeparator('?');if(!list.empty()){AutoCompShow(GetCurrentPos()-start,list);if(completionItems_.count(selected))AutoCompSelect(U(selected));}changingCompletion_=false;
    }
    void AcceptCompletion(wxStyledTextEvent& event){
        auto name=Utf8(event.GetText());auto found=completionItems_.find(name);if(found==completionItems_.end())return;auto item=found->second;
        changingCompletion_=true;AutoCompCancel();changingCompletion_=false;++completionGeneration;
        int start=completionStart_,end=headerCompletion_?GetCurrentPos():WordEndPosition(GetCurrentPos(),true);
        if(start<0||end<start||end>GetTextLength())return;
        auto before=GetTextRange(std::max(0,start-64),start);before.Trim();bool address=before.EndsWith("&");
        auto expansion=satr::ExpandCompletion(item,Utf8(GetTextRange(end,std::min(end+128,GetTextLength()))),address,headerClose_);
        BeginUndoAction();SetTargetStart(start);SetTargetEnd(end);ReplaceTarget(U(expansion.text));GotoPos(std::min(GetTextLength(),start+static_cast<int>(expansion.caret)));EndUndoAction();
        if(item.kind==satr::CompletionKind::Function&&!address){
            auto tip=item.signature.empty()?item.name+"(...)":item.signature;
            if(!IsC()&&item.header.size()>2&&item.header.substr(item.header.size()-2)==".h")item.header="c"+item.header.substr(0,item.header.size()-2);
            if(!item.header.empty())tip+="\n#include <"+item.header+">";
            semanticTips_[item.name]=tip;if(semanticTips_.size()>128)semanticTips_.erase(semanticTips_.begin());CallTipShow(start,U(tip));
        }
    }
public:
    wxString path;
    wxString suggestedName="main.cpp";
    int cppStandard=17;
    unsigned completionGeneration=0;
    std::function<void(Document*)> requestSemantic;
    bool IsC()const{return wxFileName(path.empty()?suggestedName:path).GetExt().Lower()=="c";}
    bool IsPython()const{return wxFileName(path.empty()?suggestedName:path).GetExt().Lower()=="py";}
    bool IsJavaScript()const{auto ext=wxFileName(path.empty()?suggestedName:path).GetExt().Lower();return ext=="js"||ext=="mjs"||ext=="cjs";}
    bool IsJava()const{return wxFileName(path.empty()?suggestedName:path).GetExt().Lower()=="java";}
    bool IsNative()const{return !IsPython()&&!IsJavaScript()&&!IsJava();}
    wxString LanguageName()const{return IsPython()?"Python":IsJavaScript()?"JavaScript":IsJava()?"Java":IsC()?"C":"C++";}
    void ConfigureLanguage(){
        SetLexer(IsPython()?wxSTC_LEX_PYTHON:wxSTC_LEX_CPP);
        if(IsPython()){
            SetKeyWords(0,"False None True and as assert async await break class continue def del elif else except finally for from global if import in is lambda nonlocal not or pass raise return try while with yield");
            StyleSetForeground(wxSTC_P_COMMENTLINE,wxColour("#66836E"));
            for(int style:{wxSTC_P_STRING,wxSTC_P_CHARACTER,wxSTC_P_TRIPLE,wxSTC_P_TRIPLEDOUBLE})StyleSetForeground(style,wxColour("#167052"));
            StyleSetForeground(wxSTC_P_WORD,wxColour("#7445A9"));StyleSetForeground(wxSTC_P_NUMBER,wxColour("#AC570F"));
        }else{
            SetKeyWords(0,IsJava()?satr::JavaKeywords():IsJavaScript()?"async await break case catch class const continue debugger default delete do else export extends false finally for from function if import in instanceof let new null of return static super switch this throw true try typeof undefined var void while with yield":
                "alignas alignof asm auto bool break case catch char char16_t char32_t class const constexpr continue decltype default delete do double else enum explicit extern false float for friend goto if inline int long namespace new noexcept nullptr operator private protected public register reinterpret_cast return short signed sizeof static static_assert static_cast struct switch template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while include define");
            for(int style:{wxSTC_C_COMMENT,wxSTC_C_COMMENTLINE,wxSTC_C_COMMENTDOC})StyleSetForeground(style,wxColour("#66836E"));
            StyleSetForeground(wxSTC_C_WORD,wxColour("#7445A9"));StyleSetForeground(wxSTC_C_STRING,wxColour("#167052"));StyleSetForeground(wxSTC_C_CHARACTER,wxColour("#167052"));StyleSetForeground(wxSTC_C_NUMBER,wxColour("#AC570F"));
        }
        Colourise(0,-1);
    }
    explicit Document(wxWindow* parent) : wxStyledTextCtrl(parent, wxID_ANY) {
        SetLayoutDirection(wxLayout_LeftToRight);
        SetCodePage(wxSTC_CP_UTF8);
        SetLexer(wxSTC_LEX_CPP);
        StyleSetFont(wxSTC_STYLE_DEFAULT, theme::Code(12));
        StyleSetForeground(wxSTC_STYLE_DEFAULT, Ink); StyleSetBackground(wxSTC_STYLE_DEFAULT, Paper); StyleClearAll();
        StyleSetForeground(wxSTC_C_COMMENT, wxColour("#66836E"));
        StyleSetForeground(wxSTC_C_COMMENTLINE, wxColour("#66836E"));
        StyleSetForeground(wxSTC_C_COMMENTDOC, wxColour("#66836E"));
        StyleSetForeground(wxSTC_C_WORD, wxColour("#7445A9")); StyleSetBold(wxSTC_C_WORD, false);
        StyleSetForeground(wxSTC_C_STRING, wxColour("#167052"));
        StyleSetForeground(wxSTC_C_CHARACTER, wxColour("#167052"));
        StyleSetForeground(wxSTC_C_NUMBER, wxColour("#AC570F"));
        StyleSetForeground(wxSTC_C_PREPROCESSOR, wxColour("#2868B7"));
        StyleSetForeground(wxSTC_C_OPERATOR, Ink);
        StyleSetForeground(wxSTC_C_IDENTIFIER, Ink);
        StyleSetForeground(wxSTC_STYLE_LINENUMBER, wxColour("#8696AD"));
        StyleSetBackground(wxSTC_STYLE_LINENUMBER, Paper);
        SetKeyWords(0, "alignas alignof asm auto bool break case catch char char16_t char32_t class const constexpr continue decltype default delete do double else enum explicit extern false float for friend goto if inline int long namespace new noexcept nullptr operator private protected public register reinterpret_cast return short signed sizeof static static_assert static_cast struct switch template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while include define");
        SetMarginType(0, wxSTC_MARGIN_NUMBER); SetMarginWidth(0, FromDIP(62));
        SetMarginWidth(1, 4); SetMarginWidth(2, 0);
        SetTabWidth(4); SetIndent(4); SetUseTabs(false); SetIndentationGuides(wxSTC_IV_LOOKBOTH);
        SetCaretForeground(Blue); SetCaretWidth(2); SetCaretLineVisible(true); SetCaretLineBackground(wxColour("#F0F5FC"));
        SetSelBackground(true, wxColour("#DAE7FA")); SetSelForeground(true, Ink);SetExtraAscent(3);SetExtraDescent(3);
        StyleSetForeground(wxSTC_STYLE_BRACELIGHT,theme::Accent);StyleSetBackground(wxSTC_STYLE_BRACELIGHT,theme::Raised);
        SetFoldMarginColour(true,Paper);SetFoldMarginHiColour(true,Paper);SetWhitespaceForeground(true,theme::Border);
        AutoCompSetMaxHeight(10);AutoCompSetIgnoreCase(false);AutoCompSetDropRestOfWord(true);AutoCompSetAutoHide(true);
        AutoCompSetMaxWidth(54);
        const char* labels[]={"k","f","T","v","N","#"};const char* colors[]={"#7959B5","#2D6FC8","#147D6A","#9B681F","#7959B5","#5F718C"};
        for(int i=0;i<6;++i){wxBitmap bitmap(FromDIP(18),FromDIP(18));wxMemoryDC dc(bitmap);dc.SetBackground(wxBrush(Paper));dc.Clear();dc.SetPen(*wxTRANSPARENT_PEN);dc.SetBrush(wxBrush(wxColour(colors[i])));dc.DrawRoundedRectangle(1,1,FromDIP(16),FromDIP(16),3);dc.SetFont(theme::Code(8));dc.SetTextForeground(*wxWHITE);auto size=dc.GetTextExtent(labels[i]);dc.DrawText(labels[i],(FromDIP(18)-size.x)/2,(FromDIP(18)-size.y)/2);dc.SelectObject(wxNullBitmap);RegisterImage(i+1,bitmap);}
        CallTipSetBackground(theme::Soft);CallTipSetForeground(Ink);CallTipSetForegroundHighlight(Blue);
        SetWrapMode(wxSTC_WRAP_NONE); SetScrollWidthTracking(true); SetScrollWidth(100);
        SetEOLMode(wxSTC_EOL_LF); SetBufferedDraw(true);
        IndicatorSetStyle(0, wxSTC_INDIC_SQUIGGLE); IndicatorSetForeground(0, Red);
        IndicatorSetStyle(1, wxSTC_INDIC_SQUIGGLE); IndicatorSetForeground(1, theme::Warning);
        MarkerDefine(1, wxSTC_MARK_BACKGROUND); MarkerSetBackground(1, wxColour("#FFF0F2"));
        SetName(T("محرر الكود"));
        UsePopUp(wxSTC_POPUP_NEVER);
        Bind(wxEVT_STC_AUTOCOMP_SELECTION,&Document::AcceptCompletion,this);
        Bind(wxEVT_STC_AUTOCOMP_CANCELLED,[this](wxStyledTextEvent&){if(!changingCompletion_)++completionGeneration;});
        Bind(wxEVT_CHAR,[this](wxKeyEvent& e){if(!HandlePair(e.GetUnicodeKey()))e.Skip();});
        Bind(wxEVT_KEY_DOWN,[this](wxKeyEvent& e){
            if(e.GetKeyCode()==WXK_ESCAPE){++completionGeneration;CallTipCancel();}
            if(e.GetKeyCode()==WXK_BACK&&!AutoCompActive()&&GetSelectionEmpty()&&GetCurrentPos()>0){
                int p=GetCurrentPos();auto pair=GetTextRange(p-1,std::min(p+1,GetTextLength()));
                if(pair=="()"||pair=="[]"||pair=="{}"||pair=="\"\""||pair=="''"){BeginUndoAction();DeleteRange(p-1,2);GotoPos(p-1);EndUndoAction();++completionGeneration;return;}
            }e.Skip();
        });
        Bind(wxEVT_CONTEXT_MENU, [this](wxContextMenuEvent&) {
            wxMenu menu;
            menu.Append(wxID_UNDO,T("تراجع")); menu.Append(wxID_REDO,T("إعادة")); menu.AppendSeparator();
            menu.Append(wxID_CUT,T("قص")); menu.Append(wxID_COPY,T("نسخ")); menu.Append(wxID_PASTE,T("لصق")); menu.Append(wxID_SELECTALL,T("تحديد الكل"));
            menu.Enable(wxID_UNDO,CanUndo()); menu.Enable(wxID_REDO,CanRedo()); menu.Enable(wxID_PASTE,CanPaste());
            menu.Enable(wxID_COPY,GetSelectionStart()!=GetSelectionEnd()); menu.Enable(wxID_CUT,GetSelectionStart()!=GetSelectionEnd());
            menu.Bind(wxEVT_MENU,[this](wxCommandEvent& event) {
                switch(event.GetId()) { case wxID_UNDO: Undo(); break; case wxID_REDO: Redo(); break; case wxID_CUT: Cut(); break; case wxID_COPY: Copy(); break; case wxID_PASTE: Paste(); break; case wxID_SELECTALL: SelectAll(); break; }
            });
            PopupMenu(&menu);
        });
        Bind(wxEVT_STC_CHARADDED, [this](wxStyledTextEvent& e) {
            if (e.GetKey() == '\n') {
                int line = GetCurrentLine();
                if (line > 0) {
                    int indent = GetLineIndentation(line - 1);
                    wxString previous = GetLine(line - 1); previous.Trim();
                    if (previous.EndsWith("{")||(IsPython()&&previous.EndsWith(":"))) indent += 4;
                    SetLineIndentation(line, indent); GotoPos(GetLineIndentPosition(line));
                }
            }
            const int key=e.GetKey();
            if((key>='a'&&key<='z')||(key>='A'&&key<='Z')||key=='_'||key=='.'||key==':'||key=='>') CompleteWord(false);
            if(key=='(') ShowFunctionTip();
            if(key==')'||key==';') CallTipCancel();
        });
    }
    void CompleteWord(bool manual) {
        int end=GetCurrentPos(),start=WordStartPosition(end,true);auto prefix=Utf8(GetTextRange(start,end));
        auto line=Utf8(GetTextRange(PositionFromLine(GetCurrentLine()),end));auto include=line.find_first_not_of(" \t");if(include!=std::string::npos&&line.compare(include,8,"#include")!=0)include=std::string::npos;auto delimiter=include==std::string::npos?std::string::npos:line.find_first_of("<\"",include+8);
        headerCompletion_=IsNative()&&delimiter!=std::string::npos&&line.find_first_of(">\"",delimiter+1)==std::string::npos;
        if(headerCompletion_){headerClose_=line[delimiter]=='<'?'>':'"';prefix=line.substr(delimiter+1);start=end-static_cast<int>(prefix.size());++completionGeneration;Present(satr::HeaderCompletions(prefix,IsC(),cppStandard),start);return;}
        if(LiteralAt(end))return;
        auto before=GetTextRange(std::max(0,start-2),start);bool member=before.EndsWith(".")||before.EndsWith("->"),qualified=before.EndsWith("::");
        if(!manual&&!member&&!qualified&&prefix.size()<2)return;
        ++completionGeneration;completionStart_=start;
        auto items=IsNative()?satr::LocalCompletions(prefix,Utf8(GetText()),IsC(),cppStandard,member,qualified):IsJava()?satr::JavaCompletions(prefix):satr::ScriptCompletions(prefix,IsPython());
        if(!items.empty())Present(items,start);else{changingCompletion_=true;AutoCompCancel();completionItems_.clear();changingCompletion_=false;}
        if(IsNative()&&requestSemantic)requestSemantic(this);
    }
    void ShowSemanticCompletions(const satr::LspJson& result){
        if(!result.is_object()&&!result.is_array())return;
        auto list=result.is_array()?result:result.value("items",satr::LspJson::array());if(!list.is_array())return;
        std::map<std::string,satr::CompletionItem> items;auto context=GetTextRange(std::max(0,completionStart_-2),completionStart_);bool member=context.EndsWith(".")||context.EndsWith("->");
        if(!member)items=completionItems_;
        auto prefix=Utf8(GetTextRange(completionStart_,GetCurrentPos()));
        std::set<std::string> semanticSeen;
        for(const auto& entry:list){if(!entry.is_object())continue;satr::CompletionItem item;item.name=entry.value("filterText",entry.value("label",""));while(!item.name.empty()&&item.name.front()==' ')item.name.erase(0,1);
            if(item.name.size()>128||item.name.compare(0,prefix.size(),prefix)!=0)continue;
            item.insert=entry.value("insertText",item.name);if(entry.contains("textEdit")&&entry["textEdit"].is_object())item.insert=entry["textEdit"].value("newText",item.insert);
            if(IsC()&&item.insert.find("::")!=std::string::npos)continue;
            if(!semanticSeen.insert(item.name).second)continue;
            int kind=entry.value("kind",1);item.kind=(kind==2||kind==3||kind==4)?satr::CompletionKind::Function:(kind==7||kind==8||kind==13||kind==22||kind==25)?satr::CompletionKind::Type:kind==9?satr::CompletionKind::Namespace:(kind==5||kind==6||kind==10||kind==12||kind==20||kind==21)?satr::CompletionKind::Variable:satr::CompletionKind::Word;
            if(kind==15&&item.insert.find('(')!=std::string::npos)item.kind=satr::CompletionKind::Function;
            if((kind==1||kind==18)&&satr::Callable(item.name))item.kind=satr::CompletionKind::Function;
            item.signature=entry.value("label",item.name);if(entry.contains("detail")&&entry["detail"].is_string())item.signature+="\n"+entry["detail"].get<std::string>();item.header=satr::HeaderFor(item.name);
            if(item.kind==satr::CompletionKind::Function&&item.signature.find('(')==std::string::npos){auto tip=satr::FunctionTips().find(item.name);if(tip!=satr::FunctionTips().end())item.signature=tip->second;}
            items[item.name]=std::move(item);
        }
        std::vector<satr::CompletionItem> ordered;for(const auto& item:items){ordered.push_back(item.second);if(ordered.size()==100)break;}if(!ordered.empty())Present(ordered,completionStart_);
    }
    bool HandlePair(int key){
        if(GetReadOnly())return false;
        int pos=GetCurrentPos();bool selected=!GetSelectionEmpty();
        if(!AutoCompActive()&&(key==WXK_RETURN||key=='\n')&&!selected&&pos>0&&GetCharAt(pos-1)=='{'&&GetCharAt(pos)=='}'){
            int indent=GetLineIndentation(GetCurrentLine());auto value="\n"+std::string(indent+4,' ')+"\n"+std::string(indent,' ');BeginUndoAction();InsertText(pos,U(value));GotoPos(pos+indent+5);EndUndoAction();++completionGeneration;return true;
        }
        if(key!=')'&&key!=']'&&key!='}'&&key!='('&&key!='['&&key!='{'&&key!='"'&&key!='\'')return false;
        if(key=='"'||key=='\''){int slashes=0;for(int at=pos-1;at>=0&&GetCharAt(at)=='\\';--at)++slashes;if(slashes%2)return false;}
        if(!selected&&(key==')'||key==']'||key=='}'||key=='"'||key=='\'')&&GetCharAt(pos)==key){GotoPos(pos+1);if(key==')')CallTipCancel();++completionGeneration;return true;}
        if(LiteralAt(pos))return false;
        char close=key=='('?')':key=='['?']':key=='{'?'}':key=='"'?'"':key=='\''?'\'':0;if(!close)return false;
        if((key=='"'||key=='\'')&&pos>0&&GetCharAt(pos-1)=='\\')return false;
        if(key=='\''&&pos>0&&std::isalnum(static_cast<unsigned char>(GetCharAt(pos-1))))return false;
        if(!selected&&pos<GetTextLength()&&(std::isalnum(static_cast<unsigned char>(GetCharAt(pos)))||GetCharAt(pos)=='_'))return false;
        changingCompletion_=true;AutoCompCancel();changingCompletion_=false;int start=GetSelectionStart();auto inner=GetSelectedText();
        BeginUndoAction();ReplaceSelection(wxString(static_cast<char>(key))+inner+wxString(close));if(selected)SetSelection(start+1,start+1+static_cast<int>(inner.utf8_str().length()));else GotoPos(start+1);EndUndoAction();++completionGeneration;
        if(key=='(')ShowFunctionTip();
        return true;
    }
    void ShowFunctionTip() {
        if(!IsNative())return;
        int end=GetCurrentPos()-1,start=WordStartPosition(end,true);auto word=Utf8(GetTextRange(start,end));
        auto semantic=semanticTips_.find(word);if(semantic!=semanticTips_.end()){CallTipShow(start,U(semantic->second));return;}auto found=satr::FunctionTips().find(word);if(found!=satr::FunctionTips().end())CallTipShow(start,U(found->second));
    }
    void ClearProblems() {
        for (int i = 0; i < 2; ++i) { SetIndicatorCurrent(i); IndicatorClearRange(0, GetTextLength()); }
        MarkerDeleteAll(1);
    }
};

class FileData : public wxTreeItemData { public: wxString path; explicit FileData(wxString value) : path(std::move(value)) {} };
struct BuildStep { wxString executable; std::vector<std::wstring> args; wxString label; int kind=0;wxString prepared,object,dependencies;bool preprocessed=false,restoreTried=false; };

enum { ID_NEW = wxID_HIGHEST + 1, ID_OPEN, ID_SAVE, ID_SAVE_AS, ID_CLOSE_TAB, ID_BUILD, ID_RUN, ID_STOP,
       ID_PROJECT, ID_NEW_PROJECT, ID_CLOSE_PROJECT, ID_SETTINGS, ID_FIND, ID_NEXT_ERROR, ID_EXAMPLE, ID_ERROR_EXAMPLE, ID_ABOUT, ID_EOF, ID_TIMER, ID_MENU, ID_HOME, ID_NEW_C, ID_NEW_CPP, ID_PROBLEMS, ID_FIX, ID_SETUP, ID_NEW_PY, ID_NEW_JS, ID_NEW_JAVA };

class SatrFrame : public wxFrame {
    wxAuiNotebook* editors_ = nullptr;
    wxSimplebook* workspace_=nullptr;
    wxScrolledWindow* home_=nullptr;
    wxGridSizer* homeCards_=nullptr;
    wxStaticText* breadcrumb_=nullptr;
    std::vector<wxString> recentFiles_;
    theme::Book* bottom_ = nullptr;
    wxMenuBar* menuModel_=nullptr;
    wxStaticText* statusLabels_[3]={nullptr,nullptr,nullptr};
    wxTreeCtrl* tree_ = nullptr;
    wxListCtrl* problems_ = nullptr;
    wxTextCtrl *console_ = nullptr, *log_ = nullptr, *input_ = nullptr, *details_ = nullptr;
    wxStaticText *projectTitle_ = nullptr, *stateText_ = nullptr;
    theme::Button *runButton_ = nullptr, *buildButton_ = nullptr, *stopButton_ = nullptr, *sendButton_ = nullptr;
    wxSplitterWindow *horizontal_ = nullptr, *vertical_ = nullptr;
    wxTimer timer_;
    satr::Process process_;
    satr::ExternalConsole external_;
    std::unique_ptr<satr::LanguageServer> languageServer_;
    wxWeakRef<Document> pendingAssist_;
    ULONGLONG assistDue_=0;
    unsigned assistGeneration_=0;
    int assistPosition_=0;
    bool advancedCompletion_=true, localRepair_=true;
    bool optimized_=false,cacheEnabled_=true;
    bool portableTest_=false;
    json toolPaths_=json::object();
    theme::Button* fixButton_=nullptr;
    std::unique_ptr<satr::RepairTask> repair_;
    wxWeakRef<Document> repairDocument_;
    std::string repairSource_;
    wxString repairPath_;
    satr::RepairTask::State repairState_=satr::RepairTask::State::Idle;
    bool externalRun_=false;
    bool scriptBuild_=false,javaBuild_=false;
    satr::JavaPlan javaPlan_;
    std::future<satr::JavaPlan> javaPreparation_;
    std::future<bool> javaCommit_;
    wxString scriptSource_;
    satr::BuildCache buildCache_;
    satr::NativeRuntime nativeRuntime_;
    std::future<bool> nativeRuntimePreparation_;
    bool waitingForNativeRuntime_=false;
    bool benchmarkExternal_=false;
    std::future<bool> cacheRestore_;
    BuildStep activeStep_;
    std::string activeKey_,activePrepared_,activeOutput_;
    unsigned reusedUnits_=0;bool reusedLink_=false;
    std::vector<std::wstring> runArguments_;
    std::vector<satr::Diagnostic> diagnostics_;
    std::deque<BuildStep> steps_;
    wxString compiler_, standard_ = "c++17", projectFile_, projectDir_, projectName_, binary_, settingsPath_, buildWorkingDirectory_;
    std::vector<wxString> sources_, includeDirs_, definitions_, libraries_;
    std::string buildOutput_, runtimePending_;
    bool building_ = false, running_ = false, runAfterBuild_ = false, stopping_ = false;
    bool diagnosticsStale_ = false;
    std::chrono::steady_clock::time_point started_;
    wxString findText_;
    int smokeStage_ = 0;
    wxString smokeDirectory_;
    json smokeResults_ = json::array();
    std::chrono::steady_clock::time_point smokeStarted_ = std::chrono::steady_clock::now();

    Document* Current() const { return editors_->GetSelection() == wxNOT_FOUND ? nullptr : dynamic_cast<Document*>(editors_->GetCurrentPage()); }
    wxMenuBar* GetMenuBar() const {return menuModel_;}
    void SetStatusText(const wxString& text,int field=0) {if(field>=0&&field<3&&statusLabels_[field]){statusLabels_[field]->SetLabel(text);statusLabels_[field]->GetParent()->Layout();}}
    void Notify(const wxString& text, bool failure = false) {
        stateText_->SetLabel(text); stateText_->SetForegroundColour(failure ? wxColour("#FFACBC") : theme::Muted);
        SetStatusText(text, 0); stateText_->GetParent()->Layout();
    }
    void Message(const wxString& text, long style = wxOK | wxICON_INFORMATION) {
        if(!smokeDirectory_.empty()){SmokeRecord("unexpected_dialog",false,Utf8(text));return;}
        wxMessageDialog dlg(this, text, U("Satr Studio"), style); dlg.SetOKLabel(T("حسنًا")); dlg.ShowModal();
    }
    theme::Button* Button(wxWindow* parent, const wxString& label, int id, bool primary = false) {
        wxString icon=id==ID_RUN?"play":(id==ID_BUILD||id==ID_FIX)?"build":id==ID_STOP?"stop":id==ID_SAVE?"save":id==ID_OPEN?"folder":id==ID_NEW?"plus":(id==ID_SETTINGS||id==ID_SETUP)?"settings":id==ID_MENU?"menu":id==ID_FIND?"search":id==ID_HOME?"home":id==ID_PROBLEMS?"alert":id==wxID_APPLY?"arrow":"";
        return new theme::Button(parent,id,label,icon,primary,primary?142:id==ID_EOF?144:id==ID_SETTINGS?120:(id==ID_FIX||id==ID_SETUP)?184:80);
    }
    wxMenu* Menu(const std::vector<std::pair<int, wxString>>& items) {
        auto* menu = new wxMenu;
        for (const auto& item : items) { if (item.first == -1) menu->AppendSeparator(); else menu->Append(item.first, item.second); }
        return menu;
    }
    wxButton* CancelButton(wxDialog& dialog){
        auto* button=new wxButton(&dialog,wxID_ANY,T("إلغاء"));
        button->Bind(wxEVT_BUTTON,[&dialog](wxCommandEvent&){dialog.EndModal(wxID_CANCEL);});
        dialog.SetEscapeId(button->GetId());return button;
    }
    void MakeMenus() {
        delete menuModel_;menuModel_=nullptr;auto* bar = new wxMenuBar;
        bar->Append(Menu({{ID_NEW,T("ملف جديد\tCtrl+N")},{ID_NEW_C,"C"},{ID_NEW_CPP,"C++"},{ID_NEW_PY,"Python"},{ID_NEW_JS,"JavaScript"},{ID_NEW_JAVA,"Java"},{ID_OPEN,T("فتح ملف…\tCtrl+O")},{ID_SAVE,T("حفظ\tCtrl+S")},{ID_SAVE_AS,T("حفظ باسم…\tCtrl+Shift+S")},{ID_CLOSE_TAB,T("إغلاق الملف\tCtrl+W")},{-1,""},{ID_NEW_PROJECT,T("مشروع جديد…")},{ID_PROJECT,T("فتح مشروع…")},{ID_CLOSE_PROJECT,T("إغلاق المشروع")},{-1,""},{wxID_EXIT,T("خروج\tAlt+F4")}}),T("ملف"));
        bar->Append(Menu({{wxID_UNDO,T("تراجع\tCtrl+Z")},{wxID_REDO,T("إعادة\tCtrl+Y")},{-1,""},{wxID_CUT,T("قص\tCtrl+X")},{wxID_COPY,T("نسخ\tCtrl+C")},{wxID_PASTE,T("لصق\tCtrl+V")},{wxID_SELECTALL,T("تحديد الكل\tCtrl+A")},{ID_FIND,T("بحث في الملف…\tCtrl+F")}}),T("تحرير"));
        bar->Append(Menu({{ID_RUN,T("ترجمة وتشغيل\tF9")},{ID_BUILD,T("ترجمة فقط\tCtrl+F9")},{ID_STOP,T("إيقاف\tShift+F9")},{ID_NEXT_ERROR,T("الخطأ التالي\tF4")},{ID_FIX,T("تصحيح الأخطاء\tCtrl+Shift+F9")}}),T("تشغيل"));
        bar->Append(Menu({{ID_EXAMPLE,T("مثال جاهز للتشغيل")},{ID_ERROR_EXAMPLE,T("مثال يحتوي على أخطاء")},{-1,""},{ID_SETUP,T("تحميل أدوات التشغيل…")},{ID_SETTINGS,T("الإعدادات…")},{ID_ABOUT,T("عن Satr Studio")}}),T("مساعدة"));
        menuModel_=bar;
        const wxAcceleratorEntry shortcuts[]={
            {wxACCEL_CTRL,'N',ID_NEW},{wxACCEL_CTRL,'O',ID_OPEN},{wxACCEL_CTRL,'S',ID_SAVE},{wxACCEL_CTRL|wxACCEL_SHIFT,'S',ID_SAVE_AS},{wxACCEL_CTRL,'W',ID_CLOSE_TAB},{wxACCEL_CTRL,'F',ID_FIND},
            {wxACCEL_NORMAL,WXK_F9,ID_RUN},{wxACCEL_CTRL,WXK_F9,ID_BUILD},{wxACCEL_SHIFT,WXK_F9,ID_STOP},{wxACCEL_NORMAL,WXK_F4,ID_NEXT_ERROR},{wxACCEL_CTRL|wxACCEL_SHIFT,WXK_F9,ID_FIX}
        };SetAcceleratorTable(wxAcceleratorTable(sizeof(shortcuts)/sizeof(shortcuts[0]),shortcuts));
    }
    void MakeLayout() {
        SetBackgroundColour(theme::Base);SetFont(theme::Font());
        auto* root=new wxPanel(this);root->SetBackgroundColour(theme::Base);root->SetForegroundColour(Ink);
        auto* layout=new wxBoxSizer(wxVERTICAL);
        auto* header=new wxPanel(root);header->SetBackgroundColour(theme::Base);auto* row=new wxBoxSizer(wxHORIZONTAL);
        runButton_=Button(header,T("تشغيل  F9"),ID_RUN,true);buildButton_=Button(header,T("ترجمة"),ID_BUILD);stopButton_=Button(header,T("إيقاف"),ID_STOP);
        row->Add(runButton_,0,wxRIGHT|wxALIGN_CENTER_VERTICAL,FromDIP(8));row->Add(buildButton_,0,wxRIGHT|wxALIGN_CENTER_VERTICAL,FromDIP(4));row->Add(stopButton_,0,wxALIGN_CENTER_VERTICAL);
        row->AddStretchSpacer();
        row->Add(Button(header,T("الرئيسية"),ID_HOME),0,wxRIGHT|wxALIGN_CENTER_VERTICAL,FromDIP(4));
        row->Add(Button(header,T("حفظ"),ID_SAVE),0,wxRIGHT|wxALIGN_CENTER_VERTICAL,FromDIP(4));
        row->Add(Button(header,T("فتح"),ID_OPEN),0,wxRIGHT|wxALIGN_CENTER_VERTICAL,FromDIP(4));
        row->Add(Button(header,T("جديد"),ID_NEW),0,wxRIGHT|wxALIGN_CENTER_VERTICAL,FromDIP(16));
        row->Add(Button(header,T("القائمة"),ID_MENU),0,wxRIGHT|wxALIGN_CENTER_VERTICAL,FromDIP(18));
        auto* brand=new wxStaticText(header,wxID_ANY,U("Satr Studio"));brand->SetFont(theme::Font(19,true));brand->SetForegroundColour(theme::Accent);
        row->Add(brand,0,wxALIGN_CENTER_VERTICAL|wxRIGHT,FromDIP(12));header->SetSizer(row);
        layout->Add(header,0,wxEXPAND|wxLEFT|wxRIGHT|wxTOP|wxBOTTOM,FromDIP(16));

        horizontal_=new wxSplitterWindow(root,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxSP_LIVE_UPDATE|wxBORDER_NONE);
        horizontal_->SetBackgroundColour(theme::Border);horizontal_->SetMinimumPaneSize(FromDIP(130));horizontal_->SetSashGravity(1.0);
        vertical_=new wxSplitterWindow(horizontal_,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxSP_LIVE_UPDATE|wxBORDER_NONE);
        vertical_->SetBackgroundColour(theme::Border);vertical_->SetMinimumPaneSize(FromDIP(190));vertical_->SetSashGravity(1.0);
        workspace_=new wxSimplebook(vertical_,wxID_ANY);workspace_->SetBackgroundColour(theme::Soft);
        home_=new wxScrolledWindow(workspace_,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxVSCROLL|wxBORDER_NONE);home_->SetBackgroundColour(theme::Soft);home_->SetScrollRate(0,12);workspace_->AddPage(home_,T("الرئيسية"));
        home_->Bind(wxEVT_SIZE,[this](wxSizeEvent& e){if(homeCards_){int columns=home_->GetClientSize().x<FromDIP(860)?1:3;if(homeCards_->GetCols()!=columns){homeCards_->SetCols(columns);home_->Layout();home_->FitInside();}}e.Skip();});
        auto* editorPage=new wxPanel(workspace_);editorPage->SetBackgroundColour(Paper);auto* editorLayout=new wxBoxSizer(wxVERTICAL);
        breadcrumb_=new wxStaticText(editorPage,wxID_ANY,T("الملف  /  C++"),wxDefaultPosition,wxDefaultSize,wxALIGN_RIGHT);breadcrumb_->SetFont(theme::Font(10));breadcrumb_->SetForegroundColour(Muted);editorLayout->Add(breadcrumb_,0,wxEXPAND|wxALL,FromDIP(12));
        editors_=new wxAuiNotebook(editorPage,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxAUI_NB_TOP|wxAUI_NB_TAB_MOVE|wxAUI_NB_CLOSE_ON_ACTIVE_TAB|wxAUI_NB_SCROLL_BUTTONS|wxBORDER_NONE);
        editors_->SetBackgroundColour(theme::Editor);editors_->SetArtProvider(new theme::TabArt);editors_->SetTabCtrlHeight(FromDIP(44));
        editorLayout->Add(editors_,1,wxEXPAND);editorPage->SetSizer(editorLayout);workspace_->AddPage(editorPage,T("المحرر"));

        auto* sidebar=new wxPanel(vertical_);sidebar->SetBackgroundColour(theme::Panel);auto* side=new wxBoxSizer(wxVERTICAL);
        auto* top=new wxBoxSizer(wxHORIZONTAL);top->Add(Button(sidebar,T("بحث"),ID_FIND),0);top->AddStretchSpacer();
        auto* title=new wxStaticText(sidebar,wxID_ANY,T("الملفات"));title->SetForegroundColour(theme::Text);title->SetFont(theme::Font(12,true));top->Add(title,0,wxALIGN_CENTER_VERTICAL);
        side->Add(top,0,wxEXPAND|wxLEFT|wxRIGHT|wxTOP,FromDIP(16));
        projectTitle_=new wxStaticText(sidebar,wxID_ANY,T("الملفات المفتوحة"),wxDefaultPosition,wxDefaultSize,wxALIGN_RIGHT);projectTitle_->SetForegroundColour(theme::Muted);projectTitle_->SetFont(theme::Font(10));side->Add(projectTitle_,0,wxEXPAND|wxALL,FromDIP(16));
        tree_=new wxTreeCtrl(sidebar,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxTR_HIDE_ROOT|wxTR_NO_LINES|wxTR_HAS_BUTTONS|wxTR_FULL_ROW_HIGHLIGHT|wxBORDER_NONE);
        tree_->SetBackgroundColour(theme::Panel);tree_->SetForegroundColour(theme::Text);tree_->SetFont(theme::Code(11));tree_->SetIndent(FromDIP(14));side->Add(tree_,1,wxEXPAND|wxLEFT|wxRIGHT,FromDIP(12));
        auto* tip=new wxPanel(sidebar);tip->SetBackgroundColour(theme::Raised);auto* tipLayout=new wxBoxSizer(wxVERTICAL);
        auto* tipTitle=new wxStaticText(tip,wxID_ANY,T("تشغيل البرنامج"),wxDefaultPosition,wxDefaultSize,wxALIGN_RIGHT);tipTitle->SetForegroundColour(theme::Accent);tipTitle->SetFont(theme::Font(11,true));
        auto* tipText=new wxStaticText(tip,wxID_ANY,T("F9 لتشغيل البرنامج في نافذة مستقلة.\nاكتب المدخلات في نافذة التشغيل."),wxDefaultPosition,wxDefaultSize,wxALIGN_RIGHT);tipText->SetForegroundColour(theme::Muted);tipText->SetFont(theme::Font(9));tipText->Wrap(FromDIP(210));
        tipLayout->Add(tipTitle,0,wxEXPAND|wxLEFT|wxRIGHT|wxTOP,FromDIP(12));tipLayout->Add(tipText,0,wxEXPAND|wxALL,FromDIP(12));tip->SetSizer(tipLayout);side->Add(tip,0,wxEXPAND|wxALL,FromDIP(12));
        fixButton_=Button(sidebar,T("تصحيح الأخطاء"),ID_FIX);fixButton_->SetToolTip(T("يفحص الملف ويعرض التصحيح قبل تطبيقه. Ctrl+Shift+F9"));
        side->Add(fixButton_,0,wxEXPAND|wxLEFT|wxRIGHT,FromDIP(12));
        side->Add(Button(sidebar,T("الأخطاء"),ID_PROBLEMS),0,wxEXPAND|wxLEFT|wxRIGHT,FromDIP(12));
        side->Add(Button(sidebar,T("تحميل أدوات التشغيل"),ID_SETUP),0,wxEXPAND|wxLEFT|wxRIGHT,FromDIP(12));
        side->Add(Button(sidebar,T("الإعدادات"),ID_SETTINGS),0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,FromDIP(12));sidebar->SetSizer(side);
        vertical_->SplitVertically(workspace_,sidebar,-FromDIP(265));

        bottom_=new theme::Book(horizontal_);
        auto* issues=new wxPanel(bottom_);issues->SetBackgroundColour(theme::Editor);auto* issueLayout=new wxBoxSizer(wxVERTICAL);
        problems_=new wxListCtrl(issues,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxLC_REPORT|wxLC_SINGLE_SEL|wxLC_NO_HEADER|wxBORDER_NONE);
        problems_->SetBackgroundColour(theme::Editor);problems_->SetForegroundColour(Ink);problems_->SetFont(theme::Font(11));
        problems_->AppendColumn(T("النوع"),wxLIST_FORMAT_RIGHT,FromDIP(92));problems_->AppendColumn(T("المشكلة"),wxLIST_FORMAT_RIGHT,FromDIP(540));problems_->AppendColumn(T("الملف"),wxLIST_FORMAT_LEFT,FromDIP(220));problems_->AppendColumn(T("السطر"),wxLIST_FORMAT_RIGHT,FromDIP(110));problems_->AppendColumn(T("العمود"),wxLIST_FORMAT_RIGHT,FromDIP(110));
        details_=new wxTextCtrl(issues,wxID_ANY,T("اضغط F9 لتشغيل البرنامج. تظهر أخطاء الترجمة هنا."),wxDefaultPosition,FromDIP(wxSize(-1,62)),wxTE_MULTILINE|wxTE_READONLY|wxBORDER_NONE|wxTE_RIGHT);
        details_->SetFont(theme::Font(10));details_->SetBackgroundColour(theme::Soft);details_->SetForegroundColour(Muted);
        issueLayout->Add(problems_,1,wxEXPAND|wxALL,FromDIP(12));issueLayout->Add(details_,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,FromDIP(12));issues->SetSizer(issueLayout);
        auto* consolePanel=new wxPanel(bottom_);consolePanel->SetBackgroundColour(theme::Editor);auto* consoleLayout=new wxBoxSizer(wxVERTICAL);
        console_=new wxTextCtrl(consolePanel,wxID_ANY,wxEmptyString,wxDefaultPosition,wxDefaultSize,wxTE_MULTILINE|wxTE_READONLY|wxTE_RICH2|wxHSCROLL|wxBORDER_NONE);
        console_->SetFont(theme::Code(12));console_->SetBackgroundColour(theme::Editor);console_->SetForegroundColour(Ink);console_->SetLayoutDirection(wxLayout_LeftToRight);
        auto* inputRow=new wxBoxSizer(wxHORIZONTAL);
        input_=new wxTextCtrl(consolePanel,wxID_ANY,wxEmptyString,wxDefaultPosition,FromDIP(wxSize(-1,38)),wxTE_PROCESS_ENTER|wxBORDER_NONE);
        input_->SetBackgroundColour(theme::Raised);input_->SetForegroundColour(Ink);input_->SetFont(theme::Code(11));input_->SetHint(T("اكتب مدخلات البرنامج هنا…"));input_->SetName(T("مدخلات البرنامج"));input_->SetLayoutDirection(wxLayout_LeftToRight);input_->SetMaxLength(2048);
        sendButton_=Button(consolePanel,T("إرسال"),wxID_APPLY,true);inputRow->Add(input_,1,wxRIGHT|wxALIGN_CENTER_VERTICAL,FromDIP(10));inputRow->Add(sendButton_,0,wxRIGHT,FromDIP(8));inputRow->Add(Button(consolePanel,T("إنهاء الإدخال"),ID_EOF),0);
        consoleLayout->Add(console_,1,wxEXPAND|wxALL,FromDIP(14));consoleLayout->Add(inputRow,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,FromDIP(14));consolePanel->SetSizer(consoleLayout);
        log_=new wxTextCtrl(bottom_,wxID_ANY,wxEmptyString,wxDefaultPosition,wxDefaultSize,wxTE_MULTILINE|wxTE_READONLY|wxHSCROLL|wxBORDER_NONE);log_->SetFont(theme::Code(10));log_->SetBackgroundColour(theme::Editor);log_->SetForegroundColour(Muted);log_->SetLayoutDirection(wxLayout_LeftToRight);
        bottom_->AddPage(issues,T("الأخطاء"));if(!smokeDirectory_.empty())bottom_->AddPage(consolePanel,T("اختبار داخلي"));else consolePanel->Hide();bottom_->AddPage(log_,T("سجل الترجمة"));
        horizontal_->Initialize(vertical_);bottom_->Hide();layout->Add(horizontal_,1,wxEXPAND|wxLEFT|wxRIGHT,FromDIP(12));
        auto* status=new wxPanel(root);status->SetBackgroundColour(theme::Base);auto* statusRow=new wxBoxSizer(wxHORIZONTAL);
        statusLabels_[0]=new wxStaticText(status,wxID_ANY,T("جاهز"));statusLabels_[1]=new wxStaticText(status,wxID_ANY,T("سطر 1  |  عمود 1"));statusLabels_[2]=new wxStaticText(status,wxID_ANY,"UTF-8  /  C++");
        for(auto* label:statusLabels_){label->SetFont(theme::Font(9));label->SetForegroundColour(theme::Muted);}
        statusRow->Add(statusLabels_[0],1,wxALIGN_CENTER_VERTICAL);statusRow->Add(statusLabels_[1],0,wxALIGN_CENTER_VERTICAL|wxRIGHT,FromDIP(24));statusRow->Add(statusLabels_[2],0,wxALIGN_CENTER_VERTICAL);status->SetSizer(statusRow);
        stateText_=statusLabels_[0];layout->Add(status,0,wxEXPAND|wxALL,FromDIP(12));root->SetSizer(layout);auto* frameSizer=new wxBoxSizer(wxVERTICAL);frameSizer->Add(root,1,wxEXPAND);SetSizer(frameSizer);
    }

    void ShowProblemsPanel(bool show=true) {
        if(show&&!horizontal_->IsSplit()){bottom_->Show();horizontal_->SplitHorizontally(vertical_,bottom_,-FromDIP(230));}
        else if(!show&&horizontal_->IsSplit()){horizontal_->Unsplit(bottom_);bottom_->Hide();}
    }
    void PopulateHome() {
        homeCards_=nullptr;if(home_->GetSizer())home_->GetSizer()->Clear(true);
        auto* layout=new wxBoxSizer(wxVERTICAL);layout->Add(new theme::Banner(home_),0,wxEXPAND|wxALL,FromDIP(26));
        auto* setupNote=new wxStaticText(home_,wxID_ANY,ToolsPresent()?T("C وC++ وPython وJavaScript وJava جاهزة."):T("بعض الأدوات ناقصة. اضغط «تحميل أدوات التشغيل» لتجهيز التطبيق."),wxDefaultPosition,wxDefaultSize,wxALIGN_RIGHT);setupNote->SetFont(theme::Font(11));setupNote->SetForegroundColour(Muted);setupNote->Wrap(FromDIP(750));layout->Add(setupNote,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,FromDIP(26));
        auto* label=new wxStaticText(home_,wxID_ANY,T("ابدأ العمل"),wxDefaultPosition,wxDefaultSize,wxALIGN_RIGHT);label->SetFont(theme::Font(15,true));label->SetForegroundColour(Ink);layout->Add(label,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,FromDIP(26));
        auto* cards=new wxGridSizer(home_->GetClientSize().x<FromDIP(860)?1:3,FromDIP(14),FromDIP(14));homeCards_=cards;
        cards->Add(new theme::Card(home_,ID_PROJECT,T("فتح مشروع"),T("افتح مشروع Satr Studio أو Code::Blocks"),"folder",theme::Purple),1,wxEXPAND);
        cards->Add(new theme::Card(home_,ID_NEW_C,T("ملف C جديد"),T("برنامج C جاهز للكتابة"),"code",wxColour("#2CA38C")),1,wxEXPAND);
        cards->Add(new theme::Card(home_,ID_NEW_CPP,T("ملف C++ جديد"),T("برنامج C++ جاهز للكتابة"),"plus",Blue),1,wxEXPAND);
        cards->Add(new theme::Card(home_,ID_NEW_PY,"Python",T("ملف Python جديد"),"code",wxColour("#BA850F")),1,wxEXPAND);
        cards->Add(new theme::Card(home_,ID_NEW_JS,"JavaScript",T("ملف JavaScript جديد"),"code",theme::Purple),1,wxEXPAND);
        cards->Add(new theme::Card(home_,ID_NEW_JAVA,"Java",T("ملف Java جديد"),"code",wxColour("#B06436")),1,wxEXPAND);
        cards->Add(new theme::Card(home_,ID_OPEN,T("فتح ملف"),".c  .cpp  .py  .js  .java","folder",Blue),1,wxEXPAND);
        layout->Add(cards,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,FromDIP(26));
        auto* recentLabel=new wxStaticText(home_,wxID_ANY,T("الملفات الأخيرة"),wxDefaultPosition,wxDefaultSize,wxALIGN_RIGHT);recentLabel->SetFont(theme::Font(13,true));recentLabel->SetForegroundColour(Ink);layout->Add(recentLabel,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,FromDIP(26));
        int count=0;for(const auto& path:recentFiles_){if(!wxFileExists(path)||count++>=4)continue;
            auto* row=new wxPanel(home_);row->SetBackgroundColour(*wxWHITE);auto* line=new wxBoxSizer(wxHORIZONTAL);
            auto* button=new theme::Button(row,wxID_ANY,wxFileName(path).GetFullName(),"file",false,240);button->Bind(wxEVT_BUTTON,[this,path](wxCommandEvent&){OpenFile(path);workspace_->SetSelection(1);SetBusy(building_||running_);});
            auto* location=new wxStaticText(row,wxID_ANY,wxFileName(path).GetPath(),wxDefaultPosition,wxDefaultSize,wxST_ELLIPSIZE_MIDDLE);location->SetFont(theme::Code(9));location->SetForegroundColour(Muted);
            location->SetMinSize(FromDIP(wxSize(1,-1)));
            line->Add(location,1,wxALIGN_CENTER_VERTICAL|wxLEFT|wxRIGHT,FromDIP(16));line->Add(button,0);row->SetSizer(line);layout->Add(row,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,FromDIP(26));
        }
        if(count==0){auto* empty=new wxStaticText(home_,wxID_ANY,T("لم تفتح ملفات بعد."),wxDefaultPosition,wxDefaultSize,wxALIGN_RIGHT);empty->SetForegroundColour(Muted);empty->SetFont(theme::Font(11));layout->Add(empty,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,FromDIP(26));}
        home_->SetSizer(layout);home_->FitInside();home_->Layout();
    }
    void ShowHome(){PopulateHome();workspace_->SetSelection(0);ShowProblemsPanel(false);SetTitle(T("Satr Studio — الرئيسية"));SetBusy(building_||running_);}
    void UpdateTitle(Document* document) {
        int page = editors_->GetPageIndex(document);
        if (page != wxNOT_FOUND) editors_->SetPageText(page,(document->GetModify() ? U("● ") : wxString()) + (document->path.empty() ? document->suggestedName : wxFileName(document->path).GetFullName()));
        if (document == Current()) {SetTitle(U("Satr Studio — ") + (document->path.empty() ? T("ملف جديد") : wxFileName(document->path).GetFullName()));breadcrumb_->SetLabel(document->path.empty()?T("الملف  /  ملف جديد"):document->path);}
    }
    Document* AddDocument(const wxString& path, const wxString& text) {
        auto* document = new Document(editors_); document->path = path;document->ConfigureLanguage();
        document->cppStandard=standard_=="c++23"?23:standard_=="c++20"?20:17;
        document->requestSemantic=[this](Document* doc){if(!advancedCompletion_||!doc->IsNative())return;pendingAssist_=doc;assistGeneration_=doc->completionGeneration;assistPosition_=doc->GetCurrentPos();assistDue_=GetTickCount64()+180;};
        document->SetText(text); document->EmptyUndoBuffer(); document->SetSavePoint();
        editors_->AddPage(document,path.empty() ? U("main.cpp") : wxFileName(path).GetFullName(),true);
        document->Bind(wxEVT_STC_SAVEPOINTLEFT,[this,document](wxStyledTextEvent&) { UpdateTitle(document); });
        document->Bind(wxEVT_STC_SAVEPOINTREACHED,[this,document](wxStyledTextEvent&) { UpdateTitle(document); });
        document->Bind(wxEVT_STC_MODIFIED,[this,document](wxStyledTextEvent& event) {
            if(event.GetModificationType() & (wxSTC_MOD_INSERTTEXT | wxSTC_MOD_DELETETEXT)) {
                if(building_ || !diagnostics_.empty()) { diagnosticsStale_=true; details_->SetValue(T("تغيّر الكود. أعد الترجمة لتحديث الأخطاء.")); document->ClearProblems(); }
            }
        });
        document->Bind(wxEVT_STC_UPDATEUI,[this,document](wxStyledTextEvent&) {
            if (Current() != document) return;
            SetStatusText(wxString::Format(T("سطر %d  |  عمود %d"),document->GetCurrentLine()+1,document->GetColumn(document->GetCurrentPos())+1),1);
            int pos = document->GetCurrentPos()-1, opposite = pos >= 0 ? document->BraceMatch(pos) : wxSTC_INVALID_POSITION;
            if (opposite >= 0) document->BraceHighlight(pos,opposite); else document->BraceHighlight(wxSTC_INVALID_POSITION,wxSTC_INVALID_POSITION);
        });
        document->Bind(wxEVT_KEY_DOWN,[document](wxKeyEvent& e) {
            if (e.ControlDown() && e.GetKeyCode() == WXK_SPACE) {
                document->CompleteWord(true);
            } else e.Skip();
        });
        if(!path.empty()){recentFiles_.erase(std::remove(recentFiles_.begin(),recentFiles_.end(),path),recentFiles_.end());recentFiles_.insert(recentFiles_.begin(),path);if(recentFiles_.size()>8)recentFiles_.resize(8);}
        workspace_->SetSelection(1);UpdateTitle(document); document->SetFocus(); UpdateTree();SetBusy(building_||running_); return document;
    }
    Document* OpenFile(const wxString& requested) {
        wxString path = Canonical(requested);
        for (size_t i=0;i<editors_->GetPageCount();++i) {
            auto* document = static_cast<Document*>(editors_->GetPage(i));
            if (SamePath(document->path,path)) { editors_->SetSelection(i);workspace_->SetSelection(1);SetBusy(building_||running_);return document; }
        }
        wxFile file(path);
        if (!file.IsOpened()) { Message(T("تعذر فتح الملف:\n")+path,wxOK|wxICON_ERROR); return nullptr; }
        if (file.Length() > 8*1024*1024) { Message(T("هذا الملف أكبر من 8 ميجابايت. افتح ملف كود أصغر للحفاظ على استجابة المحرر.")); return nullptr; }
        wxString text;
        if (!file.ReadAll(&text,wxConvUTF8)) { Message(T("تعذر قراءة الملف بترميز UTF-8. حوّله إلى UTF-8 ثم أعد فتحه."),wxOK|wxICON_ERROR); return nullptr; }
        return AddDocument(path,text);
    }
    bool Save(Document* document, bool saveAs = false) {
        if (!document) return false;
        wxString path = document->path;
        if (path.empty() || saveAs) {
            if(!smokeDirectory_.empty()){SmokeRecord("unexpected_save_dialog",false,Utf8(document->suggestedName));return false;}
            wxFileDialog dialog(this,T("حفظ الملف"),path.empty() ? wxString() : wxFileName(path).GetPath(),path.empty() ? document->suggestedName : wxFileName(path).GetFullName(),T("ملفات C++ (*.cpp)|*.cpp|ملفات C (*.c)|*.c|Python (*.py)|*.py|JavaScript (*.js;*.mjs;*.cjs)|*.js;*.mjs;*.cjs|Java (*.java)|*.java|ملفات الترويسة (*.h;*.hpp)|*.h;*.hpp|كل الملفات (*.*)|*.*"),wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
            dialog.SetFilterIndex(document->IsC()?1:document->IsPython()?2:document->IsJavaScript()?3:document->IsJava()?4:0);
            if (dialog.ShowModal()!=wxID_OK) return false;
            path=dialog.GetPath();
        }
        if (!WriteUtf8(path,document->GetText())) { Message(T("تعذر حفظ الملف. تحقق من صلاحية المجلد:\n")+path,wxOK|wxICON_ERROR); return false; }
        document->path=Canonical(path); document->ConfigureLanguage();document->SetSavePoint(); UpdateTitle(document); UpdateTree(); return true;
    }
    bool CanClose(Document* document) {
        if (!document || !document->GetModify()) return true;
        wxMessageDialog dialog(this,T("هل تريد حفظ التغييرات قبل إغلاق هذا الملف؟\n") + (document->path.empty() ? T("ملف جديد") : document->path),T("تغييرات غير محفوظة"),wxYES_NO|wxCANCEL|wxICON_QUESTION);
        dialog.SetYesNoCancelLabels(T("حفظ"),T("تجاهل التغييرات"),T("إلغاء"));
        int result=dialog.ShowModal(); return result==wxID_NO || (result==wxID_YES && Save(document));
    }
    void UpdateTree() {
        tree_->DeleteAllItems(); auto root=tree_->AddRoot("root"); std::set<wxString> paths;
        for (const auto& source:sources_) paths.insert(source);
        for (size_t i=0;i<editors_->GetPageCount();++i) { auto* doc=static_cast<Document*>(editors_->GetPage(i)); if (!doc->path.empty()) paths.insert(doc->path); }
        for (const auto& path:paths) tree_->AppendItem(root,wxFileName(path).GetFullName(),-1,-1,new FileData(path));
        projectTitle_->SetLabel(projectName_.empty() ? T("الملفات المفتوحة") : projectName_);
    }
    void ResetProject() { sources_.clear(); includeDirs_.clear(); definitions_.clear(); libraries_.clear(); projectDir_.clear(); projectFile_.clear(); projectName_.clear(); diagnostics_.clear(); diagnosticsStale_=false; ShowProblems(); details_->SetValue(T("ترجم المشروع لعرض الأخطاء والتحذيرات.")); log_->Clear(); }
    void LoadProject(const wxString& path) {
        if (building_ || running_) { Notify(T("أوقف العملية الحالية قبل تغيير المشروع.")); return; }
        std::vector<wxString> files, includes, defines, libs;
        wxString base=wxFileName(path).GetPath(), title=wxFileName(path).GetName(); bool unsupported=false;
        try {
            if (wxFileName(path).GetExt().Lower()=="cbp") {
                wxXmlDocument xml;
                if (!xml.Load(path) || !xml.GetRoot() || xml.GetRoot()->GetName()!="CodeBlocks_project_file") throw std::runtime_error("Invalid CBP");
                wxXmlNode* project=xml.GetRoot()->GetChildren(); while(project && project->GetName()!="Project") project=project->GetNext();
                if (!project) throw std::runtime_error("Missing project");
                for (auto* node=project->GetChildren();node;node=node->GetNext()) {
                    if(node->GetName()=="Option" && node->HasAttribute("title")) title=node->GetAttribute("title");
                    if(node->GetName()=="Unit") files.push_back(Canonical(node->GetAttribute("filename"),base));
                    if(node->GetName()=="Compiler") for(auto* add=node->GetChildren();add;add=add->GetNext()) {
                        if(add->HasAttribute("directory")) includes.push_back(Canonical(add->GetAttribute("directory"),base));
                        if(add->HasAttribute("option") && add->GetAttribute("option").StartsWith("-D")) defines.push_back(add->GetAttribute("option").Mid(2));
                    }
                    if(node->GetName()=="ExtraCommands" || node->GetName()=="MakeCommands") unsupported=true;
                }
                unsupported=true; // Target-specific options are deliberately not executed/imported.
            } else {
                auto data=json::parse(Utf8(ReadUtf8(path)));
                if(data.value("format","")!="satr-project-v1") throw std::runtime_error("Invalid Satr format");
                title=U(data.value("name",Utf8(title)));
                for(const auto& item:data.at("sources")) files.push_back(Canonical(U(item.get<std::string>()),base));
                for(const auto& item:data.value("includeDirectories",std::vector<std::string>{})) includes.push_back(Canonical(U(item),base));
                for(const auto& item:data.value("defines",std::vector<std::string>{})) defines.push_back(U(item));
                for(const auto& item:data.value("libraries",std::vector<std::string>{})) libs.push_back(U(item));
            }
            if(files.empty() || files.size()>200) throw std::runtime_error("Invalid source count");
            for(const auto& file:files) if(!wxFileExists(file)) throw std::runtime_error("Missing source file");
        } catch(...) { Message(T("تعذر قراءة المشروع. تحقق من وجود الملفات ومن صيغة مشروع Satr Studio أو Code::Blocks."),wxOK|wxICON_ERROR); return; }
        ResetProject(); sources_=std::move(files); includeDirs_=std::move(includes); definitions_=std::move(defines); libraries_=std::move(libs);
        projectDir_=base; projectFile_=path; projectName_=title; UpdateTree();
        for(const auto& file:sources_) if(SourceFile(file)) { OpenFile(file); break; }
        if(unsupported) Message(T("تم فتح ملفات مشروع Code::Blocks. هذه النسخة تستورد الملفات ومسارات الترويسات وتعريفات المشروع العامة فقط. إعدادات الأهداف والأوامر المخصصة والإضافات لا تُنفّذ. راجع إعدادات المترجم قبل الترجمة."));
        Notify(T("تم فتح المشروع: ")+title);
    }
    wxString AskText(const wxString& title,const wxString& label,const wxString& value=wxEmptyString) {
        wxDialog dialog(this,wxID_ANY,title); auto* layout=new wxBoxSizer(wxVERTICAL);
        auto* caption=new wxStaticText(&dialog,wxID_ANY,label,wxDefaultPosition,wxDefaultSize,wxALIGN_RIGHT);
        auto* entry=new wxTextCtrl(&dialog,wxID_ANY,value,wxDefaultPosition,FromDIP(wxSize(370,-1)),wxTE_PROCESS_ENTER);
        layout->Add(caption,0,wxEXPAND|wxALL,FromDIP(14)); layout->Add(entry,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,FromDIP(14));
        auto* buttons=new wxBoxSizer(wxHORIZONTAL); buttons->AddStretchSpacer(); buttons->Add(CancelButton(dialog),0,wxRIGHT,FromDIP(8)); auto* ok=new wxButton(&dialog,wxID_OK,T("متابعة")); buttons->Add(ok); ok->SetDefault(); layout->Add(buttons,0,wxEXPAND|wxALL,FromDIP(14));
        dialog.SetSizerAndFit(layout); dialog.CentreOnParent(); entry->SetFocus(); return dialog.ShowModal()==wxID_OK ? entry->GetValue() : wxString();
    }
    void NewProject() {
        if(building_||running_) { Notify(T("أوقف العملية الحالية قبل إنشاء مشروع.")); return; }
        wxDirDialog choose(this,T("اختر المجلد الذي سيحتوي مشروعك الجديد")); if(choose.ShowModal()!=wxID_OK) return;
        auto name=AskText(T("مشروع جديد"),T("اسم المشروع"),"MyProject"); if(name.empty()) return;
        if(name.find_first_of("\\/:*?\"<>|")!=wxString::npos || name=="." || name=="..") { Message(T("اكتب اسمًا صالحًا للمجلد دون رموز المسارات.")); return; }
        auto folder=choose.GetPath()+wxFILE_SEP_PATH+name;
        if(wxDirExists(folder) || !wxFileName::Mkdir(folder)) { Message(T("المجلد موجود بالفعل أو تعذر إنشاؤه. اختر اسمًا آخر.")); return; }
        auto main=folder+"/main.cpp"; if(!WriteUtf8(main,Starter())) { Message(T("تعذر إنشاء ملف المشروع."),wxOK|wxICON_ERROR); return; }
        json data={{"format","satr-project-v1"},{"name",Utf8(name)},{"sources",{"main.cpp"}}};
        auto project=folder+"/project.satr"; if(!WriteUtf8(project,U(data.dump(2)))) { Message(T("تعذر حفظ ملف المشروع."),wxOK|wxICON_ERROR); return; } LoadProject(project);
    }
    static wxString Starter() { return U("#include <iostream>\n\nint main()\n{\n    // اكتب الكود هنا ثم اضغط F9\n    std::cout << \"Hello from Satr Studio!\" << std::endl;\n    return 0;\n}\n"); }

    wxString ToolRoot()const{return UserDataDirectory()+"/tools";}
    wxString ToolPath(const char* kind,const wxString& bundled)const{
        auto it=toolPaths_.find(kind);if(it!=toolPaths_.end()&&it->is_string()){auto value=U(it->get<std::string>());if(wxFileExists(value))return value;}
        return BaseDirectory()+bundled;
    }
    wxString ClangdPath()const{
#ifdef _WIN32
        auto path=ToolPath("clangd","/toolchain/clangd-19.1.2/clangd_19.1.2/bin/clangd.exe");
        if(wxFileExists(path))return path;
        return wxFileName(compiler_).GetPath()+"/clangd.exe";
#else
        for(const auto* path:{"/usr/bin/clangd","/usr/bin/clangd-18","/usr/bin/clangd-19","/usr/bin/clangd-20"})if(wxFileExists(path))return path;
        return "/usr/bin/clangd";
#endif
    }
    wxString PythonPath()const{
#ifdef _WIN32
        return ToolPath("python","/toolchain/python-3.14.9/python.exe");
#else
        return "/usr/bin/python3";
#endif
    }
    wxString NodePath()const{
#ifdef _WIN32
        return ToolPath("node","/toolchain/node-v24.22.1-win-x64/node.exe");
#else
        auto bundled=BaseDirectory()+"/toolchain/node-v24.22.1-linux-x64/bin/node";
        return wxFileExists(bundled)?bundled:wxString("/usr/bin/node");
#endif
    }
    wxString JavacPath()const{
#ifdef _WIN32
        return ToolPath("javac","/toolchain/java-21/jdk-21.0.12.1+1/bin/javac.exe");
#else
        const wxString path="/usr/lib/jvm/java-21-openjdk-amd64/bin/javac";return wxFileExists(path)?path:wxString("/usr/bin/javac");
#endif
    }
    wxString JavaPath()const{
#ifdef _WIN32
        return ToolPath("java","/toolchain/java-21/jdk-21.0.12.1+1/bin/java.exe");
#else
        const wxString path="/usr/lib/jvm/java-21-openjdk-amd64/bin/java";return wxFileExists(path)?path:wxString("/usr/bin/java");
#endif
    }
    wxString AssistantRuntime()const{return ToolPath("runtime","/assistant/"+ProgramName("llama-completion"));}
    wxString AssistantModel()const{return ToolPath("model","/assistant/qwen2.5-coder-0.5b-instruct-q4_k_m.gguf");}
    bool VisualRuntimePresent()const{
#ifdef _WIN32
        wchar_t system[MAX_PATH];GetSystemDirectoryW(system,MAX_PATH);
        for(const auto* name:{L"vcruntime140.dll",L"vcruntime140_1.dll",L"msvcp140.dll"})if(!wxFileExists(wxString(system)+"/"+name))return false;
#endif
        return true;
    }
    bool ToolsPresent()const{return wxFileExists(compiler_)&&wxFileExists(CCompiler(compiler_))&&wxFileExists(ClangdPath())&&wxFileExists(AssistantRuntime())&&wxFileExists(AssistantModel())&&VisualRuntimePresent()&&wxFileExists(PythonPath())&&wxFileExists(NodePath())&&wxFileExists(JavacPath())&&wxFileExists(JavaPath());}
    void PrepareTools(){
        if(building_||running_||(repair_&&repair_->Busy())){Notify(T("أوقف العملية الحالية قبل تجهيز الأدوات."));return;}
#ifdef _WIN32
        auto script=BaseDirectory()+"/setup/SetupTools.ps1";
        if(!wxFileExists(script)){Message(T("ملفات التجهيز غير موجودة. فك ضغط حزمة Satr Studio كاملة ثم حاول مجددًا."),wxOK|wxICON_ERROR);return;}
        languageServer_.reset();pendingAssist_=nullptr;
        auto root=smokeDirectory_.empty()?ToolRoot():smokeDirectory_+"/tools-setup";
        ToolsSetupDialog dialog(this,script,BaseDirectory(),root,wxEmptyString,!smokeDirectory_.empty());
        if(dialog.ShowModal()==wxID_OK){
            toolPaths_=dialog.Paths();auto compiler=toolPaths_.value("compiler","");if(!compiler.empty()&&wxFileExists(U(compiler)))compiler_=U(compiler);
            SaveSettings();if(workspace_->GetSelection()==0)PopulateHome();Notify(T("كل الأدوات جاهزة. يمكنك الآن كتابة البرامج وتشغيلها."));
        }else Notify(T("لم يكتمل تجهيز الأدوات. يمكنك المحاولة مجددًا."));
#else
        if(ToolsPresent()){Notify(T("أدوات التشغيل جاهزة."));return;}
        std::string error;auto results=UserDataDirectory()+"/run";EnsureDirectory(results);
        if(!external_.Start((BaseDirectory()+"/SatrRunner").ToStdWstring(),L"/bin/bash",BaseDirectory().ToStdWstring(),(results+"/setup-result.txt").ToStdWstring(),error,false,{(BaseDirectory()+"/setup/SetupTools-Linux.sh").ToStdWstring()})){
            Message(T("تعذر فتح نافذة التثبيت. شغّل Install-Linux.sh من حزمة التطبيق.\n")+U(error),wxOK|wxICON_ERROR);return;
        }
        externalRun_=true;running_=true;stopping_=false;SetBusy(true);Notify(T("أكمل تثبيت الأدوات في النافذة الخارجية."));
#endif
    }
#include "settings_ui.inc"
    void LoadSettings() {
        auto local=UserDataDirectory(); EnsureDirectory(local); settingsPath_=local+"/settings.json";
        try { auto data=json::parse(Utf8(ReadUtf8(settingsPath_))); compiler_=U(data.value("compiler","")); standard_=U(data.value("standard","c++17"));advancedCompletion_=data.value("advancedCompletion",true);localRepair_=data.value("localRepair",true);optimized_=data.value("optimized",false);cacheEnabled_=data.value("buildCache",true);for(const auto& path:data.value("recentFiles",std::vector<std::string>{}))if(recentFiles_.size()<8)recentFiles_.push_back(U(path)); } catch(...) {}
        try{auto tools=json::parse(Utf8(ReadUtf8(ToolRoot()+"/ready.json")));if(tools.is_object()&&tools.value("schema",0)==1&&tools.value("ready",false))toolPaths_=tools;}catch(...){}
#ifdef _WIN32
        auto bundled=BaseDirectory()+"/toolchain/gcc-14.2.0/mingw64/bin/g++.exe";
        if(portableTest_){toolPaths_=json::object();compiler_=bundled;}
        else if(wxFileExists(bundled)&&(compiler_.empty()||compiler_.Contains("CodeBlocks")))compiler_=bundled;
        if(standard_!="c++17"&&standard_!="c++20"&&standard_!="c++23") standard_="c++17";
        if(!wxFileExists(compiler_)) {
            wxArrayString candidates; candidates.Add(ToolPath("compiler","/toolchain/gcc-14.2.0/mingw64/bin/g++.exe"));candidates.Add(BaseDirectory()+"/MinGW/bin/g++.exe"); candidates.Add("C:/Program Files/CodeBlocks/MinGW/bin/g++.exe"); candidates.Add("C:/Program Files (x86)/CodeBlocks/MinGW/bin/g++.exe");
            for(const auto& path:candidates) if(wxFileExists(path)) { compiler_=path; break; }
        }
#else
        if(!wxFileExists(compiler_)||portableTest_)compiler_="/usr/bin/g++";
        if(standard_!="c++17"&&standard_!="c++20"&&standard_!="c++23")standard_="c++17";
#endif
        if(!wxFileExists(compiler_)) Notify(T("اضغط «تحميل أدوات التشغيل» لتجهيز المترجم تلقائيًا."),true);
    }
    void SaveSettings() {std::vector<std::string> recent;for(const auto& path:recentFiles_)recent.push_back(Utf8(path));json data={{"compiler",Utf8(compiler_)},{"standard",Utf8(standard_)},{"recentFiles",recent},{"advancedCompletion",advancedCompletion_},{"localRepair",localRepair_},{"language",ui::language},{"optimized",optimized_},{"buildCache",cacheEnabled_}};WriteUtf8(settingsPath_,U(data.dump(2)));}
    void PollAssistance(){
        if(building_||running_||(repair_&&repair_->Busy()))return;
        if(languageServer_)languageServer_->Poll();
        auto* document=pendingAssist_.get();if(!advancedCompletion_||!document||GetTickCount64()<assistDue_)return;
        if(Current()!=document||workspace_->GetSelection()!=1||document->GetCurrentPos()!=assistPosition_||document->completionGeneration!=assistGeneration_){pendingAssist_=nullptr;return;}
        if(!languageServer_){
            auto executable=ClangdPath();
            languageServer_=std::make_unique<satr::LanguageServer>();std::string error;
            if(!wxFileExists(executable)||!languageServer_->Start(executable.ToStdWstring(),compiler_.ToStdWstring(),BaseDirectory().ToStdWstring(),error)){
                languageServer_->Stop();if(!building_&&!running_)Notify(T("اضغط «تحميل أدوات التشغيل» لتجهيز الإكمال المتقدم."));
            }
        }
        if(languageServer_->Failed()){pendingAssist_=nullptr;return;}if(!languageServer_->Ready()||languageServer_->Busy())return;
        auto source=Utf8(document->GetText());if(source.size()>256*1024){pendingAssist_=nullptr;return;}
        auto file=document->path;auto directory=projectDir_.empty()?(file.empty()?BaseDirectory():wxFileName(file).GetPath()):projectDir_;
        if(file.empty())file=directory+wxString::Format("/satr-unsaved-%d.",document->GetId())+(document->IsC()?"c":"cpp");
        auto driver=document->IsC()?CCompiler(compiler_):compiler_;
        std::vector<std::string> command={Utf8(driver),document->IsC()?"-std=c17":"-std="+Utf8(standard_),Utf8(file)};
        for(const auto& include:includeDirs_)command.push_back("-I"+Utf8(include));
        for(const auto& define:definitions_)command.push_back("-D"+Utf8(define));
        int position=document->GetCurrentPos(),line=document->LineFromPosition(position),column=position-document->PositionFromLine(line);unsigned generation=document->completionGeneration;
        wxWeakRef<Document> weak(document);pendingAssist_=nullptr;
        languageServer_->Complete(Utf8(file),source,command,Utf8(directory),line,column,[this,weak,source,position,generation](const satr::LspJson& result){
            auto* doc=weak.get();if(!doc||doc!=Current()||workspace_->GetSelection()!=1||doc->completionGeneration!=generation||doc->GetCurrentPos()!=position||Utf8(doc->GetText())!=source)return;
            doc->ShowSemanticCompletions(result);
        });
    }
    void SetBusy(bool value) {
        value=value||(repair_&&repair_->Busy());
        bool editing=workspace_->GetSelection()==1&&Current();fixButton_->Enable(!value&&editing&&Current()->IsNative());GetMenuBar()->Enable(ID_FIX,!value&&editing&&Current()->IsNative());runButton_->Enable(!value&&editing); buildButton_->Enable(!value&&editing); stopButton_->Enable(value||external_.HasWindow());
        if(editing)SetStatusText("UTF-8  /  "+Current()->LanguageName(),2);
        sendButton_->Enable(running_); input_->Enable(running_);
        GetMenuBar()->Enable(ID_RUN,!value); GetMenuBar()->Enable(ID_BUILD,!value); GetMenuBar()->Enable(ID_STOP,value||external_.HasWindow());
    }
    void AppendBounded(wxTextCtrl* control,const wxString& text) {
        if(control->GetLastPosition()>1024*1024) control->Remove(0,512*1024);
        control->AppendText(text);
    }
    void ShowProblems() {
        problems_->DeleteAllItems();
        for(size_t page=0;page<editors_->GetPageCount();++page) static_cast<Document*>(editors_->GetPage(page))->ClearProblems();
        int errors=0,warnings=0;
        for(size_t i=0;i<diagnostics_.size();++i) {
            const auto& d=diagnostics_[i]; bool warning=d.severity=="warning", note=d.severity=="note";
            if(warning) ++warnings; else if(!note) ++errors;
            long row=problems_->InsertItem(i,warning?T("تحذير"):note?T("ملاحظة"):T("خطأ"));
            problems_->SetItem(row,1,T(d.summary)); problems_->SetItem(row,2,wxFileName(U(d.file)).GetFullName());
            problems_->SetItem(row,3,d.line>0?wxString::Format(T("سطر %d"),d.line):U("—")); problems_->SetItem(row,4,d.column>0?wxString::Format(T("عمود %d"),d.column):U("—"));
            problems_->SetItemTextColour(row,warning?theme::Warning:note?Muted:Red);problems_->SetItemBackgroundColour(row,i%2?theme::Soft:theme::Editor);
            if(!diagnosticsStale_ && !note && d.line>0) for(size_t page=0;page<editors_->GetPageCount();++page) {
                auto* doc=static_cast<Document*>(editors_->GetPage(page)); if(!SamePath(Canonical(U(d.file),buildWorkingDirectory_),doc->path) || d.line>doc->GetLineCount()) continue;
                int line=d.line-1, start=doc->PositionFromLine(line), end=doc->GetLineEndPosition(line);
                int position=std::min(end,d.byteColumn>0?start+d.byteColumn-1:doc->FindColumn(line,std::max(0,d.column-1))); doc->SetIndicatorCurrent(warning?1:0); doc->IndicatorFillRange(position,std::max(1,std::min(12,end-position))); if(!warning) doc->MarkerAdd(line,1);
            }
        }
        bottom_->SetPageText(0,diagnostics_.empty()?T("الأخطاء"):wxString::Format(T("الأخطاء  (%d)"),static_cast<int>(diagnostics_.size())));
        if(diagnostics_.empty()) details_->SetValue(T("اكتملت الترجمة دون أخطاء أو تحذيرات."));
        else details_->SetValue(wxString::Format(T("%d خطأ، %d تحذير. اضغط على الخطأ للانتقال إلى مكانها. زر الفأرة الأيمن يعرض الرسالة الأصلية."),errors,warnings));
        if(diagnosticsStale_) details_->SetValue(T("تغيّر الكود. أعد الترجمة لتحديث الأخطاء."));
    }
    void JumpToProblem(long index) {
        if(index<0||static_cast<size_t>(index)>=diagnostics_.size()) return;
        const auto& d=diagnostics_[index]; details_->SetValue((diagnosticsStale_?T("تغيّر الكود؛ قد يكون موضع الخطأ مختلفًا.\n"):wxString())+T(d.summary)+"\n"+T(d.hint));
        if(d.file.empty()||d.line<1) return;
        wxString path=Canonical(U(d.file),buildWorkingDirectory_); if(!wxFileExists(path)) return;
        auto* document=OpenFile(path); if(!document) return;
        int line=std::min(d.line-1,document->GetLineCount()-1); int position=std::min(document->GetLineEndPosition(line),d.byteColumn>0?document->PositionFromLine(line)+d.byteColumn-1:document->FindColumn(line,std::max(0,d.column-1)));
        document->GotoPos(position); document->EnsureVisible(line); document->ScrollToLine(std::max(0,line-5)); document->SetFocus();
    }
    void BuildScript(Document* document,bool run){
        if((document->GetModify()||document->path.empty())&&!Save(document))return;
        auto runtime=document->IsPython()?PythonPath():NodePath();
        if(!wxFileExists(runtime)){PrepareTools();runtime=document->IsPython()?PythonPath():NodePath();if(!wxFileExists(runtime))return;}
        javaBuild_=false;scriptBuild_=true;scriptSource_=document->path;reusedUnits_=0;reusedLink_=false;binary_=runtime;buildWorkingDirectory_=wxFileName(document->path).GetPath();
        runArguments_.clear();
        if(document->IsPython())runArguments_={L"-u",L"-X",L"utf8",(BaseDirectory()+"/setup/run_python.py").ToStdWstring(),document->path.ToStdWstring()};
        else {
            runArguments_={document->path.ToStdWstring()};
            auto folder=wxFileName(document->path).GetPath();
            auto loader=BaseDirectory()+"/setup/node_cache.cjs";
            if(wxFileExists(loader)&&(wxFileName(document->path).GetSize()>65536||wxFileExists(folder+"/package.json")))runArguments_={L"--require",loader.ToStdWstring(),document->path.ToStdWstring()};
        }
        diagnostics_.clear();diagnosticsStale_=false;ShowProblems();log_->Clear();console_->Clear();runtimePending_.clear();
        if(run){RunProgram();return;}
        BuildStep step;step.executable=runtime;step.label=T("فحص الملف");
        if(document->IsPython())step.args={L"-X",L"utf8",L"-c",L"import sys,tokenize; p=sys.argv[1]; compile(tokenize.open(p).read(),p,'exec')",document->path.ToStdWstring()};
        else step.args={L"--check",document->path.ToStdWstring()};
        steps_.clear();steps_.push_back(std::move(step));buildOutput_.clear();runAfterBuild_=false;building_=true;stopping_=false;started_=std::chrono::steady_clock::now();SetBusy(true);StartNextStep();
    }
    void BuildJava(Document* document,bool run){
        for(size_t i=0;i<editors_->GetPageCount();++i){auto* doc=static_cast<Document*>(editors_->GetPage(i));if(doc->IsJava()&&(doc==document||!doc->path.empty())&&(doc->GetModify()||doc->path.empty())&&!Save(doc))return;}
        if(!wxFileExists(JavacPath())||!wxFileExists(JavaPath())){PrepareTools();if(!wxFileExists(JavacPath())||!wxFileExists(JavaPath()))return;}
        javaBuild_=true;scriptBuild_=false;scriptSource_=document->path;binary_=JavaPath();buildWorkingDirectory_=wxFileName(document->path).GetPath();
        steps_.clear();diagnostics_.clear();diagnosticsStale_=false;reusedUnits_=0;reusedLink_=false;buildOutput_.clear();log_->Clear();console_->Clear();runtimePending_.clear();ShowProblems();
        runAfterBuild_=run;building_=true;stopping_=false;started_=std::chrono::steady_clock::now();SetBusy(true);Notify(T("التحقق من الملفات"));
        auto source=satr::FsPath(document->path.ToStdWstring()),compiler=satr::FsPath(JavacPath().ToStdWstring()),runtime=satr::FsPath(JavaPath().ToStdWstring()),data=satr::FsPath(UserDataDirectory().ToStdWstring());
        std::string environment;for(const auto* name:{"JAVA_TOOL_OPTIONS","JDK_JAVA_OPTIONS","JDK_JAVAC_OPTIONS","_JAVA_OPTIONS"}){wxString value;wxGetEnv(name,&value);if(!value.empty())environment+=std::string(name)+"="+Utf8(value)+"\n";}
        try{javaPreparation_=std::async(std::launch::async,[source,compiler,runtime,data,cache=cacheEnabled_,environment,previous=std::move(javaCommit_)]()mutable{if(previous.valid())previous.get();return satr::PrepareJava(source,compiler,runtime,data,cache,environment);});}
        catch(const std::exception& error){buildOutput_=error.what();FinishBuild(1);}
    }
    void PollJavaBuild(){
        if(!javaPreparation_.valid()||javaPreparation_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
        try{
            javaPlan_=javaPreparation_.get();if(stopping_){FinishBuild(130);return;}
            runArguments_=javaPlan_.runArgs;
            if(javaPlan_.hit){reusedUnits_=1;reusedLink_=true;buildOutput_=javaPlan_.output;AppendBounded(log_,T("لم يتغير الكود؛ أُعيد استخدام الترجمة.\n")+U(buildOutput_));FinishBuild(0);return;}
            BuildStep step;step.kind=4;step.executable=JavacPath();step.args=javaPlan_.compileArgs;step.label=T("ترجمة: ")+wxFileName(scriptSource_).GetFullName();steps_.push_back(std::move(step));StartNextStep();
        }catch(const std::exception& error){buildOutput_=error.what();FinishBuild(1);}
    }
    void Build(bool run) {
        if(process_.Active()||building_||running_||(repair_&&repair_->Busy())) return;
        pendingAssist_=nullptr;
        workspace_->SetSelection(1);
        auto* document=Current(); if(!document) return;
        if(document->IsJava()){BuildJava(document,run);return;}
        if(!document->IsNative()){BuildScript(document,run);return;}
        buildCache_.SetStorage(cacheEnabled_?satr::FsPath((UserDataDirectory()+"/build-cache-v1").ToStdWstring()):fs::path());
        javaBuild_=false;scriptBuild_=false;runArguments_.clear();reusedUnits_=0;reusedLink_=false;
        if(!wxFileExists(compiler_)) { PrepareTools(); if(!wxFileExists(compiler_))return; }
        for(size_t i=0;i<editors_->GetPageCount();++i) { auto* doc=static_cast<Document*>(editors_->GetPage(i)); if((doc==document||!doc->path.empty())&&(doc->GetModify()||doc->path.empty())&&!Save(doc)) return; }
        std::vector<wxString> sources;
        if(projectFile_.empty()) { if(NativeSourceFile(document->path)) sources.push_back(document->path); }
        else for(const auto& file:sources_) if(NativeSourceFile(file)) sources.push_back(file);
        if(sources.empty()) { Message(T("اختر ملف C أو C++، أو افتح مشروعًا يحتوي على ملفات مصدر.")); return; }
        buildWorkingDirectory_=projectFile_.empty()?wxFileName(document->path).GetPath():projectDir_;
        wxString directory=UserDataDirectory()+"/build/"+wxString::Format("session-%lu",GetCurrentProcessId());
        if(!EnsureDirectory(directory)) { Message(T("تعذر إنشاء مجلد الترجمة المؤقت."),wxOK|wxICON_ERROR); return; }
        // GNU ld on Windows can reject Unicode output names. Use an ASCII
        // private staging directory; source paths stay Unicode and unmodified.
#ifdef _WIN32
        bool ascii=true; for(auto c:directory) if(c.GetValue()>127) ascii=false;
        if(!ascii) { std::wstring shortPath(32768,L'\0'); DWORD count=GetShortPathNameW(directory.wc_str(),shortPath.data(),static_cast<DWORD>(shortPath.size())); if(count>0&&count<shortPath.size()) directory=wxString(shortPath.data(),count); }
        for(auto c:directory) if(c.GetValue()>127) { Message(T("المترجم الحالي يحتاج مسار ترجمة بأحرف لاتينية. ضع نسخة Satr Studio في حساب ويندوز بمسار لاتيني أو فعّل أسماء المسارات القصيرة."),wxOK|wxICON_ERROR); return; }
#endif
        binary_=directory+"/"+ProgramName("program"); steps_.clear(); diagnostics_.clear(); diagnosticsStale_=false; buildOutput_.clear(); log_->Clear(); console_->Clear(); runtimePending_.clear(); ShowProblems(); details_->SetValue(T("جارٍ الترجمة؛ ستظهر النتائج عند الانتهاء."));
        std::vector<std::wstring> objects; bool hasCpp=false; auto bin=wxFileName(compiler_).GetPath();
        for(size_t i=0;i<sources.size();++i) {
            bool isC=wxFileName(sources[i]).GetExt().Lower()=="c"; hasCpp|=!isC;
            wxString object=directory+wxString::Format("/unit-%u.o",static_cast<unsigned>(i)); objects.push_back(object.ToStdWstring());
            BuildStep step; step.executable=isC?CCompiler(compiler_):compiler_; step.label=T("ترجمة: ")+wxFileName(sources[i]).GetFullName();
            step.args={L"-c",sources[i].ToStdWstring(),L"-o",object.ToStdWstring(),L"-Wall",L"-Wextra",L"-g0",optimized_?L"-O2":L"-O0",L"-fdiagnostics-format=json",L"-fdiagnostics-color=never",L"-fmessage-length=0",isC?L"-std=c17":L"-std="+standard_.ToStdWstring()};
            for(const auto& include:includeDirs_) step.args.push_back(L"-I"+include.ToStdWstring());
            for(const auto& define:definitions_) step.args.push_back(L"-D"+define.ToStdWstring());
            auto prepared=directory+wxString::Format("/unit-%u.",static_cast<unsigned>(i))+(isC?"i":"ii");
            step.kind=2;step.prepared=prepared;step.object=object;
            steps_.push_back(std::move(step));
        }
        BuildStep link; link.executable=hasCpp?compiler_:CCompiler(compiler_); link.label=T("ربط البرنامج"); link.args=objects; link.args.push_back(L"-o"); link.args.push_back(binary_.ToStdWstring());
#ifdef _WIN32
        // Copy/validate private DLLs while the compiler works, off the UI thread.
        if(nativeRuntimePreparation_.valid())nativeRuntimePreparation_.get();
        try{nativeRuntimePreparation_=std::async(std::launch::async,[this,compiler=compiler_,directory]{return nativeRuntime_.Prepare(satr::FsPath(compiler.ToStdWstring()),satr::FsPath(directory.ToStdWstring()));});}
        catch(...){link.args.push_back(L"-static");}
        // Generated programs are run artifacts, not debugger builds. Runtime
        // DLLs are private to this build; PATH and user projects stay untouched.
        link.args.push_back(L"-s");
#endif
        for(const auto& library:libraries_) link.args.push_back(L"-l"+library.ToStdWstring());
        link.kind=3;link.dependencies=directory+"/link.d";
        link.args.push_back(L"-Wl,--dependency-file="+link.dependencies.ToStdWstring());
        steps_.push_back(std::move(link));
        runAfterBuild_=run; building_=true; stopping_=false; started_=std::chrono::steady_clock::now(); SetBusy(true); bottom_->SetSelection(0); StartNextStep();
    }
    wxString WorkingDirectory() const { return buildWorkingDirectory_; }
    std::string StepKey(const BuildStep& step)const{
        std::string key=Utf8(step.executable)+"\n"+satr::FileStamp(satr::FsPath(step.executable.ToStdWstring()))+"\n"+Utf8(WorkingDirectory());
        for(const auto* name:{"PATH","LIBRARY_PATH","GCC_EXEC_PREFIX","COMPILER_PATH","CPATH","C_INCLUDE_PATH","CPLUS_INCLUDE_PATH","SOURCE_DATE_EPOCH"}){wxString value;wxGetEnv(name,&value);key+='\n';key+=name;key+='=';key+=Utf8(value);}
        for(size_t i=0;i<step.args.size();++i){key+='\n';key+=(step.kind==2&&i==3)?"<object>":satr::ToUtf8(step.args[i]);}return key;
    }
    void StartNextStep() {
        while(!steps_.empty()){
            if(steps_.front().kind==3&&nativeRuntimePreparation_.valid()){
                if(nativeRuntimePreparation_.wait_for(std::chrono::seconds(0))!=std::future_status::ready){waitingForNativeRuntime_=true;return;}
                waitingForNativeRuntime_=false;
                if(!nativeRuntimePreparation_.get())steps_.front().args.push_back(L"-static");
            }
            activeStep_=std::move(steps_.front());steps_.pop_front();activeKey_=StepKey(activeStep_);activePrepared_.clear();activeOutput_.clear();
            if(activeStep_.kind==2&&cacheEnabled_&&!activeStep_.restoreTried&&!buildCache_.HasMatchingUnit(activeStep_.object.ToStdWstring(),activeKey_)&&buildCache_.CanRestore(activeKey_)){
                activeStep_.restoreTried=true;auto object=activeStep_.object.ToStdWstring();auto key=activeKey_;
                Notify(T("التحقق من الملفات"));cacheRestore_=std::async(std::launch::async,[this,object,key]{return buildCache_.Restore(object,key);});return;
            }
            if(activeStep_.kind==2&&cacheEnabled_&&!activeStep_.preprocessed&&buildCache_.HasUnit(activeStep_.object.ToStdWstring())){
                auto preprocess=activeStep_;preprocess.kind=1;preprocess.args[0]=L"-E";preprocess.args[3]=preprocess.prepared.ToStdWstring();preprocess.args.push_back(L"-fpch-preprocess");
                activeStep_.preprocessed=true;steps_.push_front(activeStep_);steps_.push_front(std::move(preprocess));continue;
            }
            if(activeStep_.kind==2&&activeStep_.preprocessed){
                activePrepared_=satr::CacheRead(satr::FsPath(activeStep_.prepared.ToStdWstring()));std::string cached;
                if(buildCache_.UnitHit(activeStep_.object.ToStdWstring(),activeKey_,activePrepared_,cached)){
                    ++reusedUnits_;buildOutput_+=cached;AppendBounded(log_,T("لم يتغير الكود؛ أُعيد استخدام الترجمة.\n")+U(cached));continue;
                }
                buildCache_.Invalidate(activeStep_.object.ToStdWstring());
            }
            if(activeStep_.kind==3&&cacheEnabled_&&libraries_.empty()&&buildCache_.LinkHit(activeKey_)){reusedLink_=true;AppendBounded(log_,T("البرنامج جاهز من الترجمة السابقة.\n"));continue;}
            Notify(activeStep_.label);AppendBounded(log_,activeStep_.label+"\n");std::string error;
            auto args=activeStep_.args;
            if(activeStep_.kind==2){if(activeStep_.preprocessed)args[1]=activeStep_.prepared.ToStdWstring();else args.push_back(L"-save-temps=obj");}
            if(!process_.Start(activeStep_.executable.ToStdWstring(),args,WorkingDirectory().ToStdWstring(),error)){
                buildOutput_+="\n"+error;AppendBounded(log_,U(error)+"\n");FinishBuild(1);
            }
            return;
        }
        FinishBuild(0);
    }
    void CommitBuildStep(){
        if(activeStep_.kind==4){try{javaCommit_=std::async(std::launch::async,[plan=javaPlan_,output=buildOutput_]{return satr::CommitJava(plan,output);});}catch(...){} }

        if(activeStep_.kind==2&&cacheEnabled_){if(!activeStep_.preprocessed)activePrepared_=satr::CacheRead(satr::FsPath(activeStep_.prepared.ToStdWstring()));buildCache_.Commit(activeStep_.object.ToStdWstring(),activeKey_,std::move(activePrepared_),activeOutput_);}
        if(activeStep_.kind==3&&libraries_.empty())buildCache_.CommitLink(activeKey_,satr::FsPath(activeStep_.dependencies.ToStdWstring()),satr::FsPath(binary_.ToStdWstring()),satr::FsPath(WorkingDirectory().ToStdWstring()));
    }
    void FinishBuild(DWORD code) {
        waitingForNativeRuntime_=false;
        building_=false; steps_.clear(); diagnostics_=javaBuild_?satr::JavaDiagnostics(buildOutput_):satr::ParseDiagnostics(buildOutput_);
        if(scriptBuild_&&code!=0){
            satr::Diagnostic d;d.file=Utf8(scriptSource_);d.severity="error";d.summary="خطأ في صياغة الكود";d.original=buildOutput_;d.hint=buildOutput_;
            auto marker="File \""+d.file+"\", line ";auto pos=buildOutput_.rfind(marker);
            if(pos!=std::string::npos)pos+=marker.size();
            else {marker=d.file+":";pos=buildOutput_.find(marker);if(pos!=std::string::npos)pos+=marker.size();}
            if(pos!=std::string::npos){try{d.line=std::stoi(buildOutput_.substr(pos));}catch(...){}}
            diagnostics_={d};
        }
        ShowProblems();
        double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started_).count();
        if(stopping_) { Notify(T("تم إيقاف الترجمة")); details_->SetValue(T("أُوقفت الترجمة قبل اكتماله. أعد الترجمة للحصول على النتائج.")); SetBusy(false); return; }
        if(code!=0) {
            if(diagnostics_.empty()) { auto d=satr::ExplainDiagnostic("Compiler process failed","error"); d.summary="تعذر إكمال الترجمة"; d.hint="افتح سجل الترجمة لمعرفة التفاصيل وتحقق من إعدادات المترجم."; d.original=buildOutput_; diagnostics_.push_back(d); ShowProblems(); }
            Notify(wxString::Format(T("تعذرت الترجمة؛ راجع الأخطاء · %.2f ثانية"),seconds),true);ShowProblemsPanel();bottom_->SetSelection(0); SetBusy(false);
        } else {
            Notify(wxString::Format(T("اكتملت الترجمة · %.2f ثانية"),seconds)); AppendBounded(log_,wxString::Format(T("اكتملت الترجمة بنجاح في %.2f ثانية.\n"),seconds)); SetBusy(false);
            if(runAfterBuild_) RunProgram();
        }
    }
    void RunProgram() {
        if(smokeDirectory_.empty()||smokeStage_==7||benchmarkExternal_){
            std::string error;externalRun_=true;stopping_=false;
            auto results=UserDataDirectory()+"/run";EnsureDirectory(results);
            if(!external_.Start((BaseDirectory()+"/"+ProgramName("SatrRunner")).ToStdWstring(),binary_.ToStdWstring(),WorkingDirectory().ToStdWstring(),(results+wxString::Format("/result-%lu.txt",GetCurrentProcessId())).ToStdWstring(),error,false,runArguments_)){
                externalRun_=false;running_=false;SetBusy(false);Notify(T("تعذر فتح نافذة التشغيل الخارجية"),true);Message(T("تحقق من وجود SatrRunner.exe بجوار التطبيق.\n")+U(error),wxOK|wxICON_ERROR);return;
            }
            running_=true;SetBusy(true);Notify(T("البرنامج يعمل في نافذة خارجية — أدخل البيانات هناك"));return;
        }
        externalRun_=false;
        std::string error; running_=true; stopping_=false; console_->Clear(); runtimePending_.clear(); bottom_->SetSelection(1); SetBusy(true);
        if(!process_.Start(binary_.ToStdWstring(),runArguments_,WorkingDirectory().ToStdWstring(),error)) { running_=false; SetBusy(false); Notify(T("تعذر تشغيل البرنامج"),true); console_->AppendText(U(error)); return; }
        Notify(T("البرنامج يعمل — يمكنك إدخال البيانات أو إيقافه")); input_->SetFocus();
    }
    void SendInput() {
        if(!running_) return;
        std::string value=Utf8(input_->GetValue())+"\n";
        if(!process_.Send(value)) { Notify(T("تعذر إرسال المدخلات الآن. قد يكون البرنامج انتهى أو امتلأت قائمة الإدخال."),true); return; }
        AppendBounded(console_,input_->GetValue()+"\n"); input_->Clear();
    }
    void OnTick(wxTimerEvent&) {
        PollRepair();
        PollAssistance();
        if(externalRun_&&!running_&&!external_.Active()&&stopButton_->IsEnabled())SetBusy(building_);
        if(externalRun_)external_.Poll();
        if(externalRun_&&running_){if(external_.Finished()||!external_.Active()){
            running_=false;SetBusy(false);auto code=external_.ExitCode();Notify(stopping_?T("تم إيقاف البرنامج ونافذته"):code==0?T("انتهى البرنامج بنجاح — النتيجة في نافذة التشغيل"):wxString::Format(T("انتهى البرنامج برمز %lu — راجع نافذة التشغيل"),code),code!=0&&!stopping_);
        }}
        if(process_.Active()) {
            auto text=process_.Poll();
            if(building_) { if(activeOutput_.size()<4*1024*1024)activeOutput_+=text;if(buildOutput_.size()<4*1024*1024) buildOutput_+=text; if(!text.empty()) AppendBounded(log_,U(text)); }
            if(running_) {
                runtimePending_+=text;
                // Preserve UTF-8 code points split between pipe chunks.
                size_t cut=runtimePending_.size(), start=cut;
                while(start>0 && (static_cast<unsigned char>(runtimePending_[start-1])&0xC0)==0x80) --start;
                if(start>0) { unsigned char lead=runtimePending_[start-1]; size_t need=lead>=0xF0?4:lead>=0xE0?3:lead>=0xC0?2:1; if(cut-(start-1)<need) cut=start-1; }
                if(!process_.Active()) cut=runtimePending_.size();
                if(cut) { auto chunk=runtimePending_.substr(0,cut); auto decoded=U(chunk); if(decoded.empty()&&!chunk.empty()) decoded=wxString(chunk.c_str(),wxConvLocal,chunk.size()); AppendBounded(console_,decoded); runtimePending_.erase(0,cut); }
            }
            if(!process_.Active()) {
                auto code=process_.ExitCode();
                if(building_) { if(code==0&&!stopping_)CommitBuildStep();if(code==0&&!steps_.empty()&&!stopping_) StartNextStep(); else FinishBuild(code); }
                else if(running_) { running_=false; SetBusy(false); auto label=stopping_?T("تم إيقاف البرنامج"):code==0?T("انتهى البرنامج بنجاح"):wxString::Format(T("انتهى البرنامج برمز %lu (0x%08lX)"),code,code); AppendBounded(console_,"\n\n"+label+"\n"); Notify(label,code!=0&&!stopping_); }
            }
        }
        if(cacheRestore_.valid()&&cacheRestore_.wait_for(std::chrono::seconds(0))==std::future_status::ready){
            cacheRestore_.get();if(stopping_)FinishBuild(130);else{steps_.push_front(activeStep_);StartNextStep();}
        }
        if(waitingForNativeRuntime_&&nativeRuntimePreparation_.wait_for(std::chrono::seconds(0))==std::future_status::ready){
            if(stopping_)FinishBuild(130);else StartNextStep();
        }
        PollJavaBuild();
        SmokeTick();
    }

    bool ApplyRepair(Document* doc,const std::string& original,const std::string& corrected) {
        if(!doc||Utf8(doc->GetText())!=original)return false;
        int position=doc->GetCurrentPos();doc->AutoCompCancel();doc->CallTipCancel();
        doc->BeginUndoAction();doc->SetTargetStart(0);doc->SetTargetEnd(doc->GetTextLength());doc->ReplaceTarget(U(corrected));doc->GotoPos(std::min(position,doc->GetTextLength()));doc->EndUndoAction();
        doc->ClearProblems();UpdateTitle(doc);return true;
    }
    void PreviewRepair(Document* doc,const std::string& original,const std::string& corrected,bool ai) {
        wxDialog dialog(this,wxID_ANY,T("مراجعة التصحيح"),wxDefaultPosition,FromDIP(wxSize(1050,680)),wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER);
        dialog.SetBackgroundColour(theme::Soft);dialog.SetFont(theme::Font(10));auto* layout=new wxBoxSizer(wxVERTICAL);
        auto* note=new wxStaticText(&dialog,wxID_ANY,ai?T("اقتراح من المساعد المحلي. اجتاز فحص الترجمة؛ راجع التغيير للتأكد من أنه يحافظ على المطلوب."):T("تصحيح اقترحه المترجم واجتاز فحص الترجمة. راجع التغيير قبل تطبيقه."),wxDefaultPosition,wxDefaultSize,wxALIGN_RIGHT);
        note->Wrap(FromDIP(960));layout->Add(note,0,wxEXPAND|wxALL,FromDIP(16));
        auto* columns=new wxBoxSizer(wxHORIZONTAL);
        for(int i=0;i<2;++i){auto* panel=new wxPanel(&dialog);auto* column=new wxBoxSizer(wxVERTICAL);auto* label=new wxStaticText(panel,wxID_ANY,i?T("بعد التصحيح"):T("الكود الحالي"),wxDefaultPosition,wxDefaultSize,wxALIGN_RIGHT);column->Add(label,0,wxEXPAND|wxALL,FromDIP(8));auto* editor=new Document(panel);editor->SetText(U(i?corrected:original));editor->SetReadOnly(true);editor->requestSemantic=nullptr;column->Add(editor,1,wxEXPAND);panel->SetSizer(column);columns->Add(panel,1,wxEXPAND|wxALL,FromDIP(6));}
        layout->Add(columns,1,wxEXPAND|wxLEFT|wxRIGHT,FromDIP(10));
        auto* buttons=new wxBoxSizer(wxHORIZONTAL);buttons->Add(new wxStaticText(&dialog,wxID_ANY,T("يمكنك التراجع بـ Ctrl+Z. لن يُحفظ الملف تلقائيًا.")),1,wxALIGN_CENTER_VERTICAL);buttons->Add(CancelButton(dialog),0,wxRIGHT,FromDIP(8));buttons->Add(new wxButton(&dialog,wxID_OK,T("تطبيق التصحيح")));layout->Add(buttons,0,wxEXPAND|wxALL,FromDIP(16));dialog.SetSizer(layout);dialog.CentreOnParent();
        if(!smokeDirectory_.empty()){
            dialog.Layout();SmokeRecord("repair_preview_has_two_editors",columns->GetItemCount()==2);
            SmokeRecord("repair_preview_does_not_modify_source",Utf8(doc->GetText())==original);
            if(smokeStage_==10){
                SmokeRecord("compiler_repair_ready_without_ai",!ai);
                SmokeRecord("repair_apply_uses_single_undo",ApplyRepair(doc,original,corrected)&&doc->CanUndo());
                doc->Undo();SmokeRecord("repair_undo_restores_exact_source",Utf8(doc->GetText())==original);
                SmokeRecord("repair_rejects_stale_source",!ApplyRepair(doc,"outdated snapshot",corrected));
                doc->SetText("int main(){int count=3 return count;}\n");doc->EmptyUndoBuffer();smokeStage_=11;StartRepair();
                doc->AppendText("// user continued typing\n");PollRepair();SmokeRecord("typing_cancels_pending_repair",!repair_&&doc->GetText().Contains("user continued typing"));
                doc->SetText("#include <cstdio>\nint main() {\n    int value = 3\n    printf(\"%d\\n\", value);\n    return 0;\n}\n");doc->EmptyUndoBuffer();smokeStage_=12;StartRepair();
            }else if(smokeStage_==12){
                SmokeRecord("gui_real_local_ai_suggests_verified_repair",ai&&corrected.find("printf")!=std::string::npos);
                SmokeRecord("gui_ai_repair_can_be_applied",ApplyRepair(doc,original,corrected));
                doc->Undo();SmokeRecord("gui_ai_repair_is_reversible",Utf8(doc->GetText())==original);
                SmokeFinish();
            }
            return;
        }
        if(dialog.ShowModal()==wxID_OK){if(ApplyRepair(doc,original,corrected))Notify(T("تم تطبيق التصحيح. جرّب البرنامج بـ F9، أو تراجع بـ Ctrl+Z."));else Notify(T("تغيّر الملف؛ أعد طلب التصحيح."),true);}
        else Notify(T("لم يُطبّق التصحيح"));
    }
    void StartRepair(){
        auto* doc=Current();if(!doc||building_||running_||(repair_&&repair_->Busy()))return;
        if(!doc->IsNative()){Notify(T("تصحيح الأخطاء متاح لملفات C وC++. اضغط «ترجمة» لفحص هذا الملف."));return;}
        if(!wxFileExists(compiler_)||!VisualRuntimePresent()||!wxFileExists(AssistantRuntime())||!wxFileExists(AssistantModel())){PrepareTools();if(!ToolsPresent())return;}
        satr::RepairConfig config;config.c=doc->IsC();config.compiler=(config.c?CCompiler(compiler_):compiler_).ToStdWstring();config.standard=standard_.ToStdWstring();
        config.directory=(doc->path.empty()?(projectDir_.empty()?BaseDirectory():projectDir_):wxFileName(doc->path).GetPath()).ToStdWstring();
        config.scratch=(UserDataDirectory()+wxString::Format("/repair/session-%lu",GetCurrentProcessId())).ToStdWstring();
        config.runtime=AssistantRuntime().ToStdWstring();config.model=AssistantModel().ToStdWstring();config.allowAI=localRepair_;
        for(const auto& i:includeDirs_)config.includes.push_back(i.ToStdWstring());
        for(const auto& d:definitions_)config.defines.push_back(d.ToStdWstring());
        repairSource_=Utf8(doc->GetText());repairDocument_=doc;repairPath_=doc->path;pendingAssist_=nullptr;languageServer_.reset();doc->AutoCompCancel();doc->CallTipCancel();
        repair_=std::make_unique<satr::RepairTask>();repairState_=satr::RepairTask::State::Idle;repair_->Start(config,repairSource_);SetBusy(true);Notify(T("جارٍ فحص الأخطاء…"));
    }
    void PollRepair(){
        if(!repair_)return;
        auto* doc=repairDocument_.get();
        if(!doc||doc->path!=repairPath_||Utf8(doc->GetText())!=repairSource_){repair_->Cancel();repair_.reset();SetBusy(false);Notify(T("تغيّر الملف؛ أُوقف التصحيح. اضغط الزر للمحاولة مجددًا."));return;}
        repair_->Poll();auto state=repair_->Status();
        if(repair_->Busy()){
            if(state!=repairState_){Notify(state==satr::RepairTask::State::Thinking?T("المساعد يقترح تصحيحًا… يمكنك إيقافه من زر إيقاف."):state==satr::RepairTask::State::Verifying?T("جارٍ التحقق من التصحيح…"):T("جارٍ فحص الأخطاء…"));repairState_=state;}return;
        }
        auto corrected=repair_->Candidate(),error=repair_->Error();bool ai=repair_->UsedAI();repair_.reset();SetBusy(false);
        if(state==satr::RepairTask::State::Ready)PreviewRepair(doc,repairSource_,corrected,ai);
        else if(state==satr::RepairTask::State::Clean)Notify(T("لا توجد أخطاء ترجمة في هذا الملف. الأخطاء المنطقية تحتاج اختبار البرنامج."));
        else if(state!=satr::RepairTask::State::Cancelled){Notify(T(error),true);if(!smokeDirectory_.empty()&&smokeStage_>=10){SmokeRecord("repair_failed",false,error);SmokeFinish();}}
    }
    void Find() {
        auto* document=Current(); if(!document) return;
        auto text=AskText(T("بحث في الملف"),T("النص المطلوب"),findText_); if(text.empty()) return; findText_=text;
        document->SetSearchFlags(0); document->SetTargetStart(document->GetSelectionEnd()); document->SetTargetEnd(document->GetTextLength());
        int found=document->SearchInTarget(text);
        if(found<0) { document->SetTargetStart(0); document->SetTargetEnd(document->GetTextLength()); found=document->SearchInTarget(text); }
        if(found<0) { Notify(T("لم يتم العثور على النص.")); return; }
        document->SetSelection(document->GetTargetStart(),document->GetTargetEnd()); document->EnsureCaretVisible(); document->SetFocus();
    }
    void OpenExample(bool broken) {
        if(building_||running_) { Notify(T("أوقف العملية الحالية قبل فتح المثال.")); return; }
        ResetProject(); auto path=BaseDirectory()+(broken?"/examples/errors/main.cpp":"/examples/hello/main.cpp");
        // Installed examples may be read-only; edit a new document so Save
        // always asks the student where to keep their own copy.
        AddDocument(wxEmptyString,wxFileExists(path)?ReadUtf8(path):Starter());
        Notify(broken?T("اضغط F9 لتجربة شرح الخطأ بالعربية."):T("اضغط F9 لترجمة المثال وتشغيله."));
    }
    void Action(wxCommandEvent& e) {
        if(auto* text=dynamic_cast<wxTextCtrl*>(wxWindow::FindFocus())) {
            switch(e.GetId()) {
            case wxID_UNDO: if(text->CanUndo()) text->Undo(); return;
            case wxID_REDO: if(text->CanRedo()) text->Redo(); return;
            case wxID_CUT: if(text->IsEditable()) text->Cut(); return;
            case wxID_COPY: text->Copy(); return;
            case wxID_PASTE: if(text->IsEditable()) text->Paste(); return;
            case wxID_SELECTALL: text->SelectAll(); return;
            }
        }
        auto* document=Current();
        switch(e.GetId()) {
        case ID_HOME: ShowHome();break;
        case ID_PROBLEMS: ShowProblemsPanel(!horizontal_->IsSplit());break;
        case ID_NEW_C: case ID_NEW_CPP: case ID_NEW_PY: case ID_NEW_JS: case ID_NEW_JAVA: {if(building_||running_){Notify(T("أوقف العملية الحالية قبل تغيير المشروع."));break;}ResetProject();bool c=e.GetId()==ID_NEW_C;auto* doc=AddDocument(wxEmptyString,c?U("#include <stdio.h>\n\nint main(void)\n{\n    printf(\"Hello from Satr Studio!\\n\");\n    return 0;\n}\n"):Starter());doc->suggestedName=c?"main.c":"main.cpp";
            if(e.GetId()==ID_NEW_PY){doc->suggestedName="main.py";doc->SetText("print(\"Hello from Satr Studio!\")\n");}
            if(e.GetId()==ID_NEW_JS){doc->suggestedName="main.js";doc->SetText("console.log(\"Hello from Satr Studio!\");\n");}
            if(e.GetId()==ID_NEW_JAVA){doc->suggestedName="Main.java";doc->SetText("public class Main {\n    public static void main(String[] args) {\n        System.out.println(\"Hello from Satr Studio!\");\n    }\n}\n");}
            doc->ConfigureLanguage();doc->EmptyUndoBuffer();doc->SetSavePoint();UpdateTitle(doc);SetBusy(false);break;}
        case ID_MENU: {
            wxMenu popup;
            for(size_t i=0;i<menuModel_->GetMenuCount();++i){auto* menu=new wxMenu;for(auto* item:menuModel_->GetMenu(i)->GetMenuItems()){if(item->IsSeparator())menu->AppendSeparator();else{menu->Append(item->GetId(),item->GetItemLabel());menu->Enable(item->GetId(),item->IsEnabled());}}popup.AppendSubMenu(menu,menuModel_->GetMenuLabel(i));}
            PopupMenu(&popup);break;
        }
        case ID_NEW: {wxMenu menu;menu.Append(ID_NEW_C,"C");menu.Append(ID_NEW_CPP,"C++");menu.Append(ID_NEW_PY,"Python");menu.Append(ID_NEW_JS,"JavaScript");menu.Append(ID_NEW_JAVA,"Java");PopupMenu(&menu);break;}
        case ID_OPEN: { wxFileDialog dlg(this,T("فتح ملف كود"),wxEmptyString,wxEmptyString,T("ملفات الكود (*.c;*.cpp;*.cc;*.cxx;*.h;*.hpp;*.py;*.js;*.mjs;*.cjs;*.java)|*.c;*.cpp;*.cc;*.cxx;*.h;*.hpp;*.py;*.js;*.mjs;*.cjs;*.java|كل الملفات (*.*)|*.*"),wxFD_OPEN|wxFD_FILE_MUST_EXIST|wxFD_MULTIPLE); if(dlg.ShowModal()==wxID_OK) { wxArrayString paths; dlg.GetPaths(paths); for(const auto& path:paths) OpenFile(path); } break; }
        case ID_SAVE: Save(document); break;
        case ID_SAVE_AS: Save(document,true); break;
        case ID_CLOSE_TAB: if(document&&CanClose(document)) { editors_->DeletePage(editors_->GetSelection()); UpdateTree(); } break;
        case ID_FIX: StartRepair(); break;
        case ID_BUILD: Build(false); break;
        case ID_RUN: Build(true); break;
        case ID_STOP: if(repair_&&repair_->Busy()){repair_->Cancel();repair_.reset();SetBusy(false);Notify(T("تم إلغاء التصحيح"));break;}stopping_=true;if(process_.Active())process_.Stop();if(external_.Active())external_.Stop();Notify(T("جارٍ إيقاف العملية…"));SetBusy(building_||running_);break;
        case ID_SETUP: PrepareTools(); break;
        case ID_SETTINGS: Settings(); break;
        case ID_NEW_PROJECT: NewProject(); break;
        case ID_CLOSE_PROJECT: if(building_||running_) Notify(T("أوقف العملية الحالية قبل إغلاق المشروع.")); else { ResetProject(); UpdateTree(); Notify(T("تم إغلاق المشروع. ستُترجم ملفات هذا التبويب فقط.")); } break;
        case ID_PROJECT: { wxFileDialog dlg(this,T("فتح مشروع"),wxEmptyString,wxEmptyString,T("مشروع Satr Studio (*.satr)|*.satr|مشروع Code::Blocks (*.cbp)|*.cbp"),wxFD_OPEN|wxFD_FILE_MUST_EXIST); if(dlg.ShowModal()==wxID_OK) LoadProject(dlg.GetPath()); break; }
        case ID_FIND: Find(); break;
        case ID_NEXT_ERROR: { if(diagnostics_.empty()) break; long selected=problems_->GetNextItem(-1,wxLIST_NEXT_ALL,wxLIST_STATE_SELECTED); long next=(selected+1)%diagnostics_.size(); problems_->SetItemState(next,wxLIST_STATE_SELECTED,wxLIST_STATE_SELECTED); problems_->EnsureVisible(next); JumpToProblem(next); bottom_->SetSelection(0); break; }
        case ID_EXAMPLE: OpenExample(false); break;
        case ID_ERROR_EXAMPLE: OpenExample(true); break;
        case wxID_APPLY: SendInput(); break;
        case ID_EOF: if(running_) { process_.EndInput(); Notify(T("تم إنهاء إدخال البيانات للبرنامج.")); } break;
        case ID_ABOUT: Message(T("Satr Studio 0.9.0\nمحرر C وC++ وPython وJavaScript وJava بواجهة عربية.\n\nإكمال تلقائي، شرح الأخطاء، وتشغيل في نافذة مستقلة.\nزر تصحيح الأخطاء يفحص الملف ويعرض التعديل قبل تطبيقه.\nالمساعد يعمل محليًا عند الطلب، ولا يرسل الكود إلى الإنترنت.")); break;
        case wxID_UNDO: if(document) document->Undo(); break;
        case wxID_REDO: if(document) document->Redo(); break;
        case wxID_CUT: if(document) document->Cut(); break;
        case wxID_COPY: if(document) document->Copy(); break;
        case wxID_PASTE: if(document) document->Paste(); break;
        case wxID_SELECTALL: if(document) document->SelectAll(); break;
        case wxID_EXIT: Close(); break;
        default: e.Skip();
        }
    }
    void SmokeRecord(const std::string& name,bool okay,const std::string& detail="") { smokeResults_.push_back({{"name",name},{"passed",okay},{"detail",detail}}); WriteUtf8(smokeDirectory_+"/progress.json",U(smokeResults_.dump(2))); }
    void SmokeFinish() {
        if(benchmarkExternal_){external_.Stop();externalRun_=false;}
        json result={{"checks",smokeResults_},{"passed",true}}; for(const auto& item:smokeResults_) if(!item.at("passed").get<bool>()) result["passed"]=false;
        for(size_t i=0;i<editors_->GetPageCount();++i)static_cast<Document*>(editors_->GetPage(i))->SetSavePoint();
        WriteUtf8(smokeDirectory_+"/gui-smoke.json",U(result.dump(2))); smokeStage_=0; Close();
    }
    #include "language_smoke.inc"
    void SmokeTick() {
        if(LanguageSmoke){LanguageSmokeTick();return;}
        if(smokeStage_==0) return;
        if(std::chrono::steady_clock::now()-smokeStarted_>std::chrono::seconds(180)) { process_.Stop();external_.Stop(); SmokeRecord("timeout",false,Utf8(log_->GetValue())); SmokeFinish(); return; }
        if((smokeStage_==1||smokeStage_==5)&&!building_&&!running_) { SmokeRecord("build_start_failed",false,Utf8(log_->GetValue())+" compiler="+Utf8(compiler_)); SmokeFinish(); return; }
        if(smokeStage_==1 && running_) { SmokeRecord("interactive_stdin_accepted",process_.Send("21\n")); smokeStage_=2; return; }
        if(smokeStage_==2 && !running_&&!building_) {
            SmokeRecord("unicode_source_build_and_console_run",Utf8(console_->GetValue()).find("42")!=std::string::npos && process_.ExitCode()==0);
            SmokeRecord("working_directory_survives_tab_switch",console_->GetValue().Contains("CWD_OK"));
            auto file=smokeDirectory_+U("/ملفات تجربة/broken.cpp"); WriteUtf8(file,U("int main()\n{\n    int value = ;\n    return value;\n}\n")); OpenFile(file); smokeStage_=3; Build(false); return;
        }
        if(smokeStage_==3 && !building_&&!running_) {
            SmokeRecord("arabic_diagnostic_has_source_location",!diagnostics_.empty()&&diagnostics_[0].line==3&&!diagnostics_[0].summary.empty());
            JumpToProblem(0); SmokeRecord("click_error_navigates_to_line",Current()&&Current()->GetCurrentLine()==2);
            Current()->SetText("#include <iostream>\nint main(){std::cout << \"FIXED\";return 0;}\n");
            SmokeRecord("editing_sets_modified_state",Current()->GetModify()); Save(Current()); SmokeRecord("save_clears_modified_state",!Current()->GetModify()); smokeStage_=4; Build(true); return;
        }
        if(smokeStage_==4 && !building_&&!running_) {
            SmokeRecord("corrected_code_rebuilds_and_runs",diagnostics_.empty()&&console_->GetValue().Contains("FIXED")&&process_.ExitCode()==0);
            auto file=smokeDirectory_+"/infinite.cpp"; WriteUtf8(file,"int main(){for(;;){}}\n"); OpenFile(file); smokeStage_=5; Build(true); return;
        }
        if(smokeStage_==5 && running_) { stopping_=true; process_.Stop(); smokeStage_=6; return; }
        if(smokeStage_==6 && !running_&&!building_) {
            SmokeRecord("stop_running_program",process_.ExitCode()!=0&&!process_.Active());
#ifdef _WIN32
            auto file=smokeDirectory_+"/external.cpp";WriteUtf8(file,"#include <windows.h>\n#include <fstream>\n#include <cstdio>\nint main(){DWORD a=0,b=0;bool ok=GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE),&a)&&GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE),&b);std::ofstream(\"external-result.txt\")<<(ok?\"REAL_CONSOLE_OK\":\"FAIL\");printf(\"External console works!\\n\");return ok?7:9;}\n");OpenFile(file);smokeStage_=7;Build(true);return;
#else
            auto file=smokeDirectory_+"/external.cpp";WriteUtf8(file,"#include <unistd.h>\n#include <fstream>\n#include <cstdio>\nint main(){bool ok=isatty(0)&&isatty(1);std::ofstream(\"external-result.txt\")<<(ok?\"REAL_CONSOLE_OK\":\"FAIL\");printf(\"External console works!\\n\");return ok?7:9;}\n");OpenFile(file);smokeStage_=7;Build(true);return;
#endif
        }
        if(smokeStage_==7&&externalRun_&&(running_||external_.Finished())){smokeStage_=8;return;}
        if(smokeStage_==8&&!running_&&!building_){
            SmokeRecord("external_real_console_stdin_stdout",ReadUtf8(smokeDirectory_+"/external-result.txt")=="REAL_CONSOLE_OK");
            SmokeRecord("external_program_exit_code",external_.ExitCode()==7);
            SmokeRecord("external_window_remains_open_after_exit",external_.Active());external_.Stop();
            SmokeRecord("external_window_stop",!external_.Active());
            Current()->SetText("struct Planet {int orbitDays;void rotate(int speed);};\nint main(){Planet earth;earth.ro");Current()->GotoPos(Current()->GetTextLength());Current()->CompleteWord(true);smokeStage_=9;return;
        }
        if(smokeStage_==9&&Current()&&Current()->AutoCompActive()&&Current()->AutoCompGetCurrentText()=="rotate"){
            SmokeRecord("semantic_member_popup",true);Current()->AutoCompComplete();
            SmokeRecord("semantic_method_inserts_parentheses",Current()->GetText().EndsWith("earth.rotate()")&&Current()->GetCurrentPos()==Current()->GetTextLength()-1);
            SmokeRecord("semantic_method_signature",Current()->CallTipActive());Current()->CallTipCancel();
            Current()->SetText(U("// عربي\nint main(){return 0}\n"));Current()->EmptyUndoBuffer();smokeStage_=10;StartRepair();
            SmokeRecord("repair_button_disables_while_working",repair_&&repair_->Busy()&&!fixButton_->IsEnabled()&&stopButton_->IsEnabled());
        }
    }
public:
    void OpenFromCommandLine(const wxString& file){OpenFile(file);}
    explicit SatrFrame(const wxString& smoke=wxEmptyString,bool portable=false) : wxFrame(nullptr,wxID_ANY,U("Satr Studio — C / C++ / Python / JavaScript / Java"),wxDefaultPosition,wxSize(1280,850)), timer_(this,ID_TIMER), smokeDirectory_(smoke) {
        portableTest_=portable;
        auto screen=wxGetClientDisplayRect();auto wanted=FromDIP(wxSize(1400,900));auto minimum=FromDIP(wxSize(1000,650));
        SetMinSize(wxSize(std::min(minimum.x,screen.width-32),std::min(minimum.y,screen.height-32)));
        SetSize(wxSize(std::min(wanted.x,screen.width-32),std::min(wanted.y,screen.height-32)));MakeMenus();MakeLayout();LoadSettings();SetBusy(false);
#ifdef _WIN32
        if(HMODULE dwm=LoadLibraryExW(L"dwmapi.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32)){
            using SetAttribute=HRESULT(WINAPI*)(HWND,DWORD,LPCVOID,DWORD);auto set=reinterpret_cast<SetAttribute>(reinterpret_cast<void*>(GetProcAddress(dwm,"DwmSetWindowAttribute")));
            if(set){BOOL dark=TRUE;COLORREF caption=RGB(13,17,25),text=RGB(229,236,247);set(static_cast<HWND>(GetHandle()),20,&dark,sizeof(dark));set(static_cast<HWND>(GetHandle()),35,&caption,sizeof(caption));set(static_cast<HWND>(GetHandle()),36,&text,sizeof(text));}FreeLibrary(dwm);
        }
        wxIcon icon("IDI_ICON1",wxBITMAP_TYPE_ICO_RESOURCE); if(icon.IsOk()) SetIcon(icon);
#else
        wxIcon icon(BaseDirectory()+"/Satr.ico",wxBITMAP_TYPE_ICO);if(icon.IsOk())SetIcon(icon);
#endif
        Bind(wxEVT_MENU,&SatrFrame::Action,this); Bind(wxEVT_BUTTON,&SatrFrame::Action,this); Bind(wxEVT_TIMER,&SatrFrame::OnTick,this,ID_TIMER);
        input_->Bind(wxEVT_TEXT_ENTER,[this](wxCommandEvent&) { SendInput(); });
        tree_->Bind(wxEVT_TREE_ITEM_ACTIVATED,[this](wxTreeEvent& event) { auto* data=dynamic_cast<FileData*>(tree_->GetItemData(event.GetItem())); if(data) OpenFile(data->path); });
        problems_->Bind(wxEVT_LIST_ITEM_SELECTED,[this](wxListEvent& event) { JumpToProblem(event.GetIndex()); });
        problems_->Bind(wxEVT_LIST_ITEM_ACTIVATED,[this](wxListEvent& event) { JumpToProblem(event.GetIndex()); });
        problems_->Bind(wxEVT_CONTEXT_MENU,[this](wxContextMenuEvent&) { long row=problems_->GetNextItem(-1,wxLIST_NEXT_ALL,wxLIST_STATE_SELECTED); if(row<0||static_cast<size_t>(row)>=diagnostics_.size()) return; wxMenu menu; menu.Append(9001,T("عرض رسالة المترجم الأصلية")); menu.Bind(wxEVT_MENU,[this,row](wxCommandEvent&) { Message(U(diagnostics_[row].original)); }); PopupMenu(&menu); });
        editors_->Bind(wxEVT_AUINOTEBOOK_PAGE_CLOSE,[this](wxAuiNotebookEvent& event) { auto* doc=dynamic_cast<Document*>(editors_->GetPage(event.GetSelection())); if(!CanClose(doc)) event.Veto(); else CallAfter([this] { UpdateTree(); }); });
        editors_->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED,[this](wxAuiNotebookEvent& event) { if(Current()) UpdateTitle(Current());SetBusy(building_||running_); event.Skip(); });
        Bind(wxEVT_CLOSE_WINDOW,[this](wxCloseEvent& event) {
            if(process_.Active()||external_.HasWindow()) { wxMessageDialog dialog(this,T("هناك عملية أو نافذة تشغيل مفتوحة. هل تريد إيقافها وإغلاق Satr Studio؟"),T("إغلاق Satr Studio"),wxYES_NO|wxICON_QUESTION); dialog.SetYesNoLabels(T("إيقاف وإغلاق"),T("متابعة العمل")); if(dialog.ShowModal()!=wxID_YES) { event.Veto(); return; } }
            for(size_t i=0;i<editors_->GetPageCount();++i) if(!CanClose(static_cast<Document*>(editors_->GetPage(i)))) { event.Veto(); return; }
            if(repair_)repair_->Cancel();
            process_.Stop();external_.Stop();SaveSettings(); timer_.Stop(); event.Skip();
        });
        timer_.Start(16); Centre();
        if(smoke.empty()) {OpenExample(false);ShowHome();
#ifdef _WIN32
            CallAfter([this]{auto results=UserDataDirectory()+"/run";if(EnsureDirectory(results))external_.PrepareAsync((BaseDirectory()+"/SatrRunner.exe").ToStdWstring(),BaseDirectory().ToStdWstring(),(results+wxString::Format("/result-%lu.txt",GetCurrentProcessId())).ToStdWstring());});
#endif
        }
        else CallAfter([this] {
            if(LanguageSmoke){StartLanguageSmoke();return;}
            wxFileName::Mkdir(smokeDirectory_+U("/ملفات تجربة"),wxS_DIR_DEFAULT,wxPATH_MKDIR_FULL);
            if(portableTest_){
                SmokeRecord("portable_compiler_detected_without_codeblocks",compiler_.StartsWith(BaseDirectory()+"/toolchain"));
                SmokeRecord("portable_clangd_detected",ClangdPath().StartsWith(BaseDirectory()+"/toolchain"));
                wxCommandEvent setup(wxEVT_BUTTON,ID_SETUP);Action(setup);
                SmokeRecord("one_click_tools_setup_succeeds",toolPaths_.value("ready",false)&&ToolsPresent());
            }
            auto size=GetSize();ShowHome();Layout();home_->Layout();home_->FitInside();
            SmokeRecord("home_layout_fits_default_window",home_->GetSizer()->GetMinSize().x<=home_->GetClientSize().x,std::to_string(home_->GetSizer()->GetMinSize().x)+"/"+std::to_string(home_->GetClientSize().x));
            SetSize(GetMinSize());Layout();home_->Layout();home_->FitInside();
            SmokeRecord("home_layout_fits_small_window",home_->GetSizer()->GetMinSize().x<=home_->GetClientSize().x,std::to_string(home_->GetSizer()->GetMinSize().x)+"/"+std::to_string(home_->GetClientSize().x));SetSize(size);Layout();
            wxCommandEvent cTemplate(wxEVT_BUTTON,ID_NEW_C);Action(cTemplate);
            SmokeRecord("home_c_template_opens_editor",workspace_->GetSelection()==1&&Current()->suggestedName=="main.c"&&Current()->GetText().Contains("stdio.h"));editors_->DeletePage(editors_->GetSelection());
            auto file=smokeDirectory_+U("/ملفات تجربة/main.cpp"); WriteUtf8(file,"#include <iostream>\n#include <fstream>\nint main(){int n;std::cin>>n;std::cout<<n*2<<std::endl;std::ifstream f(\"relative.txt\");std::string s;f>>s;std::cout<<s;return 0;}\n");
            WriteUtf8(smokeDirectory_+U("/ملفات تجربة/relative.txt"),"CWD_OK");
            OpenFile(file); SmokeRecord("editor_ltr_utf8",Current()&&Current()->GetLayoutDirection()==wxLayout_LeftToRight&&Current()->GetCodePage()==wxSTC_CP_UTF8);
            auto* doc=Current();doc->SetText("print");doc->GotoPos(5);doc->EmptyUndoBuffer();doc->CompleteWord(true);
            SmokeRecord("autocomplete_popup_suggests_printf",doc->AutoCompActive()&&doc->AutoCompGetCurrentText()=="printf");
            doc->AutoCompComplete();SmokeRecord("accept_completion_inserts_printf_parentheses",doc->GetText()=="printf()"&&doc->GetCurrentPos()==7);
            SmokeRecord("function_signature_tooltip",doc->CallTipActive());doc->CallTipCancel();doc->Undo();SmokeRecord("completion_is_one_undo",doc->GetText()=="print");
            doc->SetText("print(42)");doc->GotoPos(5);doc->CompleteWord(true);doc->AutoCompComplete();SmokeRecord("completion_reuses_existing_arguments",doc->GetText()=="printf(42)"&&doc->GetCurrentPos()==7);doc->CallTipCancel();
            doc->SetText("pri_SUFFIX");doc->GotoPos(3);doc->CompleteWord(true);doc->AutoCompSelect("printf");doc->AutoCompComplete();SmokeRecord("completion_replaces_remaining_identifier",doc->GetText()=="printf()");doc->CallTipCancel();
            doc->SetText("#include <std");doc->GotoPos(doc->GetTextLength());doc->CompleteWord(true);doc->AutoCompSelect("stdio.h");doc->AutoCompComplete();SmokeRecord("include_completion_closes_delimiter",doc->GetText()=="#include <stdio.h>");
            doc->SetText("// print");doc->GotoPos(doc->GetTextLength());doc->CompleteWord(true);SmokeRecord("no_completion_in_comments",!doc->AutoCompActive());
            doc->SetText("printf");doc->GotoPos(6);doc->HandlePair('(');SmokeRecord("typed_opening_parenthesis_is_paired",doc->GetText()=="printf()"&&doc->GetCurrentPos()==7);doc->CallTipCancel();doc->HandlePair(')');SmokeRecord("closing_parenthesis_is_not_duplicated",doc->GetText()=="printf()"&&doc->GetCurrentPos()==8);
            doc->SetText("");doc->GotoPos(0);doc->HandlePair('"');SmokeRecord("quote_pair_places_caret_inside",doc->GetText()=="\"\""&&doc->GetCurrentPos()==1);doc->HandlePair('"');SmokeRecord("closing_quote_is_not_duplicated",doc->GetText()=="\"\""&&doc->GetCurrentPos()==2);
            doc->SetText("{}");doc->GotoPos(1);doc->HandlePair(WXK_RETURN);SmokeRecord("block_newline_indentation",doc->GetText()=="{\n    \n}"&&doc->GetCurrentPos()==6);
            doc->SetText(ReadUtf8(file));doc->EmptyUndoBuffer();doc->SetSavePoint();
            smokeStage_=1; Build(true);
            auto other=smokeDirectory_+"/other.cpp"; WriteUtf8(other,"// Different working directory\n"); OpenFile(other);
        });
    }
};

class SatrApp : public wxApp {
public:
    bool OnInit() override {
        wxInitAllImageHandlers();
        for(int i=1;i<argc;++i)if(wxString(argv[i])=="--smoke-test"||wxString(argv[i])=="--language-test"){delete wxLog::SetActiveTarget(new wxLogStderr());break;}
        SetAppName("Satr"); SetAppDisplayName(U("Satr Studio"));
        for(const auto& file:{"IBMPlexSansArabic-Regular.ttf","IBMPlexSansArabic-Medium.ttf","IBMPlexSansArabic-SemiBold.ttf"}){
            auto path=BaseDirectory()+"/fonts/"+file;
#ifdef _WIN32
            AddFontResourceExW(path.wc_str(),FR_PRIVATE,nullptr);
#else
            wxFont::AddPrivateFont(path);
#endif
        }
        wxString smoke;bool portable=false;
        for(int i=1;i<argc;++i)if(wxString(argv[i])=="--portable-test")portable=true;
        for(int i=1;i<argc;++i) if(wxString(argv[i])=="--smoke-test"&&i+1<argc) smoke=Canonical(wxString(argv[++i]));
        for(int i=1;i<argc;++i) if(wxString(argv[i])=="--language-test"&&i+1<argc){smoke=Canonical(wxString(argv[++i]));LanguageSmoke=true;}
        if(!smoke.empty())TestDataDirectory=smoke+"/profile";
        try{auto settings=json::parse(Utf8(ReadUtf8(UserDataDirectory()+"/settings.json")));ui::SetLanguage(settings.value("language","ar"));}catch(...){}
        auto* translations=new wxTranslations;
        wxTranslations::Set(translations); translations->SetLanguage(U(ui::language));
        wxFileTranslationsLoader::AddCatalogLookupPathPrefix(BaseDirectory()+"/locale");
        translations->AddCatalog("wxstd");
        auto* frame=new SatrFrame(smoke,portable); frame->Show();
        if(smoke.empty())for(int i=1;i<argc;++i){auto file=Canonical(wxString(argv[i]));if(SourceFile(file)&&wxFileExists(file))frame->OpenFromCommandLine(file);}
        return true;
    }
};
wxIMPLEMENT_APP(SatrApp);
