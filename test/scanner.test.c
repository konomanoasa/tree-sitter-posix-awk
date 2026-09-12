#include <stdio.h>

#include "../src/scanner.c"

typedef struct {
  TSLexer lexer;
  const char *source;
  size_t offset;
  size_t token_start;
  size_t token_end;
  size_t advance_count;
  bool content_started;
} MockLexer;

static MockLexer *mock_lexer(TSLexer *lexer) {
  return (MockLexer *)lexer;
}

static const MockLexer *const_mock_lexer(const TSLexer *lexer) {
  return (const MockLexer *)lexer;
}

static void mock_advance(TSLexer *lexer, bool skip) {
  MockLexer *mock = mock_lexer(lexer);
  mock->advance_count++;
  if (mock->source[mock->offset] != '\0') {
    mock->offset++;
  }
  if (skip && !mock->content_started) {
    mock->token_start = mock->offset;
  } else {
    mock->content_started = true;
  }
  lexer->lookahead = (unsigned char)mock->source[mock->offset];
}

static void mock_mark_end(TSLexer *lexer) {
  MockLexer *mock = mock_lexer(lexer);
  mock->token_end = mock->offset;
}

static bool mock_eof(const TSLexer *lexer) {
  const MockLexer *mock = const_mock_lexer(lexer);
  return mock->source[mock->offset] == '\0';
}

static MockLexer make_mock_lexer(const char *source) {
  return (MockLexer){
    .lexer =
      {
        .lookahead = (unsigned char)source[0],
        .advance = mock_advance,
        .mark_end = mock_mark_end,
        .eof = mock_eof,
      },
    .source = source,
  };
}

static int expect_scan_result_at(
  const char *test_name,
  const char *source,
  const bool *valid_symbols,
  LexicalMode initial_mode,
  bool expected_scanned,
  enum TokenType expected_symbol,
  size_t expected_token_start,
  size_t expected_token_end,
  LexicalMode expected_mode
) {
  MockLexer mock = make_mock_lexer(source);
  ScannerState state = {.mode = initial_mode};
  const bool scanned = tree_sitter_posix_awk_external_scanner_scan(
    &state,
    &mock.lexer,
    valid_symbols
  );
  if (
    scanned ==
    expected_scanned &&
    state.mode ==
    expected_mode &&
    (!scanned ||
      (mock.lexer.result_symbol ==
        expected_symbol &&
        mock.token_start ==
        expected_token_start &&
        mock.token_end == expected_token_end))
  ) {
    return 0;
  }

  fprintf(
    stderr,
    "%s: scanned=%u symbol=%u start=%zu end=%zu mode=%u\n",
    test_name,
    scanned,
    mock.lexer.result_symbol,
    mock.token_start,
    mock.token_end,
    (unsigned)state.mode
  );
  return 1;
}

static int expect_scan_result(
  const char *test_name,
  const char *source,
  const bool *valid_symbols,
  bool expected_scanned,
  enum TokenType expected_symbol,
  size_t expected_token_end
) {
  return expect_scan_result_at(
    test_name,
    source,
    valid_symbols,
    LEXICAL_MODE_OUTSIDE,
    expected_scanned,
    expected_symbol,
    0,
    expected_token_end,
    LEXICAL_MODE_OUTSIDE
  );
}

static void set_all_symbols_valid(bool *valid_symbols) {
  for (size_t i = 0; i < TOKEN_TYPE_COUNT; i++) {
    valid_symbols[i] = true;
  }
}

