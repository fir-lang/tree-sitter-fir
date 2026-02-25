#include "tree_sitter/parser.h"

#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// Token types - MUST match the externals array order in grammar.js exactly
enum TokenType {
    // Layout tokens (0-2)
    START_BLOCK,  // emitted after ':' when an indented block follows (combines NEWLINE+INDENT)
    END_BLOCK,    // emitted when an indented block ends (DEDENT)
    NEWLINE,

    // Identifiers (3-5)
    UPPER_ID,
    LOWER_ID,
    LABEL,

    // Literals (10-11)
    INT_LITERAL,
    CHAR_LITERAL,

    // String tokens (12-16)
    BEGIN_STR,
    END_STR,
    STRING_CONTENT,
    BEGIN_INTERPOLATION,
    END_INTERPOLATION,

    // Comments (17-18)
    BLOCK_COMMENT,
    LINE_COMMENT,

    // Delimiters (19-27)
    LPAREN,
    RPAREN,
    LBRACKET,
    RBRACKET,
    LBRACE,
    RBRACE,
    BACKSLASH_LPAREN,

    // Punctuation (26-33)
    COLON,
    COMMA,
    DOT,
    DOTDOT,
    EQ,
    UNDERSCORE,
    SLASH,
    SEMICOLON,

    // Operators (36-57)
    PLUS,
    MINUS,
    STAR,
    EQEQ,
    NEQ,
    LT,
    GT,
    LTEQ,
    GTEQ,
    LSHIFT,
    RSHIFT,
    AMP,
    AMPAMP,
    PIPE,
    TILDE,
    EXCLAMATION,
    PERCENT,
    CARET,
    PLUSEQ,
    MINUSEQ,
    STAREQ,
    CARETEQ,

    // Keywords (58+)
    KW_AND,
    KW_AS,
    KW_BREAK,
    KW_CONTINUE,
    KW_DO,
    KW_ELIF,
    KW_ELSE,
    KW_FN,
    KW_UPPER_FN,
    KW_FOR,
    KW_IF,
    KW_IMPL,
    KW_IMPORT,
    KW_IN,
    KW_IS,
    KW_LET,
    KW_LOOP,
    KW_MATCH,
    KW_NOT,
    KW_OR,
    KW_PRIM,
    KW_RETURN,
    KW_TRAIT,
    KW_TYPE,
    KW_VALUE,
    KW_WHILE,
    // 'row' is not a keyword in the reference implementation, but it is here,
    // to be able to treat `row[` as two tokens and use the `[` as a delimiter
    // in queries.
    KW_ROW,

    TOKEN_COUNT,
};

// Frame types for the delimiter/indentation stack
typedef enum {
    FRAME_INDENTED,
    FRAME_PAREN,
    FRAME_BRACKET,
    FRAME_INTERPOLATION,
} FrameKind;

typedef struct {
    FrameKind kind;
    uint16_t block_col;  // only meaningful for FRAME_INDENTED
} Frame;

#define MAX_DEPTH 128

typedef struct {
    Frame stack[MAX_DEPTH];
    uint8_t depth;           // always >= 1 (bottom = FRAME_INDENTED col=0)
    uint8_t pending_end_blocks;
    bool in_string;          // inside a string literal
    bool eof_newline_emitted;
} Scanner;

// ==================== Helpers ====================

