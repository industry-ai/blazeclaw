#pragma once

#include <string>

namespace blazeclaw::irc {

class IrcMessageParser {
public:
    static bool ParsePrivmsgLine(const std::string& line,
                                 std::string& nick,
                                 std::string& user,
                                 std::string& host,
                                 std::string& channel,
                                 std::string& message);

    static bool ParseJoinPartLine(const std::string& line,
                                  std::string& nick,
                                  std::string& user,
                                  std::string& host,
                                  std::string& channel);

    static std::string BuildPrivmsgPayload(const std::string& channel,
                                           const std::string& message);
};

} // namespace blazeclaw::irc