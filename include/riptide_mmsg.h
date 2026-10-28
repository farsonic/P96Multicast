#ifndef RIPTIDE_MMSG_H
#define RIPTIDE_MMSG_H
/*
 * Panorama - many datagrams per call with Riptide (0.17 and later).
 *
 * riptide.library version 3 has Linux's sendmmsg()/recvmmsg():
 * RiptideSendMMsg() at LVO -534 and RiptideRecvMMsg() at -540. They work
 * on the bsdsocket.library sockets of the task that opened riptide.library.
 * Only the interface is here (the layout is Linux's struct mmsghdr), so
 * the card builds without Riptide's SDK and runs with any stack: it uses
 * them only when riptide.library 3 is running.
 */
#include <exec/types.h>

#define RIPTIDE_MMSG_LIB     "riptide.library"
#define RIPTIDE_MMSG_VERSION 3

struct RtMMsgHdr {
	APTR  name;          /* msg_name: struct sockaddr * or NULL */
	ULONG namelen;
	APTR  iov;           /* msg_iov: struct iovec * */
	ULONG iovlen;
	APTR  control;       /* not used */
	ULONG controllen;
	LONG  flags;         /* msg_flags: MSG_TRUNC ... when receiving */
	ULONG len;           /* out: bytes sent / received */
};

/* returns how many were sent / received, or -1 (Errno()) when the first failed */
LONG __RtSendMMsg(__reg("a6") struct Library *, __reg("d0") LONG fd, __reg("a0") APTR vec,
                  __reg("d1") ULONG vlen, __reg("d2") ULONG flags) = "\tjsr\t-534(a6)";
LONG __RtRecvMMsg(__reg("a6") struct Library *, __reg("d0") LONG fd, __reg("a0") APTR vec,
                  __reg("d1") ULONG vlen, __reg("d2") ULONG flags) = "\tjsr\t-540(a6)";

#endif
