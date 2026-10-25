#include "tree_sitter/alloc.h"
#include "tree_sitter/parser.h"

#include <stddef.h>
#include <string.h>

#define ARRAY_LENGTH(array) (sizeof(array) / sizeof((array)[0]))

enum TokenType {
  ROOT = 0,
  PRINT_ROOT = 1,
  FOR_IN = 2,
  PRINT_ARGUMENTS = 3,
  BEGIN_CHILD = 4,
  END_CHILD = 5,
  RESUME = 6,
  NEXT_ARGUMENT = 7,
  SAVE_0 = 8,
  SAVE_1 = 9,
  SAVE_2 = 10,
  SAVE_3 = 11,
  RESTORE_0 = 12,
  RESTORE_1 = 13,
  RESTORE_2 = 14,
  RESTORE_3 = 15,
  CLASS_UNARY = 16,
  CLASS_NON_UNARY = 17,
  SELECT_ATOM = 18,
  SELECT_NAME = 19,
  SELECT_GROUP = 20,
  SELECT_CALL = 21,
  SELECT_SUBSCRIPT = 22,
  SELECT_GET = 23,
  SELECT_TARGET_GET = 24,
  SELECT_FILE = 25,
  SELECT_FIELD = 26,
  SELECT_PREFIX = 27,
  SELECT_POSTFIX = 28,
  SELECT_BINARY = 29,
  SELECT_CONCAT = 30,
  SELECT_ASSIGNMENT = 31,
  SELECT_CONDITIONAL = 32,
  SELECT_BRANCHES = 33,
  SELECT_PIPE = 34,
  SELECT_MEMBERSHIP = 35,
  SELECT_TUPLE = 36,
  LOCAL_ROOT = 37,
  LOCAL_PRINT_ROOT = 38,
  GETLINE_WORD = 39,
  FUNC_NAME_WORD = 40,
  DIVISION_SLASH = 41,
  ERE_OPENING_SLASH = 42,
  INCR_OPERATOR = 43,
  DECR_OPERATOR = 44,
  PLUS_OPERATOR = 45,
  MINUS_OPERATOR = 46,
  FIELD_OPERATOR = 47,
  NOT_OPERATOR = 48,
  GETLINE_PIPE = 49,
  ERE_COMPOUND_OPENING = 50,
  ERE_ESCAPED_COMPOUND_OPENING = 51,
  ERE_COMPOUND_SINGLE = 52,
  ERE_COMPOUND_MULTI = 53,
  ERE_DOT_CLOSING = 54,
  ERE_EQUAL_CLOSING = 55,
  ERE_COLON_CLOSING = 56,
  ERE_BRACKET_LITERAL_OPEN = 57,
  ERE_COMPOUND_CONTENT = 58,
  ERE_CLOSING_HYPHEN = 59,
  ERE_CLOSING = 60,
  STRING_OPENING = 61,
  STRING_CLOSING = 62,
  STRING_ESCAPE = 63,
  ERE_NAMED_ESCAPE = 64,
  ERE_QUOTED_ESCAPE = 65,
  ERE_OCTAL_ESCAPE = 66,
  ERE_UNDEFINED_ESCAPE = 67,
  ERE_ESCAPED_DELIMITER = 68,
  COMMENT = 69,
  CONTINUATION_BACKSLASH = 70,
  CONTINUATION_NEWLINE = 71,
  LITERAL_BREAK = 72,
  STRAY_BACKSLASH = 73,
  ERROR_SENTINEL = 74,
  TOKEN_TYPE_COUNT,
};

typedef enum {
  WORD_NONE,
  WORD_NAME,
  WORD_KEYWORD,
  WORD_MEMBERSHIP,
  WORD_GETLINE,
  WORD_BUILTIN,
} WordKind;

typedef struct {
  const char *spelling;
  WordKind kind;
} ReservedWord;

typedef enum {
  LEXICAL_MODE_OUTSIDE,
  LEXICAL_MODE_ERE_BODY,
  LEXICAL_MODE_ERE_COLLATING,
  LEXICAL_MODE_ERE_EQUIVALENCE,
  LEXICAL_MODE_ERE_CLASS,
  LEXICAL_MODE_STRING,
  LEXICAL_MODE_CONTINUED_NEWLINE,
} LexicalMode;

#ifndef BOUNDARY_CACHE_CAPACITY
#define BOUNDARY_CACHE_CAPACITY 32
#endif

enum {
  STATE_VALUES_OFFSET = 5,
  STATE_CACHE_COUNT_OFFSET = 45,
  STATE_CACHE_INDEX_OFFSET = 46,
  STATE_CACHE_REPEATS_OFFSET = 47,
  STATE_CACHE_DELTA_OFFSET = 51,
  STATE_DECISION_HASH_OFFSET = 55,
  STATE_HEADER_BYTES = 63,
  STATE_OUTLINE_BYTES = 28,
};

typedef char BoundarySerializationFits
  [(STATE_HEADER_BYTES + BOUNDARY_CACHE_CAPACITY * STATE_OUTLINE_BYTES <=
     TREE_SITTER_SERIALIZATION_BUFFER_SIZE)
      ? 1
      : -1];

typedef struct {
  uint64_t descriptor, children[2];
  uint32_t repeat;
} BoundaryOutline;

typedef struct {
  BoundaryOutline outline;
  size_t children[2];
} BoundaryTree;

enum BoundaryPhase { BOUNDARY_READY, BOUNDARY_SAVING };
typedef struct {
  uint64_t active, pending[2], next, carry;
  enum BoundaryPhase phase;
  unsigned selected;
  bool unary, print;
  uint64_t decision_hash;
  unsigned cache_count, cache_index;
  uint32_t cache_repeats, cache_delta;
  BoundaryOutline cache[BOUNDARY_CACHE_CAPACITY + 1];
} BoundaryState;

static void boundary_reset(BoundaryState *state, uint64_t active) {
  memset(state, 0, offsetof(BoundaryState, decision_hash));
  state->active = active;
}

typedef struct {
  LexicalMode mode;
  BoundaryState boundary;
} ScannerState;

#define SAVE_FIRST SAVE_0
#define RESTORE_FIRST RESTORE_0
enum { SAVED_DIGITS = RESTORE_FIRST - SAVE_FIRST };
// BEGIN generated reserved words from grammar.js.
enum {
  MAX_RESERVED_WORD_LENGTH = 8,
};

static const ReservedWord RESERVED_WORDS[] = {
  {"BEGIN", WORD_KEYWORD},
  {"END", WORD_KEYWORD},
  {"break", WORD_KEYWORD},
  {"continue", WORD_KEYWORD},
  {"delete", WORD_KEYWORD},
  {"do", WORD_KEYWORD},
  {"else", WORD_KEYWORD},
  {"exit", WORD_KEYWORD},
  {"for", WORD_KEYWORD},
  {"function", WORD_KEYWORD},
  {"if", WORD_KEYWORD},
  {"next", WORD_KEYWORD},
  {"nextfile", WORD_KEYWORD},
  {"print", WORD_KEYWORD},
  {"printf", WORD_KEYWORD},
  {"return", WORD_KEYWORD},
  {"while", WORD_KEYWORD},
  {"in", WORD_MEMBERSHIP},
  {"getline", WORD_GETLINE},
  {"atan2", WORD_BUILTIN},
  {"close", WORD_BUILTIN},
  {"cos", WORD_BUILTIN},
  {"exp", WORD_BUILTIN},
  {"fflush", WORD_BUILTIN},
  {"gsub", WORD_BUILTIN},
  {"index", WORD_BUILTIN},
  {"int", WORD_BUILTIN},
  {"length", WORD_BUILTIN},
  {"log", WORD_BUILTIN},
  {"match", WORD_BUILTIN},
  {"rand", WORD_BUILTIN},
  {"sin", WORD_BUILTIN},
  {"split", WORD_BUILTIN},
  {"sprintf", WORD_BUILTIN},
  {"sqrt", WORD_BUILTIN},
  {"srand", WORD_BUILTIN},
  {"sub", WORD_BUILTIN},
  {"substr", WORD_BUILTIN},
  {"system", WORD_BUILTIN},
  {"tolower", WORD_BUILTIN},
  {"toupper", WORD_BUILTIN},
};
// END generated reserved words.

static bool is_ascii_blank(int32_t character) {
  return character == ' ' || character == '\t';
}

static bool is_ere_mode(LexicalMode mode) {
  return mode >= LEXICAL_MODE_ERE_BODY && mode <= LEXICAL_MODE_ERE_CLASS;
}

static bool is_ascii_digit(int32_t character) {
  return character >= '0' && character <= '9';
}

static bool is_ascii_letter(int32_t character) {
  return (character >= 'A' && character <= 'Z') ||
    (character >= 'a' && character <= 'z');
}

static bool is_word_start(int32_t character) {
  return is_ascii_letter(character) || character == '_';
}

static bool is_word_continue(int32_t character) {
  return is_word_start(character) || is_ascii_digit(character);
}

static bool is_octal_digit(int32_t character) {
  return character >= '0' && character <= '7';
}

static bool character_in(int32_t character, const char *characters) {
  if (character <= 0 || character > 127) {
    return false;
  }
  return strchr(characters, (char)character) != NULL;
}

static bool emit(TSLexer *lexer, enum TokenType token) {
  lexer->result_symbol = token;
  return true;
}

static bool emit_mode(
  ScannerState *state,
  TSLexer *lexer,
  LexicalMode mode,
  enum TokenType token
) {
  state->mode = mode;
  return emit(lexer, token);
}

static bool emit_delimiter(
  ScannerState *state,
  TSLexer *lexer,
  LexicalMode mode,
  enum TokenType token
) {
  lexer->advance(lexer, false);
  lexer->mark_end(lexer);
  return emit_mode(state, lexer, mode, token);
}

static size_t doubled_capacity(size_t capacity, size_t size, size_t initial) {
  const size_t count = capacity ? capacity * 2 : initial;
  return count <= capacity || count > SIZE_MAX / size ? 0 : count;
}

static void *
grow_array(void *items, size_t *capacity, size_t size, size_t initial) {
  const size_t count = doubled_capacity(*capacity, size, initial);
  void *grown = count ? ts_realloc(items, count * size) : NULL;
  if (grown) *capacity = count;
  return grown;
}

static WordKind classify_word(const char *word, size_t length) {
  if (length > MAX_RESERVED_WORD_LENGTH) {
    return WORD_NAME;
  }
  for (size_t i = 0; i < ARRAY_LENGTH(RESERVED_WORDS); i++) {
    const char *spelling = RESERVED_WORDS[i].spelling;
    if (spelling[0] == word[0] && strcmp(spelling, word) == 0) {
      return RESERVED_WORDS[i].kind;
    }
  }
  return WORD_NAME;
}

static WordKind scan_word_spelling(TSLexer *lexer) {
  char spelling[MAX_RESERVED_WORD_LENGTH + 1] = {0};
  size_t length = 0;
  if (!is_word_start(lexer->lookahead)) {
    return WORD_NONE;
  }
  do {
    if (length < MAX_RESERVED_WORD_LENGTH) {
      spelling[length] = (char)lexer->lookahead;
    }
    if (length <= MAX_RESERVED_WORD_LENGTH) {
      length++;
    }
    lexer->advance(lexer, false);
  } while (is_word_continue(lexer->lookahead));
  return classify_word(spelling, length);
}

static bool skip_token_layout(TSLexer *lexer) {
  for (;;) {
    if (is_ascii_blank(lexer->lookahead)) {
      lexer->advance(lexer, false);
    } else if (lexer->lookahead == '\\') {
      lexer->advance(lexer, false);
      if (lexer->lookahead != '\n') {
        return false;
      }
      lexer->advance(lexer, false);
    } else {
      return true;
    }
  }
}

static bool scan_sign(TSLexer *lexer, const bool *valid_symbols) {
  const int32_t first = lexer->lookahead;
  if (first != '+' && first != '-') {
    return false;
  }
  lexer->advance(lexer, false);
  lexer->mark_end(lexer);
  if (lexer->lookahead == '=') {
    return false;
  }
  if (lexer->lookahead == first) {
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    const enum TokenType token = first == '+' ? INCR_OPERATOR : DECR_OPERATOR;
    return valid_symbols[token] && emit(lexer, token);
  }
  const enum TokenType token = first == '+' ? PLUS_OPERATOR : MINUS_OPERATOR;
  return valid_symbols[token] && emit(lexer, token);
}

static bool scan_contextual_word(TSLexer *lexer, bool recovering) {
  WordKind word = scan_word_spelling(lexer);
  lexer->mark_end(lexer);
  if (word == WORD_NAME && lexer->lookahead == '(')
    return emit(lexer, FUNC_NAME_WORD);
  if (recovering && (word == WORD_NAME || word == WORD_GETLINE))
    return emit(lexer, ERROR_SENTINEL);
  return word == WORD_GETLINE && emit(lexer, GETLINE_WORD);
}

static bool
scan_prefix_operator(TSLexer *lexer, const bool *valid, bool recovering) {
  int32_t first = lexer->lookahead;
  lexer->advance(lexer, false);
  lexer->mark_end(lexer);
  if (first == '!' && (lexer->lookahead == '=' || lexer->lookahead == '~'))
    return false;
  if (recovering) return emit(lexer, ERROR_SENTINEL);
  enum TokenType token = first == '$' ? FIELD_OPERATOR : NOT_OPERATOR;
  return valid[token] && emit(lexer, token);
}

