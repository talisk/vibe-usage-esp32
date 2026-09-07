"""Run with: python -m services.codex_gateway.gateway --token-file /private/path.

This is a narrow device adapter, not an OpenAI proxy. No request body, token,
transcript, provider error text, or account information is logged or persisted.
"""
from __future__ import annotations

import argparse
import asyncio
import contextlib
from email import policy
from email.parser import BytesParser
import hmac
import ipaddress
import json
import os
from pathlib import Path
import re
import secrets
import stat
from typing import Callable

from aiohttp import web

from .codex_client import CodexClient, GatewayError, _valid_text, check_version, strict_json

MAX_BODY = 140_000
MAX_CHAT_BODY = 16_384
HEARTBEAT_SECONDS = 1.0
STATE = web.AppKey("gateway_state", object)


def token_from_file(path: Path, *, create: bool = False) -> str:
    """Use a dedicated random local bearer token, never a subscription credential."""
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    if create:
        try:
            fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0), 0o600)
            with os.fdopen(fd, "w", encoding="ascii") as handle:
                handle.write(secrets.token_urlsafe(32) + "\n")
        except FileExistsError:
            pass
        except OSError:
            raise GatewayError("token_file_unavailable", 503) from None
    try:
        fd = os.open(path, flags)
        with os.fdopen(fd, "r", encoding="ascii") as handle:
            info = os.fstat(handle.fileno())
            if (not stat.S_ISREG(info.st_mode) or stat.S_IMODE(info.st_mode) != 0o600
                    or info.st_uid != os.getuid() or info.st_nlink != 1 or info.st_size > 130):
                raise GatewayError("token_file_must_be_private", 503)
            token = handle.read(130).strip()
    except (OSError, UnicodeError):
        raise GatewayError("token_file_unavailable", 503) from None
    if re.fullmatch(r"[A-Za-z0-9_-]{32,96}", token) is None:
        raise GatewayError("invalid_gateway_token", 503)
    return token


def validate_binding(host: str) -> str:
    try:
        address = ipaddress.IPv4Address(host)
    except ipaddress.AddressValueError:
        raise GatewayError("bind_requires_ipv4_literal", 503) from None
    private = any(address in network for network in (
        ipaddress.ip_network("10.0.0.0/8"), ipaddress.ip_network("172.16.0.0/12"),
        ipaddress.ip_network("192.168.0.0/16")))
    if not (address.is_loopback or private):
        raise GatewayError("bind_requires_loopback_or_lan", 503)
    return str(address)


def parse_chat(raw: bytes) -> dict:
    if len(raw) > MAX_CHAT_BODY:
        raise GatewayError("request_too_large", 413)
    body = strict_json(raw)
    if (not isinstance(body, dict) or set(body) != {"model", "messages", "stream", "max_tokens"}
            or body["model"] != "codex-default" or body["stream"] is not False
            or type(body["max_tokens"]) is not int or body["max_tokens"] != 400):
        raise GatewayError("unsupported_chat_request", 400)
    messages = body["messages"]
    if not isinstance(messages, list) or len(messages) != 2:
        raise GatewayError("unsupported_chat_request", 400)
    for message, role in zip(messages, ("system", "user")):
        if (not isinstance(message, dict) or set(message) != {"role", "content"}
                or message["role"] != role or not _valid_text(message["content"], 8192)):
            raise GatewayError("unsupported_chat_request", 400)
    # The supplied system text is ignored. A fixed host-side prompt owns the
    # operation; only bounded task data reaches the model.
    data = strict_json(messages[1]["content"])
    if (not isinstance(data, dict) or set(data) != {"items", "instruction"}
            or not _valid_text(data["instruction"], 512)
            or not isinstance(data["items"], list) or len(data["items"]) > 12):
        raise GatewayError("invalid_todo_input", 400)
    ids = set()
    for item in data["items"]:
        if (not isinstance(item, dict) or set(item) != {"id", "title", "completed"}
                or type(item["id"]) is not int or not 1 <= item["id"] <= 0xFFFFFFFF
                or item["id"] in ids or not _valid_text(item["title"], 96)
                or type(item["completed"]) is not bool):
            raise GatewayError("invalid_todo_input", 400)
        ids.add(item["id"])
    return data


