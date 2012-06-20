; YUV-> RGB conversion code.
;
; Copyright (C) 2011 Robin Watts (robin at wss.co.uk) for Pinknoise
; Productions Ltd.
;
; Licensed under the BSD license. See 'COPYING' for details of
; (non-)warranty.
;
;
; The algorithm used here is based heavily on one created by Sophie Wilson
; of Acorn/e-14/Broadcomm. Many thanks.
;
; Additional tweaks (in the fast fixup code) are from Paul Gardiner.
;
; The old implementation of YUV -> RGB did:
;
; R = CLAMP((Y-16)*1.164 +           1.596*V)
; G = CLAMP((Y-16)*1.164 - 0.391*U - 0.813*V)
; B = CLAMP((Y-16)*1.164 + 2.018*U          )
;
; We're going to bend that here as follows:
;
; R = CLAMP(y +           1.596*V)
; G = CLAMP(y - 0.383*U - 0.813*V)
; B = CLAMP(y + 1.976*U          )
;
; where y = 0               for       Y <=  16,
;       y = (  Y-16)*1.164, for  16 < Y <= 239,
;       y = (239-16)*1.164, for 239 < Y
;
; i.e. We clamp Y to the 16 to 239 range (which it is supposed to be in
; anyway). We then pick the B_U factor so that B never exceeds 511. We then
; shrink the G_U factor in line with that to avoid a colour shift as much as
; possible.
;
; We're going to use tables to do it faster, but rather than doing it using
; 5 tables as as the above suggests, we're going to do it using just 3.
;
; We do this by working in parallel within a 32 bit word, and using one
; table each for Y U and V.
;
; Source Y values are    0 to 255, so    0.. 260 after scaling
; Source U values are -128 to 127, so  -49.. 49(G), -253..251(B) after
; Source V values are -128 to 127, so -204..203(R), -104..103(G) after
;
; So total summed values:
; -223 <= R <= 481, -173 <= G <= 431, -253 <= B < 511
;
; We need to pack R G and B into a 32 bit word, and because of Bs range we
; need 2 bits above the valid range of B to detect overflow, and another one
; to detect the sense of the overflow. We therefore adopt the following
; representation:
;
; osGGGGGgggggosBBBBBbbbosRRRRRrrr
;
; Each such word breaks down into 3 ranges.
;
; osGGGGGggggg   osBBBBBbbb   osRRRRRrrr
;
; Thus we have 8 bits for each B and R table entry, and 10 bits for G (good
; as G is the most noticable one). The s bit for each represents the sign,
; and o represents the overflow.
;
; For R and B we pack the table by taking the 11 bit representation of their
; values, and toggling bit 10 in the U and V tables.
;
; For the green case we calculate 4*G (thus effectively using 10 bits for the
; valid range) truncate to 12 bits. We toggle bit 11 in the Y table.

; Theorarm library
; Copyright (C) 2009 Robin Watts for Pinknoise Productions Ltd

	AREA	|.text|, CODE, READONLY

	EXPORT	yuv420_2_rgb565
	EXPORT	yuv420_2_rgb565_PROFILE
	EXPORT	yuv2rgb_table

; void yuv420_2_rgb565
;  uint8_t *dst_ptr
;  uint8_t *y_ptr
;  uint8_t *u_ptr
;  uint8_t *v_ptr
;  int      width
;  int      height
;  int      y_span
;  int      uv_span
;  int      dst_span
;  int     *tables
;  int      dither

DITH1	*	7
DITH2	*	6

yuv420_2_rgb565_PROFILE		; Symbol exposed for profiling purposes
CONST_mask
	DCD	0x07E0F81F
CONST_flags
	DCD	0x40080100
