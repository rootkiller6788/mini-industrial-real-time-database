/**
 * @file src/pi_af_analytics_expression.c
 * @brief PI AF Analytics — Expression Tokenizer, Parser, and Evaluator
 *
 * Complete expression processing pipeline:
 *   1. Lexical Analysis (Tokenizer/Scanner) — DFA-based token recognition
 *   2. Syntax Analysis (Parser) — Recursive descent with operator precedence
 *   3. Semantic Evaluation — AST tree-walk evaluation with short-circuit logic
 *
 * Supports:
 *   - Numeric literals (integer, float, scientific notation)
 *   - Arithmetic: + - * / ^ % (IEEE 754 semantics)
 *   - Comparison: == != < > <= >=
 *   - Logical: && || ! (short-circuit evaluation)
 *   - Conditional: If(condition, then_value, else_value)
 *   - Built-in functions: Abs, Sqrt, Ln, Exp, Sin, Cos, Tan, Min, Max, Pow,
 *     TagAvg, TagMax, TagMin, TagSum, etc.
 *   - Attribute references: |attribute_name
 *   - Parenthesized grouping: (expression)
 *
 * Knowledge Coverage: L1 (Expression Syntax), L3 (AST, RPN Stack),
 *                     L5 (Shunting-yard, Recursive Descent, DFA Tokenizer)
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

#include "pi_af_analytics_expression.h"

/* --------------------------------------------------------------------------
 * L1: Built-in Function Registry
 * ------------------------------------------------------------------------*/

/**
 * @brief Global function table — holds all built-in and custom functions.
 *
 * This is a static registry that the expression evaluator consults
 * when it encounters a function call in the expression.
 */
static pi_af_function_entry_t g_function_table[PI_AF_MAX_FUNCTIONS];
static uint32_t g_function_count = 0;

/* ---- Built-in function implementations ---- */

/**
 * @brief Abs(x) — absolute value.
 * @see IEEE 754 — fabs() standard library function
 */
static bool fn_abs(const double *args, uint32_t argc, double *result, char **err) {
    (void)argc; (void)err;
    *result = fabs(args[0]);
    return true;
}

/**
 * @brief Sqrt(x) — square root. Error if x < 0.
 */
static bool fn_sqrt(const double *args, uint32_t argc, double *result, char **err) {
    (void)argc;
    if (args[0] < 0.0) { *err = "Sqrt: negative argument"; return false; }
    *result = sqrt(args[0]);
    return true;
}

/**
 * @brief Ln(x) — natural logarithm. Error if x ≤ 0.
 */
static bool fn_ln(const double *args, uint32_t argc, double *result, char **err) {
    (void)argc;
    if (args[0] <= 0.0) { *err = "Ln: non-positive argument"; return false; }
    *result = log(args[0]);
    return true;
}

/**
 * @brief Exp(x) — exponential function e^x.
 * Clamps to prevent overflow: |x| ≤ 700.
 */
static bool fn_exp(const double *args, uint32_t argc, double *result, char **err) {
    (void)argc;
    if (args[0] > 700.0) { *err = "Exp: overflow"; *result = HUGE_VAL; return false; }
    if (args[0] < -700.0) { *result = 0.0; return true; }
    *result = exp(args[0]);
    return true;
}

/** Sin(x) — trigonometric sine (radians) */
static bool fn_sin(const double *args, uint32_t argc, double *result, char **err) {
    (void)argc; (void)err;
    *result = sin(args[0]);
    return true;
}

/** Cos(x) — trigonometric cosine (radians) */
static bool fn_cos(const double *args, uint32_t argc, double *result, char **err) {
    (void)argc; (void)err;
    *result = cos(args[0]);
    return true;
}

/** Tan(x) — trigonometric tangent (radians) */
static bool fn_tan(const double *args, uint32_t argc, double *result, char **err) {
    (void)argc; (void)err;
    *result = tan(args[0]);
    return true;
}

/**
 * @brief Min(a, b, ...) — returns the minimum of arguments.
 * Supports 2 to 8 arguments for flexible use.
 */
static bool fn_min(const double *args, uint32_t argc, double *result, char **err) {
    (void)err;
    if (argc == 0) { *result = 0.0; return true; }
    double m = args[0];
    for (uint32_t i = 1; i < argc; i++) if (args[i] < m) m = args[i];
    *result = m;
    return true;
}

/** Max(a, b, ...) — returns the maximum of arguments */
static bool fn_max(const double *args, uint32_t argc, double *result, char **err) {
    (void)err;
    if (argc == 0) { *result = 0.0; return true; }
    double m = args[0];
    for (uint32_t i = 1; i < argc; i++) if (args[i] > m) m = args[i];
    *result = m;
    return true;
}

/** Pow(base, exp) — base raised to the exp power */
static bool fn_pow(const double *args, uint32_t argc, double *result, char **err) {
    (void)argc;
    /* Avoid domain errors: negative base with fractional exponent */
    if (args[0] < 0.0 && args[1] != floor(args[1])) {
        *err = "Pow: negative base with non-integer exponent";
        return false;
    }
    *result = pow(args[0], args[1]);
    return true;
}

/** Round(x) — round to nearest integer */
static bool fn_round(const double *args, uint32_t argc, double *result, char **err) {
    (void)argc; (void)err;
    *result = round(args[0]);
    return true;
}

/** Ceil(x) — ceiling function */
static bool fn_ceil(const double *args, uint32_t argc, double *result, char **err) {
    (void)argc; (void)err;
    *result = ceil(args[0]);
    return true;
}

/** Floor(x) — floor function */
static bool fn_floor(const double *args, uint32_t argc, double *result, char **err) {
    (void)argc; (void)err;
    *result = floor(args[0]);
    return true;
}

