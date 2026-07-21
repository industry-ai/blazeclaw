#include "pch.h"
#include "CChatRoomBridge.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <winhttp.h>

#include "CMgrChannels.h"
#include "ChatMessage.h"
#include "AppProtoHeader.h"
#include "CIrcChatTransport.h"
#include "CNetwork_c.h"
#include "NetworkTimeouts.h"

#pragma comment(lib, "winhttp.lib")

namespace {

// HTTP POST JSON 请求，完全对齐 AIAssistant/JsBridge 的 HttpPostJson 实现
static std::string HttpPostJson(const std::wstring& host, INTERNET_PORT port, const std::wstring& path,
	const std::string& body, DWORD timeoutMs, DWORD& statusCode)
{
	statusCode = 0;
	HINTERNET session = WinHttpOpen(L"BlazeClaw/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session) {
		TRACE(_T("[HttpPostJson] WinHttpOpen failed err=%lu\n"), GetLastError());
		return {};
	}

	HINTERNET connection = nullptr;
	HINTERNET request = nullptr;
	std::string response;
	std::wstring headers = L"Content-Type: application/json\r\nAccept: application/json\r\n";

	try {
		WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
		connection = WinHttpConnect(session, host.c_str(), port, 0);
		if (!connection) {
			TRACE(_T("[HttpPostJson] WinHttpConnect failed host=%s port=%d err=%lu\n"), host.c_str(), port, GetLastError());
			throw std::runtime_error("WinHttpConnect failed");
		}

		request = WinHttpOpenRequest(connection, L"POST", path.c_str(), nullptr,
			WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
		if (!request) {
			TRACE(_T("[HttpPostJson] WinHttpOpenRequest failed err=%lu\n"), GetLastError());
			throw std::runtime_error("WinHttpOpenRequest failed");
		}

		BOOL sendOk = WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(headers.size()),
			(LPVOID)body.data(), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
		if (!sendOk) {
			TRACE(_T("[HttpPostJson] WinHttpSendRequest failed err=%lu\n"), GetLastError());
			throw std::runtime_error("WinHttpSendRequest failed");
		}
		if (!WinHttpReceiveResponse(request, nullptr)) {
			TRACE(_T("[HttpPostJson] WinHttpReceiveResponse failed err=%lu\n"), GetLastError());
			throw std::runtime_error("WinHttpReceiveResponse failed");
		}

		DWORD statusSize = sizeof(statusCode);
		if (!WinHttpQueryHeaders(request,
			WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX,
			&statusCode,
			&statusSize,
			WINHTTP_NO_HEADER_INDEX)) {
			TRACE(_T("[HttpPostJson] WinHttpQueryHeaders failed err=%lu\n"), GetLastError());
			throw std::runtime_error("WinHttpQueryHeaders failed");
		}

		for (;;) {
			DWORD availableSize = 0;
			if (!WinHttpQueryDataAvailable(request, &availableSize)) {
				throw std::runtime_error("WinHttpQueryDataAvailable failed");
			}
			if (availableSize == 0) {
				break;
			}

			std::vector<char> buffer(availableSize + 1, 0);
			DWORD downloaded = 0;
			if (!WinHttpReadData(request, buffer.data(), availableSize, &downloaded)) {
				throw std::runtime_error("WinHttpReadData failed");
			}
			response.append(buffer.data(), downloaded);
		}
	}
	catch (const std::exception& ex) {
		TRACE(_T("[HttpPostJson] exception: %hs\n"), ex.what());
		if (request) WinHttpCloseHandle(request);
		if (connection) WinHttpCloseHandle(connection);
		WinHttpCloseHandle(session);
		throw;
	}

	if (request) WinHttpCloseHandle(request);
	if (connection) WinHttpCloseHandle(connection);
	WinHttpCloseHandle(session);
	return response;
}

} // anonymous namespace

namespace blazeclaw::irc {

uint64_t GetTimestampMs() {
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count());
}

int64_t GetTimestampSeconds() {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

// 应用协议响应由 CConnection_c 按 AppProtoHeader::seq 匹配，并在接收线程调用 callback。
// WebView 的 requestId 由调用方闭包保留，不进入应用协议 payload。
// timeout_cb：如果提供，超时后会被调用（传入 body 用于重发）；如果 timeout_cb 返回 true，则不调用原始 callback。
bool SendAppProtoCommandAsyncWithCb(
    const std::string& command,
    const nlohmann::json& body,
    std::function<void(bool ok, const std::string& payload_or_error)> callback,
    std::function<bool(const std::string& cmd, const nlohmann::json& body)> timeout_cb) {
    nlohmann::json cmdBody = body;
    cmdBody["cmd"] = command;
    cmdBody["ts"] = static_cast<int64_t>(GetTimestampSeconds());
    const std::string payload = cmdBody.dump();

    TRACE(_T("[CChatRoomBridge] SendAppProtoCommandAsyncWithCb: cmd=%hs payload_len=%zu\n"),
          command.c_str(), payload.size());

    if (!CNetwork_c::Instance().IsTcpConnected()) {
        TRACE(_T("[CChatRoomBridge] SendAppProtoCommandAsyncWithCb: TCP not connected\n"));
        if (callback) {
            callback(false, "tcp_not_connected");
        }
        return false;
    }

    struct CbState {
        std::decay_t<decltype(callback)> cb;
        std::atomic<bool> called{ false };
        // 共享的剩余重试次数：每次超时+1次重试会递减，到 0 则彻底失败。
        // 用 shared_ptr 让 SendAppProtoCommandAsyncWithCb 递归调用（timeout_cb 内重试）时
        // 多个 CbState 共享同一个 budget。
        std::shared_ptr<std::atomic<int>> retry_remaining;
    };
    auto cb_state = std::make_shared<CbState>();
    cb_state->cb = std::move(callback);
    cb_state->retry_remaining = std::make_shared<std::atomic<int>>(
        blazeclaw::net::kBridgeMaxRetryCount);

    auto done_cb = [cb_state, command](uint32_t seq, const std::string& resp_payload) {
        TRACE(_T("[CChatRoomBridge] done_callback: cmd=%hs seq=%u payload_len=%zu\n"),
              command.c_str(), seq, resp_payload.size());

        std::string payload_preview = resp_payload.size() <= 500 ? resp_payload
            : resp_payload.substr(0, 500) + "...";
        TRACE(_T("[CChatRoomBridge] done_callback: payload preview: %hs\n"), payload_preview.c_str());

        if (cb_state->cb && !cb_state->called.exchange(true)) {
            cb_state->cb(true, resp_payload);
        }
    };

    const uint32_t seq = CNetwork_c::Instance().SendTcpNoWaitWithCallback(
        static_cast<uint16_t>(MsgType::NodeBindReq), payload, done_cb);

    TRACE(_T("[CChatRoomBridge] SendAppProtoCommandAsyncWithCb: cmd=%hs seq=%u\n"),
          command.c_str(), seq);

    if (seq > 0) {
        // 超时时长走统一常量 kBridgeRequestTimeoutMs（默认 30s），与前端 chatroomBridgeRequest 默认一致。
        // 如果提供了 timeout_cb，超时后调用它进行重试（次数受 kBridgeMaxRetryCount 限制）。
        std::thread([cb_state, seq, command, body, timeout_cb]() {
            const auto timeout_ms = blazeclaw::net::kBridgeRequestTimeoutMs;

            std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
            if (cb_state->called.load()) {
                return;
            }

            int retries_left = cb_state->retry_remaining->load();
            if (retries_left > 0 && timeout_cb) {
                cb_state->retry_remaining->store(retries_left - 1);
                TRACE(_T("[CChatRoomBridge] timeout fired after %ums: cmd=%hs seq=%u retries_left=%d\n"),
                      timeout_ms, command.c_str(), seq, retries_left - 1);
                // timeout_cb 返回 true 表示我们来处理响应（重试本身），此时不调用原始 callback
                if (timeout_cb(command, body)) {
                    cb_state->called.store(true);
                    return;
                }
                // timeout_cb 返回 false（极端情况下）：继续走失败路径
            } else {
                TRACE(_T("[CChatRoomBridge] timeout fired after %ums: cmd=%hs seq=%u retries_exhausted\n"),
                      timeout_ms, command.c_str(), seq);
            }

            // 彻底失败：回调原始 caller。
            try {
                if (cb_state->cb && !cb_state->called.exchange(true)) {
                    cb_state->cb(false, "request_timeout");
                }
            } catch (...) {}
        }).detach();
    }

    return seq > 0;
}

std::string GetCurrentSessionId() {
    std::ostringstream oss;
    oss << "session-" << std::chrono::steady_clock::now().time_since_epoch().count();
    return oss.str();
}

std::string GetCurrentNickname() {
    return "user";
}

std::string IrcPushEventTypeToString(IrcPushEventType type) {
    switch (type) {
        case IrcPushEventType::Unknown: return "unknown";
        case IrcPushEventType::Privmsg: return "privmsg";
        case IrcPushEventType::Join: return "join";
        case IrcPushEventType::Part: return "part";
        case IrcPushEventType::Kick: return "kick";
        case IrcPushEventType::Kicked: return "kicked";
        case IrcPushEventType::Ban: return "ban";
        case IrcPushEventType::Mode: return "mode";
        case IrcPushEventType::Topic: return "topic";
        case IrcPushEventType::Notice: return "notice";
        case IrcPushEventType::Ping: return "ping";
        case IrcPushEventType::Nick: return "nick";
        case IrcPushEventType::Quit: return "quit";
        case IrcPushEventType::Error: return "error";
        case IrcPushEventType::IrcMessageResp: return "irc_message_resp";
        case IrcPushEventType::Names: return "names";
        case IrcPushEventType::EndOfNames: return "end_of_names";
        case IrcPushEventType::GroupInvited: return "group_invited";
        default: return "unknown";
    }
}

std::string JsonEscape(const std::string& value);

void CChatRoomBridge::EmitResponse(const std::string& request_id, bool ok,
                                   const std::string& error, const std::string& payload_json) {
    nlohmann::json response;
    response["channel"] = "chatroom.bridge.response";
    response["requestId"] = request_id;
    response["ok"] = ok;
    if (!error.empty()) {
        response["error"] = nlohmann::json{ { "message", error } };
    }
    if (!payload_json.empty()) {
        try {
            response["payload"] = nlohmann::json::parse(payload_json);
        } catch (...) {
            response["payload"] = nlohmann::json::object();
        }
    } else {
        response["payload"] = nlohmann::json::object();
    }
    if (deps_.emit_to_web) {
        deps_.emit_to_web(response.dump());
    }
}

/**
 * 从本地 CMgrChannels 构造房间信息 JSON（GET_ROOM_INFO 服务端调用失败时的 fallback）
 * 返回空字符串表示本地也找不到该频道。
 */
static std::string BuildRoomInfoFallbackFromLocal(const std::string& channel) {
    auto& mgr = CMgrChannels::Instance();
    auto chan = mgr.GetChannel(channel);
    if (!chan) {
        // 本地也没这个 channel —— 仍然返回最小化 JSON（含 name/room_id + 空 members），
        // 保证前端至少能渲染出房间名，不会空白。
        std::ostringstream oss;
        std::string name = channel;
        if (!name.empty() && name[0] == '#') name = name.substr(1);
        oss << "{";
        oss << "\"channel\":\"" << JsonEscape(channel) << "\",";
        oss << "\"name\":\"" << JsonEscape(name) << "\",";
        oss << "\"room_id\":\"" << JsonEscape(channel) << "\",";
        oss << "\"topic\":\"\",";
        oss << "\"founder\":\"\",";
        oss << "\"member_count\":0,";
        oss << "\"operator_count\":0,";
        oss << "\"mode_flags\":0,";
        oss << "\"user_limit\":0,";
        oss << "\"has_key\":false";
        oss << ",\"members\":[]";
        oss << "}";
        return oss.str();
    }
    const auto modeFlags = static_cast<uint32_t>(chan->GetModes());
    const auto members = chan->GetMemberNicks();
    const auto ops = chan->GetOperators();
    std::ostringstream oss;
    oss << "{";
    oss << "\"channel\":\"" << JsonEscape(chan->GetName()) << "\",";
    oss << "\"name\":\"" << JsonEscape(chan->GetName()) << "\",";
    oss << "\"room_id\":\"" << JsonEscape(chan->GetName()) << "\",";
    oss << "\"topic\":\"" << JsonEscape(chan->GetTopic()) << "\",";
    oss << "\"founder\":\"" << JsonEscape(chan->GetFounderNick()) << "\",";
    oss << "\"member_count\":" << chan->GetMemberCount() << ",";
    oss << "\"operator_count\":" << chan->GetOperatorCount() << ",";
    oss << "\"mode_flags\":" << modeFlags << ",";
    oss << "\"user_limit\":" << chan->GetUserLimit() << ",";
    oss << "\"has_key\":" << (chan->GetKey().empty() ? "false" : "true");
    oss << ",\"members\":[";
    for (size_t i = 0; i < members.size(); ++i) {
        if (i > 0) oss << ",";
        const bool isOp = std::find(ops.begin(), ops.end(), members[i]) != ops.end();
        oss << "{";
        oss << "\"member_kind\":\"user\",";
        oss << "\"user_id\":\"" << JsonEscape(members[i]) << "\",";
        oss << "\"nick\":\"" << JsonEscape(members[i]) << "\",";
        oss << "\"role\":\"" << (isOp ? "operator" : "member") << "\"";
        oss << "}";
    }
    oss << "]";
    oss << "}";
    return oss.str();
}

std::string JsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char ch : value) {
        switch (ch) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(ch));
                    out += buf;
                } else {
                    out.push_back(ch);
                }
        }
    }
    return out;
}

