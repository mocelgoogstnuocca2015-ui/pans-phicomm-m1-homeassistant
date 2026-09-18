#!/usr/bin/env python3
"""生成仅替换 M1 用户应用、保留已验证内核前缀的兼容 OTA 包。"""

import argparse
import hashlib
import struct
from pathlib import Path


USER_APP_OFFSET = 0x75000
MD5_SIZE = 16
# 必须与 m1_ota.c 中 g_captive_prefix_md5 保持一致。
CAPTIVE_PREFIX_MD5 = "72adb44fcb95ca84b5729d1754c88266"


def crc16_xmodem(data: bytes) -> int:
    crc = 0
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc


def read_checked_base(path: Path) -> bytes:
    data = path.read_bytes()
    if len(data) <= USER_APP_OFFSET + MD5_SIZE:
        raise ValueError("原厂 OTA 包长度不足")
    if hashlib.md5(data[:-MD5_SIZE]).digest() != data[-MD5_SIZE:]:
        raise ValueError("原厂 OTA 包尾部 MD5 不匹配")
    return data


def validate_prefix(data: bytes) -> bytes:
    if len(data) != USER_APP_OFFSET:
        raise ValueError("内核前缀长度必须恰好为 0x75000 字节")
    if hashlib.md5(data).hexdigest() != CAPTIVE_PREFIX_MD5:
        raise ValueError("内核前缀不是当前已验证的门户内核，拒绝生成 OTA")
    return data


def read_checked_prefix(path: Path) -> bytes:
    return validate_prefix(path.read_bytes())


def read_checked_user_app(path: Path) -> bytes:
    data = path.read_bytes()
    if len(data) < 8:
        raise ValueError("用户应用包长度不足")
    payload_length, crc_a, crc_b = struct.unpack_from("<IHH", data, 0)
    if (crc_a != crc_b) or (payload_length + 8 != len(data)):
        raise ValueError("用户应用头长度或双 CRC 字段无效")
    if crc16_xmodem(data[8:]) != crc_a:
        raise ValueError("用户应用有效载荷 CRC 不匹配")
    return data


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    prefix_group = parser.add_mutually_exclusive_group(required=True)
    prefix_group.add_argument("--base-ota", type=Path,
                              help="完整兼容 OTA 包；仅取其前 0x75000 字节")
    prefix_group.add_argument("--kernel-prefix", type=Path,
                              help="已校验的 0x75000 字节内核前缀")
    parser.add_argument("--user-app", type=Path, required=True, help="当前构建的 .usr.bin")
    parser.add_argument("--output", type=Path, required=True, help="输出兼容 OTA 包")
    args = parser.parse_args()

    if args.kernel_prefix is not None:
        prefix = read_checked_prefix(args.kernel_prefix)
    else:
        prefix = validate_prefix(read_checked_base(args.base_ota)[:USER_APP_OFFSET])
    user_app = read_checked_user_app(args.user_app)
    image_without_md5 = prefix + user_app
    output = image_without_md5 + hashlib.md5(image_without_md5).digest()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)

    print("兼容 OTA 已生成")
    print("长度={}".format(len(output)))
    print("内核前缀 MD5={}".format(hashlib.md5(prefix).hexdigest()))
    print("SHA256={}".format(hashlib.sha256(output).hexdigest()))
    print("MD5={}".format(hashlib.md5(output).hexdigest()))


if __name__ == "__main__":
    main()
