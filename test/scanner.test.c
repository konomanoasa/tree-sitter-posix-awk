#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "../src/scanner.c"

#ifdef TREE_SITTER_REUSE_ALLOCATOR
static size_t reuse_calloc_calls;
static size_t reuse_realloc_calls;
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

static void *reuse_realloc(void *allocation, size_t size) {
  reuse_realloc_calls += 1;
  const bool allocated = allocation != NULL;
  void *result = realloc(allocation, size);
  if (result != NULL && !allocated) {
    reuse_live_allocations += 1;
  }
  return result;
}

void *(*ts_current_calloc)(size_t, size_t) = reuse_calloc;
void *(*ts_current_realloc)(void *, size_t) = reuse_realloc;
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
    {"getline spelling excludes its target", "getline value", GETLINE_WORD, 7},
    {"function name excludes parenthesis", "follow(", FUNC_NAME_WORD, 6},
    {"single plus excludes following continuation", "+\\\nx", PLUS_OPERATOR, 1},
    {"single minus excludes following continuation",
      "-\\\nx",
      MINUS_OPERATOR,
      1},
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

  const bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
  failed |= expect_scan_result(
    "a function name cannot fall back to an ordinary name",
    "follow(",
    valid_symbols,
    true,
    FUNC_NAME_WORD,
    6
  );
  failed |= expect_scan_result(
    "getline cannot fall back to an ordinary name",
    "getline",
    valid_symbols,
    true,
    GETLINE_WORD,
    7
  );

  return failed;
}

static int test_update_operator_boundaries(void) {
  static const struct {
    const char *name;
    const char *source;
    const char *separated_source;
    enum TokenType composite;
    enum TokenType single;
  } cases[] = {
    {"increment", "++x", "+\\\n+x", INCR_OPERATOR, PLUS_OPERATOR},
    {"decrement", "--x", "-\\\n-x", DECR_OPERATOR, MINUS_OPERATOR},
  };
  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
    valid_symbols[cases[i].composite] = true;
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
      true,
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

    MockLexer restored = make_mock_lexer("\nname(");
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
    const bool name_valid[TOKEN_TYPE_COUNT] = {[FUNC_NAME_WORD] = true};
    assert(tree_sitter_posix_awk_external_scanner_scan(
      &state,
      &restored.lexer,
      name_valid
    ));
    assert(restored.lexer.result_symbol == FUNC_NAME_WORD);
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
  const bool valid_symbols[TOKEN_TYPE_COUNT] = {[FUNC_NAME_WORD] = true};
  return expect_scan_result_at(
    "blanks precede a function name",
    "  value(",
    valid_symbols,
    LEXICAL_MODE_OUTSIDE,
    true,
    FUNC_NAME_WORD,
    2,
    7,
    LEXICAL_MODE_OUTSIDE
  );
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
  } cases[] = {
    {"repeated continuations prevent function adjacency", "follow\\\n\\\n("},
    {"blank before a continuation prevents function adjacency", "follow \\\n("},
    {"blank after a continuation prevents function adjacency", "follow\\\n ("},
    {"raw newline prevents function adjacency", "follow\\\n\n("},
    {"comment prevents function adjacency", "follow\\\n# note\n("},
    {"incomplete continuation prevents function adjacency", "follow\\("},
  };
  const bool valid_symbols[TOKEN_TYPE_COUNT] = {
    [FUNC_NAME_WORD] = true,
  };

  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    failed |= expect_scan_result(
      cases[i].name,
      cases[i].source,
      valid_symbols,
      false,
      FUNC_NAME_WORD,
      0
    );
  }
  return failed;
}

