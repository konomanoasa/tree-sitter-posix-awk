#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "../src/scanner.c"

#ifdef TREE_SITTER_REUSE_ALLOCATOR
static size_t reuse_calloc_calls;
static size_t reuse_free_calls;
static size_t reuse_live_allocations;
static bool reuse_fail_next_calloc;

static void *reuse_calloc(size_t count, size_t size) {
  reuse_calloc_calls += 1;
  if (reuse_fail_next_calloc) {
    reuse_fail_next_calloc = false;
    return NULL;
  }
  void *result = calloc(count, size);
  if (result != NULL) {
    reuse_live_allocations += 1;
  }
  return result;
}

static void reuse_free(void *allocation) {
  reuse_free_calls += 1;
  if (allocation != NULL) {
    assert(reuse_live_allocations > 0);
    reuse_live_allocations -= 1;
  }
  free(allocation);
}

void *(*ts_current_calloc)(size_t, size_t) = reuse_calloc;
void (*ts_current_free)(void *) = reuse_free;
#endif

typedef struct {
  TSLexer lexer;
  const char *source;
  size_t length;
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
  if (mock->offset < mock->length) {
    mock->offset++;
  }
  if (skip && !mock->content_started) {
    mock->token_start = mock->offset;
  } else {
    mock->content_started = true;
  }
  lexer->lookahead =
    mock->offset < mock->length ? (unsigned char)mock->source[mock->offset] : 0;
}

static void mock_mark_end(TSLexer *lexer) {
  MockLexer *mock = mock_lexer(lexer);
  mock->token_end = mock->offset;
}

