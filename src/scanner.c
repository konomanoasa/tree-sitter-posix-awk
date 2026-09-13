#include "tree_sitter/alloc.h"
#include "tree_sitter/parser.h"

#include <stddef.h>
#include <string.h>

#define ARRAY_LENGTH(array) (sizeof(array) / sizeof((array)[0]))

enum TokenType {
  BEGIN_WORD,
  END_WORD,
  FUNCTION_WORD,
  PRINT_WORD,
  BREAK_WORD,
  CONTINUE_WORD,
  DELETE_WORD,
  DO_WORD,
  ELSE_WORD,
  EXIT_WORD,
  FOR_WORD,
  IF_WORD,
  NEXT_WORD,
  NEXTFILE_WORD,
  PRINTF_WORD,
  RETURN_WORD,
  WHILE_WORD,
  NAME_WORD,
  FOR_IN_VARIABLE_WORD,
  GETLINE_WORD,
  IN_WORD,
  BUILTIN_FUNC_NAME_WORD,
  BUILTIN_CALL_WORD,
  FUNC_NAME_WORD,
  NUMBER_INTEGER,
  NUMBER_FRACTION,
  NUMBER_EXPONENT,
  DIVISION_SLASH_GUARD,
  ERE_OPENING_SLASH_GUARD,
  DIV_ASSIGN_OPERATOR,
  ADD_ASSIGN_OPERATOR,
  SUB_ASSIGN_OPERATOR,
  MUL_ASSIGN_OPERATOR,
  MOD_ASSIGN_OPERATOR,
  POW_ASSIGN_OPERATOR,
  OR_OPERATOR,
  AND_OPERATOR,
  NO_MATCH_OPERATOR,
  EQ_OPERATOR,
  LE_OPERATOR,
  GE_OPERATOR,
  NE_OPERATOR,
  INCR_OPERATOR,
  DECR_OPERATOR,
  APPEND_OPERATOR,
  PLUS_OPERATOR,
  MINUS_OPERATOR,
  STAR_OPERATOR,
  PERCENT_OPERATOR,
  CARET_OPERATOR,
  BANG_OPERATOR,
  LESS_OPERATOR,
  GREATER_OPERATOR,
  EQUAL_OPERATOR,
  PIPE_OPERATOR,
  OUTPUT_GREATER_GUARD,
  ERE_COMPOUND_OPENING,
  ERE_DOT_CLOSING,
  ERE_EQUAL_CLOSING,
  ERE_COLON_CLOSING,
  ERE_BRACKET_LITERAL_OPEN,
  ERE_COMPOUND_CONTENT,
  ERE_CLOSING_HYPHEN,
  ERE_CLOSING,
  STRING_OPENING,
  STRING_END,
  STRING_CONTENT_GUARD,
  STRING_ESCAPE_GUARD,
  ERE_NAMED_ESCAPE_GUARD,
  ERE_QUOTED_ESCAPE_GUARD,
  ERE_OCTAL_ESCAPE_GUARD,
  ERE_UNDEFINED_ESCAPE_GUARD,
  ERE_ESCAPED_DELIMITER_GUARD,
  ERE_CLASS_NAME_GUARD,
  ERE_DUP_COUNT_GUARD,
  TOKEN_WHOLE,
  TOKEN_CONTENT,
  TOKEN_FINAL_CONTENT,
  TOKEN_LINE_CONTINUATION,
  COMMENT,
  LINE_CONTINUATION,
  ERROR_SENTINEL,
  TOKEN_TYPE_COUNT,
};

enum {
  MAX_RESERVED_WORD_LENGTH = 8,
  SERIALIZED_SCANNER_STATE_SIZE = 6,
};

typedef char SerializedScannerStateFitsTreeSitterBuffer
  [SERIALIZED_SCANNER_STATE_SIZE <= TREE_SITTER_SERIALIZATION_BUFFER_SIZE ? 1
                                                                          : -1];

typedef enum {
  WORD_KIND_NONE,
  WORD_KIND_BEGIN,
  WORD_KIND_END,
  WORD_KIND_FUNCTION,
  WORD_KIND_BREAK,
  WORD_KIND_CONTINUE,
  WORD_KIND_DELETE,
  WORD_KIND_DO,
  WORD_KIND_ELSE,
  WORD_KIND_EXIT,
  WORD_KIND_FOR,
  WORD_KIND_IF,
  WORD_KIND_NEXT,
  WORD_KIND_NEXTFILE,
  WORD_KIND_PRINT,
  WORD_KIND_PRINTF,
  WORD_KIND_RETURN,
  WORD_KIND_WHILE,
  WORD_KIND_GETLINE,
  WORD_KIND_IN,
  WORD_KIND_BUILTIN_FUNC_NAME,
  WORD_KIND_BUILTIN_CALL,
  WORD_KIND_FUNC_NAME,
  WORD_KIND_NAME,
  WORD_KIND_FOR_IN_VARIABLE,
} WordKind;

