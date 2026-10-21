import assert from "node:assert/strict";
import { writeFileSync } from "node:fs";
import { join } from "node:path";
import { after, before, test } from "node:test";
import { createTreeSitter, grammars, root } from "../../scripts/tree-sitter.js";
import {
  clean as assertClean,
  assertContinuationRanges,
  hasRecovery,
  parseCst,
  sourceEndPoint,
} from "./cst.js";

const commandTimeoutMs = 60_000;
const cstOutputLimitBytes = 16 * 1024 * 1024;
const summaryTimeoutMicros = 10_000_000;
const grammar = grammars[0];

let runtime;
let nativeLibrary;
let sequence = 0;

before(() => {
  runtime = createTreeSitter();
  nativeLibrary = join(
    runtime.directory,
    process.platform === "win32" ? "parser.dll" : "parser",
  );
  const result = runtime.run(
    ["build", join(root, grammar.path), "--output", nativeLibrary],
    {
      timeout: commandTimeoutMs,
    },
  );
  assert.ifError(result.error);
  assert.equal(result.status, 0, result.stdout + result.stderr);
});
after(() => runtime?.close());

export function applyEdits(source, edits) {
  let bytes = Buffer.from(source);
  for (const edit of edits) {
    const { byte, deleteBytes, insert } = edit;
    const description = JSON.stringify(edit);
    assert.ok(
      Number.isSafeInteger(byte) && byte >= 0,
      `invalid byte offset: ${description}`,
    );
    assert.ok(
      Number.isSafeInteger(deleteBytes) && deleteBytes >= 0,
      `invalid deletion length: ${description}`,
    );
    assert.equal(typeof insert, "string", `invalid insertion: ${description}`);
    assert.ok(
      byte <= bytes.length && deleteBytes <= bytes.length - byte,
      `edit exceeds ${bytes.length} source bytes: ${description}`,
    );
    bytes = Buffer.concat([
      bytes.subarray(0, byte),
      Buffer.from(insert),
      bytes.subarray(byte + deleteBytes),
    ]);
  }
  return bytes;
}

function writeSource(source) {
  const path = join(runtime.directory, `${sequence++}.awk`);
  writeFileSync(path, source);
  return path;
}

export function parse(source, edits = []) {
  const path = writeSource(source);
  const arguments_ = [
    "parse",
    "--lib-path",
    nativeLibrary,
    "--lang-name",
    grammar.name,
    "--cst",
  ];
  if (edits.length)
    arguments_.push(
      "--edits",
      ...edits.map(
        ({ byte, deleteBytes, insert }) => `${byte} ${deleteBytes} ${insert}`,
      ),
      "--",
    );
  arguments_.push(path);
  const result = runtime.run(arguments_, {
    timeout: commandTimeoutMs,
    maxBuffer: cstOutputLimitBytes,
  });
  assert.ifError(result.error);
  assert.equal(result.signal, null);
  const tree = parseCst(
    result.stdout,
    applyEdits(source, edits),
    result.status,
  );
  if (result.status === 0 && !hasRecovery(tree))
    assertContinuationRanges(tree, applyEdits(source, edits));
  return { ...result, tree, output: result.stdout + result.stderr };
}

function clean(source) {
  const result = parse(source);
  assert.equal(result.status, 0, result.output);
  assertClean(result.tree);
  return result.tree;
}

export function parseSummary(
  source,
  edits = [],
  { timeoutMicros = summaryTimeoutMicros, completionOnly = false } = {},
) {
  const path = writeSource(source);
  const arguments_ = [
    "parse",
    "--lib-path",
    nativeLibrary,
    "--lang-name",
    grammar.name,
    "--quiet",
    "--time",
    "--json-summary",
    ...(completionOnly ? [] : ["--timeout", String(timeoutMicros)]),
  ];
  if (edits.length) {
    arguments_.push(
      "--edits",
      ...edits.map(
        ({ byte, deleteBytes, insert }) => `${byte} ${deleteBytes} ${insert}`,
      ),
      "--",
    );
  }
  arguments_.push(path);
  const result = runtime.run(arguments_, { timeout: commandTimeoutMs });
  assert.ifError(result.error);
  assert.ok(
    result.status === 0 || result.status === 1,
    result.stdout + result.stderr,
  );
  const start = result.stdout.indexOf("{\n");
  assert.notEqual(start, -1, result.stdout + result.stderr);
  const { parse_summaries: summaries } = JSON.parse(result.stdout.slice(start));
  assert.equal(summaries?.length, 1, "parser produced no complete root");
  const summary = summaries[0];
  const final = applyEdits(source, edits);
  assert.equal(
    `${summary.end.row}:${summary.end.column}`,
    sourceEndPoint(final),
    "root must reach the edited source end",
  );
  assert.equal(summary.bytes, final.length, "parser omitted source bytes");
  assert.equal(summary.successful, result.status === 0);
  const freshMs = summary.duration.secs * 1000 + summary.duration.nanos / 1e6;
  let incrementalMs;
  if (edits.length) {
    const match = /Edit: +([0-9.]+) ms/.exec(result.stdout);
    assert.ok(match, "parser produced no edit timing");
    incrementalMs = Number(match[1]);
  }
  assert.ok(Number.isFinite(freshMs) && freshMs >= 0);
  if (edits.length)
    assert.ok(Number.isFinite(incrementalMs) && incrementalMs >= 0);
  return { ...summary, freshMs, incrementalMs };
}

function lines(...sourceLines) {
  return `${sourceLines.join("\n")}\n`;
}

function parseDescription(label, result) {
  return `${label}\nExit status: ${result.status}\n${result.stdout}${result.stderr}`;
}

function assertStatus(label, result, expectedStatus) {
  assert.equal(result.status, expectedStatus, parseDescription(label, result));
}

function assertDeterministicEdit(testName, initialSource, finalSource, edits) {
  assert.deepEqual(
    applyEdits(initialSource, edits),
    Buffer.from(finalSource),
    `${testName}: edits do not produce the final source`,
  );

  const incremental = parse(initialSource, edits);
  const fresh = parse(finalSource);
  for (const [label, result] of Object.entries({
    "native incremental parse": incremental,
    "native fresh parse": fresh,
  })) {
    assertStatus(`${testName} ${label}`, result, 0);
    assertClean(result.tree);
  }
  assert.equal(
    incremental.tree,
    fresh.tree,
    `${testName}: native incremental and fresh CSTs differ`,
  );
  return fresh.tree;
}

function freshTest(name, source, assertions) {
  test(`posix_awk: ${name}`, () => assertions(clean(source)));
}

function determinismTest(name, initial, final, edits, assertions) {
  test(`posix_awk: ${name}`, () => {
    const tree = assertDeterministicEdit(name, initial, final, edits);
    assertions?.(tree);
  });
}

function editHistoryTest(name, source, edits) {
  const reverts = [];
  let current = Buffer.from(source);
  for (const edit of edits) {
    const { byte, deleteBytes, insert } = edit;
    const deleted = current.subarray(byte, byte + deleteBytes).toString("utf8");
    reverts.unshift({
      byte,
      deleteBytes: Buffer.byteLength(insert),
      insert: deleted,
    });
    current = applyEdits(current, [edit]);
  }
  determinismTest(name, source, source, [...edits, ...reverts]);
}

export {
  assertDeterministicEdit,
  assertStatus,
  determinismTest,
  editHistoryTest,
  freshTest,
  lines,
  nativeLibrary,
  parseDescription,
  runtime,
  writeSource,
};