static int test_getline_operand_lookahead(void) {
  static const struct {
    const char *name;
    const char *source;
    enum TokenType expected_symbol;
  } cases[] = {
    {"no operand follows at the end", "getline", GETLINE_WORD},
    {"a terminator is no operand", "getline;", GETLINE_WORD},
    {"a source is no operand", "getline < file", GETLINE_WORD},
    {"a grouping is no lvalue", "getline (x)", GETLINE_WORD},
    {"a call is no lvalue", "getline f(x)", GETLINE_WORD},
    {"a built-in name is no lvalue", "getline length", GETLINE_WORD},
    {"a prefix update is no lvalue", "getline ++x", GETLINE_WORD},
    {"a name is the target", "getline x", GETLINE_TARGET_WORD},
    {"a source follows the target", "getline x < file", GETLINE_TARGET_WORD},
    {"an operator follows the target", "getline x + 1", GETLINE_TARGET_WORD},
    {"a single sign is no update", "getline x - -y", GETLINE_TARGET_WORD},
    {"a postfix update takes the name", "getline x++", GETLINE_OMITTED_WORD},
    {"a postfix decrement takes the name",
      "getline x --",
      GETLINE_OMITTED_WORD},
    {"an update before an operator stays postfix",
      "getline x++ + 1",
      GETLINE_OMITTED_WORD},
    {"an update before a name becomes its prefix",
      "getline x++ y",
      GETLINE_TARGET_WORD},
    {"an operand behind the target is not looked into",
      "getline x a[i",
      GETLINE_TARGET_WORD},
    {"alternating updates and names end in a postfix update",
      "getline x++ y++",
      GETLINE_OMITTED_WORD},
    {"alternating updates and names end in a name",
      "getline x++ y-- z",
      GETLINE_TARGET_WORD},
    {"an update cannot prefix a call",
      "getline x++ f(y)",
      GETLINE_OMITTED_WORD},
    {"two updates cannot both prefix", "getline x++ ++y", GETLINE_OMITTED_WORD},
    {"a subscript belongs to the target",
      "getline a[i, j]",
      GETLINE_TARGET_WORD},
    {"blanks may precede the subscript",
      "getline a \t[i]--",
      GETLINE_OMITTED_WORD},
    {"nested groups and strings are skipped",
      "getline a[b[\"]\"], (c)]++",
      GETLINE_OMITTED_WORD},
    {"a slash behind an operand divides",
      "getline a[i/2]++",
      GETLINE_OMITTED_WORD},
    {"a slash behind an operator opens an ERE",
      "getline a[x ~ /]/]++",
      GETLINE_OMITTED_WORD},
    {"a slash behind an update divides",
      "getline a[i++ / 2]++",
      GETLINE_OMITTED_WORD},
    {"a continuation inside the subscript is layout",
      "getline a[i,\\\nj]++",
      GETLINE_OMITTED_WORD},
    {"a raw newline behind a comma is layout",
      "getline a[i,\nj]++",
      GETLINE_OMITTED_WORD},
    {"a comment behind a logical operator is layout",
      "getline a[i || # c\n\nj]++",
      GETLINE_OMITTED_WORD},
    {"a raw newline behind an operand breaks the subscript",
      "getline a[i\n]++",
      GETLINE_TARGET_WORD},
    {"a comment behind an operand breaks the subscript",
      "getline a[i # c\n]++",
      GETLINE_TARGET_WORD},
    {"an unterminated subscript stays the target",
      "getline a[i",
      GETLINE_TARGET_WORD},
    {"a numbered field is an lvalue", "getline $1++", GETLINE_OMITTED_WORD},
    {"a named field is an lvalue", "getline $ NF", GETLINE_TARGET_WORD},
    {"nested fields are one lvalue", "getline $$a[1]++", GETLINE_OMITTED_WORD},
    {"a grouped field is an lvalue",
      "getline $(i + 1)++ y",
      GETLINE_TARGET_WORD},
    {"a fractional field index is an lvalue",
      "getline $1.5e3f++",
      GETLINE_OMITTED_WORD},
    {"a name behind a field index is another operand",
      "getline $1e++",
      GETLINE_TARGET_WORD},
    {"a string field index is an lvalue",
      "getline $\"f\"++",
      GETLINE_OMITTED_WORD},
    {"an ERE field index is an lvalue",
      "getline $/re/++",
      GETLINE_OMITTED_WORD},
    {"a call field index is an lvalue",
      "getline $f(x)++",
      GETLINE_OMITTED_WORD},
    {"a built-in field index may omit its arguments",
      "getline $length++ y",
      GETLINE_TARGET_WORD},
    {"a built-in field index takes spaced arguments",
      "getline $length (x)++",
      GETLINE_OMITTED_WORD},
    {"a getline inside the subscript is skipped",
      "getline a[getline x++]++",
      GETLINE_OMITTED_WORD},
    {"a unary field keeps a following update inside",
      "getline $-x++",
      GETLINE_TARGET_WORD},
    {"a unary target completes its enclosing getline field",
      "getline $getline $-x",
      GETLINE_TARGET_WORD},
    {"a getline field without a target is an lvalue",
      "getline $getline++",
      GETLINE_OMITTED_WORD},
    {"a getline field takes no target that is no lvalue",
      "getline $getline length++",
      GETLINE_TARGET_WORD},
    {"a getline field takes its own target",
      "getline $getline x++",
      GETLINE_OMITTED_WORD},
    {"an update before a name follows a getline field",
      "getline $getline x++ y",
      GETLINE_TARGET_WORD},
    {"a prefix-updated getline field gives up its target",
      "getline x++ $getline y++",
      GETLINE_TARGET_WORD},
    {"a prefix-updated getline field with a target settles the target",
      "getline x++ $getline y a[i",
      GETLINE_TARGET_WORD},
    {"a prefix-updated getline field needs an lvalue behind its update",
      "getline x++ $getline++",
      GETLINE_OMITTED_WORD},
    {"a redirected getline field remains the target",
      "getline $getline < file",
      GETLINE_TARGET_WORD},
    {"a redirected field takes the update after a constant source",
      "getline $getline < \"f\" ++",
      GETLINE_OMITTED_WORD},
    {"a source lvalue takes its own postfix update",
      "getline $getline < x++",
      GETLINE_TARGET_WORD},
    {"a continued logical source skips the following raw newline",
      "getline $getline < 1 &&\\\n\n1++",
      GETLINE_OMITTED_WORD},
    {"a continued logical source skips the following comment",
      "getline $getline < 1 ||\\\n# source\n1--",
      GETLINE_OMITTED_WORD},
    {"a logical source alternates comments and continuations",
      "getline $getline < 1 && # first\n\\\n# second\n1++",
      GETLINE_OMITTED_WORD},
    {"a continued logical source preserves its own updated lvalue",
      "getline $getline < 1 &&\\\n# source\nx++",
      GETLINE_TARGET_WORD},
    {"an inner source target defers the enclosing target decision",
      "getline $getline < a < getline x++ < y",
      GETLINE_PREFER_TARGET_WORD},
    {"a parenthesized source settles its inner target decision",
      "getline $getline < (getline x++)",
      GETLINE_TARGET_WORD},
    {"an updated source leaves the second update for the field",
      "getline $getline < x++++",
      GETLINE_OMITTED_WORD},
    {"a pipe source leaves the update for the field",
      "getline $getline < x | getline ++",
      GETLINE_OMITTED_WORD},
    {"a source conditional keeps an update on its alternative",
      "getline $getline < a ? x : y++",
      GETLINE_TARGET_WORD},
    {"a grouped alternative leaves the update for the field",
      "getline $getline < a ? x : (y)++",
      GETLINE_OMITTED_WORD},
    {"a prefix-updated field can omit its own redirected target",
      "getline x++ $getline $getline < 1 ++",
      GETLINE_TARGET_WORD},
    {"an incomplete exponent leaves its name for an update",
      "getline $getline < 1e++",
      GETLINE_TARGET_WORD},
    {"membership ends before a concatenated operand outside the inner field",
      "getline $getline < $getline < 1 in a 1++",
      GETLINE_OMITTED_WORD},
    {"membership ends before a comparison outside the inner field",
      "getline $getline < $getline < x in a < \"f\" ++",
      GETLINE_OMITTED_WORD},
    {"a pipe lets a comparison become the right operand of another comparison",
      "getline $getline < x < y < z | getline ++",
      GETLINE_OMITTED_WORD},
    {"a pipe lets a match become the right operand of another match",
      "getline $getline < x ~ y ~ 1 | getline ++",
      GETLINE_OMITTED_WORD},
    {"a pipe wraps a completed match before the following match",
      "getline $getline < x ~ y | getline z ~ \"f\" ++",
      GETLINE_OMITTED_WORD},
    {"a unary field ends before its enclosing getline source",
      "getline $getline $-x < x | getline ++",
      GETLINE_OMITTED_WORD},
    {"a compared getline field is an lvalue",
      "getline $getline <= x++",
      GETLINE_TARGET_WORD},
    {"a continuation precedes the update",
      "getline x\\\n++",
      GETLINE_OMITTED_WORD},
    {"a raw newline ends the lookahead", "getline x\n++", GETLINE_TARGET_WORD},
    {"a comment ends the lookahead", "getline x # c\n++", GETLINE_TARGET_WORD},
    {"a stray backslash keeps the target",
      "getline x \\ ++",
      GETLINE_TARGET_WORD},
  };
  bool valid_symbols[TOKEN_TYPE_COUNT] = {
    [GETLINE_WORD] = true,
    [GETLINE_TARGET_WORD] = true,
    [GETLINE_OMITTED_WORD] = true,
    [GETLINE_PREFER_TARGET_WORD] = true,
    [GETLINE_PREFER_OMITTED_WORD] = true,
  };

  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    failed |= expect_scan_result(
      cases[i].name,
      cases[i].source,
      valid_symbols,
      true,
      cases[i].expected_symbol,
      7
    );
  }

  valid_symbols[GETLINE_FIELD_WORD] = true;
  failed |= expect_scan_result(
    "a prefix-updated field keeps the lookahead",
    "getline x--",
    valid_symbols,
    true,
    GETLINE_OMITTED_WORD,
    7
  );
  valid_symbols[GETLINE_WORD] = false;
  valid_symbols[GETLINE_TARGET_WORD] = false;
  valid_symbols[GETLINE_OMITTED_WORD] = false;
  failed |= expect_scan_result(
    "a getline directly under a field takes any following lvalue",
    "getline x--",
    valid_symbols,
    true,
    GETLINE_FIELD_WORD,
    7
  );
  return failed;
}

