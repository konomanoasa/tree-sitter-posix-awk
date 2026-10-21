import assert from "node:assert/strict";
import { test } from "node:test";
import {
  contains,
  continuationMarker,
  hasRecovery,
  matchingLineCount,
} from "./support/cst.js";
import {
  applyEdits,
  determinismTest,
  editHistoryTest,
  lines,
  parse,
} from "./support/parser.js";

const fuzzSeed = 1n;
const fuzzIterations = 100;
const editsPerHistory = 5;
const maximumFragments = 3;
const maximumDeletionBytes = 2;
const randomStateBits = 64;
const randomOutputShift = 32n;
const randomMultiplier = 6364136223846793005n;
const randomIncrement = 1n;

editHistoryTest(
  "a closed item stays terminated after an edit history that broke its action",
  lines(
    "",
    "BEGIN {",
    "  print > file",
    "  print value >> archive",
    "  print value | command",
    "  printf format > file",
    "  print (left > right)",
    "  print left || right",
    "  print (command | getline input_target)",
    "  print value | getline output_target",
    "  print value > target > suffix",
    "}",
  ),
  [
    { byte: 133, deleteBytes: 3, insert: "" },
    { byte: 18, deleteBytes: 0, insert: "lN}" },
  ],
);

determinismTest(
  "a getline after a reused output pipe remains in the print destination",
  lines("", "BEGIN {", '  print $getline < "f" | "cmd"', "}"),
  lines("", "BEGIN {", '  print $getline < "f" | getline "cmd"', "}"),
  [
    { byte: 0, deleteBytes: 7, insert: "" },
    { byte: 0, deleteBytes: 0, insert: "\nBEGIN " },
    { byte: 34, deleteBytes: 0, insert: "getline " },
  ],
  (tree) => contains(tree, "output_redirection"),
);

editHistoryTest(
  "a builtin call keeps its parenthesis after an edit history that split the name",
  lines(
    "",
    "BEGIN {",
    "  f()",
    "  f(value)",
    "  f (value)",
    "  call\\",
    "(value)",
    "  length",
    "  length()",
    "  length (value)",
    "  atan2(1, 2)",
    "}",
  ),
  [
    { byte: 83, deleteBytes: 3, insert: "" },
    { byte: 44, deleteBytes: 0, insert: "eD]" },
    { byte: 6, deleteBytes: 0, insert: ">9|" },
  ],
);

editHistoryTest(
  "division stays division after an edit history that concatenated the operands",
  lines(
    "",
    "BEGIN {",
    "  a * b + c",
    "  a + b * c / d % e",
    "  -d ^ e + f",
    "  g ^ h ^ i",
    "  j - k - l",
    "  m n + o",
    "  !p && q",
    "}",
  ),
  [
    { byte: 33, deleteBytes: 1, insert: "" },
    { byte: 1, deleteBytes: 0, insert: "a,^" },
  ],
);

editHistoryTest(
  "a getline target survives an edit history with an unterminated string",
  lines(
    "",
    "BEGIN {",
    "  left\\",
    "~ right",
    "  array[position\\",
    "]",
    "  getline target\\",
    "<= limit",
    "  getline target\\",
    "< source",
    "}",
  ),
  [
    { byte: 17, deleteBytes: 3, insert: "" },
    { byte: 34, deleteBytes: 3, insert: "" },
    { byte: 76, deleteBytes: 0, insert: '"' },
    { byte: 54, deleteBytes: 2, insert: "" },
  ],
);

const closedLeadingNewline = "\n \\\n x\n";

const reopenedLeadingNewline = "\n \\\n \n\ny";

determinismTest(
  "reopen leading newline after unrelated edit",
  closedLeadingNewline,
  reopenedLeadingNewline,
  [
    { byte: 7, deleteBytes: 0, insert: "y" },
    { byte: 5, deleteBytes: 1, insert: "\n" },
  ],
  (tree) => {
    assert.equal(
      matchingLineCount(tree, /^[ \t0-9:-]+leading: newline_opt/),
      1,
      "Expected one leading newline_opt",
    );
    contains(tree, continuationMarker);
  },
);

