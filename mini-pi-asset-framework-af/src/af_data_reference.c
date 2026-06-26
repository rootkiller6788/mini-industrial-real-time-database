/**
 * @file af_data_reference.c
 * @brief PI AF Data Reference Pipeline - value resolution engine
 *
 * Knowledge coverage:
 * L2: Data reference types - PI Point, Formula, Table Lookup, String Builder,
 *     Constant, Attribute Reference, Analysis
 * L3: Pipeline architecture - parse config, resolve source, return value
 * L5: Shunting-yard algorithm for formula expression parsing
 * L5: Table lookup with linear interpolation
 * L5: Rollup analytics (avg, min, max, sum, count, stddev)
 * L6: End-to-end attribute value resolution in AF hierarchy
 * L7: OSIsoft PI AF SDK Data Reference patterns
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include "af_data_reference.h"
#include "af_element.h"
#include "af_attribute.h"

/* Mini JSON string value extractor */
static const char* json_get_str(const char *json, const char *key)
{
    if (!json || !key) return NULL;
    static char buf[AF_MAX_CONFIG_LEN];
    char search[512];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return NULL;
    p += strlen(search);
    while (*p && (*p == ':' || *p == ' ' || *p == '\t')) p++;
    if (*p != '"') return NULL;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < (int)sizeof(buf) - 1) {
        buf[i++] = *p++;
    }
    buf[i] = 0;
    return buf;
}

static int json_get_int(const char *json, const char *key)
{
    const char *v = json_get_str(json, key);
    return v ? atoi(v) : 0;
}

/* Token types for formula expression parsing */
typedef enum { TK_NUM, TK_VAR, TK_OP, TK_LPAREN, TK_RPAREN } token_type_t;

typedef struct {
    token_type_t type;
    double num_val;
    char var_name[AF_MAX_ATTR_NAME_LEN];
    char op;
} token_t;

static int op_precedence(char op)
{
    switch (op) {
    case '+': case '-': return 1;
    case '*': case '/': return 2;
    case '^': return 3;
    default: return 0;
    }
}

static bool is_op_char(char c)
{
    return c == '+' || c == '-' || c == '*' || c == '/' || c == '^';
}

/* Tokenize formula string to token stream */
static int tokenize(const char *formula, token_t *tokens, int max_tokens)
{
    int tok_count = 0;
    const char *p = formula;

    while (*p && tok_count < max_tokens) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == 0) break;

        if (isdigit((unsigned char)*p) ||
            (*p == '.' && isdigit((unsigned char)*(p + 1)))) {
            char *end;
            tokens[tok_count].type = TK_NUM;
            tokens[tok_count].num_val = strtod(p, &end);
            p = end;
            tok_count++;
        } else if (isalpha((unsigned char)*p) || *p == '_') {
            tokens[tok_count].type = TK_VAR;
            int i = 0;
            while (*p && (isalnum((unsigned char)*p) || *p == '_') &&
                   i < (int)sizeof(tokens[tok_count].var_name) - 1) {
                tokens[tok_count].var_name[i++] = *p++;
            }
            tokens[tok_count].var_name[i] = 0;
            tok_count++;
        } else if (*p == '(') {
            tokens[tok_count++].type = TK_LPAREN; p++;
        } else if (*p == ')') {
            tokens[tok_count++].type = TK_RPAREN; p++;
        } else if (is_op_char(*p)) {
            tokens[tok_count].type = TK_OP;
            tokens[tok_count].op = *p;
            tok_count++; p++;
        } else {
            p++;
        }
    }
    return tok_count;
}

