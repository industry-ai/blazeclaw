#include "pch.h"
#include "SkillsPromptFormatterCompat.h"

#include <sstream>

namespace blazeclaw::core {

	std::wstring EscapeSkillPromptXmlCompat(const std::wstring& value) {
		std::wstring escaped;
		escaped.reserve(value.size());
		for (const wchar_t ch : value) {
			switch (ch) {
			case L'&':
				escaped += L"&amp;";
				break;
			case L'<':
				escaped += L"&lt;";
				break;
			case L'>':
				escaped += L"&gt;";
				break;
			case L'"':
				escaped += L"&quot;";
				break;
			case L'\'':
				escaped += L"&apos;";
				break;
			default:
				escaped.push_back(ch);
				break;
			}
		}

		return escaped;
	}

	std::wstring FormatSkillsForPromptCompat(
		const std::vector<SkillPromptProjectionCompat>& skills) {
		if (skills.empty()) {
			return {};
		}

		std::wstringstream builder;
		builder
			<< L"\n\nThe following skills provide specialized instructions for specific tasks.\n"
			<< L"Use the read tool to load a skill's file when the task matches its description.\n"
			<< L"When a skill file references a relative path, resolve it against the skill directory (parent of SKILL.md / dirname of the path) and use that absolute path in tool commands.\n\n"
			<< L"<available_skills>\n";

		for (const auto& skill : skills) {
			builder << L"  <skill>\n";
			builder << L"    <name>" << EscapeSkillPromptXmlCompat(skill.name)
				<< L"</name>\n";
			builder << L"    <description>"
				<< EscapeSkillPromptXmlCompat(skill.description)
				<< L"</description>\n";
			builder << L"    <location>"
				<< EscapeSkillPromptXmlCompat(skill.filePath.wstring())
				<< L"</location>\n";
			builder << L"  </skill>\n";
		}

		builder << L"</available_skills>";
		return builder.str();
	}

	std::wstring FormatSkillsForPromptCompactCompat(
		const std::vector<SkillPromptProjectionCompat>& skills) {
		if (skills.empty()) {
			return {};
		}

		std::wstringstream builder;
		builder
			<< L"\n\nThe following skills provide specialized instructions for specific tasks.\n"
			<< L"Use the read tool to load a skill's file when the task matches its description.\n"
			<< L"When a skill file references a relative path, resolve it against the skill directory (parent of SKILL.md / dirname of the path) and use that absolute path in tool commands.\n\n"
			<< L"<available_skills>\n";

		for (const auto& skill : skills) {
			builder << L"  <skill>\n";
			builder << L"    <name>" << EscapeSkillPromptXmlCompat(skill.name)
				<< L"</name>\n";
			builder << L"    <location>"
				<< EscapeSkillPromptXmlCompat(skill.filePath.wstring())
				<< L"</location>\n";
			builder << L"  </skill>\n";
		}

		builder << L"</available_skills>";
		return builder.str();
	}

} // namespace blazeclaw::core
