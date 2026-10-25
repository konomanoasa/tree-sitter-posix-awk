import assert from "node:assert/strict";
import { test } from "node:test";
import {
  clean,
  contains,
  continuationMarker,
  dirty,
  excludes,
  matchingLineCount,
} from "./support/cst.js";
import {
  assertDeterministicEdit,
  assertStatus,
  determinismTest,
  editHistoryTest,
  lines,
  parse,
  parseSummary,
} from "./support/parser.js";
import { normalWorkloads } from "./support/workloads.js";

const longContinuationCount = 16_384;
const longNumberDigits = 65_536;
const getlineSourceFieldCount = 1_000;
const getlineSourceGrowthFields = 20;

function freshTest(name, source, assertions) {
  test(`posix_awk: ${name}`, () => {
    const result = parse(source);
    assertStatus(name, result, 0);
    clean(result.tree);
    assertions(result.tree);
  });
}

freshTest(
  "a long continuation gap preserves a for-in header",
  `{ for (k in array ${"\\\n".repeat(longContinuationCount)}); }`,
  (tree) => {
    contains(tree, "variable: name `k`");
    contains(tree, "array: name `array`");
  },
);

freshTest(
  "a long number remains one leaf",
  `{ print ${"1".repeat(longNumberDigits)} }\n`,
  (tree) => {
    const numbers = [...tree.matchAll(/^0:([0-9]+) +- +0:([0-9]+) +number /gm)];
    assert.deepEqual(
      numbers.map((match) => [Number(match[1]), Number(match[2])]),
      [[8, 8 + longNumberDigits]],
    );
  },
);

for (const { source, spelling, kind, start } of [
  { source: "{ x += y }", spelling: "+=", kind: "add_assign", start: 4 },
  { source: "{ x -= y }", spelling: "-=", kind: "sub_assign", start: 4 },
  { source: "{ x *= y }", spelling: "*=", kind: "mul_assign", start: 4 },
  { source: "{ x /= y }", spelling: "/=", kind: "div_assign", start: 4 },
  { source: "{ x %= y }", spelling: "%=", kind: "mod_assign", start: 4 },
  { source: "{ x ^= y }", spelling: "^=", kind: "pow_assign", start: 4 },
  { source: "{ x || y }", spelling: "||", kind: "or", start: 4 },
  { source: "{ x && y }", spelling: "&&", kind: "and", start: 4 },
  { source: "{ x !~ y }", spelling: "!~", kind: "no_match", start: 4 },
  { source: "{ x == y }", spelling: "==", kind: "eq", start: 4 },
  { source: "{ x <= y }", spelling: "<=", kind: "le", start: 4 },
  { source: "{ x >= y }", spelling: ">=", kind: "ge", start: 4 },
  { source: "{ x != y }", spelling: "!=", kind: "ne", start: 4 },
  { source: "{ print x >> y }", spelling: ">>", kind: "append", start: 10 },
]) {
  test(`posix_awk: ${kind} is contiguous and cannot span a continuation`, () => {
    const result = parse(source);
    assertStatus(kind, result, 0);
    clean(result.tree);
    const leaves = [
      ...result.tree.matchAll(
        /^0:([0-9]+) +- +0:([0-9]+) +(?:operator: )?([a-z_]+) `([^`]*)`$/gm,
      ),
    ]
      .filter((match) => match[3] === kind)
      .map((match) => [Number(match[1]), Number(match[2]), match[4]]);
    assert.deepEqual(leaves, [[start, start + 2, spelling]]);
    const split = `${source.slice(0, start + 1)}\\\n${source.slice(start + 1)}`;
    const separated = parse(split);
    assert.ok(separated.status === 0 || separated.status === 1);
    dirty(separated.tree);
    assert.equal(separated.tree.includes(`${kind} \`${spelling}\``), false);
  });
}

test("posix_awk: blanks before a raw newline end an unterminated literal", () => {
  for (const literal of [
    '"broken',
    "/[[:",
    "/[[.a",
    "/[[=",
    "/[[=a",
    "/a{1",
    "/a{1,",
    "/[[: }",
  ]) {
    for (const blanks of [" ", "\t", " \t"]) {
      const source = lines(
        `BEGIN { x = ${literal}${blanks}`,
        'BEGIN { print "s" ~ /b/ }',
      );
      const result = parse(source);
      dirty(result.tree);
      assert.match(result.tree, /^1:15 +- +1:16 +string_content `s`$/m, source);
      assert.match(
        result.tree,
        /^1:21 +- +1:22 +ordinary_character `b`$/m,
        source,
      );
    }
  }
});

for (const count of [
  getlineSourceFieldCount,
  getlineSourceFieldCount + getlineSourceGrowthFields,
]) {
  const innerSource = `1${" + $a".repeat(count)} + $y--`;
  const source = `{ getline $getline < ${innerSource} < 1 < 2 }`;
  freshTest(
    `getline source ownership survives ${count} intervening fields`,
    source,
    (tree) => {
      const sources = [
        ...tree.matchAll(/^0:([0-9]+) +- +0:([0-9]+) +source: expr$/gm),
      ].map((match) => source.slice(Number(match[1]), Number(match[2])));
      assert.deepEqual(sources, [innerSource, "1"]);
    },
  );
}

determinismTest(
  "semicolons change for-in to a membership expression in a classic for",
  "{ for (k in a); }",
  "{ for (k in a;;); }",
  [{ byte: 13, deleteBytes: 0, insert: ";;" }],
  (tree) => {
    contains(tree, "initializer: simple_statement");
    excludes(tree, "variable: name");
  },
);

determinismTest(
  "removing semicolons changes a classic for to for-in",
  "{ for (k in a;;); }",
  "{ for (k in a); }",
  [{ byte: 13, deleteBytes: 2, insert: "" }],
  (tree) => {
    contains(tree, "variable: name `k`");
    contains(tree, "array: name `a`");
    excludes(tree, "initializer: simple_statement");
  },
);

