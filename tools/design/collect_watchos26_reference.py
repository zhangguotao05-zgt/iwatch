"""采集固定版本的 Apple 官方公开配图索引；不生成或改画图标。"""

import concurrent.futures
import hashlib
import json
import re
import struct
import time
import urllib.request
from datetime import datetime, timezone
from html.parser import HTMLParser
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CACHE = ROOT / "work/watchos26-reference-20260923"
OUT = ROOT / "docs/assets/watchos26-review"
TOC = "https://support.apple.com/zh-cn/guide/watch/toc/26"


class Elements(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.links = []
        self.images = []
        self.link = None
        self.heading = None
        self.headings = []
        self.figure_depth = 0

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag == "a":
            self.link = {"href": a.get("href", ""), "text": ""}
        if tag in ("h1", "h2", "h3"):
            self.heading = {"level": tag, "text": ""}
        if tag == "figure":
            self.figure_depth += 1
        if tag == "img" and a.get("src", "").startswith("https://help.apple.com/assets/"):
            self.images.append({
                "url": a["src"], "alt": a.get("alt", ""),
                "width": int(a.get("width", "0")), "height": int(a.get("height", "0")),
                "original_name": a.get("originalimagename", ""),
                "heading": self.headings[-1]["text"] if self.headings else "",
                "figure": self.figure_depth > 0,
            })

    def handle_endtag(self, tag):
        if tag == "a" and self.link is not None:
            self.link["text"] = " ".join(self.link["text"].split())
            self.links.append(self.link)
            self.link = None
        if tag in ("h1", "h2", "h3") and self.heading is not None:
            self.heading["text"] = " ".join(self.heading["text"].split())
            self.headings.append(self.heading)
            self.heading = None
        if tag == "figure":
            self.figure_depth = max(0, self.figure_depth - 1)

    def handle_data(self, data):
        if self.link is not None:
            self.link["text"] += data
        if self.heading is not None:
            self.heading["text"] += data


def get(url):
    for attempt in range(3):
        try:
            request = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(request, timeout=35) as response:
                return response.read(), response.geturl()
        except Exception:
            if attempt == 2:
                raise
            time.sleep(attempt + 1)


def topic(link):
    url = link["href"]
    identity = url.split("/guide/watch/")[1].split("/")[0]
    cached = CACHE / f"{identity}.html"
    try:
        if cached.exists():
            raw = cached.read_bytes()
            final_url = url
        else:
            raw, final_url = get(url)
            cached.write_bytes(raw)
        parser = Elements()
        parser.feed(raw.decode("utf-8"))
        if "/26/" not in final_url:
            raise ValueError("版本固定链接发生了跨版本跳转：" + final_url)
        return {"id": identity, "title": link["text"], "url": url,
                "html_sha256": hashlib.sha256(raw).hexdigest(),
                "images": parser.images, "error": None}
    except Exception as exc:
        return {"id": identity, "title": link["text"], "url": url,
                "images": [], "error": str(exc)}


def archive_image(image):
    """原样保存公开配图，记录实际像素与哈希；不裁切、放大或重新压缩。"""
    name = hashlib.sha256(image["url"].encode()).hexdigest()[:16] + ".png"
    target = OUT / "originals" / name
    try:
        raw = target.read_bytes() if target.exists() else get(image["url"])[0]
        if not raw.startswith(b"\x89PNG\r\n\x1a\n"):
            raise ValueError("响应不是 PNG 原图")
        if not target.exists():
            target.write_bytes(raw)
        width, height = struct.unpack(">II", raw[16:24])
        return {**image, "local_name": name, "bytes": len(raw),
                "sha256": hashlib.sha256(raw).hexdigest(),
                "pixel_width": width, "pixel_height": height, "asset_error": None}
    except Exception as exc:
        return {**image, "asset_error": str(exc)}


def main():
    CACHE.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "originals").mkdir(exist_ok=True)
    raw, _ = get(TOC)
    (CACHE / "toc.html").write_bytes(raw)
    parser = Elements()
    parser.feed(raw.decode("utf-8"))
    links = {}
    for link in parser.links:
        if re.fullmatch(r"https://support.apple.com/zh-cn/guide/watch/[a-z0-9]+/26/watchos/26", link["href"]):
            links.setdefault(link["href"], link)
    print(f"目录主题：{len(links)}", flush=True)
    results = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:
        for index, result in enumerate(pool.map(topic, links.values()), 1):
            results[result["url"]] = result
            if index % 20 == 0 or result["error"]:
                print(f"已核对 {index}/{len(links)}；{result['title']}；图片 {len(result['images'])}；错误 {result['error']}", flush=True)
    topics = list(results.values())
    images = {}
    for page in topics:
        for item in page.pop("images"):
            entry = images.setdefault(item["url"], {**item, "topics": []})
            if page["id"] not in entry["topics"]:
                entry["topics"].append(page["id"])
        page["images"] = [url for url, entry in images.items() if page["id"] in entry["topics"]]
    archived = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        for index, result in enumerate(pool.map(archive_image, images.values()), 1):
            archived.append(result)
            if index % 60 == 0 or result["asset_error"]:
                print(f"原图归档 {index}/{len(images)}；错误 {result['asset_error']}", flush=True)
    data = {"version": "watchOS 26", "locale": "zh-CN", "collected_at": datetime.now(timezone.utc).isoformat(),
            "toc_url": TOC, "toc_sha256": hashlib.sha256(raw).hexdigest(),
            "scope": "官方固定版本中文使用手册目录中的公开主题和配图；非全部运行时页面／全部 SF Symbols",
            "topics": topics, "images": archived}
    serialized = json.dumps(data, ensure_ascii=False, indent=2)
    (OUT / "sources.json").write_text(serialized + "\n", encoding="utf-8")
    (OUT / "sources.js").write_text("window.WATCH_REFERENCE = " + serialized.replace("</", "<\\/") + ";\n", encoding="utf-8")
    print(json.dumps({"topics": len(topics), "images": len(images), "large_images": sum(i['width'] >= 120 or i['height'] >= 120 for i in images.values()),
                      "errors": [t for t in topics if t['error']],
                      "asset_errors": [i for i in archived if i['asset_error']]}, ensure_ascii=False), flush=True)


if __name__ == "__main__":
    main()