typedef struct {
  const char *spelling;
  WordKind kind;
} WordEntry;

typedef struct {
  WordKind kind;
  enum TokenType token;
} WordToken;

typedef struct {
  int32_t first;
  int32_t second;
  enum TokenType token;
} CompositeOperator;

typedef enum {
  NUMBER_KIND_NONE,
  NUMBER_KIND_INTEGER,
  NUMBER_KIND_FRACTION,
  NUMBER_KIND_EXPONENT,
} NumberKind;

typedef enum {
  LEXICAL_MODE_OUTSIDE,
  LEXICAL_MODE_ERE_BODY,
  LEXICAL_MODE_STRING,
} LexicalMode;

typedef enum {
  PAYLOAD_NONE,
  PAYLOAD_WHOLE,
  PAYLOAD_SPLIT,
} PayloadMode;

typedef struct {
  LexicalMode mode;
  PayloadMode payload;
  uint32_t remaining;
} ScannerState;

static const WordEntry WORDS[] = {
  {"BEGIN", WORD_KIND_BEGIN},
  {"break", WORD_KIND_BREAK},
  {"continue", WORD_KIND_CONTINUE},
  {"delete", WORD_KIND_DELETE},
  {"do", WORD_KIND_DO},
  {"else", WORD_KIND_ELSE},
  {"END", WORD_KIND_END},
  {"exit", WORD_KIND_EXIT},
  {"for", WORD_KIND_FOR},
  {"function", WORD_KIND_FUNCTION},
  {"getline", WORD_KIND_GETLINE},
  {"if", WORD_KIND_IF},
  {"in", WORD_KIND_IN},
  {"next", WORD_KIND_NEXT},
  {"nextfile", WORD_KIND_NEXTFILE},
  {"print", WORD_KIND_PRINT},
  {"printf", WORD_KIND_PRINTF},
  {"return", WORD_KIND_RETURN},
  {"while", WORD_KIND_WHILE},
  {"atan2", WORD_KIND_BUILTIN_FUNC_NAME},
  {"close", WORD_KIND_BUILTIN_FUNC_NAME},
  {"cos", WORD_KIND_BUILTIN_FUNC_NAME},
  {"exp", WORD_KIND_BUILTIN_FUNC_NAME},
  {"fflush", WORD_KIND_BUILTIN_FUNC_NAME},
  {"gsub", WORD_KIND_BUILTIN_FUNC_NAME},
  {"index", WORD_KIND_BUILTIN_FUNC_NAME},
  {"int", WORD_KIND_BUILTIN_FUNC_NAME},
  {"length", WORD_KIND_BUILTIN_FUNC_NAME},
  {"log", WORD_KIND_BUILTIN_FUNC_NAME},
  {"match", WORD_KIND_BUILTIN_FUNC_NAME},
  {"rand", WORD_KIND_BUILTIN_FUNC_NAME},
  {"sin", WORD_KIND_BUILTIN_FUNC_NAME},
  {"split", WORD_KIND_BUILTIN_FUNC_NAME},
  {"sprintf", WORD_KIND_BUILTIN_FUNC_NAME},
  {"sqrt", WORD_KIND_BUILTIN_FUNC_NAME},
  {"srand", WORD_KIND_BUILTIN_FUNC_NAME},
  {"sub", WORD_KIND_BUILTIN_FUNC_NAME},
  {"substr", WORD_KIND_BUILTIN_FUNC_NAME},
  {"system", WORD_KIND_BUILTIN_FUNC_NAME},
  {"tolower", WORD_KIND_BUILTIN_FUNC_NAME},
  {"toupper", WORD_KIND_BUILTIN_FUNC_NAME},
};

static const WordToken WORD_TOKENS[] = {
  {WORD_KIND_BEGIN, BEGIN_WORD},
  {WORD_KIND_END, END_WORD},
  {WORD_KIND_FUNCTION, FUNCTION_WORD},
  {WORD_KIND_PRINT, PRINT_WORD},
  {WORD_KIND_BREAK, BREAK_WORD},
  {WORD_KIND_CONTINUE, CONTINUE_WORD},
  {WORD_KIND_DELETE, DELETE_WORD},
  {WORD_KIND_DO, DO_WORD},
  {WORD_KIND_ELSE, ELSE_WORD},
  {WORD_KIND_EXIT, EXIT_WORD},
  {WORD_KIND_FOR, FOR_WORD},
  {WORD_KIND_IF, IF_WORD},
  {WORD_KIND_NEXT, NEXT_WORD},
  {WORD_KIND_NEXTFILE, NEXTFILE_WORD},
  {WORD_KIND_PRINTF, PRINTF_WORD},
  {WORD_KIND_RETURN, RETURN_WORD},
  {WORD_KIND_WHILE, WHILE_WORD},
  {WORD_KIND_NAME, NAME_WORD},
  {WORD_KIND_FOR_IN_VARIABLE, FOR_IN_VARIABLE_WORD},
  {WORD_KIND_GETLINE, GETLINE_WORD},
  {WORD_KIND_IN, IN_WORD},
  {WORD_KIND_BUILTIN_FUNC_NAME, BUILTIN_FUNC_NAME_WORD},
  {WORD_KIND_BUILTIN_CALL, BUILTIN_CALL_WORD},
  {WORD_KIND_FUNC_NAME, FUNC_NAME_WORD},
};

