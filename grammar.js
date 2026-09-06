// Even numbers only: the adjacent operand chain (see CLASSIFICATIONS) uses
// the odd number just above each tier to outrank the non_unary chain.
const PRECEDENCE = {
  assignment: 2,
  logicalOr: 4,
  logicalAnd: 6,
  membership: 8,
  match: 10,
  comparison: 12,
  concatenation: 14,
  additive: 16,
  multiplicative: 18,
  postfixUpdate: 20,
  field: 22,
};

// Every line continuation is preceded by a scanner marker naming what
// follows it. The marker carries the lookahead that chose the owner, so a
// reused subtree can never keep a continuation whose target has changed,
// and the parser never has to guess which construct owns the continuation.
// Each marker is an external token named after its target, with one hidden
// rule for the continuations it introduces so that every use of a marker
// shares the same parse states.
const CONTINUATION_TARGETS = [
  "operator",
  "additive_operator",
  "multiplicative_operator",
  "exponentiation_operator",
  "comparison_operator",
  "match_operator",
  "membership_operator",
  "logical_and_operator",
  "logical_or_operator",
  "conditional_question",
  "conditional_colon",
  "less_than",
  "input_pipe",
  "output_redirection",
  "else",
  "do_tail",
  "semicolon",
  "newline",
  "close_brace",
  "simple_statement",
  "expression",
  "comma",
  "open_bracket",
  "action",
  "close_parenthesis",
  "close_bracket",
  "statement",
  "item",
  "eof",
];

const continuationRules = Object.fromEntries(
  CONTINUATION_TARGETS.map((target) => [
    `_${target}_continuation`,
    ($) => seq($[`_lc_before_${target}`], repeat1($.line_continuation)),
  ]),
);

const continuationsBefore = ($, target) => $[`_${target}_continuation`];

const optionalContinuationsBefore = ($, target) =>
  optional(continuationsBefore($, target));

const newlineContinuations = ($) => optionalContinuationsBefore($, "newline");

const newlineLayout = ($) =>
  optional(seq(newlineContinuations($), $.newline_opt));

const continuedMember = ($, marker, member) =>
  choice(member, seq(continuationsBefore($, marker), member));

const continuedStatement = ($, statement) =>
  continuedMember($, "statement", statement);

const continuedOperatorWith = ($, marker, operator) =>
  continuedMember($, marker, field("operator", operator));

const continuedOperator = ($, operator) =>
  continuedOperatorWith($, "operator", operator);

const continuedExpressionMember = ($, member) =>
  continuedMember($, "expression", member);

const continuedExpression = ($, name, expression) =>
  continuedExpressionMember($, field(name, expression));

const requiredAfterOptionalNewline = (
  $,
  targetGuard,
  present,
  required = present,
) =>
  choice(
    seq(newlineContinuations($), targetGuard, $.newline_opt, present),
    required,
  );

const functionBody = ($) =>
  requiredAfterOptionalNewline(
    $,
    $._action_target_guard,
    continuedMember($, "action", field("body", $.action)),
  );

const requiredParameter = ($) =>
  requiredAfterOptionalNewline(
    $,
    $._parameter_target_guard,
    continuedExpressionMember($, $.name),
  );

const continuedParameter = ($) =>
  seq(continuedMember($, "comma", ","), requiredParameter($));

const ereEscapeWithCharacter = ($, character) =>
  seq($._ere_escape_start, $._escape_introducer, character);

// A hyphen right before the closing bracket is a distinct token, so a
// trailing hyphen, a range ending in a hyphen, and a range separator never
// compete for the same source.
const ereClosingHyphen = ($) => alias($._ere_closing_hyphen, "-");

const ereBracketListAlternatives = ($, followList) => [
  followList,
  seq(followList, ereClosingHyphen($)),
];

// XBD 9.5.2: range_expression : start_range end_range | start_range '-'.
// The ending hyphen is a literal wherever it stands, not only before the
// closing bracket ("[%--@]" ranges from '%' to '-' and then lists '@').
const ereRangeExpressionWith = ($, startRange) =>
  seq(
    startRange,
    choice($.end_range, ereClosingHyphen($), $._ere_bracket_hyphen),
  );

// Bracket expressions whose first element is a literal "]" or "-" repeat the
// follow_list hierarchy with that element in the first position.
const initialFollowListRules = (prefix, firstElement) => {
  const name = (part) => `_initial_${prefix}_${part}`;
  return {
    [name("follow_list")]: ($) =>
      choice(
        alias($[name("expression_term")], $.expression_term),
        seq(alias($[name("follow_list")], $.follow_list), $.expression_term),
      ),
    [name("expression_term")]: ($) =>
      choice(
        alias($[name("single_expression")], $.single_expression),
        alias($[name("range_expression")], $.range_expression),
      ),
    [name("single_expression")]: ($) =>
      alias($[name("end_range")], $.end_range),
    [name("range_expression")]: ($) =>
      ereRangeExpressionWith($, alias($[name("start_range")], $.start_range)),
    [name("start_range")]: ($) =>
      seq(alias($[name("end_range")], $.end_range), $._ere_bracket_hyphen),
    [name("end_range")]: ($) => alias(firstElement($), $.collating_element),
  };
};

const ereCompoundOpening = ($, punctuation) =>
  seq(
    $._ere_compound_open_guard,
    token.immediate("["),
    token.immediate(punctuation),
  );

const ereCompoundClosing = (guard, punctuation) =>
  seq(guard, token.immediate(punctuation), token.immediate("]"));

// A raw newline or EOF ends a static ERE, or its compound bracket form,
// lexically; the scanner's zero-width lexical end stands in for the absent
// closing, so the ERE never owns the source that follows the newline.
const ereLexicalEnd = ($) => $._ere_lexical_end;

const ereRequiredPayload = ($, payload, closing) =>
  choice(ereLexicalEnd($), seq(payload, choice(closing, ereLexicalEnd($))));

const ereCompound = ($, opening, payload, closing) =>
  seq(opening, ereRequiredPayload($, payload, closing));

const header = ($, keyword, rest) => seq(keyword, rest, newlineLayout($));

const conditionalHeader = ($, keyword) =>
  header($, keyword, continuedExpressionMember($, $._parenthesized_condition));

const keywordHeader = ($, keyword) => seq(keyword, newlineLayout($));

const doWhileTail = ($) =>
  seq(
    $.while_keyword,
    continuedExpressionMember($, $._parenthesized_condition),
  );

const continuedDoTail = ($) => continuedMember($, "do_tail", doWhileTail($));