static int check_source_token_ranges(void) {
  static const struct {
    const char *name;
    const char *source;
    enum TokenType token;
    size_t expected_token_end;
  } cases[] = {
    {"keyword spelling", "END", END_WORD, 3},
    {"name spelling", "value", NAME_WORD, 5},
    {"built-in spelling", "length", BUILTIN_FUNC_NAME_WORD, 6},
    {"function name excludes parenthesis", "follow(", FUNC_NAME_WORD, 6},
    {"continued function name excludes continuation",
      "follow\\\n(",
      FUNC_NAME_WORD,
      6},
    {"built-in call allows blanks before the parenthesis",
      "length (",
      BUILTIN_CALL_WORD,
      6},
    {"built-in call allows a continuation before the parenthesis",
      "length\\\n(",
      BUILTIN_CALL_WORD,
      6},
    {"bare built-in before a name", "length x", BUILTIN_FUNC_NAME_WORD, 6},
    {"for-in variable", "k in a)", FOR_IN_VARIABLE_WORD, 1},
    {"integer spelling", "123", NUMBER_INTEGER, 3},
    {"fraction spelling", ".5", NUMBER_FRACTION, 2},
    {"exponent spelling", "1.5e+2", NUMBER_EXPONENT, 6},
    {"incomplete exponent keeps integer", "1e+", NUMBER_INTEGER, 1},
    {"addition assignment spelling", "+=", ADD_ASSIGN_OPERATOR, 2},
    {"logical-or spelling", "||", OR_OPERATOR, 2},
  };

  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
    valid_symbols[cases[i].token] = true;
    failed |= expect_scan_result(
      cases[i].name,
      cases[i].source,
      valid_symbols,
      true,
      cases[i].token,
      cases[i].expected_token_end
    );
  }

  bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
  valid_symbols[NAME_WORD] = true;
  valid_symbols[FUNC_NAME_WORD] = true;
  failed |= expect_scan_result(
    "function name wins over plain name",
    "follow(",
    valid_symbols,
    true,
    FUNC_NAME_WORD,
    6
  );

  valid_symbols[NAME_WORD] = false;
  failed |= expect_scan_result(
    "plain name is not a function name",
    "follow",
    valid_symbols,
    false,
    FUNC_NAME_WORD,
    0
  );

  valid_symbols[FUNC_NAME_WORD] = false;
  valid_symbols[NAME_WORD] = true;
  failed |= expect_scan_result(
    "reserved word is not a name",
    "END",
    valid_symbols,
    false,
    NAME_WORD,
    0
  );

  failed |= expect_scan_result(
    "for-in shape stays a name where the parser cannot take a variable",
    "k in a)",
    valid_symbols,
    true,
    NAME_WORD,
    1
  );

  valid_symbols[FOR_IN_VARIABLE_WORD] = true;
  failed |= expect_scan_result(
    "classic for keeps the name",
    "k in a;",
    valid_symbols,
    true,
    NAME_WORD,
    1
  );
  return failed;
}

static int check_getline_target_lookahead(void) {
  static const struct {
    const char *name;
    const char *source;
    enum TokenType token;
    size_t maximum_advances;
  } cases[] = {
    {"getline before a name takes a target",
      "getline value",
      GETLINE_TARGET_WORD,
      13},
    {"getline before a field takes a target",
      "getline $1",
      GETLINE_TARGET_WORD,
      8},
    {"getline before a string is bare", "getline \"a\"", GETLINE_WORD, 8},
    {"getline before an adjacent call is bare", "getline f(", GETLINE_WORD, 9},
    {"getline before a spaced name takes a target",
      "getline f (",
      GETLINE_TARGET_WORD,
      9},
    {"getline before a keyword is bare", "getline next", GETLINE_WORD, 12},
    {"getline before a built-in stops after its spelling",
      "getline length (value)",
      GETLINE_WORD,
      14},
    {"continued getline before a name takes a target",
      "getline \\\nvalue",
      GETLINE_TARGET_WORD,
      15},
    {"getline before a continued call is bare",
      "getline f\\\n(",
      GETLINE_WORD,
      11},
    {"getline before a spaced continued name takes a target",
      "getline f \\\n(",
      GETLINE_TARGET_WORD,
      9},
    {"two getlines stop lookahead after the second spelling",
      "getline getline",
      GETLINE_WORD,
      15},
    {"many getlines stop lookahead after the second spelling",
      "getline getline getline getline getline getline target",
      GETLINE_WORD,
      15},
  };

  int failed = 0;
  bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
  valid_symbols[GETLINE_WORD] = true;
  valid_symbols[GETLINE_TARGET_WORD] = true;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    MockLexer mock = make_mock_lexer(cases[i].source);
    ScannerState state = {.mode = LEXICAL_MODE_OUTSIDE};
    const bool scanned = tree_sitter_posix_awk_external_scanner_scan(
      &state,
      &mock.lexer,
      valid_symbols
    );
    if (
      scanned &&
      mock.lexer.result_symbol ==
      cases[i].token &&
      mock.token_start ==
      0 &&
      mock.token_end ==
      7 &&
      state.mode ==
      LEXICAL_MODE_OUTSIDE &&
      mock.advance_count <= cases[i].maximum_advances
    ) {
      continue;
    }
    fprintf(
      stderr,
      "%s: scanned=%u symbol=%u start=%zu end=%zu mode=%u advances=%zu "
      "maximum=%zu\n",
      cases[i].name,
      scanned,
      mock.lexer.result_symbol,
      mock.token_start,
      mock.token_end,
      (unsigned)state.mode,
      mock.advance_count,
      cases[i].maximum_advances
    );
    failed = 1;
  }
  return failed;
}

