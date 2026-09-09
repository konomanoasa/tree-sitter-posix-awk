const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { after, before, test } = require("node:test");

const {
  createEnvironment,
  grammar,
  repositoryDirectory,
  run,
  runChecked,
} = require("../scripts/tree-sitter.js");

const runtime = createEnvironment("tree-sitter-posix-awk-runtime.");
const nativeLibrary = path.join(
  runtime.directory,
  process.platform === "win32" ? "parser.dll" : "parser",
);
let sourceSequence = 0;

before(() => {
  runChecked(["build", "--output", nativeLibrary, repositoryDirectory], {
    environment: runtime,
    stdio: "inherit",
  });
});

after(() => {
  runtime.remove();
});

function lines(...sourceLines) {
  return `${sourceLines.join("\n")}\n`;
}

function writeSource(testName, label, source) {
  sourceSequence += 1;
  const filename = `${String(sourceSequence).padStart(3, "0")}-${testName}-${label}.awk`;
  const sourcePath = path.join(runtime.directory, filename);
  fs.writeFileSync(sourcePath, source);
  return sourcePath;
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
    args.push("--edits", ...edits, "--", sourcePath);
  } else {
    args.push(sourcePath);
  }

  const result = run(args, { environment: runtime });
  if (result.error !== undefined) {
    throw result.error;
  }
  return {
    status: result.status,
    stderr: result.stderr,
    stdout: result.stdout,
    tree: normalizeParseTree(result.stdout, sourcePath),
  };
}

function parseDescription(label, result) {
  return `${label}\nExit status: ${result.status}\n${result.stdout}${result.stderr}`;
}

function assertStatus(label, result, expectedStatus) {
  assert.equal(result.status, expectedStatus, parseDescription(label, result));
}

function assertFresh(testName, source, expectedStatus = 0) {
  const sourcePath = writeSource(testName, "fresh", source);
  const native = captureParse(sourcePath);
  assertStatus(`${testName} native fresh parse`, native, expectedStatus);
  return native.tree;
}

// An edit is "<position> <delete count> <inserted text>" in bytes, the
// shape `tree-sitter parse --edits` takes.
function parseEdit(testName, edit, sourceLength) {
  const match = edit.match(/^([0-9]+) ([0-9]+) (.*)$/s);
  assert.notEqual(
    match,
    null,
    `${testName}: invalid edit ${JSON.stringify(edit)}`,
  );
  const position = Number(match[1]);
  const deleteCount = Number(match[2]);
  assert.ok(
    Number.isSafeInteger(position) &&
      Number.isSafeInteger(deleteCount) &&
      position <= sourceLength &&
      deleteCount <= sourceLength - position,
    `${testName}: out-of-bounds edit ${JSON.stringify(edit)} for ${sourceLength} bytes`,
  );
  return { deleteCount, inserted: Buffer.from(match[3]), position };
}

function applyEdits(testName, initial, edits) {
  let actual = Buffer.from(initial);
  for (const edit of edits) {
    const { deleteCount, inserted, position } = parseEdit(
      testName,
      edit,
      actual.length,
    );
    actual = Buffer.concat([
      actual.subarray(0, position),
      inserted,
      actual.subarray(position + deleteCount),
    ]);
  }
  return actual;
}