void CChatRoomBridge::Initialize(ChatRoomBridgeDependencies deps,
                                  ChatRoomBridgeConfig config) {
    deps_ = std::move(deps);
    config_ = config;
    initialized_ = true;

    // Wire IRC transport: forward all server push events to the web UI via
    // deps_.emit_to_web. CNetwork_c is shared with the rest of the app and
    // has its own TCP/TLS push receivers; this hook just bridges the IRC layer.
    auto& transport = CIrcChatTransport::Instance();
    transport.Initialize();
    TRACE(_T("[CChatRoomBridge] Initialize: transport initialized, now setting push callback\n"));
    transport.SetPushCallback([this](const IrcPushEvent& event) {
        TRACE(_T("[CChatRoomBridge] Push event RECEIVED: type=%d channel=%s sender=%s\n"),
              static_cast<int>(event.type),
              CA2T(event.channel.substr(0, 50).c_str()),
              CA2T(event.sender_nick.substr(0, 30).c_str()));
        // 诊断输出（避免 TRACE 直接输出 UTF-8 字符串导致乱码）
        {
            std::string type_str = IrcPushEventTypeToString(event.type);
            std::string chan_short = event.channel.size() > 30
                ? event.channel.substr(0, 30) + "..." : event.channel;
            std::string sender_short = event.sender_nick.size() > 30
                ? event.sender_nick.substr(0, 30) + "..." : event.sender_nick;
            std::string msg_short = event.message.size() > 50
                ? event.message.substr(0, 50) + "..." : event.message;
            TRACE(_T("[CChatRoomBridge] Push event: type=%d(%s) channel=%s sender=%s msg=%s\n"),
                  static_cast<int>(event.type), CA2T(type_str.c_str()),
                  CA2T(chan_short.c_str()), CA2T(sender_short.c_str()),
                  CA2T(msg_short.c_str()));
        }

        if (event.type == IrcPushEventType::Unknown) {
            // Unknown 类型通常是服务端返回的非 IRC 格式数据（如 app proto JSON），
            // 打印原始数据以便诊断
            if (!event.raw_line.empty()) {
                std::string raw_short = event.raw_line.size() > 200
                    ? event.raw_line.substr(0, 200) + "..." : event.raw_line;
                TRACE(_T("[CChatRoomBridge] Push event: Unknown raw_line=%s\n"),
                      CA2T(raw_short.c_str()));
            }
            // 不跳过，让上层决定如何处理
        }

        // ── 把 JOIN / NAMES / TOPIC 等 push 同步进本地 CMgrChannels ──
        // 否则 list_room_members / names / get_room_info 走 fallback 时本地没有成员列表。
        if (!event.channel.empty() && event.channel[0] == '#') {
            auto& mgr = CMgrChannels::Instance();
            auto chan = mgr.GetChannel(event.channel);
            if (!chan) {
                // 收到某个频道的第一个 push（JOIN/NAMES/PRIVMSG），先在本地建好频道
                auto nickname = deps_.get_current_nickname ? deps_.get_current_nickname() : GetCurrentNickname();
                auto peer = CPeer::Create(nickname);
                mgr.RegisterMember(peer);
                chan = mgr.CreateChannel(event.channel, peer);
            }
            if (event.type == IrcPushEventType::Join) {
                // 服务端推 JOIN：注册发送者到本地频道
                std::string nick = event.sender_nick;
                if (nick.empty()) nick = event.sender_user;
                if (!nick.empty()) {
                    auto member = mgr.GetMember(nick);
                    if (!member) {
                        member = CPeer::Create(nick);
                        mgr.RegisterMember(member);
                    }
                    chan->AddMember(member);
                }
            } else if (event.type == IrcPushEventType::Part ||
                       event.type == IrcPushEventType::Quit ||
                       event.type == IrcPushEventType::Kick) {
                std::string nick = event.sender_nick;
                if (nick.empty()) nick = event.sender_user;
                if (!nick.empty()) {
                    chan->RemoveMember(nick);
                }
            } else if (event.type == IrcPushEventType::Kicked) {
                // 从本地频道移除被踢的成员（服务端推送，被踢方收到）
                std::string kicked_nick;
                if (!event.kicked_user_id.empty()) {
                    kicked_nick = event.kicked_user_id;
                } else if (event.kicked_node_id != 0) {
                    // 设备被踢：本地 CPeer 目前没有 node_id 字段，仅记录日志
                    TRACE(_T("[CChatRoomBridge] Kicked event: node_id=%d from %s (device, no local record)\n"),
                          event.kicked_node_id, CA2T(event.channel.c_str()));
                }
                if (!kicked_nick.empty()) {
                    chan->RemoveMember(kicked_nick);
                }
            } else if (event.type == IrcPushEventType::Topic) {
                if (!event.message.empty()) {
                    chan->SetTopic(event.message);
                }
            } else if (event.type == IrcPushEventType::Names) {
                // RPL_NAMREPLY (353)：批量加 names 到本地 channel。
                // message 是空格分隔的 nick 列表（可能含 @ / + / % 前缀表示 mode）。
                std::string names_blob = event.message;
                size_t i = 0;
                while (i < names_blob.size()) {
                    while (i < names_blob.size() && (names_blob[i] == ' ' || names_blob[i] == ',')) ++i;
                    std::string nick;
                    while (i < names_blob.size() && names_blob[i] != ' ' && names_blob[i] != ',') {
                        if (names_blob[i] == '@' || names_blob[i] == '+' ||
                            names_blob[i] == '%' || names_blob[i] == '~' ||
                            names_blob[i] == '&') {
                            ++i;
                            continue;
                        }
                        nick.push_back(names_blob[i]);
                        ++i;
                    }
                    if (!nick.empty()) {
                        auto member = mgr.GetMember(nick);
                        if (!member) {
                            member = CPeer::Create(nick);
                            mgr.RegisterMember(member);
                        }
                        chan->AddMember(member);
                    }
                }
            }
        }

        nlohmann::json payload;
        payload["type"] = static_cast<int>(event.type);
        payload["sender"] = event.sender_nick;
        payload["channel"] = event.channel;
        payload["message"] = event.message;
        payload["raw"] = event.raw_line;
        payload["timestamp_ms"] = event.timestamp_ms;

        // 兜底：如果 sender_nick 在 ParseIrcMessage 异常路径下被丢掉
        // （例如 JSON 末尾有非 UTF-8 二进制垃圾导致 nlohmann 静默返回 discarded），
        // 这里从 event.raw_line 二次解析 JSON，从中提取 from / sender 字段，
        // 保证前端拿到的 payload.sender 永远有真实发送者 ID。
        // 这是修复 "群聊只显示 +1 但消息正文/发送者昵称为空" 的关键修复。
        if (payload["sender"].get<std::string>().empty() && !event.raw_line.empty()) {
            try {
                size_t json_end = event.raw_line.find_last_of('}');
                std::string clean_raw = (json_end != std::string::npos)
                    ? event.raw_line.substr(0, json_end + 1)
                    : event.raw_line;
                auto reparsed = nlohmann::json::parse(clean_raw, nullptr, false);
                if (!reparsed.is_discarded() && reparsed.is_object()) {
                    std::string recovered = reparsed.value("from",
                        reparsed.value("from_user",
                        reparsed.value("from_phone",
                        reparsed.value("sender", std::string()))));
                    if (!recovered.empty()) {
                        payload["sender"] = recovered;
                        TRACE(_T("[CChatRoomBridge] sender_nick fallback recovered: %s\n"),
                              CA2T(recovered.c_str()));
                    }
                }
            } catch (...) {
                // raw_line 不是合法 JSON，放弃兜底
            }
        }

        // ── Agent 回复覆写：服务端回显时 from 为当前用户手机号，
        // 前端 isSelfById 会命中导致 AI 消息显示在右侧。这里检测
        // pending_agent_replies_ 中的 "channel:message" 键，命中则覆写 sender。
        if (event.type == IrcPushEventType::Privmsg) {
            std::string key = event.channel + ":" + event.message;
            {
                std::lock_guard<std::mutex> lock(pending_agent_replies_mutex_);
                auto it = pending_agent_replies_.find(key);
                if (it != pending_agent_replies_.end()) {
                    payload["sender"] = "炎图AI助手";
                    pending_agent_replies_.erase(it);
                    TRACE(_T("[CChatRoomBridge] Agent reply matched, sender overridden to 炎图AI助手\n"));
                }
            }
        }

        std::string payload_str = payload.dump();
        TRACE(_T("[CChatRoomBridge] OnNetworkPush: emitting to web\n"));
        OnNetworkPush(IrcPushEventTypeToString(event.type), payload_str);
    });

    transport.SetConnectionStateCallback([this](bool is_tcp, bool is_connected) {
        // CIrcChatTransport 已经在内部 ScheduleAutoReconnect 起了重连线程；
        // 这里把状态变化原样转给 web，同时管理请求队列。
        TRACE(_T("[CChatRoomBridge] ConnectionState: is_tcp=%d is_connected=%d\n"),
              is_tcp, is_connected);

        // TCP 连接状态变化
        if (is_tcp) {
            is_connected_.store(is_connected);
            if (is_connected) {
                // 连接恢复，重试排队的请求
                TRACE(_T("[CChatRoomBridge] Connection restored, retrying pending requests\n"));
                RetryPendingRequests();
            } else {
                // 连接断开，TRACE 记录（请求会在 HandleWebMessage 时进入队列）
                TRACE(_T("[CChatRoomBridge] Connection lost, requests will be queued\n"));
            }
        }
    });

}

void CChatRoomBridge::InitializeWithTransport(ChatRoomBridgeDependencies deps,
                                              ChatRoomBridgeConfig config) {
    deps_ = std::move(deps);
    config_ = config;
    
    // Initialize IRC transport
    auto& transport = CIrcChatTransport::Instance();
    transport.Initialize();

    // Set up push callback to forward events to web
    transport.SetPushCallback([this](const IrcPushEvent& event) {
        TRACE(_T("[CChatRoomBridge] Push event: type=%d channel=%s sender=%s msg=%s\n"),
              static_cast<int>(event.type), CA2T(event.channel.c_str()), 
              CA2T(event.sender_nick.c_str()), CA2T(event.message.substr(0, 100).c_str()));

        if (event.type == IrcPushEventType::Unknown) {
            TRACE(_T("[CChatRoomBridge] Push event: skipped (Unknown type)\n"));
            return;
        }

        // ── 同步 push 事件到本地 CMgrChannels（用于 list_room_members / get_room_info fallback） ──
        if (!event.channel.empty() && event.channel[0] == '#') {
            auto& mgr = CMgrChannels::Instance();
            auto chan = mgr.GetChannel(event.channel);
            if (!chan) {
                auto nickname = deps_.get_current_nickname ? deps_.get_current_nickname() : GetCurrentNickname();
                auto peer = CPeer::Create(nickname);
                mgr.RegisterMember(peer);
                chan = mgr.CreateChannel(event.channel, peer);
            }
            if (event.type == IrcPushEventType::Join) {
                std::string nick = event.sender_nick;
                if (nick.empty()) nick = event.sender_user;
                if (!nick.empty()) {
                    auto member = mgr.GetMember(nick);
                    if (!member) {
                        member = CPeer::Create(nick);
                        mgr.RegisterMember(member);
                    }
                    chan->AddMember(member);
                }
            } else if (event.type == IrcPushEventType::Part ||
                       event.type == IrcPushEventType::Quit ||
                       event.type == IrcPushEventType::Kick) {
                std::string nick = event.sender_nick;
                if (nick.empty()) nick = event.sender_user;
                if (!nick.empty()) {
                    chan->RemoveMember(nick);
                }
            } else if (event.type == IrcPushEventType::Kicked) {
                std::string kicked_nick;
                if (!event.kicked_user_id.empty()) {
                    kicked_nick = event.kicked_user_id;
                } else if (event.kicked_node_id != 0) {
                    // 设备被踢：本地 CPeer 目前没有 node_id 字段，仅记录日志
                    TRACE(_T("[CChatRoomBridge] Kicked event: node_id=%d from %s (device, no local record)\n"),
                          event.kicked_node_id, CA2T(event.channel.c_str()));
                }
                if (!kicked_nick.empty()) {
                    chan->RemoveMember(kicked_nick);
                }
            } else if (event.type == IrcPushEventType::Topic) {
                if (!event.message.empty()) {
                    chan->SetTopic(event.message);
                }
            } else if (event.type == IrcPushEventType::Names) {
                // RPL_NAMREPLY (353)：批量加 names 到本地 channel。
                // message 是空格分隔的 nick 列表（可能含 @ / + / % 前缀表示 mode）。
                std::string names_blob = event.message;
                size_t i = 0;
                while (i < names_blob.size()) {
                    while (i < names_blob.size() && (names_blob[i] == ' ' || names_blob[i] == ',')) ++i;
                    std::string nick;
                    while (i < names_blob.size() && names_blob[i] != ' ' && names_blob[i] != ',') {
                        if (names_blob[i] == '@' || names_blob[i] == '+' ||
                            names_blob[i] == '%' || names_blob[i] == '~' ||
                            names_blob[i] == '&') {
                            ++i;
                            continue;
                        }
                        nick.push_back(names_blob[i]);
                        ++i;
                    }
                    if (!nick.empty()) {
                        auto member = mgr.GetMember(nick);
                        if (!member) {
                            member = CPeer::Create(nick);
                            mgr.RegisterMember(member);
                        }
                        chan->AddMember(member);
                    }
                }
            }
        }

        nlohmann::json payload;
        payload["type"] = static_cast<int>(event.type);
        payload["sender"] = event.sender_nick;
        payload["channel"] = event.channel;
        payload["message"] = event.message;
        payload["raw"] = event.raw_line;
        payload["timestamp_ms"] = event.timestamp_ms;

        // 兜底：如果 sender_nick 在 ParseIrcMessage 异常路径下被丢掉
        // （例如 JSON 末尾有非 UTF-8 二进制垃圾导致 nlohmann 静默返回 discarded），
        // 这里从 event.raw_line 二次解析 JSON，从中提取 from / sender 字段，
        // 保证前端拿到的 payload.sender 永远有真实发送者 ID。
        // 这是修复 "群聊只显示 +1 但消息正文/发送者昵称为空" 的关键修复。
        if (payload["sender"].get<std::string>().empty() && !event.raw_line.empty()) {
            try {
                size_t json_end = event.raw_line.find_last_of('}');
                std::string clean_raw = (json_end != std::string::npos)
                    ? event.raw_line.substr(0, json_end + 1)
                    : event.raw_line;
                auto reparsed = nlohmann::json::parse(clean_raw, nullptr, false);
                if (!reparsed.is_discarded() && reparsed.is_object()) {
                    std::string recovered = reparsed.value("from",
                        reparsed.value("from_user",
                        reparsed.value("from_phone",
                        reparsed.value("sender", std::string()))));
                    if (!recovered.empty()) {
                        payload["sender"] = recovered;
                        TRACE(_T("[CChatRoomBridge] sender_nick fallback recovered: %s\n"),
                              CA2T(recovered.c_str()));
                    }
                }
            } catch (...) {
                // raw_line 不是合法 JSON，放弃兜底
            }
        }

        // ── Agent 回复覆写：服务端回显时 from 为当前用户手机号，
        // 前端 isSelfById 会命中导致 AI 消息显示在右侧。这里检测
        // pending_agent_replies_ 中的 "channel:message" 键，命中则覆写 sender。
        if (event.type == IrcPushEventType::Privmsg) {
            std::string key = event.channel + ":" + event.message;
            {
                std::lock_guard<std::mutex> lock(pending_agent_replies_mutex_);
                auto it = pending_agent_replies_.find(key);
                if (it != pending_agent_replies_.end()) {
                    payload["sender"] = "炎图AI助手";
                    pending_agent_replies_.erase(it);
                    TRACE(_T("[CChatRoomBridge] Agent reply matched, sender overridden to 炎图AI助手\n"));
                }
            }
        }

        std::string payload_str = payload.dump();
        TRACE(_T("[CChatRoomBridge] OnNetworkPush: emitting to web\n"));
        OnNetworkPush(IrcPushEventTypeToString(event.type), payload_str);
    });

    transport.SetConnectionStateCallback([this](bool is_tcp, bool is_connected) {
        // 同 Initialize —— transport 内部已自动重连，这里管理请求队列和重试。
        TRACE(_T("[CChatRoomBridge] InitWithTransport ConnectionState: is_tcp=%d is_connected=%d\n"),
              is_tcp, is_connected);

        // TCP 连接状态变化
        if (is_tcp) {
            is_connected_.store(is_connected);
            if (is_connected) {
                // 连接恢复，重试排队的请求
                TRACE(_T("[CChatRoomBridge] Connection restored, retrying pending requests\n"));
                RetryPendingRequests();
            } else {
                // 连接断开
                TRACE(_T("[CChatRoomBridge] Connection lost, requests will be queued\n"));
            }
        }
    });
}

