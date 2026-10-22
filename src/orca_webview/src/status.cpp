#include "orca/webview/types.hpp"

namespace orca::wv {

Status Status::make(Errc code, std::string message, int64_t native)
{
    Status status;
    status.code    = code;
    status.message = std::move(message);
    status.native  = native;
    return status;
}

} // namespace orca::wv