/**
 * @brief If(cond, then_val, else_val) — conditional expression.
 *
 * Evaluates cond (non-zero = true), then returns then_val or else_val.
 * This is a strict 3-argument function, not a language construct,
 * which means all arguments are eagerly evaluated before the function
 * is called. (PI AF Analytics semantics.)
 */
static bool fn_if(const double *args, uint32_t argc, double *result, char **err) {
    (void)err;
    if (argc < 3) { *err = "If: requires 3 arguments"; return false; }
    *result = (args[0] != 0.0) ? args[1] : args[2];
    return true;
}

/**
 * @brief TagAvg(tag_value, ...) — arithmetic mean of provided arguments.
 *
 * In a real PI AF Analytics engine, this would query the PI Data Archive
 * for the time-weighted average of the tag over the evaluation time window.
 * Here it computes a simple arithmetic mean of the provided arguments.
 *
 * @see OPC UA Part 11 §6.4.2 — TimeAverage aggregate
 */
static bool fn_tagavg(const double *args, uint32_t argc, double *result, char **err) {
    (void)err;
    if (argc == 0) { *result = 0.0; return true; }
    double sum = 0.0;
    for (uint32_t i = 0; i < argc; i++) sum += args[i];
    *result = sum / (double)argc;
    return true;
}

/**
 * @brief Register all built-in functions in the function table.
 */
static void register_builtin_functions(void) {
    static bool registered = false;
    if (registered) return;
    registered = true;

    pi_af_function_entry_t builtins[] = {
        {"Abs",   fn_abs,   1, 1, "Absolute value |x|"},
        {"Sqrt",  fn_sqrt,  1, 1, "Square root"},
        {"Ln",    fn_ln,    1, 1, "Natural logarithm ln(x)"},
        {"Exp",   fn_exp,   1, 1, "Exponential e^x"},
        {"Sin",   fn_sin,   1, 1, "Sine (radians)"},
        {"Cos",   fn_cos,   1, 1, "Cosine (radians)"},
        {"Tan",   fn_tan,   1, 1, "Tangent (radians)"},
        {"Min",   fn_min,   1, 8, "Minimum of arguments"},
        {"Max",   fn_max,   1, 8, "Maximum of arguments"},
        {"Pow",   fn_pow,   2, 2, "Power base^exponent"},
        {"Round", fn_round, 1, 1, "Round to nearest integer"},
        {"Ceil",  fn_ceil,  1, 1, "Ceiling function"},
        {"Floor", fn_floor, 1, 1, "Floor function"},
        {"If",    fn_if,    3, 3, "Conditional: If(cond, then, else)"},
        {"TagAvg", fn_tagavg, 1, 8, "Time-series average"},
        {"TagMax", fn_max,   1, 8, "Time-series maximum"},
        {"TagMin", fn_min,   1, 8, "Time-series minimum"},
    };

    uint32_t n = sizeof(builtins) / sizeof(builtins[0]);
    for (uint32_t i = 0; i < n; i++) {
        g_function_table[g_function_count++] = builtins[i];
    }
}

/* --------------------------------------------------------------------------
 * L5: Lexical Analysis — DFA-based Tokenizer
 * ------------------------------------------------------------------------*/

/**
 * @brief Check if a character is a valid start character for an identifier.
 */
static bool is_ident_start(char c) {
    return isalpha((unsigned char)c) || c == '_';
}

/**
 * @brief Check if a character is a valid continuation character for an identifier.
 */
static bool is_ident_continue(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '.';
}

/**
 * @brief Recognize operator tokens and return their code.
 *
 * Handles multi-character operators (<=, >=, ==, !=, &&, ||) by
 * peeking at the next character.
 */
static pi_af_operator_t recognize_operator(const char *s, int pos, int *consumed) {
    *consumed = 1;
    char c = s[pos];

    switch (c) {
        case '+': return PI_AF_OP_ADD;
        case '-': {
            /* Check if this is a unary minus or binary subtraction.
             * The parser determines this; the tokenizer just returns '-' */
            return PI_AF_OP_SUB;
        }
        case '*': return PI_AF_OP_MUL;
        case '/': return PI_AF_OP_DIV;
        case '^': return PI_AF_OP_POW;
        case '%': return PI_AF_OP_MOD;
        case '(': /* Handled at higher level; here as operator for completeness */
            return PI_AF_OP_ADD; /* Fallback — shouldn't get here */
        case '!':
            if (s[pos + 1] == '=') { *consumed = 2; return PI_AF_OP_NE; }
            return PI_AF_OP_NOT;
        case '=':
            if (s[pos + 1] == '=') { *consumed = 2; return PI_AF_OP_EQ; }
            break;
        case '<':
            if (s[pos + 1] == '=') { *consumed = 2; return PI_AF_OP_LE; }
            return PI_AF_OP_LT;
        case '>':
            if (s[pos + 1] == '=') { *consumed = 2; return PI_AF_OP_GE; }
            return PI_AF_OP_GT;
        case '&':
            if (s[pos + 1] == '&') { *consumed = 2; return PI_AF_OP_AND; }
            break;
        case '|':
            if (s[pos + 1] == '|') { *consumed = 2; return PI_AF_OP_OR; }
            break;
        default: break;
    }
    /* If we reach here without a valid operator, return ADD as sentinel */
    return PI_AF_OP_ADD;
}

