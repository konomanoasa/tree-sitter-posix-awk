import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { test } from "node:test";
import { grammars } from "../scripts/tree-sitter.js";
import nodeTypes from "../src/node-types.json" with { type: "json" };
import {
  assertStatus,
  captureParse,
  clean,
  contains,
  continuationMarker,
  continuationRanges,
  determinismTest,
  dirty,
  freshTest,
  lines,
  nativeLibrary,
  parseDescription,
  parseSummary,
  runtime,
  writeSource,
} from "./support/parser.js";

const grammar = grammars[0];

const fieldContractSource = `/(a|b)/, /c/ {
  $1 = "value";
  value = -other;
  print -value + other;
  print value + other;
}
`;

const fieldContractQuery = String.raw`(action
  opening: "{" @action.opening
  closing: "}" @action.closing)

(normal_pattern
  separator: "," @normal-pattern.separator)

(ere
  opening: "/" @ere.opening
  closing: "/" @ere.closing)

(ere_expression
  opening: "(" @ere-expression.opening
  closing: ")" @ere-expression.closing)

(lvalue
  operator: "$" @lvalue.operator)

(non_unary_expr
  operator: "=" @non-unary-expr.operator)

(unary_expr
  operator: "-" @unary-expr.operator)

(non_unary_print_expr
  operator: "+" @non-unary-print-expr.operator)

(unary_print_expr
  operator: "-" @unary-print-expr.operator)

(string
  opening: "\"" @string.opening
  closing: "\"" @string.closing)

(terminated_statement
  terminator: ";" @terminated-statement.terminator)
`;

test("posix_awk: anonymous-token field contract", () => {
  const queryPath = path.join(runtime.directory, "fields.scm");
  const sourcePath = writeSource(
    "anonymous-token-field-contract",
    "source",
    fieldContractSource,
  );
  fs.writeFileSync(queryPath, fieldContractQuery);
  const result = runtime.run(
    [
      "query",
      "--lib-path",
      nativeLibrary,
      "--lang-name",
      grammar.name,
      "--captures",
      queryPath,
      sourcePath,
    ],
    { timeout: 60_000 },
  );
  assertStatus("anonymous-token field query", result, 0);
  const captures = [];
  for (const line of result.stdout.split("\n")) {
    const match = line.match(/ - ([^,]+), start:/);
    if (match !== null) {
      captures.push(match[1]);
    }
  }
  captures.sort();
  assert.deepEqual(captures, [
    "action.closing",
    "action.opening",
    "ere-expression.closing",
    "ere-expression.opening",
    "ere.closing",
    "ere.closing",
    "ere.opening",
    "ere.opening",
    "lvalue.operator",
    "non-unary-expr.operator",
    "non-unary-expr.operator",
    "non-unary-print-expr.operator",
    "normal-pattern.separator",
    "string.closing",
    "string.opening",
    "terminated-statement.terminator",
    "terminated-statement.terminator",
    "terminated-statement.terminator",
    "terminated-statement.terminator",
    "unary-expr.operator",
    "unary-print-expr.operator",
  ]);
});

const namedFieldSource = lines(
  "",
  "BEGIN {",
  "  if (cond) left = right; else i++",
  "  do delete arr[i, j]; while (top)",
  '  print a, b > "out"',
  '  "cmd" | getline target',
  "  for (k in rows) break",
  "}",
  "function plus(first) { return first }",
);

freshTest("named node field contract", namedFieldSource, (tree) => {
  for (const memberField of [
    "leading: newline_opt",
    "item: item",
    "terminator: terminator",
    "pattern: pattern",
    "action: action",
    "body: terminated_statement_list",
    "statement: terminatable_statement",
    "condition: expr",
    "consequence: terminated_statement",
    "alternative: terminated_statement",
    "left: lvalue",
    "right: expr",
    "operand: lvalue",
    "operator: incr",
    "body: terminated_statement",
    "array: name",
    "subscripts: expr_list",
    "statement: simple_print_statement",
    "arguments: print_expr_list",
    "redirection: output_redirection",
    "source: non_unary_expr",
    "get: simple_get",
    "target: lvalue",
    "variable: name",
    "name: func_name",
    "parameters: param_list",
    "body: action",
    "body: unterminated_statement_list",
  ]) {
    contains(tree, memberField);
  }
  clean(tree);
});

test("posix_awk: actions, EREs and strings require one opening and closing token", () => {
  for (const [type, opening, closing] of [
    ["action", "{", "}"],
    ["ere", "/", "/"],
    ["string", '"', '"'],
  ]) {
    const node = nodeTypes.find((node) => node.named && node.type === type);
    assert.ok(node, type);
    assert.ok(node.fields, type);
    for (const [field, token] of [
      ["opening", opening],
      ["closing", closing],
    ]) {
      assert.deepEqual(
        node.fields[field],
        {
          multiple: false,
          required: true,
          types: [{ type: token, named: false }],
        },
        `${type}.${field}`,
      );
    }
  }
});