/* Shunting-yard: infix to Reverse Polish Notation */
static int shunting_yard(const token_t *infix, int n_in,
                          token_t *rpn, int max_rpn)
{
    token_t op_stack[256];
    int op_top = 0;
    int rpn_count = 0;

    for (int i = 0; i < n_in; i++) {
        token_t t = infix[i];

        if (t.type == TK_NUM || t.type == TK_VAR) {
            if (rpn_count >= max_rpn) return -1;
            rpn[rpn_count++] = t;
        } else if (t.type == TK_LPAREN) {
            if (op_top >= 256) return -1;
            op_stack[op_top++] = t;
        } else if (t.type == TK_RPAREN) {
            while (op_top > 0 && op_stack[op_top - 1].type != TK_LPAREN) {
                if (rpn_count >= max_rpn) return -1;
                rpn[rpn_count++] = op_stack[--op_top];
            }
            if (op_top == 0) return -1;
            op_top--;
        } else if (t.type == TK_OP) {
            while (op_top > 0 && op_stack[op_top - 1].type == TK_OP &&
                   op_precedence(op_stack[op_top - 1].op) >=
                   op_precedence(t.op)) {
                if (rpn_count >= max_rpn) return -1;
                rpn[rpn_count++] = op_stack[--op_top];
            }
            if (op_top >= 256) return -1;
            op_stack[op_top++] = t;
        }
    }

    while (op_top > 0) {
        if (op_stack[op_top - 1].type == TK_LPAREN) return -1;
        if (rpn_count >= max_rpn) return -1;
        rpn[rpn_count++] = op_stack[--op_top];
    }

    return rpn_count;
}

/* Evaluate RPN expression with variable resolver */
static int eval_rpn(const token_t *rpn, int n_rpn,
                     double (*var_resolver)(const char *name, void *ctx),
                     void *ctx, double *result)
{
    double stack[256];
    int top = 0;

    for (int i = 0; i < n_rpn; i++) {
        token_t t = rpn[i];
        if (t.type == TK_NUM) {
            if (top >= 256) return -1;
            stack[top++] = t.num_val;
        } else if (t.type == TK_VAR) {
            if (top >= 256) return -1;
            stack[top++] = var_resolver(t.var_name, ctx);
        } else if (t.type == TK_OP) {
            if (top < 2) return -1;
            double b = stack[--top];
            double a = stack[--top];
            switch (t.op) {
            case '+': stack[top++] = a + b; break;
            case '-': stack[top++] = a - b; break;
            case '*': stack[top++] = a * b; break;
            case '/':
                if (b == 0.0) return -1;
                stack[top++] = a / b;
                break;
            case '^': stack[top++] = pow(a, b); break;
            default: return -1;
            }
        }
    }
    if (top != 1) return -1;
    *result = stack[0];
    return 0;
}

/* Variable resolver context */
typedef struct {
    const AFElement *element;
} var_resolver_ctx_t;

static double var_resolver_get(const char *name, void *ctx_ptr)
{
    var_resolver_ctx_t *vctx = (var_resolver_ctx_t*)ctx_ptr;
    if (!vctx || !vctx->element) return 0.0;
    AFAttribute *attr = af_element_get_attribute(vctx->element, name);
    if (!attr) return 0.0;
    const af_value_t *val = af_attribute_get_value(attr);
    if (!val || !val->is_good) return 0.0;
    switch (val->type) {
    case AF_VAL_INT32:    return (double)val->value.v_int32;
    case AF_VAL_FLOAT64:  return val->value.v_float64;
    case AF_VAL_BOOLEAN:  return val->value.v_boolean ? 1.0 : 0.0;
    case AF_VAL_ENUM:     return (double)val->value.v_enum_val;
    default:              return 0.0;
    }
}

