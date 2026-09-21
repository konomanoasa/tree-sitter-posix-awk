#include "tree_sitter/alloc.h"
#include "tree_sitter/parser.h"

#include <stddef.h>
#include <string.h>

#define ARRAY_LENGTH(array) (sizeof(array) / sizeof((array)[0]))

enum TokenType {
  BEGIN_KEYWORD,
  END_KEYWORD,
  FUNCTION_KEYWORD,
  PRINT_KEYWORD,
  BREAK_KEYWORD,
  CONTINUE_KEYWORD,
  DELETE_KEYWORD,
  DO_KEYWORD,
  ELSE_KEYWORD,
  EXIT_KEYWORD,
  FOR_KEYWORD,
  IF_KEYWORD,
  NEXT_KEYWORD,
  NEXTFILE_KEYWORD,
  PRINTF_KEYWORD,
  RETURN_KEYWORD,
  WHILE_KEYWORD,
  NAME_WORD,
  FOR_IN_VARIABLE_WORD,
  GETLINE_WORD,
  IN_WORD,
  BUILTIN_FUNC_NAME_WORD,
  BUILTIN_CALL_WORD,
  FUNC_NAME_WORD,
  NUMBER,
  DIVISION_SLASH,
  ERE_OPENING_SLASH,
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
  OUTPUT_GREATER,
  ERE_COMPOUND_OPENING,
  ERE_DOT_CLOSING,
  ERE_EQUAL_CLOSING,
  ERE_COLON_CLOSING,
  ERE_BRACKET_LITERAL_OPEN,
  ERE_COMPOUND_CONTENT,
  ERE_CLOSING_HYPHEN,
  ERE_CLOSING,
  STRING_OPENING,
  STRING_CLOSING,
  STRING_CONTENT,
  STRING_ESCAPE,
  ERE_NAMED_ESCAPE,
  ERE_QUOTED_ESCAPE,
  ERE_OCTAL_ESCAPE,
  ERE_UNDEFINED_ESCAPE,
  ERE_ESCAPED_DELIMITER,
  ERE_CLASS_NAME,
  ERE_DUP_COUNT,
  COMMENT,
  CONTINUATION_BACKSLASH,
  CONTINUATION_NEWLINE,
  STRAY_BACKSLASH,
  ERROR_SENTINEL,
  TOKEN_TYPE_COUNT,
};

enum {
  MAX_RESERVED_WORD_LENGTH = 8,
  SERIALIZED_SCANNER_STATE_SIZE = 1,
};

typedef char SerializedScannerStateFitsTreeSitterBuffer
  [SERIALIZED_SCANNER_STATE_SIZE <= TREE_SITTER_SERIALIZATION_BUFFER_SIZE ? 1
                                                                          : -1];

typedef struct {
  const char *spelling;
  enum TokenType token;
} ReservedWord;

typedef struct {
  int32_t first;
  int32_t second;
  enum TokenType token;
} CompositeOperator;

typedef enum {
  LEXICAL_MODE_OUTSIDE,
  LEXICAL_MODE_ERE_BODY,
  LEXICAL_MODE_ERE_COLLATING,
  LEXICAL_MODE_ERE_EQUIVALENCE,
  LEXICAL_MODE_ERE_CLASS,
  LEXICAL_MODE_STRING,
  LEXICAL_MODE_CONTINUED_NEWLINE,
} LexicalMode;

typedef struct {
  LexicalMode mode;
} ScannerState;