static const CompositeOperator COMPOSITE_OPERATORS[] = {
  {'/', '=', DIV_ASSIGN_OPERATOR},
  {'+', '=', ADD_ASSIGN_OPERATOR},
  {'-', '=', SUB_ASSIGN_OPERATOR},
  {'*', '=', MUL_ASSIGN_OPERATOR},
  {'%', '=', MOD_ASSIGN_OPERATOR},
  {'^', '=', POW_ASSIGN_OPERATOR},
  {'|', '|', OR_OPERATOR},
  {'&', '&', AND_OPERATOR},
  {'!', '~', NO_MATCH_OPERATOR},
  {'=', '=', EQ_OPERATOR},
  {'<', '=', LE_OPERATOR},
  {'>', '=', GE_OPERATOR},
  {'!', '=', NE_OPERATOR},
  {'+', '+', INCR_OPERATOR},
  {'-', '-', DECR_OPERATOR},
  {'>', '>', APPEND_OPERATOR},
};

typedef struct {
  int32_t character;
  enum TokenType token;
} SingleOperator;

static const SingleOperator SINGLE_OPERATORS[] = {
  {'+', PLUS_OPERATOR},
  {'-', MINUS_OPERATOR},
  {'*', STAR_OPERATOR},
  {'%', PERCENT_OPERATOR},
  {'^', CARET_OPERATOR},
  {'!', BANG_OPERATOR},
  {'<', LESS_OPERATOR},
  {'>', GREATER_OPERATOR},
  {'=', EQUAL_OPERATOR},
  {'|', PIPE_OPERATOR},
};

