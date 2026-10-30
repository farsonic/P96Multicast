/*
 * Panorama - panoramapic.datatype: pictures decoded by the viewer.
 *
 * A DataTypes class for JPEG, PNG and the other picture files a computer
 * decodes in a moment and a 68020 in a minute. It reads the file, sends it
 * to the viewer (the one Panorama.card last heard from: the viewers run a
 * decoder, panoview --decode-port) and gets the pixels back, 3 bytes each,
 * which it writes into picture.datatype (V43, as Picasso96 installs it:
 * PDTM_WRITEPIXELARRAY). Multiview, Workbench and every program that shows
 * pictures through datatypes gets them this way, none the wiser.
 *
 * DEVS:DataTypes/JPEG and PNG (dist/mkdtype.py) name this class for files
 * starting with their bytes. The viewer's address: ENV:PanoramaDecoder
 * ("address" or "address:port"), else Panorama.card's last viewer, port
 * CU_DECODE_PORT.
 */
#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <utility/tagitem.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/datatypes.h>
#include <proto/bsdsocket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <clib/alib_protos.h>
#include <string.h>
#include "mc_status.h"
#include "copper.h"

#define DT_LIBSIZE 128
#define MAXFILE    (64UL << 20)
#define MAXSIDE    16384

struct DtBase {
	struct Library lib;
	BPTR seglist;
	Class *cl;
	struct Library *picbase;      /* picture.datatype, the superclass */
};
typedef char dt_libsize_check[sizeof(struct DtBase) <= DT_LIBSIZE ? 1 : -1];

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
struct IntuitionBase *IntuitionBase;
struct Library *UtilityBase;
struct Library *DataTypesBase;
extern char LibName[], LibId[];

/* ---------------------------------------------------------------- the decoding */

/* n bytes from the socket, all of them; FALSE if it ended first */
static BOOL recv_all(struct Library *SocketBase, LONG s, UBYTE *buf, ULONG n)
{
	while (n) {
		LONG got = recv(s, buf, n > 65536 ? 65536 : n, 0);

		if (got <= 0)
			return FALSE;
		buf += got;
		n -= got;
	}
	return TRUE;
}

static BOOL send_all(struct Library *SocketBase, LONG s, const UBYTE *buf, ULONG n)
{
	while (n) {
		LONG sent = send(s, (UBYTE *)buf, n > 65536 ? 65536 : n, 0);

		if (sent <= 0)
			return FALSE;
		buf += sent;
		n -= sent;
	}
	return TRUE;
}

/* "a.b.c.d" or "a.b.c.d:port" */
static BOOL parse_addr(const char *v, ULONG *addr, UWORD *port)
{
	ULONG a = 0, n = 0;
	int parts = 0, digits = 0;

	for (;; v++) {
		if (*v >= '0' && *v <= '9') {
			n = n * 10 + (*v - '0');
			digits++;
			if (n > 255)
				return FALSE;
		} else if (*v == '.' || *v == ':' || !*v) {
			if (!digits)
				return FALSE;
			a = (a << 8) | n;
			parts++;
			n = 0;
			digits = 0;
			if (*v != '.')
				break;
		} else {
			return FALSE;
		}
	}
	if (parts != 4)
		return FALSE;
	*addr = a;
	if (*v == ':') {
		ULONG p = 0;

		for (v++; *v >= '0' && *v <= '9'; v++)
			p = p * 10 + (*v - '0');
		if (!p || p > 65535 || *v)
			return FALSE;
		*port = p;
	}
	return TRUE;
}

/* the decoder: ENV:PanoramaDecoder, else the viewer Panorama.card last heard from */
static BOOL decoder(ULONG *addr, UWORD *port)
{
	struct McBase *card;
	char buf[64];

	*port = CU_DECODE_PORT;
	if (GetVar((STRPTR)"PanoramaDecoder", (STRPTR)buf, sizeof(buf), 0) > 0 &&
	    parse_addr(buf, addr, port))
		return TRUE;
	Forbid();
	card = (struct McBase *)FindName(&SysBase->LibList, "Panorama.card");
	*addr = card && card->status.magic == MC_STATUS_MAGIC ? card->status.viewer_addr : 0;
	Permit();
	return *addr != 0;
}

/*
 * The file's bytes to the viewer, its pixels into the object. A
 * SocketBase of this task's own: the dispatcher runs in whatever task
 * shows the picture.
 */
