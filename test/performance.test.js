import assert from "node:assert/strict";
import { test } from "node:test";
import { parseSummary } from "./support/parser.js";

const normalParseTimeoutMicros = 1_000_000;
const normalGetlineDepth = 64;
const normalGetlineDepths = [16, 32, normalGetlineDepth];
const largeLiteralCharacters = 80_000;
const largeRepeatedItems = 16_000;
const builtinContinuationCount = 32_768;
const stressBlockDepth = 2_000;
const stressGetlineTargetCount = 2_000;
const stressConcatenatedOperands = 32_000;
const stressAdditiveOperands = 16_000;
const stressExpressionDepth = 16_000;
const stressGetlineCount = 1_600;
const stressIncompleteGetlineCount = 1_200;
const stressIncompleteOperatorCount = 4_000;

test("posix_awk: large tokens and statement lists parse through EOF", () => {
  for (const [name, source] of [
    [
      "long string",
      `BEGIN { print "${"x".repeat(largeLiteralCharacters)}" }\n`,
    ],
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
    [
      "a long builtin call continuation gap",
      `{ length ${"\\\n".repeat(builtinContinuationCount)}(1) }`,
    ],
    ["wide statement list", `BEGIN { ${"x++;".repeat(largeRepeatedItems)} }\n`],
  ]) {
    assert.equal(parseSummary(source).successful, true, name);
  }
});

test("posix_awk: stress: deep blocks and long getline expressions parse through EOF", () => {
  for (const [name, source] of [
    [
      "deep blocks",
      `BEGIN ${"{".repeat(stressBlockDepth)}print 1;${"}".repeat(stressBlockDepth)}\n`,
    ],
    [
      "getline fields chained by prefix updates",
      `{ ${"getline x ++ $ ".repeat(stressGetlineTargetCount)}getline y }\n`,
    ],
    [
      "a long concatenated getline source without a pipe",
      `{ getline $getline < ${"a ".repeat(stressConcatenatedOperands)}x }\n`,
    ],
    [
      "a long additive getline source with a pipe",
      `{ getline $getline < ${"a + ".repeat(stressAdditiveOperands)}x | getline }\n`,
    ],
  ]) {
    assert.equal(
      parseSummary(source, { completionOnly: true }).successful,
      true,
      name,
    );
  }
});

test("posix_awk: long unterminated strings parse through EOF with native recovery", () => {
  const source = `BEGIN { print "${"x".repeat(largeLiteralCharacters)}`;
  assert.equal(parseSummary(source).successful, false);
});

test(`posix_awk: ${normalGetlineDepth} nested getline sequences parse within one second`, () => {
  for (const expression of [
    "$getline $-getline < 1 ++ ",
    "$getline $-getline < 1 ",
  ]) {
    assert.equal(
      parseSummary(`{ ${expression.repeat(normalGetlineDepth)}}\n`, {
        timeoutMicros: normalParseTimeoutMicros,
      }).successful,
      true,
      expression,
    );
  }
});

test("posix_awk: stress: 16000 operators or nested expressions parse through EOF", () => {
  const count = stressExpressionDepth;
  for (const [label, expression] of [
    ["addition", `${"x + ".repeat(count)}x`],
    ["assignment", `${"x = ".repeat(count)}1`],
    ["conditional", `${"a ? b : ".repeat(count)}1`],
    ["parenthesis", `${"(".repeat(count)}x${")".repeat(count)}`],
    ["function call", `${"f(".repeat(count)}x${")".repeat(count)}`],
    ["subscript", `${"a[".repeat(count)}x${"]".repeat(count)}`],
    ["field", `${"$(".repeat(count)}x${")".repeat(count)}`],
    ["getline field", `${"getline $(".repeat(count)}x${")".repeat(count)}`],
    ["field of getline", `${"$(getline ".repeat(count)}x${")".repeat(count)}`],
  ]) {
    assert.equal(
      parseSummary(`{ ${expression} }\n`, { completionOnly: true }).successful,
      true,
      label,
    );
  }
});

test("posix_awk: stress: 1600 nested getline sequences finish without truncation", () => {
  for (const expression of [
    "$getline $-getline < 1 ++ ",
    "$getline $-getline < 1 ",
  ]) {
    assert.equal(
      parseSummary(`{ ${expression.repeat(stressGetlineCount)}}\n`, {
        completionOnly: true,
      }).successful,
      true,
      expression,
    );
  }
});

test("posix_awk: incomplete nested getline choices recover within one second", () => {
  for (const unit of ["getline $getline < ", "getline $getline $getline < "]) {
    for (const count of normalGetlineDepths) {
      for (const ending of ["", "x y = 1", "x in"]) {
        const source = `{ ${unit.repeat(count)}${ending} }\n`;
        assert.equal(
          parseSummary(source, { timeoutMicros: normalParseTimeoutMicros })
            .successful,
          false,
          source,
        );
      }
    }
  }
});

test("posix_awk: stress: repeated incomplete expressions recover through EOF", () => {
  for (const source of [
    `${") getline < ".repeat(stressIncompleteGetlineCount)}\n`,
    `BEGIN { ${"] ^ getline < , 1 ".repeat(stressIncompleteGetlineCount)}}\n`,
    `${"$ , ".repeat(stressIncompleteOperatorCount)}\n`,
    `${"$ ) ".repeat(stressIncompleteOperatorCount)}\n`,
    `${"! , ".repeat(stressIncompleteOperatorCount)}\n`,
    `${"!! , ".repeat(stressIncompleteOperatorCount)}\n`,
    `${"x = , ".repeat(stressIncompleteOperatorCount)}\n`,
  ]) {
    assert.equal(
      parseSummary(source, { completionOnly: true }).successful,
      false,
    );
  }
});
