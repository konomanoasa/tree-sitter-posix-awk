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
  {WORD_KIND_BEGIN, BEGIN_KEYWORD},
  {WORD_KIND_END, END_KEYWORD},
  {WORD_KIND_FUNCTION, FUNCTION_KEYWORD},
  {WORD_KIND_PRINT, PRINT_KEYWORD},
  {WORD_KIND_BREAK, BREAK_KEYWORD},
  {WORD_KIND_CONTINUE, CONTINUE_KEYWORD},
  {WORD_KIND_DELETE, DELETE_KEYWORD},
  {WORD_KIND_DO, DO_KEYWORD},
  {WORD_KIND_ELSE, ELSE_KEYWORD},
  {WORD_KIND_EXIT, EXIT_KEYWORD},
  {WORD_KIND_FOR, FOR_KEYWORD},
  {WORD_KIND_IF, IF_KEYWORD},
  {WORD_KIND_NEXT, NEXT_KEYWORD},
  {WORD_KIND_NEXTFILE, NEXTFILE_KEYWORD},
  {WORD_KIND_PRINTF, PRINTF_KEYWORD},
  {WORD_KIND_RETURN, RETURN_KEYWORD},
  {WORD_KIND_WHILE, WHILE_KEYWORD},
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

static WordKind scan_word_spelling(TSLexer *lexer) {
  char spelling[MAX_RESERVED_WORD_LENGTH + 1] = {0};
  size_t length = 0;
  if (!is_word_start(lexer->lookahead)) {
    return WORD_KIND_NONE;
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
  if (!skip_token_layout(lexer) || scan_word_spelling(lexer) != WORD_KIND_IN) {
    return false;
  }
  if (
    !skip_token_layout(lexer) || scan_word_spelling(lexer) != WORD_KIND_NAME
  ) {
    return false;
  }
  return skip_token_layout(lexer) && lexer->lookahead == ')';
}

static WordKind
promote_word_kind(TSLexer *lexer, const bool *valid_symbols, WordKind kind) {
  switch (kind) {
  case WORD_KIND_NAME:
    if (lexer->lookahead == '(') {
      return WORD_KIND_FUNC_NAME;
    }
    if (valid_symbols[FOR_IN_VARIABLE_WORD] && scan_for_in_shape(lexer)) {
      return WORD_KIND_FOR_IN_VARIABLE;
    }
    return kind;
  case WORD_KIND_BUILTIN_FUNC_NAME:
    return skip_token_layout(lexer) && lexer->lookahead == '('
      ? WORD_KIND_BUILTIN_CALL
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

static bool scan_word_token(TSLexer *lexer, const bool *valid_symbols) {
  WordKind kind = scan_word_spelling(lexer);
  lexer->mark_end(lexer);
  kind = promote_word_kind(lexer, valid_symbols, kind);
  const WordToken *token = find_word_token(kind);
  return token !=
    NULL &&
    valid_symbols[token->token] &&
    emit(lexer, token->token);
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
  if (
    floating &&
    (lexer->lookahead ==
      'f' ||
      lexer->lookahead ==
      'F' ||
      lexer->lookahead ==
      'l' ||
      lexer->lookahead == 'L')
  ) {
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
  }
  return emit(lexer, NUMBER);
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

static bool
scan_composite_operator_start(TSLexer *lexer, const bool *valid_symbols) {
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
  const SingleOperator *single = find_single_operator(first);
  return single !=
    NULL &&
    valid_symbols[single->token] &&
    emit(lexer, single->token);
}

static bool scan_greater_start(TSLexer *lexer, const bool *valid_symbols) {
  lexer->advance(lexer, false);
  lexer->mark_end(lexer);
  if (lexer->lookahead == '=' || lexer->lookahead == '>') {
    const enum TokenType token =
      lexer->lookahead == '=' ? GE_OPERATOR : APPEND_OPERATOR;
    if (!valid_symbols[token]) {
      return false;
    }
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    return emit(lexer, token);
  }
  if (valid_symbols[OUTPUT_GREATER]) {
    return emit(lexer, OUTPUT_GREATER);
  }
  return valid_symbols[GREATER_OPERATOR] && emit(lexer, GREATER_OPERATOR);
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
  if (lexer->eof(lexer) || lexer->lookahead == '\n') {
    return false;
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
  return valid_symbols[token] && emit(lexer, token);
}

static bool scan_backslash(
  ScannerState *state,
  TSLexer *lexer,
  const bool *valid_symbols,
  bool recovering
) {
  lexer->advance(lexer, false);
  if (state->mode == LEXICAL_MODE_OUTSIDE) {
    if (lexer->lookahead != '\n' || !valid_symbols[CONTINUATION_BACKSLASH]) {
      return false;
    }
    lexer->mark_end(lexer);
    return emit_mode(
      state,
      lexer,
      LEXICAL_MODE_CONTINUED_NEWLINE,
      CONTINUATION_BACKSLASH
    );
  }
  return !recovering && scan_escape(state, lexer, valid_symbols);
}

static bool scan_string_content(TSLexer *lexer) {
  bool consumed = false;
  while (
    !lexer->eof(lexer) &&
    lexer->lookahead !=
    '"' &&
    lexer->lookahead !=
    '\\' &&
    lexer->lookahead != '\n'
  ) {
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
  lexer->advance(lexer, false);
  lexer->mark_end(lexer);
  const bool matches = token->delimiter == ERE_COMPOUND_OPENING
    ? character_in(lexer->lookahead, ".=:")
    : lexer->lookahead == ']';
  if (
    matches &&
    (token->delimiter == ERE_COMPOUND_OPENING || state->mode == token->mode)
  ) {
    if (!valid_symbols[token->delimiter]) {
      return false;
    }
    LexicalMode mode = LEXICAL_MODE_ERE_BODY;
    if (token->delimiter == ERE_COMPOUND_OPENING) {
      for (size_t i = 1; i < ARRAY_LENGTH(ERE_COMPOUND_TOKENS); i++) {
        if (ERE_COMPOUND_TOKENS[i].first == lexer->lookahead) {
          mode = ERE_COMPOUND_TOKENS[i].mode;
          break;
        }
      }
    }
    return emit_mode(state, lexer, mode, token->delimiter);
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
    return scan_backslash(state, lexer, valid_symbols, recovering);
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
  if (is_word_start(lexer->lookahead) && has_word_token(valid_symbols)) {
    return scan_word_token(lexer, valid_symbols);
  }
  if (
    (is_ascii_digit(lexer->lookahead) || lexer->lookahead == '.') &&
    valid_symbols[NUMBER]
  ) {
    return scan_number_token(lexer);
  }
  if (
    lexer->lookahead ==
    '/' &&
    (valid_symbols[DIVISION_SLASH] ||
      valid_symbols[ERE_OPENING_SLASH] ||
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
      valid_symbols[OUTPUT_GREATER])
  ) {
    return scan_greater_start(lexer, valid_symbols);
  }
  const SingleOperator *single = find_single_operator(lexer->lookahead);
  if (
    (single != NULL && valid_symbols[single->token]) ||
    has_valid_composite_operator_start(lexer->lookahead, valid_symbols)
  ) {
    return scan_composite_operator_start(lexer, valid_symbols);
  }
  return false;
}
