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
  GETLINE_TARGET_WORD,
  IN_WORD,
  BUILTIN_FUNC_NAME_WORD,
  BUILTIN_CALL_WORD,
  FUNC_NAME_WORD,
  NUMBER_INTEGER,
  NUMBER_FRACTION,
  NUMBER_EXPONENT,
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
  OUTPUT_GREATER_GUARD,
  LC_MARKER_FIRST,
  LC_BEFORE_OPERATOR = LC_MARKER_FIRST,
  LC_BEFORE_ADDITIVE_OPERATOR,
  LC_BEFORE_MULTIPLICATIVE_OPERATOR,
  LC_BEFORE_EXPONENTIATION_OPERATOR,
  LC_BEFORE_COMPARISON_OPERATOR,
  LC_BEFORE_MATCH_OPERATOR,
  LC_BEFORE_MEMBERSHIP_OPERATOR,
  LC_BEFORE_LOGICAL_AND_OPERATOR,
  LC_BEFORE_LOGICAL_OR_OPERATOR,
  LC_BEFORE_CONDITIONAL_QUESTION,
  LC_BEFORE_CONDITIONAL_COLON,
  LC_BEFORE_LESS_THAN,
  LC_BEFORE_INPUT_PIPE,
  LC_BEFORE_OUTPUT_REDIRECTION,
  LC_BEFORE_ELSE,
  LC_BEFORE_DO_TAIL,
  LC_BEFORE_SEMICOLON,
  LC_BEFORE_NEWLINE,
  LC_BEFORE_CLOSE_BRACE,
  LC_BEFORE_SIMPLE_STATEMENT,
  LC_BEFORE_EXPRESSION,
  LC_BEFORE_COMMA,
  LC_BEFORE_OPEN_BRACKET,
  LC_BEFORE_ACTION,
  LC_BEFORE_CLOSE_PARENTHESIS,
  LC_BEFORE_CLOSE_BRACKET,
  LC_BEFORE_STATEMENT,
  LC_BEFORE_ITEM,
  LC_BEFORE_EOF,
  LC_MARKER_LAST = LC_BEFORE_EOF,
  CLOSED_ITEM_BOUNDARY,
  NORMAL_PATTERN_ITEM_BOUNDARY,
  STATEMENT_RECOVERY,
  ACTION_RECOVERY,
  ERE_COMPOUND_OPEN_GUARD,
  ERE_DOT_CLOSE_GUARD,
  ERE_EQUAL_CLOSE_GUARD,
  ERE_COLON_CLOSE_GUARD,
  ERE_ESCAPE_START,
  ERE_ESCAPED_DELIMITER_START,
  ERE_ESCAPED_DELIMITER_END,
  ERE_CLOSING_HYPHEN,
  ERE_LEXICAL_END,
  ERE_CLOSING,
  STRING_OPENING,
  STRING_END,
  COMMENT,
  EXPRESSION_TARGET_GUARD,
  PRINT_EXPRESSION_TARGET_GUARD,
  ACTION_TARGET_GUARD,
  PARAMETER_TARGET_GUARD,
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
  WORD_KIND_GETLINE_TARGET,
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

typedef enum {
  WORD_ROLE_EXPRESSION_START = 1U << 0,
  WORD_ROLE_ITEM_START = 1U << 1,
  WORD_ROLE_RESERVED_ITEM_START = 1U << 2,
} WordRole;

// The zero-width tokens a lookahead can follow, most specific first: the
// recovery for an absent statement, then the line-continuation markers.
enum { MAX_MARKERS_PER_TARGET = 4 };

typedef struct {
  size_t count;
  enum TokenType items[MAX_MARKERS_PER_TARGET];
} MarkerList;

#define MARKERS(...) \
  { \
    ARRAY_LENGTH(((const enum TokenType[]){__VA_ARGS__})), { \
      __VA_ARGS__ \
    } \
  }

// A value can start a simple statement, an expression operand, a statement,
// or an item. Where more than one of these markers is valid in a parse state,
// the grammar accepts the value in the more specific role as well: after a
// top-level pattern, a value continues the pattern as a concatenation while
// only a reserved word starts the next item.
#define VALUE_MARKERS \
  MARKERS( \
    LC_BEFORE_SIMPLE_STATEMENT, \
    LC_BEFORE_EXPRESSION, \
    LC_BEFORE_STATEMENT, \
    LC_BEFORE_ITEM \
  )

typedef struct {
  WordKind kind;
  enum TokenType token;
  unsigned roles;
  MarkerList markers;
} WordToken;

// A punctuation or operator spelling and the markers it can follow. A
// two-character spelling is matched before the one-character spelling that
// starts it.
typedef struct {
  int32_t first;
  int32_t second;
  MarkerList markers;
} BoundaryTarget;

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

