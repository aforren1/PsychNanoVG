/* mxArray to C marshaling for psychnanovg (SPEC 7.2).
 *
 * Every reader raises a MATLAB error and does not return when the argument
 * is the wrong class or out of range, so the generated handlers stay free of
 * error plumbing.
 */
#ifndef PNVG_MARSHAL_H
#define PNVG_MARSHAL_H

#include <stddef.h>

#include "mex.h"
#include "nanovg.h"

/* A borrowed UTF-8 string. Short strings stay in `stack`, which keeps the
 * per-call path free of heap traffic; longer ones use mxMalloc, which MATLAB
 * reclaims even when a later argument check raises an error. */
#define PNVG_STR_STACK 4096

typedef struct {
    char *p;
    size_t n;
    int heap;
    char stack[PNVG_STR_STACK];
} pnvg_str;

void pnvg_err(const char *id, const char *fmt, ...);
/* Raises the identifier that belongs to a core status code (SPEC 5.5), with
 * the core's last error text after the subcommand name. */
void pnvg_raise(int status, const char *cmd);

double pnvg_arg_double(const mxArray *a, int i, const char *cmd);
float pnvg_arg_float(const mxArray *a, int i, const char *cmd);
int pnvg_arg_int(const mxArray *a, int i, const char *cmd);
unsigned char pnvg_arg_uchar(const mxArray *a, int i, const char *cmd);
int pnvg_arg_enum(const mxArray *a, int i, const char *cmd);
int pnvg_arg_image(const mxArray *a, int i, const char *cmd);
int pnvg_arg_font(const mxArray *a, int i, const char *cmd);
NVGcolor pnvg_arg_color(const mxArray *a, int i, const char *cmd);
NVGpaint pnvg_arg_paint(const mxArray *a, int i, const char *cmd);
void pnvg_arg_string(const mxArray *a, int i, const char *cmd, pnvg_str *out);
void pnvg_str_free(pnvg_str *s);
void pnvg_arg_xform(const mxArray *a, int i, const char *cmd, float *dst6);
const unsigned char *pnvg_arg_bytes(const mxArray *a, int i, const char *cmd,
                                    int *n);
const unsigned char *pnvg_arg_image_data(const mxArray *a, int i,
                                         const char *cmd, int *w, int *h);

mxArray *pnvg_make_row(const float *v, int n);
mxArray *pnvg_make_color(NVGcolor c);
mxArray *pnvg_make_glyphs(const NVGglyphPosition *p, int n, const char *base);
mxArray *pnvg_make_rows(const NVGtextRow *r, int n, const char *base);

/* Stores a returned NVGpaint in the paint table and gives back the index. */
int pnvg_paint_store(const NVGpaint *p);

/* Looks a NanoVG constant up by name, with or without the NVG_ prefix, and
 * with names joined by |. Returns 0 and sets *ok when the name is unknown. */
int pnvg_enum_lookup(const char *name, int *ok);

#endif /* PNVG_MARSHAL_H */
