"""发布工具的离线输入与输出回归；使用临时合成数据，不包含真实设备镜像。"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1] / "tools"
spec = importlib.util.spec_from_file_location("pack", TOOLS / "pack_compatible_ota.py")
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)


class ReleaseToolsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.image = self.root / "synthetic.bin"
        self.image.write_bytes(b"not-a-real-device-image")

    def cli(self, name, *args):
        return subprocess.run([sys.executable, str(TOOLS / name), *map(str, args)],
                              capture_output=True, text=True, encoding="utf-8",
                              env={**os.environ, "PYTHONIOENCODING": "utf-8"})

    def test_crc_known_vector(self):
        self.assertEqual(pack.crc16_xmodem(b"123456789"), 0x31C3)

    def test_user_app_crc_and_lengths(self):
        payload = b"synthetic-app"
        crc = pack.crc16_xmodem(payload)
        data = struct.pack("<IHH", len(payload), crc, crc) + payload
        self.image.write_bytes(data)
        self.assertEqual(pack.read_checked_user_app(self.image), data)
        for invalid in (b"", data[:-1], data[:6] + b"\0\0" + data[8:], data[:-1] + b"!"):
            self.image.write_bytes(invalid)
            with self.assertRaises(ValueError):
                pack.read_checked_user_app(self.image)

    def test_unknown_kernel_rejected(self):
        for data in (b"", b"\0" * pack.USER_APP_OFFSET):
            with self.assertRaises(ValueError):
                pack.validate_prefix(data)

    def test_base_tail_md5(self):
        body = b"\0" * (pack.USER_APP_OFFSET + 9)
        data = body + hashlib.md5(body).digest()
        self.image.write_bytes(data)
        self.assertEqual(pack.read_checked_base(self.image), data)
        self.image.write_bytes(data[:-1] + bytes([data[-1] ^ 1]))
        with self.assertRaises(ValueError):
            pack.read_checked_base(self.image)

    def test_payload_not_bound_to_wifi(self):
        result = self.cli("create_ota_command.py", "--ota", self.image,
                          "--url", "http://192.0.2.10/m1.bin", "--payload-only")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout,
                         "http://192.0.2.10/m1.bin|" + hashlib.md5(self.image.read_bytes()).hexdigest())

    def test_bad_url_rejected(self):
        for url in ("https://example.com/m1.bin", "http://host/a b", "http://host/a|b"):
            self.assertNotEqual(self.cli("create_ota_command.py", "--ota", self.image,
                                        "--url", url).returncode, 0)

    def discovery_args(self):
        return ["--ota", self.image, "--url", "http://192.0.2.10/m1.bin",
                "--version", "2026.09.17.24", "--release-summary", "离线测试，不可刷写",
                "--output", self.root / "discovery.json"]

    def test_discovery_requires_explicit_device(self):
        self.assertNotEqual(self.cli("create_ha_update_discovery.py", *self.discovery_args()).returncode, 0)

    def test_discovery_topics(self):
        result = self.cli("create_ha_update_discovery.py", *self.discovery_args(),
                          "--device-id", "m1_001122334455")
        self.assertEqual(result.returncode, 0, result.stderr)
        output = json.loads((self.root / "discovery.json").read_text(encoding="utf-8"))
        self.assertEqual(output["command_topic"], "m1/m1_001122334455/ota/set")
        self.assertEqual(output["device"]["identifiers"], ["m1_001122334455"])
        self.assertNotIn("password", json.dumps(output))

    def test_invalid_template_inputs_rejected(self):
        result = self.cli("create_ha_update_discovery.py", *self.discovery_args(),
                          "--device-id", "bad/#")
        self.assertNotEqual(result.returncode, 0)
        result = self.cli("create_ha_update_discovery.py", *self.discovery_args(),
                          "--device-id", "m1_001122334455", "--version", "bad' }}")
        self.assertNotEqual(result.returncode, 0)

    def test_kernel_patch_rejects_wrong_baseline_without_output(self):
        output = self.root / "patched.bin"
        result = self.cli("patch_captive_dhcp_kernel.py", self.image, output)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
