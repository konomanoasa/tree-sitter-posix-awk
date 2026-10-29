import assert from "node:assert/strict";
import { test } from "node:test";
import {
  clean,
  contains,
  continuationMarker,
  continuationRanges,
  hasRecovery,
  matchingLineCount,
} from "./support/cst.js";
import {
  applyEdits,
  assertStatus,
  determinismTest,
  editHistoryTest,
  lines,
  parse,
} from "./support/parser.js";

import { getlineSelectionWorkloads } from "./support/workloads.js";

const fuzzSeed = 1n;
const fuzzIterations = 100;
const editsPerHistory = 5;
const maximumFragments = 3;
const maximumDeletionBytes = 2;
const randomStateBits = 64;
const randomOutputShift = 32n;
const randomMultiplier = 6364136223846793005n;
const randomIncrement = 1n;

for (const { name, before, after, byte, removed, inserted, status } of [
  {
    name: "lowering the upper bound invalidates an interval",
    before: "/a{1,2}/",
    after: "/a{1,0}/",
    byte: 5,
    removed: 1,
    inserted: "0",
    status: 1,
  },
  {
    name: "raising the upper bound repairs an interval",
    before: "/a{2,1}/",
    after: "/a{2,3}/",
    byte: 5,
    removed: 1,
    inserted: "3",
    status: 0,
  },
  {
    name: "removing a maximum repairs reversed bounds",
    before: "/a{2,1}/",
    after: "/a{2,}/",
    byte: 5,
    removed: 1,
    inserted: "",
    status: 0,
  },
  {
    name: "leading zeroes preserve an upper bound",
    before: "/a{1,2}/",
    after: "/a{1,002}/",
    byte: 5,
    removed: 0,
    inserted: "00",
    status: 0,
  },
  {
    name: "changing the lower bound rechecks a reused maximum",
    before: "/a{1,2}/",
    after: "/a{3,2}/",
    byte: 3,
    removed: 1,
    inserted: "3",
    status: 1,
  },
  {
    name: "raising a count above 255 invalidates the interval",
    before: "/a{255}/",
    after: "/a{256}/",
    byte: 5,
    removed: 1,
    inserted: "6",
    status: 1,
  },
  {
    name: "lowering a count to 255 repairs the interval",
    before: "/a{256}/",
    after: "/a{255}/",
    byte: 5,
    removed: 1,
    inserted: "5",
    status: 0,
  },
  {
    name: "replacing a letter with a digit turns a brace into an interval",
    before: "/a{x}/",
    after: "/a{2}/",
    byte: 3,
    removed: 1,
    inserted: "2",
    status: 0,
  },
  {
    name: "removing an interval minimum makes its brace literal",
    before: "/a{1,2}/",
    after: "/a{,2}/",
    byte: 3,
    removed: 1,
    inserted: "",
    status: 0,
  },
  {
    name: "adding blank after a brace makes the former count literal",
    before: "/a{2}/",
    after: "/a{ 2}/",
    byte: 3,
    removed: 0,
    inserted: " ",
    status: 0,
  },
  {
    name: "an invalid interval becomes literal after removing its first digit",
    before: "/(a{2x})/",
    after: "/(a{x})/",
    byte: 4,
    removed: 1,
    inserted: "",
    status: 0,
  },
]) {
  determinismTest(
    name,
    before,
    after,
    [{ byte, deleteBytes: removed, insert: inserted }],
    undefined,
    status,
  );
}

editHistoryTest(
  "reversed interval bounds disappear after undoing a lower-bound edit",
  "/a{0001,02}/",
  [{ byte: 6, deleteBytes: 1, insert: "3" }],
);

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

const nestedGetlineRepairCount = 16;
const precedingGetlineCount = 510;

