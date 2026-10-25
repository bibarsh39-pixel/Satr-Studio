    void ApplyLanguage(const std::string& code) {
        const auto previous=ui::language;ui::SetLanguage(code);
        auto* translations=new wxTranslations;wxTranslations::Set(translations);translations->SetLanguage(U(ui::language));translations->AddCatalog("wxstd");
        Freeze();
        std::function<void(wxWindow*)> relabel=[&](wxWindow* window){
            if(dynamic_cast<Document*>(window)||dynamic_cast<wxTextCtrl*>(window)||dynamic_cast<wxTreeCtrl*>(window))return;
            if(auto* label=dynamic_cast<wxStaticText*>(window)){
                label->SetLabel(ui::Relabel(label->GetLabel(),previous));
                auto style=label->GetWindowStyleFlag()&~(wxALIGN_LEFT|wxALIGN_RIGHT|wxALIGN_CENTRE_HORIZONTAL);
                label->SetWindowStyleFlag(style|(ui::Arabic()?wxALIGN_RIGHT:wxALIGN_LEFT));
            }else if(auto* button=dynamic_cast<theme::Button*>(window)){
                button->SetLabel(ui::Relabel(button->GetLabel(),previous));button->Refresh();
            }
            if(auto* tip=window->GetToolTip())window->SetToolTip(ui::Relabel(tip->GetTip(),previous));
            for(auto* child:window->GetChildren())relabel(child);
            window->Layout();
        };
        relabel(this);MakeMenus();PopulateHome();UpdateTree();
        const char* columns[]={"النوع","المشكلة","الملف","السطر","العمود"};
        for(int i=0;i<5;++i){wxListItem column;problems_->GetColumn(i,column);column.SetText(T(columns[i]));column.SetAlign(ui::Arabic()?wxLIST_FORMAT_RIGHT:wxLIST_FORMAT_LEFT);problems_->SetColumn(i,column);}
        input_->SetHint(T("اكتب مدخلات البرنامج هنا…"));
        bottom_->SetPageText(smokeDirectory_.empty()?1:2,T("سجل الترجمة"));ShowProblems();
        if(Current()){UpdateTitle(Current());SetStatusText(wxString::Format(T("سطر %d  |  عمود %d"),Current()->GetCurrentLine()+1,Current()->GetColumn(Current()->GetCurrentPos())+1),1);}
        SetBusy(building_||running_);Notify(T("تم حفظ الإعدادات"));Layout();Refresh();Thaw();
    }
    void Settings() {
        if(building_||running_||(repair_&&repair_->Busy())){Notify(T("أوقف العملية الحالية قبل تغيير الإعدادات."));return;}
        wxDialog dialog(this,wxID_ANY,T("إعدادات Satr Studio"),wxDefaultPosition,FromDIP(wxSize(690,680)),wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER);
        dialog.SetBackgroundColour(theme::Soft);dialog.SetFont(theme::Font());
        auto* outer=new wxBoxSizer(wxVERTICAL);
        auto* page=new wxScrolledWindow(&dialog,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxVSCROLL);page->SetScrollRate(0,12);page->SetBackgroundColour(theme::Soft);
        auto* body=new wxBoxSizer(wxVERTICAL);page->SetSizer(body);
        auto heading=[&](const wxString& text){auto* label=new wxStaticText(page,wxID_ANY,text);label->SetFont(theme::Font(12,true));label->SetForegroundColour(theme::Ink);body->Add(label,0,wxEXPAND|wxTOP|wxBOTTOM,FromDIP(10));};
        std::vector<std::pair<wxStaticText*,wxString>> notes;
        auto note=[&](const wxString& text){auto* label=new wxStaticText(page,wxID_ANY,text);label->SetForegroundColour(theme::Hint);label->SetFont(theme::Font(10));label->Wrap(FromDIP(610));notes.push_back({label,text});body->Add(label,0,wxEXPAND|wxTOP|wxBOTTOM,FromDIP(7));};
        heading(T("اللغة"));auto* language=new wxChoice(page,wxID_ANY);
        language->Append(U("العربية"));language->Append("English");language->Append(U("Français"));language->SetSelection(ui::language=="fr"?2:ui::language=="en"?1:0);language->SetName(T("اللغة"));
        body->Add(language,0,wxEXPAND);note(T("تتغير لغة الواجهة فور الحفظ. يبقى الكود كما كتبته."));
        heading(T("الأداء"));auto* performance=new wxChoice(page,wxID_ANY);performance->Append(T("بدء سريع"));performance->Append(T("أداء أعلى"));performance->SetSelection(optimized_?1:0);performance->SetName(T("الأداء"));body->Add(performance,0,wxEXPAND);
        note(T("بدء سريع مناسب للتمارين. أداء أعلى يسرّع الحسابات الطويلة، وقد يزيد وقت الترجمة الأولى."));
        auto* cache=new wxCheckBox(page,wxID_ANY,T("استخدام ذاكرة الترجمة"));cache->SetValue(cacheEnabled_);body->Add(cache,0,wxTOP|wxBOTTOM,FromDIP(7));note(T("فحص الملفات والترويسات قبل إعادة استخدام الترجمة."));
        heading(T("المترجم والإكمال"));
        body->Add(new wxStaticText(page,wxID_ANY,T("مترجم C++ — اختر ملف g++")),0,wxBOTTOM,FromDIP(6));
        auto* field=new wxTextCtrl(page,wxID_ANY,compiler_);field->SetLayoutDirection(wxLayout_LeftToRight);field->SetName(T("مترجم C++ — اختر ملف g++"));
        auto* row=new wxBoxSizer(wxHORIZONTAL);row->Add(field,1,wxALIGN_CENTER_VERTICAL|wxRIGHT,FromDIP(8));auto* browse=new wxButton(page,wxID_OPEN,T("اختيار…"));row->Add(browse);body->Add(row,0,wxEXPAND|wxBOTTOM,FromDIP(12));
        body->Add(new wxStaticText(page,wxID_ANY,T("معيار لغة C++")),0,wxBOTTOM,FromDIP(6));auto* standard=new wxChoice(page,wxID_ANY);standard->Append("C++17");standard->Append("C++20");standard->Append("C++23");standard->SetSelection(standard_=="c++20"?1:standard_=="c++23"?2:0);body->Add(standard,0,wxEXPAND|wxBOTTOM,FromDIP(10));
        auto* advanced=new wxCheckBox(page,wxID_ANY,T("إكمال المتغيرات والدوال من المكتبات"));advanced->SetValue(advancedCompletion_);body->Add(advanced,0,wxTOP|wxBOTTOM,FromDIP(7));
        auto* localRepair=new wxCheckBox(page,wxID_ANY,T("استخدام الذكاء الاصطناعي المحلي عند طلب تصحيح الأخطاء"));localRepair->SetValue(localRepair_);body->Add(localRepair,0,wxTOP|wxBOTTOM,FromDIP(7));
        heading(T("الخصوصية"));note(T("لا يرسل التطبيق الكود إلى الإنترنت. تنزيل الأدوات يبدأ عند طلبك فقط. البرامج التي تشغّلها تملك صلاحيات حسابك."));
        outer->Add(page,1,wxEXPAND|wxALL,FromDIP(22));
        auto* buttons=new wxBoxSizer(wxHORIZONTAL);buttons->AddStretchSpacer();buttons->Add(CancelButton(dialog),0,wxRIGHT,FromDIP(8));auto* save=new wxButton(&dialog,wxID_OK,T("حفظ الإعدادات"));save->SetDefault();buttons->Add(save);outer->Add(buttons,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,FromDIP(22));
        browse->Bind(wxEVT_BUTTON,[&](wxCommandEvent&){wxFileDialog choose(&dialog,T("اختيار مترجم C++"),wxEmptyString,ProgramName("g++"),"C++ compiler|*",wxFD_OPEN|wxFD_FILE_MUST_EXIST);if(choose.ShowModal()==wxID_OK)field->SetValue(choose.GetPath());});
        dialog.SetSizer(outer);dialog.SetMinSize(FromDIP(wxSize(570,400)));dialog.CentreOnParent();page->FitInside();
        page->Bind(wxEVT_SIZE,[&](wxSizeEvent& event){for(auto& note:notes){note.first->SetLabel(note.second);note.first->Wrap(std::max(100,page->GetClientSize().x-FromDIP(14)));}page->Layout();page->FitInside();event.Skip();});
        if(dialog.ShowModal()!=wxID_OK)return;
        // Language/preferences remain editable on a machine whose compiler is not installed yet.
        if(field->GetValue()!=compiler_&&!wxFileExists(field->GetValue())){Message(T("المترجم غير موجود في هذا المسار. لم يتم تغيير الإعدادات."),wxOK|wxICON_ERROR);return;}
        compiler_=field->GetValue().empty()?wxString():Canonical(field->GetValue());standard_=standard->GetSelection()==1?"c++20":standard->GetSelection()==2?"c++23":"c++17";
        advancedCompletion_=advanced->GetValue();localRepair_=localRepair->GetValue();optimized_=performance->GetSelection()==1;cacheEnabled_=cache->GetValue();
        pendingAssist_=nullptr;languageServer_.reset();
        for(size_t i=0;i<editors_->GetPageCount();++i){auto* doc=static_cast<Document*>(editors_->GetPage(i));doc->cppStandard=standard_=="c++23"?23:standard_=="c++20"?20:17;++doc->completionGeneration;}
        ApplyLanguage(language->GetSelection()==2?"fr":language->GetSelection()==1?"en":"ar");SaveSettings();
    }
