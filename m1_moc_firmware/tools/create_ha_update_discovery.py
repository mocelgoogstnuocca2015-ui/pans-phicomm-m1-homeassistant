#!/usr/bin/env python3
"""生成斐讯 M1 的 Home Assistant MQTT Update Discovery 配置。

输出内容含有指定镜像、URL 和 MD5 的 OTA 载荷；请只写入受信任的 MQTT Broker。
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


def ota_payload(ota: Path, url: str) -> str:
    if not url.startswith("http://") or any(c.isspace() or c == "|" for c in url):
        raise ValueError("URL 必须是无空格的 HTTP 下载地址")
    image_md5 = hashlib.md5(ota.read_bytes()).hexdigest()
    return f"{url}|{image_md5}"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ota", type=Path, required=True, help="兼容 OTA 镜像")
    parser.add_argument("--url", required=True, help="设备下载镜像的 HTTP URL")
    parser.add_argument("--version", required=True, help="即将安装的固件版本")
    parser.add_argument("--device-id", required=True, help="设备 MQTT 标识，例如 m1_001122334455")
    parser.add_argument("--release-summary", required=True, help="不超过 255 字符的更新说明")
    parser.add_argument("--release-url", default="", help="可选的发布说明 URL")
    parser.add_argument("--output", type=Path, required=True, help="Discovery JSON 输出文件")
    args = parser.parse_args()

    if not re.fullmatch(r"m1_[0-9a-f]{12}", args.device_id):
        raise SystemExit("设备 ID 必须是 m1_ 加 12 位小写 MAC 十六进制，请从设备状态主题读取")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._+-]{0,63}", args.version):
        raise SystemExit("版本只允许字母、数字、点、下划线、加号或连字符，长度不超过 64")
    if not args.ota.is_file():
        raise SystemExit("兼容 OTA 镜像不存在")
    if len(args.release_summary) > 255:
        raise SystemExit("更新说明超过 Home Assistant 的 255 字符限制")

    state_topic = f"m1/{args.device_id}/state"
    availability_topic = f"m1/{args.device_id}/availability"
    config = {
        "name": "M1 固件更新",
        "unique_id": f"{args.device_id}_firmware_update",
        "device_class": "firmware",
        "entity_category": "diagnostic",
        "state_topic": state_topic,
        "value_template": (
            "{{ {'installed_version': value_json.firmware_version | default('unknown', true), "
            f"'latest_version': '{args.version}', "
            "'title': 'M1 EMW3080B 固件', "
            "'in_progress': value_json.ota_state == 1, "
            "'update_percentage': value_json.ota_progress if value_json.ota_state == 1 else none} | to_json }}"
        ),
        "command_topic": f"m1/{args.device_id}/ota/set",
        "payload_install": ota_payload(args.ota, args.url),
        "qos": 1,
        "availability_topic": availability_topic,
        "payload_available": "online",
        "payload_not_available": "offline",
        "release_summary": args.release_summary,
        "device": {
            "identifiers": [args.device_id],
            "name": "斐讯 M1",
            "model": "EMW3080B / MX1290",
            "manufacturer": "斐讯",
        },
    }
    if args.release_url:
        config["release_url"] = args.release_url

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(config, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")


if __name__ == "__main__":
    main()
