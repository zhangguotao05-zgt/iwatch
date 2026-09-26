# -*- coding: utf-8 -*-
"""从已核验的离线目录生成目标固件所用的资源查询接口。"""

import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REPORT = ROOT / "docs/ui/assets/v00/resource-bridge.json"
GENERATED = ROOT / "work/v00/resource-bridge/generated/catalog"
TARGET = ROOT / "firmware/iwatch/src/resource/images"


def write_or_check(path, data, check):
    if check:
        if path.read_bytes() != data:
            raise ValueError("目标资源目录已过期：{}".format(path))
    else:
        path.write_bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    report = json.loads(REPORT.read_text(encoding="utf-8"))
    source = (GENERATED / "iw_v00_resource_catalog.c").read_text(encoding="utf-8")
    header = (GENERATED / "iw_v00_resource_catalog.h").read_text(encoding="utf-8")
    digest = hashlib.sha256(source.encode("utf-8")).hexdigest()
    if digest != report["foreground_catalog_sha256"]:
        raise ValueError("离线目录与已核验资源报告不一致")
    if len([r for r in report["resources"] if r["role"] != "optional_background"]) != 37:
        raise ValueError("前景资源数量已变化")

    header = header.replace('#include "iw_v00_resource_guard.h"',
                            '#include "../iw_v00_resource_guard.h"\n#include "lvgl.h"')
    header = header.replace("size_t iw_v00_resource_catalog_count(void);",
                            "size_t iw_v00_resource_catalog_count(void);\n"
                            "const lv_image_dsc_t *iw_v00_resource_catalog_image(size_t index);")
    source = source.replace(
        "    return iw_v00_resource_validate_binding(&entry->binding, &view, for_release);",
        "    if (image->header.magic != LV_IMAGE_HEADER_MAGIC ||\n"
        "        image->header.flags != LV_IMAGE_FLAGS_EZIP)\n"
        "        return IW_V00_RESOURCE_BINDING;\n"
        "    return iw_v00_resource_validate_binding(&entry->binding, &view, for_release);")
    source += ("\nconst lv_image_dsc_t *iw_v00_resource_catalog_image(size_t index)\n"
               "{\n    return index < iw_v00_resource_catalog_count() ? "
               "catalog[index].image : NULL;\n}\n")
    write_or_check(TARGET / "iw_v00_resource_catalog.h", header.encode("utf-8"), args.check)
    write_or_check(TARGET / "iw_v00_resource_catalog.c", source.encode("utf-8"), args.check)
    print("V00 目标资源目录：37 项，来源 SHA-256 {}".format(digest))


if __name__ == "__main__":
    main()
