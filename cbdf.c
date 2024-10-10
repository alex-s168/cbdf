// https://en.wikipedia.org/wiki/Glyph_Bitmap_Distribution_Format#:~:text=The%20Glyph%20Bitmap%20Distribution%20Format%20(BDF)

#include <string.h>
#include <stdlib.h>

#include "cbdf.h"
#include "../allib/filelib/filelib.h"

static void BDF_Char_setBit(BDF_Char * c, unsigned i, bool value)
{
    unsigned char * dst = &c->bmp[i / 8];
    unsigned mask = (0b10000000 >> (i % 8));
    if (value) {
        *dst |= mask;
    } else {
        *dst &= ~mask;
    }
}

void BDF_Char_bit2boolmap(BDF_Char const * c, bool* dest)
{
    unsigned wh = c->bbW * c->bbH;
    unsigned writer = 0;
    unsigned i = 0;
    for (; i < wh / 8; i ++) {
        unsigned bits = c->bmp[i];
        for (unsigned j = 0; j < 8; j ++)
        {
            dest[writer ++] = bits & (0b10000000 >> j);
        }
    }
    unsigned bits = c->bmp[i];
    for (unsigned j = 0; j < 8; j ++)
    {
        dest[writer ++] = bits & (0b10000000 >> j);
    }
}

void BDF_Font_free(BDF_Font *font)
{
    Ally a = font->ally;

    BDF_Char* chars = font->chars.fixed.data;
    for (unsigned i = 0; i < font->chars.fixed.len; i ++)
    {
        BDF_Char* c = &chars[i];
        yfree(a, c->bmp, c->_bmpSizeBytes);
    }
    DynamicList_clear(&font->chars);

    DynamicList_clear(&font->props);

    DynamicList TYPES(void) * freeList = font->freeList.fixed.data;
    for (unsigned i = 0; i < font->freeList.fixed.len; i ++)
    {
        DynamicList_clear(&freeList[i]);
    }
    DynamicList_clear(&font->freeList);
}

typedef struct {
    void *filterCharData;
    bool (*filterCharFn)(BDF_char_t, void*);

    bool     inProperties;
    bool     isInChar;
    bool     skipThisChar;
    unsigned bmpWriter;
    BDF_Char currentChar;
} ParserState;

static const char msg_no_mem[] = "out of memory (malloc)";
static const char msg_too_many_prop[] = "more font properties than specified";
static const char msg_unknown_op[] = "unknown operation";

__attribute__ ((noreturn))
static void err_exit(const char *msg) {
    fprintf(stderr, "error: %s\n", msg);
    exit(1);
}

__attribute__ ((noreturn))
static void err_exit2(const char *msg, const char * arg) {
    fprintf(stderr, "error: %s (%s)\n", msg, arg);
    exit(1);
}

static unsigned char hex_decode_char(char v)
{
    if (v >= 'A' && v <= 'F')
        return v - 'A' + 10;
    return v - '0';
}

