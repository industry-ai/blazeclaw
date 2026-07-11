#pragma once

#include <string>

namespace blazeclaw::app::chatcontroller {

struct NativeControllerBuildMarker {
	std::string name = "chat-controller";
};

NativeControllerBuildMarker CreateNativeControllerBuildMarker();

} // namespace blazeclaw::app::chatcontroller
