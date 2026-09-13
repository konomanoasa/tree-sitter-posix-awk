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
  use std::ops::Range;

  use tree_sitter::{
    Node, Parser, Query, QueryCursor, StreamingIterator, Tree,
  };

  struct EreCase {
    name: &'static str,
    source: &'static str,
    leaves: &'static [(&'static str, usize, usize)],
  }

  struct LeafCase {
    name: &'static str,
    source: &'static str,
    kind: &'static str,
    range: Range<usize>,
    leaves: &'static [(&'static str, usize, usize)],
  }

  const LEAF_CASES: &[LeafCase] = &[
    LeafCase {
      name: "a complete name remains one leaf",
      source: "foo\n",
      kind: "name",
      range: 0..3,
      leaves: &[("name", 0, 3)],
    },
    LeafCase {
      name: "a split name owns consecutive continuations separately",
      source: "fo\\\n\\\no\n",
      kind: "name",
      range: 0..7,
      leaves: &[
        ("token_content", 0, 2),
        ("line_continuation", 2, 4),
        ("line_continuation", 4, 6),
        ("token_content", 6, 7),
      ],
    },
    LeafCase {
      name: "a split keyword retains its complete physical range",
      source: "BE\\\nGIN {}\n",
      kind: "begin_keyword",
      range: 0..7,
      leaves: &[
        ("token_content", 0, 2),
        ("line_continuation", 2, 4),
        ("token_content", 4, 7),
      ],
    },
    LeafCase {
      name: "a split builtin excludes its following parenthesis",
      source: "{len\\\ngth(x)}\n",
      kind: "builtin_func_name",
      range: 1..9,
      leaves: &[
        ("token_content", 1, 4),
        ("line_continuation", 4, 6),
        ("token_content", 6, 9),
      ],
    },
    LeafCase {
      name: "a split function name excludes its following continuation",
      source: "{f\\\noo\\\n(x)}\n",
      kind: "func_name",
      range: 1..6,
      leaves: &[
        ("token_content", 1, 2),
        ("line_continuation", 2, 4),
        ("token_content", 4, 6),
      ],
    },
    LeafCase {
      name: "a split number retains decimal exponent and suffix fragments",
      source: "1\\\n.2e\\\n+3F\n",
      kind: "number",
      range: 0..11,
      leaves: &[
        ("token_content", 0, 1),
        ("line_continuation", 1, 3),
        ("token_content", 3, 6),
        ("line_continuation", 6, 8),
        ("token_content", 8, 11),
      ],
    },
    LeafCase {
      name: "a split logical operator retains one operator parent",
      source: "a|\\\n|b\n",
      kind: "or",
      range: 1..5,
      leaves: &[
        ("token_content", 1, 2),
        ("line_continuation", 2, 4),
        ("token_content", 4, 5),
      ],
    },
    LeafCase {
      name: "complete Unicode string content remains one leaf",
      source: "\"é😀\"\n",
      kind: "string_content",
      range: 1..7,
      leaves: &[("string_content", 1, 7)],
    },
    LeafCase {
      name: "split Unicode string content retains byte ranges",
      source: "\"é\\\n😀\"\n",
      kind: "string_content",
      range: 1..9,
      leaves: &[
        ("token_content", 1, 3),
        ("line_continuation", 3, 5),
        ("token_content", 5, 9),
      ],
    },
    LeafCase {
      name: "a split octal escape excludes the following string character",
      source: "\"\\1\\\n23x\"\n",
      kind: "escape_sequence",
      range: 1..7,
      leaves: &[
        ("token_content", 1, 3),
        ("line_continuation", 3, 5),
        ("token_content", 5, 7),
      ],
    },
    LeafCase {
      name: "a split escaped slash keeps the escape introducer separate",
      source: "/\\\\\n//\n",
      kind: "escaped_delimiter",
      range: 1..5,
      leaves: &[
        ("token_content", 1, 2),
        ("line_continuation", 2, 4),
        ("token_content", 4, 5),
      ],
    },
    LeafCase {
      name: "a split ERE class name excludes compound delimiters",
      source: "/[[:al\\\npha:]]/\n",
      kind: "class_name",
      range: 4..11,
      leaves: &[
        ("token_content", 4, 6),
        ("line_continuation", 6, 8),
        ("token_content", 8, 11),
      ],
    },
    LeafCase {
      name: "a split ERE repetition count remains one count",
      source: "/a{1\\\n2}/\n",
      kind: "dup_count",
      range: 3..7,
      leaves: &[
        ("token_content", 3, 4),
        ("line_continuation", 4, 6),
        ("token_content", 6, 7),
      ],
    },
  ];

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

  fn parse(source: &str, name: &str) -> Tree {
    let mut parser = Parser::new();
    parser
      .set_language(&super::LANGUAGE.into())
      .expect("generated grammar must load");
    let tree = parser
      .parse(source, None)
      .expect("parser must return a tree");
    assert!(!tree.root_node().has_error(), "{name}");
    tree
  }

  fn assert_leaf_partition(
    node: Node<'_>,
    expected: &[(&str, usize, usize)],
    name: &str,
  ) {
    let mut leaves = Vec::new();
    collect_leaves(node, &mut leaves);
    let actual: Vec<_> = leaves
      .iter()
      .map(|leaf| (leaf.kind(), leaf.start_byte(), leaf.end_byte()))
      .collect();
    assert_eq!(actual, expected, "{name}");
    let mut end = node.start_byte();
    for leaf in leaves {
      assert_eq!(leaf.start_byte(), end, "{name}");
      assert!(leaf.end_byte() > end, "{name}");
      end = leaf.end_byte();
    }
    assert_eq!(end, node.end_byte(), "{name}");
  }

  fn assert_leaf_highlights(
    source: &str,
    tree: &Tree,
    ranges: impl IntoIterator<Item = Range<usize>>,
    name: &str,
    query: &Query,
  ) {
    let mut cursor = QueryCursor::new();
    let mut covered = vec![false; source.len()];
    let mut captures =
      cursor.captures(query, tree.root_node(), source.as_bytes());
    while let Some((matched, index)) = captures.next() {
      let node = matched.captures()[*index].node;
      assert_eq!(
        node.child_count(),
        0,
        "{name}: {} {:?} must be a leaf",
        node.kind(),
        node.byte_range(),
      );
      covered[node.byte_range()].fill(true);
    }
    for range in ranges {
      assert!(covered[range].iter().all(|byte| *byte), "{name}");
    }
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
      let tree = parse(&format!("{}\n", case.source), case.name);
      let ere = tree
        .root_node()
        .descendant_for_byte_range(0, case.source.len())
        .expect("ERE must span the entire fixture");
      assert_eq!(ere.kind(), "ere", "{}", case.name);
      assert_leaf_partition(ere, case.leaves, case.name);
    }
  }

  #[test]
  fn highlights_capture_only_leaves_and_cover_ere_and_token_spellings() {
    let query = Query::new(&super::LANGUAGE.into(), super::HIGHLIGHTS_QUERY)
      .expect("highlight query must compile");
    for case in ERE_CASES {
      let source = format!("{}\n", case.source);
      let tree = parse(&source, case.name);
      assert_leaf_highlights(
        &source,
        &tree,
        std::iter::once(0..case.source.len()),
        case.name,
        &query,
      );
    }
    for case in LEAF_CASES {
      let tree = parse(case.source, case.name);
      assert_leaf_highlights(
        case.source,
        &tree,
        std::iter::once(case.range.clone()),
        case.name,
        &query,
      );
    }
  }

  #[test]
  fn split_leaves_partition_source_and_own_only_their_internal_continuations() {
    for case in LEAF_CASES {
      let tree = parse(case.source, case.name);
      let node = tree
        .root_node()
        .descendant_for_byte_range(case.range.start, case.range.end)
        .expect("lexical node must span the expected range");
      assert_eq!(node.kind(), case.kind, "{}", case.name);
      assert_eq!(node.byte_range(), case.range, "{}", case.name);
      assert_leaf_partition(node, case.leaves, case.name);
      let mut cursor = node.walk();
      if cursor.goto_first_child() {
        loop {
          let child = cursor.node();
          assert_eq!(child.child_count(), 0, "{}", case.name);
          match child.kind() {
            "token_content" => {
              assert_eq!(cursor.field_name(), Some("content"), "{}", case.name);
            }
            "line_continuation" => {
              assert_eq!(cursor.field_name(), None, "{}", case.name);
              assert_eq!(
                child.utf8_text(case.source.as_bytes()).unwrap(),
                "\\\n",
                "{}",
                case.name,
              );
            }
            kind => panic!("{}: unexpected lexical child {kind}", case.name),
          }
          if !cursor.goto_next_sibling() {
            break;
          }
        }
      }
    }
  }

  #[derive(Debug, PartialEq, Eq)]
  struct LogicalNode {
    kind: String,
    field: Option<String>,
    range: Range<usize>,
    spelling: Option<String>,
    children: Vec<LogicalNode>,
  }

  fn logical_node(
    node: Node<'_>,
    field: Option<&str>,
    source: &str,
    continuations: &[Range<usize>],
  ) -> Option<LogicalNode> {
    if node.kind() == "line_continuation" {
      return None;
    }
    let split = node.child_by_field_name("content").is_some();
    let spelling = (split || node.child_count() == 0).then(|| {
      let text = node.utf8_text(source.as_bytes()).unwrap();
      if split {
        text.replace("\\\n", "")
      } else {
        text.to_owned()
      }
    });
    let mut children = Vec::new();
    let mut cursor = node.walk();
    if cursor.goto_first_child() {
      loop {
        let child = cursor.node();
        if split {
          assert!(
            matches!(child.kind(), "token_content" | "line_continuation"),
            "{} {:?} unexpectedly owns {} {:?}",
            node.kind(),
            node.byte_range(),
            child.kind(),
            child.byte_range()
          );
        } else if let Some(child) =
          logical_node(child, cursor.field_name(), source, continuations)
        {
          children.push(child);
        }
        if !cursor.goto_next_sibling() {
          break;
        }
      }
    }
    let logical_offset = |offset| {
      offset
        - continuations
          .iter()
          .take_while(|range| range.end <= offset)
          .map(Range::len)
          .sum::<usize>()
    };
    Some(LogicalNode {
      kind: node.kind().to_owned(),
      field: field.map(str::to_owned),
      range: logical_offset(node.start_byte())..logical_offset(node.end_byte()),
      spelling,
      children,
    })
  }

  fn logical_tree(tree: &Tree, source: &str) -> Option<LogicalNode> {
    let mut leaves = Vec::new();
    collect_leaves(tree.root_node(), &mut leaves);
    let continuations: Vec<_> = leaves
      .into_iter()
      .filter(|node| node.kind() == "line_continuation")
      .map(|node| node.byte_range())
      .collect();
    logical_node(tree.root_node(), None, source, &continuations)
  }

  #[test]
  fn continued_sources_preserve_logical_cst_fields_classification_and_spelling()
  {
    for (name, plain, continued) in [
      (
        "function declarations, parameters, calls and keywords",
        "function foo(param) { return length(param) }\n",
        "fun\\\nction f\\\noo(pa\\\nram) { re\\\nturn len\\\ngth(pa\\\nram) }\n",
      ),
      (
        "function adjacency across repeated boundary continuations",
        "{foo(x); foo (x)}\n",
        "{fo\\\no\\\n\\\n(x); fo\\\no \\\n(x)}\n",
      ),
      (
        "for-in lookahead and reserved words",
        "BEGIN { for (key in array) print key }\n",
        "BE\\\nGIN { f\\\nor (k\\\ney i\\\nn arr\\\nay) pr\\\nint k\\\ney }\n",
      ),
      (
        "decimal fraction, exponent, sign and suffix",
        "1.2e+3F\n",
        "1\\\n.2e\\\n+3F\n",
      ),
      (
        "Unicode content, octal escape and escaped quote",
        "\"é😀\\123x\\\"\"\n",
        "\"é\\\n😀\\1\\\n23x\\\\\n\"\"\n",
      ),
      (
        "literal boundary continuations do not create empty content",
        "{print \"\";print /a/}\n",
        "{print \"\\\n\";print /\\\na\\\n/}\n",
      ),
      (
        "ERE ordinary characters, alternation and repetition modifier",
        "/(ab|c)+?/\n",
        "/(a\\\nb|\\\nc)\\\n+\\\n?/\n",
      ),
      (
        "ERE compound delimiters, class name and repetition count",
        "/[[:alpha:][.ch.][=a=]]{12}/\n",
        "/[[\\\n:al\\\npha:\\\n][\\\n.ch.\\\n][\\\n=a=\\\n]]{1\\\n2}/\n",
      ),
      (
        "ERE named, octal, quoted and delimiter escapes",
        "/\\n\\123\\.\\//\n",
        "/\\\\\nn\\1\\\n23\\\\\n.\\\\\n//\n",
      ),
      (
        "assignment and update operators",
        "{x+=1;x-=2;x*=3;x/=4;x%=5;x^=6;x++;x--}\n",
        "{x+\\\n=1;x-\\\n=2;x*\\\n=3;x/\\\n=4;x%\\\n=5;x^\\\n=6;x+\\\n+;x-\\\n-}\n",
      ),
      (
        "logical, comparison, match and output operators",
        "{print a&&b||c;print (a==b),(a!=b),(a<=b),(a>=b),(a!~b);print x>>f}\n",
        "{print a&\\\n&b|\\\n|c;print (a=\\\n=b),(a!\\\n=b),(a<\\\n=b),(a>\\\n=b),(a!\\\n~b);print x>\\\n>f}\n",
      ),
      (
        "comment backslashes remain raw comment text",
        "BEGIN { print 1 # keep \\\n}\n",
        "BE\\\nGIN { print 1 # keep \\\n}\n",
      ),
    ] {
      let plain_tree = parse(plain, name);
      let continued_tree = parse(continued, name);
      assert_eq!(
        logical_tree(&continued_tree, continued),
        logical_tree(&plain_tree, plain),
        "{name}",
      );
    }
  }

  #[test]
  fn line_continuations_keep_exact_ranges_and_owners() {
    struct Case {
      name: &'static str,
      source: &'static str,
      owners: &'static [(usize, usize, &'static str, usize, usize)],
    }
    let query = Query::new(&super::LANGUAGE.into(), super::HIGHLIGHTS_QUERY)
      .expect("highlight query must compile");
    for case in [
      Case {
        name: "source and AWK expression boundaries",
        source: "\\\nBEGIN { print f\\\n(x) \\\n+ \\\ny }\\\n",
        owners: &[
          (0, 2, "program", 0, 34),
          (17, 19, "non_unary_print_expr", 16, 22),
          (23, 25, "non_unary_print_expr", 16, 30),
          (27, 29, "non_unary_print_expr", 16, 30),
          (32, 34, "program", 0, 34),
        ],
      },
      Case {
        name: "ERE concatenation boundary",
        source: "/a\\\nb/\n",
        owners: &[(2, 4, "ere_branch", 1, 5)],
      },
      Case {
        name: "ERE opening and closing boundaries",
        source: "/\\\na\\\n/\n",
        owners: &[(1, 3, "ere", 0, 7), (4, 6, "ere", 0, 7)],
      },
      Case {
        name: "string opening and closing boundaries",
        source: "\"\\\na\\\n\"\n",
        owners: &[(1, 3, "string", 0, 7), (4, 6, "string", 0, 7)],
      },
      Case {
        name: "compound class delimiter boundaries",
        source: "/[[\\\n:a:\\\n]]/\n",
        owners: &[
          (3, 5, "character_class", 2, 11),
          (8, 10, "character_class", 2, 11),
        ],
      },
      Case {
        name: "a trailing hyphen excludes its following continuation",
        source: "/[-\\\n]/\n",
        owners: &[(3, 5, "bracket_expression", 1, 6)],
      },
    ] {
      let tree = parse(case.source, case.name);
      let mut leaves = Vec::new();
      collect_leaves(tree.root_node(), &mut leaves);
      let actual: Vec<_> = leaves
        .into_iter()
        .filter(|node| node.kind() == "line_continuation")
        .map(|node| {
          assert_eq!(node.utf8_text(case.source.as_bytes()).unwrap(), "\\\n");
          let owner = node.parent().expect("continuation must have an owner");
          (
            node.start_byte(),
            node.end_byte(),
            owner.kind(),
            owner.start_byte(),
            owner.end_byte(),
          )
        })
        .collect();
      assert_eq!(actual, case.owners, "{}", case.name);
      assert_leaf_highlights(
        case.source,
        &tree,
        case.owners.iter().map(|(start, end, ..)| *start..*end),
        case.name,
        &query,
      );
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
