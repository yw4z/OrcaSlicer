#ifndef slic3r_Notebook_hpp_
#define slic3r_Notebook_hpp_

//#ifdef _WIN32

#include <string>
#include <vector>
#include <string>
#include <wx/bookctrl.h>
#include <wx/imaglist.h>
#include <wx/sizer.h>

class ScalableButton;
class Button;

// custom message the ButtonsListCtrl sends to its parent (Notebook) to notify a selection change:
wxDECLARE_EVENT(wxCUSTOMEVT_NOTEBOOK_SEL_CHANGED, wxCommandEvent);

class ButtonsListCtrl : public wxControl
{
public:
    // BBS
    ButtonsListCtrl(wxWindow* parent, wxBoxSizer* side_tools = NULL);
    ~ButtonsListCtrl() {}

    void OnPaint(wxPaintEvent&);
    void SetSelection(int sel);
    void UpdateMode();
    void Rescale();
    bool InsertPage(size_t n, const wxString &text, bool bSelect = false, const std::string &bmp_name = "", int imageId = wxBookCtrlBase::NO_IMAGE);
    void RemovePage(size_t n);
    bool SetPageImage(size_t n, const std::string& bmp_name) const;
    bool SetPageImage(size_t n, int imageId);
    void SetImageList(wxImageList* imageList) { m_imageList = imageList; }
    void SetPageText(size_t n, const wxString& strText);
    void SetCompact(size_t n, bool compact); // ORCA
    wxString GetPageText(size_t n) const;
    wxFlexGridSizer* GetBtnsSizer(){return m_buttons_sizer;}; // ORCA

private:
    wxFlexGridSizer*                m_buttons_sizer;
    wxBoxSizer*                     m_sizer;
    // BBS: use Button
    std::vector<Button*>            m_pageButtons;
    int                             m_selection {-1};
    int                             m_btn_margin;
    int                             m_line_margin;
    std::vector<wxString>           m_pageLabels; // ORCA
    wxImageList*                    m_imageList{nullptr};
};

class Notebook : public wxBookCtrlBase
{
public:
    // Negative values below wxBookCtrlBase::NO_IMAGE are reserved for the built-in
    // tabs. Nonnegative values are wxImageList indices supplied by plugin pages.
    static constexpr int PAGE_HOME         = -2;
    static constexpr int PAGE_PREPARE      = -3;
    static constexpr int PAGE_PREVIEW      = -4;
    static constexpr int PAGE_MONITOR      = -5;
    static constexpr int PAGE_MULTI_DEVICE = -6;
    static constexpr int PAGE_PROJECT      = -7;
    static constexpr int PAGE_CALIBRATION  = -8;

    Notebook(wxWindow * parent,
                 wxWindowID winid = wxID_ANY,
                 const wxPoint & pos = wxDefaultPosition,
                 const wxSize & size = wxDefaultSize,
                // BBS
                 wxBoxSizer* side_tools = NULL,
                 long style = 0)
    {
        Init();
        Create(parent, winid, pos, size, side_tools, style);
    }

    bool Create(wxWindow * parent,
                wxWindowID winid = wxID_ANY,
                const wxPoint & pos = wxDefaultPosition,
                const wxSize & size = wxDefaultSize,
                // BBS
                wxBoxSizer* side_tools = NULL,
                long style = 0)
    {
        if (!wxBookCtrlBase::Create(parent, winid, pos, size, style | wxBK_TOP))
            return false;

        m_bookctrl = new ButtonsListCtrl(this, side_tools);

        wxSizer* mainSizer = new wxBoxSizer(IsVertical() ? wxVERTICAL : wxHORIZONTAL);

        if (style & wxBK_RIGHT || style & wxBK_BOTTOM)
            mainSizer->Add(0, 0, 1, wxEXPAND, 0);

        m_controlSizer = new wxBoxSizer(IsVertical() ? wxHORIZONTAL : wxVERTICAL);
        m_controlSizer->Add(m_bookctrl, wxSizerFlags(1).Expand());
        wxSizerFlags flags;
        if (IsVertical())
            flags.Expand();
        else
            flags.CentreVertical();
        mainSizer->Add(m_controlSizer, flags.Border(wxALL, m_controlMargin));
        SetSizer(mainSizer);

        this->Bind(wxCUSTOMEVT_NOTEBOOK_SEL_CHANGED, [this](wxCommandEvent& evt)
        {
            if (int page_idx = evt.GetId(); page_idx >= 0)
                SetSelection(page_idx);
        });

        this->Bind(wxEVT_NAVIGATION_KEY, &Notebook::OnNavigationKey, this);

        return true;
    }


