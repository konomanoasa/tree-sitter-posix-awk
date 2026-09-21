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
        .result_symbol = UINT16_MAX,
        .advance = mock_advance,
        .mark_end = mock_mark_end,
        .eof = mock_eof,
      },
    .source = source,
    .length = strlen(source),
  };
}

static void resume_mock_lexer(MockLexer *mock) {
  mock->offset = mock->token_end;
  mock->token_start = mock->token_end;
  mock->content_started = false;
  mock->lexer.result_symbol = UINT16_MAX;
  mock->lexer.lookahead =
    mock->offset < mock->length ? (unsigned char)mock->source[mock->offset] : 0;
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
  bool valid = scanned == expected_scanned && state.mode == expected_mode;
  if (scanned) {
    valid = valid &&
      mock.lexer.result_symbol ==
      expected_symbol &&
      mock.token_start ==
      expected_token_start &&
      mock.token_end == expected_token_end;
  }
  if (valid) {
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
    {"keyword spelling", "END", END_KEYWORD, 3},
    {"getline spelling excludes its target", "getline value", GETLINE_WORD, 7},
    {"name spelling", "value", NAME_WORD, 5},
    {"built-in spelling", "length", BUILTIN_FUNC_NAME_WORD, 6},
    {"function name excludes parenthesis", "follow(", FUNC_NAME_WORD, 6},
    {"continuation separates a name from its parenthesis",
      "follow\\\n(",
      NAME_WORD,
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
    {"integer spelling", "123", NUMBER, 3},
    {"fraction spelling", ".5", NUMBER, 2},
    {"exponent spelling", "1.5e+2", NUMBER, 6},
    {"fraction with lowercase float suffix", "1.0f", NUMBER, 4},
    {"leading fraction with uppercase float suffix", ".5F", NUMBER, 3},
    {"exponent with lowercase long double suffix", "1e2l", NUMBER, 4},
    {"trailing period with uppercase long double suffix", "1.L", NUMBER, 3},
    {"fraction and exponent with float suffix", "1.5e+2F", NUMBER, 7},
    {"fraction consumes only one suffix character", "1.5ff", NUMBER, 4},
    {"exponent consumes only one suffix character", "1e2LL", NUMBER, 4},
    {"integer does not consume a floating suffix", "1f", NUMBER, 1},
    {"incomplete exponent keeps integer", "1e+", NUMBER, 1},
    {"incomplete exponent and float suffix keep integer", "1ef", NUMBER, 1},
    {"incomplete signed exponent and float suffix keep fraction",
      "1.e+f",
      NUMBER,
      2},
    {"incomplete exponent and long double suffix keep fraction",
      ".5eL",
      NUMBER,
      2},
    {"addition assignment spelling", "+=", ADD_ASSIGN_OPERATOR, 2},
    {"logical-or spelling", "||", OR_OPERATOR, 2},
    {"single plus excludes following continuation", "+\\\nx", PLUS_OPERATOR, 1},
    {"single minus excludes following continuation",
      "-\\\nx",
      MINUS_OPERATOR,
      1},
    {"single star excludes following continuation", "*\\\nx", STAR_OPERATOR, 1},
    {"single percent excludes following continuation",
      "%\\\nx",
      PERCENT_OPERATOR,
      1},
    {"single caret excludes following continuation",
      "^\\\nx",
      CARET_OPERATOR,
      1},
    {"single bang excludes following continuation", "!\\\nx", BANG_OPERATOR, 1},
    {"single less excludes following continuation", "<\\\nx", LESS_OPERATOR, 1},
    {"single greater excludes following continuation",
      ">\\\nx",
      GREATER_OPERATOR,
      1},
    {"single equal excludes following continuation",
      "=\\\nx",
      EQUAL_OPERATOR,
      1},
    {"single pipe excludes following continuation", "|\\\nx", PIPE_OPERATOR, 1},
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

static int test_continuations_separate_token_spellings(void) {
  static const struct {
    const char *name;
    const char *source;
    enum TokenType expected_symbol;
    size_t expected_token_end;
  } cases[] = {
    {"continuation splits a keyword into names", "BE\\\nGIN", NAME_WORD, 2},
    {"continuation ends an identifier", "value\\\n_tail", NAME_WORD, 5},
    {"repeated continuations do not join a builtin",
      "len\\\n\\\ngth",
      NAME_WORD,
      3},
    {"continuation prevents user call adjacency", "follow\\\n(", NAME_WORD, 6},
    {"builtin calls permit continuation layout",
      "length\\\n(",
      BUILTIN_CALL_WORD,
      6},
    {"keyword before continuation stays reserved", "END\\\n;", END_KEYWORD, 3},
    {"continuation ends an integer", "1\\\n2", NUMBER, 1},
    {"continuation before decimal point ends an integer", "1\\\n.", NUMBER, 1},
    {"continuation after decimal point ends a fraction", "1.\\\n5", NUMBER, 2},
    {"continuation before exponent ends an integer", "1\\\ne2", NUMBER, 1},
    {"continuation prevents exponent completion", "1e\\\n2", NUMBER, 1},
    {"continuation prevents signed exponent completion", "1e+\\\n2", NUMBER, 1},
    {"continuation separates a fractional suffix", "1.0\\\nF", NUMBER, 3},
    {"continuation separates an exponent suffix", "1e2\\\nl", NUMBER, 3},
    {"backslash does not join integer digits", "1\\2", NUMBER, 1},
    {"backslash does not complete an exponent", "1e\\2", NUMBER, 1},
    {"backslash does not attach a suffix", "1.0\\F", NUMBER, 3},
  };
  const bool valid_symbols[TOKEN_TYPE_COUNT] = {
    [BEGIN_KEYWORD] = true,
    [END_KEYWORD] = true,
    [NAME_WORD] = true,
    [FUNC_NAME_WORD] = true,
    [BUILTIN_FUNC_NAME_WORD] = true,
    [BUILTIN_CALL_WORD] = true,
    [NUMBER] = true,
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
    "continuation cannot complete a leading fraction",
    ".\\\n5",
    valid_symbols,
    false,
    NUMBER,
    0
  );
  return failed;
}

static void test_continuation_token_sequences(void) {
  static const struct {
    const char *source;
    enum TokenType tokens[4];
  } cases[] = {
    {"a\\\nb",
      {NAME_WORD, CONTINUATION_BACKSLASH, CONTINUATION_NEWLINE, NAME_WORD}},
    {"1\\\n2", {NUMBER, CONTINUATION_BACKSLASH, CONTINUATION_NEWLINE, NUMBER}},
    {"+\\\n=",
      {PLUS_OPERATOR,
        CONTINUATION_BACKSLASH,
        CONTINUATION_NEWLINE,
        EQUAL_OPERATOR}},
  };
  const size_t expected_starts[] = {0, 1, 2, 3};
  const size_t expected_ends[] = {1, 2, 3, 4};
  const LexicalMode expected_modes[] = {
    LEXICAL_MODE_OUTSIDE,
    LEXICAL_MODE_CONTINUED_NEWLINE,
    LEXICAL_MODE_OUTSIDE,
    LEXICAL_MODE_OUTSIDE,
  };
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    MockLexer mock = make_mock_lexer(cases[i].source);
    ScannerState state = {.mode = LEXICAL_MODE_OUTSIDE};
    for (size_t part = 0; part < ARRAY_LENGTH(cases[i].tokens); part++) {
      bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
      valid_symbols[cases[i].tokens[part]] = true;
      assert(tree_sitter_posix_awk_external_scanner_scan(
        &state,
        &mock.lexer,
        valid_symbols
      ));
      assert(mock.lexer.result_symbol == cases[i].tokens[part]);
      assert(mock.token_start == expected_starts[part]);
      assert(mock.token_end == expected_ends[part]);
      assert(state.mode == expected_modes[part]);
      resume_mock_lexer(&mock);
    }
  }
}

static int test_composite_operator_boundaries(void) {
  static const struct {
    const char *name;
    const char *source;
    const char *separated_source;
    enum TokenType composite;
    enum TokenType single;
  } cases[] = {
    {"division assignment",
      "/=",
      "/\\\n=",
      DIV_ASSIGN_OPERATOR,
      DIVISION_SLASH},
    {"addition assignment", "+=", "+\\\n=", ADD_ASSIGN_OPERATOR, PLUS_OPERATOR},
    {"subtraction assignment",
      "-=",
      "-\\\n=",
      SUB_ASSIGN_OPERATOR,
      MINUS_OPERATOR},
    {"multiplication assignment",
      "*=",
      "*\\\n=",
      MUL_ASSIGN_OPERATOR,
      STAR_OPERATOR},
    {"remainder assignment",
      "%=",
      "%\\\n=",
      MOD_ASSIGN_OPERATOR,
      PERCENT_OPERATOR},
    {"exponent assignment",
      "^=",
      "^\\\n=",
      POW_ASSIGN_OPERATOR,
      CARET_OPERATOR},
    {"logical or", "||", "|\\\n|", OR_OPERATOR, PIPE_OPERATOR},
    {"logical and", "&&", "&\\\n&", AND_OPERATOR, ERROR_SENTINEL},
    {"no match", "!~", "!\\\n~", NO_MATCH_OPERATOR, BANG_OPERATOR},
    {"equality", "==", "=\\\n=", EQ_OPERATOR, EQUAL_OPERATOR},
    {"less or equal", "<=", "<\\\n=", LE_OPERATOR, LESS_OPERATOR},
    {"greater or equal", ">=", ">\\\n=", GE_OPERATOR, GREATER_OPERATOR},
    {"inequality", "!=", "!\\\n=", NE_OPERATOR, BANG_OPERATOR},
    {"increment", "++", "+\\\n+", INCR_OPERATOR, PLUS_OPERATOR},
    {"decrement", "--", "-\\\n-", DECR_OPERATOR, MINUS_OPERATOR},
    {"append", ">>", ">\\\n>", APPEND_OPERATOR, GREATER_OPERATOR},
  };
  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
    const bool has_single = cases[i].single != ERROR_SENTINEL;
    valid_symbols[cases[i].composite] = true;
    if (has_single)
      valid_symbols[cases[i].single] = true;
    failed |= expect_scan_result(
      cases[i].name,
      cases[i].source,
      valid_symbols,
      true,
      cases[i].composite,
      2
    );
    failed |= expect_scan_result(
      cases[i].name,
      cases[i].separated_source,
      valid_symbols,
      has_single,
      cases[i].single,
      1
    );
    valid_symbols[cases[i].composite] = false;
    failed |= expect_scan_result(
      cases[i].name,
      cases[i].source,
      valid_symbols,
      false,
      cases[i].single,
      0
    );
  }
  return failed;
}

static int test_backslash_tokens_by_mode(void) {
  static const struct {
    const char *name;
    const char *source;
    LexicalMode mode;
    enum TokenType token;
    LexicalMode final_mode;
  } cases[] = {
    {"continuation before LF outside literals",
      "\\\n",
      LEXICAL_MODE_OUTSIDE,
      CONTINUATION_BACKSLASH,
      LEXICAL_MODE_CONTINUED_NEWLINE},
    {"stray backslash before a letter",
      "\\x",
      LEXICAL_MODE_OUTSIDE,
      STRAY_BACKSLASH,
      LEXICAL_MODE_OUTSIDE},
    {"stray backslash before a blank and LF",
      "\\ \n",
      LEXICAL_MODE_OUTSIDE,
      STRAY_BACKSLASH,
      LEXICAL_MODE_OUTSIDE},
    {"stray backslash before a tab and LF",
      "\\\t\n",
      LEXICAL_MODE_OUTSIDE,
      STRAY_BACKSLASH,
      LEXICAL_MODE_OUTSIDE},
    {"stray backslash before CR LF",
      "\\\r\n",
      LEXICAL_MODE_OUTSIDE,
      STRAY_BACKSLASH,
      LEXICAL_MODE_OUTSIDE},
    {"stray backslash before a continuation",
      "\\\\\n",
      LEXICAL_MODE_OUTSIDE,
      STRAY_BACKSLASH,
      LEXICAL_MODE_OUTSIDE},
    {"stray backslash at EOF outside literals",
      "\\",
      LEXICAL_MODE_OUTSIDE,
      STRAY_BACKSLASH,
      LEXICAL_MODE_OUTSIDE},
    {"stray backslash before LF in a string",
      "\\\n",
      LEXICAL_MODE_STRING,
      STRAY_BACKSLASH,
      LEXICAL_MODE_STRING},
    {"stray backslash at EOF in a string",
      "\\",
      LEXICAL_MODE_STRING,
      STRAY_BACKSLASH,
      LEXICAL_MODE_STRING},
    {"stray backslash before LF in an ERE",
      "\\\n",
      LEXICAL_MODE_ERE_BODY,
      STRAY_BACKSLASH,
      LEXICAL_MODE_ERE_BODY},
    {"stray backslash at EOF in an ERE",
      "\\",
      LEXICAL_MODE_ERE_BODY,
      STRAY_BACKSLASH,
      LEXICAL_MODE_ERE_BODY},
    {"stray backslash before LF in a collating symbol",
      "\\\n",
      LEXICAL_MODE_ERE_COLLATING,
      STRAY_BACKSLASH,
      LEXICAL_MODE_ERE_COLLATING},
    {"stray backslash at EOF in an equivalence class",
      "\\",
      LEXICAL_MODE_ERE_EQUIVALENCE,
      STRAY_BACKSLASH,
      LEXICAL_MODE_ERE_EQUIVALENCE},
    {"stray backslash before LF in a character class",
      "\\\n",
      LEXICAL_MODE_ERE_CLASS,
      STRAY_BACKSLASH,
      LEXICAL_MODE_ERE_CLASS},
  };
  const bool no_symbols[TOKEN_TYPE_COUNT] = {false};
  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    failed |= expect_scan_result_at(
      cases[i].name,
      cases[i].source,
      no_symbols,
      cases[i].mode,
      true,
      cases[i].token,
      0,
      1,
      cases[i].final_mode
    );
  }
  const bool comment_valid[TOKEN_TYPE_COUNT] = {[COMMENT] = true};
  failed |= expect_scan_result(
    "comment retains its final backslash",
    "# keep \\\n",
    comment_valid,
    true,
    COMMENT,
    8
  );
  return failed;
}

