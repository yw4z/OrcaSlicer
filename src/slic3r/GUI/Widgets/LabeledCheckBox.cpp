#include "LabeledCheckBox.hpp"

#include <wx/tglbtn.h> // to keep wxEVT_TOGGLEBUTTON

/*
Elipsize end on limited size when no wrapping
*/

LabeledCheckBox::LabeledCheckBox(wxWindow *parent, wxString label)
    : wxPanel(parent, wxID_ANY)
    , m_on(           this, "check_on"            , 18)
    , m_half(         this, "check_half"          , 18)
    , m_off(          this, "check_off"           , 18)
    , m_on_disabled(  this, "check_on_disabled"   , 18)
    , m_half_disabled(this, "check_half_disabled" , 18)
    , m_off_disabled( this, "check_off_disabled"  , 18)
    , m_on_focused(   this, "check_on_focused"    , 18) 
    , m_half_focused( this, "check_half_focused"  , 18)
    , m_off_focused(  this, "check_off_focused"   , 18)
    , m_font(Label::Body_14)
    , m_value(false)
{
    if (parent)
        SetBackgroundColour(parent->GetBackgroundColour());
    if (auto sParent = GetScrollParent(this))
        SetBackgroundColour(sParent->GetBackgroundColour());

    m_label = label;

    m_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_check = new Button(this, "", "check_off", 0, 18);
    m_check->SetPaddingSize(FromDIP(wxSize(0,0)));
    m_check->SetBackgroundColor(GetBackgroundColour());
    m_check->SetCornerRadius(0);
    m_check->SetBorderWidth(0);

    m_check->Bind(wxEVT_SET_FOCUS ,([this](wxFocusEvent e) {UpdateTextBorder(true); UpdateIcon(); e.Skip();}));
    m_check->Bind(wxEVT_KILL_FOCUS,([this](wxFocusEvent e) {UpdateTextBorder(false);UpdateIcon(); e.Skip();}));

    m_sizer->Add(m_check, 0, wxALIGN_CENTER_VERTICAL); // Dont add spacing otherwise hover events will break

    if(!label.IsEmpty()){
        m_has_text = true;

        m_text_box = new StaticBox(this);
        m_text_box->SetCornerRadius(0);
        m_text_box->SetBorderColor(GetBackgroundColour());
        m_text_box->SetCanFocus(false);
        m_text_box->DisableFocusFromKeyboard();

        m_text = new wxStaticText(m_text_box, wxID_ANY, label);
        m_text->SetFont(m_font);
        UpdateTextColor(true);

        wxBoxSizer *label_sizer = new wxBoxSizer(wxHORIZONTAL);
        label_sizer->Add(m_text, 0, wxALL, FromDIP(5));
        m_text_box->SetSizer(label_sizer);

        m_sizer->Add(m_text_box, 0, wxALIGN_CENTER_VERTICAL); // Dont add spacing otherwise hover events will break
    }

    std::vector<wxWindow*> w_list = {m_check};
    if (m_has_text) {
        w_list.push_back(m_text_box);
        w_list.push_back(m_text);
    }
    for (wxWindow* w : w_list) {
        w->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &e) {
            m_hovered = true;
            UpdateIcon();
            e.Skip();
        });
        w->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &e) {
            if(m_has_text){
                wxWindow* next_w = wxFindWindowAtPoint(wxGetMousePosition());
                if (!next_w || !IsDescendant(next_w) || next_w == this)
                    m_hovered = false;
            }
            else
                m_hovered = false;
            UpdateIcon();
            e.Skip();
        });
        w->Bind(wxEVT_LEFT_DOWN  ,[this](wxMouseEvent e) {
            if (!m_enabled || e.LeftDClick()) return;
            OnClick();
            e.Skip();
        });
        w->Bind(wxEVT_LEFT_DCLICK,[this](wxMouseEvent e) {
            if (!m_enabled) return;
            OnClick();
            e.Skip();
        });
    };

    Bind(wxEVT_CHAR_HOOK, ([this](wxKeyEvent&e){
        if(HasFocus() && e.GetKeyCode() == WXK_SPACE)
            SetValue(!m_value);
        else
            e.Skip();
    }));

    SetSizerAndFit(m_sizer);
    Layout();

    Refresh();
}

