# comment
# <- comment

function add(left, right) {
# <- keyword
#        ^^^ function
#           ^ punctuation.bracket
#            ^^^^ variable.parameter
#                ^ punctuation.delimiter
#                  ^^^^^ variable.parameter
#                       ^ punctuation.bracket
#                         ^ punctuation.bracket
  return left + right
# ^^^^^^ keyword
#        ^^^^ variable
#             ^ operator
#               ^^^^^ variable
}
# <- punctuation.bracket

BEGIN {
# <- keyword
#     ^ punctuation.bracket
  value = add(atan2(1, 2), 3)
# ^^^^^ variable
#       ^ operator
#         ^^^ function.call
#            ^ punctuation.bracket
#             ^^^^^ function.builtin
#                  ^ punctuation.bracket
#                   ^ number
#                    ^ punctuation.delimiter
#                      ^ number
#                       ^^ punctuation.bracket
#                          ^ number
#                           ^ punctuation.bracket

  text = "a\n"
# ^^^^ variable
#      ^ operator
#        ^ punctuation.delimiter
#         ^ string
#          ^^ string.escape
#            ^ punctuation.delimiter

  array[value] = $1
# ^^^^^ variable
#      ^ punctuation.bracket
#       ^^^^^ variable
#            ^ punctuation.bracket
#              ^ operator
#                ^ operator
#                 ^ number

  result = value ? left : right
# ^^^^^^ variable
#        ^ operator
#          ^^^^^ variable
#                ^ keyword.conditional.ternary
#                  ^^^^ variable
#                       ^ keyword.conditional.ternary
#                         ^^^^^ variable

  print value, text > "output"
# ^^^^^ keyword
#       ^^^^^ variable
#            ^ punctuation.delimiter
#              ^^^^ variable
#                   ^ operator
#                     ^ punctuation.delimiter
#                      ^^^^^^ string
#                            ^ punctuation.delimiter

  value += 2
#       ^^ operator
#          ^ number

  print value, \
#              ^ punctuation.special
    text
#   ^^^^ variable
}
# <- punctuation.bracket

/^(ab|c)+d{2,3}?e?f*$/ { print }
# <- punctuation.delimiter
#^ operator
# ^ punctuation.bracket
#  ^^ string.regexp
#    ^ operator
#     ^ string.regexp
#      ^ punctuation.bracket
#       ^ operator
#        ^ string.regexp
#         ^ punctuation.bracket
#          ^ number
#           ^ punctuation.delimiter
#            ^ number
#             ^ punctuation.bracket
#              ^ operator
#               ^ string.regexp
#                ^ operator
#                 ^ string.regexp
#                  ^ operator
#                   ^ operator
#                    ^ punctuation.delimiter
#                      ^ punctuation.bracket
#                        ^^^^^ keyword
#                              ^ punctuation.bracket

/a\n\t\/b/ { print }
# <- punctuation.delimiter
#^ string.regexp
# ^^ string.escape
#   ^^ string.escape
#     ^^ string.escape
#       ^ string.regexp
#        ^ punctuation.delimiter

/[^]a-c[:alpha:][.].][=a=]-]/ { print }
# <- punctuation.delimiter
#^ punctuation.bracket
# ^ operator
#  ^ character.special
#   ^ character.special
#    ^ operator
#     ^ character.special
#      ^ punctuation.bracket
#       ^ punctuation.delimiter
#        ^^^^^ character.special
#             ^ punctuation.delimiter
#              ^ punctuation.bracket
#               ^ punctuation.bracket
#                ^ punctuation.delimiter
#                 ^ character.special
#                  ^ punctuation.delimiter
#                   ^ punctuation.bracket
#                    ^ punctuation.bracket
#                     ^ punctuation.delimiter
#                      ^ character.special
#                       ^ punctuation.delimiter
#                        ^ punctuation.bracket
#                         ^ string.regexp
#                          ^ punctuation.bracket
#                           ^ punctuation.delimiter

/[%--]/ { print }
# <- punctuation.delimiter
#^ punctuation.bracket
# ^ character.special
#  ^ operator
#   ^ string.regexp
#    ^ punctuation.bracket
#     ^ punctuation.delimiter

/[%--@]/ { print }
# <- punctuation.delimiter
#^ punctuation.bracket
# ^ character.special
#  ^ operator
#   ^ string.regexp
#    ^ character.special
#     ^ punctuation.bracket
#      ^ punctuation.delimiter

/[-]/ { print }
# <- punctuation.delimiter
#^ punctuation.bracket
# ^ string.regexp
#  ^ punctuation.bracket
#   ^ punctuation.delimiter

/a)b}c/ { print }
# <- punctuation.delimiter
#^ string.regexp
# ^ string.regexp
#  ^ string.regexp
#   ^ string.regexp
#    ^ string.regexp
#     ^ punctuation.delimiter

function spaced (first) { return first }
# <- keyword
#        ^^^^^^ function
#               ^ punctuation.bracket
#                ^^^^^ variable.parameter
#                     ^ punctuation.bracket