determinismTest(
  "nested input source ownership is restored with a getline keyword",
  lines("{$getlin y < $getline y < $getline y < $getline y < $getline y < x}"),
  lines("{$getline y < $getline y < $getline y < $getline y < $getline y < x}"),
  [{ byte: 8, deleteBytes: 0, insert: "e" }],
  (tree) => {
    assert.equal(matchingLineCount(tree, /target: lvalue$/), 5, tree);
    assert.equal(matchingLineCount(tree, /source: expr$/), 5, tree);
  },
);

for (const { label, initial, final, before, inserted } of [
  {
    label: "function body after a continuation and comment",
    initial: lines("before {}", "function f() \\", "#", "END {}"),
    final: lines("before {}", "function f() \\", "#", "{}", "END {}"),
    before: "END",
    inserted: "{}\n",
  },
  {
    label: "function body after a continuation and newline",
    initial: lines("before {}", "function f() \\", "", "END {}"),
    final: lines("before {}", "function f() \\", "", "{}", "END {}"),
    before: "END",
    inserted: "{}\n",
  },
  {
    label: "special pattern action before a comment's newline",
    initial: lines("before {}", "BEGIN \\", "#", "{}", "END {}"),
    final: lines("before {}", "BEGIN \\", "{} #", "{}", "END {}"),
    before: "#",
    inserted: "{} ",
  },
]) {
  test(`posix_awk: restoring a missing ${label} matches a fresh parse`, () => {
    const tree = assertDeterministicEdit(label, initial, final, [
      { byte: initial.indexOf(before), deleteBytes: 0, insert: inserted },
    ]);
    contains(tree, continuationMarker);
  });
}

for (const { name, initial, final, edits, update } of [
  {
    name: "a getline target becomes a concatenated postfix increment",
    initial: "getline x",
    final: "getline x++",
    edits: [{ byte: 17, deleteBytes: 0, insert: "++" }],
    update: "incr",
  },
  {
    name: "a getline array target becomes a concatenated postfix decrement",
    initial: "getline a[1]",
    final: "getline a[1]--",
    edits: [{ byte: 20, deleteBytes: 0, insert: "--" }],
    update: "decr",
  },
  {
    name: "a getline field target becomes a concatenated postfix increment",
    initial: "getline $i",
    final: "getline $i++",
    edits: [{ byte: 18, deleteBytes: 0, insert: "++" }],
    update: "incr",
  },
  {
    name: "a piped getline target becomes a concatenated postfix decrement",
    initial: "command | getline x",
    final: "command | getline x--",
    edits: [{ byte: 27, deleteBytes: 0, insert: "--" }],
    update: "decr",
  },
]) {
  determinismTest(
    name,
    lines(`BEGIN { ${initial} }`),
    lines(`BEGIN { ${final} }`),
    edits,
    (tree) => {
      contains(tree, `operator: ${update}`);
      excludes(tree, "target:");
    },
  );
}

determinismTest(
  "removing a postfix update restores the getline target",
  lines("BEGIN { getline x++ }"),
  lines("BEGIN { getline x }"),
  [{ byte: 17, deleteBytes: 2, insert: "" }],
  (tree) => contains(tree, "target: lvalue"),
);

for (const { name, initial, final, edits, number } of [
  {
    name: "joining a floating suffix replaces concatenation with one number",
    initial: "1.0 f",
    final: "1.0f",
    edits: [{ byte: 17, deleteBytes: 1, insert: "" }],
    number: "1.0f",
  },
  {
    name: "completing an exponent includes its floating suffix",
    initial: "1.0eF",
    final: "1.0e2F",
    edits: [{ byte: 18, deleteBytes: 0, insert: "2" }],
    number: "1.0e2F",
  },
  {
    name: "adding a decimal point makes the following letter a floating suffix",
    initial: "1l",
    final: "1.l",
    edits: [{ byte: 15, deleteBytes: 0, insert: "." }],
    number: "1.l",
  },
  {
    name: "separating a floating suffix restores concatenation",
    initial: "1.L",
    final: "1. L",
    edits: [{ byte: 16, deleteBytes: 0, insert: " " }],
    number: "1.",
  },
]) {
  determinismTest(
    name,
    lines(`BEGIN { print ${initial} }`),
    lines(`BEGIN { print ${final} }`),
    edits,
    (tree) => contains(tree, `number \`${number}\``),
  );
}

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
    [{ byte: initial.indexOf("+"), deleteBytes: 1, insert: " " }],
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

const membershipBeforeLogicalAnd = lines("BEGIN { x = (a in b) && c }");

const membershipBeforeMatch = lines("BEGIN { x = (a in b) ~ c }");

determinismTest(
  "membership logical and to match",
  membershipBeforeLogicalAnd,
  membershipBeforeMatch,
  [{ byte: 21, deleteBytes: 2, insert: "~" }],
  (tree) => {
    contains(tree, '"~"');
    excludes(tree, "operator: and");
  },
);

determinismTest(
  "membership match to logical and",
  membershipBeforeMatch,
  membershipBeforeLogicalAnd,
  [{ byte: 21, deleteBytes: 1, insert: "&&" }],
  (tree) => {
    contains(tree, "operator: and");
    excludes(tree, '"~"');
  },
);

const plainMembership = lines("BEGIN { x = (a in b) }");

const concatenatedMembership = lines("BEGIN { x = (a in b) c }");

determinismTest(
  "insert membership concatenation operand",
  plainMembership,
  concatenatedMembership,
  [{ byte: 20, deleteBytes: 0, insert: " c" }],
  (tree) => {
    assert.match(tree, /^[ \t0-9:-]+right: name `b`$/m);
    assert.match(tree, /^[ \t0-9:-]+right: non_unary_expr$/m);
  },
);

const plainBracketList = lines("BEGIN { print /[+]?[a]/ }");

const trailingHyphenBracketList = lines("BEGIN { print /[+-]?[a]/ }");