static const ReservedWord RESERVED_WORDS[] = {
  {"BEGIN", BEGIN_KEYWORD},
  {"break", BREAK_KEYWORD},
  {"continue", CONTINUE_KEYWORD},
  {"delete", DELETE_KEYWORD},
  {"do", DO_KEYWORD},
  {"else", ELSE_KEYWORD},
  {"END", END_KEYWORD},
  {"exit", EXIT_KEYWORD},
  {"for", FOR_KEYWORD},
  {"function", FUNCTION_KEYWORD},
  {"getline", GETLINE_WORD},
  {"if", IF_KEYWORD},
  {"in", IN_WORD},
  {"next", NEXT_KEYWORD},
  {"nextfile", NEXTFILE_KEYWORD},
  {"print", PRINT_KEYWORD},
  {"printf", PRINTF_KEYWORD},
  {"return", RETURN_KEYWORD},
  {"while", WHILE_KEYWORD},
  {"atan2", BUILTIN_FUNC_NAME_WORD},
  {"close", BUILTIN_FUNC_NAME_WORD},
  {"cos", BUILTIN_FUNC_NAME_WORD},
  {"exp", BUILTIN_FUNC_NAME_WORD},
  {"fflush", BUILTIN_FUNC_NAME_WORD},
  {"gsub", BUILTIN_FUNC_NAME_WORD},
  {"index", BUILTIN_FUNC_NAME_WORD},
  {"int", BUILTIN_FUNC_NAME_WORD},
  {"length", BUILTIN_FUNC_NAME_WORD},
  {"log", BUILTIN_FUNC_NAME_WORD},
  {"match", BUILTIN_FUNC_NAME_WORD},
  {"rand", BUILTIN_FUNC_NAME_WORD},
  {"sin", BUILTIN_FUNC_NAME_WORD},
  {"split", BUILTIN_FUNC_NAME_WORD},
  {"sprintf", BUILTIN_FUNC_NAME_WORD},
  {"sqrt", BUILTIN_FUNC_NAME_WORD},
  {"srand", BUILTIN_FUNC_NAME_WORD},
  {"sub", BUILTIN_FUNC_NAME_WORD},
  {"substr", BUILTIN_FUNC_NAME_WORD},
  {"system", BUILTIN_FUNC_NAME_WORD},
  {"tolower", BUILTIN_FUNC_NAME_WORD},
  {"toupper", BUILTIN_FUNC_NAME_WORD},
};