/* Main data reference resolution entry point */
bool af_data_reference_resolve(const AFAttribute *attr,
                                af_dr_context_t *ctx,
                                af_value_t *out_val)
{
    if (!attr || !ctx || !out_val) return false;
    memset(out_val, 0, sizeof(*out_val));
    out_val->type = attr->value_type;
    out_val->is_good = false;

    switch (attr->dr_type) {
    case AF_DR_NONE:
        *out_val = attr->current_value;
        return true;

    case AF_DR_PI_POINT: {
        af_dr_pi_point_t pi_cfg;
        memset(&pi_cfg, 0, sizeof(pi_cfg));
        if (!af_dr_parse_config(attr, &pi_cfg, NULL, NULL, NULL, NULL, NULL, NULL))
            return false;
        if (!ctx->pi_value_provider) return false;
        int rc = ctx->pi_value_provider(pi_cfg.pi_server, pi_cfg.pi_tag,
                                         pi_cfg.use_snapshot, out_val);
        if (rc != 0) {
            if (attr->has_default) {
                *out_val = attr->default_value;
                return true;
            }
            return false;
        }
        return true;
    }

    case AF_DR_FORMULA: {
        af_dr_formula_t fm_cfg;
        memset(&fm_cfg, 0, sizeof(fm_cfg));
        if (!af_dr_parse_config(attr, NULL, &fm_cfg, NULL, NULL, NULL, NULL, NULL))
            return false;

        token_t tokens[256], rpn[256];
        int n_tok = tokenize(fm_cfg.formula, tokens, 256);
        if (n_tok < 1) {
            if (attr->has_default) {
                *out_val = attr->default_value;
                return true;
            }
            return false;
        }
        int n_rpn = shunting_yard(tokens, n_tok, rpn, 256);
        if (n_rpn < 0) return false;

        var_resolver_ctx_t vrctx;
        vrctx.element = attr->owner_element;

        double result;
        if (eval_rpn(rpn, n_rpn, var_resolver_get, &vrctx, &result) != 0)
            return false;

        out_val->type = AF_VAL_FLOAT64;
        out_val->value.v_float64 = result;
        out_val->is_good = true;
        return true;
    }

    case AF_DR_TABLE_LOOKUP: {
        af_dr_table_lookup_t tbl_cfg;
        memset(&tbl_cfg, 0, sizeof(tbl_cfg));
        if (!af_dr_parse_config(attr, NULL, NULL, &tbl_cfg, NULL, NULL, NULL, NULL))
            return false;
        if (tbl_cfg.row_count < 1) return false;

        AFAttribute *input_attr = NULL;
        if (attr->owner_element) {
            input_attr = af_element_get_attribute(attr->owner_element,
                                                   tbl_cfg.input_attr);
        }
        double input_val = 0.0;
        if (input_attr) {
            const af_value_t *iv = af_attribute_get_value(input_attr);
            if (iv && iv->is_good) {
                input_val = (iv->type == AF_VAL_FLOAT64)
                    ? iv->value.v_float64 : (double)iv->value.v_int32;
            }
        }

        if (tbl_cfg.use_interpolation) {
            int ilo = -1, ihi = -1;
            for (int i = 0; i < tbl_cfg.row_count; i++) {
                if (tbl_cfg.rows[i].input <= input_val) ilo = i;
                if (tbl_cfg.rows[i].input >= input_val && ihi < 0) ihi = i;
            }
            if (ilo < 0) ilo = 0;
            if (ihi < 0) ihi = tbl_cfg.row_count - 1;

            if (ilo == ihi) {
                out_val->value.v_float64 = tbl_cfg.rows[ilo].output;
            } else {
                double x0 = tbl_cfg.rows[ilo].input;
                double x1 = tbl_cfg.rows[ihi].input;
                double y0 = tbl_cfg.rows[ilo].output;
                double y1 = tbl_cfg.rows[ihi].output;
                if (x1 != x0) {
                    out_val->value.v_float64 =
                        y0 + (y1 - y0) * (input_val - x0) / (x1 - x0);
                } else {
                    out_val->value.v_float64 = y0;
                }
            }
        } else {
            double best = tbl_cfg.rows[0].output;
            for (int i = 0; i < tbl_cfg.row_count; i++) {
                if (tbl_cfg.rows[i].input == input_val) {
                    best = tbl_cfg.rows[i].output;
                    break;
                }
                if (tbl_cfg.rows[i].input < input_val)
                    best = tbl_cfg.rows[i].output;
            }
            out_val->value.v_float64 = best;
        }
        out_val->type = AF_VAL_FLOAT64;
        out_val->is_good = true;
        return true;
    }

    case AF_DR_STRING_BUILDER: {
        af_dr_string_builder_t sb_cfg;
        memset(&sb_cfg, 0, sizeof(sb_cfg));
        if (!af_dr_parse_config(attr, NULL, NULL, NULL, &sb_cfg, NULL, NULL, NULL))
            return false;

        char result_buf[AF_MAX_CONFIG_LEN];
        memset(result_buf, 0, sizeof(result_buf));
        const char *fmt = sb_cfg.format;
        size_t ri = 0;

        while (*fmt && ri < sizeof(result_buf) - 1) {
            if (*fmt == '{' && *(fmt + 1) >= '0' && *(fmt + 1) <= '9') {
                fmt++;
                int idx = *fmt - '0';
                fmt++;
                while (*fmt && *fmt != '}') fmt++;
                if (*fmt == '}') fmt++;

                if (idx < sb_cfg.component_count && attr->owner_element) {
                    AFAttribute *comp = af_element_get_attribute(
                        attr->owner_element, sb_cfg.component_attrs[idx]);
                    if (comp) {
                        char val_str[256];
                        af_attribute_value_to_string(comp, val_str, sizeof(val_str));
                        size_t vl = strlen(val_str);
                        if (ri + vl < sizeof(result_buf) - 1) {
                            memcpy(result_buf + ri, val_str, vl);
                            ri += vl;
                        }
                    }
                }
            } else {
                result_buf[ri++] = *fmt++;
            }
        }
        result_buf[ri] = 0;

        out_val->type = AF_VAL_STRING;
        out_val->value.v_string = strdup(result_buf);
        out_val->is_good = true;
        return true;
    }

    case AF_DR_CONSTANT: {
        af_dr_constant_t const_cfg;
        memset(&const_cfg, 0, sizeof(const_cfg));
        if (!af_dr_parse_config(attr, NULL, NULL, NULL, NULL, &const_cfg, NULL, NULL))
            return false;
        *out_val = const_cfg.constant_value;
        out_val->is_good = true;
        return true;
    }

    case AF_DR_ATTR_REF: {
        af_dr_attr_ref_t ref_cfg;
        memset(&ref_cfg, 0, sizeof(ref_cfg));
        if (!af_dr_parse_config(attr, NULL, NULL, NULL, NULL, NULL, &ref_cfg, NULL))
            return false;
        if (!ctx->root_element) return false;
        AFElement *target = af_element_find_by_path(
            ctx->root_element, ref_cfg.target_element_path);
        if (!target) return false;
        AFAttribute *target_attr = af_element_get_attribute(
            target, ref_cfg.target_attr_name);
        if (!target_attr) return false;
        *out_val = target_attr->current_value;
        return true;
    }

    case AF_DR_ANALYSIS: {
        af_dr_analysis_t an_cfg;
        memset(&an_cfg, 0, sizeof(an_cfg));
        if (!af_dr_parse_config(attr, NULL, NULL, NULL, NULL, NULL, NULL, &an_cfg))
            return false;
        if (!attr->owner_element) return false;

        if (an_cfg.analysis_type >= AF_ANALYSIS_ROLLUP_AVG &&
            an_cfg.analysis_type <= AF_ANALYSIS_ROLLUP_STDDEV) {

            double values[1024];
            int val_count = 0;

            for (size_t ci = 0;
                 ci < attr->owner_element->child_count && val_count < 1024;
                 ci++) {
                AFElement *child = attr->owner_element->children[ci];
                AFAttribute *ca = af_element_get_attribute(
                    child, an_cfg.source_attr);
                if (!ca) continue;
                const af_value_t *cv = af_attribute_get_value(ca);
                if (!cv || !cv->is_good) continue;
                double v = 0.0;
                if (cv->type == AF_VAL_FLOAT64)
                    v = cv->value.v_float64;
                else if (cv->type == AF_VAL_INT32)
                    v = (double)cv->value.v_int32;
                else continue;
                values[val_count++] = v;
            }

            if (val_count == 0) return false;

            double result = 0.0;
            switch (an_cfg.analysis_type) {
            case AF_ANALYSIS_ROLLUP_AVG: {
                double sum = 0.0;
                for (int i = 0; i < val_count; i++) sum += values[i];
                result = sum / val_count;
                break;
            }
            case AF_ANALYSIS_ROLLUP_MIN:
                result = values[0];
                for (int i = 1; i < val_count; i++)
                    if (values[i] < result) result = values[i];
                break;
            case AF_ANALYSIS_ROLLUP_MAX:
                result = values[0];
                for (int i = 1; i < val_count; i++)
                    if (values[i] > result) result = values[i];
                break;
            case AF_ANALYSIS_ROLLUP_SUM:
                for (int i = 0; i < val_count; i++) result += values[i];
                break;
            case AF_ANALYSIS_ROLLUP_COUNT:
                result = (double)val_count;
                break;
            case AF_ANALYSIS_ROLLUP_STDDEV: {
                double mean = 0.0, sq = 0.0;
                for (int i = 0; i < val_count; i++) mean += values[i];
                mean /= val_count;
                for (int i = 0; i < val_count; i++)
                    sq += (values[i] - mean) * (values[i] - mean);
                result = sqrt(sq / val_count);
                break;
            }
            default: break;
            }
            out_val->type = AF_VAL_FLOAT64;
            out_val->value.v_float64 = result;
            out_val->is_good = true;
            return true;
        }

        if (attr->has_default) {
            *out_val = attr->default_value;
            return true;
        }
        return false;
    }

    default:
        break;
    }

    return false;
}