static int test_escape_classification_ignores_expected_tokens(void) {
  static const struct {
    const char *name;
    const char *source;
    enum TokenType expected;
    enum TokenType token;
  } cases[] = {
    {"named escape is not reclassified as undefined",
      "\\n",
      ERE_UNDEFINED_ESCAPE,
      ERE_NAMED_ESCAPE},
    {"escaped delimiter is not reclassified as quoted",
      "\\/",
      ERE_QUOTED_ESCAPE,
      ERE_ESCAPED_DELIMITER},
    {"octal escape is not reclassified as quoted",
      "\\1",
      ERE_QUOTED_ESCAPE,
      ERE_OCTAL_ESCAPE},
  };
  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
    valid_symbols[cases[i].expected] = true;
    failed |= expect_scan_result_at(
      cases[i].name,
      cases[i].source,
      valid_symbols,
      LEXICAL_MODE_ERE_BODY,
      true,
      cases[i].token,
      0,
      2,
      LEXICAL_MODE_ERE_BODY
    );
  }
  return failed;
}

static int test_continued_newline_requires_its_marker(void) {
  const bool valid_symbols[TOKEN_TYPE_COUNT] = {
    [CONTINUATION_NEWLINE] = true,
  };
  int failed = 0;
  for (
    LexicalMode mode = LEXICAL_MODE_OUTSIDE;
    mode <= LEXICAL_MODE_CONTINUED_NEWLINE;
    mode++
  ) {
    failed |= expect_scan_result_at(
      "only a pending continuation consumes a hidden newline",
      "\n\n",
      valid_symbols,
      mode,
      mode == LEXICAL_MODE_CONTINUED_NEWLINE,
      CONTINUATION_NEWLINE,
      0,
      1,
      mode == LEXICAL_MODE_CONTINUED_NEWLINE ? LEXICAL_MODE_OUTSIDE : mode
    );
  }
  return failed;
}