// Read past output pipes so edits invalidate reused nodes.
static bool
scan_getline_pipe(TSLexer *lexer, const bool *valid_symbols, bool recovering) {
  lexer->advance(lexer, false);
  lexer->mark_end(lexer);
  const bool getline =
    skip_token_layout(lexer) && scan_word_spelling(lexer) == WORD_GETLINE;
  return getline &&
    !recovering &&
    valid_symbols[GETLINE_PIPE] &&
    emit(lexer, GETLINE_PIPE);
}

static bool scan_slash_start(
  ScannerState *state,
  TSLexer *lexer,
  const bool *valid_symbols
) {
  lexer->advance(lexer, false);
  lexer->mark_end(lexer);
  if (valid_symbols[DIVISION_SLASH]) {
    return lexer->lookahead != '=' && emit(lexer, DIVISION_SLASH);
  }
  return valid_symbols[ERE_OPENING_SLASH] &&
    emit_mode(state, lexer, LEXICAL_MODE_ERE_BODY, ERE_OPENING_SLASH);
}

static bool scan_escape(const ScannerState *state, TSLexer *lexer) {
  if (
    lexer->eof(lexer) ||
    lexer->lookahead == '\n' ||
    (lexer->lookahead == 0 && is_ere_mode(state->mode))
  ) {
    return emit(lexer, STRAY_BACKSLASH);
  }
  const int32_t character = lexer->lookahead;
  lexer->advance(lexer, false);
  if (is_octal_digit(character)) {
    for (
      unsigned count = 1; count < 3 && is_octal_digit(lexer->lookahead); count++
    ) {
      lexer->advance(lexer, false);
    }
  }
  lexer->mark_end(lexer);
  enum TokenType token = STRING_ESCAPE;
  if (is_ere_mode(state->mode)) {
    if (character == '/') {
      token = ERE_ESCAPED_DELIMITER;
    } else if (is_octal_digit(character)) {
      token = ERE_OCTAL_ESCAPE;
    } else if (character_in(character, "abfnrtv")) {
      token = ERE_NAMED_ESCAPE;
    } else if (character_in(character, "().*+?{}|^$[\\]")) {
      token = ERE_QUOTED_ESCAPE;
    } else {
      token = ERE_UNDEFINED_ESCAPE;
    }
  }
  return emit(lexer, token);
}

// Emit every backslash to prevent fallback to the internal marker.
static bool scan_backslash(ScannerState *state, TSLexer *lexer) {
  lexer->advance(lexer, false);
  lexer->mark_end(lexer);
  if (state->mode == LEXICAL_MODE_OUTSIDE) {
    if (lexer->lookahead != '\n') {
      return emit(lexer, STRAY_BACKSLASH);
    }
    return emit_mode(
      state,
      lexer,
      LEXICAL_MODE_CONTINUED_NEWLINE,
      CONTINUATION_BACKSLASH
    );
  }
  return scan_escape(state, lexer);
}

typedef struct {
  enum TokenType delimiter;
  enum TokenType content;
  int32_t first;
  LexicalMode mode;
} EreCompoundToken;

static const EreCompoundToken ERE_COMPOUND_TOKENS[] = {
  {ERE_COMPOUND_OPENING, ERE_BRACKET_LITERAL_OPEN, '[', LEXICAL_MODE_ERE_BODY},
  {ERE_DOT_CLOSING, ERE_COMPOUND_CONTENT, '.', LEXICAL_MODE_ERE_COLLATING},
  {ERE_EQUAL_CLOSING, ERE_COMPOUND_CONTENT, '=', LEXICAL_MODE_ERE_EQUIVALENCE},
  {ERE_COLON_CLOSING, ERE_COMPOUND_CONTENT, ':', LEXICAL_MODE_ERE_CLASS},
};

static bool compound_has_escape(TSLexer *lexer, int32_t marker) {
  lexer->advance(lexer, false);
  while (
    !lexer->eof(lexer) && lexer->lookahead != '\n' && lexer->lookahead != '/'
  ) {
    const int32_t character = lexer->lookahead;
    if (character == '\\') {
      return true;
    }
    lexer->advance(lexer, false);
    if (character == marker && lexer->lookahead == ']') {
      break;
    }
  }
  return false;
}

static bool skip_blanks_before_newline(TSLexer *lexer, unsigned *count) {
  while (is_ascii_blank(lexer->lookahead)) {
    lexer->advance(lexer, false);
    if (count) (*count)++;
  }
  return lexer->lookahead == '\n';
}

static bool emit_literal_break(
  ScannerState *state,
  TSLexer *lexer,
  const bool *valid_symbols
) {
  return valid_symbols[LITERAL_BREAK] &&
    emit_mode(state, lexer, LEXICAL_MODE_OUTSIDE, LITERAL_BREAK);
}

static bool scan_ere_collating_element(
  ScannerState *state,
  TSLexer *lexer,
  const bool *valid_symbols
) {
  const int32_t marker = state->mode == LEXICAL_MODE_ERE_COLLATING ? '.' : '=';
  const int32_t first = lexer->lookahead;
  unsigned count = 0;
  if (skip_blanks_before_newline(lexer, &count)) {
    return emit_literal_break(state, lexer, valid_symbols);
  }
  while (
    lexer->lookahead != 0 &&
    lexer->lookahead != '\n' &&
    lexer->lookahead != '/' &&
    lexer->lookahead != '\\'
  ) {
    lexer->mark_end(lexer);
    const int32_t character = lexer->lookahead;
    lexer->advance(lexer, false);
    if (character == marker && lexer->lookahead == ']') {
      if (count == 0 || (count == 1 && character_in(first, "-]"))) {
        return false;
      }
      const enum TokenType token =
        count == 1 ? ERE_COMPOUND_SINGLE : ERE_COMPOUND_MULTI;
      return valid_symbols[token] && emit(lexer, token);
    }
    if (count < 2) {
      count++;
    }
  }
  return false;
}

static bool scan_ere_compound_token(
  ScannerState *state,
  TSLexer *lexer,
  const EreCompoundToken *token,
  const bool *valid_symbols
) {
  const bool opening = token->delimiter == ERE_COMPOUND_OPENING;
  lexer->advance(lexer, false);
  lexer->mark_end(lexer);
  const bool terminates = opening
    ? character_in(lexer->lookahead, ".=:")
    : lexer->lookahead == ']' && state->mode == token->mode;
  if (!terminates) {
    return valid_symbols[token->content] && emit(lexer, token->content);
  }
  if (
    !valid_symbols[token->delimiter] &&
    !(opening && valid_symbols[ERE_ESCAPED_COMPOUND_OPENING])
  ) {
    return false;
  }
  LexicalMode mode = LEXICAL_MODE_ERE_BODY;
  enum TokenType delimiter = token->delimiter;
  if (opening) {
    for (size_t i = 1; i < ARRAY_LENGTH(ERE_COMPOUND_TOKENS); i++) {
      if (ERE_COMPOUND_TOKENS[i].first == lexer->lookahead) {
        mode = ERE_COMPOUND_TOKENS[i].mode;
        break;
      }
    }
    if (
      mode != LEXICAL_MODE_ERE_CLASS &&
      compound_has_escape(lexer, lexer->lookahead)
    ) {
      delimiter = ERE_ESCAPED_COMPOUND_OPENING;
    }
  }
  return valid_symbols[delimiter] && emit_mode(state, lexer, mode, delimiter);
}

static bool scan_ere_context(
  ScannerState *state,
  TSLexer *lexer,
  const bool *valid_symbols,
  bool recovering
) {
  if (!recovering) {
    if (
      (state->mode == LEXICAL_MODE_ERE_COLLATING ||
        state->mode == LEXICAL_MODE_ERE_EQUIVALENCE) &&
      (valid_symbols[ERE_COMPOUND_SINGLE] || valid_symbols[ERE_COMPOUND_MULTI])
    ) {
      return scan_ere_collating_element(state, lexer, valid_symbols);
    }
    for (size_t i = 0; i < ARRAY_LENGTH(ERE_COMPOUND_TOKENS); i++) {
      const EreCompoundToken *token = &ERE_COMPOUND_TOKENS[i];
      if (lexer->lookahead == token->first) {
        return scan_ere_compound_token(state, lexer, token, valid_symbols);
      }
    }
  }
  if (lexer->lookahead == '/' && valid_symbols[ERE_CLOSING])
    return emit_delimiter(state, lexer, LEXICAL_MODE_OUTSIDE, ERE_CLOSING);
  if (lexer->lookahead == '-' && valid_symbols[ERE_CLOSING_HYPHEN]) {
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    return lexer->lookahead == ']' && emit(lexer, ERE_CLOSING_HYPHEN);
  }
  return skip_blanks_before_newline(lexer, NULL) &&
    emit_literal_break(state, lexer, valid_symbols);
}

static bool scan_comment(TSLexer *lexer) {
  do {
    lexer->advance(lexer, false);
  } while (lexer->lookahead != '\n' && !lexer->eof(lexer));
  lexer->mark_end(lexer);
  return emit(lexer, COMMENT);
}

static bool skip_literal(TSLexer *lexer) {
  const int32_t delimiter = lexer->lookahead;
  for (;;) {
    lexer->advance(lexer, false);
    if (lexer->lookahead == '\\') {
      lexer->advance(lexer, false);
    } else if (lexer->lookahead == delimiter) {
      lexer->advance(lexer, false);
      return true;
    }
    if (lexer->eof(lexer) || lexer->lookahead == '\n') {
      return false;
    }
  }
}

static bool skip_number(TSLexer *lexer) {
  bool digits = false;
  bool floating = false;
  while (is_ascii_digit(lexer->lookahead)) {
    digits = true;
    lexer->advance(lexer, false);
  }
  if (lexer->lookahead == '.') {
    floating = true;
    lexer->advance(lexer, false);
    while (is_ascii_digit(lexer->lookahead)) {
      digits = true;
      lexer->advance(lexer, false);
    }
  }
  if (!digits) {
    return false;
  }
  lexer->mark_end(lexer);
  if (lexer->lookahead == 'e' || lexer->lookahead == 'E') {
    lexer->advance(lexer, false);
    if (lexer->lookahead == '+' || lexer->lookahead == '-') {
      lexer->advance(lexer, false);
    }
    if (!is_ascii_digit(lexer->lookahead)) {
      return true;
    }
    floating = true;
    do {
      lexer->advance(lexer, false);
    } while (is_ascii_digit(lexer->lookahead));
    lexer->mark_end(lexer);
  }
  if (floating && character_in(lexer->lookahead, "fFlL")) {
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
  }
  return true;
}

static bool skip_scope_layout(TSLexer *lexer, bool separated) {
  for (;;) {
    const int32_t character = lexer->lookahead;
    if (character == '#') {
      do {
        lexer->advance(lexer, false);
      } while (!lexer->eof(lexer) && lexer->lookahead != '\n');
    } else if (is_ascii_blank(character) || (character == '\n' && separated)) {
      lexer->advance(lexer, false);
    } else if (character == '\\') {
      lexer->advance(lexer, false);
      if (lexer->lookahead != '\n') return false;
      lexer->advance(lexer, false);
    } else {
      return true;
    }
  }
}

static bool skip_group(TSLexer *lexer, bool *tuple) {
  bool after_operand = false;
  bool separated = false;
  unsigned depth = 0;
  do {
    if (
      !skip_scope_layout(lexer, separated) ||
      lexer->eof(lexer) ||
      lexer->lookahead == '\n'
    )
      return false;
    const int32_t character = lexer->lookahead;
    separated = false;
    if (tuple && character == ',' && depth == 1) *tuple = true;
    if (character == '"' || (character == '/' && !after_operand)) {
      if (!skip_literal(lexer)) {
        return false;
      }
      after_operand = true;
      continue;
    }
    if (is_word_start(character)) {
      scan_word_spelling(lexer);
      after_operand = true;
      continue;
    }
    lexer->advance(lexer, false);
    if (character_in(character, "([")) {
      depth++;
      after_operand = false;
    } else if (character_in(character, ")]")) {
      depth--;
      after_operand = true;
    } else if (character_in(character, "+-") && lexer->lookahead == character) {
      lexer->advance(lexer, false);
      after_operand = true;
    } else if (character_in(character, "&|") && lexer->lookahead == character) {
      lexer->advance(lexer, false);
      after_operand = false;
      separated = true;
    } else {
      if (character == '{' || character == '}' || character == ';')
        return false;
      after_operand = is_word_continue(character) || character == '.';
      separated = character == ',';
    }
  } while (depth > 0);
  return true;
}

typedef enum { ATOM_PLAIN, ATOM_CALL, ATOM_SUBSCRIPT } WordAtom;

static WordAtom word_atom(TSLexer *lexer, WordKind word) {
  const bool adjacent = lexer->lookahead == '(';
  if (!skip_token_layout(lexer)) return ATOM_PLAIN;
  if (lexer->lookahead == '(' && (adjacent || word == WORD_BUILTIN))
    return ATOM_CALL;
  if (lexer->lookahead == '[' && word == WORD_NAME) return ATOM_SUBSCRIPT;
  return ATOM_PLAIN;
}