/** returns weather or not line can be freed */
static bool parseLine(ParserState *p, BDF_Font *f, char * line)
{
    char *part2 = strchr(line, ' ');
    if (part2) {
        *part2 = '\0';
        part2 ++;
    }

    if (p->inProperties) {
        if (!strcmp(line, "ENDPROPERTIES")) 
        {
            p->inProperties = false;
            return true;
        }
        else // property 
        {
            BDF_Prop *prop = DynamicList_addp(&f->props);
            prop->name = line;

            if (!part2)
            {
                prop->kind = BDF_PROP_NONE;
            }
            else if (*part2 == '"')
            {
                part2 ++;
                *strrchr(part2, '"') = '\0';
                prop->kind = BDF_PROP_STR;
                prop->v.str = part2;
            }
            else 
            {
                prop->kind = BDF_PROP_NUM;
                sscanf(part2, "%li", &prop->v.num);
            }

            return false;
        }
    }
    else if (p->isInChar && p->currentChar.bmp)
    {
        if (!strcmp(line, "ENDCHAR"))
        {
            p->isInChar = false;
            DynamicList_add(&f->chars, &p->currentChar);
            return true;
        }
        else 
        {
            unsigned rowBitIdx = 0;
            while (*line) {
                unsigned char bits4;
                bits4 = hex_decode_char(*(line++));

                for (unsigned i = 0; i < 4; i ++) {
                    if (rowBitIdx >= p->currentChar.bbW)
                        break;

                    unsigned bitIdx = rowBitIdx + p->bmpWriter;
                    BDF_Char_setBit(&p->currentChar, bitIdx, bits4 & (0b1000  >> i));

                    rowBitIdx ++;
                }
            }
            p->bmpWriter += p->currentChar.bbW;
            return true;
        }
    }
    else if (p->isInChar)
    {
        if (!strcmp(line, "ENDCHAR"))
        {
            assert(p->skipThisChar);
            p->isInChar = false;
            return true;
        }

        if (p->skipThisChar) {
            return true;
        }

        if (!strcmp(line, "ENCODING"))
        {
            short val;
            sscanf(part2, "%hi", &val);
            p->currentChar.encoded = val;
            if (p->filterCharFn) {
                if (!p->filterCharFn(p->currentChar.encoded, p->filterCharData)) {
                    p->skipThisChar = true;
                }
            }
            return true;
        }
        else if (!strcmp(line, "SWIDTH"))
        {
            sscanf(part2, "%u %u", &p->currentChar.swX, &p->currentChar.swY);
            return true;
        }
        else if (!strcmp(line, "DWIDTH"))
        {
            sscanf(part2, "%u %u", &p->currentChar.offX, &p->currentChar.offY);
            return true;
        }
        else if (!strcmp(line, "BBX"))
        {
            sscanf(part2, "%u %u %i %i", &p->currentChar.bbW, &p->currentChar.bbH, &p->currentChar.llhcX, &p->currentChar.llhcY);
            return true;
        }
        else if (!strcmp(line, "BITMAP"))
        {
            p->currentChar._bmpSizeBytes = p->currentChar.bbW * p->currentChar.bbH / 8 + 1;
            p->currentChar.bmp = yalloc(f->ally, p->currentChar._bmpSizeBytes);
            memset(p->currentChar.bmp, 0, p->currentChar._bmpSizeBytes);
            if (!p->currentChar.bmp)
                err_exit(msg_no_mem);
            p->bmpWriter = 0;
            return true;
        }
        else 
        {
            err_exit2(msg_unknown_op, line);
        }
    }
    else {
        if (!strcmp(line, "COMMENT"))
        {
            return true;
        }
        else if (!strcmp(line, "STARTFONT"))
        {
            sscanf(part2, "%u.%u", &f->major, &f->minor);
            return true;
        }
        else if (!strcmp(line, "FONT"))
        {
            f->fontId = part2;
            return false;
        }
        else if (!strcmp(line, "SIZE"))
        {
            sscanf(part2, "%u %u %u", &f->pt, &f->dpX, &f->dpY);
            return true;
        }
        else if (!strcmp(line, "FONTBOUNDINGBOX"))
        {
            sscanf(part2, "%u %u %i %i", &f->w, &f->h, &f->llhcX, &f->llhcY);
            return true;
        }
        else if (!strcmp(line, "STARTPROPERTIES"))
        {
            p->inProperties = true;
            return true;
        }
        else if (!strcmp(line, "STARTCHAR"))
        {
            p->isInChar = true;
            p->bmpWriter = 0;
            p->skipThisChar = false;
            memset(&p->currentChar, 0, sizeof(BDF_Char));
            return true;
        }
        else if (!strcmp(line, "ENDFONT"))
        {
            return true;
        }
        else if (!strcmp(line, "CHARS"))
        {
            return true;
        }
        else 
        {
            err_exit2(msg_unknown_op, line);
        }
    }
}

struct BDF_LineParseCtx {
    BDF_Font *font;
    ParserState p;
};