static int check_blank_skip_token_ranges(void) {
  static const struct {
    const char *name;
    const char *source;
    enum TokenType token;
    size_t expected_token_start;
    size_t expected_token_end;
  } cases[] = {
    {"blanks precede a keyword", "  END", END_WORD, 2, 5},
    {"a tab precedes a number", "\t1.5", NUMBER_FRACTION, 1, 4},
  };

  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
    valid_symbols[cases[i].token] = true;
    failed |= expect_scan_result_at(
      cases[i].name,
      cases[i].source,
      valid_symbols,
      LEXICAL_MODE_OUTSIDE,
      true,
      cases[i].token,
      cases[i].expected_token_start,
      cases[i].expected_token_end,
      LEXICAL_MODE_OUTSIDE
    );
  }
  return failed;
}

static int check_greater_dispatch(void) {
  static const struct {
    const char *name;
    const char *source;
    bool guard_valid;
    bool ge_valid;
    bool append_valid;
    bool expected_scanned;
    enum TokenType expected_symbol;
    size_t expected_token_end;
  } cases[] = {
    {"plain redirection beside GE",
      ">f",
      true,
      true,
      false,
      true,
      OUTPUT_GREATER_GUARD,
      0},
    {"append-looking redirection without append token",
      ">>f",
      true,
      true,
      false,
      true,
      OUTPUT_GREATER_GUARD,
      0},
    {"GE beside redirection guard",
      ">=1",
      true,
      true,
      false,
      true,
      GE_OPERATOR,
      2},
    {"GE is not split into a redirection guard",
      ">=1",
      true,
      false,
      false,
      false,
      GE_OPERATOR,
      0},
    {"append operator", ">>f", false, false, true, true, APPEND_OPERATOR, 2},
    {"plain greater stays internal",
      ">x",
      false,
      true,
      false,
      false,
      GE_OPERATOR,
      0},
  };

  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
    valid_symbols[OUTPUT_GREATER_GUARD] = cases[i].guard_valid;
    valid_symbols[GE_OPERATOR] = cases[i].ge_valid;
    valid_symbols[APPEND_OPERATOR] = cases[i].append_valid;
    failed |= expect_scan_result(
      cases[i].name,
      cases[i].source,
      valid_symbols,
      cases[i].expected_scanned,
      cases[i].expected_symbol,
      cases[i].expected_token_end
    );
  }
  return failed;
}

static int check_slash_dispatch(void) {
  int failed = 0;
  bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
  valid_symbols[DIVISION_SLASH] = true;
  valid_symbols[ERE_OPENING_SLASH] = true;
  failed |= expect_scan_result_at(
    "division precedes ERE in normal parsing",
    "/x",
    valid_symbols,
    LEXICAL_MODE_OUTSIDE,
    true,
    DIVISION_SLASH,
    0,
    1,
    LEXICAL_MODE_OUTSIDE
  );

  valid_symbols[DIVISION_SLASH] = false;
  failed |= expect_scan_result_at(
    "ERE opens without a division context",
    "/x",
    valid_symbols,
    LEXICAL_MODE_OUTSIDE,
    true,
    ERE_OPENING_SLASH,
    0,
    1,
    LEXICAL_MODE_ERE_BODY
  );

  valid_symbols[DIVISION_SLASH] = true;
  valid_symbols[DIV_ASSIGN_OPERATOR] = true;
  failed |= expect_scan_result(
    "division assignment is the longest match",
    "/=x",
    valid_symbols,
    true,
    DIV_ASSIGN_OPERATOR,
    2
  );

  valid_symbols[DIV_ASSIGN_OPERATOR] = false;
  failed |= expect_scan_result(
    "division slash does not split division assignment",
    "/=x",
    valid_symbols,
    false,
    DIVISION_SLASH,
    0
  );
  return failed;
}