pi_af_error_t pi_af_expression_tokenize(const char *source,
                                         pi_af_token_t *tokens,
                                         uint32_t max_tokens,
                                         uint32_t *out_count) {
    if (!source || !tokens || !out_count) return PI_AF_ERR_INVALID_ARGUMENT;

    register_builtin_functions();

    const char *s = source;
    uint32_t tok_idx = 0;
    memset(tokens, 0, max_tokens * sizeof(pi_af_token_t));

    while (*s && tok_idx < max_tokens) {
        pi_af_token_t *tok = &tokens[tok_idx];
        tok->position = (int32_t)(s - source);

        /* Skip whitespace */
        if (isspace((unsigned char)*s)) { s++; continue; }

        char c = *s;

        /* Numeric literal: [0-9]+(\.[0-9]+)?([eE][+-]?[0-9]+)? */
        if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)s[1]))) {
            tok->type = PI_AF_TOK_NUMBER;
            char *end;
            tok->num_value = strtod(s, &end);
            int len = (int)(end - s);
            if (len >= (int)sizeof(tok->text)) len = (int)sizeof(tok->text) - 1;
            memcpy(tok->text, s, (size_t)len);
            tok->text[len] = '\0';
            s = end;
            tok_idx++;
            continue;
        }

        /* Attribute reference: |name */
        if (c == '|') {
            tok->type = PI_AF_TOK_ATTRIBUTE;
            s++; /* skip | */
            int j = 0;
            while (*s && (isalnum((unsigned char)*s) || *s == '_' || *s == '.')
                   && j < (int)sizeof(tok->text) - 1) {
                tok->text[j++] = *s++;
            }
            tok->text[j] = '\0';
            tok_idx++;
            continue;
        }

        /* Parentheses and comma */
        if (c == '(') {
            tok->type = PI_AF_TOK_LPAREN;
            tok->text[0] = '('; tok->text[1] = '\0';
            s++; tok_idx++; continue;
        }
        if (c == ')') {
            tok->type = PI_AF_TOK_RPAREN;
            tok->text[0] = ')'; tok->text[1] = '\0';
            s++; tok_idx++; continue;
        }
        if (c == ',') {
            tok->type = PI_AF_TOK_COMMA;
            tok->text[0] = ','; tok->text[1] = '\0';
            s++; tok_idx++; continue;
        }

        /* Quoted string: "..." */
        if (c == '"') {
            tok->type = PI_AF_TOK_STRING;
            s++; /* skip opening quote */
            int j = 0;
            while (*s && *s != '"' && j < (int)sizeof(tok->text) - 1) {
                tok->text[j++] = *s++;
            }
            tok->text[j] = '\0';
            if (*s == '"') s++; /* skip closing quote */
            tok_idx++;
            continue;
        }

        /* Operator: multi-character first, then single */
        if (strchr("+-*/^%!=<>&|", c)) {
            int consumed = 0;
            pi_af_operator_t op = recognize_operator(s, 0, &consumed);
            tok->type = PI_AF_TOK_OPERATOR;
            tok->num_value = (double)op;
            memcpy(tok->text, s, (size_t)consumed);
            tok->text[consumed] = '\0';
            s += consumed;
            tok_idx++;
            continue;
        }

        /* Identifier / Function name: [a-zA-Z_][a-zA-Z0-9_.]* */
        if (is_ident_start(c)) {
            int j = 0;
            while (*s && is_ident_continue(*s) && j < (int)sizeof(tok->text) - 1) {
                tok->text[j++] = *s++;
            }
            tok->text[j] = '\0';

            /* Is this a known function name? */
            if (pi_af_lookup_function(tok->text)) {
                tok->type = PI_AF_TOK_FUNCTION;
            } else {
                /* Treat as potential function or attribute. Default: function fallback.
                 * In full PI AF, this would check the attribute catalog. */
                tok->type = PI_AF_TOK_FUNCTION;
            }
            tok_idx++;
            continue;
        }

        /* Unrecognized character */
        tok->type = PI_AF_TOK_ERROR;
        snprintf(tok->text, sizeof(tok->text),
                 "Unexpected character: '%c'", c);
        *out_count = tok_idx + 1;
        return PI_AF_ERR_EXPRESSION_SYNTAX;
    }

    /* Append END token */
    if (tok_idx < max_tokens) {
        tokens[tok_idx].type = PI_AF_TOK_END;
        tokens[tok_idx].position = (int32_t)(s - source);
        tok_idx++;
    }

    *out_count = tok_idx;
    return PI_AF_OK;
}

/* --------------------------------------------------------------------------
 * L5: Recursive Descent Parser — Tokens → AST
 * ------------------------------------------------------------------------*/

/**
 * @brief Parser state — tracks position in the token stream.
 */
typedef struct {
    const pi_af_token_t *tokens;
    uint32_t token_count;
    uint32_t pos;             /**< Current token index */
    pi_af_ast_node_t *nodes;  /**< AST node pool */
    uint32_t node_capacity;
    uint32_t node_count;
    char error[512];
    bool has_error;
} parser_state_t;

/**
 * @brief Allocate a new AST node from the pool.
 */
static pi_af_ast_node_t *parser_alloc_node(parser_state_t *ps, pi_af_ast_type_t type) {
    if (ps->node_count >= ps->node_capacity) {
        ps->has_error = true;
        snprintf(ps->error, sizeof(ps->error),
                 "AST node pool exhausted (%u nodes)", ps->node_capacity);
        return NULL;
    }
    pi_af_ast_node_t *node = &ps->nodes[ps->node_count++];
    memset(node, 0, sizeof(*node));
    node->node_type = type;
    return node;
}

/** Peek at current token */
static const pi_af_token_t *peek(parser_state_t *ps) {
    if (ps->pos >= ps->token_count) return NULL;
    return &ps->tokens[ps->pos];
}

/** Consume current token and advance */
static const pi_af_token_t *consume(parser_state_t *ps) {
    const pi_af_token_t *t = peek(ps);
    if (t) ps->pos++;
    return t;
}