void LabeledCheckBox::Wrap(int width)
{
    if(!m_has_text) return;
    m_text->Wrap((width > 0) ? std::max(width - m_check->GetSize().x, 0) : width);

    m_sizer->Fit(this);
    m_sizer->SetSizeHints(this);
    Layout();
    Refresh();
}
void LabeledCheckBox::OnClick()
{
    m_check->SetFocus();
    SetValue(!m_value);
}

void LabeledCheckBox::SetTooltip(wxString label)
{
    m_check->SetToolTip(label);
    if(m_has_text)
        m_text->SetToolTip(label);
}

bool LabeledCheckBox::SetFont(const wxFont& font) {
    m_font = font;
    if(m_has_text)
        return m_text->SetFont(font);
    return false;
};

bool LabeledCheckBox::Enable(bool enable) {
    m_enabled = enable;
    bool result = m_check->Enable(enable);
    UpdateTextColor(enable);
    UpdateIcon();
    Refresh();
    return result;
};

bool LabeledCheckBox::HasFocus() const {
    return m_check->HasFocus();
}

void LabeledCheckBox::UpdateIcon()
{
    ScalableBitmap icon;
    bool focus = HasFocus();
    icon = (!m_enabled        ) ? (m_half_checked ? m_half_disabled : m_value ? m_on_disabled : m_off_disabled )
         : (m_hovered || focus) ? (m_half_checked ? m_half_focused  : m_value ? m_on_focused  : m_off_focused  ) 
         :                        (m_half_checked ? m_half          : m_value ? m_on          : m_off          );
    m_check->SetIcon(icon.name());
    m_check->Refresh();
}

void LabeledCheckBox::UpdateTextColor(bool enabled) {
    if(m_has_text)  // just changes its color to prevent unwanted effect on windows
        m_text->SetForegroundColour(StateColor::darkModeColorFor(wxColour(enabled ? "#363636" : "#6B6A6A")));
};

void LabeledCheckBox::UpdateTextBorder(bool focused) {
    if(m_has_text)
        m_text_box->SetBorderColor(focused ? wxColour("#009688") : GetBackgroundColour());
};

wxWindow* LabeledCheckBox::GetScrollParent(wxWindow *pWindow)
{
    wxWindow *pWin = pWindow;
    while (pWin->GetParent()) {
        auto pWin2 = pWin->GetParent();
        if (auto top = dynamic_cast<wxScrollHelper *>(pWin2))
            return dynamic_cast<wxWindow *>(pWin);
        pWin = pWin2;
    }
    return nullptr;
}

void LabeledCheckBox::SetValue(bool value){
    if (m_value == value)
        return;
    m_value = value;
    m_half_checked = false;
    UpdateIcon();

    // just in case support both event to prevent crash. wxCheckbox (wxEVT_CHECKBOX) CheckBox (wxEVT_TOGGLEBUTTON)
    for (wxEventType type : {wxEVT_CHECKBOX, wxEVT_TOGGLEBUTTON}) {
        wxCommandEvent evt(type, GetId());
        evt.SetEventObject(this);
        evt.SetInt(value ? 1 : 0);
        GetEventHandler()->ProcessEvent(evt);
    }

    Refresh();
}

void LabeledCheckBox::Rescale(){
    m_on.msw_rescale();
    m_half.msw_rescale();
    m_off.msw_rescale();
    m_on_disabled.msw_rescale();
    m_half_disabled.msw_rescale();
    m_off_disabled.msw_rescale();
    m_on_focused.msw_rescale();
    m_half_focused.msw_rescale();
    m_off_focused.msw_rescale();

    m_check->Rescale();

    m_sizer->Fit(this);
    m_sizer->SetSizeHints(this);
    Layout();
    Refresh();
}