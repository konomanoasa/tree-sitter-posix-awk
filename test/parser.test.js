import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import fs, { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path, { join } from "node:path";
import { test } from "node:test";
import { pathToFileURL } from "node:url";
import { grammars } from "../scripts/tree-sitter.js";
import nodeTypes from "../src/node-types.json" with { type: "json" };
import {
  assertStatus,
  captureParse,
  clean,
  contains,
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

(extended_reg_exp
  operator: "|" @extended-reg-exp.operator)

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
    "extended-reg-exp.operator",
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

test("posix_awk: continued tokens expose content fields and no rejection marker", () => {
  assert.equal(
    nodeTypes.some((node) => node.type === "split_token"),
    false,
  );
  assert.deepEqual(
    nodeTypes.find((node) => node.type === "token_content"),
    {
      type: "token_content",
      named: true,
    },
  );
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
    "dup_count",
  ]) {
    const node = nodeTypes.find((node) => node.type === kind);
    assert.deepEqual(
      node.fields.content,
      {
        multiple: true,
        required: false,
        types: [{ type: "token_content", named: true }],
      },
      kind,
    );
  }
});

for (const [name, source] of [
  ["name", lines("{ print va\\", "lue }")],
  ["function name", lines("{ fu\\", "nc(value) }")],
  ["special pattern", lines("BE\\", "GIN {}")],
  ["keyword", lines("fun\\", "ction f() {}")],
  ["built-in name", lines("{ print len\\", "gth(value) }")],
  ["repeated continuations in a name", lines("{ print va\\", "\\", "lue }")],
  ["integer digits", lines("{ print 1\\", "2 }")],
  ["decimal point", lines("{ print 1\\", ".2 }")],
  ["leading decimal point", lines("{ print .\\", "2 }")],
  ["fraction digits", lines("{ print 1.\\", "2 }")],
  ["exponent marker", lines("{ print 1\\", "e2 }")],
  ["exponent sign", lines("{ print 1e\\", "+2 }")],
  ["exponent digits", lines("{ print 1e+\\", "2 }")],
  ["fraction suffix", lines("{ print 1.\\", "f }")],
  ["exponent suffix", lines("{ print 1e2\\", "L }")],
  ["name after an incomplete exponent", lines("{ print 1e\\", "foo }")],
  ["multiple number boundaries", lines("{ print 1\\", ".2e\\", "+\\", "3F }")],
  ["string content", lines('{ print "a\\', 'b" }')],
  ["string opening boundary", lines('{ print "\\', 'a" }')],
  ["string closing boundary", lines('{ print "a\\', '" }')],
  ["repeated continuations in a string", lines('{ print "a\\', "\\", 'b" }')],
  ["ERE content", lines("{ print /a\\", "b/ }")],
  ["ERE opening boundary", lines("{ print /\\", "a/ }")],
  ["ERE closing boundary", lines("{ print /a\\", "/ }")],
  ["ERE compound bracket form", lines("{ print /[[:al\\", "pha:]]/ }")],
  ["ERE bracket range", lines("{ print /[a-\\", "z]/ }")],
  ["ERE escaped delimiter boundary", lines("{ print /a\\\\", "/b/ }")],
  ["division assignment", lines("{ x /\\", "= 1 }")],
  ["addition assignment", lines("{ x +\\", "= 1 }")],
  ["subtraction assignment", lines("{ x -\\", "= 1 }")],
  ["multiplication assignment", lines("{ x *\\", "= 1 }")],
  ["modulus assignment", lines("{ x %\\", "= 1 }")],
  ["power assignment", lines("{ x ^\\", "= 1 }")],
  ["logical or", lines("{ print x |\\", "| y }")],
  ["logical and", lines("{ print x &\\", "& y }")],
  ["non-match", lines("{ print x !\\", "~ y }")],
  ["equality", lines("{ print (x =\\", "= y) }")],
  ["less or equal", lines("{ print (x <\\", "= y) }")],
  ["greater or equal", lines("{ print (x >\\", "= y) }")],
  ["inequality", lines("{ print (x !\\", "= y) }")],
  ["increment", lines("{ x +\\", "+ }")],
  ["decrement", lines("{ x -\\", "- }")],
  ["append", lines("{ print x >\\", "> file }")],
]) {
  freshTest(`${name} continuations preserve normal parsing`, source, (tree) => {
    contains(tree, "line_continuation");
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
  [
    "repeated function-name gaps",
    lines("{ f\\", "\\", "(x) }"),
    "func_name `f`",
  ],
  ["spaced function-name gap", lines("{ f \\", "(x) }"), "name `f`"],
  ["division and operand", lines("{ print x /\\", "y }"), '"/"'],
  ["adjacent strings", lines('{ print "a"\\', '"b" }'), "string_content `b`"],
  ["closed ERE and operator", lines("{ print /a/\\", "+ 1 }"), '"+"'],
]) {
  freshTest(
    `a token-boundary continuation preserves ${name}`,
    source,
    (tree) => {
      contains(tree, "line_continuation");
      contains(tree, expected);
    },
  );
}

freshTest(
  "comment backslashes remain comment text",
  lines("# name\\", "BEGIN { # += \\", "print 1 }"),
  (tree) => {
    assert.equal(tree.includes("line_continuation"), false, tree);
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
    assert.equal(tree.includes("line_continuation"), false, tree);
  },
);

freshTest(
  "continuations between string quotes preserve an empty string",
  lines('BEGIN { print "\\', "\\", '" }'),
  (tree) => {
    contains(tree, "string");
    contains(tree, "line_continuation");
    assert.equal(tree.includes("string_content"), false, tree);
    assert.equal(tree.includes("token_content"), false, tree);
  },
);

freshTest(
  "a blank after a continuation separates two word leaves",
  lines("BEGIN { print va\\", " lue }"),
  (tree) => {
    contains(tree, "name `va`");
    contains(tree, "name `lue`");
    contains(tree, "line_continuation");
    assert.equal(tree.includes("token_content"), false, tree);
  },
);

test("posix_awk: range patterns expose one optional separator and two optional operands", () => {
  const node = nodeTypes.find(
    (node) => node.named && node.type === "normal_pattern",
  );
  assert.ok(node);
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
    name: "a continued comparison still requires parentheses in print arguments",
    source: lines("BEGIN { print x =\\", "= y }"),
  },
  {
    name: "a continued comparison cannot become an output redirection",
    source: lines("BEGIN { print x >\\", "= y }"),
  },
  {
    name: "continuations cannot supply the expression in an empty ERE",
    source: lines("BEGIN { print /\\", "/ }"),
  },
  {
    name: "continuations cannot supply the expression in an empty ERE group",
    source: lines("BEGIN { print /(\\", ")/ }"),
  },
  {
    name: "division without a final operand is rejected",
    source: lines("BEGIN { print x /a/ }"),
  },
  {
    name: "division assignment to an invalid lvalue is rejected",
    source: lines("BEGIN { (a) /= b }"),
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
    name: "a non-newline backslash before a call opening is rejected",
    source: lines("BEGIN {", String.raw`  f\(value)`, "  after", "}"),
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
  {
    name: "a raw hyphen cannot be the sole equivalence class payload",
    source: lines("/[[=-=]]/"),
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
  test(`posix_awk: ${name}`, () => {
    const sourcePath = writeSource(name, "invalid", source);
    const result = captureParse(sourcePath);
    // A parse that recovers with missing nodes alone exits with 0.
    assert.ok(
      result.status === 0 || result.status === 1,
      parseDescription(`${name} fresh parse`, result),
    );
    dirty(result.tree);
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
      "many continuations in a name",
      `BEGIN { print ${"x\\\n".repeat(16_000)}x }\n`,
    ],
    [
      "many continuations in a number",
      `BEGIN { print ${"1\\\n".repeat(16_000)}1 }\n`,
    ],
    [
      "many continuations in string content",
      `BEGIN { print "${"x\\\n".repeat(16_000)}x" }\n`,
    ],
    ["wide statement list", `BEGIN { ${"x++;".repeat(16_000)} }\n`],
    ["deep blocks", `BEGIN ${"{".repeat(2000)}print 1;${"}".repeat(2000)}\n`],
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

test("posix_awk: corpus fuzz propagates CLI failures even when its exit status is zero", () => {
  const directory = mkdtempSync(join(tmpdir(), "tree-sitter-fuzz-exit-#-"));
  const preload = join(directory, "cli.mjs");
  const script = join(import.meta.dirname, "..", "scripts", "tree-sitter.js");
  const fixtures = [
    {
      name: "successful CLI output",
      status: 0,
      stdout: "0 test_language corpus tests failed fuzzing\n",
      stderr: "",
      expectedStatus: 0,
    },
    {
      name: "failed fuzz case with successful CLI exit status",
      status: 0,
      stdout: "1 test_language corpus tests failed fuzzing\n",
      stderr: "",
      expectedStatus: 1,
    },
    {
      name: "failed CLI exit status",
      status: 1,
      stdout: "",
      stderr: "fuzz command failed\n",
      expectedStatus: 1,
    },
  ];
  try {
    for (const fixture of fixtures) {
      writeFileSync(
        preload,
        `
import childProcess from "node:child_process";
import { syncBuiltinESMExports } from "node:module";
const fixture = ${JSON.stringify(fixture)};
childProcess.spawnSync = (_command, arguments_) => {
  if (arguments_.includes("build")) return { status: 0, stdout: "", stderr: "" };
  if (arguments_.includes("fuzz")) return fixture;
  throw new Error("unexpected CLI invocation");
};
syncBuiltinESMExports();
`,
      );
      const result = spawnSync(
        process.execPath,
        ["--import", pathToFileURL(preload).href, script, "fuzz-all"],
        {
          encoding: "utf8",
          timeout: 60_000,
          killSignal: "SIGKILL",
        },
      );
      assert.ifError(result.error);
      assert.equal(
        result.status,
        fixture.expectedStatus,
        `${fixture.name}\n${result.stdout}${result.stderr}`,
      );
      assert.ok(
        result.stdout.includes(fixture.stdout),
        `${fixture.name}: CLI stdout is missing`,
      );
      assert.ok(
        result.stderr.includes(fixture.stderr),
        `${fixture.name}: CLI stderr is missing`,
      );
    }
  } finally {
    rmSync(directory, { recursive: true, force: true });
  }
});
