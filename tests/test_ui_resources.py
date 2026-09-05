"""Catalog printf contracts; runs without font-generation dependencies."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class CatalogTests(unittest.TestCase):
    def test_translations_and_printf_signatures(self):
        seen = set()
        for line in (ROOT / "components/vibe_ui/messages.tsv").read_text().splitlines():
            row = line.split("|")
            self.assertEqual(len(row), 4, line)
            self.assertTrue(all(row), line)
            self.assertNotIn(row[0].casefold(), seen)
            seen.add(row[0].casefold())
            signatures = [re.findall(r"%(?:[-+0 #]*\d*(?:\.\d+)?[lhz]*[diuoxXfsc]|%)", text)
                          for text in row]
            for signature in signatures[1:]:
                self.assertEqual(signatures[0], signature, line)


if __name__ == "__main__":
    unittest.main()
