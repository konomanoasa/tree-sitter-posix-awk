import assert from "node:assert/strict";

const continuationMarker = JSON.stringify("\\");

function sourceEndPoint(source) {
  const bytes = Buffer.from(source);
  let row = 0;
  for (const byte of bytes) if (byte === 0x0a) row += 1;
  return `${row}:${bytes.length - bytes.lastIndexOf(0x0a) - 1}`;
}

function normalizeParseTree(stdout, sourcePath) {
  return stdout
    .split("\n")
    .filter((line) => {
      if (line.startsWith(sourcePath)) {
        const suffix = line.slice(sourcePath.length);
        if (/^[ \t]+Parse:[ \t]/.test(suffix)) {
          return false;
        }
      }
      return !/^[ \t]*Edit:[ \t]/.test(line);
    })
    .join("\n");
}

function contains(tree, expected) {
  assert.ok(
    tree.includes(expected),
    `Expected CST to contain: ${expected}\n${tree}`,
  );
}

function excludes(tree, unexpected) {
  assert.ok(
    !tree.includes(unexpected),
    `Expected CST not to contain: ${unexpected}\n${tree}`,
  );
}

const recoveryMarker = /^[0-9: \t-]+•/m;

function clean(tree) {
  assert.doesNotMatch(tree, recoveryMarker, tree);
}

function continuationRanges(tree) {
  const ranges = [];
  for (const line of tree.split("\n")) {
    const match = /^([0-9]+):([0-9]+) +- +([0-9]+):([0-9]+) +(.+)$/.exec(line);
    if (match?.[5] === continuationMarker)
      ranges.push(match.slice(1, 5).map(Number));
  }
  return ranges;
}

function assertContinuationRanges(tree, source) {
  const bytes = Buffer.from(source);
  const lineStarts = [0];
  for (let byte = 0; byte < bytes.length; byte++) {
    if (bytes[byte] === 0x0a) lineStarts.push(byte + 1);
  }
  for (const [startRow, startColumn, endRow, endColumn] of continuationRanges(
    tree,
  )) {
    assert.equal(
      endRow,
      startRow,
      "a continuation marker must exclude its newline",
    );
    assert.equal(
      endColumn,
      startColumn + 1,
      "a continuation marker must span one byte",
    );
    const byte = lineStarts[startRow] + startColumn;
    assert.deepEqual(
      bytes.subarray(byte, byte + 2),
      Buffer.from("\\\n"),
      "a continuation marker must span its backslash before a newline",
    );
    assert.equal(
      lineStarts[startRow + 1],
      byte + 2,
      "a continuation marker must end its physical source line",
    );
  }
}

function dirty(tree) {
  assert.match(tree, recoveryMarker, tree);
}

function matchingLineCount(tree, pattern) {
  return tree.split("\n").filter((line) => pattern.test(line)).length;
}

function hasRecovery(cst) {
  return recoveryMarker.test(cst);
}

function parseCst(stdout, sourcePath, source, status = 0) {
  assert.ok(
    status === 0 || status === 1,
    `parser exited with status ${status}\n${stdout}`,
  );
  const tree = normalizeParseTree(stdout, sourcePath);
  const root = /^[0-9]+:[0-9]+ +- +([0-9]+:[0-9]+) +•?([^ \n]+)/.exec(tree);
  assert.ok(root, "parser produced no CST root");
  assert.equal(
    root[1],
    sourceEndPoint(source),
    "root must reach the edited source end",
  );
  if (status === 0) assert.equal(root[2], "program", tree);
  if (status === 0 && !hasRecovery(tree))
    assertContinuationRanges(tree, source);
  return tree;
}

export {
  assertContinuationRanges,
  clean,
  contains,
  continuationMarker,
  continuationRanges,
  dirty,
  excludes,
  hasRecovery,
  matchingLineCount,
  parseCst,
  sourceEndPoint,
};
