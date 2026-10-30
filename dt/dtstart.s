; Panorama - panoramapic.datatype library header.
;
; A DataTypes class library is an ordinary Amiga library with one extra
; entry, ObtainEngine at -30, which returns the BOOPSI class.

	section	code,code

	xref	_LibInit
	xref	_LibOpen
	xref	_LibClose
	xref	_LibExpunge
	xref	_ObtainEngine
	xdef	_LibNull
	xdef	_LibName
	xdef	_LibId

VERSION		equ	1
REVISION	equ	0
LIBSIZE		equ	128		; struct DtBase, DT_LIBSIZE in panoramapic.c
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
	dc.l	_ObtainEngine		; -30
	dc.l	-1

_LibNull:
	moveq	#0,d0
	rts

_LibName:
	dc.b	"panoramapic.datatype",0
_LibId:
	dc.b	"panoramapic.datatype 1.0 (29.10.2026)",13,10,0
	even
