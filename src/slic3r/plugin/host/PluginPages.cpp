#include "PluginPages.hpp"

#include "libslic3r/AppConfig.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/Notebook.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Widgets/WebView.hpp"
#include "slic3r/GUI/Widgets/WebViewHostDialog.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/plugin/PluginManager.hpp"

#include <libslic3r/Utils.hpp>

#include <algorithm>

#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>
#include <nlohmann/json.hpp>

#include <stdexcept>
#include <wx/bookctrl.h>
#include <wx/choice.h>
#include <wx/sizer.h>

#include <utility>

namespace Slic3r {
namespace {

constexpr char PLUGIN_PAGE_BRIDGE_JS[] = R"JS(
(function () {
  if (window.top !== window.self) return;
  if (window.orca) return;
  var handlers = [];
  function deliver(payload, attempts) {
    try {
      if (window.wx && typeof window.wx.postMessage === 'function') {
        window.wx.postMessage(payload);
        return;
      }
    } catch (e) { /* retry while the native handler is being registered */ }
    if (attempts < 100)
      window.setTimeout(function () { deliver(payload, attempts + 1); }, 25);
  }
  function send(data) {
    deliver(JSON.stringify({
      channel: 'orca', kind: 'message', data: (data === undefined ? null : data)
    }), 0);
  }
  window.orca = {
    postMessage: function (data) { send(data); },
    onMessage: function (callback) {
      if (typeof callback === 'function') handlers.push(callback);
    }
  };
  window.__orcaDispatch = function (payload) {
    var data = payload ? payload.data : null;
    for (var i = 0; i < handlers.length; i++) {
      try { handlers[i](data); } catch (e) {}
    }
  };
})();
)JS";

} // namespace

PluginPage::PluginPage(wxWindow* parent, std::shared_ptr<PagesPluginCapability> capability)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
    , m_cap(std::move(capability))
    , m_lifetime(std::make_shared<std::atomic<PluginPage*>>(this))
{
    auto* topsizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(topsizer);

    m_browser = WebView::CreateWebView(this, bootstrap_url());
    if (m_browser == nullptr) {
        wxLogError("Could not initialize plugin page web view");
        return;
    }

    topsizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));
    m_browser->Bind(wxEVT_WEBVIEW_LOADED, &PluginPage::on_bootstrap_event, this);
    m_browser->Bind(wxEVT_WEBVIEW_ERROR, &PluginPage::on_bootstrap_event, this);
    m_browser->Bind(wxEVT_WEBVIEW_NEWWINDOW, &PluginPage::on_new_window, this);
    m_browser->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &PluginPage::on_script_message, this);
    m_browser->AddUserScript(wxString::FromUTF8(GUI::WebViewHostDialog::theme_user_script()));
    m_browser->AddUserScript(wxString::FromUTF8(GUI::WebViewHostDialog::plugin_defaults_user_script()));
    m_browser->AddUserScript(PLUGIN_PAGE_BRIDGE_JS);

    const std::shared_ptr<std::atomic<PluginPage*>> lifetime = m_lifetime;
    m_cap->set_message_sender([lifetime](const std::string& message) {
        if (wxTheApp == nullptr)
            return;

        GUI::wxGetApp().CallAfter([lifetime, message] {
            if (PluginPage* page = lifetime->load(std::memory_order_acquire))
                page->push_message(message);
        });
    });

}

PluginPage::~PluginPage()
{
    detach_capability();
    if (m_lifetime)
        m_lifetime->store(nullptr, std::memory_order_release);
}

void PluginPage::detach_capability()
{
    if (m_lifetime)
        m_lifetime->store(nullptr, std::memory_order_release);
    if (m_cap)
        m_cap->clear_message_sender();
    m_cap.reset();
}

wxString PluginPage::web_base_url() const
{
    const auto path = (boost::filesystem::path(resources_dir()) / "web").make_preferred().string();
    return wxString("file://") + GUI::from_u8(path) + "/";
}

wxString PluginPage::bootstrap_url() const
{
    const auto path = (boost::filesystem::path(resources_dir()) / "web/dialog/PluginWebDialog/blank.html").make_preferred().string();
    return wxString("file://") + GUI::from_u8(path);
}

void PluginPage::on_bootstrap_event(wxWebViewEvent& event)
{
    load_plugin_content();
    event.Skip();
}

