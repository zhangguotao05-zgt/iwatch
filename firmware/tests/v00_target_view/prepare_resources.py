"""将已有 GCC 生成描述符机械转换为 MSVC 可读语法，不转换原素材。"""
import argparse
import hashlib
import json
from pathlib import Path
import re

p = argparse.ArgumentParser()
p.add_argument("source", type=Path)
a = p.parse_args()
out = Path(__file__).parent / "generated"
out.mkdir(exist_ok=False)
records = []
for source in sorted(a.source.glob("v00_*.tmp.c")):
    original = source.read_bytes()
    text = original.decode("utf-8-sig")
    text = '#define ALIGN(n)\n#define SECTION(s)\n' + text
    fields = list(re.finditer(r"\s*\.header\.(\w+)\s*=\s*([^,]+),", text))
    assert len(fields) == 7, (source, len(fields))
    header = "\n.header = {" + ",".join("." + m[1] + "=" + m[2] for m in fields) + "},"
    text = text[:fields[0].start()] + header + text[fields[-1].end():]
    if not records:
        text = '#ifndef IW_TEST_RESOURCE_MAGIC\n#define IW_TEST_RESOURCE_MAGIC 0x19\n#endif\n' + text
        text = text.replace('.magic=0x19', '.magic=IW_TEST_RESOURCE_MAGIC')
    target = out / source.name.replace(".tmp", "")
    target.write_text(text, encoding="utf-8", newline="\n")
    records.append({"input": str(source.resolve()), "input_sha256": hashlib.sha256(original).hexdigest(),
                    "output": target.name, "output_sha256": hashlib.sha256(target.read_bytes()).hexdigest()})
assert len(records) == 37, len(records)
(out / "identity.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
print(f"Preserved payloads and descriptor fields: {len(records)}")
