#!/usr/bin/env python3
"""审查 Git 索引中的公开文件；只报告问题位置，绝不打印匹配到的秘密。"""
import argparse
from pathlib import Path, PurePosixPath
import posixpath
import re
import subprocess
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]
ROOT_FILES = {".gitignore", ".gitattributes", "README.md", "LICENSE",
              "THIRD_PARTY_NOTICES.md", "SECURITY.md", "CONTRIBUTING.md"}
TOOLS = {"pack_compatible_ota.py", "patch_captive_dhcp_kernel.py",
         "create_ota_command.py", "create_ha_update_discovery.py"}
TESTS = {"parser_host_test.c", "scan_host_test.cjs", "release_tools_test.py"}
TOKEN_PATTERNS = [
    rb"gh[pousr]_[A-Za-z0-9]{30,}",
    rb"github_pat_[A-Za-z0-9_]{30,}",
    rb"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----",
    rb"(?i)Authorization:\s*Bearer\s+[A-Za-z0-9._-]{20,}",
]


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def allowed(name):
    path = PurePosixPath(name)
    if name in ROOT_FILES:
        return True
    if path.parts[0] == "docs":
        return len(path.parts) == 2 and path.suffix == ".md"
    if path.parts[0] == "scripts":
        return len(path.parts) == 2 and path.suffix == ".py"
    if path.parts[0] == ".github":
        return len(path.parts) == 3 and path.parts[1] == "workflows" and path.suffix == ".yml"
    if path.parts[0] != "m1_moc_firmware":
        return False
    if len(path.parts) == 2:
        return path.suffix in {".c", ".h"} or path.name in {"README.md", "m1_moc_firmware.mk"}
    return len(path.parts) == 3 and (
        path.parts[1] == "tools" and path.name in TOOLS or
        path.parts[1] == "tests" and path.name in TESTS)


def secret_candidates(file):
    # 可额外传入私有密码文件，只在内存里比较，不输出内容，也不保存扫描副本。
    data = file.read_text(encoding="utf-8-sig").strip()
    values = {data}
    for line in data.splitlines():
        values.add(line.strip().strip('"\''))
        for separator in ("=", ":", "："):
            if separator in line:
                values.add(line.split(separator, 1)[1].strip().strip('"\''))
    return [value.encode("utf-8") for value in values if len(value) >= 6]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--private-file", action="append", type=Path, default=[])
    args = parser.parse_args()
    secrets = [value for file in args.private_file for value in secret_candidates(file)]
    names = [value.decode("utf-8") for value in git("ls-files", "-z").split(b"\0") if value]
    if not names:
        raise SystemExit("索引为空，请先按白名单暂存。")
    problems = []
    for name in names:
        if not allowed(name):
            problems.append((name, "不在公开白名单"))
            continue
        data = git("show", ":" + name)
        try:
            content = data.decode("utf-8")
        except UnicodeDecodeError:
            problems.append((name, "不是 UTF-8 文本"))
            continue
        if b"\0" in data or len(data) > 128 * 1024:
            problems.append((name, "二进制或异常大文件"))
        if any(re.search(pattern, data) for pattern in TOKEN_PATTERNS):
            problems.append((name, "疑似密钥或令牌"))
        if any(value in data for value in secrets):
            problems.append((name, "含私有文件中的候选内容"))
        if re.search(rb"[A-Za-z]:[\\/]Users[\\/]|192\.168\.(?:0|3)\.[1-9][0-9]*|m1_(?!001122334455\b)[0-9a-f]{12}\b", data, re.I):
            # 不输出具体值；开发机绝对目录、默认目标及测试设备标识不属于公共示例。
            problems.append((name, "疑似本地运行环境信息"))
        if name.endswith(".md"):
            for link in re.findall(r"\]\(([^)]+)\)", content):
                if "://" in link or link.startswith("#"):
                    continue
                relative = unquote(link.split("#", 1)[0])
                target = posixpath.normpath(posixpath.join(posixpath.dirname(name), relative))
                if target not in names:
                    problems.append((name, "相对链接未指向已暂存的公开文件"))
    for name, reason in problems:
        print(f"拒绝：{name}：{reason}")
    if problems:
        return 1
    print(f"索引检查通过：{len(names)} 个文本文件；仍需人工审查，不能保证发现所有秘密。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