static int check_line_continuation_markers(void) {
  static const struct {
    const char *name;
    const char *source;
    enum TokenType marker;
    bool expected_scanned;
  } cases[] = {
    {"continued generic operator", "\\\n+=", LC_BEFORE_OPERATOR, true},
    {"continued additive operator", "\\\n+", LC_BEFORE_ADDITIVE_OPERATOR, true},
    {"continued expression word", "\\\nname", LC_BEFORE_EXPRESSION, true},
    {"continued expression number", "\\\n.5", LC_BEFORE_EXPRESSION, true},
    {"continued else", "\\\nelse", LC_BEFORE_ELSE, true},
    {"continued do tail", "\\\nwhile", LC_BEFORE_DO_TAIL, true},
    {"non-while word is not a do tail", "\\\nEND", LC_BEFORE_DO_TAIL, false},
    {"continued membership operator",
      "\\\nin",
      LC_BEFORE_MEMBERSHIP_OPERATOR,
      true},
    {"continued simple statement",
      "\\\nprint",
      LC_BEFORE_SIMPLE_STATEMENT,
      true},
    {"continued statement keyword", "\\\nif", LC_BEFORE_STATEMENT, true},
    {"continued statement word", "\\\nname", LC_BEFORE_STATEMENT, true},
    {"continued statement action", "\\\n{", LC_BEFORE_STATEMENT, true},
    {"continued empty statement", "\\\n;", LC_BEFORE_STATEMENT, true},
    {"continued while statement", "\\\nwhile", LC_BEFORE_STATEMENT, true},
    {"reserved item start is not a statement",
      "\\\nEND",
      LC_BEFORE_STATEMENT,
      false},
    {"continued item keyword", "\\\nBEGIN", LC_BEFORE_ITEM, true},
    {"continued item expression", "\\\n$1", LC_BEFORE_ITEM, true},
    {"continued item action", "\\\n{", LC_BEFORE_ITEM, true},
    {"statement keyword is not an item", "\\\nif", LC_BEFORE_ITEM, false},
    {"continued EOF", "\\\n", LC_BEFORE_EOF, true},
    {"continued comment at EOF", "\\\n# note", LC_BEFORE_EOF, true},
    {"continued blank EOF", "\\\n  \\\n ", LC_BEFORE_EOF, true},
    {"newline is not EOF", "\\\n\n", LC_BEFORE_EOF, false},
    {"continued semicolon", "\\\n;", LC_BEFORE_SEMICOLON, true},
    {"continued comma", "\\\n,", LC_BEFORE_COMMA, true},
    {"continued close parenthesis", "\\\n)", LC_BEFORE_CLOSE_PARENTHESIS, true},
    {"continued action", "\\\n{", LC_BEFORE_ACTION, true},
    {"continued close brace", "\\\n}", LC_BEFORE_CLOSE_BRACE, true},
    {"continued newline", "\\\n\n", LC_BEFORE_NEWLINE, true},
    {"continued comment before a newline",
      "\\\n# note\n",
      LC_BEFORE_NEWLINE,
      true},
    {"continued comment at EOF is not a newline",
      "\\\n# note",
      LC_BEFORE_NEWLINE,
      false},
  };

  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
    valid_symbols[cases[i].marker] = true;
    failed |= expect_scan_result(
      cases[i].name,
      cases[i].source,
      valid_symbols,
      cases[i].expected_scanned,
      cases[i].marker,
      0
    );
  }

  static const struct {
    const char *name;
    const char *source;
    enum TokenType fallback;
    enum TokenType preferred;
  } priorities[] = {
    {"action before statement", "\\\n{", LC_BEFORE_STATEMENT, LC_BEFORE_ACTION},
    {"statement before item", "\\\n{", LC_BEFORE_ITEM, LC_BEFORE_STATEMENT},
    {"terminator before empty statement",
      "\\\n;",
      LC_BEFORE_STATEMENT,
      LC_BEFORE_SEMICOLON},
    {"do tail before while statement",
      "\\\nwhile",
      LC_BEFORE_STATEMENT,
      LC_BEFORE_DO_TAIL},
    {"expression before statement",
      "\\\nname",
      LC_BEFORE_STATEMENT,
      LC_BEFORE_EXPRESSION},
    {"expression before item",
      "\\\nname",
      LC_BEFORE_ITEM,
      LC_BEFORE_EXPRESSION},
    {"operator before expression",
      "\\\n-1",
      LC_BEFORE_EXPRESSION,
      LC_BEFORE_ADDITIVE_OPERATOR},
    {"simple statement before expression",
      "\\\nx",
      LC_BEFORE_EXPRESSION,
      LC_BEFORE_SIMPLE_STATEMENT},
  };
  for (size_t i = 0; i < ARRAY_LENGTH(priorities); i++) {
    bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
    valid_symbols[priorities[i].fallback] = true;
    valid_symbols[priorities[i].preferred] = true;
    failed |= expect_scan_result(
      priorities[i].name,
      priorities[i].source,
      valid_symbols,
      true,
      priorities[i].preferred,
      0
    );
  }
  return failed;
}