yuv420_2_rgb565
	; r0 = dst_ptr
	; r1 = y_ptr
	; r2 = u_ptr
	; r3 = v_ptr
	; <> = width
	; <> = height
	; <> = y_span
	; <> = uv_span
	; <> = dst_span
	; <> = y_table
	; <> = dither
	STMFD	r13!,{r4-r11,r14}

	LDR	r8, [r13,#10*4]		; r8 = height
	LDR	r10,[r13,#11*4]		; r10= y_span
	LDR	r9, [r13,#13*4]		; r9 = dst_span
	LDR	r14,[r13,#14*4]		; r14= y_table
	LDR	r11,[r13,#15*4]		; r11= dither
	LDR	r4, CONST_mask
	LDR	r5, CONST_flags
	ANDS	r11,r11,#3
	BEQ	asm0
	CMP	r11, #2
	BEQ	asm3
	BGT	asm2
asm1
	;  Dither: 1 2
	;          3 0
	LDR	r11,[r13,#9*4]		; r11= width
	SUBS	r8, r8, #1
	BLT	end
	BEQ	trail_row1
yloop1
	SUB	r8, r8, r11,LSL #16	; r8 = height-(width<<16)
	ADDS	r8, r8, #1<<16		; if (width == 1)
	BGE	trail_pair1		;    just do 1 column
xloop1
	LDRB	r11,[r2], #1		; r11 = u  = *u_ptr++
	LDRB	r12,[r3], #1		; r12 = v  = *v_ptr++
	LDRB	r7, [r1, r10]		; r7  = y2 = y_ptr[stride]
	LDRB	r6, [r1], #1		; r6  = y0 = *y_ptr++
	ADD	r11,r11,#256
	ADD	r12,r12,#512
	WLDRW	wr0,[r14,r11,LSL #2]	; wr0 = u  = u_table[u]
	WLDRW	wr1,[r14,r12,LSL #2]	; wr1 = v  = v_table[v]
	WLDRW	wr2,[r14,r7, LSL #2]	; wr2 = y2 = y_table[y2]
	WLDRW	wr3,[r14,r6, LSL #2]	; wr3 = y0 = y_table[y0]
	; Stall
	WADDW	wr0,wr0,wr1		; wr0 = uv = u+v

	WADDW	wr1,wr0,wr15		; wr1 = uv1 += dither1
	WADDW	wr2,wr2,wr1		; wr2 = y2 += uv1
	WADDW	wr3,wr3,wr1		; wr3 = y0 += uv1
	WADDW	wr3,wr3,wr14		; wr3 = y0 += dither2
	TMRC	r7,wr2
	TMRC	r6,wr3
	ANDS	r12,r7, r5
	TSTEQ	r6, r5
	BNE	fix101
return101
	AND	r7, r4, r7, LSR #3
	ORR	r7, r7, r7, LSR #16
	STRH	r7, [r0, r9]
	AND	r6, r4, r6, LSR #3
	LDRB	r12,[r1, r10]		; r12 = y3 = y_ptr[stride]
	LDRB	r7, [r1], #1		; r6  = y1 = *y_ptr++
	ORR	r6, r6, r6, LSR #16
	LDR	r12,[r14, r12,LSL #2]	; r7  = y3 = y_table[y2]
	STRH	r6, [r0], #2
	LDR	r6, [r14, r7, LSL #2]	; r6  = y1 = y_table[y0]

	ADD	r7, r12,r11		; r7  = y3 + uv
	ADD	r6, r6, r11		; r6  = y1 + uv
	ADD	r6, r6, r5, LSR #DITH2	; r6  = y1 + uv + dither2
	ANDS	r12,r7, r5
	TSTEQ	r6, r5
	BNE	fix102
return102
	AND	r7, r4, r7, LSR #3
	AND	r6, r4, r6, LSR #3
	ORR	r7, r7, r7, LSR #16
	ORR	r6, r6, r6, LSR #16
	STRH	r7, [r0, r9]
	STRH	r6, [r0], #2
	ADDS	r8, r8, #2<<16
	BLT	xloop1
	MOVS	r8, r8, LSL #16		; Clear the top 16 bits of r8
	MOV	r8, r8, LSR #16		; If the C bit is clear we still have
	BCC	trail_pair1		; 1 more pixel pair to do
end_xloop1
	LDR	r11,[r13,#9*4]		; r11= width
	LDR	r12,[r13,#12*4]		; r12= uv_stride
	ADD	r0, r0, r9, LSL #1
	SUB	r0, r0, r11,LSL #1
	ADD	r1, r1, r10,LSL #1
	SUB	r1, r1, r11
	SUB	r2, r2, r11,LSR #1
	SUB	r3, r3, r11,LSR #1
	ADD	r2, r2, r12
	ADD	r3, r3, r12

	SUBS	r8, r8, #2
	BGT	yloop1

	LDMLTFD	r13!,{r4-r11,pc}
trail_row1
	; We have a row of pixels left to do
	SUB	r8, r8, r11,LSL #16	; r8 = height-(width<<16)
	ADDS	r8, r8, #1<<16		; if (width == 1)
	BGE	trail_pix1		;    just do 1 pixel
xloop12
	LDRB	r11,[r2], #1		; r11 = u  = *u_ptr++
	LDRB	r12,[r3], #1		; r12 = v  = *v_ptr++
	LDRB	r6, [r1], #1		; r6  = y0 = *y_ptr++
	LDRB	r7, [r1], #1		; r7  = y1 = *y_ptr++
	ADD	r11,r11,#256
	ADD	r12,r12,#512
	LDR	r11,[r14,r11,LSL #2]	; r11 = u  = u_table[u]
	LDR	r12,[r14,r12,LSL #2]	; r12 = v  = v_table[v]
	LDR	r7, [r14,r7, LSL #2]	; r7  = y1 = y_table[y1]
	LDR	r6, [r14,r6, LSL #2]	; r6  = y0 = y_table[y0]
	ADD	r11,r11,r12		; r11 = uv = u+v

	ADD	r6, r6, r11		; r6  = y0 + uv
	ADD	r6, r6, r5, LSR #DITH1	; r6  = y0 + uv + dither1
	ADD	r7, r7, r11		; r7  = y1 + uv
	ADD	r7, r7, r5, LSR #DITH2	; r7  = y1 + uv + dither2
	ANDS	r12,r7, r5
	TSTEQ	r6, r5
	BNE	fix104
return104
	AND	r6, r4, r6, LSR #3
	ORR	r6, r6, r6, LSR #16
	STRH	r6, [r0], #2
	AND	r7, r4, r7, LSR #3
	ORR	r7, r7, r7, LSR #16
	STRH	r7, [r0], #2
	ADDS	r8, r8, #2<<16
	BLT	xloop12
	MOVS	r8, r8, LSL #16		; Clear the top 16 bits of r8
	MOV	r8, r8, LSR #16		; If the C bit is clear we still have
	BCC	trail_pix1		; 1 more pixel pair to do
end
	LDMFD	r13!,{r4-r11,pc}
trail_pix1
	; We have a single extra pixel to do
	LDRB	r11,[r2], #1		; r11 = u  = *u_ptr++
	LDRB	r12,[r3], #1		; r12 = v  = *v_ptr++
	LDRB	r6, [r1], #1		; r6  = y0 = *y_ptr++
	ADD	r11,r11,#256
	ADD	r12,r12,#512
	LDR	r11,[r14,r11,LSL #2]	; r11 = u  = u_table[u]
	LDR	r12,[r14,r12,LSL #2]	; r12 = v  = v_table[v]
	LDR	r6, [r14,r6, LSL #2]	; r6  = y0 = y_table[y0]
	ADD	r11,r11,r12		; r11 = uv = u+v

	ADD	r11,r11,r5, LSR #DITH1	; (dither 1/4)
	ADD	r6, r6, r11		; r6  = y0 + uv + dither1
	ANDS	r12,r6, r5
	BNE	fix105
return105
	AND	r6, r4, r6, LSR #3
	ORR	r6, r6, r6, LSR #16
	STRH	r6, [r0], #2

	LDMFD	r13!,{r4-r11,pc}

trail_pair1
	; We have a pair of pixels left to do
	LDRB	r11,[r2]		; r11 = u  = *u_ptr++
	LDRB	r12,[r3]		; r12 = v  = *v_ptr++
	LDRB	r7, [r1, r10]		; r7  = y2 = y_ptr[stride]
	LDRB	r6, [r1], #1		; r6  = y0 = *y_ptr++
	ADD	r11,r11,#256
	ADD	r12,r12,#512
	LDR	r11,[r14,r11,LSL #2]	; r11 = u  = u_table[u]
	LDR	r12,[r14,r12,LSL #2]	; r12 = v  = v_table[v]
	LDR	r7, [r14,r7, LSL #2]	; r7  = y2 = y_table[y2]
	LDR	r6, [r14,r6, LSL #2]	; r6  = y0 = y_table[y0]
	ADD	r11,r11,r12		; r11 = uv = u+v

	ADD	r11,r11,r5, LSR #DITH1
	ADD	r7, r7, r11		; r7  = y2 + uv + dither1
	ADD	r6, r6, r11		; r6  = y0 + uv
	ADD	r7, r7, r5, LSR #DITH2	; r7  = y2 + uv + dither3
	ANDS	r12,r7, r5
	TSTEQ	r6, r5
	BNE	fix103
return103
	AND	r7, r4, r7, LSR #3
	ORR	r7, r7, r7, LSR #16
	STRH	r7, [r0, r9]
	AND	r6, r4, r6, LSR #3
	ORR	r6, r6, r6, LSR #16
	STRH	r6, [r0], #2
	B	end_xloop1
fix101
	; r7 and r6 are the values, at least one of which has overflowed
	; r12 = r7 & mask = .s......s......s......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r7, r7, r12		; r7 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r7, LSR #1	; r12 = .o......o......o......
	ADD	r7, r7, r12,LSR #8	; r7  = fixed value

	AND	r12, r6, r5		; r12 = .S......S......S......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r6, r6, r12		; r6 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r6, LSR #1	; r12 = .o......o......o......
	ADD	r6, r6, r12,LSR #8	; r6  = fixed value
	B	return101
fix102
	; r7 and r6 are the values, at least one of which has overflowed
	; r12 = r7 & mask = .s......s......s......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r7, r7, r12		; r7 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r7, LSR #1	; r12 = .o......o......o......
	ADD	r7, r7, r12,LSR #8	; r7  = fixed value

	AND	r12, r6, r5		; r12 = .S......S......S......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS..SSSSS.SSSSSS
	ORR	r6, r6, r12		; r6 |= ..SSSSSS..SSSSS.SSSSSS
	BIC	r12,r5, r6, LSR #1	; r12 = .o......o......o......
	ADD	r6, r6, r12,LSR #8	; r6  = fixed value
	B	return102
fix103
	; r7 and r6 are the values, at least one of which has overflowed
	; r12 = r7 & mask = .s......s......s......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r7, r7, r12		; r7 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r7, LSR #1	; r12 = .o......o......o......
	ADD	r7, r7, r12,LSR #8	; r7  = fixed value

	AND	r12, r6, r5		; r12 = .S......S......S......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r6, r6, r12		; r6 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r6, LSR #1	; r12 = .o......o......o......
	ADD	r6, r6, r12,LSR #8	; r6  = fixed value
	B	return103
fix104
	; r7 and r6 are the values, at least one of which has overflowed
	; r12 = r7 & mask = .s......s......s......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r7, r7, r12		; r7 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r7, LSR #1	; r12 = .o......o......o......
	ADD	r7, r7, r12,LSR #8	; r7  = fixed value

	AND	r12, r6, r5		; r12 = .S......S......S......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r6, r6, r12		; r6 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r6, LSR #1	; r12 = .o......o......o......
	ADD	r6, r6, r12,LSR #8	; r6  = fixed value
	B	return104
fix105
	; r6 is the value, which has has overflowed
	; r12 = r7 & mask = .s......s......s......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r6, r6, r12		; r6 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r6, LSR #1	; r12 = .o......o......o......
	ADD	r6, r6, r12,LSR #8	; r6  = fixed value
	B	return105

;------------------------------------------------------------------------
asm0
	;  Dither: 0 3
	;          2 1
	LDR	r11,[r13,#9*4]		; r11= width
	SUBS	r8, r8, #1
	BLT	end
	BEQ	trail_row0
yloop0
	SUB	r8, r8, r11,LSL #16	; r8 = height-(width<<16)
	ADDS	r8, r8, #1<<16		; if (width == 1)
	BGE	trail_pair0		;    just do 1 column
xloop0
	LDRB	r11,[r2], #1		; r11 = u  = *u_ptr++
	LDRB	r12,[r3], #1		; r12 = v  = *v_ptr++
	LDRB	r7, [r1, r10]		; r7  = y2 = y_ptr[stride]
	LDRB	r6, [r1], #1		; r6  = y0 = *y_ptr++
	ADD	r11,r11,#256
	ADD	r12,r12,#512
	LDR	r11,[r14,r11,LSL #2]	; r11 = u  = u_table[u]
	LDR	r12,[r14,r12,LSL #2]	; r12 = v  = v_table[v]
	LDR	r7, [r14,r7, LSL #2]	; r7  = y2 = y_table[y2]
	LDR	r6, [r14,r6, LSL #2]	; r6  = y0 = y_table[y0]
	ADD	r11,r11,r12		; r11 = uv = u+v

	ADD	r7, r7, r11		; r7  = y2 + uv
	ADD	r6, r6, r11		; r6  = y0 + uv
	ADD	r7, r7, r5, LSR #DITH2	; r7  = y2 + uv + dither2
	ANDS	r12,r7, r5
	TSTEQ	r6, r5
	BNE	fix001
return001
	AND	r7, r4, r7, LSR #3
	ORR	r7, r7, r7, LSR #16
	STRH	r7, [r0, r9]
	AND	r6, r4, r6, LSR #3
	LDRB	r12,[r1, r10]		; r12 = y3 = y_ptr[stride]
	LDRB	r7, [r1], #1		; r6  = y1 = *y_ptr++
	ORR	r6, r6, r6, LSR #16
	LDR	r12,[r14, r12,LSL #2]	; r7  = y3 = y_table[y2]
	STRH	r6, [r0], #2
	LDR	r6, [r14, r7, LSL #2]	; r6  = y1 = y_table[y0]

	ADD	r11,r11,r5, LSR #DITH1
	ADD	r7, r12,r11		; r7  = y3 + uv + dither1
	ADD	r6, r6, r11		; r6  = y1 + uv + dither1
	ADD	r6, r6, r5, LSR #DITH2	; r6  = y1 + uv + dither3
	ANDS	r12,r7, r5
	TSTEQ	r6, r5
	BNE	fix002
return002
	AND	r7, r4, r7, LSR #3
	AND	r6, r4, r6, LSR #3
	ORR	r7, r7, r7, LSR #16
	ORR	r6, r6, r6, LSR #16
	STRH	r7, [r0, r9]
	STRH	r6, [r0], #2
	ADDS	r8, r8, #2<<16
	BLT	xloop0
	MOVS	r8, r8, LSL #16		; Clear the top 16 bits of r8
	MOV	r8, r8, LSR #16		; If the C bit is clear we still have
	BCC	trail_pair0		; 1 more pixel pair to do
end_xloop0
	LDR	r11,[r13,#9*4]		; r11= width
	LDR	r12,[r13,#12*4]		; r12= uv_stride
	ADD	r0, r0, r9, LSL #1
	SUB	r0, r0, r11,LSL #1
	ADD	r1, r1, r10,LSL #1
	SUB	r1, r1, r11
	SUB	r2, r2, r11,LSR #1
	SUB	r3, r3, r11,LSR #1
	ADD	r2, r2, r12
	ADD	r3, r3, r12

	SUBS	r8, r8, #2
	BGT	yloop0

	LDMLTFD	r13!,{r4-r11,pc}
trail_row0
	; We have a row of pixels left to do
	SUB	r8, r8, r11,LSL #16	; r8 = height-(width<<16)
	ADDS	r8, r8, #1<<16		; if (width == 1)
	BGE	trail_pix0		;    just do 1 pixel
xloop02
	LDRB	r11,[r2], #1		; r11 = u  = *u_ptr++
	LDRB	r12,[r3], #1		; r12 = v  = *v_ptr++
	LDRB	r6, [r1], #1		; r6  = y0 = *y_ptr++
	LDRB	r7, [r1], #1		; r7  = y1 = *y_ptr++
	ADD	r11,r11,#256
	ADD	r12,r12,#512
	LDR	r11,[r14,r11,LSL #2]	; r11 = u  = u_table[u]
	LDR	r12,[r14,r12,LSL #2]	; r12 = v  = v_table[v]
	LDR	r7, [r14,r7, LSL #2]	; r7  = y1 = y_table[y1]
	LDR	r6, [r14,r6, LSL #2]	; r6  = y0 = y_table[y0]
	ADD	r11,r11,r12		; r11 = uv = u+v

	ADD	r6, r6, r11		; r6  = y0 + uv
	ADD	r7, r7, r11		; r7  = y1 + uv
	ADD	r7, r7, r5, LSR #DITH1	; r7  = y1 + uv + dither1
	ADD	r7, r7, r5, LSR #DITH2	; r7  = y1 + uv + dither3
	ANDS	r12,r7, r5
	TSTEQ	r6, r5
	BNE	fix004
return004
	AND	r6, r4, r6, LSR #3
	ORR	r6, r6, r6, LSR #16
	STRH	r6, [r0], #2
	AND	r7, r4, r7, LSR #3
	ORR	r7, r7, r7, LSR #16
	STRH	r7, [r0], #2
	ADDS	r8, r8, #2<<16
	BLT	xloop02
	MOVS	r8, r8, LSL #16		; Clear the top 16 bits of r8
	MOV	r8, r8, LSR #16		; If the C bit is clear we still have
	BCC	trail_pix0		; 1 more pixel pair to do

	LDMFD	r13!,{r4-r11,pc}
trail_pix0
	; We have a single extra pixel to do
	LDRB	r11,[r2], #1		; r11 = u  = *u_ptr++
	LDRB	r12,[r3], #1		; r12 = v  = *v_ptr++
	LDRB	r6, [r1], #1		; r6  = y0 = *y_ptr++
	ADD	r11,r11,#256
	ADD	r12,r12,#512
	LDR	r11,[r14,r11,LSL #2]	; r11 = u  = u_table[u]
	LDR	r12,[r14,r12,LSL #2]	; r12 = v  = v_table[v]
	LDR	r6, [r14,r6, LSL #2]	; r6  = y0 = y_table[y0]
	; Stall (on Xscale)
	ADD	r11,r11,r12		; r11 = uv = u+v

	ADD	r6, r6, r11		; r6  = y0 + uv
	ANDS	r12,r6, r5
	BNE	fix005
return005
	AND	r6, r4, r6, LSR #3
	ORR	r6, r6, r6, LSR #16
	STRH	r6, [r0], #2

	LDMFD	r13!,{r4-r11,pc}

trail_pair0
	; We have a pair of pixels left to do
	LDRB	r11,[r2]		; r11 = u  = *u_ptr++
	LDRB	r12,[r3]		; r12 = v  = *v_ptr++
	LDRB	r7, [r1, r10]		; r7  = y2 = y_ptr[stride]
	LDRB	r6, [r1], #1		; r6  = y0 = *y_ptr++
	ADD	r11,r11,#256
	ADD	r12,r12,#512
	LDR	r11,[r14,r11,LSL #2]	; r11 = u  = u_table[u]
	LDR	r12,[r14,r12,LSL #2]	; r12 = v  = v_table[v]
	LDR	r7, [r14,r7, LSL #2]	; r7  = y2 = y_table[y2]
	LDR	r6, [r14,r6, LSL #2]	; r6  = y0 = y_table[y0]
	ADD	r11,r11,r12		; r11 = uv = u+v

	ADD	r7, r7, r11		; r7  = y2 + uv
	ADD	r6, r6, r11		; r6  = y0 + uv
	ADD	r7, r7, r5, LSR #DITH2	; r7  = y2 + uv + dither2
	ANDS	r12,r7, r5
	TSTEQ	r6, r5
	BNE	fix003
return003
	AND	r7, r4, r7, LSR #3
	ORR	r7, r7, r7, LSR #16
	STRH	r7, [r0, r9]
	AND	r6, r4, r6, LSR #3
	ORR	r6, r6, r6, LSR #16
	STRH	r6, [r0], #2
	B	end_xloop0
fix001
	; r7 and r6 are the values, at least one of which has overflowed
	; r12 = r7 & mask = .s......s......s......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r7, r7, r12		; r7 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r7, LSR #1	; r12 = .o......o......o......
	ADD	r7, r7, r12,LSR #8	; r7  = fixed value

	AND	r12, r6, r5		; r12 = .S......S......S......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r6, r6, r12		; r6 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r6, LSR #1	; r12 = .o......o......o......
	ADD	r6, r6, r12,LSR #8	; r6  = fixed value
	B	return001
fix002
	; r7 and r6 are the values, at least one of which has overflowed
	; r12 = r7 & mask = .s......s......s......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r7, r7, r12		; r7 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r7, LSR #1	; r12 = .o......o......o......
	ADD	r7, r7, r12,LSR #8	; r7  = fixed value

	AND	r12, r6, r5		; r12 = .S......S......S......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS..SSSSS.SSSSSS
	ORR	r6, r6, r12		; r6 |= ..SSSSSS..SSSSS.SSSSSS
	BIC	r12,r5, r6, LSR #1	; r12 = .o......o......o......
	ADD	r6, r6, r12,LSR #8	; r6  = fixed value
	B	return002
fix003
	; r7 and r6 are the values, at least one of which has overflowed
	; r12 = r7 & mask = .s......s......s......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r7, r7, r12		; r7 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r7, LSR #1	; r12 = .o......o......o......
	ADD	r7, r7, r12,LSR #8	; r7  = fixed value

	AND	r12, r6, r5		; r12 = .S......S......S......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r6, r6, r12		; r6 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r6, LSR #1	; r12 = .o......o......o......
	ADD	r6, r6, r12,LSR #8	; r6  = fixed value
	B	return003
fix004
	; r7 and r6 are the values, at least one of which has overflowed
	; r12 = r7 & mask = .s......s......s......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r7, r7, r12		; r7 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r7, LSR #1	; r12 = .o......o......o......
	ADD	r7, r7, r12,LSR #8	; r7  = fixed value

	AND	r12, r6, r5		; r12 = .S......S......S......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r6, r6, r12		; r6 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r6, LSR #1	; r12 = .o......o......o......
	ADD	r6, r6, r12,LSR #8	; r6  = fixed value
	B	return004
fix005
	; r6 is the value, which has has overflowed
	; r12 = r7 & mask = .s......s......s......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r6, r6, r12		; r6 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r6, LSR #1	; r12 = .o......o......o......
	ADD	r6, r6, r12,LSR #8	; r6  = fixed value
	B	return005

;------------------------------------------------------------------------
asm2
	;  Dither: 2 1
	;          0 3
	LDR	r11,[r13,#9*4]		; r11= width
	SUBS	r8, r8, #1
	BLT	end
	BEQ	trail_row2
yloop2
	SUB	r8, r8, r11,LSL #16	; r8 = height-(width<<16)
	ADDS	r8, r8, #1<<16		; if (width == 1)
	BGE	trail_pair2		;    just do 1 column
xloop2
	LDRB	r11,[r2], #1		; r11 = u  = *u_ptr++
	LDRB	r12,[r3], #1		; r12 = v  = *v_ptr++
	LDRB	r7, [r1, r10]		; r7  = y2 = y_ptr[stride]
	LDRB	r6, [r1], #1		; r6  = y0 = *y_ptr++
	ADD	r11,r11,#256
	ADD	r12,r12,#512
	LDR	r11,[r14,r11,LSL #2]	; r11 = u  = u_table[u]
	LDR	r12,[r14,r12,LSL #2]	; r12 = v  = v_table[v]
	LDR	r7, [r14,r7, LSL #2]	; r7  = y2 = y_table[y2]
	LDR	r6, [r14,r6, LSL #2]	; r6  = y0 = y_table[y0]
	ADD	r11,r11,r12		; r11 = uv = u+v

	ADD	r7, r7, r11		; r7  = y2 + uv
	ADD	r6, r6, r11		; r6  = y0 + uv
	ADD	r6, r6, r5, LSR #DITH2	; r6  = y0 + uv + dither2
	ANDS	r12,r7, r5
	TSTEQ	r6, r5
	BNE	fix201
return201
	AND	r7, r4, r7, LSR #3
	ORR	r7, r7, r7, LSR #16
	STRH	r7, [r0, r9]
	AND	r6, r4, r6, LSR #3
	LDRB	r12,[r1, r10]		; r12 = y3 = y_ptr[stride]
	LDRB	r7, [r1], #1		; r6  = y1 = *y_ptr++
	ORR	r6, r6, r6, LSR #16
	LDR	r12,[r14, r12,LSL #2]	; r7  = y3 = y_table[y2]
	STRH	r6, [r0], #2
	LDR	r6, [r14, r7, LSL #2]	; r6  = y1 = y_table[y0]

	ADD	r11,r11,r5, LSR #DITH1
	ADD	r7, r12,r11		; r7  = y3 + uv + dither1
	ADD	r7, r7, r5, LSR #DITH2	; r7  = y3 + uv + dither3
	ADD	r6, r6, r11		; r6  = y1 + uv + dither1
	ANDS	r12,r7, r5
	TSTEQ	r6, r5
	BNE	fix202
return202
	AND	r7, r4, r7, LSR #3
	AND	r6, r4, r6, LSR #3
	ORR	r7, r7, r7, LSR #16
	ORR	r6, r6, r6, LSR #16
	STRH	r7, [r0, r9]
	STRH	r6, [r0], #2
	ADDS	r8, r8, #2<<16
	BLT	xloop2
	MOVS	r8, r8, LSL #16		; Clear the top 16 bits of r8
	MOV	r8, r8, LSR #16		; If the C bit is clear we still have
	BCC	trail_pair2		; 1 more pixel pair to do
end_xloop2
	LDR	r11,[r13,#9*4]		; r11= width
	LDR	r12,[r13,#12*4]		; r12= uv_stride
	ADD	r0, r0, r9, LSL #1
	SUB	r0, r0, r11,LSL #1
	ADD	r1, r1, r10,LSL #1
	SUB	r1, r1, r11
	SUB	r2, r2, r11,LSR #1
	SUB	r3, r3, r11,LSR #1
	ADD	r2, r2, r12
	ADD	r3, r3, r12

	SUBS	r8, r8, #2
	BGT	yloop2

	LDMLTFD	r13!,{r4-r11,pc}
trail_row2
	; We have a row of pixels left to do
	SUB	r8, r8, r11,LSL #16	; r8 = height-(width<<16)
	ADDS	r8, r8, #1<<16		; if (width == 1)
	BGE	trail_pix2		;    just do 1 pixel
xloop22
	LDRB	r11,[r2], #1		; r11 = u  = *u_ptr++
	LDRB	r12,[r3], #1		; r12 = v  = *v_ptr++
	LDRB	r6, [r1], #1		; r6  = y0 = *y_ptr++
	LDRB	r7, [r1], #1		; r7  = y1 = *y_ptr++
	ADD	r11,r11,#256
	ADD	r12,r12,#512
	LDR	r11,[r14,r11,LSL #2]	; r11 = u  = u_table[u]
	LDR	r12,[r14,r12,LSL #2]	; r12 = v  = v_table[v]
	LDR	r7, [r14,r7, LSL #2]	; r7  = y1 = y_table[y1]
	LDR	r6, [r14,r6, LSL #2]	; r6  = y0 = y_table[y0]
	ADD	r11,r11,r12		; r11 = uv = u+v

	ADD	r6, r6, r11		; r6  = y0 + uv
	ADD	r6, r6, r5, LSR #DITH2	; r6  = y0 + uv + dither2
	ADD	r7, r7, r11		; r7  = y1 + uv
	ADD	r7, r7, r5, LSR #DITH1	; r7  = y1 + uv + dither1
	ANDS	r12,r7, r5
	TSTEQ	r6, r5
	BNE	fix204
return204
	AND	r6, r4, r6, LSR #3
	ORR	r6, r6, r6, LSR #16
	STRH	r6, [r0], #2
	AND	r7, r4, r7, LSR #3
	ORR	r7, r7, r7, LSR #16
	STRH	r7, [r0], #2
	ADDS	r8, r8, #2<<16
	BLT	xloop22
	MOVS	r8, r8, LSL #16		; Clear the top 16 bits of r8
	MOV	r8, r8, LSR #16		; If the C bit is clear we still have
	BCC	trail_pix2		; 1 more pixel pair to do

	LDMFD	r13!,{r4-r11,pc}
trail_pix2
	; We have a single extra pixel to do
	LDRB	r11,[r2], #1		; r11 = u  = *u_ptr++
	LDRB	r12,[r3], #1		; r12 = v  = *v_ptr++
	LDRB	r6, [r1], #1		; r6  = y0 = *y_ptr++
	ADD	r11,r11,#256
	ADD	r12,r12,#512
	LDR	r11,[r14,r11,LSL #2]	; r11 = u  = u_table[u]
	LDR	r12,[r14,r12,LSL #2]	; r12 = v  = v_table[v]
	LDR	r6, [r14,r6, LSL #2]	; r6  = y0 = y_table[y0]
	ADD	r11,r11,r12		; r11 = uv = u+v

	ADD	r11,r11,r5, LSR #DITH2
	ADD	r6, r6, r11		; r6  = y0 + uv + dither2
	ANDS	r12,r6, r5
	BNE	fix205
return205
	AND	r6, r4, r6, LSR #3
	ORR	r6, r6, r6, LSR #16
	STRH	r6, [r0], #2

	LDMFD	r13!,{r4-r11,pc}

trail_pair2
	; We have a pair of pixels left to do
	LDRB	r11,[r2]		; r11 = u  = *u_ptr++
	LDRB	r12,[r3]		; r12 = v  = *v_ptr++
	LDRB	r7, [r1, r10]		; r7  = y2 = y_ptr[stride]
	LDRB	r6, [r1], #1		; r6  = y0 = *y_ptr++
	ADD	r11,r11,#256
	ADD	r12,r12,#512
	LDR	r11,[r14,r11,LSL #2]	; r11 = u  = u_table[u]
	LDR	r12,[r14,r12,LSL #2]	; r12 = v  = v_table[v]
	LDR	r7, [r14,r7, LSL #2]	; r7  = y2 = y_table[y2]
	LDR	r6, [r14,r6, LSL #2]	; r6  = y0 = y_table[y0]
	ADD	r11,r11,r12		; r11 = uv = u+v

	ADD	r7, r7, r11		; r7  = y2 + uv
	ADD	r6, r6, r11		; r6  = y0 + uv
	ADD	r6, r6, r5, LSR #DITH2	; r6  = y0 + uv + 2
	ANDS	r12,r7, r5
	TSTEQ	r6, r5
	BNE	fix203
return203
	AND	r7, r4, r7, LSR #3
	ORR	r7, r7, r7, LSR #16
	STRH	r7, [r0, r9]
	AND	r6, r4, r6, LSR #3
	ORR	r6, r6, r6, LSR #16
	STRH	r6, [r0], #2
	B	end_xloop2
fix201
	; r7 and r6 are the values, at least one of which has overflowed
	; r12 = r7 & mask = .s......s......s......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r7, r7, r12		; r7 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r7, LSR #1	; r12 = .o......o......o......
	ADD	r7, r7, r12,LSR #8	; r7  = fixed value

	AND	r12, r6, r5		; r12 = .S......S......S......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r6, r6, r12		; r6 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r6, LSR #1	; r12 = .o......o......o......
	ADD	r6, r6, r12,LSR #8	; r6  = fixed value
	B	return201
fix202
	; r7 and r6 are the values, at least one of which has overflowed
	; r12 = r7 & mask = .s......s......s......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r7, r7, r12		; r7 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r7, LSR #1	; r12 = .o......o......o......
	ADD	r7, r7, r12,LSR #8	; r7  = fixed value

	AND	r12, r6, r5		; r12 = .S......S......S......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS..SSSSS.SSSSSS
	ORR	r6, r6, r12		; r6 |= ..SSSSSS..SSSSS.SSSSSS
	BIC	r12,r5, r6, LSR #1	; r12 = .o......o......o......
	ADD	r6, r6, r12,LSR #8	; r6  = fixed value
	B	return202
fix203
	; r7 and r6 are the values, at least one of which has overflowed
	; r12 = r7 & mask = .s......s......s......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r7, r7, r12		; r7 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r7, LSR #1	; r12 = .o......o......o......
	ADD	r7, r7, r12,LSR #8	; r7  = fixed value

	AND	r12, r6, r5		; r12 = .S......S......S......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r6, r6, r12		; r6 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r6, LSR #1	; r12 = .o......o......o......
	ADD	r6, r6, r12,LSR #8	; r6  = fixed value
	B	return203
fix204
	; r7 and r6 are the values, at least one of which has overflowed
	; r12 = r7 & mask = .s......s......s......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r7, r7, r12		; r7 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r7, LSR #1	; r12 = .o......o......o......
	ADD	r7, r7, r12,LSR #8	; r7  = fixed value

	AND	r12, r6, r5		; r12 = .S......S......S......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r6, r6, r12		; r6 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r6, LSR #1	; r12 = .o......o......o......
	ADD	r6, r6, r12,LSR #8	; r6  = fixed value
	B	return204
fix205
	; r6 is the value, which has has overflowed
	; r12 = r7 & mask = .s......s......s......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r6, r6, r12		; r6 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r6, LSR #1	; r12 = .o......o......o......
	ADD	r6, r6, r12,LSR #8	; r6  = fixed value
	B	return205

;------------------------------------------------------------------------
asm3
	;  Dither: 3 0
	;          1 2
	LDR	r11,[r13,#9*4]		; r11= width
	SUBS	r8, r8, #1
	BLT	end
	BEQ	trail_row3
yloop3
	SUB	r8, r8, r11,LSL #16	; r8 = height-(width<<16)
	ADDS	r8, r8, #1<<16		; if (width == 1)
	BGE	trail_pair3		;    just do 1 column
xloop3
	LDRB	r11,[r2], #1		; r11 = u  = *u_ptr++
	LDRB	r12,[r3], #1		; r12 = v  = *v_ptr++
	LDRB	r7, [r1, r10]		; r7  = y2 = y_ptr[stride]
	LDRB	r6, [r1], #1		; r6  = y0 = *y_ptr++
	ADD	r11,r11,#256
	ADD	r12,r12,#512
	LDR	r11,[r14,r11,LSL #2]	; r11 = u  = u_table[u]
	LDR	r12,[r14,r12,LSL #2]	; r12 = v  = v_table[v]
	LDR	r7, [r14,r7, LSL #2]	; r7  = y2 = y_table[y2]
	LDR	r6, [r14,r6, LSL #2]	; r6  = y0 = y_table[y0]
	ADD	r11,r11,r12		; r11 = uv = u+v

	ADD	r12,r11,r5, LSR #DITH1
	ADD	r7, r7, r12		; r7  = y2 + uv + dither1
	ADD	r6, r6, r12		; r6  = y0 + uv + dither1
	ADD	r6, r6, r5, LSR #DITH2	; r6  = y0 + uv + dither3
	ANDS	r12,r7, r5
	TSTEQ	r6, r5
	BNE	fix301
return301
	AND	r7, r4, r7, LSR #3
	ORR	r7, r7, r7, LSR #16
	STRH	r7, [r0, r9]
	AND	r6, r4, r6, LSR #3
	LDRB	r12,[r1, r10]		; r12 = y3 = y_ptr[stride]
	LDRB	r7, [r1], #1		; r6  = y1 = *y_ptr++
	ORR	r6, r6, r6, LSR #16
	LDR	r12,[r14, r12,LSL #2]	; r7  = y3 = y_table[y2]
	STRH	r6, [r0], #2
	LDR	r6, [r14, r7, LSL #2]	; r6  = y1 = y_table[y0]

	ADD	r7, r12,r11		; r7  = y3 + uv
	ADD	r7, r7, r5, LSR #DITH2	; r7  = y3 + uv + dither2
	ADD	r6, r6, r11		; r6  = y1 + uv
	ANDS	r12,r7, r5
	TSTEQ	r6, r5
	BNE	fix302
return302
	AND	r7, r4, r7, LSR #3
	AND	r6, r4, r6, LSR #3
	ORR	r7, r7, r7, LSR #16
	ORR	r6, r6, r6, LSR #16
	STRH	r7, [r0, r9]
	STRH	r6, [r0], #2
	ADDS	r8, r8, #2<<16
	BLT	xloop3
	MOVS	r8, r8, LSL #16		; Clear the top 16 bits of r8
	MOV	r8, r8, LSR #16		; If the C bit is clear we still have
	BCC	trail_pair3		; 1 more pixel pair to do
end_xloop3
	LDR	r11,[r13,#9*4]		; r11= width
	LDR	r12,[r13,#12*4]		; r12= uv_stride
	ADD	r0, r0, r9, LSL #1
	SUB	r0, r0, r11,LSL #1
	ADD	r1, r1, r10,LSL #1
	SUB	r1, r1, r11
	SUB	r2, r2, r11,LSR #1
	SUB	r3, r3, r11,LSR #1
	ADD	r2, r2, r12
	ADD	r3, r3, r12

	SUBS	r8, r8, #2
	BGT	yloop3

	LDMLTFD	r13!,{r4-r11,pc}
trail_row3
	; We have a row of pixels left to do
	SUB	r8, r8, r11,LSL #16	; r8 = height-(width<<16)
	ADDS	r8, r8, #1<<16		; if (width == 1)
	BGE	trail_pix3		;    just do 1 pixel
xloop32
	LDRB	r11,[r2], #1		; r11 = u  = *u_ptr++
	LDRB	r12,[r3], #1		; r12 = v  = *v_ptr++
	LDRB	r6, [r1], #1		; r6  = y0 = *y_ptr++
	LDRB	r7, [r1], #1		; r7  = y1 = *y_ptr++
	ADD	r11,r11,#256
	ADD	r12,r12,#512
	LDR	r11,[r14,r11,LSL #2]	; r11 = u  = u_table[u]
	LDR	r12,[r14,r12,LSL #2]	; r12 = v  = v_table[v]
	LDR	r7, [r14,r7, LSL #2]	; r7  = y1 = y_table[y1]
	LDR	r6, [r14,r6, LSL #2]	; r6  = y0 = y_table[y0]
	ADD	r11,r11,r12		; r11 = uv = u+v

	ADD	r6, r6, r11		; r6  = y0 + uv
	ADD	r6, r6, r5, LSR #DITH1	; r6  = y0 + uv + dither1
	ADD	r6, r6, r5, LSR #DITH2	; r6  = y0 + uv + dither3
	ADD	r7, r7, r11		; r7  = y1 + uv
	ANDS	r12,r7, r5
	TSTEQ	r6, r5
	BNE	fix304
return304
	AND	r6, r4, r6, LSR #3
	ORR	r6, r6, r6, LSR #16
	STRH	r6, [r0], #2
	AND	r7, r4, r7, LSR #3
	ORR	r7, r7, r7, LSR #16
	STRH	r7, [r0], #2
	ADDS	r8, r8, #2<<16
	BLT	xloop32
	MOVS	r8, r8, LSL #16		; Clear the top 16 bits of r8
	MOV	r8, r8, LSR #16		; If the C bit is clear we still have
	BCC	trail_pix3		; 1 more pixel pair to do

	LDMFD	r13!,{r4-r11,pc}
trail_pix3
	; We have a single extra pixel to do
	LDRB	r11,[r2], #1		; r11 = u  = *u_ptr++
	LDRB	r12,[r3], #1		; r12 = v  = *v_ptr++
	LDRB	r6, [r1], #1		; r6  = y0 = *y_ptr++
	ADD	r11,r11,#256
	ADD	r12,r12,#512
	LDR	r11,[r14,r11,LSL #2]	; r11 = u  = u_table[u]
	LDR	r12,[r14,r12,LSL #2]	; r12 = v  = v_table[v]
	LDR	r6, [r14,r6, LSL #2]	; r6  = y0 = y_table[y0]
	ADD	r11,r11,r12		; r11 = uv = u+v

	ADD	r11,r11,r5, LSR #DITH1
	ADD	r11,r11,r5, LSR #DITH2
	ADD	r6, r6, r11		; r6  = y0 + uv + dither3
	ANDS	r12,r6, r5
	BNE	fix305
return305
	AND	r6, r4, r6, LSR #3
	ORR	r6, r6, r6, LSR #16
	STRH	r6, [r0], #2

	LDMFD	r13!,{r4-r11,pc}

trail_pair3
	; We have a pair of pixels left to do
	LDRB	r11,[r2]		; r11 = u  = *u_ptr++
	LDRB	r12,[r3]		; r12 = v  = *v_ptr++
	LDRB	r7, [r1, r10]		; r7  = y2 = y_ptr[stride]
	LDRB	r6, [r1], #1		; r6  = y0 = *y_ptr++
	ADD	r11,r11,#256
	ADD	r12,r12,#512
	LDR	r11,[r14,r11,LSL #2]	; r11 = u  = u_table[u]
	LDR	r12,[r14,r12,LSL #2]	; r12 = v  = v_table[v]
	LDR	r7, [r14,r7, LSL #2]	; r7  = y2 = y_table[y2]
	LDR	r6, [r14,r6, LSL #2]	; r6  = y0 = y_table[y0]
	ADD	r11,r11,r12		; r11 = uv = u+v

	ADD	r12,r11,r5, LSR #DITH1
	ADD	r7, r7, r12		; r7  = y2 + uv + dither1
	ADD	r6, r6, r12		; r6  = y0 + uv + dither1
	ADD	r6, r6, r5, LSR #DITH2	; r6  = y0 + uv + dither3
	ANDS	r12,r7, r5
	TSTEQ	r6, r5
	BNE	fix303
return303
	AND	r7, r4, r7, LSR #3
	ORR	r7, r7, r7, LSR #16
	STRH	r7, [r0, r9]
	AND	r6, r4, r6, LSR #3
	ORR	r6, r6, r6, LSR #16
	STRH	r6, [r0], #2
	B	end_xloop3
fix301
	; r7 and r6 are the values, at least one of which has overflowed
	; r12 = r7 & mask = .s......s......s......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r7, r7, r12		; r7 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r7, LSR #1	; r12 = .o......o......o......
	ADD	r7, r7, r12,LSR #8	; r7  = fixed value

	AND	r12, r6, r5		; r12 = .S......S......S......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r6, r6, r12		; r6 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r6, LSR #1	; r12 = .o......o......o......
	ADD	r6, r6, r12,LSR #8	; r6  = fixed value
	B	return301
fix302
	; r7 and r6 are the values, at least one of which has overflowed
	; r12 = r7 & mask = .s......s......s......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r7, r7, r12		; r7 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r7, LSR #1	; r12 = .o......o......o......
	ADD	r7, r7, r12,LSR #8	; r7  = fixed value

	AND	r12, r6, r5		; r12 = .S......S......S......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS..SSSSS.SSSSSS
	ORR	r6, r6, r12		; r6 |= ..SSSSSS..SSSSS.SSSSSS
	BIC	r12,r5, r6, LSR #1	; r12 = .o......o......o......
	ADD	r6, r6, r12,LSR #8	; r6  = fixed value
	B	return302
fix303
	; r7 and r6 are the values, at least one of which has overflowed
	; r12 = r7 & mask = .s......s......s......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r7, r7, r12		; r7 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r7, LSR #1	; r12 = .o......o......o......
	ADD	r7, r7, r12,LSR #8	; r7  = fixed value

	AND	r12, r6, r5		; r12 = .S......S......S......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r6, r6, r12		; r6 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r6, LSR #1	; r12 = .o......o......o......
	ADD	r6, r6, r12,LSR #8	; r6  = fixed value
	B	return303
fix304
	; r7 and r6 are the values, at least one of which has overflowed
	; r12 = r7 & mask = .s......s......s......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r7, r7, r12		; r7 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r7, LSR #1	; r12 = .o......o......o......
	ADD	r7, r7, r12,LSR #8	; r7  = fixed value

	AND	r12, r6, r5		; r12 = .S......S......S......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r6, r6, r12		; r6 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r6, LSR #1	; r12 = .o......o......o......
	ADD	r6, r6, r12,LSR #8	; r6  = fixed value
	B	return304
fix305
	; r6 is the value, which has has overflowed
	; r12 = r7 & mask = .s......s......s......
	SUB	r12,r12,r12,LSR #8	; r12 = ..SSSSSS.SSSSSS.SSSSSS
	ORR	r6, r6, r12		; r6 |= ..SSSSSS.SSSSSS.SSSSSS
	BIC	r12,r5, r6, LSR #1	; r12 = .o......o......o......
	ADD	r6, r6, r12,LSR #8	; r6  = fixed value
	B	return305


yuv2rgb_table
y_table
        DCD 0x7FFFFFED
        DCD 0x7FFFFFEF
        DCD 0x7FFFFFF0
        DCD 0x7FFFFFF1
        DCD 0x7FFFFFF2
        DCD 0x7FFFFFF3
        DCD 0x7FFFFFF4
        DCD 0x7FFFFFF6
        DCD 0x7FFFFFF7
        DCD 0x7FFFFFF8
        DCD 0x7FFFFFF9
        DCD 0x7FFFFFFA
        DCD 0x7FFFFFFB
        DCD 0x7FFFFFFD
        DCD 0x7FFFFFFE
        DCD 0x7FFFFFFF
        DCD 0x80000000
        DCD 0x80400801
        DCD 0x80A01002
        DCD 0x80E01803
        DCD 0x81202805
        DCD 0x81803006
        DCD 0x81C03807
        DCD 0x82004008
        DCD 0x82604809
        DCD 0x82A0500A
        DCD 0x82E0600C
        DCD 0x8340680D
        DCD 0x8380700E
        DCD 0x83C0780F
        DCD 0x84208010
        DCD 0x84608811
        DCD 0x84A09813
        DCD 0x8500A014
        DCD 0x8540A815
        DCD 0x8580B016
        DCD 0x85E0B817
        DCD 0x8620C018
        DCD 0x8660D01A
        DCD 0x86C0D81B
        DCD 0x8700E01C
        DCD 0x8740E81D
        DCD 0x87A0F01E
        DCD 0x87E0F81F
        DCD 0x88210821
        DCD 0x88811022
        DCD 0x88C11823
        DCD 0x89012024
        DCD 0x89412825
        DCD 0x89A13026
        DCD 0x89E14028
        DCD 0x8A214829
        DCD 0x8A81502A
        DCD 0x8AC1582B
        DCD 0x8B01602C
        DCD 0x8B61682D
        DCD 0x8BA1782F
        DCD 0x8BE18030
        DCD 0x8C418831
        DCD 0x8C819032
        DCD 0x8CC19833
        DCD 0x8D21A034
        DCD 0x8D61B036
        DCD 0x8DA1B837
        DCD 0x8E01C038
        DCD 0x8E41C839
        DCD 0x8E81D03A
        DCD 0x8EE1D83B
        DCD 0x8F21E83D
        DCD 0x8F61F03E
        DCD 0x8FC1F83F
        DCD 0x90020040
        DCD 0x90420841
        DCD 0x90A21042
        DCD 0x90E22044
        DCD 0x91222845
        DCD 0x91823046
        DCD 0x91C23847
        DCD 0x92024048
        DCD 0x92624849
        DCD 0x92A2504A
        DCD 0x92E2604C
        DCD 0x9342684D
        DCD 0x9382704E
        DCD 0x93C2784F
        DCD 0x94228050
        DCD 0x94628851
        DCD 0x94A29853
        DCD 0x9502A054
        DCD 0x9542A855
        DCD 0x9582B056
        DCD 0x95E2B857
        DCD 0x9622C058
        DCD 0x9662D05A
        DCD 0x96C2D85B
        DCD 0x9702E05C
        DCD 0x9742E85D
        DCD 0x97A2F05E
        DCD 0x97E2F85F
        DCD 0x98230861
        DCD 0x98831062
        DCD 0x98C31863
        DCD 0x99032064
        DCD 0x99632865
        DCD 0x99A33066
        DCD 0x99E34068
        DCD 0x9A434869
        DCD 0x9A83506A
        DCD 0x9AC3586B
        DCD 0x9B23606C
        DCD 0x9B63686D
        DCD 0x9BA3786F
        DCD 0x9BE38070
        DCD 0x9C438871
        DCD 0x9C839072
        DCD 0x9CC39873
        DCD 0x9D23A074
        DCD 0x9D63B076
        DCD 0x9DA3B877
        DCD 0x9E03C078
        DCD 0x9E43C879
        DCD 0x9E83D07A
        DCD 0x9EE3D87B
        DCD 0x9F23E87D
        DCD 0x9F63F07E
        DCD 0x9FC3F87F
        DCD 0xA0040080
        DCD 0xA0440881
        DCD 0xA0A41082
        DCD 0xA0E42084
        DCD 0xA1242885
        DCD 0xA1843086
        DCD 0xA1C43887
        DCD 0xA2044088
        DCD 0xA2644889
        DCD 0xA2A4588B
        DCD 0xA2E4608C
        DCD 0xA344688D
        DCD 0xA384708E
        DCD 0xA3C4788F
        DCD 0xA4248090
        DCD 0xA4649092
        DCD 0xA4A49893
        DCD 0xA504A094
        DCD 0xA544A895
        DCD 0xA584B096
        DCD 0xA5E4B897
        DCD 0xA624C098
        DCD 0xA664D09A
        DCD 0xA6C4D89B
        DCD 0xA704E09C
        DCD 0xA744E89D
        DCD 0xA7A4F09E
        DCD 0xA7E4F89F
        DCD 0xA82508A1
        DCD 0xA88510A2
        DCD 0xA8C518A3
        DCD 0xA90520A4
        DCD 0xA96528A5
        DCD 0xA9A530A6
        DCD 0xA9E540A8
        DCD 0xAA4548A9
        DCD 0xAA8550AA
        DCD 0xAAC558AB
        DCD 0xAB2560AC
        DCD 0xAB6568AD
        DCD 0xABA578AF
        DCD 0xAC0580B0
        DCD 0xAC4588B1
        DCD 0xAC8590B2
        DCD 0xACE598B3
        DCD 0xAD25A0B4
        DCD 0xAD65B0B6
        DCD 0xADA5B8B7
        DCD 0xAE05C0B8
        DCD 0xAE45C8B9
        DCD 0xAE85D0BA
        DCD 0xAEE5D8BB
        DCD 0xAF25E8BD
        DCD 0xAF65F0BE
        DCD 0xAFC5F8BF
        DCD 0xB00600C0
        DCD 0xB04608C1
        DCD 0xB0A610C2
        DCD 0xB0E620C4
        DCD 0xB12628C5
        DCD 0xB18630C6
        DCD 0xB1C638C7
        DCD 0xB20640C8
        DCD 0xB26648C9
        DCD 0xB2A658CB
        DCD 0xB2E660CC
        DCD 0xB34668CD
        DCD 0xB38670CE
        DCD 0xB3C678CF
        DCD 0xB42680D0
        DCD 0xB46690D2
        DCD 0xB4A698D3
        DCD 0xB506A0D4
        DCD 0xB546A8D5
        DCD 0xB586B0D6
        DCD 0xB5E6B8D7
        DCD 0xB626C8D9
        DCD 0xB666D0DA
        DCD 0xB6C6D8DB
        DCD 0xB706E0DC
        DCD 0xB746E8DD
        DCD 0xB7A6F0DE
        DCD 0xB7E6F8DF
        DCD 0xB82708E1
        DCD 0xB88710E2
        DCD 0xB8C718E3
        DCD 0xB90720E4
        DCD 0xB96728E5
        DCD 0xB9A730E6
        DCD 0xB9E740E8
        DCD 0xBA4748E9
        DCD 0xBA8750EA
        DCD 0xBAC758EB
        DCD 0xBB2760EC
        DCD 0xBB6768ED
        DCD 0xBBA778EF
        DCD 0xBC0780F0
        DCD 0xBC4788F1
        DCD 0xBC8790F2
        DCD 0xBCE798F3
        DCD 0xBD27A0F4
        DCD 0xBD67B0F6
        DCD 0xBDC7B8F7
        DCD 0xBE07C0F8
        DCD 0xBE47C8F9
        DCD 0xBEA7D0FA
        DCD 0xBEE7D8FB
        DCD 0xBF27E8FD
        DCD 0xBF87F0FE
        DCD 0xBFC7F8FF
        DCD 0xC0080100
        DCD 0xC0480901
        DCD 0xC0A81102
        DCD 0xC0E82104
        DCD 0xC0E82104
        DCD 0xC0E82104
        DCD 0xC0E82104
        DCD 0xC0E82104
        DCD 0xC0E82104
        DCD 0xC0E82104
        DCD 0xC0E82104
        DCD 0xC0E82104
        DCD 0xC0E82104
        DCD 0xC0E82104
        DCD 0xC0E82104
        DCD 0xC0E82104
        DCD 0xC0E82104
        DCD 0xC0E82104
        DCD 0xC0E82104
        DCD 0xC0E82104
u_table
        DCD 0x0C400103
        DCD 0x0C200105
        DCD 0x0C200107
        DCD 0x0C000109
        DCD 0x0BE0010B
        DCD 0x0BC0010D
        DCD 0x0BA0010F
        DCD 0x0BA00111
        DCD 0x0B800113
        DCD 0x0B600115
        DCD 0x0B400117
        DCD 0x0B400119
        DCD 0x0B20011B
        DCD 0x0B00011D
        DCD 0x0AE0011F
        DCD 0x0AE00121
        DCD 0x0AC00123
        DCD 0x0AA00125
        DCD 0x0A800127
        DCD 0x0A600129
        DCD 0x0A60012B
        DCD 0x0A40012D
        DCD 0x0A20012F
        DCD 0x0A000131
        DCD 0x0A000132
        DCD 0x09E00134
        DCD 0x09C00136
        DCD 0x09A00138
        DCD 0x09A0013A
        DCD 0x0980013C
        DCD 0x0960013E
        DCD 0x09400140
        DCD 0x09400142
        DCD 0x09200144
        DCD 0x09000146
        DCD 0x08E00148
        DCD 0x08C0014A
        DCD 0x08C0014C
        DCD 0x08A0014E
        DCD 0x08800150
        DCD 0x08600152
        DCD 0x08600154
        DCD 0x08400156
        DCD 0x08200158
        DCD 0x0800015A
        DCD 0x0800015C
        DCD 0x07E0015E
        DCD 0x07C00160
        DCD 0x07A00162
        DCD 0x07A00164
        DCD 0x07800166
        DCD 0x07600168
        DCD 0x0740016A
        DCD 0x0720016C
        DCD 0x0720016E
        DCD 0x07000170
        DCD 0x06E00172
        DCD 0x06C00174
        DCD 0x06C00176
        DCD 0x06A00178
        DCD 0x0680017A
        DCD 0x0660017C
        DCD 0x0660017E
        DCD 0x06400180
        DCD 0x06200182
        DCD 0x06000184
        DCD 0x05E00185
        DCD 0x05E00187
        DCD 0x05C00189
        DCD 0x05A0018B
        DCD 0x0580018D
        DCD 0x0580018F
        DCD 0x05600191
        DCD 0x05400193
        DCD 0x05200195
        DCD 0x05200197
        DCD 0x05000199
        DCD 0x04E0019B
        DCD 0x04C0019D
        DCD 0x04C0019F
        DCD 0x04A001A1
        DCD 0x048001A3
        DCD 0x046001A5
        DCD 0x044001A7
        DCD 0x044001A9
        DCD 0x042001AB
        DCD 0x040001AD
        DCD 0x03E001AF
        DCD 0x03E001B1
        DCD 0x03C001B3
        DCD 0x03A001B5
        DCD 0x038001B7
        DCD 0x038001B9
        DCD 0x036001BB
        DCD 0x034001BD
        DCD 0x032001BF
        DCD 0x032001C1
        DCD 0x030001C3
        DCD 0x02E001C5
        DCD 0x02C001C7
        DCD 0x02A001C9
        DCD 0x02A001CB
        DCD 0x028001CD
        DCD 0x026001CF
        DCD 0x024001D1
        DCD 0x024001D3
        DCD 0x022001D5
        DCD 0x020001D7
        DCD 0x01E001D8
        DCD 0x01E001DA
        DCD 0x01C001DC
        DCD 0x01A001DE
        DCD 0x018001E0
        DCD 0x016001E2
        DCD 0x016001E4
        DCD 0x014001E6
        DCD 0x012001E8
        DCD 0x010001EA
        DCD 0x010001EC
        DCD 0x00E001EE
        DCD 0x00C001F0
        DCD 0x00A001F2
        DCD 0x00A001F4
        DCD 0x008001F6
        DCD 0x006001F8
        DCD 0x004001FA
        DCD 0x004001FC
        DCD 0x002001FE
        DCD 0x00000200
        DCD 0xFFE00202
        DCD 0xFFC00204
        DCD 0xFFC00206
        DCD 0xFFA00208
        DCD 0xFF80020A
        DCD 0xFF60020C
        DCD 0xFF60020E
        DCD 0xFF400210
        DCD 0xFF200212
        DCD 0xFF000214
        DCD 0xFF000216
        DCD 0xFEE00218
        DCD 0xFEC0021A
        DCD 0xFEA0021C
        DCD 0xFEA0021E
        DCD 0xFE800220
        DCD 0xFE600222
        DCD 0xFE400224
        DCD 0xFE200226
        DCD 0xFE200228
        DCD 0xFE000229
        DCD 0xFDE0022B
        DCD 0xFDC0022D
        DCD 0xFDC0022F
        DCD 0xFDA00231
        DCD 0xFD800233
        DCD 0xFD600235
        DCD 0xFD600237
        DCD 0xFD400239
        DCD 0xFD20023B
        DCD 0xFD00023D
        DCD 0xFCE0023F
        DCD 0xFCE00241
        DCD 0xFCC00243
        DCD 0xFCA00245
        DCD 0xFC800247
        DCD 0xFC800249
        DCD 0xFC60024B
        DCD 0xFC40024D
        DCD 0xFC20024F
        DCD 0xFC200251
        DCD 0xFC000253
        DCD 0xFBE00255
        DCD 0xFBC00257
        DCD 0xFBC00259
        DCD 0xFBA0025B
        DCD 0xFB80025D
        DCD 0xFB60025F
        DCD 0xFB400261
        DCD 0xFB400263
        DCD 0xFB200265
        DCD 0xFB000267
        DCD 0xFAE00269
        DCD 0xFAE0026B
        DCD 0xFAC0026D
        DCD 0xFAA0026F
        DCD 0xFA800271
        DCD 0xFA800273
        DCD 0xFA600275
        DCD 0xFA400277
        DCD 0xFA200279
        DCD 0xFA20027B
        DCD 0xFA00027C
        DCD 0xF9E0027E
        DCD 0xF9C00280
        DCD 0xF9A00282
        DCD 0xF9A00284
        DCD 0xF9800286
        DCD 0xF9600288
        DCD 0xF940028A
        DCD 0xF940028C
        DCD 0xF920028E
        DCD 0xF9000290
        DCD 0xF8E00292
        DCD 0xF8E00294
        DCD 0xF8C00296
        DCD 0xF8A00298
        DCD 0xF880029A
        DCD 0xF860029C
        DCD 0xF860029E
        DCD 0xF84002A0
        DCD 0xF82002A2
        DCD 0xF80002A4
        DCD 0xF80002A6
        DCD 0xF7E002A8
        DCD 0xF7C002AA
        DCD 0xF7A002AC
        DCD 0xF7A002AE
        DCD 0xF78002B0
        DCD 0xF76002B2
        DCD 0xF74002B4
        DCD 0xF74002B6
        DCD 0xF72002B8
        DCD 0xF70002BA
        DCD 0xF6E002BC
        DCD 0xF6C002BE
        DCD 0xF6C002C0
        DCD 0xF6A002C2
        DCD 0xF68002C4
        DCD 0xF66002C6
        DCD 0xF66002C8
        DCD 0xF64002CA
        DCD 0xF62002CC
        DCD 0xF60002CE
        DCD 0xF60002CF
        DCD 0xF5E002D1
        DCD 0xF5C002D3
        DCD 0xF5A002D5
        DCD 0xF5A002D7
        DCD 0xF58002D9
        DCD 0xF56002DB
        DCD 0xF54002DD
        DCD 0xF52002DF
        DCD 0xF52002E1
        DCD 0xF50002E3
        DCD 0xF4E002E5
        DCD 0xF4C002E7
        DCD 0xF4C002E9
        DCD 0xF4A002EB
        DCD 0xF48002ED
        DCD 0xF46002EF
        DCD 0xF46002F1
        DCD 0xF44002F3
        DCD 0xF42002F5
        DCD 0xF40002F7
        DCD 0xF3E002F9
        DCD 0xF3E002FB
v_table
        DCD 0x1A09A000
        DCD 0x19E9A800
        DCD 0x19A9B800
        DCD 0x1969C800
        DCD 0x1949D000
        DCD 0x1909E000
        DCD 0x18C9E800
        DCD 0x18A9F800
        DCD 0x186A0000
        DCD 0x182A1000
        DCD 0x180A2000
        DCD 0x17CA2800
        DCD 0x17AA3800
        DCD 0x176A4000
        DCD 0x172A5000
        DCD 0x170A6000
        DCD 0x16CA6800
        DCD 0x168A7800
        DCD 0x166A8000
        DCD 0x162A9000
        DCD 0x160AA000
        DCD 0x15CAA800
        DCD 0x158AB800
        DCD 0x156AC000
        DCD 0x152AD000
        DCD 0x14EAE000
        DCD 0x14CAE800
        DCD 0x148AF800
        DCD 0x146B0000
        DCD 0x142B1000
        DCD 0x13EB2000
        DCD 0x13CB2800
        DCD 0x138B3800
        DCD 0x134B4000
        DCD 0x132B5000
        DCD 0x12EB6000
        DCD 0x12CB6800
        DCD 0x128B7800
        DCD 0x124B8000
        DCD 0x122B9000
        DCD 0x11EBA000
        DCD 0x11ABA800
        DCD 0x118BB800
        DCD 0x114BC000
        DCD 0x112BD000
        DCD 0x10EBE000
        DCD 0x10ABE800
        DCD 0x108BF800
        DCD 0x104C0000
        DCD 0x100C1000
        DCD 0x0FEC2000
        DCD 0x0FAC2800
        DCD 0x0F8C3800
        DCD 0x0F4C4000
        DCD 0x0F0C5000
        DCD 0x0EEC5800
        DCD 0x0EAC6800
        DCD 0x0E6C7800
        DCD 0x0E4C8000
        DCD 0x0E0C9000
        DCD 0x0DEC9800
        DCD 0x0DACA800
        DCD 0x0D6CB800
        DCD 0x0D4CC000
        DCD 0x0D0CD000
        DCD 0x0CCCD800
        DCD 0x0CACE800
        DCD 0x0C6CF800
        DCD 0x0C4D0000
        DCD 0x0C0D1000
        DCD 0x0BCD1800
        DCD 0x0BAD2800
        DCD 0x0B6D3800
        DCD 0x0B2D4000
        DCD 0x0B0D5000
        DCD 0x0ACD5800
        DCD 0x0AAD6800
        DCD 0x0A6D7800
        DCD 0x0A2D8000
        DCD 0x0A0D9000
        DCD 0x09CD9800
        DCD 0x098DA800
        DCD 0x096DB800
        DCD 0x092DC000
        DCD 0x090DD000
        DCD 0x08CDD800
        DCD 0x088DE800
        DCD 0x086DF800
        DCD 0x082E0000
        DCD 0x07EE1000
        DCD 0x07CE1800
        DCD 0x078E2800
        DCD 0x076E3800
        DCD 0x072E4000
        DCD 0x06EE5000
        DCD 0x06CE5800
        DCD 0x068E6800
        DCD 0x064E7800
        DCD 0x062E8000
        DCD 0x05EE9000
        DCD 0x05CE9800
        DCD 0x058EA800
        DCD 0x054EB800
        DCD 0x052EC000
        DCD 0x04EED000
        DCD 0x04AED800
        DCD 0x048EE800
        DCD 0x044EF000
        DCD 0x042F0000
        DCD 0x03EF1000
        DCD 0x03AF1800
        DCD 0x038F2800
        DCD 0x034F3000
        DCD 0x030F4000
        DCD 0x02EF5000
        DCD 0x02AF5800
        DCD 0x028F6800
        DCD 0x024F7000
        DCD 0x020F8000
        DCD 0x01EF9000
        DCD 0x01AF9800
        DCD 0x016FA800
        DCD 0x014FB000
        DCD 0x010FC000
        DCD 0x00EFD000
        DCD 0x00AFD800
        DCD 0x006FE800
        DCD 0x004FF000
        DCD 0x00100000
        DCD 0xFFD01000
        DCD 0xFFB01800
        DCD 0xFF702800
        DCD 0xFF303000
        DCD 0xFF104000
        DCD 0xFED05000
        DCD 0xFEB05800
        DCD 0xFE706800
        DCD 0xFE307000
        DCD 0xFE108000
        DCD 0xFDD09000
        DCD 0xFD909800
        DCD 0xFD70A800
        DCD 0xFD30B000
        DCD 0xFD10C000
        DCD 0xFCD0D000
        DCD 0xFC90D800
        DCD 0xFC70E800
        DCD 0xFC30F000
        DCD 0xFBF10000
        DCD 0xFBD11000
        DCD 0xFB911800
        DCD 0xFB712800
        DCD 0xFB313000
        DCD 0xFAF14000
        DCD 0xFAD14800
        DCD 0xFA915800
        DCD 0xFA516800
        DCD 0xFA317000
        DCD 0xF9F18000
        DCD 0xF9D18800
        DCD 0xF9919800
        DCD 0xF951A800
        DCD 0xF931B000
        DCD 0xF8F1C000
        DCD 0xF8B1C800
        DCD 0xF891D800
        DCD 0xF851E800
        DCD 0xF831F000
        DCD 0xF7F20000
        DCD 0xF7B20800
        DCD 0xF7921800
        DCD 0xF7522800
        DCD 0xF7123000
        DCD 0xF6F24000
        DCD 0xF6B24800
        DCD 0xF6925800
        DCD 0xF6526800
        DCD 0xF6127000
        DCD 0xF5F28000
        DCD 0xF5B28800
        DCD 0xF5729800
        DCD 0xF552A800
        DCD 0xF512B000
        DCD 0xF4F2C000
        DCD 0xF4B2C800
        DCD 0xF472D800
        DCD 0xF452E800
        DCD 0xF412F000
        DCD 0xF3D30000
        DCD 0xF3B30800
        DCD 0xF3731800
        DCD 0xF3532800
        DCD 0xF3133000
        DCD 0xF2D34000
        DCD 0xF2B34800
        DCD 0xF2735800
        DCD 0xF2336800
        DCD 0xF2137000
        DCD 0xF1D38000
        DCD 0xF1B38800
        DCD 0xF1739800
        DCD 0xF133A800
        DCD 0xF113B000
        DCD 0xF0D3C000
        DCD 0xF093C800
        DCD 0xF073D800
        DCD 0xF033E000
        DCD 0xF013F000
        DCD 0xEFD40000
        DCD 0xEF940800
        DCD 0xEF741800
        DCD 0xEF342000
        DCD 0xEEF43000
        DCD 0xEED44000
        DCD 0xEE944800
        DCD 0xEE745800
        DCD 0xEE346000
        DCD 0xEDF47000
        DCD 0xEDD48000
        DCD 0xED948800
        DCD 0xED549800
        DCD 0xED34A000
        DCD 0xECF4B000
        DCD 0xECD4C000
        DCD 0xEC94C800
        DCD 0xEC54D800
        DCD 0xEC34E000
        DCD 0xEBF4F000
        DCD 0xEBB50000
        DCD 0xEB950800
        DCD 0xEB551800
        DCD 0xEB352000
        DCD 0xEAF53000
        DCD 0xEAB54000
        DCD 0xEA954800
        DCD 0xEA555800
        DCD 0xEA156000
        DCD 0xE9F57000
        DCD 0xE9B58000
        DCD 0xE9958800
        DCD 0xE9559800
        DCD 0xE915A000
        DCD 0xE8F5B000
        DCD 0xE8B5C000
        DCD 0xE875C800
        DCD 0xE855D800
        DCD 0xE815E000
        DCD 0xE7F5F000
        DCD 0xE7B60000
        DCD 0xE7760800
        DCD 0xE7561800
        DCD 0xE7162000
        DCD 0xE6D63000
        DCD 0xE6B64000
        DCD 0xE6764800
        DCD 0xE6365800

	END