    // Methods specific to this class.

    // A method allowing to add a new page without any label (which is unused
    // by this control) and show it immediately.
    bool ShowNewPage(wxWindow * page)
    {
        return AddPage(page, wxString(), false, NO_IMAGE);
    }


    // Set effect to use for showing/hiding pages.
    void SetEffects(wxShowEffect showEffect, wxShowEffect hideEffect)
    {
        m_showEffect = showEffect;
        m_hideEffect = hideEffect;
    }

    // Or the same effect for both of them.
    void SetEffect(wxShowEffect effect)
    {
        SetEffects(effect, effect);
    }

    // And the same for time outs.
    void SetEffectsTimeouts(unsigned showTimeout, unsigned hideTimeout)
    {
        m_showTimeout = showTimeout;
        m_hideTimeout = hideTimeout;
    }

    void SetEffectTimeout(unsigned timeout)
    {
        SetEffectsTimeouts(timeout, timeout);
    }


    // Implement base class pure virtual methods.

    bool AddPage(wxWindow* page, const wxString& text, bool bSelect = false, int imageId = NO_IMAGE) override
    {
        DoInvalidateBestSize();
        return InsertPage(GetPageCount(), page, text, bSelect, imageId);
    }

    // Page management
    virtual bool InsertPage(size_t n,
                            wxWindow * page,
                            const wxString & text,
                            bool bSelect = false,
                            int imageId = NO_IMAGE) override
    {
        wxString page_name;
        std::string bmp_name;
        const bool is_fixed_page = get_fixed_page_info(imageId, page_name, bmp_name);
        const int stored_image_id = is_fixed_page ? NO_IMAGE : imageId;

        if (!wxBookCtrlBase::InsertPage(n, page, text, bSelect, stored_image_id))
            return false;

        m_pageNames.insert(m_pageNames.begin() + n, page_name);
        m_pageImageIds.insert(m_pageImageIds.begin() + n, stored_image_id);
        GetBtnsListCtrl()->InsertPage(n, text, bSelect, bmp_name, stored_image_id);

        if (!DoSetSelectionAfterInsertion(n, bSelect))
            page->Hide();

        return true;
    }

    bool InsertPage(size_t n,
                    const wxString& id,
                    wxWindow* page,
                    const wxString& text,
                    int imageId,
                    bool bSelect = false)
    {
        if (!wxBookCtrlBase::InsertPage(n, page, text, bSelect, imageId))
            return false;

        m_pageNames.insert(m_pageNames.begin() + n, id);
        m_pageImageIds.insert(m_pageImageIds.begin() + n, imageId);
        GetBtnsListCtrl()->InsertPage(n, text, bSelect, "", imageId);

        if (!DoSetSelectionAfterInsertion(n, bSelect))
            page->Hide();

        return true;
    }

    bool InsertPage(size_t n,
                    const wxString& id,
                    wxWindow * page,
                    const wxString & text,
                    const std::string& bmp_name = "",
                    bool bSelect = false)
    {
        if (!wxBookCtrlBase::InsertPage(n, page, text, bSelect))
            return false;

        m_pageNames.insert(m_pageNames.begin() + n, id);
        m_pageImageIds.insert(m_pageImageIds.begin() + n, NO_IMAGE);
        GetBtnsListCtrl()->InsertPage(n, text, bSelect, bmp_name);

        // wxBookCtrlBase::InsertPage() only inserts into the page list and sizes the
        // new page to the current page's rect — it never touches visibility. A freshly
        // constructed page defaults to shown, so without this it renders on top of
        // whatever page is currently selected until the next SetSelection() call hides
        // it. Mirrors the pure-virtual InsertPage() override above, which already does
        // this correctly.
        if (!DoSetSelectionAfterInsertion(n, bSelect))
            page->Hide();

        return true;
    }

    virtual int SetSelection(size_t n) override
    {
        int ret = DoSetSelection(n, SetSelection_SendEvent);
        int new_sel = GetSelection();
        //check the new_sel firstly
        if (new_sel != n) {
            //not allowed, skip it
            return ret;
        }
        GetBtnsListCtrl()->SetSelection(n);

        // check that only the selected page is visible and others are hidden:
        for (size_t page = 0; page < m_pages.size(); page++) {
            wxWindow* win_a = GetPage(page);
            wxWindow* win_b = GetPage(n);
            if (page != n && GetPage(page) != GetPage(n)) {
                m_pages[page]->Hide();
            }
        }

        return ret;
    }

