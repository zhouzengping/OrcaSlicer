#pragma once

// Linux WebKitGTK backend (backend_gtk.cpp).

#include "backend.h"

namespace orca::wv::detail {

Backend* create_backend_gtk(void* native_parent, const WebView::Config& config, EventProxy& proxy);

} // namespace orca::wv::detail
