#pragma once

#ifndef BLAZECLAW_ENABLE_NATIVE_CHAT_PATH
#define BLAZECLAW_ENABLE_NATIVE_CHAT_PATH 0
#endif

namespace blazeclaw::app::chatui {

inline constexpr bool kNativeChatPathEnabled =
	BLAZECLAW_ENABLE_NATIVE_CHAT_PATH != 0;

} // namespace blazeclaw::app::chatui