const continuedSimpleStatement = ($, name) =>
  continuedMember($, "simple_statement", field(name, $.simple_statement));

// A conditional without else is right associative so that an else binds to
// the nearest if (POSIX) by shifting instead of closing the inner statement.
const controlStatements = ($, body) => [
  prec.right(
    seq($._if_header, continuedStatement($, field("consequence", body))),
  ),
  seq(
    $._if_header,
    continuedStatement($, field("consequence", $.terminated_statement)),
    continuedMember($, "else", $._else_header),
    continuedStatement($, field("alternative", body)),
  ),
  seq($._while_header, continuedStatement($, field("body", body))),
  seq($._for_header, continuedStatement($, field("body", body))),
];

const actionBoundaryControlBody = ($) =>
  alias($.action_body_boundary_control, $.unterminated_statement);

// Structural Recovery (CST.md) fixes the boundary of an item around a
// required member that is absent. Tree-sitter's cost-based recovery would
// take the next item's action for that member, so a zero-width scanner
// token stands in for it instead. The token has no node: the member is
// simply absent from the CST, and the item ends where POSIX ends it.
//
// The scanner emits the absent statement only when the closing brace (or
// EOF) follows the construct that still requires it: the body of a control
// header, or the `while` tail of a do statement.
const missingStatement = ($) => $._statement_recovery;

// The scanner emits the absent action when what follows a special pattern
// or a function header (past blanks, continuations, and the header's own
// newlines) cannot open an action.
const missingAction = ($) => $._action_recovery;

const statementTerminatedBy = ($, target, terminator) =>
  seq(
    field("statement", $.terminatable_statement),
    optionalContinuationsBefore($, target),
    field("terminator", terminator),
    newlineLayout($),
  );

const parenthesized = ($, member) =>
  seq(
    "(",
    continuedExpressionMember($, member),
    $._continued_close_parenthesis,
  );

const parenthesizedPrintStatement = ($, keyword) =>
  seq(
    keyword,
    continuedExpressionMember(
      $,
      parenthesized($, field("arguments", $.multiple_expr_list)),
    ),
  );

const callArguments = ($) =>
  continuedExpressionMember(
    $,
    seq(
      "(",
      choice(
        $._continued_close_parenthesis,
        seq(
          continuedExpressionMember($, $.expr_list),
          $._continued_close_parenthesis,
        ),
      ),
    ),
  );

const subscript = ($, subscripts) =>
  seq(
    continuedMember($, "open_bracket", "["),
    continuedExpressionMember($, subscripts),
    $._continued_close_bracket,
  );

const subscriptedName = ($, subscripts) =>
  seq($.name, subscript($, subscripts));

// Another item follows an item without a terminator: the scanner's
// zero-width boundary stands in for the absent terminator (see
// missingStatement), so both items keep their own CST.
const itemEnd = ($, boundary) =>
  choice(
    seq(
      optional(
        choice(
          continuationsBefore($, "newline"),
          continuationsBefore($, "semicolon"),
        ),
      ),
      field("terminator", $.terminator),
    ),
    seq(optionalContinuationsBefore($, "item"), boundary),
  );

const terminatedStatements = ($) =>
  seq(
    $.terminated_statement,
    repeat(continuedStatement($, $.terminated_statement)),
  );

const statementListWithTail = ($, tail) =>
  choice(tail, seq(terminatedStatements($), continuedStatement($, tail)));

const rawNewlines = ($) =>
  seq($.newline, repeat(seq(newlineContinuations($), $.newline)));

const EXPRESSION_CONTEXT = {
  normal: {
    prefix: "normal",
    expression: "expr",
    unaryExpression: "unary_expr",
    nonUnaryExpression: "non_unary_expr",
    comparison: true,
    input: true,
  },
  print: {
    prefix: "print",
    expression: "print_expr",
    unaryExpression: "unary_print_expr",
    nonUnaryExpression: "non_unary_print_expr",
    comparison: false,
    input: false,
  },
};

const classTierName = (context, classification, tier) =>
  `_${context.prefix}_${classification}_${tier}_expr`;

// Operand rules are visible but only ever referenced through an alias, so
// tree-sitter names their symbol after the alias and node-types.json never
// lists them. Hidden rules would work too, but a hidden child makes the
// parent inherit the child's fields, and ts_node_child_by_field_name would
// then descend into the operand instead of returning the parent's own
// member (an inner `right` for the outer `right`).
const classOperandName = (context, classification, tier) =>
  `${context.prefix}_${classification}_${tier}_operand`;

const anyTierName = (context, tier) => `${context.prefix}_${tier}_expr`;

const classTier = ($, context, classification, tier) =>
  $[classTierName(context, classification, tier)];

// The adjacent chain shares its productions with the non_unary chain; where a
// parser state could reduce into either, the adjacent chain wins.
const adjacentLeaf = (classification, rule) =>
  classification === "adjacent" ? prec(1, rule) : rule;

const classPrecedence = (classification, precedence) =>
  classification === "adjacent" ? precedence + 1 : precedence;

const aliasedClassTier = ($, context, classification, tier) =>
  alias(
    $[classOperandName(context, classification, tier)],
    $[
      classification === "unary"
        ? context.unaryExpression
        : context.nonUnaryExpression
    ],
  );

// Operand classifications: POSIX unary_expr, POSIX non_unary_expr, and the
// non_unary_expr that directly follows another operand (the right operand of
// a concatenation). A slash after a complete operand is always division, so
// an ERE can never start an adjacent operand; keeping it out of the grammar
// stops a reused subtree from re-lexing that slash as an ERE.
const CLASSIFICATIONS = ["unary", "non_unary", "adjacent"];
const BINARY_CLASSIFICATIONS = ["unary", "non_unary"];

const aliasedAnyTier = ($, context, tier) =>
  alias($[anyTierName(context, tier)], $[context.expression]);

const expressionTargetGuard = ($, context) =>
  context.input ? $._expression_target_guard : $._print_expression_target_guard;

const nonUnaryAtom = ($, context, adjacent = false) => {
  const atoms = [
    $._parenthesized_expression,
    $.number,
    $.string,
    $.lvalue,
    $._user_function_call,
    $._builtin_function_call,
    $.builtin_func_name,
  ];
  if (!adjacent) {
    atoms.push($.ere);
  }
  if (context.input) {
    atoms.push($.non_unary_input_function);
  }
  return choice(...atoms);
};

