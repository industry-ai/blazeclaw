#pragma once

// Central WebView2 SDK detection. Every translation unit that gates code on
// HAVE_WEBVIEW2_HEADER must include this header (not duplicate __has_include blocks).

#if defined(__has_include)
# if __has_include(<WebView2.h>)
#  ifndef HAVE_WEBVIEW2_HEADER
#   define HAVE_WEBVIEW2_HEADER
#  endif
#  include <WebView2.h>
# endif
#endif