def parse_audio(raw: bytes, content_type: str) -> bytes:
    from .voice import VoiceError, validate_wav

    if len(raw) > MAX_BODY or len(content_type) > 200 or "\r" in content_type or "\n" in content_type:
        raise GatewayError("invalid_audio_request", 400)
    try:
        message = BytesParser(policy=policy.default).parsebytes(
            b"Content-Type: " + content_type.encode("ascii") + b"\r\nMIME-Version: 1.0\r\n\r\n" + raw)
        if (message.get_content_type() != "multipart/form-data" or not message.is_multipart()
                or message.defects or not message.get_boundary() or len(message.get_boundary()) > 70):
            raise ValueError("invalid multipart")
        parts = list(message.iter_parts())
        if len(parts) != 3:
            raise ValueError("invalid fields")
        fields = {}
        for part in parts:
            if (part.is_multipart() or part.defects or part.get_content_disposition() != "form-data"
                    or part.get("Content-Transfer-Encoding") or part.get("Content-Encoding")
                    or len(part.get_all("Content-Disposition", [])) != 1
                    or len(part.get_all("Content-Type", [])) > 1):
                raise ValueError("invalid part")
            name = part.get_param("name", header="Content-Disposition")
            if name not in ("file", "model", "response_format") or name in fields:
                raise ValueError("invalid field")
            payload = part.get_payload(decode=True)
            if not isinstance(payload, bytes):
                raise ValueError("invalid part body")
            if name == "file":
                if part.get_content_type() not in ("audio/wav", "audio/x-wav", "application/octet-stream"):
                    raise ValueError("invalid file type")
            elif part.get_filename() is not None or len(payload) > 32:
                raise ValueError("invalid field")
            fields[name] = payload
        if fields["model"] != b"codex-voice" or fields["response_format"] != b"json":
            raise ValueError("unsupported model")
        validate_wav(fields["file"])
        return fields["file"]
    except (ValueError, TypeError, UnicodeError, KeyError, VoiceError):
        raise GatewayError("invalid_audio_request", 400) from None


class GatewayState:
    def __init__(self, token: str, client_factory: Callable, transcriber: Callable | None):
        self.token = token
        self.client_factory = client_factory
        self.transcriber = transcriber
        self.busy = False
        self.stopping = False
        self.handlers: set[asyncio.Task] = set()


def error_response(error: GatewayError) -> web.Response:
    return web.json_response({"error": {"code": error.code}}, status=error.status,
                             headers={"Cache-Control": "no-store"})


async def read_body(request: web.Request, limit: int) -> bytes:
    if request.content_length is not None and request.content_length > limit:
        raise GatewayError("request_too_large", 413)
    data = bytearray()
    async with asyncio.timeout(12):
        async for chunk in request.content.iter_chunked(8192):
            if len(data) + len(chunk) > limit:
                raise GatewayError("request_too_large", 413)
            data.extend(chunk)
    return bytes(data)


async def _transcribe(client, raw: bytes) -> str:
    from .voice import transcribe_wav
    return await transcribe_wav(client, raw)


async def _watch_disconnect(request: web.Request, task: asyncio.Task):
    while not task.done():
        if request.transport is None or request.transport.is_closing():
            task.cancel()
            return
        await asyncio.sleep(0.2)


