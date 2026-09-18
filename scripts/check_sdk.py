#!/usr/bin/env python3
"""只读检查开发环境的 SDK 适配指纹，不下载、修改或刷写任何文件。"""
import argparse
import hashlib
from pathlib import Path

EXPECTED = {
    "MiCO/system/config_server/config_server.c": "0e49ce5306c3cf3e8069a31bc404763e6acb0c75d91ef799602787ee7f331613",
    "MiCO/system/easylink/system_easylink_softap.c": "29646d105c38cee8d6d12ff4fe288a028955296ba661635849a716e9460ab3e0",
    "MiCO/system/mico_system_init.c": "a090a2d1ba7f4e32ef9ef617afcbeaf32c7f090621205d4ead5165bc81da367f",
    "MiCO/system/qc_test/qc_test.mk": "2f6ff7a9db4943affb2c1c990fe4e05c5ff6641e0a18a9c66b581f5fa5450658",
    "libraries/daemons/ota_server/ota_server.c": "e85331c8e02bb0204d5684b5ab9a83812b0d634a8711d0a4f772e2594d617580",
    "libraries/daemons/ota_server/ota_server.h": "7e362bb31f39811c6a643c211a94d01d2682361b38cf92a5a00bc5a9943b95c2",
    "libraries/protocols/mqtt/MQTTClient.c": "fac30bd400011e364d1c50ad43f03a05bbd8c38b434c588c06cb591293f6ff2d",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    args = parser.parse_args()
    failed = False
    for relative, expected in EXPECTED.items():
        file = args.sdk / relative
        matched = file.is_file() and hashlib.sha256(file.read_bytes()).hexdigest() == expected
        print(("匹配：" if matched else "未匹配：") + relative)
        failed |= not matched
    if failed:
        print("请按 docs/开发与构建.md 核对依赖与适配；不要直接编译后刷写。")
    else:
        print("七个适配文件匹配；此检查不代表整个 SDK、内核或无线 OTA 已验收。")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
