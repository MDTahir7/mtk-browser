#pragma once

#include "include/cef_request_context.h"
#include "include/cef_scheme.h"

namespace mtk::home {

// Called in every CEF process before initialization completes.
void RegisterCustomScheme(CefRawPtr<CefSchemeRegistrar> registrar);

// Registers the browser-owned documents (mtk://home/ and mtk://history/) for a
// request context. A null context means the global context; private windows pass
// their in-memory context. `private_session` serves the same pages with no
// profile-local rows, so a private window never renders history, bookmarks or
// download activity.
bool RegisterSchemeHandler(CefRefPtr<CefRequestContext> context = nullptr,
                           bool private_session = false);

}  // namespace mtk::home
