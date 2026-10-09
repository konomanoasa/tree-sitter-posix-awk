#include "tree_sitter/alloc.h"
#include "tree_sitter/parser.h"

#include <stddef.h>
#include <string.h>

#define ARRAY_LENGTH(array) (sizeof(array) / sizeof((array)[0]))

enum TokenType {
  BEGIN_ISOLATION,
  COMPLETE_FIELD_UPDATE,
  SUSPEND_UPDATE,
  RESTORE_UPDATE,
  WITHHELD_SOURCE_FILE_WORD,
  WITHHELD_SOURCE_FILE_TARGET_WORD,
  WITHHELD_SOURCE_FILE_FIELD_WORD,
  WITHHELD_SOURCE_FILE_FIELD_TARGET_WORD,
  WITHHELD_FILE_WORD,
  WITHHELD_FILE_TARGET_WORD,
  WITHHELD_SOURCE_FIELD_OPERATOR,
  WITHHELD_SOURCE_UPDATED_FIELD_OPERATOR,
  CONTINUED_FIELD_OPERATOR,
  CONTINUED_UPDATED_FIELD_OPERATOR,
  WITHHELD_SOURCE_GETLINE_WORD,
  WITHHELD_SOURCE_GETLINE_TARGET_WORD,
  WITHHELD_SOURCE_FIELD_WORD,
  WITHHELD_SOURCE_FIELD_TARGET_WORD,
  READY_GETLINE_WORD,
  READY_GETLINE_TARGET_WORD,
  GETLINE_FIELD_TARGET_WORD,
  WITHHELD_GETLINE_WORD,
  WITHHELD_GETLINE_TARGET_WORD,
  CONTINUED_GETLINE_FIELD_WORD,
  CONTINUED_GETLINE_FIELD_TARGET_WORD,
  CONTINUED_GETLINE_WORD,
  CONTINUED_GETLINE_TARGET_WORD,
  GETLINE_WORD,
  GETLINE_TARGET_WORD,
  GETLINE_OMITTED_WORD,
  GETLINE_FIELD_WORD,
  FUNC_NAME_WORD,
  DIVISION_SLASH,
  ERE_OPENING_SLASH,
  INCR_OPERATOR,
  DECR_OPERATOR,
  PLUS_OPERATOR,
  MINUS_OPERATOR,
  POSTFIX_FIELD_OPERATOR,
  FIELD_OPERATOR,
  NOT_OPERATOR,
  GETLINE_PIPE,
  ERE_COMPOUND_OPENING,
  ERE_ESCAPED_COMPOUND_OPENING,
  ERE_COMPOUND_SINGLE,
  ERE_COMPOUND_MULTI,
  ERE_DOT_CLOSING,
  ERE_EQUAL_CLOSING,
  ERE_COLON_CLOSING,
  ERE_BRACKET_LITERAL_OPEN,
  ERE_COMPOUND_CONTENT,
  ERE_CLOSING_HYPHEN,
  ERE_CLOSING,
  STRING_OPENING,
  STRING_CLOSING,
  STRING_ESCAPE,
  ERE_NAMED_ESCAPE,
  ERE_QUOTED_ESCAPE,
  ERE_OCTAL_ESCAPE,
  ERE_UNDEFINED_ESCAPE,
  ERE_ESCAPED_DELIMITER,
  COMMENT,
  CONTINUATION_BACKSLASH,
  CONTINUATION_NEWLINE,
  LITERAL_BREAK,
  STRAY_BACKSLASH,
  ERROR_SENTINEL,
  TOKEN_TYPE_COUNT,
};

enum {
  SERIALIZED_SCANNER_STATE_SIZE = 19,
  DECISION_CAPACITY = TREE_SITTER_SERIALIZATION_BUFFER_SIZE -
    SERIALIZED_SCANNER_STATE_SIZE,
  DECISION_FIELD = 4,
  DECISION_GETLINE = 8,
};

typedef char SerializedScannerStateFitsTreeSitterBuffer
  [SERIALIZED_SCANNER_STATE_SIZE <= TREE_SITTER_SERIALIZATION_BUFFER_SIZE ? 1
                                                                          : -1];

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

typedef struct {
  LexicalMode mode;
  uint64_t updates;
  uint64_t remaining_decisions;
  uint16_t decision_count;
  unsigned char decisions[DECISION_CAPACITY];
} ScannerState;

static void clear_decisions(ScannerState *state) {
  state->decision_count = 0;
  state->remaining_decisions = 0;
}

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

