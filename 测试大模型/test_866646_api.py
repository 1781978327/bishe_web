#!/usr/bin/env python3
import argparse
import base64
import json
import os
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path


WORK_DIR = Path("/home/orangepi/Desktop/web/测试大模型")
PYTHON_EXAMPLE = WORK_DIR / "new.py"
DEFAULT_BASE_URL = "https://api.866646.xyz/"
DEFAULT_MODEL = "qwen3-vl:235b-instruct"


def read_quoted_assignment(path: Path, variable_name: str) -> str | None:
    if not path.exists():
        return None
    text = path.read_text(encoding="utf-8")
    pattern = re.compile(rf'(?m)^\s*{re.escape(variable_name)}\s*=\s*(?:r)?"([^"]*)"')
    match = pattern.search(text)
    return match.group(1) if match else None


def first_non_blank(*values: str | None) -> str | None:
    for value in values:
        if value and value.strip():
            return value.strip()
    return None


def trim_trailing_slash(value: str) -> str:
    return value.rstrip("/")


def build_opener(proxy_url: str | None):
    if not proxy_url:
        return urllib.request.build_opener()
    proxy_map = {"http": proxy_url, "https": proxy_url}
    return urllib.request.build_opener(urllib.request.ProxyHandler(proxy_map))


def do_request(opener, url: str, api_key: str, payload: dict | None = None) -> tuple[int, str]:
    data = None
    headers = {
        "Authorization": f"Bearer {api_key}",
    }
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"
    request = urllib.request.Request(url=url, data=data, headers=headers, method="POST" if data else "GET")
    try:
        with opener.open(request, timeout=120) as response:
            return response.getcode(), response.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        return exc.code, body


def make_image_content(image_path: Path) -> dict:
    image_bytes = image_path.read_bytes()
    mime = "image/jpeg"
    suffix = image_path.suffix.lower()
    if suffix == ".png":
        mime = "image/png"
    elif suffix == ".webp":
        mime = "image/webp"
    encoded = base64.b64encode(image_bytes).decode("utf-8")
    return {
        "type": "image_url",
        "image_url": {
            "url": f"data:{mime};base64,{encoded}"
        }
    }


def build_payload(model: str, prompt: str, image_path: Path | None) -> dict:
    if image_path is None:
        return {
            "model": model,
            "messages": [
                {
                    "role": "user",
                    "content": prompt,
                }
            ],
        }

    return {
        "model": model,
        "messages": [
            {
                "role": "user",
                "content": [
                    {"type": "text", "text": prompt},
                    make_image_content(image_path),
                ],
            }
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Local test script for https://api.866646.xyz/")
    parser.add_argument("--api-key", help="API key, or use VISION_API_KEY, or fallback to new.py")
    parser.add_argument("--base-url", help="Base URL, default https://api.866646.xyz/")
    parser.add_argument("--model", default=DEFAULT_MODEL, help=f"Model name, default {DEFAULT_MODEL}")
    parser.add_argument("--prompt", default="Hello, please reply briefly.", help="User prompt")
    parser.add_argument("--image", help="Optional local image path for a multimodal request")
    parser.add_argument("--proxy", help="Optional proxy URL, or use HTTPS_PROXY/HTTP_PROXY")
    parser.add_argument("--list-models", action="store_true", help="Call GET /v1/models only")
    parser.add_argument("--print-payload", action="store_true", help="Print JSON payload before request")
    args = parser.parse_args()

    api_key = first_non_blank(
        args.api_key,
        os.getenv("VISION_API_KEY"),
        read_quoted_assignment(PYTHON_EXAMPLE, "api_key"),
    )
    if not api_key:
        print("Missing API key. Use --api-key or VISION_API_KEY.")
        return 1

    base_url = first_non_blank(
        args.base_url,
        os.getenv("VISION_BASE_URL"),
        read_quoted_assignment(PYTHON_EXAMPLE, "base_url"),
        DEFAULT_BASE_URL,
    )
    proxy_url = first_non_blank(
        args.proxy,
        os.getenv("HTTPS_PROXY"),
        os.getenv("HTTP_PROXY"),
    )

    image_path = None
    if args.image:
        image_path = Path(args.image)
    else:
        fallback_image = read_quoted_assignment(PYTHON_EXAMPLE, "image_path")
        if fallback_image:
            image_path = Path(fallback_image)
    if image_path and not image_path.exists():
        print(f"Image not found: {image_path}")
        return 1

    opener = build_opener(proxy_url)
    final_base_url = trim_trailing_slash(base_url)

    print("=== 866646 Local Test ===")
    print(f"Base URL: {final_base_url}")
    print(f"Model: {args.model}")
    print(f"Proxy: {proxy_url or '(none)'}")
    print(f"Image: {image_path or '(none)'}")
    print(f"API key: {'set' if api_key else 'missing'}")
    print()

    if args.list_models:
        status, body = do_request(opener, f"{final_base_url}/v1/models", api_key)
        print(f"GET /v1/models -> HTTP {status}")
        print(body)
        return 0 if 200 <= status < 300 else 2

    payload = build_payload(args.model, args.prompt, image_path)
    if args.print_payload:
        preview = json.dumps(payload, ensure_ascii=False)
        if len(preview) > 3000:
            preview = preview[:3000] + "...(truncated)"
        print("=== Payload Preview ===")
        print(preview)
        print()

    status, body = do_request(opener, f"{final_base_url}/v1/chat/completions", api_key, payload)
    print(f"POST /v1/chat/completions -> HTTP {status}")
    print(body)
    return 0 if 200 <= status < 300 else 2


if __name__ == "__main__":
    raise SystemExit(main())
