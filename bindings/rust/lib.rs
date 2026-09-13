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
  }

  const LEAF_CASES: &[LeafCase] = &[
    LeafCase {
      name: "a complete name remains one leaf",
      source: "foo\n",
      kind: "name",
      range: 0..3,
    },
    LeafCase {
      name: "the name before consecutive continuations remains separate",
      source: "fo\\\n\\\no\n",
      kind: "name",
      range: 0..2,
    },
    LeafCase {
      name: "the name after consecutive continuations remains separate",
      source: "fo\\\n\\\no\n",
      kind: "name",
      range: 6..7,
    },
    LeafCase {
      name: "a keyword excludes its following continuation",
      source: "BEGIN\\\n{}\n",
      kind: "begin_keyword",
      range: 0..5,
    },
    LeafCase {
      name: "a builtin excludes its following continuation",
      source: "{length\\\n(x)}\n",
      kind: "builtin_func_name",
      range: 1..7,
    },
    LeafCase {
      name: "a function name excludes its adjacent parenthesis",
      source: "{foo(x)}\n",
      kind: "func_name",
      range: 1..4,
    },
    LeafCase {
      name: "a continuation before the parenthesis makes a variable name",
      source: "{foo\\\n(x)}\n",
      kind: "name",
      range: 1..4,
    },
    LeafCase {
      name: "a decimal constant includes its exponent and suffix",
      source: "1.2e+3F\n",
      kind: "number",
      range: 0..7,
    },
    LeafCase {
      name: "a logical operator excludes its following continuation",
      source: "a||\\\nb\n",
      kind: "or",
      range: 1..3,
    },
    LeafCase {
      name: "Unicode string content remains one leaf",
      source: "\"é😀\"\n",
      kind: "string_content",
      range: 1..7,
    },
    LeafCase {
      name: "an octal escape excludes the following string character",
      source: "\"\\123x\"\n",
      kind: "escape_sequence",
      range: 1..5,
    },
    LeafCase {
      name: "an escaped slash excludes the closing delimiter",
      source: "/\\//\n",
      kind: "escaped_delimiter",
      range: 1..3,
    },
    LeafCase {
      name: "an ERE class name excludes compound delimiters",
      source: "/[[:alpha:]]/\n",
      kind: "class_name",
      range: 4..9,
    },
    LeafCase {
      name: "an ERE repetition count remains one leaf",
      source: "/a{12}/\n",
      kind: "dup_count",
      range: 3..5,
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
  fn fields_belong_to_direct_children() {
    let language: tree_sitter::Language = super::LANGUAGE.into();
    let fields: Vec<_> = (1..=language.field_count())
      .map(|id| {
        language
          .field_name_for_id(id as u16)
          .expect("declared field must have a name")
      })
      .collect();
    for (name, source) in [
      ("unterminated action", "{}"),
      ("unterminated pattern", "a"),
      ("unterminated pattern and action", "a {}"),
      ("unterminated function", "function f(a,b) {}"),
      ("terminated item", "{}\n"),
      ("leading newline", "\n{}\n"),
      ("terminated and unterminated items", "BEGIN {}\nEND {}"),
      (
        "function name and parameter boundaries",
        "function f\\\n(a,\\\nb) {}",
      ),
      (
        "nested statements and expressions",
        "BEGIN { if (a) print a ? b : c > d; else for (i in a) { getline a[i] < file } }\n",
      ),
      (
        "shared direct input and pipe aliases",
        "{print $-getline target < (a | getline); ($+$a=b) | getline target}\n",
      ),
      ("nested field after arithmetic unary", "{$+$a}"),
      ("nested field after logical unary in print", "{print $!$a}"),
      (
        "postfix field and getline target aliases",
        "{$-x++; $(-x)++; $$x++; $getline x++; $getline $-x++; $getline $(x)++}\n",
      ),
      ("group alternation", "/(a|b)c/"),
      ("group concatenation", "/(ab)c/"),
      ("nested group repetition", "/((a|b)+?c)d/"),
      ("ERE boundary continuation", "/(a|b)/\\\n&& /c/"),
      ("ungrouped expression", "/a|bc+/"),
    ] {
      let tree = parse(source, name);
      let mut pending = vec![tree.root_node()];
      while let Some(node) = pending.pop() {
        for field in &fields {
          if let Some(child) = node.child_by_field_name(field) {
            assert_eq!(
              child.parent(),
              Some(node),
              "{name}: {}.{field} must name a direct child",
              node.kind(),
            );
          }
        }
        pending.extend(node.children(&mut node.walk()));
      }
    }
  }

  #[test]
  fn unary_field_assignment_preserves_target_and_right_association() {
    for (name, target, operand) in [
      ("positive name", "$+a", "+a"),
      ("negative name", "$-a", "-a"),
      ("negated array element", "$!a[b]", "!a[b]"),
      ("right associative exponentiation", "$-a^b^c", "-a^b^c"),
      ("nested field reference", "$+$a", "+$a"),
      ("postfix update inside unary operand", "$+a++", "+a++"),
      ("unary operand boundary", "$-\\\nname", "-\\\nname"),
    ] {
      for operator in ["=", "+=", "-=", "*=", "/=", "%=", "^="] {
        for (prefix, kind) in
          [("{", "non_unary_expr"), ("{print ", "non_unary_print_expr")]
        {
          let source = format!("{prefix}{target}{operator}c=d}}\n");
          let label = format!("{name}: {source:?}");
          let tree = parse(&source, &label);
          let assignment = tree
            .root_node()
            .descendant_for_byte_range(prefix.len(), source.len() - 2)
            .unwrap();
          assert_eq!(assignment.kind(), kind, "{label}");
          let left = assignment.child_by_field_name("left").unwrap();
          assert_eq!(left.kind(), "lvalue", "{label}");
          assert_eq!(
            left.utf8_text(source.as_bytes()).unwrap(),
            target,
            "{label}"
          );
          for (node, field, expected) in [
            (assignment, "operator", operator),
            (left, "operator", "$"),
            (left, "operand", operand),
            (assignment, "right", "c=d"),
          ] {
            let child = node.child_by_field_name(field).unwrap();
            assert_eq!(child.parent(), Some(node), "{label}: {field}");
            assert_eq!(
              child.utf8_text(source.as_bytes()).unwrap(),
              expected,
              "{label}: {field}",
            );
          }
          let right = assignment.child_by_field_name("right").unwrap();
          let nested = right.named_child(0).unwrap();
          for (field, expected) in
            [("left", "c"), ("operator", "="), ("right", "d")]
          {
            assert_eq!(
              nested
                .child_by_field_name(field)
                .unwrap()
                .utf8_text(source.as_bytes())
                .unwrap(),
              expected,
              "{label}: nested {field}",
            );
          }
        }
      }
    }
  }

  #[test]
  fn unary_field_assignment_keeps_parenthesized_pipe_ownership() {
    for (name, source, pipe_source) in [
      (
        "assignment supplies pipe",
        "{($+a=b) | getline}\n",
        "($+a=b)",
      ),
      ("pipe supplies assignment", "{$+a=(b | getline)}\n", "b"),
    ] {
      let tree = parse(source, name);
      let mut pending = vec![tree.root_node()];
      let mut fields = Vec::new();
      let mut sources = Vec::new();
      while let Some(node) = pending.pop() {
        if node.kind() == "lvalue"
          && node.child_by_field_name("operator").is_some()
        {
          fields.push(node.utf8_text(source.as_bytes()).unwrap());
        }
        if let Some(child) = node.child_by_field_name("source") {
          sources.push(child.utf8_text(source.as_bytes()).unwrap());
        }
        pending.extend(node.children(&mut node.walk()));
      }
      assert_eq!(fields, ["$+a"], "{name}");
      assert_eq!(sources, [pipe_source], "{name}");
    }
  }

  #[test]
  fn nested_unary_fields_complete_before_the_outer_assignment() {
    for (name, atom, operator, right) in [
      ("name", "a", "=", "b"),
      ("number", "1", "+=", "b=c"),
      ("parenthesized pipe", "(cmd | getline)", "=", "b"),
      ("call argument pipe", "f(cmd | getline)", "=", "b"),
      ("subscript pipe", "a[cmd | getline]", "=", "b"),
    ] {
      for depth in [10, 64] {
        for prefix in ["{", "{print "] {
          let target = format!("{}${atom}", "$+".repeat(depth));
          let source = format!("{prefix}{target}{operator}{right}}}\n");
          let label = format!("{name}, depth {depth}, {prefix}");
          let tree = parse(&source, &label);
          let assignment = tree
            .root_node()
            .descendant_for_byte_range(prefix.len(), source.len() - 2)
            .unwrap();
          for (field, expected) in [
            ("left", target.as_str()),
            ("operator", operator),
            ("right", right),
          ] {
            assert_eq!(
              assignment
                .child_by_field_name(field)
                .unwrap()
                .utf8_text(source.as_bytes())
                .unwrap(),
              expected,
              "{label}: {field}",
            );
          }
          let mut lvalue = assignment.child_by_field_name("left").unwrap();
          for level in 0..=depth {
            assert_eq!(lvalue.kind(), "lvalue", "{label}: level {level}");
            assert_eq!(lvalue.start_byte(), prefix.len() + level * 2);
            assert_eq!(lvalue.end_byte(), prefix.len() + target.len());
            assert_eq!(
              lvalue.child_by_field_name("operator").unwrap().kind(),
              "$"
            );
            let operand = lvalue.child_by_field_name("operand").unwrap();
            let expression = operand.named_child(0).unwrap();
            if level == depth {
              assert_eq!(
                operand.utf8_text(source.as_bytes()).unwrap(),
                atom,
                "{label}"
              );
              assert_eq!(expression.kind(), "non_unary_expr", "{label}");
            } else {
              assert_eq!(expression.kind(), "unary_expr", "{label}");
              assert_eq!(
                expression.child_by_field_name("operator").unwrap().kind(),
                "+"
              );
              lvalue = expression
                .child_by_field_name("operand")
                .unwrap()
                .named_child(0)
                .unwrap()
                .named_child(0)
                .unwrap();
            }
          }
        }
      }
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
  fn lexical_tokens_keep_complete_leaf_ranges() {
    for case in LEAF_CASES {
      let tree = parse(case.source, case.name);
      let node = tree
        .root_node()
        .descendant_for_byte_range(case.range.start, case.range.end)
        .expect("lexical node must span the expected range");
      assert_eq!(node.kind(), case.kind, "{}", case.name);
      assert_eq!(node.byte_range(), case.range, "{}", case.name);
      assert_eq!(node.child_count(), 0, "{}", case.name);
    }
  }

  #[test]
  fn continuation_markers_are_anonymous_and_exclude_newlines() {
    struct Case {
      name: &'static str,
      source: &'static str,
      markers: &'static [(usize, usize)],
    }
    assert!(!super::NODE_TYPES.contains("\"line_continuation\""));
    let query = Query::new(&super::LANGUAGE.into(), super::HIGHLIGHTS_QUERY)
      .expect("highlight query must compile");
    for case in [
      Case {
        name: "a continuation alone creates no newline token",
        source: "\\\n",
        markers: &[(0, 1)],
      },
      Case {
        name: "source and AWK expression boundaries",
        source: "\\\n{a +\\\nb}\\\n",
        markers: &[(0, 1), (6, 7), (10, 11)],
      },
      Case {
        name: "consecutive continuations separate two names",
        source: "fo\\\n\\\no\n",
        markers: &[(2, 3), (4, 5)],
      },
      Case {
        name: "a continuation separates two numbers",
        source: "1\\\n2\n",
        markers: &[(1, 2)],
      },
      Case {
        name: "a continuation separates a definition name and parenthesis",
        source: "function f\\\n(){}",
        markers: &[(10, 11)],
      },
      Case {
        name: "a continuation follows a parameter separator",
        source: "function f(a,\\\nb){}",
        markers: &[(13, 14)],
      },
      Case {
        name: "a continuation separates complete string constants",
        source: "\"a\"\\\n\"b\"\n",
        markers: &[(3, 4)],
      },
      Case {
        name: "a continuation follows a complete static ERE",
        source: "/a/\\\n&& /b/\n",
        markers: &[(3, 4)],
      },
      Case {
        name: "string and ERE escapes remain distinct from continuations",
        source: "\"\\n\"\\\n&& /\\n/\n",
        markers: &[(4, 5)],
      },
      Case {
        name: "a comment backslash does not become a continuation marker",
        source: "# \\\n/a/\\\n&& /b/\n",
        markers: &[(7, 8)],
      },
    ] {
      let tree = parse(case.source, case.name);
      let mut leaves = Vec::new();
      collect_leaves(tree.root_node(), &mut leaves);
      let actual: Vec<_> = leaves
        .iter()
        .filter(|node| node.kind() == "\\")
        .map(|node| {
          assert!(!node.is_named(), "{}", case.name);
          assert_eq!(node.utf8_text(case.source.as_bytes()).unwrap(), "\\");
          (node.start_byte(), node.end_byte())
        })
        .collect();
      assert_eq!(actual, case.markers, "{}", case.name);
      for (_, end) in case.markers {
        assert_eq!(case.source.as_bytes()[*end], b'\n');
        assert!(
          leaves.iter().all(|leaf| !leaf.byte_range().contains(end)),
          "{}: a continuation newline must have no leaf",
          case.name,
        );
      }
      assert_leaf_highlights(
        case.source,
        &tree,
        case.markers.iter().map(|(start, end)| *start..*end),
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