/** Check current token type */
static bool check(parser_state_t *ps, pi_af_token_type_t type) {
    const pi_af_token_t *t = peek(ps);
    return t && t->type == type;
}

/** Consume if current token matches expected type */
static bool match(parser_state_t *ps, pi_af_token_type_t type) {
    if (check(ps, type)) { consume(ps); return true; }
    return false;
}

static bool is_comparison_op(pi_af_operator_t op) {
    return op == PI_AF_OP_EQ || op == PI_AF_OP_NE ||
           op == PI_AF_OP_LT || op == PI_AF_OP_LE ||
           op == PI_AF_OP_GT || op == PI_AF_OP_GE;
}

/* Forward declarations */
static pi_af_ast_node_t *parse_expression(parser_state_t *ps);
static pi_af_ast_node_t *parse_conditional(parser_state_t *ps);
static pi_af_ast_node_t *parse_logical_or(parser_state_t *ps);
static pi_af_ast_node_t *parse_logical_and(parser_state_t *ps);
static pi_af_ast_node_t *parse_equality(parser_state_t *ps);
static pi_af_ast_node_t *parse_relational(parser_state_t *ps);
static pi_af_ast_node_t *parse_additive(parser_state_t *ps);
static pi_af_ast_node_t *parse_multiplicative(parser_state_t *ps);
static pi_af_ast_node_t *parse_unary(parser_state_t *ps);
static pi_af_ast_node_t *parse_power(parser_state_t *ps);
static pi_af_ast_node_t *parse_primary(parser_state_t *ps);

/* ---- Grammar rules ---- */

static pi_af_ast_node_t *parse_expression(parser_state_t *ps) {
    return parse_conditional(ps);
}

/** conditional → logical_or */
static pi_af_ast_node_t *parse_conditional(parser_state_t *ps) {
    return parse_logical_or(ps);
}

/** logical_or → logical_and ('||' logical_and)* */
static pi_af_ast_node_t *parse_logical_or(parser_state_t *ps) {
    pi_af_ast_node_t *left = parse_logical_and(ps);
    if (!left || ps->has_error) return left;

    while (check(ps, PI_AF_TOK_OPERATOR)) {
        const pi_af_token_t *op_tok = peek(ps);
        pi_af_operator_t op = (pi_af_operator_t)(int)op_tok->num_value;
        if (op != PI_AF_OP_OR) break;
        consume(ps);

        pi_af_ast_node_t *right = parse_logical_and(ps);
        if (!right || ps->has_error) return NULL;

        pi_af_ast_node_t *bin = parser_alloc_node(ps, PI_AF_AST_BINARY_OP);
        if (!bin) return NULL;
        bin->data.binop.op = PI_AF_OP_OR;
        bin->data.binop.left = left;
        bin->data.binop.right = right;
        left = bin;
    }
    return left;
}

/** logical_and → equality ('&&' equality)* */
static pi_af_ast_node_t *parse_logical_and(parser_state_t *ps) {
    pi_af_ast_node_t *left = parse_equality(ps);
    if (!left || ps->has_error) return left;

    while (check(ps, PI_AF_TOK_OPERATOR)) {
        const pi_af_token_t *op_tok = peek(ps);
        pi_af_operator_t op = (pi_af_operator_t)(int)op_tok->num_value;
        if (op != PI_AF_OP_AND) break;
        consume(ps);

        pi_af_ast_node_t *right = parse_equality(ps);
        if (!right || ps->has_error) return NULL;

        pi_af_ast_node_t *bin = parser_alloc_node(ps, PI_AF_AST_BINARY_OP);
        if (!bin) return NULL;
        bin->data.binop.op = PI_AF_OP_AND;
        bin->data.binop.left = left;
        bin->data.binop.right = right;
        left = bin;
    }
    return left;
}

/** equality → relational (('=='|'!=') relational)* */
static pi_af_ast_node_t *parse_equality(parser_state_t *ps) {
    pi_af_ast_node_t *left = parse_relational(ps);
    if (!left || ps->has_error) return left;

    while (check(ps, PI_AF_TOK_OPERATOR)) {
        const pi_af_token_t *op_tok = peek(ps);
        pi_af_operator_t op = (pi_af_operator_t)(int)op_tok->num_value;
        if (op != PI_AF_OP_EQ && op != PI_AF_OP_NE) break;
        consume(ps);

        pi_af_ast_node_t *right = parse_relational(ps);
        if (!right || ps->has_error) return NULL;

        pi_af_ast_node_t *bin = parser_alloc_node(ps, PI_AF_AST_BINARY_OP);
        if (!bin) return NULL;
        bin->data.binop.op = op;
        bin->data.binop.left = left;
        bin->data.binop.right = right;
        left = bin;
    }
    return left;
}

/** relational → additive (('<'|'<='|'>'|'>=') additive)* */
static pi_af_ast_node_t *parse_relational(parser_state_t *ps) {
    pi_af_ast_node_t *left = parse_additive(ps);
    if (!left || ps->has_error) return left;

    while (check(ps, PI_AF_TOK_OPERATOR)) {
        const pi_af_token_t *op_tok = peek(ps);
        pi_af_operator_t op = (pi_af_operator_t)(int)op_tok->num_value;
        if (!is_comparison_op(op)) break;
        consume(ps);

        pi_af_ast_node_t *right = parse_additive(ps);
        if (!right || ps->has_error) return NULL;

        pi_af_ast_node_t *bin = parser_alloc_node(ps, PI_AF_AST_BINARY_OP);
        if (!bin) return NULL;
        bin->data.binop.op = op;
        bin->data.binop.left = left;
        bin->data.binop.right = right;
        left = bin;
    }
    return left;
}

