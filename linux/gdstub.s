; gdstub.s - zeST Gemdos host stub program
;
; Copyright (c) 2025-2026 Francois Galea <fgalea at free.fr>
; This program is free software: you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation, either version 3 of the License, or
; (at your option) any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program.  If not, see <https://www.gnu.org/licenses/>.

	opt	o+

DMABUFSZ	equ	5	; DMA buffer size in sectors

OP_GEMDOS	equ	1	; new GEMDOS call
OP_ACTION	equ	2	; get next action to perform
OP_RESULT	equ	3	; send result

	section	text

begin:
	bra.w	gemdos_install	; Gemdos entry point
	bra.w	bootsec_install	; Bootsector entry point

; Send command
; a0: command block address
; d1: transfer size in sectors (0: do not set up DMA)
; d2: command size in bytes
; d3: transfer direction 0:read / 1:write
; return:
; d0: -1 if error or status
send_command:
	movem.l	d4-d5/a6,-(sp)

	lsl	#8,d3		; read(0) / write($100)

	lea	$ffff8604.w,a6	; DMA data / control

	move	d1,d5		; transfer size
	beq.s	scnodma		; if size=0, don't set up DMA
	move	#$190,d4	; DMA sector count register
	eor	d3,d4		; write bit flipped wrt. required transfer direction
	move	d4,2(a6)
	bchg	#8,d4		; write bit set to correct direction
	move	d4,2(a6)
	move	d5,(a6)		; set sector count
scnodma:
	move	d3,d0		; direction bit
	move.b	#$88,d0		; first command byte: DRQ:internal, CS:external, A1=0
	move	d0,2(a6)	; first DMA control value
	move.l	d3,d1		; direction bit
	move.b	#$8a,d1		; next command: DRQ:internal, CS:external, A1=1
	subq	#2,d2		; send the first 5 bytes
	swap	d1
	move.b	(a0)+,d1	; first command byte
	cmp.b	#$1f,d1		; SCSI command with id ≥ 0x1f?
	bcs.s	scsreg
	move.b	#$1f,d1		; ICD extended command
	subq.l	#1,a0		; actual command byte will be sent next
	addq	#1,d2		; one more byte to send
scsreg:	or.b	drive_id(pc),d1	; add drive id
	bra.s	scss
scsb:	swap	d1
	move.b	(a0)+,d1	; next command byte
scss:	swap	d1
	move.l	d1,(a6)		; command byte + next DMA control value
	bsr	irqwait50
	bmi.s	scend		; timeout: error
	dbra	d2,scsb

	move.b	#$00,d1		; DRQ:external, CS:internal, A1=0
	swap	d1
	move.b	(a0)+,d1	; final command byte
	swap	d1
	move.l	d1,(a6)		; command byte + next DMA control value
	tst	d5
	beq.s	scnodma2
	bsr	irqwait3000
	bmi.s	scend		; timeout: error
	bra.s	scgetstatus
scnodma2:
	bsr	irqwait50
	bmi.s	scend		; timeout: error
scgetstatus:
	move	#$8a,2(a6)	; status register
	moveq	#0,d0
	move	(a6),d0		; get status
scend:
	movem.l	(sp)+,d4-d5/a6
	rts

; wait for IRQ, 3 s timeout
irqwait3000:
	move.l	#600,d0		; 3000 ms
	bra.s	irqwait
; wait for IRQ, 50 ms timeout
irqwait50:
	moveq.l	#10,d0		; 50 ms
irqwait:
	add.l	$4ba.w,d0	; _hz_200
iwlp:	btst	#5,$fffffa01.w
	beq.s	iwac
	cmp.l	$4ba.w,d0	; timeout?
	bpl.s	iwlp
	moveq	#-1,d0		; timeout
	rts
iwac:	moveq	#0,d0
	rts

