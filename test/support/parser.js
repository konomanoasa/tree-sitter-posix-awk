import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { after, before, test } from "node:test";
import { createTreeSitter, grammars, root } from "../../scripts/tree-sitter.js";
import { clean, parseCst, sourceEndPoint } from "./cst.js";

const commandTimeoutMs = 60_000;
const cstOutputLimitBytes = 16 * 1024 * 1024;
const summaryTimeoutMicros = 10_000_000;
const grammar = grammars[0];

let runtime;
let nativeLibrary;

let sourceSequence = 0;

before(() => {
  runtime = createTreeSitter();
  nativeLibrary = path.join(
    runtime.directory,
    process.platform === "win32" ? "parser.dll" : "parser",
  );
  const result = runtime.run(
    ["build", "--output", nativeLibrary, path.join(root, grammar.path)],
    {
      stdio: "inherit",
      timeout: commandTimeoutMs,
    },
  );
  assert.ifError(result.error);
  assert.equal(result.status, 0);
});

after(() => {
  runtime?.close();
});

function applyEdits(source, edits) {
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

function formatEdit({ byte, deleteBytes, insert }) {
  return `${byte} ${deleteBytes} ${insert}`;
}

function lines(...sourceLines) {
  return `${sourceLines.join("\n")}\n`;
}

function writeSource(label, source) {
  sourceSequence += 1;
  const filename = `${sourceSequence}-${label}.awk`;
  const sourcePath = path.join(runtime.directory, filename);
  fs.writeFileSync(sourcePath, source);
  return sourcePath;
}

function captureParse(sourcePath, edits = []) {
  const args = [
    "parse",
    "--lib-path",
    nativeLibrary,
    "--lang-name",
    grammar.name,
    "--cst",
  ];
  if (edits.length > 0) {
    args.push("--edits", ...edits.map(formatEdit), "--", sourcePath);
  } else {
    args.push(sourcePath);
  }

  const result = runtime.run(args, {
    maxBuffer: cstOutputLimitBytes,
    timeout: commandTimeoutMs,
  });
  if (result.error !== undefined) {
    throw result.error;
  }
  const source = applyEdits(fs.readFileSync(sourcePath), edits);
  const tree = parseCst(result.stdout, sourcePath, source, result.status);
  return {
    status: result.status,
    stderr: result.stderr,
    stdout: result.stdout,
    tree,
  };
}

function parseDescription(label, result) {
  return `${label}\nExit status: ${result.status}\n${result.stdout}${result.stderr}`;
}

function assertStatus(label, result, expectedStatus) {
  assert.equal(result.status, expectedStatus, parseDescription(label, result));
}

function assertFresh(testName, source, expectedStatus = 0) {
  const sourcePath = writeSource("fresh", source);
  const native = captureParse(sourcePath);
  assertStatus(`${testName} native fresh parse`, native, expectedStatus);
  if (expectedStatus === 0) clean(native.tree);
  return native.tree;
}

function assertDeterministicEdit(testName, initialSource, finalSource, edits) {
  const initialPath = writeSource("initial", initialSource);
  const finalPath = writeSource("final", finalSource);
  assert.deepEqual(
    applyEdits(initialSource, edits),
    Buffer.from(finalSource),
    `${testName}: edits do not produce the final source`,
  );

  const incremental = captureParse(initialPath, edits);
  const fresh = captureParse(finalPath);
  for (const [label, result] of Object.entries({
    "native incremental parse": incremental,
    "native fresh parse": fresh,
  })) {
    assertStatus(`${testName} ${label}`, result, 0);
    clean(result.tree);
  }
  assert.equal(
    incremental.tree,
    fresh.tree,
    `${testName}: native incremental and fresh CSTs differ`,
  );
  return fresh.tree;
}

function freshTest(name, source, assertions) {
  test(`posix_awk: ${name}`, () => assertions(assertFresh(name, source)));
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

function parseSummary(
  source,
  { timeoutMicros = summaryTimeoutMicros, completionOnly = false } = {},
) {
  const sourcePath = writeSource("summary", source);
  const result = runtime.run(
    [
      "parse",
      "--lib-path",
      nativeLibrary,
      "--lang-name",
      grammar.name,
      "--quiet",
      "--json-summary",
      ...(completionOnly ? [] : ["--timeout", String(timeoutMicros)]),
      sourcePath,
    ],
    { timeout: commandTimeoutMs },
  );
  assert.ifError(result.error);
  assert.ok(
    result.status === 0 || result.status === 1,
    result.stdout + result.stderr,
  );
  const start = result.stdout.indexOf("{\n");
  assert.notEqual(start, -1, "parser produced no summary");
  const { parse_summaries: summaries } = JSON.parse(result.stdout.slice(start));
  assert.equal(summaries?.length, 1, "parser produced no complete root");
  const summary = summaries[0];
  assert.equal(
    `${summary.end.row}:${summary.end.column}`,
    sourceEndPoint(source),
    "root must reach the source end",
  );
  assert.equal(
    summary.bytes,
    Buffer.byteLength(source),
    "parser omitted source bytes",
  );
  return summary;
}

export {
  applyEdits,
  assertDeterministicEdit,
  assertStatus,
  captureParse,
  determinismTest,
  editHistoryTest,
  freshTest,
  lines,
  nativeLibrary,
  parseDescription,
  parseSummary,
  runtime,
  writeSource,
};