void PluginPage::load_plugin_content()
{
    if (m_content_loaded || m_browser == nullptr || m_cap == nullptr)
        return;

    m_content_loaded = true;
    try {
        m_browser->SetPage(wxString::FromUTF8(m_cap->get_ui()), web_base_url());
    } catch (const std::exception& error) {
        BOOST_LOG_TRIVIAL(error) << "Failed to load plugin page '" << m_cap->name() << "': " << error.what();
        detach_capability();
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "Failed to load plugin page '" << m_cap->name() << "'";
        detach_capability();
    }
}

void PluginPage::on_new_window(wxWebViewEvent& event)
{
    const wxString url = event.GetURL();
    if (!url.empty() && m_browser != nullptr)
        m_browser->LoadURL(url);
    event.Veto();
}

void PluginPage::on_script_message(wxWebViewEvent& event)
{
    if (!m_cap)
        return;

    const wxString payload = event.GetString();
    nlohmann::json root    = nlohmann::json::parse(payload.utf8_string(), nullptr, false);
    if (root.is_discarded() || root.value("channel", std::string()) != "orca" ||
        root.value("kind", std::string()) != "message")
        return;

    const nlohmann::json data = root.contains("data") ? root["data"] : nlohmann::json();
    try {
        m_cap->on_message(data.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    } catch (const std::exception& error) {
        BOOST_LOG_TRIVIAL(error) << "Plugin page message handler failed for '" << m_cap->name() << "': " << error.what();
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "Plugin page message handler failed for '" << m_cap->name() << "'";
    }
}

void PluginPage::push_message(const std::string& message)
{
    if (m_browser == nullptr)
        return;

    nlohmann::json data = nlohmann::json::parse(message, nullptr, false);
    if (data.is_discarded())
        data = message;

    const wxString script = wxString("(function dispatch(payload, attempts) {\n") +
                            wxString("  if (typeof window.__orcaDispatch === 'function') { window.__orcaDispatch(payload); return; }\n") +
                            wxString("  if (attempts < 100) window.setTimeout(function() { dispatch(payload, attempts + 1); }, 25);\n") +
                            wxString("})({data: ") +
                            wxString::FromUTF8(data.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)) +
                            wxString("}, 0);");
    WebView::RunScript(m_browser, script);
}

class PluginPagesOverflowPanel : public wxPanel
{
public:
    explicit PluginPagesOverflowPanel(Notebook* parent)
        : wxPanel(parent, wxID_ANY)
        , m_notebook(parent)
    {
        auto* sizer = new wxBoxSizer(wxVERTICAL);

        m_choice = new wxChoice(this, wxID_ANY);
        m_choice->Bind(wxEVT_CHOICE, &PluginPagesOverflowPanel::on_choice, this);
        sizer->Add(m_choice, wxSizerFlags().Expand().Border(wxALL, FromDIP(4)));

        m_content_sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(m_content_sizer, wxSizerFlags().Expand().Proportion(1));

        SetSizer(sizer);
    }

    void add_entry(const PluginCapabilityId& id, PluginPage* page, const wxString& title)
    {
        page->Reparent(this);
        page->Hide();
        m_entries.push_back({id, page, title});
        m_choice->Append(title);
        if (m_entries.size() == 1)
            select_index(0);
    }

    void select_entry(const PluginCapabilityId& id)
    {
        for (size_t i = 0; i < m_entries.size(); ++i) {
            if (m_entries[i].id == id) {
                select_index(i);
                return;
            }
        }
    }

    void clear()
    {
        if (m_shown_index != wxNOT_FOUND)
            m_entries[static_cast<size_t>(m_shown_index)].page->Hide();
        m_content_sizer->Clear(false);

        for (const Entry& entry : m_entries)
            entry.page->Reparent(m_notebook);

        m_entries.clear();
        m_choice->Clear();
        m_shown_index = wxNOT_FOUND;
    }

    wxString current_title() const { return m_shown_index == wxNOT_FOUND ? wxString() : m_entries[static_cast<size_t>(m_shown_index)].title; }
    int current_image_id() const
    {
        return m_shown_index == wxNOT_FOUND ? wxBookCtrlBase::NO_IMAGE : m_entries[static_cast<size_t>(m_shown_index)].page->get_icon_image_id();
    }

private:
    struct Entry
    {
        PluginCapabilityId id;
        PluginPage* page;
        wxString title;
    };

    void on_choice(wxCommandEvent&)
    {
        const int selection = m_choice->GetSelection();
        if (selection != wxNOT_FOUND)
            select_index(static_cast<size_t>(selection));
    }

