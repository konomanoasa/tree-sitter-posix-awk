#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/scanner.c"

enum {
  SERIALIZATION_GUARD_BYTE = 0x5a,
  SERIALIZED_HEADER_BYTES = 63,
  SERIALIZED_OUTLINE_BYTES = 28,
  SERIALIZED_CACHE_COUNT_OFFSET = 45,
  OUTLINE_KIND_SHIFT = 40,
  BOUNDARY_DIGIT_BITS = 2,
  BOUNDARY_RADIX = 1U << BOUNDARY_DIGIT_BITS,
  BOUNDARY_DIGIT_COUNT = sizeof(uint64_t) * CHAR_BIT / BOUNDARY_DIGIT_BITS,
};

#ifdef TREE_SITTER_REUSE_ALLOCATOR
static size_t reuse_calloc_calls;
static size_t reuse_realloc_calls;
static size_t reuse_free_calls;
static size_t reuse_live_allocations;
static bool reuse_fail_next_calloc;
static bool reuse_fail_next_realloc;

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
  if (reuse_fail_next_realloc) {
    reuse_fail_next_realloc = false;
    return NULL;
  }
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

static MockLexer make_mock_lexer_bytes(const char *source, size_t length) {
  return (MockLexer){
    .lexer =
      {
        .lookahead = length ? (unsigned char)source[0] : 0,
        .result_symbol = UINT16_MAX,
        .advance = mock_advance,
        .mark_end = mock_mark_end,
        .eof = mock_eof,
      },
    .source = source,
    .length = length,
  };
}

static MockLexer make_mock_lexer(const char *source) {
  return make_mock_lexer_bytes(source, strlen(source));
}

static void resume_mock_lexer(MockLexer *mock) {
  mock->offset = mock->token_end;
  mock->token_start = mock->token_end;
  mock->content_started = false;
  mock->lexer.result_symbol = UINT16_MAX;
  mock->lexer.lookahead =
    mock->offset < mock->length ? (unsigned char)mock->source[mock->offset] : 0;
}

static unsigned serialize_state(ScannerState *state, char *buffer) {
  char guarded[TREE_SITTER_SERIALIZATION_BUFFER_SIZE + 2];
  memset(guarded, SERIALIZATION_GUARD_BYTE, sizeof(guarded));
  const unsigned length =
    tree_sitter_posix_awk_external_scanner_serialize(state, guarded + 1);
  assert(length <= TREE_SITTER_SERIALIZATION_BUFFER_SIZE);
  assert(guarded[0] == SERIALIZATION_GUARD_BYTE);
  for (size_t index = length + 1; index < sizeof(guarded); index++) {
    assert(guarded[index] == SERIALIZATION_GUARD_BYTE);
  }
  memcpy(buffer, guarded + 1, length);
  return length;
}

