#include "pch.h"
#include "ServiceManagerRoutingIntentHelpers.h"

#include "ServiceManagerTextHelpers.h"

#include <algorithm>
#include <cctype>

namespace blazeclaw::core::servicemanager_routing_intent {

	namespace {

		std::string ToLowerAscii(const std::string& value)
		{
			std::string lowered = value;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](const unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return lowered;
		}

		bool ContainsAnyFragment(
			const std::string& lowerText,
			std::initializer_list<const char*> fragments)
		{
			for (const auto* fragment : fragments)
			{
				if (fragment == nullptr || *fragment == '\0')
				{
					continue;
				}
				if (lowerText.find(fragment) != std::string::npos)
				{
					return true;
				}
			}
			return false;
		}

		bool ContainsAnyWideFragment(
			const std::wstring& text,
			std::initializer_list<const wchar_t*> fragments)
		{
			for (const auto* fragment : fragments)
			{
				if (fragment == nullptr || *fragment == L'\0')
				{
					continue;
				}
				if (text.find(fragment) != std::wstring::npos)
				{
					return true;
				}
			}
			return false;
		}

	} // namespace

	std::string CanonicalizeForRouting(const std::string& message)
	{
		std::string canonical = message;
		const std::wstring wide = servicemanager_text::Utf8ToWideLocal(message);
		auto appendToken = [&](const char* token) {
			if (token == nullptr || *token == '\0')
			{
				return;
			}
			if (canonical.find(token) == std::string::npos)
			{
				canonical.append(" ");
				canonical.append(token);
			}
		};

		if (ContainsAnyWideFragment(
			wide,
			{ L"邮箱", L"收件箱", L"邮件", L"新邮件", L"查邮箱", L"查一下邮箱" }))
		{
			appendToken("inbox");
			appendToken("email");
		}
		if (ContainsAnyWideFragment(
			wide,
			{ L"回复", L"回信", L"需要回复", L"尽快回复" }))
		{
			appendToken("reply");
		}
		if (ContainsAnyWideFragment(
			wide,
			{ L"2小时", L"两小时", L"两个小时" }))
		{
			appendToken("within 2 hours");
		}
		return canonical;
	}

	bool LooksLikeInboxReplyUrgencyIntent(const std::string& message)
	{
		const std::string lower = ToLowerAscii(message);
		const std::wstring wide = servicemanager_text::Utf8ToWideLocal(message);
		const bool inboxSignalZh = ContainsAnyWideFragment(
			wide,
			{ L"邮箱", L"收件箱", L"邮件", L"新邮件", L"查邮箱", L"查一下邮箱" });
		const bool inboxSignal = ContainsAnyFragment(
			lower,
			{ "inbox", "unread", "mailbox", "email", "mail" }) || inboxSignalZh;
		const bool replySignalZh = ContainsAnyWideFragment(
			wide,
			{ L"回复", L"回信", L"需要回复", L"尽快回复" });
		const bool replySignal = ContainsAnyFragment(
			lower,
			{ "reply", "respond", "needs a reply", "need a reply" }) ||
			replySignalZh;
		const bool urgencySignalZh = ContainsAnyWideFragment(
			wide,
			{ L"2小时", L"两小时", L"两个小时", L"紧急", L"尽快" });
		const bool urgencySignal = ContainsAnyFragment(
			lower,
			{ "within 2 hours", "within two hours", "2h", "2 hours", "urgent" }) ||
			urgencySignalZh;
	return (inboxSignal && replySignal) || (inboxSignal && urgencySignal);
	}

	bool LooksLikeInboxIntentAnyLanguage(const std::string& message)
	{
		const std::string lower = ToLowerAscii(message);
		const std::wstring wide = servicemanager_text::Utf8ToWideLocal(message);
		const bool inboxSignalZh = ContainsAnyWideFragment(
			wide,
			{ L"邮箱", L"收件箱", L"邮件", L"新邮件", L"查邮箱", L"查一下邮箱" });
		const bool replySignalZh = ContainsAnyWideFragment(
			wide,
			{ L"回复", L"回信", L"需要回复", L"尽快回复" });
		const bool inboxSignal = ContainsAnyFragment(
			lower,
			{ "inbox", "unread", "mailbox", "email", "mail" }) || inboxSignalZh;
		const bool replySignal = ContainsAnyFragment(
			lower,
			{ "reply", "respond", "needs a reply", "need a reply" }) ||
			replySignalZh;
		return inboxSignal && replySignal;
	}

	bool LooksLikeTwoHourUrgencyAnyLanguage(const std::string& message)
	{
		const std::string lower = ToLowerAscii(message);
		const std::wstring wide = servicemanager_text::Utf8ToWideLocal(message);
		return ContainsAnyFragment(
			lower,
			{ "within 2 hours", "within two hours", "2h", "2 hours" }) ||
			ContainsAnyWideFragment(wide, { L"2小时", L"两小时", L"两个小时" });
	}

} // namespace blazeclaw::core::servicemanager_routing_intent