static inline bool is_upper(int32_t c) { return c >= 'A' && c <= 'Z'; }
static inline bool is_lower(int32_t c) { return c >= 'a' && c <= 'z'; }
static inline bool is_digit(int32_t c) { return c >= '0' && c <= '9'; }
static inline bool is_alnum(int32_t c) { return is_upper(c) || is_lower(c) || is_digit(c); }
static inline bool is_id_char(int32_t c) { return is_alnum(c) || c == '_'; }
static inline bool is_hex(int32_t c) { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static inline bool is_bin(int32_t c) { return c == '0' || c == '1'; }

static Frame top_frame(Scanner *s) {
    return s->stack[s->depth - 1];
}

static void push_frame(Scanner *s, FrameKind kind, uint16_t col) {
    if (s->depth < MAX_DEPTH) {
        s->stack[s->depth].kind = kind;
        s->stack[s->depth].block_col = col;
        s->depth++;
    }
}

static void pop_frame(Scanner *s) {
    if (s->depth > 1) {
        s->depth--;
    }
}

// Check if we're inside a non-indented frame (paren/bracket/interpolation)
static bool in_non_indented(Scanner *s) {
    Frame f = top_frame(s);
    return f.kind != FRAME_INDENTED;
}

// Count INDENTED frames above the nearest non-INDENTED frame
static uint8_t indented_frames_above_delimiter(Scanner *s) {
    uint8_t count = 0;
    for (int i = s->depth - 1; i >= 0; i--) {
        if (s->stack[i].kind == FRAME_INDENTED) {
            count++;
        } else {
            break;
        }
    }
    return count;
}

// Advance the lexer and return the character consumed
static int32_t advance(TSLexer *lexer) {
    int32_t c = lexer->lookahead;
    lexer->advance(lexer, false);
    return c;
}

static void skip(TSLexer *lexer) {
    lexer->advance(lexer, true);
}

// Skip horizontal whitespace (spaces and tabs), return the column after skipping
static void skip_horizontal_ws(TSLexer *lexer) {
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
        skip(lexer);
    }
}

// Skip all whitespace including newlines
static void skip_all_ws(TSLexer *lexer) {
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
           lexer->lookahead == '\n' || lexer->lookahead == '\r') {
        skip(lexer);
    }
}

// Skip newlines and blank lines (lines with only whitespace).
// Stops at any non-whitespace character, including comments.
static void skip_blank_lines(TSLexer *lexer) {
    while (true) {
        if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
            skip(lexer);
            continue;
        }
        if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                skip(lexer);
            }
            // If we hit a newline, it was a blank line — continue skipping
            if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
                continue;
            }
            // Otherwise we found a non-blank line
            break;
        }
        break;
    }
}

// ==================== Keyword matching ====================

typedef struct {
    const char *text;
    enum TokenType token;
} Keyword;

static const Keyword keywords[] = {
    {"and", KW_AND},
    {"as", KW_AS},
    {"break", KW_BREAK},
    {"continue", KW_CONTINUE},
    {"do", KW_DO},
    {"elif", KW_ELIF},
    {"else", KW_ELSE},
    {"fn", KW_FN},
    {"for", KW_FOR},
    {"if", KW_IF},
    {"impl", KW_IMPL},
    {"import", KW_IMPORT},
    {"in", KW_IN},
    {"is", KW_IS},
    {"let", KW_LET},
    {"loop", KW_LOOP},
    {"match", KW_MATCH},
    {"not", KW_NOT},
    {"or", KW_OR},
    {"prim", KW_PRIM},
    {"return", KW_RETURN},
    {"row", KW_ROW},
    {"trait", KW_TRAIT},
    {"type", KW_TYPE},
    {"value", KW_VALUE},
    {"while", KW_WHILE},
    {NULL, 0},
};

static enum TokenType lookup_keyword(const char *word, int len) {
    for (const Keyword *kw = keywords; kw->text; kw++) {
        if ((int)strlen(kw->text) == len && strncmp(kw->text, word, len) == 0) {
            return kw->token;
        }
    }
    return LOWER_ID;
}

// ==================== Token scanning functions ====================

// Scan an upper-case identifier.
// Assumes lookahead is '_' or uppercase letter.
// Returns the token type (UPPER_ID or KW_UPPER_FN).
static enum TokenType scan_upper_id(TSLexer *lexer, const bool *valid) {
    // Consume _*[A-Z][A-Za-z0-9_]*
    char word[64];
    int len = 0;
    while (lexer->lookahead == '_' && len < 63) { word[len++] = (char)advance(lexer); }
    if (!is_upper(lexer->lookahead)) return TOKEN_COUNT; // not actually upper id
    word[len++] = (char)advance(lexer); // consume first upper char
    while (is_id_char(lexer->lookahead) && len < 63) { word[len++] = (char)advance(lexer); }
    word[len] = '\0';

    lexer->mark_end(lexer);

    // Check for "Fn" keyword
    if (len == 2 && word[0] == 'F' && word[1] == 'n') {
        if (valid[KW_UPPER_FN]) return KW_UPPER_FN;
    }

    return UPPER_ID;
}

