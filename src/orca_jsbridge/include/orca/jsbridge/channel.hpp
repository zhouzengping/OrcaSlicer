#pragma once

#include <memory>
#include <string>

#include "orca/jsbridge/export.h"
#include "orca/webview/types.hpp"
#include "orca/webview/webview.hpp"

namespace orca::jb {

/*
 * Minimal outbound transport the Bridge needs. The production adapter wraps a
 * live orca::wv::WebView; tests provide their own fake to run headless.
 */
class ORCA_JB_API Channel
{
public:
    virtual ~Channel() = default;

    virtual wv::Status post_message_json(const std::string& json_text) = 0;
    virtual wv::Status add_init_script(const std::string& js) = 0;
};

/*
 * Adapter over a live webview. The view may still be in Creating state: init
 * scripts queue inside the view until creation completes.
 * Returns nullptr when view is null.
 */
ORCA_JB_API std::unique_ptr<Channel> make_webview_channel(const std::shared_ptr<wv::WebView>& view);

} // namespace orca::jb
