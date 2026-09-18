#!/usr/bin/env python3
"""生成 M1-Setup 的 DHCP DNS 补丁内核。

补丁仅针对经确认的斐讯 M1 zM1 兼容 OTA 基线（非斐讯官方出厂固件）：
在 MX1290 内核的 DHCP 服务端共享 OFFER/ACK 构包函数中，将
可选的 Option 51（租约时间）替换为 Option 6（DNS），并复用原厂
的“写入接口地址”子程序填入 SoftAP 地址。Option 3（路由器）保留，
因此客户端仍可获得默认网关；客户端未收到租约时间时使用默认租约。

脚本默认失败即停止：必须匹配完整基线哈希、偏移、原始字节和上下文。
输出仅为 0x75000 字节内核区，不能直接当作整包 OTA 发布。
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


BASELINE_SHA256 = "20c5e6ae1692e3e047063b637b137635ab9e57711dba7c27ef0eb6cf1390887e"
KERNEL_SIZE = 0x75000
PATCH_OFFSET = 0x16B0C
# 原厂：写 Option 51 + 四字节租约时间。
# 补丁：写 Option 6 + 四字节 SoftAP 地址，随后跳过旧租约时间代码。
EXPECTED = bytes.fromhex("33 20 07 F8 06 0F DF F8 B0 04 00 68")
REPLACEMENT = bytes.fromhex("06 20 00 F0 1C F8 04 20 78 70 07 E0")
CONTEXT_OFFSET = PATCH_OFFSET - 8
EXPECTED_CONTEXT = bytes.fromhex(
    "FF F7 A6 FF 04 20 78 70 "
    "33 20 07 F8 06 0F DF F8 B0 04 00 68 F2 F7 2F FC "
    "01 46 B8 1C FF F7 98 FF 04 20 78 70 36 20 00 F0 0E F8 "
    "04 20 78 70 03 20 00 F0 09 F8"
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description="生成 M1 SoftAP DHCP DNS 补丁内核")
    parser.add_argument("input", type=Path, help="已合法取得且哈希匹配的 zM1 完整 OTA 基线")
    parser.add_argument("output", type=Path, help="输出的 0x75000 字节内核文件")
    args = parser.parse_args()

    source = args.input.read_bytes()
    source_hash = sha256(source)
    if source_hash != BASELINE_SHA256:
        raise SystemExit(f"拒绝处理：原厂基线 SHA-256 不匹配：{source_hash}")
    if len(source) < KERNEL_SIZE:
        raise SystemExit(f"拒绝处理：输入长度 {len(source)} 小于内核区 {KERNEL_SIZE}")
    if source[PATCH_OFFSET : PATCH_OFFSET + len(EXPECTED)] != EXPECTED:
        actual = source[PATCH_OFFSET : PATCH_OFFSET + len(EXPECTED)].hex(" ")
        raise SystemExit(f"拒绝处理：补丁原始字节不匹配：{actual}")
    actual_context = source[CONTEXT_OFFSET : CONTEXT_OFFSET + len(EXPECTED_CONTEXT)]
    if actual_context != EXPECTED_CONTEXT:
        raise SystemExit("拒绝处理：补丁上下文不匹配")

    kernel = bytearray(source[:KERNEL_SIZE])
    kernel[PATCH_OFFSET : PATCH_OFFSET + len(REPLACEMENT)] = REPLACEMENT
    changed = [index for index, (before, after) in enumerate(zip(source[:KERNEL_SIZE], kernel)) if before != after]
    expected_changed = [
        PATCH_OFFSET + index
        for index, (before, after) in enumerate(zip(EXPECTED, REPLACEMENT))
        if before != after
    ]
    if changed != expected_changed:
        raise SystemExit(f"拒绝处理：变更范围异常：{changed}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(kernel)
    print(f"输入 SHA-256: {source_hash}")
    print(f"输出 SHA-256: {sha256(kernel)}")
    print(f"内核长度: {len(kernel)}")
    print("变更：以 DHCP Option 6 替换 Option 51，并保留 Option 3（网关）")
    print("跳转目标：0x16B4A 原厂接口地址写入子程序；旧租约时间代码不再执行")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