static bool mock_eof(const TSLexer *lexer) {
  const MockLexer *mock = const_mock_lexer(lexer);
  return mock->offset == mock->length;
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
    .length = strlen(source),
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

static int test_source_token_ranges(void) {
  static const struct {
    const char *name;
    const char *source;
    enum TokenType token;
    size_t expected_token_end;
  } cases[] = {
    {"keyword spelling", "END", END_WORD, 3},
    {"getline spelling excludes its target", "getline value", GETLINE_WORD, 7},
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
    {"fraction with lowercase float suffix", "1.0f", NUMBER_FRACTION, 4},
    {"leading fraction with uppercase float suffix", ".5F", NUMBER_FRACTION, 3},
    {"exponent with lowercase long double suffix", "1e2l", NUMBER_EXPONENT, 4},
    {"trailing period with uppercase long double suffix",
      "1.L",
      NUMBER_FRACTION,
      3},
    {"fraction and exponent with float suffix", "1.5e+2F", NUMBER_EXPONENT, 7},
    {"fraction consumes only one suffix character",
      "1.5ff",
      NUMBER_FRACTION,
      4},
    {"exponent consumes only one suffix character",
      "1e2LL",
      NUMBER_EXPONENT,
      4},
    {"integer does not consume a floating suffix", "1f", NUMBER_INTEGER, 1},
    {"incomplete exponent keeps integer", "1e+", NUMBER_INTEGER, 1},
    {"incomplete exponent and float suffix keep integer",
      "1ef",
      NUMBER_INTEGER,
      1},
    {"incomplete signed exponent and float suffix keep fraction",
      "1.e+f",
      NUMBER_FRACTION,
      2},
    {"incomplete exponent and long double suffix keep fraction",
      ".5eL",
      NUMBER_FRACTION,
      2},
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

static int test_split_token_boundaries(void) {
  static const struct {
    const char *name;
    const char *source;
    enum TokenType expected_symbol;
    size_t expected_token_end;
  } cases[] = {
    {"split keyword", "BE\\\nGIN", SPLIT_TOKEN, 4},
    {"split identifier", "value\\\n_tail", SPLIT_TOKEN, 7},
    {"split builtin with repeated pairs", "len\\\n\\\ngth", SPLIT_TOKEN, 7},
    {"continued user call stays adjacent", "follow\\\n(", FUNC_NAME_WORD, 6},
    {"continued builtin call stays adjacent",
      "length\\\n(",
      BUILTIN_CALL_WORD,
      6},
    {"keyword boundary is not a split", "END\\\n;", END_WORD, 3},
    {"raw backslash prevents user call promotion", "follow\\(", NAME_WORD, 6},
    {"raw backslash after pairs prevents user call promotion",
      "follow\\\n\\(",
      NAME_WORD,
      6},
    {"raw backslash prevents builtin call promotion",
      "length\\(",
      BUILTIN_FUNC_NAME_WORD,
      6},
    {"raw backslash after pairs prevents builtin call promotion",
      "length\\\n\\(",
      BUILTIN_FUNC_NAME_WORD,
      6},
    {"split integer digits", "1\\\n2", SPLIT_TOKEN, 4},
    {"split leading fraction", ".\\\n5", SPLIT_TOKEN, 4},
    {"split trailing decimal point", "1\\\n.", SPLIT_TOKEN, 4},
    {"split fractional digits", "1.\\\n5", SPLIT_TOKEN, 5},
    {"split exponent introducer", "1\\\ne2", SPLIT_TOKEN, 5},
    {"split exponent digits", "1e\\\n2", SPLIT_TOKEN, 5},
    {"split exponent sign", "1e\\\n+2", SPLIT_TOKEN, 6},
    {"split signed exponent digits", "1e+\\\n2", SPLIT_TOKEN, 6},
    {"split fraction suffix", "1.0\\\nF", SPLIT_TOKEN, 6},
    {"split exponent suffix", "1e2\\\nl", SPLIT_TOKEN, 6},
    {"integer boundary before addition", "1\\\n+2", NUMBER_INTEGER, 1},
    {"integer boundary before suffix-like name", "1\\\nf", NUMBER_INTEGER, 1},
    {"incomplete continued exponent preserves integer",
      "1\\\ne",
      NUMBER_INTEGER,
      1},
    {"incomplete continued exponent sign preserves integer",
      "1\\\ne+f",
      NUMBER_INTEGER,
      1},
    {"incomplete continued exponent preserves fraction",
      "1.\\\ne+f",
      NUMBER_FRACTION,
      2},
    {"incomplete exponent does not accept suffix",
      "1.5\\\neL",
      NUMBER_FRACTION,
      3},
    {"completed suffix ends numeric token", "1.0f\\\nF", NUMBER_FRACTION, 4},
    {"exponent boundary before addition", "1e2\\\n+3", NUMBER_EXPONENT, 3},
    {"raw backslash does not join integer digits", "1\\2", NUMBER_INTEGER, 1},
    {"raw backslash does not join fractional digits",
      "1.\\5",
      NUMBER_FRACTION,
      2},
    {"raw backslash does not complete exponent", "1e\\2", NUMBER_INTEGER, 1},
    {"raw backslash does not complete signed exponent",
      "1e+\\2",
      NUMBER_INTEGER,
      1},
    {"raw backslash does not extend exponent", "1e2\\3", NUMBER_EXPONENT, 3},
    {"raw backslash does not attach numeric suffix",
      "1.0\\F",
      NUMBER_FRACTION,
      3},
    {"raw backslash after pairs does not join digits",
      "1\\\n\\2",
      NUMBER_INTEGER,
      1},
    {"raw backslash after continued exponent sign preserves integer",
      "1e\\\n+\\2",
      NUMBER_INTEGER,
      1},
    {"accepted split survives later invalid lookahead",
      "1\\\n2\\3",
      SPLIT_TOKEN,
      4},
  };
  const bool valid_symbols[TOKEN_TYPE_COUNT] = {
    [SPLIT_TOKEN] = true,
    [END_WORD] = true,
    [NAME_WORD] = true,
    [FUNC_NAME_WORD] = true,
    [BUILTIN_FUNC_NAME_WORD] = true,
    [BUILTIN_CALL_WORD] = true,
    [NUMBER_INTEGER] = true,
    [NUMBER_FRACTION] = true,
    [NUMBER_EXPONENT] = true,
  };
  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    failed |= expect_scan_result(
      cases[i].name,
      cases[i].source,
      valid_symbols,
      true,
      cases[i].expected_symbol,
      cases[i].expected_token_end
    );
  }
  failed |= expect_scan_result(
    "raw backslash cannot complete a leading fraction",
    ".\\5",
    valid_symbols,
    false,
    NUMBER_FRACTION,
    0
  );
  return failed;
}

static int test_split_composite_operators(void) {
  static const struct {
    const char *name;
    const char *source;
  } split_cases[] = {
    {"split division assignment", "/\\\n="},
    {"split addition assignment", "+\\\n="},
    {"split subtraction assignment", "-\\\n="},
    {"split multiplication assignment", "*\\\n="},
    {"split remainder assignment", "%\\\n="},
    {"split exponent assignment", "^\\\n="},
    {"split logical or", "|\\\n|"},
    {"split logical and", "&\\\n&"},
    {"split no-match", "!\\\n~"},
    {"split equality", "=\\\n="},
    {"split less-or-equal", "<\\\n="},
    {"split greater-or-equal", ">\\\n="},
    {"split inequality", "!\\\n="},
    {"split increment", "+\\\n+"},
    {"split decrement", "-\\\n-"},
    {"split append", ">\\\n>"},
  };
  bool valid_symbols[TOKEN_TYPE_COUNT] = {[SPLIT_TOKEN] = true};
  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(split_cases); i++) {
    failed |= expect_scan_result(
      split_cases[i].name,
      split_cases[i].source,
      valid_symbols,
      true,
      SPLIT_TOKEN,
      4
    );
  }
  valid_symbols[ADD_ASSIGN_OPERATOR] = true;
  failed |= expect_scan_result(
    "raw backslash cannot create assignment",
    "+\\=",
    valid_symbols,
    false,
    ADD_ASSIGN_OPERATOR,
    0
  );
  failed |= expect_scan_result(
    "raw backslash after pairs cannot create assignment",
    "+\\\n\\=",
    valid_symbols,
    false,
    ADD_ASSIGN_OPERATOR,
    0
  );
  failed |= expect_scan_result(
    "continued single operator is not split",
    "+\\\nx",
    valid_symbols,
    false,
    SPLIT_TOKEN,
    0
  );
  failed |= expect_scan_result(
    "repeated pairs split composite operator",
    "+\\\n\\\n=",
    valid_symbols,
    true,
    SPLIT_TOKEN,
    6
  );
  valid_symbols[GE_OPERATOR] = true;
  valid_symbols[OUTPUT_GREATER_GUARD] = true;
  failed |= expect_scan_result(
    "raw backslash keeps greater guard",
    ">\\=",
    valid_symbols,
    true,
    OUTPUT_GREATER_GUARD,
    0
  );
  failed |= expect_scan_result(
    "raw backslash after pairs keeps greater guard",
    ">\\\n\\=",
    valid_symbols,
    true,
    OUTPUT_GREATER_GUARD,
    0
  );
  valid_symbols[DIVISION_SLASH] = true;
  valid_symbols[DIV_ASSIGN_OPERATOR] = true;
  failed |= expect_scan_result(
    "raw backslash cannot create division assignment",
    "/\\=",
    valid_symbols,
    true,
    DIVISION_SLASH,
    1
  );
  failed |= expect_scan_result(
    "raw backslash after pairs cannot create division assignment",
    "/\\\n\\=",
    valid_symbols,
    true,
    DIVISION_SLASH,
    1
  );
  return failed;
}

static int test_split_lexical_modes(void) {
  static const struct {
    const char *name;
    const char *source;
    LexicalMode initial_mode;
    bool expected_scanned;
    enum TokenType expected_symbol;
    size_t expected_token_end;
    LexicalMode expected_mode;
  } cases[] = {
    {"string continuation is split",
      "\\\n",
      LEXICAL_MODE_STRING,
      true,
      SPLIT_TOKEN,
      2,
      LEXICAL_MODE_STRING},
    {"ERE continuation is split",
      "\\\n",
      LEXICAL_MODE_ERE_BODY,
      true,
      SPLIT_TOKEN,
      2,
      LEXICAL_MODE_ERE_BODY},
    {"escaped ERE delimiter context detects split",
      "\\\n",
      LEXICAL_MODE_ERE_ESCAPED_DELIMITER,
      true,
      SPLIT_TOKEN,
      2,
      LEXICAL_MODE_ERE_ESCAPED_DELIMITER},
    {"outside continuation is layout",
      "\\\n",
      LEXICAL_MODE_OUTSIDE,
      false,
      SPLIT_TOKEN,
      0,
      LEXICAL_MODE_OUTSIDE},
    {"comment backslash remains comment text",
      "# keep \\\n",
      LEXICAL_MODE_OUTSIDE,
      true,
      COMMENT,
      8,
      LEXICAL_MODE_OUTSIDE},
    {"raw string escape does not end string",
      "\\\"",
      LEXICAL_MODE_STRING,
      false,
      STRING_END,
      0,
      LEXICAL_MODE_STRING},
    {"raw ERE escape remains valid",
      "\\=",
      LEXICAL_MODE_ERE_BODY,
      true,
      ERE_ESCAPE_START,
      0,
      LEXICAL_MODE_ERE_BODY},
    {"escaped slash keeps delimiter mode",
      "\\/",
      LEXICAL_MODE_ERE_BODY,
      true,
      ERE_ESCAPED_DELIMITER_START,
      0,
      LEXICAL_MODE_ERE_ESCAPED_DELIMITER},
    {"ERE opening keeps escaped equals",
      "/\\=/",
      LEXICAL_MODE_OUTSIDE,
      true,
      ERE_OPENING_SLASH,
      1,
      LEXICAL_MODE_ERE_BODY},
    {"ERE opening keeps escaped slash",
      "/\\//",
      LEXICAL_MODE_OUTSIDE,
      true,
      ERE_OPENING_SLASH,
      1,
      LEXICAL_MODE_ERE_BODY},
    {"ERE opening keeps AWK escape",
      "/\\n/",
      LEXICAL_MODE_OUTSIDE,
      true,
      ERE_OPENING_SLASH,
      1,
      LEXICAL_MODE_ERE_BODY},
    {"ERE opening precedes interior continuation",
      "/\\\na/",
      LEXICAL_MODE_OUTSIDE,
      true,
      ERE_OPENING_SLASH,
      1,
      LEXICAL_MODE_ERE_BODY},
  };
  const bool valid_symbols[TOKEN_TYPE_COUNT] = {
    [SPLIT_TOKEN] = true,
    [COMMENT] = true,
    [STRING_END] = true,
    [ERE_OPENING_SLASH] = true,
    [DIV_ASSIGN_OPERATOR] = true,
    [ERE_ESCAPE_START] = true,
    [ERE_ESCAPED_DELIMITER_START] = true,
  };
  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    failed |= expect_scan_result_at(
      cases[i].name,
      cases[i].source,
      valid_symbols,
      cases[i].initial_mode,
      cases[i].expected_scanned,
      cases[i].expected_symbol,
      0,
      cases[i].expected_token_end,
      cases[i].expected_mode
    );
  }
  return failed;
}