static int test_update_operator_lookahead(void) {
  static const struct {
    const char *name;
    const char *source;
    bool scanned;
  } cases[] = {
    {"a name follows", "++x", true},
    {"a field follows", "++$1", true},
    {"blanks precede the name", "++ \tx", true},
    {"a continuation precedes the name", "++\\\nx", true},
    {"the source ends", "++", false},
    {"a terminator follows", "++;", false},
    {"a raw newline follows", "++\nx", false},
    {"a number follows", "++1", false},
    {"a grouping follows", "++(x)", false},
    {"a call follows", "++f(x)", false},
    {"a built-in name follows", "++length", false},
    {"a keyword follows", "++getline", false},
    {"another update follows", "++ ++x", false},
    {"a stray backslash follows", "++\\x", false},
  };
  const bool valid_symbols[TOKEN_TYPE_COUNT] = {
    [INCR_OPERATOR] = true,
    [PLUS_OPERATOR] = true,
  };

  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    failed |= expect_scan_result(
      cases[i].name,
      cases[i].source,
      valid_symbols,
      cases[i].scanned,
      INCR_OPERATOR,
      2
    );
  }
  return failed;
}

static int test_deep_getline_source_lookahead(void) {
  const char fragment[] = "getline $getline < ";
  const size_t depth = 8192;
  const size_t length = depth * (sizeof(fragment) - 1);
  char *source = malloc(length + 6);
  assert(source != NULL);
  for (size_t i = 0; i < depth; i++) {
    memcpy(source + i * (sizeof(fragment) - 1), fragment, sizeof(fragment) - 1);
  }
  const bool valid_symbols[TOKEN_TYPE_COUNT] = {
    [GETLINE_TARGET_WORD] = true,
    [GETLINE_OMITTED_WORD] = true,
    [GETLINE_PREFER_TARGET_WORD] = true,
    [GETLINE_PREFER_OMITTED_WORD] = true,
  };
  const struct {
    const char *source;
    enum TokenType expected_symbol;
  } tails[] = {
    {"x", GETLINE_TARGET_WORD},
    {"x++++", GETLINE_PREFER_TARGET_WORD},
  };
  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(tails); i++) {
    memcpy(source + length, tails[i].source, strlen(tails[i].source) + 1);
    MockLexer mock = make_mock_lexer(source);
    ScannerState state = {0};
    const bool scanned = tree_sitter_posix_awk_external_scanner_scan(
      &state,
      &mock.lexer,
      valid_symbols
    );
    if (
      !scanned ||
      mock.lexer.result_symbol !=
      tails[i].expected_symbol ||
      mock.token_end !=
      7 ||
      mock.advance_count > strlen(source)
    ) {
      fprintf(
        stderr,
        "deep getline source %zu: invalid token or repeated source reads\n",
        i
      );
      failed = 1;
    }
    char serialized[TREE_SITTER_SERIALIZATION_BUFFER_SIZE];
    assert(
      tree_sitter_posix_awk_external_scanner_serialize(&state, serialized) == 0
    );
  }
  free(source);
  return failed;
}

