(string_content) @string

(comment) @comment

(line_continuation) @punctuation.special

[
  (number)
  (dup_count)
] @number

(name) @variable

(func_name) @function.call

(builtin_func_name) @function.builtin

[
  (begin_keyword)
  (break_keyword)
  (continue_keyword)
  (delete_keyword)
  (do_keyword)
  (else_keyword)
  (end_keyword)
  (exit_keyword)
  (for_keyword)
  (function_keyword)
  (getline_keyword)
  (if_keyword)
  (in_keyword)
  (next_keyword)
  (nextfile_keyword)
  (print_keyword)
  (printf_keyword)
  (return_keyword)
  (while_keyword)
] @keyword

[
  (add_assign)
  (and)
  (append)
  (decr)
  (div_assign)
  (eq)
  (ge)
  (incr)
  (le)
  (mod_assign)
  (mul_assign)
  (ne)
  (no_match)
  (or)
  (pow_assign)
  (sub_assign)
  (left_anchor)
  (right_anchor)
  (repetition_modifier)
  "!"
  "$"
  "%"
  "*"
  "+"
  "-"
  "/"
  ":"
  "<"
  "="
  ">"
  "?"
  "^"
  "|"
  "~"
] @operator

[
  ","
  ";"
] @punctuation.delimiter

[
  "("
  ")"
  "{"
  "}"
] @punctuation.bracket

(lvalue
  [
    "["
    "]"
  ] @punctuation.bracket)

(simple_statement
  [
    "["
    "]"
  ] @punctuation.bracket)

(string
  "\"" @punctuation.delimiter)

(ere
  "/" @punctuation.delimiter)

(ordinary_character) @string.regexp

[
  (collating_element)
  (meta_character)
  (class_name)
  (wildcard)
] @character.special

(start_range
  "-" @operator)

(collating_element
  "-" @string.regexp)

(bracket_list
  "-" @string.regexp)

(range_expression
  "-" @string.regexp)

(bracket_expression
  [
    "["
    "]"
  ] @punctuation.bracket)

(character_class
  [
    "["
    "]"
  ] @punctuation.bracket)

(character_class
  ":" @punctuation.delimiter)

(collating_symbol
  [
    "["
    "]"
  ] @punctuation.bracket)

(collating_symbol
  "." @punctuation.delimiter)

(equivalence_class
  [
    "["
    "]"
  ] @punctuation.bracket)

(equivalence_class
  "=" @punctuation.delimiter)

[
  (escaped_delimiter)
  (quoted_character)
  (escape_sequence)
] @string.escape

(ordinary_character
  [
    ")"
    "}"
  ] @string.regexp)

(escaped_delimiter
  "/" @string.escape)

(non_unary_expr
  [
    "?"
    ":"
  ] @keyword.conditional.ternary)

(unary_expr
  [
    "?"
    ":"
  ] @keyword.conditional.ternary)

(non_unary_print_expr
  [
    "?"
    ":"
  ] @keyword.conditional.ternary)

(unary_print_expr
  [
    "?"
    ":"
  ] @keyword.conditional.ternary)

(item
  name: [
    (name)
    (func_name)
  ] @function)

(param_list
  (name) @variable.parameter)
