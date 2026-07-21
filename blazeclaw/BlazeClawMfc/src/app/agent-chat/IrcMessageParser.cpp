#include "pch.h"
#include "IrcMessageParser.h"

#include <chrono>
#include <cstdio>
#include <sstream>

namespace blazeclaw::irc {

namespace {

int64_t GetCurrentUnixSeconds() {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

std::string EscapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(
                        buf,
                        sizeof(buf),
                        "\\u%04x",
                        static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    return out;
}

} // namespace

bool IrcMessageParser::ParsePrivmsgLine(const std::string& line,
                                        std::string& nick,
                                        std::string& user,
                                        std::string& host,
                                        std::string& channel,
                                        std::string& message) {
    if (line.empty() || line[0] != ':') {
        return false;
    }

    size_t pos = 1;
    size_t space = line.find(' ', pos);
    if (space == std::string::npos) return false;

    std::string prefix = line.substr(pos, space - pos);
    pos = space + 1;

    size_t nick_end = prefix.find('!');
    if (nick_end != std::string::npos) {
        nick = prefix.substr(0, nick_end);
        size_t user_end = prefix.find('@', nick_end + 1);
        if (user_end != std::string::npos) {
            user = prefix.substr(nick_end + 1, user_end - nick_end - 1);
            host = prefix.substr(user_end + 1);
        }
    } else {
        nick = prefix;
    }

    space = line.find(' ', pos);
    if (space == std::string::npos) return false;
    pos = space + 1;

    space = line.find(' ', pos);
    if (space != std::string::npos) {
        channel = line.substr(pos, space - pos);
        pos = space + 1;
    } else {
        channel = line.substr(pos);
        if (!channel.empty() && channel[0] == ':') {
            channel = channel.substr(1);
        }
        message.clear();
        return true;
    }

    if (pos < line.size()) {
        message = (line[pos] == ':') ? line.substr(pos + 1) : line.substr(pos);
    }
    return true;
}

bool IrcMessageParser::ParseJoinPartLine(const std::string& line,
                                         std::string& nick,
                                         std::string& user,
                                         std::string& host,
                                         std::string& channel) {
    if (line.empty() || line[0] != ':') return false;

    size_t pos = 1;
    size_t space = line.find(' ', pos);
    if (space == std::string::npos) return false;

    std::string prefix = line.substr(pos, space - pos);
    pos = space + 1;

    size_t nick_end = prefix.find('!');
    if (nick_end != std::string::npos) {
        nick = prefix.substr(0, nick_end);
        size_t user_end = prefix.find('@', nick_end + 1);
        if (user_end != std::string::npos) {
            user = prefix.substr(nick_end + 1, user_end - nick_end - 1);
            host = prefix.substr(user_end + 1);
        }
    } else {
        nick = prefix;
    }

    space = line.find(' ', pos);
    if (space != std::string::npos) {
        std::string rest = line.substr(space + 1);
        channel = (!rest.empty() && rest[0] == ':') ? rest.substr(1) : rest;
    }
    return true;
}

std::string IrcMessageParser::BuildPrivmsgPayload(const std::string& channel,
                                                  const std::string& message) {
    std::ostringstream oss;
    oss << "{";
    oss << "\"cmd\":\"PRIVMSG\",";
    oss << "\"channel\":\"" << EscapeJson(channel) << "\",";
    oss << "\"message\":\"" << EscapeJson(message) << "\",";
    oss << "\"ts\":" << GetCurrentUnixSeconds();
    oss << "}";
    return oss.str();
}

} // namespace blazeclaw::irc