; ==================== Keywords ====================

[
  (kw_and)
  (kw_as)
  (kw_break)
  (kw_continue)
  (kw_do)
  (kw_elif)
  (kw_else)
  (kw_fn)
  (kw_for)
  (kw_if)
  (kw_impl)
  (kw_import)
  (kw_in)
  (kw_is)
  (kw_let)
  (kw_loop)
  (kw_match)
  (kw_not)
  (kw_or)
  (kw_prim)
  (kw_return)
  (kw_row)
  (kw_trait)
  (kw_type)
  (kw_value)
  (kw_while)
  (kw_upper_fn)
] @keyword

; ==================== Literals ====================

(int_literal) @number

(char_literal) @string

(string_expression) @string

(string_content) @string

(string_interpolation) @embedded

; ==================== Comments ====================

(line_comment) @comment

(block_comment) @comment

; ==================== Types ====================

(named_type (upper_id) @type)

(type_declaration (upper_id) @type)

(trait_declaration (upper_id) @type)

(impl_declaration (upper_id) @type)

(parent_type (upper_id) @type)

(type_variable (lower_id) @type)

(import_path (upper_id) @type)

; ==================== Constructors ====================

(constructor_declaration (upper_id) @constructor)

(constructor_expression (upper_id) @constructor)

(bare_constructor_pattern (upper_id) @constructor)

(constructor_pattern (upper_id) @constructor)

; ==================== Variables (general, low priority) ====================

(variable_expression (lower_id) @variable)

(variable_pattern (lower_id) @variable)

; Fields and properties
(field_access_expression (lower_id) @property)

(record_field_expression (lower_id) @property)

(record_type_field (lower_id) @property)

(field (lower_id) @property)

; Label ('ident)
(label) @label

; ==================== Higher priority: parameters and functions ====================
; These come last so they override @variable for the same nodes.

; Parameters
(param (lower_id) @variable.parameter)

(lambda_param (lower_id) @variable.parameter)

; Function name in declaration
(fun_sig (lower_id) @function)

; Function/constructor calls - the callee identifier
(call_expression
  (variable_expression (lower_id) @function))

(call_expression
  (constructor_expression (upper_id) @constructor))
