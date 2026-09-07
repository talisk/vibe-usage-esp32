import asyncio
import json
import os
from pathlib import Path
import signal
import sys
import tempfile
import unittest
from unittest.mock import patch

from services.codex_gateway.codex_client import (
    CodexClient, GatewayError, OUTPUT_SCHEMA, _check_provider, _normalized_result,
    launch_configuration, rpc_error, strict_json, validate_action,
)


class ValidationTests(unittest.TestCase):
    def test_action_requires_exact_fields_and_existing_ids(self):
        items = [{"id": 9, "title": "Synthetic", "completed": False}]
        self.assertEqual(validate_action('{"action":"complete","id":9}', items), '{"action":"complete","id":9}')
        invalid = ['{"action":"complete","id":8}', '{"action":"complete","id":true}',
                   '{"action":"noop","extra":1}', '{"action":"noop","action":"noop"}',
                   '{"action":"add","title":"x","delay_seconds":0,"repeat_seconds":1}',
                   '{"action":"add","title":"x","delay_seconds":0.0,"repeat_seconds":0}',
                   '{"action":"add","title":"x","delay_seconds":31622401,"repeat_seconds":0}',
                   '{"action":"add","title":"\\ud800","delay_seconds":0,"repeat_seconds":0}']
        for raw in invalid:
            with self.subTest(raw=raw), self.assertRaises(GatewayError):
                validate_action(raw, items)

    def test_schema_result_requires_irrelevant_null_fields(self):
        raw = {key: None for key in OUTPUT_SCHEMA["required"]}
        raw["action"] = "noop"
        self.assertEqual(_normalized_result(json.dumps(raw), []), '{"action":"noop"}')
        raw["title"] = "unexpected"
        with self.assertRaises(GatewayError):
            _normalized_result(json.dumps(raw), [])

    def test_duplicate_json_nonfinite_and_error_privacy(self):
        for raw in ('{"a":1,"a":2}', '{"x":NaN}', '"\\ud800"'[:-1]):
            with self.assertRaises(GatewayError):
                strict_json(raw)
        error = rpc_error({"message": "401 credential PRIVATE_CANARY"})
        self.assertEqual(error.status, 401)
        self.assertNotIn("PRIVATE_CANARY", str(error))

    def test_custom_auth_provider_and_origins_refused(self):
        for cfg in ({"profile": "x"}, {"model_provider": "other"},
                    {"model_providers": {"openai": {"base_url": "https://other.invalid"}}},
                    {"chatgpt_base_url": "https://other.invalid"},
                    {"experimental_realtime_webrtc_call_base_url": "https://other.invalid"}):
            with self.subTest(config=cfg), self.assertRaises(GatewayError):
                _check_provider(cfg)

    def test_launch_drops_api_keys_disables_inherited_tools(self):
        with tempfile.TemporaryDirectory() as directory:
            home = Path(directory)
            (home / "config.toml").write_text('[mcp_servers."test-name"]\ncommand="never"\n')
            with patch.dict(os.environ, {"OPENAI_API_KEY": "PRIVATE_CANARY", "CODEX_API_KEY": "PRIVATE_CANARY"}):
                cmd, env = launch_configuration("codex", home)
            self.assertNotIn("OPENAI_API_KEY", env)
            self.assertNotIn("CODEX_API_KEY", env)
            self.assertIn('mcp_servers={"test-name" = { enabled = false }}', cmd)
            self.assertIn("features.apps=false", cmd)
            self.assertIn("features.shell_tool=false", cmd)
            self.assertNotIn("PRIVATE_CANARY", repr(cmd))


class ProtocolTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.home = Path(self.tmp.name)
        self.real_spawn = asyncio.create_subprocess_exec
        fixture = Path(__file__).with_name("fake_codex.py")

        async def fake_spawn(*args, **kwargs):
            return await self.real_spawn(sys.executable, str(fixture), **kwargs)

        self.patcher = patch("services.codex_gateway.codex_client.asyncio.create_subprocess_exec", fake_spawn)
        self.patcher.start()

    async def asyncTearDown(self):
        self.patcher.stop()
        self.tmp.cleanup()

    async def test_completed_turn_and_ephemeral_process_cleanup(self):
        client = CodexClient(codex_home=self.home)
        with patch.dict(os.environ, {"OPENAI_API_KEY": "PRIVATE_CANARY"}):
            async with client:
                info = await client.request("test/environment", {})
                self.assertFalse(info["api_key_present"])
                workspace = Path(info["cwd"])
                self.assertTrue(workspace.exists())
                result = json.loads(await client.complete_todo({"items": [], "instruction": "Synthetic task"}))
                self.assertEqual(result, {"action": "add", "title": "Synthetic task", "delay_seconds": 0, "repeat_seconds": 0})
        self.assertIsNone(client.proc)
        self.assertFalse(workspace.exists())
        self.assertEqual(list(self.home.iterdir()), [])

    async def test_api_key_account_rejected_without_inference(self):
        with patch.dict(os.environ, {"VIBE_TEST_AUTH": "apiKey"}):
            with self.assertRaises(GatewayError) as caught:
                async with CodexClient(codex_home=self.home):
                    self.fail("API key account accepted")
        self.assertEqual(caught.exception.status, 401)

    async def test_server_tool_rpc_rejected_and_unexpected_voice_turn_interrupted(self):
        for method, expected in (("test/inbound", "unexpected_codex_tool_request"),
                                 ("test/unexpected", "unexpected_codex_turn")):
            async with CodexClient(codex_home=self.home) as client:
                with self.assertRaises(GatewayError) as caught:
                    await client.request(method, {})
                self.assertEqual(caught.exception.code, expected)
                if method == "test/unexpected":
                    self.assertTrue(client.unexpected_turn)

    async def test_cancelled_request_kills_descendant_process(self):
        client = CodexClient(codex_home=self.home)
        async with client:
            pid = (await client.request("test/child", {}))["pid"]
            operation = asyncio.create_task(client.request("test/sleep", {}))
            await asyncio.sleep(0.02)
            operation.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await operation
        # A terminated descendant may briefly be a zombie until macOS init reaps it.
        process = await self.real_spawn("ps", "-p", str(pid), "-o", "stat=", stdout=asyncio.subprocess.PIPE)
        status, _ = await process.communicate()
        self.assertTrue(not status.strip() or status.strip().startswith(b"Z"), status)

    async def test_provider_error_only_returns_fixed_code(self):
        async with CodexClient(codex_home=self.home) as client:
            with self.assertRaises(GatewayError) as caught:
                await client.request("test/error", {})
        self.assertEqual(caught.exception.code, "subscription_login_required")
        self.assertNotIn("PRIVATE_CANARY", repr(caught.exception))

    async def test_cancellation_during_cleanup_still_kills_and_removes_workspace(self):
        client = await CodexClient(codex_home=self.home).__aenter__()
        info = await client.request("test/environment", {})
        workspace = Path(info["cwd"])
        await client.request("test/ignore_term", {})
        # Keep the process inside a request, so stdin EOF alone cannot exit it.
        operation = asyncio.create_task(client.request("test/sleep", {}))
        await asyncio.sleep(0.02)
        operation.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await operation
        proc = client.proc
        closing = asyncio.create_task(client.close())
        await asyncio.sleep(0.02)
        closing.cancel()
        await asyncio.sleep(0.02)
        closing.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await closing
        self.assertIsNotNone(proc.returncode)
        self.assertIsNone(client.proc)
        self.assertFalse(workspace.exists())


if __name__ == "__main__":
    unittest.main()
