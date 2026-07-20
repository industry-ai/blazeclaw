#include "pch.h"
#include "CPeer.h"

namespace blazeclaw::irc {

std::shared_ptr<CPeer> CPeer::Create(const std::string& nickname) {
    return std::shared_ptr<CPeer>(new CPeer(nickname));
}

std::string CPeer::GetPrefix() const {
    std::string prefix;
    prefix.reserve(nickname_.size() + username_.size() + hostname_.size() + 2);
    prefix = nickname_;
    if (!username_.empty()) {
        prefix += "!";
        prefix += username_;
    }
    if (!hostname_.empty()) {
        prefix += "@";
        prefix += hostname_;
    }
    return prefix;
}

std::string CPeer::GetHostmask() const {
    return GetPrefix();
}

} // namespace blazeclaw::irc
