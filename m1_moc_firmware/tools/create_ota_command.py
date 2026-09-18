#!/usr/bin/env python3
"""生成斐讯 M1 通用兼容 OTA 的 MQTT 命令。"""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ota", type=Path, required=True, help="兼容 OTA 文件")
    parser.add_argument("--url", required=True, help="HA 上该文件的 HTTP URL")
    parser.add_argument("--payload-only", action="store_true", help="仅输出可直接发布的 MQTT 载荷")
    args = parser.parse_args()

    if not args.url.startswith("http://") or any(c.isspace() or c == "|" for c in args.url):
        raise SystemExit("URL 必须是无空格的 HTTP 下载地址")
    if not args.ota.is_file():
        raise SystemExit("OTA 文件不存在")
    file_md5 = hashlib.md5(args.ota.read_bytes()).hexdigest()
    payload = f"{args.url}|{file_md5}"
    if args.payload_only:
        # 可直接管道给 mosquitto_pub -s；末尾换行会使设备端的 MD5 字段失效。
        sys.stdout.write(payload)
    else:
        print(f"镜像 MD5: {file_md5}")
        print(f"命令载荷: {payload}")


if __name__ == "__main__":
    main()