for (const { name, source } of getlineSelectionWorkloads) {
  const byte = source.lastIndexOf("x");
  determinismTest(
    `${name} preserves selection after editing its final operand`,
    source,
    `${source.slice(0, byte)}value${source.slice(byte + 1)}`,
    [{ byte, deleteBytes: 1, insert: "value" }],
  );
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

for (const { name, ending, replacement } of [
  { name: "missing source", ending: "", replacement: "value" },
  { name: "invalid assignment", ending: "x y = 1", replacement: "x + 1" },
  { name: "missing array name", ending: "x in", replacement: "x in table" },
]) {
  const prefix = `{ ${"getline $getline $getline < ".repeat(nestedGetlineRepairCount)}`;
  const before = `${prefix}${ending} }`;
  const after = `${prefix}${replacement} }`;
  const repair = {
    byte: prefix.length,
    deleteBytes: ending.length,
    insert: replacement,
  };
  determinismTest(`repair nested getline ${name}`, before, after, [repair]);
  determinismTest(`undo and redo nested getline ${name}`, after, after, [
    {
      byte: prefix.length,
      deleteBytes: replacement.length,
      insert: ending,
    },
    repair,
  ]);
}

for (const { name, before, after, beforeStatus, afterStatus } of [
  {
    name: "parent file source survives grouping its nested source",
    before: "{ getline $getline < z + $y-- < 1 < 2 }",
    after: "{ getline $getline < (z) + $y-- < 1 < 2 }",
  },
  {
    name: "parent file source survives a nested call becoming a subscript",
    before: "{ getline $getline < f(z) + $y-- < 1 < 2 }",
    after: "{ getline $getline < a[z] + $y-- < 1 < 2 }",
  },
  {
    name: "adding an outer field cannot rescue a committed target update",
    beforeStatus: 1,
    afterStatus: 1,
    before: "{ getline < getline x++ }",
    after: "{ $getline < getline x++ }",
  },
  {
    name: "nested fields cannot take updates following a committed target",
    beforeStatus: 1,
    afterStatus: 1,
    before: "{ $getline < getline x++ }",
    after: "{ $getline < $getline < getline x++++ }",
  },
  {
    name: "grouping an invalid target update keeps the syntax error",
    beforeStatus: 1,
    afterStatus: 1,
    before: "{ $getline < getline x++ }",
    after: "{ $getline < (getline x++)++ }",
  },
  {
    name: "a target update inside a call is rejected after editing",
    afterStatus: 1,
    before: "{ $getline < f()++ }",
    after: "{ $getline < f(getline x++)++ }",
  },
  {
    name: "source moves across multiple nested targets",
    before: "{ $getline $!getline $!getline < 1 }",
    after: "{ $getline $!getline $!getline < 1 ++ }",
  },
]) {
  for (const { initial, final, status } of [
    { initial: before, final: after, status: afterStatus ?? 0 },
    { initial: after, final: before, status: beforeStatus ?? 0 },
  ]) {
    let start = 0;
    while (initial[start] === final[start]) start++;
    let suffix = 0;
    while (
      suffix < Math.min(initial.length, final.length) - start &&
      initial.at(-suffix - 1) === final.at(-suffix - 1)
    )
      suffix++;
    determinismTest(
      `${name}: ${initial} to ${final}`,
      initial,
      final,
      [
        {
          byte: start,
          deleteBytes: initial.length - start - suffix,
          insert: final.slice(start, final.length - suffix),
        },
      ],
      undefined,
      status,
    );
  }
}

{
  const prefix = `{ ${"$getline < 1 ".repeat(precedingGetlineCount)}`;
  determinismTest(
    "editing a late getline invalidates the first lookahead",
    `${prefix}getline x++ }`,
    `${prefix}getline x++ y }`,
    [{ byte: prefix.length + 11, deleteBytes: 0, insert: " y" }],
  );
}

for (const { label, initial, final, removed, inserted } of [
  {
    label: "an else without a consequence",
    initial: "{ if (a) else }",
    final: "{ if (a) ; }",
    removed: "else",
    inserted: ";",
  },
  {
    label: "an incomplete for header",
    initial: "{ for (); }",
    final: "{ for (;;); }",
    removed: ")",
    inserted: ";;)",
  },
  {
    label: "a do without a body or tail",
    initial: "{ do }",
    final: "{ do ; while (a) }",
    removed: "do",
    inserted: "do ; while (a)",
  },
  {
    label: "a delete without an array name",
    initial: "{ if(a) delete [] }",
    final: "{ if(a) delete a[0] }",
    removed: "[]",
    inserted: "a[0]",
  },
  {
    label: "a call with missing arguments",
    initial: "{ if(a) f(,) }",
    final: "{ if(a) f(x) }",
    removed: ",",
    inserted: "x",
  },
  {
    label: "a membership operator without operands",
    initial: "{ if(a) in }",
    final: "{ if(a) x in a }",
    removed: "in",
    inserted: "x in a",
  },
]) {
  const prefix = lines("before {}");
  const following = "END { print after }";
  determinismTest(
    `repairing ${label} matches a fresh parse`,
    prefix + lines(initial, following),
    prefix + lines(final, following),
    [
      {
        byte: prefix.length + initial.indexOf(removed),
        deleteBytes: removed.length,
        insert: inserted,
      },
    ],
  );
}

for (const { name, initial, final, byte, insert } of [
  {
    name: "a field update moves a source out of a unary operand",
    initial: "{ $getline $-getline < 1 }\n",
    final: "{ $getline $-getline < 1 ++ }\n",
    byte: 24,
    insert: " ++",
  },
  {
    name: "a field update moves a source out of a logical-not operand",
    initial: "{ $getline $!getline < x -- }\n",
    final: "{ $getline $!getline < x -- ++ }\n",
    byte: 27,
    insert: " ++",
  },
  {
    name: "a field update moves a source out of a prefix-update operand",
    initial: "{ $getline $--$getline < 1 }\n",
    final: "{ $getline $--$getline < 1 ++ }\n",
    byte: 26,
    insert: " ++",
  },
  {
    name: "a final update follows a grouped membership source",
    initial: "{ $getline < ((x) in a) -- x }\n",
    final: "{ $getline < ((x) in a) -- x ++ }\n",
    byte: 28,
    insert: " ++",
  },
  {
    name: "a final lvalue follows the update after a grouped membership source",
    initial: "{ $getline < ((x) in a) -- x ++ }\n",
    final: "{ $getline < ((x) in a) -- x ++ y }\n",
    byte: 31,
    insert: " y",
  },
]) {
  determinismTest(name, initial, final, [{ byte, deleteBytes: 0, insert }]);
  determinismTest(`reversing ${name}`, final, initial, [
    { byte, deleteBytes: insert.length, insert: "" },
  ]);
}

for (const { name, initial, final, edits } of [
  {
    name: "division to match ERE",
    initial: "BEGIN { print x / a }\n",
    final: "BEGIN { print x ~ /a/ }\n",
    edits: [
      {
        byte: 16,
        deleteBytes: 3,
        insert: "~ /a/",
      },
    ],
  },
  {
    name: "insert div assign equals",
    initial: "BEGIN { x / 2 }\n",
    final: "BEGIN { x /= 2 }\n",
    edits: [
      {
        byte: 11,
        deleteBytes: 0,
        insert: "=",
      },
    ],
  },
  {
    name: "delete div assign equals",
    initial: "BEGIN { x /= 2 }\n",
    final: "BEGIN { x / 2 }\n",
    edits: [
      {
        byte: 11,
        deleteBytes: 1,
        insert: "",
      },
    ],
  },
  {
    name: "operand to ERE",
    initial: "BEGIN { print x ~ a }\n",
    final: "BEGIN { print x ~ /a/ }\n",
    edits: [
      {
        byte: 18,
        deleteBytes: 1,
        insert: "/a/",
      },
    ],
  },
  {
    name: "ERE to operand",
    initial: "BEGIN { print x ~ /a/ }\n",
    final: "BEGIN { print x ~ a }\n",
    edits: [
      {
        byte: 18,
        deleteBytes: 3,
        insert: "a",
      },
    ],
  },
  {
    name: "delete membership concatenation operand",
    initial: "BEGIN { x = (a in b) c }\n",
    final: "BEGIN { x = (a in b) }\n",
    edits: [
      {
        byte: 20,
        deleteBytes: 2,
        insert: "",
      },
    ],
  },
  {
    name: "a closing parenthesis completes an empty ERE group",
    initial: "/(/",
    final: "/()/",
    edits: [
      {
        byte: 2,
        deleteBytes: 0,
        insert: ")",
      },
    ],
  },
  {
    name: "inserting a group replaces an empty alternation branch",
    initial: "/(a|)/",
    final: "/(a|())/",
    edits: [
      {
        byte: 4,
        deleteBytes: 0,
        insert: "()",
      },
    ],
  },
  {
    name: "removing both ERE branches preserves the alternation operator",
    initial: "/a|b/",
    final: "/|/",
    edits: [
      { byte: 3, deleteBytes: 1, insert: "" },
      { byte: 1, deleteBytes: 1, insert: "" },
    ],
  },
  {
    name: "insert ERE closing slash",
    initial: "BEGIN { print /abc\nprint /ok/ }\n",
    final: "BEGIN { print /abc/\nprint /ok/ }\n",
    edits: [
      {
        byte: 18,
        deleteBytes: 0,
        insert: "/",
      },
    ],
  },
  {
    name: "division through broken to escaped ERE",
    initial: "BEGIN { print x / a }\n",
    final: "BEGIN { print /ab\\/c/ }\n",
    edits: [
      {
        byte: 14,
        deleteBytes: 5,
        insert: "/ab\\/c/",
      },
      {
        byte: 20,
        deleteBytes: 1,
        insert: "",
      },
      {
        byte: 20,
        deleteBytes: 0,
        insert: "/",
      },
    ],
  },
  {
    name: "raw newline to line continuation",
    initial: "BEGIN {\n  print 1\n}\n",
    final: "BEGIN {\n  print 1\\\n}\n",
    edits: [
      {
        byte: 17,
        deleteBytes: 0,
        insert: "\\",
      },
    ],
  },
  {
    name: "line continuation to raw newline",
    initial: "BEGIN {\n  print 1\\\n}\n",
    final: "BEGIN {\n  print 1\n}\n",
    edits: [
      {
        byte: 17,
        deleteBytes: 1,
        insert: "",
      },
    ],
  },
  {
    name: "insert if body before close brace",
    initial: "BEGIN { if (condition) }\n",
    final: "BEGIN { if (condition) body }\n",
    edits: [
      {
        byte: 23,
        deleteBytes: 0,
        insert: "body ",
      },
    ],
  },
  {
    name: "restore if body before end item",
    initial: "BEGIN { if (condition)\nEND { print target }\n",
    final: "BEGIN { if (condition) print body }\nEND { print target }\n",
    edits: [
      {
        byte: 22,
        deleteBytes: 1,
        insert: " print body }\n",
      },
    ],
  },
  {
    name: "restore do tail before end item",
    initial: "BEGIN { do print body;\nEND { print target }\n",
    final: "BEGIN { do print body; while (condition) }\nEND { print target }\n",
    edits: [
      {
        byte: 22,
        deleteBytes: 1,
        insert: " while (condition) }\n",
      },
    ],
  },
  {
    name: "insert do tail before close brace",
    initial: "BEGIN { do print body\n}\n",
    final: "BEGIN { do print body\nwhile (condition) }\n",
    edits: [
      {
        byte: 22,
        deleteBytes: 0,
        insert: "while (condition) ",
      },
    ],
  },
  {
    name: "restore special pattern action",
    initial: "BEGIN\nEND {}\n",
    final: "BEGIN {}\nEND {}\n",
    edits: [
      {
        byte: 5,
        deleteBytes: 0,
        insert: " {}",
      },
    ],
  },
  {
    name: "restore function body",
    initial: "function f()\nEND {}\n",
    final: "function f() {}\nEND {}\n",
    edits: [
      {
        byte: 12,
        deleteBytes: 0,
        insert: " {}",
      },
    ],
  },
  {
    name: "insert subscript and action closers at EOF",
    initial: "BEGIN { delete array[offset",
    final: "BEGIN { delete array[offset] }",
    edits: [
      {
        byte: 27,
        deleteBytes: 0,
        insert: "] }",
      },
    ],
  },
  {
    name: "comment backslash to line continuation",
    initial: "#\\\nBEGIN {}\n",
    final: "\\\nBEGIN {}\n",
    edits: [
      {
        byte: 0,
        deleteBytes: 1,
        insert: "",
      },
    ],
  },
  {
    name: "insert string closing quote",
    initial: 'BEGIN { print "abc\n}\n',
    final: 'BEGIN { print "abc"\n}\n',
    edits: [
      {
        byte: 18,
        deleteBytes: 0,
        insert: '"',
      },
    ],
  },
  {
    name: "delete builtin concat line continuation",
    initial: 'BEGIN { x = length\\\n"" }\n',
    final: 'BEGIN { x = length "" }\n',
    edits: [
      {
        byte: 18,
        deleteBytes: 2,
        insert: " ",
      },
    ],
  },
  {
    name: "delete statement gap line continuation",
    initial: "BEGIN {\n;\\\nx\n}\n",
    final: "BEGIN {\n; x\n}\n",
    edits: [
      {
        byte: 9,
        deleteBytes: 2,
        insert: " ",
      },
    ],
  },
]) {
  determinismTest(name, initial, final, edits);
}

function continuationHistoryTest(name, initialSource, steps) {
  test(`posix_awk: ${name}`, () => {
    const edits = [];
    for (const [index, step] of steps.entries()) {
      edits.push(...step.edits);
      const label = `${name}, edit ${index + 1}`;
      assert.deepEqual(
        applyEdits(initialSource, edits),
        Buffer.from(step.source),
        label,
      );
      const fresh = parse(step.source);
      const incremental = parse(initialSource, edits);
      for (const result of [fresh, incremental]) {
        if (step.valid === false) {
          assert.ok(result.status === 0 || result.status === 1, label);
          assert.equal(hasRecovery(result.tree), true, label);
        } else {
          assertStatus(label, result, 0);
          clean(result.tree);
          if (step.ranges !== undefined)
            assert.deepEqual(
              continuationRanges(result.tree),
              step.ranges,
              label,
            );
          if (step.newlines !== undefined)
            assert.equal(
              matchingLineCount(result.tree, /[ \t]newline$/),
              step.newlines,
              label,
            );
        }
      }
      if (step.valid !== false)
        assert.equal(incremental.tree, fresh.tree, label);
    }
  });
}

continuationHistoryTest(
  "editing each byte of repeated continuations at EOF preserves layout",
  "{}\\",
  [
    {
      edits: [{ byte: 3, deleteBytes: 0, insert: "\n" }],
      source: "{}\\\n",
      ranges: [[0, 2, 0, 3]],
      newlines: 0,
    },
    {
      edits: [{ byte: 4, deleteBytes: 0, insert: "\\\n" }],
      source: "{}\\\n\\\n",
      ranges: [
        [0, 2, 0, 3],
        [1, 0, 1, 1],
      ],
      newlines: 0,
    },
    {
      edits: [{ byte: 2, deleteBytes: 1, insert: "" }],
      source: "{}\n\\\n",
      ranges: [[1, 0, 1, 1]],
      newlines: 1,
    },
    {
      edits: [{ byte: 2, deleteBytes: 0, insert: "\\" }],
      source: "{}\\\n\\\n",
      ranges: [
        [0, 2, 0, 3],
        [1, 0, 1, 1],
      ],
      newlines: 0,
    },
    {
      edits: [{ byte: 3, deleteBytes: 1, insert: "" }],
      source: "{}\\\\\n",
      valid: false,
    },
    {
      edits: [{ byte: 3, deleteBytes: 0, insert: "\n" }],
      source: "{}\\\n\\\n",
      ranges: [
        [0, 2, 0, 3],
        [1, 0, 1, 1],
      ],
      newlines: 0,
    },
    {
      edits: [{ byte: 2, deleteBytes: 4, insert: "" }],
      source: "{}",
      ranges: [],
      newlines: 0,
    },
  ],
);

continuationHistoryTest(
  "Unicode edits before a continuation preserve byte ranges and newline ownership",
  '{ print "あ"\\\n x }',
  [
    {
      edits: [{ byte: 12, deleteBytes: 0, insert: "界" }],
      source: '{ print "あ界"\\\n x }',
      ranges: [[0, 16, 0, 17]],
      newlines: 0,
    },
    {
      edits: [{ byte: 16, deleteBytes: 1, insert: "" }],
      source: '{ print "あ界"\n x }',
      ranges: [],
      newlines: 1,
    },
    {
      edits: [{ byte: 16, deleteBytes: 0, insert: "\\" }],
      source: '{ print "あ界"\\\n x }',
      ranges: [[0, 16, 0, 17]],
      newlines: 0,
    },
    {
      edits: [{ byte: 12, deleteBytes: 3, insert: "" }],
      source: '{ print "あ"\\\n x }',
      ranges: [[0, 13, 0, 14]],
      newlines: 0,
    },
  ],
);

for (const { name, before, after, valid } of [
  {
    name: "keyword",
    before: "BE",
    after: "GIN {}\n",
    valid: true,
  },
  {
    name: "name",
    before: "BEGIN { print va",
    after: "lue }\n",
    valid: true,
  },
  {
    name: "built-in name",
    before: "BEGIN { print len",
    after: "gth(x) }\n",
    valid: true,
  },
  {
    name: "assignment operator",
    before: "BEGIN { x +",
    after: "= 1 }\n",
    valid: false,
  },
  {
    name: "unary field assignment name",
    before: "BEGIN { $-na",
    after: "me = value }\n",
    valid: false,
  },
  {
    name: "unary field assignment operator",
    before: "BEGIN { print $!array[index] +",
    after: "= value }\n",
    valid: false,
  },
  {
    name: "exponent",
    before: "BEGIN { print 1e+",
    after: "2 }\n",
    valid: true,
  },
  {
    name: "number suffix",
    before: "BEGIN { print 1.0",
    after: "F }\n",
    valid: true,
  },
  {
    name: "string",
    before: 'BEGIN { print "a',
    after: 'b" }\n',
    valid: false,
  },
  {
    name: "ERE bracket",
    before: "BEGIN { print /[a",
    after: "-z]/ }\n",
    valid: false,
  },
  {
    name: "string escape",
    before: 'BEGIN { print "\\',
    after: 'n" }\n',
    valid: false,
  },
  {
    name: "octal escape",
    before: 'BEGIN { print "\\1',
    after: '23" }\n',
    valid: false,
  },
  {
    name: "ERE escaped delimiter",
    before: "BEGIN { print /a\\",
    after: "/b/ }\n",
    valid: false,
  },
  {
    name: "ERE class name",
    before: "BEGIN { print /[[:al",
    after: "pha:]]/ }\n",
    valid: false,
  },
  {
    name: "ERE duplication count",
    before: "BEGIN { print /a{1",
    after: "2}/ }\n",
    valid: false,
  },
  {
    name: "ERE compound delimiter",
    before: "BEGIN { print /[[",
    after: ":alpha:]]/ }\n",
    valid: false,
  },
]) {
  const initial = before + after;
  const split = `${before}\\\n${after}`;
  const prefix = "# shifted source\n";
  continuationHistoryTest(
    `${name} token boundaries recover through continuation insertion and removal`,
    initial,
    [
      {
        edits: [{ byte: before.length, deleteBytes: 0, insert: "\\\n" }],
        source: split,
        valid,
      },
      {
        edits: [{ byte: 0, deleteBytes: 0, insert: prefix }],
        source: prefix + split,
        valid,
      },
      {
        edits: [
          { byte: prefix.length + before.length, deleteBytes: 2, insert: "" },
        ],
        source: prefix + initial,
      },
      {
        edits: [{ byte: 0, deleteBytes: prefix.length, insert: "" }],
        source: initial,
      },
    ],
  );
}

continuationHistoryTest(
  "quotes change a safe continuation into a string split and back",
  lines("{ print a\\", "+b }"),
  [
    {
      edits: [
        { byte: 13, deleteBytes: 0, insert: '"' },
        { byte: 8, deleteBytes: 0, insert: '"' },
      ],
      source: lines('{ print "a\\', '+b" }'),
      valid: false,
    },
    {
      edits: [
        { byte: 14, deleteBytes: 1, insert: "" },
        { byte: 8, deleteBytes: 1, insert: "" },
      ],
      source: lines("{ print a\\", "+b }"),
    },
  ],
);

continuationHistoryTest(
  "a slash changes a division gap into an ERE split and back",
  lines("{ print x /\\", "a/ + b }"),
  [
    {
      edits: [{ byte: 10, deleteBytes: 0, insert: "~ " }],
      source: lines("{ print x ~ /\\", "a/ + b }"),
      valid: false,
    },
    {
      edits: [{ byte: 10, deleteBytes: 2, insert: "" }],
      source: lines("{ print x /\\", "a/ + b }"),
    },
  ],
);

continuationHistoryTest(
  "a comment marker changes continuation ownership without retaining scanner state",
  lines("{", "# fo\\", "o", "}"),
  [
    {
      edits: [{ byte: 2, deleteBytes: 2, insert: "" }],
      source: lines("{", "fo\\", "o", "}"),
    },
    {
      edits: [{ byte: 2, deleteBytes: 0, insert: "# " }],
      source: lines("{", "# fo\\", "o", "}"),
    },
  ],
);

continuationHistoryTest(
  "a blank preserves two tokens separated by a continuation",
  lines("{ f\\", "oo(x) }"),
  [
    {
      edits: [{ byte: 3, deleteBytes: 0, insert: " " }],
      source: lines("{ f \\", "oo(x) }"),
    },
    {
      edits: [{ byte: 3, deleteBytes: 1, insert: "" }],
      source: lines("{ f\\", "oo(x) }"),
    },
  ],
);

test("posix_awk: every byte inside a Unicode payload can be deleted and repaired through an edit history", () => {
  const prefix = 'BEGIN { print "';
  const suffix = '" }\n';
  const payload = "é😀";
  const replacement = "x";
  const source = prefix + payload + suffix;
  const changed = prefix + replacement + suffix;
  const payloadByte = Buffer.byteLength(prefix);
  const payloadLength = Buffer.byteLength(payload);
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
      const fresh = parse(expectedSource);
      const incremental = parse(source, history);
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
