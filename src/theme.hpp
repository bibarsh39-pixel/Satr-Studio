#pragma once
#include <wx/wx.h>
#include "ui_i18n.hpp"
#include <wx/graphics.h>
#include <wx/dcbuffer.h>
#include <wx/aui/aui.h>
#include <wx/access.h>
#include <functional>
#include <cmath>
#include <memory>
#include <vector>

namespace theme {
inline const wxColour Base("#142239"), Editor("#FFFFFF"), Panel("#1C2E48"), Raised("#2B4160"), Border("#DAE2ED");
inline const wxColour Text("#EBF1FA"), Muted("#AEBED4"), Accent("#80E2D2"), Blue("#2D6FC8"), Error("#BC3652"), Warning("#906012");
inline const wxColour Ink("#23334D"), Soft("#F2F5FA"), Hint("#5F718C"), Lilac("#EFEBFF"), Purple("#6E55BD");
inline wxFont Font(int size=11,bool bold=false) { return wxFont(size,wxFONTFAMILY_SWISS,wxFONTSTYLE_NORMAL,bold?wxFONTWEIGHT_SEMIBOLD:wxFONTWEIGHT_NORMAL,false,"IBM Plex Sans Arabic"); }
inline wxFont Code(int size=12) { return wxFont(size,wxFONTFAMILY_MODERN,wxFONTSTYLE_NORMAL,wxFONTWEIGHT_NORMAL,false,"Consolas"); }
inline wxString T(const char* text) { return ui::Text(text); }

inline void Glyph(wxGraphicsContext* g,const wxString& name,double x,double y,double s,const wxColour& color) {
    g->PushState(); g->Translate(x,y); g->Scale(s/24,s/24); g->SetPen(wxPen(color,1.8)); g->SetBrush(*wxTRANSPARENT_BRUSH);
    auto p=g->CreatePath();
    if(name=="play") { p.MoveToPoint(8,4);p.AddLineToPoint(20,12);p.AddLineToPoint(8,20);p.CloseSubpath();g->SetBrush(wxBrush(color)); }
    else if(name=="stop") { p.AddRoundedRectangle(5,5,14,14,2); }
    else if(name=="plus") { p.MoveToPoint(12,5);p.AddLineToPoint(12,19);p.MoveToPoint(5,12);p.AddLineToPoint(19,12); }
    else if(name=="file") { p.MoveToPoint(7,3);p.AddLineToPoint(15,3);p.AddLineToPoint(20,8);p.AddLineToPoint(20,21);p.AddLineToPoint(5,21);p.AddLineToPoint(5,3);p.CloseSubpath();p.MoveToPoint(14,3);p.AddLineToPoint(14,9);p.AddLineToPoint(20,9); }
    else if(name=="folder") { p.MoveToPoint(3,7);p.AddLineToPoint(3,20);p.AddLineToPoint(21,20);p.AddLineToPoint(21,8);p.AddLineToPoint(12,8);p.AddLineToPoint(10,5);p.AddLineToPoint(3,5);p.CloseSubpath(); }
    else if(name=="save") { p.MoveToPoint(4,3);p.AddLineToPoint(17,3);p.AddLineToPoint(21,7);p.AddLineToPoint(21,21);p.AddLineToPoint(3,21);p.AddLineToPoint(3,3);p.CloseSubpath();p.AddRectangle(7,3,9,6);p.AddRectangle(7,14,10,7); }
    else if(name=="build") { p.MoveToPoint(4,8);p.AddLineToPoint(12,3);p.AddLineToPoint(20,8);p.AddLineToPoint(20,17);p.AddLineToPoint(12,22);p.AddLineToPoint(4,17);p.CloseSubpath();p.MoveToPoint(4,8);p.AddLineToPoint(12,13);p.AddLineToPoint(20,8);p.MoveToPoint(12,13);p.AddLineToPoint(12,22); }
    else if(name=="settings") { p.AddCircle(12,12,6);p.AddCircle(12,12,2);for(int i=0;i<8;++i){double a=i*3.14159265359/4;p.MoveToPoint(12+7*cos(a),12+7*sin(a));p.AddLineToPoint(12+10*cos(a),12+10*sin(a));} }
    else if(name=="menu") { for(int i=0;i<3;++i){p.MoveToPoint(4,6+i*6);p.AddLineToPoint(20,6+i*6);} }
    else if(name=="search") { p.AddCircle(10,10,6);p.MoveToPoint(15,15);p.AddLineToPoint(21,21); }
    else if(name=="close") { p.MoveToPoint(7,7);p.AddLineToPoint(17,17);p.MoveToPoint(17,7);p.AddLineToPoint(7,17); }
    else if(name=="code") { p.MoveToPoint(8,6);p.AddLineToPoint(2,12);p.AddLineToPoint(8,18);p.MoveToPoint(16,6);p.AddLineToPoint(22,12);p.AddLineToPoint(16,18); }
    else if(name=="terminal") { p.MoveToPoint(4,6);p.AddLineToPoint(10,12);p.AddLineToPoint(4,18);p.MoveToPoint(12,18);p.AddLineToPoint(21,18); }
    else if(name=="alert") { p.AddCircle(12,12,9);p.MoveToPoint(12,6);p.AddLineToPoint(12,13);p.AddCircle(12,17,0.7); }
    else if(name=="arrow") { p.MoveToPoint(5,12);p.AddLineToPoint(19,12);p.MoveToPoint(14,7);p.AddLineToPoint(19,12);p.AddLineToPoint(14,17); }
    else if(name=="home") { p.MoveToPoint(3,11);p.AddLineToPoint(12,3);p.AddLineToPoint(21,11);p.MoveToPoint(5,10);p.AddLineToPoint(5,21);p.AddLineToPoint(19,21);p.AddLineToPoint(19,10);p.MoveToPoint(10,21);p.AddLineToPoint(10,14);p.AddLineToPoint(14,14);p.AddLineToPoint(14,21); }
    g->DrawPath(p);g->PopState();
}

class Button : public wxControl {
    wxString icon_;
    bool primary_=false,hover_=false,pressed_=false,selected_=false;
#if wxUSE_ACCESSIBILITY
    class Accessible : public wxAccessible {
        Button* button_;
    public:
        explicit Accessible(Button* button):wxAccessible(button),button_(button){}
        wxAccStatus GetRole(int id,wxAccRole* role) override {if(id)return wxACC_NOT_IMPLEMENTED;*role=wxROLE_SYSTEM_PUSHBUTTON;return wxACC_OK;}
        wxAccStatus GetName(int id,wxString* name) override {if(id)return wxACC_NOT_IMPLEMENTED;*name=button_->GetLabel();return wxACC_OK;}
        wxAccStatus GetChildCount(int* count) override {*count=0;return wxACC_OK;}
        wxAccStatus GetDefaultAction(int id,wxString* name) override {if(id)return wxACC_NOT_IMPLEMENTED;*name=T("تنفيذ");return wxACC_OK;}
        wxAccStatus GetLocation(wxRect& rect,int id) override {if(id)return wxACC_NOT_IMPLEMENTED;rect=button_->GetScreenRect();return wxACC_OK;}
        wxAccStatus GetState(int id,long* state) override {if(id)return wxACC_NOT_IMPLEMENTED;*state=wxACC_STATE_SYSTEM_FOCUSABLE;if(button_->HasFocus())*state|=wxACC_STATE_SYSTEM_FOCUSED;if(!button_->IsEnabled())*state|=wxACC_STATE_SYSTEM_UNAVAILABLE;if(button_->selected_)*state|=wxACC_STATE_SYSTEM_PRESSED;return wxACC_OK;}
        wxAccStatus DoDefaultAction(int id) override {if(id)return wxACC_NOT_IMPLEMENTED;button_->Fire();return wxACC_OK;}
    };
#endif
    void Fire() { if(!IsEnabled()) return; wxCommandEvent event(wxEVT_BUTTON,GetId());event.SetEventObject(this);ProcessWindowEvent(event); }
    void Paint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));dc.Clear();
        std::unique_ptr<wxGraphicsContext> g(wxGraphicsContext::Create(dc));if(!g)return;
        auto parentBg=GetParent()->GetBackgroundColour();bool light=parentBg.Red()+parentBg.Green()+parentBg.Blue()>450;
        auto size=GetClientSize();wxColour bg=primary_?Blue:selected_?(light?wxColour("#E6EDFA"):Raised):hover_?(light?wxColour("#E8EEF7"):Raised):parentBg;
        if(pressed_)bg=primary_?wxColour("#245FAF"):light?Border:Raised;
        wxColour fg=!IsEnabled()?wxColour("#8392A7"):primary_?*wxWHITE:selected_?(light?Blue:Accent):light?Ink:Text;
        g->SetBrush(wxBrush(bg));g->SetPen(wxPen(HasFocus()?(light?Blue:Accent):primary_?bg:selected_?Border:bg,HasFocus()?2:1));
        g->DrawRoundedRectangle(1,1,size.x-2,size.y-2,FromDIP(8));
        g->Flush();
        dc.SetFont(GetFont());dc.SetTextForeground(fg);wxSize text=dc.GetTextExtent(GetLabel());
        int gap=icon_.empty()?0:FromDIP(9),iconSize=icon_.empty()?0:FromDIP(18),start=(size.x-text.x-gap-iconSize)/2;
        dc.DrawText(GetLabel(),start,(size.y-text.y)/2);
        if(!icon_.empty())Glyph(g.get(),icon_,start+text.x+gap,(size.y-iconSize)/2,iconSize,fg);
    }