typedef struct {
  TSLexer lexer;
  TSLexer *source;
  int32_t *characters;
  size_t length;
  size_t capacity;
  size_t position;
  size_t marked_end;
  bool failed;
} InputProbe;

static void probe_restore(InputProbe *probe, size_t position) {
  probe->position = position;
  probe->lexer.lookahead = probe->failed ? 0
    : position < probe->length           ? probe->characters[position]
                                         : probe->source->lookahead;
}

static bool probe_eof(const TSLexer *lexer) {
  const InputProbe *probe = (const InputProbe *)lexer;
  return probe->failed ||
    (probe->position == probe->length && probe->source->eof(probe->source));
}

static void probe_advance(TSLexer *lexer, bool skip) {
  InputProbe *probe = (InputProbe *)lexer;
  (void)skip;
  if (probe_eof(lexer)) {
    return;
  }
  if (probe->position == probe->length) {
    if (probe->length == probe->capacity) {
      int32_t *characters = grow_array(
        probe->characters,
        &probe->capacity,
        sizeof(*characters),
        64
      );
      if (!characters) {
        probe->failed = true;
        lexer->lookahead = 0;
        return;
      }
      probe->characters = characters;
    }
    probe->characters[probe->length++] = lexer->lookahead;
    probe->source->advance(probe->source, false);
  }
  probe_restore(probe, probe->position + 1);
}

static void probe_mark_end(TSLexer *lexer) {
  InputProbe *probe = (InputProbe *)lexer;
  probe->marked_end = probe->position;
}

static InputProbe probe_open(TSLexer *source) {
  return (InputProbe){
    .lexer =
      {.lookahead = source->lookahead,
        .advance = probe_advance,
        .mark_end = probe_mark_end,
        .eof = probe_eof},
    .source = source,
  };
}

static bool probe_number(InputProbe *probe) {
  if (!skip_number(&probe->lexer)) return false;
  probe_restore(probe, probe->marked_end);
  return true;
}

enum ProbeToken {
  PROBE_END = 0,
  PROBE_VALUE = 256,
  PROBE_TUPLE,
  PROBE_NAME,
  PROBE_GETLINE,
  PROBE_UPDATE,
  PROBE_ASSIGNMENT,
  PROBE_COMPARISON,
  PROBE_MATCH,
  PROBE_MEMBERSHIP,
  PROBE_AND,
  PROBE_OR,
};

static int probe_token(InputProbe *probe, bool expect_operand) {
  TSLexer *lexer = &probe->lexer;
  if (!skip_token_layout(lexer) || probe_eof(lexer)) {
    return PROBE_END;
  }
  const int32_t character = lexer->lookahead;
  if (is_ascii_digit(character) || character == '.') {
    return probe_number(probe) ? PROBE_VALUE : PROBE_END;
  }
  if (character == '"' || (expect_operand && character == '/')) {
    return skip_literal(lexer) ? PROBE_VALUE : PROBE_END;
  }
  if (character == '(') {
    bool tuple = false;
    return skip_group(lexer, &tuple) ? (tuple ? PROBE_TUPLE : PROBE_VALUE)
                                     : PROBE_END;
  }
  if (is_word_start(character)) {
    const WordKind word = scan_word_spelling(lexer);
    if (word == WORD_GETLINE) {
      return PROBE_GETLINE;
    }
    if (word == WORD_MEMBERSHIP) {
      return PROBE_MEMBERSHIP;
    }
    if (word != WORD_NAME && word != WORD_BUILTIN) {
      return PROBE_END;
    }
    const WordAtom atom = word_atom(lexer, word);
    if (atom != ATOM_PLAIN && !skip_group(lexer, NULL)) {
      return PROBE_END;
    }
    return atom == ATOM_SUBSCRIPT || (atom == ATOM_PLAIN && word == WORD_NAME)
      ? PROBE_NAME
      : PROBE_VALUE;
  }
  lexer->advance(lexer, false);
  if (character_in(character, "+-") && lexer->lookahead == character) {
    lexer->advance(lexer, false);
    return PROBE_UPDATE;
  }
  if (character_in(character, "&|") && lexer->lookahead == character) {
    lexer->advance(lexer, false);
    if (!skip_scope_layout(lexer, true)) {
      return PROBE_END;
    }
    return character == '&' ? PROBE_AND : PROBE_OR;
  }
  if (character_in(character, "+-*/%^=<>!") && lexer->lookahead == '=') {
    lexer->advance(lexer, false);
    return character_in(character, "=<>!") ? PROBE_COMPARISON
                                           : PROBE_ASSIGNMENT;
  }
  if (character == '!' && lexer->lookahead == '~') {
    lexer->advance(lexer, false);
    return PROBE_MATCH;
  }
  return character;
}

static bool probe_print_arguments(InputProbe *input) {
  const size_t start = input->position;
  bool tuple = false;
  const bool arguments = input->lexer.lookahead == '(' &&
    skip_group(&input->lexer, &tuple) &&
    tuple &&
    probe_token(input, false) != PROBE_MEMBERSHIP;
  probe_restore(input, start);
  return arguments;
}

// BEGIN generated expression rules from grammar.js.
enum ExpressionPrecedence {
  EXPR_ASSIGNMENT = 2,
  EXPR_CONDITIONAL = 3,
  EXPR_LOGICAL_OR = 4,
  EXPR_LOGICAL_AND = 6,
  EXPR_MEMBERSHIP = 8,
  EXPR_MATCH = 10,
  EXPR_COMPARISON = 12,
  EXPR_CONCATENATION = 14,
  EXPR_ADDITIVE = 16,
  EXPR_MULTIPLICATIVE = 18,
  EXPR_UNARY = 20,
  EXPR_EXPONENTIATION = 21,
  EXPR_POSTFIX = 23,
  EXPR_FIELD = 24,
  EXPR_FILE_SOURCE = EXPR_ADDITIVE,
};

static int expression_right_precedence(int precedence) {
  switch (precedence) {
    case EXPR_ASSIGNMENT:
      return EXPR_ASSIGNMENT;
    case EXPR_CONDITIONAL:
      return EXPR_CONDITIONAL;
    case EXPR_UNARY:
      return EXPR_UNARY;
    case EXPR_EXPONENTIATION:
      return EXPR_UNARY;
    case EXPR_FIELD:
      return EXPR_FIELD;
    default:
      return precedence + 1;
  }
}

static bool expression_non_associative(int precedence) {
  switch (precedence) {
    case EXPR_MATCH:
    case EXPR_COMPARISON:
      return true;
    default:
      return false;
  }
}
// END generated expression rules.

static int probe_precedence(int token) {
  switch (token) {
    case '=':
    case PROBE_ASSIGNMENT:
      return EXPR_ASSIGNMENT;
    case '?':
      return EXPR_CONDITIONAL;
    case PROBE_OR:
      return EXPR_LOGICAL_OR;
    case PROBE_AND:
      return EXPR_LOGICAL_AND;
    case PROBE_MEMBERSHIP:
      return EXPR_MEMBERSHIP;
    case '~':
    case PROBE_MATCH:
      return EXPR_MATCH;
    case '<':
    case '>':
    case PROBE_COMPARISON:
      return EXPR_COMPARISON;
    case '+':
    case '-':
      return EXPR_ADDITIVE;
    case '*':
    case '/':
    case '%':
      return EXPR_MULTIPLICATIVE;
    case '^':
      return EXPR_EXPONENTIATION;
    case PROBE_UPDATE:
    case PROBE_NAME:
    case PROBE_VALUE:
    case PROBE_TUPLE:
    case PROBE_GETLINE:
    case '$':
    case '!':
      return EXPR_CONCATENATION;
    default:
      return 0;
  }
}

typedef struct {
  int *tokens;
  size_t length, capacity;
  bool failed;
  uint32_t expected;
  BoundaryTree *tree;
  size_t tree_count, tree_capacity, tree_root;
} RecognizerInput;

enum {
  RECOGNIZED_LVALUE = EXPR_FIELD + 1,
  RECOGNIZED_GET = 1,
  RECOGNIZED_FILE,
  RECOGNIZED_FIELD,
  RECOGNIZED_PREFIX,
  RECOGNIZED_POSTFIX,
  RECOGNIZED_CONCAT,
  RECOGNIZED_BRANCHES,
  RECOGNIZED_TUPLE,
};

enum {
  SIGNATURE_UNARY = 1,
  SIGNATURE_POSTFIX = 2,
  SIGNATURE_LVALUE = 4,
};

static uint32_t boundary_signature(
  int kind,
  int ceiling,
  bool lvalue,
  bool postfix,
  bool unary
) {
  return ((uint32_t)kind << 8) |
    ((uint32_t)ceiling << 3) |
    (lvalue ? SIGNATURE_LVALUE : 0u) |
    (postfix ? SIGNATURE_POSTFIX : 0u) |
    (unary ? SIGNATURE_UNARY : 0u);
}

static int signature_ceiling(uint32_t signature) {
  return (signature >> 3) & 31;
}

static int signature_kind(uint32_t signature) {
  return (int)(signature >> 8);
}

static uint64_t make_descriptor(uint32_t signature, uint32_t length) {
  return (uint64_t)signature << 32 | length;
}

static uint32_t descriptor_signature(uint64_t descriptor) {
  return (uint32_t)(descriptor >> 32);
}

static uint32_t descriptor_length(uint64_t descriptor) {
  return (uint32_t)descriptor;
}

static int descriptor_kind(uint64_t descriptor) {
  return signature_kind(descriptor_signature(descriptor));
}

enum { SAVED_SIGNATURE_BITS = 17 };
static const uint64_t SAVED_SIGNATURE_MASK =
  (UINT64_C(1) << SAVED_SIGNATURE_BITS) - 1;
static const uint64_t SAVED_HIGH_MASK =
  ~((UINT64_C(1) << (32 + SAVED_SIGNATURE_BITS)) - 1);
static uint64_t saved_descriptor(uint64_t descriptor) {
  return (descriptor & SAVED_HIGH_MASK) |
    (uint64_t)descriptor_length(descriptor) << SAVED_SIGNATURE_BITS |
    (descriptor_signature(descriptor) & SAVED_SIGNATURE_MASK);
}

static uint64_t restored_descriptor(uint64_t saved) {
  return (saved & SAVED_HIGH_MASK) |
    make_descriptor(
      (uint32_t)(saved & SAVED_SIGNATURE_MASK),
      (uint32_t)(saved >> SAVED_SIGNATURE_BITS)
    );
}

static void recognizer_destroy(RecognizerInput *parser) {
  ts_free(parser->tokens);
  ts_free(parser->tree);
}

static void recognizer_add_token(RecognizerInput *parser, int token) {
  if (parser->failed) return;
  if (parser->length == parser->capacity) {
    int *tokens =
      grow_array(parser->tokens, &parser->capacity, sizeof(*tokens), 32);
    if (!tokens) {
      parser->failed = true;
      return;
    }
    parser->tokens = tokens;
  }
  parser->tokens[parser->length++] = token;
}

static int recognizer_token(const RecognizerInput *parser, size_t position) {
  return position < parser->length ? parser->tokens[position] : PROBE_END;
}

typedef struct {
  size_t start, end;
  int kind, ceiling;
  bool lvalue, postfix, unary;
  uint64_t children[2];
  size_t child_indices[2];
} RecognizedValue;

typedef enum {
  RECOGNIZER_PREFIX,
  RECOGNIZER_FIELD,
  RECOGNIZER_UNARY,
  RECOGNIZER_TARGET,
  RECOGNIZER_SOURCE,
  RECOGNIZER_GET,
  RECOGNIZER_TAIL,
  RECOGNIZER_BINARY,
  RECOGNIZER_CONSEQUENCE,
  RECOGNIZER_ALTERNATIVE,
} RecognizerStage;

typedef struct {
  size_t start, operator_position, context;
  int minimum, token, precedence;
  RecognizerStage stage;
  bool get_only;
  RecognizedValue left, consequence;
} RecognizerFrame;

typedef struct {
  size_t parent;
  uint64_t control, left, consequence;
} RecognizerContext;

typedef struct {
  size_t position, context;
} RecognizerFailure;

typedef struct {
  RecognizerInput *input;
  RecognizerFrame *frames;
  size_t *choices, *sources;
  bool *alternatives;
  size_t depth, context_depth;
  RecognizedValue value;
  bool valid, exhausted, checked, feasible;
  size_t remaining;
  RecognizerContext *contexts;
  size_t context_count, context_capacity, *context_slots, context_slot_capacity;
  RecognizerFailure *failures, *history;
  size_t failure_count, failure_capacity, history_count, history_capacity;
} Recognizer;

static uint32_t value_signature(RecognizedValue value) {
  return boundary_signature(
    value.kind,
    value.ceiling,
    value.lvalue,
    value.postfix,
    value.unary
  );
}

static size_t recognizer_context_hash(RecognizerContext value) {
  return value.parent * 0x9e3779b1u ^
    (size_t)value.control * 0x85ebca6bu ^
    (size_t)value.left * 0xc2b2ae35u ^
    (size_t)value.consequence;
}

