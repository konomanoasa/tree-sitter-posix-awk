use std::ops::Range;

use konomanoasa_tree_sitter_posix_awk as grammar;
use tree_sitter::{
  InputEdit, Node, Parser, Point, Query, QueryCursor, StreamingIterator, Tree,
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
    name: "a blank before an escape remains content in every literal position",
    source: r"/ \.[ \]][[. \..]]/",
    leaves: &[
      ("/", 0, 1),
      ("ordinary_character_content", 1, 2),
      ("escape_sequence", 2, 4),
      ("[", 4, 5),
      ("collating_element_content", 5, 6),
      ("escape_sequence", 6, 8),
      ("]", 8, 9),
      ("[", 9, 10),
      ("[", 10, 11),
      (".", 11, 12),
      ("collating_element_content", 12, 13),
      ("escape_sequence", 13, 15),
      (".", 15, 16),
      ("]", 16, 17),
      ("]", 17, 18),
      ("/", 18, 19),
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

fn parser() -> Parser {
  let mut parser = Parser::new();
  parser.set_language(&grammar::LANGUAGE.into()).unwrap();
  parser
}

fn parse(source: &str, name: &str) -> Tree {
  let tree = parser().parse(source, None).unwrap();
  assert!(!tree.root_node().has_error(), "{name}");
  tree
}

fn point(text: &[u8], byte: usize) -> Point {
  let row = text[..byte].iter().filter(|&&b| b == b'\n').count();
  let column = byte
    - text[..byte]
      .iter()
      .rposition(|&b| b == b'\n')
      .map_or(0, |i| i + 1);
  Point::new(row, column)
}

fn edit(
  tree: &mut Tree,
  text: &mut Vec<u8>,
  start: usize,
  deleted: usize,
  inserted: &str,
) {
  let old_end = start + deleted;
  let new_end = start + inserted.len();
  let start_position = point(text, start);
  let old_end_position = point(text, old_end);
  text.splice(start..old_end, inserted.bytes());
  tree.edit(&InputEdit {
    start_byte: start,
    old_end_byte: old_end,
    new_end_byte: new_end,
    start_position,
    old_end_position,
    new_end_position: point(text, new_end),
  });
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

fn assert_same_cst(actual: Node<'_>, expected: Node<'_>, context: &str) {
  let mut pending = vec![(actual, expected)];
  while let Some((actual, expected)) = pending.pop() {
    assert_eq!(actual.kind(), expected.kind(), "{context}");
    assert_eq!(actual.range(), expected.range(), "{context}");
    assert_eq!(actual.is_named(), expected.is_named(), "{context}");
    assert_eq!(actual.is_extra(), expected.is_extra(), "{context}");
    assert_eq!(actual.is_error(), expected.is_error(), "{context}");
    assert_eq!(actual.is_missing(), expected.is_missing(), "{context}");
    assert_eq!(actual.has_error(), expected.has_error(), "{context}");
    assert_eq!(actual.child_count(), expected.child_count(), "{context}");
    for index in 0..actual.child_count() {
      assert_eq!(
        actual.field_name_for_child(index),
        expected.field_name_for_child(index),
        "{context}",
      );
      pending
        .push((actual.child(index).unwrap(), expected.child(index).unwrap()));
    }
  }
}

fn assert_fresh_cst(
  reused: &mut Parser,
  source: &[u8],
  expected: &Tree,
  context: &str,
) {
  for _ in 0..2 {
    let actual = reused.parse(source, None).unwrap();
    assert_same_cst(actual.root_node(), expected.root_node(), context);
  }
  reused.reset();
  let actual = reused.parse(source, None).unwrap();
  assert_same_cst(actual.root_node(), expected.root_node(), context);
  let independent = parser().parse(source, None).unwrap();
  assert_same_cst(independent.root_node(), expected.root_node(), context);
}

#[test]
fn parses_valid_source() {
  let source = "BEGIN { print 1 }\n";
  let language = grammar::LANGUAGE.into();
  let mut parser = Parser::new();
  parser.set_language(&language).unwrap();
  let tree = parser.parse(source, None).unwrap();
  let root = tree.root_node();
  assert_eq!(root.kind(), "program");
  assert_eq!(root.byte_range(), 0..source.len());
  assert!(!root.has_error());
  assert!(grammar::NODE_TYPES.contains("\"program\""));
  Query::new(&language, grammar::HIGHLIGHTS_QUERY).unwrap();
}

#[test]
fn ere_leaves_partition_complete_source_spellings() {
  for case in ERE_CASES {
    let tree = parse(&format!("{}\n", case.source), case.name);
    let ere = tree
      .root_node()
      .descendant_for_byte_range(0, case.source.len())
      .unwrap();
    assert_eq!(ere.kind(), "ere", "{}", case.name);
    assert_leaf_partition(ere, case.leaves, case.name);
  }
}

#[test]
fn fields_belong_to_direct_children() {
  let language: tree_sitter::Language = grammar::LANGUAGE.into();
  let fields: Vec<_> = (1..=language.field_count())
    .map(|id| language.field_name_for_id(id as u16).unwrap())
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
  let query =
    Query::new(&grammar::LANGUAGE.into(), grammar::HIGHLIGHTS_QUERY).unwrap();
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
      .unwrap();
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
  assert!(!grammar::NODE_TYPES.contains("\"line_continuation\""));
  let query =
    Query::new(&grammar::LANGUAGE.into(), grammar::HIGHLIGHTS_QUERY).unwrap();
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
fn batched_edits_before_a_postfix_update_converge_with_a_fresh_parse() {
  for (name, source, update) in [
    ("name", "{ target\\\n++ }\n", "++"),
    ("decrement", "{ target\\\n-- }\n", "--"),
    ("subscript", "{ a[i]\\\n++ }\n", "++"),
    ("field", "{ $1\\\n++ }\n", "++"),
    ("unary operand", "{ -x\\\n++ }\n", "++"),
    ("print argument", "{ print x\\\n++ }\n", "++"),
    ("assignment", "{ y = x\\\n++ }\n", "++"),
    ("repeated continuation", "{ x\\\n\\\n++ }\n", "++"),
    ("concatenation", "{ x\\\n++ z }\n", "++"),
  ] {
    let byte = source.find(update).unwrap();
    let mut parser = parser();
    let mut text = source.as_bytes().to_vec();
    let mut tree = parser.parse(&text, None).unwrap();
    assert!(!tree.root_node().has_error(), "{name}");
    let expected = tree.root_node().to_sexp();
    for (deleted, inserted) in [(update, "x"), ("x", update)] {
      edit(&mut tree, &mut text, byte, deleted.len(), "");
      edit(&mut tree, &mut text, byte, 0, inserted);
      tree = parser.parse(&text, Some(&tree)).unwrap();
    }
    assert_eq!(text, source.as_bytes(), "{name}");
    assert_eq!(tree.root_node().to_sexp(), expected, "{name}");
  }
}

#[test]
fn fresh_parses_ignore_previous_recovery_and_edit_history() {
  let cases = [
    ("unfinished action", "BEGIN {", true),
    ("missing operand", "{ x = }", true),
    ("unfinished condition", "{ if (x", true),
    ("unfinished parameters", "function f(a,", true),
    ("unfinished string", "{ print \"犬", true),
    ("unfinished string escape", "{ print \"a\\", true),
    ("unfinished ERE", "/犬", true),
    ("unfinished ERE escape", "/a\\", true),
    ("unfinished bracket", "/[a", true),
    ("unfinished collating symbol", "/[[.a", true),
    ("unfinished equivalence class", "/[[=a", true),
    ("unfinished character class", "/[[:alpha", true),
    ("unfinished interval", "/a{1,", true),
    ("unfinished group alternation", "/(a|", true),
    ("unfinished negative bracket", "/[^]", true),
    ("unfinished compound closing", "/[[.a.", true),
    ("unfinished class closing", "/[[:alpha:", true),
    ("raw newline in ERE", "/[a\nb]/", true),
    ("raw newline in string", "\"a\nb\"", true),
    ("carriage return before newline", "{ x\r\n}", true),
    ("BOM before unfinished string", "\u{feff}\"a\\", true),
    ("BOM inside expression", "{ x\u{feff}+ }", true),
    ("stray backslash", "{}\\", true),
    ("continued missing operand", "{ x +\\\n", true),
    ("invalid continuation", "{ x\\ \n}", true),
    ("embedded NUL", "{\0}", true),
    (
      "getline ambiguity before error",
      "{ x = getline a + line b+~",
      true,
    ),
    (
      "getline ambiguity after bracket",
      "{ [a ? b : getline c-- ; (y) ? 1 : 2 }",
      true,
    ),
    (
      "complete getline expression",
      "{ x = getline a + line b++ }",
      false,
    ),
    (
      "complete getline conditional",
      "{ a ? b : getline c-- ; (y) ? 1 : 2 }",
      false,
    ),
    ("complete continuation", "{ target\\\n++ }\n", false),
    ("empty source", "", false),
  ];
  let mut reused = parser();
  for (name, source, has_error) in cases {
    let expected = parser().parse(source, None).unwrap();
    assert_eq!(expected.root_node().has_error(), has_error, "{name}");
    for (previous_name, previous_source, _) in cases {
      let context = format!("{name} after {previous_name}");
      let mut text = previous_source.as_bytes().to_vec();
      let mut previous = reused.parse(&text, None).unwrap();
      edit(&mut previous, &mut text, 0, 0, "[\\\n");
      let _edited = reused.parse(&text, Some(&previous)).unwrap();
      assert_fresh_cst(&mut reused, source.as_bytes(), &expected, &context);
    }
  }
}

#[test]
fn fresh_parses_of_truncated_and_mutated_tokens_are_repeatable() {
  let cases = [
    (
      "conditionals, calls and updates",
      "function f(a,b) { if (a) return a[b]++; else return $-f(b) }\n",
    ),
    (
      "loop and output redirection",
      "BEGIN { for (i in a) printf \"%s\", a[i] >> file; do i--; while(i) }\n",
    ),
    (
      "input target and pipe ambiguity",
      "{ x = getline a + b++; command | getline a[i]; getline $-x < file }\n",
    ),
    (
      "division, assignment and ERE boundaries",
      "{ x /= 2; x / 2 ~ /^(ab|c){1,3}?$/; print /[]a-z-]/ }\n",
    ),
    (
      "collating symbol, equivalence class and character class",
      "/[[.é.][=犬=][:alpha:]\\141\\/]/\n",
    ),
    (
      "escaped Unicode string and octal escape",
      "{ print \"é犬🙂\\n\\141\\\"\" }\n",
    ),
    (
      "BOM, continued names and comment backslash",
      "\u{feff}BEGIN\\\n{ fo\\\no; x\\\n++ } # \\\n",
    ),
    (
      "number suffix and exponent boundaries",
      "{ a = 1.2e-3F + .5L; a += 2E+3; a *= 7; a %= 2; a ^= 3 }\n",
    ),
  ];
  let mut reused = parser();
  let mut previous = b"/[[:".to_vec();
  let mut malformed = 0;
  for (name, source) in cases {
    parse(source, name);
    for end in 0..=source.len() {
      let mut mutations = vec![source.as_bytes()[..end].to_vec()];
      if end < source.len() {
        let mut deleted = source.as_bytes().to_vec();
        deleted.remove(end);
        mutations.push(deleted);
        for inserted in [b"\0".as_slice(), b"\\\n", b"\r\n", b"[", b"\""] {
          let mut mutated = source.as_bytes().to_vec();
          mutated.splice(end..end, inserted.iter().copied());
          mutations.push(mutated);
        }
      }
      for (mutation, text) in mutations.into_iter().enumerate() {
        let context =
          format!("{name}, byte {end}, mutation {mutation}: {text:?}");
        let expected = parser().parse(&text, None).unwrap();
        malformed += usize::from(expected.root_node().has_error());
        let _previous = reused.parse(&previous, None).unwrap();
        assert_fresh_cst(&mut reused, &text, &expected, &context);
        previous = text;
      }
    }
  }
  assert!(
    malformed > 1_000,
    "must exercise malformed and incomplete input"
  );
}

#[test]
fn fresh_parses_remain_repeatable_after_long_unterminated_constructs() {
  let mut reused = parser();
  for length in [255, 256, 1_023, 1_024, 1_025] {
    for (name, opening, content, closing) in [
      ("string", "{ print \"", "é", "\" }\n"),
      ("ERE", "/", "a", "/\n"),
      ("collating symbol", "/[[.", "a", ".]]/\n"),
      ("equivalence class", "/[[=", "a", "=]]/\n"),
      ("word", "{ ", "a", " }\n"),
      ("comment", "{ #", "a", "\n}\n"),
      ("nested unary fields", "{ ", "$+", "a }\n"),
    ] {
      let unfinished = format!("{opening}{}", content.repeat(length));
      let complete = format!("{unfinished}{closing}");
      for source in [unfinished, complete] {
        let context =
          format!("{name}, length {length}, {} bytes", source.len());
        let expected = parser().parse(&source, None).unwrap();
        assert_fresh_cst(&mut reused, source.as_bytes(), &expected, &context);
      }
    }
  }
}

#[test]
fn restoring_batched_edits_preserves_a_terminated_division_item() {
  let source = "\n# slash\nBEGIN {\n  x / 2\n}\n";
  let mut text = source.as_bytes().to_vec();
  let mut parser = parser();
  let mut tree = parser.parse(&text, None).unwrap();
  assert!(!tree.root_node().has_error());
  let mut reversals = Vec::new();
  for (start, deleted, inserted) in [
    (16, 0, "?"),
    (14, 0, "x"),
    (23, 2, " "),
    (15, 0, "--"),
    (11, 1, "("),
    (10, 2, ";"),
    (26, 3, ")"),
    (13, 2, "{"),
  ] {
    reversals.push((
      start,
      inserted.len(),
      String::from_utf8(text[start..start + deleted].to_vec()).unwrap(),
    ));
    edit(&mut tree, &mut text, start, deleted, inserted);
  }
  tree = parser.parse(&text, Some(&tree)).unwrap();
  for (start, deleted, inserted) in reversals.into_iter().rev() {
    edit(&mut tree, &mut text, start, deleted, &inserted);
  }
  assert_eq!(text, source.as_bytes());
  let actual = parser.parse(&text, Some(&tree)).unwrap();
  let expected = parser.parse(&text, None).unwrap();
  assert!(!expected.root_node().has_error());
  assert_same_cst(
    actual.root_node(),
    expected.root_node(),
    "division item after batched edits and restoration",
  );
}

#[test]
fn repairing_a_reparsed_getline_preserves_target_ownership() {
  let mut parser = parser();
  let mut text = b"{ x = getline a + line b+~".to_vec();
  let mut tree = parser.parse(&text, None).unwrap();
  tree = parser.parse(&text, Some(&tree)).unwrap();
  let byte = text.iter().position(|&byte| byte == b'~').unwrap();
  edit(&mut tree, &mut text, byte, 1, "+ }");
  let actual = parser.parse(&text, Some(&tree)).unwrap();
  let expected = parser.parse(&text, None).unwrap();
  assert!(!expected.root_node().has_error());
  assert_same_cst(
    actual.root_node(),
    expected.root_node(),
    "repair after a reparse without edits",
  );
}

#[test]
fn removing_an_invalid_bracket_before_getline_restores_statements() {
  let mut parser = parser();
  let mut text = b"{ a ? b : getline c-- ; (y) ? 1 : 2 }".to_vec();
  let mut tree = parser.parse(&text, None).unwrap();
  assert!(!tree.root_node().has_error());
  edit(&mut tree, &mut text, 2, 0, "[");
  tree = parser.parse(&text, Some(&tree)).unwrap();
  edit(&mut tree, &mut text, 2, 1, "");
  let actual = parser.parse(&text, Some(&tree)).unwrap();
  let expected = parser.parse(&text, None).unwrap();
  assert!(!expected.root_node().has_error());
  assert_same_cst(
    actual.root_node(),
    expected.root_node(),
    "repair before a conditional getline",
  );
}

#[test]
fn fixed_seed_getline_edit_histories_match_fresh_parses_after_recovery() {
  let seeds = [
    "{ x = getline a + line b++ }",
    "{ a ? b : getline c-- ; (y) ? 1 : 2 }",
    "{ getline x++ }",
    "{ getline a[i]-- }",
    "{ getline x++ y }",
    "{ source | getline x-- }",
    "{ -source | getline x-- }",
    "{ $getline x++ }",
    "{ getline $getline x < source }",
    "{ print (getline x++ + y) }",
    "{ getline x\\\n++ }",
  ];
  let mut random = 0x79d07b23u32;
  let mut next = || {
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    random as usize
  };
  let insertions = [
    "", "[", "]", "(", ")", "++", "--", "x", " ", ";", "\n", "+", "?", "~",
    "<", "\\\n",
  ];
  let mut independent = parser();
  for history in 0..5_000 {
    let source = seeds[history % seeds.len()];
    let mut parser = parser();
    let mut text = source.as_bytes().to_vec();
    let mut tree = parser.parse(&text, None).unwrap();
    assert!(!tree.root_node().has_error(), "{source}");
    let mut reversals = Vec::new();
    let mut steps = Vec::new();
    for step in 0..10 {
      let (start, deleted, inserted) = if step < 5 {
        let start = next() % (text.len() + 1);
        let deleted = (next() % 4).min(text.len() - start);
        let inserted = insertions[next() % insertions.len()].to_string();
        reversals.push((
          start,
          inserted.len(),
          String::from_utf8(text[start..start + deleted].to_vec()).unwrap(),
        ));
        (start, deleted, inserted)
      } else {
        reversals.pop().unwrap()
      };
      steps.push((start, deleted, inserted.clone()));
      edit(&mut tree, &mut text, start, deleted, &inserted);
      tree = parser.parse(&text, Some(&tree)).unwrap();
      let fresh = parser.parse(&text, None).unwrap();
      let expected = independent.parse(&text, None).unwrap();
      let context = format!(
        "history {history}, step {step}, source {source:?}, edits {steps:?}, final {:?}",
        String::from_utf8_lossy(&text)
      );
      assert_same_cst(fresh.root_node(), expected.root_node(), &context);
      if !fresh.root_node().has_error() {
        assert_same_cst(tree.root_node(), fresh.root_node(), &context);
      }
    }
    assert_eq!(
      text,
      source.as_bytes(),
      "history {history} must restore its source"
    );
  }
}