static BOOL decode(Object *o, UBYTE *data, ULONG size, struct BitMapHeader *bmhd, STRPTR name)
{
	struct Library *SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 3);
	struct sockaddr_in sa;
	UBYTE head[12], *rows = NULL;
	ULONG addr, rowbytes, band, y;
	UWORD port, w, h;
	LONG s = -1;
	BOOL ok = FALSE;

	if (!SocketBase) {
		SetIoErr(DTERROR_COULDNT_OPEN);
		return FALSE;
	}
	if (!decoder(&addr, &port)) {
		SetIoErr(DTERROR_COULDNT_OPEN);
		goto out;
	}
	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
		goto out;
	memset(&sa, 0, sizeof(sa));
	sa.sin_len = sizeof(sa);
	sa.sin_family = AF_INET;
	sa.sin_port = port;
	sa.sin_addr.s_addr = addr;
	if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0)
		goto out;
	{
		/* a slow viewer is one thing, a gone one another: a minute */
		struct timeval tv;

		tv.tv_secs = 60;
		tv.tv_micro = 0;
		setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	}
	memcpy(head, "CUDC", 4);
	head[4] = 1;
	head[5] = 0;
	head[6] = 0;
	head[7] = 0;
	head[8] = size >> 24;
	head[9] = size >> 16;
	head[10] = size >> 8;
	head[11] = size;
	if (!send_all(SocketBase, s, head, 12) || !send_all(SocketBase, s, data, size) ||
	    !recv_all(SocketBase, s, head, 12) || memcmp(head, "CUDR", 4) != 0 || head[4] != 0 ||
	    head[5] != 0) {
		SetIoErr(ERROR_OBJECT_WRONG_TYPE);
		goto out;
	}
	w = (head[8] << 8) | head[9];
	h = (head[10] << 8) | head[11];
	if (!w || !h || w > MAXSIDE || h > MAXSIDE) {
		SetIoErr(ERROR_OBJECT_WRONG_TYPE);
		goto out;
	}

	/* the picture: w x h of 24 bits, filled through picture.datatype V43 */
	bmhd->bmh_Width = bmhd->bmh_PageWidth = w;
	bmhd->bmh_Height = bmhd->bmh_PageHeight = h;
	bmhd->bmh_Depth = 24;
	bmhd->bmh_Masking = 0;
	bmhd->bmh_Compression = 0;
	bmhd->bmh_XAspect = bmhd->bmh_YAspect = 1;
	SetDTAttrs(o, NULL, NULL, DTA_ObjName, (ULONG)(name ? FilePart(name) : (STRPTR)"picture"),
	           DTA_NominalHoriz, w, DTA_NominalVert, h, PDTA_SourceMode, PMODE_V43, TAG_DONE);
	rowbytes = (ULONG)w * 3;
	band = 65536 / rowbytes;
	if (band < 1)
		band = 1;
	if (band > h)
		band = h;
	rows = AllocVec(band * rowbytes, MEMF_ANY);
	if (!rows) {
		SetIoErr(ERROR_NO_FREE_STORE);
		goto out;
	}
	for (y = 0; y < h; y += band) {
		ULONG n = h - y < band ? h - y : band;

		if (!recv_all(SocketBase, s, rows, n * rowbytes)) {
			SetIoErr(ERROR_OBJECT_WRONG_TYPE);
			goto out;
		}
		if (!DoMethod(o, PDTM_WRITEPIXELARRAY, (ULONG)rows, PBPAFMT_RGB, rowbytes, 0, y, w, n)) {
			SetIoErr(ERROR_NO_FREE_STORE);
			goto out;
		}
	}
	ok = TRUE;
out:
	if (rows)
		FreeVec(rows);
	if (s >= 0)
		CloseSocket(s);
	CloseLibrary(SocketBase);
	return ok;
}

/* OM_NEW, after the superclass: the file in, the picture made */
static BOOL load(Object *o)
{
	ULONG srctype = 0;
	BPTR fh = 0;
	STRPTR name = NULL;
	struct BitMapHeader *bmhd = NULL;
	UBYTE *data;
	LONG size;
	BOOL ok;

	GetDTAttrs(o, DTA_SourceType, (ULONG)&srctype, DTA_Handle, (ULONG)&fh, DTA_Name, (ULONG)&name,
	           PDTA_BitMapHeader, (ULONG)&bmhd, TAG_DONE);
	if (srctype != DTST_FILE || !fh || !bmhd) {
		SetIoErr(ERROR_OBJECT_WRONG_TYPE);
		return FALSE;
	}
	Seek(fh, 0, OFFSET_END);
	size = Seek(fh, 0, OFFSET_BEGINNING);
	if (size <= 0 || (ULONG)size > MAXFILE) {
		SetIoErr(ERROR_OBJECT_WRONG_TYPE);
		return FALSE;
	}
	data = AllocVec(size, MEMF_ANY);
	if (!data) {
		SetIoErr(ERROR_NO_FREE_STORE);
		return FALSE;
	}
	if (Read(fh, data, size) != size) {
		FreeVec(data);
		SetIoErr(DTERROR_COULDNT_OPEN);
		return FALSE;
	}
	ok = decode(o, data, size, bmhd, name);
	FreeVec(data);
	return ok;
}