test("posix_awk: lexical tokens expose contiguous leaves", () => {
  for (const removed of ["split_token", "token_content", "line_continuation"]) {
    assert.equal(
      nodeTypes.some((node) => node.type === removed),
      false,
      removed,
    );
  }
  for (const kind of [
    "name",
    "func_name",
    "builtin_func_name",
    "begin_keyword",
    "number",
    "and",
    "or",
    "string_content",
    "escape_sequence",
    "escaped_delimiter",
    "class_name",
    "duplication_count",
    "quoted_character",
    "collating_element_single",
    "collating_element_multi",
    "repetition_modifier",
  ]) {
    const node = nodeTypes.find((node) => node.type === kind);
    assert.ok(node, kind);
    assert.deepEqual(node.fields ?? {}, {}, kind);
    assert.equal(node.children, undefined, kind);
  }
});

test("posix_awk: continuation markers are anonymous leaves", () => {
  const node = nodeTypes.find((node) => node.type === "\\");
  assert.ok(node);
  assert.equal(node.named, false);
  assert.equal(node.children, undefined);
});

freshTest(
  "keyword range excludes blanks and continuation",
  "  END\\\n{}",
  (tree) => {
    clean(tree);
    assert.match(tree, /^0:2 +- +0:5 +end_keyword `END`$/m);
  },
);

freshTest(
  "keyword prefixes remain whole identifiers",
  "{ BEGINNING = END_value + printable + info + if_while }",
  (tree) => {
    clean(tree);
    assert.deepEqual(
      [...tree.matchAll(/name `([^`]*)`/g)].map((match) => match[1]),
      ["BEGINNING", "END_value", "printable", "info", "if_while"],
    );
  },
);

freshTest(
  "a prefix update before a spaced call spelling applies to the name",
  "{ ++follow (x) }",
  (tree) => {
    contains(tree, "name `follow`");
    assert.doesNotMatch(tree, /func_name/);
  },
);

freshTest(
  "for-in permits blanks and continuations at each word boundary",
  lines("{ for (k \\", "in \t\\", "array \\", "); }"),
  (tree) => {
    contains(tree, "variable: name `k`");
    contains(tree, "array: name `array`");
  },
);

freshTest(
  "a long continuation gap preserves a for-in header",
  `{ for (k in array ${"\\\n".repeat(16_384)}); }`,
  (tree) => {
    contains(tree, "variable: name `k`");
    contains(tree, "array: name `array`");
  },
);

freshTest(
  "a builtin call crosses layout without changing its name range",
  lines("{ length \t\\", " \\", "\t(1) }"),
  (tree) => {
    assert.match(tree, /^0:2 +- +0:8 +builtin_func_name `length`$/m);
    contains(tree, "expr_list");
  },
);

for (const [name, source] of [
  ["a raw newline", lines("{ length \\", "", "(1) }")],
  ["a comment", lines("{ length \\", "# note", "(1) }")],
]) {
  freshTest(
    `a bare builtin before ${name} does not become a call`,
    source,
    (tree) => {
      contains(tree, "builtin_func_name `length`");
      assert.doesNotMatch(tree, /expr_list/);
    },
  );
}

for (const { source, spelling, start = 8, end } of [
  { source: "\t1.5", spelling: "1.5", start: 9, end: 12 },
  { source: "123", spelling: "123", end: 11 },
  { source: ".5", spelling: ".5", end: 10 },
  { source: "1.5e+2", spelling: "1.5e+2", end: 14 },
  { source: "1.0f", spelling: "1.0f", end: 12 },
  { source: ".5F", spelling: ".5F", end: 11 },
  { source: "1e2l", spelling: "1e2l", end: 12 },
  { source: "1.L", spelling: "1.L", end: 11 },
  { source: "1.5e+2F", spelling: "1.5e+2F", end: 15 },
  { source: "1.5ff", spelling: "1.5f", end: 12 },
  { source: "1e2LL", spelling: "1e2L", end: 12 },
  { source: "1f", spelling: "1", end: 9 },
  { source: "1e+ x", spelling: "1", end: 9 },
  { source: "1ef", spelling: "1", end: 9 },
  { source: "1.e+f", spelling: "1.", end: 10 },
  { source: ".5eL", spelling: ".5", end: 10 },
]) {
  freshTest(`number boundary in ${source}`, `{ print ${source} }\n`, (tree) => {
    const numbers = [
      ...tree.matchAll(/^0:([0-9]+) +- +0:([0-9]+) +number `([^`]*)`$/gm),
    ];
    assert.deepEqual(
      numbers.map((match) => [Number(match[1]), Number(match[2]), match[3]]),
      [[start, end, spelling]],
    );
  });
}

freshTest(
  "a long number remains one leaf",
  `{ print ${"1".repeat(65_536)} }\n`,
  (tree) => {
    const numbers = [...tree.matchAll(/^0:([0-9]+) +- +0:([0-9]+) +number /gm)];
    assert.deepEqual(
      numbers.map((match) => [Number(match[1]), Number(match[2])]),
      [[8, 65_544]],
    );
  },
);

