; P96Multicast - Wrap.card library header, thunks and calls into the
; wrapped card.
;
; Wrap.card is a P96 card driver that loads another card driver and sits
; between it and rtg.library. Its FindCard and InitCard call the wrapped
; card's, then replace each BoardInfo function pointer the card filled in
; with a thunk that calls _wrap_pre (wrap.c) and then the card's function.

	section	code,code

	xref	_LibInit
	xref	_LibOpen
	xref	_LibClose
	xref	_LibExpunge
	xref	_FindCard
	xref	_InitCard
	xref	_wrap_pre
	xdef	_LibNull
	xdef	_LibName
	xdef	_LibId
	xdef	_InnerFindCard
	xdef	_InnerInitCard
	xdef	_WrapThunks
	xdef	_WrapThunksEnd

VERSION		equ	0
REVISION	equ	4
LIBSIZE		equ	128		; struct WrapBase, WRAP_LIBSIZE in wrap_status.h
NSLOTS		equ	68		; WRAP_NSLOTS
RTF_AUTOINIT	equ	$80
NT_LIBRARY	equ	9

; not runnable: return an error if someone tries
Start:
	moveq	#-1,d0
	rts

RomTag:
	dc.w	$4afc			; RTC_MATCHWORD
	dc.l	RomTag
	dc.l	EndSkip
	dc.b	RTF_AUTOINIT
	dc.b	VERSION
	dc.b	NT_LIBRARY
	dc.b	0			; priority
	dc.l	_LibName
	dc.l	_LibId
	dc.l	InitTable
EndSkip:

InitTable:
	dc.l	LIBSIZE
	dc.l	FuncTable
	dc.l	0			; no data table: LibInit fills the base
	dc.l	_LibInit

FuncTable:
	dc.l	_LibOpen
	dc.l	_LibClose
	dc.l	_LibExpunge
	dc.l	_LibNull
	dc.l	_FindCard		; -30
	dc.l	_InitCard		; -36
	dc.l	-1

_LibNull:
	moveq	#0,d0
	rts

; BOOL InnerFindCard(a0 bi, a1 card base): the wrapped card's FindCard,
; called as a library function.
_InnerFindCard:
	move.l	a6,-(sp)
	move.l	a1,a6
	jsr	-30(a6)
	move.l	(sp)+,a6
	rts

; BOOL InnerInitCard(a0 bi, a1 tooltypes, a2 card base)
_InnerInitCard:
	move.l	a6,-(sp)
	move.l	a2,a6
	jsr	-36(a6)
	move.l	(sp)+,a6
	rts

; One thunk per BoardInfo function slot, 8 bytes each: wrap.c finds slot n's
; at _WrapThunks + 8 * n, and checks the table's size.
;
; BoardInfo functions take all their arguments in registers, with the
; BoardInfo in a0, and some (SetMemoryMode, SetWriteMask, SetClearMask,
; SetReadPlane) must preserve every register; they may be called from
; interrupts. So the thunk saves everything, lets _wrap_pre look at the
; arguments, restores everything and calls the card's function with a6 set
; to the card's library base: AROS's p96gfx.hidd calls card functions with
; a6 = the library it opened, which here is Wrap.card.
;
; _wrap_pre(a0 bi, d0 slot, a1 saved registers) returns the card's function
; in d0 and stores the card's base in the saved a6.
_WrapThunks:
slot	set	0
	rept	NSLOTS
	pea	(slot).w
	bra.w	thunk_common
slot	set	slot+1
	endr
_WrapThunksEnd:

thunk_common:
	move.l	a6,-(sp)		; sp: caller's a6, slot, return address
	movem.l	d0-d7/a0-a6,-(sp)	; 60 bytes
	move.l	sp,a1
	move.l	64(sp),d0
	jsr	_wrap_pre
	move.l	d0,64(sp)		; the slot number becomes the function
	movem.l	(sp)+,d0-d7/a0-a6
	jsr	([4,sp])
	move.l	(sp)+,a6		; neither touches the condition codes
	addq.l	#4,sp
	rts

_LibName:
	dc.b	"Wrap.card",0
_LibId:
	dc.b	"Wrap.card 0.4 (26.10.2026)",13,10,0
	even