static int check_linear_line_continuation_lookahead(void) {
  const size_t continuation_count = 32768;
  char *source = malloc((continuation_count * 2U) + 2U);
  if (source == NULL) {
    fprintf(stderr, "linear line-continuation lookahead: allocation failed\n");
    return 1;
  }
  for (size_t i = 0; i < continuation_count; i++) {
    source[i * 2U] = '\\';
    source[(i * 2U) + 1U] = '\n';
  }
  source[continuation_count * 2U] = 'a';
  source[(continuation_count * 2U) + 1U] = '\0';

  MockLexer mock = make_mock_lexer(source);
  ScannerState state = {.mode = LEXICAL_MODE_OUTSIDE};
  bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
  valid_symbols[LC_BEFORE_EXPRESSION] = true;
  const bool scanned = tree_sitter_posix_awk_external_scanner_scan(
    &state,
    &mock.lexer,
    valid_symbols
  );
  const bool valid_result = scanned &&
    mock.lexer.result_symbol ==
    LC_BEFORE_EXPRESSION &&
    mock.token_end ==
    0 &&
    mock.advance_count <=
    continuation_count *
    4U;
  if (!valid_result) {
    fprintf(
      stderr,
      "linear line-continuation lookahead: scanned=%u symbol=%u advances=%zu "
      "end=%zu\n",
      scanned,
      mock.lexer.result_symbol,
      mock.advance_count,
      mock.token_end
    );
  }

  free(source);
  return valid_result ? 0 : 1;
}

static int check_ere_state_transitions(void) {
  int failed = 0;
  bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
  valid_symbols[ERE_OPENING_SLASH] = true;
  failed |= expect_scan_result_at(
    "ERE opening enters body mode",
    "/a",
    valid_symbols,
    LEXICAL_MODE_OUTSIDE,
    true,
    ERE_OPENING_SLASH,
    0,
    1,
    LEXICAL_MODE_ERE_BODY
  );

  valid_symbols[ERE_OPENING_SLASH] = false;
  valid_symbols[ERE_CLOSING] = true;
  failed |= expect_scan_result_at(
    "ERE closing exits body mode",
    "/",
    valid_symbols,
    LEXICAL_MODE_ERE_BODY,
    true,
    ERE_CLOSING,
    0,
    1,
    LEXICAL_MODE_OUTSIDE
  );

  valid_symbols[ERE_CLOSING] = false;
  valid_symbols[ERE_ESCAPE_START] = true;
  failed |= expect_scan_result_at(
    "ERE escape guard keeps body mode",
    "\\n",
    valid_symbols,
    LEXICAL_MODE_ERE_BODY,
    true,
    ERE_ESCAPE_START,
    0,
    0,
    LEXICAL_MODE_ERE_BODY
  );

  valid_symbols[ERE_ESCAPE_START] = false;
  valid_symbols[ERE_ESCAPED_DELIMITER_START] = true;
  failed |= expect_scan_result_at(
    "escaped delimiter guard enters delimiter mode",
    "\\/",
    valid_symbols,
    LEXICAL_MODE_ERE_BODY,
    true,
    ERE_ESCAPED_DELIMITER_START,
    0,
    0,
    LEXICAL_MODE_ERE_ESCAPED_DELIMITER
  );

  valid_symbols[ERE_ESCAPED_DELIMITER_START] = false;
  valid_symbols[ERE_ESCAPED_DELIMITER_END] = true;
  failed |= expect_scan_result_at(
    "escaped delimiter token returns to body mode",
    "/",
    valid_symbols,
    LEXICAL_MODE_ERE_ESCAPED_DELIMITER,
    true,
    ERE_ESCAPED_DELIMITER_END,
    0,
    1,
    LEXICAL_MODE_ERE_BODY
  );

  valid_symbols[ERE_ESCAPED_DELIMITER_END] = false;
  valid_symbols[ERE_COMPOUND_OPEN_GUARD] = true;
  failed |= expect_scan_result_at(
    "compound opener guard is zero width",
    "[.",
    valid_symbols,
    LEXICAL_MODE_ERE_BODY,
    true,
    ERE_COMPOUND_OPEN_GUARD,
    0,
    0,
    LEXICAL_MODE_ERE_BODY
  );

  valid_symbols[ERE_COMPOUND_OPEN_GUARD] = false;
  valid_symbols[ERE_CLOSING_HYPHEN] = true;
  failed |= expect_scan_result_at(
    "hyphen before the closing bracket is one token",
    "-]",
    valid_symbols,
    LEXICAL_MODE_ERE_BODY,
    true,
    ERE_CLOSING_HYPHEN,
    0,
    1,
    LEXICAL_MODE_ERE_BODY
  );
  failed |= expect_scan_result_at(
    "range hyphen stays internal",
    "-a",
    valid_symbols,
    LEXICAL_MODE_ERE_BODY,
    false,
    ERE_CLOSING_HYPHEN,
    0,
    0,
    LEXICAL_MODE_ERE_BODY
  );

  valid_symbols[ERE_CLOSING_HYPHEN] = false;
  valid_symbols[ERE_DOT_CLOSE_GUARD] = true;
  failed |= expect_scan_result_at(
    "compound closer guard is zero width",
    ".]",
    valid_symbols,
    LEXICAL_MODE_ERE_BODY,
    true,
    ERE_DOT_CLOSE_GUARD,
    0,
    0,
    LEXICAL_MODE_ERE_BODY
  );

  set_all_symbols_valid(valid_symbols);
  failed |= expect_scan_result_at(
    "error mode suppresses ERE compound guards",
    "[.",
    valid_symbols,
    LEXICAL_MODE_ERE_BODY,
    false,
    ERE_COMPOUND_OPEN_GUARD,
    0,
    0,
    LEXICAL_MODE_ERE_BODY
  );
  failed |= expect_scan_result_at(
    "error mode suppresses escaped-delimiter guards",
    "\\/",
    valid_symbols,
    LEXICAL_MODE_ERE_BODY,
    false,
    ERE_ESCAPED_DELIMITER_START,
    0,
    0,
    LEXICAL_MODE_ERE_BODY
  );
  return failed;
}

