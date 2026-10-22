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
	xdef	_SetMemoryMode
	xdef	_LibNull
	xdef	_LibName
	xdef	_LibId

VERSION		equ	1
REVISION	equ	1
LIBSIZE		equ	80		; struct McBase, MC_LIBSIZE in mc_status.h
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

; SetMemoryMode(a0 bi, d7 RGBFormat): the memory is plain RAM with a single
; linear aperture, so there is nothing to switch. P96 requires that this
; function preserves all registers.
_SetMemoryMode:
	rts

_LibName:
	dc.b	"Multicast.card",0
_LibId:
	dc.b	"Multicast.card 1.1 (22.10.2026)",13,10,0
	even