void CChatRoomBridge::Shutdown() {
    CIrcChatTransport::Instance().Shutdown();
    initialized_ = false;
}

void CChatRoomBridge::HandleWebMessageAsync(const BridgeRequest& req) {
    if (!initialized_) {
        TRACE(_T("[CChatRoomBridge] HandleWebMessageAsync: bridge not initialized\n"));
        nlohmann::json response;
        response["channel"] = "chatroom.bridge.response";
        response["requestId"] = req.request_id;
        response["ok"] = false;
        response["error"] = nlohmann::json{ { "message", "bridge_not_initialized" } };
        response["payload"] = nlohmann::json::object();
        if (deps_.emit_to_web) {
            deps_.emit_to_web(response.dump());
        }
        return;
    }

    ++requests_received_;

    // TCP 连接断开时，将需要网络往返的命令加入队列
    // 连接恢复后会自动重试（见 RetryPendingRequests）
    auto needs_network_roundtrip = [](const std::string& kind) -> bool {
        return kind == "list_conversations" ||
               kind == "create_conversation" ||
               kind == "join_channel" ||
               kind == "part_channel" || kind == "kick_member" ||
               kind == "ban_member" || kind == "set_topic" ||
               kind == "get_room_info" ||
               kind == "list_room_members" || kind == "invite_member" ||
               kind == "remove_member" || kind == "create_topic" ||
               kind == "reply_topic" || kind == "list_topics" ||
               kind == "close_topic" || kind == "create_post" ||
               kind == "list_posts" || kind == "update_post" ||
               kind == "close_post" || kind == "respond_to_post" ||
               kind == "send_message" || kind == "send_prompt" ||
               kind == "list_personal_tasks" || kind == "create_personal_task" ||
               kind == "set_task_status" || kind == "reschedule_task";
    };

    if (!is_connected_.load() && needs_network_roundtrip(req.kind)) {
        // 连接断开，将请求加入队列
        {
            std::lock_guard<std::mutex> lock(pending_requests_queue_mutex_);
            pending_requests_queue_.push_back(req);
        }
        TRACE(_T("[CChatRoomBridge] Connection lost, queued request: kind=%s requestId=%s queue_size=%d\n"),
              CA2T(req.kind.c_str()), CA2T(req.request_id.c_str()),
              static_cast<int>(pending_requests_queue_.size()));

        // 不发送响应 —— 重试成功后才会通过 callback 发送响应
        return;
    }

    // join_channel 走 221 (IrcMessageReq) 通道 —— 与 PRIVMSG/PART/MODE/TOPIC 等 IRC
    // 命令的协议类型保持一致。挂 seq 等响应，超时后回退本地 CMgrChannels 注册。
    if (req.kind == "join_channel") {
        HandleJoinChannelViaIrc(req);
        return;
    }

    // send_message 走 221 (IrcMessageReq) 通道 —— 与 PRIVMSG 命令的协议类型保持一致。
    // 挂 seq 等 222 ack 确认消息已被服务端接收，30s 超时降级为 fire-and-forget。
    // Why：直接 fire-and-forget 无法区分"发送成功"和"发送失败但上层不知道"，
    // 服务端确认机制提供可靠的端到端交付保证（对齐 §17.1）。
    if (req.kind == "send_message") {
        HandleSendMessageViaIrc(req);
        return;
    }

    // Commands that need server response → async with callback
    if (req.kind == "list_conversations" ||
        req.kind == "create_conversation" ||
        req.kind == "part_channel" || req.kind == "kick_member" ||
        req.kind == "ban_member" || req.kind == "set_topic" ||
        req.kind == "get_room_info" ||
        req.kind == "list_room_members" || req.kind == "invite_member" ||
        req.kind == "remove_member" || req.kind == "create_topic" ||
        req.kind == "reply_topic" || req.kind == "list_topics" ||
        req.kind == "close_topic" || req.kind == "create_post" ||
        req.kind == "list_posts" || req.kind == "update_post" ||
        req.kind == "close_post" || req.kind == "respond_to_post") {

        // Parse payload
        nlohmann::json payload;
        try {
            if (!req.payload_json.empty()) {
                payload = nlohmann::json::parse(req.payload_json);
            }
        } catch (...) {}

        // GET_ROOM_INFO / LIST_ROOM_MEMBERS 服务端契约使用 room_id 字段（对齐 chat-bridge.mjs），
        // 同时兼容早期前端传来的 channel 字段。channel_for_fallback 用于 30s 超时 fallback。
        std::string room_id;
        std::string channel_for_fallback;
        if (payload.is_object()) {
            if (payload.contains("room_id") && payload["room_id"].is_string()) {
                room_id = payload["room_id"].get<std::string>();
            }
            if (room_id.empty() && payload.contains("channel") && payload["channel"].is_string()) {
                room_id = payload["channel"].get<std::string>();
            }
            channel_for_fallback = room_id;
        }
        if (!channel_for_fallback.empty() && channel_for_fallback[0] != '#') {
            channel_for_fallback = "#" + channel_for_fallback;
        }
        if (!room_id.empty() && room_id[0] != '#') {
            room_id = "#" + room_id;
        }

        std::string command = req.kind;
        if (req.kind == "create_conversation") command = "CREATE_CONVERSATION";
        else if (req.kind == "get_room_info") command = "GET_ROOM_INFO";
        else if (req.kind == "list_room_members") command = "LIST_ROOM_MEMBERS";
        else if (req.kind == "part_channel") command = "LEAVE";
        else if (req.kind == "list_posts") command = "CONVERSATION_POST_LIST";
        else if (req.kind == "create_post") command = "CONVERSATION_POST_CREATE";
        else if (req.kind == "update_post") command = "CONVERSATION_POST_UPDATE";
        else if (req.kind == "close_post") command = "CONVERSATION_POST_CLOSE";
        else if (req.kind == "respond_to_post") command = "CONVERSATION_POST_RESPONSE";
        else if (req.kind == "invite_member") command = "INVITE_MEMBER";
        else if (req.kind == "remove_member") command = "REMOVE_MEMBER";
        else if (req.kind == "list_conversations") command = "LIST_MY_CHAT_ROOMS";
        // 个人任务命令（对齐 chat-bridge.mjs 中的 PERSONAL_TASK_* 命令）
        else if (req.kind == "list_personal_tasks") command = "PERSONAL_TASK_LIST";
        else if (req.kind == "create_personal_task") command = "PERSONAL_TASK_CREATE";
        else if (req.kind == "set_task_status") command = "PERSONAL_TASK_SET_STATUS";
        else if (req.kind == "reschedule_task") command = "PERSONAL_TASK_RESCHEDULE";

        // 房间信息/成员查询的 payload 必须带 room_id，
        nlohmann::json cmd_payload = payload.is_object() ? payload : nlohmann::json::object();
        if (req.kind == "get_room_info" || req.kind == "list_room_members") {
            cmd_payload = nlohmann::json::object();
            cmd_payload["room_id"] = room_id;
        }
        // 个人任务命令：前端发送 snake_case，服务端期望 camelCase，需要转换
        else if (req.kind == "create_personal_task") {
            nlohmann::json converted;
            converted["id"] = cmd_payload.value("id", "");
            converted["ownerUserId"] = cmd_payload.value("owner_user_id", "");
            converted["creatorUserId"] = cmd_payload.value("creator_user_id", cmd_payload.value("owner_user_id", ""));
            converted["title"] = cmd_payload.value("title", "新任务");
            converted["summary"] = cmd_payload.value("summary", "");
            converted["sourceConversationId"] = cmd_payload.value("source_conversation_id", "");
            converted["createdFromMessageId"] = cmd_payload.value("created_from_message_id", "");
            converted["dueAt"] = cmd_payload.value("due_at", "");
            converted["status"] = cmd_payload.value("status", "pending");
            converted["deliveryTargetJson"] = cmd_payload.value("delivery_target_json", "");
            converted["reminderId"] = cmd_payload.value("reminder_id", "");
            converted["cronId"] = cmd_payload.value("cron_id", "");
            cmd_payload = std::move(converted);
        }
        else if (req.kind == "set_task_status") {
            nlohmann::json converted;
            converted["personalTaskId"] = cmd_payload.value("personalTaskId", cmd_payload.value("id", ""));
            converted["ownerUserId"] = cmd_payload.value("owner_user_id", "");
            converted["status"] = cmd_payload.value("status", "");
            cmd_payload = std::move(converted);
        }
        else if (req.kind == "reschedule_task") {
            nlohmann::json converted;
            converted["personalTaskId"] = cmd_payload.value("personalTaskId", cmd_payload.value("id", ""));
            converted["ownerUserId"] = cmd_payload.value("owner_user_id", "");
            converted["dueTime"] = cmd_payload.value("due_time", "");
            cmd_payload = std::move(converted);
        }
        // create_post：deadlineAt 需要转成字符串
        else if (req.kind == "create_post") {
            nlohmann::json converted;
            converted["id"] = cmd_payload.value("id", "");
            converted["conversationId"] = cmd_payload.value("conversationId", cmd_payload.value("channel", ""));
            converted["title"] = cmd_payload.value("title", "");
            converted["summary"] = cmd_payload.value("summary", "");
            converted["taskKind"] = cmd_payload.value("taskKind", "notice");
            converted["actionType"] = cmd_payload.value("actionType", "read");
            converted["resourceType"] = cmd_payload.value("resourceType", "none");
            converted["resourceUrl"] = cmd_payload.value("resourceUrl", "");
            // deadlineAt 必须是字符串（服务端 JSON schema 要求 string）
            if (cmd_payload.contains("deadlineAt")) {
                if (cmd_payload["deadlineAt"].is_number()) {
                    converted["deadlineAt"] = std::to_string(cmd_payload["deadlineAt"].get<int64_t>());
                } else {
                    converted["deadlineAt"] = cmd_payload.value("deadlineAt", "");
                }
            } else {
                converted["deadlineAt"] = "";
            }
            converted["createdById"] = cmd_payload.value("createdById", "");
            converted["createdByName"] = cmd_payload.value("createdByName", "");
            cmd_payload = std::move(converted);
            TRACE(_T("[CChatRoomBridge] create_post payload: %hs\n"), cmd_payload.dump().c_str());
        }
        // invite_member：前端发送 target 字段，服务端根据 member_kind 期望 phone / user_id / node_id
        else if (req.kind == "invite_member") {
            nlohmann::json converted;
            converted["room_id"] = cmd_payload.value("room_id", cmd_payload.value("channel", ""));
            converted["member_kind"] = cmd_payload.value("member_kind", "user");
            converted["role"] = cmd_payload.value("role", "member");
            const std::string member_kind = cmd_payload.value("member_kind", "user");
            const std::string target = cmd_payload.value("target", "");
            if (member_kind == "phone") {
                converted["phone"] = target;
            } else if (member_kind == "node") {
                converted["node_id"] = target;
            } else {
                converted["user_id"] = target;
            }
            cmd_payload = std::move(converted);
        }
        // remove_member：前端发送 target 字段，服务端根据 member_kind 期望 user_id / node_id
        else if (req.kind == "remove_member") {
            nlohmann::json converted;
            converted["room_id"] = cmd_payload.value("room_id", cmd_payload.value("channel", ""));
            converted["member_kind"] = cmd_payload.value("member_kind", "user");
            const std::string member_kind = cmd_payload.value("member_kind", "user");
            const std::string target = cmd_payload.value("target", "");
            if (member_kind == "node") {
                converted["node_id"] = target;
            } else {
                converted["user_id"] = target;
            }
            cmd_payload = std::move(converted);
        }

        const std::string request_id = req.request_id;
        std::string requested_name;
        std::string requested_channel;
        try {
            if (!req.payload_json.empty()) {
                auto p = nlohmann::json::parse(req.payload_json);
                requested_name = p.value("name", "");
                requested_channel = p.value("channel", "");
                if (requested_channel.empty()) {
                    requested_channel = p.value("room_id", "");
                }
            }
        } catch (...) {}
        std::string fallback_name = requested_name.empty()
            ? (requested_channel.empty()
                ? std::string("#new-channel")
                : (requested_channel[0] == '#'
                    ? requested_channel
                    : "#" + requested_channel))
            : (requested_name[0] == '#'
                ? requested_name
                : "#" + requested_name);

        // 是否需要超时重试的命令（get_room_info / list_room_members）
        bool needs_retry = (req.kind == "get_room_info" || req.kind == "list_room_members");

        // 超时重试回调：重新发送请求，重试响应会调用原始 callback
        std::function<bool(const std::string&, const nlohmann::json&)> timeout_cb;
        std::shared_ptr<std::function<void(bool, const std::string&)>> retry_callback_holder;
        if (needs_retry) {
            // 保存原始 callback 的状态指针用于重试
            retry_callback_holder = std::make_shared<std::function<void(bool, const std::string&)>>(
                [this, request_id, kind = req.kind, channel_for_fallback, fallback_name](bool ok, const std::string& payload_or_error) {
                    nlohmann::json response;
                    response["channel"] = "chatroom.bridge.response";
                    response["requestId"] = request_id;
                    std::string final_payload = payload_or_error;

                    if (!ok) {
                        if (kind == "create_conversation") {
                            nlohmann::json local;
                            local["channel"] = fallback_name;
                            local["fallback"] = true;
                            final_payload = local.dump();
                            ok = true;
                        } else if (!channel_for_fallback.empty()) {
                            if (kind == "get_room_info" || kind == "list_room_members") {
                                std::string local = BuildRoomInfoFallbackFromLocal(channel_for_fallback);
                                if (!local.empty()) {
                                    final_payload = local;
                                    ok = true;
                                }
                            }
                            if (!ok && kind == "list_posts") {
                                nlohmann::json empty;
                                empty["ok"] = true;
                                empty["posts"] = nlohmann::json::array();
                                empty["conversationId"] = channel_for_fallback;
                                final_payload = empty.dump();
                                ok = true;
                            }
                        }
                        if (!ok) {
                            response["ok"] = false;
                            response["error"] = nlohmann::json{ { "message", payload_or_error } };
                        } else {
                            response["ok"] = true;
                        }
                    } else {
                        response["ok"] = true;
                    }

                    if (ok && !final_payload.empty()) {
                        try {
                            auto parsed = nlohmann::json::parse(final_payload);
                            if (kind == "get_room_info") {
                                if (parsed.contains("room_info") && parsed["room_info"].is_object()) {
                                    nlohmann::json room_info = parsed["room_info"];
                                    parsed = std::move(room_info);
                                } else if (parsed.contains("info") && parsed["info"].is_object()) {
                                    nlohmann::json room_info = parsed["info"];
                                    parsed = std::move(room_info);
                                }
                            } else if (kind == "list_room_members" &&
                                       !parsed.contains("members") &&
                                       parsed.contains("room_members") &&
                                       parsed["room_members"].is_array()) {
                                parsed["members"] = std::move(parsed["room_members"]);
                                parsed.erase("room_members");
                            }
                            if (kind == "list_conversations" && parsed.contains("rooms")) {
                                parsed["conversations"] = std::move(parsed["rooms"]);
                                parsed.erase("rooms");
                            }
                            if (kind == "list_room_members" && !parsed.contains("members")) {
                                parsed["members"] = nlohmann::json::array();
                            }
                            if ((kind == "get_room_info" || kind == "list_room_members") &&
                                !channel_for_fallback.empty()) {
                                bool need_merge = false;
                                if (!parsed.contains("members")) {
                                    need_merge = true;
                                } else if (parsed["members"].is_array() && parsed["members"].empty()) {
                                    need_merge = true;
                                }
                                if (need_merge) {
                                    std::string local = BuildRoomInfoFallbackFromLocal(channel_for_fallback);
                                    if (!local.empty()) {
                                        try {
                                            auto local_parsed = nlohmann::json::parse(local);
                                            if (local_parsed.contains("members") &&
                                                local_parsed["members"].is_array() &&
                                                !local_parsed["members"].empty()) {
                                                parsed["members"] = local_parsed["members"];
                                            }
                                            if (!parsed.contains("name") && local_parsed.contains("name")) {
                                                parsed["name"] = local_parsed["name"];
                                            }
                                            if (!parsed.contains("topic") && local_parsed.contains("topic")) {
                                                parsed["topic"] = local_parsed["topic"];
                                            }
                                            if (!parsed.contains("founder") && local_parsed.contains("founder")) {
                                                parsed["founder"] = local_parsed["founder"];
                                            }
                                            if (!parsed.contains("room_id") && local_parsed.contains("room_id")) {
                                                parsed["room_id"] = local_parsed["room_id"];
                                            }
                                            if (!parsed.contains("channel") && local_parsed.contains("channel")) {
                                                parsed["channel"] = local_parsed["channel"];
                                            }
                                        } catch (...) {}
                                    }
                                }
                            }
                            response["payload"] = parsed;
                        } catch (...) {
                            response["payload"] = nlohmann::json::object();
                        }
                    }

                    if (deps_.emit_to_web) {
                        deps_.emit_to_web(response.dump());
                    }
                    ++requests_handled_;
                });

            timeout_cb = [retry_callback_holder](const std::string& cmd, const nlohmann::json& body) -> bool {
                TRACE(_T("[CChatRoomBridge] timeout_cb: retrying cmd=%hs\n"), cmd.c_str());
                SendAppProtoCommandAsyncWithCb(cmd, body, *retry_callback_holder, nullptr);
                return true; // 返回 true 表示我们来处理响应，原始 callback 会被跳过
            };
        }

        // 传递 callback（第三个参数）和 timeout_cb（第四个参数）
        SendAppProtoCommandAsyncWithCb(command, cmd_payload,
            [this, request_id, kind = req.kind, channel_for_fallback, fallback_name](bool ok, const std::string& payload_or_error) {
                nlohmann::json response;
                response["channel"] = "chatroom.bridge.response";
                response["requestId"] = request_id;
                std::string final_payload = payload_or_error;

                if (!ok) {
                    if (kind == "create_conversation") {
                        // CREATE_CONVERSATION 服务端无响应：本地降级创建 channel
                        nlohmann::json local;
                        local["channel"] = fallback_name;
                        local["fallback"] = true;
                        final_payload = local.dump();
                        ok = true;
                    } else if (!channel_for_fallback.empty()) {
                        if (kind == "get_room_info" || kind == "list_room_members") {
                            std::string local = BuildRoomInfoFallbackFromLocal(channel_for_fallback);
                            if (!local.empty()) {
                                final_payload = local;
                                ok = true;
                            }
                        } else if (!ok && kind == "list_posts") {
                            nlohmann::json empty;
                            empty["ok"] = true;
                            empty["posts"] = nlohmann::json::array();
                            empty["conversationId"] = channel_for_fallback;
                            final_payload = empty.dump();
                            ok = true;
                        }
                    }
                    if (!ok) {
                        response["ok"] = false;
                        response["error"] = nlohmann::json{ { "message", payload_or_error } };
                    } else {
                        response["ok"] = true;
                    }
                } else {
                    response["ok"] = true;
                }

                // create_conversation：在本地 CMgrChannels 注册 channel（不管服务端 ok 还是 fallback），
                // 确保后续 get_room_info/list_room_members 能找到 channel。
                if (ok && kind == "create_conversation") {
                    std::string channel_name = fallback_name;
                    if (!final_payload.empty()) {
                        try {
                            auto parsed = nlohmann::json::parse(final_payload, nullptr, false);
                            if (!parsed.is_discarded()) {
                                if (parsed.contains("conversation")) {
                                    const auto& cv = parsed["conversation"];
                                    if (cv.is_string()) {
                                        channel_name = cv.get<std::string>();
                                    } else if (cv.is_object()) {
                                        channel_name = cv.value("id", channel_name);
                                    }
                                }
                                if ((channel_name.empty() || channel_name == fallback_name) &&
                                    parsed.contains("channel") &&
                                    parsed["channel"].is_string()) {
                                    channel_name = parsed["channel"].get<std::string>();
                                }
                            }
                        } catch (...) {}
                    }
                    if (channel_name.empty()) channel_name = fallback_name;
                    if (channel_name[0] != '#') channel_name = "#" + channel_name;

                    auto& mgr = CMgrChannels::Instance();
                    std::string nickname = deps_.get_current_nickname ? deps_.get_current_nickname() : GetCurrentNickname();
                    auto peer = mgr.GetMember(nickname);
                    if (!peer) {
                        peer = CPeer::Create(nickname);
                        mgr.RegisterMember(peer);
                    }
                    if (!mgr.GetChannel(channel_name)) {
                        mgr.CreateChannel(channel_name, peer);
                    }

                    nlohmann::json out;
                    out["channel"] = channel_name;
                    final_payload = out.dump();
                }

                if (ok && !final_payload.empty()) {
                    try {
                        auto parsed = nlohmann::json::parse(final_payload);

                        // 230 房间管理响应可能带业务信封；WebView API 需要直接拿到房间对象。
                        if (kind == "get_room_info") {
                            if (parsed.contains("room_info") && parsed["room_info"].is_object()) {
                                nlohmann::json room_info = parsed["room_info"];
                                parsed = std::move(room_info);
                            } else if (parsed.contains("info") && parsed["info"].is_object()) {
                                nlohmann::json room_info = parsed["info"];
                                parsed = std::move(room_info);
                            }
                        } else if (kind == "list_room_members" &&
                                   !parsed.contains("members") &&
                                   parsed.contains("room_members") &&
                                   parsed["room_members"].is_array()) {
                            parsed["members"] = std::move(parsed["room_members"]);
                            parsed.erase("room_members");
                        }

                        // 前端 chatApi.js 期望 conversations 字段，但服务器返回的是 rooms
                        if (kind == "list_conversations" && parsed.contains("rooms")) {
                            parsed["conversations"] = std::move(parsed["rooms"]);
                            parsed.erase("rooms");
                        }
                        // list_conversations：把每个房间缓存到本地 CMgrChannels，
                        // 让后续 get_room_info fallback 至少能找到 channel 拿到基本信息。
                        if (kind == "list_conversations" && parsed.contains("conversations")) {
                            auto& mgr = CMgrChannels::Instance();
                            std::string nickname = deps_.get_current_nickname ? deps_.get_current_nickname() : GetCurrentNickname();
                            auto peer = mgr.GetMember(nickname);
                            if (!peer) {
                                peer = CPeer::Create(nickname);
                                mgr.RegisterMember(peer);
                            }
                            const auto& rooms = parsed["conversations"];
                            if (rooms.is_array()) {
                                for (const auto& r : rooms) {
                                    std::string rid;
                                    if (r.is_string()) {
                                        rid = r.get<std::string>();
                                    } else if (r.is_object()) {
                                        rid = r.value("id", std::string());
                                        if (rid.empty()) rid = r.value("room_id", std::string());
                                        if (rid.empty()) rid = r.value("roomId", std::string());
                                        if (rid.empty()) rid = r.value("conversation_id", std::string());
                                        if (rid.empty()) rid = r.value("conversationId", std::string());
                                        if (rid.empty()) rid = r.value("channel", std::string());
                                    }
                                    if (rid.empty()) continue;
                                    if (rid[0] != '#') rid = "#" + rid;
                                    if (!mgr.GetChannel(rid)) {
                                        mgr.CreateChannel(rid, peer);
                                    }
                                }
                            }
                        }
                        // 调试：打印 GET_ROOM_INFO 解包后的响应
                        if (kind == "get_room_info") {
                            TRACE(_T("[CChatRoomBridge] get_room_info response: %hs\n"), parsed.dump().c_str());
                        }
                        // list_room_members 标准化：确保 members 字段存在
                        if (kind == "list_room_members" && !parsed.contains("members")) {
                            parsed["members"] = nlohmann::json::array();
                        }
                        // ── 关键修复：服务端响应数据不完整时，merge 本地 CMgrChannels fallback ──
                        // 服务端 LIST_MY_CHAT_ROOMS 返回的 rooms 不含 members；
                        // GET_ROOM_INFO 在某些环境下返回空对象；
                        // LIST_ROOM_MEMBERS 可能根本没注册到服务端。
                        // 一律 merge 本地 channel 的 members，确保前端能渲染群成员列表。
                        if ((kind == "get_room_info" || kind == "list_room_members") &&
                            !channel_for_fallback.empty()) {
                            bool need_merge = false;
                            if (!parsed.contains("members")) {
                                need_merge = true;
                            } else if (parsed["members"].is_array() &&
                                       parsed["members"].empty()) {
                                need_merge = true;
                            }
                            if (need_merge) {
                                std::string local = BuildRoomInfoFallbackFromLocal(channel_for_fallback);
                                if (!local.empty()) {
                                    try {
                                        auto local_parsed = nlohmann::json::parse(local);
                                        if (local_parsed.contains("members") &&
                                            local_parsed["members"].is_array() &&
                                            !local_parsed["members"].empty()) {
                                            parsed["members"] = local_parsed["members"];
                                            TRACE(_T("[CChatRoomBridge] %hs merged local members count=%zu\n"),
                                                  CA2T(kind.c_str()), local_parsed["members"].size());
                                        }
                                        // 也补上其他本地有但服务端没有的字段
                                        if (!parsed.contains("name") && local_parsed.contains("name")) {
                                            parsed["name"] = local_parsed["name"];
                                        }
                                        if (!parsed.contains("topic") && local_parsed.contains("topic")) {
                                            parsed["topic"] = local_parsed["topic"];
                                        }
                                        if (!parsed.contains("founder") && local_parsed.contains("founder")) {
                                            parsed["founder"] = local_parsed["founder"];
                                        }
                                        if (!parsed.contains("room_id") && local_parsed.contains("room_id")) {
                                            parsed["room_id"] = local_parsed["room_id"];
                                        }
                                        if (!parsed.contains("channel") && local_parsed.contains("channel")) {
                                            parsed["channel"] = local_parsed["channel"];
                                        }
                                    } catch (...) {}
                                }
                            }
                        }
                        // join_channel：在本地 CMgrChannels 注册 channel，确保后续查询能找到
                        if (ok && !channel_for_fallback.empty()) {
                            auto& mgr = CMgrChannels::Instance();
                            std::string nickname = deps_.get_current_nickname ? deps_.get_current_nickname() : GetCurrentNickname();
                            std::string session_id = deps_.get_current_session_id ? deps_.get_current_session_id() : GetCurrentSessionId();
                            auto peer = mgr.GetMember(nickname);
                            if (!peer) {
                                peer = CPeer::Create(nickname);
                                mgr.RegisterMember(peer);
                            }
                            if (!mgr.GetChannel(channel_for_fallback)) {
                                mgr.CreateChannel(channel_for_fallback, peer);
                            }
                            mgr.Join(peer, channel_for_fallback);
                            TrackJoinedChannel(session_id, channel_for_fallback);
                            if (response["payload"].is_object() && !response["payload"].contains("channel")) {
                                response["payload"]["channel"] = channel_for_fallback;
                                response["payload"]["joined"] = true;
                            }
                        }
                        response["payload"] = parsed;
                    } catch (...) {
                        response["payload"] = nlohmann::json::object();
                    }
                } else {
                    response["payload"] = nlohmann::json::object();
                }
                if (deps_.emit_to_web) {
                    deps_.emit_to_web(response.dump());
                }
                if (response["ok"].get<bool>()) {
                    ++requests_handled_;
                } else {
                    ++requests_failed_;
                }
            },
            timeout_cb ? timeout_cb : nullptr);
        return;
    }

    // Commands that are fully local (no server round-trip) → sync
    if (req.kind == "send_prompt" || req.kind == "set_mode" ||
        req.kind == "promote_operator" || req.kind == "demote_operator" ||
        req.kind == "whois" || req.kind == "names") {
        BridgeResponse resp;
        resp.request_id = req.request_id;
        resp.timestamp_ms = GetTimestampMs();
        bool success = DispatchRequest(req, resp);

        nlohmann::json response;
        response["channel"] = "chatroom.bridge.response";
        response["requestId"] = req.request_id;
        response["ok"] = success;
        if (!resp.error_message.empty()) {
            response["error"] = nlohmann::json{ { "message", resp.error_message } };
        }
        if (!resp.payload_json.empty()) {
            try {
                auto parsed = nlohmann::json::parse(resp.payload_json);
                response["payload"] = parsed;
            } catch (...) {
                response["payload"] = nlohmann::json::object();
            }
        } else {
            response["payload"] = nlohmann::json::object();
        }
        if (deps_.emit_to_web) {
            deps_.emit_to_web(response.dump());
        }
        if (success) {
            ++requests_handled_;
        } else {
            ++requests_failed_;
        }
        return;
    }

    // Unknown kind → immediate error
    nlohmann::json response;
    response["channel"] = "chatroom.bridge.response";
    response["requestId"] = req.request_id;
    response["ok"] = false;
    response["error"] = nlohmann::json{ { "message", "unsupported_chatroom_kind" } };
    response["payload"] = nlohmann::json::object();
    if (deps_.emit_to_web) {
        deps_.emit_to_web(response.dump());
    }
    ++requests_failed_;
}