static WordKind classify_word(const char *word, size_t length) {
  if (length > MAX_RESERVED_WORD_LENGTH) {
    return WORD_NAME;
  }
  for (size_t i = 0; i < ARRAY_LENGTH(RESERVED_WORDS); i++) {
    if (strcmp(RESERVED_WORDS[i].spelling, word) == 0) {
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

static bool scan_lvalue_start(TSLexer *lexer) {
  if (!skip_token_layout(lexer)) {
    return false;
  }
  if (lexer->lookahead == '$') {
    return true;
  }
  return scan_word_spelling(lexer) == WORD_NAME && lexer->lookahead != '(';
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

typedef enum {
  NUMBER_ABSENT,
  NUMBER_WHOLE,
  NUMBER_BEFORE_WORD,
} NumberExtent;

// BEFORE_WORD consumes an incomplete exponent beyond the token end.
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

// A slash after an update is division: prefix updates require an lvalue.
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

// Restoring this cursor must preserve the source lexer's furthest lookahead.
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
    const WordKind word = scan_word_spelling(lexer);
    const bool call = word == WORD_NAME && lexer->lookahead == '(';
    if (word == WORD_GETLINE) {
      return PROBE_GETLINE;
    }
    if (word == WORD_MEMBERSHIP) {
      return PROBE_MEMBERSHIP;
    }
    if (word != WORD_NAME && word != WORD_BUILTIN) {
      return PROBE_END;
    }
    if (!skip_token_layout(lexer)) {
      return word == WORD_NAME ? PROBE_NAME : PROBE_VALUE;
    }
    if (call || word == WORD_BUILTIN) {
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

typedef struct DelayedNode DelayedNode;
struct DelayedNode {
  size_t start, end, decision, rank, plan;
  int ceiling, kind, value;
  bool lvalue, postfix, unary, blocked_postfix, focus_postfix;
  bool focus_target, focus_source, ranked;
  size_t outer_postfix;
};

typedef struct {
  DelayedNode *items;
  size_t *slots, *heap;
  size_t length, capacity, heap_length, table_capacity;
} DelayedSet;

typedef struct {
  size_t left, right;
} DelayedRank;

typedef struct {
  int *tokens;
  size_t length, capacity;
  DelayedNode candidate;
  bool failed;
  DelayedSet *memo;
  bool *computed;
  DelayedRank *ranks;
  size_t rank_length, rank_capacity, *rank_slots, rank_slot_capacity;
  size_t focus, blocked;
} DelayedParser;

enum {
  DELAYED_LEVELS = EXPR_FIELD + 2,
  DELAYED_LVALUE = EXPR_FIELD + 1,
  DELAYED_GET = 1,
  DELAYED_FILE,
  DELAYED_FIELD,
  DELAYED_PREFIX,
  DELAYED_POSTFIX,
};

static size_t delayed_rank_hash(size_t left, size_t right) {
  return (left * 0x9e3779b1u) ^ (right * 0x85ebca6bu);
}

static size_t
delayed_rank_pair(DelayedParser *parser, size_t left, size_t right) {
  if (parser->failed || (!left && !right))
    return 0;
  if (parser->rank_length >= parser->rank_slot_capacity / 2) {
    if (parser->rank_slot_capacity > SIZE_MAX / 2 / sizeof(size_t)) {
      parser->failed = true;
      return 0;
    }
    const size_t capacity =
      parser->rank_slot_capacity ? parser->rank_slot_capacity * 2 : 64;
    size_t *slots = ts_calloc(capacity, sizeof(*slots));
    if (!slots) {
      parser->failed = true;
      return 0;
    }
    for (size_t i = 0; i < parser->rank_length; i++) {
      const DelayedRank rank = parser->ranks[i];
      size_t slot = delayed_rank_hash(rank.left, rank.right) & (capacity - 1);
      while (slots[slot])
        slot = (slot + 1) & (capacity - 1);
      slots[slot] = i + 1;
    }
    ts_free(parser->rank_slots);
    parser->rank_slots = slots;
    parser->rank_slot_capacity = capacity;
  }
  size_t slot =
    delayed_rank_hash(left, right) & (parser->rank_slot_capacity - 1);
  while (parser->rank_slots[slot]) {
    const size_t i = parser->rank_slots[slot] - 1;
    if (parser->ranks[i].left == left && parser->ranks[i].right == right)
      return parser->length + 3 + i;
    slot = (slot + 1) & (parser->rank_slot_capacity - 1);
  }
  if (parser->rank_length == parser->rank_capacity) {
    if (parser->rank_capacity > SIZE_MAX / 2 / sizeof(*parser->ranks)) {
      parser->failed = true;
      return 0;
    }
    const size_t capacity =
      parser->rank_capacity ? parser->rank_capacity * 2 : 32;
    DelayedRank *ranks = ts_realloc(parser->ranks, capacity * sizeof(*ranks));
    if (!ranks) {
      parser->failed = true;
      return 0;
    }
    parser->ranks = ranks;
    parser->rank_capacity = capacity;
  }
  const size_t index = parser->rank_length++;
  parser->ranks[index] = (DelayedRank){left, right};
  parser->rank_slots[slot] = index + 1;
  return parser->length + 3 + index;
}

static DelayedRank
delayed_rank_children(const DelayedParser *parser, size_t rank) {
  return rank ? parser->ranks[rank - parser->length - 3] : (DelayedRank){0};
}

static size_t delayed_rank_single(
  DelayedParser *parser,
  size_t position,
  size_t value,
  size_t begin,
  size_t end
) {
  if (end - begin == 1)
    return value;
  const size_t middle = begin + (end - begin) / 2;
  return position < middle
    ? delayed_rank_pair(
        parser,
        delayed_rank_single(parser, position, value, begin, middle),
        0
      )
    : delayed_rank_pair(
        parser,
        0,
        delayed_rank_single(parser, position, value, middle, end)
      );
}

static size_t delayed_rank_merge(DelayedParser *parser, size_t a, size_t b) {
  if (!a || !b || parser->failed)
    return a ? a : b;
  const DelayedRank left = delayed_rank_children(parser, a),
                    right = delayed_rank_children(parser, b);
  const size_t first = delayed_rank_merge(parser, left.left, right.left);
  const size_t second = delayed_rank_merge(parser, left.right, right.right);
  return delayed_rank_pair(parser, first, second);
}

static bool
delayed_better(DelayedParser *parser, DelayedNode *left, DelayedNode *right) {
  size_t a = left->rank, b = right->rank;
  size_t begin = 0, end = parser->length;
  while (a != b && end - begin > 1) {
    const DelayedRank first = delayed_rank_children(parser, a),
                      second = delayed_rank_children(parser, b);
    const size_t middle = begin + (end - begin) / 2;
    if (first.left != second.left) {
      a = first.left;
      b = second.left;
      end = middle;
    } else {
      a = first.right;
      b = second.right;
      begin = middle;
    }
  }
  return a < b;
}

static size_t
delayed_rank_at(const DelayedParser *parser, size_t rank, size_t position) {
  size_t begin = 0, end = parser->length;
  while (rank && end - begin > 1) {
    const size_t middle = begin + (end - begin) / 2;
    const DelayedRank node = delayed_rank_children(parser, rank);
    if (position < middle) {
      rank = node.left;
      end = middle;
    } else {
      rank = node.right;
      begin = middle;
    }
  }
  return rank;
}

static DelayedNode *delayed_node(
  DelayedParser *parser,
  int kind,
  size_t start,
  size_t end,
  DelayedNode *left,
  DelayedNode *right
) {
  DelayedNode *node = &parser->candidate;
  *node = (DelayedNode){
    .start = start,
    .end = end,
    .decision = SIZE_MAX,
    .kind = kind,
    .ceiling = EXPR_FIELD,
    .rank = delayed_rank_merge(
      parser,
      left ? left->rank : 0,
      right ? right->rank : 0
    ),
    .plan = delayed_rank_merge(
      parser,
      left ? left->plan : 0,
      right ? right->plan : 0
    ),
    .focus_target =
      (left && left->focus_target) || (right && right->focus_target),
    .focus_source = (left && left->focus_source) ||
      (right && right->focus_source) ||
      (kind == DELAYED_FILE && start == parser->focus),
    .focus_postfix = (left && left->focus_postfix) ||
      (right && right->focus_postfix) ||
      (kind ==
        DELAYED_POSTFIX &&
        left &&
        left->kind ==
        DELAYED_FIELD &&
        left->start == parser->focus),
    .outer_postfix = (left ? left->outer_postfix : 0) +
      (right ? right->outer_postfix : 0) +
      (kind ==
        DELAYED_POSTFIX &&
        left &&
        left->kind ==
        DELAYED_FIELD &&
        left->start <
        parser->focus &&
        left->start != parser->blocked),
    .blocked_postfix = (left && left->blocked_postfix) ||
      (right && right->blocked_postfix) ||
      (kind ==
        DELAYED_POSTFIX &&
        left &&
        left->kind ==
        DELAYED_FIELD &&
        left->start == parser->blocked),
  };
  if (
    kind ==
    DELAYED_FILE ||
    (kind == DELAYED_POSTFIX && left && left->kind == DELAYED_FIELD)
  ) {
    const size_t position = kind == DELAYED_FILE ? start : left->start;
    node->plan = delayed_rank_merge(
      parser,
      node->plan,
      delayed_rank_single(parser, position, 1, 0, parser->length)
    );
  }
  return node;
}

static void delayed_set_destroy(DelayedSet *set) {
  ts_free(set->items);
  ts_free(set->slots);
  ts_free(set->heap);
}

static size_t delayed_hash(const DelayedNode *node) {
  size_t value = node->end;
  value = value * 31 + (size_t)node->ceiling;
  value = value * 31 + node->outer_postfix;
  value = value * 2 + node->lvalue;
  value = value * 2 + node->postfix;
  value = value * 2 + node->unary;
  value = value * 2 + node->blocked_postfix;
  return value * 2 + node->focus_postfix;
}

static void
delayed_add(DelayedParser *parser, DelayedSet *set, DelayedNode *node) {
  if (parser->failed)
    return;
  if (!node->ranked) {
    if (node->decision != SIZE_MAX) {
      const size_t value = node->kind == DELAYED_FILE
        ? parser->length - node->start + 1
        : parser->tokens[node->decision] == '<' ? parser->length + 2
                                                : (size_t)node->value + 1;
      node->rank = delayed_rank_merge(
        parser,
        node->rank,
        delayed_rank_single(parser, node->decision, value, 0, parser->length)
      );
    }
    node->ranked = true;
    if (parser->failed)
      return;
  }
  if (set->length >= set->table_capacity / 2) {
    if (set->table_capacity > SIZE_MAX / 2 / sizeof(*set->slots)) {
      parser->failed = true;
      return;
    }
    const size_t capacity = set->table_capacity ? set->table_capacity * 2 : 16;
    size_t *heads = ts_calloc(capacity, sizeof(*heads));
    if (!heads) {
      parser->failed = true;
      return;
    }
    for (size_t i = 0; i < set->length; i++) {
      size_t slot = delayed_hash(&set->items[i]) & (capacity - 1);
      while (heads[slot])
        slot = (slot + 1) & (capacity - 1);
      heads[slot] = i + 1;
    }
    ts_free(set->slots);
    set->slots = heads;
    set->table_capacity = capacity;
  }
  size_t slot = delayed_hash(node) & (set->table_capacity - 1);
  while (set->slots[slot]) {
    const size_t i = set->slots[slot] - 1;
    DelayedNode *old = &set->items[i];
    if (
      old->end ==
      node->end &&
      old->lvalue ==
      node->lvalue &&
      old->postfix ==
      node->postfix &&
      old->unary ==
      node->unary &&
      old->ceiling ==
      node->ceiling &&
      old->outer_postfix ==
      node->outer_postfix &&
      old->blocked_postfix ==
      node->blocked_postfix &&
      old->focus_postfix == node->focus_postfix
    ) {
      if (delayed_better(parser, node, old))
        set->items[i] = *node;
      return;
    }
    slot = (slot + 1) & (set->table_capacity - 1);
  }
  if (set->length == set->capacity) {
    if (set->capacity > SIZE_MAX / 2 / sizeof(*set->items)) {
      parser->failed = true;
      return;
    }
    const size_t capacity = set->capacity ? set->capacity * 2 : 8;
    DelayedNode *items = ts_realloc(set->items, capacity * sizeof(*set->items));
    if (!items) {
      parser->failed = true;
      return;
    }
    set->items = items;
    size_t *next = ts_realloc(set->heap, capacity * sizeof(*set->heap));
    if (!next) {
      parser->failed = true;
      return;
    }
    set->heap = next;
    set->capacity = capacity;
  }
  const size_t index = set->length++;
  set->items[index] = *node;
  set->slots[slot] = index + 1;
  size_t position = set->heap_length++;
  while (
    position && set->items[set->heap[(position - 1) / 2]].end > node->end
  ) {
    set->heap[position] = set->heap[(position - 1) / 2];
    position = (position - 1) / 2;
  }
  set->heap[position] = index;
}

static DelayedNode delayed_pop(DelayedSet *set) {
  DelayedNode node = set->items[set->heap[0]];
  const size_t last = set->heap[--set->heap_length];
  size_t position = 0;
  while (position * 2 + 1 < set->heap_length) {
    size_t child = position * 2 + 1;
    if (
      child +
      1 <
      set->heap_length &&
      set->items[set->heap[child + 1]].end < set->items[set->heap[child]].end
    )
      child++;
    if (set->items[last].end <= set->items[set->heap[child]].end)
      break;
    set->heap[position] = set->heap[child];
    position = child;
  }
  set->heap[position] = last;
  return node;
}

static int delayed_token(const DelayedParser *parser, size_t position) {
  return position < parser->length ? parser->tokens[position] : PROBE_END;
}

static DelayedSet *
delayed_expr(DelayedParser *parser, size_t start, int minimum) {
  return &parser->memo[start * DELAYED_LEVELS + minimum];
}

static DelayedSet
delayed_get(DelayedParser *parser, size_t start, bool redirected) {
  DelayedSet result = {0};
  DelayedNode *bare =
    delayed_node(parser, DELAYED_GET, start, start + 1, NULL, NULL);
  bare->decision = start;
  bare->value = 1;
  bare->postfix = true;
  delayed_add(parser, &result, bare);
  DelayedSet *targets = delayed_expr(parser, start + 1, DELAYED_LVALUE);
  for (size_t i = 0; i < targets->length; i++) {
    DelayedNode *target = &targets->items[i];
    DelayedNode *node =
      delayed_node(parser, DELAYED_GET, start, target->end, NULL, target);
    node->decision = start;
    node->focus_target |= start == parser->focus;
    node->postfix = target->postfix;
    delayed_add(parser, &result, node);
  }
  const size_t count = result.length;
  DelayedNode *gets = ts_calloc(count, sizeof(*gets));
  if (!gets && count) {
    parser->failed = true;
    return result;
  }
  if (count)
    memcpy(gets, result.items, count * sizeof(*gets));
  for (size_t i = 0; redirected && i < count; i++) {
    DelayedNode *get = &gets[i];
    if (delayed_token(parser, get->end) != '<')
      continue;
    DelayedSet *sources = delayed_expr(parser, get->end + 1, EXPR_FILE_SOURCE);
    for (size_t j = 0; j < sources->length; j++) {
      DelayedNode *source = &sources->items[j];
      DelayedNode *node =
        delayed_node(parser, DELAYED_FILE, start, source->end, get, source);
      node->decision = get->end;
      node->value = 0;
      node->postfix = true;
      delayed_add(parser, &result, node);
    }
  }
  ts_free(gets);
  return result;
}

static DelayedSet
delayed_prefix(DelayedParser *parser, size_t start, bool lvalue) {
  DelayedSet result = {0};
  const int token = delayed_token(parser, start);
  if (token == PROBE_NAME || (!lvalue && token == PROBE_VALUE)) {
    DelayedNode *node = delayed_node(parser, 0, start, start + 1, NULL, NULL);
    node->lvalue = token == PROBE_NAME;
    node->postfix = true;
    delayed_add(parser, &result, node);
  } else if (token == '$') {
    DelayedSet *operands = delayed_expr(parser, start + 1, EXPR_FIELD);
    for (size_t i = 0; i < operands->length; i++) {
      DelayedNode *operand = &operands->items[i];
      DelayedNode *node =
        delayed_node(parser, DELAYED_FIELD, start, operand->end, NULL, operand);
      node->lvalue = true;
      node->postfix = operand->postfix;
      delayed_add(parser, &result, node);
    }
  } else if (!lvalue && token == PROBE_GETLINE) {
    result = delayed_get(parser, start, true);
  } else if (
    !lvalue &&
    (token == PROBE_UPDATE || token == '+' || token == '-' || token == '!')
  ) {
    DelayedSet *operands = delayed_expr(
      parser,
      start + 1,
      token == PROBE_UPDATE ? DELAYED_LVALUE : EXPR_UNARY
    );
    for (size_t i = 0; i < operands->length; i++) {
      DelayedNode *operand = &operands->items[i];
      DelayedNode *node = delayed_node(
        parser,
        DELAYED_PREFIX,
        start,
        operand->end,
        NULL,
        operand
      );
      node->ceiling = token == PROBE_UPDATE ? EXPR_POSTFIX : EXPR_UNARY;
      node->unary = token == '+' || token == '-';
      if (token == PROBE_UPDATE) {
        node->decision = start;
        node->value = 1;
      }
      delayed_add(parser, &result, node);
    }
  }
  return result;
}

static void delayed_compute(DelayedParser *parser, size_t start, int minimum) {
  DelayedSet *result = &parser->memo[start * DELAYED_LEVELS + minimum];
  DelayedSet work = delayed_prefix(parser, start, minimum == DELAYED_LVALUE);
  if (minimum == DELAYED_LVALUE) {
    *result = work;
    return;
  }
  while (!parser->failed && work.heap_length) {
    DelayedNode popped = delayed_pop(&work);
    DelayedNode *left = &popped;
    const size_t end = left->end;
    const int token = delayed_token(parser, end);
    int precedence =
      token == PROBE_UPDATE ? EXPR_CONCATENATION : probe_precedence(token);
    const bool pipe = token == '|' && minimum <= EXPR_ASSIGNMENT;
    if (pipe)
      precedence = EXPR_ASSIGNMENT;
    const bool postfix = token ==
      PROBE_UPDATE &&
      left->lvalue &&
      left->postfix &&
      minimum <=
      EXPR_POSTFIX &&
      EXPR_POSTFIX <= left->ceiling;
    if (postfix) {
      DelayedNode *node =
        delayed_node(parser, DELAYED_POSTFIX, start, end + 1, left, NULL);
      node->decision = end;
      node->ceiling = EXPR_POSTFIX;
      delayed_add(parser, &work, node);
    }
    const bool eligible = precedence >
      0 &&
      minimum <=
      precedence &&
      precedence <=
      left->ceiling &&
      (pipe || precedence != EXPR_ASSIGNMENT || left->lvalue);
    if (!eligible) {
      if (!postfix)
        delayed_add(parser, result, left);
      continue;
    }
    DelayedSet local = {0}, *rights = &local;
    if (token == PROBE_MEMBERSHIP) {
      if (delayed_token(parser, end + 1) == PROBE_NAME)
        delayed_add(
          parser,
          &local,
          delayed_node(parser, 0, end + 1, end + 2, NULL, NULL)
        );
    } else if (pipe) {
      if (delayed_token(parser, end + 1) == PROBE_GETLINE)
        local = delayed_get(parser, end + 1, false);
    } else if (token == '?') {
      DelayedSet *consequences = delayed_expr(parser, end + 1, EXPR_ASSIGNMENT);
      for (size_t j = 0; j < consequences->length; j++) {
        DelayedNode *consequence = &consequences->items[j];
        if (delayed_token(parser, consequence->end) != ':')
          continue;
        DelayedSet *alternatives =
          delayed_expr(parser, consequence->end + 1, EXPR_CONDITIONAL);
        for (size_t k = 0; k < alternatives->length; k++) {
          DelayedNode *alternative = &alternatives->items[k];
          delayed_add(
            parser,
            &local,
            delayed_node(
              parser,
              0,
              end + 1,
              alternative->end,
              consequence,
              alternative
            )
          );
        }
      }
    } else {
      rights = delayed_expr(
        parser,
        precedence == EXPR_CONCATENATION ? end : end + 1,
        expression_right_precedence(precedence)
      );
    }
    for (size_t j = 0; j < rights->length; j++) {
      DelayedNode *right = &rights->items[j];
      if (precedence == EXPR_CONCATENATION && right->unary)
        continue;
      DelayedNode *node =
        delayed_node(parser, token, start, right->end, left, right);
      node->unary = left->unary;
      node->ceiling =
        pipe ? EXPR_FIELD : precedence - expression_non_associative(precedence);
      if (token == '<') {
        node->decision = end;
        node->value = 1;
      }
      delayed_add(parser, &work, node);
    }
    delayed_set_destroy(&local);
  }
  delayed_set_destroy(&work);
}

static void delayed_token_add(DelayedParser *parser, int token) {
  if (parser->failed)
    return;
  if (parser->length == parser->capacity) {
    if (parser->capacity > SIZE_MAX / 2 / sizeof(*parser->tokens)) {
      parser->failed = true;
      return;
    }
    const size_t capacity = parser->capacity ? parser->capacity * 2 : 32;
    int *tokens =
      ts_realloc(parser->tokens, capacity * sizeof(*parser->tokens));
    if (!tokens) {
      parser->failed = true;
      return;
    }
    parser->tokens = tokens;
    parser->capacity = capacity;
  }
  parser->tokens[parser->length++] = token;
}

static void delayed_destroy(DelayedParser *parser) {
  if (parser->memo)
    for (size_t i = 0; i < (parser->length + 1) * DELAYED_LEVELS; i++)
      delayed_set_destroy(&parser->memo[i]);
  ts_free(parser->memo);
  ts_free(parser->computed);
  ts_free(parser->ranks);
  ts_free(parser->rank_slots);
  ts_free(parser->tokens);
}

static void delayed_prepare(DelayedParser *parser, size_t selected) {
  parser->computed[EXPR_ASSIGNMENT] = true;
  parser->computed[selected * DELAYED_LEVELS + EXPR_ASSIGNMENT] = true;
  for (size_t i = 0; i < parser->length; i++) {
    const int token = parser->tokens[i];
    const size_t next = (i + 1) * DELAYED_LEVELS;
    if (token == '$')
      parser->computed[next + EXPR_FIELD] = true;
    if (token == PROBE_GETLINE || token == PROBE_UPDATE)
      parser->computed[next + DELAYED_LVALUE] = true;
    if (token == '<')
      parser->computed[next + EXPR_FILE_SOURCE] = true;
    if (token == '+' || token == '-' || token == '!')
      parser->computed[next + EXPR_UNARY] = true;
    if (token == '?')
      parser->computed[next + EXPR_ASSIGNMENT] = true;
    if (token == ':')
      parser->computed[next + EXPR_CONDITIONAL] = true;
    const int precedence =
      token == PROBE_UPDATE ? EXPR_CONCATENATION : probe_precedence(token);
    if (precedence == EXPR_CONCATENATION)
      parser->computed[i * DELAYED_LEVELS + EXPR_CONCATENATION + 1] = true;
    else if (precedence)
      parser->computed[next + expression_right_precedence(precedence)] = true;
  }
  for (size_t start = parser->length + 1; start && !parser->failed;) {
    start--;
    for (
      int minimum = 0; minimum < DELAYED_LEVELS && !parser->failed; minimum++
    ) {
      if (parser->computed[start * DELAYED_LEVELS + minimum])
        delayed_compute(parser, start, minimum);
    }
  }
}

typedef struct {
  size_t start, end, outer_postfix;
  int kind, ceiling;
  bool lvalue, postfix, unary, blocked_postfix;
} OrderedValue;

typedef enum {
  ORDERED_PREFIX,
  ORDERED_FIELD,
  ORDERED_UNARY,
  ORDERED_TARGET,
  ORDERED_SOURCE,
  ORDERED_GET,
  ORDERED_TAIL,
  ORDERED_BINARY,
  ORDERED_CONSEQUENCE,
  ORDERED_ALTERNATIVE,
} OrderedStage;

typedef struct {
  size_t start, operator_position;
  int minimum, token, precedence;
  OrderedStage stage;
  bool get_only;
  OrderedValue left, consequence;
} OrderedFrame;

typedef struct {
  DelayedParser *input;
  OrderedFrame *frames;
  size_t *choices, *sources;
  bool *alternatives;
  unsigned char *plan;
  size_t depth, maximum_depth, budget;
  OrderedValue value;
  bool valid;
} OrderedParser;

static void ordered_push(OrderedParser *parser, size_t start, int minimum) {
  if (parser->depth > parser->input->length) {
    parser->valid = false;
    return;
  }
  parser->frames[parser->depth++] = (OrderedFrame){
    .start = start,
    .minimum = minimum,
    .get_only = minimum == 0,
  };
  // Limit speculative retries; the chart shares continuations after this
  // budget.
  if (parser->depth > parser->maximum_depth) {
    parser->maximum_depth = parser->depth;
    const size_t allowance = parser->input->length > SIZE_MAX / 8
      ? SIZE_MAX
      : 8 * parser->input->length;
    parser->budget = parser->budget > SIZE_MAX - allowance
      ? SIZE_MAX
      : parser->budget + allowance;
  }
}

static void ordered_return(OrderedParser *parser, OrderedValue value) {
  parser->value = value;
  parser->depth--;
}

static OrderedValue ordered_node(
  int kind,
  size_t start,
  size_t end,
  OrderedValue left,
  OrderedValue right
) {
  return (OrderedValue){
    .kind = kind,
    .start = start,
    .end = end,
    .ceiling = EXPR_FIELD,
    .outer_postfix = left.outer_postfix + right.outer_postfix,
    .blocked_postfix = left.blocked_postfix || right.blocked_postfix,
  };
}

static void ordered_step(OrderedParser *parser) {
  OrderedFrame *frame = &parser->frames[parser->depth - 1];
  const int token = delayed_token(parser->input, frame->start);
  const OrderedValue empty = {0};
  switch (frame->stage) {
  case ORDERED_PREFIX:
    if (
      token ==
      PROBE_NAME ||
      (token == PROBE_VALUE && frame->minimum != DELAYED_LVALUE)
    ) {
      frame->left =
        ordered_node(0, frame->start, frame->start + 1, empty, empty);
      frame->left.lvalue = token == PROBE_NAME;
      frame->left.postfix = true;
      frame->stage = ORDERED_TAIL;
    } else if (token == '$') {
      frame->stage = ORDERED_FIELD;
      ordered_push(parser, frame->start + 1, EXPR_FIELD);
    } else if (token == PROBE_GETLINE && frame->minimum != DELAYED_LVALUE) {
      frame->left =
        ordered_node(DELAYED_GET, frame->start, frame->start + 1, empty, empty);
      frame->left.postfix = true;
      const int next = delayed_token(parser->input, frame->start + 1);
      if (next == PROBE_NAME || next == '$') {
        parser->alternatives[frame->start] = parser->choices[frame->start] == 0;
        if (!parser->choices[frame->start]) {
          parser->plan[frame->start] = 1;
          frame->stage = ORDERED_TARGET;
          ordered_push(parser, frame->start + 1, DELAYED_LVALUE);
          break;
        }
      }
      frame->stage = ORDERED_GET;
    } else if (
      frame->minimum !=
      DELAYED_LVALUE &&
      (token == '+' || token == '-' || token == '!' || token == PROBE_UPDATE)
    ) {
      frame->stage = ORDERED_UNARY;
      ordered_push(
        parser,
        frame->start + 1,
        token == PROBE_UPDATE ? DELAYED_LVALUE : EXPR_UNARY
      );
    } else {
      parser->valid = false;
    }
    break;
  case ORDERED_FIELD:
    frame->left = ordered_node(
      DELAYED_FIELD,
      frame->start,
      parser->value.end,
      empty,
      parser->value
    );
    frame->left.lvalue = true;
    frame->left.postfix = parser->value.postfix;
    frame->stage = ORDERED_TAIL;
    break;
  case ORDERED_UNARY:
    frame->left = ordered_node(
      DELAYED_PREFIX,
      frame->start,
      parser->value.end,
      empty,
      parser->value
    );
    frame->left.ceiling = token == PROBE_UPDATE ? EXPR_POSTFIX : EXPR_UNARY;
    frame->left.unary = token == '+' || token == '-';
    frame->stage = ORDERED_TAIL;
    break;
  case ORDERED_TARGET:
    frame->left = ordered_node(
      DELAYED_GET,
      frame->start,
      parser->value.end,
      empty,
      parser->value
    );
    frame->left.postfix = parser->value.postfix;
    frame->stage = ORDERED_GET;
    break;
  case ORDERED_GET:
    if (
      !frame->get_only && delayed_token(parser->input, frame->left.end) == '<'
    ) {
      const size_t position = frame->left.end;
      const size_t owner = parser->sources[position]++;
      if (parser->choices[position] == owner) {
        parser->alternatives[position] = true;
        parser->plan[frame->start] |= 2;
        frame->stage = ORDERED_SOURCE;
        ordered_push(parser, position + 1, EXPR_FILE_SOURCE);
        break;
      }
    }
    frame->stage = ORDERED_TAIL;
    break;
  case ORDERED_SOURCE:
    frame->left = ordered_node(
      DELAYED_FILE,
      frame->start,
      parser->value.end,
      frame->left,
      parser->value
    );
    frame->left.postfix = true;
    frame->stage = ORDERED_TAIL;
    break;
  case ORDERED_TAIL: {
    if (frame->minimum == DELAYED_LVALUE || frame->get_only) {
      ordered_return(parser, frame->left);
      break;
    }
    const size_t end = frame->left.end;
    const int next = delayed_token(parser->input, end);
    const bool pipe = next == '|' && frame->minimum <= EXPR_ASSIGNMENT;
    const int precedence = pipe ? EXPR_ASSIGNMENT
      : next == PROBE_UPDATE    ? EXPR_CONCATENATION
                                : probe_precedence(next);
    const bool postfix = next ==
      PROBE_UPDATE &&
      frame->left.lvalue &&
      frame->left.postfix &&
      frame->minimum <=
      EXPR_POSTFIX &&
      EXPR_POSTFIX <= frame->left.ceiling;
    const bool eligible = precedence >
      0 &&
      frame->minimum <=
      precedence &&
      precedence <=
      frame->left.ceiling &&
      (pipe || precedence != EXPR_ASSIGNMENT || frame->left.lvalue);
    if (postfix && (!eligible || !parser->choices[end])) {
      parser->alternatives[end] = eligible;
      const OrderedValue left = frame->left;
      frame->left =
        ordered_node(DELAYED_POSTFIX, frame->start, end + 1, left, empty);
      frame->left.ceiling = EXPR_POSTFIX;
      if (left.kind == DELAYED_FIELD) {
        parser->plan[left.start] = 1;
        frame->left.outer_postfix += left.start <
          parser->input->focus &&
          left.start != parser->input->blocked;
        frame->left.blocked_postfix |= left.start == parser->input->blocked;
      }
      break;
    }
    if (!eligible) {
      ordered_return(parser, frame->left);
      break;
    }
    frame->token = next;
    frame->operator_position = end;
    frame->precedence = precedence;
    frame->stage = ORDERED_BINARY;
    if (next == PROBE_MEMBERSHIP) {
      if (delayed_token(parser->input, end + 1) != PROBE_NAME) {
        parser->valid = false;
        break;
      }
      parser->value = ordered_node(0, end + 1, end + 2, empty, empty);
    } else if (pipe) {
      if (delayed_token(parser->input, end + 1) != PROBE_GETLINE) {
        parser->valid = false;
        break;
      }
      ordered_push(parser, end + 1, 0);
    } else if (next == '?') {
      frame->stage = ORDERED_CONSEQUENCE;
      ordered_push(parser, end + 1, EXPR_ASSIGNMENT);
    } else {
      if (next == '<')
        parser->alternatives[end] = false;
      ordered_push(
        parser,
        precedence == EXPR_CONCATENATION ? end : end + 1,
        expression_right_precedence(precedence)
      );
    }
    break;
  }
  case ORDERED_CONSEQUENCE:
    if (delayed_token(parser->input, parser->value.end) != ':') {
      parser->valid = false;
      break;
    }
    frame->consequence = parser->value;
    frame->stage = ORDERED_ALTERNATIVE;
    ordered_push(parser, parser->value.end + 1, EXPR_CONDITIONAL);
    break;
  case ORDERED_ALTERNATIVE:
    parser->value = ordered_node(
      0,
      frame->operator_position + 1,
      parser->value.end,
      frame->consequence,
      parser->value
    );
    frame->stage = ORDERED_BINARY;
    break;
  case ORDERED_BINARY: {
    if (frame->precedence == EXPR_CONCATENATION && parser->value.unary) {
      parser->valid = false;
      break;
    }
    const bool unary = frame->left.unary;
    frame->left = ordered_node(
      frame->token,
      frame->start,
      parser->value.end,
      frame->left,
      parser->value
    );
    frame->left.unary = unary;
    frame->left.ceiling = frame->token == '|'
      ? EXPR_FIELD
      : frame->precedence - expression_non_associative(frame->precedence);
    frame->stage = ORDERED_TAIL;
    break;
  }
  }
}

static unsigned char *ordered_parse(DelayedParser *input, uint64_t required) {
  if (input->length > (SIZE_MAX / sizeof(OrderedFrame)) - 2)
    return NULL;
  OrderedParser parser = {.input = input};
  parser.frames = ts_calloc(input->length + 2, sizeof(*parser.frames));
  parser.choices = ts_calloc(input->length, sizeof(*parser.choices));
  parser.sources = ts_calloc(input->length, sizeof(*parser.sources));
  parser.alternatives = ts_calloc(input->length, sizeof(*parser.alternatives));
  parser.plan = ts_calloc(input->length, sizeof(*parser.plan));
  unsigned char *result = NULL;
  if (
    !parser.frames ||
    !parser.choices ||
    !parser.sources ||
    !parser.alternatives ||
    !parser.plan
  )
    goto done;
  parser.budget = 1;
  while (parser.budget) {
    memset(parser.sources, 0, input->length * sizeof(*parser.sources));
    memset(
      parser.alternatives,
      0,
      input->length * sizeof(*parser.alternatives)
    );
    memset(parser.plan, 0, input->length * sizeof(*parser.plan));
    parser.depth = 0;
    parser.valid = true;
    ordered_push(&parser, 0, EXPR_ASSIGNMENT);
    while (parser.valid && parser.depth && parser.budget) {
      parser.budget--;
      ordered_step(&parser);
    }
    if (
      parser.valid &&
      !parser.depth &&
      parser.value.end ==
      input->length &&
      parser.value.outer_postfix ==
      required &&
      !parser.value.blocked_postfix
    ) {
      result = parser.plan;
      parser.plan = NULL;
      break;
    }
    size_t position = input->length;
    while (position && !parser.alternatives[position - 1])
      position--;
    if (!position)
      break;
    parser.choices[position - 1]++;
    memset(
      parser.choices + position,
      0,
      (input->length - position) * sizeof(*parser.choices)
    );
  }
done:
  ts_free(parser.frames);
  ts_free(parser.choices);
  ts_free(parser.sources);
  ts_free(parser.alternatives);
  ts_free(parser.plan);
  return result;
}

static int take_decision(ScannerState *state, bool field) {
  if (!state->decision_count)
    return -1;
  const int decision = state->decisions[0];
  if ((decision & 12) != (field ? DECISION_FIELD : DECISION_GETLINE)) {
    clear_decisions(state);
    return -1;
  }
  if (--state->remaining_decisions) {
    memmove(state->decisions, state->decisions + 1, state->decision_count - 1);
    state->decisions[state->decision_count - 1] = (unsigned char)decision;
  } else {
    clear_decisions(state);
  }
  return decision & 3;
}

static void cache_decisions(
  ScannerState *state,
  const DelayedParser *parser,
  const unsigned char *plan,
  const DelayedNode *best
) {
  const size_t capacity = parser->length - parser->focus - 1;
  if (!capacity || capacity > SIZE_MAX / sizeof(size_t))
    return;
  unsigned char *decisions = ts_calloc(capacity, sizeof(*decisions));
  size_t *prefixes = ts_calloc(capacity, sizeof(*prefixes));
  if (!decisions || !prefixes) {
    ts_free(decisions);
    ts_free(prefixes);
    return;
  }
  size_t length = 0;
  for (size_t i = parser->focus + 1; i < parser->length; i++) {
    const int token = parser->tokens[i];
    if (token == '$')
      decisions[length++] = DECISION_FIELD |
        (plan      ? plan[i]
            : best ? delayed_rank_at(parser, best->plan, i) != 0
                   : 0);
    if (token == PROBE_GETLINE)
      decisions[length++] = DECISION_GETLINE |
        (plan      ? plan[i]
            : best ? (delayed_rank_at(parser, best->rank, i) == 1) |
              (delayed_rank_at(parser, best->plan, i) ? 2 : 0)
                   : delayed_token(parser, i + 1) ==
              PROBE_NAME ||
              delayed_token(parser, i + 1) == '$');
  }
  if (length) {
    for (size_t i = 1; i < length; i++) {
      size_t prefix = prefixes[i - 1];
      while (prefix && decisions[prefix] != decisions[i])
        prefix = prefixes[prefix - 1];
      prefixes[i] = prefix + (decisions[prefix] == decisions[i]);
    }
    const size_t period = length - prefixes[length - 1];
    state->decision_count =
      period <= DECISION_CAPACITY ? (uint16_t)period : DECISION_CAPACITY;
    state->remaining_decisions =
      period <= DECISION_CAPACITY ? length : DECISION_CAPACITY;
    memcpy(state->decisions, decisions, state->decision_count);
  }
  ts_free(decisions);
  ts_free(prefixes);
}

typedef enum {
  GETLINE_EXPRESSION,
  GETLINE_UPDATED_FIELD,
  GETLINE_UPDATED_SOURCE,
  GETLINE_FIELD_OPERATOR,
  GETLINE_TARGET_BEFORE_SOURCE,
  GETLINE_PLAIN_FIELD,
  GETLINE_SOURCE_BEFORE_OUTER_SOURCE,
} GetlineContext;

static int delayed_getline(
  TSLexer *lexer,
  GetlineContext context,
  bool field_focus,
  bool *continued,
  ScannerState *state
) {
  const uint64_t updates = state->updates;
  const int cached = take_decision(state, field_focus);
  if (cached >= 0) {
    *continued = updates &&
      (context ==
        GETLINE_UPDATED_SOURCE ||
        context ==
        GETLINE_TARGET_BEFORE_SOURCE ||
        context == GETLINE_SOURCE_BEFORE_OUTER_SOURCE);
    return cached;
  }
  InputProbe input = {
    .lexer =
      {.lookahead = lexer->lookahead,
        .advance = probe_advance,
        .mark_end = probe_mark_end,
        .eof = probe_eof},
    .source = lexer
  };
  DelayedParser parser = {.blocked = SIZE_MAX};
  const bool owns_context = context ==
    GETLINE_UPDATED_FIELD ||
    context ==
    GETLINE_UPDATED_SOURCE ||
    context ==
    GETLINE_TARGET_BEFORE_SOURCE ||
    context == GETLINE_SOURCE_BEFORE_OUTER_SOURCE;
  const uint64_t ancestors = owns_context && updates ? updates - 1 : updates;
  for (uint64_t i = 0; i < ancestors; i++) {
    delayed_token_add(&parser, '$');
    delayed_token_add(&parser, PROBE_GETLINE);
    delayed_token_add(&parser, '<');
  }
  if (context == GETLINE_PLAIN_FIELD)
    parser.blocked = parser.length;
  const uint64_t required = ancestors + owns_context;
  if (context && context != GETLINE_FIELD_OPERATOR)
    delayed_token_add(&parser, '$');
  if (context == GETLINE_UPDATED_SOURCE) {
    delayed_token_add(&parser, PROBE_GETLINE);
    delayed_token_add(&parser, '<');
  }
  if (
    context ==
    GETLINE_TARGET_BEFORE_SOURCE ||
    context == GETLINE_SOURCE_BEFORE_OUTER_SOURCE
  ) {
    delayed_token_add(&parser, PROBE_GETLINE);
    delayed_token_add(&parser, '$');
    delayed_token_add(&parser, '!');
    if (context == GETLINE_SOURCE_BEFORE_OUTER_SOURCE) {
      delayed_token_add(&parser, PROBE_GETLINE);
      delayed_token_add(&parser, '<');
    }
  }
  const size_t selected = parser.length;
  parser.focus = selected;
  delayed_token_add(&parser, field_focus ? '$' : PROBE_GETLINE);
  bool operand = field_focus;
  int questions = 0;
  while (!parser.failed) {
    const int token = probe_token(&input, operand);
    if (
      !token ||
      token ==
      '\n' ||
      token ==
      '#' ||
      token ==
      ';' ||
      token ==
      ',' ||
      token ==
      ')' ||
      token ==
      ']' ||
      token ==
      '}' ||
      token ==
      '{' ||
      (token == ':' && questions == 0)
    )
      break;
    if (token == '?')
      questions++;
    if (token == ':')
      questions--;
    delayed_token_add(&parser, token);
    if (token != PROBE_UPDATE)
      operand =
        token != PROBE_NAME && token != PROBE_VALUE && token != PROBE_GETLINE;
  }
  bool has_updates = false;
  for (size_t i = 0; i < parser.length; i++)
    has_updates |= parser.tokens[i] == PROBE_UPDATE;
  if (!has_updates) {
    const int token = delayed_token(&parser, selected + 1);
    const int decision = field_focus     ? 0
      : context == GETLINE_UPDATED_FIELD ? -1
                                         : token == PROBE_NAME || token == '$';
    *continued = false;
    if (decision >= 0 && !parser.failed && !input.failed && !updates)
      cache_decisions(state, &parser, NULL, NULL);
    delayed_destroy(&parser);
    ts_free(input.characters);
    return decision;
  }
  if (
    parser.failed ||
    input.failed ||
    parser.length >=
    SIZE_MAX /
    DELAYED_LEVELS /
    sizeof(*parser.memo)
  ) {
    delayed_destroy(&parser);
    ts_free(input.characters);
    *continued = false;
    return -1;
  }
  unsigned char *plan = ordered_parse(&parser, required);
  if (plan) {
    const int decision = plan[selected];
    *continued = context ==
      GETLINE_UPDATED_SOURCE ||
      context ==
      GETLINE_TARGET_BEFORE_SOURCE ||
      context == GETLINE_SOURCE_BEFORE_OUTER_SOURCE;
    cache_decisions(state, &parser, plan, NULL);
    ts_free(plan);
    delayed_destroy(&parser);
    ts_free(input.characters);
    return decision;
  }
  parser.memo =
    ts_calloc((parser.length + 1) * DELAYED_LEVELS, sizeof(*parser.memo));
  parser.computed =
    ts_calloc((parser.length + 1) * DELAYED_LEVELS, sizeof(*parser.computed));
  if (!parser.memo || !parser.computed) {
    delayed_destroy(&parser);
    ts_free(input.characters);
    *continued = false;
    return -1;
  }
  delayed_prepare(&parser, selected);
  if (parser.failed) {
    delayed_destroy(&parser);
    ts_free(input.characters);
    *continued = false;
    return -1;
  }
  DelayedSet *candidates = delayed_expr(&parser, 0, EXPR_ASSIGNMENT);
  DelayedNode *best = NULL;
  for (size_t i = 0; i < candidates->length; i++) {
    DelayedNode *node = &candidates->items[i];
    if (
      node->end ==
      parser.length &&
      (node->outer_postfix == required) &&
      !node->blocked_postfix &&
      (!best || delayed_better(&parser, node, best))
    )
      best = node;
  }
  *continued = (context ==
                 GETLINE_UPDATED_SOURCE ||
                 context ==
                 GETLINE_TARGET_BEFORE_SOURCE ||
                 context == GETLINE_SOURCE_BEFORE_OUTER_SOURCE) &&
    best != NULL;
  if (
    (context ==
      GETLINE_UPDATED_SOURCE ||
      context ==
      GETLINE_TARGET_BEFORE_SOURCE ||
      context == GETLINE_SOURCE_BEFORE_OUTER_SOURCE) &&
    !best
  ) {
    candidates = delayed_expr(&parser, selected, EXPR_ASSIGNMENT);
    for (size_t i = 0; i < candidates->length; i++) {
      DelayedNode *node = &candidates->items[i];
      if (
        node->end ==
        parser.length &&
        (!best || delayed_better(&parser, node, best))
      )
        best = node;
    }
  }
  int decision = field_focus && best ? best->focus_postfix : -1;
  if (best && !field_focus)
    decision = best->focus_target | (best->focus_source ? 2 : 0);
  if (best)
    cache_decisions(state, &parser, NULL, best);
  delayed_destroy(&parser);
  ts_free(input.characters);
  return decision;
}

static enum TokenType
scan_getline_operand(ScannerState *state, TSLexer *lexer) {
  bool continued;
  const int decision =
    delayed_getline(lexer, GETLINE_EXPRESSION, false, &continued, state);
  return (decision >= 0 && (decision & 1)) ? GETLINE_TARGET_WORD
    : decision == 0                        ? GETLINE_OMITTED_WORD
                                           : GETLINE_WORD;
}

static bool scan_contextual_word(
  ScannerState *state,
  TSLexer *lexer,
  const bool *valid_symbols,
  bool recovering
) {
  const WordKind word = scan_word_spelling(lexer);
  lexer->mark_end(lexer);
  if (word == WORD_NAME && lexer->lookahead == '(') {
    return emit(lexer, FUNC_NAME_WORD);
  }
  // Names and getline can restart nested expressions at every recovery.
  if (recovering && (word == WORD_NAME || word == WORD_GETLINE)) {
    return emit(lexer, ERROR_SENTINEL);
  }
  if (word != WORD_GETLINE) {
    return false;
  }
  enum TokenType token = GETLINE_WORD;
  if (
    valid_symbols[READY_GETLINE_WORD] ||
    valid_symbols[READY_GETLINE_TARGET_WORD]
  ) {
    bool continued;
    const int decision =
      delayed_getline(lexer, GETLINE_UPDATED_FIELD, false, &continued, state);
    return emit(
      lexer,
      (decision >= 0 && (decision & 1)) ? READY_GETLINE_TARGET_WORD
                                        : READY_GETLINE_WORD
    );
  }
  const bool withheld_source = valid_symbols[WITHHELD_SOURCE_FILE_WORD] ||
    valid_symbols[WITHHELD_SOURCE_FILE_TARGET_WORD] ||
    valid_symbols[WITHHELD_SOURCE_FILE_FIELD_WORD] ||
    valid_symbols[WITHHELD_SOURCE_FILE_FIELD_TARGET_WORD] ||
    valid_symbols[WITHHELD_SOURCE_GETLINE_WORD] ||
    valid_symbols[WITHHELD_SOURCE_GETLINE_TARGET_WORD] ||
    valid_symbols[WITHHELD_SOURCE_FIELD_WORD] ||
    valid_symbols[WITHHELD_SOURCE_FIELD_TARGET_WORD];
  const bool withheld = valid_symbols[WITHHELD_GETLINE_WORD] ||
    valid_symbols[WITHHELD_GETLINE_TARGET_WORD] ||
    valid_symbols[WITHHELD_FILE_WORD] ||
    valid_symbols[WITHHELD_FILE_TARGET_WORD];
  if (
    withheld_source ||
    withheld ||
    valid_symbols[CONTINUED_GETLINE_WORD] ||
    valid_symbols[CONTINUED_GETLINE_TARGET_WORD] ||
    valid_symbols[CONTINUED_GETLINE_FIELD_WORD] ||
    valid_symbols[CONTINUED_GETLINE_FIELD_TARGET_WORD]
  ) {
    bool continued;
    const int decision = delayed_getline(
      lexer,
      withheld_source ? GETLINE_SOURCE_BEFORE_OUTER_SOURCE
        : withheld    ? GETLINE_TARGET_BEFORE_SOURCE
                      : GETLINE_UPDATED_SOURCE,
      false,
      &continued,
      state
    );
    if (decision >= 0 && continued) {
      if (withheld_source) {
        const bool field =
          !valid_symbols[WITHHELD_SOURCE_GETLINE_TARGET_WORD] &&
          !valid_symbols[WITHHELD_SOURCE_FILE_TARGET_WORD];
        if (decision & 2)
          return emit(
            lexer,
            (decision & 1) ? (field ? WITHHELD_SOURCE_FILE_FIELD_TARGET_WORD
                                    : WITHHELD_SOURCE_FILE_TARGET_WORD)
                           : (field ? WITHHELD_SOURCE_FILE_FIELD_WORD
                                    : WITHHELD_SOURCE_FILE_WORD)
          );
        return emit(
          lexer,
          (decision >= 0 && (decision & 1))
            ? (field ? WITHHELD_SOURCE_FIELD_TARGET_WORD
                     : WITHHELD_SOURCE_GETLINE_TARGET_WORD)
            : (field ? WITHHELD_SOURCE_FIELD_WORD
                     : WITHHELD_SOURCE_GETLINE_WORD)
        );
      }
      if (withheld)
        return emit(
          lexer,
          (decision & 2)
            ? ((decision & 1) ? WITHHELD_FILE_TARGET_WORD : WITHHELD_FILE_WORD)
            : ((decision & 1) ? WITHHELD_GETLINE_TARGET_WORD
                              : WITHHELD_GETLINE_WORD)
        );
      const bool field = !valid_symbols[CONTINUED_GETLINE_TARGET_WORD];
      return emit(
        lexer,
        (decision >= 0 && (decision & 1))
          ? (field ? CONTINUED_GETLINE_FIELD_TARGET_WORD
                   : CONTINUED_GETLINE_TARGET_WORD)
          : (field ? CONTINUED_GETLINE_FIELD_WORD : CONTINUED_GETLINE_WORD)
      );
    }
    if (valid_symbols[GETLINE_TARGET_WORD])
      return emit(
        lexer,
        (decision >= 0 && (decision & 1)) ? GETLINE_TARGET_WORD
                                          : GETLINE_OMITTED_WORD
      );
    if (
      valid_symbols[GETLINE_FIELD_WORD] ||
      valid_symbols[GETLINE_FIELD_TARGET_WORD]
    )
      return emit(
        lexer,
        (decision >= 0 && (decision & 1)) ? GETLINE_FIELD_TARGET_WORD
                                          : GETLINE_FIELD_WORD
      );
    return emit(lexer, GETLINE_WORD);
  } else if (valid_symbols[GETLINE_TARGET_WORD]) {
    token = scan_getline_operand(state, lexer);
  } else if (
    valid_symbols[GETLINE_FIELD_WORD] ||
    valid_symbols[GETLINE_FIELD_TARGET_WORD]
  ) {
    bool continued;
    const int decision =
      delayed_getline(lexer, GETLINE_PLAIN_FIELD, false, &continued, state);
    token = (decision >= 0 && (decision & 1)) ? GETLINE_FIELD_TARGET_WORD
                                              : GETLINE_FIELD_WORD;
  }
  return emit(lexer, token);
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
    return scan_lvalue_start(lexer) &&
      valid_symbols[token] &&
      emit(lexer, token);
  }
  const enum TokenType token = first == '+' ? PLUS_OPERATOR : MINUS_OPERATOR;
  return valid_symbols[token] && emit(lexer, token);
}

static bool scan_prefix_operator(
  ScannerState *state,
  TSLexer *lexer,
  const bool *valid_symbols,
  bool recovering
) {
  const int32_t first = lexer->lookahead;
  lexer->advance(lexer, false);
  lexer->mark_end(lexer);
  if (first == '$' && !recovering) {
    const bool withheld = valid_symbols[WITHHELD_SOURCE_FIELD_OPERATOR] ||
      valid_symbols[WITHHELD_SOURCE_UPDATED_FIELD_OPERATOR];
    const bool source = valid_symbols[CONTINUED_FIELD_OPERATOR] ||
      valid_symbols[CONTINUED_UPDATED_FIELD_OPERATOR];
    const enum TokenType plain = withheld ? WITHHELD_SOURCE_FIELD_OPERATOR
      : source                            ? CONTINUED_FIELD_OPERATOR
                                          : FIELD_OPERATOR;
    const enum TokenType updated = withheld
      ? WITHHELD_SOURCE_UPDATED_FIELD_OPERATOR
      : source ? CONTINUED_UPDATED_FIELD_OPERATOR
               : POSTFIX_FIELD_OPERATOR;
    if (valid_symbols[updated]) {
      bool continued;
      const int result = delayed_getline(
        lexer,
        withheld   ? GETLINE_SOURCE_BEFORE_OUTER_SOURCE
          : source ? GETLINE_UPDATED_SOURCE
                   : GETLINE_FIELD_OPERATOR,
        true,
        &continued,
        state
      );
      if (result == 1)
        state->updates++;
      return emit(lexer, result == 1 ? updated : plain);
    }
    take_decision(state, true);
    return valid_symbols[plain] && emit(lexer, plain);
  }
  if (first == '!' && (lexer->lookahead == '=' || lexer->lookahead == '~')) {
    return false;
  }
  if (recovering) {
    return emit(lexer, ERROR_SENTINEL);
  }
  const enum TokenType token = first == '$' ? FIELD_OPERATOR : NOT_OPERATOR;
  return valid_symbols[token] && emit(lexer, token);
}

// Read past output pipes too, so later edits invalidate reused nodes.
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

// Other lexing can consume trailing blanks together with the raw newline.
static bool skip_blanks_before_newline(TSLexer *lexer, unsigned *count) {
  while (is_ascii_blank(lexer->lookahead)) {
    lexer->advance(lexer, false);
    (*count)++;
  }
  return lexer->lookahead == '\n';
}

// Save the mode change without consuming the newline.
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
    !lexer->eof(lexer) &&
    lexer->lookahead !=
    '\n' &&
    lexer->lookahead !=
    '/' &&
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
      mode !=
      LEXICAL_MODE_ERE_CLASS &&
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
      (state->mode ==
        LEXICAL_MODE_ERE_COLLATING ||
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
  unsigned blanks = 0;
  return skip_blanks_before_newline(lexer, &blanks) &&
    emit_literal_break(state, lexer, valid_symbols);
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
  if (
    state->mode ==
    LEXICAL_MODE_OUTSIDE &&
    !state->updates &&
    !state->decision_count
  ) {
    return 0;
  }
  buffer[0] = (char)state->mode;
  for (unsigned i = 0; i < 8; i++)
    buffer[i + 1] = (char)(state->updates >> (i * 8));
  buffer[9] = (char)state->decision_count;
  buffer[10] = (char)(state->decision_count >> 8);
  for (unsigned i = 0; i < 8; i++)
    buffer[i + 11] = (char)(state->remaining_decisions >> (i * 8));
  memcpy(
    buffer + SERIALIZED_SCANNER_STATE_SIZE,
    state->decisions,
    state->decision_count
  );
  return SERIALIZED_SCANNER_STATE_SIZE + state->decision_count;
}

void tree_sitter_posix_awk_external_scanner_deserialize(
  void *payload,
  const char *buffer,
  unsigned length
) {
  ScannerState *state = payload;
  *state = (ScannerState){0};
  if (
    length <
    SERIALIZED_SCANNER_STATE_SIZE ||
    length > TREE_SITTER_SERIALIZATION_BUFFER_SIZE
  ) {
    return;
  }
  const unsigned char mode = (unsigned char)buffer[0];
  const uint16_t count =
    (unsigned char)buffer[9] | ((uint16_t)(unsigned char)buffer[10] << 8);
  if (
    mode <=
    LEXICAL_MODE_CONTINUED_NEWLINE &&
    count <=
    DECISION_CAPACITY &&
    length ==
    SERIALIZED_SCANNER_STATE_SIZE +
    count
  ) {
    state->mode = (LexicalMode)mode;
    for (unsigned i = 0; i < 8; i++)
      state->updates |= (uint64_t)(unsigned char)buffer[i + 1] << (i * 8);
    for (unsigned i = 0; i < 8; i++)
      state->remaining_decisions |= (uint64_t)(unsigned char)buffer[i + 11]
        << (i * 8);
    if ((count == 0) != (state->remaining_decisions == 0)) {
      *state = (ScannerState){0};
      return;
    }
    state->decision_count = count;
    memcpy(state->decisions, buffer + SERIALIZED_SCANNER_STATE_SIZE, count);
  }
}

bool tree_sitter_posix_awk_external_scanner_scan(
  void *payload,
  TSLexer *lexer,
  const bool *valid_symbols
) {
  ScannerState *state = payload;
  const bool recovering = valid_symbols[ERROR_SENTINEL];
  if (recovering)
    clear_decisions(state);
  if (!recovering && state->mode == LEXICAL_MODE_OUTSIDE) {
    lexer->mark_end(lexer);
    if (valid_symbols[COMPLETE_FIELD_UPDATE]) {
      if (state->updates)
        state->updates--;
      return emit(lexer, COMPLETE_FIELD_UPDATE);
    }
    while (is_ascii_blank(lexer->lookahead))
      lexer->advance(lexer, true);
    lexer->mark_end(lexer);
    const bool nonempty = lexer->lookahead != ')' && lexer->lookahead != ']';
    if (valid_symbols[SUSPEND_UPDATE] && state->updates && nonempty) {
      state->updates--;
      return emit(lexer, SUSPEND_UPDATE);
    }
    if (valid_symbols[BEGIN_ISOLATION] && nonempty) {
      clear_decisions(state);
      return emit(lexer, BEGIN_ISOLATION);
    }
    if (
      valid_symbols[RESTORE_UPDATE] &&
      (lexer->lookahead == ')' || lexer->lookahead == ']')
    ) {
      state->updates++;
      clear_decisions(state);
      return emit(lexer, RESTORE_UPDATE);
    }
  }
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
  if (lexer->lookahead == '\n' && state->mode != LEXICAL_MODE_OUTSIDE) {
    return emit_literal_break(state, lexer, valid_symbols);
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
    unsigned blanks = 0;
    return skip_blanks_before_newline(lexer, &blanks) &&
      emit_literal_break(state, lexer, valid_symbols);
  }
  if (lexer->lookahead == '#' && valid_symbols[COMMENT]) {
    return scan_comment(lexer);
  }
  if (lexer->lookahead == '"' && valid_symbols[STRING_OPENING]) {
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    return emit_mode(state, lexer, LEXICAL_MODE_STRING, STRING_OPENING);
  }
  if (is_word_start(lexer->lookahead)) {
    return scan_contextual_word(state, lexer, valid_symbols, recovering);
  }
  if (lexer->lookahead == '|') {
    return scan_getline_pipe(lexer, valid_symbols, recovering);
  }
  if (lexer->lookahead == '$' || lexer->lookahead == '!') {
    return scan_prefix_operator(state, lexer, valid_symbols, recovering);
  }
  if (recovering) {
    return false;
  }
  if (lexer->lookahead == '/') {
    return scan_slash_start(state, lexer, valid_symbols);
  }
  return scan_sign(lexer, valid_symbols);
}