function createEditHistoryGenerator() {
  let seed = fuzzSeed;
  function next(maximum) {
    seed = BigInt.asUintN(
      randomStateBits,
      seed * randomMultiplier + randomIncrement,
    );
    return Number(seed >> randomOutputShift) % maximum;
  }
  return function* (fragments, insertions, joinSource) {
    for (let iteration = 0; iteration < fuzzIterations; iteration++) {
      const parts = [];
      const count = 1 + next(maximumFragments);
      for (let part = 0; part < count; part++) {
        parts.push(fragments[next(fragments.length)]);
      }
      const initial = joinSource(parts);
      let source = Buffer.from(initial);
      const edits = [];
      for (let step = 0; step < editsPerHistory; step++) {
        const position = next(source.length + 1);
        const insert = next(2) === 0 || position === source.length;
        const edit = insert
          ? {
              byte: position,
              deleteBytes: 0,
              insert: insertions[next(insertions.length)],
            }
          : {
              byte: position,
              deleteBytes: Math.min(
                next(maximumDeletionBytes) + 1,
                source.length - position,
              ),
              insert: "",
            };
        edits.push(edit);
        source = applyEdits(source, [edit]);
        yield {
          initial,
          source,
          edits: [...edits],
          context: `seed ${fuzzSeed}, iteration ${iteration}, source ${JSON.stringify(initial)}, edits ${JSON.stringify(edits)}`,
        };
      }
    }
  };
}

const fuzzFragments = [
  "BEGIN { print 1 }",
  "END { print 2 }",
  "{ print $1 }",
  "/a/ { print }",
  "a, b { next }",
  'BEGIN { x = "value" }',
  "BEGIN { x = a + b * c }",
  "BEGIN { x = (a, b) in array }",
  "BEGIN { print /[a-z]/ }",
  "BEGIN { if (x) print x; else print y }",
  "BEGIN { while (x) x-- }",
  "BEGIN { for (i = 0; i < 3; i++) print i }",
  "BEGIN { for (i in a) print a[i] }",
  "BEGIN { do x++; while (x < 3) }",
  "function f(x) { return x }",
  "BEGIN { x = f(1) }",
  "BEGIN { getline x < file }",
  "BEGIN { print x > file }",
  "BEGIN { delete a[1] }",
  "BEGIN { exit 1 }",
  "#comment",
  "BEGIN {}",
  "BEGIN {",
  "BEGIN { if (",
  'BEGIN { print "',
  "BEGIN { print /[",
  "}",
];

const fuzzInsertions = "abcxyz12!{}();,\n\\/*[]().^$|+?-:=# \t'\"<>%&";

test("posix_awk: fixed-seed generated histories converge", (context) => {
  const generateHistories = createEditHistoryGenerator();
  let checked = 0;
  let compared = 0;
  for (const history of generateHistories(
    fuzzFragments,
    fuzzInsertions,
    (parts) => lines(...parts),
  )) {
    const fresh = parse(history.source);
    const repeated = parse(history.source);
    const incremental = parse(history.initial, history.edits);
    for (const result of [fresh, incremental]) {
      assert.ok(result.status === 0 || result.status === 1, history.context);
    }
    assert.equal(repeated.status, fresh.status, history.context);
    assert.equal(repeated.tree, fresh.tree, history.context);
    if (fresh.status === 0 && !hasRecovery(fresh.tree)) {
      assert.equal(incremental.status, 0, history.context);
      assert.equal(hasRecovery(incremental.tree), false, history.context);
      assert.equal(incremental.tree, fresh.tree, history.context);
      compared += 1;
    }
    checked += 1;
  }
  assert.equal(checked, fuzzIterations * editsPerHistory);
  assert.ok(compared > 0);
  context.diagnostic(
    `posix_awk: compared ${checked} repeated fresh CSTs and ${compared} valid incremental CSTs`,
  );
});