bool CChatRoomBridge::HandleWebMessage(const BridgeRequest& req, BridgeResponse& resp) {
    if (!initialized_) {
        resp.ok = false;
        resp.error_message = "bridge_not_initialized";
        return false;
    }

    ++requests_received_;
    resp.request_id = req.request_id;
    resp.timestamp_ms = GetTimestampMs();

    bool success = DispatchRequest(req, resp);

    if (success) {
        ++requests_handled_;
    } else {
        ++requests_failed_;
    }

    return success;
}

bool CChatRoomBridge::DispatchRequest(const BridgeRequest& req,
                                       BridgeResponse& resp) {
    resp.ok = false;

    std::string kind = req.kind;
    if (kind.empty()) {
        return false;
    }

    // Route to appropriate handler
    if (kind == "send_message") {
        return HandleSendMessage(req, resp);
    } else if (kind == "send_prompt") {
        return HandleSendPrompt(req, resp);
    } else if (kind == "part_channel") {
        return HandlePartChannel(req, resp);
    } else if (kind == "kick_member") {
        return HandleKickMember(req, resp);
    } else if (kind == "ban_member") {
        return HandleBanMember(req, resp);
    } else if (kind == "set_topic") {
        return HandleSetTopic(req, resp);
    } else if (kind == "set_mode") {
        return HandleSetMode(req, resp);
    } else if (kind == "promote_operator") {
        return HandlePromoteOperator(req, resp);
    } else if (kind == "demote_operator") {
        return HandleDemoteOperator(req, resp);
    } else if (kind == "whois") {
        return HandleWhois(req, resp);
    } else if (kind == "names") {
        return HandleNames(req, resp);
    } else if (kind == "create_topic") {
        return HandleCreateTopic(req, resp);
    } else if (kind == "reply_topic") {
        return HandleReplyTopic(req, resp);
    } else if (kind == "list_topics") {
        return HandleListTopics(req, resp);
    } else if (kind == "close_topic") {
        return HandleCloseTopic(req, resp);
    }

    resp.error_message = std::string("Unknown request kind: ") + kind;
    return false;
}

