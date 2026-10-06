export const EXPRESSION = {
  assignment: { precedence: 2, associativity: "right" },
  conditional: { precedence: 3, associativity: "right" },
  logical_or: { precedence: 4, associativity: "left" },
  logical_and: { precedence: 6, associativity: "left" },
  membership: { precedence: 8, associativity: "left" },
  match: { precedence: 10, associativity: "none" },
  comparison: { precedence: 12, associativity: "none" },
  concatenation: { precedence: 14, associativity: "left" },
  additive: { precedence: 16, associativity: "left" },
  multiplicative: { precedence: 18, associativity: "left" },
  unary: { precedence: 20, associativity: "right" },
  exponentiation: {
    precedence: 21,
    associativity: "right",
    rightOperand: "unary",
  },
  postfix: { precedence: 23, associativity: "left" },
  field: { precedence: 24, associativity: "right" },
};

export const RESERVED_WORDS = {
  keyword: [
    "BEGIN",
    "END",
    "break",
    "continue",
    "delete",
    "do",
    "else",
    "exit",
    "for",
    "function",
    "if",
    "next",
    "nextfile",
    "print",
    "printf",
    "return",
    "while",
  ],
  membership: ["in"],
  getline: ["getline"],
  builtin: [
    "atan2",
    "close",
    "cos",
    "exp",
    "fflush",
    "gsub",
    "index",
    "int",
    "length",
    "log",
    "match",
    "rand",
    "sin",
    "split",
    "sprintf",
    "sqrt",
    "srand",
    "sub",
    "substr",
    "system",
    "tolower",
    "toupper",
  ],
};

const OPERAND_PRECEDENCE = 2;
const CLASSIFICATIONS = ["unary", "non_unary"];

function nextExpressionTier(tier, comparison) {
  return Object.keys(EXPRESSION)
    .filter(
      (name) =>
        (comparison || name !== "comparison") &&
        EXPRESSION[name].precedence > EXPRESSION[tier].precedence,
    )
    .sort(
      (left, right) =>
        EXPRESSION[left].precedence - EXPRESSION[right].precedence,
    )[0];
}

const bind = ({ precedence, associativity }, rule) =>
  associativity === "left"
    ? prec.left(precedence, rule)
    : associativity === "right"
      ? prec.right(precedence, rule)
      : prec(precedence, rule);

// Skipping blanks can reach a backslash the external scanner never saw.
const continuationMarker = () => token.immediate("\\");

const signOperator = ($, character) =>
  alias(character === "+" ? $._plus_operator : $._minus_operator, character);

const twoCharacterTokenRules = Object.fromEntries(
  Object.entries({
    add_assign: "+=",
    sub_assign: "-=",
    mul_assign: "*=",
    div_assign: "/=",
    mod_assign: "%=",
    pow_assign: "^=",
    or: "||",
    and: "&&",
    no_match: "!~",
    eq: "==",
    le: "<=",
    ge: ">=",
    ne: "!=",
    append: ">>",
  }).map(([name, spelling]) => [name, () => spelling]),
);

const trailingUpdateOperators = ($) => [
  alias($.trailing_incr, $.incr),
  alias($.trailing_decr, $.decr),
];

const newlineLayout = ($) => optional($.newline_opt);

const afterOptionalNewline = ($, member) => seq(newlineLayout($), member);

const ereClosingHyphen = ($) => alias($._ere_closing_hyphen, "-");

const ereRangeExpressionWith = ($, startRange) =>
  seq(
    startRange,
    choice($.end_range, ereClosingHyphen($), $._ere_bracket_hyphen),
  );

