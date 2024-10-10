#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "../allib/dynamic_list/dynamic_list.h"

typedef struct {
    const char * name;
    enum {
        BDF_PROP_STR,
        BDF_PROP_NUM,
        BDF_PROP_NONE,
    } kind;
    union {
        const char * str;
        long num;
    } v;
} BDF_Prop;

/** UTF-16 */
typedef uint16_t BDF_char_t;

typedef struct {
    BDF_char_t encoded;
    unsigned swX, swY; // scalable width 
    unsigned offX, offY; // offset to add to position to get to pos of next glyph
    unsigned bbW, bbH; // bounding box 
    unsigned llhcX, llhcY; // low left hand corner
    unsigned char * bmp; // row-major bit set

INTERNAL 
    unsigned _bmpSizeBytes;
} BDF_Char;

typedef struct {
    unsigned major, minor;
    const char * fontId; // X logical font description
    unsigned pt;
    unsigned dpX, dpY;
    unsigned w, h;
    signed llhcX, llhcY; // low left hand corner
    DynamicList TYPES(BDF_Prop) props;
    DynamicList TYPES(BDF_Char) chars;
    DynamicList TYPES(DynamicList TYPES(void)) freeList;
    Ally ally;
} BDF_Font;

typedef struct BDF_LineParseCtx BDF_LineParseCtx;



void BDF_Char_bit2boolmap(BDF_Char const * c, bool* dest);

void BDF_Font_free(BDF_Font *font);

/** result depends on arg ptr! */
BDF_LineParseCtx* BDF_Font_fromLines_beginWithFilter(
        BDF_Font *dest, Ally ally,
        bool (*wantChar)(BDF_char_t,void*), void* wantCharData);

/** result depends on arg ptr! */
BDF_LineParseCtx* BDF_Font_fromLines_begin(
        BDF_Font *dest, Ally ally);

void BDF_Font_fromLines_nextBunch(BDF_LineParseCtx *ctx, const char ** lines, unsigned linesLen);

void BDF_Font_fromLines_end(BDF_LineParseCtx* ctx);

void BDF_Font_fromFileWithFilter(BDF_Font* dest, FILE* f, Ally ally,
        bool (*wantChar)(BDF_char_t,void*), void* wantCharData);

void BDF_Font_fromFile(BDF_Font* dest, FILE* f, Ally ally);

BDF_Char * BDF_Font_findChar(BDF_Font const* font, BDF_char_t code);

BDF_Prop * BDF_Font_getProp(BDF_Font const *font, const char * name);
const char * BDF_Font_getStrProp(BDF_Font const *font, const char *name);
const long BDF_Font_getNumProp(BDF_Font const *font, const char *name);