bool CChatRoomBridge::HandleSendMessage(const BridgeRequest& req,
                                       BridgeResponse& resp) {
    // Parse payload JSON: {"channel": "#xxx", "message": "text"}
    std::string channel;
    std::string message;

    try {
        auto payload = nlohmann::json::parse(req.payload_json);
        channel = payload.value("channel", "");
        message = payload.value("message", "");
    } catch (...) {
        resp.ok = false;
        resp.error_message = "invalid_payload";
        resp.payload_json = "{\"sent\":false,\"error\":\"invalid JSON payload\"}";
        return true;
    }

    if (channel.empty() || message.empty()) {
        resp.ok = false;
        resp.error_message = "missing_channel_or_message";
        resp.payload_json = "{\"sent\":false,\"error\":\"channel and message are required\"}";
        return true;
    }

    // Normalize channel name
    if (!channel.empty() && channel[0] != '#') {
        channel = "#" + channel;
    }

    // Use fire-and-forget to send PRIVMSG.
    // The server echoes it back via push (privmsg event), which arrives asynchronously
    // through the push receiver thread.  This avoids the race where a concurrent push
    // message on the socket is misread as the response to this request.
    auto& transport = CIrcChatTransport::Instance();
    bool ok = transport.SendPrivmsgNoWait(channel, message);

    resp.ok = ok;
    if (!ok) {
        resp.error_message = "network_send_failed";
        resp.payload_json = "{\"sent\":false,\"error\":\"failed to send via network\"}";
        } else {
        resp.payload_json = "{\"sent\":true}";
        }

    // ── OpenClaw AI Agent 调用（对齐 AIAssistant/JsBridge 的 chatSendPrivmsg 中 agent mention 检测逻辑）──
    // 条件：消息中包含 @炎图AI助手 或频道为 workspace
    if (ok) {
        // 诊断：打印 message 的前 120 字节（十六进制 + 可读字符）
        {
            std::ostringstream diag;
            diag << "HandleSendMessage message(len=" << message.size() << "): ";
            size_t showLen = (std::min)(message.size(), size_t(120));
            for (size_t i = 0; i < showLen; ++i) {
                diag << std::hex << std::setfill('0') << std::setw(2)
                     << (static_cast<unsigned int>(static_cast<unsigned char>(message[i]))) << " ";
            }
            TRACE(_T("[CChatRoomBridge] %hs\n"), diag.str().c_str());
        }

        bool isAgentMention = false;
        // TEMP: 禁用 Agent 请求，避免 OpenClaw 服务不可用时卡死
        // TODO: 重新启用前确保 OpenClaw 服务 (192.168.20.12:3000) 已运行
        
        if (message.find('@') != std::string::npos) {
            // UTF-8 编码的 "炎图AI助手": E7 82 8E E5 9B BE 41 49 E5 8A A9 E6 89 8B
            static const char kAgentNameUtf8[] = "\xE7\x82\x8E\xE5\x9B\xBE" "AI" "\xE5\x8A\xA9\xE6\x89\x8B";
            isAgentMention = (message.find("@" + std::string(kAgentNameUtf8)) != std::string::npos);
            if (!isAgentMention) {
                // GBK 误解释形态（UTF-8 字节被当 GBK 解码）
                isAgentMention = (message.find("@鐐庡浘AI鍔╂墜") != std::string::npos);
            }
            // 兜底：直接搜索源码中的 UTF-8 literal（取决于源文件编码）
            if (!isAgentMention) {
                isAgentMention = (message.find("@炎图AI助手") != std::string::npos);
            }
        }
        if (!isAgentMention && channel.find("#workspace_") != std::string::npos) {
            isAgentMention = true;
        }
        
        // 暂时禁用 Agent 检测，避免 OpenClaw 服务不可用时卡死
        //isAgentMention = false;
        TRACE(_T("[CChatRoomBridge] HandleSendMessage: isAgentMention=%d\n"), isAgentMention ? 1 : 0);
        // TEMP: 禁用 Agent 请求触发，避免 OpenClaw 服务不可用时卡死
         if (isAgentMention) {
             TRACE(_T("[CChatRoomBridge] HandleSendMessage: agent mention detected, triggering OpenClaw request\n"));
             SendOpenClawAgentRequest(channel, message);
         }
    }

    // 异步处理，done_cb 或 timeout thread 会发 EmitResponse。此处不填充 resp
    // （已由 HandleWebMessageAsync 忽略），也不在此 EmitResponse——避免与 async callback 竞争。
    return true;
}

bool CChatRoomBridge::HandleSendPrompt(const BridgeRequest& req,
                                        BridgeResponse& resp) {
    // Parse payload JSON: {"channel": "#xxx", "message": "text"}
    // SendPrompt is similar to SendMessage but used for AI commands via TCP plaintext
    std::string channel;
    std::string message;

    try {
        auto payload = nlohmann::json::parse(req.payload_json);
        channel = payload.value("channel", "");
        message = payload.value("message", "");
    } catch (...) {
        resp.ok = false;
        resp.error_message = "invalid_payload";
        resp.payload_json = "{\"sent\":false,\"error\":\"invalid JSON payload\"}";
        return true;
    }

    if (channel.empty() || message.empty()) {
        resp.ok = false;
        resp.error_message = "missing_channel_or_message";
        resp.payload_json = "{\"sent\":false,\"error\":\"channel and message are required\"}";
        return true;
    }

    // Normalize channel name
    if (!channel.empty() && channel[0] != '#') {
        channel = "#" + channel;
    }

    // Use fire-and-forget via TCP (for AI commands).
    auto& transport = CIrcChatTransport::Instance();
    bool ok = transport.SendIrcCommandTcpNoWait(channel, "PRIVMSG", message);
    
    resp.ok = ok;
    if (!ok) {
        resp.error_message = "network_send_failed";
        resp.payload_json = "{\"sent\":false,\"error\":\"failed to send via network\"}";
    } else {
        resp.payload_json = "{\"sent\":true}";
    }

    return true;
}

