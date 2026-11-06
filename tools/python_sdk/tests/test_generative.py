import os
import unittest
import infinite

class TestGenerative(unittest.TestCase):
    def test_generate_notes_missing_binary(self):
        # Even if find_binary is None, functions return structured error
        orig_bin = infinite.find_binary
        try:
            infinite.find_binary = lambda: None
            res = infinite.generate_notes()
            self.assertFalse(res["ok"])
            self.assertIn("error", res)
            self.assertIn("creator", res)
            self.assertEqual(res["creator"], "Naman Soni (n1m21n)")
        finally:
            infinite.find_binary = orig_bin

    def test_generate_sample_missing_binary(self):
        orig_bin = infinite.find_binary
        try:
            infinite.find_binary = lambda: None
            res = infinite.generate_sample()
            self.assertFalse(res["ok"])
            self.assertIn("error", res)
            self.assertIn("creator", res)
        finally:
            infinite.find_binary = orig_bin

    def test_render_mesh_missing_binary(self):
        orig_bin = infinite.find_binary
        try:
            infinite.find_binary = lambda: None
            res = infinite.render_mesh()
            self.assertFalse(res["ok"])
            self.assertIn("error", res)
            self.assertIn("creator", res)
        finally:
            infinite.find_binary = orig_bin

    def test_real_binary_generative_calls(self):
        bin_path = infinite.find_binary()
        if not bin_path or not os.path.exists(bin_path):
            self.skipTest("Infinite binary not available on host")
            
        r1 = infinite.generate_notes(kind="chords", duration=1.0)
        self.assertTrue(r1.get("ok"))
        self.assertIn("audio_wav", r1)
        self.assertEqual(r1.get("creator"), "Naman Soni (n1m21n)")
        
        r2 = infinite.generate_sample(part="bass", duration=1.0)
        self.assertTrue(r2.get("ok"))
        self.assertIn("audio_wav", r2)
        
        r3 = infinite.render_mesh(topology="plane")
        self.assertTrue(r3.get("ok"))
        self.assertIn("frame_path", r3)

if __name__ == "__main__":
    unittest.main()