    virtual int ChangeSelection(size_t n) override
    {
        GetBtnsListCtrl()->SetSelection(n);
        return DoSetSelection(n);
    }

    // Labels are stored by the custom button list; page images use the wx image-list IDs below.
    virtual bool SetPageText(size_t n, const wxString & strText) override
    {
        wxCHECK_MSG(n < GetPageCount(), false, wxS("Invalid page"));

        GetBtnsListCtrl()->SetPageText(n, strText);

        return true;
    }

    virtual wxString GetPageText(size_t n) const override
    {
        wxCHECK_MSG(n < GetPageCount(), wxString(), wxS("Invalid page"));
        return GetBtnsListCtrl()->GetPageText(n);
    }

    virtual bool SetPageImage(size_t n, int imageId) override
    {
        if (n >= m_pageImageIds.size())
            return false;

        if (!GetBtnsListCtrl()->SetPageImage(n, imageId))
            return false;

        m_pageImageIds[n] = imageId;
        return true;
    }

    virtual int GetPageImage(size_t n) const override
    {
        return n < m_pageImageIds.size() ? m_pageImageIds[n] : NO_IMAGE;
    }

    void SetImageList(wxImageList* imageList)
    {
        m_imageList = imageList;
        GetBtnsListCtrl()->SetImageList(imageList);
    }

    bool SetPageImage(size_t n, const std::string& bmp_name)
    {
        return GetBtnsListCtrl()->SetPageImage(n, bmp_name);
    }

    // Override some wxWindow methods too.
    virtual void SetFocus() override
    {
        wxWindow* const page = GetCurrentPage();
        if (page)
            page->SetFocus();
    }

    // wxBookCtrlBase::DeleteAllPages() clears its page list directly rather than
    // going through DoRemovePage() per page, so it would otherwise leave
    // m_pageNames desynchronized (a mutation path outside the four this class
    // already keeps in sync). Not currently called on a Notebook anywhere in
    // this codebase, but kept correct for the same reason the rest of this
    // bookkeeping exists.
    virtual bool DeleteAllPages() override
    {
        m_pageNames.clear();
        m_pageImageIds.clear();
        return wxBookCtrlBase::DeleteAllPages();
    }

    ButtonsListCtrl* GetBtnsListCtrl() const { return static_cast<ButtonsListCtrl*>(m_bookctrl); }

    int FindPageByName(const wxString& id) const
    {
        if (id.empty())
            return wxNOT_FOUND;
        for (size_t i = 0; i < m_pageNames.size(); ++i)
            if (m_pageNames[i] == id)
                return static_cast<int>(i);
        return wxNOT_FOUND;
    }

    wxWindow* GetPageByName(const wxString& id) const
    {
        const int idx = FindPageByName(id);
        return idx == wxNOT_FOUND ? nullptr : GetPage(static_cast<size_t>(idx));
    }

    bool SelectPageByName(const wxString& id)
    {
        const int idx = FindPageByName(id);
        if (idx == wxNOT_FOUND)
            return false;
        SetSelection(static_cast<size_t>(idx));
        return true;
    }

    // Inverse of FindPageByName: index -> id. Empty string for an out-of-range
    // index or a page that was never given an id (e.g. settings Tab pages).
    wxString GetPageName(size_t n) const
    {
        return n < m_pageNames.size() ? m_pageNames[n] : wxString();
    }

    wxString GetSelectedPageName() const
    {
        const int sel = GetSelection();
        return sel < 0 ? wxString() : GetPageName(static_cast<size_t>(sel));
    }

    void UpdateMode()
    {
        GetBtnsListCtrl()->UpdateMode();
    }

    void Rescale()
    {
        GetBtnsListCtrl()->Rescale();
    }