// join_channel 通过 221 (IrcMessageReq) 发送（协议类型与 PRIVMSG/PART/MODE/TOPIC 等
// IRC 命令一致）。服务端会在 221 通道回 222 IrcMessageResp，payload 为 JOIN ack JSON。
// 挂 seq 等响应：收到 222 ack 后 resolve；30s 未响应则降级（本地注册 + 返回 fallback:true）。
void CChatRoomBridge::HandleJoinChannelViaIrc(const BridgeRequest& req) {
    std::string request_id = req.request_id;

    // Parse channel from payload
    std::string channel;
    try {
        auto pl = nlohmann::json::parse(req.payload_json);
        channel = pl.value("channel", pl.value("room_id", ""));
    } catch (...) {
        channel = "";
    }
    if (!channel.empty() && channel[0] != '#' && channel[0] != '&') {
        channel = "#" + channel;
    }

    // Build 221 IRC payload: {"cmd":"JOIN","channel":"#xxx"}
    nlohmann::json irc_payload_json;
    irc_payload_json["cmd"] = "JOIN";
    irc_payload_json["channel"] = channel;
    std::string irc_payload = irc_payload_json.dump();

    if (!CNetwork_c::Instance().IsTcpConnected()) {
        TRACE(_T("[CChatRoomBridge] HandleJoinChannelViaIrc: TCP not connected\n"));
        EmitResponse(request_id, false, "tcp_not_connected",
                    "{\"joined\":false,\"error\":\"tcp_not_connected\"}");
        return;
    }

    // 用 shared_ptr 原子标记追踪 callback 是否已被调用（用于超时判断）。
    // 关键：called 必须被 done_cb 和超时线程以 shared_ptr 捕获（非 weak_ptr），
    // 否则 called 在函数返回时析构，后续 weak_ptr.lock() 永远返回 null。
    auto called = std::make_shared<std::atomic<bool>>(false);

    auto register_channel_locally = [this, channel]() {
        auto& mgr = CMgrChannels::Instance();
        std::string nickname = deps_.get_current_nickname ? deps_.get_current_nickname() : GetCurrentNickname();
        auto peer = mgr.GetMember(nickname);
        if (!peer) {
            peer = CPeer::Create(nickname);
            mgr.RegisterMember(peer);
        }
        if (!mgr.GetChannel(channel)) {
            mgr.CreateChannel(channel, peer);
        }
        mgr.Join(peer, channel);
    };

    auto emit_success = [this, channel](const std::string& rid, bool fallback) {
        nlohmann::json out;
        out["joined"] = true;
        out["channel"] = channel;
        if (fallback) out["fallback"] = true;
        EmitResponse(rid, true, "", out.dump());
    };

    // seq callback：服务端 222 JOIN ack 到达时触发
    auto done_cb = [this, request_id, channel, called,
                    register_channel_locally, emit_success]
                    (uint32_t seq, const std::string& resp_payload) {
        TRACE(_T("[CChatRoomBridge] HandleJoinChannelViaIrc ack: seq=%u resp_len=%zu\n"),
              seq, resp_payload.size());

        if (called->exchange(true)) return;  // 已处理过

        register_channel_locally();
        std::string sid = deps_.get_current_session_id ? deps_.get_current_session_id() : GetCurrentSessionId();
        TrackJoinedChannel(sid, channel);

        // 把服务端 ack JSON 透传给前端（room_members/status/session_id 等元数据）
        try {
            auto ack = nlohmann::json::parse(resp_payload);
            nlohmann::json out = ack;
            out["joined"] = (ack.value("status", std::string("ok")) == "ok");
            out["channel"] = channel;
            EmitResponse(request_id, true, "", out.dump());
        } catch (...) {
            emit_success(request_id, false);
        }
    };

    uint32_t seq = CNetwork_c::Instance().SendTcpNoWaitWithCallback(
        static_cast<uint16_t>(MsgType::IrcMessageReq), irc_payload, done_cb);

    TRACE(_T("[CChatRoomBridge] HandleJoinChannelViaIrc: channel=%hs seq=%u\n"),
          channel.c_str(), seq);

    if (seq == 0) {
        EmitResponse(request_id, false, "tcp_send_failed",
                     "{\"joined\":false,\"error\":\"tcp_send_failed\"}");
        return;
    }

    // 30s 超时降级：本地注册 + 返回 {joined:true, fallback:true}
    std::thread([called, request_id, channel,
                 register_channel_locally, emit_success]() {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        if (!called->load()) {
            TRACE(_T("[CChatRoomBridge] HandleJoinChannelViaIrc timeout fallback: channel=%hs\n"),
                  channel.c_str());
            register_channel_locally();
            emit_success(request_id, true);
        }
    }).detach();
}

// send_message 走 221 (IrcMessageReq) → 222 (IrcMessageResp) 通道，挂 seq 等 ACK。
// 服务端 ACK 的 JSON payload 与接收到的 push 消息 JSON 格式一致，仅 seq 不同。
// 收到 222 ack（status=ok/accepted）后 resolve；30s 未响应则降级返回 fallback:true。
void CChatRoomBridge::HandleSendMessageViaIrc(const BridgeRequest& req) {
    std::string request_id = req.request_id;

    // Parse channel and message from payload
    std::string channel;
    std::string message;
    try {
        auto pl = nlohmann::json::parse(req.payload_json);
        channel = pl.value("channel", "");
        message = pl.value("message", "");
    } catch (...) {
        channel = "";
        message = "";
    }

    if (channel.empty() || message.empty()) {
        EmitResponse(request_id, false, "missing_channel_or_message",
                     "{\"sent\":false,\"error\":\"channel and message are required\"}");
        return;
    }

    if (!channel.empty() && channel[0] != '#') {
        channel = "#" + channel;
    }

    // Build 221 IRC PRIVMSG payload
    nlohmann::json irc_payload_json;
    irc_payload_json["cmd"] = "PRIVMSG";
    irc_payload_json["channel"] = channel;
    irc_payload_json["message"] = message;
    irc_payload_json["ts"] = static_cast<int64_t>(GetTimestampSeconds());
    std::string irc_payload = irc_payload_json.dump();

    if (!CNetwork_c::Instance().IsTcpConnected()) {
        TRACE(_T("[CChatRoomBridge] HandleSendMessageViaIrc: TCP not connected\n"));
        EmitResponse(request_id, false, "tcp_not_connected",
                     "{\"sent\":false,\"error\":\"tcp_not_connected\"}");
        return;
    }

    // ── OpenClaw AI Agent 检测：在发送前检查是否需要触发 AI 请求 ──
    // 对齐 AIAssistant/JsBridge 的 chatSendPrivmsg 中 agent mention 检测逻辑
    bool isAgentMention = false;
    if (message.find('@') != std::string::npos) {
        static const char kAgentNameUtf8[] = "\xE7\x82\x8E\xE5\x9B\xBE" "AI" "\xE5\x8A\xA9\xE6\x89\x8B";
        isAgentMention = (message.find("@" + std::string(kAgentNameUtf8)) != std::string::npos);
        if (!isAgentMention) {
            isAgentMention = (message.find("@鐐庡浘AI鍔╂墜") != std::string::npos);
        }
        if (!isAgentMention) {
            isAgentMention = (message.find("@炎图AI助手") != std::string::npos);
        }
    }
    if (!isAgentMention && channel.find("#workspace_") != std::string::npos) {
        isAgentMention = true;
    }
    TRACE(_T("[CChatRoomBridge] HandleSendMessageViaIrc: isAgentMention=%d\n"), isAgentMention ? 1 : 0);

    // 用 shared_ptr 原子标记追踪 callback 是否已被调用（用于超时判断）。
    // 关键：called 必须被 done_cb 和超时线程以 shared_ptr 捕获（非 weak_ptr），
    // 否则 called 在函数返回时析构，后续 weak_ptr.lock() 永远返回 null，
    // ACK 回调静默丢弃、前端永远收不到响应。
    auto called = std::make_shared<std::atomic<bool>>(false);

    auto emit_success = [this, channel](const std::string& rid, bool fallback) {
        nlohmann::json out;
        out["sent"] = true;
        out["channel"] = channel;
        if (fallback) out["fallback"] = true;
        EmitResponse(rid, true, "", out.dump());
    };

    // seq callback：服务端 222 PRIVMSG ack 到达时触发
    // ack JSON 格式与接收到的 push 消息一致：{"event":"PRIVMSG","channel":"#xxx","message":"...","status":"ok",...}
    auto done_cb = [this, request_id, channel, called, emit_success]
                   (uint32_t seq, const std::string& resp_payload) {
        TRACE(_T("[CChatRoomBridge] HandleSendMessageViaIrc ack: seq=%u resp_len=%zu\n"),
              seq, resp_payload.size());

        if (called->exchange(true)) return;  // 已处理过

        try {
            auto ack = nlohmann::json::parse(resp_payload);
            std::string status = ack.value("status", std::string());
            // 服务端 PRIVMSG ack 的 status 可能为 "ok" 或 "accepted"（对齐 §17.1 注意项）
            bool is_ok = (status == "ok" || status == "accepted");

            nlohmann::json out;
            out["sent"] = is_ok;
            out["channel"] = channel;
            if (!is_ok) {
                std::string err = ack.value("error", ack.value("message", "server_rejected"));
                out["error"] = err;
                EmitResponse(request_id, false, err, out.dump());
            } else {
                // 把服务端 ack JSON 透传给前端（含 ts / session_id 等元数据）
                out["ack"] = ack;
                EmitResponse(request_id, true, "", out.dump());
            }
        } catch (...) {
            // ack 解析失败，视为成功（至少服务端回了 222 帧）
            emit_success(request_id, false);
        }
    };

    uint32_t seq = CNetwork_c::Instance().SendTcpNoWaitWithCallback(
        static_cast<uint16_t>(MsgType::IrcMessageReq), irc_payload, done_cb);

    TRACE(_T("[CChatRoomBridge] HandleSendMessageViaIrc: channel=%hs seq=%u\n"),
          channel.c_str(), seq);

    if (seq == 0) {
        EmitResponse(request_id, false, "tcp_send_failed",
                     "{\"sent\":false,\"error\":\"tcp_send_failed\"}");
        return;
    }

    // 触发 OpenClaw AI Agent 请求（后台线程，不阻塞 ACK 等待）
    if (isAgentMention) {
        TRACE(_T("[CChatRoomBridge] HandleSendMessageViaIrc: agent mention detected, triggering OpenClaw request\n"));
        SendOpenClawAgentRequest(channel, message);
    }

    // 30s 超时降级：返回 {sent:true, fallback:true}
    std::thread([called, request_id, channel, emit_success]() {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        if (!called->load()) {
            TRACE(_T("[CChatRoomBridge] HandleSendMessageViaIrc timeout fallback: channel=%hs\n"),
                  channel.c_str());
            emit_success(request_id, true);
        }
    }).detach();
}

bool CChatRoomBridge::HandlePartChannel(const BridgeRequest& req,
                                         BridgeResponse& resp) {
    auto& mgr = CMgrChannels::Instance();
    std::string nickname = deps_.get_current_nickname ? deps_.get_current_nickname() : GetCurrentNickname();
    std::string session_id = deps_.get_current_session_id ? deps_.get_current_session_id() : GetCurrentSessionId();

    auto peer = mgr.GetMember(nickname);
    if (!peer) {
        resp.ok = false;
        resp.error_message = "Not in any channel";
        return false;
    }

    std::string channel;
    std::string reason;
    try {
        auto payload = nlohmann::json::parse(req.payload_json);
        channel = payload.value("channel", "");
        reason = payload.value("reason", "");
    } catch (...) {
        channel = "";
    }

    // Normalize channel name
    if (!channel.empty() && channel[0] != '#') {
        channel = "#" + channel;
    }

    // Use fire-and-forget via TCP (PART will be echoed back via push).
    auto& transport = CIrcChatTransport::Instance();
    transport.SendPartNoWait(channel, reason);

    auto result = mgr.Part(peer, channel);
    resp.ok = result.success;
    resp.error_message = result.error_message;

    if (result.success) {
        UntrackJoinedChannel(session_id, channel);
        std::ostringstream oss;
        oss << "{\"parted\":true,\"channel\":\"" << channel << "\"}";
        resp.payload_json = oss.str();
    }

    return true;
}

bool CChatRoomBridge::HandleKickMember(const BridgeRequest& req,
                                        BridgeResponse& resp) {
    auto& mgr = CMgrChannels::Instance();
    std::string nickname = deps_.get_current_nickname ? deps_.get_current_nickname() : GetCurrentNickname();

    std::string channel;
    std::string target;
    std::string reason;
    try {
        auto payload = nlohmann::json::parse(req.payload_json);
        channel = payload.value("channel", "");
        target = payload.value("target", "");
        reason = payload.value("reason", "");
    } catch (...) {
        channel = "";
        target = "";
    }

    // Normalize channel name
    if (!channel.empty() && channel[0] != '#') {
        channel = "#" + channel;
    }

    if (channel.empty() || target.empty()) {
        resp.ok = false;
        resp.error_message = "missing_channel_or_target";
        resp.payload_json = "{\"kicked\":false,\"error\":\"channel and target are required\"}";
        return true;
    }

    // Use fire-and-forget via TLS (KICK will be echoed back via push).
    auto& transport = CIrcChatTransport::Instance();
    transport.SendKickNoWait(channel, target, reason);

    auto result = mgr.Kick(nickname, channel, target);
    resp.ok = result.success;
    resp.error_message = result.error_message;

    if (result.success) {
        resp.payload_json = "{\"kicked\":true}";
    } else {
        std::ostringstream oss;
        oss << "{\"kicked\":false,\"error\":\"" << result.error_message << "\"}";
        resp.payload_json = oss.str();
    }

    return true;
}

bool CChatRoomBridge::HandleBanMember(const BridgeRequest& req,
                                        BridgeResponse& resp) {
    auto& mgr = CMgrChannels::Instance();
    std::string nickname = deps_.get_current_nickname ? deps_.get_current_nickname() : GetCurrentNickname();

    std::string channel;
    std::string mask;
    try {
        auto payload = nlohmann::json::parse(req.payload_json);
        channel = payload.value("channel", "");
        mask = payload.value("mask", "");
    } catch (...) {
        channel = "";
        mask = "";
    }

    // Normalize channel name
    if (!channel.empty() && channel[0] != '#') {
        channel = "#" + channel;
    }

    if (channel.empty() || mask.empty()) {
        resp.ok = false;
        resp.error_message = "missing_channel_or_mask";
        resp.payload_json = "{\"banned\":false,\"error\":\"channel and mask are required\"}";
        return true;
    }

    // Use fire-and-forget via TLS (MODE will be echoed back via push).
    auto& transport = CIrcChatTransport::Instance();
    transport.SendModeNoWait(channel, "+b " + mask);

    auto result = mgr.Ban(nickname, channel, mask);
    resp.ok = result.success;
    resp.error_message = result.error_message;

    if (result.success) {
        resp.payload_json = "{\"banned\":true}";
    } else {
        std::ostringstream oss;
        oss << "{\"banned\":false,\"error\":\"" << result.error_message << "\"}";
        resp.payload_json = oss.str();
    }

    return true;
}

