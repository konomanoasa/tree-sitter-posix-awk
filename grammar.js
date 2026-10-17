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

export const FILE_SOURCE_TIER = "additive";

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

// Skipping blanks can reach a backslash the external scanner never saw.
const continuationMarker = () => token.immediate("\\");

const signOperator = ($, character) =>
  alias(character === "+" ? $._plus_operator : $._minus_operator, character);

const prefixOperator = ($, unary) =>
  field(
    "operator",
    unary
      ? choice(signOperator($, "+"), signOperator($, "-"))
      : alias($._not_operator, "!"),
  );

const updateOperator = ($) => field("operator", choice($.incr, $.decr));

const fieldOperator = ($) => field("operator", alias($._field_operator, "$"));

const assignmentOperator = ($) =>
  choice(
    "=",
    $.pow_assign,
    $.mod_assign,
    $.mul_assign,
    $.div_assign,
    $.add_assign,
    $.sub_assign,
  );

const binaryOperators = ($) => ({
  additive: [signOperator($, "+"), signOperator($, "-")],
  multiplicative: ["*", alias($._division_slash, "/"), "%"],
  exponentiation: ["^"],
  match: ["~", $.no_match],
  comparison: ["<", ">", $.le, $.ge, $.eq, $.ne],
  logical_and: [$.and],
  logical_or: [$.or],
});

const oneOf = (members) =>
  members.length === 1 ? members[0] : choice(...members);

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
  seq(
    keyword,
    $._print_arguments,
    parenthesized(field("arguments", $.multiple_expr_list)),
  );

const rawNewlines = ($) => repeat1($.newline);

const listElementTail = ($, element) =>
  seq(",", afterOptionalNewline($, element));

const boundaryKinds = [
  "atom",
  "name",
  "group",
  "call",
  "subscript",
  "get",
  "target_get",
  "file",
  "field",
  "prefix",
  "postfix",
  "binary",
  "concat",
  "assignment",
  "conditional",
  "branches",
  "pipe",
  "membership",
  "tuple",
];
const boundaryDigits = [0, 1, 2, 3];
const boundaryExternals = ($) => [
  $._root,
  $._print_root,
  $._for_in,
  $._print_arguments,
  $._begin_child,
  $._end_child,
  $._resume,
  $._next_argument,
  ...boundaryDigits.map((digit) => $[`_save_${digit}`]),
  ...boundaryDigits.map((digit) => $[`_restore_${digit}`]),
  $._class_unary,
  $._class_non_unary,
  ...boundaryKinds.map((kind) => $[`_select_${kind}`]),
  $._local_root,
  $._local_print_root,
];

const boundaryRules = {};
for (const [kind, body] of Object.entries({
  expr: ($) => alias($._boundary_expr, $.expr),
  unary: ($) => $.unary_expr,
  non_unary: ($) => $.non_unary_expr,
  print_expr: ($) => alias($._boundary_print_expr, $.print_expr),
  print_unary: ($) => $.unary_print_expr,
  print_non_unary: ($) => $.non_unary_print_expr,
  name: ($) => seq($._select_name, $.name),
  tuple: ($) =>
    seq(
      $._select_group,
      "(",
      field(
        "left",
        alias($._boundary_multiple_arguments, $.multiple_expr_list),
      ),
      ")",
    ),
  lvalue: ($) => $.lvalue,
  get: ($) => $.simple_get,
  branches: ($) => $._branches,
  print_branches: ($) => $._print_branches,
})) {
  boundaryRules[`_checkpoint_${kind}`] = ($) =>
    choice(
      ...boundaryDigits.map((digit) =>
        seq(
          $[`_save_${digit}`],
          $[`_checkpoint_${kind}`],
          $[`_restore_${digit}`],
        ),
      ),
      seq($._begin_child, body($), $._end_child),
    );
  boundaryRules[`_child_${kind}`] = ($) =>
    seq($[`_checkpoint_${kind}`], $._resume);
}