static size_t recognizer_context(Recognizer *parser) {
  size_t parent = parser->context_depth
    ? parser->frames[parser->context_depth - 1].context
    : 0;
  for (size_t i = parser->context_depth; i < parser->depth; i++) {
    if (!parser->remaining) {
      parser->exhausted = true;
      return 0;
    }
    parser->remaining--;
    RecognizerFrame *frame = &parser->frames[i];
    RecognizerContext value = {
      .parent = parent,
      .control = (uint64_t)frame->stage |
        ((uint64_t)(unsigned)frame->minimum << 8) |
        ((uint64_t)(unsigned)frame->precedence << 16) |
        ((uint64_t)(unsigned)frame->token << 24) |
        ((uint64_t)frame->get_only << 40) |
        ((uint64_t)(frame->operator_position == SIZE_MAX) << 41),
      .left = value_signature(frame->left),
      .consequence = value_signature(frame->consequence)
    };
    if (parser->context_count >= parser->context_slot_capacity / 2) {
      const size_t capacity =
        doubled_capacity(parser->context_slot_capacity, sizeof(size_t), 64);
      size_t *slots = capacity ? ts_calloc(capacity, sizeof(*slots)) : NULL;
      if (!slots) return 0;
      for (size_t j = 0; j < parser->context_count; j++) {
        size_t slot =
          recognizer_context_hash(parser->contexts[j]) & (capacity - 1);
        while (slots[slot]) slot = (slot + 1) & (capacity - 1);
        slots[slot] = j + 1;
      }
      ts_free(parser->context_slots);
      parser->context_slots = slots;
      parser->context_slot_capacity = capacity;
    }
    size_t slot =
      recognizer_context_hash(value) & (parser->context_slot_capacity - 1);
    while (parser->context_slots[slot]) {
      RecognizerContext old = parser->contexts[parser->context_slots[slot] - 1];
      if (
        old.parent == value.parent &&
        old.control == value.control &&
        old.left == value.left &&
        old.consequence == value.consequence
      )
        break;
      slot = (slot + 1) & (parser->context_slot_capacity - 1);
    }
    if (!parser->context_slots[slot]) {
      if (parser->context_count == parser->context_capacity) {
        RecognizerContext *contexts = grow_array(
          parser->contexts,
          &parser->context_capacity,
          sizeof(*contexts),
          32
        );
        if (!contexts) return 0;
        parser->contexts = contexts;
      }
      parser->contexts[parser->context_count++] = value;
      parser->context_slots[slot] = parser->context_count;
    }
    parent = parser->context_slots[slot];
    frame->context = parent;
    parser->context_depth = i + 1;
  }
  return parent;
}

static size_t recognizer_failure_hash(RecognizerFailure value) {
  return value.position * 0x9e3779b1u ^ value.context * 0x85ebca6bu;
}

static bool
recognizer_failed(Recognizer *parser, RecognizerFailure value, bool add) {
  if (!value.context) return false;
  if (add && parser->failure_count >= parser->failure_capacity / 2) {
    const size_t capacity =
      doubled_capacity(parser->failure_capacity, sizeof(*parser->failures), 64);
    RecognizerFailure *failures =
      capacity ? ts_calloc(capacity, sizeof(*failures)) : NULL;
    if (!failures) return false;
    for (size_t i = 0; i < parser->failure_capacity; i++)
      if (parser->failures[i].context) {
        size_t slot =
          recognizer_failure_hash(parser->failures[i]) & (capacity - 1);
        while (failures[slot].context) slot = (slot + 1) & (capacity - 1);
        failures[slot] = parser->failures[i];
      }
    ts_free(parser->failures);
    parser->failures = failures;
    parser->failure_capacity = capacity;
  }
  if (!parser->failure_capacity) return false;
  size_t slot = recognizer_failure_hash(value) & (parser->failure_capacity - 1);
  while (parser->failures[slot].context) {
    if (
      parser->failures[slot].position == value.position &&
      parser->failures[slot].context == value.context
    )
      return true;
    slot = (slot + 1) & (parser->failure_capacity - 1);
  }
  if (add) {
    parser->failures[slot] = value;
    parser->failure_count++;
  }
  return false;
}

static bool recognizer_checkpoint(Recognizer *parser) {
  if (parser->frames[parser->depth - 1].stage != RECOGNIZER_PREFIX) return true;
  RecognizerFailure value = {
    parser->frames[parser->depth - 1].start,
    recognizer_context(parser)
  };
  if (recognizer_failed(parser, value, false)) return false;
  if (!value.context) return true;
  if (parser->history_count == parser->history_capacity) {
    RecognizerFailure *history = grow_array(
      parser->history,
      &parser->history_capacity,
      sizeof(*history),
      32
    );
    if (!history) return true;
    parser->history = history;
  }
  parser->history[parser->history_count++] = value;
  return true;
}

static void recognizer_push(Recognizer *parser, size_t start, int minimum) {
  if (parser->depth > parser->input->length) {
    parser->valid = false;
    return;
  }
  parser->frames[parser->depth++] = (RecognizerFrame){
    .start = start,
    .minimum = minimum,
    .get_only = minimum == 0,
  };
}

static void recognizer_return(Recognizer *parser, RecognizedValue value) {
  parser->value = value;
  parser->depth--;
}

static uint64_t recognizer_descriptor(RecognizedValue node) {
  if (node.start == node.end) return 0;
  return make_descriptor(
    value_signature(node),
    (uint32_t)(node.end - node.start)
  );
}

static size_t recognizer_store(Recognizer *parser, RecognizedValue value) {
  if (!value.end) return 0;
  RecognizerInput *input = parser->input;
  if (input->tree_count == input->tree_capacity) {
    BoundaryTree *tree =
      grow_array(input->tree, &input->tree_capacity, sizeof(*tree), 64);
    if (!tree) {
      parser->valid = false;
      return 0;
    }
    input->tree = tree;
  }
  input->tree[input->tree_count++] = (BoundaryTree){
    .outline =
      {.descriptor = recognizer_descriptor(value),
        .children = {value.children[0], value.children[1]},
        .repeat = 1},
    .children = {value.child_indices[0], value.child_indices[1]},
  };
  return input->tree_count;
}

static RecognizedValue recognizer_node(
  Recognizer *parser,
  int kind,
  size_t start,
  size_t end,
  RecognizedValue left,
  RecognizedValue right
) {
  return (RecognizedValue){
    .child_indices =
      {recognizer_store(parser, left.end ? left : right),
        left.end ? recognizer_store(parser, right) : 0},
    .kind = kind,
    .children =
      {recognizer_descriptor(left.end ? left : right),
        left.end ? recognizer_descriptor(right) : 0},
    .start = start,
    .end = end,
    .ceiling = EXPR_FIELD,
  };
}

typedef struct {
  int token, precedence;
  bool pipe, postfix, eligible;
} Continuation;

static Continuation continuation(
  const RecognizerInput *input,
  size_t end,
  int minimum,
  uint32_t left
) {
  const int token = recognizer_token(input, end);
  const int ceiling = signature_ceiling(left);
  const bool lvalue = left & SIGNATURE_LVALUE;
  const bool pipe = token == '|' && minimum <= EXPR_ASSIGNMENT;
  const int precedence = pipe ? EXPR_ASSIGNMENT : probe_precedence(token);
  return (Continuation){
    .token = token,
    .precedence = precedence,
    .pipe = pipe,
    .postfix = token == PROBE_UPDATE &&
      lvalue &&
      (left & SIGNATURE_POSTFIX) &&
      minimum <= EXPR_POSTFIX &&
      EXPR_POSTFIX <= ceiling,
    .eligible = precedence > 0 &&
      minimum <= precedence &&
      precedence <= ceiling &&
      (pipe || precedence != EXPR_ASSIGNMENT || lvalue),
  };
}

static size_t right_start(Continuation next, size_t end) {
  return next.precedence == EXPR_CONCATENATION ? end : end + 1;
}

static int right_minimum(Continuation next) {
  return next.pipe      ? 0
    : next.token == '?' ? EXPR_ASSIGNMENT
                        : expression_right_precedence(next.precedence);
}

static int operator_kind(int token, int precedence) {
  return precedence == EXPR_CONCATENATION ? RECOGNIZED_CONCAT : token;
}

static int operator_ceiling(int token, int precedence) {
  return token == '|' ? EXPR_FIELD
                      : precedence - expression_non_associative(precedence);
}

static void recognizer_step(Recognizer *parser) {
  RecognizerFrame *frame = &parser->frames[parser->depth - 1];
  const int token = recognizer_token(parser->input, frame->start);
  const RecognizedValue empty = {0};
  switch (frame->stage) {
    case RECOGNIZER_PREFIX:
      if (
        token == PROBE_NAME ||
        ((token == PROBE_VALUE || token == PROBE_TUPLE) &&
          frame->minimum != RECOGNIZED_LVALUE)
      ) {
        frame->left = recognizer_node(
          parser,
          token == PROBE_TUPLE ? RECOGNIZED_TUPLE : 0,
          frame->start,
          frame->start + 1,
          empty,
          empty
        );
        frame->left.lvalue = token == PROBE_NAME;
        frame->left.postfix = true;
        frame->stage = RECOGNIZER_TAIL;
      } else if (token == '$') {
        frame->stage = RECOGNIZER_FIELD;
        recognizer_push(parser, frame->start + 1, EXPR_FIELD);
      } else if (
        token == PROBE_GETLINE && frame->minimum != RECOGNIZED_LVALUE
      ) {
        frame->left = recognizer_node(
          parser,
          RECOGNIZED_GET,
          frame->start,
          frame->start + 1,
          empty,
          empty
        );
        frame->left.postfix = true;
        const int next = recognizer_token(parser->input, frame->start + 1);
        if (next == PROBE_NAME || next == '$') {
          parser->alternatives[frame->start] =
            parser->choices[frame->start] == 0;
          if (!parser->choices[frame->start]) {
            frame->stage = RECOGNIZER_TARGET;
            recognizer_push(parser, frame->start + 1, RECOGNIZED_LVALUE);
            break;
          }
        }
        frame->stage = RECOGNIZER_GET;
      } else if (
        frame->minimum != RECOGNIZED_LVALUE &&
        (token == '+' || token == '-' || token == '!' || token == PROBE_UPDATE)
      ) {
        frame->stage = RECOGNIZER_UNARY;
        recognizer_push(
          parser,
          frame->start + 1,
          token == PROBE_UPDATE ? RECOGNIZED_LVALUE : EXPR_UNARY
        );
      } else {
        parser->valid = false;
      }
      break;
    case RECOGNIZER_FIELD:
      frame->left = recognizer_node(
        parser,
        RECOGNIZED_FIELD,
        frame->start,
        parser->value.end,
        empty,
        parser->value
      );
      frame->left.lvalue = true;
      frame->left.postfix = parser->value.postfix;
      frame->stage = RECOGNIZER_TAIL;
      break;
    case RECOGNIZER_UNARY:
      frame->left = recognizer_node(
        parser,
        RECOGNIZED_PREFIX,
        frame->start,
        parser->value.end,
        empty,
        parser->value
      );
      frame->left.ceiling = token == PROBE_UPDATE ? EXPR_POSTFIX : EXPR_UNARY;
      frame->left.unary = token == '+' || token == '-';
      frame->stage = RECOGNIZER_TAIL;
      break;
    case RECOGNIZER_TARGET:
      frame->left = recognizer_node(
        parser,
        RECOGNIZED_GET,
        frame->start,
        parser->value.end,
        empty,
        parser->value
      );
      frame->left.postfix = parser->value.postfix;
      frame->stage = RECOGNIZER_GET;
      break;
    case RECOGNIZER_GET:
      if (
        !frame->get_only &&
        recognizer_token(parser->input, frame->left.end) == '<'
      ) {
        const size_t position = frame->left.end;
        const size_t owner = parser->sources[position]++;
        if (parser->choices[position] == owner) {
          parser->alternatives[position] = true;
          frame->stage = RECOGNIZER_SOURCE;
          recognizer_push(parser, position + 1, EXPR_FILE_SOURCE);
          break;
        }
      }
      frame->stage = RECOGNIZER_TAIL;
      break;
    case RECOGNIZER_SOURCE:
      frame->left = recognizer_node(
        parser,
        RECOGNIZED_FILE,
        frame->start,
        parser->value.end,
        frame->left,
        parser->value
      );
      frame->left.postfix = true;
      frame->stage = RECOGNIZER_TAIL;
      break;
    case RECOGNIZER_TAIL: {
      if (frame->minimum == RECOGNIZED_LVALUE || frame->get_only) {
        recognizer_return(parser, frame->left);
        break;
      }
      const size_t end = frame->left.end;
      const Continuation next = continuation(
        parser->input,
        end,
        frame->minimum,
        value_signature(frame->left)
      );
      if (next.postfix && (!next.eligible || !parser->choices[end])) {
        parser->alternatives[end] = next.eligible;
        const RecognizedValue left = frame->left;
        frame->left = recognizer_node(
          parser,
          RECOGNIZED_POSTFIX,
          frame->start,
          end + 1,
          left,
          empty
        );
        frame->left.ceiling = EXPR_POSTFIX;
        break;
      }
      if (!next.eligible) {
        recognizer_return(parser, frame->left);
        break;
      }
      frame->token = next.token;
      frame->operator_position = end;
      frame->precedence = next.precedence;
      frame->stage =
        next.token == '?' ? RECOGNIZER_CONSEQUENCE : RECOGNIZER_BINARY;
      if (next.token == PROBE_MEMBERSHIP) {
        if (recognizer_token(parser->input, end + 1) != PROBE_NAME) {
          parser->valid = false;
          break;
        }
        parser->value =
          recognizer_node(parser, 0, end + 1, end + 2, empty, empty);
        parser->value.lvalue = parser->value.postfix = true;
        break;
      }
      if (
        next.pipe && recognizer_token(parser->input, end + 1) != PROBE_GETLINE
      ) {
        parser->valid = false;
        break;
      }
      if (next.token == '<') parser->alternatives[end] = false;
      recognizer_push(parser, right_start(next, end), right_minimum(next));
      break;
    }
    case RECOGNIZER_CONSEQUENCE:
      if (recognizer_token(parser->input, parser->value.end) != ':') {
        parser->valid = false;
        break;
      }
      frame->consequence = parser->value;
      frame->stage = RECOGNIZER_ALTERNATIVE;
      recognizer_push(parser, parser->value.end + 1, EXPR_CONDITIONAL);
      break;
    case RECOGNIZER_ALTERNATIVE:
      parser->value = recognizer_node(
        parser,
        RECOGNIZED_BRANCHES,
        frame->operator_position + 1,
        parser->value.end,
        frame->consequence,
        parser->value
      );
      if (frame->operator_position == SIZE_MAX) {
        recognizer_return(parser, parser->value);
        break;
      }
      frame->stage = RECOGNIZER_BINARY;
      break;
    case RECOGNIZER_BINARY: {
      const bool unary = frame->left.unary;
      frame->left = recognizer_node(
        parser,
        operator_kind(frame->token, frame->precedence),
        frame->start,
        parser->value.end,
        frame->left,
        parser->value
      );
      frame->left.unary = unary;
      frame->left.ceiling = operator_ceiling(frame->token, frame->precedence);
      frame->stage = RECOGNIZER_TAIL;
      break;
    }
  }
}

