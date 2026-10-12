use std::ops::Range;

use konomanoasa_tree_sitter_posix_awk as grammar;
use tree_sitter::{
  InputEdit, Node, Parser, Point, Query, QueryCursor, StreamingIterator, Tree,
};

struct EreCase {
  name: &'static str,
  source: &'static str,
}

struct LeafCase {
  name: &'static str,
  source: &'static str,
  range: Range<usize>,
}

const LEAF_CASES: &[LeafCase] = &[
  LeafCase {
    name: "a complete name remains one leaf",
    source: "foo\n",
    range: 0..3,
  },
  LeafCase {
    name: "the name before consecutive continuations remains separate",
    source: "fo\\\n\\\no\n",
    range: 0..2,
  },
  LeafCase {
    name: "the name after consecutive continuations remains separate",
    source: "fo\\\n\\\no\n",
    range: 6..7,
  },
  LeafCase {
    name: "a keyword excludes its following continuation",
    source: "BEGIN\\\n{}\n",
    range: 0..5,
  },
  LeafCase {
    name: "a builtin excludes its following continuation",
    source: "{length\\\n(x)}\n",
    range: 1..7,
  },
  LeafCase {
    name: "a function name excludes its adjacent parenthesis",
    source: "{foo(x)}\n",
    range: 1..4,
  },
  LeafCase {
    name: "a continuation before the parenthesis makes a variable name",
    source: "{foo\\\n(x)}\n",
    range: 1..4,
  },
  LeafCase {
    name: "a decimal constant includes its exponent and suffix",
    source: "1.2e+3F\n",
    range: 0..7,
  },
  LeafCase {
    name: "a logical operator excludes its following continuation",
    source: "a||\\\nb\n",
    range: 1..3,
  },
  LeafCase {
    name: "Unicode string content remains one leaf",
    source: "\"é😀\"\n",
    range: 1..7,
  },
  LeafCase {
    name: "an octal escape excludes the following string character",
    source: "\"\\123x\"\n",
    range: 1..5,
  },
  LeafCase {
    name: "an escaped slash excludes the closing delimiter",
    source: "/\\//\n",
    range: 1..3,
  },
  LeafCase {
    name: "an ERE class name excludes compound delimiters",
    source: "/[[:alpha:]]/\n",
    range: 4..9,
  },
  LeafCase {
    name: "an ERE repetition count remains one leaf",
    source: "/a{12}/\n",
    range: 3..5,
  },
];