static ULONG dispatch(__reg("a0") Class *cl, __reg("a2") Object *o, __reg("a1") Msg msg)
{
	if (msg->MethodID == OM_NEW) {
		Object *n = (Object *)DoSuperMethodA(cl, o, msg);

		if (n && !load(n)) {
			LONG err = IoErr();

			CoerceMethod(cl, n, OM_DISPOSE);
			SetIoErr(err);
			n = NULL;
		}
		return (ULONG)n;
	}
	return DoSuperMethodA(cl, o, msg);
}

/* ---------------------------------------------------------------- the library */

struct Library *LibInit(__reg("d0") struct DtBase *base, __reg("a0") BPTR seglist,
                        __reg("a6") struct ExecBase *sysbase)
{
	SysBase = sysbase;
	base->seglist = seglist;
	base->lib.lib_Node.ln_Type = NT_LIBRARY;
	base->lib.lib_Node.ln_Name = LibName;
	base->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
	base->lib.lib_Version = 1;
	base->lib.lib_Revision = 0;
	base->lib.lib_IdString = LibId;

	DOSBase = (struct DosLibrary *)OpenLibrary((STRPTR)"dos.library", 37);
	IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
	UtilityBase = OpenLibrary((STRPTR)"utility.library", 37);
	DataTypesBase = OpenLibrary((STRPTR)"datatypes.library", 39);
	if (!DOSBase || !IntuitionBase || !UtilityBase || !DataTypesBase) {
		if (DataTypesBase)
			CloseLibrary(DataTypesBase);
		if (UtilityBase)
			CloseLibrary(UtilityBase);
		if (IntuitionBase)
			CloseLibrary((struct Library *)IntuitionBase);
		if (DOSBase)
			CloseLibrary((struct Library *)DOSBase);
		FreeMem((UBYTE *)base - base->lib.lib_NegSize,
		        base->lib.lib_NegSize + base->lib.lib_PosSize);
		return NULL;
	}
	return &base->lib;
}

struct Library *LibOpen(__reg("a6") struct DtBase *base)
{
	base->lib.lib_OpenCnt++;
	base->lib.lib_Flags &= ~LIBF_DELEXP;
	if (!base->cl) {
		/* the class, once: picture.datatype V43 (Picasso96's, or newer) under it */
		base->picbase = OpenLibrary((STRPTR)"datatypes/picture.datatype", 43);
		if (base->picbase) {
			base->cl = MakeClass(NULL, (STRPTR)"picture.datatype", NULL, 4, 0);
			if (base->cl) {
				base->cl->cl_Dispatcher.h_Entry = (HOOKFUNC)dispatch;
				base->cl->cl_UserData = (ULONG)base;
			}
		}
		if (!base->cl) {
			if (base->picbase)
				CloseLibrary(base->picbase);
			base->picbase = NULL;
			base->lib.lib_OpenCnt--;
			return NULL;
		}
	}
	return &base->lib;
}

BPTR LibExpunge(__reg("a6") struct DtBase *base);

BPTR LibClose(__reg("a6") struct DtBase *base)
{
	base->lib.lib_OpenCnt--;
	if (!base->lib.lib_OpenCnt && (base->lib.lib_Flags & LIBF_DELEXP))
		return LibExpunge(base);
	return 0;
}

BPTR LibExpunge(__reg("a6") struct DtBase *base)
{
	BPTR seglist;

	if (base->lib.lib_OpenCnt) {
		base->lib.lib_Flags |= LIBF_DELEXP;
		return 0;
	}
	if (base->cl) {
		if (!FreeClass(base->cl)) {
			/* pictures of ours still about: not yet */
			base->lib.lib_Flags |= LIBF_DELEXP;
			return 0;
		}
		base->cl = NULL;
		CloseLibrary(base->picbase);
		base->picbase = NULL;
	}
	seglist = base->seglist;
	Remove(&base->lib.lib_Node);
	CloseLibrary(DataTypesBase);
	CloseLibrary(UtilityBase);
	CloseLibrary((struct Library *)IntuitionBase);
	CloseLibrary((struct Library *)DOSBase);
	FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
	return seglist;
}

Class *ObtainEngine(__reg("a6") struct DtBase *base)
{
	return base->cl;
}
