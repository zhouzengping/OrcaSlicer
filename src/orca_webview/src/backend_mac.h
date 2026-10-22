#pragma once

// macOS WKWebView backend (backend_mac.mm).

#include "backend.h"

namespace orca::wv::detail {

Backend* create_backend_mac(void* native_parent, const WebView::Config& config, EventProxy& proxy);

} // namespace orca::wv::detail