typedef struct {
  uint64_t descriptor;
  uint32_t start, children[2], decision, rank;
} SharedNode;

typedef struct {
  uint32_t *items, *slots, *heap;
  size_t length, capacity, slot_capacity, heap_length;
} SharedSet;

typedef struct {
  RecognizerInput *input;
  SharedNode *nodes;
  size_t length, capacity, generated;
  SharedSet *memo;
  bool *needed;
  uint32_t *ranks[2], *stack;
  bool failed, select;
} SharedRecognition;

enum { RECOGNIZER_LEVELS = RECOGNIZED_LVALUE + 1 };

static uint32_t shared_end(SharedNode node) {
  return node.start + descriptor_length(node.descriptor);
}

static uint32_t shared_shape(SharedNode node) {
  return descriptor_signature(node.descriptor);
}

static SharedNode shared_node(
  SharedRecognition *parser,
  int kind,
  uint32_t start,
  uint32_t end,
  uint32_t left,
  uint32_t right,
  int ceiling,
  bool lvalue,
  bool postfix,
  bool unary
) {
  if (parser->generated != SIZE_MAX) parser->generated++;
  SharedNode node = {
    .descriptor = make_descriptor(
      boundary_signature(kind, ceiling, lvalue, postfix, unary),
      end - start
    ),
    .start = start,
    .children = {left ? left : right, left ? right : 0},
    .decision = UINT32_MAX,
  };
  if (kind == RECOGNIZED_GET) {
    node.decision = start;
    node.rank = right ? 1 : 2;
  } else if (kind == RECOGNIZED_FILE || kind == '<') {
    node.decision = shared_end(parser->nodes[left - 1]);
    node.rank = kind == '<' ? (uint32_t)parser->input->length + 2
                            : (uint32_t)parser->input->length - start + 1;
  } else if (kind == RECOGNIZED_POSTFIX) {
    node.decision = end - 1;
    node.rank = 1;
  } else if (
    kind == RECOGNIZED_PREFIX && parser->input->tokens[start] == PROBE_UPDATE
  ) {
    node.decision = start;
    node.rank = 2;
  }
  return node;
}

static bool
shared_better(SharedRecognition *parser, SharedNode a, SharedNode b) {
  if (!parser->select) return false;
  const SharedNode roots[] = {a, b};
  for (unsigned side = 0; side < 2; side++) {
    memset(parser->ranks[side], 0, parser->input->length * sizeof(uint32_t));
    size_t depth = 0;
    SharedNode node = roots[side];
    for (;;) {
      if (node.decision != UINT32_MAX)
        parser->ranks[side][node.decision] = node.rank;
      for (unsigned child = 0; child < 2; child++)
        if (node.children[child]) parser->stack[depth++] = node.children[child];
      if (!depth) break;
      node = parser->nodes[parser->stack[--depth] - 1];
    }
  }
  for (size_t i = a.start; i < shared_end(a); i++) {
    if (parser->ranks[0][i] != parser->ranks[1][i])
      return parser->ranks[0][i] < parser->ranks[1][i];
  }
  return false;
}

static size_t shared_hash(SharedNode node) {
  return (size_t)shared_end(node) * 0x9e3779b1u ^ shared_shape(node);
}

static void shared_add(
  SharedRecognition *parser,
  SharedSet *set,
  SharedNode node,
  uint32_t existing
) {
  if (parser->failed) return;
  if (set->length >= set->slot_capacity / 2) {
    const size_t capacity =
      doubled_capacity(set->slot_capacity, sizeof(uint32_t), 8);
    uint32_t *slots = capacity ? ts_calloc(capacity, sizeof(*slots)) : NULL;
    if (!slots) {
      parser->failed = true;
      return;
    }
    for (size_t i = 0; i < set->length; i++) {
      size_t slot =
        shared_hash(parser->nodes[set->items[i] - 1]) & (capacity - 1);
      while (slots[slot]) slot = (slot + 1) & (capacity - 1);
      slots[slot] = (uint32_t)i + 1;
    }
    ts_free(set->slots);
    set->slots = slots;
    set->slot_capacity = capacity;
  }
  size_t slot = shared_hash(node) & (set->slot_capacity - 1);
  while (set->slots[slot]) {
    uint32_t index = set->items[set->slots[slot] - 1];
    SharedNode old = parser->nodes[index - 1];
    if (
      shared_end(old) == shared_end(node) &&
      shared_shape(old) == shared_shape(node)
    ) {
      if (shared_better(parser, node, old)) parser->nodes[index - 1] = node;
      return;
    }
    slot = (slot + 1) & (set->slot_capacity - 1);
  }
  if (set->length == set->capacity) {
    size_t capacity = set->capacity;
    uint32_t *items = set->capacity <= UINT32_MAX / 2
      ? grow_array(set->items, &capacity, sizeof(*items), 4)
      : NULL;
    if (!items) {
      parser->failed = true;
      return;
    }
    set->items = items;
    uint32_t *heap = grow_array(set->heap, &set->capacity, sizeof(*heap), 4);
    if (!heap) {
      parser->failed = true;
      return;
    }
    set->heap = heap;
  }
  if (!existing) {
    if (parser->length == parser->capacity) {
      SharedNode *nodes = parser->capacity <= UINT32_MAX / 2
        ? grow_array(parser->nodes, &parser->capacity, sizeof(*nodes), 64)
        : NULL;
      if (!nodes) {
        parser->failed = true;
        return;
      }
      parser->nodes = nodes;
    }
    parser->nodes[parser->length++] = node;
    existing = (uint32_t)parser->length;
  }
  set->items[set->length] = existing;
  set->slots[slot] = (uint32_t)++set->length;
  size_t position = set->heap_length++;
  while (
    position &&
    shared_end(parser->nodes[set->heap[(position - 1) / 2] - 1]) >
      shared_end(node)
  ) {
    set->heap[position] = set->heap[(position - 1) / 2];
    position = (position - 1) / 2;
  }
  set->heap[position] = existing;
}

static uint32_t shared_pop(SharedRecognition *parser, SharedSet *set) {
  uint32_t result = set->heap[0], last = set->heap[--set->heap_length];
  size_t position = 0;
  while (position * 2 + 1 < set->heap_length) {
    size_t child = position * 2 + 1;
    if (
      child + 1 < set->heap_length &&
      shared_end(parser->nodes[set->heap[child + 1] - 1]) <
        shared_end(parser->nodes[set->heap[child] - 1])
    )
      child++;
    if (
      shared_end(parser->nodes[last - 1]) <=
      shared_end(parser->nodes[set->heap[child] - 1])
    )
      break;
    set->heap[position] = set->heap[child];
    position = child;
  }
  set->heap[position] = last;
  return result;
}

static void shared_set_destroy(SharedSet *set) {
  ts_free(set->items);
  ts_free(set->slots);
  ts_free(set->heap);
}

static SharedSet *
shared_expr(SharedRecognition *parser, size_t start, int minimum) {
  return &parser->memo[start * RECOGNIZER_LEVELS + minimum];
}

static void shared_get(
  SharedRecognition *parser,
  SharedSet *result,
  uint32_t start,
  bool redirected
) {
  shared_add(
    parser,
    result,
    shared_node(
      parser,
      RECOGNIZED_GET,
      start,
      start + 1,
      0,
      0,
      EXPR_FIELD,
      false,
      true,
      false
    ),
    0
  );
  SharedSet *targets = shared_expr(parser, start + 1, RECOGNIZED_LVALUE);
  for (size_t i = 0; i < targets->length && !parser->failed; i++) {
    uint32_t target = targets->items[i];
    SharedNode child = parser->nodes[target - 1];
    shared_add(
      parser,
      result,
      shared_node(
        parser,
        RECOGNIZED_GET,
        start,
        shared_end(child),
        0,
        target,
        EXPR_FIELD,
        false,
        shared_shape(child) & SIGNATURE_POSTFIX,
        false
      ),
      0
    );
  }
  const size_t count = result->length;
  for (size_t i = 0; redirected && i < count && !parser->failed; i++) {
    uint32_t get = result->items[i];
    uint32_t end = shared_end(parser->nodes[get - 1]);
    if (recognizer_token(parser->input, end) != '<') continue;
    SharedSet *sources = shared_expr(parser, end + 1, EXPR_FILE_SOURCE);
    for (size_t j = 0; j < sources->length && !parser->failed; j++) {
      uint32_t source = sources->items[j];
      shared_add(
        parser,
        result,
        shared_node(
          parser,
          RECOGNIZED_FILE,
          start,
          shared_end(parser->nodes[source - 1]),
          get,
          source,
          EXPR_FIELD,
          false,
          true,
          false
        ),
        0
      );
    }
  }
}

static void shared_prefix(
  SharedRecognition *parser,
  SharedSet *result,
  uint32_t start,
  int minimum
) {
  int token = recognizer_token(parser->input, start);
  bool lvalue = minimum == RECOGNIZED_LVALUE;
  if (
    token == PROBE_NAME ||
    (!lvalue && (token == PROBE_VALUE || token == PROBE_TUPLE))
  ) {
    shared_add(
      parser,
      result,
      shared_node(
        parser,
        token == PROBE_TUPLE ? RECOGNIZED_TUPLE : 0,
        start,
        start + 1,
        0,
        0,
        EXPR_FIELD,
        token == PROBE_NAME,
        true,
        false
      ),
      0
    );
  } else if (
    token == '$' ||
    (!lvalue &&
      (token == '+' || token == '-' || token == '!' || token == PROBE_UPDATE))
  ) {
    SharedSet *operands = shared_expr(
      parser,
      start + 1,
      token == '$'              ? EXPR_FIELD
        : token == PROBE_UPDATE ? RECOGNIZED_LVALUE
                                : EXPR_UNARY
    );
    for (size_t i = 0; i < operands->length && !parser->failed; i++) {
      uint32_t operand = operands->items[i];
      SharedNode child = parser->nodes[operand - 1];
      shared_add(
        parser,
        result,
        shared_node(
          parser,
          token == '$' ? RECOGNIZED_FIELD : RECOGNIZED_PREFIX,
          start,
          shared_end(child),
          0,
          operand,
          token == '$'              ? EXPR_FIELD
            : token == PROBE_UPDATE ? EXPR_POSTFIX
                                    : EXPR_UNARY,
          token == '$',
          token == '$' && (shared_shape(child) & SIGNATURE_POSTFIX),
          token == '+' || token == '-'
        ),
        0
      );
    }
  } else if (!lvalue && token == PROBE_GETLINE) {
    shared_get(parser, result, start, minimum != 0);
  }
}

static void
shared_branches(SharedRecognition *parser, SharedSet *result, uint32_t start) {
  SharedSet *consequences = shared_expr(parser, start, EXPR_ASSIGNMENT);
  for (size_t i = 0; i < consequences->length && !parser->failed; i++) {
    uint32_t consequence = consequences->items[i];
    uint32_t end = shared_end(parser->nodes[consequence - 1]);
    if (recognizer_token(parser->input, end) != ':') continue;
    SharedSet *alternatives = shared_expr(parser, end + 1, EXPR_CONDITIONAL);
    for (size_t j = 0; j < alternatives->length && !parser->failed; j++) {
      uint32_t alternative = alternatives->items[j];
      shared_add(
        parser,
        result,
        shared_node(
          parser,
          RECOGNIZED_BRANCHES,
          start,
          shared_end(parser->nodes[alternative - 1]),
          consequence,
          alternative,
          EXPR_FIELD,
          false,
          false,
          false
        ),
        0
      );
    }
  }
}

