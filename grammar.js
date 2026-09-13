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

const KEYWORDS = [
  "begin",
  "end",
  "function",
  "print",
  "break",
  "continue",
  "delete",
  "do",
  "else",
  "exit",
  "for",
  "if",
  "next",
  "nextfile",
  "printf",
  "return",
  "while",
];

const classifiedToken = ($, classification) =>
  seq(classification, $._token_payload);

const keywordRules = Object.fromEntries(
  KEYWORDS.map((keyword) => [
    `${keyword}_keyword`,
    ($) => classifiedToken($, $[`_${keyword}_word`]),
  ]),
);

const TWO_CHARACTER_TOKENS = [
  "div_assign",
  "add_assign",
  "sub_assign",
  "mul_assign",
  "mod_assign",
  "pow_assign",
  "or",
  "and",
  "no_match",
  "eq",
  "le",
  "ge",
  "ne",
  "incr",
  "decr",
  "append",
];

const SINGLE_CHARACTER_OPERATORS = {
  "+": "plus",
  "-": "minus",
  "*": "star",
  "%": "percent",
  "^": "caret",
  "!": "bang",
  "<": "less",
  ">": "greater",
  "=": "equal",
  "|": "pipe",
};

const singleOperator = ($, character) =>
  alias($[`_${SINGLE_CHARACTER_OPERATORS[character]}`], character);

const singleCharacterTokenRules = Object.fromEntries(
  Object.values(SINGLE_CHARACTER_OPERATORS).map((name) => [
    `_${name}`,
    ($) => seq($[`_${name}_operator`], $._token_whole),
  ]),
);

const twoCharacterTokenRules = Object.fromEntries(
  TWO_CHARACTER_TOKENS.map((name) => [
    name,
    ($) => classifiedToken($, $[`_${name}_operator`]),
  ]),
);

const newlineLayout = ($) => optional($.newline_opt);

const afterOptionalNewline = ($, member) => seq(newlineLayout($), member);

const functionBody = ($) => afterOptionalNewline($, field("body", $.action));

// A separate closing-hyphen token avoids competing with range separators.
const ereClosingHyphen = ($) => alias($._ere_closing_hyphen, "-");

const ereBracketListAlternatives = ($, followList) => [
  followList,
  seq(followList, ereClosingHyphen($)),
];

const ereRangeExpressionWith = ($, startRange) =>
  seq(
    startRange,
    choice($.end_range, ereClosingHyphen($), $._ere_bracket_hyphen),
  );

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
  seq(alias($._ere_compound_opening, "["), token.immediate(punctuation));

const ereCompoundClosing = (closing, punctuation) =>
  seq(alias(closing, punctuation), token.immediate("]"));

const ereExpressionRules = (insideGroup) => {
  const name = (rule) => (insideGroup ? `group_${rule}` : rule);
  const member = ($, rule) =>
    insideGroup ? alias($[name(rule)], $[rule]) : $[rule];
  return {
    [name("extended_reg_exp")]: ($) =>
      choice(
        member($, "ere_branch"),
        seq(
          field("left", member($, "extended_reg_exp")),
          field("operator", token.immediate("|")),
          field("right", member($, "ere_branch")),
        ),
      ),

    [name("ere_branch")]: ($) =>
      choice(
        member($, "ere_expression"),
        seq(
          field("left", member($, "ere_branch")),
          field("right", member($, "ere_expression")),
        ),
      ),

    [name("ere_expression")]: ($) =>
      choice(
        member($, "one_char_or_coll_elem_ere"),
        $.left_anchor,
        $.right_anchor,
        seq(
          field("opening", token.immediate("(")),
          field(
            "expression",
            alias($.group_extended_reg_exp, $.extended_reg_exp),
          ),
          field("closing", $._ere_close_parenthesis),
        ),
        seq(
          field("operand", member($, "ere_expression")),
          field("operator", $.ere_dupl_symbol),
        ),
      ),

    [name("one_char_or_coll_elem_ere")]: ($) =>
      choice(
        member($, "ordinary_character"),
        $.quoted_character,
        $.wildcard,
        $.bracket_expression,
        alias($.ere_octal_escape_sequence, $.escape_sequence),
        alias($.ere_undefined_escape_sequence, $.escape_sequence),
      ),

    [name("ordinary_character")]: ($) =>
      choice(
        alias($._ordinary_character, $.ordinary_character_content),
        ...(insideGroup ? [] : [$._ere_ordinary_close_parenthesis]),
        $._ere_ordinary_close_brace,
        $.escaped_delimiter,
        alias($.ere_named_escape_sequence, $.escape_sequence),
      ),
  };
};