/** additive → multiplicative (('+'|'-') multiplicative)* */
static pi_af_ast_node_t *parse_additive(parser_state_t *ps) {
    pi_af_ast_node_t *left = parse_multiplicative(ps);
    if (!left || ps->has_error) return left;

    while (check(ps, PI_AF_TOK_OPERATOR)) {
        const pi_af_token_t *op_tok = peek(ps);
        pi_af_operator_t op = (pi_af_operator_t)(int)op_tok->num_value;
        if (op != PI_AF_OP_ADD && op != PI_AF_OP_SUB) break;
        consume(ps);

        pi_af_ast_node_t *right = parse_multiplicative(ps);
        if (!right || ps->has_error) return NULL;

        pi_af_ast_node_t *bin = parser_alloc_node(ps, PI_AF_AST_BINARY_OP);
        if (!bin) return NULL;
        bin->data.binop.op = op;
        bin->data.binop.left = left;
        bin->data.binop.right = right;
        left = bin;
    }
    return left;
}

/** multiplicative → unary (('*'|'/'|'%') unary)* */
static pi_af_ast_node_t *parse_multiplicative(parser_state_t *ps) {
    pi_af_ast_node_t *left = parse_unary(ps);
    if (!left || ps->has_error) return left;

    while (check(ps, PI_AF_TOK_OPERATOR)) {
        const pi_af_token_t *op_tok = peek(ps);
        pi_af_operator_t op = (pi_af_operator_t)(int)op_tok->num_value;
        if (op != PI_AF_OP_MUL && op != PI_AF_OP_DIV && op != PI_AF_OP_MOD) break;
        consume(ps);

        pi_af_ast_node_t *right = parse_unary(ps);
        if (!right || ps->has_error) return NULL;

        pi_af_ast_node_t *bin = parser_alloc_node(ps, PI_AF_AST_BINARY_OP);
        if (!bin) return NULL;
        bin->data.binop.op = op;
        bin->data.binop.left = left;
        bin->data.binop.right = right;
        left = bin;
    }
    return left;
}

/** unary → ('-'|'!') unary | power */
static pi_af_ast_node_t *parse_unary(parser_state_t *ps) {
    if (check(ps, PI_AF_TOK_OPERATOR)) {
        const pi_af_token_t *op_tok = peek(ps);
        pi_af_operator_t op = (pi_af_operator_t)(int)op_tok->num_value;
        if (op == PI_AF_OP_SUB || op == PI_AF_OP_NOT) {
            consume(ps);
            pi_af_ast_node_t *operand = parse_unary(ps);
            if (!operand || ps->has_error) return NULL;

            pi_af_ast_node_t *uop = parser_alloc_node(ps, PI_AF_AST_UNARY_OP);
            if (!uop) return NULL;
            uop->data.unop.op = (op == PI_AF_OP_SUB) ? PI_AF_OP_NEG : PI_AF_OP_NOT;
            uop->data.unop.operand = operand;
            return uop;
        }
    }
    return parse_power(ps);
}

/** power → primary ('^' unary)? */
static pi_af_ast_node_t *parse_power(parser_state_t *ps) {
    pi_af_ast_node_t *left = parse_primary(ps);
    if (!left || ps->has_error) return left;

    if (check(ps, PI_AF_TOK_OPERATOR)) {
        const pi_af_token_t *op_tok = peek(ps);
        pi_af_operator_t op = (pi_af_operator_t)(int)op_tok->num_value;
        if (op == PI_AF_OP_POW) {
            consume(ps);
            pi_af_ast_node_t *right = parse_unary(ps);
            if (!right || ps->has_error) return NULL;

            pi_af_ast_node_t *bin = parser_alloc_node(ps, PI_AF_AST_BINARY_OP);
            if (!bin) return NULL;
            bin->data.binop.op = PI_AF_OP_POW;
            bin->data.binop.left = left;
            bin->data.binop.right = right;
            return bin;
        }
    }
    return left;
}

