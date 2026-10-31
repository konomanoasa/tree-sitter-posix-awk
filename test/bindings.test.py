import unittest
from types import CapsuleType

import tree_sitter_posix_awk

from tree_sitter import Language, Parser, Query, QueryCursor


class BindingsTest(unittest.TestCase):
    def test_language_capsule_parses_source_with_external_scanner(self):
        capsule = tree_sitter_posix_awk.language()
        self.assertIsInstance(capsule, CapsuleType)
        language = Language(capsule)
        self.assertEqual(language.name, "posix_awk")
        source = b"/a/ { print $1 }\n"
        tree = Parser(language).parse(source)
        self.assertEqual(tree.root_node.type, "program")
        self.assertFalse(tree.root_node.has_error)
        self.assertEqual(tree.root_node.text, source)

    def test_packaged_highlights_capture_keyword(self):
        language = Language(tree_sitter_posix_awk.language())
        tree = Parser(language).parse(b"/a/ { print $1 }\n")
        query = Query(language, tree_sitter_posix_awk.HIGHLIGHTS_QUERY)
        captures = QueryCursor(query).captures(tree.root_node)
        self.assertEqual(
            [node.text for node in captures["keyword"]], [b"print"]
        )


if __name__ == "__main__":
    unittest.main()
