#include "HyperLink.hpp"
#include "Label.hpp"

#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

namespace Slic3r { namespace GUI {

HyperLink::HyperLink(wxWindow* parent, const wxString& label, const wxString& url, long style)
    : wxStaticText(parent, wxID_ANY, label, wxDefaultPosition, wxDefaultSize, style | wxUSE_MARKUP)
    , m_url(url)
    , m_normalColor(wxColour("#009687")) // used slightly different color otherwise automatically uses ColorForDark that not visible enough
    , m_hoverColor(wxColour("#26A69A"))
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    SetForegroundColour(m_normalColor);
    HyperLink::SetFont(Label::Head_14);

    //SetCursor(wxCursor(wxCURSOR_HAND)); // drectly using on class sets curson on parent permanently on linux

    if (!m_url.IsEmpty())
        SetToolTip(m_url);

    Bind(wxEVT_LEFT_DOWN, ([this](wxMouseEvent& e) {
        if (!m_url.IsEmpty())
            wxLaunchDefaultBrowser(m_url);
    }));

#ifdef __WXGTK__
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& e) {
        SetLabelColor(this, m_hoverColor);
        Refresh();
        e.Skip();
    });

    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& e) {
        SetLabelColor(this, m_normalColor);
        Refresh();
        e.Skip();
    });
    // GTK: wxEVT_SET_CURSOR is needed because SetCursor() gets overridden
    Bind(wxEVT_SET_CURSOR, [this](wxSetCursorEvent& e) {
        e.SetCursor(wxCursor(wxCURSOR_HAND));
        e.StopPropagation(); // prevents affecting its parent window
    });
#else
    // Windows / macOS: SetCursor() in ENTER/LEAVE is sufficient
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& e) {
        SetCursor(wxCursor(wxCURSOR_HAND));
        SetForegroundColour(m_normalColor);
    });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& e) {
        SetCursor(wxNullCursor);
        SetForegroundColour(m_hoverColor);
    });
#endif
}

#ifdef __WXGTK__
void HyperLink::SetLabelColor(wxStaticText* label, const wxColour& color)
{
    GtkWidget* widget = label->GetHandle();
    GtkCssProvider* provider = gtk_css_provider_new();

    wxString css = wxString::Format("* { color: rgb(%d, %d, %d); }",color.Red(), color.Green(), color.Blue());

    gtk_css_provider_load_from_data(provider, css.utf8_str(), -1, nullptr);
    gtk_style_context_add_provider(
        gtk_widget_get_style_context(widget),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);
}
#endif

bool HyperLink::SetFont(const wxFont& font)
{ // ensure it stays underlined
    #ifdef __WXMAC__
        // SetUnderlined not works on macOS. set underline via markup
        bool result = wxStaticText::SetFont(font);
        SetLabelMarkup("<u>" + GetLabelText() + "</u>");
        return result;
    #else
        wxFont f = font;
        f.SetUnderlined(true);
        return wxStaticText::SetFont(f);
    #endif
}

void HyperLink::SetURL(const wxString& url)
{
    m_url = url;
    SetToolTip(m_url);
}

wxString HyperLink::GetURL() const { return m_url; }

}} // namespace Slic3r::GUI
