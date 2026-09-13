import assert from "node:assert/strict";
import { test } from "node:test";
import {
  applyEdits,
  assertDeterministicEdit,
  assertStatus,
  captureParse,
  clean,
  cleanContinuation,
  contains,
  determinismTest,
  editHistoryTest,
  excludes,
  freshTest,
  hasRecovery,
  lines,
  matchingLineCount,
  writeSource,
} from "./support/parser.js";

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
    contains(tree, "line_continuation");
  });
}

const division = lines("BEGIN { print x / a }");

const matchEre = lines("BEGIN { print x ~ /a/ }");

const divisionAssignment = lines("BEGIN { x /= 2 }");

const divisionExpression = lines("BEGIN { x / 2 }");

const matchOperand = lines("BEGIN { print x ~ a }");

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

determinismTest("division to match ERE", division, matchEre, [
  { byte: 16, deleteBytes: 3, insert: "~ /a/" },
]);

determinismTest(
  "insert div assign equals",
  divisionExpression,
  divisionAssignment,
  [{ byte: 11, deleteBytes: 0, insert: "=" }],
);

determinismTest(
  "delete div assign equals",
  divisionAssignment,
  divisionExpression,
  [{ byte: 11, deleteBytes: 1, insert: "" }],
);

determinismTest("operand to ERE", matchOperand, matchEre, [
  { byte: 18, deleteBytes: 1, insert: "/a/" },
]);

determinismTest("ERE to operand", matchEre, matchOperand, [
  { byte: 18, deleteBytes: 3, insert: "a" },
]);

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

determinismTest(
  "delete membership concatenation operand",
  concatenatedMembership,
  plainMembership,
  [{ byte: 20, deleteBytes: 2, insert: "" }],
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
      matchingLineCount(tree, /^[ \t0-9:-]+collating_element$/),
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
      matchingLineCount(tree, /^[ \t0-9:-]+collating_element$/),
      2,
      "Expected a plain range between two collating elements",
    );
  },
);

const closedEre = lines("BEGIN { print /abc/", "print /ok/ }");

const unclosedEre = lines("BEGIN { print /abc", "print /ok/ }");

determinismTest("insert ERE closing slash", unclosedEre, closedEre, [
  { byte: 18, deleteBytes: 0, insert: "/" },
]);

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
  "division through broken to escaped ERE",
  division,
  escapedEreDelimiter,
  [
    { byte: 14, deleteBytes: 5, insert: String.raw`/ab\/c/` },
    { byte: 20, deleteBytes: 1, insert: "" },
    { byte: 20, deleteBytes: 0, insert: "/" },
  ],
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
    assert.match(tree, /^[ \t0-9:-]+ordinary_character$/m);
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
    contains(tree, "collating_element_content `^`");
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
    assert.match(tree, /^[ \t0-9:-]+collating_element$/m);
    excludes(tree, "meta_character");
  },
);

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

for (const [operator, continuation, suffix, kind] of [
  ["+", "\\\n", "=5}", "add_assign"],
  ["-", "\\\n", "=5}", "sub_assign"],
  ["*", "\\\n", "=5}", "mul_assign"],
  ["%", "\\\n", "=5}", "mod_assign"],
  ["^", "\\\n", "=5}", "pow_assign"],
  ["|", "\\\n", "|5}", "or"],
  ["+", "\\\n\\\n", "+}", "incr"],
  ["-", "\\\n\\\n", "-}", "decr"],
]) {
  const prefix = `{x${operator}${continuation}`;
  determinismTest(
    `${kind} is reclassified after repairing its second character`,
    `${prefix}/\\x`,
    prefix + suffix,
    [
      { byte: prefix.length + 2, deleteBytes: 1, insert: "" },
      { byte: prefix.length, deleteBytes: 2, insert: suffix },
    ],
    (tree) => {
      contains(tree, `operator: ${kind}`);
      contains(tree, "line_continuation");
    },
  );
}