for (const print of [false, true]) {
  const prefix = print ? "print_" : "";
  const expr = ($) => $[`_child_${prefix}expr`];
  for (const unary of [false, true]) {
    const classification = unary ? "unary" : "non_unary";
    const klass = ($) => $[`_child_${prefix}${classification}`];
    boundaryRules[`${classification}_${prefix}expr`] = ($) =>
      seq(
        unary ? $._class_unary : $._class_non_unary,
        choice(
          seq(
            $._select_prefix,
            prefixOperator($, unary),
            field("operand", expr($)),
          ),
          seq(
            $._select_binary,
            field("left", klass($)),
            field(
              "operator",
              choice(
                ...Object.entries(binaryOperators($)).flatMap(
                  ([tier, operators]) =>
                    // A unary left operand of ^ is a pipe, which print excludes.
                    (print && tier === "comparison") ||
                    (print && unary && tier === "exponentiation")
                      ? []
                      : operators,
                ),
              ),
            ),
            newlineLayout($),
            field("right", expr($)),
          ),
          seq(
            $._select_concat,
            field("left", klass($)),
            field("right", $[`_child_${prefix}non_unary`]),
          ),
          seq(
            $._select_conditional,
            field("condition", klass($)),
            "?",
            $[`_child_${prefix}branches`],
          ),
          seq(
            $._select_membership,
            field("left", klass($)),
            $._membership_operator,
            field("right", $._child_name),
          ),
          ...(!print
            ? [unary ? $.unary_input_function : $.non_unary_input_function]
            : []),
          ...(unary
            ? []
            : [
                seq(
                  $._select_tuple,
                  $._child_tuple,
                  $._membership_operator,
                  field("right", $._child_name),
                ),
                seq(
                  $._select_atom,
                  choice($.number, $.string, $.ere, $.builtin_func_name),
                ),
                $.lvalue,
                seq($._select_group, "(", $._child_expr, ")"),
                seq(
                  $._select_call,
                  choice($.func_name, $.builtin_func_name),
                  "(",
                  optional(alias($._boundary_arguments, $.expr_list)),
                  ")",
                ),
                seq(
                  $._select_postfix,
                  field("operand", $._child_lvalue),
                  updateOperator($),
                ),
                seq(
                  $._select_prefix,
                  updateOperator($),
                  field("operand", $._child_lvalue),
                ),
                seq(
                  $._select_assignment,
                  field("left", $._child_lvalue),
                  field("operator", assignmentOperator($)),
                  field("right", expr($)),
                ),
              ]),
        ),
      );
  }
  boundaryRules[`_${prefix}branches`] = ($) =>
    seq(
      $._select_branches,
      field("consequence", expr($)),
      ":",
      field("alternative", expr($)),
    );
}
Object.assign(boundaryRules, {
  _boundary_expr: ($) => choice($.unary_expr, $.non_unary_expr),
  _boundary_print_expr: ($) =>
    choice($.unary_print_expr, $.non_unary_print_expr),
  lvalue: ($) =>
    choice(
      seq($._select_name, $.name),
      seq(
        $._select_subscript,
        $.name,
        "[",
        alias($._boundary_arguments, $.expr_list),
        "]",
      ),
      seq($._select_field, fieldOperator($), field("operand", $._child_expr)),
    ),
  _boundary_arguments: ($) =>
    choice(
      $._child_expr,
      alias($._boundary_multiple_arguments, $.multiple_expr_list),
    ),
  _boundary_multiple_arguments: ($) =>
    seq(
      $._child_expr,
      repeat1(seq(",", newlineLayout($), $._next_argument, $._child_expr)),
    ),
  simple_get: ($) =>
    choice(
      seq($._select_get, $.getline_keyword),
      seq(
        $._select_target_get,
        $.getline_keyword,
        field("target", $._child_lvalue),
      ),
    ),
  non_unary_input_function: ($) =>
    choice(
      field("get", $.simple_get),
      seq(
        $._select_file,
        field("get", $._child_get),
        "<",
        field("source", $._child_expr),
      ),
      seq(
        $._select_pipe,
        field("source", $._child_non_unary),
        alias($._getline_pipe, "|"),
        field("get", $._child_get),
      ),
    ),
  unary_input_function: ($) =>
    seq(
      $._select_pipe,
      field("source", $._child_unary),
      alias($._getline_pipe, "|"),
      field("get", $._child_get),
    ),
  getline_keyword: ($) => $._getline_word,
});

const localRules = {};
const localNode = ($, name) => alias($[`local_${name}`], $[name]);
const localSubscriptable = ($) =>
  seq($.name, optional(seq("[", localNode($, "expr_list"), "]")));
