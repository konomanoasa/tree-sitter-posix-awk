import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { test } from "node:test";
import { grammars } from "../scripts/tree-sitter.js";
import nodeTypes from "../src/node-types.json" with { type: "json" };
import { parseCorpus } from "./support/corpus.js";
import {
  assertContinuationRanges,
  continuationRanges,
  hasRecovery,
  parseCst,
} from "./support/cst.js";
import {
  applyEdits,
  assertStatus,
  captureParse,
  nativeLibrary,
  parseSummary,
  runtime,
  writeSource,
} from "./support/parser.js";

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
  const tree = parseCst(output, sourcePath, cstSource);
  assert.equal(tree, cstOutput);
  assert.deepEqual(continuationRanges(tree), [[0, 6, 0, 7]]);
  assert.equal(hasRecovery(tree), false);
  const comment = "0:0 - 0:13     comment `#Edit: source`";
  assert.equal(
    parseCst(`0:0 - 0:13   program\n${comment}`, sourcePath, "#Edit: source"),
    `0:0 - 0:13   program\n${comment}`,
  );
});

test("posix_awk: CST helpers reject missing, truncated and misplaced roots", () => {
  assert.throws(
    () => parseCst("", "source.awk", ""),
    /parser produced no CST root/,
  );
  assert.throws(
    () => parseCst(cstOutput, "source.awk", `${cstSource}x`),
    /root must reach the edited source end/,
  );
  assert.throws(
    () =>
      parseCst(
        "0:0 - 0:1   wrong_root\n0:0 - 0:1     program",
        "source.awk",
        "x",
      ),
    assert.AssertionError,
  );
  for (const tree of [
    "0:0 - 0:1   •program",
    "0:0 - 0:1   program\n0:1 - 0:1     •name",
  ]) {
    assert.equal(parseCst(tree, "source.awk", "x"), tree);
    assert.equal(hasRecovery(tree), true);
  }
  assert.equal(
    parseCst("0:0 - 0:1   ERROR", "source.awk", "x", 1),
    "0:0 - 0:1   ERROR",
  );
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
  const sourcePath = writeSource("source", fieldContractSource);
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
    parseSummary(`BEGIN { ${"x++;".repeat(timeoutStatementCount)} }\n`, {
      timeoutMicros: immediateParserTimeoutMicros,
    }),
  );
});

test("posix_awk: every byte inside a Unicode payload can be deleted and repaired through an edit history", () => {
  const prefix = 'BEGIN { print "';
  const suffix = '" }\n';
  const payload = "é😀";
  const replacement = "x";
  const source = prefix + payload + suffix;
  const changed = prefix + replacement + suffix;
  const payloadByte = Buffer.byteLength(prefix);
  const payloadLength = Buffer.byteLength(payload);
  const initial = writeSource("initial", source);
  const brokenPayloads = [
    [0xa9, 0xf0, 0x9f, 0x98, 0x80],
    [0xc3, 0xf0, 0x9f, 0x98, 0x80],
    [0xc3, 0xa9, 0x9f, 0x98, 0x80],
    [0xc3, 0xa9, 0xf0, 0x98, 0x80],
    [0xc3, 0xa9, 0xf0, 0x9f, 0x80],
    [0xc3, 0xa9, 0xf0, 0x9f, 0x98],
  ];
  for (const [removedByte, brokenPayload] of brokenPayloads.entries()) {
    const edits = [
      { byte: payloadByte + removedByte, deleteBytes: 1, insert: "" },
      { byte: payloadByte, deleteBytes: payloadLength - 1, insert: payload },
      { byte: payloadByte, deleteBytes: payloadLength, insert: replacement },
      {
        byte: payloadByte,
        deleteBytes: Buffer.byteLength(replacement),
        insert: payload,
      },
    ];
    const expected = [
      Buffer.concat([
        Buffer.from(prefix),
        Buffer.from(brokenPayload),
        Buffer.from(suffix),
      ]),
      Buffer.from(source),
      Buffer.from(changed),
      Buffer.from(source),
    ];
    for (const [step, expectedSource] of expected.entries()) {
      const history = edits.slice(0, step + 1);
      const label = `UTF-8 byte ${removedByte}, edit ${step + 1}`;
      assert.deepEqual(applyEdits(source, history), expectedSource, label);
      const final = writeSource("expected", expectedSource);
      const fresh = captureParse(final);
      const incremental = captureParse(initial, history);
      for (const result of [fresh, incremental]) {
        assert.ok(result.status === 0 || result.status === 1, label);
        if (step === 0) continue;
        assert.equal(result.status, 0, label);
        assert.equal(hasRecovery(result.tree), false, label);
        const end =
          payloadByte + Buffer.byteLength(step === 2 ? replacement : payload);
        assert.match(
          result.tree,
          new RegExp(`^0:${payloadByte - 1} +- +0:${end + 1} +string$`, "m"),
        );
        assert.match(
          result.tree,
          new RegExp(`^0:${payloadByte} +- +0:${end} +string_content `, "m"),
        );
      }
      if (step > 0) assert.equal(incremental.tree, fresh.tree, label);
    }
  }
});

const corpusDirectory = new URL("./corpus/", import.meta.url);

for (const file of fs.readdirSync(corpusDirectory).sort()) {
  if (!file.endsWith(".txt")) continue;
  const source = fs.readFileSync(new URL(file, corpusDirectory), "utf8");
  for (const { name, source: input } of parseCorpus(source)) {
    test(`posix_awk: ${file}: ${name}: fresh CST is complete and repeatable`, () => {
      const sourcePath = writeSource("corpus", input);
      const first = captureParse(sourcePath);
      const repeated = captureParse(sourcePath);
      assert.equal(repeated.status, first.status, name);
      assert.equal(repeated.tree, first.tree, name);
    });
  }
}
