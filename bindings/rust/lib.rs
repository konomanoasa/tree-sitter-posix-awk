//! POSIX awk grammar for the Tree-sitter parsing library.
//!
//! ```
//! let mut parser = tree_sitter::Parser::new();
//! parser
//!     .set_language(&tree_sitter_posix_awk::LANGUAGE.into())
//!     .expect("POSIX awk grammar must load");
//! let tree = parser
//!     .parse("BEGIN { print 1 }\n", None)
//!     .expect("parser must return a tree");
//! assert!(!tree.root_node().has_error());
//! ```

use tree_sitter_language::LanguageFn;

unsafe extern "C" {
  fn tree_sitter_posix_awk() -> *const ();
}

/// The tree-sitter [`LanguageFn`] for this grammar.
pub const LANGUAGE: LanguageFn =
  unsafe { LanguageFn::from_raw(tree_sitter_posix_awk) };

/// The content of the [`node-types.json`] file for this grammar.
///
/// [`node-types.json`]: https://tree-sitter.github.io/tree-sitter/using-parsers/6-static-node-types
pub const NODE_TYPES: &str = include_str!("../../src/node-types.json");

#[cfg(with_highlights_query)]
/// The syntax highlighting query for this grammar.
pub const HIGHLIGHTS_QUERY: &str = include_str!("../../queries/highlights.scm");

#[cfg(with_injections_query)]
/// The language injection query for this grammar.
pub const INJECTIONS_QUERY: &str = include_str!("../../queries/injections.scm");

#[cfg(with_locals_query)]
/// The local variable query for this grammar.
pub const LOCALS_QUERY: &str = include_str!("../../queries/locals.scm");

#[cfg(with_tags_query)]
/// The symbol tagging query for this grammar.
pub const TAGS_QUERY: &str = include_str!("../../queries/tags.scm");

#[cfg(test)]
mod tests {
  use tree_sitter::{
    Node, Parser, Query, QueryCursor, StreamingIterator, Tree,
  };