public:
    void SetLabel(const wxString& label) override {
        wxControl::SetLabel(label);SetName(label);
        if(GetFont().IsOk())SetMinSize(wxSize(std::max(FromDIP(76),GetTextExtent(label).x+FromDIP(icon_.empty()?24:49)),FromDIP(40)));
    }
    Button(wxWindow* parent,int id,const wxString& label,const wxString& icon="",bool primary=false,int width=100)
      :wxControl(parent,id,wxDefaultPosition,parent->FromDIP(wxSize(width,40)),wxBORDER_NONE|wxWANTS_CHARS),icon_(icon),primary_(primary) {
        SetFont(Font(11,primary));SetLabel(label);SetToolTip(label);SetBackgroundStyle(wxBG_STYLE_PAINT);SetMinSize(wxSize(std::max(GetMinSize().x,parent->FromDIP(width)),parent->FromDIP(40)));SetCursor(wxCursor(wxCURSOR_HAND));
#if wxUSE_ACCESSIBILITY
        SetAccessible(new Accessible(this));
#endif
        Bind(wxEVT_PAINT,&Button::Paint,this);
        Bind(wxEVT_ENTER_WINDOW,[this](wxMouseEvent&){hover_=true;Refresh();});Bind(wxEVT_LEAVE_WINDOW,[this](wxMouseEvent&){hover_=false;Refresh();});
        Bind(wxEVT_LEFT_DOWN,[this](wxMouseEvent&){if(IsEnabled()){pressed_=true;SetFocus();CaptureMouse();Refresh();}});
        Bind(wxEVT_LEFT_UP,[this](wxMouseEvent& e){bool fire=pressed_&&GetClientRect().Contains(e.GetPosition());pressed_=false;if(HasCapture())ReleaseMouse();Refresh();if(fire)Fire();});
        Bind(wxEVT_MOUSE_CAPTURE_LOST,[this](wxMouseCaptureLostEvent&){pressed_=false;Refresh();});
        Bind(wxEVT_KEY_DOWN,[this](wxKeyEvent& e){if(e.GetKeyCode()==WXK_RETURN||e.GetKeyCode()==WXK_SPACE)Fire();else e.Skip();});
        Bind(wxEVT_SET_FOCUS,[this](wxFocusEvent& e){Refresh();e.Skip();});Bind(wxEVT_KILL_FOCUS,[this](wxFocusEvent& e){Refresh();e.Skip();});
    }
    bool Enable(bool enabled=true) override { bool result=wxControl::Enable(enabled);Refresh();return result; }
    void Select(bool selected) { selected_=selected;Refresh(); }
};

