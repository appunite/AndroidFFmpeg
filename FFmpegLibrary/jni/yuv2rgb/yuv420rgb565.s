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
	LDR	r11,[r14,r11,LSL #2]	; r11 = u  = u_table[u]
	LDR	r12,[r14,r12,LSL #2]	; r12 = v  = v_table[v]
	LDR	r7, [r14,r7, LSL #2]	; r7  = y2 = y_table[y2]
	LDR	r6, [r14,r6, LSL #2]	; r6  = y0 = y_table[y0]
	ADD	r11,r11,r12		; r11 = uv = u+v

	ADD	r12,r11,r5, LSR #DITH1
	ADD	r7, r7, r12		; r7  = y2 + uv + dither1
	ADD	r6, r6, r12		; r6  = y0 + uv + dither1
	ADD	r7, r7, r5, LSR #DITH2	; r7  = y2 + uv + dither3
	ANDS	r12,r7, r5
	TSTEQ	r6, r5
	BNE	fix101
return101
	AND	r7, r4, r7, LSR #3
	AND	r6, r4, r6, LSR #3
	ORR	r7, r7, r7, LSR #16
	STRH	r7, [r0, r9]
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
	ADD	r1, r1, r10,LSL #1
	SUB	r0, r0, r11,LSL #1
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
	ADD	r7, r7, r11		; r7  = y1 + uv
	ADD	r6, r6, r5, LSR #DITH1	; r6  = y0 + uv + dither1
	ADD	r7, r7, r5, LSR #DITH2	; r7  = y1 + uv + dither2
	ANDS	r12,r7, r5
	TSTEQ	r6, r5
	BNE	fix104
return104
	AND	r6, r4, r6, LSR #3
	AND	r7, r4, r7, LSR #3
	ORR	r6, r6, r6, LSR #16
	ORR	r7, r7, r7, LSR #16
	STRH	r6, [r0], #2
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
	AND	r6, r4, r6, LSR #3
	ORR	r7, r7, r7, LSR #16
	ORR	r6, r6, r6, LSR #16
	STRH	r7, [r0, r9]
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
	AND	r6, r4, r6, LSR #3
	ORR	r7, r7, r7, LSR #16
	STRH	r7, [r0, r9]
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
	AND	r7, r4, r7, LSR #3
	ORR	r6, r6, r6, LSR #16
	ORR	r7, r7, r7, LSR #16
	STRH	r6, [r0], #2
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
	AND	r6, r4, r6, LSR #3
	ORR	r7, r7, r7, LSR #16
	ORR	r6, r6, r6, LSR #16
	STRH	r7, [r0, r9]
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
	AND	r6, r4, r6, LSR #3
	ORR	r7, r7, r7, LSR #16
	STRH	r7, [r0, r9]
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
	ADD	r1, r1, r10,LSL #1
	SUB	r0, r0, r11,LSL #1
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
	LDR	r6, [r14,r6, LSL #2]	; r6  = y0 = y_table[y0]
	LDR	r7, [r14,r7, LSL #2]	; r7  = y1 = y_table[y1]
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
	AND	r7, r4, r7, LSR #3
	ORR	r6, r6, r6, LSR #16
	ORR	r7, r7, r7, LSR #16
	STRH	r6, [r0], #2
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
	AND	r6, r4, r6, LSR #3
	ORR	r7, r7, r7, LSR #16
	ORR	r6, r6, r6, LSR #16
	STRH	r7, [r0, r9]
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
	AND	r6, r4, r6, LSR #3
	ORR	r7, r7, r7, LSR #16
	STRH	r7, [r0, r9]
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
	ADD	r7, r7, r11		; r7  = y1 + uv
	ADD	r6, r6, r5, LSR #DITH2	; r6  = y0 + uv + dither3
	ANDS	r12,r7, r5
	TSTEQ	r6, r5
	BNE	fix304
return304
	AND	r6, r4, r6, LSR #3
	AND	r7, r4, r7, LSR #3
	ORR	r6, r6, r6, LSR #16
	ORR	r7, r7, r7, LSR #16
	STRH	r6, [r0], #2
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
	AND	r6, r4, r6, LSR #3
	ORR	r7, r7, r7, LSR #16
	ORR	r6, r6, r6, LSR #16
	STRH	r7, [r0, r9]
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

	END