static int check_string_and_comment_modes(void) {
  static const struct {
    const char *name;
    const char *source;
    LexicalMode initial_mode;
    enum TokenType valid;
    bool expected_scanned;
    enum TokenType expected_symbol;
    size_t expected_token_start;
    size_t expected_token_end;
    LexicalMode expected_mode;
  } cases[] = {
    {"string opening enters string mode",
      "\"a\"",
      LEXICAL_MODE_OUTSIDE,
      STRING_OPENING,
      true,
      STRING_OPENING,
      0,
      1,
      LEXICAL_MODE_STRING},
    {"string end before the closing quote is zero width",
      "\"",
      LEXICAL_MODE_STRING,
      STRING_END,
      true,
      STRING_END,
      0,
      0,
      LEXICAL_MODE_OUTSIDE},
    {"raw newline ends the string",
      "\n",
      LEXICAL_MODE_STRING,
      STRING_END,
      true,
      STRING_END,
      0,
      0,
      LEXICAL_MODE_OUTSIDE},
    {"EOF ends the string",
      "",
      LEXICAL_MODE_STRING,
      STRING_END,
      true,
      STRING_END,
      0,
      0,
      LEXICAL_MODE_OUTSIDE},
    {"string content stays internal",
      "a\"",
      LEXICAL_MODE_STRING,
      STRING_END,
      false,
      STRING_END,
      0,
      0,
      LEXICAL_MODE_STRING},
    {"string end waits for the escape character",
      "\"",
      LEXICAL_MODE_STRING,
      COMMENT,
      false,
      STRING_END,
      0,
      0,
      LEXICAL_MODE_STRING},
    {"hash inside a string is content",
      "#\"",
      LEXICAL_MODE_STRING,
      COMMENT,
      false,
      COMMENT,
      0,
      0,
      LEXICAL_MODE_STRING},
    {"hash inside an ERE is content",
      "#/",
      LEXICAL_MODE_ERE_BODY,
      COMMENT,
      false,
      COMMENT,
      0,
      0,
      LEXICAL_MODE_ERE_BODY},
    {"comment spans to its newline",
      "# c\nx",
      LEXICAL_MODE_OUTSIDE,
      COMMENT,
      true,
      COMMENT,
      0,
      3,
      LEXICAL_MODE_OUTSIDE},
    {"comment spans to EOF",
      "# c",
      LEXICAL_MODE_OUTSIDE,
      COMMENT,
      true,
      COMMENT,
      0,
      3,
      LEXICAL_MODE_OUTSIDE},
    {"blanks precede a comment",
      "  # c\n",
      LEXICAL_MODE_OUTSIDE,
      COMMENT,
      true,
      COMMENT,
      2,
      5,
      LEXICAL_MODE_OUTSIDE},
    {"a quote is not a comment",
      "\"",
      LEXICAL_MODE_OUTSIDE,
      COMMENT,
      false,
      COMMENT,
      0,
      0,
      LEXICAL_MODE_OUTSIDE},
  };

  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
    valid_symbols[cases[i].valid] = true;
    failed |= expect_scan_result_at(
      cases[i].name,
      cases[i].source,
      valid_symbols,
      cases[i].initial_mode,
      cases[i].expected_scanned,
      cases[i].expected_symbol,
      cases[i].expected_token_start,
      cases[i].expected_token_end,
      cases[i].expected_mode
    );
  }

  bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
  set_all_symbols_valid(valid_symbols);
  failed |= expect_scan_result_at(
    "raw newline resets string mode in error mode",
    "\n\"",
    valid_symbols,
    LEXICAL_MODE_STRING,
    true,
    STRING_END,
    0,
    0,
    LEXICAL_MODE_OUTSIDE
  );
  return failed;
}

