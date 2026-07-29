#include "PluginPages.hpp"

#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/Notebook.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Widgets/WebView.hpp"
#include "slic3r/GUI/Widgets/WebViewHostDialog.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/plugin/PluginManager.hpp"

#include <libslic3r/Utils.hpp>

#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>
#include <nlohmann/json.hpp>

#include <stdexcept>
#include <wx/bookctrl.h>
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

PluginPages::~PluginPages()
{
    shutdown();
}

void PluginPages::initialize(Notebook* parent)
{
    shutdown();
    m_parent = parent;
    if (m_parent == nullptr)
        return;

    // Keep image-list indices stable for the lifetime of this notebook. Removing an image
    // would shift every later index, so deregistration only removes the page.
    m_image_list = std::make_unique<wxImageList>(20, 20, true, 0);
    m_parent->SetImageList(m_image_list.get());

    for (const auto& capability : PluginManager::instance().get_plugin_capabilities("", PluginCapabilityType::Pages)) {
        if (capability)
            on_cap_register(capability->identity());
    }
}

void PluginPages::shutdown()
{
    while (!m_pages.empty())
        remove_page(m_pages.begin()->first);
    if (m_parent != nullptr)
        m_parent->SetImageList(nullptr);
    m_image_list.reset();
    m_parent = nullptr;
}

std::shared_ptr<PagesPluginCapability> PluginPages::get_pages_cap(const PluginCapabilityId& id, bool is_enabled) const
{
    auto capability = PluginManager::instance().get_plugin_capability(id, /*only_enabled=*/false);
    if (!capability || capability->is_enabled() != is_enabled || capability->type() != PluginCapabilityType::Pages)
        return nullptr;

    return std::dynamic_pointer_cast<PagesPluginCapability>(capability);
}

void PluginPages::on_cap_register(const PluginCapabilityId& id)
{
    if (m_parent == nullptr || m_pages.find(id) != m_pages.end())
        return;

    auto capability = get_pages_cap(id, true);
    if (!capability)
        return;

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
        return;
    }

    const wxString title = wxString::FromUTF8(id.name);

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
    if (!m_parent->AddPage(page, title, false, image_id)) {
        if (image_id != wxBookCtrlBase::NO_IMAGE && m_image_list && image_id == m_image_list->GetImageCount() - 1)
            m_image_list->Remove(image_id);
        page->Destroy();
        return;
    }

    m_pages.emplace(id, page);
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
    if (m_parent != nullptr) {
        const int index = m_parent->FindPage(page);
        if (index != wxNOT_FOUND)
            m_parent->RemovePage(static_cast<size_t>(index));
    }

    if (m_image_list && removed_image_id != wxBookCtrlBase::NO_IMAGE &&
        removed_image_id >= 0 && removed_image_id < m_image_list->GetImageCount()) {
        m_image_list->Remove(removed_image_id);

        // wxImageList IDs are positional. Removing one shifts all later images down by
        // one, so update both the page state and the notebook button for those pages.
        for (const auto& [other_id, other_page] : m_pages) {
            if (other_id == id)
                continue;

            const int other_image_id = other_page->get_icon_image_id();
            if (other_image_id <= removed_image_id)
                continue;

            const int updated_image_id = other_image_id - 1;
            other_page->set_icon_image_id(updated_image_id);

            if (m_parent != nullptr) {
                const int other_index = m_parent->FindPage(other_page);
                if (other_index != wxNOT_FOUND)
                    m_parent->SetPageImage(static_cast<size_t>(other_index), updated_image_id);
            }
        }
    }

    page->Destroy();
    m_pages.erase(it);
}

} // namespace Slic3r