; set DMA pointer
set_dma_ptr:
	move.l	d0,-(sp)
	move.l	a0,d0
	move.b	d0,$ffff860d.w	; DMA pointer, low byte
	lsr	#8,d0
	move.b	d0,$ffff860b.w	; DMA pointer, mid byte
	swap	d0
	move.b	d0,$ffff8609.w	; DMA pointer, high byte
	move.l	(sp)+,d0
	rts

	dc.l	'XBRA','zeST',0
gemdos:
	move	usp,a0		; user call stack
	btst.b	#5,(sp)		; called from supervisor mode?
	beq.s	gdusr
	lea	6(sp),a0	; supervisor call stack
	tst	$59e.w		; _longframe
	beq.s	gdusr
	addq.l	#2,a0		; two additional bytes on 030+
gdusr:
	lea	resblk(pc),a1
	move	(a0),d0		; GEMDOS opcode
	rept	4
	move.l	(a0)+,(a1)+
	endr
	bsr	manage_command
	tst	d0		; fallback or return ?
	beq.s	gdfb		; 0: fallback
	move.l	d1,d0		; return value
	rte
gdfb:
	move.l	gemdos-4(pc),a0
	jmp	(a0)


; d0: GEMDOS code
manage_command:
	movem.l	d2-d3/a2,-(sp)
	lea	resblk(pc),a0
	st	$43e.w		; flock
	bsr	set_dma_ptr
	lea	cdb(pc),a0
	move.w	#$1100+OP_GEMDOS,(a0)	; SPACE(6) header + GEMDOS call code
	move.w	d0,2(a0)	; GEMDOS opcode
	move.w	#'zS',4(a0)	; command tag
	moveq	#1,d1		; sector count
	moveq	#6,d2		; command size
	moveq	#1,d3		; DMA direction: write
	bsr	send_command
	sf	$43e.w		; flock

	tst	d0		; status code
	beq	endcmd		; STATUS_OK: fall back to TOS code

; Now we are in action mode
action_loop:
	lea	resblk(pc),a0
	st	$43e.w		; flock
	bsr	set_dma_ptr
	lea	cdb(pc),a0
	move.w	#$1100+OP_ACTION,(a0)	; SPACE(6) header + ACTION call code
	move.l	#'zeST',2(a0)	; command tag
	moveq	#DMABUFSZ,d1	; sector count
	moveq	#6,d2		; command size
	moveq	#0,d3		; DMA direction: read
	bsr	send_command
	sf	$43e.w		; flock

	lea	resblk(pc),a0
	move.l	a0,a1
	move	(a0)+,d0	; Action code
	add	d0,d0
	move	jtbl(pc,d0.w),d0
	jmp	jtbl(pc,d0.w)

jtbl:	dc.w	action_fallback-jtbl	; Fall back to TOS code
	dc.w	action_return-jtbl	; Return from GEMDOS
	dc.w	action_rdmem-jtbl	; Read from memory
	dc.w	action_wrmem-jtbl	; Write to memory
	dc.w	action_wrmem0-jtbl	; Write to memory then return 0
	dc.w	action_gemdos-jtbl	; GEMDOS call
	dc.w	action_patch-jtbl	; patch the stack and fall back

action_gemdos:
; Call GEMDOS
	move.l	sp,cdb
	move	(a0)+,d0	; size of data to put on stack
	suba.w	d0,sp		; allocate required stack space
	move.l	sp,a1
	lsr	#2,d0		; number of longs
	bcc.s	gdccl0		; odd number of words?
	move.w	(a0)+,(a1)+	; copy single word
gdccl0:	subq.w	#1,d0		; number of longs-1
	bcs.s	gdccl2
gdccl1:	move.l	(a0)+,(a1)+
	dbra	d0,gdccl1
gdccl2:
	tst	$59e.w		; _longframe
	beq.s	gdcnlf
	clr	-(sp)		; two additional bytes on 030+
gdcnlf:	pea	gdcret(pc)	; return address
	move	sr,-(sp)
	move.l	gemdos-4(pc),a0
	jmp	(a0)
gdcret:	move.l	cdb(pc),sp	; restore stack pointer
	lea	resblk(pc),a1
	move.l	d0,(a1)+
	bra	send_result