for (const [marker, kind] of [
  ["=", "equivalence_class"],
  [".", "collating_symbol"],
]) {
  determinismTest(
    `a ${kind} closer is reclassified after repairing a continuation`,
    `/[[${marker}a${marker}\\))`,
    `/[[${marker}a${marker}\\\n]]/`,
    [
      { byte: 8, deleteBytes: 1, insert: "" },
      { byte: 7, deleteBytes: 1, insert: "\n]]/" },
    ],
    (tree) => {
      contains(tree, kind);
      contains(tree, "line_continuation");
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
    assert.match(tree, /^[ \t0-9:-]+collating_element$/m);
    excludes(tree, "collating_symbol");
  },
);

const rawStatementNewline = lines("BEGIN {", "  print 1", "}");

const continuedStatementBoundary = lines("BEGIN {", "  print 1\\", "}");

determinismTest(
  "raw newline to line continuation",
  rawStatementNewline,
  continuedStatementBoundary,
  [{ byte: 17, deleteBytes: 0, insert: "\\" }],
);

determinismTest(
  "line continuation to raw newline",
  continuedStatementBoundary,
  rawStatementNewline,
  [{ byte: 17, deleteBytes: 1, insert: "" }],
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
    contains(tree, "line_continuation");
  },
);

const bodylessIf = lines("BEGIN { if (condition) }");

const bodiedIf = lines("BEGIN { if (condition) body }");

determinismTest("insert if body before close brace", bodylessIf, bodiedIf, [
  { byte: 23, deleteBytes: 0, insert: "body " },
]);

const sameLineItems = lines("BEGIN {} END {}");

const separatedItems = lines("BEGIN {}", "END {}");

determinismTest(
  "insert item terminator between items",
  sameLineItems,
  separatedItems,
  [{ byte: 8, deleteBytes: 1, insert: "\n" }],
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
  "restore if body before end item",
  missingReservedIf,
  completeReservedIf,
  [{ byte: 22, deleteBytes: 1, insert: " print body }\n" }],
);

const completeDoTail = lines(
  "BEGIN { do print body; while (condition) }",
  "END { print target }",
);

const missingDoTail = lines("BEGIN { do print body;", "END { print target }");

determinismTest(
  "restore do tail before end item",
  missingDoTail,
  completeDoTail,
  [{ byte: 22, deleteBytes: 1, insert: " while (condition) }\n" }],
);

const closedDoTail = lines("BEGIN { do print body", "while (condition) }");

const openDoTail = lines("BEGIN { do print body", "}");

determinismTest("insert do tail before close brace", openDoTail, closedDoTail, [
  { byte: 22, deleteBytes: 0, insert: "while (condition) " },
]);

const specialPatternItems = lines("BEGIN {}", "END {}");

const actionlessSpecialPattern = lines("BEGIN", "END {}");

determinismTest(
  "restore special pattern action",
  actionlessSpecialPattern,
  specialPatternItems,
  [{ byte: 5, deleteBytes: 0, insert: " {}" }],
);

const functionItems = lines("function f() {}", "END {}");

const bodylessFunction = lines("function f()", "END {}");

determinismTest("restore function body", bodylessFunction, functionItems, [
  { byte: 12, deleteBytes: 0, insert: " {}" },
]);

const closedSubscriptEof = "BEGIN { delete array[offset] }";

const openSubscriptEof = "BEGIN { delete array[offset";

determinismTest(
  "insert subscript and action closers at EOF",
  openSubscriptEof,
  closedSubscriptEof,
  [{ byte: 27, deleteBytes: 0, insert: "] }" }],
);

const commentBackslash = lines("#\\", "BEGIN {}");

const leadingContinuation = lines("\\", "BEGIN {}");

determinismTest(
  "comment backslash to line continuation",
  commentBackslash,
  leadingContinuation,
  [{ byte: 0, deleteBytes: 1, insert: "" }],
);

const blankCall = lines("BEGIN { f (value) }");

const continuedCall = lines("BEGIN { f\\", "(value) }");