static void test_repeated_continuation_ranges_and_restoration(void) {
  MockLexer mock = make_mock_lexer(" \t\\\n\\\n");
  ScannerState state = {.mode = LEXICAL_MODE_OUTSIDE};
  const bool valid_symbols[TOKEN_TYPE_COUNT] = {
    [CONTINUATION_BACKSLASH] = true,
    [CONTINUATION_NEWLINE] = true,
  };
  const enum TokenType expected_symbols[] = {
    CONTINUATION_BACKSLASH,
    CONTINUATION_NEWLINE,
    CONTINUATION_BACKSLASH,
    CONTINUATION_NEWLINE,
  };
  const LexicalMode expected_modes[] = {
    LEXICAL_MODE_CONTINUED_NEWLINE,
    LEXICAL_MODE_OUTSIDE,
    LEXICAL_MODE_CONTINUED_NEWLINE,
    LEXICAL_MODE_OUTSIDE,
  };
  const size_t expected_starts[] = {2, 3, 4, 5};
  const size_t expected_ends[] = {3, 4, 5, 6};
  for (size_t index = 0; index < ARRAY_LENGTH(expected_symbols); index++) {
    assert(tree_sitter_posix_awk_external_scanner_scan(
      &state,
      &mock.lexer,
      valid_symbols
    ));
    assert(mock.lexer.result_symbol == expected_symbols[index]);
    assert(mock.token_start == expected_starts[index]);
    assert(mock.token_end == expected_ends[index]);
    assert(state.mode == expected_modes[index]);
    char buffer[TREE_SITTER_SERIALIZATION_BUFFER_SIZE];
    const unsigned length =
      tree_sitter_posix_awk_external_scanner_serialize(&state, buffer);
    state.mode = LEXICAL_MODE_STRING;
    tree_sitter_posix_awk_external_scanner_deserialize(&state, buffer, length);
    assert(state.mode == expected_modes[index]);
    resume_mock_lexer(&mock);
  }
  assert(mock_eof(&mock.lexer));
  assert(!tree_sitter_posix_awk_external_scanner_scan(
    &state,
    &mock.lexer,
    valid_symbols
  ));
  assert(state.mode == LEXICAL_MODE_OUTSIDE);
}