static bool is_ascii_blank(int32_t character) {
  return character == ' ' || character == '\t';
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

typedef struct {
  TSLexer *lexer;
  uint32_t offset;
  uint32_t end;
  uint32_t continuations;
  int32_t character;
  bool eof;
  bool overflow;
} LogicalCursor;

typedef struct {
  uint32_t length;
  uint32_t continuations;
} TokenSpan;

typedef struct {
  WordKind kind;
  TokenSpan span;
} ScannedWord;

static bool advance_logical_source(LogicalCursor *cursor) {
  if (cursor->offset == UINT32_MAX) {
    cursor->overflow = true;
    return false;
  }
  cursor->lexer->advance(cursor->lexer, false);
  cursor->offset++;
  return true;
}

static void logical_next(LogicalCursor *cursor) {
  TSLexer *lexer = cursor->lexer;
  for (;;) {
    if (lexer->eof(lexer) || cursor->overflow) {
      cursor->eof = true;
      cursor->character = 0;
      return;
    }
    const int32_t character = lexer->lookahead;
    if (!advance_logical_source(cursor)) {
      cursor->eof = true;
      return;
    }
    if (character == '\\' && lexer->lookahead == '\n') {
      if (!advance_logical_source(cursor)) {
        cursor->eof = true;
        return;
      }
      cursor->continuations++;
      continue;
    }
    cursor->character = character;
    cursor->end = cursor->offset;
    cursor->eof = false;
    return;
  }
}

static TokenSpan logical_span(const LogicalCursor *cursor) {
  return (TokenSpan){cursor->end, cursor->continuations};
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

static bool begin_payload(
  ScannerState *state,
  TSLexer *lexer,
  const bool *valid_symbols,
  enum TokenType token,
  TokenSpan span
) {
  if (span.length == 0 || !valid_symbols[token]) {
    return false;
  }
  state->payload = span.continuations == 0 ? PAYLOAD_WHOLE : PAYLOAD_SPLIT;
  state->remaining = span.length;
  return emit(lexer, token);
}

static bool
scan_payload(ScannerState *state, TSLexer *lexer, const bool *valid_symbols) {
  uint32_t consumed = 0;
  enum TokenType token = TOKEN_WHOLE;
  lexer->mark_end(lexer);
  if (state->payload == PAYLOAD_WHOLE) {
    while (consumed < state->remaining && !lexer->eof(lexer)) {
      lexer->advance(lexer, false);
      consumed++;
    }
    if (consumed != state->remaining) {
      return false;
    }
    lexer->mark_end(lexer);
  } else {
    while (consumed < state->remaining && !lexer->eof(lexer)) {
      const int32_t character = lexer->lookahead;
      lexer->advance(lexer, false);
      if (character == '\\' && lexer->lookahead == '\n') {
        if (consumed == 0 && state->remaining >= 2) {
          lexer->advance(lexer, false);
          consumed = 2;
          lexer->mark_end(lexer);
          token = TOKEN_LINE_CONTINUATION;
        }
        break;
      }
      consumed++;
      lexer->mark_end(lexer);
    }
    if (token != TOKEN_LINE_CONTINUATION) {
      token =
        consumed == state->remaining ? TOKEN_FINAL_CONTENT : TOKEN_CONTENT;
    }
  }
  if (consumed == 0 || !valid_symbols[token]) {
    return false;
  }
  state->remaining -= consumed;
  if (state->remaining == 0) {
    state->payload = PAYLOAD_NONE;
  }
  return emit(lexer, token);
}

static WordKind classify_word(const char *word, size_t length) {
  if (length > MAX_RESERVED_WORD_LENGTH) {
    return WORD_KIND_NAME;
  }
  for (size_t i = 0; i < ARRAY_LENGTH(WORDS); i++) {
    if (strcmp(WORDS[i].spelling, word) == 0) {
      return WORDS[i].kind;
    }
  }
  return WORD_KIND_NAME;
}

static ScannedWord scan_word_spelling(LogicalCursor *cursor) {
  ScannedWord word = {0};
  char spelling[MAX_RESERVED_WORD_LENGTH + 1] = {0};
  size_t length = 0;
  if (cursor->eof || !is_word_start(cursor->character)) {
    return word;
  }
  do {
    if (length < MAX_RESERVED_WORD_LENGTH) {
      spelling[length] = (char)cursor->character;
    }
    if (length <= MAX_RESERVED_WORD_LENGTH) {
      length++;
    }
    word.span = logical_span(cursor);
    logical_next(cursor);
  } while (!cursor->eof && is_word_continue(cursor->character));
  word.kind = classify_word(spelling, length);
  return word;
}

static void logical_skip_blanks(LogicalCursor *cursor) {
  while (!cursor->eof && is_ascii_blank(cursor->character)) {
    logical_next(cursor);
  }
}

static bool scan_for_in_shape(LogicalCursor *cursor) {
  logical_skip_blanks(cursor);
  if (scan_word_spelling(cursor).kind != WORD_KIND_IN) {
    return false;
  }
  logical_skip_blanks(cursor);
  if (scan_word_spelling(cursor).kind != WORD_KIND_NAME) {
    return false;
  }
  logical_skip_blanks(cursor);
  return !cursor->eof && cursor->character == ')';
}

static WordKind promote_word_kind(
  LogicalCursor *cursor,
  const bool *valid_symbols,
  WordKind kind
) {
  switch (kind) {
  case WORD_KIND_NAME:
    if (!cursor->eof && cursor->character == '(') {
      return WORD_KIND_FUNC_NAME;
    }
    if (valid_symbols[FOR_IN_VARIABLE_WORD] && scan_for_in_shape(cursor)) {
      return WORD_KIND_FOR_IN_VARIABLE;
    }
    return kind;
  case WORD_KIND_BUILTIN_FUNC_NAME:
    logical_skip_blanks(cursor);
    return !cursor->eof && cursor->character == '(' ? WORD_KIND_BUILTIN_CALL
                                                    : kind;
  default:
    return kind;
  }
}

static const WordToken *find_word_token(WordKind kind) {
  for (size_t i = 0; i < ARRAY_LENGTH(WORD_TOKENS); i++) {
    if (WORD_TOKENS[i].kind == kind) {
      return &WORD_TOKENS[i];
    }
  }
  return NULL;
}

static bool has_word_token(const bool *valid_symbols) {
  for (size_t i = 0; i < ARRAY_LENGTH(WORD_TOKENS); i++) {
    if (valid_symbols[WORD_TOKENS[i].token]) {
      return true;
    }
  }
  return false;
}

static bool scan_word_token(
  ScannerState *state,
  TSLexer *lexer,
  const bool *valid_symbols
) {
  LogicalCursor cursor = {.lexer = lexer};
  logical_next(&cursor);
  const ScannedWord word = scan_word_spelling(&cursor);
  const WordKind kind = promote_word_kind(&cursor, valid_symbols, word.kind);
  const WordToken *token = find_word_token(kind);
  return !cursor.overflow &&
    token !=
    NULL &&
    begin_payload(state, lexer, valid_symbols, token->token, word.span);
}

static enum TokenType number_kind_token(NumberKind kind) {
  switch (kind) {
  case NUMBER_KIND_FRACTION:
    return NUMBER_FRACTION;
  case NUMBER_KIND_EXPONENT:
    return NUMBER_EXPONENT;
  default:
    return NUMBER_INTEGER;
  }
}

static NumberKind scan_number_kind(LogicalCursor *cursor, TokenSpan *span) {
  NumberKind kind = NUMBER_KIND_NONE;
  while (!cursor->eof && is_ascii_digit(cursor->character)) {
    kind = NUMBER_KIND_INTEGER;
    *span = logical_span(cursor);
    logical_next(cursor);
  }
  if (!cursor->eof && cursor->character == '.') {
    if (kind != NUMBER_KIND_NONE) {
      kind = NUMBER_KIND_FRACTION;
      *span = logical_span(cursor);
    }
    logical_next(cursor);
    while (!cursor->eof && is_ascii_digit(cursor->character)) {
      kind = NUMBER_KIND_FRACTION;
      *span = logical_span(cursor);
      logical_next(cursor);
    }
  }
  if (kind == NUMBER_KIND_NONE) {
    return kind;
  }
  if (!cursor->eof && (cursor->character == 'e' || cursor->character == 'E')) {
    logical_next(cursor);
    if (
      !cursor->eof && (cursor->character == '+' || cursor->character == '-')
    ) {
      logical_next(cursor);
    }
    if (cursor->eof || !is_ascii_digit(cursor->character)) {
      return kind;
    }
    do {
      kind = NUMBER_KIND_EXPONENT;
      *span = logical_span(cursor);
      logical_next(cursor);
    } while (!cursor->eof && is_ascii_digit(cursor->character));
  }
  if (
    !cursor->eof &&
    kind !=
    NUMBER_KIND_INTEGER &&
    (cursor->character ==
      'f' ||
      cursor->character ==
      'F' ||
      cursor->character ==
      'l' ||
      cursor->character == 'L')
  ) {
    *span = logical_span(cursor);
  }
  return kind;
}

static bool has_number_token(const bool *valid_symbols) {
  return valid_symbols[NUMBER_INTEGER] ||
    valid_symbols[NUMBER_FRACTION] ||
    valid_symbols[NUMBER_EXPONENT];
}

static bool scan_number_token(
  ScannerState *state,
  TSLexer *lexer,
  const bool *valid_symbols
) {
  LogicalCursor cursor = {.lexer = lexer};
  TokenSpan span = {0};
  logical_next(&cursor);
  const NumberKind kind = scan_number_kind(&cursor, &span);
  return kind !=
    NUMBER_KIND_NONE &&
    !cursor.overflow &&
    begin_payload(state, lexer, valid_symbols, number_kind_token(kind), span);
}

static const CompositeOperator *
find_composite_operator(int32_t first, int32_t second) {
  for (size_t i = 0; i < ARRAY_LENGTH(COMPOSITE_OPERATORS); i++) {
    if (
      COMPOSITE_OPERATORS[i].first ==
      first &&
      COMPOSITE_OPERATORS[i].second == second
    ) {
      return &COMPOSITE_OPERATORS[i];
    }
  }
  return NULL;
}

static const SingleOperator *find_single_operator(int32_t character) {
  for (size_t i = 0; i < ARRAY_LENGTH(SINGLE_OPERATORS); i++) {
    if (SINGLE_OPERATORS[i].character == character) {
      return &SINGLE_OPERATORS[i];
    }
  }
  return NULL;
}

static bool
has_valid_composite_operator_start(int32_t first, const bool *valid_symbols) {
  for (size_t i = 0; i < ARRAY_LENGTH(COMPOSITE_OPERATORS); i++) {
    if (
      COMPOSITE_OPERATORS[i].first ==
      first &&
      valid_symbols[COMPOSITE_OPERATORS[i].token]
    ) {
      return true;
    }
  }
  return false;
}

static bool scan_composite_operator_start(
  ScannerState *state,
  TSLexer *lexer,
  const bool *valid_symbols
) {
  const int32_t first = lexer->lookahead;
  LogicalCursor cursor = {.lexer = lexer};
  logical_next(&cursor);
  logical_next(&cursor);
  const CompositeOperator *composite =
    cursor.eof ? NULL : find_composite_operator(first, cursor.character);
  if (cursor.overflow) {
    return false;
  }
  if (composite != NULL) {
    return begin_payload(
      state,
      lexer,
      valid_symbols,
      composite->token,
      logical_span(&cursor)
    );
  }
  const SingleOperator *single = find_single_operator(first);
  return single !=
    NULL &&
    begin_payload(
      state,
      lexer,
      valid_symbols,
      single->token,
      (TokenSpan){1, 0}
    );
}

static bool scan_greater_start(
  ScannerState *state,
  TSLexer *lexer,
  const bool *valid_symbols
) {
  LogicalCursor cursor = {.lexer = lexer};
  logical_next(&cursor);
  logical_next(&cursor);
  if (!cursor.eof && cursor.character == '=') {
    return begin_payload(
      state,
      lexer,
      valid_symbols,
      GE_OPERATOR,
      logical_span(&cursor)
    );
  }
  if (
    !cursor.eof && cursor.character == '>' && valid_symbols[APPEND_OPERATOR]
  ) {
    return begin_payload(
      state,
      lexer,
      valid_symbols,
      APPEND_OPERATOR,
      logical_span(&cursor)
    );
  }
  if (valid_symbols[OUTPUT_GREATER_GUARD]) {
    return emit(lexer, OUTPUT_GREATER_GUARD);
  }
  return begin_payload(
    state,
    lexer,
    valid_symbols,
    GREATER_OPERATOR,
    (TokenSpan){1, 0}
  );
}

static bool scan_slash_start(
  ScannerState *state,
  TSLexer *lexer,
  const bool *valid_symbols
) {
  LogicalCursor cursor = {.lexer = lexer};
  logical_next(&cursor);
  logical_next(&cursor);
  if (!cursor.eof && cursor.character == '=') {
    if (valid_symbols[DIV_ASSIGN_OPERATOR]) {
      return begin_payload(
        state,
        lexer,
        valid_symbols,
        DIV_ASSIGN_OPERATOR,
        logical_span(&cursor)
      );
    }
    if (valid_symbols[DIVISION_SLASH_GUARD]) {
      return false;
    }
  }
  if (valid_symbols[DIVISION_SLASH_GUARD]) {
    return begin_payload(
      state,
      lexer,
      valid_symbols,
      DIVISION_SLASH_GUARD,
      (TokenSpan){1, 0}
    );
  }
  if (
    begin_payload(
      state,
      lexer,
      valid_symbols,
      ERE_OPENING_SLASH_GUARD,
      (TokenSpan){1, 0}
    )
  ) {
    state->mode = LEXICAL_MODE_ERE_BODY;
    return true;
  }
  return false;
}

static bool is_octal_digit(int32_t character) {
  return character >= '0' && character <= '7';
}

static bool character_in(int32_t character, const char *characters) {
  return character >
    0 &&
    character <=
    127 &&
    strchr(characters, (char)character) != NULL;
}

static bool
scan_escape(ScannerState *state, TSLexer *lexer, const bool *valid_symbols) {
  LogicalCursor cursor = {.lexer = lexer, .offset = 1};
  logical_next(&cursor);
  if (cursor.eof || cursor.character == '\n') {
    return false;
  }
  const int32_t character = cursor.character;
  TokenSpan span = logical_span(&cursor);
  if (is_octal_digit(character)) {
    for (unsigned count = 1; count < 3; count++) {
      logical_next(&cursor);
      if (cursor.eof || !is_octal_digit(cursor.character)) {
        break;
      }
      span = logical_span(&cursor);
    }
  }
  enum TokenType token = STRING_ESCAPE_GUARD;
  if (state->mode == LEXICAL_MODE_ERE_BODY) {
    if (character == '/') {
      token = ERE_ESCAPED_DELIMITER_GUARD;
    } else if (is_octal_digit(character)) {
      token = ERE_OCTAL_ESCAPE_GUARD;
    } else if (character_in(character, "abfnrtv")) {
      token = ERE_NAMED_ESCAPE_GUARD;
    } else if (character_in(character, "().*+?{}|^$[\\]")) {
      token = ERE_QUOTED_ESCAPE_GUARD;
    } else {
      token = ERE_UNDEFINED_ESCAPE_GUARD;
    }
  }
  return !cursor.overflow &&
    begin_payload(state, lexer, valid_symbols, token, span);
}

static bool scan_backslash(
  ScannerState *state,
  TSLexer *lexer,
  const bool *valid_symbols,
  bool recovering
) {
  lexer->advance(lexer, false);
  if (lexer->lookahead == '\n') {
    if (!valid_symbols[LINE_CONTINUATION]) {
      return false;
    }
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    return emit(lexer, LINE_CONTINUATION);
  }
  return !recovering &&
    state->mode !=
    LEXICAL_MODE_OUTSIDE &&
    scan_escape(state, lexer, valid_symbols);
}

static bool scan_string_content(
  ScannerState *state,
  TSLexer *lexer,
  const bool *valid_symbols
) {
  LogicalCursor cursor = {.lexer = lexer};
  TokenSpan span = {0};
  logical_next(&cursor);
  while (
    !cursor.eof &&
    cursor.character !=
    '"' &&
    cursor.character !=
    '\\' &&
    cursor.character != '\n'
  ) {
    span = logical_span(&cursor);
    logical_next(&cursor);
  }
  return !cursor.overflow &&
    begin_payload(state, lexer, valid_symbols, STRING_CONTENT_GUARD, span);
}

static bool scan_ere_spelling(
  ScannerState *state,
  TSLexer *lexer,
  const bool *valid_symbols,
  enum TokenType token
) {
  LogicalCursor cursor = {.lexer = lexer};
  TokenSpan span = {0};
  logical_next(&cursor);
  if (
    cursor.eof ||
    (token == ERE_CLASS_NAME_GUARD && !is_ascii_letter(cursor.character))
  ) {
    return false;
  }
  while (
    !cursor.eof &&
    (is_ascii_digit(cursor.character) ||
      (token == ERE_CLASS_NAME_GUARD && is_ascii_letter(cursor.character)))
  ) {
    span = logical_span(&cursor);
    logical_next(&cursor);
  }
  return !cursor.overflow &&
    begin_payload(state, lexer, valid_symbols, token, span);
}

typedef struct {
  enum TokenType delimiter;
  enum TokenType content;
  int32_t first;
} EreCompoundToken;

static const EreCompoundToken ERE_COMPOUND_TOKENS[] = {
  {ERE_COMPOUND_OPENING, ERE_BRACKET_LITERAL_OPEN, '['},
  {ERE_DOT_CLOSING, ERE_COMPOUND_CONTENT, '.'},
  {ERE_EQUAL_CLOSING, ERE_COMPOUND_CONTENT, '='},
  {ERE_COLON_CLOSING, ERE_COMPOUND_CONTENT, ':'},
};

static bool scan_ere_compound_token(
  TSLexer *lexer,
  const EreCompoundToken *token,
  const bool *valid_symbols
) {
  lexer->advance(lexer, false);
  lexer->mark_end(lexer);
  LogicalCursor cursor = {.lexer = lexer, .offset = 1};
  logical_next(&cursor);
  const bool matches = !cursor.eof &&
    (token->delimiter == ERE_COMPOUND_OPENING
        ? character_in(cursor.character, ".=:")
        : cursor.character == ']');
  if (matches && valid_symbols[token->delimiter]) {
    return emit(lexer, token->delimiter);
  }
  return valid_symbols[token->content] && emit(lexer, token->content);
}

static bool scan_ere_context(
  ScannerState *state,
  TSLexer *lexer,
  const bool *valid_symbols,
  bool recovering
) {
  if (!recovering) {
    for (size_t i = 0; i < ARRAY_LENGTH(ERE_COMPOUND_TOKENS); i++) {
      const EreCompoundToken *token = &ERE_COMPOUND_TOKENS[i];
      if (
        lexer->lookahead ==
        token->first &&
        (valid_symbols[token->delimiter] || valid_symbols[token->content])
      ) {
        return scan_ere_compound_token(lexer, token, valid_symbols);
      }
    }
    if (
      valid_symbols[ERE_CLASS_NAME_GUARD] && is_ascii_letter(lexer->lookahead)
    ) {
      return scan_ere_spelling(
        state,
        lexer,
        valid_symbols,
        ERE_CLASS_NAME_GUARD
      );
    }
    if (
      valid_symbols[ERE_DUP_COUNT_GUARD] && is_ascii_digit(lexer->lookahead)
    ) {
      return scan_ere_spelling(
        state,
        lexer,
        valid_symbols,
        ERE_DUP_COUNT_GUARD
      );
    }
  }
  if (lexer->lookahead == '/' && valid_symbols[ERE_CLOSING]) {
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    return emit_mode(state, lexer, LEXICAL_MODE_OUTSIDE, ERE_CLOSING);
  }
  if (lexer->lookahead == '-' && valid_symbols[ERE_CLOSING_HYPHEN]) {
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    LogicalCursor cursor = {.lexer = lexer, .offset = 1};
    logical_next(&cursor);
    return !cursor.eof &&
      cursor.character ==
      ']' &&
      emit(lexer, ERE_CLOSING_HYPHEN);
  }
  return false;
}

static bool scan_comment(TSLexer *lexer) {
  do {
    lexer->advance(lexer, false);
  } while (lexer->lookahead != '\n' && !lexer->eof(lexer));
  lexer->mark_end(lexer);
  return emit(lexer, COMMENT);
}

void *tree_sitter_posix_awk_external_scanner_create(void) {
  return ts_calloc(1, sizeof(ScannerState));
}

void tree_sitter_posix_awk_external_scanner_destroy(void *payload) {
  ts_free(payload);
}

unsigned
tree_sitter_posix_awk_external_scanner_serialize(void *payload, char *buffer) {
  const ScannerState *state = payload;
  if (state->mode == LEXICAL_MODE_OUTSIDE && state->payload == PAYLOAD_NONE) {
    return 0;
  }
  buffer[0] = (char)state->mode;
  buffer[1] = (char)state->payload;
  for (unsigned i = 0; i < 4; i++) {
    buffer[2 + i] = (char)(state->remaining >> (i * 8));
  }
  return SERIALIZED_SCANNER_STATE_SIZE;
}

void tree_sitter_posix_awk_external_scanner_deserialize(
  void *payload,
  const char *buffer,
  unsigned length
) {
  ScannerState *state = payload;
  *state = (ScannerState){0};
  if (length != SERIALIZED_SCANNER_STATE_SIZE) {
    return;
  }
  const unsigned char mode = (unsigned char)buffer[0];
  const unsigned char payload_mode = (unsigned char)buffer[1];
  if (mode > LEXICAL_MODE_STRING || payload_mode > PAYLOAD_SPLIT) {
    return;
  }
  uint32_t remaining = 0;
  for (unsigned i = 0; i < 4; i++) {
    remaining |= (uint32_t)(unsigned char)buffer[2 + i] << (i * 8);
  }
  if ((payload_mode == PAYLOAD_NONE) != (remaining == 0)) {
    return;
  }
  state->mode = (LexicalMode)mode;
  state->payload = (PayloadMode)payload_mode;
  state->remaining = remaining;
}

bool tree_sitter_posix_awk_external_scanner_scan(
  void *payload,
  TSLexer *lexer,
  const bool *valid_symbols
) {
  ScannerState *state = payload;
  if (state->payload != PAYLOAD_NONE) {
    return scan_payload(state, lexer, valid_symbols);
  }
  const bool recovering = valid_symbols[ERROR_SENTINEL];
  if (state->mode == LEXICAL_MODE_OUTSIDE) {
    while (is_ascii_blank(lexer->lookahead)) {
      lexer->advance(lexer, true);
    }
  }
  lexer->mark_end(lexer);
  if (lexer->lookahead == '\\') {
    return scan_backslash(state, lexer, valid_symbols, recovering);
  }
  if (state->mode == LEXICAL_MODE_ERE_BODY) {
    return scan_ere_context(state, lexer, valid_symbols, recovering);
  }
  if (state->mode == LEXICAL_MODE_STRING) {
    if (
      (
        lexer->lookahead == '"' || lexer->lookahead == '\n' || lexer->eof(lexer)
      ) &&
      valid_symbols[STRING_END]
    ) {
      return emit_mode(state, lexer, LEXICAL_MODE_OUTSIDE, STRING_END);
    }
    return !recovering &&
      valid_symbols[STRING_CONTENT_GUARD] &&
      scan_string_content(state, lexer, valid_symbols);
  }
  if (lexer->lookahead == '#' && valid_symbols[COMMENT]) {
    return scan_comment(lexer);
  }
  if (lexer->lookahead == '"' && valid_symbols[STRING_OPENING]) {
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    return emit_mode(state, lexer, LEXICAL_MODE_STRING, STRING_OPENING);
  }
  if (recovering) {
    return false;
  }
  if (is_word_start(lexer->lookahead) && has_word_token(valid_symbols)) {
    return scan_word_token(state, lexer, valid_symbols);
  }
  if (
    (is_ascii_digit(lexer->lookahead) || lexer->lookahead == '.') &&
    has_number_token(valid_symbols)
  ) {
    return scan_number_token(state, lexer, valid_symbols);
  }
  if (
    lexer->lookahead ==
    '/' &&
    (valid_symbols[DIVISION_SLASH_GUARD] ||
      valid_symbols[ERE_OPENING_SLASH_GUARD] ||
      valid_symbols[DIV_ASSIGN_OPERATOR])
  ) {
    return scan_slash_start(state, lexer, valid_symbols);
  }
  if (
    lexer->lookahead ==
    '>' &&
    (valid_symbols[GE_OPERATOR] ||
      valid_symbols[GREATER_OPERATOR] ||
      valid_symbols[APPEND_OPERATOR] ||
      valid_symbols[OUTPUT_GREATER_GUARD])
  ) {
    return scan_greater_start(state, lexer, valid_symbols);
  }
  const SingleOperator *single = find_single_operator(lexer->lookahead);
  if (
    (single != NULL && valid_symbols[single->token]) ||
    has_valid_composite_operator_start(lexer->lookahead, valid_symbols)
  ) {
    return scan_composite_operator_start(state, lexer, valid_symbols);
  }
  return false;
}