static const CompositeOperator COMPOSITE_OPERATORS[] = {
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

// Redirection precedes comparison where the parser accepts both.
static const SingleOperator SINGLE_OPERATORS[] = {
  {'+', PLUS_OPERATOR},
  {'-', MINUS_OPERATOR},
  {'*', STAR_OPERATOR},
  {'%', PERCENT_OPERATOR},
  {'^', CARET_OPERATOR},
  {'!', BANG_OPERATOR},
  {'<', LESS_OPERATOR},
  {'>', OUTPUT_GREATER},
  {'>', GREATER_OPERATOR},
  {'=', EQUAL_OPERATOR},
  {'|', PIPE_OPERATOR},
};

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

static enum TokenType classify_word(const char *word, size_t length) {
  if (length > MAX_RESERVED_WORD_LENGTH) {
    return NAME_WORD;
  }
  for (size_t i = 0; i < ARRAY_LENGTH(RESERVED_WORDS); i++) {
    if (strcmp(RESERVED_WORDS[i].spelling, word) == 0) {
      return RESERVED_WORDS[i].token;
    }
  }
  return NAME_WORD;
}

static enum TokenType scan_word_spelling(TSLexer *lexer) {
  char spelling[MAX_RESERVED_WORD_LENGTH + 1] = {0};
  size_t length = 0;
  if (!is_word_start(lexer->lookahead)) {
    return TOKEN_TYPE_COUNT;
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

static bool scan_for_in_shape(TSLexer *lexer) {
  if (!skip_token_layout(lexer) || scan_word_spelling(lexer) != IN_WORD) {
    return false;
  }
  if (!skip_token_layout(lexer) || scan_word_spelling(lexer) != NAME_WORD) {
    return false;
  }
  return skip_token_layout(lexer) && lexer->lookahead == ')';
}

static enum TokenType
promote_word(TSLexer *lexer, const bool *valid_symbols, enum TokenType token) {
  switch (token) {
  case NAME_WORD:
    if (lexer->lookahead == '(') {
      return FUNC_NAME_WORD;
    }
    if (valid_symbols[FOR_IN_VARIABLE_WORD] && scan_for_in_shape(lexer)) {
      return FOR_IN_VARIABLE_WORD;
    }
    return token;
  case BUILTIN_FUNC_NAME_WORD:
    return skip_token_layout(lexer) && lexer->lookahead == '('
      ? BUILTIN_CALL_WORD
      : token;
  default:
    return token;
  }
}

static bool scan_word_token(TSLexer *lexer, const bool *valid_symbols) {
  enum TokenType token = scan_word_spelling(lexer);
  lexer->mark_end(lexer);
  token = promote_word(lexer, valid_symbols, token);
  return valid_symbols[token] && emit(lexer, token);
}

static bool scan_number_token(TSLexer *lexer) {
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
      return emit(lexer, NUMBER);
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
  return emit(lexer, NUMBER);
}

static const CompositeOperator *
find_composite_operator(int32_t first, int32_t second) {
  for (size_t i = 0; i < ARRAY_LENGTH(COMPOSITE_OPERATORS); i++) {
    const CompositeOperator *entry = &COMPOSITE_OPERATORS[i];
    if (entry->first == first && entry->second == second) {
      return entry;
    }
  }
  return NULL;
}

static const SingleOperator *
find_single_operator(int32_t character, const bool *valid_symbols) {
  for (size_t i = 0; i < ARRAY_LENGTH(SINGLE_OPERATORS); i++) {
    const SingleOperator *entry = &SINGLE_OPERATORS[i];
    if (entry->character == character && valid_symbols[entry->token]) {
      return entry;
    }
  }
  return NULL;
}

static bool scan_operator(TSLexer *lexer, const bool *valid_symbols) {
  const int32_t first = lexer->lookahead;
  lexer->advance(lexer, false);
  lexer->mark_end(lexer);
  const CompositeOperator *composite =
    find_composite_operator(first, lexer->lookahead);
  if (composite != NULL) {
    if (!valid_symbols[composite->token]) {
      return false;
    }
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    return emit(lexer, composite->token);
  }
  const SingleOperator *single = find_single_operator(first, valid_symbols);
  return single != NULL && emit(lexer, single->token);
}

static bool scan_slash_start(
  ScannerState *state,
  TSLexer *lexer,
  const bool *valid_symbols
) {
  lexer->advance(lexer, false);
  lexer->mark_end(lexer);
  if (lexer->lookahead == '=') {
    if (valid_symbols[DIV_ASSIGN_OPERATOR]) {
      lexer->advance(lexer, false);
      lexer->mark_end(lexer);
      return emit(lexer, DIV_ASSIGN_OPERATOR);
    }
    if (valid_symbols[DIVISION_SLASH]) {
      return false;
    }
  }
  if (valid_symbols[DIVISION_SLASH]) {
    return emit(lexer, DIVISION_SLASH);
  }
  return valid_symbols[ERE_OPENING_SLASH] &&
    emit_mode(state, lexer, LEXICAL_MODE_ERE_BODY, ERE_OPENING_SLASH);
}

static bool scan_escape(const ScannerState *state, TSLexer *lexer) {
  if (lexer->eof(lexer) || lexer->lookahead == '\n') {
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

// Every backslash must be emitted to prevent fallback to the internal marker.
static bool
scan_backslash(ScannerState *state, TSLexer *lexer, bool recovering) {
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
  return !recovering && scan_escape(state, lexer);
}

static bool scan_string_content(TSLexer *lexer) {
  bool consumed = false;
  while (!lexer->eof(lexer) && !character_in(lexer->lookahead, "\"\\\n")) {
    consumed = true;
    lexer->advance(lexer, false);
  }
  lexer->mark_end(lexer);
  return consumed && emit(lexer, STRING_CONTENT);
}

static bool scan_ere_spelling(TSLexer *lexer, enum TokenType token) {
  do {
    lexer->advance(lexer, false);
  } while (
    is_ascii_digit(lexer->lookahead) ||
    (token == ERE_CLASS_NAME && is_ascii_letter(lexer->lookahead))
  );
  lexer->mark_end(lexer);
  return emit(lexer, token);
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
  if (!valid_symbols[token->delimiter]) {
    return false;
  }
  LexicalMode mode = LEXICAL_MODE_ERE_BODY;
  if (opening) {
    for (size_t i = 1; i < ARRAY_LENGTH(ERE_COMPOUND_TOKENS); i++) {
      if (ERE_COMPOUND_TOKENS[i].first == lexer->lookahead) {
        mode = ERE_COMPOUND_TOKENS[i].mode;
        break;
      }
    }
  }
  return emit_mode(state, lexer, mode, token->delimiter);
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
      if (lexer->lookahead == token->first) {
        return scan_ere_compound_token(state, lexer, token, valid_symbols);
      }
    }
    if (valid_symbols[ERE_CLASS_NAME] && is_ascii_letter(lexer->lookahead)) {
      return scan_ere_spelling(lexer, ERE_CLASS_NAME);
    }
    if (valid_symbols[ERE_DUP_COUNT] && is_ascii_digit(lexer->lookahead)) {
      return scan_ere_spelling(lexer, ERE_DUP_COUNT);
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
    return lexer->lookahead == ']' && emit(lexer, ERE_CLOSING_HYPHEN);
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
  if (state->mode == LEXICAL_MODE_OUTSIDE) {
    return 0;
  }
  buffer[0] = (char)state->mode;
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
  if (mode <= LEXICAL_MODE_CONTINUED_NEWLINE) {
    state->mode = (LexicalMode)mode;
  }
}

bool tree_sitter_posix_awk_external_scanner_scan(
  void *payload,
  TSLexer *lexer,
  const bool *valid_symbols
) {
  ScannerState *state = payload;
  const bool recovering = valid_symbols[ERROR_SENTINEL];
  if (state->mode == LEXICAL_MODE_CONTINUED_NEWLINE) {
    if (lexer->lookahead != '\n' || !valid_symbols[CONTINUATION_NEWLINE]) {
      return false;
    }
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    return emit_mode(state, lexer, LEXICAL_MODE_OUTSIDE, CONTINUATION_NEWLINE);
  }
  if (state->mode == LEXICAL_MODE_OUTSIDE) {
    while (is_ascii_blank(lexer->lookahead)) {
      lexer->advance(lexer, true);
    }
  }
  lexer->mark_end(lexer);
  if (lexer->lookahead == '\\') {
    return scan_backslash(state, lexer, recovering);
  }
  if (is_ere_mode(state->mode)) {
    return scan_ere_context(state, lexer, valid_symbols, recovering);
  }
  if (state->mode == LEXICAL_MODE_STRING) {
    if (lexer->lookahead == '"' && valid_symbols[STRING_CLOSING]) {
      lexer->advance(lexer, false);
      lexer->mark_end(lexer);
      return emit_mode(state, lexer, LEXICAL_MODE_OUTSIDE, STRING_CLOSING);
    }
    return !recovering &&
      valid_symbols[STRING_CONTENT] &&
      scan_string_content(lexer);
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
  if (is_word_start(lexer->lookahead)) {
    return scan_word_token(lexer, valid_symbols);
  }
  if (
    (is_ascii_digit(lexer->lookahead) || lexer->lookahead == '.') &&
    valid_symbols[NUMBER]
  ) {
    return scan_number_token(lexer);
  }
  if (lexer->lookahead == '/') {
    return scan_slash_start(state, lexer, valid_symbols);
  }
  return scan_operator(lexer, valid_symbols);
}