  struct EreCase {
    name: &'static str,
    source: &'static str,
    leaves: &'static [(&'static str, usize, usize)],
  }

  const ERE_CASES: &[EreCase] = &[
    EreCase {
      name: "ordinary characters and complete escapes retain their source ranges",
      source: r"/a é)}.\n\/\\\141\q(a*?)/",
      leaves: &[
        ("/", 0, 1),
        ("ordinary_character_content", 1, 2),
        ("ordinary_character_content", 2, 3),
        ("ordinary_character_content", 3, 5),
        (")", 5, 6),
        ("}", 6, 7),
        (".", 7, 8),
        ("escape_sequence", 8, 10),
        ("escaped_delimiter", 10, 12),
        ("escape_sequence", 12, 14),
        ("escape_sequence", 14, 18),
        ("escape_sequence", 18, 20),
        ("(", 20, 21),
        ("ordinary_character_content", 21, 22),
        ("*", 22, 23),
        ("?", 23, 24),
        (")", 24, 25),
        ("/", 25, 26),
      ],
    },
    EreCase {
      name: "collating forms separate raw fragments from adjacent escapes",
      source: r"/[[.a\nb.][=é\t犬=][.\/\141\q.]]/",
      leaves: &[
        ("/", 0, 1),
        ("[", 1, 2),
        ("[", 2, 3),
        (".", 3, 4),
        ("collating_element_content", 4, 5),
        ("escape_sequence", 5, 7),
        ("collating_element_content", 7, 8),
        (".", 8, 9),
        ("]", 9, 10),
        ("[", 10, 11),
        ("=", 11, 12),
        ("collating_element_content", 12, 14),
        ("escape_sequence", 14, 16),
        ("collating_element_content", 16, 19),
        ("=", 19, 20),
        ("]", 20, 21),
        ("[", 21, 22),
        (".", 22, 23),
        ("escaped_delimiter", 23, 25),
        ("escape_sequence", 25, 29),
        ("escape_sequence", 29, 31),
        (".", 31, 32),
        ("]", 32, 33),
        ("]", 33, 34),
        ("/", 34, 35),
      ],
    },
    EreCase {
      name: "compound punctuation, spaces and Unicode retain individual leaves",
      source: r"/[[. a-]🙂.]]/",
      leaves: &[
        ("/", 0, 1),
        ("[", 1, 2),
        ("[", 2, 3),
        (".", 3, 4),
        ("collating_element_content", 4, 5),
        ("collating_element_content", 5, 6),
        ("collating_element_content", 6, 7),
        ("collating_element_content", 7, 8),
        ("collating_element_content", 8, 12),
        (".", 12, 13),
        ("]", 13, 14),
        ("]", 14, 15),
        ("/", 15, 16),
      ],
    },
    EreCase {
      name: "bracket literals distinguish raw brackets and a trailing hyphen",
      source: r"/[]a[\/\n-]/",
      leaves: &[
        ("/", 0, 1),
        ("[", 1, 2),
        ("collating_element_content", 2, 3),
        ("collating_element_content", 3, 4),
        ("collating_element_content", 4, 5),
        ("escaped_delimiter", 5, 7),
        ("escape_sequence", 7, 9),
        ("-", 9, 10),
        ("]", 10, 11),
        ("/", 11, 12),
      ],
    },
    EreCase {
      name: "range and list hyphens retain anonymous leaves",
      source: r"/[--ac-]/",
      leaves: &[
        ("/", 0, 1),
        ("[", 1, 2),
        ("-", 2, 3),
        ("-", 3, 4),
        ("collating_element_content", 4, 5),
        ("collating_element_content", 5, 6),
        ("-", 6, 7),
        ("]", 7, 8),
        ("/", 8, 9),
      ],
    },
  ];

  fn parse_ere(case: &EreCase) -> Tree {
    let mut parser = Parser::new();
    parser
      .set_language(&super::LANGUAGE.into())
      .expect("generated grammar must load");
    let tree = parser
      .parse(format!("{}\n", case.source), None)
      .expect("parser must return a tree");
    assert!(!tree.root_node().has_error(), "{}", case.name);
    tree
  }

  fn collect_leaves<'tree>(node: Node<'tree>, leaves: &mut Vec<Node<'tree>>) {
    if node.child_count() == 0 {
      leaves.push(node);
    } else {
      for child in node.children(&mut node.walk()) {
        collect_leaves(child, leaves);
      }
    }
  }

  #[test]
  fn ere_leaves_partition_complete_source_spellings() {
    for case in ERE_CASES {
      let tree = parse_ere(case);
      let ere = tree
        .root_node()
        .descendant_for_byte_range(0, case.source.len())
        .expect("ERE must span the entire fixture");
      assert_eq!(ere.kind(), "ere", "{}", case.name);
      let mut leaves = Vec::new();
      collect_leaves(ere, &mut leaves);
      let actual: Vec<_> = leaves
        .iter()
        .map(|node| (node.kind(), node.start_byte(), node.end_byte()))
        .collect();
      assert_eq!(actual, case.leaves, "{}", case.name);
      let mut end = 0;
      for node in leaves {
        assert_eq!(node.start_byte(), end, "{}", case.name);
        assert!(node.end_byte() > end, "{}", case.name);
        end = node.end_byte();
      }
      assert_eq!(end, case.source.len(), "{}", case.name);
    }
  }

  #[test]
  fn ere_highlights_capture_only_leaves_and_cover_every_source_byte() {
    let query = Query::new(&super::LANGUAGE.into(), super::HIGHLIGHTS_QUERY)
      .expect("highlight query must compile");
    let mut cursor = QueryCursor::new();
    for case in ERE_CASES {
      let tree = parse_ere(case);
      let source = format!("{}\n", case.source);
      let mut covered = vec![false; case.source.len()];
      let mut captures =
        cursor.captures(&query, tree.root_node(), source.as_bytes());
      while let Some((matched, index)) = captures.next() {
        let node = matched.captures()[*index].node;
        assert_eq!(
          node.child_count(),
          0,
          "{}: {} {:?} must be a leaf",
          case.name,
          node.kind(),
          node.byte_range(),
        );
        covered[node.byte_range()].fill(true);
      }
      assert!(covered.into_iter().all(|byte| byte), "{}", case.name);
    }
  }

  #[test]
  fn posix_awk_grammar_loads_and_parses() {
    let source = "BEGIN { print 1 }\n";
    let mut parser = tree_sitter::Parser::new();
    parser
      .set_language(&super::LANGUAGE.into())
      .expect("generated grammar must load");
    let tree = parser
      .parse(source, None)
      .expect("parser must return a tree");
    let root = tree.root_node();
    assert_eq!(root.kind(), "program");
    assert_eq!(root.start_byte(), 0);
    assert_eq!(root.end_byte(), source.len());
    assert!(!root.has_error());
  }
}