static int test_linear_split_number_detection(void) {
  const size_t continuation_count = 32768;
  const size_t source_length = continuation_count * 3U + 1U;
  char *source = malloc(source_length + 1U);
  if (source == NULL) {
    return 1;
  }
  for (size_t i = 0; i < continuation_count; i++) {
    memcpy(source + i * 3U, "1\\\n", 3);
  }
  source[source_length - 1U] = '2';
  source[source_length] = '\0';
  MockLexer mock = make_mock_lexer(source);
  ScannerState state = {.mode = LEXICAL_MODE_OUTSIDE};
  const bool valid_symbols[TOKEN_TYPE_COUNT] = {[SPLIT_TOKEN] = true};
  const bool scanned = tree_sitter_posix_awk_external_scanner_scan(
    &state,
    &mock.lexer,
    valid_symbols
  );
  const bool valid_result = scanned &&
    mock.lexer.result_symbol ==
    SPLIT_TOKEN &&
    mock.token_start ==
    0 &&
    mock.token_end ==
    source_length &&
    mock.advance_count <= source_length;
  if (!valid_result) {
    fprintf(
      stderr,
      "linear split number: scanned=%u symbol=%u advances=%zu end=%zu\n",
      scanned,
      mock.lexer.result_symbol,
      mock.advance_count,
      mock.token_end
    );
  }
  free(source);
  return valid_result ? 0 : 1;
}