const header = ($, keyword, rest) => seq(keyword, rest, newlineLayout($));

const conditionalHeader = ($, keyword) =>
  header($, keyword, $._parenthesized_condition);

const keywordHeader = ($, keyword) => seq(keyword, newlineLayout($));

const doWhileTail = ($) => seq($.while_keyword, $._parenthesized_condition);

const controlStatements = ($, body) => [
  prec.right(seq($._if_header, field("consequence", body))),
  seq(
    $._if_header,
    field("consequence", $.terminated_statement),
    $._else_header,
    field("alternative", body),
  ),
  seq($._while_header, field("body", body)),
  seq($._for_header, field("body", body)),
];

const statementTerminatedBy = ($, terminator) =>
  seq(
    field("statement", $.terminatable_statement),
    field("terminator", terminator),
    newlineLayout($),
  );

const parenthesized = (member) => seq("(", member, ")");

const parenthesizedPrintStatement = ($, keyword) =>
  seq(keyword, parenthesized(field("arguments", $.multiple_expr_list)));

const callArguments = ($) => parenthesized(optional($.expr_list));

const subscript = (subscripts) => seq("[", subscripts, "]");

const terminatedStatements = ($) => repeat1($.terminated_statement);

const statementListWithTail = ($, tail) =>
  seq(repeat($.terminated_statement), tail);

const rawNewlines = ($) => repeat1($.newline);

const EXPRESSION_CONTEXT = {
  normal: {
    prefix: "normal",
    expression: "expr",
    unaryExpression: "unary_expr",
    nonUnaryExpression: "non_unary_expr",
    comparison: true,
    input: ($) =>
      choice(
        alias($.direct_input_function, $.non_unary_input_function),
        alias($.piped_input_function, $.non_unary_input_function),
      ),
    unaryInput: true,
  },
  print: {
    prefix: "print",
    expression: "print_expr",
    unaryExpression: "unary_print_expr",
    nonUnaryExpression: "non_unary_print_expr",
    comparison: false,
  },
  field: {
    prefix: "field",
    expression: "expr",
    unaryExpression: "unary_expr",
    nonUnaryExpression: "non_unary_expr",
    input: ($) => alias($.direct_input_function, $.non_unary_input_function),
  },
};

const classTierName = (context, classification, tier) =>
  `_${context.prefix}_${classification}_${tier}_expr`;

// Hidden operand rules would forward child fields into the parent, making
// ts_node_child_by_field_name return an inner operand. Visible rules with
// aliases preserve field ownership without exposing extra node types.
const classOperandName = (context, classification, tier) =>
  `${context.prefix}_${classification}_${tier}_operand`;

const anyTierName = (context, tier) => `${context.prefix}_${tier}_expr`;

const classTier = ($, context, classification, tier) =>
  $[classTierName(context, classification, tier)];

const aliasedClassTier = ($, context, classification, tier) =>
  alias(
    $[classOperandName(context, classification, tier)],
    $[
      classification === "unary"
        ? context.unaryExpression
        : context.nonUnaryExpression
    ],
  );

const CLASSIFICATIONS = ["unary", "non_unary"];

const aliasedAnyTier = ($, context, tier) =>
  alias($[anyTierName(context, tier)], $[context.expression]);

const nonUnaryAtom = ($, context) => {
  const atoms = [
    $._parenthesized_expression,
    $.number,
    $.string,
    $.lvalue,
    $._user_function_call,
    $._builtin_function_call,
    $.builtin_func_name,
    $.ere,
  ];
  if (context.input) {
    atoms.push(context.input($));
  }
  return choice(...atoms);
};

