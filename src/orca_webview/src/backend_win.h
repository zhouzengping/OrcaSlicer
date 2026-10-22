#pragma once

// Windows WebView2 backend (backend_win.cpp + backend_win_environment.cpp).

#include "backend.h"

namespace orca::wv::detail {

Backend* create_backend_win(void* native_parent, const WebView::Config& config, EventProxy& proxy);

} // namespace orca::wv::detail