function assertDeterministicEdit(testName, initialSource, finalSource, edits) {
  const initialPath = writeSource(testName, "initial", initialSource);
  const finalPath = writeSource(testName, "final", finalSource);
  assert.deepEqual(
    applyEdits(testName, initialSource, edits),
    Buffer.from(finalSource),
    `${testName}: edits do not produce the final source`,
  );

  const incremental = captureParse(initialPath, edits);
  const fresh = captureParse(finalPath);
  for (const [label, result] of [
    ["native incremental parse", incremental],
    ["native fresh parse", fresh],
  ]) {
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

// The --cst renderer prefixes every node on an error path with "•" and
// prints a missing named leaf as a zero-width node without the word MISSING,
// so the bullet is the only marker that covers both ERROR and missing nodes.
function clean(tree) {
  excludes(tree, "•");
}

function cleanContinuation(tree) {
  contains(tree, "line_continuation");
  clean(tree);
}

function dirty(tree) {
  contains(tree, "•");
}

function matchingLineCount(tree, pattern) {
  return tree.split("\n").filter((line) => pattern.test(line)).length;
}

function pointAtMost(left, right) {
  return (
    left.row < right.row ||
    (left.row === right.row && left.column <= right.column)
  );
}

function containsRange(outer, inner) {
  return (
    pointAtMost(outer.start, inner.start) && pointAtMost(inner.end, outer.end)
  );
}

// The renderer's description column is not a reliable depth signal (error
// bullets and range-text widths shift it per line), so recover each node's
// depth from range containment over the preorder line sequence instead.
function parseCstLines(tree) {
  const stack = [];
  return tree
    .split("\n")
    .filter((line) => line.length > 0)
    .map((line) => {
      const match = line.match(
        /^([0-9]+):([0-9]+)[ \t]+-[ \t]+([0-9]+):([0-9]+)[ \t]+(.+)$/,
      );
      assert.notEqual(match, null, `Unrecognized CST line: ${line}`);
      const record = {
        description: match[5],
        end: { column: Number(match[4]), row: Number(match[3]) },
        start: { column: Number(match[2]), row: Number(match[1]) },
      };
      while (
        stack.length > 0 &&
        !containsRange(stack[stack.length - 1], record)
      ) {
        stack.pop();
      }
      record.depth = stack.length;
      stack.push(record);
      return record;
    });
}

function sourcePoint(source, index) {
  const prefix = source.slice(0, index);
  const lastNewline = prefix.lastIndexOf("\n");
  return {
    column: Buffer.byteLength(prefix.slice(lastNewline + 1)),
    row: prefix.split("\n").length - 1,
  };
}

function samePoint(left, right) {
  return left.row === right.row && left.column === right.column;
}

function relativePoint(point, origin) {
  return {
    column:
      point.row === origin.row ? point.column - origin.column : point.column,
    row: point.row - origin.row,
  };
}

// An item inside item_list carries the `item` field and the final item of
// program does not; the comparison is about the item itself.
function normalizeItemSubtree(records, rootIndex) {
  const root = records[rootIndex];
  let endIndex = rootIndex + 1;
  while (endIndex < records.length && records[endIndex].depth > root.depth) {
    endIndex += 1;
  }

  return records
    .slice(rootIndex, endIndex)
    .map((record) => {
      const start = relativePoint(record.start, root.start);
      const end = relativePoint(record.end, root.start);
      const description = record === root ? "item" : record.description;
      return `${"  ".repeat(record.depth - root.depth)}${start.row}:${start.column} - ${end.row}:${end.column} ${description}`;
    })
    .join("\n");
}

function topLevelItems(tree) {
  const records = parseCstLines(tree);
  const items = [];
  for (let index = 0; index < records.length; index += 1) {
    if (["item: item", "item"].includes(records[index].description)) {
      items.push({
        end: records[index].end,
        normalized: normalizeItemSubtree(records, index),
        start: records[index].start,
      });
    }
  }
  return items;
}

function assertPreservedItems(testName, source, preservedSources) {
  const sourcePath = writeSource(testName, "boundary", source);
  const result = captureParse(sourcePath);
  assert.ok(
    result.status === 0 || result.status === 1,
    parseDescription(`${testName} boundary parse`, result),
  );

  const actualItems = topLevelItems(result.tree);
  let searchStart = 0;
  for (const [index, itemSource] of preservedSources.entries()) {
    const sourceStart = source.indexOf(itemSource, searchStart);
    assert.notEqual(
      sourceStart,
      -1,
      `${testName}: preserved item ${JSON.stringify(itemSource)} is absent`,
    );
    const expectedStart = sourcePoint(source, sourceStart);
    const expectedEnd = sourcePoint(source, sourceStart + itemSource.length);
    const matches = actualItems.filter(
      (item) =>
        samePoint(item.start, expectedStart) &&
        samePoint(item.end, expectedEnd),
    );
    assert.equal(
      matches.length,
      1,
      `${testName}: expected one top-level item for ${JSON.stringify(itemSource)}\n${result.tree}`,
    );

    const isolatedTree = assertFresh(
      `${testName}-isolated-${index + 1}`,
      itemSource,
    );
    clean(isolatedTree);
    const isolatedItems = topLevelItems(isolatedTree);
    assert.equal(
      isolatedItems.length,
      1,
      `${testName}: isolated source must contain exactly one item\n${isolatedTree}`,
    );
    assert.deepEqual(
      matches[0].normalized,
      isolatedItems[0].normalized,
      `${testName}: recovered and isolated item CSTs differ for ${JSON.stringify(itemSource)}`,
    );
    searchStart = sourceStart + itemSource.length;
  }
}

function freshTest(name, source, assertions) {
  test(name, () => assertions(assertFresh(name, source)));
}

function determinismTest(name, initial, final, edits, assertions = () => {}) {
  test(name, () =>
    assertions(assertDeterministicEdit(name, initial, final, edits)),
  );
}

// Applies the edits, then reverts them in reverse order, so the final source
// equals the initial one while the intermediate versions (and the subtrees
// reused from them) passed through syntax errors.
function editHistoryTest(name, source, edits) {
  const reverts = [];
  let current = Buffer.from(source);
  for (const edit of edits) {
    const { deleteCount, inserted, position } = parseEdit(
      name,
      edit,
      current.length,
    );
    const deleted = current.subarray(position, position + deleteCount);
    reverts.unshift(`${position} ${inserted.length} ${deleted}`);
    current = applyEdits(name, current, [edit]);
  }
  determinismTest(name, source, source, [...edits, ...reverts]);
}

const closedItemBoundaryCases = [
  ["ERE pattern", "/ready/ {}"],
  ["number pattern", "1 {}"],
  ["name pattern", "active {}"],
  ["action-only item", "{}"],
  ["BEGIN item", "BEGIN {}"],
  ["END item", "END {}"],
  ["function item", "function following() {}"],
];

for (const [label, following] of closedItemBoundaryCases) {
  test(`a closed item boundary preserves a following ${label}`, () => {
    const preceding = "BEGIN {}";
    assertPreservedItems(
      `closed-item-before-${label}`,
      `${preceding} ${following}`,
      [preceding, following],
    );
  });
}

for (const [label, following] of [
  ["BEGIN item", "BEGIN {}"],
  ["END item", "END {}"],
  ["function item", "function following() {}"],
]) {
  test(`an actionless pattern boundary preserves a following ${label}`, () => {
    const preceding = "active";
    assertPreservedItems(
      `normal-pattern-before-${label}`,
      `${preceding} ${following}`,
      [preceding, following],
    );
  });
}

const physicallyClosedMalformedItems = [
  ["missing assignment operand", "middle { value = ; }"],
  ["missing if body", "middle { if (condition) }"],
  ["missing while body", "middle { while (condition) }"],
  ["missing classic for body", "middle { for (;;) }"],
  ["missing for-in body", "middle { for (key in values) }"],
  ["missing nested control bodies", "middle { if (outer) while (inner) }"],
  ["missing control operand", "middle { if (condition +) print value }"],
  ["missing do tail", "middle { do print value; }"],
  ["missing function parameter", "function malformed(first,) {}"],
  ["missing function header", "function malformed {}"],
  ["missing function body", "function malformed()"],
  ["missing special pattern action", "BEGIN"],
  ["missing do tail after a newline", "middle { do print value\n}"],
  ["missing do tail after a bodyless control", "middle { do while (inner) }"],
  ["missing left range arm", ", right {}"],
  ["missing right range arm", "left, {}"],
  ["hash after an ERE class name", "middle { print /[[:alpha#:]]/ }"],
  ["hash after an ERE interval count", "middle { print /a{2#}/ }"],
];

for (const [label, malformed] of physicallyClosedMalformedItems) {
  test(`a physically closed item with ${label} preserves adjacent items`, () => {
    const preceding = "BEGIN { print before }";
    const following = "END { print after }";
    assertPreservedItems(
      `physically-closed-${label}`,
      lines(preceding, `${malformed} ${following}`),
      [preceding, following],
    );
  });
}

for (const { label, initial, final, before, inserted, preserved } of [
  {
    label: "function body after a continuation and comment",
    initial: lines("before {}", "function f() \\", "#", "END {}"),
    final: lines("before {}", "function f() \\", "#", "{}", "END {}"),
    before: "END",
    inserted: "{}\n",
    preserved: ["before {}", "END {}"],
  },
  {
    label: "function body after a continuation and newline",
    initial: lines("before {}", "function f() \\", "", "END {}"),
    final: lines("before {}", "function f() \\", "", "{}", "END {}"),
    before: "END",
    inserted: "{}\n",
    preserved: ["before {}", "END {}"],
  },
  {
    label: "special pattern action before a comment's newline",
    initial: lines("before {}", "BEGIN \\", "#", "{}", "END {}"),
    final: lines("before {}", "BEGIN \\", "{} #", "{}", "END {}"),
    before: "#",
    inserted: "{} ",
    preserved: ["before {}", "{}", "END {}"],
  },
]) {
  test(`a missing ${label} preserves adjacent items and repairs cleanly`, () => {
    assertPreservedItems(label, initial, preserved);
    const tree = assertDeterministicEdit(label, initial, final, [
      `${initial.indexOf(before)} 0 ${inserted}`,
    ]);
    contains(tree, "line_continuation");
  });
}

for (const [label, malformedLine] of [
  ["string", 'middle { print "broken'],
  ["ERE", "middle { print /broken"],
  ["ERE character class opener", "middle { print /[[:"],
  ["ERE character class", "middle { print /[[:alpha"],
  ["ERE interval opener", "middle { print /a{"],
  ["ERE interval", "middle { print /a{1,"],
]) {
  test(`a raw newline ending an unterminated ${label} preserves adjacent items`, () => {
    const preceding = "BEGIN { print before }";
    const following = "END { print after }";
    assertPreservedItems(
      `raw-newline-${label}`,
      lines(preceding, malformedLine, `} ${following}`),
      [preceding, following],
    );
  });
}

for (const [label, malformed] of [
  ["string", 'END { print "broken'],
  ["ERE", "END { print /broken"],
  ["action", "END { print after"],
  ["parenthesis", "END { print (after"],
  ["bracket", "END { print array[after"],
]) {
  test(`an unterminated ${label} at EOF preserves preceding items`, () => {
    const preceding = "BEGIN { print before }";
    assertPreservedItems(
      `unterminated-${label}-at-EOF`,
      `${preceding}\n${malformed}`,
      [preceding],
    );
  });
}

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

test("anonymous-token field contract", () => {
  const queryPath = path.join(runtime.directory, "fields.scm");
  const sourcePath = writeSource(
    "anonymous-token-field-contract",
    "source",
    fieldContractSource,
  );
  fs.writeFileSync(queryPath, fieldContractQuery);
  const result = run(
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
    { environment: runtime },
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

freshTest("named-node-field-contract", namedFieldSource, (tree) => {
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

const division = lines("BEGIN { print x / a }");
const matchEre = lines("BEGIN { print x ~ /a/ }");
const divisionAssignment = lines("BEGIN { x /= 2 }");
const divisionExpression = lines("BEGIN { x / 2 }");
const matchOperand = lines("BEGIN { print x ~ a }");

const invalidClassificationCases = [
  {
    assertions: (tree) => {
      assert.equal(
        matchingLineCount(tree, /^[ \t0-9:-]*ere([ \t]|$)/),
        0,
        "Expected division priority to prevent an ERE node",
      );
    },
    name: "division context wins over an ERE-shaped spelling",
    source: lines("BEGIN { print x /a/ }"),
  },
  {
    assertions: (tree) => {
      assert.equal(
        matchingLineCount(tree, /^[ \t0-9:-]*"\/"$/),
        0,
        "Expected no division token where '/=' is the longest match",
      );
    },
    name: "div-assign never splits after an invalid lvalue",
    source: lines("BEGIN { (a) /= b }"),
  },
  {
    assertions: (tree) => {
      excludes(tree, "output_redirection");
    },
    name: "greater-than-or-equal never becomes output redirection",
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
    assertions: (tree) => excludes(tree, "func_name"),
    name: "a non-newline backslash does not create call adjacency",
    source: lines("BEGIN {", String.raw`  f\(value)`, "  after", "}"),
  },
  {
    name: "an unparenthesized assignment in a conditional alternative is rejected",
    source: lines("BEGIN { x = a ? b : c = d }"),
  },
  {
    assertions: (tree) => excludes(tree, "comment"),
    name: "a hash where the ERE grammar accepts no character never opens a comment",
    source: lines(
      "BEGIN {",
      "  print /[[:alpha#:]]/",
      "  print /a{2#}/",
      "  print /[[:#:]]/",
      "  print /a{#}/",
      "}",
    ),
  },
];

for (const classificationCase of invalidClassificationCases) {
  test(classificationCase.name, () => {
    const sourcePath = writeSource(
      classificationCase.name,
      "invalid",
      classificationCase.source,
    );
    const result = captureParse(sourcePath);
    // A parse that recovers with missing nodes alone exits with 0.
    assert.ok(
      result.status === 0 || result.status === 1,
      parseDescription(`${classificationCase.name} fresh parse`, result),
    );
    dirty(result.tree);
    classificationCase.assertions?.(result.tree);
  });
}

determinismTest("division-to-match-ere", division, matchEre, ["16 3 ~ /a/"]);
determinismTest(
  "insert-div-assign-equals",
  divisionExpression,
  divisionAssignment,
  ["11 0 ="],
);
determinismTest(
  "delete-div-assign-equals",
  divisionAssignment,
  divisionExpression,
  ["11 1 "],
);
determinismTest("operand-to-ere", matchOperand, matchEre, ["18 1 /a/"]);
determinismTest("ere-to-operand", matchEre, matchOperand, ["18 3 a"]);

for (const [label, initial, final] of [
  [
    "expression",
    lines("BEGIN { value = a + /b/ + c }"),
    lines("BEGIN { value = a   /b/ + c }"),
  ],
  [
    "print expression with a continuation",
    lines("BEGIN { print a + \\", "/b/ + c }"),
    lines("BEGIN { print a   \\", "/b/ + c }"),
  ],
  [
    "top-level pattern",
    lines("a + /b/ + c { print }"),
    lines("a   /b/ + c { print }"),
  ],
]) {
  determinismTest(
    `removing an operator reclassifies ERE slashes as division in the ${label}`,
    initial,
    final,
    [`${initial.indexOf("+")} 1  `],
    (tree) => {
      excludes(tree, "extended_reg_exp");
      assert.equal(
        matchingLineCount(tree, /^[ \t0-9:-]*"\/"$/),
        2,
        "Expected both slashes to be division tokens in the valid expression",
      );
    },
  );
}

const membershipBeforeLogicalAnd = lines("BEGIN { x = a in b && c }");
const membershipBeforeMatch = lines("BEGIN { x = a in b ~ c }");
determinismTest(
  "membership-logical-and-to-match",
  membershipBeforeLogicalAnd,
  membershipBeforeMatch,
  ["19 2 ~"],
  (tree) => {
    contains(tree, '"~"');
    excludes(tree, "operator: and");
  },
);
determinismTest(
  "membership-match-to-logical-and",
  membershipBeforeMatch,
  membershipBeforeLogicalAnd,
  ["19 1 &&"],
  (tree) => {
    contains(tree, "operator: and");
    excludes(tree, '"~"');
  },
);

const plainMembership = lines("BEGIN { x = a in b }");
const concatenatedMembership = lines("BEGIN { x = a in b c }");
determinismTest(
  "insert-membership-concatenation-operand",
  plainMembership,
  concatenatedMembership,
  ["18 0  c"],
  (tree) => {
    assert.match(tree, /^[ \t0-9:-]+right: name `b`$/m);
    assert.match(tree, /^[ \t0-9:-]+right: non_unary_expr$/m);
  },
);
determinismTest(
  "delete-membership-concatenation-operand",
  concatenatedMembership,
  plainMembership,
  ["18 2 "],
);

const plainBracketList = lines("BEGIN { print /[+]?[a]/ }");
const trailingHyphenBracketList = lines("BEGIN { print /[+-]?[a]/ }");
determinismTest(
  "insert-trailing-bracket-hyphen",
  plainBracketList,
  trailingHyphenBracketList,
  ["17 0 -"],
  (tree) => {
    assert.equal(
      matchingLineCount(tree, /^[ \t0-9:-]+bracket_expression$/),
      2,
      "Expected trailing bracket hyphen source to contain two bracket expressions",
    );
    contains(tree, "ere_dupl_symbol");
    excludes(tree, "range_expression");
  },
);

const shortRangeEre = lines("BEGIN { print /[%-@]/ }");
const hyphenEndedRangeEre = lines("BEGIN { print /[%--@]/ }");
determinismTest(
  "insert-range-ending-hyphen",
  shortRangeEre,
  hyphenEndedRangeEre,
  ["18 0 -"],
  (tree) => {
    assert.equal(
      matchingLineCount(tree, /^[ \t0-9:-]+range_expression$/),
      1,
      "Expected one range expression ending at the inserted hyphen",
    );
    assert.equal(
      matchingLineCount(tree, /^[ \t0-9:-]+collating_element/),
      2,
      "Expected the range start and the following element only",
    );
  },
);
determinismTest(
  "delete-range-ending-hyphen",
  hyphenEndedRangeEre,
  shortRangeEre,
  ["18 1 "],
  (tree) => {
    assert.equal(
      matchingLineCount(tree, /^[ \t0-9:-]+collating_element/),
      2,
      "Expected a plain range between two collating elements",
    );
  },
);

const closedEre = lines("BEGIN { print /abc/", "print /ok/ }");
const unclosedEre = lines("BEGIN { print /abc", "print /ok/ }");
determinismTest("insert-ere-closing-slash", unclosedEre, closedEre, ["18 0 /"]);

const plainEreDelimiter = lines("BEGIN { print /ab/ }");
const escapedEreDelimiter = lines(String.raw`BEGIN { print /ab\/c/ }`);
determinismTest(
  "escape-ere-closing-slash",
  plainEreDelimiter,
  escapedEreDelimiter,
  [String.raw`17 1 \/c/`],
  (tree) => {
    contains(tree, "escaped_delimiter");
  },
);
determinismTest(
  "division-through-broken-to-escaped-ere",
  division,
  escapedEreDelimiter,
  [String.raw`14 5 /ab\/c/`, "20 1 ", "20 0 /"],
);
determinismTest(
  "restore-ere-closing-slash",
  escapedEreDelimiter,
  plainEreDelimiter,
  ["17 4 /"],
  (tree) => excludes(tree, "escaped_delimiter"),
);

const threeDigitOctalEre = lines(String.raw`BEGIN { print /\124/ }`);
const octalFollowedByCharacterEre = lines(String.raw`BEGIN { print /\1234/ }`);
determinismTest(
  "split-ere-octal-at-three-digits",
  threeDigitOctalEre,
  octalFollowedByCharacterEre,
  ["18 0 3"],
  (tree) => {
    contains(tree, "escape_sequence");
    contains(tree, "ordinary_character");
  },
);
determinismTest(
  "join-ere-octal-at-three-digits",
  octalFollowedByCharacterEre,
  threeDigitOctalEre,
  ["18 1 "],
  (tree) => {
    contains(tree, "escape_sequence");
    excludes(tree, "ordinary_character");
  },
);

const greedyEre = lines("BEGIN { print /a*/ }");
const shortestEre = lines("BEGIN { print /a*?/ }");
determinismTest(
  "insert-ere-repetition-modifier",
  greedyEre,
  shortestEre,
  ["17 0 ?"],
  (tree) => contains(tree, "repetition_modifier"),
);
determinismTest(
  "delete-ere-repetition-modifier",
  shortestEre,
  greedyEre,
  ["17 1 "],
  (tree) => excludes(tree, "repetition_modifier"),
);

const equivalenceEre = lines("BEGIN { print /[[=a=]]/ }");
const classEre = lines("BEGIN { print /[[:alpha:]]/ }");
determinismTest(
  "equivalence-to-character-class",
  equivalenceEre,
  classEre,
  ["16 5 [:alpha:]"],
  (tree) => {
    contains(tree, "character_class");
    contains(tree, "class_name");
    excludes(tree, "equivalence_class");
  },
);
determinismTest(
  "character-class-to-equivalence",
  classEre,
  equivalenceEre,
  ["16 9 [=a=]"],
  (tree) => {
    contains(tree, "equivalence_class");
    excludes(tree, "character_class");
  },
);

const literalOpenBracketEre = lines("BEGIN { print /[[]/ }");
const collatingSymbolEre = lines("BEGIN { print /[[.x.]]/ }");
determinismTest(
  "literal-bracket-to-collating-symbol",
  literalOpenBracketEre,
  collatingSymbolEre,
  ["17 0 .x.]"],
  (tree) => {
    contains(tree, "collating_symbol");
  },
);
determinismTest(
  "collating-symbol-to-literal-bracket",
  collatingSymbolEre,
  literalOpenBracketEre,
  ["17 4 "],
  (tree) => {
    contains(tree, "collating_element");
    excludes(tree, "collating_symbol");
  },
);

const rawStatementNewline = lines("BEGIN {", "  print 1", "}");
const continuedStatementBoundary = lines("BEGIN {", "  print 1\\", "}");
determinismTest(
  "raw-newline-to-line-continuation",
  rawStatementNewline,
  continuedStatementBoundary,
  ["17 0 \\"],
);
determinismTest(
  "line-continuation-to-raw-newline",
  continuedStatementBoundary,
  rawStatementNewline,
  ["17 1 "],
);

const outputPipe = lines("BEGIN { print value | command }");
const logicalOrPrint = lines("BEGIN { print value || command }");
determinismTest(
  "output-pipe-to-logical-or",
  outputPipe,
  logicalOrPrint,
  ["21 0 |"],
  (tree) => {
    contains(tree, "operator: or");
    excludes(tree, "output_redirection");
  },
);

const plainAppend = lines("BEGIN { print value >> archive }");
const continuedAppend = lines("BEGIN { print value \\", ">> archive }");
determinismTest(
  "insert-append-line-continuation",
  plainAppend,
  continuedAppend,
  ["20 0 \\\n"],
  (tree) => {
    contains(tree, "redirection: output_redirection");
    contains(tree, "append");
    contains(tree, "line_continuation");
  },
);

const bodylessIf = lines("BEGIN { if (condition) }");
const bodiedIf = lines("BEGIN { if (condition) body }");
determinismTest("insert-if-body-before-close-brace", bodylessIf, bodiedIf, [
  "23 0 body ",
]);

const sameLineItems = lines("BEGIN {} END {}");
const separatedItems = lines("BEGIN {}", "END {}");
determinismTest(
  "insert-item-terminator-between-items",
  sameLineItems,
  separatedItems,
  ["8 1 \n"],
  (tree) => contains(tree, "terminator: terminator"),
);

const completeReservedIf = lines(
  "BEGIN { if (condition) print body }",
  "END { print target }",
);
const missingReservedIf = lines(
  "BEGIN { if (condition)",
  "END { print target }",
);
determinismTest(
  "restore-if-body-before-end-item",
  missingReservedIf,
  completeReservedIf,
  ["22 1  print body }\n"],
);

const completeDoTail = lines(
  "BEGIN { do print body; while (condition) }",
  "END { print target }",
);
const missingDoTail = lines("BEGIN { do print body;", "END { print target }");
determinismTest(
  "restore-do-tail-before-end-item",
  missingDoTail,
  completeDoTail,
  ["22 1  while (condition) }\n"],
);

const closedDoTail = lines("BEGIN { do print body", "while (condition) }");
const openDoTail = lines("BEGIN { do print body", "}");
determinismTest("insert-do-tail-before-close-brace", openDoTail, closedDoTail, [
  "22 0 while (condition) ",
]);

const specialPatternItems = lines("BEGIN {}", "END {}");
const actionlessSpecialPattern = lines("BEGIN", "END {}");
determinismTest(
  "restore-special-pattern-action",
  actionlessSpecialPattern,
  specialPatternItems,
  ["5 0  {}"],
);

const functionItems = lines("function f() {}", "END {}");
const bodylessFunction = lines("function f()", "END {}");
determinismTest("restore-function-body", bodylessFunction, functionItems, [
  "12 0  {}",
]);
const closedSubscriptEof = "BEGIN { delete array[offset] }";
const openSubscriptEof = "BEGIN { delete array[offset";
determinismTest(
  "insert-subscript-and-action-closers-at-eof",
  openSubscriptEof,
  closedSubscriptEof,
  ["27 0 ] }"],
);

const commentBackslash = lines("#\\", "BEGIN {}");
const leadingContinuation = lines("\\", "BEGIN {}");
determinismTest(
  "comment-backslash-to-line-continuation",
  commentBackslash,
  leadingContinuation,
  ["0 1 "],
);

const blankCall = lines("BEGIN { f (value) }");
const continuedCall = lines("BEGIN { f\\", "(value) }");
determinismTest(
  "blank-to-line-continuation-call",
  blankCall,
  continuedCall,
  ["9 1 \\\n"],
  (tree) => {
    assert.match(tree, /^[ \t0-9:-]*func_name[ \t]/m);
    contains(tree, "line_continuation");
  },
);

const plainAddAssign = lines("BEGIN { value += other }");
const continuedAddAssign = lines("BEGIN { value \\", "+= other }");
determinismTest(
  "insert-add-assign-line-continuation",
  plainAddAssign,
  continuedAddAssign,
  ["14 0 \\\n"],
  (tree) => {
    contains(tree, "add_assign");
    contains(tree, "line_continuation");
  },
);

const plainAdditive = lines("BEGIN { left + right }");
const continuedAdditive = lines("BEGIN { left\\", "+ right }");
determinismTest(
  "insert-additive-operator-line-continuation",
  plainAdditive,
  continuedAdditive,
  ["12 1 \\\n"],
  (tree) => {
    contains(tree, '"+"');
    contains(tree, "line_continuation");
  },
);

const plainComparison = lines("BEGIN { left < right }");
const continuedComparison = lines("BEGIN { left\\", "< right }");
determinismTest(
  "insert-comparison-operator-line-continuation",
  plainComparison,
  continuedComparison,
  ["12 1 \\\n"],
  (tree) => {
    contains(tree, '"<"');
    contains(tree, "line_continuation");
  },
);

const continuedGetlineRedirect = lines(
  "BEGIN { getline target\\",
  "< source }",
);
const continuedGetlineComparison = lines(
  "BEGIN { getline target\\",
  "<= source }",
);
freshTest("continued-getline-redirect", continuedGetlineRedirect, (tree) => {
  contains(tree, "source: expr");
  excludes(tree, "operator: le");
  cleanContinuation(tree);
});
determinismTest(
  "continued-getline-redirect-to-comparison",
  continuedGetlineRedirect,
  continuedGetlineComparison,
  ["25 0 ="],
  (tree) => {
    contains(tree, "operator: le");
    excludes(tree, "source: expr");
    contains(tree, "line_continuation");
  },
);

const plainConditional = lines("BEGIN { condition ? yes : no }");
const continuedConditional = lines("BEGIN { condition ? yes\\", ": no }");
determinismTest(
  "insert-conditional-colon-line-continuation",
  plainConditional,
  continuedConditional,
  ["23 1 \\\n"],
  (tree) => {
    contains(tree, "alternative: expr");
    contains(tree, "line_continuation");
  },
);

const plainLogical = lines("BEGIN { left && right }");
const continuedLogical = lines("BEGIN { left\\", "&& right }");
determinismTest(
  "insert-logical-operator-line-continuation",
  plainLogical,
  continuedLogical,
  ["12 1 \\\n"],
  (tree) => {
    contains(tree, "and");
    contains(tree, "line_continuation");
  },
);

const plainPipeGetline = lines("BEGIN { source | getline target }");
const continuedPipeGetline = lines("BEGIN { source\\", "| getline target }");
determinismTest(
  "insert-input-pipe-line-continuation",
  plainPipeGetline,
  continuedPipeGetline,
  ["14 1 \\\n"],
  (tree) => {
    contains(tree, "non_unary_input_function");
    contains(tree, "line_continuation");
  },
);

const unaryPipeGetline = lines("BEGIN { -source | getline target }");
const fieldPipeGetline = lines("BEGIN { $source | getline target }");
determinismTest(
  "unary-to-field-pipe-getline",
  unaryPipeGetline,
  fieldPipeGetline,
  ["8 1 $"],
  (tree) => {
    assert.match(tree, /^[ \t0-9:-]*non_unary_input_function$/m);
  },
);

const closedString = lines('BEGIN { print "abc"', "}");
const unclosedString = lines('BEGIN { print "abc', "}");
determinismTest("insert-string-closing-quote", unclosedString, closedString, [
  '18 0 "',
]);

const hashString = lines('BEGIN { print "#"', "}");
const hashComment = lines('BEGIN { print #"', "}");
determinismTest(
  "string-to-comment",
  hashString,
  hashComment,
  ["14 1 "],
  (tree) => {
    contains(tree, "comment");
    excludes(tree, "string");
  },
);
determinismTest(
  "comment-to-string",
  hashComment,
  hashString,
  ['14 0 "'],
  (tree) => {
    contains(tree, "string_content");
    excludes(tree, "comment");
  },
);

const hashEre = lines("BEGIN { print /#/", "}");
const hashEreComment = lines("BEGIN { print #/", "}");
determinismTest(
  "ere-to-comment",
  hashEre,
  hashEreComment,
  ["14 1 "],
  (tree) => {
    contains(tree, "comment");
    excludes(tree, "ere");
  },
);
determinismTest(
  "comment-to-ere",
  hashEreComment,
  hashEre,
  ["14 0 /"],
  (tree) => {
    contains(tree, "ordinary_character");
    excludes(tree, "comment");
  },
);

const plainEscape = lines('BEGIN { print "an" }');
const backslashEscape = lines(String.raw`BEGIN { print "a\n" }`);
determinismTest(
  "insert-string-escape-backslash",
  plainEscape,
  backslashEscape,
  ["16 0 \\"],
  (tree) => contains(tree, "escape_sequence"),
);
determinismTest(
  "delete-string-escape-backslash",
  backslashEscape,
  plainEscape,
  ["16 1 "],
  (tree) => excludes(tree, "escape_sequence"),
);

const topLevelErePattern = lines("/ready/ { print }");
const topLevelDivisionPattern = lines("total / count { print }");
determinismTest(
  "top-level-ere-to-division-pattern",
  topLevelErePattern,
  topLevelDivisionPattern,
  ["0 7 total / count"],
  (tree) => {
    contains(tree, "normal_pattern");
    excludes(tree, "extended_reg_exp");
    assert.equal(
      matchingLineCount(tree, /^[ \t0-9:-]*"\/"$/),
      1,
      "Expected one division slash in the top-level normal pattern",
    );
  },
);
determinismTest(
  "top-level-division-to-ere-pattern",
  topLevelDivisionPattern,
  topLevelErePattern,
  ["0 13 /ready/"],
  (tree) => {
    contains(tree, "normal_pattern");
    contains(tree, "extended_reg_exp");
    assert.equal(
      matchingLineCount(tree, /^[ \t0-9:-]*"\/"$/),
      2,
      "Expected opening and closing slashes in the top-level ERE pattern",
    );
  },
);

const adjacentFunctionName = lines("function compute(value) {}");
const continuedSpacedFunctionName = lines("function compute \\", "(value) {}");
determinismTest(
  "adjacent-to-continued-spaced-function-name",
  adjacentFunctionName,
  continuedSpacedFunctionName,
  ["16 0  \\\n"],
  (tree) => {
    assert.match(tree, /^[ \t0-9:-]*name:[ \t]+name[ \t]+`compute`$/m);
    excludes(tree, "func_name");
    contains(tree, "line_continuation");
  },
);
determinismTest(
  "continued-spaced-to-adjacent-function-name",
  continuedSpacedFunctionName,
  adjacentFunctionName,
  ["16 3 "],
  (tree) => {
    assert.match(tree, /^[ \t0-9:-]*name:[ \t]+func_name[ \t]+`compute`$/m);
    excludes(tree, "line_continuation");
  },
);

const compactRangePattern = lines("start,stop {}");
const multilineRangePattern = lines("start,", "stop {}");
determinismTest(
  "insert-range-pattern-newline",
  compactRangePattern,
  multilineRangePattern,
  ["6 0 \n"],
  (tree) => {
    contains(tree, "normal_pattern");
    contains(tree, "left: expr");
    contains(tree, "right: expr");
    contains(tree, "newline_opt");
    assert.equal(
      matchingLineCount(tree, /^[ \t0-9:-]+newline_opt$/),
      1,
      "Expected one range-pattern newline_opt owner",
    );
  },
);
determinismTest(
  "delete-range-pattern-newline",
  multilineRangePattern,
  compactRangePattern,
  ["6 1 "],
  (tree) => {
    excludes(tree, "newline_opt");
  },
);

const separatedClosedItems = "BEGIN {}\nEND {}";
const directOpenActionItem = "BEGIN {END {}";
determinismTest(
  "restore-action-close-and-item-terminator",
  directOpenActionItem,
  separatedClosedItems,
  ["7 0 }\n"],
  (tree) => {
    contains(tree, "terminator: terminator");
  },
);

const bareBuiltinConcat = lines('BEGIN { x = length "" }');
const continuedBuiltinConcat = lines("BEGIN { x = length\\", '"" }');
determinismTest(
  "insert-builtin-concat-line-continuation",
  bareBuiltinConcat,
  continuedBuiltinConcat,
  ["18 1 \\\n"],
  (tree) => {
    contains(tree, "builtin_func_name");
    contains(tree, "line_continuation");
  },
);
determinismTest(
  "delete-builtin-concat-line-continuation",
  continuedBuiltinConcat,
  bareBuiltinConcat,
  ["18 2  "],
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
  ["133 3 ", "18 0 lN}"],
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
  ["83 3 ", "44 0 eD]", "6 0 >9|"],
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
  ["33 1 ", "1 0 a,^"],
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
  ["17 3 ", "34 3 ", '76 0 "', "54 2 "],
);

// A leading newline_opt closes when the continuation after it is followed
// by an item. An edit elsewhere re-lexes that continuation after the closed
// node; the marker must still record what follows it, or a later edit that
// replaces the item with a newline reuses the closed node.
const closedLeadingNewline = "\n \\\n x\n";
const reopenedLeadingNewline = "\n \\\n \n\ny";
determinismTest(
  "reopen-leading-newline-after-unrelated-edit",
  closedLeadingNewline,
  reopenedLeadingNewline,
  ["7 0 y", "5 1 \n"],
  (tree) => {
    assert.equal(
      matchingLineCount(tree, /^[ \t0-9:-]+leading: newline_opt/),
      1,
      "Expected one leading newline_opt",
    );
    contains(tree, "line_continuation");
  },
);

const separatedEmptyStatement = lines("BEGIN {", "; x", "}");
const continuedEmptyStatement = lines("BEGIN {", ";\\", "x", "}");
determinismTest(
  "insert-statement-gap-line-continuation",
  separatedEmptyStatement,
  continuedEmptyStatement,
  ["9 1 \\\n"],
  (tree) => {
    contains(tree, "line_continuation");
  },
);
determinismTest(
  "delete-statement-gap-line-continuation",
  continuedEmptyStatement,
  separatedEmptyStatement,
  ["9 2  "],
);