action_rdmem:
; Read from memory
	move.l	(a0)+,a2	; address to read from
	move	(a0),d0		; how many bytes (0 if string)
	beq.s	read_string
	move.l	a2,d1
	btst	#0,d1		; odd address?
	bne.s	rdmlpb

	move	d0,d1
	and	#3,d0		; remaining bytes
	lsr	#2,d1		; number of longs
	subq	#1,d1		; number of longs-1
	bcs.s	rdmlpb		; no longs
	moveq	#7,d2
	and	d1,d2		; 0-7
	eor	#7,d2		; 7-0
	lsr	#3,d1		; (number of longs-1)/8
	add	d2,d2
	jmp	rdmlp(pc,d2.w)
rdmlp:
	rept	8
	move.l	(a2)+,(a1)+	; copy data
	endr
	dbra	d1,rdmlp
; Remaining bytes
rdmlpb:
	subq	#1,d0		; byte counter
	bmi.s	send_result
rdmblp:
	move.b	(a2)+,(a1)+	; copy bytes
	dbra	d0,rdmblp
	bra.s	send_result

read_string:
	move.b	(a2)+,(a1)+	; copy string
	bne.s	read_string
	bra.s	send_result

action_wrmem:
	bsr	write_mem
	bra	action_loop

action_wrmem0:
	bsr	write_mem
	moveq	#0,d1
	bra	endcmd

action_patch:
	move	usp,a1		; user call stack
	btst.b	#5,16(sp)	; called from supervisor mode?
	beq.s	apusr
	lea	16+6(sp),a1	; supervisor call stack
	tst	$59e.w		; _longframe
	beq.s	apusr
	addq.l	#2,a1		; two additional bytes on 030+
apusr:	move	(a0)+,d0	; size of data
	lsr	#2,d0		; number of longs
	bcc.s	apl0		; odd number of words?
	move.w	(a0)+,(a1)+	; copy single word
apl0:	subq.w	#1,d0		; number of longs-1
	bcs.s	apl2
apl1:	move.l	(a0)+,(a1)+
	dbra	d0,apl1
apl2:


action_fallback:
	moveq	#0,d0
	bra	endcmd

action_return:
	move.l	(a0),d1		; return value
	bra	endcmd

endcmd:
	movem.l	(sp)+,d2-d3/a2
	rts

send_result:
	lea	resblk(pc),a0
	sub.l	a0,a1		; number of bytes to be sent
	st	$43e.w		; flock
	bsr	set_dma_ptr
	lea	cdb(pc),a0
	move.w	#$1100+OP_RESULT,(a0)	; SPACE(6) header + RESULT call code
	move.w	a1,2(a0)	; number of data bytes
	move.w	#'zS',4(a0)	; command tag
	moveq	#DMABUFSZ,d1	; number of sectors
	moveq	#6,d2		; command size
	moveq	#1,d3		; DMA direction: write
	bsr	send_command
	sf	$43e.w		; flock

	bra	action_loop

write_mem:
; Write to memory
	move.l	(a0)+,a2	; address to write to
	move	(a0)+,d0	; how many bytes
	beq.s	wrmend
	move.l	a2,d1
	btst	#0,d1		; odd address?
	bne.s	wrmlpb

	move	d0,d1
	and	#3,d0		; remaining bytes
	lsr	#2,d1		; number of longs
	subq	#1,d1		; number of longs-1
	bcs.s	wrmlpb		; no longs
	moveq	#7,d2
	and	d1,d2		; 0-7
	eor	#7,d2		; 7-0
	lsr	#3,d1		; (number of longs-1)/8
	add	d2,d2
	jmp	wrmlp(pc,d2.w)
wrmlp:
	rept	8
	move.l	(a0)+,(a2)+	; copy data
	endr
	dbra	d1,wrmlp
; Remaining bytes
wrmlpb:
	subq	#1,d0		; byte counter
	bcs.s	wrmend
wrmblp:
	move.b	(a0)+,(a2)+	; copy bytes
	dbra	d0,wrmblp
