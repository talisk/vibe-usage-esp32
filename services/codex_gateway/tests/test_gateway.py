import asyncio
import io
import gzip
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import wave

from aiohttp import ClientSession, web
from aiohttp.test_utils import TestServer

from services.codex_gateway.codex_client import GatewayError
from services.codex_gateway.gateway import (
    STATE, create_app, parse_audio, parse_chat, token_from_file, validate_binding,
)

TOKEN = "synthetic_fixture_token_not_a_real_secret_1234567890"


def chat_body(**kwargs):
    body = {"model": "codex-default", "stream": False, "max_tokens": 400,
            "messages": [{"role": "system", "content": "ignored untrusted system text"},
                         {"role": "user", "content": json.dumps({"items": [], "instruction": "Synthetic task"})}]}
    body.update(kwargs)
    return body


def wav_bytes():
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as wav:
        wav.setparams((1, 2, 8000, 0, "NONE", "not compressed"))
        wav.writeframes(b"\x01\x00" * 800)
    return buffer.getvalue()


def multipart(*, model=b"codex-voice", duplicate=False):
    parts = [("model", model, False), ("response_format", b"json", False), ("file", wav_bytes(), True)]
    if duplicate:
        parts.append(("model", model, False))
    raw = b""
    for name, value, file in parts:
        raw += b"--VibeFixture\r\nContent-Disposition: form-data; name=\"" + name.encode() + b"\""
        if file:
            raw += b'; filename="voice.wav"\r\nContent-Type: audio/wav'
        raw += b"\r\n\r\n" + value + b"\r\n"
    return raw + b"--VibeFixture--\r\n"


class InputTests(unittest.TestCase):
    def test_device_chat_schema_and_bounded_items(self):
        self.assertEqual(parse_chat(json.dumps(chat_body()).encode())["instruction"], "Synthetic task")
        for kwargs in ({"model": "arbitrary-model"}, {"tools": []}, {"max_tokens": True}, {"stream": True}):
            with self.subTest(kwargs=kwargs), self.assertRaises(GatewayError):
                parse_chat(json.dumps(chat_body(**kwargs)).encode())
        body = chat_body()
        body["messages"][1]["content"] = '{"items":[],"instruction":"x","instruction":"y"}'
        with self.assertRaises(GatewayError):
            parse_chat(json.dumps(body).encode())
        items = [{"id": 1, "title": "Task", "completed": False}] * 2
        body["messages"][1]["content"] = json.dumps({"items": items, "instruction": "Finish task"})
        with self.assertRaises(GatewayError):
            parse_chat(json.dumps(body).encode())

    def test_multipart_accepts_firmware_wav_and_rejects_extra_fields_models(self):
        content_type = "multipart/form-data; boundary=VibeFixture"
        self.assertEqual(parse_audio(multipart(), content_type), wav_bytes())
        for raw in (multipart(model=b"gpt-arbitrary"), multipart(duplicate=True), b"bad multipart"):
            with self.assertRaises(GatewayError):
                parse_audio(raw, content_type)

    def test_token_permissions_symlinks_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "token"
            first = token_from_file(path, create=True)
            self.assertGreaterEqual(len(first), 32)
            self.assertEqual(token_from_file(path, create=True), first)
            os.chmod(path, 0o644)
            with self.assertRaises(GatewayError):
                token_from_file(path)
            os.chmod(path, 0o600)
            link = path.with_name("link")
            link.symlink_to(path)
            with self.assertRaises(GatewayError):
                token_from_file(link)

    def test_only_explicit_loopback_or_rfc1918_binding(self):
        for address in ("127.0.0.1", "192.168.1.9", "10.1.2.3", "172.16.1.9"):
            self.assertEqual(validate_binding(address), address)
        for address in ("0.0.0.0", "::", "localhost", "8.8.8.8", "100.64.0.1", "169.254.1.2"):
            with self.assertRaises(GatewayError):
                validate_binding(address)


class FakeClient:
    def __init__(self, *, gate=None, fail=None, enter_fail=None):
        self.gate, self.fail, self.enter_fail = gate, fail, enter_fail
        self.closed = asyncio.Event()
        self.cancelled = False

    async def __aenter__(self):
        if self.enter_fail:
            raise self.enter_fail
        return self

    async def complete_todo(self, data):
        try:
            if self.gate:
                await self.gate.wait()
            if self.fail:
                raise self.fail
            return '{"action":"noop"}'
        except asyncio.CancelledError:
            self.cancelled = True
            raise

    async def close(self):
        self.closed.set()


class HttpTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.instances = []
        self.options = {}

        def factory():
            instance = FakeClient(**self.options)
            self.instances.append(instance)
            return instance

        async def transcribe(client, audio):
            self.assertEqual(audio, wav_bytes())
            return "Synthetic voice"

        self.app = create_app(TOKEN, client_factory=factory, transcriber=transcribe)
        self.received_body_bytes = []

        @web.middleware
        async def observe_compressed_input(request, handler):
            if request.headers.get("Content-Encoding"):
                self.received_body_bytes.append(request.content.total_bytes)
            return await handler(request)

        self.app.middlewares.append(observe_compressed_input)
        self.server = TestServer(self.app)
        await self.server.start_server()
        self.session = ClientSession()
        self.headers = {"Authorization": "Bearer " + TOKEN}

    async def asyncTearDown(self):
        await self.session.close()
        await self.server.close()

    async def post_chat(self, **kwargs):
        return await self.session.post(self.server.make_url("/v1/chat/completions"), json=chat_body(),
                                       headers=kwargs.pop("headers", self.headers), **kwargs)

    async def test_auth_input_errors_do_not_start_codex(self):
        response = await self.post_chat(headers={})
        self.assertEqual(response.status, 401)
        self.assertNotIn(TOKEN, await response.text())
        response = await self.session.post(self.server.make_url("/v1/chat/completions"),
                                           data=b"x" * 17000, headers=self.headers)
        self.assertEqual(response.status, 413)
        self.assertEqual(self.instances, [])

    async def test_precise_preflight_auth_error_no_success_text(self):
        self.options = {"enter_fail": GatewayError("subscription_login_required", 401)}
        response = await self.post_chat()
        self.assertEqual(response.status, 401)
        self.assertEqual(await response.json(), {"error": {"code": "subscription_login_required"}})
        await self.instances[0].closed.wait()

    async def test_transport_never_inflates_compressed_unauthenticated_body(self):
        compressed = gzip.compress(b"x" * 1_000_000)
        response = await self.session.post(self.server.make_url("/v1/chat/completions"),
                                           data=compressed, headers={"Content-Encoding": "gzip"})
        self.assertEqual(response.status, 401)
        await response.read()
        self.assertEqual(self.instances, [])
        self.assertTrue(self.received_body_bytes)
        self.assertLessEqual(self.received_body_bytes[0], len(compressed))

    async def test_heartbeat_busy_and_single_final_json(self):
        gate = asyncio.Event()
        self.options = {"gate": gate}
        with patch("services.codex_gateway.gateway.HEARTBEAT_SECONDS", 0.02):
            first = await self.post_chat()
            self.assertEqual(await first.content.readexactly(1), b" ")
            second = await self.post_chat()
            self.assertEqual(second.status, 429)
            self.assertEqual(len(self.instances), 1)
            gate.set()
            result = await first.json()
        self.assertEqual(result["choices"][0]["message"]["content"], '{"action":"noop"}')
        await self.instances[0].closed.wait()
        self.assertFalse(self.app[STATE].busy)

    async def test_late_failure_is_error_json_without_mutation_shape(self):
        gate = asyncio.Event()
        self.options = {"gate": gate, "fail": GatewayError("subscription_limit", 429)}
        with patch("services.codex_gateway.gateway.HEARTBEAT_SECONDS", 0.02):
            response = await self.post_chat()
            self.assertEqual(await response.content.readexactly(1), b" ")
            gate.set()
            result = await response.json()
        self.assertEqual(response.status, 200)
        self.assertEqual(result, {"error": {"code": "subscription_limit"}})
        self.assertNotIn("choices", result)
        self.assertNotIn("text", result)

    async def test_disconnect_cancels_inference_and_releases_slot(self):
        self.options = {"gate": asyncio.Event()}
        response = await self.post_chat()
        await response.content.readexactly(1)
        instance = self.instances[0]
        response.close()
        await asyncio.wait_for(instance.closed.wait(), 2)
        self.assertTrue(instance.cancelled)
        self.assertFalse(self.app[STATE].busy)

    async def test_shutdown_cancels_pending_operation(self):
        self.options = {"gate": asyncio.Event()}
        response = await self.post_chat()
        await response.content.readexactly(1)
        instance = self.instances[0]
        await self.app.shutdown()
        response.close()
        self.assertTrue(instance.closed.is_set())
        self.assertTrue(instance.cancelled)
        self.assertFalse(self.app[STATE].busy)

    async def test_audio_contract_success(self):
        response = await self.session.post(self.server.make_url("/v1/audio/transcriptions"),
                    data=multipart(), headers={**self.headers, "Content-Type": "multipart/form-data; boundary=VibeFixture"})
        self.assertEqual(response.status, 200)
        self.assertEqual(await response.json(), {"text": "Synthetic voice"})


if __name__ == "__main__":
    unittest.main()