class Book : public wxPanel {
    wxBoxSizer *layout_,*bar_;wxPanel* tabs_;std::vector<wxWindow*> pages_;std::vector<Button*> buttons_;int selection_=-1;
public:
    explicit Book(wxWindow* parent):wxPanel(parent,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxBORDER_NONE) {
        SetBackgroundColour(Editor);layout_=new wxBoxSizer(wxVERTICAL);tabs_=new wxPanel(this);tabs_->SetBackgroundColour(Soft);bar_=new wxBoxSizer(wxHORIZONTAL);bar_->AddStretchSpacer();tabs_->SetSizer(bar_);layout_->Add(tabs_,0,wxEXPAND);SetSizer(layout_);
    }
    void AddPage(wxWindow* page,const wxString& label) {
        int index=static_cast<int>(pages_.size());auto* button=new Button(tabs_,wxID_ANY,label,index==0?"alert":index==1?"terminal":"build",false,index==1?170:145);
        bar_->Insert(0,button,0,wxALL,FromDIP(3));pages_.push_back(page);buttons_.push_back(button);layout_->Add(page,1,wxEXPAND);page->Hide();
        button->Bind(wxEVT_BUTTON,[this,index](wxCommandEvent&){SetSelection(index);});if(selection_<0)SetSelection(0);
    }
    int SetSelection(int index) {int old=selection_;if(index<0||index>=static_cast<int>(pages_.size()))return old;selection_=index;for(size_t i=0;i<pages_.size();++i){pages_[i]->Show(static_cast<int>(i)==index);buttons_[i]->Select(static_cast<int>(i)==index);}Layout();return old;}
    void SetPageText(size_t index,const wxString& text){if(index<buttons_.size()){buttons_[index]->SetLabel(text);buttons_[index]->Refresh();}}
};