static int check_error_mode_real_tokens(void) {
  static const struct {
    const char *name;
    const char *source;
    bool expected_scanned;
    enum TokenType expected_symbol;
    size_t expected_token_end;
    LexicalMode expected_mode;
  } cases[] = {
    {"error mode emits keyword",
      "END",
      true,
      END_WORD,
      3,
      LEXICAL_MODE_OUTSIDE},
    {"error mode emits name",
      "value",
      true,
      NAME_WORD,
      5,
      LEXICAL_MODE_OUTSIDE},
    {"error mode emits function name",
      "follow(",
      true,
      FUNC_NAME_WORD,
      6,
      LEXICAL_MODE_OUTSIDE},
    {"error mode emits built-in",
      "length",
      true,
      BUILTIN_FUNC_NAME_WORD,
      6,
      LEXICAL_MODE_OUTSIDE},
    {"error mode emits built-in call",
      "length (",
      true,
      BUILTIN_CALL_WORD,
      6,
      LEXICAL_MODE_OUTSIDE},
    {"error mode emits getline target",
      "getline x",
      true,
      GETLINE_TARGET_WORD,
      7,
      LEXICAL_MODE_OUTSIDE},
    {"error mode emits integer",
      "42",
      true,
      NUMBER_INTEGER,
      2,
      LEXICAL_MODE_OUTSIDE},
    {"error mode emits fraction",
      ".5",
      true,
      NUMBER_FRACTION,
      2,
      LEXICAL_MODE_OUTSIDE},
    {"error mode emits exponent",
      "1.5e+2",
      true,
      NUMBER_EXPONENT,
      6,
      LEXICAL_MODE_OUTSIDE},
    {"error mode emits composite operator",
      "+=",
      true,
      ADD_ASSIGN_OPERATOR,
      2,
      LEXICAL_MODE_OUTSIDE},
    {"error mode emits GE", ">=", true, GE_OPERATOR, 2, LEXICAL_MODE_OUTSIDE},
    {"error mode emits append",
      ">>",
      true,
      APPEND_OPERATOR,
      2,
      LEXICAL_MODE_OUTSIDE},
    {"error mode preserves slash longest match",
      "/=",
      true,
      DIV_ASSIGN_OPERATOR,
      2,
      LEXICAL_MODE_OUTSIDE},
    {"error mode keeps division precedence",
      "/x",
      true,
      DIVISION_SLASH,
      1,
      LEXICAL_MODE_OUTSIDE},
    {"error mode suppresses greater guard",
      ">",
      false,
      OUTPUT_GREATER_GUARD,
      0,
      LEXICAL_MODE_OUTSIDE},
    {"error mode suppresses line-continuation markers",
      "\\\nname",
      false,
      LC_BEFORE_EXPRESSION,
      0,
      LEXICAL_MODE_OUTSIDE},
    {"error mode emits no token for unknown punctuation",
      "@",
      false,
      ERROR_SENTINEL,
      0,
      LEXICAL_MODE_OUTSIDE},
    {"error mode emits comment", "# c", true, COMMENT, 3, LEXICAL_MODE_OUTSIDE},
    {"error mode emits string opening",
      "\"a",
      true,
      STRING_OPENING,
      1,
      LEXICAL_MODE_STRING},
  };

  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
    set_all_symbols_valid(valid_symbols);
    failed |= expect_scan_result_at(
      cases[i].name,
      cases[i].source,
      valid_symbols,
      LEXICAL_MODE_OUTSIDE,
      cases[i].expected_scanned,
      cases[i].expected_symbol,
      0,
      cases[i].expected_token_end,
      cases[i].expected_mode
    );
  }
  return failed;
}

