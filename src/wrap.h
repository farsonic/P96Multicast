/*
 * P96Multicast - Wrap.card internals, shared by wrap.c and mirror.c.
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

/* mirror.c */
void wrap_mirror_start(struct WrapBoard *b, struct Library *inner, struct WrapMirror *m);

#endif