const initialFollowListRules = (prefix, firstElement) => {
  const name = (part) => `_initial_${prefix}_${part}`;
  return {
    [name("follow_list")]: ($) =>
      seq(
        alias($[name("expression_term")], $.expression_term),
        repeat($.expression_term),
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
    [name("end_range")]: ($) => firstElement($),
  };
};

const ereCompoundOpening = ($, punctuation, escaped = false) =>
  seq(
    alias(
      escaped ? $._ere_escaped_compound_opening : $._ere_compound_opening,
      "[",
    ),
    token.immediate(punctuation),
  );

const ereCompoundClosing = (closing, punctuation) =>
  seq(alias(closing, punctuation), token.immediate("]"));

const ereExpressionRules = (insideGroup) => {
  const name = (rule) => (insideGroup ? `group_${rule}` : rule);
  const member = ($, rule) =>
    insideGroup ? alias($[name(rule)], $[rule]) : $[rule];
  return {
    [name("extended_reg_exp")]: ($) =>
      seq(
        member($, "ere_branch"),
        repeat(seq(token.immediate("|"), member($, "ere_branch"))),
      ),

    [name("ere_branch")]: ($) => repeat1(member($, "ere_expression")),

    [name("ere_expression")]: ($) =>
      seq(
        choice(
          member($, "one_char_or_coll_elem_ere"),
          token.immediate("^"),
          token.immediate("$"),
          seq(
            field("opening", token.immediate("(")),
            optional(
              field(
                "expression",
                alias($.group_extended_reg_exp, $.extended_reg_exp),
              ),
            ),
            field("closing", $._ere_close_parenthesis),
          ),
        ),
        repeat($.ere_dupl_symbol),
      ),

    [name("one_char_or_coll_elem_ere")]: ($) =>
      choice(
        alias($._ordinary_character, $.ordinary_character),
        ...(insideGroup
          ? []
          : [alias(token.immediate(")"), $.ordinary_character)]),
        alias(token.immediate("}"), $.ordinary_character),
        $.ordinary_character,
        $.quoted_character,
        token.immediate("."),
        $.bracket_expression,
        alias($.ere_octal_escape_sequence, $.escape_sequence),
        alias($.ere_undefined_escape_sequence, $.escape_sequence),
      ),
  };
};

const header = ($, keyword, rest) => seq(keyword, rest, newlineLayout($));

const conditionalHeader = ($, keyword) =>
  header($, keyword, $._parenthesized_condition);

const keywordHeader = ($, keyword) => seq(keyword, newlineLayout($));

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

const parenthesized = (member) => seq("(", member, ")");

const parenthesizedPrintStatement = ($, keyword) =>
  seq(keyword, parenthesized(field("arguments", $.multiple_expr_list)));

const callArguments = ($) => parenthesized(optional($.expr_list));

const subscript = (subscripts) => seq("[", subscripts, "]");

const redirectedInput = ($, get, source = $.expr) =>
  prec.right(
    EXPRESSION.field.precedence,
    seq(field("get", get), "<", field("source", source)),
  );

const pipedInput = ($, source, get) =>
  prec.right(
    EXPRESSION.field.precedence,
    seq(
      field("source", source),
      alias($._getline_pipe, "|"),
      field("get", get),
    ),
  );

const rawNewlines = ($) => repeat1($.newline);

const simpleGet = ($, sourceContext = false) => {
  const keyword = (rule) => alias(rule, $.getline_keyword);
  return choice(
    $.getline_keyword,
    keyword($.omitted_getline_keyword),
    keyword($.preferred_omitted_getline_keyword),
    seq(
      choice(
        keyword($.target_getline_keyword),
        keyword($.preferred_target_getline_keyword),
      ),
      field("target", $._lvalue),
    ),
    prec.dynamic(-1, keyword($.preferred_target_getline_keyword)),
    prec.dynamic(
      -1,
      seq(
        choice(
          keyword($.preferred_omitted_getline_keyword),
          ...(sourceContext ? [keyword($.omitted_getline_keyword)] : []),
        ),
        field("target", $._lvalue),
      ),
    ),
  );
};

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
    unaryInput: ($) => $.unary_input_function,
  },
  source: {
    prefix: "source",
    expression: "source_expr",
    publicExpression: "expr",
    unaryExpression: "unary_expr",
    nonUnaryExpression: "non_unary_expr",
    comparison: true,
    input: ($) =>
      choice(
        alias($.source_direct_input_function, $.non_unary_input_function),
        alias($.source_piped_input_function, $.non_unary_input_function),
      ),
    unaryInput: ($) =>
      alias($.source_unary_input_function, $.unary_input_function),
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

// Hidden operand rules leak inner fields into the parent.
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

const tierHelpers = (context, rules) => ({
  unary: (tier) => classTierName(context, "unary", tier),
  nonUnary: (tier) => classTierName(context, "non_unary", tier),
  addOperand: (classification, tier) =>
    Object.assign(rules, classOperandRules(context, classification, tier)),
});

const aliasedAnyTier = ($, context, tier) =>
  alias(
    $[anyTierName(context, tier)],
    $[context.publicExpression ?? context.expression],
  );

const nonUnaryAtom = ($, context, lvalue = $._lvalue) => {
  const atoms = [
    $._parenthesized_expression,
    $.number,
    $.string,
    lvalue,
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
    prec(OPERAND_PRECEDENCE, classTier($, context, classification, tier)),
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
  const rules = {};
  Object.assign(rules, anyTierRules(context, "unary"));
  const { unary, nonUnary, addOperand } = tierHelpers(context, rules);
  const not = `_${context.prefix}_not_expr`;
  rules[unary("unary")] = ($) =>
    choice(
      ...(context.unaryInput
        ? [classTier($, context, "unary", "exponentiation")]
        : []),
      ...["+", "-"].map((operator) =>
        seq(
          field("operator", signOperator($, operator)),
          field("operand", aliasedAnyTier($, context, "unary")),
        ),
      ),
    );
  rules[not] = ($) =>
    seq(
      field("operator", "!"),
      field("operand", aliasedAnyTier($, context, "unary")),
    );
  rules[nonUnary("unary")] = ($) =>
    choice($[nonUnary("exponentiation")], $[not]);

  const exponentiationTail = `_${context.prefix}_exponentiation_tail`;
  rules[exponentiationTail] = ($) =>
    seq(
      $._exponentiation_operator,
      field(
        "right",
        aliasedAnyTier($, context, EXPRESSION.exponentiation.rightOperand),
      ),
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
    rules[unary("update")] = context.unaryInput;
  }

  rules[nonUnary("update")] = ($) =>
    choice(
      $[nonUnary("atom")],
      $._prefix_update_expr,
      seq(
        field("operand", alias($.postfix_lvalue, $.lvalue)),
        field(
          "operator",
          choice($.incr, $.decr, ...trailingUpdateOperators($)),
        ),
      ),
      seq(
        field("operand", alias($.redirected_field, $.lvalue)),
        field("operator", choice(...trailingUpdateOperators($))),
      ),
    );

  rules[nonUnary("atom")] = ($) =>
    nonUnaryAtom($, context, prec(-1, $._lvalue));

  return rules;
};

const tieredExpressionRules = (context) => {
  const rules = {};
  const { unary, nonUnary, addOperand } = tierHelpers(context, rules);
  const conditionalTail = `_${context.prefix}_conditional_tail`;
  const expression = ($) =>
    context.publicExpression
      ? alias($[context.expression], $[context.publicExpression])
      : $[context.expression];
  rules[conditionalTail] = ($) =>
    seq(
      "?",
      field("consequence", expression($)),
      ":",
      field("alternative", aliasedAnyTier($, context, "conditional")),
    );

  const addAnyTier = (tier) =>
    Object.assign(rules, anyTierRules(context, tier));

  const addBinaryTier = (
    tier,
    operator,
    { newlineAfterOperator = false } = {},
  ) => {
    const binding = EXPRESSION[tier];
    const nextTier = nextExpressionTier(tier, context.comparison);
    const leftAssociative = binding.associativity === "left";
    addAnyTier(nextTier);
    const tail = `_${context.prefix}_${tier}_tail`;
    rules[tail] = ($) => {
      const right = field("right", aliasedAnyTier($, context, nextTier));
      return seq(
        operator($),
        newlineAfterOperator ? afterOptionalNewline($, right) : right,
      );
    };
    for (const classification of CLASSIFICATIONS) {
      if (leftAssociative) {
        addOperand(classification, tier);
      }
      const operation = ($) =>
        seq(
          field(
            "left",
            aliasedClassTier(
              $,
              context,
              classification,
              leftAssociative ? tier : nextTier,
            ),
          ),
          $[tail],
        );
      rules[classTierName(context, classification, tier)] = ($) =>
        choice(
          classTier($, context, classification, nextTier),
          bind(binding, operation($)),
        );
    }
  };

  rules[unary("assignment")] = ($) =>
    classTier($, context, "unary", "conditional");
  rules[nonUnary("assignment")] = ($) =>
    choice(
      classTier($, context, "non_unary", "conditional"),
      bind(
        EXPRESSION.assignment,
        seq(
          field("left", $._lvalue),
          field(
            "operator",
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

  addBinaryTier("logical_or", ($) => $._logical_or_operator, {
    newlineAfterOperator: true,
  });

  addBinaryTier("logical_and", ($) => $._logical_and_operator, {
    newlineAfterOperator: true,
  });

  const membershipTail = `_${context.prefix}_membership_tail`;
  rules[membershipTail] = ($) =>
    seq($._membership_operator, field("right", $.name));
  for (const classification of CLASSIFICATIONS) {
    rules[classTierName(context, classification, "membership")] = ($) => {
      const members = [
        classTier($, context, classification, "match"),
        bind(
          EXPRESSION.membership,
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

  addBinaryTier("match", ($) => $._match_operator);

  if (context.comparison) {
    addBinaryTier("comparison", ($) => $._comparison_operator);
  }

  const concatenationTail = `_${context.prefix}_concatenation_tail`;
  // Reusing an arithmetic left operand here can bypass postfix lookahead.
  const concatenationOperand = `${context.prefix}_concatenation_operand`;
  rules[concatenationOperand] = ($) =>
    prec(1, classTier($, context, "non_unary", "additive"));
  rules[concatenationTail] = ($) =>
    field(
      "right",
      alias($[concatenationOperand], $[context.nonUnaryExpression]),
    );
  for (const classification of CLASSIFICATIONS) {
    rules[classTierName(context, classification, "concatenation")] = ($) =>
      choice(
        classTier($, context, classification, "additive"),
        bind(
          EXPRESSION.concatenation,
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

  addBinaryTier("additive", ($) => $._additive_operator);

  addBinaryTier("multiplicative", ($) => $._multiplicative_operator);

  return { ...rules, ...unaryExpressionRules(context) };
};

const listElementTail = ($, element) =>
  seq(",", afterOptionalNewline($, element));

export default globalThis.grammar?.({
  name: "posix_awk",

  externals: ($) => [
    $._getline_word,
    $._getline_target_word,
    $._getline_omitted_word,
    $._getline_field_word,
    $._getline_prefer_target_word,
    $._getline_prefer_omitted_word,
    $._func_name_word,
    $._division_slash,
    $._ere_opening_slash,
    $._incr_operator,
    $._decr_operator,
    $._plus_operator,
    $._minus_operator,
    $._getline_pipe,
    $._ere_compound_opening,
    $._ere_escaped_compound_opening,
    $._ere_compound_single,
    $._ere_compound_multi,
    $._ere_dot_closing,
    $._ere_equal_closing,
    $._ere_colon_closing,
    $._ere_bracket_literal_open,
    $._ere_compound_content,
    $._ere_closing_hyphen,
    $._ere_closing,
    $._string_opening,
    $._string_closing,
    $._string_escape,
    $._ere_named_escape,
    $._ere_quoted_escape,
    $._ere_octal_escape,
    $._ere_undefined_escape,
    $._ere_escaped_delimiter,
    $.comment,
    continuationMarker(),
    $._continuation_newline,
    $._literal_break,
    $._stray_backslash,
    $._error_sentinel,
  ],

  word: ($) => $.name,

  reserved: {
    global: ($) => [
      ...[...RESERVED_WORDS.keyword, ...RESERVED_WORDS.membership].map(
        (word) => $[`${word.toLowerCase()}_keyword`],
      ),
      $.builtin_func_name,
    ],
  },

  extras: ($) => [
    token(repeat1(choice(" ", "\t"))),
    $.comment,
    continuationMarker(),
    $._continuation_newline,
    $._literal_break,
  ],

  inline: ($) => [
    // Shared operator reductions can freeze reused operands after edits.
    $._additive_operator,
    $._multiplicative_operator,
    $._exponentiation_operator,
    $._comparison_operator,
    $._match_operator,
    $._membership_operator,
    $._logical_and_operator,
    $._logical_or_operator,
    $._lvalue,
    $._field_simple_get,
    $._item,
    // A recovered tail can retain errors when reused after its prefix changes.
    $._normal_conditional_tail,
    // Unit-reduction merging can leak update fields through atom children.
    $._normal_non_unary_update_expr,
    $._print_non_unary_update_expr,
    $._field_non_unary_update_expr,
    $._normal_unary_assignment_expr,
    $._normal_unary_update_expr,
    $._print_unary_assignment_expr,
    $._source_non_unary_update_expr,
    $._source_unary_assignment_expr,
    $._source_unary_update_expr,
    $._source_conditional_tail,
  ],

  conflicts: ($) => [
    [$._for_in_clause, $.postfix_lvalue],
    [$.item_list, $._item_list],
    [$.simple_get],
    [$.source_simple_get],
  ],

  rules: {
    program: ($) => seq(optional($.item_list), optional($._item)),

    _additive_operator: ($) =>
      field("operator", choice(signOperator($, "+"), signOperator($, "-"))),

    _multiplicative_operator: ($) =>
      field("operator", choice("*", alias($._division_slash, "/"), "%")),

    _exponentiation_operator: () => field("operator", "^"),

    _comparison_operator: ($) =>
      field("operator", choice("<", $.le, $.ne, $.eq, ">", $.ge)),

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

    _function_item: ($) =>
      seq($._function_header, afterOptionalNewline($, field("body", $.action))),

    _function_header_prefix: ($) =>
      seq($.function_keyword, field("name", choice($.name, $.func_name)), "("),

    _function_header: ($) =>
      seq(
        $._function_header_prefix,
        optional(field("parameters", $.param_list)),
        ")",
      ),

    param_list: ($) => seq($.name, repeat(listElementTail($, $.name))),

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

    terminated_statement_list: ($) => repeat1($.terminated_statement),

    unterminated_statement_list: ($) =>
      seq(repeat($.terminated_statement), $.unterminated_statement),

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
      seq(field("variable", $.name), $.in_keyword, field("array", $.name)),

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
        seq(
          field("statement", $.terminatable_statement),
          field("terminator", choice($.newline, ";")),
          newlineLayout($),
        ),
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
          $.while_keyword,
          $._parenthesized_condition,
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

    output_redirection: ($) => seq(choice(">", $.append, "|"), $.expr),

    print_expr_list: ($) =>
      seq($.print_expr, repeat(listElementTail($, $.print_expr))),

    expr_list: ($) => choice($.expr, $.multiple_expr_list),

    multiple_expr_list: ($) => seq($.expr, repeat1(listElementTail($, $.expr))),

    print_expr: ($) => choice($.unary_print_expr, $.non_unary_print_expr),

    unary_print_expr: ($) => $._print_unary_assignment_expr,

    non_unary_print_expr: ($) => $._print_non_unary_assignment_expr,

    expr: ($) => choice($.unary_expr, $.non_unary_expr),

    unary_expr: ($) => $._normal_unary_assignment_expr,

    non_unary_expr: ($) => $._normal_non_unary_assignment_expr,

    _prefix_update_expr: ($) =>
      seq(
        field("operator", choice($.incr, $.decr)),
        field("operand", choice($._lvalue, alias($.prefix_field, $.lvalue))),
      ),

    ...tieredExpressionRules(EXPRESSION_CONTEXT.normal),

    source_expr: ($) =>
      choice(
        alias($.source_full_unary_expr, $.unary_expr),
        alias($.source_full_non_unary_expr, $.non_unary_expr),
      ),

    source_full_unary_expr: ($) => $._source_unary_assignment_expr,

    source_full_non_unary_expr: ($) => $._source_non_unary_assignment_expr,

    ...tieredExpressionRules(EXPRESSION_CONTEXT.source),

    ...tieredExpressionRules(EXPRESSION_CONTEXT.print),

    ...unaryExpressionRules(EXPRESSION_CONTEXT.field),

    nonpostfix_field_expr: ($) =>
      choice(
        alias($.normal_unary_field_expr, $.unary_expr),
        alias($.nonpostfix_non_unary_field_expr, $.non_unary_expr),
      ),

    normal_unary_field_expr: ($) =>
      prec(EXPRESSION.field.precedence, $._field_unary_unary_expr),

    nonpostfix_non_unary_field_expr: ($) =>
      prec(
        EXPRESSION.field.precedence,
        choice(
          $._field_not_expr,
          $._prefix_update_expr,
          alias($.nonpostfix_lvalue, $.lvalue),
          alias($.nonpostfix_field_input_function, $.non_unary_input_function),
        ),
      ),

    _parenthesized_expression: ($) => parenthesized($.expr),

    _user_function_call: ($) => seq($.func_name, callArguments($)),

    _builtin_function_call: ($) =>
      prec(
        EXPRESSION.field.precedence + 1,
        seq($.builtin_func_name, callArguments($)),
      ),

    _lvalue: ($) =>
      choice(
        alias($.postfix_lvalue, $.lvalue),
        alias($.nonpostfix_lvalue, $.lvalue),
      ),

    nonpostfix_lvalue: ($) =>
      seq(
        field("operator", "$"),
        field("operand", alias($.nonpostfix_field_expr, $.expr)),
      ),

    postfix_lvalue: ($) =>
      choice(
        seq($.name, optional(subscript($.expr_list))),
        seq(
          field("operator", "$"),
          field("operand", alias($.postfix_field_expr, $.expr)),
        ),
      ),

    postfix_field_expr: ($) =>
      alias($.postfix_non_unary_field_expr, $.non_unary_expr),

    postfix_non_unary_field_expr: ($) =>
      prec(
        EXPRESSION.field.precedence,
        nonUnaryAtom(
          $,
          {
            ...EXPRESSION_CONTEXT.field,
            input: ($) =>
              alias($.postfix_field_input_function, $.non_unary_input_function),
          },
          alias($.postfix_lvalue, $.lvalue),
        ),
      ),

    prefix_field: ($) =>
      seq(
        field("operator", "$"),
        field("operand", alias($.prefix_field_expr, $.expr)),
      ),

    prefix_field_expr: ($) =>
      alias($.prefix_non_unary_field_expr, $.non_unary_expr),

    prefix_non_unary_field_expr: ($) =>
      prec(
        EXPRESSION.field.precedence,
        choice(
          alias($.direct_input_function, $.non_unary_input_function),
          alias($.prefix_field, $.lvalue),
        ),
      ),

    redirected_field: ($) =>
      seq(
        field("operator", "$"),
        field("operand", alias($.redirected_field_expr, $.expr)),
      ),

    redirected_field_expr: ($) =>
      alias($.redirected_non_unary_field_expr, $.non_unary_expr),

    redirected_non_unary_field_expr: ($) =>
      prec(
        EXPRESSION.field.precedence,
        choice(
          alias($.redirected_field_input_function, $.non_unary_input_function),
          alias($.redirected_field, $.lvalue),
        ),
      ),

    redirected_field_input_function: ($) =>
      choice(
        prec(1, $._redirected_field_input),
        field("get", alias($.redirected_field_simple_get, $.simple_get)),
      ),

    redirected_field_simple_get: ($) =>
      seq(
        alias($.field_getline_keyword, $.getline_keyword),
        field("target", alias($.redirected_field, $.lvalue)),
      ),

    _redirected_field_input: ($) =>
      redirectedInput($, $._field_simple_get, alias($.source_expr, $.expr)),

    nonpostfix_field_input_function: ($) =>
      choice(
        field("get", alias($.nonpostfix_field_simple_get, $.simple_get)),
        $._redirected_field_input,
      ),

    postfix_field_input_function: ($) =>
      field("get", alias($.postfix_field_simple_get, $.simple_get)),

    _field_simple_get: ($) =>
      choice(
        alias($.postfix_field_simple_get, $.simple_get),
        alias($.nonpostfix_field_simple_get, $.simple_get),
      ),

    nonpostfix_field_simple_get: ($) =>
      seq(
        alias($.field_getline_keyword, $.getline_keyword),
        field("target", alias($.nonpostfix_lvalue, $.lvalue)),
      ),

    postfix_field_simple_get: ($) =>
      choice(
        prec(-1, alias($.field_getline_keyword, $.getline_keyword)),
        seq(
          alias($.field_getline_keyword, $.getline_keyword),
          field("target", alias($.postfix_lvalue, $.lvalue)),
        ),
      ),

    direct_input_function: ($) =>
      choice(field("get", $.simple_get), redirectedInput($, $.simple_get)),

    source_direct_input_function: ($) =>
      choice(
        field("get", alias($.source_simple_get, $.simple_get)),
        redirectedInput(
          $,
          alias($.source_simple_get, $.simple_get),
          alias($.source_expr, $.expr),
        ),
      ),

    source_piped_input_function: ($) =>
      pipedInput(
        $,
        alias($.source_full_non_unary_expr, $.non_unary_expr),
        alias($.source_simple_get, $.simple_get),
      ),

    source_unary_input_function: ($) =>
      pipedInput(
        $,
        alias($.source_full_unary_expr, $.unary_expr),
        alias($.source_simple_get, $.simple_get),
      ),

    piped_input_function: ($) => pipedInput($, $.non_unary_expr, $.simple_get),

    unary_input_function: ($) => pipedInput($, $.unary_expr, $.simple_get),

    simple_get: ($) => simpleGet($),

    source_simple_get: ($) => simpleGet($, true),

    trailing_incr: () => "++",

    trailing_decr: () => "--",

    getline_keyword: ($) => $._getline_word,

    target_getline_keyword: ($) => $._getline_target_word,

    omitted_getline_keyword: ($) => $._getline_omitted_word,

    field_getline_keyword: ($) => $._getline_field_word,

    preferred_target_getline_keyword: ($) => $._getline_prefer_target_word,

    preferred_omitted_getline_keyword: ($) => $._getline_prefer_omitted_word,

    ...Object.fromEntries(
      [...RESERVED_WORDS.keyword, ...RESERVED_WORDS.membership].map(
        (keyword) => [`${keyword.toLowerCase()}_keyword`, () => keyword],
      ),
    ),

    func_name: ($) => $._func_name_word,

    builtin_func_name: () => token(choice(...RESERVED_WORDS.builtin)),

    name: () => /[A-Za-z_][A-Za-z0-9_]*/,

    number: () =>
      /(([0-9]+[.][0-9]*|[.][0-9]+)([eE][+-]?[0-9]+)?|[0-9]+[eE][+-]?[0-9]+)[fFlL]?|[0-9]+/,

    string: ($) =>
      seq(
        field("opening", alias($._string_opening, '"')),
        repeat(choice($.string_content, $.escape_sequence)),
        field("closing", alias($._string_closing, '"')),
      ),

    string_content: () =>
      token.immediate(repeat1(choice(/[^"\\\n]/, prec(1, "\0")))),

    escape_sequence: ($) => $._string_escape,

    ...twoCharacterTokenRules,

    incr: ($) => $._incr_operator,

    decr: ($) => $._decr_operator,

    ere: ($) =>
      seq(
        field("opening", alias($._ere_opening_slash, "/")),
        optional(field("expression", $.extended_reg_exp)),
        field("closing", alias($._ere_closing, "/")),
      ),

    ...ereExpressionRules(false),
    ...ereExpressionRules(true),

    quoted_character: ($) => $._ere_quoted_escape,

    ordinary_character: ($) =>
      field(
        "escape",
        choice(
          $.escaped_delimiter,
          alias($.ere_named_escape_sequence, $.escape_sequence),
        ),
      ),

    ere_dupl_symbol: ($) =>
      prec.right(
        seq(
          choice(
            token.immediate("*"),
            token.immediate("+"),
            token.immediate("?"),
            $._ere_interval,
          ),
          optional(
            field(
              "modifier",
              alias(token.immediate("?"), $.repetition_modifier),
            ),
          ),
        ),
      ),

    _ere_interval: ($) =>
      seq(
        token.immediate("{"),
        field("minimum", $.duplication_count),
        optional(
          seq(
            token.immediate(","),
            optional(field("maximum", $.duplication_count)),
          ),
        ),
        $._ere_close_brace,
      ),

    duplication_count: () => token.immediate(/[0-9]+/),

    bracket_expression: ($) =>
      seq(
        token.immediate("["),
        choice($.matching_list, $.nonmatching_list),
        $._ere_close_bracket,
      ),

    matching_list: ($) => $.bracket_list,

    nonmatching_list: ($) => seq(token.immediate(prec(1, "^")), $.bracket_list),

    bracket_list: ($) =>
      seq(
        choice(
          $.follow_list,
          alias($._initial_close_follow_list, $.follow_list),
          alias($._initial_hyphen_follow_list, $.follow_list),
        ),
        optional(ereClosingHyphen($)),
      ),

    ...initialFollowListRules("close", ($) => $._ere_initial_close),

    ...initialFollowListRules("hyphen", ($) => $._ere_initial_hyphen),

    _ere_initial_close: ($) =>
      alias(token.immediate("]"), $.collating_element_single),
    _ere_initial_hyphen: ($) =>
      choice(
        alias(token.immediate("-"), $.collating_element_single),
        alias($._ere_closing_hyphen, $.collating_element_single),
      ),

    follow_list: ($) => repeat1($.expression_term),

    expression_term: ($) => choice($.single_expression, $.range_expression),

    single_expression: ($) =>
      choice($.end_range, $.character_class, $.equivalence_class),

    range_expression: ($) => ereRangeExpressionWith($, $.start_range),

    start_range: ($) => seq($.end_range, $._ere_bracket_hyphen),

    end_range: ($) =>
      choice(
        alias($._ere_bracket_character, $.collating_element_single),
        alias($._ere_bracket_literal_open, $.collating_element_single),
        $.collating_element,
        $.collating_symbol,
      ),

    collating_element: ($) =>
      choice(
        $.escaped_delimiter,
        alias($.ere_bracket_escape_sequence, $.escape_sequence),
      ),

    collating_symbol: ($) =>
      seq(
        choice(
          seq(
            ereCompoundOpening($, "."),
            field(
              "element",
              choice($._ere_raw_collating_element, $.meta_character),
            ),
          ),
          seq(
            ereCompoundOpening($, ".", true),
            field(
              "element",
              alias($._ere_compound_collating_element, $.collating_element),
            ),
          ),
        ),
        $._ere_dot_close,
      ),

    equivalence_class: ($) =>
      seq(
        choice(
          seq(
            ereCompoundOpening($, "="),
            field("element", $._ere_raw_collating_element),
          ),
          seq(
            ereCompoundOpening($, "=", true),
            field(
              "element",
              alias($._ere_compound_collating_element, $.collating_element),
            ),
          ),
        ),
        $._ere_equal_close,
      ),

    character_class: ($) =>
      seq($._ere_open_colon, field("name", $.class_name), $._ere_colon_close),

    _ere_raw_collating_element: ($) =>
      choice(
        alias($._ere_compound_single, $.collating_element_single),
        alias($._ere_compound_multi, $.collating_element_multi),
      ),

    class_name: () => token.immediate(/[A-Za-z][A-Za-z0-9]*/),

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

    _ere_dot_close: ($) => ereCompoundClosing($._ere_dot_closing, "."),

    _ere_equal_close: ($) => ereCompoundClosing($._ere_equal_closing, "="),

    _ere_open_colon: ($) => ereCompoundOpening($, ":"),

    _ere_colon_close: ($) => ereCompoundClosing($._ere_colon_closing, ":"),

    escaped_delimiter: ($) => $._ere_escaped_delimiter,

    ere_named_escape_sequence: ($) => $._ere_named_escape,

    ere_octal_escape_sequence: ($) => $._ere_octal_escape,

    ere_undefined_escape_sequence: ($) => $._ere_undefined_escape,

    ere_bracket_escape_sequence: ($) =>
      choice(
        $._ere_named_escape,
        $._ere_quoted_escape,
        $._ere_octal_escape,
        $._ere_undefined_escape,
      ),

    // Tree-sitter rejects POSIX bracket spelling for these delimiters.
    _ordinary_character: () => token.immediate(/[^.\x5B\x5C*^$+?{|}()/\n]/),

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