static void
shared_compute(SharedRecognition *parser, uint32_t start, int minimum) {
  SharedSet *result = shared_expr(parser, start, minimum), work = {0};
  shared_prefix(parser, &work, start, minimum);
  if (minimum == RECOGNIZED_LVALUE || minimum == 0) {
    *result = work;
    return;
  }
  while (work.heap_length && !parser->failed) {
    uint32_t index = shared_pop(parser, &work);
    SharedNode left = parser->nodes[index - 1];
    const uint32_t end = shared_end(left), shape = shared_shape(left);
    const Continuation next = continuation(parser->input, end, minimum, shape);
    if (next.postfix)
      shared_add(
        parser,
        &work,
        shared_node(
          parser,
          RECOGNIZED_POSTFIX,
          start,
          end + 1,
          index,
          0,
          EXPR_POSTFIX,
          false,
          false,
          false
        ),
        0
      );
    if (!next.eligible) {
      if (!next.postfix) shared_add(parser, result, left, index);
      continue;
    }
    SharedSet local = {0}, *rights = &local;
    if (next.token == PROBE_MEMBERSHIP) {
      if (recognizer_token(parser->input, end + 1) == PROBE_NAME)
        shared_add(
          parser,
          &local,
          shared_node(
            parser,
            0,
            end + 1,
            end + 2,
            0,
            0,
            EXPR_FIELD,
            true,
            true,
            false
          ),
          0
        );
    } else if (next.token == '?') {
      shared_branches(parser, &local, end + 1);
    } else if (
      !next.pipe || recognizer_token(parser->input, end + 1) == PROBE_GETLINE
    ) {
      rights = shared_expr(parser, right_start(next, end), right_minimum(next));
    }
    for (size_t i = 0; i < rights->length && !parser->failed; i++) {
      const uint32_t right = rights->items[i];
      const SharedNode child = parser->nodes[right - 1];
      shared_add(
        parser,
        &work,
        shared_node(
          parser,
          operator_kind(next.token, next.precedence),
          start,
          shared_end(child),
          index,
          right,
          operator_ceiling(next.token, next.precedence),
          false,
          false,
          shape & SIGNATURE_UNARY
        ),
        0
      );
    }
    shared_set_destroy(&local);
  }
  shared_set_destroy(&work);
}

static bool shared_tree(SharedRecognition *parser, uint32_t root) {
  RecognizerInput *input = parser->input;
  if (input->length > (SIZE_MAX / sizeof(BoundaryTree) - 1) / 2) return false;
  const size_t capacity = input->length * 2 + 1;
  BoundaryTree *tree = ts_realloc(input->tree, capacity * sizeof(*tree));
  if (!tree) return false;
  input->tree = tree;
  input->tree_capacity = capacity;
  input->tree_count = 1;
  input->tree_root = 1;
  parser->stack[0] = root;
  for (size_t i = 0; i < input->tree_count; i++) {
    SharedNode node = parser->nodes[parser->stack[i] - 1];
    input->tree[i] =
      (BoundaryTree){.outline = {.descriptor = node.descriptor, .repeat = 1}};
    for (unsigned child = 0; child < 2; child++) {
      uint32_t index = node.children[child];
      if (!index) continue;
      input->tree[i].outline.children[child] =
        parser->nodes[index - 1].descriptor;
      parser->stack[input->tree_count++] = index;
      input->tree[i].children[child] = input->tree_count;
    }
  }
  return true;
}

static bool
shared_recognize(RecognizerInput *input, bool select, size_t *generated) {
  if (
    input->length > UINT32_MAX - 2 ||
    input->length > SIZE_MAX / RECOGNIZER_LEVELS / sizeof(SharedSet) - 1
  )
    return false;
  SharedRecognition parser = {.input = input, .select = select};
  const size_t count = (input->length + 1) * RECOGNIZER_LEVELS;
  parser.memo = ts_calloc(count, sizeof(*parser.memo));
  parser.needed = ts_calloc(count, sizeof(*parser.needed));
  parser.ranks[0] = ts_calloc(input->length, sizeof(uint32_t));
  parser.ranks[1] = ts_calloc(input->length, sizeof(uint32_t));
  parser.stack = ts_calloc(input->length * 2 + 1, sizeof(uint32_t));
  bool accepted = false;
  if (
    !parser.memo ||
    !parser.needed ||
    !parser.ranks[0] ||
    !parser.ranks[1] ||
    !parser.stack
  )
    goto done;
  parser.needed[EXPR_ASSIGNMENT] = true;
  for (size_t i = 0; i < input->length; i++) {
    const int token = input->tokens[i];
    size_t next = (i + 1) * RECOGNIZER_LEVELS;
    if (token == '$') parser.needed[next + EXPR_FIELD] = true;
    if (token == PROBE_GETLINE || token == PROBE_UPDATE)
      parser.needed[next + RECOGNIZED_LVALUE] = true;
    if (token == '<') parser.needed[next + EXPR_FILE_SOURCE] = true;
    if (token == '+' || token == '-' || token == '!')
      parser.needed[next + EXPR_UNARY] = true;
    if (token == '?') parser.needed[next + EXPR_ASSIGNMENT] = true;
    if (token == ':') parser.needed[next + EXPR_CONDITIONAL] = true;
    if (token == '|') parser.needed[next] = true;
    const int precedence = probe_precedence(token);
    if (precedence == EXPR_CONCATENATION)
      parser.needed[i * RECOGNIZER_LEVELS + EXPR_CONCATENATION + 1] = true;
    else if (precedence)
      parser.needed[next + expression_right_precedence(precedence)] = true;
  }
  for (size_t start = input->length + 1; start && !parser.failed;) {
    start--;
    for (
      int minimum = 0; minimum < RECOGNIZER_LEVELS && !parser.failed; minimum++
    )
      if (parser.needed[start * RECOGNIZER_LEVELS + minimum])
        shared_compute(&parser, (uint32_t)start, minimum);
  }
  SharedSet local = {0}, *result = shared_expr(&parser, 0, EXPR_ASSIGNMENT);
  if (signature_kind(input->expected) == RECOGNIZED_BRANCHES) {
    shared_branches(&parser, &local, 0);
    result = &local;
  }
  uint32_t best = 0;
  for (size_t i = 0; i < result->length && !parser.failed; i++) {
    uint32_t index = result->items[i];
    SharedNode node = parser.nodes[index - 1];
    if (
      shared_end(node) == input->length &&
      (!input->expected || shared_shape(node) == input->expected) &&
      (!best || shared_better(&parser, node, parser.nodes[best - 1]))
    )
      best = index;
  }
  if (best && !parser.failed) accepted = !select || shared_tree(&parser, best);
  shared_set_destroy(&local);
done:
  if (generated) *generated = parser.generated;
  if (parser.memo)
    for (size_t i = 0; i < count; i++) shared_set_destroy(&parser.memo[i]);
  ts_free(parser.memo);
  ts_free(parser.needed);
  ts_free(parser.nodes);
  ts_free(parser.ranks[0]);
  ts_free(parser.ranks[1]);
  ts_free(parser.stack);
  return accepted;
}

#ifndef RECOGNIZER_WORK_FACTOR
#define RECOGNIZER_WORK_FACTOR 8
#endif

enum { RECOGNIZER_RESUME_FACTOR = 32 };

static bool recognizer_extend(Recognizer *parser) {
  if (parser->checked) return false;
  parser->checked = true;
  size_t generated = 0;
  parser->feasible = shared_recognize(parser->input, false, &generated);
  if (!parser->feasible) return false;
  // Stored nodes omit the alternatives that dominate selection work.
  parser->remaining = generated > SIZE_MAX / RECOGNIZER_RESUME_FACTOR
    ? SIZE_MAX
    : generated * RECOGNIZER_RESUME_FACTOR;
  parser->exhausted = false;
  return true;
}

static bool recognizer_parse(RecognizerInput *input) {
  if (input->length > (SIZE_MAX / sizeof(RecognizerFrame)) - 2) return false;
  Recognizer parser = {.input = input};
  parser.frames = ts_calloc(input->length + 2, sizeof(*parser.frames));
  parser.choices = ts_calloc(input->length, sizeof(*parser.choices));
  parser.sources = ts_calloc(input->length, sizeof(*parser.sources));
  parser.alternatives = ts_calloc(input->length, sizeof(*parser.alternatives));
  bool result = false;
  size_t limit = input->length > SIZE_MAX / (input->length ? input->length : 1)
    ? SIZE_MAX
    : input->length * input->length;
  limit = RECOGNIZER_WORK_FACTOR && limit > SIZE_MAX / RECOGNIZER_WORK_FACTOR
    ? SIZE_MAX
    : limit * RECOGNIZER_WORK_FACTOR;
  parser.remaining = limit;
  if (
    !parser.frames || !parser.choices || !parser.sources || !parser.alternatives
  )
    goto done;
  for (;;) {
    if (
      input->length > parser.remaining &&
      (!recognizer_extend(&parser) || input->length > parser.remaining)
    )
      goto done;
    parser.remaining -= input->length;
    memset(parser.sources, 0, input->length * sizeof(*parser.sources));
    memset(
      parser.alternatives,
      0,
      input->length * sizeof(*parser.alternatives)
    );
    parser.depth = 0;
    parser.context_depth = 0;
    parser.history_count = 0;
    input->tree_count = 0;
    parser.valid = true;
    recognizer_push(&parser, 0, EXPR_ASSIGNMENT);
    if (signature_kind(input->expected) == RECOGNIZED_BRANCHES) {
      parser.frames[0].stage = RECOGNIZER_CONSEQUENCE;
      parser.frames[0].operator_position = SIZE_MAX;
      recognizer_push(&parser, 0, EXPR_ASSIGNMENT);
    }
    while (parser.valid && parser.depth) {
      if (!parser.remaining && !recognizer_extend(&parser)) goto done;
      parser.remaining--;
      if (!recognizer_checkpoint(&parser)) {
        parser.valid = false;
        break;
      }
      if (parser.exhausted) {
        if (recognizer_extend(&parser)) continue;
        goto done;
      }
      if (parser.context_depth >= parser.depth)
        parser.context_depth = parser.depth - 1;
      recognizer_step(&parser);
    }
    if (
      parser.valid &&
      !parser.depth &&
      parser.value.end == input->length &&
      (!input->expected || input->expected == value_signature(parser.value))
    ) {
      input->tree_root = recognizer_store(&parser, parser.value);
      result = input->tree_root != 0;
      break;
    }
    size_t position = input->length;
    while (position && !parser.alternatives[position - 1]) position--;
    if (!position) break;
    for (size_t i = 0; i < parser.history_count; i++) {
      if (parser.history[i].position > position - 1)
        recognizer_failed(&parser, parser.history[i], true);
    }
    parser.choices[position - 1]++;
    memset(
      parser.choices + position,
      0,
      (input->length - position) * sizeof(*parser.choices)
    );
  }
done:
  ts_free(parser.contexts);
  ts_free(parser.context_slots);
  ts_free(parser.failures);
  ts_free(parser.history);
  ts_free(parser.frames);
  ts_free(parser.choices);
  ts_free(parser.sources);
  ts_free(parser.alternatives);
  return !result && parser.feasible ? shared_recognize(input, true, NULL)
                                    : result;
}

static const uint64_t UNCONSTRAINED = UINT64_MAX;

static bool
boundary_emit(TSLexer *lexer, BoundaryState *state, unsigned token) {
  if (
    (token >= CLASS_UNARY && token <= SELECT_TUPLE) ||
    token == BEGIN_CHILD ||
    token == END_CHILD ||
    token == NEXT_ARGUMENT
  )
    state->decision_hash =
      (state->decision_hash ^ token) * UINT64_C(0x100000001b3);
  lexer->result_symbol = (TSSymbol)token;
  return true;
}

static void cache_clear(BoundaryState *state) {
  state->cache_count = state->cache_index = state->cache_repeats =
    state->cache_delta = 0;
}

enum {
  SHIFT_DESCRIPTOR = 1,
  SHIFT_LEFT = 2,
  SHIFT_RIGHT = 4,
};

static bool period_shift(
  uint64_t first,
  uint64_t next,
  uint32_t delta,
  unsigned flag,
  unsigned *shifts
) {
  if (first == next) return true;
  if (
    descriptor_signature(first) != descriptor_signature(next) ||
    descriptor_length(first) < descriptor_length(next) ||
    descriptor_length(first) - descriptor_length(next) != delta
  )
    return false;
  *shifts |= flag;
  return true;
}

static bool shifted_field(
  uint64_t pattern,
  uint64_t current,
  bool shifted,
  uint64_t shift
) {
  if (!shifted) return current == pattern;
  return shift < descriptor_length(pattern) && current == pattern - shift;
}