const classOperandRules = (context, classification, tier) => ({
  [classOperandName(context, classification, tier)]: ($) =>
    prec(1, classTier($, context, classification, tier)),
});

const anyTierRules = (context, tier) => ({
  ...classOperandRules(context, "unary", tier),
  ...classOperandRules(context, "non_unary", tier),
  [anyTierName(context, tier)]: ($) =>
    choice(
      aliasedClassTier($, context, "unary", tier),
      aliasedClassTier($, context, "non_unary", tier),
    ),
});

const unaryExpressionRules = (context) => {
  const rules = anyTierRules(context, "unary");
  const unary = (tier) => classTierName(context, "unary", tier);
  const nonUnary = (tier) => classTierName(context, "non_unary", tier);
  const not = `_${context.prefix}_not_expr`;
  const addOperand = (classification, tier) =>
    Object.assign(rules, classOperandRules(context, classification, tier));
  rules[unary("unary")] = ($) =>
    choice(
      ...(context.unaryInput
        ? [classTier($, context, "unary", "exponentiation")]
        : []),
      ...["+", "-"].map((operator) =>
        seq(
          field("operator", singleOperator($, operator)),
          field("operand", aliasedAnyTier($, context, "unary")),
        ),
      ),
    );
  rules[not] = ($) =>
    seq(
      field("operator", singleOperator($, "!")),
      field("operand", aliasedAnyTier($, context, "unary")),
    );
  rules[nonUnary("unary")] = ($) =>
    choice($[nonUnary("exponentiation")], $[not]);

  const exponentiationTail = `_${context.prefix}_exponentiation_tail`;
  rules[exponentiationTail] = ($) =>
    seq(
      $._exponentiation_operator,
      field("right", aliasedAnyTier($, context, "unary")),
    );
  const exponentiationClassifications = context.unaryInput
    ? CLASSIFICATIONS
    : ["non_unary"];
  for (const classification of exponentiationClassifications) {
    addOperand(classification, "update");
    rules[classTierName(context, classification, "exponentiation")] = ($) =>
      choice(
        classTier($, context, classification, "update"),
        seq(
          field("left", aliasedClassTier($, context, classification, "update")),
          $[exponentiationTail],
        ),
      );
  }
  if (context.unaryInput) {
    rules[unary("update")] = ($) => $.unary_input_function;
  }

  rules[nonUnary("update")] = ($) =>
    choice(
      $[nonUnary("atom")],
      $._prefix_update_expr,
      prec.left(
        PRECEDENCE.postfixUpdate,
        seq(
          field("operand", $.lvalue),
          field("operator", choice($.incr, $.decr)),
        ),
      ),
    );

  rules[nonUnary("atom")] = ($) => nonUnaryAtom($, context);

  return rules;
};