const localPrefixUpdate = ($) =>
  seq(updateOperator($), field("operand", localNode($, "lvalue")));
const localAtoms = ($, print, lvalue = localNode($, "lvalue")) =>
  choice(
    $._local_atom,
    lvalue,
    ...(!print ? [localNode($, "non_unary_input_function")] : []),
  );
for (const print of [false, true]) {
  const prefix = print ? "print_" : "";
  const expression = ($) => localNode($, `${prefix}expr`);
  const tiers = Object.keys(EXPRESSION).filter(
    (name) => name !== "field" && (!print || name !== "comparison"),
  );
  const classifications = (name) =>
    name === "atom" || EXPRESSION[name].precedence > EXPRESSION.unary.precedence
      ? ["non_unary"]
      : ["unary", "non_unary"];
  const tier = (classification, name) =>
    `_local_${prefix}${classification}_${name}`;
  const operand = (classification, name) =>
    `local_${prefix}${classification}_${name}`;
  const union = (name) => `local_${prefix}${name}`;
  const classified = (classification, name) => ($) =>
    alias(
      $[operand(classification, name)],
      $[`${classification}_${prefix}expr`],
    );
  const referenced = new Set();
  const member = (classification, name) => {
    referenced.add(operand(classification, name));
    return classified(classification, name);
  };
  const any = (name) => {
    referenced.add(union(name));
    for (const classification of classifications(name))
      member(classification, name);
    return ($) => alias($[union(name)], $[`${prefix}expr`]);
  };
  const tierOperations = (classification, name, next) => {
    if (name === "assignment") {
      return ($) =>
        classification === "unary"
          ? []
          : [
              seq(
                field("left", localNode($, "lvalue")),
                field("operator", assignmentOperator($)),
                field("right", expression($)),
              ),
            ];
    }
    if (name === "conditional") {
      const condition = member(classification, next);
      const alternative = any(name);
      return ($) => [
        seq(
          field("condition", condition($)),
          "?",
          field("consequence", expression($)),
          ":",
          field("alternative", alternative($)),
        ),
      ];
    }
    if (name === "membership") {
      const left = member(classification, name);
      return ($) => [
        seq(
          field("left", left($)),
          $._membership_operator,
          field("right", $.name),
        ),
        ...(classification === "non_unary"
          ? [
              seq(
                "(",
                field("left", localNode($, "multiple_expr_list")),
                ")",
                $._membership_operator,
                field("right", $.name),
              ),
            ]
          : []),
      ];
    }
    if (name === "concatenation") {
      const left = member(classification, name);
      const right = member("non_unary", next);
      return ($) => [seq(field("left", left($)), field("right", right($)))];
    }
    if (name === "unary") {
      const operand = any(name);
      return ($) => [
        seq(
          prefixOperator($, classification === "unary"),
          field("operand", operand($)),
        ),
      ];
    }
    if (name === "postfix") {
      return ($) => [
        seq(
          field("operand", alias($.local_postfix_lvalue, $.lvalue)),
          updateOperator($),
        ),
        localPrefixUpdate($),
      ];
    }
    const left = member(
      classification,
      EXPRESSION[name].associativity === "left" ? name : next,
    );
    const right = any(EXPRESSION[name].rightOperand ?? next);
    return ($) => [
      seq(
        field("left", left($)),
        field("operator", oneOf(binaryOperators($)[name])),
        ...(name === "logical_or" || name === "logical_and"
          ? [newlineLayout($)]
          : []),
        field("right", right($)),
      ),
    ];
  };
  const tierRules = {};
  for (const classification of ["unary", "non_unary"]) {
    for (const [index, name] of tiers.entries()) {
      if (!classifications(name).includes(classification)) continue;
      const next = tiers[index + 1] ?? "atom";
      const operations = tierOperations(classification, name, next);
      const fallback = name !== "unary" || classification !== "unary";
      tierRules[tier(classification, name)] = ($) =>
        oneOf([
          ...(fallback ? [$[tier(classification, next)]] : []),
          ...operations($),
        ]);
    }
  }
  const assignments = ["unary", "non_unary"].map((classification) =>
    member(classification, "assignment"),
  );
  for (const name of [...tiers, "atom"]) {
    for (const classification of classifications(name)) {
      if (referenced.has(operand(classification, name)))
        localRules[operand(classification, name)] = ($) =>
          prec(
            EXPRESSION[name]?.precedence ?? EXPRESSION.field.precedence,
            $[tier(classification, name)],
          );
    }
    if (referenced.has(union(name)))
      localRules[union(name)] = ($) =>
        choice(
          ...classifications(name).map((classification) =>
            classified(classification, name)($),
          ),
        );
  }
  Object.assign(localRules, tierRules);
  localRules[tier("non_unary", "atom")] = ($) => localAtoms($, print);
  localRules[`local_${prefix}expr`] = ($) =>
    choice(...assignments.map((assignment) => assignment($)));
}
Object.assign(localRules, {
  _local_atom: ($) =>
    choice(
      $.number,
      $.string,
      $.ere,
      $.builtin_func_name,
      seq("(", localNode($, "expr"), ")"),
      prec(
        1,
        seq(
          choice($.func_name, $.builtin_func_name),
          "(",
          optional(localNode($, "expr_list")),
          ")",
        ),
      ),
    ),
  local_lvalue: ($) =>
    choice(
      localSubscriptable($),
      seq(
        fieldOperator($),
        field("operand", alias($.local_field_expr, $.expr)),
      ),
    ),
  local_postfix_lvalue: ($) =>
    prec(
      1,
      choice(
        localSubscriptable($),
        seq(
          fieldOperator($),
          field("operand", alias($.local_postfix_field_expr, $.expr)),
        ),
      ),
    ),
  local_postfix_field_expr: ($) =>
    alias($.local_postfix_field_atom, $.non_unary_expr),
  local_postfix_field_atom: ($) =>
    prec(1, localAtoms($, false, alias($.local_postfix_lvalue, $.lvalue))),
  local_field_expr: ($) =>
    choice(
      alias($.local_unary_unary, $.unary_expr),
      alias($.local_field_non_unary, $.non_unary_expr),
    ),
  local_field_non_unary: ($) =>
    choice(
      $._local_non_unary_atom,
      seq(
        prefixOperator($, false),
        field("operand", alias($.local_unary, $.expr)),
      ),
      localPrefixUpdate($),
    ),
  local_expr_list: ($) =>
    choice(localNode($, "expr"), localNode($, "multiple_expr_list")),
  local_multiple_expr_list: ($) =>
    seq(
      localNode($, "expr"),
      repeat1(listElementTail($, localNode($, "expr"))),
    ),
  local_non_unary_input_function: ($) =>
    field("get", localNode($, "simple_get")),
  local_simple_get: ($) =>
    prec.right(
      EXPRESSION.field.precedence,
      seq($.getline_keyword, optional(field("target", localNode($, "lvalue")))),
    ),
});