static int
expect_mode(const char *test_name, LexicalMode expected, LexicalMode actual) {
  if (actual == expected) {
    return 0;
  }
  fprintf(
    stderr,
    "%s: expected mode %u, received %u\n",
    test_name,
    (unsigned)expected,
    (unsigned)actual
  );
  return 1;
}

static int
expect_length(const char *test_name, unsigned expected, unsigned actual) {
  if (actual == expected) {
    return 0;
  }
  fprintf(
    stderr,
    "%s: expected serialized length %u, received %u\n",
    test_name,
    expected,
    actual
  );
  return 1;
}

static int check_round_trip(
  const char *test_name,
  LexicalMode mode,
  unsigned expected_length
) {
  ScannerState source = {.mode = mode};
  ScannerState destination = {.mode = LEXICAL_MODE_ERE_ESCAPED_DELIMITER};
  char buffer[TREE_SITTER_SERIALIZATION_BUFFER_SIZE] = {0};
  const unsigned length =
    tree_sitter_posix_awk_external_scanner_serialize(&source, buffer);

  int failed = expect_length(test_name, expected_length, length);
  tree_sitter_posix_awk_external_scanner_deserialize(
    &destination,
    buffer,
    length
  );
  failed |= expect_mode(test_name, mode, destination.mode);
  return failed;
}

static int check_serialization(void) {
  int failed = 0;
  failed |=
    check_round_trip("outside mode round trip", LEXICAL_MODE_OUTSIDE, 0);
  failed |= check_round_trip(
    "ERE body mode round trip",
    LEXICAL_MODE_ERE_BODY,
    SERIALIZED_SCANNER_STATE_SIZE
  );
  failed |= check_round_trip(
    "escaped delimiter mode round trip",
    LEXICAL_MODE_ERE_ESCAPED_DELIMITER,
    SERIALIZED_SCANNER_STATE_SIZE
  );
  failed |= check_round_trip(
    "string mode round trip",
    LEXICAL_MODE_STRING,
    SERIALIZED_SCANNER_STATE_SIZE
  );

  char buffer[TREE_SITTER_SERIALIZATION_BUFFER_SIZE] = {
    (char)LEXICAL_MODE_ERE_BODY,
    0,
  };
  ScannerState destination = {.mode = LEXICAL_MODE_ERE_BODY};
  tree_sitter_posix_awk_external_scanner_deserialize(&destination, buffer, 0);
  failed |= expect_mode(
    "empty serialized state resets to outside",
    LEXICAL_MODE_OUTSIDE,
    destination.mode
  );

  destination.mode = LEXICAL_MODE_ERE_BODY;
  tree_sitter_posix_awk_external_scanner_deserialize(
    &destination,
    buffer,
    SERIALIZED_SCANNER_STATE_SIZE + 1
  );
  failed |= expect_mode(
    "oversized serialized state resets to outside",
    LEXICAL_MODE_OUTSIDE,
    destination.mode
  );

  buffer[0] = (char)(LEXICAL_MODE_STRING + 1);
  destination.mode = LEXICAL_MODE_ERE_BODY;
  tree_sitter_posix_awk_external_scanner_deserialize(
    &destination,
    buffer,
    SERIALIZED_SCANNER_STATE_SIZE
  );
  failed |= expect_mode(
    "invalid serialized mode resets to outside",
    LEXICAL_MODE_OUTSIDE,
    destination.mode
  );
  return failed;
}

int main(void) {
  int failed = 0;
  failed |= check_serialization();
  failed |= check_source_token_ranges();
  failed |= check_getline_target_lookahead();
  failed |= check_blank_skip_token_ranges();
  failed |= check_greater_dispatch();
  failed |= check_slash_dispatch();
  failed |= check_line_continuation_markers();
  failed |= check_linear_line_continuation_lookahead();
  failed |= check_ere_state_transitions();
  failed |= check_string_and_comment_modes();
  failed |= check_error_mode_real_tokens();
  return failed;
}