static bool cache_period(
  BoundaryState *state,
  const RecognizerInput *parser,
  const size_t *order,
  size_t length
) {
  size_t best_period = 0, best_repeats = 0, best_covered = 0;
  uint32_t best_delta = 0;
  unsigned best_shifts[BOUNDARY_CACHE_CAPACITY + 1] = {0};
  for (
    size_t period = 1;
    period <= BOUNDARY_CACHE_CAPACITY && period * 2 <= length;
    period++
  ) {
    const uint64_t head = parser->tree[order[0] - 1].outline.descriptor;
    const uint64_t next = parser->tree[order[period] - 1].outline.descriptor;
    if (
      descriptor_signature(head) != descriptor_signature(next) ||
      descriptor_length(head) <= descriptor_length(next)
    )
      continue;
    const uint32_t delta = descriptor_length(head) - descriptor_length(next);
    unsigned shifts[BOUNDARY_CACHE_CAPACITY + 1] = {0};
    bool periodic = true;
    for (size_t i = 0; i < period && periodic; i++) {
      const BoundaryOutline a = parser->tree[order[i] - 1].outline;
      const BoundaryOutline b = parser->tree[order[i + period] - 1].outline;
      periodic = period_shift(
                   a.descriptor,
                   b.descriptor,
                   delta,
                   SHIFT_DESCRIPTOR,
                   &shifts[i]
                 ) &&
        period_shift(
          a.children[0],
          b.children[0],
          delta,
          SHIFT_LEFT,
          &shifts[i]
        ) &&
        period_shift(
          a.children[1],
          b.children[1],
          delta,
          SHIFT_RIGHT,
          &shifts[i]
        );
    }
    if (!periodic) continue;
    size_t matched = period * 2;
    while (matched < length) {
      const size_t i = matched % period;
      const uint64_t shift = (uint64_t)delta * (matched / period);
      const BoundaryOutline pattern = parser->tree[order[i] - 1].outline;
      const BoundaryOutline current = parser->tree[order[matched] - 1].outline;
      if (
        !shifted_field(
          pattern.descriptor,
          current.descriptor,
          shifts[i] & SHIFT_DESCRIPTOR,
          shift
        ) ||
        !shifted_field(
          pattern.children[0],
          current.children[0],
          shifts[i] & SHIFT_LEFT,
          shift
        ) ||
        !shifted_field(
          pattern.children[1],
          current.children[1],
          shifts[i] & SHIFT_RIGHT,
          shift
        )
      )
        break;
      matched++;
    }
    const size_t repeats = matched / period;
    if (repeats * period > best_covered) {
      best_period = period;
      best_repeats = repeats;
      best_covered = repeats * period;
      best_delta = delta;
      memcpy(best_shifts, shifts, sizeof(best_shifts));
    }
  }
  if (best_covered <= BOUNDARY_CACHE_CAPACITY) return false;
  state->cache_count = (unsigned)best_period;
  state->cache_repeats = (uint32_t)best_repeats;
  state->cache_delta = best_delta;
  for (unsigned i = 0; i < state->cache_count; i++) {
    state->cache[i] = parser->tree[order[i] - 1].outline;
    state->cache[i].repeat = best_shifts[i];
  }
  return true;
}

static void
cache_boundary(BoundaryState *state, const RecognizerInput *parser) {
  cache_clear(state);
  if (!BOUNDARY_CACHE_CAPACITY) return;
  if (parser->tree_count > SIZE_MAX / 2 / sizeof(size_t) - 1) return;
  size_t *order = ts_calloc(parser->tree_count * 2 + 2, sizeof(*order));
  if (!order) return;
  size_t *stack = order + parser->tree_count + 1, length = 0, depth = 0;
  stack[depth++] = parser->tree_root;
  while (depth) {
    const size_t index = stack[--depth];
    order[length++] = index;
    const BoundaryTree node = parser->tree[index - 1];
    if (node.children[1]) stack[depth++] = node.children[1];
    if (node.children[0]) stack[depth++] = node.children[0];
  }
  if (!cache_period(state, parser, order, length)) {
    for (size_t i = 0; i < length; i++) {
      const BoundaryOutline outline = parser->tree[order[i] - 1].outline;
      if (state->cache_count) {
        BoundaryOutline *last = &state->cache[state->cache_count - 1];
        const uint32_t count = descriptor_length(last->descriptor);
        const uint32_t left = descriptor_length(last->children[0]);
        const uint64_t delta = count > left ? count - left : 0;
        if (
          delta &&
          delta * last->repeat < left &&
          outline.descriptor == last->descriptor - delta * last->repeat &&
          outline.children[0] == last->children[0] - delta * last->repeat &&
          outline.children[1] == last->children[1]
        ) {
          last->repeat++;
          continue;
        }
      }
      if (state->cache_count == BOUNDARY_CACHE_CAPACITY) break;
      state->cache[state->cache_count++] = outline;
    }
  }
  ts_free(order);
}

static bool
take_boundary(BoundaryState *state, uint64_t requested, BoundaryOutline *node) {
  if (
    !state->cache_count ||
    state->cache[state->cache_index].descriptor != requested
  ) {
    cache_clear(state);
    return false;
  }
  BoundaryOutline outline = state->cache[state->cache_index];
  if (state->cache_delta) {
    if (++state->cache_index == state->cache_count) {
      state->cache_index = 0;
      if (--state->cache_repeats) {
        for (unsigned i = 0; i < state->cache_count; i++) {
          BoundaryOutline *cached = &state->cache[i];
          if (cached->repeat & SHIFT_DESCRIPTOR)
            cached->descriptor -= state->cache_delta;
          if (cached->repeat & SHIFT_LEFT)
            cached->children[0] -= state->cache_delta;
          if (cached->repeat & SHIFT_RIGHT)
            cached->children[1] -= state->cache_delta;
        }
      } else cache_clear(state);
    }
  } else if (--state->cache[0].repeat) {
    uint32_t delta = descriptor_length(outline.descriptor) -
      descriptor_length(outline.children[0]);
    state->cache[0].descriptor -= delta;
    state->cache[0].children[0] -= delta;
  } else {
    state->cache_count--;
    memmove(
      state->cache,
      state->cache + 1,
      state->cache_count * sizeof(*state->cache)
    );
  }
  *node = outline;
  return true;
}

static void collect_expression(
  InputProbe *input,
  RecognizerInput *parser,
  size_t count,
  bool print
) {
  const bool branches = signature_kind(parser->expected) == RECOGNIZED_BRANCHES;
  bool expect_operand = true;
  int questions = 0;
  while (!parser->failed && (!count || parser->length < count)) {
    const int token = probe_token(input, expect_operand);
    if (
      !token ||
      character_in(token, "\n;,)]}{#") ||
      (token == ':' && !questions && !branches) ||
      (print && !count && !questions && (token == '>' || token == '|'))
    )
      break;
    if (token == '?') questions++;
    if (token == ':' && questions) questions--;
    recognizer_add_token(parser, token);
    if (token != PROBE_UPDATE)
      expect_operand = token != PROBE_NAME &&
        token != PROBE_VALUE &&
        token != PROBE_TUPLE &&
        token != PROBE_GETLINE;
  }
}

static unsigned
atom_token(InputProbe *input, BoundaryState *state, bool lvalue) {
  unsigned token = lvalue ? SELECT_NAME : SELECT_ATOM;
  probe_restore(input, 0);
  TSLexer *probe = &input->lexer;
  if (is_word_start(probe->lookahead)) {
    const WordAtom atom = word_atom(probe, scan_word_spelling(probe));
    if (atom == ATOM_CALL) token = SELECT_CALL;
    if (atom == ATOM_SUBSCRIPT) token = SELECT_SUBSCRIPT;
  } else if (probe->lookahead == '(') token = SELECT_GROUP;
  if (
    token == SELECT_GROUP || token == SELECT_CALL || token == SELECT_SUBSCRIPT
  ) {
    probe->advance(probe, false);
    skip_token_layout(probe);
    state->pending[0] = probe->lookahead == ')' ? 0 : UNCONSTRAINED;
  }
  return token;
}

static unsigned selection_token(
  BoundaryOutline selected,
  InputProbe *input,
  BoundaryState *state
) {
  const uint32_t signature = descriptor_signature(selected.descriptor);
  switch (signature_kind(signature)) {
    case RECOGNIZED_GET:
      return selected.children[0] ? SELECT_TARGET_GET : SELECT_GET;
    case RECOGNIZED_FILE:
      return SELECT_FILE;
    case RECOGNIZED_FIELD:
      return SELECT_FIELD;
    case RECOGNIZED_PREFIX:
      return SELECT_PREFIX;
    case RECOGNIZED_POSTFIX:
      return SELECT_POSTFIX;
    case RECOGNIZED_CONCAT:
      return SELECT_CONCAT;
    case RECOGNIZED_TUPLE:
    case 0:
      return atom_token(input, state, signature & SIGNATURE_LVALUE);
    case RECOGNIZED_BRANCHES:
      return SELECT_BRANCHES;
    case '?':
      return SELECT_CONDITIONAL;
    case '|':
      return SELECT_PIPE;
    case '=':
    case PROBE_ASSIGNMENT:
      return SELECT_ASSIGNMENT;
    case PROBE_MEMBERSHIP:
      return descriptor_kind(selected.children[0]) == RECOGNIZED_TUPLE
        ? SELECT_TUPLE
        : SELECT_MEMBERSHIP;
    default:
      return SELECT_BINARY;
  }
}

static bool select_boundary(TSLexer *lexer, BoundaryState *state) {
  const uint64_t requested = state->active;
  const size_t count =
    requested == UNCONSTRAINED ? 0 : descriptor_length(requested);
  InputProbe input = probe_open(lexer);
  RecognizerInput parser = {
    .expected = requested == UNCONSTRAINED ? 0 : descriptor_signature(requested)
  };
  BoundaryOutline selected = {0};
  if (!take_boundary(state, requested, &selected)) {
    collect_expression(&input, &parser, count, state->print);
    if (
      parser.length &&
      (!count || parser.length == count) &&
      !input.failed &&
      !parser.failed &&
      recognizer_parse(&parser)
    ) {
      selected = parser.tree[parser.tree_root - 1].outline;
      cache_boundary(state, &parser);
      take_boundary(state, selected.descriptor, &selected);
    }
  }
  unsigned token = ERROR_SENTINEL;
  if (descriptor_length(selected.descriptor)) {
    state->unary = descriptor_signature(selected.descriptor) & SIGNATURE_UNARY;
    state->pending[0] = selected.children[0];
    state->pending[1] = selected.children[1];
    token = selection_token(selected, &input, state);
  }
  recognizer_destroy(&parser);
  ts_free(input.characters);
  state->active = 0;
  state->selected = token;
  return token != ERROR_SENTINEL;
}

static bool restoring_requested(const bool *valid) {
  bool restoring = valid[END_CHILD] || valid[RESUME];
  for (unsigned digit = 0; digit < SAVED_DIGITS; digit++)
    restoring |= valid[RESTORE_FIRST + digit];
  return restoring;
}

static bool root_requested(const bool *valid) {
  return valid[ROOT] ||
    valid[PRINT_ROOT] ||
    valid[FOR_IN] ||
    valid[PRINT_ARGUMENTS] ||
    valid[LOCAL_ROOT] ||
    valid[LOCAL_PRINT_ROOT];
}

static bool boundary_requested(const bool *valid) {
  for (unsigned i = ROOT; i <= LOCAL_PRINT_ROOT; i++)
    if (valid[i]) return true;
  return false;
}

#ifndef LOCAL_EXPRESSIONS
#define LOCAL_EXPRESSIONS 1
#endif

static bool local_expression(InputProbe *probe, bool print) {
  enum { GETLINE_SEEN = 1, AMBIGUOUS_SEEN = 2 };
  TSLexer *lexer = &probe->lexer;
  size_t depth = 0, questions = 0, capacity = 0;
  bool after_operand = false, separated = false, result = false;
  unsigned char flags = 0;
  unsigned char *scopes = NULL;
  for (;;) {
    if (!skip_scope_layout(lexer, separated)) goto done;
    const int32_t character = lexer->lookahead;
    if (
      lexer->eof(lexer) ||
      character == '\n' ||
      character_in(character, "{};") ||
      (!depth && character_in(character, ",)]")) ||
      (!depth && character == ':' && !questions) ||
      (print && !depth && !questions && character == '>')
    )
      break;
    separated = false;
    if (character == '"' || (character == '/' && !after_operand)) {
      if (!skip_literal(lexer)) goto done;
      after_operand = true;
      continue;
    }
    if (is_word_start(character)) {
      const WordKind word = scan_word_spelling(lexer);
      if (word == WORD_KEYWORD) break;
      if (word == WORD_GETLINE) flags |= GETLINE_SEEN;
      after_operand = word != WORD_MEMBERSHIP;
    } else if (is_ascii_digit(character) || character == '.') {
      probe_number(probe);
      after_operand = true;
    } else {
      lexer->advance(lexer, false);
      if (
        print &&
        !depth &&
        !questions &&
        character == '|' &&
        lexer->lookahead != '|'
      )
        break;
      if (character_in(character, "([")) {
        if (depth == capacity) {
          unsigned char *grown =
            grow_array(scopes, &capacity, sizeof(*scopes), 32);
          if (!grown) goto done;
          scopes = grown;
        }
        scopes[depth++] = flags;
        flags = 0;
        after_operand = false;
      } else if (character_in(character, ")]")) {
        flags = scopes[--depth];
        after_operand = true;
      } else if (
        character_in(character, "+-") && lexer->lookahead == character
      ) {
        lexer->advance(lexer, false);
        flags |= AMBIGUOUS_SEEN;
        after_operand = true;
      } else if (
        character_in(character, "&|") && lexer->lookahead == character
      ) {
        lexer->advance(lexer, false);
        separated = true;
        after_operand = false;
      } else {
        if (character == '|' || (character == '<' && lexer->lookahead != '='))
          flags |= AMBIGUOUS_SEEN;
        if (character == ',') flags = 0;
        after_operand = false;
        separated = character == ',';
        if (!depth && character == '?') questions++;
        if (!depth && character == ':' && questions) questions--;
      }
    }
    if (flags == (GETLINE_SEEN | AMBIGUOUS_SEEN)) goto done;
  }
  result = depth == 0 && LOCAL_EXPRESSIONS;
done:
  ts_free(scopes);
  return result;
}