determinismTest(
  "insert trailing bracket hyphen",
  plainBracketList,
  trailingHyphenBracketList,
  [{ byte: 17, deleteBytes: 0, insert: "-" }],
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
  "insert range ending hyphen",
  shortRangeEre,
  hyphenEndedRangeEre,
  [{ byte: 18, deleteBytes: 0, insert: "-" }],
  (tree) => {
    assert.equal(
      matchingLineCount(tree, /^[ \t0-9:-]+range_expression$/),
      1,
      "Expected one range expression ending at the inserted hyphen",
    );
    assert.equal(
      matchingLineCount(tree, /^[ \t0-9:-]+collating_element_single `/),
      2,
      "Expected the range start and the following element only",
    );
  },
);

determinismTest(
  "delete range ending hyphen",
  hyphenEndedRangeEre,
  shortRangeEre,
  [{ byte: 18, deleteBytes: 1, insert: "" }],
  (tree) => {
    assert.equal(
      matchingLineCount(tree, /^[ \t0-9:-]+collating_element_single `/),
      2,
      "Expected a plain range between two collating elements",
    );
  },
);

determinismTest(
  "deleting the ERE body removes its expression node",
  "/a/",
  "//",
  [{ byte: 1, deleteBytes: 1, insert: "" }],
  (tree) => {
    contains(tree, "ere");
    excludes(tree, "extended_reg_exp");
  },
);

determinismTest(
  "inserting an ERE body creates its expression node",
  "//",
  "/a/",
  [{ byte: 1, deleteBytes: 0, insert: "a" }],
  (tree) => contains(tree, "expression: extended_reg_exp"),
);

determinismTest(
  "a closing slash completes an empty ERE at EOF",
  "/",
  "//",
  [{ byte: 1, deleteBytes: 0, insert: "/" }],
  (tree) => {
    contains(tree, "ere");
    excludes(tree, "extended_reg_exp");
  },
);

determinismTest(
  "deleting an ERE group body leaves only its delimiters",
  "/(a)/",
  "/()/",
  [{ byte: 2, deleteBytes: 1, insert: "" }],
  (tree) => {
    assert.equal(matchingLineCount(tree, /expression: extended_reg_exp$/), 1);
    excludes(tree, "ordinary_character");
  },
);

determinismTest(
  "inserting an ERE group body restores its expression field",
  "/()/",
  "/(a)/",
  [{ byte: 2, deleteBytes: 0, insert: "a" }],
  (tree) => {
    assert.equal(matchingLineCount(tree, /expression: extended_reg_exp$/), 2);
    contains(tree, "ordinary_character");
  },
);

determinismTest(
  "escaping an empty ERE group's closer moves it into the group body",
  lines("/())/"),
  lines(String.raw`/(\))/`),
  [{ byte: 2, deleteBytes: 0, insert: "\\" }],
  (tree) => contains(tree, "quoted_character"),
);

determinismTest(
  "unescaping an ERE group's closer leaves the unmatched parenthesis outside",
  lines(String.raw`/(\))/`),
  lines("/())/"),
  [{ byte: 2, deleteBytes: 1, insert: "" }],
  (tree) => {
    excludes(tree, "quoted_character");
    contains(tree, "ordinary_character");
  },
);

editHistoryTest(
  "an ERE group opening reclassifies its closing parenthesis and back",
  lines("/a))/"),
  [{ byte: 1, deleteBytes: 0, insert: "(" }],
);

const plainEreDelimiter = lines("BEGIN { print /ab/ }");

const escapedEreDelimiter = lines(String.raw`BEGIN { print /ab\/c/ }`);

determinismTest(
  "escape ERE closing slash",
  plainEreDelimiter,
  escapedEreDelimiter,
  [{ byte: 17, deleteBytes: 1, insert: String.raw`\/c/` }],
  (tree) => {
    contains(tree, "escaped_delimiter");
  },
);

determinismTest(
  "restore ERE closing slash",
  escapedEreDelimiter,
  plainEreDelimiter,
  [{ byte: 17, deleteBytes: 4, insert: "/" }],
  (tree) => excludes(tree, "escaped_delimiter"),
);

const threeDigitOctalEre = lines(String.raw`BEGIN { print /\124/ }`);

const octalFollowedByCharacterEre = lines(String.raw`BEGIN { print /\1234/ }`);

