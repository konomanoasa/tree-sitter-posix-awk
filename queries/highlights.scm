(string_content
  !content) @string

(string_content
  content: (token_content) @string)

(comment) @comment

(line_continuation) @punctuation.special

[
  (number
    !content)
  (dup_count
    !content)
] @number

(number
  content: (token_content) @number)

(dup_count
  content: (token_content) @number)

(name
  !content) @variable

(name
  content: (token_content) @variable)

(func_name
  !content) @function.call

(func_name
  content: (token_content) @function.call)

(builtin_func_name
  !content) @function.builtin

(builtin_func_name
  content: (token_content) @function.builtin)

[
  (begin_keyword
    !content)
  (break_keyword
    !content)
  (continue_keyword
    !content)
  (delete_keyword
    !content)
  (do_keyword
    !content)
  (else_keyword
    !content)
  (end_keyword
    !content)
  (exit_keyword
    !content)
  (for_keyword
    !content)
  (function_keyword
    !content)
  (getline_keyword
    !content)
  (if_keyword
    !content)
  (in_keyword
    !content)
  (next_keyword
    !content)
  (nextfile_keyword
    !content)
  (print_keyword
    !content)
  (printf_keyword
    !content)
  (return_keyword
    !content)
  (while_keyword
    !content)
] @keyword

(begin_keyword
  content: (token_content) @keyword)

(break_keyword
  content: (token_content) @keyword)

(continue_keyword
  content: (token_content) @keyword)

(delete_keyword
  content: (token_content) @keyword)

(do_keyword
  content: (token_content) @keyword)

(else_keyword
  content: (token_content) @keyword)

(end_keyword
  content: (token_content) @keyword)

(exit_keyword
  content: (token_content) @keyword)

(for_keyword
  content: (token_content) @keyword)

(function_keyword
  content: (token_content) @keyword)

(getline_keyword
  content: (token_content) @keyword)

(if_keyword
  content: (token_content) @keyword)

(in_keyword
  content: (token_content) @keyword)

(next_keyword
  content: (token_content) @keyword)

(nextfile_keyword
  content: (token_content) @keyword)

(print_keyword
  content: (token_content) @keyword)

(printf_keyword
  content: (token_content) @keyword)

(return_keyword
  content: (token_content) @keyword)

(while_keyword
  content: (token_content) @keyword)

[
  (add_assign
    !content)
  (and
    !content)
  (append
    !content)
  (decr
    !content)
  (div_assign
    !content)
  (eq
    !content)
  (ge
    !content)
  (incr
    !content)
  (le
    !content)
  (mod_assign
    !content)
  (mul_assign
    !content)
  (ne
    !content)
  (no_match
    !content)
  (or
    !content)
  (pow_assign
    !content)
  (sub_assign
    !content)
  (left_anchor)
  (right_anchor)
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

(add_assign
  content: (token_content) @operator)

(and
  content: (token_content) @operator)

(append
  content: (token_content) @operator)

(decr
  content: (token_content) @operator)

(div_assign
  content: (token_content) @operator)

(eq
  content: (token_content) @operator)

(ge
  content: (token_content) @operator)

(incr
  content: (token_content) @operator)

(le
  content: (token_content) @operator)

(mod_assign
  content: (token_content) @operator)

(mul_assign
  content: (token_content) @operator)

(ne
  content: (token_content) @operator)

(no_match
  content: (token_content) @operator)

(or
  content: (token_content) @operator)

(pow_assign
  content: (token_content) @operator)

(sub_assign
  content: (token_content) @operator)

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

(ordinary_character_content) @string.regexp

[
  (collating_element_content)
  (meta_character)
  (class_name
    !content)
] @character.special

(class_name
  content: (token_content) @character.special)

(wildcard
  "." @character.special)

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
  (escaped_delimiter
    !content)
  (escape_sequence
    !content)
] @string.escape

(escaped_delimiter
  content: (token_content) @string.escape)

(escape_sequence
  content: (token_content) @string.escape)

(ordinary_character
  [
    ")"
    "}"
  ] @string.regexp)

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
    (name
      !content)
    (func_name
      !content)
  ] @function)

(item
  name: [
    (name
      content: (token_content) @function)
    (func_name
      content: (token_content) @function)
  ])

(param_list
  (name
    !content) @variable.parameter)

(param_list
  (name
    content: (token_content) @variable.parameter))
