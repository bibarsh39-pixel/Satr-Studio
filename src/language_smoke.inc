    struct LanguageCase {std::string file,expected;bool cache=false;bool changeHeader=false;bool syntaxError=false;};
    bool benchmarkMode_=false;
    std::vector<LanguageCase> languageCases_={
        {"user_inventory.cpp","New Total Inventory Value: $9202.38"},{"user_inventory.cpp","New Total Inventory Value: $9202.38",true},
        {"c_complex.c","C_OK:399980000"},{"c_complex.c","C_OK:399980000",true},
        {"c_complex.c","C_OK:599970000",false,true},
        {"cpp_complex.cpp","CPP_OK:4999950000"},{"cpp_complex.cpp","CPP_OK:4999950000",true},
        {"python_complex.py","PYTHON_OK:499500"},{"python_complex.py","PYTHON_OK:499500"},
        {"javascript_complex.js","JS_OK:332833500"},{"javascript_complex.js","JS_OK:332833500"},
        {"javascript_module.mjs","ESM_OK"},
        {"broken.py","",false,false,true},{"broken.js","",false,false,true},
        {"optimization.c","QUICK"},{"optimization.c","OPTIMIZED"},
        {"c_complex.c","C_OK:599970000"},{"cpp_complex.cpp","CPP_OK:4999950000"},
        {"JavaSmall.java","JAVA_SMALL:42"},{"JavaSmall.java","JAVA_SMALL:42",true},
        {"JavaComplex.java","JAVA_COMPLEX:4999950000"},{"JavaComplex.java","JAVA_COMPLEX:4999950000",true},
        {"BrokenJava.java","",false,false,true}
    };
    size_t languageCase_=0;
    ULONGLONG caseStarted_=0,lastHeartbeat_=0,maxHeartbeatGap_=0;
    unsigned heartbeatCount_=0;
    void StartLanguageCase(){
        if(languageCase_>=languageCases_.size()){SmokeFinish();return;}
        auto item=languageCases_[languageCase_];
        if(!benchmarkMode_&&languageCase_==15){optimized_=true;SmokeRecord("optimized_mode_enabled",true);}
        if(item.changeHeader)WriteUtf8(smokeDirectory_+"/fixtures/values.h","#define MULTIPLIER 3\n");
        if(!benchmarkMode_&&languageCase_==9)WriteUtf8(smokeDirectory_+"/fixtures/package.json","{\"private\":true}\n");
        auto* doc=OpenFile(smokeDirectory_+"/fixtures/"+U(item.file));if(!doc){SmokeFinish();return;}
        SmokeRecord(std::string(item.file)+"_language",doc->LanguageName()==(doc->IsC()?"C":doc->IsPython()?"Python":doc->IsJavaScript()?"JavaScript":doc->IsJava()?"Java":"C++"));
        caseStarted_=lastHeartbeat_=GetTickCount64();maxHeartbeatGap_=0;heartbeatCount_=0;
        Build(!item.syntaxError);
    }
    void LanguageSmokeTick(){
        if(smokeStage_!=100)return;
        auto now=GetTickCount64();maxHeartbeatGap_=std::max(maxHeartbeatGap_,now-lastHeartbeat_);lastHeartbeat_=now;++heartbeatCount_;
        if(now-caseStarted_>120000){process_.Stop();SmokeRecord("language_timeout",false,Utf8(log_->GetValue()));SmokeFinish();return;}
        if(building_||running_)return;
        const auto& item=languageCases_[languageCase_];
        auto output=Utf8(console_->GetValue());
        auto code=benchmarkExternal_?external_.ExitCode():process_.ExitCode();
        SmokeRecord(std::string(item.file)+(item.syntaxError?"_rejects_invalid_syntax":"_run"),item.syntaxError?code!=0&&!diagnostics_.empty():code==0&&(benchmarkExternal_||output.find(item.expected)!=std::string::npos),item.syntaxError?buildOutput_:output);
        smokeResults_.back()["elapsedMs"]=now-caseStarted_;smokeResults_.back()["maxUiTickGapMs"]=maxHeartbeatGap_;smokeResults_.back()["uiTicks"]=heartbeatCount_;
        smokeResults_.back()["reusedUnits"]=reusedUnits_;smokeResults_.back()["reusedLink"]=reusedLink_;
        if(benchmarkMode_){
            smokeResults_.back()["externalConsole"]=benchmarkExternal_;
            smokeResults_.back()["outputChecked"]=!benchmarkExternal_;
            if(benchmarkExternal_){auto file=UserDataDirectory()+wxString::Format("/run/result-%lu.txt",GetCurrentProcessId());auto data=satr::ReadSessionFile(file.ToStdWstring());if(data.is_object())smokeResults_.back()["runner"]=data;}
        }
        if(item.cache)SmokeRecord(std::string(item.file)+"_safe_cache",reusedUnits_==1&&reusedLink_,Utf8(log_->GetValue()));
        if(item.changeHeader)SmokeRecord("changed_header_recompiled",reusedUnits_==0);
        if(!benchmarkMode_&&languageCase_==15)SmokeRecord("optimization_mode_changes_compiled_program",output.find("OPTIMIZED")!=std::string::npos&&output.find("QUICK")==std::string::npos);
        if(item.syntaxError){SmokeRecord(std::string(item.file)+"_error_location",diagnostics_.size()==1&&diagnostics_[0].line==1);JumpToProblem(0);SmokeRecord(std::string(item.file)+"_click_navigates",Current()->GetCurrentLine()==0);}
        ++languageCase_;StartLanguageCase();
    }
    void StartLanguageSmoke(){
        // Only the explicit --language-test path can load benchmark cases.
        auto manifest=smokeDirectory_+"/benchmark.json";
        if(wxFileExists(manifest)){
            try{
                auto config=json::parse(Utf8(ReadUtf8(manifest)));languageCases_.clear();
                for(const auto& item:config.at("cases")){auto file=item.at("file").get<std::string>();if(file.find("..")!=std::string::npos||file.find(':')!=std::string::npos)throw std::runtime_error("Invalid fixture");for(int i=0;i<config.value("runs",6);++i)languageCases_.push_back({file,item.at("expected").get<std::string>()});}
                benchmarkMode_=true;benchmarkExternal_=config.value("external",false);optimized_=config.value("optimized",false);smokeStage_=100;
#ifdef _WIN32
                if(benchmarkExternal_){smokeStage_=0;auto results=UserDataDirectory()+"/run";EnsureDirectory(results);external_.PrepareAsync((BaseDirectory()+"/SatrRunner.exe").ToStdWstring(),BaseDirectory().ToStdWstring(),(results+wxString::Format("/result-%lu.txt",GetCurrentProcessId())).ToStdWstring());TestLater([this]{smokeStage_=100;StartLanguageCase();});return;}
#endif
                StartLanguageCase();return;
            }catch(const std::exception& error){SmokeRecord("benchmark_configuration",false,error.what());SmokeFinish();return;}
        }
        optimized_=false;
        WriteUtf8(smokeDirectory_+"/fixtures/values.h","#define MULTIPLIER 2\n");
        WriteUtf8(smokeDirectory_+"/fixtures/optimization.c","#include <stdio.h>\nint main(void){\n#ifdef __OPTIMIZE__\nputs(\"OPTIMIZED\");\n#else\nputs(\"QUICK\");\n#endif\nreturn 0;}\n");
        WriteUtf8(smokeDirectory_+"/fixtures/broken.py","def broken(:\n    pass\n");
        WriteUtf8(smokeDirectory_+"/fixtures/broken.js","const value = ;\n");
        WriteUtf8(smokeDirectory_+"/fixtures/BrokenJava.java","public class BrokenJava { public static void main(String[] args) { System.out.println(42) } }\n");
        for(int id:{ID_NEW_PY,ID_NEW_JS,ID_NEW_JAVA}){
            wxCommandEvent event(wxEVT_BUTTON,id);Action(event);auto* doc=Current();bool python=id==ID_NEW_PY,java=id==ID_NEW_JAVA;std::string name=python?"python":java?"java":"javascript";
            SmokeRecord(name+"_template_and_lexer",python?doc->IsPython()&&doc->GetLexer()==wxSTC_LEX_PYTHON:java?doc->IsJava()&&doc->suggestedName=="Main.java"&&doc->GetText().Contains("public class Main"):doc->IsJavaScript()&&doc->GetText().Contains("console.log"));
            doc->SetText(python?"pri":java?"System.out.printl":"console.lo");doc->GotoPos(doc->GetTextLength());doc->CompleteWord(true);doc->AutoCompSelect(python?"print":java?"println":"log");doc->AutoCompComplete();
            SmokeRecord(name+"_completion_parentheses",doc->GetText()==(python?"print()":java?"System.out.println()":"console.log()")&&doc->GetCurrentPos()==doc->GetTextLength()-1);
            doc->SetSavePoint();editors_->DeletePage(editors_->GetSelection());
        }
        OpenExample(false); // A fresh normal session has an unnamed example tab.
        auto* before=Current();auto code=before->GetText();before->SetSelection(3,8);auto pages=editors_->GetPageCount();
        for(const std::string language:{"en","fr","ar"}){
            ApplyLanguage(language);SaveSettings();
            SmokeRecord("ui_language_"+language,runButton_->GetLabel()==(language=="fr"?U("Exécuter  F9"):language=="en"?U("Run  F9"):U("تشغيل  F9")));
            SmokeRecord("language_preserves_code_"+language,Current()==before&&before->GetText()==code&&before->GetSelectionStart()==3&&before->GetSelectionEnd()==8&&editors_->GetPageCount()==pages);
            auto saved=json::parse(Utf8(ReadUtf8(settingsPath_)));SmokeRecord("language_saved_"+language,saved.value("language","")==language);
            SmokeRecord("language_diagnostic_"+language,T(satr::ExplainDiagnostic("expected ';' before '}' token").summary)==(language=="fr"?U("Point-virgule attendu"):language=="en"?U("Expected semicolon"):U("فاصلة منقوطة متوقعة")));
            wxListItem column;column.SetMask(wxLIST_MASK_TEXT);problems_->GetColumn(2,column);SmokeRecord("column_language_"+language,column.GetText()==T("الملف"));
        }

        wxString screenshots;wxGetEnv("SATR_TEST_SCREENSHOTS",&screenshots);
        if(screenshots=="1"){VisualLanguageStage(0);return;}
        smokeStage_=100;StartLanguageCase();
    }
    void TestLater(std::function<void()> callback){
        struct Deferred:wxTimer{std::function<void()> callback;void Notify()override{auto next=std::move(callback);delete this;next();}};
        auto* timer=new Deferred;timer->callback=std::move(callback);timer->StartOnce(350);
    }
    void VisualLanguageStage(int index){
        if(index==3){ApplyLanguage("ar");smokeStage_=100;StartLanguageCase();return;}
        const std::string language=index==0?"en":index==1?"fr":"ar";ApplyLanguage(language);
        TestLater([this,language,index]{
            CaptureTestWindow(this,"editor-"+language);
            TestLater([this,language]{for(auto* window:wxTopLevelWindows)if(auto* dialog=dynamic_cast<wxDialog*>(window)){CaptureTestWindow(dialog,"settings-"+language);dialog->EndModal(wxID_CANCEL);}});
            Settings();VisualLanguageStage(index+1);
        });
    }
    void CaptureTestWindow(wxWindow* window,const std::string& name){
        window->Update();
        auto size=window->GetSize();wxBitmap bitmap(size.x,size.y);wxMemoryDC memory(bitmap);
#ifdef _WIN32
        PrintWindow(static_cast<HWND>(window->GetHandle()),static_cast<HDC>(memory.GetHDC()),2);
#else
        wxWindowDC source(window);memory.Blit(0,0,size.x,size.y,&source,0,0);
#endif
        memory.SelectObject(wxNullBitmap);bitmap.SaveFile(smokeDirectory_+"/"+U(name)+".png",wxBITMAP_TYPE_PNG);
    }
