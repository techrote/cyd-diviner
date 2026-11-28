"""Digital math, channel ordering and failed-core evidence fixtures only."""
import argparse
import array
import json
import pathlib
import tempfile
import unittest
import wave
from unittest import mock

from audio_mvp_simulate import digital_quality, moments, run


class AudioModelTests(unittest.TestCase):
    def test_perfect_and_constant_signals_do_not_emit_fake_finite_snr(self):
        exact = moments([1, 3, -5, 7], [1, 3, -5, 7])
        self.assertEqual(exact["correlation"], 1)
        self.assertTrue(exact["perfect_sample_match"])
        self.assertIsNone(exact["snr_db"])
        constant = moments([0, 0, 0], [1, 1, 1])
        self.assertIsNone(constant["correlation"])
        self.assertIsNone(constant["snr_db"])
        self.assertEqual(constant["error_rms_pcm16"], 1)

    def test_explicit_lag_alignment_preserves_unaligned_error(self):
        source = [((index * index * 977 + index * 41) % 30000) - 15000 for index in range(300)]
        shifted = [0] * 5 + source[:-5]
        quality = digital_quality(source, shifted, 1, maximum_lag=10)[0]
        self.assertEqual(quality["alignment_lag_frames"], 5)
        self.assertGreater(quality["unaligned"]["error_rms_pcm16"], 0)
        self.assertEqual(quality["aligned"]["error_rms_pcm16"], 0)

    def test_core_invocation_lengths_and_multichannel_wav_are_verified(self):
        for bad_length in (False, True):
            with tempfile.TemporaryDirectory() as temporary:
                root = pathlib.Path(temporary)
                core = root / "fixture.exe"
                core.write_bytes(b"NOT AN EXECUTABLE; mocked invocation")
                args = argparse.Namespace(core=core, output=root / "model", rate=24000, channels=2,
                    block_frames=16, osr=16, frames=128, seed=23063, max_lag=4, timeout=2, cases=["clean:0"])

                def execute(command, **_):
                    samples = array.array("h", [value for index in range(128) for value in (index, -index)])
                    pathlib.Path(command[-2]).write_bytes(samples.tobytes())
                    pathlib.Path(command[-1]).write_bytes(samples.tobytes()[:-2] if bad_length else samples.tobytes())
                    receipt = {"schema": "offline_audio_model/1", "evidence_class": "OFFLINE_MODEL",
                        "sample_rate_hz": 24000, "channels": 2, "block_frames": 16, "oversample": 16,
                        "total_pcm_frames": 128, "seed": 23063, "impairment": "clean", "strength": 0}
                    return mock.Mock(returncode=0, stdout=json.dumps(receipt).encode(), stderr=b"")

                with mock.patch("audio_mvp_simulate.subprocess.run", side_effect=execute):
                    if bad_length:
                        with self.assertRaisesRegex(ValueError, "PCM length"):
                            run(args)
                    else:
                        self.assertEqual(run(args), 0)
                result = json.loads((args.output / "results.json").read_text())
                self.assertEqual(result["status"], "FAILED" if bad_length else "MODEL COMPLETE")
                self.assertEqual(result["evidence_class"], "OFFLINE_MODEL")
                if not bad_length:
                    with wave.open(str(args.output / "01-clean-0/comparison.wav"), "rb") as stream:
                        self.assertEqual((stream.getnchannels(), stream.getframerate(), stream.getnframes()), (4, 24000, 128))
                        values = array.array("h", stream.readframes(2))
                        self.assertEqual(list(values), [0, 0, 0, 0, 1, -1, 1, -1])
                self.assertTrue((args.output / "manifest.json").exists())


if __name__ == "__main__":
    unittest.main()