/** result depends on arg ptr! */
BDF_LineParseCtx* BDF_Font_fromLines_beginWithFilter(
        BDF_Font *dest, Ally ally,
        bool (*wantChar)(BDF_char_t,void*), void* wantCharData)
{
    memset(dest, 0, sizeof(BDF_Font));
    DynamicList_init(&dest->props, sizeof(BDF_Prop), ally, 0);
    DynamicList_init(&dest->chars, sizeof(BDF_Char), ally, 64);
    DynamicList_init(&dest->freeList, sizeof(DynamicList TYPES(void)), ally, 32);

    BDF_LineParseCtx* ctx = yalloc(ally, sizeof(struct BDF_LineParseCtx));
    memset(ctx, 0, sizeof(struct BDF_LineParseCtx));
    ctx->font = dest;
    ctx->font->ally = ally;
    ctx->p.filterCharFn = wantChar;
    ctx->p.filterCharData = wantCharData;
    return ctx;
}

/** result depends on arg ptr! */
BDF_LineParseCtx* BDF_Font_fromLines_begin(
        BDF_Font *dest, Ally ally)
{
    return BDF_Font_fromLines_beginWithFilter(dest, ally, NULL, NULL);
}

void BDF_Font_fromLines_nextBunch(BDF_LineParseCtx *ctx, const char ** lines, unsigned linesLen)
{
    for (unsigned i = 0; i < linesLen; i ++)
    {
        unsigned ll = strlen(lines[i]);
        DynamicList TYPES(char) heapLine;
        DynamicList_init(&heapLine, sizeof(char), ctx->font->ally, ll + 1);
        DynamicList_addAll(&heapLine, lines[i], ll, sizeof(char));
        *(char*)DynamicList_addp(&heapLine) = '\0';

        if (parseLine(&ctx->p, ctx->font, heapLine.fixed.data)) {
            DynamicList_clear(&heapLine);
        } else {
            DynamicList_add(&ctx->font->freeList, &heapLine);
        }
    }
}

void BDF_Font_fromLines_end(BDF_LineParseCtx* ctx)
{
    yfree(ctx->font->ally, ctx, sizeof(struct BDF_LineParseCtx));
}

void BDF_Font_fromFileWithFilter(BDF_Font* dest, FILE* f, Ally ally,
        bool (*wantChar)(BDF_char_t,void*), void* wantCharData)
{
    BDF_LineParseCtx* ctx =
        BDF_Font_fromLines_beginWithFilter(dest, ally, wantChar, wantCharData);

    unsigned i = 0;
    for (;;) {
        DynamicList TYPES(char) line = readLine(f, ally);
        if (line.fixed.len == 0) break;
        DynamicList_add(&line, (char[]){ '\0' });

        if (parseLine(&ctx->p, ctx->font, line.fixed.data)) {
            DynamicList_clear(&line);
        } else {
            DynamicList_add(&ctx->font->freeList, &line);
        }

        if (feof(f)) break;
    }

    BDF_Font_fromLines_end(ctx);
}

void BDF_Font_fromFile(BDF_Font* dest, FILE* f, Ally ally)
{
    BDF_Font_fromFileWithFilter(dest, f, ally, NULL, NULL);
}

BDF_Char * BDF_Font_findChar(BDF_Font const* font, BDF_char_t code)
{
    // it is probably arround index @code 

    size_t beginSearch = code / 2;

    BDF_Char * chars = font->chars.fixed.data;

    for (size_t i = beginSearch; i < font->chars.fixed.len; i ++)
    {
        if (chars[i].encoded == code)
            return &chars[i];
    }

    for (size_t i = 0; i < font->chars.fixed.len; i ++)
    {
        if (chars[i].encoded == code)
            return &chars[i];
    }

    return NULL;
}

BDF_Prop * BDF_Font_getProp(BDF_Font const *font, const char * name)
{
    BDF_Prop * props = font->props.fixed.data;

    for (size_t i = 0; i < font->props.fixed.len; i ++)
    {
        if (!strcmp(props[i].name, name))
            return &props[i];
    }

    return NULL;
}

const char * BDF_Font_getStrProp(BDF_Font const *font, const char *name)
{
    BDF_Prop * p = BDF_Font_getProp(font, name);
    if (!p) return NULL;
    if (p->kind != BDF_PROP_STR) return NULL;
    return p->v.str;
}

const long BDF_Font_getNumProp(BDF_Font const *font, const char *name)
{
    BDF_Prop * p = BDF_Font_getProp(font, name);
    if (!p) return -1;
    if (p->kind != BDF_PROP_NUM) return -1;
    return p->v.num;
}