/* Parse DR config string into typed structures */
bool af_dr_parse_config(const AFAttribute *attr,
                         af_dr_pi_point_t *out_pi,
                         af_dr_formula_t *out_formula,
                         af_dr_table_lookup_t *out_table,
                         af_dr_string_builder_t *out_sb,
                         af_dr_constant_t *out_constant,
                         af_dr_attr_ref_t *out_ref,
                         af_dr_analysis_t *out_analysis)
{
    if (!attr) return false;
    const char *cfg = attr->dr_config;

    switch (attr->dr_type) {
    case AF_DR_PI_POINT:
        if (out_pi) {
            memset(out_pi, 0, sizeof(*out_pi));
            const char *srv = json_get_str(cfg, "server");
            const char *tag = json_get_str(cfg, "tag");
            if (srv) {
                strncpy(out_pi->pi_server, srv, sizeof(out_pi->pi_server) - 1);
                out_pi->pi_server[sizeof(out_pi->pi_server) - 1] = 0;
            }
            if (tag) {
                strncpy(out_pi->pi_tag, tag, sizeof(out_pi->pi_tag) - 1);
                out_pi->pi_tag[sizeof(out_pi->pi_tag) - 1] = 0;
            }
            out_pi->use_snapshot = json_get_int(cfg, "snapshot") != 0;
        }
        return true;

    case AF_DR_FORMULA:
        if (out_formula) {
            memset(out_formula, 0, sizeof(*out_formula));
            const char *expr = json_get_str(cfg, "expr");
            if (expr) {
                strncpy(out_formula->formula, expr, AF_MAX_CONFIG_LEN - 1);
                out_formula->formula[AF_MAX_CONFIG_LEN - 1] = 0;
            }
        }
        return true;

    case AF_DR_TABLE_LOOKUP:
        if (out_table) {
            memset(out_table, 0, sizeof(*out_table));
            const char *ia = json_get_str(cfg, "input_attr");
            if (ia) {
                strncpy(out_table->input_attr, ia, sizeof(out_table->input_attr) - 1);
                out_table->input_attr[sizeof(out_table->input_attr) - 1] = 0;
            }
            out_table->use_interpolation = json_get_int(cfg, "interpolate") != 0;
        }
        return true;

    case AF_DR_STRING_BUILDER:
        if (out_sb) {
            memset(out_sb, 0, sizeof(*out_sb));
            const char *fmt = json_get_str(cfg, "format");
            if (fmt) {
                strncpy(out_sb->format, fmt, AF_MAX_CONFIG_LEN - 1);
                out_sb->format[AF_MAX_CONFIG_LEN - 1] = 0;
            }
        }
        return true;

    case AF_DR_CONSTANT:
        if (out_constant) {
            memset(out_constant, 0, sizeof(*out_constant));
            const char *vs = json_get_str(cfg, "value");
            if (vs) {
                out_constant->constant_value.type = attr->value_type;
                out_constant->constant_value.value.v_float64 = atof(vs);
                out_constant->constant_value.is_good = true;
            }
        }
        return true;

    case AF_DR_ATTR_REF:
        if (out_ref) {
            memset(out_ref, 0, sizeof(*out_ref));
            const char *ep = json_get_str(cfg, "element_path");
            const char *an = json_get_str(cfg, "attr_name");
            if (ep) {
                strncpy(out_ref->target_element_path, ep, sizeof(out_ref->target_element_path) - 1);
                out_ref->target_element_path[sizeof(out_ref->target_element_path) - 1] = 0;
            }
            if (an) {
                strncpy(out_ref->target_attr_name, an, sizeof(out_ref->target_attr_name) - 1);
                out_ref->target_attr_name[sizeof(out_ref->target_attr_name) - 1] = 0;
            }
        }
        return true;

    case AF_DR_ANALYSIS:
        if (out_analysis) {
            memset(out_analysis, 0, sizeof(*out_analysis));
            out_analysis->analysis_type = (af_analysis_type_t)json_get_int(cfg, "type");
            const char *sa = json_get_str(cfg, "source_attr");
            if (sa) {
                strncpy(out_analysis->source_attr, sa, sizeof(out_analysis->source_attr) - 1);
                out_analysis->source_attr[sizeof(out_analysis->source_attr) - 1] = 0;
            }
        }
        return true;

    default:
        return false;
    }
}