const tieredExpressionRules = (context) => {
  const rules = {};
  const unary = (tier) => classTierName(context, "unary", tier);
  const nonUnary = (tier) => classTierName(context, "non_unary", tier);
  const addOperand = (classification, tier) =>
    Object.assign(rules, classOperandRules(context, classification, tier));
  const conditionalTail = `_${context.prefix}_conditional_tail`;
  const expression = ($) => $[context.expression];
  rules[conditionalTail] = ($) =>
    seq(
      "?",
      field("consequence", expression($)),
      ":",
      field("alternative", aliasedAnyTier($, context, "conditional")),
    );

  const addAnyTier = (tier) =>
    Object.assign(rules, anyTierRules(context, tier));

  const addLeftAssociativeTier = (tier, nextTier, operator, precedence) => {
    addAnyTier(nextTier);
    const tail = `_${context.prefix}_${tier}_tail`;
    rules[tail] = ($) =>
      seq(operator($), field("right", aliasedAnyTier($, context, nextTier)));
    for (const classification of CLASSIFICATIONS) {
      addOperand(classification, tier);
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

  const addNonAssociativeTier = (tier, nextTier, operator, precedence) => {
    addAnyTier(nextTier);
    const tail = `_${context.prefix}_${tier}_tail`;
    rules[tail] = ($) =>
      seq(operator($), field("right", aliasedAnyTier($, context, nextTier)));
    for (const classification of CLASSIFICATIONS) {
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
          field(
            "operator",
            choice(
              $.pow_assign,
              $.mod_assign,
              $.mul_assign,
              $.div_assign,
              $.add_assign,
              $.sub_assign,
              singleOperator($, "="),
            ),
          ),
          field("right", expression($)),
        ),
      ),
    );

  addAnyTier("conditional");
  for (const classification of CLASSIFICATIONS) {
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

  const addNewlineSeparatedTier = (tier, nextTier, operator, precedence) => {
    addAnyTier(nextTier);
    const tail = `_${context.prefix}_${tier}_tail`;
    rules[tail] = ($) =>
      seq(
        operator($),
        afterOptionalNewline(
          $,
          field("right", aliasedAnyTier($, context, nextTier)),
        ),
      );
    for (const classification of CLASSIFICATIONS) {
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

  addNewlineSeparatedTier(
    "logical_or",
    "logical_and",
    ($) => $._logical_or_operator,
    PRECEDENCE.logicalOr,
  );

  addNewlineSeparatedTier(
    "logical_and",
    "membership",
    ($) => $._logical_and_operator,
    PRECEDENCE.logicalAnd,
  );

  const membershipTail = `_${context.prefix}_membership_tail`;
  rules[membershipTail] = ($) =>
    seq($._membership_operator, field("right", $.name));
  for (const classification of CLASSIFICATIONS) {
    rules[classTierName(context, classification, "membership")] = ($) => {
      const members = [
        classTier($, context, classification, "match"),
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
            parenthesized(field("left", $.multiple_expr_list)),
            $._membership_operator,
            field("right", $.name),
          ),
        );
      }
      return choice(...members);
    };
  }

  const comparisonTier = context.comparison ? "comparison" : "concatenation";
  addNonAssociativeTier(
    "match",
    comparisonTier,
    ($) => $._match_operator,
    PRECEDENCE.match,
  );

  if (context.comparison) {
    addNonAssociativeTier(
      "comparison",
      "concatenation",
      ($) => $._comparison_operator,
      PRECEDENCE.comparison,
    );
  }

  const concatenationTail = `_${context.prefix}_concatenation_tail`;
  rules[concatenationTail] = ($) =>
    field("right", aliasedClassTier($, context, "non_unary", "additive"));
  for (const classification of CLASSIFICATIONS) {
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
      );
  }

  addLeftAssociativeTier(
    "additive",
    "multiplicative",
    ($) => $._additive_operator,
    PRECEDENCE.additive,
  );

  addLeftAssociativeTier(
    "multiplicative",
    "unary",
    ($) => $._multiplicative_operator,
    PRECEDENCE.multiplicative,
  );

  return { ...rules, ...unaryExpressionRules(context) };
};

const normalExpressionRules = tieredExpressionRules(EXPRESSION_CONTEXT.normal);
const printExpressionRules = tieredExpressionRules(EXPRESSION_CONTEXT.print);
const fieldExpressionRules = unaryExpressionRules(EXPRESSION_CONTEXT.field);

const listElementTail = ($, element) =>
  seq(",", afterOptionalNewline($, element));

export default grammar({
  name: "posix_awk",

  externals: ($) => [
    ...KEYWORDS.map((keyword) => $[`_${keyword}_word`]),
    $._name_word,
    $._for_in_variable_word,
    $._getline_word,
    $._in_word,
    $._builtin_func_name_word,
    $._builtin_call_word,
    $._func_name_word,
    $._number_integer,
    $._number_fraction,
    $._number_exponent,
    $._division_slash_guard,
    $._ere_opening_slash_guard,
    ...TWO_CHARACTER_TOKENS.map((name) => $[`_${name}_operator`]),
    ...Object.values(SINGLE_CHARACTER_OPERATORS).map(
      (name) => $[`_${name}_operator`],
    ),
    $._output_greater_guard,
    $._ere_compound_opening,
    $._ere_dot_closing,
    $._ere_equal_closing,
    $._ere_colon_closing,
    $._ere_bracket_literal_open,
    $._ere_compound_content,
    $._ere_closing_hyphen,
    $._ere_closing,
    $._string_opening,
    $._string_end,
    $._string_content_guard,
    $._string_escape_guard,
    $._ere_named_escape_guard,
    $._ere_quoted_escape_guard,
    $._ere_octal_escape_guard,
    $._ere_undefined_escape_guard,
    $._ere_escaped_delimiter_guard,
    $._ere_class_name_guard,
    $._ere_dup_count_guard,
    $._token_whole,
    $._token_content,
    $._token_final_content,
    $._token_line_continuation,
    $.comment,
    $.line_continuation,
    $._error_sentinel,
  ],

  extras: ($) => [
    token(repeat1(choice(" ", "\t"))),
    $.comment,
    $.line_continuation,
  ],

  inline: ($) => [
    $._item,
    $._normal_unary_assignment_expr,
    $._normal_unary_update_expr,
    $._print_unary_assignment_expr,
  ],

  conflicts: ($) => [
    [$.simple_get],
    // The final item joins item_list only after its terminator is known.
    [$.item_list, $._item_list],
  ],

  rules: {
    program: ($) => seq(optional($.item_list), optional($._item)),

    _token_payload: ($) =>
      choice(
        $._token_whole,
        seq(
          repeat1(
            choice(
              field("content", alias($._token_content, $.token_content)),
              alias($._token_line_continuation, $.line_continuation),
            ),
          ),
          field("content", alias($._token_final_content, $.token_content)),
        ),
      ),

    _division_slash: ($) => seq($._division_slash_guard, $._token_whole),

    _ere_opening_slash: ($) => seq($._ere_opening_slash_guard, $._token_whole),

    ...singleCharacterTokenRules,

    _additive_operator: ($) =>
      field("operator", choice(singleOperator($, "+"), singleOperator($, "-"))),

    _multiplicative_operator: ($) =>
      field(
        "operator",
        choice(
          singleOperator($, "*"),
          alias($._division_slash, "/"),
          singleOperator($, "%"),
        ),
      ),

    _exponentiation_operator: ($) => field("operator", singleOperator($, "^")),

    _comparison_operator: ($) =>
      field(
        "operator",
        choice(
          singleOperator($, "<"),
          $.le,
          $.ne,
          $.eq,
          singleOperator($, ">"),
          $.ge,
        ),
      ),

    _match_operator: ($) => field("operator", choice("~", $.no_match)),

    _membership_operator: ($) => field("operator", $.in_keyword),

    _logical_and_operator: ($) => field("operator", $.and),

    _logical_or_operator: ($) => field("operator", $.or),

    item_list: ($) => $._item_list,

    _item_list: ($) =>
      choice(
        field("leading", $.newline_opt),
        seq(optional($._item_list), $._terminated_item),
      ),

    _item: ($) =>
      choice(
        alias($.closed_item, $.item),
        alias($.normal_pattern_item, $.item),
      ),

    _terminated_item: ($) =>
      seq(field("item", $._item), field("terminator", $.terminator)),

    closed_item: ($) =>
      choice($._action_item, $._pattern_action_item, $._function_item),

    _action_item: ($) => field("action", $.action),

    _pattern_action_item: ($) =>
      seq(field("pattern", $.pattern), field("action", $.action)),

    normal_pattern_item: ($) => field("pattern", $.normal_pattern),

    _function_item: ($) => seq($._function_header, functionBody($)),

    _function_header_prefix: ($) =>
      seq($.function_keyword, field("name", choice($.name, $.func_name)), "("),

    _function_header: ($) =>
      seq(
        $._function_header_prefix,
        optional(field("parameters", $.param_list)),
        ")",
      ),

    param_list: ($) => seq($.name, repeat(seq(",", $.name))),

    pattern: ($) => choice($.normal_pattern, $.special_pattern),

    normal_pattern: ($) =>
      choice(
        $.expr,
        seq(
          field("left", $.expr),
          field("separator", ","),
          afterOptionalNewline($, field("right", $.expr)),
        ),
      ),

    special_pattern: ($) => choice($.begin_keyword, $.end_keyword),

    ...keywordRules,

    action: ($) =>
      seq(
        field("opening", "{"),
        newlineLayout($),
        optional(
          field(
            "body",
            choice($.terminated_statement_list, $.unterminated_statement_list),
          ),
        ),
        field("closing", "}"),
      ),

    terminated_statement_list: ($) => terminatedStatements($),

    unterminated_statement_list: ($) =>
      statementListWithTail($, $.unterminated_statement),

    _parenthesized_condition: ($) => parenthesized(field("condition", $.expr)),

    _if_header: ($) => conditionalHeader($, $.if_keyword),

    _else_header: ($) => keywordHeader($, $.else_keyword),

    _while_header: ($) => conditionalHeader($, $.while_keyword),

    _do_header: ($) => keywordHeader($, $.do_keyword),

    _for_classic_clause: ($) =>
      seq(
        optional(field("initializer", $.simple_statement)),
        ";",
        optional(field("condition", $.expr)),
        ";",
        optional(field("update", $.simple_statement)),
      ),

    _for_in_clause: ($) =>
      seq(
        field("variable", alias($.for_in_variable, $.name)),
        $.in_keyword,
        field("array", $.name),
      ),

    for_in_variable: ($) => classifiedToken($, $._for_in_variable_word),

    _for_header: ($) =>
      header(
        $,
        $.for_keyword,
        seq("(", choice($._for_classic_clause, $._for_in_clause), ")"),
      ),

    _self_terminating_statement: ($) =>
      choice(...controlStatements($, $.terminated_statement)),

    terminated_statement: ($) =>
      choice(
        seq($.action, newlineLayout($)),
        $._self_terminating_statement,
        seq(field("terminator", ";"), newlineLayout($)),
        statementTerminatedBy($, $.newline),
        statementTerminatedBy($, ";"),
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
        seq($.exit_keyword, optional($.expr)),
        seq($.return_keyword, optional($.expr)),
        seq(
          $._do_header,
          field("body", $.terminated_statement),
          doWhileTail($),
        ),
      ),

    simple_statement: ($) =>
      choice(
        seq(
          $.delete_keyword,
          field("array", $.name),
          optional(subscript(field("subscripts", $.expr_list))),
        ),
        $.expr,
        $.print_statement,
      ),

    print_statement: ($) =>
      seq(
        field("statement", $.simple_print_statement),
        optional(field("redirection", $.output_redirection)),
      ),

    simple_print_statement: ($) =>
      choice(
        seq($.print_keyword, optional(field("arguments", $.print_expr_list))),
        parenthesizedPrintStatement($, $.print_keyword),
        seq($.printf_keyword, field("arguments", $.print_expr_list)),
        parenthesizedPrintStatement($, $.printf_keyword),
      ),

    output_redirection: ($) =>
      seq(
        choice(
          seq(
            $._output_greater_guard,
            choice(singleOperator($, ">"), $.append),
          ),
          singleOperator($, "|"),
        ),
        $.expr,
      ),

    print_expr_list: ($) =>
      seq($.print_expr, repeat(listElementTail($, $.print_expr))),

    expr_list: ($) => choice($.expr, $.multiple_expr_list),

    multiple_expr_list: ($) =>
      seq(
        $.expr,
        listElementTail($, $.expr),
        repeat(listElementTail($, $.expr)),
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
        field("operand", $.lvalue),
      ),

    ...normalExpressionRules,

    ...printExpressionRules,

    // A pending field operand cannot start a competing pipe source.
    ...fieldExpressionRules,

    normal_field_expr: ($) =>
      choice(
        alias($.normal_unary_field_expr, $.unary_expr),
        alias($.normal_non_unary_field_expr, $.non_unary_expr),
      ),

    normal_unary_field_expr: ($) =>
      prec(PRECEDENCE.field, $._field_unary_unary_expr),

    _normal_non_unary_field_atom_expr: ($) =>
      prec(PRECEDENCE.field, nonUnaryAtom($, EXPRESSION_CONTEXT.field)),

    normal_non_unary_field_expr: ($) =>
      prec(
        PRECEDENCE.field,
        choice(
          $._normal_non_unary_field_atom_expr,
          $._field_not_expr,
          $._prefix_update_expr,
        ),
      ),

    _parenthesized_expression: ($) => parenthesized($.expr),

    _user_function_call: ($) => seq($.func_name, callArguments($)),

    _builtin_function_call: ($) =>
      seq(alias($.builtin_call_name, $.builtin_func_name), callArguments($)),

    builtin_call_name: ($) => classifiedToken($, $._builtin_call_word),

    lvalue: ($) =>
      choice(
        $.name,
        seq($.name, subscript($.expr_list)),
        seq(
          field("operator", "$"),
          field("operand", alias($.normal_field_expr, $.expr)),
        ),
      ),

    direct_input_function: ($) =>
      choice(
        field("get", $.simple_get),
        prec.right(
          PRECEDENCE.field,
          seq(
            field("get", $.simple_get),
            singleOperator($, "<"),
            field("source", $.expr),
          ),
        ),
      ),

    piped_input_function: ($) =>
      prec.right(
        PRECEDENCE.field,
        seq(
          field("source", $.non_unary_expr),
          singleOperator($, "|"),
          field("get", $.simple_get),
        ),
      ),

    unary_input_function: ($) =>
      prec.right(
        PRECEDENCE.field,
        seq(
          field("source", $.unary_expr),
          singleOperator($, "|"),
          field("get", $.simple_get),
        ),
      ),

    simple_get: ($) =>
      choice(
        $.getline_keyword,
        prec.dynamic(1, seq($.getline_keyword, field("target", $.lvalue))),
      ),

    getline_keyword: ($) => classifiedToken($, $._getline_word),

    in_keyword: ($) => classifiedToken($, $._in_word),

    func_name: ($) => classifiedToken($, $._func_name_word),

    builtin_func_name: ($) => classifiedToken($, $._builtin_func_name_word),

    name: ($) => classifiedToken($, $._name_word),

    number: ($) =>
      classifiedToken(
        $,
        choice($._number_integer, $._number_fraction, $._number_exponent),
      ),

    string: ($) =>
      seq(
        field("opening", alias($._string_opening, '"')),
        repeat(choice($.string_content, $.escape_sequence)),
        $._string_end,
        field("closing", token.immediate('"')),
      ),

    string_content: ($) => classifiedToken($, $._string_content_guard),

    escape_sequence: ($) => classifiedToken($, $._string_escape_guard),

    ...twoCharacterTokenRules,

    ere: ($) =>
      seq(
        field("opening", alias($._ere_opening_slash, "/")),
        field("expression", $.extended_reg_exp),
        field("closing", alias($._ere_closing, "/")),
      ),

    ...ereExpressionRules(false),
    ...ereExpressionRules(true),

    quoted_character: ($) =>
      alias($.ere_quoted_escape_sequence, $.escape_sequence),

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
        $.dup_count,
        optional(seq(token.immediate(","), optional($.dup_count))),
        $._ere_close_brace,
      ),

    dup_count: ($) => classifiedToken($, $._ere_dup_count_guard),

    bracket_expression: ($) =>
      seq(
        token.immediate("["),
        choice($.matching_list, $.nonmatching_list),
        $._ere_close_bracket,
      ),

    matching_list: ($) => $.bracket_list,

    nonmatching_list: ($) => seq(token.immediate(prec(1, "^")), $.bracket_list),

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

    ...initialFollowListRules("close", ($) => $._ere_initial_close),

    ...initialFollowListRules("hyphen", ($) => $._ere_initial_hyphen),

    _ere_initial_close: ($) =>
      alias(token.immediate("]"), $.collating_element_content),

    // Inlining this choice would let the anonymous hyphen alias replace the
    // collating_element wrapper.
    _ere_initial_hyphen: ($) =>
      choice($._ere_bracket_hyphen, ereClosingHyphen($)),

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
        alias($._ere_bracket_character, $.collating_element_content),
        alias($._ere_bracket_literal_open, $.collating_element_content),
        $.escaped_delimiter,
        alias($.ere_bracket_escape_sequence, $.escape_sequence),
      ),

    collating_symbol: ($) =>
      seq(
        $._ere_open_dot,
        choice(
          alias($._ere_compound_collating_element, $.collating_element),
          $.meta_character,
        ),
        $._ere_dot_close,
      ),

    equivalence_class: ($) =>
      seq(
        $._ere_open_equal,
        alias($._ere_compound_collating_element, $.collating_element),
        $._ere_equal_close,
      ),

    character_class: ($) =>
      seq($._ere_open_colon, $.class_name, $._ere_colon_close),

    class_name: ($) => classifiedToken($, $._ere_class_name_guard),

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
      choice(
        $._ere_compound_nonmeta_atom,
        alias($._ere_compound_meta_character, $.collating_element_content),
      ),

    _ere_compound_nonmeta_atom: ($) =>
      choice(
        alias($._ere_compound_nonmeta_character, $.collating_element_content),
        alias($._ere_compound_content, $.collating_element_content),
        $.escaped_delimiter,
        alias($.ere_bracket_escape_sequence, $.escape_sequence),
      ),

    _ere_open_dot: ($) => ereCompoundOpening($, "."),

    _ere_dot_close: ($) => ereCompoundClosing($._ere_dot_closing, "."),

    _ere_open_equal: ($) => ereCompoundOpening($, "="),

    _ere_equal_close: ($) => ereCompoundClosing($._ere_equal_closing, "="),

    _ere_open_colon: ($) => ereCompoundOpening($, ":"),

    _ere_colon_close: ($) => ereCompoundClosing($._ere_colon_closing, ":"),

    escaped_delimiter: ($) =>
      classifiedToken($, $._ere_escaped_delimiter_guard),

    ere_named_escape_sequence: ($) =>
      classifiedToken($, $._ere_named_escape_guard),

    ere_quoted_escape_sequence: ($) =>
      classifiedToken($, $._ere_quoted_escape_guard),

    ere_octal_escape_sequence: ($) =>
      classifiedToken($, $._ere_octal_escape_guard),

    ere_undefined_escape_sequence: ($) =>
      classifiedToken($, $._ere_undefined_escape_guard),

    ere_bracket_escape_sequence: ($) =>
      classifiedToken(
        $,
        choice(
          $._ere_named_escape_guard,
          $._ere_quoted_escape_guard,
          $._ere_octal_escape_guard,
          $._ere_undefined_escape_guard,
        ),
      ),

    // Tree-sitter rejects the POSIX bracket spelling for these delimiter
    // characters, so the exclusion sets use its hexadecimal regex escape.
    _ordinary_character: () => token.immediate(/[^.\x5B\x5C*^$+?{|}()/\n]/),

    _ere_ordinary_close_parenthesis: () => token.immediate(")"),

    _ere_ordinary_close_brace: () => token.immediate("}"),

    _ere_close_parenthesis: () => token.immediate(prec(1, ")")),

    _ere_close_brace: () => token.immediate(prec(1, "}")),

    _ere_close_bracket: () => token.immediate(prec(1, "]")),

    _ere_bracket_hyphen: () => token.immediate("-"),

    _ere_bracket_character: () => token.immediate(/[^\x2D\x2F\x5B\x5C\x5D\n]/),

    _ere_compound_nonmeta_character: () =>
      token.immediate(/[^.:=\x2D\x2F\x5C\x5D\n]/),

    _ere_compound_meta_character: () => token.immediate(/[\x2D\x5D]/),

    newline_opt: ($) => rawNewlines($),

    terminator: ($) => choice(rawNewlines($), seq(";", repeat($.newline))),

    newline: () => "\n",
  },
});