    void select_index(size_t index)
    {
        if (index >= m_entries.size())
            return;

        if (m_shown_index != wxNOT_FOUND)
            m_entries[static_cast<size_t>(m_shown_index)].page->Hide();

        m_content_sizer->Clear(false);
        m_content_sizer->Add(m_entries[index].page, wxSizerFlags().Expand().Proportion(1));
        m_entries[index].page->Show();
        Layout();

        m_shown_index = static_cast<int>(index);
        m_choice->SetSelection(static_cast<int>(index));

        const int tab_index = m_notebook->FindPage(this);
        if (tab_index != wxNOT_FOUND) {
            m_notebook->SetPageText(static_cast<size_t>(tab_index), m_entries[index].title);
            m_notebook->SetPageImage(static_cast<size_t>(tab_index), m_entries[index].page->get_icon_image_id());
        }
    }

    Notebook* m_notebook{nullptr};
    wxChoice* m_choice{nullptr};
    wxBoxSizer* m_content_sizer{nullptr};
    std::vector<Entry> m_entries;
    int m_shown_index{wxNOT_FOUND};
};

PluginPages::~PluginPages()
{
    shutdown();
    // try {
    // } catch (const std::exception& error) {
    //     BOOST_LOG_TRIVIAL(error) << "PluginPages::~PluginPages: shutdown() threw: " << error.what();
    // } catch (...) {
    //     BOOST_LOG_TRIVIAL(error) << "PluginPages::~PluginPages: shutdown() threw a non-standard exception";
    // }
}

void PluginPages::initialize(Notebook* parent)
{
    shutdown();
    m_parent = parent;
    if (m_parent == nullptr)
        return;

    m_visible_page_count = GUI::wxGetApp().app_config->get_plugin_pages_visible_count();
    m_notebook_base_index = static_cast<size_t>(m_parent->GetPageCount());

    m_image_list = std::make_unique<wxImageList>(20, 20, true, 0);
    m_parent->SetImageList(m_image_list.get());

    for (const auto& capability : PluginManager::instance().get_plugin_capabilities("", PluginCapabilityType::Pages)) {
        if (capability)
            create_page(capability->identity());
    }
    relayout();
}

void PluginPages::shutdown()
{
    while (!m_pages.empty())
        remove_page(m_pages.begin()->first);
    if (m_parent != nullptr)
        m_parent->SetImageList(nullptr);
    m_image_list.reset();
    m_parent = nullptr;
    m_notebook_base_index = 0;
}

void PluginPages::set_visible_page_count(int count)
{
    const int clamped = std::max(PLUGIN_PAGES_VISIBLE_COUNT_MIN, std::min(count, PLUGIN_PAGES_VISIBLE_COUNT_MAX));
    if (clamped == m_visible_page_count)
        return;

    m_visible_page_count = clamped;
    relayout();
}

std::shared_ptr<PagesPluginCapability> PluginPages::get_pages_cap(const PluginCapabilityId& id, bool is_enabled) const
{
    auto capability = PluginManager::instance().get_plugin_capability(id, /*only_enabled=*/false);
    if (!capability || capability->is_enabled() != is_enabled || capability->type() != PluginCapabilityType::Pages)
        return nullptr;

    return std::dynamic_pointer_cast<PagesPluginCapability>(capability);
}

bool PluginPages::create_page(const PluginCapabilityId& id)
{
    if (m_pages.find(id) != m_pages.end())
        return false;

    auto capability = get_pages_cap(id, true);
    if (!capability)
        return false;

    std::string icon;
    try {
        icon = capability->get_icon();
    } catch (const std::exception& error) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << " Failed to get icon for plugin " << id.plugin_key << ": " << error.what();
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << " Failed to get icon for plugin " << id.plugin_key;
    }

    auto* page = new PluginPage(m_parent, std::move(capability));
    if (!page->is_valid()) {
        page->Destroy();
        return false;
    }

    int image_id = wxBookCtrlBase::NO_IMAGE;
    if (!icon.empty() && m_image_list) {
        try {
            boost::filesystem::path icon_path(icon);
            const std::string extension = icon_path.extension().string();
            if (extension == ".svg" || extension == ".png")
                icon_path.replace_extension();

            const wxBitmap bitmap = create_scaled_bitmap(icon_path.string(), m_parent, 20);
            if (bitmap.IsOk())
                image_id = m_image_list->Add(bitmap);
        } catch (const std::exception& error) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << " Failed to load icon for plugin " << id.plugin_key << ": " << error.what();
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << " Failed to load icon for plugin " << id.plugin_key;
        }
    }

    page->set_icon_image_id(image_id);
    m_pages.emplace(id, page);
    m_order.push_back(id);
    return true;
}