    void OnNavigationKey(wxNavigationKeyEvent& event)
    {
        if (event.IsWindowChange()) {
            // change pages
            AdvanceSelection(event.GetDirection());
        }
        else {
            // we get this event in 3 cases
            //
            // a) one of our pages might have generated it because the user TABbed
            // out from it in which case we should propagate the event upwards and
            // our parent will take care of setting the focus to prev/next sibling
            //
            // or
            //
            // b) the parent panel wants to give the focus to us so that we
            // forward it to our selected page. We can't deal with this in
            // OnSetFocus() because we don't know which direction the focus came
            // from in this case and so can't choose between setting the focus to
            // first or last panel child
            //
            // or
            //
            // c) we ourselves (see MSWTranslateMessage) generated the event
            //
            wxWindow* const parent = GetParent();

            // the wxObject* casts are required to avoid MinGW GCC 2.95.3 ICE
            const bool isFromParent = event.GetEventObject() == (wxObject*)parent;
            const bool isFromSelf = event.GetEventObject() == (wxObject*)this;
            const bool isForward = event.GetDirection();

            if (isFromSelf && !isForward)
            {
                // focus is currently on notebook tab and should leave
                // it backwards (Shift-TAB)
                event.SetCurrentFocus(this);
                parent->HandleWindowEvent(event);
            }
            else if (isFromParent || isFromSelf)
            {
                // no, it doesn't come from child, case (b) or (c): forward to a
                // page but only if entering notebook page (i.e. direction is
                // backwards (Shift-TAB) comething from out-of-notebook, or
                // direction is forward (TAB) from ourselves),
                if (m_selection != wxNOT_FOUND &&
                    (!event.GetDirection() || isFromSelf))
                {
                    // so that the page knows that the event comes from it's parent
                    // and is being propagated downwards
                    event.SetEventObject(this);

                    wxWindow* page = m_pages[m_selection];
                    if (!page->HandleWindowEvent(event))
                    {
                        page->SetFocus();
                    }
                    //else: page manages focus inside it itself
                }
                else // otherwise set the focus to the notebook itself
                {
                    SetFocus();
                }
            }
            else
            {
                // it comes from our child, case (a), pass to the parent, but only
                // if the direction is forwards. Otherwise set the focus to the
                // notebook itself. The notebook is always the 'first' control of a
                // page.
                if (!isForward)
                {
                    SetFocus();
                }
                else if (parent)
                {
                    event.SetCurrentFocus(this);
                    parent->HandleWindowEvent(event);
                }
            }
        }
    }

protected:
    virtual void UpdateSelectedPage(size_t WXUNUSED(newsel)) override
    {
        // Nothing to do here, but must be overridden to avoid the assert in
        // the base class version.
    }

    virtual wxBookCtrlEvent * CreatePageChangingEvent() const override
    {
        return new wxBookCtrlEvent(wxEVT_BOOKCTRL_PAGE_CHANGING,
                                   GetId());
    }

    virtual void MakeChangedEvent(wxBookCtrlEvent & event) override
    {
        event.SetEventType(wxEVT_BOOKCTRL_PAGE_CHANGED);
    }

    virtual wxWindow * DoRemovePage(size_t page) override
    {
        wxWindow* const win = wxBookCtrlBase::DoRemovePage(page);
        if (win)
        {
            m_pageNames.erase(m_pageNames.begin() + page);
            m_pageImageIds.erase(m_pageImageIds.begin() + page);
            GetBtnsListCtrl()->RemovePage(page);
            DoSetSelectionAfterRemoval(page);
        }

        return win;
    }

    virtual void DoSize() override
    {
        wxWindow* const page = GetCurrentPage();
        if (page)
            page->SetSize(GetPageRect());
    }

    virtual void DoShowPage(wxWindow * page, bool show) override
    {
        if (show)
            page->ShowWithEffect(m_showEffect, m_showTimeout);
        else
            page->HideWithEffect(m_hideEffect, m_hideTimeout);
    }

private:
    static bool get_fixed_page_info(int imageId, wxString& page_name, std::string& bmp_name)
    {
        switch (imageId) {
        case PAGE_HOME:
            page_name = wxS("home");
            bmp_name = "tab_home_active";
            return true;
        case PAGE_PREPARE:
            page_name = wxS("prepare");
            bmp_name = "tab_3d_active";
            return true;
        case PAGE_PREVIEW:
            page_name = wxS("preview");
            bmp_name = "tab_preview_active";
            return true;
        case PAGE_MONITOR:
            page_name = wxS("monitor");
            bmp_name = "tab_monitor_active";
            return true;
        case PAGE_MULTI_DEVICE:
            page_name = wxS("multi_device");
            bmp_name = "tab_multi_active";
            return true;
        case PAGE_PROJECT:
            page_name = wxS("project");
            bmp_name = "tab_auxiliary_active";
            return true;
        case PAGE_CALIBRATION:
            page_name = wxS("calibration");
            bmp_name = "tab_calibration_active";
            return true;
        default:
            return false;
        }
    }

    void Init();

    std::vector<wxString> m_pageNames;   // index-parallel to wxBookCtrlBase::m_pages
    std::vector<int>      m_pageImageIds; // index-parallel to wxBookCtrlBase::m_pages
    wxImageList*          m_imageList{nullptr};

    wxShowEffect m_showEffect,
                 m_hideEffect;

    unsigned m_showTimeout,
             m_hideTimeout;
};
//#endif // _WIN32
#endif // slic3r_Notebook_hpp_