determinismTest(
  "blank to line continuation call",
  blankCall,
  continuedCall,
  [{ byte: 9, deleteBytes: 1, insert: "\\\n" }],
  (tree) => {
    assert.match(tree, /^[ \t0-9:-]*func_name[ \t]/m);
    contains(tree, "line_continuation");
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
    contains(tree, "line_continuation");
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
    contains(tree, "line_continuation");
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

freshTest("continued getline redirect", continuedGetlineRedirect, (tree) => {
  contains(tree, "source: expr");
  excludes(tree, "operator: le");
  cleanContinuation(tree);
});

determinismTest(
  "continued getline redirect to comparison",
  continuedGetlineRedirect,
  continuedGetlineComparison,
  [{ byte: 25, deleteBytes: 0, insert: "=" }],
  (tree) => {
    contains(tree, "operator: le");
    excludes(tree, "source: expr");
    contains(tree, "line_continuation");
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
    contains(tree, "line_continuation");
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
    contains(tree, "line_continuation");
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
    contains(tree, "line_continuation");
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

const closedString = lines('BEGIN { print "abc"', "}");

const unclosedString = lines('BEGIN { print "abc', "}");

determinismTest("insert string closing quote", unclosedString, closedString, [
  { byte: 18, deleteBytes: 0, insert: '"' },
]);

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
    assert.match(tree, /^[ \t0-9:-]+ordinary_character$/m);
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
    contains(tree, "line_continuation");
  },
);

determinismTest(
  "continued spaced to adjacent function name",
  continuedSpacedFunctionName,
  adjacentFunctionName,
  [{ byte: 16, deleteBytes: 3, insert: "" }],
  (tree) => {
    assert.match(tree, /^[ \t0-9:-]*name:[ \t]+func_name[ \t]+`compute`$/m);
    excludes(tree, "line_continuation");
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
    contains(tree, "line_continuation");
  },
);

determinismTest(
  "delete builtin concat line continuation",
  continuedBuiltinConcat,
  bareBuiltinConcat,
  [{ byte: 18, deleteBytes: 2, insert: " " }],
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
    contains(tree, "line_continuation");
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
    contains(tree, "line_continuation");
  },
);

determinismTest(
  "delete statement gap line continuation",
  continuedEmptyStatement,
  separatedEmptyStatement,
  [{ byte: 9, deleteBytes: 2, insert: " " }],
);

function continuationHistoryTest(name, initialSource, steps) {
  test(`posix_awk: ${name}`, () => {
    const initial = writeSource(name, "initial", initialSource);
    const edits = [];
    for (const [index, step] of steps.entries()) {
      edits.push(...step.edits);
      const label = `${name}, edit ${index + 1}`;
      assert.deepEqual(
        applyEdits(initialSource, edits),
        Buffer.from(step.source),
        label,
      );
      const fresh = captureParse(writeSource(name, "expected", step.source));
      const incremental = captureParse(initial, edits);
      for (const result of [fresh, incremental]) {
        assertStatus(label, result, 0);
        clean(result.tree);
      }
      assert.equal(incremental.tree, fresh.tree, label);
    }
  });
}

for (const [name, before, after] of [
  ["keyword", "BE", "GIN {}\n"],
  ["name", "BEGIN { print va", "lue }\n"],
  ["built-in name", "BEGIN { print len", "gth(x) }\n"],
  ["assignment operator", "BEGIN { x +", "= 1 }\n"],
  ["exponent", "BEGIN { print 1e+", "2 }\n"],
  ["number suffix", "BEGIN { print 1.0", "F }\n"],
  ["string", 'BEGIN { print "a', 'b" }\n'],
  ["ERE bracket", "BEGIN { print /[a", "-z]/ }\n"],
  ["string escape", 'BEGIN { print "\\', 'n" }\n'],
  ["octal escape", 'BEGIN { print "\\1', '23" }\n'],
  ["ERE escaped delimiter", "BEGIN { print /a\\", "/b/ }\n"],
  ["ERE class name", "BEGIN { print /[[:al", "pha:]]/ }\n"],
  ["ERE duplication count", "BEGIN { print /a{1", "2}/ }\n"],
  ["ERE compound delimiter", "BEGIN { print /[[", ":alpha:]]/ }\n"],
]) {
  const initial = before + after;
  const split = `${before}\\\n${after}`;
  const prefix = "# shifted source\n";
  continuationHistoryTest(
    `${name} continuations remain deterministic through prefix edits and removal`,
    initial,
    [
      {
        edits: [{ byte: before.length, deleteBytes: 0, insert: "\\\n" }],
        source: split,
      },
      {
        edits: [{ byte: 0, deleteBytes: 0, insert: prefix }],
        source: prefix + split,
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
  "a blank separates a split word into two tokens and back",
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

for (const { name, initial, changed, removed, inserted, expected } of [
  {
    name: "a keyword becomes a name after a continued prefix",
    initial: lines("BE\\", "GIN { print 1 }"),
    changed: lines("BE\\", "GUN { print 1 }"),
    removed: "GIN",
    inserted: "GUN",
    expected: "name",
  },
  {
    name: "a builtin becomes a user function after a continued prefix",
    initial: lines("BEGIN { print len\\", "gth(x) }"),
    changed: lines("BEGIN { print len\\", "gtx(x) }"),
    removed: "gth",
    inserted: "gtx",
    expected: "func_name",
  },
  {
    name: "an exponent edit moves a continuation out of its number",
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

function createEditHistoryGenerator() {
  let seed = 1n;
  function next(maximum) {
    seed = BigInt.asUintN(64, seed * 6364136223846793005n + 1n);
    return Number(seed >> 32n) % maximum;
  }
  return function* (fragments, insertions, joinSource) {
    for (let iteration = 0; iteration < 100; iteration++) {
      const parts = [];
      const count = 1 + next(3);
      for (let part = 0; part < count; part++) {
        parts.push(fragments[next(fragments.length)]);
      }
      const initial = joinSource(parts);
      let source = Buffer.from(initial);
      const edits = [];
      for (let step = 0; step < 5; step++) {
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
              deleteBytes: Math.min(next(2) + 1, source.length - position),
              insert: "",
            };
        edits.push(edit);
        source = applyEdits(source, [edit]);
        yield {
          initial,
          source,
          edits: [...edits],
          context: `seed 1, iteration ${iteration}, source ${JSON.stringify(initial)}, edits ${JSON.stringify(edits)}`,
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
    const initial = writeSource("generated", "initial", history.initial);
    const final = writeSource("generated", "final", history.source);
    const fresh = captureParse(final);
    const incremental = captureParse(initial, history.edits);
    for (const result of [fresh, incremental]) {
      assert.ok(result.status === 0 || result.status === 1, history.context);
    }
    if (fresh.status === 0 && !hasRecovery(fresh.tree)) {
      assert.equal(incremental.status, 0, history.context);
      assert.equal(hasRecovery(incremental.tree), false, history.context);
      assert.equal(incremental.tree, fresh.tree, history.context);
      compared += 1;
    }
    checked += 1;
  }
  assert.equal(checked, 500);
  assert.ok(compared > 0);
  context.diagnostic(
    `posix_awk: checked ${checked} generated edit states, compared ${compared} valid CSTs`,
  );
});

test("posix_awk: every byte inside a Unicode payload can be deleted and repaired through an edit history", () => {
  const prefix = 'BEGIN { print "';
  const suffix = '" }\n';
  const source = 'BEGIN { print "é😀" }\n';
  const changed = 'BEGIN { print "x" }\n';
  const payloadByte = 15;
  const initial = writeSource("utf8-history", "initial", source);
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
      { byte: payloadByte, deleteBytes: 5, insert: "é😀" },
      { byte: payloadByte, deleteBytes: 6, insert: "x" },
      { byte: payloadByte, deleteBytes: 1, insert: "é😀" },
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
      const final = writeSource("utf8-history", "expected", expectedSource);
      const fresh = captureParse(final);
      const incremental = captureParse(initial, history);
      for (const result of [fresh, incremental]) {
        assert.ok(result.status === 0 || result.status === 1, label);
        if (step === 0) continue;
        assert.equal(result.status, 0, label);
        assert.equal(hasRecovery(result.tree), false, label);
        const end = step === 2 ? 16 : 21;
        assert.match(
          result.tree,
          new RegExp(`^0:14 +- +0:${end + 1} +string$`, "m"),
        );
        assert.match(
          result.tree,
          new RegExp(`^0:15 +- +0:${end} +string_content `, "m"),
        );
      }
      if (step > 0) assert.equal(incremental.tree, fresh.tree, label);
    }
  }
});