determinismTest(
  "split ERE octal at three digits",
  threeDigitOctalEre,
  octalFollowedByCharacterEre,
  [{ byte: 18, deleteBytes: 0, insert: "3" }],
  (tree) => {
    contains(tree, "escape_sequence");
    assert.match(tree, /^[ \t0-9:-]+ordinary_character `/m);
  },
);

determinismTest(
  "join ERE octal at three digits",
  octalFollowedByCharacterEre,
  threeDigitOctalEre,
  [{ byte: 18, deleteBytes: 1, insert: "" }],
  (tree) => {
    contains(tree, "escape_sequence");
    excludes(tree, "ordinary_character");
  },
);

const greedyEre = lines("BEGIN { print /a*/ }");

const shortestEre = lines("BEGIN { print /a*?/ }");

determinismTest(
  "insert ERE repetition modifier",
  greedyEre,
  shortestEre,
  [{ byte: 17, deleteBytes: 0, insert: "?" }],
  (tree) => contains(tree, "repetition_modifier"),
);

determinismTest(
  "delete ERE repetition modifier",
  shortestEre,
  greedyEre,
  [{ byte: 17, deleteBytes: 1, insert: "" }],
  (tree) => excludes(tree, "repetition_modifier"),
);

const equivalenceEre = lines("BEGIN { print /[[=a=]]/ }");

determinismTest(
  "a collating caret becomes a meta character when replaced by a hyphen",
  lines("/[[.^.]]/"),
  lines("/[[.-.]]/"),
  [{ byte: 4, deleteBytes: 1, insert: "-" }],
  (tree) => contains(tree, "meta_character `-`"),
);

determinismTest(
  "an equivalence class accepts a caret in place of a raw hyphen",
  lines("/[[=-=]]/"),
  lines("/[[=^=]]/"),
  [{ byte: 4, deleteBytes: 1, insert: "^" }],
  (tree) => {
    contains(tree, "equivalence_class");
    contains(tree, "collating_element_single `^`");
    excludes(tree, "meta_character");
  },
);

determinismTest(
  "a multi-character equivalence payload can begin with a closing bracket",
  lines("/[[=]=]]/"),
  lines("/[[=]a=]]/"),
  [{ byte: 5, deleteBytes: 0, insert: "a" }],
  (tree) => {
    contains(tree, "equivalence_class");
    contains(tree, "collating_element_multi");
    excludes(tree, "meta_character");
  },
);

for (const marker of [".", "="]) {
  const plain = lines(`/[[${marker}aé日${marker}]]/`);
  const escaped = lines(`/[[${marker}a\\né日${marker}]]/`);
  determinismTest(
    `inserting the first ${marker} compound escape splits the raw payload`,
    plain,
    escaped,
    [{ byte: 5, deleteBytes: 0, insert: String.raw`\n` }],
    (tree) => {
      contains(tree, "element: collating_element\n");
      contains(tree, "collating_element_content `a`");
      contains(tree, "collating_element_content `é`");
      contains(tree, "collating_element_content `日`");
      excludes(tree, "collating_element_multi");
    },
  );
  determinismTest(
    `deleting the last ${marker} compound escape restores one raw payload leaf`,
    escaped,
    plain,
    [{ byte: 5, deleteBytes: 2, insert: "" }],
    (tree) => {
      contains(tree, "element: collating_element_multi `aé日`");
      excludes(tree, "collating_element_content");
    },
  );
  determinismTest(
    `extending a single Unicode ${marker} compound payload changes its lexical class`,
    lines(`/[[${marker}é${marker}]]/`),
    lines(`/[[${marker}é日${marker}]]/`),
    [{ byte: 6, deleteBytes: 0, insert: "日" }],
    (tree) => contains(tree, "element: collating_element_multi `é日`"),
  );
}

for (const { name, escaped, plain } of [
  {
    name: "collating symbol",
    escaped: lines(String.raw`/[[.a\né\141 \e\/日.]]/`),
    plain: lines(String.raw`/[[.aé\141 \e\/日.]]/`),
  },
  {
    name: "equivalence class",
    escaped: lines(String.raw`/[[=a\né\141 \e\/日=]]/`),
    plain: lines(String.raw`/[[=aé\141 \e\/日=]]/`),
  },
]) {
  determinismTest(
    `inserting a ${name} escape preserves neighboring Unicode content leaves`,
    plain,
    escaped,
    [{ byte: 5, deleteBytes: 0, insert: String.raw`\n` }],
    (tree) => {
      contains(tree, "collating_element_content `a`");
      contains(tree, "collating_element_content `é`");
      contains(tree, "collating_element_content `日`");
    },
  );
  determinismTest(
    `deleting a ${name} escape preserves neighboring Unicode content leaves`,
    escaped,
    plain,
    [{ byte: 5, deleteBytes: 2, insert: "" }],
  );
  editHistoryTest(
    `a ${name} regains its content leaves after delimiter and Unicode repairs`,
    escaped,
    [
      { byte: 21, deleteBytes: 1, insert: "" },
      { byte: 16, deleteBytes: 1, insert: "" },
      { byte: 7, deleteBytes: 2, insert: "🙂" },
    ],
  );
}

const classEre = lines("BEGIN { print /[[:alpha:]]/ }");

for (const [kind, marker] of [
  ["collating symbol", "."],
  ["equivalence class", "="],
]) {
  determinismTest(
    `inserting an escape repairs an empty ${kind}`,
    `/[[${marker}${marker}]]/`,
    `/[[${marker}\\n${marker}]]/`,
    [{ byte: 4, deleteBytes: 0, insert: String.raw`\n` }],
    (tree) => contains(tree, "escape_sequence"),
  );
}

determinismTest(
  "changing compound markers restores the correct lexical mode",
  "/[[.alpha.]]/",
  "/[[:alpha:]]/",
  [
    { byte: 3, deleteBytes: 1, insert: ":" },
    { byte: 9, deleteBytes: 1, insert: ":" },
  ],
  (tree) => contains(tree, "class_name `alpha`"),
);

for (const [first, second, operand, kind] of [
  ["/", "=", "5", "div_assign"],
  ["+", "=", "5", "add_assign"],
  ["-", "=", "5", "sub_assign"],
  ["*", "=", "5", "mul_assign"],
  ["%", "=", "5", "mod_assign"],
  ["^", "=", "5", "pow_assign"],
  ["|", "|", "5", "or"],
  ["&", "&", "5", "and"],
  ["+", "+", "", "incr"],
  ["-", "-", "", "decr"],
]) {
  determinismTest(
    `removing a continuation joins ${kind} characters into one token`,
    `{x${first}\\\n${second}${operand}}`,
    `{x${first}${second}${operand}}`,
    [{ byte: 3, deleteBytes: 2, insert: "" }],
    (tree) => {
      contains(tree, `operator: ${kind}`);
      excludes(tree, continuationMarker);
    },
  );
}

for (const [marker, kind] of [
  ["=", "equivalence_class"],
  [".", "collating_symbol"],
]) {
  determinismTest(
    `removing an internal continuation repairs a ${kind} closer`,
    `/[[${marker}a${marker}\\\n]]/`,
    `/[[${marker}a${marker}]]/`,
    [{ byte: 6, deleteBytes: 2, insert: "" }],
    (tree) => {
      contains(tree, kind);
      excludes(tree, continuationMarker);
    },
  );
}

determinismTest(
  "equivalence to character class",
  equivalenceEre,
  classEre,
  [{ byte: 16, deleteBytes: 5, insert: "[:alpha:]" }],
  (tree) => {
    contains(tree, "character_class");
    contains(tree, "class_name");
    excludes(tree, "equivalence_class");
  },
);

determinismTest(
  "character class to equivalence",
  classEre,
  equivalenceEre,
  [{ byte: 16, deleteBytes: 9, insert: "[=a=]" }],
  (tree) => {
    contains(tree, "equivalence_class");
    excludes(tree, "character_class");
  },
);

const literalOpenBracketEre = lines("BEGIN { print /[[]/ }");

const collatingSymbolEre = lines("BEGIN { print /[[.x.]]/ }");

determinismTest(
  "literal bracket to collating symbol",
  literalOpenBracketEre,
  collatingSymbolEre,
  [{ byte: 17, deleteBytes: 0, insert: ".x.]" }],
  (tree) => {
    contains(tree, "collating_symbol");
  },
);

determinismTest(
  "collating symbol to literal bracket",
  collatingSymbolEre,
  literalOpenBracketEre,
  [{ byte: 17, deleteBytes: 4, insert: "" }],
  (tree) => {
    contains(tree, "collating_element_single");
    excludes(tree, "collating_symbol");
  },
);

const outputPipe = lines("BEGIN { print value | command }");

const logicalOrPrint = lines("BEGIN { print value || command }");

determinismTest(
  "output pipe to logical or",
  outputPipe,
  logicalOrPrint,
  [{ byte: 21, deleteBytes: 0, insert: "|" }],
  (tree) => {
    contains(tree, "operator: or");
    excludes(tree, "output_redirection");
  },
);

const printArguments = lines("BEGIN { print (key, index) }");

const printMembership = lines("BEGIN { print (key, index) in table }");

determinismTest(
  "print arguments to tuple membership",
  printArguments,
  printMembership,
  [{ byte: 26, deleteBytes: 0, insert: " in table" }],
  (tree) => {
    contains(tree, "arguments: print_expr_list");
    contains(tree, "left: multiple_expr_list");
  },
);

determinismTest(
  "tuple membership to print arguments",
  printMembership,
  printArguments,
  [{ byte: 26, deleteBytes: 9, insert: "" }],
  (tree) => {
    contains(tree, "arguments: multiple_expr_list");
    excludes(tree, "print_expr_list");
  },
);

const plainAppend = lines("BEGIN { print value >> archive }");

const continuedAppend = lines("BEGIN { print value \\", ">> archive }");

determinismTest(
  "insert append line continuation",
  plainAppend,
  continuedAppend,
  [{ byte: 20, deleteBytes: 0, insert: "\\\n" }],
  (tree) => {
    contains(tree, "redirection: output_redirection");
    contains(tree, "append");
    contains(tree, continuationMarker);
  },
);

const sameLineItems = lines("BEGIN {} END {}");

const separatedItems = lines("BEGIN {}", "END {}");

determinismTest(
  "insert item terminator between items",
  sameLineItems,
  separatedItems,
  [{ byte: 8, deleteBytes: 1, insert: "\n" }],
  (tree) => contains(tree, "terminator: terminator"),
);

const blankCall = lines("BEGIN { f (value) }");

const continuedCall = lines("BEGIN { f\\", "(value) }");

determinismTest(
  "blank to line continuation preserves concatenation",
  blankCall,
  continuedCall,
  [{ byte: 9, deleteBytes: 1, insert: "\\\n" }],
  (tree) => {
    contains(tree, "name `f`");
    excludes(tree, "func_name");
    contains(tree, continuationMarker);
  },
);

const plainAddAssign = lines("BEGIN { value += other }");

const continuedAddAssign = lines("BEGIN { value \\", "+= other }");

determinismTest(
  "insert add assign line continuation",
  plainAddAssign,
  continuedAddAssign,
  [{ byte: 14, deleteBytes: 0, insert: "\\\n" }],
  (tree) => {
    contains(tree, "add_assign");
    contains(tree, continuationMarker);
  },
);

const plainAdditive = lines("BEGIN { left + right }");

const continuedAdditive = lines("BEGIN { left\\", "+ right }");

determinismTest(
  "insert additive operator line continuation",
  plainAdditive,
  continuedAdditive,
  [{ byte: 12, deleteBytes: 1, insert: "\\\n" }],
  (tree) => {
    contains(tree, '"+"');
    contains(tree, continuationMarker);
  },
);

const plainComparison = lines("BEGIN { left < right }");

const continuedComparison = lines("BEGIN { left\\", "< right }");

determinismTest(
  "insert comparison operator line continuation",
  plainComparison,
  continuedComparison,
  [{ byte: 12, deleteBytes: 1, insert: "\\\n" }],
  (tree) => {
    contains(tree, '"<"');
    contains(tree, continuationMarker);
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

determinismTest(
  "continued getline redirect to comparison",
  continuedGetlineRedirect,
  continuedGetlineComparison,
  [{ byte: 25, deleteBytes: 0, insert: "=" }],
  (tree) => {
    contains(tree, "operator: le");
    excludes(tree, "source: expr");
    contains(tree, continuationMarker);
  },
);

const plainConditional = lines("BEGIN { condition ? yes : no }");

const continuedConditional = lines("BEGIN { condition ? yes\\", ": no }");

determinismTest(
  "insert conditional colon line continuation",
  plainConditional,
  continuedConditional,
  [{ byte: 23, deleteBytes: 1, insert: "\\\n" }],
  (tree) => {
    contains(tree, "alternative: expr");
    contains(tree, continuationMarker);
  },
);

const plainLogical = lines("BEGIN { left && right }");

const continuedLogical = lines("BEGIN { left\\", "&& right }");

determinismTest(
  "insert logical operator line continuation",
  plainLogical,
  continuedLogical,
  [{ byte: 12, deleteBytes: 1, insert: "\\\n" }],
  (tree) => {
    contains(tree, "and");
    contains(tree, continuationMarker);
  },
);

const plainPipeGetline = lines("BEGIN { source | getline target }");

const continuedPipeGetline = lines("BEGIN { source\\", "| getline target }");

determinismTest(
  "insert input pipe line continuation",
  plainPipeGetline,
  continuedPipeGetline,
  [{ byte: 14, deleteBytes: 1, insert: "\\\n" }],
  (tree) => {
    contains(tree, "non_unary_input_function");
    contains(tree, continuationMarker);
  },
);

const unaryPipeGetline = lines("BEGIN { -source | getline target }");

const fieldPipeGetline = lines("BEGIN { $source | getline target }");

determinismTest(
  "unary to field pipe getline",
  unaryPipeGetline,
  fieldPipeGetline,
  [{ byte: 8, deleteBytes: 1, insert: "$" }],
  (tree) => {
    assert.match(tree, /^[ \t0-9:-]*non_unary_input_function$/m);
  },
);

const hashString = lines('BEGIN { print "#"', "}");

const hashComment = lines('BEGIN { print #"', "}");

determinismTest(
  "string to comment",
  hashString,
  hashComment,
  [{ byte: 14, deleteBytes: 1, insert: "" }],
  (tree) => {
    contains(tree, "comment");
    excludes(tree, "string");
  },
);

determinismTest(
  "comment to string",
  hashComment,
  hashString,
  [{ byte: 14, deleteBytes: 0, insert: '"' }],
  (tree) => {
    contains(tree, "string_content");
    excludes(tree, "comment");
  },
);

const hashEre = lines("BEGIN { print /#/", "}");

const hashEreComment = lines("BEGIN { print #/", "}");

determinismTest(
  "ERE to comment",
  hashEre,
  hashEreComment,
  [{ byte: 14, deleteBytes: 1, insert: "" }],
  (tree) => {
    contains(tree, "comment");
    excludes(tree, "ere");
  },
);

determinismTest(
  "comment to ERE",
  hashEreComment,
  hashEre,
  [{ byte: 14, deleteBytes: 0, insert: "/" }],
  (tree) => {
    assert.match(tree, /^[ \t0-9:-]+ordinary_character `/m);
    excludes(tree, "comment");
  },
);

