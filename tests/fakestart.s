; P96Multicast - Fake.card library header and its recording functions,
; for tests/wraptest.c (see tests/fakecard.h).

	section	code,code

	xref	_LibInit
	xref	_FindCard
	xref	_InitCard
	xdef	_FakeRecord
	xdef	_FakeKeep
	xdef	_FakeRecordRegs
	xdef	_FakeKeepRegs
	xdef	_FakeRecordCalls
	xdef	_FakeKeepCalls

Start:
	moveq	#-1,d0
	rts

RomTag:
	dc.w	$4afc
	dc.l	RomTag
	dc.l	EndSkip
	dc.b	$80			; RTF_AUTOINIT
	dc.b	1			; version
	dc.b	9			; NT_LIBRARY
	dc.b	0
	dc.l	LibName
	dc.l	LibId
	dc.l	InitTable
EndSkip:

InitTable:
	dc.l	80			; FAKE_LIBSIZE
	dc.l	FuncTable
	dc.l	0
	dc.l	_LibInit

FuncTable:
	dc.l	LibOpen
	dc.l	LibClose
	dc.l	LibNull
	dc.l	LibNull
	dc.l	_FindCard
	dc.l	_InitCard
	dc.l	-1

LibOpen:
	addq.w	#1,32(a6)		; lib_OpenCnt
	move.l	a6,d0
	rts

LibClose:
	subq.w	#1,32(a6)
LibNull:
	moveq	#0,d0
	rts

; the BoardInfo functions: record all registers, then return
_FakeRecord:
	movem.l	d0-d7/a0-a6,_FakeRecordRegs
	addq.l	#1,_FakeRecordCalls
	move.l	#$600DF00D,d0
	rts

_FakeKeep:
	movem.l	d0-d7/a0-a6,_FakeKeepRegs
	addq.l	#1,_FakeKeepCalls
	rts

LibName:
	dc.b	"Fake.card",0
LibId:
	dc.b	"Fake.card 1.0 (26.10.2026)",13,10,0
	even

	section	bss,bss
_FakeRecordRegs:
	ds.l	15
_FakeKeepRegs:
	ds.l	15
_FakeRecordCalls:
	ds.l	1
_FakeKeepCalls:
	ds.l	1
