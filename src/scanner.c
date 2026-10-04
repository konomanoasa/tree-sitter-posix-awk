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
  GETLINE_TARGET_WORD,
  GETLINE_OMITTED_WORD,
  GETLINE_FIELD_WORD,
  GETLINE_PREFER_TARGET_WORD,
  GETLINE_PREFER_OMITTED_WORD,
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
  LITERAL_BREAK,
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

static bool scan_lvalue_start(TSLexer *lexer) {
  if (!skip_token_layout(lexer)) {
    return false;
  }
  if (lexer->lookahead == '$') {
    return true;
  }
  return scan_word_spelling(lexer) == NAME_WORD && lexer->lookahead != '(';
}

// Skips a string or an ERE through its closing delimiter.
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

typedef enum {
  NUMBER_ABSENT,
  NUMBER_WHOLE,
  NUMBER_BEFORE_WORD,
} NumberExtent;

// Skips a number and, for a token, marks its end. A BEFORE_WORD number ends
// in front of an exponent marker that was consumed in search of its digits.
static NumberExtent skip_number(TSLexer *lexer, bool token) {
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
    return NUMBER_ABSENT;
  }
  if (token) {
    lexer->mark_end(lexer);
  }
  if (lexer->lookahead == 'e' || lexer->lookahead == 'E') {
    lexer->advance(lexer, false);
    if (lexer->lookahead == '+' || lexer->lookahead == '-') {
      lexer->advance(lexer, false);
    }
    if (!is_ascii_digit(lexer->lookahead)) {
      return NUMBER_BEFORE_WORD;
    }
    floating = true;
    do {
      lexer->advance(lexer, false);
    } while (is_ascii_digit(lexer->lookahead));
    if (token) {
      lexer->mark_end(lexer);
    }
  }
  if (floating && character_in(lexer->lookahead, "fFlL")) {
    lexer->advance(lexer, false);
    if (token) {
      lexer->mark_end(lexer);
    }
  }
  return NUMBER_WHOLE;
}

// Skips a bracketed group. A slash opens an ERE behind an operator and
// divides behind an operand, which includes an update operator: a prefix
// update is followed by an lvalue, never by a slash. A raw newline is only
// valid behind a comma or a logical operator.
static bool skip_group(TSLexer *lexer) {
  bool operand = false;
  bool separated = false;
  unsigned depth = 0;
  do {
    const int32_t character = lexer->lookahead;
    if (lexer->eof(lexer) || (character == '\n' && !separated)) {
      return false;
    }
    if (character == '#') {
      do {
        lexer->advance(lexer, false);
      } while (lexer->lookahead != '\n' && !lexer->eof(lexer));
      continue;
    }
    if (character_in(character, " \t\n")) {
      lexer->advance(lexer, false);
      continue;
    }
    if (character == '\\') {
      lexer->advance(lexer, false);
      if (lexer->lookahead != '\n') {
        return false;
      }
      lexer->advance(lexer, false);
      continue;
    }
    separated = false;
    if (character == '"' || (character == '/' && !operand)) {
      if (!skip_literal(lexer)) {
        return false;
      }
      operand = true;
      continue;
    }
    if (is_word_start(character)) {
      scan_word_spelling(lexer);
      operand = true;
      continue;
    }
    lexer->advance(lexer, false);
    if (character_in(character, "([")) {
      depth++;
      operand = false;
    } else if (character_in(character, ")]")) {
      depth--;
      operand = true;
    } else if (character_in(character, "+-") && lexer->lookahead == character) {
      lexer->advance(lexer, false);
      operand = true;
    } else if (character_in(character, "&|") && lexer->lookahead == character) {
      lexer->advance(lexer, false);
      operand = false;
      separated = true;
    } else {
      operand = is_word_continue(character) || character == '.';
      separated = character == ',';
    }
  } while (depth > 0);
  return true;
}

// This cursor can revisit a prospective target without rewinding Tree-sitter's
// lexer. Only the real lexer's furthest lookahead matters to token
// invalidation.
typedef struct {
  TSLexer lexer;
  TSLexer *source;
  int32_t *characters;
  size_t length;
  size_t capacity;
  size_t position;
  size_t marked_end;
  bool failed;
  bool pipe_checked;
  bool may_have_pipe;
  bool deferred_target;
} InputProbe;