static int test_linear_word_boundary_lookahead(void) {
  static const struct {
    const char *name;
    const char *prefix;
    const char *suffix;
    enum TokenType expected_symbol;
    size_t expected_token_end;
    bool expected_scanned;
  } cases[] = {
    {"name recognition stops before a long continuation gap",
      "follow",
      "(",
      FUNC_NAME_WORD,
      6,
      false},
    {"linear getline operand continuation lookahead",
      "getline x ",
      "++",
      GETLINE_OMITTED_WORD,
      7,
      true},
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
    // The getline lookahead runs where the parser accepts a target.
    valid_symbols[GETLINE_TARGET_WORD] =
      cases[i].expected_symbol == GETLINE_OMITTED_WORD;
    const bool scanned = tree_sitter_posix_awk_external_scanner_scan(
      &state,
      &mock.lexer,
      valid_symbols
    );
    const bool valid_result = scanned ==
      cases[i].expected_scanned &&
      (!scanned || mock.lexer.result_symbol == cases[i].expected_symbol) &&
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
    "raw newline leaves string mode unconsumed in error mode",
    "\n\"",
    valid_symbols,
    LEXICAL_MODE_STRING,
    true,
    LITERAL_BREAK,
    0,
    0,
    LEXICAL_MODE_OUTSIDE
  );
  return failed;
}