// Scan a lower-case identifier or keyword.
static enum TokenType scan_lower_id_or_keyword(TSLexer *lexer, Scanner *scanner, const bool *valid) {
    char word[64];
    int len = 0;

    // Consume _*[a-z][A-Za-z0-9_]*
    while (lexer->lookahead == '_' && len < 63) {
        word[len++] = (char)advance(lexer);
    }
    if (!is_lower(lexer->lookahead)) {
        // Just underscores - this shouldn't happen (underscore is separate)
        return TOKEN_COUNT;
    }
    word[len++] = (char)advance(lexer);
    while (is_id_char(lexer->lookahead) && len < 63) {
        word[len++] = (char)advance(lexer);
    }
    word[len] = '\0';

    lexer->mark_end(lexer);

    // Check for keyword
    enum TokenType kw = lookup_keyword(word, len);
    if (kw != LOWER_ID && valid[kw]) {
        return kw;
    }

    return LOWER_ID;
}

// Scan an integer literal.
// Supports decimal, hex (0x), binary (0b), with _ separators.
// Note: We don't handle negative literals here — unary minus is a separate operator.
static bool scan_int_literal(TSLexer *lexer) {
    if (!is_digit(lexer->lookahead)) return false;

    if (lexer->lookahead == '0') {
        advance(lexer);
        if (lexer->lookahead == 'x' || lexer->lookahead == 'X') {
            advance(lexer); // consume 'x'
            if (!is_hex(lexer->lookahead) && lexer->lookahead != '_') return false;
            while (is_hex(lexer->lookahead) || lexer->lookahead == '_') advance(lexer);
            return true;
        }
        if (lexer->lookahead == 'b' || lexer->lookahead == 'B') {
            advance(lexer); // consume 'b'
            if (!is_bin(lexer->lookahead) && lexer->lookahead != '_') return false;
            while (is_bin(lexer->lookahead) || lexer->lookahead == '_') advance(lexer);
            return true;
        }
        // Fall through to decimal
    }

    // Decimal
    while (is_digit(lexer->lookahead) || lexer->lookahead == '_') advance(lexer);
    return true;
}

// Scan a character literal. Assumes lookahead is '\''.
// Returns true if successful.
static bool scan_char_literal(TSLexer *lexer) {
    advance(lexer); // consume opening '

    if (lexer->lookahead == '\\') {
        advance(lexer); // consume backslash
        // Escape character: \', \n, \t, \r, \\
        advance(lexer); // consume escape char
    } else if (lexer->lookahead != '\'' && lexer->lookahead != 0) {
        // Regular character (could be multi-byte UTF-8)
        advance(lexer);
    } else {
        return false;
    }

    if (lexer->lookahead == '\'') {
        advance(lexer); // consume closing '
        return true;
    }
    return false;
}

// Scan string content. Consumes characters until " or ` or EOF.
// Handles escape sequences.
static bool scan_string_content(TSLexer *lexer) {
    bool has_content = false;

    while (true) {
        if (lexer->lookahead == '"' || lexer->lookahead == '`' || lexer->lookahead == 0) {
            return has_content;
        }
        if (lexer->lookahead == '\\') {
            has_content = true;
            advance(lexer); // consume backslash
            if (lexer->lookahead == '\n') {
                // Continuation escape: skip newline and following whitespace
                advance(lexer);
                while (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
                       lexer->lookahead == '\n' || lexer->lookahead == '\r') {
                    advance(lexer);
                }
            } else if (lexer->lookahead != 0) {
                advance(lexer); // consume escaped char
            }
        } else {
            has_content = true;
            advance(lexer);
        }
    }
}

// ==================== Main scan function ====================

