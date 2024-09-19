- +/- unary operators
- reassign indentifiers
- additional unary operations (e.g. !)
- while loops
- for loops
- arrays
- functions
- objects/structs
- classes
- optimizer for produced asm
- require braces around subsequent operators (e.g. "2 + -1" should require "2 + (-1)")
- remove condensing of multiple plus/minus (e.g. "----5" should throw an error instead of being used as "+5")

done:
- add line numbers to error messages
- fix calculations with same operator precedence