class TabArt : public wxAuiGenericTabArt {
public:
    TabArt(){SetNormalFont(Code(10));SetSelectedFont(Code(10));SetMeasuringFont(Code(10));SetColour(Panel);SetActiveColour(Editor);}
    wxAuiTabArt* Clone() override {return new TabArt(*this);}
    void DrawBorder(wxDC&,wxWindow*,const wxRect&) override {}
    int GetBorderWidth(wxWindow*) override {return 0;}
    int GetAdditionalBorderSpace(wxWindow*) override {return 0;}
    int GetIndentSize() override {return 6;}
    void DrawBackground(wxDC& dc,wxWindow*,const wxRect& r) override {dc.SetPen(*wxTRANSPARENT_PEN);dc.SetBrush(wxBrush(Soft));dc.DrawRectangle(r);}
    wxSize GetTabSize(wxDC& dc,wxWindow* wnd,const wxString& caption,const wxBitmapBundle&,bool,int,int* extent) override {
        dc.SetFont(Code(10));int w=std::clamp(dc.GetTextExtent(caption).x+wnd->FromDIP(78),wnd->FromDIP(160),wnd->FromDIP(280));*extent=w;return wxSize(w,wnd->FromDIP(42));
    }
    int GetBestTabCtrlSize(wxWindow* wnd,const wxAuiNotebookPageArray&,const wxSize&) override{return wnd->FromDIP(44);}
    void DrawTab(wxDC& dc,wxWindow* wnd,const wxAuiNotebookPage& page,const wxRect& in,int close,wxRect* tab,wxRect* button,int* extent) override {
        auto size=GetTabSize(dc,wnd,page.caption,page.bitmap,page.active,close,extent);*tab=wxRect(in.x,in.y+2,size.x,size.y);*button=wxRect(tab->GetRight()-wnd->FromDIP(29),tab->y+wnd->FromDIP(11),wnd->FromDIP(20),wnd->FromDIP(20));
        dc.SetClippingRegion(*tab);dc.SetPen(*wxTRANSPARENT_PEN);dc.SetBrush(wxBrush(page.active?Editor:Soft));dc.DrawRoundedRectangle(*tab,wnd->FromDIP(7));
        if(page.active){dc.SetPen(wxPen(Blue,3));dc.DrawLine(tab->x+10,tab->y+1,tab->GetRight()-10,tab->y+1);}
        std::unique_ptr<wxGraphicsContext> g(wxGraphicsContext::CreateFromUnknownDC(dc));if(g){Glyph(g.get(),"code",tab->x+wnd->FromDIP(13),tab->y+wnd->FromDIP(13),wnd->FromDIP(16),Blue);if(close!=wxAUI_BUTTON_STATE_HIDDEN)Glyph(g.get(),"close",button->x,button->y,button->width,Hint);}
        dc.SetFont(Code(10));dc.SetTextForeground(page.active?Ink:Hint);auto text=dc.GetTextExtent(page.caption);dc.DrawText(page.caption,tab->x+wnd->FromDIP(38),tab->y+(tab->height-text.y)/2);dc.DestroyClippingRegion();
    }
};