bool CChatRoomBridge::HandleSetTopic(const BridgeRequest& req,
                                      BridgeResponse& resp) {
    auto& mgr = CMgrChannels::Instance();
    std::string nickname = deps_.get_current_nickname ? deps_.get_current_nickname() : GetCurrentNickname();

    std::string channel;
    std::string topic;
    try {
        auto payload = nlohmann::json::parse(req.payload_json);
        channel = payload.value("channel", "");
        topic = payload.value("topic", "");
    } catch (...) {
        channel = "";
        topic = "";
    }

    // Normalize channel name
    if (!channel.empty() && channel[0] != '#') {
        channel = "#" + channel;
    }

    if (channel.empty() || topic.empty()) {
        resp.ok = false;
        resp.error_message = "missing_channel_or_topic";
        resp.payload_json = "{\"error\":\"channel and topic are required\"}";
        return true;
    }

    // Use fire-and-forget via TLS (TOPIC will be echoed back via push).
    auto& transport = CIrcChatTransport::Instance();
    transport.SendTopicNoWait(channel, topic);

    auto result = mgr.SetTopic(nickname, channel, topic);
    resp.ok = result.success;
    resp.error_message = result.error_message;

    if (result.success) {
        std::ostringstream oss;
        oss << "{\"topic\":\"" << result.topic << "\"}";
        resp.payload_json = oss.str();
    } else {
        std::ostringstream oss;
        oss << "{\"error\":\"" << result.error_message << "\"}";
        resp.payload_json = oss.str();
    }

    return true;
}

bool CChatRoomBridge::HandleSetMode(const BridgeRequest& req,
                                     BridgeResponse& resp) {
    auto& mgr = CMgrChannels::Instance();
    std::string nickname = deps_.get_current_nickname ? deps_.get_current_nickname() : GetCurrentNickname();

    std::string channel;
    std::string mode_str;
    try {
        auto payload = nlohmann::json::parse(req.payload_json);
        channel = payload.value("channel", "");
        mode_str = payload.value("mode", "");
    } catch (...) {
        channel = "";
        mode_str = "";
    }

    // Normalize channel name
    if (!channel.empty() && channel[0] != '#') {
        channel = "#" + channel;
    }

    if (channel.empty() || mode_str.empty()) {
        resp.ok = false;
        resp.error_message = "missing_channel_or_mode";
        resp.payload_json = "{\"error\":\"channel and mode are required\"}";
        return true;
    }

    // Use fire-and-forget via TLS (MODE will be echoed back via push).
    auto& transport = CIrcChatTransport::Instance();
    transport.SendModeNoWait(channel, mode_str);

    auto result = mgr.SetChannelMode(nickname, channel, mode_str);
    resp.ok = result.success;
    resp.error_message = result.error_message;

    if (result.success) {
        std::ostringstream oss;
        oss << "{\"mode\":\"" << static_cast<int>(result.new_mode) << "\"}";
        resp.payload_json = oss.str();
    } else {
        std::ostringstream oss;
        oss << "{\"error\":\"" << result.error_message << "\"}";
        resp.payload_json = oss.str();
    }

    return true;
}

bool CChatRoomBridge::HandlePromoteOperator(const BridgeRequest& req,
                                             BridgeResponse& resp) {
    auto& mgr = CMgrChannels::Instance();
    std::string nickname = deps_.get_current_nickname ? deps_.get_current_nickname() : GetCurrentNickname();

    std::string channel;
    std::string target;
    try {
        auto payload = nlohmann::json::parse(req.payload_json);
        channel = payload.value("channel", "");
        target = payload.value("target", "");
    } catch (...) {
        channel = "";
        target = "";
    }

    // Normalize channel name
    if (!channel.empty() && channel[0] != '#') {
        channel = "#" + channel;
    }

    if (channel.empty() || target.empty()) {
        resp.ok = false;
        resp.error_message = "missing_channel_or_target";
        resp.payload_json = "{\"promoted\":false,\"error\":\"channel and target are required\"}";
        return true;
    }

    // Use fire-and-forget via TLS (MODE will be echoed back via push).
    auto& transport = CIrcChatTransport::Instance();
    transport.SendModeNoWait(channel, "+o " + target);

    bool success = mgr.PromoteToOperator(nickname, channel, target);
    resp.ok = success;

    if (success) {
        resp.payload_json = "{\"promoted\":true}";
    } else {
        resp.payload_json = "{\"promoted\":false,\"error\":\"Permission denied or user not found\"}";
    }

    return true;
}

bool CChatRoomBridge::HandleDemoteOperator(const BridgeRequest& req,
                                            BridgeResponse& resp) {
    auto& mgr = CMgrChannels::Instance();
    std::string nickname = deps_.get_current_nickname ? deps_.get_current_nickname() : GetCurrentNickname();

    std::string channel;
    std::string target;
    try {
        auto payload = nlohmann::json::parse(req.payload_json);
        channel = payload.value("channel", "");
        target = payload.value("target", "");
    } catch (...) {
        channel = "";
        target = "";
    }

    // Normalize channel name
    if (!channel.empty() && channel[0] != '#') {
        channel = "#" + channel;
    }

    if (channel.empty() || target.empty()) {
        resp.ok = false;
        resp.error_message = "missing_channel_or_target";
        resp.payload_json = "{\"demoted\":false,\"error\":\"channel and target are required\"}";
        return true;
    }

    // Use fire-and-forget via TLS (MODE will be echoed back via push).
    auto& transport = CIrcChatTransport::Instance();
    transport.SendModeNoWait(channel, "-o " + target);

    bool success = mgr.DemoteOperator(nickname, channel, target);
    resp.ok = success;

    if (success) {
        resp.payload_json = "{\"demoted\":true}";
    } else {
        resp.payload_json = "{\"demoted\":false,\"error\":\"Permission denied or user not found\"}";
    }

    return true;
}

bool CChatRoomBridge::HandleWhois(const BridgeRequest& req,
                                    BridgeResponse& resp) {
    auto& mgr = CMgrChannels::Instance();

    std::string nickname;
    try {
        auto payload = nlohmann::json::parse(req.payload_json);
        nickname = payload.value("nickname", "");
    } catch (...) {
        nickname = "";
    }

    if (nickname.empty()) {
        resp.ok = false;
        resp.error_message = "missing_nickname";
        resp.payload_json = "{\"error\":\"nickname is required\"}";
        return true;
    }

    auto info = mgr.Whois(nickname);
    if (info) {
        std::ostringstream oss;
        oss << "{";
        oss << "\"nickname\":\"" << info->nickname << "\",";
        oss << "\"username\":\"" << info->username << "\",";
        oss << "\"hostname\":\"" << info->hostname << "\",";
        oss << "\"realname\":\"" << info->realname << "\",";
        oss << "\"channels\":[";
        for (size_t i = 0; i < info->channels.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "\"" << info->channels[i] << "\"";
        }
        oss << "]}";
        resp.payload_json = oss.str();
        resp.ok = true;
    } else {
        resp.ok = false;
        resp.error_message = "User not found";
        resp.payload_json = "{\"error\":\"User not found\"}";
    }

    return true;
}

bool CChatRoomBridge::HandleNames(const BridgeRequest& req,
                                   BridgeResponse& resp) {
    auto& mgr = CMgrChannels::Instance();

    std::string channel;
    try {
        auto payload = nlohmann::json::parse(req.payload_json);
        channel = payload.value("channel", "");
    } catch (...) {
        channel = "";
    }

    // Normalize channel name
    if (!channel.empty() && channel[0] != '#') {
        channel = "#" + channel;
    }

    if (channel.empty()) {
        resp.ok = false;
        resp.error_message = "missing_channel";
        resp.payload_json = "{\"error\":\"channel is required\"}";
        return true;
    }

    auto names = mgr.GetNamesList(channel);
    std::ostringstream oss;
    oss << "{\"names\":[";
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << names[i] << "\"";
    }
    oss << "]}";

    resp.ok = true;
    resp.payload_json = oss.str();
    return true;
}

bool CChatRoomBridge::HandleCreateTopic(const BridgeRequest& req,
                                         BridgeResponse& resp) {
    auto& mgr = CMgrChannels::Instance();
    std::string nickname = deps_.get_current_nickname ? deps_.get_current_nickname() : GetCurrentNickname();

    std::string channel;
    std::string title;
    std::string content;
    try {
        auto payload = nlohmann::json::parse(req.payload_json);
        channel = payload.value("channel", "");
        title = payload.value("title", "");
        content = payload.value("content", "");
    } catch (...) {
        channel = "";
        title = "";
        content = "";
    }

    // Normalize channel name
    if (!channel.empty() && channel[0] != '#') {
        channel = "#" + channel;
    }

    if (channel.empty() || title.empty()) {
        resp.ok = false;
        resp.error_message = "missing_channel_or_title";
        resp.payload_json = "{\"error\":\"channel and title are required\"}";
        return true;
    }

    auto ch = mgr.GetChannel(channel);
    if (!ch) {
        resp.ok = false;
        resp.error_message = "Channel not found";
        resp.payload_json = "{\"error\":\"Channel not found\"}";
        return true;
    }

    auto topic = ch->CreateTopic(title, nickname, content);
    resp.ok = true;
    std::ostringstream oss;
    oss << "{\"topic_id\":\"" << topic->GetId() << "\"}";
    resp.payload_json = oss.str();
    return true;
}

bool CChatRoomBridge::HandleReplyTopic(const BridgeRequest& req,
                                        BridgeResponse& resp) {
    auto& mgr = CMgrChannels::Instance();
    std::string nickname = deps_.get_current_nickname ? deps_.get_current_nickname() : GetCurrentNickname();

    std::string channel;
    std::string topic_id;
    std::string content;
    std::string parent_reply_id;
    try {
        auto payload = nlohmann::json::parse(req.payload_json);
        channel = payload.value("channel", "");
        topic_id = payload.value("topic_id", "");
        content = payload.value("content", "");
        parent_reply_id = payload.value("parent_reply_id", "");
    } catch (...) {
        channel = "";
        topic_id = "";
        content = "";
    }

    // Normalize channel name
    if (!channel.empty() && channel[0] != '#') {
        channel = "#" + channel;
    }

    if (channel.empty() || topic_id.empty() || content.empty()) {
        resp.ok = false;
        resp.error_message = "missing_required_fields";
        resp.payload_json = "{\"error\":\"channel, topic_id, and content are required\"}";
        return true;
    }

    auto ch = mgr.GetChannel(channel);
    if (!ch) {
        resp.ok = false;
        resp.error_message = "Channel not found";
        resp.payload_json = "{\"error\":\"Channel not found\"}";
        return true;
    }

    auto topic = ch->GetTopic(topic_id);
    if (!topic) {
        resp.ok = false;
        resp.error_message = "Topic not found";
        resp.payload_json = "{\"error\":\"Topic not found\"}";
        return true;
    }

    std::string reply_id = topic->AddReply(nickname, content);
    resp.ok = !reply_id.empty();

    if (resp.ok) {
        std::ostringstream oss;
        oss << "{\"reply_id\":\"" << reply_id << "\"}";
        resp.payload_json = oss.str();
    } else {
        resp.payload_json = "{\"error\":\"Failed to add reply\"}";
    }

    return true;
}

bool CChatRoomBridge::HandleListTopics(const BridgeRequest& req,
                                        BridgeResponse& resp) {
    auto& mgr = CMgrChannels::Instance();

    std::string channel;
    std::string status;
    try {
        auto payload = nlohmann::json::parse(req.payload_json);
        channel = payload.value("channel", "");
        status = payload.value("status", "");
    } catch (...) {
        channel = "";
        status = "";
    }

    // Normalize channel name
    if (!channel.empty() && channel[0] != '#') {
        channel = "#" + channel;
    }

    if (channel.empty()) {
        resp.ok = false;
        resp.error_message = "missing_channel";
        resp.payload_json = "{\"error\":\"channel is required\"}";
        return true;
    }

    auto ch = mgr.GetChannel(channel);
    if (!ch) {
        resp.ok = false;
        resp.error_message = "Channel not found";
        resp.payload_json = "{\"error\":\"Channel not found\"}";
        return true;
    }

    auto topics = ch->ListTopics();
    std::ostringstream oss;
    oss << "{\"topics\":[";
    bool first = true;
    for (const auto& topic : topics) {
        // Filter by status if specified
        if (!status.empty()) {
            auto topicStatus = topic->GetStatus();
            if (status == "open" && topicStatus != blazeclaw::irc::TopicStatus::Open) continue;
            if (status == "closed" && topicStatus != blazeclaw::irc::TopicStatus::Closed) continue;
            if (status == "pinned" && topicStatus != blazeclaw::irc::TopicStatus::Pinned) continue;
            if (status == "archived" && topicStatus != blazeclaw::irc::TopicStatus::Archived) continue;
        }
        if (!first) oss << ",";
        first = false;
        oss << "{";
        oss << "\"id\":\"" << topic->GetId() << "\",";
        oss << "\"title\":\"" << topic->GetTitle() << "\",";
        oss << "\"status\":" << static_cast<int>(topic->GetStatus()) << ",";
        oss << "\"replies\":" << topic->GetReplyCount();
        oss << "}";
    }
    oss << "]}";

    resp.ok = true;
    resp.payload_json = oss.str();
    return true;
}