// The scanner owns the lexical modes of a static ERE and a string: the
// opening token of each enters its mode, and the token that ends it returns
// to OUTSIDE. A comment is lexed only in OUTSIDE, so a `#` inside an ERE or a
// string never opens one.
typedef enum {
  LEXICAL_MODE_OUTSIDE,
  LEXICAL_MODE_ERE_BODY,
  LEXICAL_MODE_ERE_ESCAPED_DELIMITER,
  LEXICAL_MODE_STRING,
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
  {WORD_KIND_BEGIN,
    BEGIN_WORD,
    WORD_ROLE_ITEM_START | WORD_ROLE_RESERVED_ITEM_START,
    MARKERS(LC_BEFORE_ITEM)},
  {WORD_KIND_END,
    END_WORD,
    WORD_ROLE_ITEM_START | WORD_ROLE_RESERVED_ITEM_START,
    MARKERS(LC_BEFORE_ITEM)},
  {WORD_KIND_FUNCTION,
    FUNCTION_WORD,
    WORD_ROLE_ITEM_START | WORD_ROLE_RESERVED_ITEM_START,
    MARKERS(LC_BEFORE_ITEM)},
  {WORD_KIND_PRINT,
    PRINT_WORD,
    0,
    MARKERS(LC_BEFORE_SIMPLE_STATEMENT, LC_BEFORE_STATEMENT)},
  {WORD_KIND_BREAK, BREAK_WORD, 0, MARKERS(LC_BEFORE_STATEMENT)},
  {WORD_KIND_CONTINUE, CONTINUE_WORD, 0, MARKERS(LC_BEFORE_STATEMENT)},
  {WORD_KIND_DELETE,
    DELETE_WORD,
    0,
    MARKERS(LC_BEFORE_SIMPLE_STATEMENT, LC_BEFORE_STATEMENT)},
  {WORD_KIND_DO, DO_WORD, 0, MARKERS(LC_BEFORE_STATEMENT)},
  {WORD_KIND_ELSE, ELSE_WORD, 0, MARKERS(LC_BEFORE_ELSE)},
  {WORD_KIND_EXIT, EXIT_WORD, 0, MARKERS(LC_BEFORE_STATEMENT)},
  {WORD_KIND_FOR, FOR_WORD, 0, MARKERS(LC_BEFORE_STATEMENT)},
  {WORD_KIND_IF, IF_WORD, 0, MARKERS(LC_BEFORE_STATEMENT)},
  {WORD_KIND_NEXT, NEXT_WORD, 0, MARKERS(LC_BEFORE_STATEMENT)},
  {WORD_KIND_NEXTFILE, NEXTFILE_WORD, 0, MARKERS(LC_BEFORE_STATEMENT)},
  {WORD_KIND_PRINTF,
    PRINTF_WORD,
    0,
    MARKERS(LC_BEFORE_SIMPLE_STATEMENT, LC_BEFORE_STATEMENT)},
  {WORD_KIND_RETURN, RETURN_WORD, 0, MARKERS(LC_BEFORE_STATEMENT)},
  {WORD_KIND_WHILE,
    WHILE_WORD,
    0,
    MARKERS(LC_BEFORE_DO_TAIL, LC_BEFORE_STATEMENT)},
  {WORD_KIND_NAME,
    NAME_WORD,
    WORD_ROLE_EXPRESSION_START | WORD_ROLE_ITEM_START,
    VALUE_MARKERS},
  {WORD_KIND_FOR_IN_VARIABLE,
    FOR_IN_VARIABLE_WORD,
    WORD_ROLE_EXPRESSION_START | WORD_ROLE_ITEM_START,
    VALUE_MARKERS},
  {WORD_KIND_GETLINE,
    GETLINE_WORD,
    WORD_ROLE_EXPRESSION_START | WORD_ROLE_ITEM_START,
    VALUE_MARKERS},
  {WORD_KIND_GETLINE_TARGET,
    GETLINE_TARGET_WORD,
    WORD_ROLE_EXPRESSION_START | WORD_ROLE_ITEM_START,
    VALUE_MARKERS},
  {WORD_KIND_IN, IN_WORD, 0, MARKERS(LC_BEFORE_MEMBERSHIP_OPERATOR)},
  {WORD_KIND_BUILTIN_FUNC_NAME,
    BUILTIN_FUNC_NAME_WORD,
    WORD_ROLE_EXPRESSION_START | WORD_ROLE_ITEM_START,
    VALUE_MARKERS},
  {WORD_KIND_BUILTIN_CALL,
    BUILTIN_CALL_WORD,
    WORD_ROLE_EXPRESSION_START | WORD_ROLE_ITEM_START,
    VALUE_MARKERS},
  {WORD_KIND_FUNC_NAME,
    FUNC_NAME_WORD,
    WORD_ROLE_EXPRESSION_START | WORD_ROLE_ITEM_START,
    VALUE_MARKERS},
};

static const MarkerList VALUE_MARKER_LIST = VALUE_MARKERS;

static const MarkerList EOF_MARKERS =
  MARKERS(STATEMENT_RECOVERY, LC_BEFORE_EOF);