const plainEscape = lines('BEGIN { print "an" }');

const backslashEscape = lines(String.raw`BEGIN { print "a\n" }`);

determinismTest(
  "insert string escape backslash",
  plainEscape,
  backslashEscape,
  [{ byte: 16, deleteBytes: 0, insert: "\\" }],
  (tree) => contains(tree, "escape_sequence"),
);

determinismTest(
  "delete string escape backslash",
  backslashEscape,
  plainEscape,
  [{ byte: 16, deleteBytes: 1, insert: "" }],
  (tree) => excludes(tree, "escape_sequence"),
);

const topLevelErePattern = lines("/ready/ { print }");

const topLevelDivisionPattern = lines("total / count { print }");

determinismTest(
  "top level ERE to division pattern",
  topLevelErePattern,
  topLevelDivisionPattern,
  [{ byte: 0, deleteBytes: 7, insert: "total / count" }],
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
  "top level division to ERE pattern",
  topLevelDivisionPattern,
  topLevelErePattern,
  [{ byte: 0, deleteBytes: 13, insert: "/ready/" }],
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
  "adjacent to continued spaced function name",
  adjacentFunctionName,
  continuedSpacedFunctionName,
  [{ byte: 16, deleteBytes: 0, insert: " \\\n" }],
  (tree) => {
    assert.match(tree, /^[ \t0-9:-]*name:[ \t]+name[ \t]+`compute`$/m);
    excludes(tree, "func_name");
    contains(tree, continuationMarker);
  },
);

