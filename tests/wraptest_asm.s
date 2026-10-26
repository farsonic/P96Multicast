; P96Multicast - register-level helpers for tests/wraptest.c

	section	code,code

	xdef	_call_regs
	xdef	_RtgDefault

; void call_regs(a0 fn, a1 in, a2 out): load d0-d7/a0-a6 from in[0..14],
; call fn, store d0-d7/a0-a6 as fn left them in out[0..14].
_call_regs:
	movem.l	d2-d7/a2-a6,-(sp)
	move.l	a2,-(sp)		; out
	move.l	a0,-(sp)		; fn
	movem.l	(a1),d0-d7/a0-a6
	jsr	([sp])
	move.l	a6,-(sp)		; sp: a6, fn, out
	move.l	8(sp),a6
	movem.l	d0-d7/a0-a5,(a6)
	move.l	(sp)+,56(a6)
	addq.l	#8,sp
	movem.l	(sp)+,d2-d7/a2-a6
	rts

; stands in for rtg.library's fallbacks, as AROS fills every slot with one
_RtgDefault:
	moveq	#0,d0
	rts
