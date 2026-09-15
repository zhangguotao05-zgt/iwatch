from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
EXTERNAL_SCHEMES = {"http", "https", "mailto", "tel", "data", "javascript"}
FORBIDDEN = (
    re.compile(r"\bChatGPT\b", re.I),
    re.compile(r"\bCodex\b", re.I),
    re.compile(r"@oai/", re.I),
    re.compile(r"AI\s*(生成|撰写|编写)", re.I),
    re.compile(r"人工智能\s*(生成|撰写|编写)"),
)


class DocumentParser(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.links = []
        self.ids = set()
        self.text = []

    def handle_starttag(self, tag, attrs):
        values = dict(attrs)
        if "id" in values:
            self.ids.add(values["id"])
        for name in ("href", "src"):
            if name in values:
                self.links.append(values[name])

    def handle_data(self, data):
        self.text.append(data)


def parse(path):
    parser = DocumentParser()
    parser.feed(path.read_text(encoding="utf-8"))
    return parser


def main():
    documents = [ROOT / "index.html", ROOT / "当前版本.html", ROOT / "firmware/README.html"]
    documents += sorted((ROOT / "docs").rglob("*.html"))
    documents += sorted((ROOT / "hardware").rglob("*.html"))
    documents = [p for p in documents if p.is_file()]
    parsed = {p.resolve(): parse(p) for p in documents}
    errors = []
    for path, doc in parsed.items():
        relative = path.relative_to(ROOT)
        visible_text = " ".join(doc.text)
        for pattern in FORBIDDEN:
            if pattern.search(visible_text):
                errors.append(f"{relative}: prohibited authorship marker: {pattern.pattern}")
        for raw_link in doc.links:
            link = urlsplit(raw_link)
            if link.scheme.lower() in EXTERNAL_SCHEMES or raw_link.startswith("//"):
                continue
            target = path if not link.path else (path.parent / unquote(link.path)).resolve()
            if not target.exists():
                errors.append(f"{relative}: missing local target: {raw_link}")
                continue
            if link.fragment and target.suffix.lower() == ".html":
                target_doc = parsed.get(target) or parse(target)
                if unquote(link.fragment) not in target_doc.ids:
                    errors.append(f"{relative}: missing fragment: {raw_link}")
    if not (ROOT / "index.html").is_file():
        errors.append("index.html is missing")
    for error in errors:
        print(f"HTML ERROR: {error}")
    if errors:
        return 1
    print(f"HTML checks passed: {len(documents)} documents, local links and anchors valid")
    return 0


if __name__ == "__main__":
    sys.exit(main())