static int test_blank_skip_token_ranges(void) {
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

static int test_greater_dispatch(void) {
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

static int test_slash_dispatch(void) {
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

static int test_word_boundary_lookahead(void) {
  static const struct {
    const char *name;
    const char *source;
    enum TokenType expected_symbol;
    size_t expected_token_end;
  } cases[] = {
    {"function adjacency ignores repeated continuations",
      "follow\\\n\\\n(",
      FUNC_NAME_WORD,
      6},
    {"blank before a continuation prevents function adjacency",
      "follow \\\n(",
      NAME_WORD,
      6},
    {"blank after a continuation prevents function adjacency",
      "follow\\\n (",
      NAME_WORD,
      6},
    {"raw newline prevents function adjacency", "follow\\\n\n(", NAME_WORD, 6},
    {"comment prevents function adjacency",
      "follow\\\n# note\n(",
      NAME_WORD,
      6},
    {"incomplete continuation prevents function adjacency",
      "follow\\(",
      NAME_WORD,
      6},
    {"built-in call ignores mixed blank and continuation gaps",
      "length \t\\\n \\\n\t(",
      BUILTIN_CALL_WORD,
      6},
    {"raw newline prevents built-in call lookahead",
      "length \\\n\n(",
      BUILTIN_FUNC_NAME_WORD,
      6},
    {"comment prevents built-in call lookahead",
      "length \\\n# note\n(",
      BUILTIN_FUNC_NAME_WORD,
      6},
    {"incomplete continuation prevents built-in call lookahead",
      "length \\(",
      BUILTIN_FUNC_NAME_WORD,
      6},
    {"for-in lookahead ignores gaps around both words and closing parenthesis",
      "k \\\nin \t\\\narray \\\n)",
      FOR_IN_VARIABLE_WORD,
      1},
    {"raw newline prevents for-in lookahead",
      "k in \\\n\narray)",
      NAME_WORD,
      1},
    {"comment prevents for-in lookahead",
      "k in array \\\n# note\n)",
      NAME_WORD,
      1},
    {"incomplete continuation prevents for-in lookahead",
      "k in array \\)",
      NAME_WORD,
      1},
    {"reserved array word prevents for-in lookahead",
      "k in length)",
      NAME_WORD,
      1},
  };
  const bool valid_symbols[TOKEN_TYPE_COUNT] = {
    [NAME_WORD] = true,
    [FUNC_NAME_WORD] = true,
    [FOR_IN_VARIABLE_WORD] = true,
    [BUILTIN_FUNC_NAME_WORD] = true,
    [BUILTIN_CALL_WORD] = true,
  };

  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    failed |= expect_scan_result(
      cases[i].name,
      cases[i].source,
      valid_symbols,
      true,
      cases[i].expected_symbol,
      cases[i].expected_token_end
    );
  }
  return failed;
}

static int test_linear_word_boundary_lookahead(void) {
  static const struct {
    const char *name;
    const char *prefix;
    const char *suffix;
    enum TokenType expected_symbol;
    size_t expected_token_end;
  } cases[] = {
    {"linear function-name continuation lookahead",
      "follow",
      "(",
      FUNC_NAME_WORD,
      6},
    {"linear built-in continuation lookahead",
      "length ",
      "(",
      BUILTIN_CALL_WORD,
      6},
    {"linear for-in continuation lookahead",
      "k in array ",
      ")",
      FOR_IN_VARIABLE_WORD,
      1},
  };
  const size_t continuation_count = 32768;
  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    const size_t prefix_length = strlen(cases[i].prefix);
    const size_t suffix_length = strlen(cases[i].suffix);
    const size_t source_length =
      prefix_length + continuation_count * 2U + suffix_length;
    char *source = malloc(source_length + 1U);
    if (source == NULL) {
      fprintf(stderr, "%s: allocation failed\n", cases[i].name);
      return 1;
    }
    memcpy(source, cases[i].prefix, prefix_length);
    for (size_t index = 0; index < continuation_count; index++) {
      source[prefix_length + index * 2U] = '\\';
      source[prefix_length + index * 2U + 1U] = '\n';
    }
    memcpy(
      source + prefix_length + continuation_count * 2U,
      cases[i].suffix,
      suffix_length + 1U
    );

    MockLexer mock = make_mock_lexer(source);
    ScannerState state = {.mode = LEXICAL_MODE_OUTSIDE};
    bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
    valid_symbols[cases[i].expected_symbol] = true;
    const bool scanned = tree_sitter_posix_awk_external_scanner_scan(
      &state,
      &mock.lexer,
      valid_symbols
    );
    const bool valid_result = scanned &&
      mock.lexer.result_symbol ==
      cases[i].expected_symbol &&
      mock.token_start ==
      0 &&
      mock.token_end ==
      cases[i].expected_token_end &&
      mock.advance_count <= source_length;
    if (!valid_result) {
      fprintf(
        stderr,
        "%s: scanned=%u symbol=%u advances=%zu start=%zu end=%zu\n",
        cases[i].name,
        scanned,
        mock.lexer.result_symbol,
        mock.advance_count,
        mock.token_start,
        mock.token_end
      );
      failed = 1;
    }
    free(source);
  }
  return failed;
}

static int test_ere_state_transitions(void) {
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

static int test_string_and_comment_modes(void) {
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

static int test_error_mode_real_tokens(void) {
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
    {"error mode emits getline keyword",
      "getline x",
      true,
      GETLINE_WORD,
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
    {"error mode leaves line continuation to the internal lexer",
      "\\\nname",
      false,
      ERROR_SENTINEL,
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
  char buffer[TREE_SITTER_SERIALIZATION_BUFFER_SIZE + 2];
  memset(buffer, 0x5a, sizeof(buffer));
  const unsigned length =
    tree_sitter_posix_awk_external_scanner_serialize(&source, buffer + 1);
  assert(length <= TREE_SITTER_SERIALIZATION_BUFFER_SIZE);
  assert(buffer[0] == 0x5a);
  for (size_t index = length + 1; index < sizeof(buffer); index++) {
    assert(buffer[index] == 0x5a);
  }

  int failed = expect_length(test_name, expected_length, length);
  tree_sitter_posix_awk_external_scanner_deserialize(
    &destination,
    buffer + 1,
    length
  );
  failed |= expect_mode(test_name, mode, destination.mode);
  return failed;
}

static void test_lifecycle(void) {
  ScannerState *state = tree_sitter_posix_awk_external_scanner_create();
  assert(state != NULL);
  assert(state->mode == LEXICAL_MODE_OUTSIDE);
  state->mode = LEXICAL_MODE_STRING;
  tree_sitter_posix_awk_external_scanner_deserialize(state, NULL, 0);
  assert(state->mode == LEXICAL_MODE_OUTSIDE);
  tree_sitter_posix_awk_external_scanner_destroy(state);
}

static void test_nul_and_eof_are_distinct(void) {
  const char source[] = "#a\0b\n";
  MockLexer comment = make_mock_lexer(source);
  comment.length = sizeof(source) - 1;
  ScannerState state = {0};
  bool valid_symbols[TOKEN_TYPE_COUNT] = {[COMMENT] = true};
  assert(tree_sitter_posix_awk_external_scanner_scan(
    &state,
    &comment.lexer,
    valid_symbols
  ));
  assert(comment.lexer.result_symbol == COMMENT);
  assert(comment.token_end == 4);
  assert(comment.lexer.lookahead == '\n');

  valid_symbols[COMMENT] = false;
  valid_symbols[STRING_END] = true;
  MockLexer nul = make_mock_lexer("\0");
  nul.length = 1;
  state.mode = LEXICAL_MODE_STRING;
  assert(!tree_sitter_posix_awk_external_scanner_scan(
    &state,
    &nul.lexer,
    valid_symbols
  ));
  assert(state.mode == LEXICAL_MODE_STRING);

  MockLexer eof = make_mock_lexer("");
  assert(tree_sitter_posix_awk_external_scanner_scan(
    &state,
    &eof.lexer,
    valid_symbols
  ));
  assert(eof.lexer.result_symbol == STRING_END);
  assert(eof.token_end == 0);
  assert(state.mode == LEXICAL_MODE_OUTSIDE);
}

static void test_disabled_tokens_preserve_state(void) {
  const struct {
    const char *source;
    size_t length;
  } inputs[] = {
    {"BEGIN", 5},
    {"/", 1},
    {"\\/", 2},
    {"\"", 1},
    {"#comment", 8},
    {"", 0},
    {"\0", 1},
  };
  const bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
  for (
    LexicalMode mode = LEXICAL_MODE_OUTSIDE; mode <= LEXICAL_MODE_STRING; mode++
  ) {
    for (size_t index = 0; index < ARRAY_LENGTH(inputs); index++) {
      MockLexer mock = make_mock_lexer(inputs[index].source);
      mock.length = inputs[index].length;
      ScannerState state = {.mode = mode};
      assert(!tree_sitter_posix_awk_external_scanner_scan(
        &state,
        &mock.lexer,
        valid_symbols
      ));
      assert(state.mode == mode);
    }
  }
}

static int test_serialization(void) {
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

#ifdef TREE_SITTER_REUSE_ALLOCATOR
static void test_reuse_allocator_contract(void) {
  assert(reuse_calloc_calls > 0);
  assert(reuse_free_calls > 0);
  assert(reuse_live_allocations == 0);
  reuse_fail_next_calloc = true;
  assert(tree_sitter_posix_awk_external_scanner_create() == NULL);
  assert(!reuse_fail_next_calloc);
  assert(reuse_live_allocations == 0);
}
#endif

int main(void) {
  int failed = 0;
  test_lifecycle();
  test_nul_and_eof_are_distinct();
  test_disabled_tokens_preserve_state();
  failed |= test_serialization();
  failed |= test_source_token_ranges();
  failed |= test_split_token_boundaries();
  failed |= test_split_composite_operators();
  failed |= test_split_lexical_modes();
  failed |= test_linear_split_number_detection();
  failed |= test_blank_skip_token_ranges();
  failed |= test_greater_dispatch();
  failed |= test_slash_dispatch();
  failed |= test_word_boundary_lookahead();
  failed |= test_linear_word_boundary_lookahead();
  failed |= test_ere_state_transitions();
  failed |= test_string_and_comment_modes();
  failed |= test_error_mode_real_tokens();
#ifdef TREE_SITTER_REUSE_ALLOCATOR
  test_reuse_allocator_contract();
#endif
  return failed;
}
