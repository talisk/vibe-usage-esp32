"""Voice adapter lifecycle tests using in-memory media and App Server peers."""
import asyncio
import io
import json
import unittest
import wave
from contextlib import suppress
from types import SimpleNamespace
from unittest.mock import patch

from services.codex_gateway import voice


def recording():
    data = io.BytesIO()
    with wave.open(data, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(8000)
        wav.writeframes(b"\x00\x10" * 800)
    return data.getvalue()


class FakeChannel:
    def __init__(self):
        self.handlers = {}

    def on(self, event):
        def register(callback):
            self.handlers[event] = callback
            return callback
        return register

    def emit(self, event, *args):
        callback = self.handlers.get(event)
        if callback:
            callback(*args)


class FakeTrack:
    def __init__(self, pcm):
        self.pcm = pcm
        self.gate = asyncio.Event()
        self.drained = asyncio.Event()
        self.stopped = False

    def stop(self):
        self.stopped = True


class FakeClient:
    def __init__(self):
        self.events = asyncio.Queue()
        self.unexpected_turn = False
        self.stop_entered = asyncio.Event()
        self.stop_release = asyncio.Event()
        self.stop_release.set()
        self.on_stop = None
        self.consumer_cancelled = 0
        self.calls = []

    async def start_ephemeral_thread(self, **_):
        return "synthetic-thread"

    def emit(self, method, **params):
        self.events.put_nowait({"method": method, "params": {
            "threadId": "synthetic-thread", **params}})

    async def request(self, method, params, **_):
        self.calls.append(method)
        if method == "thread/realtime/start":
            self.emit("thread/realtime/sdp", sdp="synthetic-answer")
        elif method == "thread/realtime/stop":
            self.stop_entered.set()
            await self.stop_release.wait()
            if self.on_stop:
                # Deliberately do not yield after enqueueing: cleanup must let
                # the transcript consumer observe events queued with the ACK.
                self.on_stop()
        return {}

    async def next_event(self, **_):
        try:
            return await self.events.get()
        except asyncio.CancelledError:
            self.consumer_cancelled += 1
            raise


class FakePeerConnection:
    def __init__(self, client):
        self.client = client
        self.channel = FakeChannel()
        self.playback_started = asyncio.Event()
        self.playback_release = asyncio.Event()
        self.playback_release.set()
        self.close_entered = asyncio.Event()
        self.close_release = asyncio.Event()
        self.close_release.set()
        self.track = None
        self.sender = None
        self.closed = False

    def addTrack(self, track):
        self.track = track

    def createDataChannel(self, name):
        assert name == "oai-events"
        return self.channel

    async def createOffer(self):
        return SimpleNamespace(sdp="synthetic-offer", type="offer")

    async def setLocalDescription(self, description):
        self.localDescription = description

    async def setRemoteDescription(self, _):
        self.channel.emit("open")
        self.channel.emit("message", json.dumps({"type": "session.started"}))

        async def send_recording():
            await self.track.gate.wait()
            self.playback_started.set()
            self.client.emit("thread/realtime/transcript/done", role="user", text="Buy milk")
            await self.playback_release.wait()
            self.track.drained.set()

        self.sender = asyncio.create_task(send_recording(), name="fake-voice-sender")

    async def close(self):
        self.close_entered.set()
        await self.close_release.wait()
        if self.sender:
            self.sender.cancel()
            with suppress(asyncio.CancelledError):
                await self.sender
        self.channel.emit("close")
        self.closed = True


class VoiceLifecycleTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.client = FakeClient()
        self.pc = FakePeerConnection(self.client)
        for replacement in (
            patch.object(voice, "RTCPeerConnection", return_value=self.pc),
            patch.object(voice, "RTCSessionDescription", side_effect=lambda **kw: SimpleNamespace(**kw)),
            patch.object(voice, "RecordingTrack", side_effect=FakeTrack),
            patch.object(voice, "PREROLL_SECONDS", 0),
            patch.object(voice, "TAIL_SECONDS", 0),
        ):
            replacement.start()
            self.addCleanup(replacement.stop)

    async def transcribe(self):
        return await voice.transcribe_wav(self.client, recording())

    def assert_released(self):
        self.assertTrue(self.pc.closed)
        self.assertTrue(self.pc.track.stopped)
        self.assertTrue(self.pc.sender is None or self.pc.sender.done())
        self.assertGreaterEqual(self.client.consumer_cancelled, 1)
        self.assertFalse(any(task.get_name() == "codex-voice-cleanup"
                             for task in asyncio.all_tasks() if not task.done()))

    async def test_final_before_recording_end_does_not_return_early(self):
        self.pc.playback_release.clear()
        operation = asyncio.create_task(self.transcribe())
        await asyncio.wait_for(self.pc.playback_started.wait(), 1)
        await asyncio.sleep(0)
        self.assertFalse(operation.done())
        self.assertFalse(self.client.stop_entered.is_set())
        self.pc.playback_release.set()
        self.assertEqual(await asyncio.wait_for(operation, 1), "Buy milk")
        self.assert_released()

    async def test_late_partial_queued_with_stop_ack_fails(self):
        self.client.on_stop = lambda: self.client.emit(
            "thread/realtime/transcript/delta", role="user", delta="tomorrow")
        with self.assertRaises(voice.VoiceError) as caught:
            await self.transcribe()
        self.assertEqual(caught.exception.code, "voice_no_complete_transcript")
        self.assert_released()

    async def test_late_unexpected_turn_during_stop_fails(self):
        def unexpected():
            self.client.unexpected_turn = True
            self.client.emit("turn/started", turn={"id": "synthetic-unexpected"})
        self.client.on_stop = unexpected
        with self.assertRaises(voice.VoiceError) as caught:
            await self.transcribe()
        self.assertEqual(caught.exception.code, "voice_unexpected_turn")
        self.assert_released()

    async def test_late_realtime_error_during_stop_fails(self):
        self.client.on_stop = lambda: self.client.emit("thread/realtime/error", message="synthetic error")
        with self.assertRaises(voice.VoiceError) as caught:
            await self.transcribe()
        self.assertEqual(caught.exception.code, "voice_session_failed")
        self.assert_released()

    async def test_stop_failure_still_closes_media_and_fails(self):
        def broken_stop():
            raise TimeoutError
        self.client.on_stop = broken_stop
        with self.assertRaises(voice.VoiceError) as caught:
            await self.transcribe()
        self.assertEqual(caught.exception.code, "voice_cleanup_failed")
        self.assert_released()

    async def test_unresponsive_stop_is_bounded_even_if_client_ignores_timeout(self):
        self.client.stop_release.clear()
        timeout = asyncio.timeout
        with patch.object(voice.asyncio, "timeout", side_effect=lambda seconds: timeout(
                0.02 if seconds == 3 else seconds)):
            with self.assertRaises(voice.VoiceError) as caught:
                await asyncio.wait_for(self.transcribe(), 1)
        self.assertEqual(caught.exception.code, "voice_cleanup_failed")
        self.assert_released()

    async def test_async_start_error_releases_media_before_any_audio_is_sent(self):
        request = self.client.request

        async def reject_start(method, params, **kwargs):
            if method == "thread/realtime/start":
                self.client.emit("thread/realtime/error", message="synthetic rejected start")
                return {}
            return await request(method, params, **kwargs)

        self.client.request = reject_start
        with self.assertRaises(voice.VoiceError) as caught:
            await asyncio.wait_for(self.transcribe(), 1)
        self.assertEqual(caught.exception.code, "voice_session_failed")
        self.assertFalse(self.pc.track.gate.is_set())
        self.assert_released()

    async def test_operation_timeout_releases_media_and_consumer(self):
        self.pc.playback_release.clear()
        with patch.object(voice, "OPERATION_SECONDS", 0.02):
            with self.assertRaises(voice.VoiceError) as caught:
                await self.transcribe()
        self.assertEqual(caught.exception.code, "voice_timeout")
        self.assert_released()

    async def test_repeated_cancellation_during_stop_and_close_is_owned(self):
        self.pc.playback_release.clear()
        self.client.stop_release.clear()
        self.pc.close_release.clear()
        operation = asyncio.create_task(self.transcribe())
        try:
            await asyncio.wait_for(self.pc.playback_started.wait(), 1)
            operation.cancel()
            await asyncio.wait_for(self.client.stop_entered.wait(), 1)
            operation.cancel()
            await asyncio.sleep(0)
            self.assertFalse(operation.done())
            self.client.stop_release.set()
            await asyncio.wait_for(self.pc.close_entered.wait(), 1)
            operation.cancel()
            await asyncio.sleep(0)
            self.assertFalse(operation.done())
            self.pc.close_release.set()
            with self.assertRaises(asyncio.CancelledError):
                await asyncio.wait_for(operation, 1)
            self.assert_released()
        finally:
            self.client.stop_release.set()
            self.pc.close_release.set()
            operation.cancel()
            with suppress(asyncio.CancelledError):
                await operation

    async def test_cancellation_after_success_before_cleanup_finishes_is_not_success(self):
        self.client.stop_release.clear()
        operation = asyncio.create_task(self.transcribe())
        try:
            await asyncio.wait_for(self.client.stop_entered.wait(), 1)
            operation.cancel()
            await asyncio.sleep(0)
            self.assertFalse(operation.done())
            self.client.stop_release.set()
            with self.assertRaises(asyncio.CancelledError):
                await asyncio.wait_for(operation, 1)
            self.assert_released()
        finally:
            self.client.stop_release.set()
            operation.cancel()
            with suppress(asyncio.CancelledError):
                await operation


if __name__ == "__main__":
    unittest.main()