static const BoundaryTarget BOUNDARY_TARGETS[] = {
  {',', 0, MARKERS(LC_BEFORE_COMMA)},
  {'[', 0, MARKERS(LC_BEFORE_OPEN_BRACKET)},
  {'{', 0, MARKERS(LC_BEFORE_ACTION, LC_BEFORE_STATEMENT, LC_BEFORE_ITEM)},
  {')', 0, MARKERS(LC_BEFORE_CLOSE_PARENTHESIS)},
  {']', 0, MARKERS(LC_BEFORE_CLOSE_BRACKET)},
  {';', 0, MARKERS(LC_BEFORE_SEMICOLON, LC_BEFORE_STATEMENT)},
  {'}', 0, MARKERS(STATEMENT_RECOVERY, LC_BEFORE_CLOSE_BRACE)},
  {'\n', 0, MARKERS(LC_BEFORE_NEWLINE)},
  {'/', '=', MARKERS(LC_BEFORE_OPERATOR)},
  {'/', 0, MARKERS(LC_BEFORE_MULTIPLICATIVE_OPERATOR)},
  {'+', '=', MARKERS(LC_BEFORE_OPERATOR)},
  {'+', '+', MARKERS(LC_BEFORE_OPERATOR)},
  {'+', 0, MARKERS(LC_BEFORE_ADDITIVE_OPERATOR)},
  {'-', '=', MARKERS(LC_BEFORE_OPERATOR)},
  {'-', '-', MARKERS(LC_BEFORE_OPERATOR)},
  {'-', 0, MARKERS(LC_BEFORE_ADDITIVE_OPERATOR)},
  {'*', '=', MARKERS(LC_BEFORE_OPERATOR)},
  {'*', 0, MARKERS(LC_BEFORE_MULTIPLICATIVE_OPERATOR)},
  {'%', '=', MARKERS(LC_BEFORE_OPERATOR)},
  {'%', 0, MARKERS(LC_BEFORE_MULTIPLICATIVE_OPERATOR)},
  {'^', '=', MARKERS(LC_BEFORE_OPERATOR)},
  {'^', 0, MARKERS(LC_BEFORE_EXPONENTIATION_OPERATOR)},
  {'!', '=', MARKERS(LC_BEFORE_COMPARISON_OPERATOR)},
  {'!', '~', MARKERS(LC_BEFORE_MATCH_OPERATOR)},
  {'=', '=', MARKERS(LC_BEFORE_COMPARISON_OPERATOR)},
  {'=', 0, MARKERS(LC_BEFORE_OPERATOR)},
  {'<', '=', MARKERS(LC_BEFORE_COMPARISON_OPERATOR)},
  {'<', 0, MARKERS(LC_BEFORE_LESS_THAN)},
  {'>', '>', MARKERS(LC_BEFORE_OUTPUT_REDIRECTION)},
  {'>', '=', MARKERS(LC_BEFORE_COMPARISON_OPERATOR)},
  {'>',
    0,
    MARKERS(LC_BEFORE_OUTPUT_REDIRECTION, LC_BEFORE_COMPARISON_OPERATOR)},
  {'~', 0, MARKERS(LC_BEFORE_MATCH_OPERATOR)},
  {'|', '|', MARKERS(LC_BEFORE_LOGICAL_OR_OPERATOR)},
  {'|', 0, MARKERS(LC_BEFORE_OUTPUT_REDIRECTION, LC_BEFORE_INPUT_PIPE)},
  {'&', '&', MARKERS(LC_BEFORE_LOGICAL_AND_OPERATOR)},
  {'?', 0, MARKERS(LC_BEFORE_CONDITIONAL_QUESTION)},
  {':', 0, MARKERS(LC_BEFORE_CONDITIONAL_COLON)},
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

static bool character_starts_expression(int32_t character) {
  switch (character) {
  case '"':
  case '(':
  case '$':
  case '+':
  case '-':
  case '/':
  case '!':
    return true;
  default:
    return false;
  }
}

static bool character_starts_item(int32_t character) {
  return character == '{' || character_starts_expression(character);
}

static bool advance_line_continuations(TSLexer *lexer) {
  bool found = false;

  while (lexer->lookahead == '\\') {
    lexer->advance(lexer, false);
    if (lexer->lookahead != '\n') {
      return false;
    }

    lexer->advance(lexer, false);
    found = true;
  }

  return found;
}

// Blanks before a token are skipped so that the token starts after them;
// blanks inside a lookahead are consumed so that the token end marked before
// the lookahead stays where it is.
static void skip_ascii_blanks(TSLexer *lexer) {
  while (is_ascii_blank(lexer->lookahead)) {
    lexer->advance(lexer, true);
  }
}

static void advance_ascii_blanks(TSLexer *lexer) {
  while (is_ascii_blank(lexer->lookahead)) {
    lexer->advance(lexer, false);
  }
}

static void advance_comment_to_boundary(TSLexer *lexer) {
  if (lexer->lookahead != '#') {
    return;
  }
  do {
    lexer->advance(lexer, false);
  } while (lexer->lookahead != '\n' && !lexer->eof(lexer));
}

static bool advance_layout_gap(TSLexer *lexer) {
  for (;;) {
    advance_ascii_blanks(lexer);
    advance_comment_to_boundary(lexer);
    if (lexer->lookahead == '\n') {
      lexer->advance(lexer, false);
      continue;
    }
    if (lexer->lookahead != '\\') {
      return true;
    }
    if (!advance_line_continuations(lexer)) {
      return false;
    }
  }
}

// Lookahead past blanks and line continuations after the token end, so the
// token itself never moves.
static bool advance_boundary_gap_remainder(TSLexer *lexer) {
  for (;;) {
    advance_ascii_blanks(lexer);
    if (lexer->lookahead != '\\') {
      return true;
    }
    if (!advance_line_continuations(lexer)) {
      return false;
    }
  }
}

static bool emit(TSLexer *lexer, enum TokenType token) {
  lexer->result_symbol = token;
  return true;
}

// Emits the token that moves the scanner into a lexical mode.
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
  char word[MAX_RESERVED_WORD_LENGTH + 1] = {0};
  size_t length = 0;

  if (!is_word_start(lexer->lookahead)) {
    return WORD_KIND_NONE;
  }

  while (is_word_continue(lexer->lookahead)) {
    if (length < MAX_RESERVED_WORD_LENGTH) {
      word[length] = (char)lexer->lookahead;
    }
    if (length <= MAX_RESERVED_WORD_LENGTH) {
      length++;
    }
    lexer->advance(lexer, false);
  }

  return classify_word(word, length);
}

static WordKind scan_word_kind(TSLexer *lexer);

