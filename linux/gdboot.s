; gdboot.s - zeST Gemdos host bootloader program
;
; Copyright (c) 2025 Francois Galea <fgalea at free.fr>
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

PRG_SZ	equ	3		; driver program size in sectors

	section	text

cdb:
begin:
	lea	drive_id(pc),a0
	move.b	d7,(a0)		; current ACSI ID << 5

	bsr	testunit
	bmi.s	unit_notfound

	move.l	#$10000,-(sp)	; allocate a large enough buffer
	move	#72,-(sp)	; Malloc
	trap	#1
	addq.l	#6,sp
	move.l	d0,a6

	move.l	a6,a0
	moveq	#1,d1		; sector index
	moveq	#PRG_SZ,d2	; sector count
	bsr	read

; relocate the code from specified program
	lea	28(a6),a1	; skip header
	move.l	a1,a2		; destination
	move.l	a1,d1		; base address
	add.l	2(a6),a1	; +text size
	add.l	6(a6),a1	; +data size
	add.l	14(a6),a1	; +symbols size = relocation table address

	move.l	(a1)+,d0	; offset
	beq.s	reloc_end	; no relocation info: code is position-independent

reloc_next:
	add.l	d0,a2		; relocation address
	add.l	d1,(a2)		; relocate
	moveq	#0,d0
reloc_byte:
	move.b	(a1)+,d0	; offset
	beq.s	reloc_end	; if zero, we're finished
	cmp.b	#1,d0		; special value for +254
	bne.s	reloc_next
	add.w	#254,a2		; update address
	bra.s	reloc_byte
reloc_end:

	jsr	32(a6)		; run program using the bootsector entry point
	move.l	d0,-(sp)	; resident size
	move.l	a6,-(sp)	; block address
	clr	-(sp)
	move	#74,-(sp)	; Mshrink
	trap	#1
	lea	12(sp),sp

unit_notfound:
	rts

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

; Read sector(s)
; a0(i): read address
; d1(i): sector index
; d2(i): sector count
; d0(o): -1 if error or status
read:
	st	$43e.w		; flock
	bsr	set_dma_ptr
	lea	cdb(pc),a0
	move.l	d1,(a0)+
	move.b	d2,(a0)+
	clr.b	(a0)+
	subq.l	#6,a0
	move.b	#8,(a0)
	move	d2,d1		; sector count
	moveq	#6,d2		; command size
	moveq	#0,d3		; DMA direction: read
	bsr	send_command
	sf	$43e.w		; flock
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
	move.l	next_time(pc),d4
sclp:	cmp.l	$4ba.w,d4	; _hz_200
	bpl.s	sclp

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
	moveq	#0,d4
	move.b	drive_id(pc),d4	; move id bits to 7-5

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
scsreg:	or.b	d4,d1		; add drive id
	bra.s	scss
scsb:	swap	d1
	move.b	(a0)+,d1	; next command byte
scss:	swap	d1
	move.l	d1,(a6)		; command byte + next DMA control value
	bsr	irqwait5
	bmi.s	sctimeout	; timeout: error
	dbra	d2,scsb

	move.b	#$00,d1		; DRQ:external, CS:internal, A1=0
	swap	d1
	move.b	(a0)+,d1	; final command byte
	swap	d1
	move.l	d1,(a6)		; command byte + next DMA control value
	tst	d5
	beq.s	scnodma2
	bsr	irqwait3000
	bmi.s	sctimeout	; timeout: error
	bra.s	scgetstatus
scnodma2:
	bsr	irqwait5
	bmi.s	sctimeout	; timeout: error
scgetstatus:
	move	#$8a,2(a6)	; status register
	moveq	#0,d0
	move	(a6),d0		; get status

scend:
	move.l	$4ba.w,d1
	addq.l	#1,d1
	lea	next_time(pc),a0
	move.l	d1,(a0)
	movem.l	(sp)+,d4-d5/a6
	tst	d0
	rts
sctimeout:
	moveq	#-1,d0
	bra.s	scend


; wait for IRQ, 3 s timeout
irqwait3000:
	move.l	#600,d0		; 3000 ms
	bra.s	irqwait
; wait for IRQ, 5 ms timeout
irqwait5:
	moveq.l	#1,d0		; 5 ms
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

drive_id:	dc.b	0		; ACSI drive ID << 5
	even
next_time:	dc.l	0


;	section data
	even
testunit_cmd:	dc.b	0,0,0,0,0,0

;pad:		dcb.b	98,0

; Offset $1C2
;disk_size:	dc.l	$8	; just the necessary number of sectors to store the stub program
;partitions:	dcb.b	4*12,0	; partitions list
;badsecs_offset:	dc.l	0
;badsecs_cnt:	dc.l	0
;checksum:	dc.w	0