determinismTest(
  "continued spaced to adjacent function name",
  continuedSpacedFunctionName,
  adjacentFunctionName,
  [{ byte: 16, deleteBytes: 3, insert: "" }],
  (tree) => {
    assert.match(tree, /^[ \t0-9:-]*name:[ \t]+func_name[ \t]+`compute`$/m);
    excludes(tree, continuationMarker);
  },
);

const compactRangePattern = lines("start,stop {}");

const multilineRangePattern = lines("start,", "stop {}");

determinismTest(
  "insert range pattern newline",
  compactRangePattern,
  multilineRangePattern,
  [{ byte: 6, deleteBytes: 0, insert: "\n" }],
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
  "delete range pattern newline",
  multilineRangePattern,
  compactRangePattern,
  [{ byte: 6, deleteBytes: 1, insert: "" }],
  (tree) => {
    excludes(tree, "newline_opt");
  },
);

const separatedClosedItems = "BEGIN {}\nEND {}";

const directOpenActionItem = "BEGIN {END {}";

determinismTest(
  "restore action close and item terminator",
  directOpenActionItem,
  separatedClosedItems,
  [{ byte: 7, deleteBytes: 0, insert: "}\n" }],
  (tree) => {
    contains(tree, "terminator: terminator");
  },
);

const bareBuiltinConcat = lines('BEGIN { x = length "" }');

const continuedBuiltinConcat = lines("BEGIN { x = length\\", '"" }');

determinismTest(
  "insert builtin concat line continuation",
  bareBuiltinConcat,
  continuedBuiltinConcat,
  [{ byte: 18, deleteBytes: 1, insert: "\\\n" }],
  (tree) => {
    contains(tree, "builtin_func_name");
    contains(tree, continuationMarker);
  },
);

const separatedEmptyStatement = lines("BEGIN {", "; x", "}");

const continuedEmptyStatement = lines("BEGIN {", ";\\", "x", "}");

determinismTest(
  "insert statement gap line continuation",
  separatedEmptyStatement,
  continuedEmptyStatement,
  [{ byte: 9, deleteBytes: 1, insert: "\\\n" }],
  (tree) => {
    contains(tree, continuationMarker);
  },
);

determinismTest(
  "a parameter newline becomes a line continuation",
  lines("function f(first,", "second) {}"),
  lines("function f(first,\\", "second) {}"),
  [{ byte: 17, deleteBytes: 0, insert: "\\" }],
  (tree) => contains(tree, continuationMarker),
);

determinismTest(
  "a parameter continuation becomes a raw newline",
  lines("function f(first,\\", "second) {}"),
  lines("function f(first,", "second) {}"),
  [{ byte: 17, deleteBytes: 1, insert: "" }],
  (tree) => {
    contains(tree, "parameters: param_list");
    contains(tree, "newline_opt");
    excludes(tree, continuationMarker);
  },
);

determinismTest(
  "removing a parameter newline preserves a valid function",
  lines("function f(first,", "second) {}"),
  lines("function f(first,second) {}"),
  [{ byte: 17, deleteBytes: 1, insert: "" }],
  (tree) => excludes(tree, "newline_opt"),
);