const tieredExpressionRules = (context) => {
  const rules = {};
  const unary = (tier) => classTierName(context, "unary", tier);
  const nonUnary = (tier) => classTierName(context, "non_unary", tier);
  const any = (tier) => anyTierName(context, tier);
  // The operand outranks the pass-through to the next tier, so a complete
  // operand closes as soon as its operator arrives.
  const addOperand = (classification, tier) => {
    rules[classOperandName(context, classification, tier)] = ($) =>
      prec(1, classTier($, context, classification, tier));
  };
  const not = `_${context.prefix}_not_expr`;
  const assignmentRight = `_${context.prefix}_assignment_right_expr`;
  const conditionalConsequence = `_${context.prefix}_conditional_consequence_expr`;
  const conditionalAlternative = `_${context.prefix}_conditional_alternative_expr`;
  const conditionalTail = `_${context.prefix}_conditional_tail`;
  const expression = ($) => $[context.expression];
  const requiredTierName = (name, tier) =>
    `_${context.prefix}_required_${name}_${tier}_expr`;
  const requiredTier = ($, name, tier) => $[requiredTierName(name, tier)];

  const rightTiers = [
    "logical_and",
    "membership",
    ...(context.comparison ? ["comparison"] : []),
    "concatenation",
    "multiplicative",
    "unary",
  ];
  for (const tier of rightTiers) {
    rules[requiredTierName("right", tier)] = ($) =>
      continuedExpression($, "right", aliasedAnyTier($, context, tier));
  }
  rules[requiredTierName("operand", "unary")] = ($) =>
    continuedExpression($, "operand", aliasedAnyTier($, context, "unary"));
  rules[assignmentRight] = ($) =>
    continuedExpression($, "right", expression($));
  rules[conditionalConsequence] = ($) =>
    continuedExpression($, "consequence", expression($));
  rules[conditionalAlternative] = ($) =>
    continuedExpression(
      $,
      "alternative",
      aliasedAnyTier($, context, "conditional"),
    );
  rules[conditionalTail] = ($) =>
    seq(
      $._continued_conditional_question,
      $[conditionalConsequence],
      $._continued_conditional_colon,
      $[conditionalAlternative],
    );

  const addAnyTier = (tier) => {
    for (const classification of BINARY_CLASSIFICATIONS) {
      addOperand(classification, tier);
    }
    rules[any(tier)] = ($) =>
      choice(
        aliasedClassTier($, context, "unary", tier),
        aliasedClassTier($, context, "non_unary", tier),
      );
  };

  // POSIX fixes the right operand of `in` to a NAME, so a membership
  // expression is complete before any operator that follows it and can be
  // the left operand of every higher-precedence binary operator without
  // parentheses ("a in b ~ c" is "(a in b) ~ c"). The adjacent chain never
  // holds one: after a complete operand, the concatenation binds first and
  // becomes the left operand of `in` ("a b in c" is "(a b) in c").
  const membershipInLeft = ($, classification, tail) =>
    classification === "adjacent"
      ? []
      : [
          seq(
            field(
              "left",
              aliasedClassTier($, context, classification, "membership_in"),
            ),
            tail,
          ),
        ];

  const addLeftAssociativeTier = (tier, nextTier, operator, precedence) => {
    addAnyTier(nextTier);
    const tail = `_${context.prefix}_${tier}_tail`;
    rules[tail] = ($) => seq(operator($), requiredTier($, "right", nextTier));
    for (const classification of CLASSIFICATIONS) {
      addOperand(classification, tier);
      rules[classTierName(context, classification, tier)] = ($) =>
        choice(
          classTier($, context, classification, nextTier),
          prec.left(
            classPrecedence(classification, precedence),
            seq(
              field("left", aliasedClassTier($, context, classification, tier)),
              $[tail],
            ),
          ),
          ...membershipInLeft($, classification, $[tail]).map((rule) =>
            prec.left(precedence, rule),
          ),
        );
    }
  };

  const addNonAssociativeTier = (tier, nextTier, operator, precedence) => {
    addAnyTier(nextTier);
    const tail = `_${context.prefix}_${tier}_tail`;
    rules[tail] = ($) => seq(operator($), requiredTier($, "right", nextTier));
    for (const classification of BINARY_CLASSIFICATIONS) {
      rules[classTierName(context, classification, tier)] = ($) =>
        choice(
          classTier($, context, classification, nextTier),
          prec(
            precedence,
            seq(
              field(
                "left",
                aliasedClassTier($, context, classification, nextTier),
              ),
              $[tail],
            ),
          ),
          ...membershipInLeft($, classification, $[tail]).map((rule) =>
            prec(precedence, rule),
          ),
        );
    }
  };

  rules[unary("assignment")] = ($) =>
    classTier($, context, "unary", "conditional");
  rules[nonUnary("assignment")] = ($) =>
    choice(
      classTier($, context, "non_unary", "conditional"),
      prec.right(
        PRECEDENCE.assignment,
        seq(
          field("left", $.lvalue),
          continuedOperator(
            $,
            choice(
              $.pow_assign,
              $.mod_assign,
              $.mul_assign,
              $.div_assign,
              $.add_assign,
              $.sub_assign,
              "=",
            ),
          ),
          $[assignmentRight],
        ),
      ),
    );

  // The alternative is itself a conditional (right associative), so a lower
  // precedence assignment never nests inside it: "a ? b : c = d" is invalid,
  // as it is after every other operator.
  addAnyTier("conditional");
  for (const classification of BINARY_CLASSIFICATIONS) {
    addOperand(classification, "logical_or");
    rules[classTierName(context, classification, "conditional")] = ($) =>
      choice(
        classTier($, context, classification, "logical_or"),
        seq(
          field(
            "condition",
            aliasedClassTier($, context, classification, "logical_or"),
          ),
          $[conditionalTail],
        ),
      );
  }

  const addNewlineContinuedTier = (tier, nextTier, operator, precedence) => {
    addAnyTier(nextTier);
    const tail = `_${context.prefix}_${tier}_tail`;
    rules[tail] = ($) =>
      seq(
        operator($),
        requiredAfterOptionalNewline(
          $,
          expressionTargetGuard($, context),
          continuedExpression($, "right", aliasedAnyTier($, context, nextTier)),
          requiredTier($, "right", nextTier),
        ),
      );
    for (const classification of BINARY_CLASSIFICATIONS) {
      rules[classTierName(context, classification, tier)] = ($) =>
        choice(
          classTier($, context, classification, nextTier),
          prec.left(
            precedence,
            seq(
              field("left", aliasedClassTier($, context, classification, tier)),
              $[tail],
            ),
          ),
        );
    }
  };

  addNewlineContinuedTier(
    "logical_or",
    "logical_and",
    ($) => $._continued_logical_or_operator,
    PRECEDENCE.logicalOr,
  );

  addNewlineContinuedTier(
    "logical_and",
    "membership",
    ($) => $._continued_logical_and_operator,
    PRECEDENCE.logicalAnd,
  );

  const membershipTail = `_${context.prefix}_membership_tail`;
  rules[membershipTail] = ($) =>
    seq(
      $._continued_membership_operator,
      continuedExpression($, "right", $.name),
    );
  for (const classification of BINARY_CLASSIFICATIONS) {
    addOperand(classification, "membership_in");
    rules[classTierName(context, classification, "membership_in")] = ($) => {
      const members = [
        prec.left(
          PRECEDENCE.membership,
          seq(
            field(
              "left",
              aliasedClassTier($, context, classification, "membership"),
            ),
            $[membershipTail],
          ),
        ),
      ];
      if (classification === "non_unary") {
        members.push(
          seq(
            parenthesized($, field("left", $.multiple_expr_list)),
            $._continued_membership_operator,
            continuedExpression($, "right", $.name),
          ),
        );
      }
      return choice(...members);
    };
    rules[classTierName(context, classification, "membership")] = ($) =>
      choice(
        classTier($, context, classification, "match"),
        classTier($, context, classification, "membership_in"),
      );
  }

  const comparisonTier = context.comparison ? "comparison" : "concatenation";
  addNonAssociativeTier(
    "match",
    comparisonTier,
    ($) => $._continued_match_operator,
    PRECEDENCE.match,
  );

  if (context.comparison) {
    addNonAssociativeTier(
      "comparison",
      "concatenation",
      ($) => $._continued_comparison_operator,
      PRECEDENCE.comparison,
    );
  }

  addOperand("adjacent", "additive");
  const concatenationTail = `_${context.prefix}_concatenation_tail`;
  rules[concatenationTail] = ($) =>
    continuedExpressionMember(
      $,
      field("right", aliasedClassTier($, context, "adjacent", "additive")),
    );
  for (const classification of BINARY_CLASSIFICATIONS) {
    rules[classTierName(context, classification, "concatenation")] = ($) =>
      choice(
        classTier($, context, classification, "additive"),
        prec.left(
          PRECEDENCE.concatenation,
          seq(
            field(
              "left",
              aliasedClassTier($, context, classification, "concatenation"),
            ),
            $[concatenationTail],
          ),
        ),
        ...membershipInLeft($, classification, $[concatenationTail]).map(
          (rule) => prec.left(PRECEDENCE.concatenation, rule),
        ),
      );
  }

  addLeftAssociativeTier(
    "additive",
    "multiplicative",
    ($) => $._continued_additive_operator,
    PRECEDENCE.additive,
  );

  addLeftAssociativeTier(
    "multiplicative",
    "unary",
    ($) => $._continued_multiplicative_operator,
    PRECEDENCE.multiplicative,
  );

  rules[unary("unary")] = ($) =>
    choice(
      classTier($, context, "unary", "exponentiation"),
      ...["+", "-"].map((operator) =>
        seq(field("operator", operator), requiredTier($, "operand", "unary")),
      ),
    );
  rules[not] = ($) =>
    seq(field("operator", "!"), requiredTier($, "operand", "unary"));
  for (const classification of ["non_unary", "adjacent"]) {
    rules[classTierName(context, classification, "unary")] = ($) =>
      choice(
        classTier($, context, classification, "exponentiation"),
        adjacentLeaf(classification, $[not]),
      );
  }

  // The unary update tier holds only the unary input function, which print
  // expressions lack, so their unary exponentiation tier keeps just the
  // membership operand.
  const exponentiationTail = `_${context.prefix}_exponentiation_tail`;
  rules[exponentiationTail] = ($) =>
    seq(
      $._continued_exponentiation_operator,
      requiredTier($, "right", "unary"),
    );
  for (const classification of CLASSIFICATIONS) {
    const hasUpdateTier = classification !== "unary" || context.input;
    if (hasUpdateTier) {
      addOperand(classification, "update");
    }
    rules[classTierName(context, classification, "exponentiation")] = ($) =>
      choice(
        ...(hasUpdateTier
          ? [
              classTier($, context, classification, "update"),
              seq(
                field(
                  "left",
                  aliasedClassTier($, context, classification, "update"),
                ),
                $[exponentiationTail],
              ),
            ]
          : []),
        ...membershipInLeft($, classification, $[exponentiationTail]),
      );
  }
  if (context.input) {
    rules[unary("update")] = ($) => $.unary_input_function;
  }

  for (const classification of ["non_unary", "adjacent"]) {
    rules[classTierName(context, classification, "update")] = ($) =>
      choice(
        classTier($, context, classification, "atom"),
        adjacentLeaf(classification, $._prefix_update_expr),
        prec.left(
          classPrecedence(classification, PRECEDENCE.postfixUpdate),
          seq(
            field("operand", $.lvalue),
            continuedOperator($, choice($.incr, $.decr)),
          ),
        ),
      );
  }

  rules[nonUnary("atom")] = ($) => nonUnaryAtom($, context);
  rules[classTierName(context, "adjacent", "atom")] = ($) =>
    prec(1, nonUnaryAtom($, context, true));

  return rules;
};