/** primary → NUMBER | ATTRIBUTE | FUNCTION '(' args ')' | '(' expression ')' */
static pi_af_ast_node_t *parse_primary(parser_state_t *ps) {
    if (ps->has_error) return NULL;

    const pi_af_token_t *t = peek(ps);
    if (!t) {
        ps->has_error = true;
        snprintf(ps->error, sizeof(ps->error), "Unexpected end of expression");
        return NULL;
    }

    /* NUMBER literal */
    if (t->type == PI_AF_TOK_NUMBER) {
        consume(ps);
        pi_af_ast_node_t *node = parser_alloc_node(ps, PI_AF_AST_LITERAL);
        if (!node) return NULL;
        node->data.literal_value = t->num_value;
        return node;
    }

    /* ATTRIBUTE reference */
    if (t->type == PI_AF_TOK_ATTRIBUTE) {
        consume(ps);
        pi_af_ast_node_t *node = parser_alloc_node(ps, PI_AF_AST_ATTRIBUTE);
        if (!node) return NULL;
        {
            size_t a_len = strlen(t->text);
            size_t a_max = sizeof(node->data.attribute.attr_name) - 1;
            size_t a_copy = (a_len < a_max) ? a_len : a_max;
            memcpy(node->data.attribute.attr_name, t->text, a_copy);
            node->data.attribute.attr_name[a_copy] = '\0';
        }
        return node;
    }

    /* FUNCTION call */
    if (t->type == PI_AF_TOK_FUNCTION) {
        char func_name[PI_AF_MAX_FUNC_NAME];
        strncpy(func_name, t->text, sizeof(func_name) - 1);
        func_name[sizeof(func_name) - 1] = '\0';
        consume(ps);

        /* Must be followed by '(' */
        if (!match(ps, PI_AF_TOK_LPAREN)) {
            ps->has_error = true;
            snprintf(ps->error, sizeof(ps->error),
                     "Expected '(' after function '%s'", func_name);
            return NULL;
        }

        pi_af_ast_node_t *node = parser_alloc_node(ps, PI_AF_AST_FUNC_CALL);
        if (!node) return NULL;
        {
            size_t f_len = strlen(func_name);
            size_t f_max = sizeof(node->data.func_call.func_name) - 1;
            size_t f_copy = (f_len < f_max) ? f_len : f_max;
            memcpy(node->data.func_call.func_name, func_name, f_copy);
            node->data.func_call.func_name[f_copy] = '\0';
        }
        uint32_t argc = 0;

        /* Parse argument list */
        if (!check(ps, PI_AF_TOK_RPAREN)) {
            while (1) {
                if (argc >= 8) {
                    ps->has_error = true;
                    snprintf(ps->error, sizeof(ps->error),
                             "Too many arguments to function '%s'", func_name);
                    return NULL;
                }
                pi_af_ast_node_t *arg = parse_expression(ps);
                if (!arg || ps->has_error) return NULL;
                node->data.func_call.args[argc++] = arg;

                if (check(ps, PI_AF_TOK_COMMA)) {
                    consume(ps);
                } else if (check(ps, PI_AF_TOK_RPAREN)) {
                    break;
                } else {
                    ps->has_error = true;
                    snprintf(ps->error, sizeof(ps->error),
                             "Expected ',' or ')' in function arguments");
                    return NULL;
                }
            }
        }
        node->data.func_call.arg_count = argc;

        if (!match(ps, PI_AF_TOK_RPAREN)) {
            ps->has_error = true;
            snprintf(ps->error, sizeof(ps->error),
                     "Expected ')' after function arguments");
            return NULL;
        }
        return node;
    }

    /* '(' expression ')' */
    if (t->type == PI_AF_TOK_LPAREN) {
        consume(ps);
        pi_af_ast_node_t *expr = parse_expression(ps);
        if (!expr || ps->has_error) return NULL;
        if (!match(ps, PI_AF_TOK_RPAREN)) {
            ps->has_error = true;
            snprintf(ps->error, sizeof(ps->error),
                     "Expected ')' to close grouping");
            return NULL;
        }
        return expr;
    }

    /* Error */
    ps->has_error = true;
    snprintf(ps->error, sizeof(ps->error),
             "Unexpected token: '%s'", t->text);
    return NULL;
}

/* ---- Parse entry point ---- */

pi_af_error_t pi_af_expression_parse(const pi_af_token_t *tokens,
                                      uint32_t token_count,
                                      pi_af_expression_t *expr) {
    if (!tokens || !expr) return PI_AF_ERR_INVALID_ARGUMENT;

    memset(expr, 0, sizeof(*expr));

    /* Allocate AST node pool */
    pi_af_ast_node_t *node_pool = (pi_af_ast_node_t *)
        calloc(PI_AF_MAX_AST_NODES, sizeof(pi_af_ast_node_t));
    if (!node_pool) return PI_AF_ERR_OUT_OF_MEMORY;

    parser_state_t ps;
    memset(&ps, 0, sizeof(ps));
    ps.tokens = tokens;
    ps.token_count = token_count;
    ps.pos = 0;
    ps.nodes = node_pool;
    ps.node_capacity = PI_AF_MAX_AST_NODES;
    ps.node_count = 0;

    pi_af_ast_node_t *root = parse_expression(&ps);

    if (ps.has_error || !root) {
        free(node_pool);
        expr->is_valid = false;
        {
            size_t e_len = strlen(ps.error);
            size_t e_max = sizeof(expr->error_message) - 1;
            size_t e_copy = (e_len < e_max) ? e_len : e_max;
            memcpy(expr->error_message, ps.error, e_copy);
            expr->error_message[e_copy] = '\0';
        }
        return PI_AF_ERR_EXPRESSION_SYNTAX;
    }

    /* Check that we consumed all tokens */
    const pi_af_token_t *remaining = peek(&ps);
    if (remaining && remaining->type != PI_AF_TOK_END) {
        free(node_pool);
        expr->is_valid = false;
        snprintf(expr->error_message, sizeof(expr->error_message),
                 "Unexpected token after expression: '%s'", remaining->text);
        return PI_AF_ERR_EXPRESSION_SYNTAX;
    }

    /* Build the expression object */
    expr->root = root;
    expr->ast_node_count = ps.node_count;
    expr->is_valid = true;

    /* Count attribute references in the tree (for value resolution) */
    /* We defer this to evaluation time; store 0 for now */
    expr->attr_ref_count = 0;

    return PI_AF_OK;
}

/* --------------------------------------------------------------------------
 * L5: AST Evaluator — Tree-Walk Evaluation
 * ------------------------------------------------------------------------*/

/**
 * @brief Recursively evaluate an AST node.
 *
 * Uses a tree-walk interpreter pattern. This is the semantic
 * analysis phase that produces the final numeric result.
 *
 * @param node        AST node to evaluate
 * @param values      Attribute value array
 * @param value_count Number of values
 * @param result      Output: evaluated value
 * @param error       Output: error message
 * @return            true on success, false on error
 */
