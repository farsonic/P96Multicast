/*
 * Panorama - kdebug.h: debugging output on the serial port (make DEBUG=1),
 * as exec's own: RawDoFmt() to RawPutChar(), from any task or interrupt.
 * Nothing of it is compiled without MC_DEBUG.
 *
 *   KDEBUG(("x=%ld\n", (LONG)x));
 *
 * Under FS-UAE, serial_port = tcp://127.0.0.1:1234 and a reader on that
 * port (emu/boing_cross_test.sh SERIAL=1) collect it.
 */
#ifndef KDEBUG_H
#define KDEBUG_H

#ifdef MC_DEBUG
#include <exec/types.h>
#include <exec/execbase.h>
#include <proto/exec.h>

void __kdebug_RawPutChar(__reg("a6") struct ExecBase *, __reg("d0") LONG c) = "\tjsr\t-516(a6)";

static void kdebug_putch(__reg("d0") char c, __reg("a3") APTR data)
{
	if (c)
		__kdebug_RawPutChar(*(struct ExecBase **)4, c);
}

static void kdebug(const char *fmt, ...)
{
	RawDoFmt((STRPTR)fmt, (APTR)(&fmt + 1), (void (*)())kdebug_putch, NULL);
}

#define KDEBUG(args) kdebug args
#else
#define KDEBUG(args)
#endif

#endif