const normalExpressionRules = tieredExpressionRules(EXPRESSION_CONTEXT.normal);
const printExpressionRules = tieredExpressionRules(EXPRESSION_CONTEXT.print);

const continuedListElementWith = ($, targetGuard, element) =>
  seq(
    continuedMember($, "comma", ","),
    requiredAfterOptionalNewline(
      $,
      targetGuard,
      continuedExpressionMember($, element),
    ),
  );

const continuedListElement = ($, element) =>
  continuedListElementWith($, $._expression_target_guard, element);

const continuedPipeGet = ($) =>
  continuedExpressionMember($, field("get", $.simple_get));

const continuedPrintListElement = ($, element) =>
  continuedListElementWith($, $._print_expression_target_guard, element);

module.exports = grammar({
  name: "posix_awk",

  externals: ($) => [
    $._begin_word,
    $._end_word,
    $._function_word,
    $._print_word,
    $._break_word,
    $._continue_word,
    $._delete_word,
    $._do_word,
    $._else_word,
    $._exit_word,
    $._for_word,
    $._if_word,
    $._next_word,
    $._nextfile_word,
    $._printf_word,
    $._return_word,
    $._while_word,
    $._name_word,
    $._for_in_variable_word,
    $._getline_word,
    $._getline_target_word,
    $._in_word,
    $._builtin_func_name_word,
    $._builtin_call_word,
    $._func_name_word,
    $._number_integer,
    $._number_fraction,
    $._number_exponent,
    $._division_slash,
    $._ere_opening_slash,
    $._div_assign_operator,
    $._add_assign_operator,
    $._sub_assign_operator,
    $._mul_assign_operator,
    $._mod_assign_operator,
    $._pow_assign_operator,
    $._or_operator,
    $._and_operator,
    $._no_match_operator,
    $._eq_operator,
    $._le_operator,
    $._ge_operator,
    $._ne_operator,
    $._incr_operator,
    $._decr_operator,
    $._append_operator,
    $._output_greater_guard,
    ...CONTINUATION_TARGETS.map((target) => $[`_lc_before_${target}`]),
    $._closed_item_boundary,
    $._normal_pattern_item_boundary,
    $._statement_recovery,
    $._action_recovery,
    $._ere_compound_open_guard,
    $._ere_dot_close_guard,
    $._ere_equal_close_guard,
    $._ere_colon_close_guard,
    $._ere_escape_start,
    $._ere_escaped_delimiter_start,
    $._ere_escaped_delimiter_end,
    $._ere_closing_hyphen,
    $._ere_lexical_end,
    $._ere_closing,
    $._expression_target_guard,
    $._print_expression_target_guard,
    $._action_target_guard,
    $._parameter_target_guard,
    $._error_sentinel,
  ],

  extras: ($) => [token(repeat1(choice(" ", "\t"))), $.comment],

  inline: ($) => [
    $._item,
    $._normal_unary_assignment_expr,
    $._normal_unary_update_expr,
    $._print_unary_assignment_expr,
  ],

  conflicts: () => [],

  rules: {
    program: ($) =>
      seq(
        optional(
          choice(
            continuationsBefore($, "item"),
            continuationsBefore($, "newline"),
            continuationsBefore($, "eof"),
          ),
        ),
        optional(
          seq(
            alias($._item_list, $.item_list),
            optional(
              choice(
                continuationsBefore($, "item"),
                continuationsBefore($, "eof"),
              ),
            ),
          ),
        ),
        optional(seq($._item, optionalContinuationsBefore($, "eof"))),
      ),

    ...continuationRules,

    _continued_close_parenthesis: ($) =>
      continuedMember($, "close_parenthesis", ")"),

    _continued_close_bracket: ($) => continuedMember($, "close_bracket", "]"),

    _continued_additive_operator: ($) =>
      continuedOperatorWith($, "additive_operator", choice("+", "-")),

    _continued_multiplicative_operator: ($) =>
      continuedOperatorWith(
        $,
        "multiplicative_operator",
        choice("*", alias($._division_slash, "/"), "%"),
      ),

    _continued_exponentiation_operator: ($) =>
      continuedOperatorWith($, "exponentiation_operator", "^"),

    _continued_comparison_operator: ($) =>
      choice(
        continuedOperatorWith($, "less_than", "<"),
        continuedOperatorWith(
          $,
          "comparison_operator",
          choice($.le, $.ne, $.eq, ">", $.ge),
        ),
      ),

    _continued_match_operator: ($) =>
      continuedOperatorWith($, "match_operator", choice("~", $.no_match)),

    _continued_membership_operator: ($) =>
      continuedOperatorWith($, "membership_operator", $.in_keyword),

    _continued_logical_and_operator: ($) =>
      continuedOperatorWith($, "logical_and_operator", $.and),

    _continued_logical_or_operator: ($) =>
      continuedOperatorWith($, "logical_or_operator", $.or),

    _continued_conditional_question: ($) =>
      continuedMember($, "conditional_question", "?"),

    _continued_conditional_colon: ($) =>
      continuedMember($, "conditional_colon", ":"),

    _continued_input_redirect: ($) => continuedMember($, "less_than", "<"),

    _continued_input_pipe: ($) => continuedMember($, "input_pipe", "|"),

    // POSIX item_list is left recursive with the terminator inside each step,
    // so the parser never has to close the list before it knows whether the
    // next item is terminated. Keeping that shape (and hiding the recursion
    // behind one alias) leaves the grammar LR(1) at every item boundary.
    _item_list: ($) =>
      choice(
        field("leading", $.newline_opt),
        seq(
          optional(seq($._item_list, optionalContinuationsBefore($, "item"))),
          $._terminated_item,
        ),
      ),

    _item: ($) =>
      choice(
        alias($._closed_item, $.item),
        alias($._normal_pattern_item, $.item),
      ),

    _terminated_item: ($) =>
      choice(
        seq(
          field("item", alias($._closed_item, $.item)),
          itemEnd($, $._closed_item_boundary),
        ),
        seq(
          field("item", alias($._normal_pattern_item, $.item)),
          itemEnd($, $._normal_pattern_item_boundary),
        ),
      ),

    _closed_item: ($) =>
      choice(
        $._action_item,
        $._pattern_action_item,
        $._special_pattern_item,
        $._function_item,
      ),

    _action_item: ($) => field("action", $.action),

    _pattern_action_item: ($) =>
      seq(
        field("pattern", $.pattern),
        continuedMember($, "action", field("action", $.action)),
      ),

    // A special pattern requires an action, while a normal pattern alone is
    // an item, so only the special pattern's `pattern` stands without one.
    _special_pattern_item: ($) =>
      seq(
        field("pattern", alias($._special_pattern, $.pattern)),
        missingAction($),
      ),

    _special_pattern: ($) => $.special_pattern,

    _normal_pattern_item: ($) => field("pattern", $.normal_pattern),

    _function_item: ($) =>
      seq($._function_header, choice(functionBody($), missingAction($))),

    _function_header_prefix: ($) =>
      seq(
        $.function_keyword,
        continuedExpressionMember(
          $,
          field("name", choice($.name, $.func_name)),
        ),
        continuedExpressionMember($, "("),
      ),

    _function_header: ($) =>
      seq(
        $._function_header_prefix,
        optional(
          continuedExpressionMember($, field("parameters", $.param_list)),
        ),
        $._continued_close_parenthesis,
      ),

    param_list: ($) => seq($.name, repeat(continuedParameter($))),

    pattern: ($) => choice($.normal_pattern, $.special_pattern),

    normal_pattern: ($) =>
      choice(
        $.expr,
        seq(
          field("left", $.expr),
          continuedMember($, "comma", field("separator", ",")),
          requiredAfterOptionalNewline(
            $,
            $._expression_target_guard,
            continuedExpression($, "right", $.expr),
          ),
        ),
      ),

    special_pattern: ($) => choice($.begin_keyword, $.end_keyword),

    begin_keyword: ($) => $._begin_word,

    end_keyword: ($) => $._end_word,

    function_keyword: ($) => $._function_word,

    // Visible only through aliases; see classOperandName.
    action_boundary_body: ($) =>
      statementListWithTail($, actionBoundaryControlBody($)),

    action_body_boundary_control: ($) =>
      choice(
        seq($._if_header, missingStatement($)),
        seq($._while_header, missingStatement($)),
        seq($._for_header, missingStatement($)),
        ...controlStatements($, actionBoundaryControlBody($)),
      ),

    action: ($) =>
      seq(
        field("opening", "{"),
        newlineLayout($),
        optional(
          continuedStatement(
            $,
            field(
              "body",
              choice(
                $.terminated_statement_list,
                $.unterminated_statement_list,
                alias($.action_boundary_body, $.unterminated_statement_list),
              ),
            ),
          ),
        ),
        optionalContinuationsBefore($, "close_brace"),
        field("closing", "}"),
      ),

    terminated_statement_list: ($) => terminatedStatements($),

    unterminated_statement_list: ($) =>
      statementListWithTail($, $.unterminated_statement),

    _parenthesized_condition: ($) =>
      parenthesized($, field("condition", $.expr)),

    _if_header: ($) => conditionalHeader($, $.if_keyword),

    _else_header: ($) => keywordHeader($, $.else_keyword),

    _while_header: ($) => conditionalHeader($, $.while_keyword),

    _do_header: ($) => keywordHeader($, $.do_keyword),

    _for_classic_clause: ($) =>
      seq(
        optional(continuedSimpleStatement($, "initializer")),
        continuedMember($, "semicolon", ";"),
        optional(continuedExpressionMember($, field("condition", $.expr))),
        continuedMember($, "semicolon", ";"),
        optional(continuedSimpleStatement($, "update")),
      ),

    _for_in_clause: ($) =>
      seq(
        optionalContinuationsBefore($, "simple_statement"),
        field("variable", alias($._for_in_variable_word, $.name)),
        continuedMember($, "membership_operator", $.in_keyword),
        continuedExpressionMember($, field("array", $.name)),
      ),

    _for_header: ($) =>
      header(
        $,
        $.for_keyword,
        seq(
          continuedExpressionMember($, "("),
          choice($._for_classic_clause, $._for_in_clause),
          $._continued_close_parenthesis,
        ),
      ),

    _self_terminating_statement: ($) =>
      choice(...controlStatements($, $.terminated_statement)),

    terminated_statement: ($) =>
      choice(
        seq($.action, newlineLayout($)),
        $._self_terminating_statement,
        seq(field("terminator", ";"), newlineLayout($)),
        statementTerminatedBy($, "newline", $.newline),
        statementTerminatedBy($, "semicolon", ";"),
      ),

    unterminated_statement: ($) =>
      choice(
        field("statement", $.terminatable_statement),
        ...controlStatements($, $.unterminated_statement),
      ),

    terminatable_statement: ($) =>
      choice(
        $.simple_statement,
        $.break_keyword,
        $.continue_keyword,
        $.next_keyword,
        $.nextfile_keyword,
        seq($.exit_keyword, optional(continuedExpressionMember($, $.expr))),
        seq($.return_keyword, optional(continuedExpressionMember($, $.expr))),
        seq(
          $._do_header,
          continuedStatement(
            $,
            field(
              "body",
              choice(
                $.terminated_statement,
                alias($.action_body_boundary_control, $.terminated_statement),
              ),
            ),
          ),
          choice(continuedDoTail($), missingStatement($)),
        ),
      ),

    simple_statement: ($) =>
      choice(
        seq(
          $.delete_keyword,
          continuedExpressionMember($, field("array", $.name)),
          optional(subscript($, field("subscripts", $.expr_list))),
        ),
        $.expr,
        $.print_statement,
      ),

    print_statement: ($) =>
      choice(
        field("statement", $.simple_print_statement),
        seq(
          field("statement", $.simple_print_statement),
          continuedMember(
            $,
            "output_redirection",
            field("redirection", $.output_redirection),
          ),
        ),
      ),

    simple_print_statement: ($) =>
      choice(
        seq(
          $.print_keyword,
          optional(
            continuedExpressionMember($, field("arguments", $.print_expr_list)),
          ),
        ),
        parenthesizedPrintStatement($, $.print_keyword),
        seq(
          $.printf_keyword,
          continuedExpressionMember($, field("arguments", $.print_expr_list)),
        ),
        parenthesizedPrintStatement($, $.printf_keyword),
      ),

    output_redirection: ($) =>
      seq(
        choice(seq($._output_greater_guard, choice(">", $.append)), "|"),
        continuedExpressionMember($, $.expr),
      ),

    print_keyword: ($) => $._print_word,

    printf_keyword: ($) => $._printf_word,

    break_keyword: ($) => $._break_word,

    continue_keyword: ($) => $._continue_word,

    delete_keyword: ($) => $._delete_word,

    do_keyword: ($) => $._do_word,

    else_keyword: ($) => $._else_word,

    exit_keyword: ($) => $._exit_word,

    for_keyword: ($) => $._for_word,

    if_keyword: ($) => $._if_word,

    next_keyword: ($) => $._next_word,

    nextfile_keyword: ($) => $._nextfile_word,

    return_keyword: ($) => $._return_word,

    while_keyword: ($) => $._while_word,

    print_expr_list: ($) =>
      seq($.print_expr, repeat(continuedPrintListElement($, $.print_expr))),

    expr_list: ($) => choice($.expr, $.multiple_expr_list),

    multiple_expr_list: ($) =>
      seq(
        $.expr,
        continuedListElement($, $.expr),
        repeat(continuedListElement($, $.expr)),
      ),

    print_expr: ($) => choice($.unary_print_expr, $.non_unary_print_expr),

    unary_print_expr: ($) => $._print_unary_assignment_expr,

    non_unary_print_expr: ($) => $._print_non_unary_assignment_expr,

    expr: ($) => choice($.unary_expr, $.non_unary_expr),

    unary_expr: ($) => $._normal_unary_assignment_expr,

    non_unary_expr: ($) => $._normal_non_unary_assignment_expr,

    _prefix_update_expr: ($) =>
      seq(
        field("operator", choice($.incr, $.decr)),
        continuedExpression($, "operand", $.lvalue),
      ),

    ...normalExpressionRules,

    ...printExpressionRules,

    // Visible only through aliases; see classOperandName.
    normal_field_expr: ($) =>
      choice(
        alias($.normal_unary_field_expr, $.unary_expr),
        alias($.normal_non_unary_field_expr, $.non_unary_expr),
      ),

    normal_unary_field_expr: ($) =>
      prec(PRECEDENCE.field, $._normal_unary_unary_expr),

    _normal_non_unary_field_atom_expr: ($) =>
      prec(PRECEDENCE.field, nonUnaryAtom($, EXPRESSION_CONTEXT.normal)),

    normal_non_unary_field_expr: ($) =>
      prec(
        PRECEDENCE.field,
        choice(
          $._normal_non_unary_field_atom_expr,
          $._normal_not_expr,
          $._prefix_update_expr,
        ),
      ),

    _parenthesized_expression: ($) => parenthesized($, $.expr),

    _user_function_call: ($) => seq($.func_name, callArguments($)),

    _builtin_function_call: ($) =>
      seq(alias($._builtin_call_word, $.builtin_func_name), callArguments($)),

    lvalue: ($) =>
      choice(
        $.name,
        subscriptedName($, $.expr_list),
        seq(
          field("operator", "$"),
          continuedExpression($, "operand", alias($.normal_field_expr, $.expr)),
        ),
      ),

    non_unary_input_function: ($) =>
      choice(
        field("get", $.simple_get),
        prec.right(
          PRECEDENCE.field,
          seq(
            field("get", $.simple_get),
            $._continued_input_redirect,
            continuedExpression($, "source", $.expr),
          ),
        ),
        prec.right(
          PRECEDENCE.field,
          seq(
            field("source", $.non_unary_expr),
            $._continued_input_pipe,
            continuedPipeGet($),
          ),
        ),
      ),

    unary_input_function: ($) =>
      prec.right(
        PRECEDENCE.field,
        seq(
          field("source", $.unary_expr),
          $._continued_input_pipe,
          continuedPipeGet($),
        ),
      ),

    simple_get: ($) =>
      choice(
        $.getline_keyword,
        seq(
          alias($._getline_target_word, $.getline_keyword),
          continuedExpressionMember($, field("target", $.lvalue)),
        ),
      ),

    getline_keyword: ($) => $._getline_word,

    in_keyword: ($) => $._in_word,

    func_name: ($) => $._func_name_word,

    builtin_func_name: ($) => $._builtin_func_name_word,

    name: ($) => $._name_word,

    number: ($) =>
      choice($._number_integer, $._number_fraction, $._number_exponent),

    string: ($) =>
      seq(
        field("opening", '"'),
        repeat(choice($.string_content, $.escape_sequence)),
        field("closing", token.immediate('"')),
      ),

    string_content: () => token.immediate(prec(1, /[^"\\\n]+/)),

    escape_sequence: ($) =>
      seq(
        $._escape_introducer,
        choice($._escape_character, $._escape_octal_digits),
      ),

    add_assign: ($) => $._add_assign_operator,

    sub_assign: ($) => $._sub_assign_operator,

    mul_assign: ($) => $._mul_assign_operator,

    div_assign: ($) => $._div_assign_operator,

    mod_assign: ($) => $._mod_assign_operator,

    pow_assign: ($) => $._pow_assign_operator,

    or: ($) => $._or_operator,

    and: ($) => $._and_operator,

    no_match: ($) => $._no_match_operator,

    eq: ($) => $._eq_operator,

    le: ($) => $._le_operator,

    ge: ($) => $._ge_operator,

    ne: ($) => $._ne_operator,

    incr: ($) => $._incr_operator,

    decr: ($) => $._decr_operator,

    append: ($) => $._append_operator,

    ere: ($) =>
      seq(
        field("opening", alias($._ere_opening_slash, "/")),
        choice(
          seq(
            field("expression", $.extended_reg_exp),
            field("closing", alias($._ere_closing, "/")),
          ),
          seq(
            optional(field("expression", $.extended_reg_exp)),
            ereLexicalEnd($),
          ),
        ),
      ),

    extended_reg_exp: ($) =>
      choice(
        $.ere_branch,
        seq(
          field("left", $.extended_reg_exp),
          field("operator", token.immediate("|")),
          field("right", $.ere_branch),
        ),
      ),

    ere_branch: ($) =>
      choice(
        $.ere_expression,
        seq(field("left", $.ere_branch), field("right", $.ere_expression)),
      ),

    ere_expression: ($) =>
      choice(
        $.one_char_or_coll_elem_ere,
        $.left_anchor,
        $.right_anchor,
        seq(
          field("opening", token.immediate("(")),
          field("expression", $.extended_reg_exp),
          field("closing", $._ere_close_parenthesis),
        ),
        seq(
          field("operand", $.ere_expression),
          field("operator", $.ere_dupl_symbol),
        ),
      ),

    one_char_or_coll_elem_ere: ($) =>
      choice(
        $.ordinary_character,
        $.quoted_character,
        $.wildcard,
        $.bracket_expression,
        alias($._ere_octal_escape_sequence, $.escape_sequence),
        alias($._ere_undefined_escape_sequence, $.escape_sequence),
      ),

    ordinary_character: ($) =>
      choice(
        $._ordinary_character,
        $._ere_ordinary_close_parenthesis,
        $._ere_ordinary_close_brace,
        $.escaped_delimiter,
        alias($._ere_named_escape_sequence, $.escape_sequence),
      ),

    quoted_character: ($) =>
      alias($._ere_quoted_escape_sequence, $.escape_sequence),

    wildcard: () => token.immediate("."),

    left_anchor: () => token.immediate("^"),

    right_anchor: () => token.immediate("$"),

    ere_dupl_symbol: ($) =>
      prec.right(
        seq(
          choice(
            token.immediate("*"),
            token.immediate("+"),
            token.immediate("?"),
            $._ere_interval,
          ),
          optional(field("modifier", $.repetition_modifier)),
        ),
      ),

    repetition_modifier: () => token.immediate("?"),

    _ere_interval: ($) =>
      seq(
        token.immediate("{"),
        ereRequiredPayload(
          $,
          seq(
            $.dup_count,
            optional(seq(token.immediate(","), optional($.dup_count))),
          ),
          $._ere_close_brace,
        ),
      ),

    dup_count: ($) => $._number_digit_chunk,

    bracket_expression: ($) =>
      seq(
        token.immediate("["),
        choice($.matching_list, $.nonmatching_list),
        $._ere_close_bracket,
      ),

    matching_list: ($) => $.bracket_list,

    nonmatching_list: ($) => seq(token.immediate(prec(3, "^")), $.bracket_list),

    bracket_list: ($) =>
      choice(
        ...ereBracketListAlternatives($, $.follow_list),
        ...ereBracketListAlternatives(
          $,
          alias($._initial_close_follow_list, $.follow_list),
        ),
        ...ereBracketListAlternatives(
          $,
          alias($._initial_hyphen_follow_list, $.follow_list),
        ),
      ),

    ...initialFollowListRules("close", ($) => $._ere_bracket_close_character),

    ...initialFollowListRules("hyphen", ($) =>
      choice($._ere_bracket_hyphen, ereClosingHyphen($)),
    ),

    follow_list: ($) =>
      choice($.expression_term, seq($.follow_list, $.expression_term)),

    expression_term: ($) => choice($.single_expression, $.range_expression),

    single_expression: ($) =>
      choice($.end_range, $.character_class, $.equivalence_class),

    range_expression: ($) => ereRangeExpressionWith($, $.start_range),

    start_range: ($) => seq($.end_range, $._ere_bracket_hyphen),

    end_range: ($) => choice($.collating_element, $.collating_symbol),

    collating_element: ($) =>
      choice(
        $._ere_bracket_character,
        $._ere_bracket_open_character,
        $.escaped_delimiter,
        alias($._ere_bracket_escape_sequence, $.escape_sequence),
      ),

    collating_symbol: ($) =>
      ereCompound(
        $,
        $._ere_open_dot,
        choice(
          alias($._ere_compound_collating_element, $.collating_element),
          $.meta_character,
        ),
        $._ere_dot_close,
      ),

    equivalence_class: ($) =>
      ereCompound(
        $,
        $._ere_open_equal,
        choice(
          alias($._ere_compound_collating_element, $.collating_element),
          alias($._ere_compound_meta_character, $.collating_element),
        ),
        $._ere_equal_close,
      ),

    character_class: ($) =>
      ereCompound($, $._ere_open_colon, $.class_name, $._ere_colon_close),

    class_name: ($) => $._ere_class_name_spelling,

    meta_character: ($) => $._ere_compound_meta_character,

    _ere_compound_collating_element: ($) =>
      choice(
        $._ere_compound_nonmeta_atom,
        seq(
          $._ere_compound_atom,
          $._ere_compound_atom,
          repeat($._ere_compound_atom),
        ),
      ),

    _ere_compound_atom: ($) =>
      choice($._ere_compound_nonmeta_atom, $._ere_compound_meta_character),

    _ere_compound_nonmeta_atom: ($) =>
      choice(
        $._ere_compound_nonmeta_character,
        $.escaped_delimiter,
        alias($._ere_bracket_escape_sequence, $.escape_sequence),
      ),

    _ere_open_dot: ($) => ereCompoundOpening($, "."),

    _ere_dot_close: ($) => ereCompoundClosing($._ere_dot_close_guard, "."),

    _ere_open_equal: ($) => ereCompoundOpening($, "="),

    _ere_equal_close: ($) => ereCompoundClosing($._ere_equal_close_guard, "="),

    _ere_open_colon: ($) => ereCompoundOpening($, ":"),

    _ere_colon_close: ($) => ereCompoundClosing($._ere_colon_close_guard, ":"),

    escaped_delimiter: ($) =>
      seq(
        $._ere_escaped_delimiter_start,
        $._escape_introducer,
        alias($._ere_escaped_delimiter_end, "/"),
      ),

    _ere_named_escape_sequence: ($) =>
      ereEscapeWithCharacter($, $._ere_named_escape_character),

    _ere_quoted_escape_sequence: ($) =>
      ereEscapeWithCharacter($, $._ere_quoted_escape_character),

    _ere_octal_escape_sequence: ($) =>
      ereEscapeWithCharacter($, $._escape_octal_digits),

    _ere_undefined_escape_sequence: ($) =>
      ereEscapeWithCharacter($, $._ere_undefined_escape_character),

    _ere_bracket_escape_sequence: ($) =>
      choice(
        $._ere_named_escape_sequence,
        $._ere_quoted_escape_sequence,
        $._ere_octal_escape_sequence,
        $._ere_undefined_escape_sequence,
      ),

    // Tree-sitter rejects the POSIX bracket spelling for these delimiter
    // characters, so the exclusion sets use its hexadecimal regex escape.
    // The comment extra competes in ERE lexical states, so every ERE token
    // whose set contains "#" carries a lexical precedence above the comment.
    _ordinary_character: () =>
      token.immediate(prec(1, /[^.\x5B\x5C*^$+?{|}()/\n]/)),

    _ere_ordinary_close_parenthesis: () => token.immediate(prec(1, ")")),

    _ere_ordinary_close_brace: () => token.immediate(prec(1, "}")),

    _ere_close_parenthesis: () => token.immediate(prec(2, ")")),

    _ere_close_brace: () => token.immediate(prec(2, "}")),

    _ere_close_bracket: () => token.immediate(prec(3, "]")),

    _ere_bracket_hyphen: () => token.immediate("-"),

    _ere_bracket_character: () =>
      token.immediate(prec(1, /[^\x2D\x2F\x5B\x5C\x5D\n]/)),

    _ere_bracket_open_character: () => token.immediate(prec(1, "[")),

    _ere_bracket_close_character: () => token.immediate(prec(1, "]")),

    _ere_compound_nonmeta_character: () =>
      token.immediate(prec(1, /[^\x2D\x2F\x5C\x5D\x5E\n]/)),

    _ere_compound_meta_character: () => token.immediate(/[\x2D\x5D\x5E]/),

    _ere_class_name_spelling: () => token.immediate(/[A-Za-z][A-Za-z0-9]*/),

    _ere_named_escape_character: () => token.immediate(prec(2, /[abfnrtv]/)),

    _ere_quoted_escape_character: () =>
      token.immediate(prec(2, /[().*+?{}|^$\x5B\x5C\x5D]/)),

    _ere_undefined_escape_character: () =>
      token.immediate(prec(1, /[^0-7\x2F\x5C\n]/)),

    newline_opt: ($) => rawNewlines($),

    terminator: ($) =>
      choice(
        rawNewlines($),
        seq(";", repeat(seq(newlineContinuations($), $.newline))),
      ),

    newline: () => "\n",

    line_continuation: () => token(seq("\\", "\n")),

    comment: () => token(seq("#", /[^\n]*/)),

    _number_digit_chunk: () => token.immediate(/[0-9]+/),

    _escape_introducer: () => token.immediate(/\\/),

    _escape_character: () =>
      choice(token.immediate(/\\/), token.immediate(prec(1, /[^0-7\\\n]/))),

    _escape_octal_digits: () => token.immediate(/[0-7]{1,3}/),
  },
});