determinismTest(
  "inserting a parameter newline preserves a valid function",
  lines("function f(first,second) {}"),
  lines("function f(first,", "second) {}"),
  [{ byte: 17, deleteBytes: 0, insert: "\n" }],
  (tree) => contains(tree, "newline_opt"),
);

determinismTest(
  "restoring a parameter after a newline repairs the function header",
  lines("function f(first,", ") {}"),
  lines("function f(first,", "second) {}"),
  [{ byte: 18, deleteBytes: 0, insert: "second" }],
  (tree) => {
    contains(tree, "parameters: param_list");
    contains(tree, "newline_opt");
  },
);

for (const { name, initial, changed, removed, inserted, expected } of [
  {
    name: "editing a name after a continuation keeps the preceding name separate",
    initial: lines("BE\\", "GIN { print 1 }"),
    changed: lines("BE\\", "GUN { print 1 }"),
    removed: "GIN",
    inserted: "GUN",
    expected: "name",
  },
  {
    name: "editing a call name after a continuation keeps the preceding name separate",
    initial: lines("BEGIN { print len\\", "gth(x) }"),
    changed: lines("BEGIN { print len\\", "gtx(x) }"),
    removed: "gth",
    inserted: "gtx",
    expected: "func_name",
  },
  {
    name: "editing a name after a number keeps the continuation outside the number",
    initial: lines("BEGIN { print 1\\", "e+2 }"),
    changed: lines("BEGIN { print 1\\", "e+x }"),
    removed: "e+2",
    inserted: "e+x",
    expected: "number `1`",
  },
]) {
  determinismTest(
    name,
    initial,
    changed,
    [
      {
        byte: initial.indexOf(removed),
        deleteBytes: removed.length,
        insert: inserted,
      },
    ],
    (tree) => contains(tree, expected),
  );
  determinismTest(`${name} and changes back`, changed, initial, [
    {
      byte: changed.indexOf(inserted),
      deleteBytes: inserted.length,
      insert: removed,
    },
  ]);
}

for (const { operator, replacementByte } of [
  { operator: "<", replacementByte: 47 },
  { operator: "~", replacementByte: 47 },
  { operator: "&&", replacementByte: 48 },
  { operator: "||", replacementByte: 48 },
]) {
  determinismTest(
    `replacing an update extends the ${operator} operand following a getline source`,
    `{ getline $getline < x ~ y | getline z ${operator} "f" ++ }\n`,
    `{ getline $getline < x ~ y | getline z++ ${operator} "f" getline }\n`,
    [
      { byte: 38, deleteBytes: 0, insert: "++" },
      { byte: replacementByte, deleteBytes: 2, insert: "getline" },
    ],
    (tree) => {
      assert.match(tree, /^0:21 +- +0:22 +source: expr$/m);
      assert.equal(matchingLineCount(tree, /target: lvalue$/), 1);
    },
  );
}

const validExpressions = [
  "-getline x++",
  "a + getline x++",
  "a getline x++",
  "q = $-getline x++",
  "a ^ getline x++",
  "getline $-getline x++",
  "++$getline x--",
  "print $-getline x++ tline y-- z",
  "x getline y++ | getline",
  "print $getline x++",
  "print $-getline a[1]-- ++y z",
  "getline $-x++",
  "getline (x)",
  '$getline < "f" ++',
  '$getline < "f" --',
  '$getline < ("f") ++',
  "$getline < -1 ++",
  "$getline < f() ++",
  "$getline < /f/ ++",
  "$getline < ++y ++",
  '$getline x < "f" ++',
  '$getline $-x < "f" ++',
  'print $getline < "f" ++',
  'printf $getline $-x < "f" --',
  '$getline < "f" ++ | getline',
  '$getline < "f" ++y',
  "$getline < y++",
  "$getline < y++++",
  "$getline < y + 1 ++",
  '$getline < (x = "f") ++',
  '$getline < (a < "f") ++',
  "$getline < getline x++",
  "$getline < (a < getline x++) < y",
  "$getline < (a < getline x--) < y",
  "$getline < $getline < getline x++++",
  "$getline < $getline < $getline < getline x++++++",
  "getline $getline < (a < getline x++) < y",
  "print ($getline < (a < getline x++) < y)",
  "f($getline < (a < getline x++) < y)",
  "x[$getline < (a < getline x++) < y]",
  "$getline < f(getline x++)++ < y",
  '$getline $getline < "f" ++',
  'getline $getline $-x < "f" ++',
  'getline line < dir "/" name',
  "getline < file > 0",
  "getline x++ $getline y++",
  "getline a[i/2]++ y",
  "getline a[i++ / 2]++",
  'getline $getline < "f"',
  'getline $getline < "f" ++',
  "getline $getline < x++",
  "getline $getline < x++++",
  "getline $getline < (x | getline) ++",
  "getline $getline < (a ? x : (y))++",
  "getline $getline < 1e++",
  "getline $getline < a && # source\n x++",
  "getline $getline < (1 &&\\\n\n1)++",
  "getline $getline < (1 ||\\\n# source\n1)--",
  "getline $getline < (1 && # first\n\\\n# second\n1)++",
  "getline x++ $getline $getline < 1 ++",
  "getline $getline < getline $getline < x++++",
  "getline $getline < (($getline < 1 in a) 1)++",
  'getline $getline < (($getline < x in a) < "f") ++',
  "getline $getline < (x < (y < z | getline)) ++",
  "getline $getline < (x ~ (y ~ 1 | getline)) ++",
  'getline $getline < (x ~ y | getline z ~ "f") ++',
  "getline $getline $-x < (x | getline) ++",
  "$getline $-getline < 1 ++",
  "$getline $!getline < x -- ++",
  "$getline $--$getline < 1 ++",
  "getline $getline $-getline < 1 ++",
  "getline $getline $!getline < x -- ++",
  "getline $getline $--$getline < 1 ++",
  "$getline < ((x) in a) -- x ++",
  "getline $getline < ((x) in a) -- x ++",
];