wrmend:
	rts

drive_id:	dc.b	0	; ACSI drive ID << 5
	even
cdb:		dcb.b	10,0
resblk:

end_resident:

; Test unit
; d0: -1 if error, status otherwise
testunit:
	st	$43e.w		; flock
	lea	testunit_cmd(pc),a0
	moveq	#0,d1		; no sectors transferred
	moveq	#6,d2		; command size
	moveq	#0,d3		; DMA direction: read
	bsr	send_command
	sf	$43e.w		; flock
	rts

; Device inquiry
; d0(o): status
; a0(o): device name
inquiry:
	st	$43e.w		; flock
	lea	rblk(pc),a0
	bsr	set_dma_ptr
	lea	inquiry_cmd(pc),a0
	moveq	#1,d1		; one sector transferred
	moveq	#6,d2		; command size
	moveq	#0,d3		; DMA direction: read
	bsr	send_command
	sf	$43e.w		; flock

	sf	rblk+32
	lea	rblk+8(pc),a0
	rts

; Installation from bootsector
bootsec_install:
	movem.l	d3-d7/a3-a6,-(sp)
	move.b	d7,drive_id

	lea	hello_txt(pc),a0
	bsr	text_print

	bsr	install_device

	movem.l	(sp)+,d3-d7/a3-a6
	move.l	#end_resident-begin+28+512*DMABUFSZ,d0	; resident size
	rts


; Installation from GEMDOS
gemdos_install:
	lea	hello_txt(pc),a0
	bsr	text_print

	move	#$7a53,-(sp)	; 'zS' zeST stub detection
	trap	#1
	addq.l	#2,sp
	lea	already_txt(pc),a0
	tst	d0
	beq.s	noinstall

	pea	dev_detect(pc)
	move	#38,-(sp)	; Supexec
	trap	#14
	addq.l	#6,sp

	tst	d0
	bne.s	noinstall

	clr	-(sp)
	move.l	#end_resident-begin+256+512*DMABUFSZ,-(sp)
	move	#$31,-(sp)	; Ptermres
	trap	#1

; exit without installing
noinstall:
	bsr	text_print
	clr	-(sp)
	trap	#1

; scan all ACSI devices
dev_detect:
	moveq	#0,d7		; device ID<<5
acsi_scan_lp:
	move.b	d7,drive_id
	bsr	install_device
	beq.s	acsi_scan_end
	add.b	#$20,d7		; next ID
	bne.s	acsi_scan_lp
	lea	failed_txt(pc),a0
	moveq	#1,d0		; not found
acsi_scan_end:
	rts

; test device and set it up
install_device:
	bsr	testunit
	bmi.s	unit_notfound

	bsr	inquiry
	bne.s	unit_notfound
	cmp.b	#$a,rblk	; device type ($a: obsolete type id)
	bne.s	unit_notfound

	move.l	$84.w,gemdos-4
	move.l	#gemdos,$84.w

	moveq	#-1,d0
	lea	resblk(pc),a0
	move.l	#begin,(a0)+
	move.l	#resblk,(a0)+
	bsr	manage_command	; call host to init new drive

	moveq	#0,d0
	rts

unit_notfound:
	moveq	#1,d0
	rts

text_print:
	move.l	a0,-(sp)
	move	#9,-(sp)		; Cconws
	trap	#1
	addq.l	#6,sp
	rts



	section data
hello_txt:	dc.b	13,10
		dc.b	27,"p- zeST GEMDOS stub -",27,"q",13,10
		dc.b	$bd," 2025-2026 Fran",$87,"ois Galea",13,10,0
already_txt:	dc.b	"Driver already installed",13,10,0
failed_txt:	dc.b	"Driver not installed",13,10,0

	even
testunit_cmd:	dc.b	0,0,0,0,0,0
;capacity_cmd:	dc.b	$25,0,0,0,0,0,0,0,0,0
inquiry_cmd:	dc.b	$12,0,0,0,48,0

	section	bss
rblk:		ds.b	512
