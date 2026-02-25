/// <reference types="tree-sitter-cli/dsl" />

// Separator with optional trailing separator (matches LALRPOP's Sep<T, S>)
function sep(rule, separator) {
  return optional(seq(rule, repeat(seq(separator, rule)), optional(separator)));
}

// Separator requiring at least one element, no trailing (matches LALRPOP's Sep1<T, S>)
function sep1(rule, separator) {
  return seq(rule, repeat(seq(separator, rule)));
}

module.exports = grammar({
  name: 'fir',

  // word: $ => $.lower_id,  // Disabled: all tokens are external, keywords handled by scanner

  extras: $ => [$.line_comment, $.block_comment],

  externals: $ => [
    // Layout tokens (0-2)
    $._start_block,   // emitted by scanner after ':' when block follows (combines NEWLINE+INDENT)
    $._end_block,     // emitted by scanner when block ends (DEDENT)
    $._newline,

    // Identifiers (3-9)
    $.upper_id,
    $.upper_id_path,             // UpperId.UpperId (no spaces)
    $.upper_id_lbracket,        // UpperId[ (no space)
    $.upper_id_path_lbracket,   // UpperIdPath[ (no space)
    $.upper_id_dot_lbracket,    // UpperId.[
    $.lower_id,
    $.label,                     // 'ident

    // Literals (10-11)
    $.int_literal,
    $.char_literal,

    // String tokens (12-16)
    $.begin_str,
    $.end_str,
    $.string_content,
    $.begin_interpolation,
    $.end_interpolation,

    // Comments (17-18)
    $.block_comment,
    $.line_comment,

    // Delimiters (19-26)
    $.lparen,           // (
    $.rparen,           // )
    $.lbracket,         // [
    $.rbracket,         // ]
    $.lbrace,           // {
    $.rbrace,           // }
    $.backslash_lparen,  // \(
    $.lparen_row,      // row(
    $.lbracket_row,    // row[

    // Punctuation (27-33)
    $._colon,           // :
    $._comma,           // ,
    $._dot,             // .
    $._dotdot,          // ..
    $._eq,              // =
    $._underscore,      // _
    $._slash,           // /
    $._semicolon,       // ;

    // Operators (34-56)
    $._plus,            // +
    $._minus,           // -
    $._star,            // *
    $._eqeq,           // ==
    $._neq,            // !=
    $._lt,             // <
    $._gt,             // >
    $._lteq,           // <=
    $._gteq,           // >=
    $._lshift,         // <<
    $._rshift,         // >>
    $._amp,            // &
    $._ampamp,         // &&
    $._pipe,           // |
    $._tilde,          // ~
    $._exclamation,    // !
    $._percent,        // %
    $._caret,          // ^
    $._pluseq,         // +=
    $._minuseq,        // -=
    $._stareq,         // *=
    $._careteq,        // ^=

    // Keywords (57+) - named for highlighting
    $.kw_and,
    $.kw_as,
    $.kw_break,
    $.kw_continue,
    $.kw_do,
    $.kw_elif,
    $.kw_else,
    $.kw_fn,
    $.kw_upper_fn,    // Fn
    $.kw_for,
    $.kw_if,
    $.kw_impl,
    $.kw_import,
    $.kw_in,
    $.kw_is,
    $.kw_let,
    $.kw_loop,
    $.kw_match,
    $.kw_not,
    $.kw_or,
    $.kw_prim,
    $.kw_return,
    $.kw_trait,
    $.kw_type,
    $.kw_value,
    $.kw_while,
  ],

  rules: {
    source_file: $ => seq(repeat($._top_decl), repeat($._newline)),

    _top_decl: $ => seq(repeat($._newline), choice(
      $.type_declaration,
      $.function_declaration,
      $.import_declaration,
      $.trait_declaration,
      $.impl_declaration,
    )),

    // ==================== Type declarations ====================

    type_declaration: $ => choice(
      // [value] type Name TypeDeclRhs
      seq(optional($.kw_value), $.kw_type, $.upper_id, $._type_decl_rhs),
      // [value] type Name[TypeParams] TypeDeclRhs
      seq(optional($.kw_value), $.kw_type, $.upper_id_lbracket, $.type_params, $._type_decl_rhs),
      // prim type Name NEWLINE
      seq($.kw_prim, $.kw_type, $.upper_id, $._newline),
      // prim type Name[TypeParams] NEWLINE
      seq($.kw_prim, $.kw_type, $.upper_id_lbracket, $.type_params, $._newline),
    ),

    _type_decl_rhs: $ => choice(
      // Empty product
      $._newline,
      // Sum type
      seq($._colon, $._start_block, $.constructor_list, $._end_block),
      // Product with fields
      seq($.lparen, sep($.field, $._comma), $.rparen, $._newline),
    ),

    type_params: $ => seq(sep($.lower_id, $._comma), $.rbracket),

    constructor_list: $ => repeat1($.constructor_declaration),

    constructor_declaration: $ => choice(
      seq($.upper_id, $._newline),
      seq($.upper_id, $.lparen, sep($.field, $._comma), $.rparen, $._newline),
    ),

    field: $ => seq(optional(seq($.lower_id, $._colon)), $._type),

    // ==================== Types ====================

    _type: $ => choice(
      $.named_type,
      $.type_variable,
      $.fn_type,
      $.record_type,
      $.variant_type,
      $.paren_type,
    ),

    named_type: $ => choice(
      $.upper_id,
      seq($.upper_id_lbracket, sep($._type, $._comma), $.rbracket),
    ),

    type_variable: $ => $.lower_id,

    fn_type: $ => seq($.kw_upper_fn, $.lparen, sep($._type, $._comma), $.rparen, optional($._return_type)),

    record_type: $ => choice(
      seq($.lparen, sep($.record_type_field, $._comma), optional($.row_extension), $.rparen),
      seq($.lparen_row, sep($.record_type_field, $._comma), optional($.row_extension), $.rparen),
    ),

    variant_type: $ => choice(
      seq($.lbracket, sep($._named_type_entry, $._comma), optional($.row_extension), $.rbracket),
      seq($.lbracket_row, sep($._named_type_entry, $._comma), optional($.row_extension), $.rbracket),
    ),

    paren_type: $ => seq($.lparen, $._type, $.rparen),

    _named_type_entry: $ => choice(
      $.upper_id,
      seq($.upper_id_lbracket, sep($._type, $._comma), $.rbracket),
    ),

    record_type_field: $ => seq($.lower_id, $._colon, $._type),

    row_extension: $ => seq($._dotdot, $.lower_id),

    // TypeNoFn: same as Type but without fn_type, used in return position
    _type_no_fn: $ => choice(
      $.named_type,
      $.type_variable,
      $.record_type,
      $.variant_type,
      $.paren_type,
    ),

    _return_type: $ => choice(
      $._type_no_fn,
      seq($._type_no_fn, $._slash, $._type_no_fn),
      seq($._slash, $._type_no_fn),
    ),

    // ==================== Function declarations ====================

    function_declaration: $ => choice(
      // Block body
      seq(optional($.parent_type), $._fun_sig, $._colon, $._start_block, $.statements, $._end_block),
      // Inline body
      seq(optional($.parent_type), $._fun_sig, $._colon, $._inline_expr, $._newline),
      // No body / prim
      seq(optional($.kw_prim), optional($.parent_type), $._fun_sig, $._newline),
    ),

    parent_type: $ => seq($.upper_id, $._dot),

    _fun_sig: $ => $.fun_sig,

    fun_sig: $ => seq(
      $.lower_id,
      optional($.context),
      $.param_list,
      optional($._return_type),
    ),

    param_list: $ => seq($.lparen, sep($.param, $._comma), $.rparen),

    param: $ => seq($.lower_id, optional(seq($._colon, $._type))),

    context: $ => seq($.lbracket, sep($._type, $._comma), $.rbracket),

    // ==================== Statements ====================

    statements: $ => repeat1($._statement),

    _statement: $ => choice(
      $.break_statement,
      $.continue_statement,
      $.let_statement,
      $.assign_statement,
      $.expression_statement,
      $.for_statement,
      $.while_statement,
      $.loop_statement,
    ),

    break_statement: $ => seq($.kw_break, optional($.label), $._newline),

    continue_statement: $ => seq($.kw_continue, optional($.label), $._newline),

    let_statement: $ => choice(
      seq($.kw_let, $._pattern, optional(seq($._colon, $._type)), $._eq, $._inline_expr, $._newline),
      seq($.kw_let, $._pattern, optional(seq($._colon, $._type)), $._eq, $._block_expr),
    ),

    assign_statement: $ => choice(
      seq($._inline_expr, $._assign_op, $._inline_expr, $._newline),
      seq($._inline_expr, $._assign_op, $._block_expr),
    ),

    _assign_op: $ => choice($._eq, $._pluseq, $._minuseq, $._stareq, $._careteq),

    expression_statement: $ => choice(
      seq($._inline_expr, $._newline),
      $._block_expr,
    ),

    for_statement: $ => seq(
      optional($.label), $.kw_for, $._pattern, optional(seq($._colon, $._type)),
      $.kw_in, $._expr, $._colon, $._start_block, $.statements, $._end_block,
    ),

    while_statement: $ => seq(
      optional($.label), $.kw_while, $._expr, $._colon,
      $._start_block, $.statements, $._end_block,
    ),

    loop_statement: $ => seq(
      optional($.label), $.kw_loop, $._colon,
      $._start_block, $.statements, $._end_block,
    ),

    // ==================== Expressions ====================

    _expr: $ => choice($._inline_expr, $._block_expr),

    _inline_expr: $ => choice(
      $.variable_expression,
      $.constructor_expression,
      $.parenthesized_expression,
      $.record_expression,
      $.int_literal,
      $.string_expression,
      $.char_literal,
      $.call_expression,
      $.field_access_expression,
      $.sequence_expression,
      $.unary_expression,
      $.binary_expression,
      $.is_expression,
      $.return_expression,
      $.inline_lambda,
    ),

    _block_expr: $ => choice(
      $.match_expression,
      $.if_expression,
      $.do_expression,
      $.block_lambda,
    ),

    variable_expression: $ => prec(0, seq($.lower_id, optional($.type_arguments))),

    constructor_expression: $ => prec(0, choice(
      $.upper_id,
      seq($.upper_id_lbracket, sep1($._type, $._comma), $.rbracket),
      $.upper_id_path,
      seq($.upper_id_path_lbracket, sep1($._type, $._comma), $.rbracket),
    )),

    parenthesized_expression: $ => prec(0, seq($.lparen, $._expr, $.rparen)),

    record_expression: $ => prec(0, seq($.lparen, sep($.record_field_expression, $._comma), $.rparen)),

    record_field_expression: $ => seq($.lower_id, $._eq, $._expr),

    string_expression: $ => seq(
      $.begin_str,
      repeat(choice($.string_content, $.string_interpolation)),
      $.end_str,
    ),

    string_interpolation: $ => seq($.begin_interpolation, $._expr, $.end_interpolation),

    call_expression: $ => prec.left(15, seq($._inline_expr, $.lparen, sep($.call_argument, $._comma), $.rparen)),

    field_access_expression: $ => prec.left(15, seq($._inline_expr, $._dot, $.lower_id, optional($.type_arguments))),

    sequence_expression: $ => prec(0, choice(
      seq($.lbracket, sep($.sequence_element, $._comma), $.rbracket),
      seq($.upper_id_dot_lbracket, sep($.sequence_element, $._comma), $.rbracket),
    )),

    sequence_element: $ => choice(
      $._inline_expr,
      seq($._inline_expr, $._eq, $._inline_expr),
    ),

    unary_expression: $ => choice(
      prec(3, seq($.kw_not, $._inline_expr)),
      prec(3, seq($._minus, $._inline_expr)),
      prec(3, seq($._tilde, $._inline_expr)),
    ),

    binary_expression: $ => choice(
      prec.left(5, seq($._inline_expr, $._star, $._inline_expr)),
      prec.left(5, seq($._inline_expr, $._slash, $._inline_expr)),
      prec.left(6, seq($._inline_expr, $._plus, $._inline_expr)),
      prec.left(6, seq($._inline_expr, $._minus, $._inline_expr)),
      prec.left(7, seq($._inline_expr, $._lshift, $._inline_expr)),
      prec.left(7, seq($._inline_expr, $._rshift, $._inline_expr)),
      prec.left(8, seq($._inline_expr, $._amp, $._inline_expr)),
      prec.left(9, seq($._inline_expr, $._pipe, $._inline_expr)),
      prec.left(10, seq($._inline_expr, $._eqeq, $._inline_expr)),
      prec.left(10, seq($._inline_expr, $._neq, $._inline_expr)),
      prec.left(10, seq($._inline_expr, $._lt, $._inline_expr)),
      prec.left(10, seq($._inline_expr, $._gt, $._inline_expr)),
      prec.left(10, seq($._inline_expr, $._lteq, $._inline_expr)),
      prec.left(10, seq($._inline_expr, $._gteq, $._inline_expr)),
      prec.left(11, seq($._inline_expr, $.kw_and, $._inline_expr)),
      prec.left(12, seq($._inline_expr, $.kw_or, $._inline_expr)),
    ),

    is_expression: $ => prec.left(10, seq($._inline_expr, $.kw_is, $._pattern)),

    return_expression: $ => prec.right(13, seq($.kw_return, optional($._inline_expr))),

    inline_lambda: $ => prec.right(0, seq(
      $.backslash_lparen, sep($.lambda_param, $._comma), $.rparen,
      optional($._return_type), $._colon, $._inline_expr,
    )),

    lambda_param: $ => seq($.lower_id, optional(seq($._colon, $._type))),

    call_argument: $ => choice(
      seq($.lower_id, $._eq, $._expr),
      $._expr,
    ),

    type_arguments: $ => seq($.lbracket, sep($._type, $._comma), $.rbracket),

    // Block expressions

    match_expression: $ => seq(
      $.kw_match, $._inline_expr, $._colon,
      $._start_block, repeat($.match_arm), $._end_block,
    ),

    match_arm: $ => choice(
      seq($._pattern, $._colon, $._start_block, $.statements, $._end_block),
      seq($._pattern, $.kw_if, $._expr, $._colon, $._start_block, $.statements, $._end_block),
      seq($._pattern, $._colon, $._statement),
      seq($._pattern, $.kw_if, $._expr, $._colon, $._statement),
    ),

    if_expression: $ => seq(
      $.kw_if, $._expr, $._colon, $._start_block, $.statements, $._end_block,
      repeat($.elif_clause),
      optional($.else_clause),
    ),

    elif_clause: $ => seq($.kw_elif, $._expr, $._colon, $._start_block, $.statements, $._end_block),

    else_clause: $ => seq($.kw_else, $._colon, $._start_block, $.statements, $._end_block),

    do_expression: $ => seq($.kw_do, $._colon, $._start_block, $.statements, $._end_block),

    block_lambda: $ => seq(
      $.backslash_lparen, sep($.lambda_param, $._comma), $.rparen,
      optional($._return_type), $._colon,
      $._start_block, $.statements, $._end_block,
    ),

    // ==================== Patterns ====================

    _pattern: $ => choice(
      $.variable_pattern,
      $.constructor_pattern,
      $.bare_constructor_pattern,
      $.record_pattern,
      $.ignore_pattern,
      $.string_pattern,
      $.char_pattern,
      $.variant_pattern,
      $.or_pattern,
    ),

    variable_pattern: $ => $.lower_id,

    bare_constructor_pattern: $ => prec(-1, $._constructor),

    constructor_pattern: $ => choice(
      seq($._constructor, $.lparen, $._field_pats, $.rparen),
      seq($._constructor, $.lparen, $.rparen),
    ),

    _constructor: $ => choice($.upper_id, $.upper_id_path),

    record_pattern: $ => choice(
      seq($.lparen, $._field_pats, $.rparen),
      seq($.lparen, $.rparen),
    ),

    ignore_pattern: $ => $._underscore,

    string_pattern: $ => seq($.begin_str, optional($.string_content), $.end_str),

    char_pattern: $ => $.char_literal,

    variant_pattern: $ => prec(2, seq($._tilde, $._pattern)),

    or_pattern: $ => prec.right(11, seq($._pattern, $._pipe, $._pattern)),

    _field_pats: $ => choice(
      $._dotdot,
      seq(sep1($.field_pattern, $._comma), optional($._comma)),  // allow trailing comma
      seq(sep1($.field_pattern, $._comma), $._comma, $._dotdot),
    ),

    field_pattern: $ => choice(
      seq($.lower_id, $._eq, $._pattern),
      $._pattern,
    ),

    // ==================== Import declarations ====================

    import_declaration: $ => seq(
      $.kw_import, $.lbracket,
      sep1($.import_path, $._comma), optional($._comma),
      $.rbracket, $._newline,
    ),

    import_path: $ => sep1($.upper_id, $._slash),

    // ==================== Trait declarations ====================

    trait_declaration: $ => choice(
      seq($.kw_trait, $.upper_id_lbracket, sep($.lower_id, $._comma), $.rbracket, $._colon,
          $._start_block, repeat1($._trait_item), $._end_block),
      seq($.kw_trait, $.upper_id_lbracket, sep($.lower_id, $._comma), $.rbracket),
    ),

    _trait_item: $ => $.trait_function_declaration,

    trait_function_declaration: $ => choice(
      seq($._fun_sig, $._colon, $._start_block, $.statements, $._end_block),
      seq(optional($.kw_prim), $._fun_sig, $._newline),
      seq($._fun_sig, $._colon, $._inline_expr, $._newline),
    ),

    // ==================== Impl declarations ====================

    impl_declaration: $ => choice(
      seq($.kw_impl, optional($.context), $.upper_id_lbracket, sep($._type, $._comma), $.rbracket, $._colon,
          $._start_block, repeat1($._impl_item), $._end_block),
      seq($.kw_impl, optional($.context), $.upper_id_lbracket, sep($._type, $._comma), $.rbracket),
    ),

    _impl_item: $ => $.impl_function_declaration,

    impl_function_declaration: $ => choice(
      seq($._fun_sig, $._colon, $._start_block, $.statements, $._end_block),
      seq(optional($.kw_prim), $._fun_sig, $._newline),
      seq($._fun_sig, $._colon, $._inline_expr, $._newline),
    ),
  },
});