static void test_pending_continuation_rejects_changed_boundaries(void) {
  const struct {
    const char *source;
    size_t length;
  } changed_suffixes[] = {
    {"", 0},
    {" \n", 2},
    {"\t\n", 2},
    {"\r\n", 2},
    {"\\\n", 2},
    {"name", 4},
    {"# comment\n", 10},
    {"\"", 1},
    {"/", 1},
    {"\0", 1},
  };
  for (size_t index = 0; index < ARRAY_LENGTH(changed_suffixes); index++) {
    const bool marker_valid[TOKEN_TYPE_COUNT] = {
      [CONTINUATION_BACKSLASH] = true,
    };
    MockLexer marker = make_mock_lexer("\\\n");
    ScannerState state = {.mode = LEXICAL_MODE_OUTSIDE};
    assert(tree_sitter_posix_awk_external_scanner_scan(
      &state,
      &marker.lexer,
      marker_valid
    ));
    assert(marker.token_end == 1);
    assert(state.mode == LEXICAL_MODE_CONTINUED_NEWLINE);

    bool all_symbols[TOKEN_TYPE_COUNT] = {false};
    for (size_t symbol = 0; symbol < TOKEN_TYPE_COUNT; symbol++) {
      all_symbols[symbol] = true;
    }
    MockLexer changed = make_mock_lexer(changed_suffixes[index].source);
    changed.length = changed_suffixes[index].length;
    assert(!tree_sitter_posix_awk_external_scanner_scan(
      &state,
      &changed.lexer,
      all_symbols
    ));
    assert(state.mode == LEXICAL_MODE_CONTINUED_NEWLINE);

    MockLexer restored = make_mock_lexer("\nname");
    assert(tree_sitter_posix_awk_external_scanner_scan(
      &state,
      &restored.lexer,
      all_symbols
    ));
    assert(restored.lexer.result_symbol == CONTINUATION_NEWLINE);
    assert(restored.token_start == 0);
    assert(restored.token_end == 1);
    assert(state.mode == LEXICAL_MODE_OUTSIDE);
    resume_mock_lexer(&restored);
    const bool name_valid[TOKEN_TYPE_COUNT] = {[NAME_WORD] = true};
    assert(tree_sitter_posix_awk_external_scanner_scan(
      &state,
      &restored.lexer,
      name_valid
    ));
    assert(restored.lexer.result_symbol == NAME_WORD);
    assert(restored.token_start == 1);
    assert(restored.token_end == 5);
  }
}

