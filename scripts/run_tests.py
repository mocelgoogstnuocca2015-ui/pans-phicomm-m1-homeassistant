#!/usr/bin/env python3
"""运行公开源码的离线主机回归，不访问硬件或网络。"""
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    for tool in ("gcc", "node"):
        if shutil.which(tool) is None:
            raise SystemExit(f"缺少 {tool}，请安装后加入 PATH。")
    out = ROOT / "output" / "host-tests"
    out.mkdir(parents=True, exist_ok=True)
    exe = out / ("parser_host_test.exe" if os.name == "nt" else "parser_host_test")
    commands = [
        ["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror",
         "m1_moc_firmware/tests/parser_host_test.c", "m1_moc_firmware/m1_sensor_parser.c", "-o", str(exe)],
        [str(exe)],
        ["node", "m1_moc_firmware/tests/scan_host_test.cjs"],
        [sys.executable, "-m", "unittest", "discover", "-s", "m1_moc_firmware/tests", "-p", "*_test.py", "-v"],
    ]
    for command in commands:
        subprocess.run(command, cwd=ROOT, check=True)
    print("全部主机测试通过；未进行实机刷写或无线 OTA。")


if __name__ == "__main__":
    main()
