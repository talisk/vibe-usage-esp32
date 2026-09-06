import asyncio
import io
import struct
import unittest
import wave

from services.codex_gateway.voice import RecordingTrack, TranscriptCollector, VoiceError, validate_wav


def make_wav(pcm=None, rate=8000, channels=1, width=2):
    data = io.BytesIO()
    with wave.open(data, "wb") as out:
        out.setframerate(rate)
        out.setnchannels(channels)
        out.setsampwidth(width)
        out.writeframes(pcm if pcm is not None else struct.pack("<h", 1000) * 800)
    return data.getvalue()


class WavTests(unittest.TestCase):
    def test_firmware_eight_second_recording(self):
        pcm = struct.pack("<h", -1200) * 64000
        self.assertEqual(validate_wav(make_wav(pcm)), pcm)

    def test_reject_wrong_format_or_duration(self):
        bad = [make_wav(rate=16000), make_wav(channels=2), make_wav(width=1),
               make_wav(b""), make_wav(bytes(128002)), b"not a wave file"]
        for item in bad:
            with self.subTest(length=len(item)), self.assertRaises(VoiceError):
                validate_wav(item)

    def test_truncated_and_appended_bytes_fail(self):
        good = make_wav()
        for bad in [good[:-1], good + b"x", b"RIFX" + good[4:], good[:12] + b"data" + good[16:]]:
            with self.subTest(data=bad[:16]), self.assertRaises(VoiceError):
                validate_wav(bad)


class CollectorTests(unittest.TestCase):
    @staticmethod
    def done(ident, text, role="user"):
        return {"type": "turn.done", "turn": {"id": ident, "role": role, "transcript": text}}

    def test_only_final_user_text_is_returned(self):
        state = TranscriptCollector()
        state.ingest({"type": "input_transcript.added", "item": {"text": "Do not"}})
        self.assertFalse(state.ready)
        self.assertEqual(state.text, "")
        state.ingest(self.done("assistant-1", "Delete all of them", "assistant"))
        self.assertEqual(state.text, "")
        state.ingest(self.done("user-1", "Do not delete the task"))
        self.assertTrue(state.ready)
        self.assertEqual(state.text, "Do not delete the task")

    def test_multiple_turns_are_ordered_and_deduplicated(self):
        state = TranscriptCollector()
        for ident in ["first", "second"]:
            state.ingest({"type": "turn.created", "turn": {"id": ident, "role": "user"}})
        state.ingest(self.done("second", "after two hours"))
        self.assertFalse(state.ready)
        state.ingest(self.done("first", "Remind me to drink water"))
        state.ingest(self.done("second", "after two hours"))
        self.assertEqual(state.text, "Remind me to drink water after two hours")
        self.assertTrue(state.ready)

    def test_final_does_not_hide_later_unfinished_speech(self):
        state = TranscriptCollector()
        state.ingest(self.done("first", "Remind me"))
        state.ingest({"type": "input_transcript.added", "item": {"text": "in two hours"}})
        state.ingest(self.done("first", "Remind me"))
        self.assertFalse(state.ready)
        state.ingest(self.done("second", "in two hours"))
        self.assertEqual(state.text, "Remind me in two hours")

    def test_delayed_first_final_does_not_clear_second_partial(self):
        state = TranscriptCollector()
        for delta in ["Remind me", "after two hours"]:
            state.ingest({"type": "input_transcript.added", "item": {"text": delta}})
        state.ingest(self.done("first", "Remind me"))
        self.assertFalse(state.ready)
        self.assertEqual(state.text, "Remind me")
        state.ingest(self.done("second", "after two hours"))
        self.assertTrue(state.ready)
        self.assertEqual(state.text, "Remind me after two hours")

    def test_unmatched_revision_fails_instead_of_dropping_partial(self):
        state = TranscriptCollector()
        state.ingest({"type": "input_transcript.added", "item": {"text": "do not delete"}})
        with self.assertRaisesRegex(VoiceError, "voice_transcript_inconsistent"):
            state.ingest(self.done("first", "delete"))

    def test_conflicts_nul_and_utf8_overflow_fail(self):
        for text in ["a\0b", "中" * 171]:
            with self.subTest(text_length=len(text)), self.assertRaises(VoiceError):
                TranscriptCollector().ingest(self.done("turn-1", text))
        state = TranscriptCollector()
        state.ingest(self.done("turn-1", "keep it"))
        with self.assertRaises(VoiceError):
            state.ingest(self.done("turn-1", "delete it"))

    def test_error_delegation_and_missing_turn_id_fail(self):
        bad = [{"type": "error"}, {"type": "delegation.created"},
               {"type": "turn.done", "turn": {"role": "user", "transcript": "hello"}}]
        for event in bad:
            with self.subTest(kind=event["type"]), self.assertRaises(VoiceError):
                TranscriptCollector().ingest(event)


class TrackTests(unittest.IsolatedAsyncioTestCase):
    async def test_silence_gate_full_recording_and_drain(self):
        pcm = struct.pack("<h", 1200) * 320
        track = RecordingTrack(pcm)
        try:
            self.assertEqual(len(track.pcm), len(pcm) * 6)
            silence = await track.recv()
            self.assertEqual(bytes(silence.planes[0]), bytes(1920))
            self.assertEqual(track.offset, 0)
            self.assertFalse(track.drained.is_set())
            track.gate.set()
            first = await track.recv()
            self.assertEqual(first.pts, 960)
            self.assertNotEqual(bytes(first.planes[0]), bytes(1920))
            await track.recv()
            self.assertEqual(track.offset, len(track.pcm))
            self.assertFalse(track.drained.is_set())
            tail = await track.recv()
            self.assertTrue(track.drained.is_set())
            self.assertEqual(bytes(tail.planes[0]), bytes(1920))
        finally:
            track.stop()


if __name__ == "__main__":
    unittest.main()