static bool scan(Scanner *scanner, TSLexer *lexer, const bool *valid) {
    // 1. Pending end_blocks (dedents)
    if (scanner->pending_end_blocks > 0 && valid[END_BLOCK]) {
        scanner->pending_end_blocks--;
        pop_frame(scanner);
        lexer->result_symbol = END_BLOCK;
        return true;
    }

    // 2. String mode
    if (scanner->in_string) {
        if (valid[STRING_CONTENT] && lexer->lookahead != '"' && lexer->lookahead != '`' && lexer->lookahead != 0) {
            lexer->result_symbol = STRING_CONTENT;
            return scan_string_content(lexer);
        }
        if (valid[END_STR] && lexer->lookahead == '"') {
            advance(lexer);
            scanner->in_string = false;
            lexer->result_symbol = END_STR;
            return true;
        }
        if (valid[BEGIN_INTERPOLATION] && lexer->lookahead == '`') {
            advance(lexer);
            scanner->in_string = false;
            push_frame(scanner, FRAME_INTERPOLATION, 0);
            lexer->result_symbol = BEGIN_INTERPOLATION;
            return true;
        }
        return false;
    }

    // 3. Handle whitespace and layout

    // In non-indented mode: skip whitespace
    if (in_non_indented(scanner)) {
        // Skip horizontal whitespace first
        skip_horizontal_ws(lexer);

        // If grammar wants NEWLINE and we're at a newline, emit it
        if (valid[NEWLINE] && (lexer->lookahead == '\n' || lexer->lookahead == '\r')) {
            // Skip the newline(s) and any following blank lines
            while (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
                skip(lexer);
                skip_horizontal_ws(lexer);
            }
            lexer->result_symbol = NEWLINE;
            return true;
        }

        // Skip remaining whitespace (newlines are not significant in non-indented mode).
        // Don't skip comments — let section 4 emit them as proper tokens.
        skip_all_ws(lexer);

        // Check for START_BLOCK request inside non-indented context.
        // If we're at a comment, fall through to section 4 to emit it first.
        // Tree-sitter will call us again with valid[START_BLOCK] still true.
        if (valid[START_BLOCK] && lexer->lookahead != '#') {
            uint32_t col = lexer->get_column(lexer);
            push_frame(scanner, FRAME_INDENTED, (uint16_t)col);
            lexer->result_symbol = START_BLOCK;
            return true;
        }
    } else {
        // In indented mode: skip horizontal whitespace only.
        // Newlines are significant for layout.

        bool at_newline = false;

        // Skip horizontal whitespace on current line
        skip_horizontal_ws(lexer);

        // Check for newline(s)
        while (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
            at_newline = true;
            skip(lexer);
            // Skip horizontal whitespace on next line
            skip_horizontal_ws(lexer);
            // Skip blank lines
            if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
                continue;
            }
            break;
        }

        // EOF handling
        if (lexer->eof(lexer)) {
            if (valid[NEWLINE] && !scanner->eof_newline_emitted) {
                scanner->eof_newline_emitted = true;
                lexer->result_symbol = NEWLINE;
                return true;
            }
            if (valid[END_BLOCK] && top_frame(scanner).kind == FRAME_INDENTED && scanner->depth > 1) {
                pop_frame(scanner);
                lexer->result_symbol = END_BLOCK;
                return true;
            }
            return false;
        }

        // Handle START_BLOCK request (grammar just saw ':' and wants to open a block).
        //
        // Instead of consuming comments via skip() (which hides them from the parse
        // tree), we fall through to section 4 to emit any comment as an extras token.
        // Tree-sitter will call us again with valid[START_BLOCK] still true, and we
        // can then emit START_BLOCK once we reach a non-comment token.
        if (valid[START_BLOCK]) {
            if (!at_newline) {
                // Still on the ':' line after horizontal whitespace was skipped.
                if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
                    // Just whitespace after ':', skip to next non-blank line.
                    skip_blank_lines(lexer);
                    // Now at first non-blank-line token, fall through below.
                } else if (lexer->lookahead != '#') {
                    // Code on the same line as ':' (e.g. `A: expr`).
                    // Use current column as block indent.
                    uint32_t col = lexer->get_column(lexer);
                    push_frame(scanner, FRAME_INDENTED, (uint16_t)col);
                    lexer->result_symbol = START_BLOCK;
                    return true;
                }
                // If lookahead is '#': comment on ':' line, fall through to
                // section 4 to emit it. Tree-sitter will call us again with
                // valid[START_BLOCK] still true.
            }

            // We've crossed a newline. At the first non-blank-line token.
            // If it's a comment, fall through to section 4 to emit it.
            // Otherwise emit START_BLOCK.
            if (lexer->lookahead != '#') {
                uint32_t col = lexer->get_column(lexer);
                push_frame(scanner, FRAME_INDENTED, (uint16_t)col);
                lexer->result_symbol = START_BLOCK;
                return true;
            }
            // Fall through to section 4 to emit the comment.
        }

        // Check for closing delimiters/comma that terminate indented blocks
        if (lexer->lookahead == ')' || lexer->lookahead == ']' ||
            lexer->lookahead == ',' || lexer->lookahead == '}') {
            if (valid[NEWLINE]) {
                lexer->result_symbol = NEWLINE;
                uint8_t count = indented_frames_above_delimiter(scanner);
                if (count > 0) {
                    scanner->pending_end_blocks = count;
                }
                return true;
            }
            if (valid[END_BLOCK] && top_frame(scanner).kind == FRAME_INDENTED && scanner->depth > 1) {
                pop_frame(scanner);
                lexer->result_symbol = END_BLOCK;
                return true;
            }
        }

        // Indentation check after newline
        if (at_newline) {
            uint32_t col = lexer->get_column(lexer);
            Frame frame = top_frame(scanner);

            if (col < frame.block_col) {
                // Dedented - count how many frames need to be popped
                uint8_t dedent_count = 0;
                for (int i = scanner->depth - 1; i >= 1; i--) {
                    if (scanner->stack[i].kind == FRAME_INDENTED && scanner->stack[i].block_col > col) {
                        dedent_count++;
                    } else {
                        break;
                    }
                }
                if (dedent_count == 0) dedent_count = 1; // at least one
                if (valid[NEWLINE]) {
                    scanner->pending_end_blocks = dedent_count;
                    lexer->result_symbol = NEWLINE;
                    return true;
                }
                if (valid[END_BLOCK]) {
                    scanner->pending_end_blocks = dedent_count - 1;
                    pop_frame(scanner);
                    lexer->result_symbol = END_BLOCK;
                    return true;
                }
            } else if (col == frame.block_col) {
                if (valid[NEWLINE]) {
                    lexer->result_symbol = NEWLINE;
                    return true;
                }
            } else {
                // col > frame.block_col: continuation line — no NEWLINE emitted.
                // Matches the reference scanner which also emits nothing for continuation
                // lines, allowing multi-line expressions (e.g. `if expr\n  is Pat:`).
            }
        }
    }

    // 4. Scan actual tokens

    int32_t c = lexer->lookahead;

    // EOF
    if (lexer->eof(lexer)) {
        if (valid[NEWLINE] && !scanner->eof_newline_emitted) {
            scanner->eof_newline_emitted = true;
            lexer->result_symbol = NEWLINE;
            return true;
        }
        if (valid[END_BLOCK] && top_frame(scanner).kind == FRAME_INDENTED && scanner->depth > 1) {
            pop_frame(scanner);
            lexer->result_symbol = END_BLOCK;
            return true;
        }
        return false;
    }

    // Comments
    if (c == '#') {
        lexer->mark_end(lexer);
        advance(lexer);
        if (lexer->lookahead == '|') {
            // Block comment
            if (valid[BLOCK_COMMENT]) {
                advance(lexer); // consume '|'
                int depth = 1;
                while (depth > 0 && lexer->lookahead != 0) {
                    if (lexer->lookahead == '#') {
                        advance(lexer);
                        if (lexer->lookahead == '|') { advance(lexer); depth++; }
                    } else if (lexer->lookahead == '|') {
                        advance(lexer);
                        if (lexer->lookahead == '#') { advance(lexer); depth--; }
                    } else {
                        advance(lexer);
                    }
                }
                lexer->mark_end(lexer);
                lexer->result_symbol = BLOCK_COMMENT;
                return true;
            }
            return false;
        } else {
            // Line comment
            if (valid[LINE_COMMENT]) {
                while (lexer->lookahead != '\n' && lexer->lookahead != 0) {
                    advance(lexer);
                }
                lexer->mark_end(lexer);
                lexer->result_symbol = LINE_COMMENT;
                return true;
            }
            return false;
        }
    }

    // String start
    if (c == '"' && valid[BEGIN_STR]) {
        advance(lexer);
        scanner->in_string = true;
        lexer->result_symbol = BEGIN_STR;
        return true;
    }

    // End interpolation (backtick outside string)
    if (c == '`' && valid[END_INTERPOLATION]) {
        advance(lexer);
        // Pop FRAME_INTERPOLATION
        if (top_frame(scanner).kind == FRAME_INTERPOLATION) {
            pop_frame(scanner);
        }
        scanner->in_string = true;
        lexer->result_symbol = END_INTERPOLATION;
        return true;
    }

    // Backslash-lparen
    if (c == '\\') {
        advance(lexer);
        if (lexer->lookahead == '(' && valid[BACKSLASH_LPAREN]) {
            advance(lexer);
            push_frame(scanner, FRAME_PAREN, 0);
            lexer->result_symbol = BACKSLASH_LPAREN;
            return true;
        }
        // Just a backslash - currently not used in grammar
        return false;
    }

    // Parentheses
    if (c == '(' && valid[LPAREN]) {
        advance(lexer);
        push_frame(scanner, FRAME_PAREN, 0);
        lexer->result_symbol = LPAREN;
        return true;
    }

    if (c == ')' && valid[RPAREN]) {
        advance(lexer);
        // Pop frame(s)
        if (top_frame(scanner).kind == FRAME_PAREN) {
            pop_frame(scanner);
        }
        lexer->result_symbol = RPAREN;
        return true;
    }

    // Brackets
    if (c == '[' && valid[LBRACKET]) {
        advance(lexer);
        push_frame(scanner, FRAME_BRACKET, 0);
        lexer->result_symbol = LBRACKET;
        return true;
    }

    if (c == ']' && valid[RBRACKET]) {
        advance(lexer);
        if (top_frame(scanner).kind == FRAME_BRACKET) {
            pop_frame(scanner);
        }
        lexer->result_symbol = RBRACKET;
        return true;
    }

    // Braces
    if (c == '{' && valid[LBRACE]) {
        advance(lexer);
        push_frame(scanner, FRAME_INDENTED, 0);
        lexer->result_symbol = LBRACE;
        return true;
    }

    if (c == '}' && valid[RBRACE]) {
        advance(lexer);
        if (top_frame(scanner).kind == FRAME_INDENTED && scanner->depth > 1) {
            pop_frame(scanner);
        }
        lexer->result_symbol = RBRACE;
        return true;
    }

    // Single quote: could be label or char literal
    if (c == '\'') {
        // Peek: if followed by lowercase letter, could be label
        advance(lexer); // consume '

        if (is_lower(lexer->lookahead) && valid[LABEL]) {
            // Could be label: 'identifier
            // But also could be char literal: 'a'
            // Labels don't have a closing quote, chars do.
            // Scan the identifier part, then check for closing quote.
            char buf[64];
            int len = 0;
            buf[len++] = (char)lexer->lookahead;
            advance(lexer);
            while (is_id_char(lexer->lookahead) && len < 63) {
                buf[len++] = (char)lexer->lookahead;
                advance(lexer);
            }

            if (lexer->lookahead == '\'') {
                // Char literal with a lowercase char: 'a'
                // But only if len == 1 (single char)
                if (len == 1 && valid[CHAR_LITERAL]) {
                    advance(lexer); // consume closing '
                    lexer->result_symbol = CHAR_LITERAL;
                    return true;
                }
                // Multi-char like 'ab' - not valid, treat as label
            }

            // It's a label
            lexer->mark_end(lexer);
            lexer->result_symbol = LABEL;
            return true;
        }

        if (valid[CHAR_LITERAL]) {
            // Char literal: 'c' or '\n' etc.
            if (lexer->lookahead == '\\') {
                advance(lexer); // consume backslash
                advance(lexer); // consume escape char
            } else if (lexer->lookahead != '\'' && lexer->lookahead != 0) {
                advance(lexer); // consume the character
            }
            if (lexer->lookahead == '\'') {
                advance(lexer); // consume closing '
                lexer->result_symbol = CHAR_LITERAL;
                return true;
            }
        }

        return false;
    }

    // Identifiers
    if (c == '_' || is_upper(c)) {
        // Could be upper_id, underscore, or lower_id starting with _
        if (c == '_') {
            // Check what follows: if just underscores or nothing after, it's UNDERSCORE
            // If followed by uppercase, it's upper_id
            // If followed by lowercase, it's lower_id
            // Peek ahead
            advance(lexer); // consume first _
            lexer->mark_end(lexer); // mark after first _ for UNDERSCORE case

            // Count underscores
            while (lexer->lookahead == '_') advance(lexer);

            if (is_upper(lexer->lookahead)) {
                // Upper id: _*[A-Z]...
                advance(lexer);
                while (is_id_char(lexer->lookahead)) advance(lexer);
                lexer->mark_end(lexer);

                // Check for "Fn" keyword
                // (unlikely with leading underscore, but consistent)
                lexer->result_symbol = UPPER_ID;
                return true;
            }

            if (is_lower(lexer->lookahead)) {
                // Lower id: _*[a-z]...
                advance(lexer);
                while (is_id_char(lexer->lookahead)) advance(lexer);
                lexer->mark_end(lexer);
                lexer->result_symbol = LOWER_ID;
                return true;
            }

            // Just underscore(s) — return UNDERSCORE
            if (valid[UNDERSCORE]) {
                lexer->result_symbol = UNDERSCORE;
                return true;
            }
            return false;
        }

        // Starts with uppercase letter
        enum TokenType tt = scan_upper_id(lexer, valid);
        if (tt != TOKEN_COUNT && valid[tt]) {
            lexer->result_symbol = tt;
            return true;
        }
        return false;
    }

    if (is_lower(c)) {
        enum TokenType tt = scan_lower_id_or_keyword(lexer, scanner, valid);
        if (tt != TOKEN_COUNT && valid[tt]) {
            lexer->result_symbol = tt;
            return true;
        }
        return false;
    }

    // Digits
    if (is_digit(c) && valid[INT_LITERAL]) {
        if (scan_int_literal(lexer)) {
            lexer->mark_end(lexer);
            lexer->result_symbol = INT_LITERAL;
            return true;
        }
        return false;
    }

    // Operators and punctuation
    // Handle multi-character operators first (longest match)

    if (c == '=') {
        advance(lexer);
        if (lexer->lookahead == '=' && valid[EQEQ]) {
            advance(lexer);
            lexer->result_symbol = EQEQ;
            return true;
        }
        if (valid[EQ]) {
            lexer->result_symbol = EQ;
            return true;
        }
        return false;
    }

    if (c == '!') {
        advance(lexer);
        if (lexer->lookahead == '=' && valid[NEQ]) {
            advance(lexer);
            lexer->result_symbol = NEQ;
            return true;
        }
        if (valid[EXCLAMATION]) {
            lexer->result_symbol = EXCLAMATION;
            return true;
        }
        return false;
    }

    if (c == '<') {
        advance(lexer);
        if (lexer->lookahead == '<' && valid[LSHIFT]) {
            advance(lexer);
            lexer->result_symbol = LSHIFT;
            return true;
        }
        if (lexer->lookahead == '=' && valid[LTEQ]) {
            advance(lexer);
            lexer->result_symbol = LTEQ;
            return true;
        }
        if (valid[LT]) {
            lexer->result_symbol = LT;
            return true;
        }
        return false;
    }

    if (c == '>') {
        advance(lexer);
        if (lexer->lookahead == '>' && valid[RSHIFT]) {
            advance(lexer);
            lexer->result_symbol = RSHIFT;
            return true;
        }
        if (lexer->lookahead == '=' && valid[GTEQ]) {
            advance(lexer);
            lexer->result_symbol = GTEQ;
            return true;
        }
        if (valid[GT]) {
            lexer->result_symbol = GT;
            return true;
        }
        return false;
    }

    if (c == '+') {
        advance(lexer);
        if (lexer->lookahead == '=' && valid[PLUSEQ]) {
            advance(lexer);
            lexer->result_symbol = PLUSEQ;
            return true;
        }
        if (valid[PLUS]) {
            lexer->result_symbol = PLUS;
            return true;
        }
        return false;
    }

    if (c == '-') {
        advance(lexer);
        if (lexer->lookahead == '=' && valid[MINUSEQ]) {
            advance(lexer);
            lexer->result_symbol = MINUSEQ;
            return true;
        }
        if (valid[MINUS]) {
            lexer->result_symbol = MINUS;
            return true;
        }
        return false;
    }

    if (c == '*') {
        advance(lexer);
        if (lexer->lookahead == '=' && valid[STAREQ]) {
            advance(lexer);
            lexer->result_symbol = STAREQ;
            return true;
        }
        if (valid[STAR]) {
            lexer->result_symbol = STAR;
            return true;
        }
        return false;
    }

    if (c == '^') {
        advance(lexer);
        if (lexer->lookahead == '=' && valid[CARETEQ]) {
            advance(lexer);
            lexer->result_symbol = CARETEQ;
            return true;
        }
        if (valid[CARET]) {
            lexer->result_symbol = CARET;
            return true;
        }
        return false;
    }

    if (c == '&') {
        advance(lexer);
        if (lexer->lookahead == '&' && valid[AMPAMP]) {
            advance(lexer);
            lexer->result_symbol = AMPAMP;
            return true;
        }
        if (valid[AMP]) {
            lexer->result_symbol = AMP;
            return true;
        }
        return false;
    }

    if (c == '.') {
        advance(lexer);
        if (lexer->lookahead == '.' && valid[DOTDOT]) {
            advance(lexer);
            lexer->result_symbol = DOTDOT;
            return true;
        }
        if (valid[DOT]) {
            lexer->result_symbol = DOT;
            return true;
        }
        return false;
    }

    // Simple single-character tokens
    if (c == '|' && valid[PIPE]) { advance(lexer); lexer->result_symbol = PIPE; return true; }
    if (c == '~' && valid[TILDE]) { advance(lexer); lexer->result_symbol = TILDE; return true; }
    if (c == '/' && valid[SLASH]) { advance(lexer); lexer->result_symbol = SLASH; return true; }
    if (c == '%' && valid[PERCENT]) { advance(lexer); lexer->result_symbol = PERCENT; return true; }
    if (c == ':' && valid[COLON]) { advance(lexer); lexer->result_symbol = COLON; return true; }
    if (c == ',' && valid[COMMA]) { advance(lexer); lexer->result_symbol = COMMA; return true; }
    if (c == ';' && valid[SEMICOLON]) { advance(lexer); lexer->result_symbol = SEMICOLON; return true; }

    return false;
}

