/*
 * Panorama - Wrap.card internals, shared by wrap.c and mirror.c.
 */
#ifndef P96MC_WRAP_H
#define P96MC_WRAP_H

#include <exec/types.h>
#include <exec/libraries.h>
#include "boardinfo.h"
#include "wrap_status.h"

extern struct ExecBase *SysBase;
extern struct DosLibrary *DOSBase;

extern void LibNull(void);

/* wrapstart.s: a card's FindCard and InitCard, called as library functions */
BOOL InnerFindCard(__reg("a0") struct BoardInfo *bi, __reg("a1") struct Library *card);
BOOL InnerInitCard(__reg("a0") struct BoardInfo *bi, __reg("a1") char **tooltypes,
                   __reg("a2") struct Library *card);

/* wrap.c */
struct Library *wrap_open_card(const char *card);
ULONG wrap_bytesperpixel(ULONG fmt);

/* Panorama.card 2's functions for heads (its orders.c) */
#define WRAP_LVO_MCRECORD  -42
#define WRAP_LVO_MCDAMAGE  -48
#define WRAP_LVO_MCPOINTER -54
typedef void (*McRecordFn)(__reg("a0") struct BoardInfo *head, __reg("d0") ULONG kind,
                           __reg("a1") ULONG *regs, __reg("a6") struct Library *card);
typedef void (*McDamageFn)(__reg("a0") struct BoardInfo *head, __reg("d0") LONG x,
                           __reg("d1") LONG y, __reg("d2") LONG w, __reg("d3") LONG h,
                           __reg("a6") struct Library *card);
#define WRAP_LVO(card, lvo) ((APTR)((UBYTE *)(card) + (lvo)))

/* mirror.c */
BOOL wrap_is_copper(struct Library *card);
void wrap_mirror_start(struct WrapBoard *b, struct Library *inner, struct WrapMirror *m);

#endif