for (const expression of validExpressions) {
  test(`posix_awk: each single-byte repair restores ${expression}`, () => {
    const source = `{ ${expression} }\n`;
    const fresh = parse(source);
    assertStatus(expression, fresh, 0);
    clean(fresh.tree);
    for (let byte = 0; byte < source.length; byte += 1) {
      const initial = source.slice(0, byte) + source.slice(byte + 1);
      assertDeterministicEdit(
        `restore byte ${byte} in getline expression`,
        initial,
        source,
        [{ byte, deleteBytes: 0, insert: source[byte] }],
      );
    }
  });
}

for (const [label, before, after] of [
  ["a target update", "{ getline x }", "{ getline x++ }"],
  ["a field update", "{ $getline x }", "{ $getline x++ }"],
  ["a nested source", "{ $(getline $(x)) }", "{ $(getline $(x) < y) }"],
  ["an update inside a target", "{ getline $(x) }", "{ getline $(x++) }"],
  [
    "a pipe inside a print condition",
    "{ print a ? (b) : c }",
    "{ print a ? (b | getline) : c }",
  ],
]) {
  let byte = 0;
  while (before[byte] === after[byte]) byte++;
  const insert = after.slice(byte, byte + after.length - before.length);
  const edit = { byte, deleteBytes: 0, insert };
  determinismTest(
    `expression selection changes after inserting ${label}`,
    before,
    after,
    [edit],
  );
  editHistoryTest(
    `expression selection survives undo and redo of ${label}`,
    before,
    [edit],
  );
}

for (const [label, broken] of [
  ["a separate action", "{ $a[] ++ }\n{ $getline x++ }"],
  ["the preceding statement", "{ $a[] ++; $getline x++ }"],
]) {
  const repaired = broken.replace("[]", "[1]");
  determinismTest(`repair ${label} before a getline field`, broken, repaired, [
    { byte: 5, deleteBytes: 0, insert: "1" },
  ]);
  editHistoryTest(`undo and redo the repair in ${label}`, repaired, [
    { byte: 5, deleteBytes: 1, insert: "" },
  ]);
}

{
  const prefix = `{ getline $getline < 1${" + $a".repeat(getlineSourceFieldCount)}`;
  const tail = " + $y-- < 1 < 2 }";
  const edit = {
    byte: prefix.length,
    deleteBytes: 0,
    insert: " + $a".repeat(getlineSourceGrowthFields),
  };
  determinismTest(
    "source ownership survives adding fields to a long source",
    prefix + tail,
    prefix + edit.insert + tail,
    [edit],
  );
  editHistoryTest(
    "source ownership survives undoing growth in a long source",
    prefix + tail,
    [edit],
  );
}

const largeLiteralCharacters = 80_000;
const builtinContinuationCount = 32_768;
const nestedBlockDepth = 2_000;
const getlineTargetCount = 2_000;
const concatenatedOperandCount = 32_000;
const additiveOperandCount = 16_000;
const expressionDepth = 16_000;
const nestedGetlineCount = 1_600;
const incompleteGetlineCount = 1_200;
const incompleteOperatorCount = 4_000;

test("posix_awk: deep blocks and long getline expressions parse through EOF", () => {
  for (const [name, source] of [
    [
      "deep blocks",
      `BEGIN ${"{".repeat(nestedBlockDepth)}print 1;${"}".repeat(nestedBlockDepth)}\n`,
    ],
    [
      "getline fields chained by prefix updates",
      `{ ${"getline x ++ $ ".repeat(getlineTargetCount)}getline y }\n`,
    ],
    [
      "a long concatenated getline source without a pipe",
      `{ getline $getline < ${"a ".repeat(concatenatedOperandCount)}x }\n`,
    ],
    [
      "a long additive getline source with a pipe",
      `{ getline $getline < ${"a + ".repeat(additiveOperandCount)}x | getline }\n`,
    ],
  ]) {
    assert.equal(
      parseSummary(source, [], { completionOnly: true }).successful,
      true,
      name,
    );
  }
});

test("posix_awk: long unterminated strings parse through EOF with native recovery", () => {
  const source = `BEGIN { print "${"x".repeat(largeLiteralCharacters)}`;
  assert.equal(parseSummary(source).successful, false);
});

test("posix_awk: 16000 operators or nested expressions parse through EOF", () => {
  const count = expressionDepth;
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
      parseSummary(`{ ${expression} }\n`, [], { completionOnly: true })
        .successful,
      true,
      label,
    );
  }
});

test("posix_awk: 1600 nested getline sequences finish without truncation", () => {
  for (const expression of [
    "$getline $-getline < 1 ++ ",
    "$getline $-getline < 1 ",
  ]) {
    assert.equal(
      parseSummary(`{ ${expression.repeat(nestedGetlineCount)}}\n`, [], {
        completionOnly: true,
      }).successful,
      true,
      expression,
    );
  }
});

test("posix_awk: repeated incomplete expressions recover through EOF", () => {
  for (const source of [
    `${") getline < ".repeat(incompleteGetlineCount)}\n`,
    `BEGIN { ${"] ^ getline < , 1 ".repeat(incompleteGetlineCount)}}\n`,
    `${"$ , ".repeat(incompleteOperatorCount)}\n`,
    `${"$ ) ".repeat(incompleteOperatorCount)}\n`,
    `${"! , ".repeat(incompleteOperatorCount)}\n`,
    `${"!! , ".repeat(incompleteOperatorCount)}\n`,
    `${"x = , ".repeat(incompleteOperatorCount)}\n`,
  ]) {
    assert.equal(
      parseSummary(source, [], { completionOnly: true }).successful,
      false,
    );
  }
});

for (const { name, source, successful } of normalWorkloads()) {
  test(`posix_awk: ${name} recovers or completes through EOF`, () => {
    assert.equal(parseSummary(source).successful, successful, name);
  });
}

test("posix_awk: a long builtin call continuation gap parses through EOF", () => {
  const source = `{ length ${"\\\n".repeat(builtinContinuationCount)}(1) }`;
  assert.equal(parseSummary(source).successful, true);
});