// ==================== Tree-sitter API ====================

void *tree_sitter_fir_external_scanner_create(void) {
    Scanner *scanner = calloc(1, sizeof(Scanner));
    scanner->depth = 1;
    scanner->stack[0].kind = FRAME_INDENTED;
    scanner->stack[0].block_col = 0;
    return scanner;
}

void tree_sitter_fir_external_scanner_destroy(void *payload) {
    free(payload);
}

unsigned tree_sitter_fir_external_scanner_serialize(void *payload, char *buffer) {
    Scanner *scanner = (Scanner *)payload;
    unsigned pos = 0;

    buffer[pos++] = (char)scanner->depth;
    buffer[pos++] = (char)scanner->pending_end_blocks;
    buffer[pos++] = (char)scanner->in_string;
    buffer[pos++] = (char)scanner->eof_newline_emitted;

    for (uint8_t i = 0; i < scanner->depth && pos + 3 <= TREE_SITTER_SERIALIZATION_BUFFER_SIZE; i++) {
        buffer[pos++] = (char)scanner->stack[i].kind;
        buffer[pos++] = (char)(scanner->stack[i].block_col & 0xFF);
        buffer[pos++] = (char)((scanner->stack[i].block_col >> 8) & 0xFF);
    }

    return pos;
}

void tree_sitter_fir_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
    Scanner *scanner = (Scanner *)payload;

    if (length == 0) {
        scanner->depth = 1;
        scanner->stack[0].kind = FRAME_INDENTED;
        scanner->stack[0].block_col = 0;
        scanner->pending_end_blocks = 0;
        scanner->in_string = false;
        scanner->eof_newline_emitted = false;
        return;
    }

    unsigned pos = 0;
    scanner->depth = (uint8_t)buffer[pos++];
    scanner->pending_end_blocks = (uint8_t)buffer[pos++];
    scanner->in_string = (bool)buffer[pos++];
    scanner->eof_newline_emitted = (bool)buffer[pos++];

    for (uint8_t i = 0; i < scanner->depth && pos + 3 <= length; i++) {
        scanner->stack[i].kind = (FrameKind)buffer[pos++];
        scanner->stack[i].block_col = (uint16_t)((unsigned char)buffer[pos] | ((unsigned char)buffer[pos + 1] << 8));
        pos += 2;
    }
}

bool tree_sitter_fir_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
    Scanner *scanner = (Scanner *)payload;
    return scan(scanner, lexer, valid_symbols);
}