static void
assert_state_equal(const ScannerState *actual, const ScannerState *expected) {
  assert(actual->mode == expected->mode);
  const BoundaryState *left = &actual->boundary;
  const BoundaryState *right = &expected->boundary;
  assert(left->active == right->active);
  assert(left->pending[0] == right->pending[0]);
  assert(left->pending[1] == right->pending[1]);
  assert(left->next == right->next);
  assert(left->carry == right->carry);
  assert(left->phase == right->phase);
  assert(left->selected == right->selected);
  assert(left->unary == right->unary);
  assert(left->print == right->print);
  assert(left->decision_hash == right->decision_hash);
  assert(left->cache_count == right->cache_count);
  assert(left->cache_index == right->cache_index);
  assert(left->cache_repeats == right->cache_repeats);
  assert(left->cache_delta == right->cache_delta);
  for (unsigned index = 0; index < right->cache_count; index++) {
    assert(left->cache[index].descriptor == right->cache[index].descriptor);
    assert(left->cache[index].children[0] == right->cache[index].children[0]);
    assert(left->cache[index].children[1] == right->cache[index].children[1]);
    assert(left->cache[index].repeat == right->cache[index].repeat);
  }
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
      mock.lexer.result_symbol == expected_symbol &&
      mock.token_start == expected_token_start &&
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
    {"field operator excludes following continuation",
      "$\\\nx",
      FIELD_OPERATOR,
      1},
    {"negation excludes following continuation", "!\\\nx", NOT_OPERATOR, 1},
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

static int test_prefix_operator_boundaries(void) {
  static const struct {
    const char *name;
    const char *source;
    const char *separated_source;
    enum TokenType token;
  } cases[] = {
    {"inequality", "!=x", "!\\\n=x", NOT_OPERATOR},
    {"negated match", "!~x", "!\\\n~x", NOT_OPERATOR},
  };
  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
    valid_symbols[cases[i].token] = true;
    failed |= expect_scan_result(
      cases[i].name,
      cases[i].source,
      valid_symbols,
      false,
      cases[i].token,
      0
    );
    failed |= expect_scan_result(
      cases[i].name,
      cases[i].separated_source,
      valid_symbols,
      true,
      cases[i].token,
      1
    );
    set_all_symbols_valid(valid_symbols);
    failed |= expect_scan_result(
      cases[i].name,
      cases[i].source,
      valid_symbols,
      false,
      cases[i].token,
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
    const unsigned length = serialize_state(&state, buffer);
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

static int test_getline_pipe_lookahead(void) {
  static const struct {
    const char *name;
    const char *source;
    bool expected_scanned;
  } cases[] = {
    {"a pipe before getline continues a getline source", "| getline", true},
    {"a pipe adjacent to getline continues a getline source", "|getline", true},
    {"a pipe before a continued getline excludes the layout",
      "| \\\ngetline",
      true},
    {"a pipe before a command leaves an output redirection",
      "| \"cmd\"",
      false},
    {"a pipe before a longer word leaves an output redirection",
      "| getlinex",
      false},
    {"a getline inside a comment does not follow a pipe", "| # getline", false},
    {"logical or is not a getline pipe", "|| getline", false},
  };
  int failed = 0;
  bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
  valid_symbols[GETLINE_PIPE] = true;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    failed |= expect_scan_result(
      cases[i].name,
      cases[i].source,
      valid_symbols,
      cases[i].expected_scanned,
      GETLINE_PIPE,
      1
    );
  }
  static const struct {
    const char *name;
    bool recovering;
  } read_cases[] = {
    {"an output redirection reads the getline after its pipe", false},
    {"error mode reads the getline after a pipe", true},
  };
  for (size_t i = 0; i < ARRAY_LENGTH(read_cases); i++) {
    bool symbols[TOKEN_TYPE_COUNT] = {false};
    if (read_cases[i].recovering) {
      set_all_symbols_valid(symbols);
    }
    MockLexer mock = make_mock_lexer("| getline x");
    ScannerState state = {.mode = LEXICAL_MODE_OUTSIDE};
    const bool scanned =
      tree_sitter_posix_awk_external_scanner_scan(&state, &mock.lexer, symbols);
    if (scanned || mock.offset != strlen("| getline")) {
      fprintf(
        stderr,
        "%s: scanned=%u offset=%zu\n",
        read_cases[i].name,
        scanned,
        mock.offset
      );
      failed = 1;
    }
  }
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

static uint64_t *
recognition_outlines(const RecognizerInput *input, size_t *length) {
  size_t *stack = calloc(input->tree_count + 1, sizeof(*stack));
  const size_t outline_words = 3;
  uint64_t *outlines =
    calloc(input->tree_count * outline_words + 1, sizeof(*outlines));
  assert(stack != NULL && outlines != NULL);
  size_t depth = 0;
  *length = 0;
  stack[depth++] = input->tree_root;
  while (depth) {
    BoundaryTree node = input->tree[stack[--depth] - 1];
    outlines[(*length)++] = node.outline.descriptor;
    outlines[(*length)++] = node.outline.children[0];
    outlines[(*length)++] = node.outline.children[1];
    if (node.children[1]) stack[depth++] = node.children[1];
    if (node.children[0]) stack[depth++] = node.children[0];
  }
  free(stack);
  return outlines;
}

static enum TokenType
recognize_getline(const char *source, size_t *advances, bool compare_shared) {
  MockLexer mock = make_mock_lexer(source);
  InputProbe probe = {
    .lexer =
      {.lookahead = mock.lexer.lookahead,
        .advance = probe_advance,
        .mark_end = probe_mark_end,
        .eof = probe_eof},
    .source = &mock.lexer
  };
  RecognizerInput input = {0};
  bool operand = true;
  int questions = 0;
  for (;;) {
    int token = probe_token(&probe, operand);
    if (
      !token ||
      character_in(token, "\n;,) ]}{#") ||
      (token == ':' && !questions)
    )
      break;
    if (token == '?') questions++;
    if (token == ':' && questions) questions--;
    recognizer_add_token(&input, token);
    if (token != PROBE_UPDATE)
      operand = token != PROBE_NAME &&
        token != PROBE_VALUE &&
        token != PROBE_TUPLE &&
        token != PROBE_GETLINE;
  }
  enum TokenType result = GETLINE_WORD;
  const bool recognized = input.length && recognizer_parse(&input);
  if (compare_shared && input.length) {
    size_t length = 0;
    uint64_t *expected =
      recognized ? recognition_outlines(&input, &length) : NULL;
    assert(shared_parse(&input) == recognized);
    if (recognized) {
      size_t actual_length = 0;
      uint64_t *actual = recognition_outlines(&input, &actual_length);
      assert(length == actual_length);
      assert(memcmp(expected, actual, length * sizeof(*expected)) == 0);
      free(actual);
    }
    free(expected);
  }
  if (recognized) {
    size_t index = input.tree_root;
    while (index) {
      BoundaryTree node = input.tree[index - 1];
      if ((node.outline.descriptor >> OUTLINE_KIND_SHIFT) == RECOGNIZED_GET) {
        result = node.children[0] ? SELECT_TARGET_GET : SELECT_GET;
        break;
      }
      index = node.children[0];
    }
  }
  *advances = mock.advance_count;
  ts_free(probe.characters);
  recognizer_destroy(&input);
  return result;
}

static int test_getline_operand_lookahead(void) {
  static const struct {
    const char *name;
    const char *source;
    enum TokenType expected_symbol;
  } cases[] = {
    {"no operand follows at the end", "getline", SELECT_GET},
    {"a terminator is no operand", "getline;", SELECT_GET},
    {"a source is no operand", "getline < file", SELECT_GET},
    {"a grouping is no lvalue", "getline (x)", SELECT_GET},
    {"a call is no lvalue", "getline f(x)", SELECT_GET},
    {"a built-in name is no lvalue", "getline length", SELECT_GET},
    {"a prefix update is no lvalue", "getline ++x", SELECT_GET},
    {"a name is the target", "getline x", SELECT_TARGET_GET},
    {"a source follows the target", "getline x < file", SELECT_TARGET_GET},
    {"an operator follows the target", "getline x + 1", SELECT_TARGET_GET},
    {"a single sign is no update", "getline x - -y", SELECT_TARGET_GET},
    {"a postfix update takes the name", "getline x++", SELECT_GET},
    {"a postfix decrement takes the name", "getline x --", SELECT_GET},
    {"an update before an operator stays postfix",
      "getline x++ + 1",
      SELECT_GET},
    {"an update before a name becomes its prefix",
      "getline x++ y",
      SELECT_TARGET_GET},
    {"an operand behind the target is not looked into",
      "getline x a[i",
      SELECT_TARGET_GET},
    {"alternating updates and names end in a postfix update",
      "getline x++ y++",
      SELECT_GET},
    {"alternating updates and names end in a name",
      "getline x++ y-- z",
      SELECT_TARGET_GET},
    {"an update cannot prefix a call", "getline x++ f(y)", SELECT_GET},
    {"two updates cannot both prefix", "getline x++ ++y", SELECT_GET},
    {"a subscript belongs to the target", "getline a[i, j]", SELECT_TARGET_GET},
    {"blanks may precede the subscript", "getline a \t[i]--", SELECT_GET},
    {"nested groups and strings are skipped",
      "getline a[b[\"]\"], (c)]++",
      SELECT_GET},
    {"a slash behind an operand divides", "getline a[i/2]++", SELECT_GET},
    {"a slash behind an operator opens an ERE",
      "getline a[x ~ /]/]++",
      SELECT_GET},
    {"a slash behind an update divides", "getline a[i++ / 2]++", SELECT_GET},
    {"a continuation inside the subscript is layout",
      "getline a[i,\\\nj]++",
      SELECT_GET},
    {"a raw newline behind a comma is layout",
      "getline a[i,\nj]++",
      SELECT_GET},
    {"a comment behind a logical operator is layout",
      "getline a[i || # c\n\nj]++",
      SELECT_GET},
    {"a raw newline behind an operand breaks the subscript",
      "getline a[i\n]++",
      SELECT_GET},
    {"a comment behind an operand breaks the subscript",
      "getline a[i # c\n]++",
      SELECT_GET},
    {"an unterminated subscript preserves the keyword range",
      "getline a[i",
      SELECT_GET},
    {"a numbered field is an lvalue", "getline $1++", SELECT_GET},
    {"a named field is an lvalue", "getline $ NF", SELECT_TARGET_GET},
    {"nested fields are one lvalue", "getline $$a[1]++", SELECT_GET},
    {"a grouped field is an lvalue", "getline $(i + 1)++ y", SELECT_TARGET_GET},
    {"a fractional field index is an lvalue", "getline $1.5e3f++", SELECT_GET},
    {"a name behind a field index is another operand",
      "getline $1e++",
      SELECT_TARGET_GET},
    {"a string field index is an lvalue", "getline $\"f\"++", SELECT_GET},
    {"an ERE field index is an lvalue", "getline $/re/++", SELECT_GET},
    {"a call field index is an lvalue", "getline $f(x)++", SELECT_GET},
    {"a built-in field index may omit its arguments",
      "getline $length++ y",
      SELECT_TARGET_GET},
    {"a built-in field index takes spaced arguments",
      "getline $length (x)++",
      SELECT_GET},
    {"a getline inside the subscript is skipped",
      "getline a[getline x++]++",
      SELECT_GET},
    {"a unary field keeps a following update inside",
      "getline $-x++",
      SELECT_TARGET_GET},
    {"a unary target completes its enclosing getline field",
      "getline $getline $-x",
      SELECT_TARGET_GET},
    {"a getline field without a target is an lvalue",
      "getline $getline++",
      SELECT_GET},
    {"a getline field takes no target that is no lvalue",
      "getline $getline length++",
      GETLINE_WORD},
    {"the earlier getline target precedes a nested target",
      "getline $getline x++",
      SELECT_TARGET_GET},
    {"an update before a name follows a getline field",
      "getline $getline x++ y",
      SELECT_TARGET_GET},
    {"a prefix-updated getline field gives up its target",
      "getline x++ $getline y++",
      SELECT_TARGET_GET},
    {"a prefix-updated getline field with a target settles the target",
      "getline x++ $getline y a[i",
      SELECT_TARGET_GET},
    {"a prefix-updated getline field needs an lvalue behind its update",
      "getline x++ $getline++",
      SELECT_GET},
    {"a redirected getline field remains the target",
      "getline $getline < file",
      SELECT_TARGET_GET},
    {"a redirected field takes the update after a constant source",
      "getline $getline < \"f\" ++",
      SELECT_GET},
    {"a source lvalue takes its own postfix update",
      "getline $getline < x++",
      SELECT_TARGET_GET},
    {"a continued logical source skips the following raw newline",
      "getline $getline < (1 &&\\\n\n1)++",
      SELECT_GET},
    {"a continued logical source skips the following comment",
      "getline $getline < (1 ||\\\n# source\n1)--",
      SELECT_GET},
    {"a logical source alternates comments and continuations",
      "getline $getline < (1 && # first\n\\\n# second\n1)++",
      SELECT_GET},
    {"a continued logical source preserves its own updated lvalue",
      "getline $getline < 1 &&\\\n# source\nx++",
      SELECT_TARGET_GET},
    {"an inner source target defers the enclosing target decision",
      "getline $getline < a < getline x++ < y",
      SELECT_TARGET_GET},
    {"a parenthesized source settles its inner target decision",
      "getline $getline < (getline x++)",
      SELECT_TARGET_GET},
    {"an updated source leaves the second update for the field",
      "getline $getline < x++++",
      SELECT_GET},
    {"a pipe source leaves the update for the field",
      "getline $getline < (x | getline)++",
      SELECT_GET},
    {"a source conditional keeps an update on its alternative",
      "getline $getline < a ? x : y++",
      SELECT_TARGET_GET},
    {"a grouped alternative leaves the update for the field",
      "getline $getline < (a ? x : (y))++",
      SELECT_GET},
    {"a prefix-updated field can omit its own redirected target",
      "getline x++ $getline $getline < 1 ++",
      SELECT_TARGET_GET},
    {"an incomplete exponent leaves its name for an update",
      "getline $getline < 1e++",
      SELECT_TARGET_GET},
    {"membership ends before a concatenated operand outside the inner field",
      "getline $getline < (($getline < 1 in a) 1)++",
      SELECT_GET},
    {"membership ends before a comparison outside the inner field",
      "getline $getline < (($getline < x in a) < \"f\")++",
      SELECT_GET},
    {"a pipe lets a comparison become the right operand of another comparison",
      "getline $getline < (x < (y < z | getline)) ++",
      SELECT_GET},
    {"a pipe lets a match become the right operand of another match",
      "getline $getline < (x ~ (y ~ 1 | getline)) ++",
      SELECT_GET},
    {"a pipe wraps a completed match before the following match",
      "getline $getline < (x ~ y | getline z ~ \"f\")++",
      SELECT_GET},
    {"a unary field ends before its enclosing getline source",
      "getline $getline $-x < (x | getline)++",
      SELECT_GET},
    {"a unary getline source defers its enclosing target decision",
      "getline $getline $-getline < 1 ++",
      SELECT_GET},
    {"a logical-not getline source defers its enclosing target decision",
      "getline $getline $!getline < x -- ++",
      SELECT_GET},
    {"a prefix-update getline source defers its enclosing target decision",
      "getline $getline $--$getline < 1 ++",
      SELECT_GET},
    {"a compared getline field is an lvalue",
      "getline $getline <= x++",
      SELECT_TARGET_GET},
    {"a continuation precedes the update", "getline x\\\n++", SELECT_GET},
    {"a raw newline ends the lookahead", "getline x\n++", SELECT_TARGET_GET},
    {"a comment ends the lookahead", "getline x # c\n++", SELECT_TARGET_GET},
    {"a stray backslash preserves the keyword range",
      "getline x \\ ++",
      SELECT_GET},
  };
  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    size_t advances = 0;
    enum TokenType actual = recognize_getline(cases[i].source, &advances, true);
    if (
      actual != cases[i].expected_symbol || advances > strlen(cases[i].source)
    ) {
      fprintf(
        stderr,
        "%s: selected=%u expected=%u advances=%zu\n",
        cases[i].name,
        actual,
        cases[i].expected_symbol,
        advances
      );
      failed = 1;
    }
  }
  return failed;
}

static int test_update_operator_lookahead(void) {
  const bool valid[TOKEN_TYPE_COUNT] = {[INCR_OPERATOR] = true};
  int failed = 0;
  failed |= expect_scan_result(
    "a selected postfix update needs no following operand",
    "++",
    valid,
    true,
    INCR_OPERATOR,
    2
  );
  failed |= expect_scan_result(
    "a selected prefix update excludes its operand",
    "++x",
    valid,
    true,
    INCR_OPERATOR,
    2
  );
  return failed;
}

#ifndef GETLINE_SOURCE_LOOKAHEAD_DEPTH
#define GETLINE_SOURCE_LOOKAHEAD_DEPTH 64
#endif

static int test_deep_getline_source_lookahead(void) {
  const char fragment[] = "getline $getline < ";
  const size_t depth = GETLINE_SOURCE_LOOKAHEAD_DEPTH;
  const size_t length = depth * (sizeof(fragment) - 1);
  const struct {
    const char *source;
    enum TokenType expected_symbol;
  } tails[] = {
    {"x", SELECT_TARGET_GET},
    {"x++++", SELECT_TARGET_GET},
  };
  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(tails); i++) {
    char *source = malloc(length + strlen(tails[i].source) + 1);
    assert(source != NULL);
    for (size_t index = 0; index < depth; index++) {
      memcpy(
        source + index * (sizeof(fragment) - 1),
        fragment,
        sizeof(fragment) - 1
      );
    }
    memcpy(source + length, tails[i].source, strlen(tails[i].source) + 1);
    size_t advances = 0;
    const enum TokenType actual = recognize_getline(source, &advances, false);
    if (actual != tails[i].expected_symbol || advances > strlen(source)) {
      fprintf(
        stderr,
        "deep getline source %zu: invalid selection or repeated source reads\n",
        i
      );
      failed = 1;
    }
    free(source);
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
      GETLINE_WORD,
      7,
      true},
  };
  const char continuation[] = "\\\n";
  const size_t continuation_bytes = sizeof(continuation) - 1;
  const size_t continuation_count = 32768;
  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    const size_t prefix_length = strlen(cases[i].prefix);
    const size_t suffix_length = strlen(cases[i].suffix);
    const size_t source_length =
      prefix_length + continuation_count * continuation_bytes + suffix_length;
    char *source = malloc(source_length + 1U);
    if (source == NULL) {
      fprintf(stderr, "%s: allocation failed\n", cases[i].name);
      return 1;
    }
    memcpy(source, cases[i].prefix, prefix_length);
    for (size_t index = 0; index < continuation_count; index++) {
      memcpy(
        source + prefix_length + index * continuation_bytes,
        continuation,
        continuation_bytes
      );
    }
    memcpy(
      source + prefix_length + continuation_count * continuation_bytes,
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
    const bool valid_result = scanned == cases[i].expected_scanned &&
      (!scanned || mock.lexer.result_symbol == cases[i].expected_symbol) &&
      mock.token_start == 0 &&
      mock.token_end == cases[i].expected_token_end &&
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

static void test_long_function_name(void) {
  const size_t name_bytes = 65536;
  const char suffix[] = "(";
  char *source = malloc(name_bytes + sizeof(suffix));
  assert(source != NULL);
  memset(source, 'a', name_bytes);
  memcpy(source + name_bytes, suffix, sizeof(suffix));
  MockLexer mock = make_mock_lexer(source);
  ScannerState state = {.mode = LEXICAL_MODE_OUTSIDE};
  const bool valid_symbols[TOKEN_TYPE_COUNT] = {[FUNC_NAME_WORD] = true};
  assert(tree_sitter_posix_awk_external_scanner_scan(
    &state,
    &mock.lexer,
    valid_symbols
  ));
  assert(mock.lexer.result_symbol == FUNC_NAME_WORD);
  assert(mock.token_start == 0);
  assert(mock.token_end == name_bytes);
  assert(mock.advance_count <= name_bytes + sizeof(suffix) - 1);
  assert(state.mode == LEXICAL_MODE_OUTSIDE);
  free(source);
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
    failed |= expect_scan_result_at(
      "blanks before a raw newline end every literal mode before the blanks",
      " \t\nx",
      break_valid,
      mode,
      literal,
      LITERAL_BREAK,
      0,
      0,
      literal ? LEXICAL_MODE_OUTSIDE : mode
    );
    failed |= expect_scan_result_at(
      "blanks before other source keep every mode",
      " \tx\n",
      break_valid,
      mode,
      false,
      LITERAL_BREAK,
      0,
      0,
      mode
    );
  }
  bool recovery_valid[TOKEN_TYPE_COUNT] = {false};
  set_all_symbols_valid(recovery_valid);
  failed |= expect_scan_result_at(
    "error recovery ends a blank collating payload before a raw newline",
    " \n",
    recovery_valid,
    LEXICAL_MODE_ERE_COLLATING,
    true,
    LITERAL_BREAK,
    0,
    0,
    LEXICAL_MODE_OUTSIDE
  );
  const bool payload_valid[TOKEN_TYPE_COUNT] = {
    [ERE_COMPOUND_SINGLE] = true,
    [ERE_COMPOUND_MULTI] = true,
    [LITERAL_BREAK] = true,
  };
  failed |= expect_scan_result_at(
    "an expected equivalence payload ends before blanks and a raw newline",
    " \t\n",
    payload_valid,
    LEXICAL_MODE_ERE_EQUIVALENCE,
    true,
    LITERAL_BREAK,
    0,
    0,
    LEXICAL_MODE_OUTSIDE
  );
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
    {"error mode prevents getline from restarting an input expression",
      "getline x++",
      true,
      ERROR_SENTINEL,
      7,
      LEXICAL_MODE_OUTSIDE},
    {"error mode prevents field expressions from nesting",
      "$ ,",
      true,
      ERROR_SENTINEL,
      1,
      LEXICAL_MODE_OUTSIDE},
    {"error mode prevents negations from nesting",
      "! ,",
      true,
      ERROR_SENTINEL,
      1,
      LEXICAL_MODE_OUTSIDE},
    {"error mode prevents names from restarting nested assignments",
      "x = ,",
      true,
      ERROR_SENTINEL,
      1,
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
  char buffer[TREE_SITTER_SERIALIZATION_BUFFER_SIZE];
  const unsigned length = serialize_state(&source, buffer);
  tree_sitter_posix_awk_external_scanner_deserialize(
    &destination,
    buffer,
    length
  );
  assert_state_equal(&destination, &source);
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

  char buffer[TREE_SITTER_SERIALIZATION_BUFFER_SIZE];
  bool valid_symbols[TOKEN_TYPE_COUNT];
  set_all_symbols_valid(valid_symbols);
  MockLexer mock = make_mock_lexer("x");
  tree_sitter_posix_awk_external_scanner_deserialize(NULL, buffer, 0);
  assert(tree_sitter_posix_awk_external_scanner_serialize(NULL, buffer) == 0);
  assert(!tree_sitter_posix_awk_external_scanner_scan(
    NULL,
    &mock.lexer,
    valid_symbols
  ));
  assert(mock.advance_count == 0);
  tree_sitter_posix_awk_external_scanner_destroy(NULL);
}

static void test_failed_probe_reads_as_end_of_input(void) {
  MockLexer mock = make_mock_lexer("x in a)");
  InputProbe probe = {
    .lexer =
      {.lookahead = mock.lexer.lookahead,
        .advance = probe_advance,
        .mark_end = probe_mark_end,
        .eof = probe_eof},
    .source = &mock.lexer
  };
  probe_advance(&probe.lexer, false);
  probe.failed = true;
  probe_restore(&probe, 0);
  assert(probe.lexer.lookahead == 0 && probe_eof(&probe.lexer));
  assert(scan_word_spelling(&probe.lexer) == WORD_NONE);
  ts_free(probe.characters);
}

static void test_nul_and_eof_are_distinct(void) {
  const char source[] = "#a\0b\n";
  MockLexer comment = make_mock_lexer_bytes(source, sizeof(source) - 1);
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

static void
assert_disabled_scan(const char *source, size_t length, LexicalMode mode) {
  const bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
  const ScannerState initial = {.mode = mode};
  ScannerState state = initial;
  MockLexer mock = make_mock_lexer_bytes(source, length);
  assert(!tree_sitter_posix_awk_external_scanner_scan(
    &state,
    &mock.lexer,
    valid_symbols
  ));
  assert_state_equal(&state, &initial);
}

static void test_disabled_tokens_preserve_state(void) {
  const char *sources[] = {"BEGIN", "/", "\"", "#comment", "\n", ""};
  const char nul[] = "\0";
  for (
    LexicalMode mode = LEXICAL_MODE_OUTSIDE;
    mode <= LEXICAL_MODE_CONTINUED_NEWLINE;
    mode++
  ) {
    for (size_t index = 0; index < ARRAY_LENGTH(sources); index++) {
      assert_disabled_scan(sources[index], strlen(sources[index]), mode);
    }
    assert_disabled_scan(nul, sizeof(nul) - 1, mode);
  }
}

static int test_serialization(void) {
  static const struct {
    const char *name;
    LexicalMode mode;
  } cases[] = {
    {"outside mode round trip", LEXICAL_MODE_OUTSIDE},
    {"ERE body mode round trip", LEXICAL_MODE_ERE_BODY},
    {"collating symbol mode round trip", LEXICAL_MODE_ERE_COLLATING},
    {"equivalence class mode round trip", LEXICAL_MODE_ERE_EQUIVALENCE},
    {"character class mode round trip", LEXICAL_MODE_ERE_CLASS},
    {"string mode round trip", LEXICAL_MODE_STRING},
    {"continued newline mode round trip", LEXICAL_MODE_CONTINUED_NEWLINE},
  };
  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    failed |=
      check_round_trip(cases[i].name, cases[i].mode, SERIALIZED_HEADER_BYTES);
  }
  ScannerState source = {.mode = LEXICAL_MODE_ERE_BODY};
  char buffer[TREE_SITTER_SERIALIZATION_BUFFER_SIZE];
  const unsigned length = serialize_state(&source, buffer);
  const char invalid_modes[] = {
    (char)(LEXICAL_MODE_CONTINUED_NEWLINE + 1),
    (char)UINT8_MAX
  };
  for (size_t i = 0; i < ARRAY_LENGTH(invalid_modes); i++) {
    ScannerState destination = {.mode = LEXICAL_MODE_STRING};
    buffer[0] = invalid_modes[i];
    tree_sitter_posix_awk_external_scanner_deserialize(
      &destination,
      buffer,
      length
    );
    failed |= expect_mode(
      "invalid encoded mode resets state",
      LEXICAL_MODE_OUTSIDE,
      destination.mode
    );
  }
  buffer[0] = LEXICAL_MODE_ERE_BODY;
  const unsigned invalid_lengths[] = {0, 1, 2, SERIALIZED_HEADER_BYTES - 1};
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

static void test_expression_boundary_serialization(void) {
  ScannerState original = {
    .mode = LEXICAL_MODE_STRING,
    .boundary = {
      .active = UINT64_C(0x123456789),
      .pending = {UINT64_C(0x223456789), UINT64_C(0x323456789)},
      .next = UINT64_C(0x423456789),
      .carry = UINT64_C(0x523456789),
      .phase = BOUNDARY_SAVING,
      .selected = SELECT_TARGET_GET,
      .unary = true,
      .print = true,
      .decision_hash = UINT64_C(0xe123456789abcdef),
      .cache_count = 1,
      .cache_repeats = 3,
      .cache_delta = 4,
      .cache = {
        {.descriptor = UINT64_C(0x67200000040),
          .children = {UINT64_C(0x6720000003c), UINT64_C(0xc200000003)},
          .repeat = 1}
      }
    }
  };
  char buffer[TREE_SITTER_SERIALIZATION_BUFFER_SIZE];
  unsigned length = serialize_state(&original, buffer);
  assert(length == SERIALIZED_HEADER_BYTES + SERIALIZED_OUTLINE_BYTES);
  ScannerState restored = {0};
  tree_sitter_posix_awk_external_scanner_deserialize(&restored, buffer, length);
  assert_state_equal(&restored, &original);
  char again[TREE_SITTER_SERIALIZATION_BUFFER_SIZE];
  assert(serialize_state(&restored, again) == length);
  assert(memcmp(buffer, again, length) == 0);
  original.boundary.cache_count = BOUNDARY_CACHE_CAPACITY;
  for (unsigned i = 1; i < BOUNDARY_CACHE_CAPACITY; i++)
    original.boundary.cache[i] = original.boundary.cache[0];
  length = serialize_state(&original, buffer);
  assert(
    length ==
    SERIALIZED_HEADER_BYTES + BOUNDARY_CACHE_CAPACITY * SERIALIZED_OUTLINE_BYTES
  );
  tree_sitter_posix_awk_external_scanner_deserialize(&restored, buffer, length);
  assert_state_equal(&restored, &original);
  assert(serialize_state(&restored, again) == length);
  assert(memcmp(buffer, again, length) == 0);
  buffer[SERIALIZED_CACHE_COUNT_OFFSET] = BOUNDARY_CACHE_CAPACITY + 1;
  tree_sitter_posix_awk_external_scanner_deserialize(&restored, buffer, length);
  assert(
    restored.mode == LEXICAL_MODE_OUTSIDE &&
    !restored.boundary.active &&
    !restored.boundary.decision_hash &&
    !restored.boundary.cache_count
  );
}

static void test_expression_decisions_survive_child_boundaries(void) {
  ScannerState states[] = {
    {.boundary = {.selected = SELECT_GET, .pending = {1, 0}}},
    {.boundary = {.selected = SELECT_TARGET_GET, .pending = {1, 0}}},
  };
  const bool stages[][TOKEN_TYPE_COUNT] = {
    {[SELECT_GET] = true, [SELECT_TARGET_GET] = true},
    {[BEGIN_CHILD] = true},
    {[END_CHILD] = true},
    {[RESUME] = true},
  };
  for (size_t step = 0; step < ARRAY_LENGTH(stages); step++) {
    for (size_t i = 0; i < ARRAY_LENGTH(states); i++) {
      MockLexer mock = make_mock_lexer("x");
      assert(tree_sitter_posix_awk_external_scanner_scan(
        &states[i],
        &mock.lexer,
        stages[step]
      ));
    }
    assert(
      states[0].boundary.decision_hash != states[1].boundary.decision_hash
    );
  }
  const bool roots[][TOKEN_TYPE_COUNT] = {
    {[ROOT] = true},
    {[ROOT] = true, [LOCAL_ROOT] = true},
  };
  for (size_t i = 0; i < ARRAY_LENGTH(states); i++) {
    const uint64_t decisions = states[i].boundary.decision_hash;
    const bool disabled[TOKEN_TYPE_COUNT] = {false};
    MockLexer mock = make_mock_lexer("x");
    assert(!tree_sitter_posix_awk_external_scanner_scan(
      &states[i],
      &mock.lexer,
      disabled
    ));
    assert(states[i].boundary.decision_hash == decisions);
    mock = make_mock_lexer("x");
    assert(tree_sitter_posix_awk_external_scanner_scan(
      &states[i],
      &mock.lexer,
      roots[i]
    ));
    assert(!states[i].boundary.decision_hash);
  }
}

static int test_local_expression_selection(void) {
  static const struct {
    const char *name, *source;
    bool print;
    enum TokenType expected;
  } cases[] = {
    {"ordinary updates stay in grammar", "$x++ + --y", false, LOCAL_ROOT},
    {"nested target without a source stays in grammar",
      "$(getline $(x))",
      false,
      LOCAL_ROOT},
    {"source ownership needs selection", "getline $getline < x", false, ROOT},
    {"grouped target updates stay in grammar",
      "getline $(x++)",
      false,
      LOCAL_ROOT},
    {"a grouped getline does not own an outer comparison",
      "(getline x) < y",
      false,
      LOCAL_ROOT},
    {"a preceding comparison does not enter a grouped getline",
      "x < (getline y)",
      false,
      LOCAL_ROOT},
    {"an outer update does not enter a grouped getline",
      "(getline x) + y++",
      false,
      LOCAL_ROOT},
    {"a preceding update does not enter a grouped getline",
      "y++ + (getline x)",
      false,
      LOCAL_ROOT},
    {"arguments select ownership independently",
      "f(getline x, ++y)",
      false,
      LOCAL_ROOT},
    {"a later argument does not inherit updates",
      "f(++y, getline x)",
      false,
      LOCAL_ROOT},
    {"subscripts select ownership independently",
      "a[getline x, i++]",
      false,
      LOCAL_ROOT},
    {"an update within a target subscript stays in grammar",
      "getline a[i++]",
      false,
      LOCAL_ROOT},
    {"a comparison token is not a file source operator",
      "getline x <= 0",
      false,
      LOCAL_ROOT},
    {"comparison tokens do not hide ambiguous updates",
      "getline x <= y++",
      false,
      ROOT},
    {"an ambiguous argument still needs selection",
      "f(1, getline x++)",
      false,
      ROOT},
    {"a grouped source still needs selection",
      "(getline x < file) + y++",
      false,
      ROOT},
    {"closing a subscript restores outer ownership candidates",
      "getline a[i++, j]++",
      false,
      ROOT},
    {"closing a group restores a preceding ownership operator",
      "x++ + (y) + getline z",
      false,
      ROOT},
    {"a pipe needs selection", "x | getline y", false, ROOT},
    {"reserved word prefixes are names", "getline_value++", false, LOCAL_ROOT},
    {"an exponent ends before a glued getline", "1e3getline < x", false, ROOT},
    {"a float suffix ends before a glued getline",
      "1.5fgetline x++",
      false,
      ROOT},
    {"literal contents do not require selection",
      "\"getline < x++\" ~ /getline[<+]/",
      false,
      LOCAL_ROOT},
    {"comments do not require selection",
      "x && # getline < x++\n y",
      false,
      LOCAL_ROOT},
    {"continued input still selects getline ownership",
      "x + \\\n getline y++",
      false,
      ROOT},
    {"print redirection ends the local region",
      "x > getline y++",
      true,
      LOCAL_PRINT_ROOT},
    {"logical or does not end the print region",
      "x || $getline y++",
      true,
      PRINT_ROOT},
    {"parentheses isolate print redirection",
      "(x > getline y++)",
      true,
      PRINT_ROOT},
  };
  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    bool valid[TOKEN_TYPE_COUNT] = {false};
    valid[cases[i].print ? PRINT_ROOT : ROOT] = true;
    valid[cases[i].print ? LOCAL_PRINT_ROOT : LOCAL_ROOT] = true;
    failed |= expect_scan_result(
      cases[i].name,
      cases[i].source,
      valid,
      true,
      cases[i].expected,
      0
    );
  }
  return failed;
}

static void test_linear_local_expression_lookahead(void) {
  const size_t nesting_depth = 8192;
  const char prefix[] = "getline $(";
  const char inner[] = "x++";
  char *source = malloc(
    nesting_depth * (sizeof(prefix) - 1) + sizeof(inner) + nesting_depth
  );
  assert(source);
  size_t offset = 0;
  for (size_t i = 0; i < nesting_depth; i++) {
    memcpy(source + offset, prefix, sizeof(prefix) - 1);
    offset += sizeof(prefix) - 1;
  }
  memcpy(source + offset, inner, sizeof(inner) - 1);
  offset += sizeof(inner) - 1;
  memset(source + offset, ')', nesting_depth);
  offset += nesting_depth;
  source[offset] = '\0';
  MockLexer mock = make_mock_lexer(source);
  ScannerState state = {0};
  const bool valid[TOKEN_TYPE_COUNT] = {[ROOT] = true, [LOCAL_ROOT] = true};
  assert(
    tree_sitter_posix_awk_external_scanner_scan(&state, &mock.lexer, valid)
  );
  assert(mock.lexer.result_symbol == LOCAL_ROOT && mock.token_end == 0);
  assert(mock.advance_count == offset);
  assert(
    !state.boundary.active &&
    !state.boundary.pending[0] &&
    !state.boundary.cache_count
  );
  free(source);
}

static void test_expression_boundary_restoration(void) {
  const uint64_t child = UINT64_C(0x1c200000001),
                 sibling = UINT64_C(0x123456789abcdef);
  ScannerState state = {.boundary = {.pending = {child, sibling}}};
  bool opening[TOKEN_TYPE_COUNT] = {[BEGIN_CHILD] = true};
  for (unsigned i = 0; i < BOUNDARY_RADIX; i++) opening[SAVE_FIRST + i] = true;
  unsigned digits[BOUNDARY_DIGIT_COUNT], count = 0;
  for (;;) {
    MockLexer mock = make_mock_lexer("getline");
    assert(
      tree_sitter_posix_awk_external_scanner_scan(&state, &mock.lexer, opening)
    );
    assert(mock.token_end == 0);
    if (mock.lexer.result_symbol == BEGIN_CHILD) break;
    assert(count < ARRAY_LENGTH(digits));
    digits[count++] = mock.lexer.result_symbol - SAVE_FIRST;
    char bytes[TREE_SITTER_SERIALIZATION_BUFFER_SIZE];
    unsigned length = serialize_state(&state, bytes);
    tree_sitter_posix_awk_external_scanner_deserialize(&state, bytes, length);
  }
  assert(state.boundary.active == child);
  const bool end[TOKEN_TYPE_COUNT] = {[END_CHILD] = true};
  MockLexer mock = make_mock_lexer(" + next");
  assert(tree_sitter_posix_awk_external_scanner_scan(&state, &mock.lexer, end));
  assert(mock.token_end == 0 && mock.advance_count == 0);
  while (count) {
    bool restore[TOKEN_TYPE_COUNT] = {false};
    restore[RESTORE_FIRST + digits[--count]] = true;
    assert(
      tree_sitter_posix_awk_external_scanner_scan(&state, &mock.lexer, restore)
    );
    assert(mock.token_end == 0);
  }
  const bool resume[TOKEN_TYPE_COUNT] = {[RESUME] = true};
  assert(
    tree_sitter_posix_awk_external_scanner_scan(&state, &mock.lexer, resume)
  );
  assert(
    state.boundary.pending[0] == sibling &&
    state.boundary.phase == BOUNDARY_READY
  );
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

  const bool for_in[TOKEN_TYPE_COUNT] = {
    [FOR_IN] = true,
    [ROOT] = true,
    [LOCAL_ROOT] = true,
  };
  MockLexer mock = make_mock_lexer("x in a) y");
  ScannerState state = {0};
  reuse_fail_next_realloc = true;
  assert(
    !tree_sitter_posix_awk_external_scanner_scan(&state, &mock.lexer, for_in)
  );
  assert(!reuse_fail_next_realloc);
  assert(reuse_live_allocations == 0);
}
#endif

static int test_collating_payload_classification(void) {
  const struct {
    const char *name;
    const char *source;
    LexicalMode mode;
    enum TokenType symbol;
    size_t end;
    LexicalMode final_mode;
  } cases[] = {
    {"raw opener stops before an escape beyond its first terminator",
      "[.a.]\\n",
      LEXICAL_MODE_ERE_BODY,
      ERE_COMPOUND_OPENING,
      1,
      LEXICAL_MODE_ERE_COLLATING},
    {"escaped opener keeps its one-byte range",
      "[.a\\n.]",
      LEXICAL_MODE_ERE_BODY,
      ERE_ESCAPED_COMPOUND_OPENING,
      1,
      LEXICAL_MODE_ERE_COLLATING},
    {"nonmatching terminator remains in an escaped equivalence payload",
      "[=a.]\\n=]",
      LEXICAL_MODE_ERE_BODY,
      ERE_ESCAPED_COMPOUND_OPENING,
      1,
      LEXICAL_MODE_ERE_EQUIVALENCE},
    {"a single payload excludes both closing bytes",
      "a.]",
      LEXICAL_MODE_ERE_COLLATING,
      ERE_COMPOUND_SINGLE,
      1,
      LEXICAL_MODE_ERE_COLLATING},
    {"a multi-character payload spans through a nonmatching terminator",
      "a=].]",
      LEXICAL_MODE_ERE_COLLATING,
      ERE_COMPOUND_MULTI,
      3,
      LEXICAL_MODE_ERE_COLLATING},
    {"a multi-character equivalence payload can begin with a meta character",
      "-a=]",
      LEXICAL_MODE_ERE_EQUIVALENCE,
      ERE_COMPOUND_MULTI,
      2,
      LEXICAL_MODE_ERE_EQUIVALENCE},
    {"raw compound payload ends at its first matching terminator",
      "ab.]c.]",
      LEXICAL_MODE_ERE_COLLATING,
      ERE_COMPOUND_MULTI,
      2,
      LEXICAL_MODE_ERE_COLLATING},
    {"a raw compound payload can begin with blanks",
      " \ta.]",
      LEXICAL_MODE_ERE_COLLATING,
      ERE_COMPOUND_MULTI,
      3,
      LEXICAL_MODE_ERE_COLLATING},
  };
  int failed = 0;
  for (size_t i = 0; i < ARRAY_LENGTH(cases); i++) {
    bool valid_symbols[TOKEN_TYPE_COUNT] = {false};
    if (cases[i].mode == LEXICAL_MODE_ERE_BODY) {
      valid_symbols[ERE_COMPOUND_OPENING] = true;
      valid_symbols[ERE_ESCAPED_COMPOUND_OPENING] = true;
    } else {
      valid_symbols[ERE_COMPOUND_SINGLE] = true;
      valid_symbols[ERE_COMPOUND_MULTI] = true;
    }
    failed |= expect_scan_result_at(
      cases[i].name,
      cases[i].source,
      valid_symbols,
      cases[i].mode,
      true,
      cases[i].symbol,
      0,
      cases[i].end,
      cases[i].final_mode
    );
  }
  return failed;
}

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
  test_failed_probe_reads_as_end_of_input();
  test_nul_and_eof_are_distinct();
  test_disabled_tokens_preserve_state();
  failed |= test_serialization();
  test_expression_boundary_serialization();
  test_expression_decisions_survive_child_boundaries();
  failed |= test_local_expression_selection();
  test_linear_local_expression_lookahead();
  test_expression_boundary_restoration();
  failed |= test_source_token_ranges();
  failed |= test_update_operator_boundaries();
  failed |= test_prefix_operator_boundaries();
  failed |= test_backslash_tokens_by_mode();
  failed |= test_escape_classification_ignores_expected_tokens();
  failed |= test_continued_newline_requires_its_marker();
  test_repeated_continuation_ranges_and_restoration();
  test_pending_continuation_rejects_changed_boundaries();
  failed |= test_literal_classification();
  failed |= test_blank_skip_token_ranges();
  failed |= test_slash_dispatch();
  failed |= test_getline_pipe_lookahead();
  failed |= test_word_boundary_lookahead();
  failed |= test_getline_operand_lookahead();
  failed |= test_update_operator_lookahead();
  failed |= test_deep_getline_source_lookahead();
  failed |= test_linear_word_boundary_lookahead();
  test_long_function_name();
  failed |= test_ere_state_transitions();
  failed |= test_collating_payload_classification();
  failed |= test_compound_terminators_do_not_become_content();
  failed |= test_string_and_comment_modes();
  failed |= test_newline_ends_every_literal_mode();
  failed |= test_error_mode_real_tokens();
#ifdef TREE_SITTER_REUSE_ALLOCATOR
  test_reuse_allocator_contract();
#endif
  return failed;
}