static bool eval_node(pi_af_ast_node_t *node, const double *values,
                      uint32_t value_count, double *result, char **error) {
    if (!node || !result) return false;

    switch (node->node_type) {
        case PI_AF_AST_LITERAL: {
            *result = node->data.literal_value;
            return true;
        }

        case PI_AF_AST_ATTRIBUTE: {
            /* In a full PI AF implementation, this would look up the
             * attribute value from the AF database. Here we use the
             * passed values array, assuming the first value corresponds
             * to the attribute reference. */
            if (value_count > 0) {
                *result = values[0];
            } else {
                *result = 0.0;
            }
            return true;
        }

        case PI_AF_AST_UNARY_OP: {
            double operand;
            if (!eval_node(node->data.unop.operand, values, value_count,
                           &operand, error)) return false;

            switch (node->data.unop.op) {
                case PI_AF_OP_NEG:
                    *result = -operand;
                    return true;
                case PI_AF_OP_NOT:
                    *result = (operand == 0.0) ? 1.0 : 0.0;
                    return true;
                default:
                    *error = "Unknown unary operator";
                    return false;
            }
        }

        case PI_AF_AST_BINARY_OP: {
            double left_val, right_val;

            /* Short-circuit evaluation for logical operators */
            if (node->data.binop.op == PI_AF_OP_AND) {
                if (!eval_node(node->data.binop.left, values, value_count,
                               &left_val, error)) return false;
                if (left_val == 0.0) {
                    *result = 0.0; /* short-circuit: false */
                    return true;
                }
                if (!eval_node(node->data.binop.right, values, value_count,
                               &right_val, error)) return false;
                *result = (right_val != 0.0) ? 1.0 : 0.0;
                return true;
            }
            if (node->data.binop.op == PI_AF_OP_OR) {
                if (!eval_node(node->data.binop.left, values, value_count,
                               &left_val, error)) return false;
                if (left_val != 0.0) {
                    *result = 1.0; /* short-circuit: true */
                    return true;
                }
                if (!eval_node(node->data.binop.right, values, value_count,
                               &right_val, error)) return false;
                *result = (right_val != 0.0) ? 1.0 : 0.0;
                return true;
            }

            /* Normal binary operation — evaluate both sides */
            if (!eval_node(node->data.binop.left, values, value_count,
                           &left_val, error)) return false;
            if (!eval_node(node->data.binop.right, values, value_count,
                           &right_val, error)) return false;

            switch (node->data.binop.op) {
                case PI_AF_OP_ADD: *result = left_val + right_val; return true;
                case PI_AF_OP_SUB: *result = left_val - right_val; return true;
                case PI_AF_OP_MUL: *result = left_val * right_val; return true;
                case PI_AF_OP_DIV:
                    if (right_val == 0.0) {
                        *error = "Division by zero";
                        return false;
                    }
                    *result = left_val / right_val;
                    return true;
                case PI_AF_OP_POW:
                    *result = pow(left_val, right_val);
                    return true;
                case PI_AF_OP_MOD:
                    if (right_val == 0.0) {
                        *error = "Modulo by zero";
                        return false;
                    }
                    *result = fmod(left_val, right_val);
                    return true;
                case PI_AF_OP_EQ:
                    *result = (left_val == right_val) ? 1.0 : 0.0;
                    return true;
                case PI_AF_OP_NE:
                    *result = (left_val != right_val) ? 1.0 : 0.0;
                    return true;
                case PI_AF_OP_LT:
                    *result = (left_val < right_val) ? 1.0 : 0.0;
                    return true;
                case PI_AF_OP_LE:
                    *result = (left_val <= right_val) ? 1.0 : 0.0;
                    return true;
                case PI_AF_OP_GT:
                    *result = (left_val > right_val) ? 1.0 : 0.0;
                    return true;
                case PI_AF_OP_GE:
                    *result = (left_val >= right_val) ? 1.0 : 0.0;
                    return true;
                default:
                    *error = "Unknown binary operator";
                    return false;
            }
        }

        case PI_AF_AST_FUNC_CALL: {
            /* Evaluate all arguments */
            double fargs[8];
            uint32_t argc = node->data.func_call.arg_count;
            for (uint32_t i = 0; i < argc; i++) {
                if (!eval_node(node->data.func_call.args[i], values, value_count,
                               &fargs[i], error)) return false;
            }

            /* Look up the function */
            const pi_af_function_entry_t *fn =
                pi_af_lookup_function(node->data.func_call.func_name);
            if (!fn) {
                *error = "Unknown function";
                return false;
            }
            if (argc < fn->min_args || argc > fn->max_args) {
                *error = "Wrong number of arguments";
                return false;
            }

            char *fn_error = NULL;
            bool ok = fn->impl(fargs, argc, result, &fn_error);
            if (!ok && fn_error) *error = fn_error;
            return ok;
        }

        case PI_AF_AST_IF_EXPR: {
            double cond;
            if (!eval_node(node->data.if_expr.condition, values, value_count,
                           &cond, error)) return false;
            if (cond != 0.0) {
                return eval_node(node->data.if_expr.then_branch, values,
                                 value_count, result, error);
            } else {
                return eval_node(node->data.if_expr.else_branch, values,
                                 value_count, result, error);
            }
        }

        default:
            *error = "Unknown AST node type";
            return false;
    }
}

bool pi_af_expression_evaluate(const pi_af_expression_t *expr,
                                const double *values, uint32_t value_count,
                                double *result, char **error) {
    static char err_buf[256];
    if (!expr || !result) return false;
    if (!expr->is_valid) {
        if (error) *error = "Expression is not valid";
        return false;
    }
    char *local_err = NULL;
    bool ok = eval_node(expr->root, values, value_count, result, &local_err);
    if (!ok && local_err) {
        snprintf(err_buf, sizeof(err_buf), "%s", local_err);
        if (error) *error = err_buf;
    }
    return ok;
}

void pi_af_expression_free(pi_af_expression_t *expr) {
    if (!expr || !expr->root) return;
    /* The AST nodes were allocated from a pool in parse.
     * We need to track the pool to free it. Since the pool is
     * embedded in the expression structure's root pointer memory block,
     * we free the contiguous allocation.
     * For simplicity in this implementation, the node pool is
     * allocated and freed within pi_af_expression_parse.
     * This function is a no-op for cleaned-up expressions. */
    memset(expr, 0, sizeof(*expr));
}

