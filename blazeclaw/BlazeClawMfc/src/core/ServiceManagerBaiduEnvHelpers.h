#pragma once

#include <optional>
#include <string>

namespace blazeclaw::core::servicemanager_baidu_env {

	std::optional<std::wstring> ResolveBaiduApiKeyFromPersistedConfig();

} // namespace blazeclaw::core::servicemanager_baidu_env