static int test_newline_ends_every_literal_mode(void) {
  const bool break_valid[TOKEN_TYPE_COUNT] = {[LITERAL_BREAK] = true};
  int failed = 0;
  for (
    LexicalMode mode = LEXICAL_MODE_OUTSIDE;
    mode <= LEXICAL_MODE_CONTINUED_NEWLINE;
    mode++
  ) {
    const bool literal = is_ere_mode(mode) || mode == LEXICAL_MODE_STRING;
    failed |= expect_scan_result_at(
      "a raw newline ends every literal mode without being consumed",
      "\nx",
      break_valid,
      mode,
      literal,
      LITERAL_BREAK,
      0,
      0,
      literal ? LEXICAL_MODE_OUTSIDE : mode
    );
  }
  const bool content_valid[TOKEN_TYPE_COUNT] = {
    [ERE_COMPOUND_CONTENT] = true,
  };
  failed |= expect_scan_result_at(
    "an unexpected raw newline is not string content",
    "\n",
    content_valid,
    LEXICAL_MODE_STRING,
    false,
    LITERAL_BREAK,
    0,
    0,
    LEXICAL_MODE_STRING
  );
  failed |= expect_scan_result_at(
    "an unexpected raw newline is not collating content",
    "\n",
    content_valid,
    LEXICAL_MODE_ERE_COLLATING,
    false,
    LITERAL_BREAK,
    0,
    0,
    LEXICAL_MODE_ERE_COLLATING
  );
  failed |= expect_scan_result_at(
    "EOF inside a string keeps string mode",
    "",
    break_valid,
    LEXICAL_MODE_STRING,
    false,
    LITERAL_BREAK,
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
    {"error mode retains function-name classification",
      "follow(",
      true,
      FUNC_NAME_WORD,
      6,
      LEXICAL_MODE_OUTSIDE},
    {"error mode supplies getline without target lookahead",
      "getline x++",
      true,
      GETLINE_WORD,
      7,
      LEXICAL_MODE_OUTSIDE},
    {"error mode suppresses division classification",
      "/x",
      false,
      DIVISION_SLASH,
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

  static const struct {
    const char *name;
    const char *source;
    LexicalMode mode;
    enum TokenType expected_symbol;
  } literal_cases[] = {
    {"error mode keeps a string escape",
      "\\\"x",
      LEXICAL_MODE_STRING,
      STRING_ESCAPE},
    {"error mode keeps an escaped ERE delimiter",
      "\\/x",
      LEXICAL_MODE_ERE_BODY,
      ERE_ESCAPED_DELIMITER},
    {"error mode keeps a collating symbol escape",
      "\\/x",
      LEXICAL_MODE_ERE_COLLATING,
      ERE_ESCAPED_DELIMITER},
  };

  int failed = 0;
  bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
  set_all_symbols_valid(valid_symbols);
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
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
  for (size_t i = 0; i < ARRAY_LENGTH(literal_cases); i++) {
    failed |= expect_scan_result_at(
      literal_cases[i].name,
      literal_cases[i].source,
      valid_symbols,
      literal_cases[i].mode,
      true,
      literal_cases[i].expected_symbol,
      0,
      2,
      literal_cases[i].mode
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

  MockLexer eof = make_mock_lexer("");
  assert(!tree_sitter_posix_awk_external_scanner_scan(
    &state,
    &eof.lexer,
    valid_symbols
  ));
  assert(state.mode == LEXICAL_MODE_OUTSIDE);
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
  assert(reuse_realloc_calls > 0);
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
  failed |= test_update_operator_boundaries();
  failed |= test_backslash_tokens_by_mode();
  failed |= test_escape_classification_ignores_expected_tokens();
  failed |= test_continued_newline_requires_its_marker();
  test_repeated_continuation_ranges_and_restoration();
  test_pending_continuation_rejects_changed_boundaries();
  failed |= test_literal_classification();
  failed |= test_blank_skip_token_ranges();
  failed |= test_slash_dispatch();
  failed |= test_word_boundary_lookahead();
  failed |= test_getline_operand_lookahead();
  failed |= test_update_operator_lookahead();
  failed |= test_deep_getline_source_lookahead();
  failed |= test_linear_word_boundary_lookahead();
  failed |= test_long_token_spans();
  failed |= test_ere_state_transitions();
  failed |= test_compound_terminators_do_not_become_content();
  failed |= test_string_and_comment_modes();
  failed |= test_newline_ends_every_literal_mode();
  failed |= test_error_mode_real_tokens();
#ifdef TREE_SITTER_REUSE_ALLOCATOR
  test_reuse_allocator_contract();
#endif
  return failed;
}
