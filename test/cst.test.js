import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { test } from "node:test";
import { grammars } from "../scripts/tree-sitter.js";
import nodeTypes from "../src/node-types.json" with { type: "json" };
import {
  assertContinuationRanges,
  continuationRanges,
  hasRecovery,
  parseCst,
} from "./support/cst.js";
import {
  assertStatus,
  nativeLibrary,
  parse,
  parseSummary,
  runtime,
  writeSource,
} from "./support/parser.js";
import { scalingWorkloads } from "./support/workloads.js";

const largeRepeatedItems = 16_000;
const commandTimeoutMs = 60_000;
const timeoutStatementCount = 16_000;
const immediateParserTimeoutMicros = 1;

const cstSource = '{ "é"\\\nx }';
const cstOutput = [
  "0:0 - 1:3   program",
  "0:0 - 1:3     item",
  "0:0 - 1:3       action: action",
  '0:0 - 0:1         "{"',
  "0:2 - 1:1         body: unterminated_statement_list",
  "0:2 - 1:1           unterminated_statement",
  "0:2 - 1:1             statement: terminatable_statement",
  "0:2 - 1:1               simple_statement",
  "0:2 - 1:1                 expr",
  "0:2 - 1:1                   non_unary_expr",
  "0:2 - 0:6                     left: non_unary_expr",
  "0:2 - 0:6                       string",
  '0:2 - 0:3                         "\\""',
  "0:3 - 0:5                         string_content `é`",
  '0:5 - 0:6                         "\\""',
  '0:6 - 0:7                     "\\\\"',
  "1:0 - 1:1                     right: non_unary_expr",
  "1:0 - 1:1                       lvalue",
  "1:0 - 1:1                         name `x`",
  '1:2 - 1:3         "}"',
  "",
].join("\n");

test("posix_awk: CST helpers preserve byte ranges and exclude CLI diagnostics", () => {
  const sourcePath = "/source.awk";
  const output = `${cstOutput}Edit: 0 0 \n${sourcePath}  Parse: 1 ms\n`;
  const tree = parseCst(output, cstSource, 0);
  assert.equal(tree, cstOutput.trimEnd());
  assert.deepEqual(continuationRanges(tree), [[0, 6, 0, 7]]);
  assert.equal(hasRecovery(tree), false);
  const comment = "0:0 - 0:13     comment `#Edit: source`";
  assert.equal(
    parseCst(`0:0 - 0:13   program\n${comment}`, "#Edit: source", 0),
    `0:0 - 0:13   program\n${comment}`,
  );
});

test("posix_awk: CST helpers reject missing, truncated and misplaced roots", () => {
  assert.throws(() => parseCst("", "", 0), /parser produced no CST root/);
  assert.throws(
    () => parseCst(cstOutput, `${cstSource}x`, 0),
    /root must reach the edited source end/,
  );
  assert.throws(
    () => parseCst("0:0 - 0:1   wrong_root\n0:0 - 0:1     program", "x", 0),
    assert.AssertionError,
  );
  for (const tree of [
    "0:0 - 0:1   •program",
    "0:0 - 0:1   program\n0:1 - 0:1     •name",
  ]) {
    assert.equal(parseCst(tree, "x", 0), tree);
    assert.equal(hasRecovery(tree), true);
  }
  assert.equal(parseCst("0:0 - 0:1   ERROR", "x", 1), "0:0 - 0:1   ERROR");
});

test("posix_awk: continuation range checks reject newlines and unrelated bytes", () => {
  for (const tree of [
    '0:6 - 1:0   "\\\\"',
    '0:6 - 0:8   "\\\\"',
    '0:5 - 0:6   "\\\\"',
    '1:0 - 1:1   "\\\\"',
  ]) {
    assert.throws(
      () => assertContinuationRanges(tree, cstSource),
      assert.AssertionError,
    );
  }
});

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
  const sourcePath = writeSource(fieldContractSource);
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
    { timeout: commandTimeoutMs },
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

test("posix_awk: a parser timeout cannot pass as complete recovery", () => {
  assert.throws(() =>
    parseSummary(`BEGIN { ${"x++;".repeat(timeoutStatementCount)} }\n`, [], {
      timeoutMicros: immediateParserTimeoutMicros,
    }),
  );
});

function parseCorpus(text) {
  const sections = text.split("==================\n");
  assert.equal(sections.shift(), "", "corpus must start with a case header");
  assert.equal(sections.length % 2, 0, "corpus must pair headers and bodies");
  const cases = [];
  for (let index = 0; index < sections.length; index += 2) {
    const [name, ...attributes] = sections[index].trimEnd().split("\n");
    assert.ok(name, "corpus case must have a name");
    for (const attribute of attributes) {
      assert.ok(attribute === ":cst" || attribute === ":error", attribute);
    }
    const parts = sections[index + 1].split("\n------------------\n");
    assert.equal(
      parts.length,
      2,
      `${name}: corpus must separate source and expectation`,
    );
    const source = parts[0].endsWith("\r") ? parts[0].slice(0, -1) : parts[0];
    cases.push({ name, source });
  }
  return cases;
}

const corpusDirectory = new URL("./corpus/", import.meta.url);

for (const file of fs.readdirSync(corpusDirectory).sort()) {
  if (!file.endsWith(".txt")) continue;
  const source = fs.readFileSync(new URL(file, corpusDirectory), "utf8");
  for (const { name, source: input } of parseCorpus(source)) {
    test(`posix_awk: ${file}: ${name}: fresh CST is complete and repeatable`, () => {
      const first = parse(input);
      const repeated = parse(input);
      assert.equal(repeated.status, first.status, name);
      assert.equal(repeated.tree, first.tree, name);
    });
  }
}

for (const [name, source] of [
  [
    "many names separated by continuations",
    `BEGIN { print ${"x\\\n".repeat(largeRepeatedItems)}x }\n`,
  ],
  [
    "many numbers separated by continuations",
    `BEGIN { print ${"1\\\n".repeat(largeRepeatedItems)}1 }\n`,
  ],
  [
    "many string escapes",
    `BEGIN { print "${String.raw`\n`.repeat(largeRepeatedItems)}x" }\n`,
  ],
]) {
  test(`posix_awk: ${name} parse through EOF`, () => {
    assert.equal(parseSummary(source).successful, true, name);
  });
}

for (const { name, sizes, source } of scalingWorkloads) {
  for (const size of sizes) {
    test(`posix_awk: ${name}/${size} completes through EOF`, () => {
      assert.equal(parseSummary(source(size)).successful, true, name);
    });
  }
}