static int test_literal_classification(void) {
  static const struct {
    const char *name;
    const char *source;
    LexicalMode mode;
    enum TokenType token;
    size_t end;
    bool accepted;
  } cases[] = {
    {"string content ends before its quote",
      "text\"",
      LEXICAL_MODE_STRING,
      STRING_CONTENT,
      4,
      true},
    {"string content ends before an escape",
      "text\\n",
      LEXICAL_MODE_STRING,
      STRING_CONTENT,
      4,
      true},
    {"string content cannot consume a newline",
      "text\n",
      LEXICAL_MODE_STRING,
      STRING_CONTENT,
      4,
      true},
    {"empty content before a quote is not a token",
      "\"",
      LEXICAL_MODE_STRING,
      STRING_CONTENT,
      0,
      false},
    {"hash in string content is not a comment",
      "# text\"",
      LEXICAL_MODE_STRING,
      STRING_CONTENT,
      6,
      true},
    {"backslash pair ends before the newline",
      "\\\\\nn",
      LEXICAL_MODE_ERE_BODY,
      ERE_QUOTED_ESCAPE,
      2,
      true},

    {"named ERE escape",
      "\\n",
      LEXICAL_MODE_ERE_BODY,
      ERE_NAMED_ESCAPE,
      2,
      true},
    {"quoted ERE escape",
      "\\+",
      LEXICAL_MODE_ERE_BODY,
      ERE_QUOTED_ESCAPE,
      2,
      true},
    {"undefined ERE escape",
      "\\q",
      LEXICAL_MODE_ERE_BODY,
      ERE_UNDEFINED_ESCAPE,
      2,
      true},
    {"ERE escaped backslash",
      "\\\\",
      LEXICAL_MODE_ERE_BODY,
      ERE_QUOTED_ESCAPE,
      2,
      true},
    {"ERE octal escape stops after three digits",
      "\\1417",
      LEXICAL_MODE_ERE_BODY,
      ERE_OCTAL_ESCAPE,
      4,
      true},
    {"ERE octal escape stops before nonoctal digit",
      "\\178",
      LEXICAL_MODE_ERE_BODY,
      ERE_OCTAL_ESCAPE,
      3,
      true},
    {"string escape retains unspecified spelling",
      "\\q",
      LEXICAL_MODE_STRING,
      STRING_ESCAPE,
      2,
      true},
    {"string octal escape stops before nonoctal digit",
      "\\178",
      LEXICAL_MODE_STRING,
      STRING_ESCAPE,
      3,
      true},
    {"class name includes digits after its initial letter",
      "al1:",
      LEXICAL_MODE_ERE_BODY,
      ERE_CLASS_NAME,
      3,
      true},
    {"class name cannot begin with a digit",
      "1alpha:",
      LEXICAL_MODE_ERE_BODY,
      ERE_CLASS_NAME,
      0,
      false},
    {"ERE count stops before continuation and comma",
      "12\\\n,",
      LEXICAL_MODE_ERE_BODY,
      ERE_DUP_COUNT,
      2,
      true},
  };
  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
    valid_symbols[cases[i].token] = true;
    failed |= expect_scan_result_at(
      cases[i].name,
      cases[i].source,
      valid_symbols,
      cases[i].mode,
      cases[i].accepted,
      cases[i].token,
      0,
      cases[i].end,
      cases[i].mode
    );
  }
  return failed;
}