bool CChatRoomBridge::HandleCloseTopic(const BridgeRequest& req,
                                        BridgeResponse& resp) {
    auto& mgr = CMgrChannels::Instance();
    std::string nickname = deps_.get_current_nickname ? deps_.get_current_nickname() : GetCurrentNickname();

    std::string channel;
    std::string topic_id;
    try {
        auto payload = nlohmann::json::parse(req.payload_json);
        channel = payload.value("channel", "");
        topic_id = payload.value("topic_id", "");
    } catch (...) {
        channel = "";
        topic_id = "";
    }

    // Normalize channel name
    if (!channel.empty() && channel[0] != '#') {
        channel = "#" + channel;
    }

    if (channel.empty() || topic_id.empty()) {
        resp.ok = false;
        resp.error_message = "missing_channel_or_topic_id";
        resp.payload_json = "{\"error\":\"channel and topic_id are required\"}";
        return true;
    }

    auto ch = mgr.GetChannel(channel);
    if (!ch) {
        resp.ok = false;
        resp.error_message = "Channel not found";
        resp.payload_json = "{\"error\":\"Channel not found\"}";
        return true;
    }

    bool success = ch->CloseTopic(topic_id, nickname);
    resp.ok = success;

    if (success) {
        resp.payload_json = "{\"closed\":true}";
    } else {
        resp.payload_json = "{\"closed\":false,\"error\":\"Permission denied or topic not found\"}";
    }

    return true;
}

// ── 群任务帖子管理 ─────────────────────────────────────────────

void CChatRoomBridge::EmitToWeb(const BridgePush& push) {
    if (!deps_.emit_to_web) {
        TRACE(_T("[CChatRoomBridge] EmitToWeb: emit_to_web is null!\n"));
        LOG_WARN("[CChatRoomBridge] EmitToWeb: emit_to_web is null!");
        return;
    }

    std::string json = BuildPushJson(push);
    TRACE(_T("[CChatRoomBridge] EmitToWeb: emitting push JSON: %s\n"), CA2T(json.substr(0, 200).c_str()));
    deps_.emit_to_web(json);
    ++push_emitted_;
}

void CChatRoomBridge::OnNetworkPush(const std::string& event_type,
                                     const std::string& payload_json) {
    std::string session_id = deps_.get_current_session_id ? deps_.get_current_session_id() : GetCurrentSessionId();

    BridgePush push;
    push.channel = "chatroom.bridge.push";
    push.event_type = event_type;
    push.payload_json = payload_json;
    push.session_id = session_id;
    push.timestamp_ms = GetTimestampMs();

    EmitToWeb(push);
}

CChatRoomBridge::Diagnostics CChatRoomBridge::GetDiagnostics() const {
    Diagnostics diag;
    diag.requests_received = requests_received_.load();
    diag.requests_handled = requests_handled_.load();
    diag.requests_failed = requests_failed_.load();
    diag.push_emitted = push_emitted_.load();

    {
        std::lock_guard<std::mutex> lock(push_queue_mutex_);
        for (const auto& queue_pair : push_queues_) {
            (void)queue_pair;
            diag.current_queue_size += queue_pair.second.size();
        }
    }

    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        diag.active_sessions = static_cast<uint32_t>(known_sessions_.size());
    }

    return diag;
}

void CChatRoomBridge::RegisterSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    known_sessions_.insert(session_id);
}

void CChatRoomBridge::UnregisterSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    known_sessions_.erase(session_id);

    {
        std::lock_guard<std::mutex> lock(push_queue_mutex_);
        push_queues_.erase(session_id);
    }
    {
        std::lock_guard<std::mutex> lock(joined_channels_mutex_);
        std::vector<std::string> to_remove;
        for (const auto& key_pair : joined_channels_) {
            if (key_pair.first.find(session_id) == 0) {
                to_remove.push_back(key_pair.first);
            }
        }
        for (const auto& key : to_remove) {
            joined_channels_.erase(key);
        }
    }
}

void CChatRoomBridge::TrackJoinedChannel(const std::string& session_id,
                                          const std::string& channel) {
    std::lock_guard<std::mutex> lock(joined_channels_mutex_);
    std::string key = session_id + ":" + channel;
    joined_channels_[key] = GetTimestampMs();
    RegisterSession(session_id);
}

void CChatRoomBridge::UntrackJoinedChannel(const std::string& session_id,
                                            const std::string& channel) {
    std::lock_guard<std::mutex> lock(joined_channels_mutex_);
    std::string key = session_id + ":" + channel;
    joined_channels_.erase(key);
}

void CChatRoomBridge::HandleRequestTimeout(const std::string& request_id) {
    std::lock_guard<std::mutex> lock(pending_requests_mutex_);

    auto it = pending_requests_.find(request_id);
    if (it != pending_requests_.end()) {
        uint64_t now = GetTimestampMs();
        if (now - it->second > config_.request_timeout_ms) {
            pending_requests_.erase(it);

            BridgeResponse resp;
            resp.request_id = request_id;
            resp.ok = false;
            resp.error_message = "Request timeout";
            resp.timestamp_ms = now;

            if (deps_.emit_to_web) {
                deps_.emit_to_web(BuildResponseJson(resp));
            }
            ++requests_failed_;
        }
    }
}

std::string CChatRoomBridge::BuildResponseJson(const BridgeResponse& resp) {
    std::ostringstream oss;
    oss << "{";
    oss << "\"requestId\":\"" << resp.request_id << "\",";
    oss << "\"ok\":" << (resp.ok ? "true" : "false") << ",";
    if (!resp.error_message.empty()) {
        oss << "\"error\":\"" << resp.error_message << "\",";
    }
    oss << "\"payload\":" << resp.payload_json;
    oss << "}";
    return oss.str();
}

std::string CChatRoomBridge::BuildPushJson(const BridgePush& push) {
    std::ostringstream oss;
    oss << "{";
    oss << "\"channel\":\"" << JsonEscape(push.channel) << "\",";
    oss << "\"eventType\":\"" << JsonEscape(push.event_type) << "\",";
    oss << "\"sessionId\":\"" << JsonEscape(push.session_id) << "\",";
    oss << "\"payload\":" << push.payload_json << ",";
    oss << "\"timestampMs\":" << push.timestamp_ms;
    oss << "}";
    return oss.str();
}

void CChatRoomBridge::RetryPendingRequests() {
    std::deque<BridgeRequest> requests_to_retry;

    // 取出所有排队的请求
    {
        std::lock_guard<std::mutex> lock(pending_requests_queue_mutex_);
        requests_to_retry = std::move(pending_requests_queue_);
    }

    if (requests_to_retry.empty()) {
        return;
    }

    TRACE(_T("[CChatRoomBridge] Retrying %d pending requests\n"), static_cast<int>(requests_to_retry.size()));

    // 逐个重试
    while (!requests_to_retry.empty()) {
        BridgeRequest req = std::move(requests_to_retry.front());
        requests_to_retry.pop_front();

        TRACE(_T("[CChatRoomBridge] Retrying request: kind=%s requestId=%s\n"),
              CA2T(req.kind.c_str()), CA2T(req.request_id.c_str()));

        // 使用 HandleWebMessageAsync 异步处理（不走排队逻辑）
        HandleWebMessageAsync(req);
    }
}

void CChatRoomBridge::SendOpenClawAgentRequest(
    const std::string& channel, const std::string& message,
    const std::string& openclawHost, int openclawPort,
    const std::string& openclawPath, DWORD timeoutMs)
{
    TRACE(_T("[CChatRoomBridge] SendOpenClawAgentRequest: channel=%hs\n"), channel.c_str());

    // 后台线程发送 HTTP POST 请求，对齐 AIAssistant/JsBridge 的 openclaw 调用逻辑
    std::thread([this, channel, message, openclawHost, openclawPort, openclawPath, timeoutMs]() {
        try {
            // 1. 剥离 @炎图AI助手 前缀（按 AIAssistant 方式处理两种编码形态）
            std::string aiPrompt = message;
            size_t pos = std::string::npos;
            if ((pos = aiPrompt.find("@炎图AI助手")) != std::string::npos) {
                aiPrompt.erase(pos, 8);
            } else if ((pos = aiPrompt.find("@鐐庡浘AI鍔╂墜")) != std::string::npos) {
                aiPrompt.erase(pos, 8);
            }
            // Trim whitespace
            size_t start = aiPrompt.find_first_not_of(" \t\n\r");
            size_t end = aiPrompt.find_last_not_of(" \t\n\r");
            if (start != std::string::npos && end != std::string::npos) {
                aiPrompt = aiPrompt.substr(start, end - start + 1);
            } else {
                aiPrompt.clear();
            }

            TRACE(_T("[CChatRoomBridge] SendOpenClawAgentRequest: aiPrompt=%hs\n"), aiPrompt.c_str());

            // 2. 构建 AI_TASK_REQUEST JSON（对齐 agent-chat 的 buildOpenClawMessage 格式）
            std::string aiTaskRequestJson;
            {
                nlohmann::json aiTaskRequest = nlohmann::json::object();
                aiTaskRequest["input"]["text"] = aiPrompt;
                aiTaskRequestJson = aiTaskRequest.dump();
            }

            const std::string kProtocolContext =
                "AgentChat AI provider. Read AI_TASK_REQUEST JSON and answer in concise Simplified Chinese.\n"
                "Use tools only when needed. For generated files/images/audio/pages, return a public http/https URL.\n"
                "Do not expose tool names, logs, code, PowerShell, base64, HTML errors, or debugging details.";

            std::string wrappedMessage = kProtocolContext;
            wrappedMessage += "\n\nAI_TASK_REQUEST:\n";
            wrappedMessage += aiTaskRequestJson;
            if (!aiPrompt.empty()) {
                wrappedMessage += "\n\nUser query:\n";
                wrappedMessage += aiPrompt;
            }

            // 3. 构建 HTTP POST 请求体
            nlohmann::json requestBody = nlohmann::json::object();
            requestBody["message"] = wrappedMessage;
            requestBody["sessionKey"] = "main";
            requestBody["conversationId"] = channel;
            requestBody["timeoutMs"] = 10000;
            requestBody["pollTimeoutMs"] = 300000;
            requestBody["stream"] = false;
            requestBody["ts"] = static_cast<int64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            requestBody["rawMessage"] = aiPrompt;

            const std::string requestBodyJson = requestBody.dump();
            TRACE(_T("[CChatRoomBridge] SendOpenClawAgentRequest: POST %hs:%d%hs body_len=%zu\n"),
                  openclawHost.c_str(), openclawPort, openclawPath.c_str(), requestBodyJson.size());

            // 4. 发送 HTTP POST 请求到 OpenClaw 服务
            DWORD statusCode = 0;
            const std::string responseRaw = HttpPostJson(
                std::wstring(openclawHost.begin(), openclawHost.end()),
                static_cast<INTERNET_PORT>(openclawPort),
                std::wstring(openclawPath.begin(), openclawPath.end()),
                requestBodyJson, timeoutMs, statusCode);

            TRACE(_T("[CChatRoomBridge] SendOpenClawAgentRequest: response status=%lu body=%hs\n"),
                  statusCode, responseRaw.c_str());

            // 5. 解析 OpenClaw 响应，提取 AI 回复文本
            if (statusCode == 200 && !responseRaw.empty()) {
                try {
                    auto responseJson = nlohmann::json::parse(responseRaw);
                    std::string aiReply;

                    bool ok = responseJson.contains("ok") && responseJson["ok"].is_boolean()
                        ? responseJson["ok"].get<bool>() : true;
                    if (!ok) {
                        std::string err = responseJson.contains("error") && responseJson["error"].is_string()
                            ? responseJson["error"].get<std::string>() : "OpenClaw returned error";
                        TRACE(_T("[CChatRoomBridge] SendOpenClawAgentRequest: business error=%hs\n"), err.c_str());
                    } else {
                        // 按优先级提取回复文本：text > reply > message > content > raw
                        if (responseJson.contains("text") && responseJson["text"].is_string()) {
                            aiReply = responseJson["text"].get<std::string>();
                        } else if (responseJson.contains("reply") && responseJson["reply"].is_string()) {
                            aiReply = responseJson["reply"].get<std::string>();
                        } else if (responseJson.contains("message") && responseJson["message"].is_string()) {
                            aiReply = responseJson["message"].get<std::string>();
                        } else if (responseJson.contains("content") && responseJson["content"].is_string()) {
                            aiReply = responseJson["content"].get<std::string>();
                        } else {
                            aiReply = responseRaw;
                        }
                    }

                    // 6. 将 AI 回复以 Agent 身份广播到频道（session_id=0, from=炎图AI助手）
                    // 先写入追踪集，push 回调收到回显时会覆写 sender 为"炎图AI助手"
                    if (!aiReply.empty()) {
                        {
                            std::lock_guard<std::mutex> lock(pending_agent_replies_mutex_);
                            pending_agent_replies_.insert(channel + ":" + aiReply);
                        }
                        TRACE(_T("[CChatRoomBridge] SendOpenClawAgentRequest: broadcasting AI reply to %hs\n"),
                              channel.c_str());
                        auto& transport = CIrcChatTransport::Instance();
                        transport.SendPrivmsgAsAgentNoWait(channel, aiReply, "炎图AI助手");
                    }
                } catch (const nlohmann::json::parse_error& e) {
                    TRACE(_T("[CChatRoomBridge] SendOpenClawAgentRequest: JSON parse error=%hs\n"), e.what());
                }
            } else if (statusCode >= 500) {
                TRACE(_T("[CChatRoomBridge] SendOpenClawAgentRequest: server error status=%lu\n"), statusCode);
            } else {
                TRACE(_T("[CChatRoomBridge] SendOpenClawAgentRequest: unexpected status=%lu, check OpenClaw server at %hs:%d%hs\n"),
                      statusCode, openclawHost.c_str(), openclawPort, openclawPath.c_str());
            }
        } catch (const std::exception& ex) {
            TRACE(_T("[CChatRoomBridge] SendOpenClawAgentRequest: exception=%hs\n"), ex.what());
        }
    }).detach();
}

} // namespace blazeclaw::irc