const ERE_CASES: &[EreCase] = &[
  EreCase {
    name: "an empty ERE retains only its delimiter ranges",
    source: "//",
  },
  EreCase {
    name: "empty groups retain delimiters within nested alternation and repetition",
    source: "/(()|())*?/",
  },
  EreCase {
    name: "ordinary characters and complete escapes retain their source ranges",
    source: r"/a é)}.\n\/\\\141\q(a*?)/",
  },
  EreCase {
    name: "a blank before an escape remains content in every literal position",
    source: r"/ \.[ \]][[. \..]]/",
  },
  EreCase {
    name: "collating forms separate raw fragments from adjacent escapes",
    source: r"/[[.a\nb.][=é\t犬=][.\/\141\q.]]/",
  },
  EreCase {
    name: "raw compound punctuation, spaces and Unicode share one payload leaf",
    source: r"/[[. a-]🙂.]]/",
  },
  EreCase {
    name: "bracket literals distinguish raw brackets and a trailing hyphen",
    source: r"/[]a[\/\n-]/",
  },
  EreCase {
    name: "a leading hyphen is a named endpoint before range and list hyphens",
    source: r"/[--ac-]/",
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

fn assert_changed_scopes(old: &Tree, new: &Tree, context: &str) {
  let scopes = |tree: &Tree| {
    let mut scopes = vec![Vec::new(); tree.root_node().end_byte()];
    let mut pending = vec![tree.root_node()];
    while let Some(node) = pending.pop() {
      for scope in &mut scopes[node.byte_range()] {
        scope.push(node.kind_id());
      }
      for index in (0..node.child_count()).rev() {
        pending.push(node.child(index).unwrap());
      }
    }
    scopes
  };
  let old_scopes = scopes(old);
  let new_scopes = scopes(new);
  let ranges: Vec<_> = old.changed_ranges(new).collect();
  for (byte, scope) in new_scopes.iter().enumerate() {
    if old_scopes.get(byte) != Some(scope) {
      assert!(
        ranges
          .iter()
          .any(|range| range.start_byte <= byte && byte < range.end_byte),
        "{context}: unreported scope change at byte {byte}: {ranges:?}",
      );
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
fn empty_ere_and_groups_own_only_opening_and_closing_delimiters() {
  for (kind, name, source, range) in [
    ("ere", "pattern at EOF", "//", 0..2),
    ("ere", "print expression", "BEGIN { print // }", 14..16),
    ("ere", "match operand", "$0 ~ //", 5..7),
    (
      "ere",
      "builtin argument",
      "BEGIN { sub(//, \"x\") }",
      12..14,
    ),
    ("ere", "getline source", "{ getline < // }", 12..14),
    ("ere", "division operand", "BEGIN { print 1 / // }", 18..20),
    (
      "ere",
      "following division",
      "BEGIN { print // / 2 }",
      14..16,
    ),
    ("ere_expression", "empty group", "/()/", 1..3),
    ("ere_expression", "nested group", "/(())/", 2..4),
    ("ere_expression", "concatenated group", "/a()b/", 2..4),
    ("ere_expression", "left alternation group", "/()|a/", 1..3),
    (
      "ere_expression",
      "right alternation group",
      "/(a|())/",
      4..6,
    ),
    (
      "ere_expression",
      "group before a literal closer",
      "/())/",
      1..3,
    ),
  ] {
    let tree = parse(source, name);
    let owner = tree
      .root_node()
      .descendant_for_byte_range(range.start, range.end)
      .unwrap();
    assert_eq!(owner.kind(), kind, "{name}");
    assert_eq!(owner.byte_range(), range, "{name}");
    assert_eq!(owner.child_count(), 2, "{name}");
    assert_eq!(owner.named_child_count(), 0, "{name}");
    assert!(owner.child_by_field_name("expression").is_none(), "{name}");
    let (opening, closing) = if kind == "ere" {
      ("/", "/")
    } else {
      ("(", ")")
    };
    for (field, token, expected) in [
      ("opening", opening, range.start..range.start + 1),
      ("closing", closing, range.end - 1..range.end),
    ] {
      let delimiter = owner.child_by_field_name(field).unwrap();
      assert_eq!(delimiter.kind(), token, "{name}: {field}");
      assert_eq!(delimiter.byte_range(), expected, "{name}: {field}");
      assert!(!delimiter.is_named(), "{name}: {field}");
    }
  }
}

#[test]
fn ere_fields_select_direct_source_children_in_flat_expressions() {
  let source = r"/(a|bc){2,3}?d{4}e{5,}()[[:alpha:][.ch.][=é=][.a\nb.]]/";
  let tree = parse(source, "shared ERE fields");
  for (kind, text, expected) in [
    (
      "ere_expression",
      "(a|bc){2,3}?",
      vec![("opening", "("), ("expression", "a|bc"), ("closing", ")")],
    ),
    (
      "ere_expression",
      "()",
      vec![("opening", "("), ("closing", ")")],
    ),
    (
      "ere_dupl_symbol",
      "{2,3}?",
      vec![("minimum", "2"), ("maximum", "3"), ("modifier", "?")],
    ),
    ("ere_dupl_symbol", "{4}", vec![("minimum", "4")]),
    ("ere_dupl_symbol", "{5,}", vec![("minimum", "5")]),
    ("character_class", "[:alpha:]", vec![("name", "alpha")]),
    ("collating_symbol", "[.ch.]", vec![("element", "ch")]),
    ("equivalence_class", "[=é=]", vec![("element", "é")]),
    ("collating_symbol", r"[.a\nb.]", vec![("element", r"a\nb")]),
  ] {
    let start = source.find(text).unwrap();
    let node = tree
      .root_node()
      .descendant_for_byte_range(start, start + text.len())
      .unwrap();
    assert_eq!(node.kind(), kind, "{text}");
    let mut cursor = node.walk();
    let actual: Vec<_> = node
      .children(&mut cursor)
      .enumerate()
      .filter_map(|(index, child)| {
        node
          .field_name_for_child(index as u32)
          .map(|field| (field, child.utf8_text(source.as_bytes()).unwrap()))
      })
      .collect();
    assert_eq!(actual, expected, "{text}");
    for (field, value) in expected {
      let child = node.child_by_field_name(field).unwrap();
      assert_eq!(child.parent(), Some(node), "{text}: {field}");
      assert_eq!(
        child.utf8_text(source.as_bytes()).unwrap(),
        value,
        "{text}: {field}"
      );
    }
  }
  let tree = parse("/()*?+?/", "repeated empty group");
  let expression = tree.root_node().descendant_for_byte_range(1, 7).unwrap();
  assert_eq!(expression.kind(), "ere_expression");
  assert!(expression.child_by_field_name("expression").is_none());
  let children: Vec<_> = expression
    .children(&mut expression.walk())
    .map(|node| node.kind())
    .collect();
  assert_eq!(children, ["(", ")", "ere_dupl_symbol", "ere_dupl_symbol"]);
  for symbol in expression.named_children(&mut expression.walk()) {
    let modifier = symbol.child_by_field_name("modifier").unwrap();
    assert_eq!(modifier.kind(), "repetition_modifier");
    assert_eq!(modifier.child_count(), 0);
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
      "ordinary updated fields without getline",
      "{$$x++; $(-x)++; $-x++; ++$!x; $!$a}",
    ),
    (
      "ordinary fields inside operators and arguments",
      "{$a + $b * $!$c; $d ? $e : $f; f($1, ($a,$b) in arr)}",
    ),
    (
      "nested getline targets without a source",
      "{getline $(getline $(-$a)); print $+$a}",
    ),
    (
      "postfix field and getline target aliases",
      "{$-x++; $(-x)++; $$x++; $getline x++; $getline $-x++; $getline $(x)++}\n",
    ),
    ("group alternation", "/(a|b)c/"),
    ("ordinary source escapes", r"/a\n\//"),
    (
      "intervals and compound payloads",
      r"/()[[.a.][.ch.][=é=][.a\nb.][:alpha:]]{2,3}?/",
    ),
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
fn continuation_markers_exclude_newlines_and_receive_leaf_highlights() {
  struct Case {
    name: &'static str,
    source: &'static str,
  }
  assert!(!grammar::NODE_TYPES.contains("\"line_continuation\""));
  let query =
    Query::new(&grammar::LANGUAGE.into(), grammar::HIGHLIGHTS_QUERY).unwrap();
  for case in [
    Case {
      name: "a continuation alone creates no newline token",
      source: "\\\n",
    },
    Case {
      name: "source and AWK expression boundaries",
      source: "\\\n{a +\\\nb}\\\n",
    },
    Case {
      name: "consecutive continuations separate two names",
      source: "fo\\\n\\\no\n",
    },
    Case {
      name: "a continuation separates two numbers",
      source: "1\\\n2\n",
    },
    Case {
      name: "a continuation separates a definition name and parenthesis",
      source: "function f\\\n(){}",
    },
    Case {
      name: "a continuation follows a parameter separator",
      source: "function f(a,\\\nb){}",
    },
    Case {
      name: "a continuation separates complete string constants",
      source: "\"a\"\\\n\"b\"\n",
    },
    Case {
      name: "a continuation follows a complete static ERE",
      source: "/a/\\\n&& /b/\n",
    },
    Case {
      name: "string and ERE escapes remain distinct from continuations",
      source: "\"\\n\"\\\n&& /\\n/\n",
    },
    Case {
      name: "a comment backslash does not become a continuation marker",
      source: "# \\\n/a/\\\n&& /b/\n",
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
    for (_, end) in &actual {
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
      actual.iter().map(|(start, end)| *start..*end),
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
fn editing_a_field_getline_lookahead_during_recovery_restores_its_target() {
  let mut parser = parser();
  let source = "{ getline x $ getline y ++ $ getline z ++ }";
  let mut text = source.as_bytes().to_vec();
  let mut tree = parser.parse(&text, None).unwrap();
  assert!(!tree.root_node().has_error());
  for (start, deleted, inserted) in [
    (0, 1, "( "),
    (25, 3, ""),
    (3, 0, "++ "),
    (26, 1, ","),
    (26, 1, "y"),
    (3, 3, ""),
    (25, 0, "++ "),
    (0, 2, "{"),
  ] {
    edit(&mut tree, &mut text, start, deleted, inserted);
    tree = parser.parse(&text, Some(&tree)).unwrap();
  }
  assert_eq!(text, source.as_bytes());
  let expected = parser.parse(&text, None).unwrap();
  assert_same_cst(
    tree.root_node(),
    expected.root_node(),
    "repair after the token behind a field getline changed",
  );
}

#[test]
fn fixed_seed_getline_edit_histories_match_fresh_parses_after_recovery() {
  let seeds = [
    "{ (getline line) + y++ }",
    "{ (getline line) < y }",
    "{ getline line <= y }",
    "{ f(getline line, ++y) }",
    "{ a[getline line, i++] }",
    "{ getline a[i++] }",
    "{ (getline a) + (getline b) + i++ }",
    "{ print (getline line) > out }",
    "{ $getline < (a < getline x++) < y }",
    "{ $getline < (a < getline x--) < y }",
    "{ $getline < $getline < getline x++++ < y }",
    "{ $getline < $getline < $getline < getline x++++++ < y }",
    "{ getline $getline < (a < getline x++) < y }",
    "{ print ($getline < (a < getline x++) < y) }",
    "{ f($getline < (a < getline x++) < y) }",
    "{ x[$getline < (a < getline x++) < y] }",
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
    "{ getline $getline < x++++ }",
    "{ getline $getline < (x | getline) ++ }",
    "{ getline $getline < (a ? x : (y))++ }",
    "{ getline $getline < getline $getline < x++++ }",
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
      let next = parser.parse(&text, Some(&tree)).unwrap();
      let fresh = parser.parse(&text, None).unwrap();
      let expected = independent.parse(&text, None).unwrap();
      let context = format!(
        "history {history}, step {step}, source {source:?}, edits {steps:?}, final {:?}",
        String::from_utf8_lossy(&text)
      );
      assert_same_cst(fresh.root_node(), expected.root_node(), &context);
      if !fresh.root_node().has_error() {
        assert_same_cst(next.root_node(), fresh.root_node(), &context);
        if !tree.root_node().has_error() {
          assert_changed_scopes(&tree, &next, &context);
        }
      }
      tree = next;
    }
    assert_eq!(
      text,
      source.as_bytes(),
      "history {history} must restore its source"
    );
  }
}

#[test]
fn getline_token_edit_histories_match_fresh_parses() {
  struct History {
    source: &'static str,
    edits: &'static [(usize, usize, &'static str)],
  }
  for History { source, edits } in [
    History {
      source: "{ getline $ getline < \"f\" < x }",
      edits: &[(10, 1, ""), (10, 0, "$"), (30, 0, "a[1]")],
    },
    History {
      source: "{ getline x < y ++ z }",
      edits: &[(10, 1, "getline"), (10, 7, "x")],
    },
    History {
      source: "{ getline < y ++ z }",
      edits: &[(10, 0, "a[1]")],
    },
    History {
      source: "{ print $ getline x ++ $ getline y ++ }",
      edits: &[(33, 2, "?"), (33, 1, "y ")],
    },
    History {
      source: "{ getline x + y getline z ++ w }",
      edits: &[(24, 2, "]"), (24, 1, "z ")],
    },
    History {
      source: "{ $getline < \"f\" ++ | getline }",
      edits: &[(0, 2, "tline"), (0, 5, "{ "), (20, 2, "$")],
    },
    History {
      source: "{ getline a[1/2] ++ $ getline x ++ }",
      edits: &[(18, 2, "a[1]"), (32, 0, ") "), (32, 2, "")],
    },
    History {
      source: "{ getline a[ b[i,\nj] ] ++ y -- z }",
      edits: &[(24, 2, "\"f\""), (29, 2, "]"), (29, 1, "--")],
    },
    History {
      source: "{ getline $1.5 ++ $ getline $1.5 ++ y }",
      edits: &[(16, 2, "\"f\""), (34, 2, "("), (34, 1, "++")],
    },
  ] {
    let mut reused = parser();
    let mut text = source.as_bytes().to_vec();
    let mut tree = parse(source, source);
    for (step, &(start, deleted, inserted)) in edits.iter().enumerate() {
      edit(&mut tree, &mut text, start, deleted, inserted);
      tree = reused.parse(&text, Some(&tree)).unwrap();
      let fresh = parser().parse(&text, None).unwrap();
      if !fresh.root_node().has_error() {
        assert_same_cst(
          tree.root_node(),
          fresh.root_node(),
          &format!("{source}: step {step}"),
        );
      }
    }
    assert!(!tree.root_node().has_error(), "{source}");
  }
}

#[test]
fn changed_ranges_include_getline_ownership_changes() {
  for (name, source, removed, inserted) in [
    (
      "file source moves inward",
      "{ $getline $-getline < 1 ++ }",
      "++",
      "",
    ),
    (
      "file source crosses two targets",
      "{ $getline $!getline $!getline < 1 ++ }",
      "++",
      "",
    ),
    (
      "grouped source",
      "{ $getline $-getline < (1) ++ }",
      "++",
      "",
    ),
    ("call source", "{ $getline $-getline < f(1) ++ }", "++", ""),
    (
      "subscript source",
      "{ $getline $-getline < a[1] ++ }",
      "++",
      "",
    ),
    (
      "source inside call arguments",
      "{ f($getline $-getline < 1 ++, x) }",
      "++",
      "",
    ),
    (
      "source inside a subscript",
      "{ a[$getline $-getline < 1 ++, x] }",
      "++",
      "",
    ),
    (
      "source inside print arguments",
      "{ print ($getline $-getline < 1 ++), x }",
      "++",
      "",
    ),
    (
      "multiline source after Unicode",
      "{ print \"é\"; $getline $-getline < \\\n1 ++; print 2 }\n{ print 3 }",
      "++",
      "",
    ),
    (
      "target and update change owners",
      "{ getline x++ }",
      "++",
      "++ y",
    ),
    (
      "grammar path changes to selected path",
      "{ getline x }",
      "x",
      "x++",
    ),
    (
      "removing a group makes the comparison a file source",
      "{ (getline x) < file }",
      "(getline x)",
      "getline x",
    ),
    (
      "a comparison changes to a file source operator",
      "{ getline x <= file }",
      "<=",
      "<",
    ),
    (
      "merging arguments changes getline ownership",
      "{ f(getline x, y++) }",
      ",",
      "+",
    ),
    (
      "merging subscripts changes getline ownership",
      "{ a[getline x, y++] }",
      ",",
      "+",
    ),
    (
      "an update after a target subscript needs selection",
      "{ getline a[i++] }",
      "]",
      "]++",
    ),
  ] {
    let mut parser = parser();
    let mut text = source.as_bytes().to_vec();
    let mut tree = parse(source, name);
    let start = source.find(removed).unwrap();
    for (deleted, inserted) in [
      (removed.len(), inserted),
      (inserted.len(), removed),
      (removed.len(), inserted),
    ] {
      edit(&mut tree, &mut text, start, deleted, inserted);
      let next = parser.parse(&text, Some(&tree)).unwrap();
      let fresh = parser.parse(&text, None).unwrap();
      assert!(!fresh.root_node().has_error(), "{name}");
      assert_same_cst(next.root_node(), fresh.root_node(), name);
      assert_changed_scopes(&tree, &next, name);
      tree = next;
    }
  }
}

#[test]
fn getline_expression_edit_histories_restore_fresh_csts() {
  let mut sources = vec![
    "{ print $-getline x++ tline y-- z }\n".to_string(),
    "{ x getline y++ | getline }\n".to_string(),
    "{ print $-getline a[1]-- ++y z }\n".to_string(),
    "{ getline $ getline < \"f\" < x a[1]}\n".to_string(),
    "{ getline $getline < \"f\" ++ }\n".to_string(),
    "{ getline $getline < x++ }\n".to_string(),
    "{ getline $getline < x++++ }\n".to_string(),
    "{ getline $getline < (x | getline) ++ }\n".to_string(),
    "{ getline $getline < (a ? x : (y))++ }\n".to_string(),
    "{ getline $getline < a && # source\n x++ }\n".to_string(),
    "{ getline x++ $getline $getline < 1 ++ }\n".to_string(),
    "{ getline $getline < getline $getline < x++++ }\n".to_string(),
    "{ getline $getline < (($getline < 1 in a) 1)++ }\n".to_string(),
    "{ getline $getline < (x < (y < z | getline)) ++ }\n".to_string(),
    "{ $-getline $-getline < a }\n".to_string(),
    "{ $-getline $-getline y < a }\n".to_string(),
    "{ $-getline $-getline < a ++ }\n".to_string(),
    "{ $getline $-getline $-getline < a }\n".to_string(),
    "{ $getline $-getline < 1 ++ }\n".to_string(),
    "{ $getline $!getline < x -- ++ }\n".to_string(),
    "{ $getline $--$getline < 1 ++ }\n".to_string(),
  ];
  for prefix in [
    "",
    "-",
    "+",
    "!",
    "a + ",
    "a - ",
    "a * ",
    "a / ",
    "a % ",
    "a ^ ",
    "a ",
    "q = ",
    "$",
    "$-",
    "$!",
    "++$",
    "--$",
    "getline $-",
    "print $",
    "print $-",
    "printf $-",
  ] {
    for updated in ["x++", "x--", "a[1]++", "$x--"] {
      for tail in ["", " y", " * n + z", " tline y-- z"] {
        sources.push(format!("{{ {prefix}getline {updated}{tail} }}\n"));
      }
    }
  }
  for context in ["", "print ", "printf ", "a + ", "a ", "q = "] {
    for target in ["", " x", " $-x"] {
      for source in ["\"f\"", "-1", "(f)", "f()", "/f/", "++y", "y++", "y + 1"]
      {
        for update in ["++", "--"] {
          for tail in ["", " z", " ++z", " | getline"] {
            sources.push(format!(
              "{{ {context}$getline{target} < {source} {update}{tail} }}\n"
            ));
          }
        }
      }
    }
  }
  let mut state = 0x917a_b065_u64;
  let mut next = || {
    state = state.wrapping_mul(6364136223846793005).wrapping_add(1);
    state as usize
  };
  let mut histories = 0;
  for source in sources {
    let expected = parse(&source, &source);
    for history in 0..50 {
      let mut reused = parser();
      let mut text = source.as_bytes().to_vec();
      let mut tree = reused.parse(&text, None).unwrap();
      let mut reversals = Vec::new();
      for _ in 0..5 {
        let start = next() % (text.len() + 1);
        let removed = usize::from(start < text.len() && next() % 2 == 0);
        let inserted =
          ["", "x", "+", "-", "$", " ", "getline", "("][next() % 8];
        let deleted =
          String::from_utf8(text[start..start + removed].to_vec()).unwrap();
        edit(&mut tree, &mut text, start, removed, inserted);
        tree = reused.parse(&text, Some(&tree)).unwrap();
        reversals.push((start, inserted.len(), deleted));
      }
      for (start, removed, inserted) in reversals.into_iter().rev() {
        edit(&mut tree, &mut text, start, removed, &inserted);
        tree = reused.parse(&text, Some(&tree)).unwrap();
      }
      assert_eq!(text, source.as_bytes());
      assert_same_cst(
        tree.root_node(),
        expected.root_node(),
        &format!("{source}: history {history}"),
      );
      histories += 1;
    }
  }
  eprintln!("checked {histories} getline edit histories");
}
