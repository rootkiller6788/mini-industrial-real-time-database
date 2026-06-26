/**
 * @file include/pi_af_analytics_expression.h
 * @brief PI AF Analytics — Expression Parser, AST Builder & Evaluator
 *
 * Implements a full expression parsing and evaluation engine that mirrors
 * the OSIsoft PI AF Analytics expression syntax. The parser handles:
 *   - Arithmetic: + - * / ^ % (with IEEE 754 compliance)
 *   - Comparison: == != < > <= >=
 *   - Logical: && || ! (short-circuit evaluation)
 *   - Conditional: If(cond, then, else)
 *   - Built-in time-series functions: TagAvg, TagMax, TagMin, TagTotal, etc.
 *   - Attribute referencing: '|attribute_name'
 *   - Numeric literals: 123, 3.14, -2.5e-3
 *
 * The pipeline: Tokenize → Parse (Shunting-yard) → Build AST → Evaluate
 *
 * Knowledge Coverage: L1 (Expression Syntax), L3 (AST, RPN),
 *                     L5 (Shunting-yard, Recursive Descent)
 *
 * Dijkstra, E.W. (1961) "Algol 60 translation" — Shunting-yard algorithm origin
 * Aho, Sethi, Ullman (1986) "Compilers" — Lexical analysis, DFA tokenizer
 * OSIsoft PI AF SDK Documentation — Performance Equation (PE) syntax reference
 *
 * MIT 6.035 — Compiler design: lexical analysis, parsing
 * Stanford CS143 — Formal syntax, operator precedence grammar
 */

#ifndef PI_AF_ANALYTICS_EXPRESSION_H
#define PI_AF_ANALYTICS_EXPRESSION_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "pi_af_analytics_core.h"

/* --------------------------------------------------------------------------
 * L1 — Definitions: Token Types & AST Node Types
 * ------------------------------------------------------------------------*/

/** Maximum tokens in a single expression */
#define PI_AF_MAX_TOKENS      256

/** Maximum AST nodes in a single expression tree */
#define PI_AF_MAX_AST_NODES   256

/** Maximum evaluation stack depth */
#define PI_AF_MAX_EVAL_DEPTH  256

/** Maximum number of user-defined functions */
#define PI_AF_MAX_FUNCTIONS   64

/** Maximum function name length */
#define PI_AF_MAX_FUNC_NAME   64

/**
 * @brief Token types produced by the lexical analyzer.
 *
 * The tokenizer (lexer) converts a character stream into a stream
 * of these tokens for consumption by the parser.
 */
typedef enum {
    PI_AF_TOK_NUMBER        = 0,   /**< Numeric literal: 3.14, -2, 1e-5 */
    PI_AF_TOK_STRING        = 1,   /**< Quoted string: "hello world" */
    PI_AF_TOK_ATTRIBUTE     = 2,   /**< Attribute reference: |tag_name */
    PI_AF_TOK_FUNCTION      = 3,   /**< Function name: TagAvg, If, Abs */
    PI_AF_TOK_OPERATOR      = 4,   /**< Operator: + - * / ^ < > = ! */
    PI_AF_TOK_LPAREN        = 5,   /**< Left parenthesis ( */
    PI_AF_TOK_RPAREN        = 6,   /**< Right parenthesis ) */
    PI_AF_TOK_COMMA         = 7,   /**< Argument separator , */
    PI_AF_TOK_END           = 8,   /**< End of expression marker */
    PI_AF_TOK_ERROR         = 9,   /**< Lexical error */
} pi_af_token_type_t;

/**
 * @brief Operator codes used in the expression evaluator.
 */
typedef enum {
    PI_AF_OP_ADD     = 0,   /**< Addition + */
    PI_AF_OP_SUB     = 1,   /**< Subtraction - */
    PI_AF_OP_MUL     = 2,   /**< Multiplication * */
    PI_AF_OP_DIV     = 3,   /**< Division / */
    PI_AF_OP_POW     = 4,   /**< Exponentiation ^ */
    PI_AF_OP_MOD     = 5,   /**< Modulo % */
    PI_AF_OP_NEG     = 6,   /**< Unary negation -x */
    PI_AF_OP_EQ      = 7,   /**< Equal == */
    PI_AF_OP_NE      = 8,   /**< Not equal != */
    PI_AF_OP_LT      = 9,   /**< Less than < */
    PI_AF_OP_LE      = 10,  /**< Less than or equal <= */
    PI_AF_OP_GT      = 11,  /**< Greater than > */
    PI_AF_OP_GE      = 12,  /**< Greater than or equal >= */
    PI_AF_OP_AND     = 13,  /**< Logical AND && */
    PI_AF_OP_OR      = 14,  /**< Logical OR || */
    PI_AF_OP_NOT     = 15,  /**< Logical NOT ! */
} pi_af_operator_t;

/**
 * @brief A single token produced by the lexer.
 */
