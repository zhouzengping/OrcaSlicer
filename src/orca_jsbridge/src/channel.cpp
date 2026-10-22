#include "orca/jsbridge/channel.hpp"

namespace orca::jb {

namespace
{

/*
 * Holds the view weakly: the channel must not keep the webview alive, and a
 * view destroyed before the bridge reports InvalidState instead of hanging.
 */
class WebviewChannel : public Channel
{
public:
    explicit WebviewChannel(const std::shared_ptr<wv::WebView>& view)
        : m_view(view)
    {
    }

    wv::Status post_message_json(const std::string& json_text) override
    {
        std::shared_ptr<wv::WebView> view = m_view.lock();
        if (view == nullptr)
            return wv::Status::make(wv::Errc::InvalidState, "webview is gone");

        return view->post_message_json(json_text);
    }

    wv::Status add_init_script(const std::string& js) override
    {
        std::shared_ptr<wv::WebView> view = m_view.lock();
        if (view == nullptr)
            return wv::Status::make(wv::Errc::InvalidState, "webview is gone");

        // Queues inside the view while Creating, passes through once Ready.
        return view->add_init_script(js);
    }

private:
    std::weak_ptr<wv::WebView> m_view;
};

} // namespace

std::unique_ptr<Channel> make_webview_channel(const std::shared_ptr<wv::WebView>& view)
{
    if (view == nullptr)
        return nullptr;

    return std::make_unique<WebviewChannel>(view);
}

} // namespace orca::jb