static int test_blank_skip_token_ranges(void) {
  static const struct {
    const char *name;
    const char *source;
    enum TokenType token;
    size_t expected_token_start;
    size_t expected_token_end;
  } cases[] = {
    {"blanks precede a keyword", "  END", END_KEYWORD, 2, 5},
    {"a tab precedes a number", "\t1.5", NUMBER, 1, 4},
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
    bool redirection_valid;
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
      OUTPUT_GREATER,
      1},
    {"append is not split into a redirection token",
      ">>f",
      true,
      true,
      false,
      false,
      OUTPUT_GREATER,
      0},
    {"GE beside redirection token",
      ">=1",
      true,
      true,
      false,
      true,
      GE_OPERATOR,
      2},
    {"GE is not split into a redirection token",
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
    valid_symbols[OUTPUT_GREATER] = cases[i].redirection_valid;
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
    {"repeated continuations prevent function adjacency",
      "follow\\\n\\\n(",
      NAME_WORD,
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
    {"name recognition stops before a long continuation gap",
      "follow",
      "(",
      NAME_WORD,
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

static int test_long_token_spans(void) {
  static const struct {
    const char *name;
    char character;
    const char *suffix;
    enum TokenType token;
    LexicalMode mode;
  } cases[] = {
    {"long function name is not truncated",
      'a',
      "(",
      FUNC_NAME_WORD,
      LEXICAL_MODE_OUTSIDE},
    {"long number is not truncated", '1', ";", NUMBER, LEXICAL_MODE_OUTSIDE},
    {"long string content is not truncated",
      'a',
      "\"",
      STRING_CONTENT,
      LEXICAL_MODE_STRING},
  };
  const size_t token_length = 65536;
  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    char *source = malloc(token_length + 2);
    assert(source != NULL);
    memset(source, cases[i].character, token_length);
    memcpy(source + token_length, cases[i].suffix, 2);
    MockLexer mock = make_mock_lexer(source);
    ScannerState state = {.mode = cases[i].mode};
    bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
    valid_symbols[cases[i].token] = true;
    const bool scanned = tree_sitter_posix_awk_external_scanner_scan(
      &state,
      &mock.lexer,
      valid_symbols
    );
    if (
      !scanned ||
      mock.lexer.result_symbol !=
      cases[i].token ||
      mock.token_start !=
      0 ||
      mock.token_end !=
      token_length ||
      mock.advance_count >
      token_length +
      1 ||
      state.mode != cases[i].mode
    ) {
      fprintf(
        stderr,
        "%s: scanned=%u start=%zu end=%zu advances=%zu\n",
        cases[i].name,
        scanned,
        mock.token_start,
        mock.token_end,
        mock.advance_count
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
  valid_symbols[ERE_NAMED_ESCAPE] = true;
  failed |= expect_scan_result_at(
    "ERE escape keeps body mode",
    "\\n",
    valid_symbols,
    LEXICAL_MODE_ERE_BODY,
    true,
    ERE_NAMED_ESCAPE,
    0,
    2,
    LEXICAL_MODE_ERE_BODY
  );

  valid_symbols[ERE_NAMED_ESCAPE] = false;
  valid_symbols[ERE_ESCAPED_DELIMITER] = true;
  failed |= expect_scan_result_at(
    "escaped delimiter retains body mode",
    "\\/",
    valid_symbols,
    LEXICAL_MODE_ERE_BODY,
    true,
    ERE_ESCAPED_DELIMITER,
    0,
    2,
    LEXICAL_MODE_ERE_BODY
  );

  valid_symbols[ERE_ESCAPED_DELIMITER] = false;
  valid_symbols[ERE_COMPOUND_OPENING] = true;
  failed |= expect_scan_result_at(
    "compound opener consumes its first character",
    "[.",
    valid_symbols,
    LEXICAL_MODE_ERE_BODY,
    true,
    ERE_COMPOUND_OPENING,
    0,
    1,
    LEXICAL_MODE_ERE_COLLATING
  );

  valid_symbols[ERE_COMPOUND_OPENING] = false;
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
  valid_symbols[ERE_DOT_CLOSING] = true;
  failed |= expect_scan_result_at(
    "compound closer consumes its first character",
    ".]",
    valid_symbols,
    LEXICAL_MODE_ERE_COLLATING,
    true,
    ERE_DOT_CLOSING,
    0,
    1,
    LEXICAL_MODE_ERE_BODY
  );

  set_all_symbols_valid(valid_symbols);
  failed |= expect_scan_result_at(
    "error mode suppresses ERE compound classification",
    "[.",
    valid_symbols,
    LEXICAL_MODE_ERE_BODY,
    false,
    ERE_COMPOUND_OPENING,
    0,
    0,
    LEXICAL_MODE_ERE_BODY
  );
  failed |= expect_scan_result_at(
    "error mode suppresses escaped delimiters",
    "\\/",
    valid_symbols,
    LEXICAL_MODE_ERE_BODY,
    false,
    ERE_ESCAPED_DELIMITER,
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
    {"string closing consumes the quote",
      "\"",
      LEXICAL_MODE_STRING,
      STRING_CLOSING,
      true,
      STRING_CLOSING,
      0,
      1,
      LEXICAL_MODE_OUTSIDE},
    {"raw newline cannot close a string",
      "\n",
      LEXICAL_MODE_STRING,
      STRING_CLOSING,
      false,
      STRING_CLOSING,
      0,
      0,
      LEXICAL_MODE_STRING},
    {"EOF cannot close a string",
      "",
      LEXICAL_MODE_STRING,
      STRING_CLOSING,
      false,
      STRING_CLOSING,
      0,
      0,
      LEXICAL_MODE_STRING},
    {"string content stays internal",
      "a\"",
      LEXICAL_MODE_STRING,
      STRING_CLOSING,
      false,
      STRING_CLOSING,
      0,
      0,
      LEXICAL_MODE_STRING},
    {"string end waits for the escape character",
      "\"",
      LEXICAL_MODE_STRING,
      COMMENT,
      false,
      STRING_CLOSING,
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
    "raw newline stays unconsumed in string error mode",
    "\n\"",
    valid_symbols,
    LEXICAL_MODE_STRING,
    false,
    STRING_CLOSING,
    0,
    0,
    LEXICAL_MODE_STRING
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
    {"error mode suppresses classification of keyword",
      "END",
      false,
      END_KEYWORD,
      0,
      LEXICAL_MODE_OUTSIDE},
    {"error mode suppresses classification of name",
      "value",
      false,
      NAME_WORD,
      0,
      LEXICAL_MODE_OUTSIDE},
    {"error mode suppresses classification of function name",
      "follow(",
      false,
      FUNC_NAME_WORD,
      0,
      LEXICAL_MODE_OUTSIDE},
    {"error mode suppresses classification of built-in",
      "length",
      false,
      BUILTIN_FUNC_NAME_WORD,
      0,
      LEXICAL_MODE_OUTSIDE},
    {"error mode suppresses classification of built-in call",
      "length (",
      false,
      BUILTIN_CALL_WORD,
      0,
      LEXICAL_MODE_OUTSIDE},
    {"error mode suppresses classification of getline keyword",
      "getline x",
      false,
      GETLINE_WORD,
      0,
      LEXICAL_MODE_OUTSIDE},
    {"error mode suppresses classification of integer",
      "42",
      false,
      NUMBER,
      0,
      LEXICAL_MODE_OUTSIDE},
    {"error mode suppresses classification of fraction",
      ".5",
      false,
      NUMBER,
      0,
      LEXICAL_MODE_OUTSIDE},
    {"error mode suppresses classification of exponent",
      "1.5e+2",
      false,
      NUMBER,
      0,
      LEXICAL_MODE_OUTSIDE},
    {"error mode suppresses classification of composite operator",
      "+=",
      false,
      ADD_ASSIGN_OPERATOR,
      0,
      LEXICAL_MODE_OUTSIDE},
    {"error mode suppresses classification of GE",
      ">=",
      false,
      GE_OPERATOR,
      0,
      LEXICAL_MODE_OUTSIDE},
    {"error mode suppresses classification of append",
      ">>",
      false,
      APPEND_OPERATOR,
      0,
      LEXICAL_MODE_OUTSIDE},
    {"error mode suppresses division assignment classification",
      "/=",
      false,
      DIV_ASSIGN_OPERATOR,
      0,
      LEXICAL_MODE_OUTSIDE},
    {"error mode suppresses division classification",
      "/x",
      false,
      DIVISION_SLASH,
      0,
      LEXICAL_MODE_OUTSIDE},
    {"error mode suppresses greater token",
      ">",
      false,
      OUTPUT_GREATER,
      0,
      LEXICAL_MODE_OUTSIDE},
    {"error mode emits only the continuation backslash",
      "\\\nname",
      true,
      CONTINUATION_BACKSLASH,
      1,
      LEXICAL_MODE_CONTINUED_NEWLINE},
    {"error mode emits a stray backslash",
      "\\name",
      true,
      STRAY_BACKSLASH,
      1,
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
  ScannerState destination = {.mode = LEXICAL_MODE_STRING};
  char buffer[TREE_SITTER_SERIALIZATION_BUFFER_SIZE + 2];
  memset(buffer, 0x5a, sizeof(buffer));
  const unsigned length =
    tree_sitter_posix_awk_external_scanner_serialize(&source, buffer + 1);
  assert(length <= TREE_SITTER_SERIALIZATION_BUFFER_SIZE);
  assert(buffer[0] == 0x5a);
  for (size_t index = length + 1; index < sizeof(buffer); index++) {
    assert(buffer[index] == 0x5a);
  }
  tree_sitter_posix_awk_external_scanner_deserialize(
    &destination,
    buffer + 1,
    length
  );
  return expect_length(test_name, expected_length, length) |
    expect_mode(test_name, mode, destination.mode);
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
  valid_symbols[STRING_CONTENT] = true;
  MockLexer nul = make_mock_lexer("\0");
  nul.length = 1;
  state.mode = LEXICAL_MODE_STRING;
  assert(tree_sitter_posix_awk_external_scanner_scan(
    &state,
    &nul.lexer,
    valid_symbols
  ));
  assert(nul.lexer.result_symbol == STRING_CONTENT);
  assert(nul.token_end == 1);
  assert(state.mode == LEXICAL_MODE_STRING);

  MockLexer eof = make_mock_lexer("");
  assert(!tree_sitter_posix_awk_external_scanner_scan(
    &state,
    &eof.lexer,
    valid_symbols
  ));
  assert(state.mode == LEXICAL_MODE_STRING);
}

static void test_disabled_tokens_preserve_state(void) {
  const struct {
    const char *source;
    size_t length;
  } inputs[] = {
    {"BEGIN", 5},
    {"/", 1},
    {"\"", 1},
    {"#comment", 8},
    {"\n", 1},
    {"", 0},
    {"\0", 1},
  };
  const bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
  for (
    LexicalMode mode = LEXICAL_MODE_OUTSIDE;
    mode <= LEXICAL_MODE_CONTINUED_NEWLINE;
    mode++
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
  static const struct {
    const char *name;
    LexicalMode mode;
    unsigned length;
  } cases[] = {
    {"outside mode round trip", LEXICAL_MODE_OUTSIDE, 0},
    {"ERE body mode round trip", LEXICAL_MODE_ERE_BODY, 1},
    {"collating symbol mode round trip", LEXICAL_MODE_ERE_COLLATING, 1},
    {"equivalence class mode round trip", LEXICAL_MODE_ERE_EQUIVALENCE, 1},
    {"character class mode round trip", LEXICAL_MODE_ERE_CLASS, 1},
    {"string mode round trip", LEXICAL_MODE_STRING, 1},
    {"continued newline mode round trip", LEXICAL_MODE_CONTINUED_NEWLINE, 1},
  };
  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    failed |= check_round_trip(cases[i].name, cases[i].mode, cases[i].length);
  }
  const char invalid_modes[] = {
    (char)(LEXICAL_MODE_CONTINUED_NEWLINE + 1),
    (char)0xff
  };
  for (size_t i = 0; i < ARRAY_LENGTH(invalid_modes); i++) {
    ScannerState destination = {.mode = LEXICAL_MODE_STRING};
    tree_sitter_posix_awk_external_scanner_deserialize(
      &destination,
      &invalid_modes[i],
      1
    );
    failed |= expect_mode(
      "invalid encoded mode resets state",
      LEXICAL_MODE_OUTSIDE,
      destination.mode
    );
  }
  const char buffer[] = {LEXICAL_MODE_ERE_BODY, 0};
  const unsigned invalid_lengths[] = {0, 2};
  for (size_t i = 0; i < ARRAY_LENGTH(invalid_lengths); i++) {
    ScannerState destination = {.mode = LEXICAL_MODE_STRING};
    tree_sitter_posix_awk_external_scanner_deserialize(
      &destination,
      buffer,
      invalid_lengths[i]
    );
    failed |= expect_mode(
      "invalid serialized length resets state",
      LEXICAL_MODE_OUTSIDE,
      destination.mode
    );
  }
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

static int test_compound_terminators_do_not_become_content(void) {
  const struct {
    const char *name;
    const char *source;
    LexicalMode mode;
    bool closing_valid;
    bool scanned;
    enum TokenType symbol;
    LexicalMode final_mode;
  } cases[] = {
    {"continuation cannot form a collating terminator",
      ".\\\n]",
      LEXICAL_MODE_ERE_COLLATING,
      true,
      true,
      ERE_COMPOUND_CONTENT,
      LEXICAL_MODE_ERE_COLLATING},
    {"continuation cannot form an equivalence terminator",
      "=\\\n]",
      LEXICAL_MODE_ERE_EQUIVALENCE,
      true,
      true,
      ERE_COMPOUND_CONTENT,
      LEXICAL_MODE_ERE_EQUIVALENCE},
    {"continuation cannot form a class terminator",
      ":\\\n]",
      LEXICAL_MODE_ERE_CLASS,
      true,
      true,
      ERE_COMPOUND_CONTENT,
      LEXICAL_MODE_ERE_CLASS},

    {"empty collating symbol cannot hide its terminator",
      ".]",
      LEXICAL_MODE_ERE_COLLATING,
      false,
      false,
      ERE_COMPOUND_CONTENT,
      LEXICAL_MODE_ERE_COLLATING},
    {"empty equivalence class cannot hide its terminator",
      "=]",
      LEXICAL_MODE_ERE_EQUIVALENCE,
      false,
      false,
      ERE_COMPOUND_CONTENT,
      LEXICAL_MODE_ERE_EQUIVALENCE},
    {"collating symbol accepts its terminator",
      ".]",
      LEXICAL_MODE_ERE_COLLATING,
      true,
      true,
      ERE_DOT_CLOSING,
      LEXICAL_MODE_ERE_BODY},
    {"equivalence class accepts its terminator",
      "=]",
      LEXICAL_MODE_ERE_EQUIVALENCE,
      true,
      true,
      ERE_EQUAL_CLOSING,
      LEXICAL_MODE_ERE_BODY},
    {"character class accepts its terminator",
      ":]",
      LEXICAL_MODE_ERE_CLASS,
      true,
      true,
      ERE_COLON_CLOSING,
      LEXICAL_MODE_ERE_BODY},
    {"an equality marker stays content in a collating symbol",
      "=]",
      LEXICAL_MODE_ERE_COLLATING,
      true,
      true,
      ERE_COMPOUND_CONTENT,
      LEXICAL_MODE_ERE_COLLATING},
    {"a dot stays content in an equivalence class",
      ".]",
      LEXICAL_MODE_ERE_EQUIVALENCE,
      true,
      true,
      ERE_COMPOUND_CONTENT,
      LEXICAL_MODE_ERE_EQUIVALENCE},
    {"a colon stays content in a collating symbol",
      ":]",
      LEXICAL_MODE_ERE_COLLATING,
      true,
      true,
      ERE_COMPOUND_CONTENT,
      LEXICAL_MODE_ERE_COLLATING},
    {"a nonterminating dot stays content",
      ".x",
      LEXICAL_MODE_ERE_COLLATING,
      false,
      true,
      ERE_COMPOUND_CONTENT,
      LEXICAL_MODE_ERE_COLLATING},
  };
  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    bool valid_symbols[TOKEN_TYPE_COUNT] = {[ERE_COMPOUND_CONTENT] = true};
    valid_symbols[ERE_DOT_CLOSING] = cases[i].closing_valid;
    valid_symbols[ERE_EQUAL_CLOSING] = cases[i].closing_valid;
    valid_symbols[ERE_COLON_CLOSING] = cases[i].closing_valid;
    failed |= expect_scan_result_at(
      cases[i].name,
      cases[i].source,
      valid_symbols,
      cases[i].mode,
      cases[i].scanned,
      cases[i].symbol,
      0,
      1,
      cases[i].final_mode
    );
  }
  return failed;
}

int main(void) {
  int failed = 0;
  test_lifecycle();
  test_nul_and_eof_are_distinct();
  test_disabled_tokens_preserve_state();
  failed |= test_serialization();
  failed |= test_source_token_ranges();
  failed |= test_continuations_separate_token_spellings();
  test_continuation_token_sequences();
  failed |= test_composite_operator_boundaries();
  failed |= test_backslash_tokens_by_mode();
  failed |= test_escape_classification_ignores_expected_tokens();
  failed |= test_continued_newline_requires_its_marker();
  test_repeated_continuation_ranges_and_restoration();
  test_pending_continuation_rejects_changed_boundaries();
  failed |= test_literal_classification();
  failed |= test_blank_skip_token_ranges();
  failed |= test_greater_dispatch();
  failed |= test_slash_dispatch();
  failed |= test_word_boundary_lookahead();
  failed |= test_linear_word_boundary_lookahead();
  failed |= test_long_token_spans();
  failed |= test_ere_state_transitions();
  failed |= test_compound_terminators_do_not_become_content();
  failed |= test_string_and_comment_modes();
  failed |= test_error_mode_real_tokens();
#ifdef TREE_SITTER_REUSE_ALLOCATOR
  test_reuse_allocator_contract();
#endif
  return failed;
}