typedef struct {
    pi_af_token_type_t type;
    char      text[256];    /**< Token text (number, name, operator symbol) */
    double    num_value;    /**< Parsed numeric value (for NUMBER tokens) */
    int32_t   position;     /**< Character offset in source expression */
} pi_af_token_t;

/**
 * @brief AST (Abstract Syntax Tree) node types.
 */
typedef enum {
    PI_AF_AST_LITERAL     = 0,  /**< Numeric literal value */
    PI_AF_AST_ATTRIBUTE   = 1,  /**< Attribute reference */
    PI_AF_AST_BINARY_OP   = 2,  /**< Binary operator (a op b) */
    PI_AF_AST_UNARY_OP    = 3,  /**< Unary operator (op a) */
    PI_AF_AST_FUNC_CALL   = 4,  /**< Function call: f(arg1, arg2, ...) */
    PI_AF_AST_IF_EXPR     = 5,  /**< Conditional: If(cond, then, else) */
} pi_af_ast_type_t;

/**
 * @brief A single node in the Abstract Syntax Tree.
 *
 * The AST is a tree representation of the parsed expression
 * that eliminates syntactic details (parentheses, precedence)
 * and directly encodes the semantic structure.
 */
typedef struct pi_af_ast_node_t {
    pi_af_ast_type_t node_type;

    union {
        double    literal_value;              /**< PI_AF_AST_LITERAL */
        struct {
            char  attr_name[PI_AF_MAX_ANALYTIC_NAME];  /**< PI_AF_AST_ATTRIBUTE */
            char  attr_path[PI_AF_MAX_ANALYTIC_NAME];
        } attribute;
        struct {
            pi_af_operator_t op;              /**< PI_AF_AST_BINARY_OP / UNARY_OP */
            struct pi_af_ast_node_t *left;
            struct pi_af_ast_node_t *right;
        } binop;
        struct {
            pi_af_operator_t op;              /**< PI_AF_AST_UNARY_OP */
            struct pi_af_ast_node_t *operand;
        } unop;
        struct {
            char  func_name[PI_AF_MAX_FUNC_NAME];  /**< PI_AF_AST_FUNC_CALL */
            uint32_t arg_count;
            struct pi_af_ast_node_t *args[8];
        } func_call;
        struct {                              /**< PI_AF_AST_IF_EXPR */
            struct pi_af_ast_node_t *condition;
            struct pi_af_ast_node_t *then_branch;
            struct pi_af_ast_node_t *else_branch;
        } if_expr;
    } data;
} pi_af_ast_node_t;

/**
 * @brief Complete expression with AST and metadata.
 */
typedef struct {
    char       source[PI_AF_MAX_EXPRESSION_LEN];  /**< Original expression text */
    pi_af_ast_node_t *root;                        /**< Root of the AST */
    uint32_t   ast_node_count;                     /**< Number of nodes in tree */
    bool       is_valid;                           /**< Whether parsing succeeded */
    char       error_message[512];                 /**< Parse error description */
    uint32_t   attribute_refs[16];                 /**< Resolved attribute indices */
    uint32_t   attr_ref_count;                     /**< Number of attribute refs */
} pi_af_expression_t;

/* --------------------------------------------------------------------------
 * L1 — Definitions: Built-in Function Catalog
 * ------------------------------------------------------------------------*/

/**
 * @brief Signature for a built-in function implementation.
 *
 * @param args      Array of argument values
 * @param arg_count Number of arguments
 * @param result    Output: function result
 * @param error     Output: error message (or NULL on success)
 * @return          true on success, false on error
 */
typedef bool (*pi_af_builtin_func_t)(const double *args, uint32_t arg_count,
                                      double *result, char **error);

/**
 * @brief Entry in the built-in function table.
 */
typedef struct {
    char                name[PI_AF_MAX_FUNC_NAME];
    pi_af_builtin_func_t  impl;
    uint32_t            min_args;
    uint32_t            max_args;
    char                description[256];
} pi_af_function_entry_t;

/* --------------------------------------------------------------------------
 * L5 — Algorithms: Expression Processing Pipeline
 * ------------------------------------------------------------------------*/

/**
 * @brief Tokenize an expression string (Lexical Analysis).
 *
 * Converts the raw expression string into a stream of tokens.
 * Implements a DFA-based tokenizer that handles:
 *   - Whitespace skipping
 *   - Numeric literal recognition (including scientific notation)
 *   - Multi-character operator recognition (== != <= >= && ||)
 *   - Attribute reference detection (|name)
 *   - Function name and keyword identification
 *
 * @param source    Expression string to tokenize
 * @param tokens    Output array for tokens
 * @param max_tokens Maximum tokens to produce
 * @param out_count Output: actual token count
 * @return          PI_AF_OK on success, negative on error
 *
 * Complexity: O(n) where n = source length
 *
 * @see Aho, Sethi, Ullman (1986) §3.4 — Finite automata for lexical analysis
 */
pi_af_error_t pi_af_expression_tokenize(const char *source,
                                         pi_af_token_t *tokens,
                                         uint32_t max_tokens,
                                         uint32_t *out_count);