static void probe_restore(InputProbe *probe, size_t position) {
  probe->position = position;
  probe->lexer.lookahead = position < probe->length
    ? probe->characters[position]
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
      if (probe->capacity > SIZE_MAX / 2 / sizeof(*probe->characters)) {
        probe->failed = true;
        lexer->lookahead = 0;
        return;
      }
      const size_t capacity = probe->capacity ? probe->capacity * 2 : 64;
      int32_t *characters =
        ts_realloc(probe->characters, capacity * sizeof(*characters));
      if (characters == NULL) {
        probe->failed = true;
        lexer->lookahead = 0;
        return;
      }
      probe->characters = characters;
      probe->capacity = capacity;
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

enum ProbeToken {
  PROBE_END = 0,
  PROBE_VALUE = 256,
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

static int probe_token(InputProbe *probe, bool operand) {
  TSLexer *lexer = &probe->lexer;
  if (!skip_token_layout(lexer) || probe_eof(lexer)) {
    return PROBE_END;
  }
  const int32_t character = lexer->lookahead;
  if (is_ascii_digit(character) || character == '.') {
    const NumberExtent extent = skip_number(lexer, true);
    if (extent == NUMBER_ABSENT) {
      return PROBE_END;
    }
    probe_restore(probe, probe->marked_end);
    return PROBE_VALUE;
  }
  if (character == '"' || (operand && character == '/')) {
    return skip_literal(lexer) ? PROBE_VALUE : PROBE_END;
  }
  if (character == '(') {
    return skip_group(lexer) ? PROBE_VALUE : PROBE_END;
  }
  if (is_word_start(character)) {
    const enum TokenType word = scan_word_spelling(lexer);
    const bool call = word == NAME_WORD && lexer->lookahead == '(';
    if (word == GETLINE_WORD) {
      return PROBE_GETLINE;
    }
    if (word == IN_WORD) {
      return PROBE_MEMBERSHIP;
    }
    if (word != NAME_WORD && word != BUILTIN_FUNC_NAME_WORD) {
      return PROBE_END;
    }
    if (!skip_token_layout(lexer)) {
      return PROBE_END;
    }
    if (call || word == BUILTIN_FUNC_NAME_WORD) {
      return lexer->lookahead != '(' || skip_group(lexer) ? PROBE_VALUE
                                                          : PROBE_END;
    }
    return lexer->lookahead != '[' || skip_group(lexer) ? PROBE_NAME
                                                        : PROBE_END;
  }
  lexer->advance(lexer, false);
  if (character_in(character, "+-") && lexer->lookahead == character) {
    lexer->advance(lexer, false);
    return PROBE_UPDATE;
  }
  if (character_in(character, "&|") && lexer->lookahead == character) {
    lexer->advance(lexer, false);
    while (!probe_eof(lexer)) {
      if (!skip_token_layout(lexer)) {
        return PROBE_END;
      }
      if (lexer->lookahead == '#') {
        while (!probe_eof(lexer) && lexer->lookahead != '\n') {
          lexer->advance(lexer, false);
        }
      } else if (lexer->lookahead == '\n') {
        lexer->advance(lexer, false);
      } else {
        break;
      }
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

typedef struct {
  bool valid;
  bool lvalue;
  // A surrounding field can finish before a postfix update; unary operands
  // cannot.
  bool postfix;
  bool redirected;
} ProbeExpression;

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
    return EXPR_POSTFIX;
  case PROBE_NAME:
  case PROBE_VALUE:
  case PROBE_GETLINE:
  case '$':
  case '!':
    return EXPR_CONCATENATION;
  default:
    return 0;
  }
}

static bool probe_has_pipe(InputProbe *input) {
  if (input->pipe_checked) {
    return input->may_have_pipe;
  }
  input->pipe_checked = true;
  const size_t start = input->position;
  probe_restore(input, 0);
  bool operand = true;
  bool found = false;
  for (;;) {
    const int token = probe_token(input, operand);
    if (token == '|') {
      found = true;
      break;
    }
    if (probe_precedence(token) == 0 && !character_in(token, "$!:")) {
      break;
    }
    operand = token !=
      PROBE_VALUE &&
      token !=
      PROBE_NAME &&
      token !=
      PROBE_GETLINE &&
      token != PROBE_UPDATE;
  }
  probe_restore(input, start);
  input->may_have_pipe = found;
  return found;
}

typedef enum {
  PROBE_EXPRESSION = 1,
  PROBE_PREFIX,
  PROBE_LVALUE,
  PROBE_GET,
  PROBE_TARGET_TAIL,
} ProbeRule;

typedef struct {
  size_t position;
  ProbeRule rule;
  int argument;
} ProbeKey;

typedef struct {
  ProbeKey key;
  size_t end;
  ProbeExpression expression;
} ProbeMemo;

enum ProbeStep {
  PROBE_START,
  PROBE_AFTER_TARGET_UPDATE,
  PROBE_AFTER_TARGET_LVALUE,
  PROBE_AFTER_FIELD_OPERAND,
  PROBE_AFTER_PREFIX_OPERAND,
  PROBE_AFTER_UNARY_OPERAND,
  PROBE_GET_TARGET,
  PROBE_GET_TAIL,
  PROBE_GET_SOURCE,
  PROBE_GET_REDIRECTED,
  PROBE_EXPR_OPERAND,
  PROBE_EXPR_NEXT,
  PROBE_EXPR_UPDATE,
  PROBE_EXPR_OPERATOR,
  PROBE_EXPR_CONSEQUENCE,
  PROBE_EXPR_RIGHT,
  PROBE_EXPR_PIPE,
  PROBE_EXPR_PIPE_SOURCE,
  PROBE_EXPR_PIPE_GET,
};

enum ProbeGetContext {
  PROBE_FIELD_GET = 1,
  PROBE_SOURCE_ALLOWED = 2,
  PROBE_NO_PIPE = 256,
};

typedef struct {
  ProbeKey key;
  unsigned stage;
  size_t end;
  size_t operand;
  size_t pipe_end;
  int token;
  int precedence;
  int non_associative;
  int ceiling;
  ProbeExpression left;
  ProbeExpression target;
} ProbeFrame;

typedef struct {
  InputProbe *input;
  ProbeFrame *frames;
  size_t length;
  size_t capacity;
  ProbeMemo *memo;
  size_t memo_length;
  size_t memo_capacity;
  ProbeExpression result;
} ProbeParser;

static size_t probe_hash(ProbeKey key) {
  return (key.position * 2654435761u) ^
    ((unsigned)key.rule * 97u + (unsigned)key.argument);
}

static ProbeMemo *probe_memo_slot(ProbeParser *parser, ProbeKey key) {
  size_t slot = probe_hash(key) & (parser->memo_capacity - 1);
  while (parser->memo[slot].key.rule != 0) {
    const ProbeKey found = parser->memo[slot].key;
    if (
      found.position ==
      key.position &&
      found.rule ==
      key.rule &&
      found.argument == key.argument
    ) {
      break;
    }
    slot = (slot + 1) & (parser->memo_capacity - 1);
  }
  return &parser->memo[slot];
}

static bool probe_memo_reserve(ProbeParser *parser) {
  if (parser->memo_length * 2 < parser->memo_capacity) {
    return true;
  }
  const size_t old_capacity = parser->memo_capacity;
  if (old_capacity > SIZE_MAX / 2 / sizeof(*parser->memo)) {
    parser->input->failed = true;
    return false;
  }
  const size_t capacity = old_capacity ? old_capacity * 2 : 64;
  ProbeMemo *memo = ts_calloc(capacity, sizeof(*memo));
  if (memo == NULL) {
    parser->input->failed = true;
    return false;
  }
  ProbeMemo *old = parser->memo;
  parser->memo = memo;
  parser->memo_capacity = capacity;
  for (size_t i = 0; i < old_capacity; i++) {
    if (old[i].key.rule != 0) {
      *probe_memo_slot(parser, old[i].key) = old[i];
    }
  }
  ts_free(old);
  return true;
}

// Enclosing rules request every other rule again at the same position.
static bool probe_memoizes(ProbeRule rule) {
  return rule != PROBE_PREFIX && rule != PROBE_GET;
}

static void probe_call(ProbeParser *parser, ProbeRule rule, int argument) {
  const ProbeKey key = {parser->input->position, rule, argument};
  if (probe_memoizes(rule)) {
    if (!probe_memo_reserve(parser)) {
      return;
    }
    const ProbeMemo *memo = probe_memo_slot(parser, key);
    if (memo->key.rule != 0) {
      probe_restore(parser->input, memo->end);
      parser->result = memo->expression;
      return;
    }
  }
  if (parser->length == parser->capacity) {
    if (parser->capacity > SIZE_MAX / 2 / sizeof(*parser->frames)) {
      parser->input->failed = true;
      return;
    }
    const size_t capacity = parser->capacity ? parser->capacity * 2 : 32;
    ProbeFrame *frames = ts_realloc(parser->frames, capacity * sizeof(*frames));
    if (frames == NULL) {
      parser->input->failed = true;
      return;
    }
    parser->frames = frames;
    parser->capacity = capacity;
  }
  parser->frames[parser->length++] = (ProbeFrame){.key = key};
}

static void probe_return(ProbeParser *parser, ProbeExpression result) {
  const ProbeKey key = parser->frames[--parser->length].key;
  parser->result = result;
  if (probe_memoizes(key.rule) && probe_memo_reserve(parser)) {
    ProbeMemo *memo = probe_memo_slot(parser, key);
    if (memo->key.rule == 0) {
      parser->memo_length++;
    }
    *memo = (ProbeMemo){key, parser->input->position, result};
  }
}

// The explicit call stack and memo table are local to this one lookahead.
// Nested input functions must neither exhaust the C stack nor repeatedly
// explore the same target after its enclosing getline omits that target.
static ProbeExpression
probe_parse(InputProbe *input, ProbeRule rule, int argument) {
  ProbeParser parser = {.input = input};
  probe_call(&parser, rule, argument);
  while (parser.length && !input->failed) {
    ProbeFrame *frame = &parser.frames[parser.length - 1];
    switch (frame->key.rule) {
    case PROBE_TARGET_TAIL:
      if (frame->stage == PROBE_AFTER_TARGET_LVALUE) {
        probe_restore(input, frame->key.position);
        probe_return(&parser, (ProbeExpression){.valid = parser.result.valid});
      } else if (frame->stage == PROBE_AFTER_TARGET_UPDATE) {
        if (parser.result.valid) {
          frame->stage = PROBE_AFTER_TARGET_LVALUE;
          probe_call(&parser, PROBE_TARGET_TAIL, 0);
        } else {
          probe_restore(input, frame->key.position);
          probe_return(&parser, (ProbeExpression){0});
        }
      } else if (
        !skip_token_layout(&input->lexer) ||
        !character_in(input->lexer.lookahead, "+-") ||
        probe_token(input, false) != PROBE_UPDATE
      ) {
        probe_restore(input, frame->key.position);
        probe_return(&parser, (ProbeExpression){.valid = true});
      } else {
        frame->stage = PROBE_AFTER_TARGET_UPDATE;
        probe_call(&parser, PROBE_LVALUE, true);
      }
      break;
    case PROBE_LVALUE:
      if (frame->stage == PROBE_AFTER_FIELD_OPERAND) {
        ProbeExpression result = parser.result;
        result.lvalue = result.valid;
        probe_return(&parser, result);
        break;
      }
      if (
        !skip_token_layout(&input->lexer) ||
        (!is_word_start(input->lexer.lookahead) &&
          input->lexer.lookahead != '$')
      ) {
        probe_restore(input, frame->key.position);
        probe_return(&parser, (ProbeExpression){0});
        break;
      }
      frame->token = probe_token(input, true);
      if (frame->token == PROBE_NAME) {
        probe_return(&parser, (ProbeExpression){true, true, true, false});
      } else if (frame->token != '$') {
        probe_restore(input, frame->key.position);
        probe_return(&parser, (ProbeExpression){0});
      } else {
        do {
          frame->operand = input->position;
        } while (probe_token(input, true) == '$');
        probe_restore(input, frame->operand);
        frame->stage = PROBE_AFTER_FIELD_OPERAND;
        probe_call(&parser, PROBE_PREFIX, !frame->key.argument | PROBE_NO_PIPE);
      }
      break;
    case PROBE_PREFIX:
      if (frame->stage == PROBE_AFTER_PREFIX_OPERAND) {
        probe_return(&parser, parser.result);
        break;
      }
      if (frame->stage == PROBE_AFTER_UNARY_OPERAND) {
        probe_return(&parser, (ProbeExpression){.valid = parser.result.valid});
        break;
      }
      switch (probe_token(input, true)) {
      case PROBE_VALUE:
        probe_return(
          &parser,
          (ProbeExpression){.valid = true, .postfix = true}
        );
        break;
      case PROBE_NAME:
        probe_return(&parser, (ProbeExpression){true, true, true, false});
        break;
      case PROBE_GETLINE:
        frame->stage = PROBE_AFTER_PREFIX_OPERAND;
        probe_call(
          &parser,
          PROBE_GET,
          (frame->key.argument & PROBE_FIELD_GET) | PROBE_SOURCE_ALLOWED
        );
        break;
      case '$':
        probe_restore(input, frame->key.position);
        frame->stage = PROBE_AFTER_PREFIX_OPERAND;
        probe_call(&parser, PROBE_LVALUE, false);
        break;
      case PROBE_UPDATE:
        frame->stage = PROBE_AFTER_UNARY_OPERAND;
        probe_call(&parser, PROBE_LVALUE, true);
        break;
      case '+':
      case '-':
      case '!':
        frame->stage = PROBE_AFTER_UNARY_OPERAND;
        probe_call(
          &parser,
          PROBE_EXPRESSION,
          EXPR_UNARY | (frame->key.argument & PROBE_NO_PIPE)
        );
        break;
      default:
        probe_restore(input, frame->key.position);
        probe_return(&parser, (ProbeExpression){0});
        break;
      }
      break;
    case PROBE_GET:
      switch (frame->stage) {
      case PROBE_START:
        frame->stage = PROBE_GET_TARGET;
        probe_call(&parser, PROBE_LVALUE, false);
        break;
      case PROBE_GET_TARGET:
        frame->target = parser.result;
        if (!frame->target.valid) {
          probe_restore(input, frame->key.position);
          frame->stage = PROBE_GET_SOURCE;
        } else if (frame->key.argument & PROBE_FIELD_GET) {
          frame->stage = PROBE_GET_SOURCE;
        } else {
          frame->stage = PROBE_GET_TAIL;
          probe_call(&parser, PROBE_TARGET_TAIL, 0);
        }
        break;
      case PROBE_GET_TAIL:
        if (!parser.result.valid) {
          // An enclosing field may take this update instead of the target.
          input->deferred_target = true;
          probe_restore(input, frame->key.position);
          frame->target = (ProbeExpression){0};
        }
        frame->stage = PROBE_GET_SOURCE;
        break;
      case PROBE_GET_SOURCE:
        frame->left = (ProbeExpression){
          .valid = true,
          .postfix = !frame->target.valid || frame->target.postfix,
          .redirected = frame->target.redirected,
        };
        frame->end = input->position;
        if (
          (frame->key.argument & PROBE_SOURCE_ALLOWED) &&
          probe_token(input, false) == '<'
        ) {
          frame->stage = PROBE_GET_REDIRECTED;
          probe_call(&parser, PROBE_EXPRESSION, EXPR_ASSIGNMENT);
        } else {
          probe_restore(input, frame->end);
          probe_return(&parser, frame->left);
        }
        break;
      default:
        probe_return(
          &parser,
          (ProbeExpression){
            .valid = parser.result.valid,
            .postfix = true,
            .redirected = true,
          }
        );
        break;
      }
      break;
    case PROBE_EXPRESSION:
      switch (frame->stage) {
      case PROBE_START:
        frame->stage = PROBE_EXPR_OPERAND;
        probe_call(&parser, PROBE_PREFIX, frame->key.argument & PROBE_NO_PIPE);
        break;
      case PROBE_EXPR_OPERAND:
        if (!parser.result.valid) {
          probe_return(&parser, parser.result);
        } else {
          frame->left = parser.result;
          frame->stage = PROBE_EXPR_NEXT;
        }
        break;
      case PROBE_EXPR_NEXT:
        frame->end = input->position;
        frame->token = probe_token(input, false);
        frame->precedence = probe_precedence(frame->token);
        if (frame->token == '|' && frame->key.argument == EXPR_ASSIGNMENT) {
          if (probe_token(input, false) != PROBE_GETLINE) {
            probe_restore(input, frame->end);
            probe_return(&parser, frame->left);
          } else {
            frame->stage = PROBE_EXPR_PIPE;
            probe_call(&parser, PROBE_GET, 0);
          }
        } else if (frame->token == PROBE_UPDATE) {
          frame->operand = input->position;
          parser.result = (ProbeExpression){
            .valid = scan_lvalue_start(&input->lexer),
          };
          frame->stage = PROBE_EXPR_UPDATE;
        } else {
          frame->stage = PROBE_EXPR_OPERATOR;
        }
        break;
      case PROBE_EXPR_UPDATE: {
        const bool prefix = parser.result.valid;
        probe_restore(input, frame->operand);
        if (
          (frame->key.argument & ~PROBE_NO_PIPE) <=
          EXPR_POSTFIX &&
          frame->left.lvalue &&
          frame->left.postfix &&
          (!frame->left.redirected || !prefix)
        ) {
          frame->left = (ProbeExpression){.valid = true};
          frame->stage = PROBE_EXPR_NEXT;
        } else {
          frame->precedence = prefix ? EXPR_CONCATENATION : 0;
          frame->stage = PROBE_EXPR_OPERATOR;
        }
        break;
      }
      case PROBE_EXPR_OPERATOR: {
        const int precedence = frame->precedence;
        if (
          precedence <
          (frame->key.argument & ~PROBE_NO_PIPE) ||
          (frame->ceiling != 0 && precedence > frame->ceiling) ||
          precedence ==
          frame->non_associative ||
          (precedence == EXPR_ASSIGNMENT && !frame->left.lvalue)
        ) {
          if (
            !(frame->key.argument & PROBE_NO_PIPE) &&
            (precedence > 0 || frame->token == '|') &&
            probe_has_pipe(input)
          ) {
            probe_restore(input, frame->key.position);
            frame->stage = PROBE_EXPR_PIPE_SOURCE;
            probe_call(
              &parser,
              PROBE_EXPRESSION,
              EXPR_ASSIGNMENT | PROBE_NO_PIPE
            );
          } else {
            probe_restore(input, frame->end);
            probe_return(&parser, frame->left);
          }
          break;
        }
        if (precedence == EXPR_CONCATENATION) {
          probe_restore(input, frame->end);
        }
        if (frame->token == '?') {
          frame->stage = PROBE_EXPR_CONSEQUENCE;
          probe_call(
            &parser,
            PROBE_EXPRESSION,
            EXPR_ASSIGNMENT | (frame->key.argument & PROBE_NO_PIPE)
          );
        } else if (frame->token == PROBE_MEMBERSHIP) {
          parser.result = (ProbeExpression){
            .valid = probe_token(input, true) == PROBE_NAME,
          };
          frame->stage = PROBE_EXPR_RIGHT;
        } else {
          const int right_precedence = expression_right_precedence(precedence);
          frame->stage = PROBE_EXPR_RIGHT;
          probe_call(
            &parser,
            PROBE_EXPRESSION,
            right_precedence | (frame->key.argument & PROBE_NO_PIPE)
          );
        }
        break;
      }
      case PROBE_EXPR_CONSEQUENCE:
        if (!parser.result.valid || probe_token(input, false) != ':') {
          probe_restore(input, frame->end);
          probe_return(&parser, frame->left);
        } else {
          frame->stage = PROBE_EXPR_RIGHT;
          probe_call(
            &parser,
            PROBE_EXPRESSION,
            EXPR_CONDITIONAL | (frame->key.argument & PROBE_NO_PIPE)
          );
        }
        break;
      case PROBE_EXPR_RIGHT:
      case PROBE_EXPR_PIPE:
      case PROBE_EXPR_PIPE_GET:
        if (
          !parser.result.valid ||
          (frame->stage ==
            PROBE_EXPR_PIPE_GET &&
            (input->position <
              frame->end ||
              input->position == frame->pipe_end))
        ) {
          probe_restore(input, frame->end);
          probe_return(&parser, frame->left);
        } else {
          if (frame->stage == PROBE_EXPR_PIPE_GET) {
            frame->pipe_end = input->position;
          }
          frame->non_associative = frame->stage ==
              PROBE_EXPR_RIGHT &&
              expression_non_associative(frame->precedence)
            ? frame->precedence
            : 0;
          frame->ceiling =
            frame->stage == PROBE_EXPR_RIGHT ? frame->precedence : 0;
          frame->left = (ProbeExpression){.valid = true};
          frame->stage = PROBE_EXPR_NEXT;
        }
        break;
      case PROBE_EXPR_PIPE_SOURCE:
        if (
          !parser.result.valid ||
          probe_token(input, false) !=
          '|' ||
          probe_token(input, false) != PROBE_GETLINE
        ) {
          probe_restore(input, frame->end);
          probe_return(&parser, frame->left);
        } else {
          frame->stage = PROBE_EXPR_PIPE_GET;
          probe_call(&parser, PROBE_GET, 0);
        }
        break;
      }
      break;
    }
  }
  const ProbeExpression result =
    input->failed ? (ProbeExpression){0} : parser.result;
  ts_free(parser.frames);
  ts_free(parser.memo);
  return result;
}

typedef enum {
  PROBE_SOURCE,
  PROBE_UNARY_TARGET,
  PROBE_PREFIX_GETLINE,
} ProbeContext;

static bool
probe_getline_field(TSLexer *lexer, ProbeContext context, bool *deferred) {
  InputProbe probe = {
    .lexer =
      {
        .lookahead = lexer->lookahead,
        .advance = probe_advance,
        .mark_end = probe_mark_end,
        .eof = probe_eof,
      },
    .source = lexer,
  };
  bool survives = true;
  bool source = context == PROBE_SOURCE;
  bool tail = false;
  if (context == PROBE_UNARY_TARGET) {
    if (
      probe_parse(&probe, PROBE_EXPRESSION, EXPR_UNARY | PROBE_NO_PIPE).valid
    ) {
      source = probe_token(&probe, false) == '<';
    }
  } else if (context == PROBE_PREFIX_GETLINE) {
    // This getline can hand its target the next update, so an update strands
    // only behind its own source or where it has no target.
    const bool targeted = probe_parse(&probe, PROBE_LVALUE, false).valid;
    const size_t end = probe.position;
    source = probe_token(&probe, false) == '<';
    if (!source) {
      probe_restore(&probe, end);
      tail = !targeted;
    }
  }
  if (source) {
    tail = probe_parse(&probe, PROBE_EXPRESSION, EXPR_ASSIGNMENT).valid;
  }
  if (tail) {
    survives = probe_parse(&probe, PROBE_TARGET_TAIL, 0).valid;
  }
  *deferred |= probe.deferred_target;
  ts_free(probe.characters);
  return survives;
}

typedef enum {
  LVALUE_SHAPE_NONE,
  LVALUE_SHAPE_POSTFIX,
  LVALUE_SHAPE_SETTLED,
  LVALUE_SHAPE_OPEN,
} LvalueShape;

// OPEN lvalues accept a postfix update. SETTLED lvalues leave no update
// operator to place: they end in a unary operand, precede another operand, or
// are malformed. POSTFIX fields leave an update outside their getline source.
static LvalueShape
skip_lvalue(TSLexer *lexer, bool prefix_update, bool *deferred) {
  bool targeted = false;
  for (;;) {
    if (!skip_token_layout(lexer)) {
      return LVALUE_SHAPE_SETTLED;
    }
    bool field = false;
    while (lexer->lookahead == '$') {
      field = true;
      lexer->advance(lexer, false);
      if (!skip_token_layout(lexer)) {
        return LVALUE_SHAPE_SETTLED;
      }
      if (character_in(lexer->lookahead, "+-!")) {
        // A source may follow the unary operand of a getline target.
        return targeted &&
            !probe_getline_field(lexer, PROBE_UNARY_TARGET, deferred)
          ? LVALUE_SHAPE_POSTFIX
          : LVALUE_SHAPE_SETTLED;
      }
    }
    bool name = false;
    bool input = false;
    if (
      field && (is_ascii_digit(lexer->lookahead) || lexer->lookahead == '.')
    ) {
      if (skip_number(lexer, false) != NUMBER_WHOLE) {
        return LVALUE_SHAPE_SETTLED;
      }
    } else if (field && lexer->lookahead == '(') {
      if (!skip_group(lexer)) {
        return LVALUE_SHAPE_SETTLED;
      }
    } else if (field && character_in(lexer->lookahead, "\"/")) {
      if (!skip_literal(lexer)) {
        return LVALUE_SHAPE_SETTLED;
      }
    } else {
      const enum TokenType word = scan_word_spelling(lexer);
      const bool call = word == NAME_WORD && lexer->lookahead == '(';
      if (field && word == GETLINE_WORD) {
        if (prefix_update) {
          return probe_getline_field(lexer, PROBE_PREFIX_GETLINE, deferred)
            ? LVALUE_SHAPE_SETTLED
            : LVALUE_SHAPE_POSTFIX;
        }
        if (!skip_token_layout(lexer)) {
          return LVALUE_SHAPE_SETTLED;
        }
        if (is_word_start(lexer->lookahead) || lexer->lookahead == '$') {
          targeted = true;
          continue;
        }
        input = true;
      } else if (field && (call || word == BUILTIN_FUNC_NAME_WORD)) {
        // A built-in name may omit its arguments.
        if (
          !skip_token_layout(lexer) ||
          (lexer->lookahead == '(' && !skip_group(lexer))
        ) {
          return LVALUE_SHAPE_SETTLED;
        }
      } else if (call || word != NAME_WORD) {
        // A getline field without a target precedes this word.
        return field || targeted ? LVALUE_SHAPE_SETTLED : LVALUE_SHAPE_NONE;
      } else {
        name = true;
      }
    }
    if (!skip_token_layout(lexer)) {
      return LVALUE_SHAPE_SETTLED;
    }
    if (name && lexer->lookahead == '[') {
      if (!skip_group(lexer) || !skip_token_layout(lexer)) {
        return LVALUE_SHAPE_SETTLED;
      }
    }
    if ((input || targeted) && lexer->lookahead == '<') {
      lexer->advance(lexer, false);
      // A source extends the field beyond what can be delimited here.
      if (lexer->lookahead != '=') {
        return probe_getline_field(lexer, PROBE_SOURCE, deferred)
          ? LVALUE_SHAPE_SETTLED
          : LVALUE_SHAPE_POSTFIX;
      }
    }
    return LVALUE_SHAPE_OPEN;
  }
}

// The lvalue after getline is its target unless that strands an update
// operator: behind the target, each update must prefix the next lvalue.
// `x++ y` keeps the target while `x++` and `x++ y++` leave only postfix
// updates. Prefix-updated getline fields also check whether their own target
// must be omitted to accommodate the next update.
static enum TokenType probe_getline_operand(TSLexer *lexer, bool *deferred) {
  for (bool updated = false;; updated = true) {
    const LvalueShape shape = skip_lvalue(lexer, updated, deferred);
    if (shape == LVALUE_SHAPE_POSTFIX) {
      return GETLINE_OMITTED_WORD;
    }
    if (shape == LVALUE_SHAPE_NONE) {
      return updated ? GETLINE_OMITTED_WORD : GETLINE_WORD;
    }
    const int32_t sign = lexer->lookahead;
    if (shape == LVALUE_SHAPE_SETTLED || (sign != '+' && sign != '-')) {
      return GETLINE_TARGET_WORD;
    }
    lexer->advance(lexer, false);
    if (lexer->lookahead != sign) {
      return GETLINE_TARGET_WORD;
    }
    lexer->advance(lexer, false);
  }
}

static enum TokenType scan_getline_operand(TSLexer *lexer) {
  bool deferred = false;
  const enum TokenType token = probe_getline_operand(lexer, &deferred);
  if (!deferred) {
    return token;
  }
  if (token == GETLINE_TARGET_WORD) {
    return GETLINE_PREFER_TARGET_WORD;
  }
  return token == GETLINE_OMITTED_WORD ? GETLINE_PREFER_OMITTED_WORD : token;
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
  case GETLINE_WORD:
    if (valid_symbols[GETLINE_TARGET_WORD]) {
      return scan_getline_operand(lexer);
    }
    return valid_symbols[GETLINE_FIELD_WORD] ? GETLINE_FIELD_WORD : token;
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
  return skip_number(lexer, true) != NUMBER_ABSENT && emit(lexer, NUMBER);
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
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    // Only an update operator before an lvalue can start a prefix update.
    // The parser's internal tokens take the other spellings, and looking
    // ahead in every state keeps them dependent on what follows.
    if (
      (
        composite->token == INCR_OPERATOR || composite->token == DECR_OPERATOR
      ) &&
      !scan_lvalue_start(lexer)
    ) {
      return false;
    }
    return valid_symbols[composite->token] && emit(lexer, composite->token);
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
    return scan_backslash(state, lexer);
  }
  // A STRING or ERE token cannot contain a newline, so its lexical mode ends
  // there; the zero-width token hands the newline back to the parser.
  if (lexer->lookahead == '\n' && state->mode != LEXICAL_MODE_OUTSIDE) {
    return valid_symbols[LITERAL_BREAK] &&
      emit_mode(state, lexer, LEXICAL_MODE_OUTSIDE, LITERAL_BREAK);
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