async def handle(request: web.Request) -> web.StreamResponse:
    state = request.app[STATE]
    # Comparing fixed bytes avoids both timing leaks and accidental token echo.
    supplied = request.headers.getall("Authorization", [])
    if (len(supplied) != 1 or len(supplied[0]) > 150
            or not hmac.compare_digest(supplied[0].encode("utf-8"), ("Bearer " + state.token).encode("ascii"))):
        response = error_response(GatewayError("invalid_gateway_token", 401))
        response.force_close()
        return response
    if request.query_string or request.headers.get("Content-Encoding"):
        response = error_response(GatewayError("unsupported_request", 400))
        response.force_close()
        return response
    if state.stopping or state.busy:
        response = error_response(GatewayError("gateway_busy", 429))
        response.force_close()
        return response
    state.busy = True
    current = asyncio.current_task()
    state.handlers.add(current)
    operation, watcher, client, response = None, None, None, None
    try:
        is_audio = request.path == "/v1/audio/transcriptions"
        raw = await read_body(request, MAX_BODY if is_audio else MAX_CHAT_BODY)
        if is_audio:
            data = parse_audio(raw, request.headers.get("Content-Type", ""))
        else:
            if request.content_type != "application/json":
                raise GatewayError("unsupported_content_type", 415)
            data = parse_chat(raw)

        async def work():
            nonlocal client
            # Preflight credentials/configuration must finish within the device's
            # first read timeout; errors here retain meaningful HTTP status.
            client = state.client_factory()
            async with asyncio.timeout(3):
                await client.__aenter__()
            ready.set()
            if is_audio:
                text = await (state.transcriber or _transcribe)(client, data)
                if not _valid_text(text, 512):
                    raise GatewayError("invalid_transcript")
                return {"text": text}
            content = await client.complete_todo(data)
            return {"choices": [{"finish_reason": "stop", "message": {"role": "assistant", "content": content}}]}

        ready = asyncio.Event()
        operation = asyncio.create_task(work())
        watcher = asyncio.create_task(_watch_disconnect(request, operation))
        async with asyncio.timeout(32 if is_audio else 25):
            while not ready.is_set() and not operation.done():
                await asyncio.wait({operation}, timeout=0.05)
            if operation.done():
                result = await operation
                return web.json_response(result, headers={"Cache-Control": "no-store"})
            response = web.StreamResponse(status=200, headers={
                "Content-Type": "application/json", "Cache-Control": "no-store",
                "X-Content-Type-Options": "nosniff"})
            response.enable_chunked_encoding()
            await response.prepare(request)
            # JSON whitespace keeps the ESP-IDF five-second socket read alive.
            # This is not an SSE stream; a single bounded JSON value follows.
            while not operation.done():
                await response.write(b" ")
                await asyncio.wait({operation}, timeout=HEARTBEAT_SECONDS)
            result = await operation
            body = json.dumps(result, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
            if len(body) > 3500:
                raise GatewayError("response_too_large")
            await response.write(body)
            await response.write_eof()
            return response
    except asyncio.CancelledError:
        raise
    except (ConnectionError, BrokenPipeError):
        raise asyncio.CancelledError from None
    except Exception as exc:
        # Never stringify an arbitrary exception: WebRTC, HTTP, and JSON parser
        # exceptions can include source audio text, SDP, or sensitive URLs.
        if isinstance(exc, GatewayError):
            error = exc
        elif isinstance(exc, TimeoutError):
            error = GatewayError("subscription_timeout", 504)
        else:
            from .voice import VoiceError
            error = GatewayError(exc.code) if isinstance(exc, VoiceError) else GatewayError("subscription_request_failed")
        if response is not None and response.prepared:
            # HTTP headers cannot change after heartbeats. An error object has
            # neither choices nor text, so firmware cannot mutate the TODO list.
            with contextlib.suppress(ConnectionError):
                await response.write(json.dumps({"error": {"code": error.code}}, separators=(",", ":")).encode())
                await response.write_eof()
            return response
        response = error_response(error)
        response.force_close()
        return response
    finally:
        for task in (watcher, operation):
            if task:
                task.cancel()
                with contextlib.suppress(asyncio.CancelledError, Exception):
                    await task
        try:
            if client:
                await client.close()
        finally:
            state.busy = False
            state.handlers.discard(current)


async def shutdown(app: web.Application):
    state = app[STATE]
    state.stopping = True
    tasks = list(state.handlers)
    for task in tasks:
        task.cancel()
    await asyncio.gather(*tasks, return_exceptions=True)


def create_app(token: str, *, client_factory: Callable = CodexClient,
               transcriber: Callable | None = None) -> web.Application:
    # Disable decompression in the transport parser, before auth/handler code.
    # Otherwise a small compressed unauthenticated body can inflate without
    # ever passing through read_body's memory limit.
    app = web.Application(client_max_size=MAX_BODY, handler_args={"auto_decompress": False})
    app[STATE] = GatewayState(token, client_factory, transcriber)
    app.router.add_post("/v1/chat/completions", handle)
    app.router.add_post("/v1/audio/transcriptions", handle)
    app.on_shutdown.append(shutdown)
    return app


def main():
    parser = argparse.ArgumentParser(description="Local Codex subscription adapter for Vibe firmware")
    parser.add_argument("--host", default="127.0.0.1", help="Loopback or explicit private LAN IPv4; wildcard binding is refused")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--token-file", type=Path, required=True, help="Dedicated 0600 local gateway token")
    parser.add_argument("--create-token", action="store_true", help="Create a random token only if the file does not exist")
    parser.add_argument("--codex", default="codex", help="Official Codex CLI executable")
    parser.add_argument("--model", help="Optional subscription LLM model; omission uses the user's Codex default")
    args = parser.parse_args()
    try:
        host = validate_binding(args.host)
        if not 1 <= args.port <= 65535:
            raise GatewayError("invalid_port", 503)
        if args.model and re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,95}", args.model) is None:
            raise GatewayError("invalid_model", 503)
        token = token_from_file(args.token_file, create=args.create_token)
        asyncio.run(check_version(args.codex))
        app = create_app(token, client_factory=lambda: CodexClient(args.codex, model=args.model))
        print(f"Vibe Codex gateway listening on {host}:{args.port}; token and request logging disabled.")
        web.run_app(app, host=host, port=args.port, access_log=None, print=None,
                    handler_cancellation=True, shutdown_timeout=5)
    except GatewayError as exc:
        parser.exit(1, exc.code + "\n")


if __name__ == "__main__":
    main()