const definition = {
  name: "posix_awk",

  externals: ($) => [
    ...boundaryExternals($),
    $._getline_word,
    $._func_name_word,
    $._division_slash,
    $._ere_opening_slash,
    $._incr_operator,
    $._decr_operator,
    $._plus_operator,
    $._minus_operator,
    $._field_operator,
    $._not_operator,
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
    $._item,
    $._membership_operator,
    ...Object.keys(localRules)
      .filter((name) => name.startsWith("_") && name !== "_local_atom")
      .map((name) => $[name]),
  ],
  conflicts: ($) => [[$.item_list, $._item_list]],

  rules: {
    program: ($) => seq(optional($.item_list), optional($._item)),

    _membership_operator: ($) => field("operator", $.in_keyword),

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
      seq(
        $._for_in,
        field("variable", $.name),
        $.in_keyword,
        field("array", $.name),
      ),

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
          optional(seq("[", field("subscripts", $.expr_list), "]")),
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
    ...boundaryRules,
    ...localRules,
    expr: ($) =>
      choice(
        seq($._root, $._boundary_expr),
        seq($._local_root, alias($.local_unary_assignment, $.unary_expr)),
        seq(
          $._local_root,
          alias($.local_non_unary_assignment, $.non_unary_expr),
        ),
      ),
    print_expr: ($) =>
      choice(
        seq($._print_root, $._boundary_print_expr),
        seq(
          $._local_print_root,
          alias($.local_print_unary_assignment, $.unary_print_expr),
        ),
        seq(
          $._local_print_root,
          alias($.local_print_non_unary_assignment, $.non_unary_print_expr),
        ),
      ),
  },
};

export default globalThis.grammar?.(definition);
