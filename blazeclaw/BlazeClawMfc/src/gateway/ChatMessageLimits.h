#pragma once

#include <cstddef>

namespace blazeclaw::gateway {

/// Soft cap for `chat.send` / `chat.inject` user message UTF-8 payload size (single message field
/// and related body overrides). Aligns with task-delta payload scale; avoids unbounded memory use.
inline constexpr std::size_t kMaxChatUserMessageUtf8Bytes = 1048576;

} // namespace blazeclaw::gateway