/**
 * @brief Parse tokens into an AST (Syntax Analysis).
 *
 * Converts the token stream into an Abstract Syntax Tree using
 * a recursive descent parser with operator precedence.
 *
 * Grammar (EBNF):
 *   expression   → conditional
 *   conditional  → logical_or ('?' conditional ':' logical_or)?
 *   logical_or   → logical_and ('||' logical_and)*
 *   logical_and  → equality ('&&' equality)*
 *   equality     → relational (('=='|'!=') relational)*
 *   relational   → additive (('<'|'<='|'>'|'>=') additive)*
 *   additive     → multiplicative (('+'|'-') multiplicative)*
 *   multiplicative → unary (('*'|'/'|'%') unary)*
 *   unary        → ('-'|'!') unary | power
 *   power        → primary ('^' unary)?
 *   primary      → NUMBER | ATTRIBUTE | FUNCTION '(' args ')' | '(' expression ')'
 *
 * @param tokens      Token array from tokenizer
 * @param token_count Number of tokens
 * @param expr        Output: parsed expression with AST
 * @return            PI_AF_OK on success, negative on parse error
 *
 * Complexity: O(n) where n = token count
 *
 * @see Dijkstra (1961) — Shunting-yard inspiration for operator precedence
 */
pi_af_error_t pi_af_expression_parse(const pi_af_token_t *tokens,
                                      uint32_t token_count,
                                      pi_af_expression_t *expr);

/**
 * @brief Evaluate a parsed expression AST.
 *
 * Walks the AST and computes the result. Attribute references are
 * resolved by looking up the current value from the provided value map.
 *
 * Short-circuit evaluation is used for && (false → skip right) and
 * || (true → skip right), matching C and PI AF semantics.
 *
 * @param expr        Parsed expression
 * @param values      Array of current attribute values (indexed by attr_ref order)
 * @param value_count Number of values provided
 * @param result      Output: evaluated result
 * @param error       Output: error message (or NULL on success)
 * @return            true on success, false on evaluation error
 *
 * Complexity: O(n) where n = AST node count
 *
 * @see Stanford ENGR205 — Expression evaluation for process control analytics
 */
bool pi_af_expression_evaluate(const pi_af_expression_t *expr,
                                const double *values, uint32_t value_count,
                                double *result, char **error);

/**
 * @brief Free memory associated with an expression AST.
 *
 * Recursively deallocates all AST nodes.
 *
 * @param expr  Expression to free (may be NULL)
 *
 * Complexity: O(n) where n = AST node count
 */
void pi_af_expression_free(pi_af_expression_t *expr);

/**
 * @brief Validate an expression string without evaluating it.
 *
 * Tokenizes and parses the expression, reporting any syntax errors.
 * Useful for UI validation before saving an analytic definition.
 *
 * @param source      Expression string
 * @param error_msg   Output: error description (or empty string on success)
 * @param error_len   Size of error_msg buffer
 * @return            true if valid, false if syntax error
 *
 * Complexity: O(n) where n = source length
 */
bool pi_af_expression_validate(const char *source, char *error_msg,
                                size_t error_len);

/**
 * @brief Get the string representation of an operator.
 *
 * Complexity: O(1)
 */
const char *pi_af_operator_symbol(pi_af_operator_t op);

/**
 * @brief Get operator precedence (higher = binds tighter).
 *
 * Complexity: O(1)
 */
int pi_af_operator_precedence(pi_af_operator_t op);

/**
 * @brief Check if an operator is left-associative.
 *
 * Complexity: O(1)
 */
bool pi_af_operator_is_left_assoc(pi_af_operator_t op);

/**
 * @brief Check if an operator is unary.
 *
 * Complexity: O(1)
 */
bool pi_af_operator_is_unary(pi_af_operator_t op);

/**
 * @brief Look up a built-in function by name.
 *
 * @param name  Function name (case-insensitive)
 * @return      Pointer to function entry, or NULL if not found
 *
 * Complexity: O(m) where m = number of registered functions
 */
const pi_af_function_entry_t *pi_af_lookup_function(const char *name);

/**
 * @brief Register a custom function in the function table.
 *
 * Custom functions can be used in expressions alongside built-ins.
 *
 * @param entry  Function definition (copied into internal table)
 * @return       PI_AF_OK on success
 *
 * Complexity: O(m) where m = number of registered functions
 */
pi_af_error_t pi_af_register_function(const pi_af_function_entry_t *entry);

/**
 * @brief Get the total number of registered functions (built-in + custom).
 *
 * Complexity: O(1)
 */
uint32_t pi_af_function_count(void);

/**
 * @brief Get a function entry by index.
 *
 * @param idx  Index (0 to function_count - 1)
 * @return     Function entry, or NULL if out of range
 *
 * Complexity: O(1)
 */
const pi_af_function_entry_t *pi_af_function_at(uint32_t idx);

#endif /* PI_AF_ANALYTICS_EXPRESSION_H */
