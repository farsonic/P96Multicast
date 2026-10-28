; P96Multicast - Multicast.card library header.
;
; A P96 card driver is an ordinary Amiga library with two extra entries:
; FindCard at -30 and InitCard at -36. The rest of the driver is reached
; through function pointers in the BoardInfo structure.

	section	code,code

	xref	_LibInit
	xref	_LibOpen
	xref	_LibClose
	xref	_LibExpunge
	xref	_FindCard
	xref	_InitCard
	xref	_mc_damage
	xref	_McRecord
	xref	_McDamage
	xref	_McPointer
	xdef	_DmgStubs
	xdef	_SetMemoryMode
	xdef	_LibNull
	xdef	_LibName
	xdef	_LibId

VERSION		equ	2
REVISION	equ	8
LIBSIZE		equ	512		; struct McBase, MC_LIBSIZE in mc_status.h
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
	dc.l	_McRecord		; -42, for Wrap.card (orders.c)
	dc.l	_McDamage		; -48
	dc.l	_McPointer		; -54
	dc.l	-1

_LibNull:
	moveq	#0,d0
	rts

; SetMemoryMode(a0 bi, d7 RGBFormat): the memory is plain RAM with a single
; linear aperture, so there is nothing to switch. P96 requires that this
; function preserves all registers.
_SetMemoryMode:
	rts

; BLITTER=1 (blit.c): one stub per drawing function, 8 bytes each, in the
; order of MC_BLIT_* in mc_status.h. A stub saves every register, lets
; _mc_damage(a0 bi, d0 kind, a1 saved registers) record the rectangle, and
; goes on to the rtg.library function _mc_damage returns, with every
; register as the caller passed it and the caller's return address.
NBLIT		equ	9		; MC_BLIT_KINDS

_DmgStubs:
kind	set	0
	rept	NBLIT
	pea	(kind).w
	bra.w	dmg_common
kind	set	kind+1
	endr

dmg_common:
	movem.l	d0-d7/a0-a6,-(sp)	; 60 bytes, above them the kind
	move.l	sp,a1
	move.l	60(sp),d0
	jsr	_mc_damage
	move.l	d0,60(sp)		; the kind becomes where to go
	movem.l	(sp)+,d0-d7/a0-a6
	rts

_LibName:
	dc.b	"Multicast.card",0
_LibId:
	dc.b	"Multicast.card 2.8 (28.10.2026)",13,10,0
	even
