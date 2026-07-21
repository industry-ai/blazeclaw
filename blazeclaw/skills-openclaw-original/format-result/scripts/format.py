"""
Format result — outputs standardized JSON response.
Usage: python client.py --skill-id tts --skill-name "tts" --url "https://..."
       python client.py --skill-id stt --skill-name "stt" --text "..." --url "..."
       python client.py --skill-id tts --skill-name "tts" --error "failed"
"""
import argparse
import json


def main():
    parser = argparse.ArgumentParser(prog="format-result")
    parser.add_argument("--skill-id", required=True)
    parser.add_argument("--skill-name", required=True)
    parser.add_argument("--url", default=None)
    parser.add_argument("--text", default=None)
    parser.add_argument("--text-title", default=None)
    parser.add_argument("--url-title", default=None)
    parser.add_argument("--error", default=None)

    args = parser.parse_args()

    if args.error:
        result = {
            "providerId": "openclaw",
            "skillId": args.skill_id,
            "skillName": args.skill_name,
            "status": "error",
            "summary": args.error,
            "outputs": [],
        }
    else:
        outputs = []
        if args.text is not None:
            outputs.append({
                "type": "text",
                "title": args.text_title or "结果",
                "text": args.text,
                "url": None,
            })
        if args.url:
            outputs.append({
                "type": "webview",
                "title": args.url_title or "文件预览",
                "url": args.url,
            })

        result = {
            "providerId": "openclaw",
            "skillId": args.skill_id,
            "skillName": args.skill_name,
            "status": "done",
            "summary": "处理完成",
            "outputs": outputs,
        }

    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