// Recognizes "in NAME )" after a scanned name, the only shape of a for-in
// header, so the parser does not have to hold a name open until the
// closing parenthesis decides between for-in and a classic for.
static bool scan_for_in_shape(TSLexer *lexer) {
  if (
    !advance_boundary_gap_remainder(lexer) ||
    scan_word_spelling(lexer) !=
    WORD_KIND_IN ||
    !advance_boundary_gap_remainder(lexer) ||
    scan_word_spelling(lexer) !=
    WORD_KIND_NAME ||
    !advance_boundary_gap_remainder(lexer)
  ) {
    return false;
  }
  return lexer->lookahead == ')';
}

// Promotes a word by what follows it, so the parser never has to choose
// between "name followed by (" and "name concatenated with (...)": FUNC_NAME
// needs the parenthesis adjacent (line continuations only), a built-in call
// allows blanks before it, getline takes a following lvalue, and a for-in
// variable is followed by "in NAME )". The for-in promotion applies only
// where the parser can accept it, because the same shape also appears in a
// parenthesized membership test.
static WordKind
promote_word_kind(TSLexer *lexer, const bool *valid_symbols, WordKind kind) {
  switch (kind) {
  case WORD_KIND_NAME:
    if (lexer->lookahead == '\\' && !advance_line_continuations(lexer)) {
      return kind;
    }
    if (lexer->lookahead == '(') {
      return WORD_KIND_FUNC_NAME;
    }
    if (
      valid_symbols !=
      NULL &&
      valid_symbols[FOR_IN_VARIABLE_WORD] &&
      scan_for_in_shape(lexer)
    ) {
      return WORD_KIND_FOR_IN_VARIABLE;
    }
    return kind;
  case WORD_KIND_BUILTIN_FUNC_NAME:
    if (!advance_boundary_gap_remainder(lexer)) {
      return kind;
    }
    return lexer->lookahead == '(' ? WORD_KIND_BUILTIN_CALL : kind;
  case WORD_KIND_GETLINE:
    if (!advance_boundary_gap_remainder(lexer)) {
      return kind;
    }
    if (lexer->lookahead == '$') {
      return WORD_KIND_GETLINE_TARGET;
    }
    if (
      is_word_start(lexer->lookahead) && scan_word_kind(lexer) == WORD_KIND_NAME
    ) {
      return WORD_KIND_GETLINE_TARGET;
    }
    return kind;
  default:
    return kind;
  }
}

static WordKind scan_word_kind(TSLexer *lexer) {
  return promote_word_kind(lexer, NULL, scan_word_spelling(lexer));
}

static const WordToken *find_word_token(WordKind kind) {
  for (size_t i = 0; i < ARRAY_LENGTH(WORD_TOKENS); i++) {
    if (WORD_TOKENS[i].kind == kind) {
      return &WORD_TOKENS[i];
    }
  }
  return NULL;
}

static bool
emit_word_kind(TSLexer *lexer, const bool *valid_symbols, WordKind kind) {
  const WordToken *token = find_word_token(kind);
  if (token == NULL || !valid_symbols[token->token]) {
    return false;
  }
  return emit(lexer, token->token);
}

static bool word_kind_is_valid(const bool *valid_symbols, WordKind kind) {
  const WordToken *token = find_word_token(kind);
  return token != NULL && valid_symbols[token->token];
}

// A spelling is worth scanning when its kind or a kind it can be promoted to
// is valid.
static bool word_spelling_is_valid(const bool *valid_symbols, WordKind kind) {
  switch (kind) {
  case WORD_KIND_NAME:
    return word_kind_is_valid(valid_symbols, WORD_KIND_NAME) ||
      word_kind_is_valid(valid_symbols, WORD_KIND_FUNC_NAME) ||
      word_kind_is_valid(valid_symbols, WORD_KIND_FOR_IN_VARIABLE);
  case WORD_KIND_BUILTIN_FUNC_NAME:
    return word_kind_is_valid(valid_symbols, kind) ||
      word_kind_is_valid(valid_symbols, WORD_KIND_BUILTIN_CALL);
  case WORD_KIND_GETLINE:
    return word_kind_is_valid(valid_symbols, kind) ||
      word_kind_is_valid(valid_symbols, WORD_KIND_GETLINE_TARGET);
  default:
    return word_kind_is_valid(valid_symbols, kind);
  }
}

static bool
scan_word_token(TSLexer *lexer, const bool *valid_symbols, WordKind kind) {
  if (!word_spelling_is_valid(valid_symbols, kind)) {
    return false;
  }

  lexer->mark_end(lexer);
  return emit_word_kind(
    lexer,
    valid_symbols,
    promote_word_kind(lexer, valid_symbols, kind)
  );
}

static bool has_word_marker(const bool *valid_symbols) {
  for (size_t i = 0; i < ARRAY_LENGTH(WORD_TOKENS); i++) {
    if (valid_symbols[WORD_TOKENS[i].token]) {
      return true;
    }
  }
  return false;
}

static unsigned word_roles(WordKind kind) {
  const WordToken *token = find_word_token(kind);
  return token == NULL ? 0 : token->roles;
}

static bool word_has_role(WordKind kind, unsigned roles) {
  return (word_roles(kind) & roles) != 0;
}

static bool word_starts_expression(WordKind kind) {
  return word_has_role(kind, WORD_ROLE_EXPRESSION_START);
}