class Banner : public wxPanel {
public:
    explicit Banner(wxWindow* parent):wxPanel(parent,wxID_ANY,wxDefaultPosition,parent->FromDIP(wxSize(-1,220)),wxBORDER_NONE){
        SetMinSize(parent->FromDIP(wxSize(-1,220)));SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT,[this](wxPaintEvent&){
            wxAutoBufferedPaintDC dc(this);dc.SetBackground(wxBrush(Soft));dc.Clear();std::unique_ptr<wxGraphicsContext> g(wxGraphicsContext::Create(dc));if(!g)return;
            int w=GetClientSize().x,h=GetClientSize().y;g->SetPen(*wxTRANSPARENT_PEN);g->SetBrush(g->CreateLinearGradientBrush(0,0,w,h,wxColour("#E1EBFC"),wxColour("#EEE8FD")));g->DrawRoundedRectangle(0,0,w,h,FromDIP(18));
            if(w>FromDIP(700)){
                double x=FromDIP(30),y=FromDIP(34),cw=FromDIP(260),ch=FromDIP(145);
                g->SetBrush(wxBrush(wxColour("#BCCFEA")));g->DrawRoundedRectangle(x+18,y+18,cw,ch,12);
                g->SetBrush(wxBrush(Base));g->DrawRoundedRectangle(x,y,cw,ch,12);g->SetBrush(wxBrush(Raised));g->DrawRoundedRectangle(x,y,cw,FromDIP(26),12);
                for(int i=0;i<3;++i){g->SetBrush(wxBrush(i==0?wxColour("#F0A7B2"):i==1?wxColour("#F3D390"):Accent));g->DrawEllipse(x+FromDIP(14+i*12),y+FromDIP(10),FromDIP(5),FromDIP(5));}
                for(int i=0;i<4;++i){g->SetBrush(wxBrush(i%2?wxColour("#8CB6F4"):Accent));g->DrawRoundedRectangle(x+FromDIP(25+(i%2)*16),y+FromDIP(48+i*20),FromDIP(130-i*18),FromDIP(5),2);}
                g->SetBrush(wxBrush(*wxWHITE));g->DrawRoundedRectangle(x+cw-FromDIP(45),y+ch-FromDIP(42),FromDIP(78),FromDIP(60),12);Glyph(g.get(),"code",x+cw-FromDIP(28),y+ch-FromDIP(28),FromDIP(42),Blue);
            }
            g->Flush();int right=w-FromDIP(34);dc.SetTextForeground(Purple);dc.SetFont(Font(10,true));wxString over=T("اكتب وشغّل");dc.DrawText(over,right-dc.GetTextExtent(over).x,FromDIP(35));
            dc.SetFont(Font(25,true));dc.SetTextForeground(Ink);wxString title=T("Satr Studio");dc.DrawText(title,right-dc.GetTextExtent(title).x,FromDIP(69));
            dc.SetFont(Font(11));dc.SetTextForeground(Hint);wxString line=T("افتح ملفًا أو أنشئ برنامجًا جديدًا.");dc.DrawText(line,right-dc.GetTextExtent(line).x,FromDIP(126));
            dc.SetFont(Code(10));dc.SetTextForeground(Blue);wxString tag="C / C++ / Python / JavaScript / Java";dc.DrawText(tag,right-dc.GetTextExtent(tag).x,FromDIP(169));
        });
    }
};

class Card : public wxPanel {
public:
    Card(wxWindow* parent,int id,const wxString& title,const wxString& caption,const wxString& icon,const wxColour& tint)
      :wxPanel(parent,wxID_ANY,wxDefaultPosition,parent->FromDIP(wxSize(240,190)),wxBORDER_NONE){
        SetBackgroundColour(*wxWHITE);SetMinSize(parent->FromDIP(wxSize(240,190)));auto* box=new wxBoxSizer(wxVERTICAL);
        auto* accent=new wxPanel(this,wxID_ANY,wxDefaultPosition,FromDIP(wxSize(-1,4)));accent->SetBackgroundColour(tint);box->Add(accent,0,wxEXPAND);
        auto* heading=new wxStaticText(this,wxID_ANY,title,wxDefaultPosition,wxDefaultSize,wxALIGN_RIGHT);heading->SetFont(Font(15,true));heading->SetForegroundColour(Ink);box->Add(heading,0,wxEXPAND|wxLEFT|wxRIGHT|wxTOP,FromDIP(20));
        auto* text=new wxStaticText(this,wxID_ANY,caption,wxDefaultPosition,wxDefaultSize,wxALIGN_RIGHT);text->SetFont(Font(10));text->SetForegroundColour(Hint);box->Add(text,0,wxEXPAND|wxLEFT|wxRIGHT|wxTOP,FromDIP(20));box->AddStretchSpacer();
        box->Add(new Button(this,id,T("فتح"),icon,false,124),0,wxALIGN_RIGHT|wxALL,FromDIP(14));SetSizer(box);
        heading->SetWindowStyleFlag(ui::Arabic()?wxALIGN_RIGHT:wxALIGN_LEFT);text->SetWindowStyleFlag(ui::Arabic()?wxALIGN_RIGHT:wxALIGN_LEFT);
        Bind(wxEVT_SIZE,[this,heading,text,title,caption](wxSizeEvent& e){int width=std::max(80,GetClientSize().x-FromDIP(40));heading->SetLabel(title);text->SetLabel(caption);heading->Wrap(width);text->Wrap(width);Layout();e.Skip();});
    }
};
}