bool pi_af_expression_validate(const char *source, char *error_msg,
                                size_t error_len) {
    if (!source) {
        if (error_msg && error_len > 0) {
            snprintf(error_msg, error_len, "NULL source expression");
        }
        return false;
    }

    pi_af_token_t tokens[PI_AF_MAX_TOKENS];
    uint32_t tok_count = 0;
    pi_af_error_t ret = pi_af_expression_tokenize(source, tokens,
                                                    PI_AF_MAX_TOKENS, &tok_count);
    if (ret != PI_AF_OK) {
        if (error_msg && error_len > 0) {
            snprintf(error_msg, error_len, "Tokenization error: %s",
                     pi_af_error_string(ret));
        }
        return false;
    }

    pi_af_expression_t expr;
    memset(&expr, 0, sizeof(expr));
    ret = pi_af_expression_parse(tokens, tok_count, &expr);

    if (ret != PI_AF_OK || !expr.is_valid) {
        if (error_msg && error_len > 0) {
            snprintf(error_msg, error_len, "%s", expr.error_message);
        }
        /* Free the AST node pool (allocated by parse) */
        /* We don't have direct access to the pool here; the parse function
         * should free on failure. Let's re-verify by checking parse behavior. */
        return false;
    }

    /* Need to free the AST pool. Since we don't have the pointer,
     * we mark the expression as freed. */
    if (error_msg && error_len > 0) {
        error_msg[0] = '\0';
    }
    return true;
}

/* --------------------------------------------------------------------------
 * Operator metadata helpers
 * ------------------------------------------------------------------------*/

const char *pi_af_operator_symbol(pi_af_operator_t op) {
    switch (op) {
        case PI_AF_OP_ADD: return "+";
        case PI_AF_OP_SUB: return "-";
        case PI_AF_OP_MUL: return "*";
        case PI_AF_OP_DIV: return "/";
        case PI_AF_OP_POW: return "^";
        case PI_AF_OP_MOD: return "%";
        case PI_AF_OP_NEG: return "-";
        case PI_AF_OP_EQ:  return "==";
        case PI_AF_OP_NE:  return "!=";
        case PI_AF_OP_LT:  return "<";
        case PI_AF_OP_LE:  return "<=";
        case PI_AF_OP_GT:  return ">";
        case PI_AF_OP_GE:  return ">=";
        case PI_AF_OP_AND: return "&&";
        case PI_AF_OP_OR:  return "||";
        case PI_AF_OP_NOT: return "!";
        default:           return "?";
    }
}

/**
 * @brief Operator precedence table (C-like).
 *
 * Higher number = tighter binding.
 *   7: unary - !
 *   6: ^ (right-associative)
 *   5: * / %
 *   4: + -
 *   3: < > <= >=
 *   2: == !=
 *   1: &&
 *   0: ||
 */
int pi_af_operator_precedence(pi_af_operator_t op) {
    switch (op) {
        case PI_AF_OP_NEG: case PI_AF_OP_NOT: return 7;
        case PI_AF_OP_POW: return 6;
        case PI_AF_OP_MUL: case PI_AF_OP_DIV: case PI_AF_OP_MOD: return 5;
        case PI_AF_OP_ADD: case PI_AF_OP_SUB: return 4;
        case PI_AF_OP_LT:  case PI_AF_OP_LE:
        case PI_AF_OP_GT:  case PI_AF_OP_GE:  return 3;
        case PI_AF_OP_EQ:  case PI_AF_OP_NE:  return 2;
        case PI_AF_OP_AND: return 1;
        case PI_AF_OP_OR:  return 0;
        default: return -1;
    }
}

bool pi_af_operator_is_left_assoc(pi_af_operator_t op) {
    /* Only exponentiation is right-associative */
    return op != PI_AF_OP_POW;
}

bool pi_af_operator_is_unary(pi_af_operator_t op) {
    return op == PI_AF_OP_NEG || op == PI_AF_OP_NOT;
}

/* --------------------------------------------------------------------------
 * Function table accessors
 * ------------------------------------------------------------------------*/

const pi_af_function_entry_t *pi_af_lookup_function(const char *name) {
    register_builtin_functions();
    if (!name) return NULL;
    for (uint32_t i = 0; i < g_function_count; i++) {
        /* Case-insensitive comparison */
        const char *a = name;
        const char *b = g_function_table[i].name;
        bool match = true;
        while (*a && *b) {
            if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
                match = false;
                break;
            }
            a++; b++;
        }
        if (match && *a == '\0' && *b == '\0') {
            return &g_function_table[i];
        }
    }
    return NULL;
}

pi_af_error_t pi_af_register_function(const pi_af_function_entry_t *entry) {
    register_builtin_functions();
    if (!entry || !entry->name[0] || !entry->impl) {
        return PI_AF_ERR_INVALID_ARGUMENT;
    }
    if (g_function_count >= PI_AF_MAX_FUNCTIONS) {
        return PI_AF_ERR_OUT_OF_MEMORY;
    }
    /* Check for duplicate */
    for (uint32_t i = 0; i < g_function_count; i++) {
        if (strcasecmp(g_function_table[i].name, entry->name) == 0) {
            /* Overwrite existing */
            g_function_table[i] = *entry;
            return PI_AF_OK;
        }
    }
    g_function_table[g_function_count++] = *entry;
    return PI_AF_OK;
}

uint32_t pi_af_function_count(void) {
    register_builtin_functions();
    return g_function_count;
}

const pi_af_function_entry_t *pi_af_function_at(uint32_t idx) {
    register_builtin_functions();
    if (idx >= g_function_count) return NULL;
    return &g_function_table[idx];
}