for (const { source, number, target } of [
  { source: "{ getline $1.5f++ }", number: "1.5f", target: false },
  { source: "{ getline $1f++ }", number: "1", target: true },
  { source: "{ getline $1.5e+2F++ }", number: "1.5e+2F", target: false },
  { source: "{ getline $1e++ }", number: "1", target: true },
  { source: "{ getline $1.0ff++ }", number: "1.0f", target: true },
  { source: "{ getline $1.L++ }", number: "1.L", target: false },
]) {
  freshTest(
    `numeric boundaries in getline lookahead: ${source}`,
    source,
    (tree) => {
      assert.equal(tree.includes("target: lvalue"), target, tree);
      assert.deepEqual(
        [...tree.matchAll(/number `([^`]*)`/g)].map((match) => match[1]),
        [number],
      );
    },
  );
}

freshTest(
  "NUL stays inside string content through the closing quote",
  '{ "a\0b\\n\0c" }',
  (tree) => {
    const content = [
      ...tree.matchAll(
        /^0:([0-9]+) +- +0:([0-9]+) +string_content `([^`]*)`$/gm,
      ),
    ].map((match) => [Number(match[1]), Number(match[2]), match[3]]);
    assert.deepEqual(content, [
      [3, 6, "a\\0b"],
      [8, 10, "\\0c"],
    ]);
  },
);

freshTest(
  "an ERE class name retains trailing digits",
  "/[[:al1:]]/",
  (tree) => {
    const names = [
      ...tree.matchAll(
        /^0:([0-9]+) +- +0:([0-9]+) +name: class_name `([^`]*)`$/gm,
      ),
    ].map((match) => [Number(match[1]), Number(match[2]), match[3]]);
    assert.deepEqual(names, [[4, 7, "al1"]]);
  },
);

for (const { source, spelling, kind, start } of [
  { source: "{ x += y }", spelling: "+=", kind: "add_assign", start: 4 },
  { source: "{ x -= y }", spelling: "-=", kind: "sub_assign", start: 4 },
  { source: "{ x *= y }", spelling: "*=", kind: "mul_assign", start: 4 },
  { source: "{ x /= y }", spelling: "/=", kind: "div_assign", start: 4 },
  { source: "{ x %= y }", spelling: "%=", kind: "mod_assign", start: 4 },
  { source: "{ x ^= y }", spelling: "^=", kind: "pow_assign", start: 4 },
  { source: "{ x || y }", spelling: "||", kind: "or", start: 4 },
  { source: "{ x && y }", spelling: "&&", kind: "and", start: 4 },
  { source: "{ x !~ y }", spelling: "!~", kind: "no_match", start: 4 },
  { source: "{ x == y }", spelling: "==", kind: "eq", start: 4 },
  { source: "{ x <= y }", spelling: "<=", kind: "le", start: 4 },
  { source: "{ x >= y }", spelling: ">=", kind: "ge", start: 4 },
  { source: "{ x != y }", spelling: "!=", kind: "ne", start: 4 },
  { source: "{ print x >> y }", spelling: ">>", kind: "append", start: 10 },
]) {
  test(`posix_awk: ${kind} is contiguous and cannot span a continuation`, () => {
    const result = captureParse(writeSource(kind, "joined", source));
    assertStatus(kind, result, 0);
    clean(result.tree);
    const leaves = [
      ...result.tree.matchAll(
        /^0:([0-9]+) +- +0:([0-9]+) +(?:operator: )?([a-z_]+) `([^`]*)`$/gm,
      ),
    ]
      .filter((match) => match[3] === kind)
      .map((match) => [Number(match[1]), Number(match[2]), match[4]]);
    assert.deepEqual(leaves, [[start, start + 2, spelling]]);
    const split = `${source.slice(0, start + 1)}\\\n${source.slice(start + 1)}`;
    const separated = captureParse(writeSource(kind, "separated", split));
    assert.ok(separated.status === 0 || separated.status === 1);
    dirty(separated.tree);
    assert.equal(separated.tree.includes(`${kind} \`${spelling}\``), false);
  });
}

for (const [name, source, ranges] of [
  ["only a continuation", "\\\n", [[0, 0, 0, 1]]],
  [
    "repeated continuations",
    "\\\n\\\n",
    [
      [0, 0, 0, 1],
      [1, 0, 1, 1],
    ],
  ],
  ["leading continuation", "\\\n{}", [[0, 0, 0, 1]]],
  ["trailing continuation at EOF", "{}\\\n", [[0, 2, 0, 3]]],
  ["expression boundary", "{ x\\\ny }", [[0, 3, 0, 4]]],
  ["parameter boundary", "function f(a,\\\nb) {}", [[0, 13, 0, 14]]],
  ["Unicode before a boundary", '{ "日本語"\\\nx }', [[0, 13, 0, 14]]],
]) {
  freshTest(`${name} exposes only its backslash bytes`, source, (tree) => {
    assert.deepEqual(continuationRanges(tree), ranges, tree);
    assert.doesNotMatch(tree, /[ \t]newline$/m, tree);
    assert.equal(tree.includes("line_continuation"), false, tree);
  });
}

for (const [name, source, expected, absent] of [
  [
    "name",
    lines("{ print va\\", "lue }"),
    ["name `va`", "name `lue`"],
    "name `value`",
  ],
  [
    "function name",
    lines("{ fu\\", "nc(value) }"),
    ["name `fu`", "func_name `nc`"],
    "func_name `func`",
  ],
  [
    "special pattern",
    lines("BE\\", "GIN {}"),
    ["name `BE`", "name `GIN`"],
    "begin_keyword",
  ],
  [
    "keyword",
    lines("fun\\", "ction f() {}"),
    ["name `fun`", "name `ction`"],
    "function_keyword",
  ],
  [
    "built-in name",
    lines("{ print len\\", "gth(value) }"),
    ["name `len`", "func_name `gth`"],
    "builtin_func_name",
  ],
  [
    "repeated gaps",
    lines("{ print va\\", "\\", "lue }"),
    ["name `va`", "name `lue`"],
    "name `value`",
  ],
  [
    "integer digits",
    lines("{ print 1\\", "2*3 }"),
    ["number `1`", "number `2`", "number `3`"],
    "number `12`",
  ],
  [
    "decimal point",
    lines("{ print 1\\", ".2 }"),
    ["number `1`", "number `.2`"],
    "number `1.2`",
  ],
  [
    "fraction digits",
    lines("{ print 1.\\", "2 }"),
    ["number `1.`", "number `2`"],
    "number `1.2`",
  ],
  [
    "exponent marker",
    lines("{ print 1\\", "e2 }"),
    ["number `1`", "name `e2`"],
    "number `1e2`",
  ],
  [
    "exponent sign",
    lines("{ print 1e\\", "+2 }"),
    ["number `1`", "name `e`", "number `2`"],
    "number `1e+2`",
  ],
  [
    "exponent digits",
    lines("{ print 1e+\\", "2 }"),
    ["number `1`", "name `e`", "number `2`"],
    "number `1e+2`",
  ],
  [
    "fraction suffix",
    lines("{ print 1.\\", "f }"),
    ["number `1.`", "name `f`"],
    "number `1.f`",
  ],
  [
    "exponent suffix",
    lines("{ print 1e2\\", "L }"),
    ["number `1e2`", "name `L`"],
    "number `1e2L`",
  ],
  [
    "plus operators",
    lines("{ print x +\\", "+ y }"),
    ['"+"', "name `y`"],
    "incr",
  ],
  [
    "minus operators",
    lines("{ print x -\\", "- y }"),
    ['"-"', "name `y`"],
    "decr",
  ],
]) {
  freshTest(`${name} stay separate across a continuation`, source, (tree) => {
    contains(tree, continuationMarker);
    for (const token of expected) contains(tree, token);
    assert.equal(tree.includes(absent), false, tree);
  });
}

for (const [name, source, expected] of [
  ["integer and name", lines("{ print 1\\", "f }"), "number `1`"],
  ["number and incomplete exponent", lines("{ print 1\\", "e+x }"), "name `e`"],
  ["incomplete exponent and sign", lines("{ print 1e\\", "+x }"), "name `e`"],
  [
    "incomplete exponent sign and name",
    lines("{ print 1e+\\", "x }"),
    "name `x`",
  ],
  ["number suffix and name", lines("{ print 1.0f\\", "oo }"), "number `1.0f`"],
  ["repeated function-name gaps", lines("{ f\\", "\\", "(x) }"), "name `f`"],
  ["spaced function-name gap", lines("{ f \\", "(x) }"), "name `f`"],
  ["division and operand", lines("{ print x /\\", "y }"), '"/"'],
  ["adjacent strings", lines('{ print "a"\\', '"b" }'), "string_content `b`"],
  ["closed ERE and operator", lines("{ print /a/\\", "+ 1 }"), '"+"'],
]) {
  freshTest(
    `a token-boundary continuation preserves ${name}`,
    source,
    (tree) => {
      contains(tree, continuationMarker);
      contains(tree, expected);
    },
  );
}

freshTest(
  "comment backslashes remain comment text",
  lines("# name\\", "BEGIN { # += \\", "print 1 }"),
  (tree) => {
    assert.equal(tree.includes(continuationMarker), false, tree);
  },
);

for (const [name, source] of [
  ["integer", lines(String.raw`{ print 1\2 }`)],
  ["fraction", lines(String.raw`{ print 1.\2 }`)],
  ["exponent", lines(String.raw`{ print 1e\2 }`)],
  ["assignment", lines(String.raw`{ x +\= 1 }`)],
  ["division assignment", lines(String.raw`{ x /\= 1 }`)],
  [
    "continuation followed by a raw backslash",
    lines("{ print 1\\", String.raw`\2 }`),
  ],
]) {
  test(`posix_awk: a raw backslash inside ${name} requires recovery`, () => {
    const result = captureParse(writeSource(name, "raw-backslash", source));
    assertStatus(name, result, 1);
    dirty(result.tree);
  });
}

freshTest(
  "an ERE escape after its opening slash is not a continuation",
  lines(String.raw`{ print /\=/, /\foo/ }`),
  (tree) => {
    contains(tree, "escape_sequence");
    assert.equal(tree.includes(continuationMarker), false, tree);
  },
);

freshTest(
  "a blank after a continuation separates two word leaves",
  lines("BEGIN { print va\\", " lue }"),
  (tree) => {
    contains(tree, "name `va`");
    contains(tree, "name `lue`");
    contains(tree, continuationMarker);
    assert.equal(tree.includes("token_content"), false, tree);
  },
);

test("posix_awk: range patterns expose one optional separator and two optional operands", () => {
  const node = nodeTypes.find(
    (node) => node.named && node.type === "normal_pattern",
  );
  assert.ok(node);
  assert.ok(node.fields);
  assert.deepEqual(Object.keys(node.fields).sort(), [
    "left",
    "right",
    "separator",
  ]);
  for (const field of ["left", "right", "separator"]) {
    assert.deepEqual(
      node.fields[field],
      {
        multiple: false,
        required: false,
        types: [
          {
            type: field === "separator" ? "," : "expr",
            named: field !== "separator",
          },
        ],
      },
      field,
    );
  }
});

for (const [name, source, kind] of [
  [
    "an equality closing sequence stays inside a collating symbol",
    "/[[.=].]]/",
    "collating_symbol",
  ],
  [
    "a dot closing sequence stays inside an equivalence class",
    "/[[=.]=]]/",
    "equivalence_class",
  ],
  [
    "a colon closing sequence stays inside a collating symbol",
    "/[[.:].]]/",
    "collating_symbol",
  ],
]) {
  freshTest(name, source, (tree) => {
    contains(tree, kind);
    contains(tree, "element: collating_element_multi");
  });
}

const membershipPrecedenceCases = [
  ["exponentiation", "", "a in b", "^ c"],
  ["multiplication", "", "a in b", "* c"],
  ["division", "", "a in b", "/ c"],
  ["modulus", "", "a in b", "% c"],
  ["addition", "", "a in b", "+ c"],
  ["subtraction", "", "a in b", "- c"],
  ["concatenation", "", "a in b", "c"],
  ["less than", "", "a in b", "< c"],
  ["less than or equal", "", "a in b", "<= c"],
  ["inequality", "", "a in b", "!= c"],
  ["equality", "", "a in b", "== c"],
  ["greater than", "", "a in b", "> c"],
  ["greater than or equal", "", "a in b", ">= c"],
  ["ERE match", "", "a in b", "~ c"],
  ["ERE non-match", "", "a in b", "!~ c"],
  ["unary exponentiation", "", "-a in b", "^ c"],
  ["multiple-index ERE match", "", "(a, c) in b", "~ d"],
  ["print addition", "print ", "a in b", "+ c"],
  ["unary print exponentiation", "print ", "-a in b", "^ c"],
  ["multiple-index print ERE match", "print ", "(a, c) in b", "~ d"],
  ["continued addition", "", "a in b", "\\\n+ c"],
].map(([name, prefix, left, tail]) => {
  const beginning = `BEGIN { ${prefix}`;
  return {
    name,
    source: lines(`${beginning}${left} ${tail} }`),
    parenthesized: lines(`${beginning}(${left}) ${tail} }`),
    edits: [
      { byte: beginning.length + left.length, deleteBytes: 0, insert: ")" },
      { byte: beginning.length, deleteBytes: 0, insert: "(" },
    ],
  };
});

for (const {
  name,
  source,
  parenthesized,
  edits,
} of membershipPrecedenceCases) {
  determinismTest(
    `parentheses repair membership before ${name}`,
    source,
    parenthesized,
    edits,
  );
}

for (const { name, source } of membershipPrecedenceCases) {
  test(`posix_awk: unparenthesized membership before ${name} is rejected`, () => {
    const result = captureParse(writeSource(name, "invalid", source));
    assert.ok(
      result.status === 0 || result.status === 1,
      parseDescription(name, result),
    );
    dirty(result.tree);
  });
}

const invalidSyntaxCases = [
  {
    name: "a backslash at EOF cannot form a continuation",
    source: "{}\\",
  },
  {
    name: "a blank before the newline cannot complete a continuation",
    source: "{}\\ \n",
  },
  {
    name: "a function definition cannot join two name tokens",
    source: lines("function l\\", "iterals() {}"),
  },
  {
    name: "a leading decimal point cannot join following digits",
    source: lines("{ print .\\", "2 }"),
  },
  ...[
    ["string content", lines('{ print "a\\', 'b" }')],
    ["string opening boundary", lines('{ print "\\', 'a" }')],
    ["string closing boundary", lines('{ print "a\\', '" }')],
    ["empty string", lines('{ print "\\', "\\", '" }')],
    ["string escape", lines('{ print "\\\\', 'n" }')],
    ["string octal escape", lines('{ print "\\1\\', '23" }')],
    ["ERE content", lines("{ print /a\\", "b/ }")],
    ["ERE opening boundary", lines("{ print /\\", "a/ }")],
    ["ERE closing boundary", lines("{ print /a\\", "/ }")],
    ["ERE compound bracket form", lines("{ print /[[:al\\", "pha:]]/ }")],
    ["ERE bracket range", lines("{ print /[a-\\", "z]/ }")],
    ["ERE duplication count", lines("{ print /a{1\\", "2}/ }")],
    ["ERE escaped delimiter", lines("{ print /a\\\\", "/b/ }")],
  ].map(([name, source]) => ({
    name: `${name} rejects an internal physical newline`,
    source,
  })),
  ...[
    ["division assignment", "/", "="],
    ["addition assignment", "+", "="],
    ["subtraction assignment", "-", "="],
    ["multiplication assignment", "*", "="],
    ["modulus assignment", "%", "="],
    ["power assignment", "^", "="],
    ["logical or", "|", "|"],
    ["logical and", "&", "&"],
    ["non-match", "!", "~"],
    ["less or equal", "<", "="],
    ["inequality", "!", "="],
    ["append", ">", ">"],
  ].map(([name, first, second]) => ({
    name: `separated ${name} characters cannot form one operator`,
    source: lines(
      `{ ${name === "append" ? "print " : ""}x ${first}\\`,
      `${second} y }`,
    ),
  })),
  {
    name: "separated equality characters do not form a comparison",
    source: lines("BEGIN { print (x =\\", "= y) }"),
  },
  {
    name: "separated greater-than and equals do not form a comparison",
    source: lines("BEGIN { print (x >\\", "= y) }"),
  },
  {
    name: "an empty ERE cannot span a physical newline",
    source: lines("BEGIN { print /\\", "/ }"),
  },
  {
    name: "an ERE alternation requires a nonempty left branch",
    source: lines("/|a/"),
  },
  {
    name: "an ERE alternation requires a nonempty right branch",
    source: lines("/a|/"),
  },
  {
    name: "an empty ERE group cannot span a physical newline",
    source: lines("BEGIN { print /(\\", ")/ }"),
  },
  {
    name: "an ERE group alternation requires a nonempty left branch",
    source: lines("/(|a)/"),
  },
  {
    name: "an ERE group alternation requires a nonempty right branch",
    source: lines("/(a|)/"),
  },
  {
    name: "an ERE group cannot contain only an alternation operator",
    source: lines("/(|)/"),
  },
  {
    name: "an ERE alternation requires a nonempty middle branch",
    source: lines("/a||b/"),
  },
  {
    name: "an ERE closing parenthesis cannot supply an empty alternation branch",
    source: lines("/(a|)b)/"),
  },
  {
    name: "a continued closing parenthesis cannot supply a nested ERE branch",
    source: lines("/((a|\\", ")b)\\", ")/"),
  },
  {
    name: "division without a final operand is rejected",
    source: lines("BEGIN { print x /a/ }"),
  },
  {
    name: "two slashes after an operand cannot replace division with an empty ERE",
    source: lines("BEGIN { print x // }"),
  },
  {
    name: "division assignment to an invalid lvalue is rejected",
    source: lines("BEGIN { (a) /= b }"),
  },
  {
    name: "a unary operator outside a field cannot form an assignment target",
    source: lines("BEGIN { +$a = b }"),
  },
  {
    name: "a postfix field update cannot form an assignment target",
    source: lines("BEGIN { $a++ = b }"),
  },
  {
    name: "exponentiation outside a field cannot form an assignment target",
    source: lines("BEGIN { $a^b = c }"),
  },
  {
    name: "a second postfix update after a continuation still requires an lvalue",
    source: lines("BEGIN { print $a++\\", "-- }"),
  },
  {
    name: "a field cannot absorb a postfix update on a unary numeric operand",
    source: lines("BEGIN { $-1++ }"),
  },
  {
    name: "a field cannot absorb a postfix update on an exponent operand",
    source: lines("BEGIN { $-x^2++ }"),
  },
  {
    name: "a field cannot absorb a postfix update after a prefix update",
    source: lines("BEGIN { $++x++ }"),
  },
  {
    name: "a field cannot absorb a second postfix update through unary negation",
    source: lines("BEGIN { $!x++-- }"),
  },
  {
    name: "nested fields cannot bypass postfix operand precedence",
    source: lines("BEGIN { $$-x^2++ }"),
  },
  {
    name: "a print field argument preserves postfix operand precedence",
    source: lines("BEGIN { print $-x^2++ }"),
  },
  {
    name: "a printf field argument preserves postfix operand precedence",
    source: lines("BEGIN { printf $++x++ }"),
  },
  {
    name: "a continuation before a postfix token cannot bypass field operand precedence",
    source: lines("BEGIN { $-x^2\\", "++ }"),
  },
  {
    name: "a getline field target cannot bypass postfix operand precedence",
    source: lines("BEGIN { $getline $-x^2++ }"),
  },
  {
    name: "nested getline field targets cannot bypass postfix operand precedence",
    source: lines("BEGIN { $getline $getline $++x++ }"),
  },
  {
    name: "greater-than-or-equal without a right operand is rejected",
    source: lines("BEGIN { print value >= }"),
  },
  {
    name: "END remains reserved in expression context",
    source: lines("BEGIN { value = (END) }"),
  },
  {
    name: "print remains reserved in expression context",
    source: lines("BEGIN { value = print }"),
  },
  {
    name: "function remains reserved outside a definition",
    source: lines("BEGIN { function }"),
  },
  {
    name: "a builtin remains reserved as an assignment target",
    source: lines("BEGIN { length = 1 }"),
  },
  {
    name: "getline remains reserved as an assignment target",
    source: "{ getline = 1 }",
  },
  {
    name: "getline cannot become the name operand of a prefix update",
    source: "{ ++getline }",
  },
  {
    name: "a function name cannot become the name operand of a prefix update",
    source: "{ ++follow(x) }",
  },
  {
    name: "a keyword prefix cannot split the name in a for header",
    source: "{ for (k inarray); }",
  },
  ...[
    ["increment before a number", "++1"],
    ["decrement before a number", "--1"],
    ["increment before a parenthesized expression", "++(x)"],
    ["decrement before a parenthesized expression", "--(x)"],
  ].map(([name, expression]) => ({
    name: `${name} is not split into unary signs`,
    source: `{ ${expression} }`,
  })),
  {
    name: "a non-newline backslash before a call opening is rejected",
    source: lines("BEGIN {", String.raw`  f\(value)`, "  after", "}"),
  },
  {
    name: "a non-newline backslash before a builtin call opening is rejected",
    source: String.raw`{ length \(value) }`,
  },
  {
    name: "an unparenthesized assignment in a conditional alternative is rejected",
    source: lines("BEGIN { x = a ? b : c = d }"),
  },
  {
    name: "hashes inside ERE class names and interval counts are rejected",
    source: lines(
      "BEGIN {",
      "  print /[[:alpha#:]]/",
      "  print /a{2#}/",
      "  print /[[:#:]]/",
      "  print /a{#}/",
      "}",
    ),
  },
  ...[
    ["an ERE interval opening", String.raw`/a{ \1}/`],
    ["an ERE interval count", String.raw`/a{1 \}/`],
    ["an ERE interval separator", String.raw`/a{1, \2}/`],
    ["an ERE interval maximum", "/a{1,2\t\\}/"],
    ["an ERE class opening", String.raw`/[[: \alpha:]]/`],
    ["an ERE class name", String.raw`/[[:alpha \:]]/`],
  ].map(([name, source]) => ({
    name: `a blank after ${name} cannot expose a backslash as a continuation`,
    source: lines(source),
  })),
  {
    name: "a raw hyphen cannot be the sole equivalence class payload",
    source: lines("/[[=-=]]/"),
  },
  {
    name: "an empty collating symbol cannot consume a later terminator",
    source: lines("/[[..].]]/"),
  },
  {
    name: "an empty equivalence class cannot consume a later terminator",
    source: lines("/[[==]=]]/"),
  },
  {
    name: "a continued compound terminator cannot become payload",
    source: lines("/[[..\\", "].]]/"),
  },
  {
    name: "a raw closing bracket cannot be the sole equivalence class payload",
    source: lines("/[[=]=]]/"),
  },
  {
    name: "a missing terminator between actions is rejected",
    source: lines("BEGIN {} END {}"),
  },
  {
    name: "a missing terminator after an actionless pattern is rejected",
    source: lines("active BEGIN {}"),
  },
  {
    name: "a missing special pattern action is rejected",
    source: lines("BEGIN", "END {}"),
  },
  {
    name: "a missing function body is rejected",
    source: lines("function f()", "END {}"),
  },
  {
    name: "a missing function parameter after a comma is rejected",
    source: lines("function malformed(first,) {}"),
  },
  {
    name: "a missing function parameter after a comma and newline is rejected",
    source: lines("function malformed(first,", ") {}"),
  },
  {
    name: "a raw newline before a function parameter comma is rejected",
    source: lines("function malformed(first", ",second) {}"),
  },
  {
    name: "a raw newline before the first function parameter is rejected",
    source: lines("function malformed(", "first) {}"),
  },
  {
    name: "a raw newline after the last function parameter is rejected",
    source: lines("function malformed(first", ") {}"),
  },
  {
    name: "a comment cannot replace a function parameter comma",
    source: lines("function malformed(first # comment \\", "second) {}"),
  },
  {
    name: "a missing parenthesized function header is rejected",
    source: lines("function malformed {}"),
  },
  {
    name: "a missing left range arm is rejected",
    source: lines(", right {}"),
  },
  {
    name: "a missing right range arm is rejected",
    source: lines("left, {}"),
  },
  {
    name: "a missing if body is rejected",
    source: lines("BEGIN { if (condition) }"),
  },
  {
    name: "a missing while body is rejected",
    source: lines("BEGIN { while (condition) }"),
  },
  {
    name: "a missing classic for body is rejected",
    source: lines("BEGIN { for (;;) }"),
  },
  {
    name: "a missing for-in body is rejected",
    source: lines("BEGIN { for (key in values) }"),
  },
  ...[
    ["a raw newline", lines("{ for (k in \\", "", "array); }")],
    ["a comment", lines("{ for (k in array \\", "# note", "); }")],
    ["an incomplete continuation", "{ for (k in array \\); }"],
    ["a reserved array name", "{ for (k in length); }"],
  ].map(([boundary, source]) => ({
    name: `for-in rejects ${boundary}`,
    source,
  })),
  {
    name: "missing nested control bodies are rejected",
    source: lines("BEGIN { if (outer) while (inner) }"),
  },
  {
    name: "a missing do tail is rejected",
    source: lines("BEGIN { do print value; }"),
  },
  {
    name: "a missing ERE closing slash before a raw newline is rejected",
    source: lines("BEGIN { print /broken", "}"),
  },
  {
    name: "a missing ERE closing slash at EOF is rejected",
    source: "BEGIN { print /broken",
  },
  {
    name: "an ERE class opener ending at a raw newline is rejected",
    source: lines("BEGIN { print /[[:", "}"),
  },
  {
    name: "an ERE class name ending at a raw newline is rejected",
    source: lines("BEGIN { print /[[:alpha", "}"),
  },
  {
    name: "an ERE interval opener ending at a raw newline is rejected",
    source: lines("BEGIN { print /a{", "}"),
  },
  {
    name: "an ERE interval count ending at a raw newline is rejected",
    source: lines("BEGIN { print /a{1,", "}"),
  },
  {
    name: "a missing string closing quote before a raw newline is rejected",
    source: lines('BEGIN { print "broken', "}"),
  },
  {
    name: "a missing string closing quote at EOF is rejected",
    source: 'BEGIN { print "broken',
  },
  {
    name: "an unterminated action at EOF is rejected",
    source: "BEGIN { print value",
  },
  {
    name: "an unterminated parenthesized expression at EOF is rejected",
    source: "BEGIN { print (value",
  },
  {
    name: "an unterminated subscript at EOF is rejected",
    source: "BEGIN { print array[index",
  },
];

for (const { name, source } of invalidSyntaxCases) {
  test(`posix_awk: ${name}; fresh recovery repeats across processes`, () => {
    const sourcePath = writeSource(name, "invalid", source);
    const result = captureParse(sourcePath);
    assert.ok(
      result.status === 0 || result.status === 1,
      parseDescription(`${name} fresh parse`, result),
    );
    dirty(result.tree);
    const repeated = captureParse(sourcePath);
    assert.equal(repeated.status, result.status, name);
    assert.equal(repeated.tree, result.tree, name);
  });
}

test("posix_awk: Unicode source retains byte ranges without normalization", () => {
  const result = captureParse(
    writeSource(
      "unicode-source-ranges",
      "source",
      'BEGIN { print "é😀" }\n# e\u0301\n',
    ),
  );
  assertStatus("Unicode source", result, 0);
  clean(result.tree);
  assert.match(result.tree, /^0:15 +- +0:21 +string_content `é😀`$/m);
  assert.match(result.tree, /^1:0 +- +1:5 +comment `# é`$/m);
});

test("posix_awk: large tokens, statement lists and nested blocks parse through EOF", () => {
  for (const [name, source] of [
    ["long string", `BEGIN { print "${"x".repeat(80_000)}" }\n`],
    [
      "many names separated by continuations",
      `BEGIN { print ${"x\\\n".repeat(16_000)}x }\n`,
    ],
    [
      "many numbers separated by continuations",
      `BEGIN { print ${"1\\\n".repeat(16_000)}1 }\n`,
    ],
    [
      "many string escapes",
      `BEGIN { print "${String.raw`\n`.repeat(16_000)}x" }\n`,
    ],
    [
      "a long builtin call continuation gap",
      `{ length ${"\\\n".repeat(32_768)}(1) }`,
    ],
    ["wide statement list", `BEGIN { ${"x++;".repeat(16_000)} }\n`],
    ["deep blocks", `BEGIN ${"{".repeat(2000)}print 1;${"}".repeat(2000)}\n`],
    [
      "getline fields chained by prefix updates",
      `{ ${"getline x ++ $ ".repeat(2000)}getline y }\n`,
    ],
    [
      "a long concatenated getline source without a pipe",
      `{ getline $getline < ${"a ".repeat(32_000)}x }\n`,
    ],
    [
      "a long additive getline source with a pipe",
      `{ getline $getline < ${"a + ".repeat(16_000)}x | getline }\n`,
    ],
  ]) {
    assert.equal(parseSummary(source).successful, true, name);
  }
});

test("posix_awk: long unterminated strings parse through EOF with native recovery", () => {
  const source = `BEGIN { print "${"x".repeat(80_000)}`;
  assert.equal(parseSummary(source).successful, false);
});

test("posix_awk: a parser timeout cannot pass as complete recovery", () => {
  assert.throws(() => parseSummary(`BEGIN { ${"x++;".repeat(16_000)} }\n`, 1));
});

for (const expression of [
  "print getline x++",
  "getline x++++",
  "a + (1,2) in array",
  "$getline $-x^2++",
  'getline < "f" ++',
  '$getline < "f" ++--',
  "$getline < y++++++",
]) {
  test(`posix_awk: reject ${expression}`, () => {
    const result = captureParse(
      writeSource("getline-probe", "invalid", `{ ${expression} }\n`),
    );
    assert.ok(result.status === 0 || result.status === 1);
    dirty(result.tree);
  });
}