static bool word_starts_print_expression(WordKind kind) {
  return kind !=
    WORD_KIND_GETLINE &&
    kind !=
    WORD_KIND_GETLINE_TARGET &&
    word_starts_expression(kind);
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

static NumberKind
accept_number(TSLexer *lexer, NumberKind kind, bool mark_end) {
  if (mark_end) {
    lexer->mark_end(lexer);
  }
  return kind;
}

// Scans one NUMBER spelling and returns its kind. The token end moves after
// each complete kind, so an incomplete exponent leaves the shorter number:
// "1e+" tokenizes as the integer "1".
static NumberKind scan_number_kind(TSLexer *lexer, bool mark_end) {
  NumberKind kind = NUMBER_KIND_NONE;

  while (is_ascii_digit(lexer->lookahead)) {
    lexer->advance(lexer, false);
    kind = accept_number(lexer, NUMBER_KIND_INTEGER, mark_end);
  }
  if (lexer->lookahead == '.') {
    lexer->advance(lexer, false);
    if (kind != NUMBER_KIND_NONE) {
      kind = accept_number(lexer, NUMBER_KIND_FRACTION, mark_end);
    }
    while (is_ascii_digit(lexer->lookahead)) {
      lexer->advance(lexer, false);
      kind = accept_number(lexer, NUMBER_KIND_FRACTION, mark_end);
    }
  }
  if (kind == NUMBER_KIND_NONE) {
    return kind;
  }

  if (lexer->lookahead == 'e' || lexer->lookahead == 'E') {
    lexer->advance(lexer, false);
    if (lexer->lookahead == '+' || lexer->lookahead == '-') {
      lexer->advance(lexer, false);
    }
    while (is_ascii_digit(lexer->lookahead)) {
      lexer->advance(lexer, false);
      kind = accept_number(lexer, NUMBER_KIND_EXPONENT, mark_end);
    }
  }
  return kind;
}

static bool scan_number_start(TSLexer *lexer) {
  return scan_number_kind(lexer, false) != NUMBER_KIND_NONE;
}

static bool
emit_number_kind(TSLexer *lexer, const bool *valid_symbols, NumberKind kind) {
  if (kind == NUMBER_KIND_NONE || !valid_symbols[number_kind_token(kind)]) {
    return false;
  }
  return emit(lexer, number_kind_token(kind));
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

static bool
scan_composite_operator_start(TSLexer *lexer, const bool *valid_symbols) {
  const int32_t first = lexer->lookahead;
  lexer->advance(lexer, false);
  const CompositeOperator *composite =
    find_composite_operator(first, lexer->lookahead);
  if (composite == NULL || !valid_symbols[composite->token]) {
    return false;
  }

  lexer->advance(lexer, false);
  lexer->mark_end(lexer);
  return emit(lexer, composite->token);
}

static bool
has_greater_start(const bool *valid_symbols, bool allow_zero_width_guard) {
  return valid_symbols[GE_OPERATOR] ||
    valid_symbols[APPEND_OPERATOR] ||
    (allow_zero_width_guard && valid_symbols[OUTPUT_GREATER_GUARD]);
}

// Dispatch together so an invalid two-character operator cannot block the
// zero-width redirection guard.
static bool scan_greater_start(
  TSLexer *lexer,
  const bool *valid_symbols,
  bool allow_zero_width_guard
) {
  lexer->advance(lexer, false);
  if (lexer->lookahead == '=') {
    if (!valid_symbols[GE_OPERATOR]) {
      return false;
    }
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    return emit(lexer, GE_OPERATOR);
  }
  if (lexer->lookahead == '>' && valid_symbols[APPEND_OPERATOR]) {
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    return emit(lexer, APPEND_OPERATOR);
  }
  if (!allow_zero_width_guard || !valid_symbols[OUTPUT_GREATER_GUARD]) {
    return false;
  }
  return emit(lexer, OUTPUT_GREATER_GUARD);
}

static bool scan_slash_start(
  ScannerState *state,
  TSLexer *lexer,
  const bool *valid_symbols,
  bool prefer_ere
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
  if (prefer_ere && valid_symbols[ERE_OPENING_SLASH]) {
    return emit_mode(state, lexer, LEXICAL_MODE_ERE_BODY, ERE_OPENING_SLASH);
  }
  if (valid_symbols[DIVISION_SLASH]) {
    return emit(lexer, DIVISION_SLASH);
  }
  if (valid_symbols[ERE_OPENING_SLASH]) {
    return emit_mode(state, lexer, LEXICAL_MODE_ERE_BODY, ERE_OPENING_SLASH);
  }
  return false;
}

static bool emit_word_item_boundary(
  TSLexer *lexer,
  const bool *valid_symbols,
  WordKind kind
) {
  const bool reserved_item_start =
    word_has_role(kind, WORD_ROLE_RESERVED_ITEM_START);
  if (
    valid_symbols[CLOSED_ITEM_BOUNDARY] &&
    word_has_role(kind, WORD_ROLE_ITEM_START)
  ) {
    return emit(lexer, CLOSED_ITEM_BOUNDARY);
  }
  if (reserved_item_start && valid_symbols[NORMAL_PATTERN_ITEM_BOUNDARY]) {
    return emit(lexer, NORMAL_PATTERN_ITEM_BOUNDARY);
  }
  return false;
}

// The zero-width recovery for an action that never follows; the source that
// stands in its place starts the next item or ends this one.
static bool emit_action_recovery(TSLexer *lexer, const bool *valid_symbols) {
  if (!valid_symbols[ACTION_RECOVERY]) {
    return false;
  }
  return emit(lexer, ACTION_RECOVERY);
}

static bool
scan_required_target_guard(TSLexer *lexer, const bool *valid_symbols) {
  lexer->advance(lexer, false);
  if (!advance_layout_gap(lexer)) {
    return false;
  }

  if (valid_symbols[ACTION_TARGET_GUARD] && lexer->lookahead == '{') {
    return emit(lexer, ACTION_TARGET_GUARD);
  }

  if (
    is_word_start(lexer->lookahead) &&
    (valid_symbols[EXPRESSION_TARGET_GUARD] ||
      valid_symbols[PRINT_EXPRESSION_TARGET_GUARD] ||
      valid_symbols[PARAMETER_TARGET_GUARD])
  ) {
    const WordKind kind = scan_word_kind(lexer);
    if (valid_symbols[PARAMETER_TARGET_GUARD] && kind == WORD_KIND_NAME) {
      return emit(lexer, PARAMETER_TARGET_GUARD);
    }
    if (
      valid_symbols[PRINT_EXPRESSION_TARGET_GUARD] &&
      word_starts_print_expression(kind)
    ) {
      return emit(lexer, PRINT_EXPRESSION_TARGET_GUARD);
    }
    if (
      valid_symbols[EXPRESSION_TARGET_GUARD] && word_starts_expression(kind)
    ) {
      return emit(lexer, EXPRESSION_TARGET_GUARD);
    }
  } else if (
    valid_symbols[EXPRESSION_TARGET_GUARD] ||
    valid_symbols[PRINT_EXPRESSION_TARGET_GUARD]
  ) {
    const bool starts_expression =
      character_starts_expression(lexer->lookahead);
    const bool starts_number = scan_number_start(lexer);
    if (starts_expression || starts_number) {
      return emit(
        lexer,
        valid_symbols[PRINT_EXPRESSION_TARGET_GUARD]
          ? PRINT_EXPRESSION_TARGET_GUARD
          : EXPRESSION_TARGET_GUARD
      );
    }
  }

  // The newline led to no target; an action that the parser still requires
  // is absent, and the newline ends its item.
  return emit_action_recovery(lexer, valid_symbols);
}

static bool scan_ere_backslash_context(
  ScannerState *state,
  TSLexer *lexer,
  const bool *valid_symbols
) {
  if (lexer->lookahead != '\\') {
    return false;
  }

  lexer->advance(lexer, false);
  if (valid_symbols[ERROR_SENTINEL]) {
    return false;
  }
  if (lexer->lookahead == '/' && valid_symbols[ERE_ESCAPED_DELIMITER_START]) {
    return emit_mode(
      state,
      lexer,
      LEXICAL_MODE_ERE_ESCAPED_DELIMITER,
      ERE_ESCAPED_DELIMITER_START
    );
  }
  if (
    lexer->lookahead !=
    '\n' &&
    !lexer->eof(lexer) &&
    valid_symbols[ERE_ESCAPE_START]
  ) {
    return emit(lexer, ERE_ESCAPE_START);
  }
  return false;
}

static bool is_ere_compound_punctuation(int32_t character) {
  switch (character) {
  case '.':
  case '=':
  case ':':
    return true;
  default:
    return false;
  }
}

typedef struct {
  enum TokenType guard;
  int32_t opening;
} EreCompoundGuard;

static const EreCompoundGuard ERE_COMPOUND_GUARDS[] = {
  {ERE_COMPOUND_OPEN_GUARD, '['},
  {ERE_DOT_CLOSE_GUARD, '.'},
  {ERE_EQUAL_CLOSE_GUARD, '='},
  {ERE_COLON_CLOSE_GUARD, ':'},
};

static bool scan_ere_compound_guard(TSLexer *lexer, enum TokenType guard) {
  lexer->advance(lexer, false);
  const bool matches = guard == ERE_COMPOUND_OPEN_GUARD
    ? is_ere_compound_punctuation(lexer->lookahead)
    : lexer->lookahead == ']';
  if (!matches) {
    return false;
  }
  return emit(lexer, guard);
}

static bool emit_first_valid_marker(
  TSLexer *lexer,
  const bool *valid_symbols,
  const MarkerList *markers
) {
  for (size_t i = 0; i < markers->count; i++) {
    if (valid_symbols[markers->items[i]]) {
      return emit(lexer, markers->items[i]);
    }
  }
  return false;
}

static bool
emit_word_marker(TSLexer *lexer, const bool *valid_symbols, WordKind kind) {
  const WordToken *token = find_word_token(kind);
  return token !=
    NULL &&
    emit_first_valid_marker(lexer, valid_symbols, &token->markers);
}

static bool has_two_character_target(int32_t first) {
  for (size_t i = 0; i < ARRAY_LENGTH(BOUNDARY_TARGETS); i++) {
    if (BOUNDARY_TARGETS[i].first == first && BOUNDARY_TARGETS[i].second != 0) {
      return true;
    }
  }
  return false;
}

static const BoundaryTarget *
find_boundary_target(int32_t first, int32_t second) {
  const BoundaryTarget *single_character = NULL;
  for (size_t i = 0; i < ARRAY_LENGTH(BOUNDARY_TARGETS); i++) {
    if (BOUNDARY_TARGETS[i].first != first) {
      continue;
    }
    if (BOUNDARY_TARGETS[i].second == second) {
      return &BOUNDARY_TARGETS[i];
    }
    if (BOUNDARY_TARGETS[i].second == 0) {
      single_character = &BOUNDARY_TARGETS[i];
    }
  }
  return single_character;
}

static bool
emit_boundary_target_marker(TSLexer *lexer, const bool *valid_symbols) {
  advance_comment_to_boundary(lexer);
  if (lexer->eof(lexer)) {
    return emit_first_valid_marker(lexer, valid_symbols, &EOF_MARKERS);
  }

  if (is_word_start(lexer->lookahead)) {
    return emit_word_marker(lexer, valid_symbols, scan_word_kind(lexer));
  }

  if (is_ascii_digit(lexer->lookahead) || lexer->lookahead == '.') {
    return scan_number_kind(lexer, false) !=
      NUMBER_KIND_NONE &&
      emit_first_valid_marker(lexer, valid_symbols, &VALUE_MARKER_LIST);
  }

  const int32_t first = lexer->lookahead;
  int32_t second = 0;
  if (has_two_character_target(first)) {
    lexer->advance(lexer, false);
    second = lexer->lookahead;
  }

  const BoundaryTarget *target = find_boundary_target(first, second);
  if (
    target !=
    NULL &&
    emit_first_valid_marker(lexer, valid_symbols, &target->markers)
  ) {
    return true;
  }

  // A sign, a negation, or a slash that cannot continue the preceding
  // expression starts a new value instead.
  return character_starts_expression(first) &&
    emit_first_valid_marker(lexer, valid_symbols, &VALUE_MARKER_LIST);
}

// Classifies what follows a run of line continuations and emits the marker
// that names it, so that the parser can place the continuations and the
// marker records the lookahead that made the choice. Where no marker can
// place them, the continuations follow an absent action instead.
static bool scan_boundary_target(TSLexer *lexer, const bool *valid_symbols) {
  return emit_boundary_target_marker(lexer, valid_symbols) ||
    emit_action_recovery(lexer, valid_symbols);
}

static bool has_line_continuation_marker(const bool *valid_symbols) {
  for (
    enum TokenType token = LC_MARKER_FIRST; token <= LC_MARKER_LAST; token++
  ) {
    if (valid_symbols[token]) {
      return true;
    }
  }
  return false;
}

static bool
scan_line_continuation_marker(TSLexer *lexer, const bool *valid_symbols) {
  if (
    !advance_line_continuations(lexer) || !advance_boundary_gap_remainder(lexer)
  ) {
    return false;
  }

  return scan_boundary_target(lexer, valid_symbols);
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
  state->mode = LEXICAL_MODE_OUTSIDE;
  if (length == SERIALIZED_SCANNER_STATE_SIZE) {
    const unsigned char mode = (unsigned char)buffer[0];
    if (mode <= LEXICAL_MODE_STRING) {
      state->mode = (LexicalMode)mode;
    }
  }
}

static bool scan_ere_context(
  ScannerState *state,
  TSLexer *lexer,
  const bool *valid_symbols
) {
  lexer->mark_end(lexer);

  const bool at_ere_end = lexer->lookahead == '\n' || lexer->eof(lexer);
  if (at_ere_end && valid_symbols[ERE_LEXICAL_END]) {
    return emit_mode(state, lexer, LEXICAL_MODE_OUTSIDE, ERE_LEXICAL_END);
  }

  if (state->mode == LEXICAL_MODE_ERE_ESCAPED_DELIMITER) {
    if (lexer->lookahead == '/' && valid_symbols[ERE_ESCAPED_DELIMITER_END]) {
      lexer->advance(lexer, false);
      lexer->mark_end(lexer);
      return emit_mode(
        state,
        lexer,
        LEXICAL_MODE_ERE_BODY,
        ERE_ESCAPED_DELIMITER_END
      );
    }
    return scan_ere_backslash_context(state, lexer, valid_symbols);
  }

  if (!valid_symbols[ERROR_SENTINEL]) {
    for (size_t i = 0; i < ARRAY_LENGTH(ERE_COMPOUND_GUARDS); i++) {
      if (
        valid_symbols[ERE_COMPOUND_GUARDS[i].guard] &&
        lexer->lookahead == ERE_COMPOUND_GUARDS[i].opening
      ) {
        return scan_ere_compound_guard(lexer, ERE_COMPOUND_GUARDS[i].guard);
      }
    }
  }

  if (lexer->lookahead == '/' && valid_symbols[ERE_CLOSING]) {
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    return emit_mode(state, lexer, LEXICAL_MODE_OUTSIDE, ERE_CLOSING);
  }

  if (lexer->lookahead == '-' && valid_symbols[ERE_CLOSING_HYPHEN]) {
    lexer->advance(lexer, false);
    if (lexer->lookahead != ']') {
      return false;
    }
    lexer->mark_end(lexer);
    return emit(lexer, ERE_CLOSING_HYPHEN);
  }

  return scan_ere_backslash_context(state, lexer, valid_symbols);
}

// A string ends lexically at its closing quote, a raw newline, or EOF; the
// zero-width end returns the scanner to OUTSIDE before the grammar's closing
// quote (or the standard recovery for its absence) takes over.
static bool scan_string_context(
  ScannerState *state,
  TSLexer *lexer,
  const bool *valid_symbols
) {
  lexer->mark_end(lexer);
  const bool at_string_end =
    lexer->lookahead == '"' || lexer->lookahead == '\n' || lexer->eof(lexer);
  if (at_string_end && valid_symbols[STRING_END]) {
    return emit_mode(state, lexer, LEXICAL_MODE_OUTSIDE, STRING_END);
  }
  return false;
}

static bool scan_string_opening(ScannerState *state, TSLexer *lexer) {
  lexer->advance(lexer, false);
  lexer->mark_end(lexer);
  return emit_mode(state, lexer, LEXICAL_MODE_STRING, STRING_OPENING);
}

static bool scan_comment(TSLexer *lexer) {
  advance_comment_to_boundary(lexer);
  lexer->mark_end(lexer);
  return emit(lexer, COMMENT);
}

static bool has_number_marker(const bool *valid_symbols) {
  return valid_symbols[NUMBER_INTEGER] ||
    valid_symbols[NUMBER_FRACTION] ||
    valid_symbols[NUMBER_EXPONENT];
}

static bool has_required_target_guard(const bool *valid_symbols) {
  return valid_symbols[ACTION_TARGET_GUARD] ||
    valid_symbols[EXPRESSION_TARGET_GUARD] ||
    valid_symbols[PRINT_EXPRESSION_TARGET_GUARD] ||
    valid_symbols[PARAMETER_TARGET_GUARD];
}

static bool scan_word_or_item_boundary(
  TSLexer *lexer,
  const bool *valid_symbols,
  bool allow_item_boundary
) {
  const bool word_is_valid = has_word_marker(valid_symbols);
  const bool item_boundary_is_valid = allow_item_boundary &&
    (valid_symbols[CLOSED_ITEM_BOUNDARY] ||
      valid_symbols[NORMAL_PATTERN_ITEM_BOUNDARY]);
  if (!word_is_valid && !item_boundary_is_valid) {
    return false;
  }

  const WordKind kind = scan_word_spelling(lexer);
  if (
    item_boundary_is_valid &&
    emit_word_item_boundary(lexer, valid_symbols, kind)
  ) {
    return true;
  }
  return word_is_valid && scan_word_token(lexer, valid_symbols, kind);
}

static bool scan_number_or_item_boundary(
  TSLexer *lexer,
  const bool *valid_symbols,
  bool allow_item_boundary
) {
  const bool item_boundary_is_valid =
    allow_item_boundary && valid_symbols[CLOSED_ITEM_BOUNDARY];
  if (!item_boundary_is_valid && !has_number_marker(valid_symbols)) {
    return false;
  }

  const NumberKind kind = scan_number_kind(lexer, !item_boundary_is_valid);
  if (kind == NUMBER_KIND_NONE) {
    return false;
  }
  if (item_boundary_is_valid) {
    return emit(lexer, CLOSED_ITEM_BOUNDARY);
  }
  return emit_number_kind(lexer, valid_symbols, kind);
}

bool tree_sitter_posix_awk_external_scanner_scan(
  void *payload,
  TSLexer *lexer,
  const bool *valid_symbols
) {
  ScannerState *state = payload;
  switch (state->mode) {
  case LEXICAL_MODE_ERE_BODY:
  case LEXICAL_MODE_ERE_ESCAPED_DELIMITER:
    return scan_ere_context(state, lexer, valid_symbols);
  case LEXICAL_MODE_STRING:
    return scan_string_context(state, lexer, valid_symbols);
  case LEXICAL_MODE_OUTSIDE:
    break;
  }

  // Error recovery marks every token valid, so only real tokens are scanned
  // there: the zero-width guards, markers, boundaries, and recoveries would
  // otherwise match at any position.
  const bool recovering = valid_symbols[ERROR_SENTINEL];
  skip_ascii_blanks(lexer);
  lexer->mark_end(lexer);

  if (lexer->lookahead == '#' && valid_symbols[COMMENT]) {
    return scan_comment(lexer);
  }

  if (!recovering) {
    if (lexer->lookahead == '\n' && has_required_target_guard(valid_symbols)) {
      return scan_required_target_guard(lexer, valid_symbols);
    }
    if (
      valid_symbols[CLOSED_ITEM_BOUNDARY] &&
      character_starts_item(lexer->lookahead)
    ) {
      return emit(lexer, CLOSED_ITEM_BOUNDARY);
    }
    if (
      valid_symbols[STATEMENT_RECOVERY] &&
      (lexer->lookahead == '}' || lexer->eof(lexer))
    ) {
      return emit(lexer, STATEMENT_RECOVERY);
    }
    if (
      lexer->lookahead ==
      '\\' &&
      (has_line_continuation_marker(valid_symbols) ||
        valid_symbols[STATEMENT_RECOVERY] ||
        valid_symbols[ACTION_RECOVERY])
    ) {
      return scan_line_continuation_marker(lexer, valid_symbols);
    }
    if (lexer->lookahead != '{' && emit_action_recovery(lexer, valid_symbols)) {
      return true;
    }
  }

  if (lexer->lookahead == '"' && valid_symbols[STRING_OPENING]) {
    return scan_string_opening(state, lexer);
  }

  if (is_word_start(lexer->lookahead)) {
    return scan_word_or_item_boundary(lexer, valid_symbols, !recovering);
  }
  if (is_ascii_digit(lexer->lookahead) || lexer->lookahead == '.') {
    return scan_number_or_item_boundary(lexer, valid_symbols, !recovering);
  }
  if (
    lexer->lookahead ==
    '/' &&
    (valid_symbols[DIVISION_SLASH] ||
      valid_symbols[ERE_OPENING_SLASH] ||
      valid_symbols[DIV_ASSIGN_OPERATOR])
  ) {
    return scan_slash_start(state, lexer, valid_symbols, recovering);
  }
  if (
    lexer->lookahead == '>' && has_greater_start(valid_symbols, !recovering)
  ) {
    return scan_greater_start(lexer, valid_symbols, !recovering);
  }
  if (has_valid_composite_operator_start(lexer->lookahead, valid_symbols)) {
    return scan_composite_operator_start(lexer, valid_symbols);
  }
  return false;
}