static bool is_for_in_header(InputProbe *probe) {
  return probe_token(probe, true) == PROBE_NAME &&
    probe_token(probe, false) == PROBE_MEMBERSHIP &&
    probe_token(probe, true) == PROBE_NAME &&
    probe_token(probe, false) == ')';
}

static unsigned root_token(InputProbe *probe, const bool *valid) {
  if (valid[FOR_IN] && is_for_in_header(probe)) return FOR_IN;
  probe_restore(probe, 0);
  if (valid[PRINT_ARGUMENTS] && probe_print_arguments(probe))
    return PRINT_ARGUMENTS;
  const int32_t first = probe->lexer.lookahead;
  if (is_word_start(first)) {
    const WordKind word = scan_word_spelling(&probe->lexer);
    if (word == WORD_KEYWORD || word == WORD_MEMBERSHIP) return ERROR_SENTINEL;
    probe_restore(probe, 0);
  }
  const bool print = valid[PRINT_ROOT] || valid[LOCAL_PRINT_ROOT];
  const bool local = (valid[LOCAL_ROOT] || valid[LOCAL_PRINT_ROOT]) &&
    local_expression(probe, print) &&
    !probe->failed;
  if (
    (!valid[ROOT] && !valid[LOCAL_ROOT] && !print) ||
    (print && (first == '>' || first == '|')) ||
    !first ||
    character_in(first, "\n;,)]}{:#")
  )
    return ERROR_SENTINEL;
  if (local) return print ? LOCAL_PRINT_ROOT : LOCAL_ROOT;
  return valid[PRINT_ROOT] ? PRINT_ROOT : ROOT;
}

static bool scan_root(TSLexer *lexer, BoundaryState *state, const bool *valid) {
  InputProbe probe = probe_open(lexer);
  const unsigned token = root_token(&probe, valid);
  ts_free(probe.characters);
  if (token == LOCAL_ROOT || token == LOCAL_PRINT_ROOT)
    *state = (BoundaryState){0};
  else if (token == ROOT || token == PRINT_ROOT)
    *state = (BoundaryState){
      .active = UNCONSTRAINED,
      .print = token == PRINT_ROOT,
    };
  return token != ERROR_SENTINEL && boundary_emit(lexer, state, token);
}

static bool
scan_boundary(TSLexer *lexer, BoundaryState *state, const bool *valid) {
  lexer->mark_end(lexer);
  if ((valid[NEXT_ARGUMENT] || valid[BEGIN_CHILD]) && lexer->lookahead == '\n')
    return false;
  if (root_requested(valid)) return scan_root(lexer, state, valid);
  if (valid[NEXT_ARGUMENT]) {
    state->pending[0] = UNCONSTRAINED;
    return boundary_emit(lexer, state, NEXT_ARGUMENT);
  }
  if (valid[RESUME]) {
    state->pending[0] = restored_descriptor(state->carry);
    state->pending[1] = 0;
    state->carry = 0;
    state->phase = BOUNDARY_READY;
    return boundary_emit(lexer, state, RESUME);
  }
  if (valid[END_CHILD]) {
    boundary_reset(state, 0);
    return boundary_emit(lexer, state, END_CHILD);
  }
  for (unsigned digit = 0; digit < SAVED_DIGITS; digit++)
    if (valid[RESTORE_FIRST + digit]) {
      state->carry = state->carry * SAVED_DIGITS + digit;
      return boundary_emit(lexer, state, RESTORE_FIRST + digit);
    }
  if (valid[BEGIN_CHILD]) {
    if (state->phase != BOUNDARY_SAVING) {
      if (!state->pending[0]) return false;
      state->next = state->pending[0];
      state->carry = saved_descriptor(state->pending[1]);
      state->phase = BOUNDARY_SAVING;
    }
    if (state->carry) {
      const unsigned digit = state->carry % SAVED_DIGITS;
      state->carry /= SAVED_DIGITS;
      return boundary_emit(lexer, state, SAVE_FIRST + digit);
    }
    uint64_t active = state->next;
    boundary_reset(state, active);
    if (active == UNCONSTRAINED) cache_clear(state);
    return boundary_emit(lexer, state, BEGIN_CHILD);
  }
  if (state->active && !select_boundary(lexer, state)) return false;
  if (valid[CLASS_UNARY] || valid[CLASS_NON_UNARY])
    return boundary_emit(
      lexer,
      state,
      state->unary ? CLASS_UNARY : CLASS_NON_UNARY
    );
  const unsigned token = state->selected;
  if (token < SELECT_ATOM || token > SELECT_TUPLE || !valid[token])
    return false;
  state->selected = 0;
  return boundary_emit(lexer, state, token);
}

static bool scan_literal(
  ScannerState *state,
  TSLexer *lexer,
  const bool *valid_symbols,
  bool recovering
) {
  lexer->mark_end(lexer);
  if (lexer->lookahead == '\\') return scan_backslash(state, lexer);
  if (lexer->lookahead == '\n')
    return emit_literal_break(state, lexer, valid_symbols);
  if (is_ere_mode(state->mode))
    return scan_ere_context(state, lexer, valid_symbols, recovering);
  if (lexer->lookahead == '"' && valid_symbols[STRING_CLOSING])
    return emit_delimiter(state, lexer, LEXICAL_MODE_OUTSIDE, STRING_CLOSING);
  return skip_blanks_before_newline(lexer, NULL) &&
    emit_literal_break(state, lexer, valid_symbols);
}

static bool scan_outside(
  ScannerState *state,
  TSLexer *lexer,
  const bool *valid_symbols,
  bool recovering
) {
  if (!recovering && restoring_requested(valid_symbols))
    return scan_boundary(lexer, &state->boundary, valid_symbols);
  while (is_ascii_blank(lexer->lookahead)) lexer->advance(lexer, true);
  if (lexer->lookahead == '\\') return scan_backslash(state, lexer);
  if (lexer->lookahead == '#' && valid_symbols[COMMENT])
    return scan_comment(lexer);
  if (!recovering && boundary_requested(valid_symbols))
    return scan_boundary(lexer, &state->boundary, valid_symbols);
  if (lexer->lookahead == '"' && valid_symbols[STRING_OPENING])
    return emit_delimiter(state, lexer, LEXICAL_MODE_STRING, STRING_OPENING);
  if (is_word_start(lexer->lookahead))
    return scan_contextual_word(lexer, recovering);
  if (lexer->lookahead == '|')
    return scan_getline_pipe(lexer, valid_symbols, recovering);
  if (lexer->lookahead == '$' || lexer->lookahead == '!')
    return scan_prefix_operator(lexer, valid_symbols, recovering);
  if (recovering) return false;
  if (lexer->lookahead == '/')
    return scan_slash_start(state, lexer, valid_symbols);
  return scan_sign(lexer, valid_symbols);
}

void *tree_sitter_posix_awk_external_scanner_create(void) {
  return ts_calloc(1, sizeof(ScannerState));
}

void tree_sitter_posix_awk_external_scanner_destroy(void *payload) {
  ts_free(payload);
}

static void write_uint(char *buffer, uint64_t value, unsigned bytes) {
  for (unsigned i = 0; i < bytes; i++) buffer[i] = (char)(value >> (8 * i));
}

static uint64_t read_uint(const char *buffer, unsigned bytes) {
  uint64_t value = 0;
  for (unsigned i = 0; i < bytes; i++)
    value |= (uint64_t)(unsigned char)buffer[i] << (8 * i);
  return value;
}

unsigned
tree_sitter_posix_awk_external_scanner_serialize(void *payload, char *buffer) {
  ScannerState *scanner = payload;
  if (!scanner) return 0;
  BoundaryState *state = &scanner->boundary;
  const uint64_t values[] = {
    state->active,
    state->pending[0],
    state->pending[1],
    state->next,
    state->carry
  };
  buffer[0] = (char)scanner->mode;
  buffer[1] = (char)state->phase;
  buffer[2] = (char)state->selected;
  buffer[3] = (char)state->unary;
  buffer[4] = (char)state->print;
  for (size_t i = 0; i < ARRAY_LENGTH(values); i++)
    write_uint(buffer + STATE_VALUES_OFFSET + i * 8, values[i], 8);
  buffer[STATE_CACHE_COUNT_OFFSET] = (char)state->cache_count;
  buffer[STATE_CACHE_INDEX_OFFSET] = (char)state->cache_index;
  write_uint(buffer + STATE_CACHE_REPEATS_OFFSET, state->cache_repeats, 4);
  write_uint(buffer + STATE_CACHE_DELTA_OFFSET, state->cache_delta, 4);
  write_uint(buffer + STATE_DECISION_HASH_OFFSET, state->decision_hash, 8);
  for (unsigned i = 0; i < state->cache_count; i++) {
    const BoundaryOutline *node = &state->cache[i];
    char *entry = buffer + STATE_HEADER_BYTES + i * STATE_OUTLINE_BYTES;
    write_uint(entry, node->descriptor, 8);
    write_uint(entry + 8, node->children[0], 8);
    write_uint(entry + 16, node->children[1], 8);
    write_uint(entry + 24, node->repeat, 4);
  }
  return STATE_HEADER_BYTES + state->cache_count * STATE_OUTLINE_BYTES;
}

void tree_sitter_posix_awk_external_scanner_deserialize(
  void *payload,
  const char *buffer,
  unsigned length
) {
  ScannerState *scanner = payload;
  if (!scanner) return;
  *scanner = (ScannerState){0};
  if (
    length < STATE_HEADER_BYTES ||
    (unsigned char)buffer[0] > LEXICAL_MODE_CONTINUED_NEWLINE ||
    (unsigned char)buffer[1] > BOUNDARY_SAVING
  )
    return;
  const unsigned count = (unsigned char)buffer[STATE_CACHE_COUNT_OFFSET];
  if (
    count > BOUNDARY_CACHE_CAPACITY ||
    length != STATE_HEADER_BYTES + count * STATE_OUTLINE_BYTES
  )
    return;
  uint64_t values[5] = {0};
  for (size_t i = 0; i < ARRAY_LENGTH(values); i++)
    values[i] = read_uint(buffer + STATE_VALUES_OFFSET + i * 8, 8);
  scanner->mode = (LexicalMode)buffer[0];
  scanner->boundary = (BoundaryState){
    .active = values[0],
    .pending = {values[1], values[2]},
    .next = values[3],
    .carry = values[4],
    .phase = (enum BoundaryPhase)buffer[1],
    .selected = (unsigned char)buffer[2],
    .unary = buffer[3] != 0,
    .print = buffer[4] != 0,
    .decision_hash = read_uint(buffer + STATE_DECISION_HASH_OFFSET, 8),
    .cache_count = count,
    .cache_index = (unsigned char)buffer[STATE_CACHE_INDEX_OFFSET],
    .cache_repeats =
      (uint32_t)read_uint(buffer + STATE_CACHE_REPEATS_OFFSET, 4),
    .cache_delta = (uint32_t)read_uint(buffer + STATE_CACHE_DELTA_OFFSET, 4)
  };
  if (scanner->boundary.cache_index >= (count ? count : 1)) {
    *scanner = (ScannerState){0};
    return;
  }
  for (unsigned i = 0; i < count; i++) {
    const char *entry = buffer + STATE_HEADER_BYTES + i * STATE_OUTLINE_BYTES;
    scanner->boundary.cache[i] = (BoundaryOutline){
      .descriptor = read_uint(entry, 8),
      .children = {read_uint(entry + 8, 8), read_uint(entry + 16, 8)},
      .repeat = (uint32_t)read_uint(entry + 24, 4)
    };
  }
}

bool tree_sitter_posix_awk_external_scanner_scan(
  void *payload,
  TSLexer *lexer,
  const bool *valid_symbols
) {
  ScannerState *state = payload;
  if (!state) return false;
  const bool recovering = valid_symbols[ERROR_SENTINEL];
  switch (state->mode) {
    case LEXICAL_MODE_OUTSIDE:
      return scan_outside(state, lexer, valid_symbols, recovering);
    case LEXICAL_MODE_CONTINUED_NEWLINE:
      return lexer->lookahead == '\n' &&
        valid_symbols[CONTINUATION_NEWLINE] &&
        emit_delimiter(
          state,
          lexer,
          LEXICAL_MODE_OUTSIDE,
          CONTINUATION_NEWLINE
        );
    default:
      return scan_literal(state, lexer, valid_symbols, recovering);
  }
}