void PluginPages::on_cap_register(const PluginCapabilityId& id)
{
    if (m_parent == nullptr)
        return;

    if (create_page(id))
        relayout();
}

void PluginPages::on_cap_deregister(const PluginCapabilityId& id)
{
    remove_page(id);
}

void PluginPages::on_plugin_register(const std::string& plugin_key)
{
    for (const auto& capability : PluginManager::instance().get_plugin_capabilities(plugin_key, PluginCapabilityType::Pages)) {
        if (capability)
            on_cap_register(capability->identity());
    }
}

void PluginPages::on_plugin_deregister(const std::string& plugin_key)
{
    for (auto it = m_pages.begin(); it != m_pages.end();) {
        if (it->first.plugin_key != plugin_key) {
            ++it;
            continue;
        }

        const PluginCapabilityId id = it->first;
        ++it;
        remove_page(id);
    }
}

void PluginPages::remove_page(const PluginCapabilityId& id)
{
    auto it = m_pages.find(id);
    if (it == m_pages.end())
        return;

    PluginPage* page = it->second;
    const int removed_image_id = page->get_icon_image_id();
    page->detach_capability();

    m_pages.erase(it);
    m_order.erase(std::remove(m_order.begin(), m_order.end(), id), m_order.end());

    if (m_image_list && removed_image_id != wxBookCtrlBase::NO_IMAGE &&
        removed_image_id >= 0 && removed_image_id < m_image_list->GetImageCount()) {
        m_image_list->Remove(removed_image_id);

        // wxImageList IDs are positional. Removing one shifts all later images down by one.
        for (auto& [other_id, other_page] : m_pages) {
            const int other_image_id = other_page->get_icon_image_id();
            if (other_image_id > removed_image_id)
                other_page->set_icon_image_id(other_image_id - 1);
        }
    }

    relayout();
    page->Destroy();
}

wxString PluginPages::page_tab_id(const PluginCapabilityId& id)
{
    return wxString::FromUTF8("plugin." + id.plugin_key + "." + id.name);
}

void PluginPages::relayout()
{
    if (m_parent == nullptr)
        return;

    m_order.erase(std::remove_if(m_order.begin(), m_order.end(),
        [this](const PluginCapabilityId& id) {
            const bool orphaned = m_pages.find(id) == m_pages.end();
            if (orphaned)
                BOOST_LOG_TRIVIAL(error) << "PluginPages::relayout: '" << id.name << "' was in m_order but not m_pages, dropping";
            return orphaned;
        }),
        m_order.end());

    wxString id_to_reselect = m_parent->GetSelectedPageName();

    while (m_parent->GetPageCount() > m_notebook_base_index)
        m_parent->RemovePage(m_parent->GetPageCount() - 1);
    if (m_overflow_panel != nullptr)
        m_overflow_panel->clear();

    const int visible_slots = std::max(1, m_visible_page_count);
    const bool need_overflow = static_cast<int>(m_order.size()) > visible_slots;
    const size_t individual_count = need_overflow ? static_cast<size_t>(visible_slots - 1) : m_order.size();

    for (size_t i = 0; i < individual_count; ++i) {
        const PluginCapabilityId& id = m_order[i];
        PluginPage* page = m_pages.at(id);
        m_parent->InsertPage(m_parent->GetPageCount(), page_tab_id(id), page, wxString::FromUTF8(id.name), page->get_icon_image_id());
    }

    if (need_overflow) {
        if (m_overflow_panel == nullptr)
            m_overflow_panel = new PluginPagesOverflowPanel(m_parent);

        bool reselecting_overflow_entry = false;
        for (size_t i = individual_count; i < m_order.size(); ++i) {
            const PluginCapabilityId& id = m_order[i];
            m_overflow_panel->add_entry(id, m_pages.at(id), wxString::FromUTF8(id.name));
            if (page_tab_id(id) == id_to_reselect) {
                m_overflow_panel->select_entry(id);
                reselecting_overflow_entry = true;
            }
        }
        if (reselecting_overflow_entry)
            id_to_reselect = "plugin.__overflow__";

        m_parent->InsertPage(m_parent->GetPageCount(), "plugin.__overflow__", m_overflow_panel,
                              m_overflow_panel->current_title(), m_overflow_panel->current_image_id());
    } else if (m_overflow_panel != nullptr) {
        m_overflow_panel->Destroy();
        m_overflow_panel = nullptr;
    }

    if (!id_to_reselect.empty())
        m_parent->SelectPageByName(id_to_reselect);
}

} // namespace Slic3r