/* Config string builders for each DR type */
int af_dr_build_pi_point_config(const char *server, const char *tag,
                                  char *buf, size_t bufsz)
{
    if (!server || !tag || !buf || bufsz == 0) return -1;
    return snprintf(buf, bufsz,
                    "{\"server\":\"%s\",\"tag\":\"%s\",\"snapshot\":1}",
                    server, tag);
}

int af_dr_build_formula_config(const char *expression,
                                char *buf, size_t bufsz)
{
    if (!expression || !buf || bufsz == 0) return -1;
    return snprintf(buf, bufsz, "{\"expr\":\"%s\"}", expression);
}

int af_dr_build_table_config(const char *input_attr, bool interpolate,
                               char *buf, size_t bufsz)
{
    if (!input_attr || !buf || bufsz == 0) return -1;
    return snprintf(buf, bufsz,
                    "{\"input_attr\":\"%s\",\"interpolate\":%d}",
                    input_attr, interpolate ? 1 : 0);
}

int af_dr_build_stringbuilder_config(const char *format,
                                       char *buf, size_t bufsz)
{
    if (!format || !buf || bufsz == 0) return -1;
    return snprintf(buf, bufsz, "{\"format\":\"%s\"}", format);
}

int af_dr_build_constant_config(const af_value_t *val,
                                  char *buf, size_t bufsz)
{
    if (!val || !buf || bufsz == 0) return -1;
    if (val->type == AF_VAL_FLOAT64) {
        return snprintf(buf, bufsz, "{\"value\":\"%.6f\"}",
                        val->value.v_float64);
    } else if (val->type == AF_VAL_INT32) {
        return snprintf(buf, bufsz, "{\"value\":\"%d\"}",
                        val->value.v_int32);
    }
    return snprintf(buf, bufsz, "{\"value\":\"0\"}");
}

int af_dr_build_analysis_config(af_analysis_type_t type,
                                  const char *source_attr,
                                  char *buf, size_t bufsz)
{
    if (!source_attr || !buf || bufsz == 0) return -1;
    return snprintf(buf, bufsz,
                    "{\"type\":%d,\"source_attr\":\"%s\"}",
                    (int)type, source_attr);
}
