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

	EXPORT	yuv420_2_rgb555

; void yuv420_to_rgb565
;  uint8_t *x_ptr
;  uint8_t *y_ptr
;  uint8_t *u_ptr
;  uint8_t *v_ptr
;  int      height
;  int      width
;  int      y_span
;  int      uv_span

DITH1	*	6
DITH2	*	7

yuv420_2_rgb555

	; r0 = x_ptr
	; r1 = y_ptr
	; r2 = u_ptr
	; r3 = v_ptr
	; <> = height
	; <> = width
	; <> = y_span
	; <> = uv_span
	STMFD	r13!,{r4-r11,r14}

	LDR	r8, [r13,#9*4]		; r8 = height
	LDR	r9, [r13,#10*4]		; r9 = width
	LDR	r10,[r13,#11*4]		; r10= y_span
	LDR	r4, =0x07C07C1F
	LDR	r5, =0x40040100
	MOV	r9, r9, LSL #1
yloop1
	SUB	r8, r8, r9, LSL #15	; r8 = height-(width<<16)
xloop1
	LDRB	r11,[r2], #1		; r11 = u  = *u_ptr++
	LDRB	r12,[r3], #1		; r12 = v  = *v_ptr++
	LDRB	r7, [r1, r10]		; r7  = y2 = y_ptr[stride]
	LDRB	r6, [r1], #1		; r6  = y0 = *y_ptr++
	ADR	r14,u_table
	LDR	r11,[r14,r11,LSL #2]	; r11 = u  = u_table[u]
	ADD	r14,r14,#1024		; r14 = v_table
	LDR	r12,[r14,r12,LSL #2]	; r12 = v  = v_table[v]
	ADD	r14,r14,#1024		; r14 = y_table
	LDR	r7, [r14,r7, LSL #2]	; r7  = y2 = y_table[y2]
	LDR	r6, [r14,r6, LSL #2]	; r6  = y0 = y_table[y0]
	ADD	r11,r11,r12		; r11 = uv = u+v

	ADD	r12,r11,r5, LSR #DITH1	; (dither 1/4)
	ADD	r7, r7, r12		; r7  = y0 + uv
	ADD	r6, r6, r12		; r6  = y2 + uv
	ADD	r6, r6, r5, LSR #DITH2	; (dither 3/4)
	ANDS	r12,r7, r5
	TSTEQ	r6, r5
	BNE	fix101
return101
	AND	r7, r4, r7, LSR #3
	ORR	r7, r7, r7, LSR #17
	BIC	r7,r7,#0x8000
	STRH	r7, [r0, r9]
	AND	r6, r4, r6, LSR #3
	ORR	r6, r6, r6, LSR #17
	LDRB	r12,[r1, r10]		; r12 = y3 = y_ptr[stride]
	LDRB	r7, [r1], #1		; r6  = y1 = *y_ptr++
	BIC	r6,r6,#0x8000
	STRH	r6, [r0], #2

	LDR	r12,[r14, r12,LSL #2]	; r7  = y3 = y_table[y2]
	LDR	r6, [r14, r7, LSL #2]	; r6  = y1 = y_table[y0]

	ADD	r7, r12,r11		; r7  = y3 + uv
	ADD	r6, r6, r11		; r6  = y1 + uv
	ADD	r7, r7, r5, LSR #DITH2	; (dither 2/4)
	ANDS	r12,r7, r5
	TSTEQ	r6, r5
	BNE	fix102
return102
	AND	r7, r4, r7, LSR #3
	AND	r6, r4, r6, LSR #3
	ORR	r7, r7, r7, LSR #17
	ORR	r6, r6, r6, LSR #17
	BIC	r7,r7,#0x8000
	BIC	r6,r6,#0x8000
	STRH	r7, [r0, r9]
	STRH	r6, [r0], #2
	ADDS	r8, r8, #2<<16
	BLT	xloop1

	LDR	r11,[r13,#12*4]		; r11 = uv_stride
	;ADD	r0, r0, #2*X_STRIDE - 2*WIDTH	; x_ptr to next line
	;ADD	r1, r1, #2*Y_STRIDE -   WIDTH	; y_ptr to next line
	ADD	r0, r0, r9
	ADD	r1, r1, r10,LSL #1
	SUB	r1, r1, r9, LSR #1
	SUB	r2, r2, r9, LSR #2
	SUB	r3, r3, r9, LSR #2
	ADD	r2, r2, r11
	ADD	r3, r3, r11

	SUBS	r8, r8, #2
	BGT	yloop1

	LDMFD	r13!,{r4-r11, pc}
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

u_table
        DCD	0x0C440C00
        DCD	0x0C341400
        DCD	0x0C141C00
        DCD	0x0C042400
        DCD	0x0BE42C00
        DCD	0x0BC43400
        DCD	0x0BB43C00
        DCD	0x0B944400
        DCD	0x0B844C00
        DCD	0x0B645400
        DCD	0x0B545C00
        DCD	0x0B346400
        DCD	0x0B246C00
        DCD	0x0B047400
        DCD	0x0AF47C00
        DCD	0x0AD48400
        DCD	0x0AC48C00
        DCD	0x0AA49400
        DCD	0x0A949C00
        DCD	0x0A74A400
        DCD	0x0A54AC00
        DCD	0x0A44B400
        DCD	0x0A24BC00
        DCD	0x0A14C400
        DCD	0x09F4C800
        DCD	0x09E4D000
        DCD	0x09C4D800
        DCD	0x09B4E000
        DCD	0x0994E800
        DCD	0x0984F000
        DCD	0x0964F800
        DCD	0x09550000
        DCD	0x09350800
        DCD	0x09251000
        DCD	0x09051800
        DCD	0x08E52000
        DCD	0x08D52800
        DCD	0x08B53000
        DCD	0x08A53800
        DCD	0x08854000
        DCD	0x08754800
        DCD	0x08555000
        DCD	0x08455800
        DCD	0x08256000
        DCD	0x08156800
        DCD	0x07F57000
        DCD	0x07E57800
        DCD	0x07C58000
        DCD	0x07B58800
        DCD	0x07959000
        DCD	0x07759800
        DCD	0x0765A000
        DCD	0x0745A800
        DCD	0x0735B000
        DCD	0x0715B800
        DCD	0x0705C000
        DCD	0x06E5C800
        DCD	0x06D5D000
        DCD	0x06B5D800
        DCD	0x06A5E000
        DCD	0x0685E800
        DCD	0x0675F000
        DCD	0x0655F800
        DCD	0x06460000
        DCD	0x06260800
        DCD	0x06161000
        DCD	0x05F61400
        DCD	0x05D61C00
        DCD	0x05C62400
        DCD	0x05A62C00
        DCD	0x05963400
        DCD	0x05763C00
        DCD	0x05664400
        DCD	0x05464C00
        DCD	0x05365400
        DCD	0x05165C00
        DCD	0x05066400
        DCD	0x04E66C00
        DCD	0x04D67400
        DCD	0x04B67C00
        DCD	0x04A68400
        DCD	0x04868C00
        DCD	0x04669400
        DCD	0x04569C00
        DCD	0x0436A400
        DCD	0x0426AC00
        DCD	0x0406B400
        DCD	0x03F6BC00
        DCD	0x03D6C400
        DCD	0x03C6CC00
        DCD	0x03A6D400
        DCD	0x0396DC00
        DCD	0x0376E400
        DCD	0x0366EC00
        DCD	0x0346F400
        DCD	0x0336FC00
        DCD	0x03170400
        DCD	0x02F70C00
        DCD	0x02E71400
        DCD	0x02C71C00
        DCD	0x02B72400
        DCD	0x02972C00
        DCD	0x02873400
        DCD	0x02673C00
        DCD	0x02574400
        DCD	0x02374C00
        DCD	0x02275400
        DCD	0x02075C00
        DCD	0x01F76000
        DCD	0x01D76800
        DCD	0x01C77000
        DCD	0x01A77800
        DCD	0x01978000
        DCD	0x01778800
        DCD	0x01579000
        DCD	0x01479800
        DCD	0x0127A000
        DCD	0x0117A800
        DCD	0x00F7B000
        DCD	0x00E7B800
        DCD	0x00C7C000
        DCD	0x00B7C800
        DCD	0x0097D000
        DCD	0x0087D800
        DCD	0x0067E000
        DCD	0x0057E800
        DCD	0x0037F000
        DCD	0x0027F800
        DCD	0x00080000
        DCD	0xFFE80800
        DCD	0xFFD81000
        DCD	0xFFB81800
        DCD	0xFFA82000
        DCD	0xFF882800
        DCD	0xFF783000
        DCD	0xFF583800
        DCD	0xFF484000
        DCD	0xFF284800
        DCD	0xFF185000
        DCD	0xFEF85800
        DCD	0xFEE86000
        DCD	0xFEC86800
        DCD	0xFEB87000
        DCD	0xFE987800
        DCD	0xFE788000
        DCD	0xFE688800
        DCD	0xFE489000
        DCD	0xFE389800
        DCD	0xFE18A000
        DCD	0xFE08A400
        DCD	0xFDE8AC00
        DCD	0xFDD8B400
        DCD	0xFDB8BC00
        DCD	0xFDA8C400
        DCD	0xFD88CC00
        DCD	0xFD78D400
        DCD	0xFD58DC00
        DCD	0xFD48E400
        DCD	0xFD28EC00
        DCD	0xFD18F400
        DCD	0xFCF8FC00
        DCD	0xFCD90400
        DCD	0xFCC90C00
        DCD	0xFCA91400
        DCD	0xFC991C00
        DCD	0xFC792400
        DCD	0xFC692C00
        DCD	0xFC493400
        DCD	0xFC393C00
        DCD	0xFC194400
        DCD	0xFC094C00
        DCD	0xFBE95400
        DCD	0xFBD95C00
        DCD	0xFBB96400
        DCD	0xFBA96C00
        DCD	0xFB897400
        DCD	0xFB697C00
        DCD	0xFB598400
        DCD	0xFB398C00
        DCD	0xFB299400
        DCD	0xFB099C00
        DCD	0xFAF9A400
        DCD	0xFAD9AC00
        DCD	0xFAC9B400
        DCD	0xFAA9BC00
        DCD	0xFA99C400
        DCD	0xFA79CC00
        DCD	0xFA69D400
        DCD	0xFA49DC00
        DCD	0xFA39E400
        DCD	0xFA19EC00
        DCD	0xF9F9F000
        DCD	0xF9E9F800
        DCD	0xF9CA0000
        DCD	0xF9BA0800
        DCD	0xF99A1000
        DCD	0xF98A1800
        DCD	0xF96A2000
        DCD	0xF95A2800
        DCD	0xF93A3000
        DCD	0xF92A3800
        DCD	0xF90A4000
        DCD	0xF8FA4800
        DCD	0xF8DA5000
        DCD	0xF8CA5800
        DCD	0xF8AA6000
        DCD	0xF89A6800
        DCD	0xF87A7000
        DCD	0xF85A7800
        DCD	0xF84A8000
        DCD	0xF82A8800
        DCD	0xF81A9000
        DCD	0xF7FA9800
        DCD	0xF7EAA000
        DCD	0xF7CAA800
        DCD	0xF7BAB000
        DCD	0xF79AB800
        DCD	0xF78AC000
        DCD	0xF76AC800
        DCD	0xF75AD000
        DCD	0xF73AD800
        DCD	0xF72AE000
        DCD	0xF70AE800
        DCD	0xF6EAF000
        DCD	0xF6DAF800
        DCD	0xF6BB0000
        DCD	0xF6AB0800
        DCD	0xF68B1000
        DCD	0xF67B1800
        DCD	0xF65B2000
        DCD	0xF64B2800
        DCD	0xF62B3000
        DCD	0xF61B3800
        DCD	0xF5FB3C00
        DCD	0xF5EB4400
        DCD	0xF5CB4C00
        DCD	0xF5BB5400
        DCD	0xF59B5C00
        DCD	0xF57B6400
        DCD	0xF56B6C00
        DCD	0xF54B7400
        DCD	0xF53B7C00
        DCD	0xF51B8400
        DCD	0xF50B8C00
        DCD	0xF4EB9400
        DCD	0xF4DB9C00
        DCD	0xF4BBA400
        DCD	0xF4ABAC00
        DCD	0xF48BB400
        DCD	0xF47BBC00
        DCD	0xF45BC400
        DCD	0xF44BCC00
        DCD	0xF42BD400
        DCD	0xF41BDC00
        DCD	0xF3FBE400
        DCD	0xF3DBEC00
v_table
        DCD	0x1A000134
        DCD	0x19D00135
        DCD	0x19A00137
        DCD	0x19700139
        DCD	0x1930013A
        DCD	0x1900013C
        DCD	0x18D0013D
        DCD	0x1890013F
        DCD	0x18600140
        DCD	0x18300142
        DCD	0x18000144
        DCD	0x17C00145
        DCD	0x17900147
        DCD	0x17600148
        DCD	0x1730014A
        DCD	0x16F0014C
        DCD	0x16C0014D
        DCD	0x1690014F
        DCD	0x16600150
        DCD	0x16200152
        DCD	0x15F00154
        DCD	0x15C00155
        DCD	0x15900157
        DCD	0x15500158
        DCD	0x1520015A
        DCD	0x14F0015C
        DCD	0x14C0015D
        DCD	0x1480015F
        DCD	0x14500160
        DCD	0x14200162
        DCD	0x13F00164
        DCD	0x13B00165
        DCD	0x13800167
        DCD	0x13500168
        DCD	0x1320016A
        DCD	0x12E0016C
        DCD	0x12B0016D
        DCD	0x1280016F
        DCD	0x12500170
        DCD	0x12100172
        DCD	0x11E00174
        DCD	0x11B00175
        DCD	0x11800177
        DCD	0x11400178
        DCD	0x1110017A
        DCD	0x10E0017C
        DCD	0x10B0017D
        DCD	0x1070017F
        DCD	0x10400180
        DCD	0x10100182
        DCD	0x0FE00184
        DCD	0x0FA00185
        DCD	0x0F700187
        DCD	0x0F400188
        DCD	0x0F10018A
        DCD	0x0ED0018B
        DCD	0x0EA0018D
        DCD	0x0E70018F
        DCD	0x0E400190
        DCD	0x0E000192
        DCD	0x0DD00193
        DCD	0x0DA00195
        DCD	0x0D700197
        DCD	0x0D300198
        DCD	0x0D00019A
        DCD	0x0CD0019B
        DCD	0x0CA0019D
        DCD	0x0C60019F
        DCD	0x0C3001A0
        DCD	0x0C0001A2
        DCD	0x0BD001A3
        DCD	0x0B9001A5
        DCD	0x0B6001A7
        DCD	0x0B3001A8
        DCD	0x0B0001AA
        DCD	0x0AC001AB
        DCD	0x0A9001AD
        DCD	0x0A6001AF
        DCD	0x0A3001B0
        DCD	0x09F001B2
        DCD	0x09C001B3
        DCD	0x099001B5
        DCD	0x096001B7
        DCD	0x092001B8
        DCD	0x08F001BA
        DCD	0x08C001BB
        DCD	0x089001BD
        DCD	0x085001BF
        DCD	0x082001C0
        DCD	0x07F001C2
        DCD	0x07C001C3
        DCD	0x078001C5
        DCD	0x075001C7
        DCD	0x072001C8
        DCD	0x06F001CA
        DCD	0x06B001CB
        DCD	0x068001CD
        DCD	0x065001CF
        DCD	0x062001D0
        DCD	0x05E001D2
        DCD	0x05B001D3
        DCD	0x058001D5
        DCD	0x055001D7
        DCD	0x051001D8
        DCD	0x04E001DA
        DCD	0x04B001DB
        DCD	0x048001DD
        DCD	0x044001DE
        DCD	0x041001E0
        DCD	0x03E001E2
        DCD	0x03B001E3
        DCD	0x037001E5
        DCD	0x034001E6
        DCD	0x031001E8
        DCD	0x02E001EA
        DCD	0x02A001EB
        DCD	0x027001ED
        DCD	0x024001EE
        DCD	0x021001F0
        DCD	0x01D001F2
        DCD	0x01A001F3
        DCD	0x017001F5
        DCD	0x014001F6
        DCD	0x010001F8
        DCD	0x00D001FA
        DCD	0x00A001FB
        DCD	0x007001FD
        DCD	0x003001FE
        DCD	0x00000200
        DCD	0xFFD00202
        DCD	0xFF900203
        DCD	0xFF600205
        DCD	0xFF300206
        DCD	0xFF000208
        DCD	0xFEC0020A
        DCD	0xFE90020B
        DCD	0xFE60020D
        DCD	0xFE30020E
        DCD	0xFDF00210
        DCD	0xFDC00212
        DCD	0xFD900213
        DCD	0xFD600215
        DCD	0xFD200216
        DCD	0xFCF00218
        DCD	0xFCC0021A
        DCD	0xFC90021B
        DCD	0xFC50021D
        DCD	0xFC20021E
        DCD	0xFBF00220
        DCD	0xFBC00222
        DCD	0xFB800223
        DCD	0xFB500225
        DCD	0xFB200226
        DCD	0xFAF00228
        DCD	0xFAB00229
        DCD	0xFA80022B
        DCD	0xFA50022D
        DCD	0xFA20022E
        DCD	0xF9E00230
        DCD	0xF9B00231
        DCD	0xF9800233
        DCD	0xF9500235
        DCD	0xF9100236
        DCD	0xF8E00238
        DCD	0xF8B00239
        DCD	0xF880023B
        DCD	0xF840023D
        DCD	0xF810023E
        DCD	0xF7E00240
        DCD	0xF7B00241
        DCD	0xF7700243
        DCD	0xF7400245
        DCD	0xF7100246
        DCD	0xF6E00248
        DCD	0xF6A00249
        DCD	0xF670024B
        DCD	0xF640024D
        DCD	0xF610024E
        DCD	0xF5D00250
        DCD	0xF5A00251
        DCD	0xF5700253
        DCD	0xF5400255
        DCD	0xF5000256
        DCD	0xF4D00258
        DCD	0xF4A00259
        DCD	0xF470025B
        DCD	0xF430025D
        DCD	0xF400025E
        DCD	0xF3D00260
        DCD	0xF3A00261
        DCD	0xF3600263
        DCD	0xF3300265
        DCD	0xF3000266
        DCD	0xF2D00268
        DCD	0xF2900269
        DCD	0xF260026B
        DCD	0xF230026D
        DCD	0xF200026E
        DCD	0xF1C00270
        DCD	0xF1900271
        DCD	0xF1600273
        DCD	0xF1300275
        DCD	0xF0F00276
        DCD	0xF0C00278
        DCD	0xF0900279
        DCD	0xF060027B
        DCD	0xF020027C
        DCD	0xEFF0027E
        DCD	0xEFC00280
        DCD	0xEF900281
        DCD	0xEF500283
        DCD	0xEF200284
        DCD	0xEEF00286
        DCD	0xEEC00288
        DCD	0xEE800289
        DCD	0xEE50028B
        DCD	0xEE20028C
        DCD	0xEDF0028E
        DCD	0xEDB00290
        DCD	0xED800291
        DCD	0xED500293
        DCD	0xED200294
        DCD	0xECE00296
        DCD	0xECB00298
        DCD	0xEC800299
        DCD	0xEC50029B
        DCD	0xEC10029C
        DCD	0xEBE0029E
        DCD	0xEBB002A0
        DCD	0xEB8002A1
        DCD	0xEB4002A3
        DCD	0xEB1002A4
        DCD	0xEAE002A6
        DCD	0xEAB002A8
        DCD	0xEA7002A9
        DCD	0xEA4002AB
        DCD	0xEA1002AC
        DCD	0xE9E002AE
        DCD	0xE9A002B0
        DCD	0xE97002B1
        DCD	0xE94002B3
        DCD	0xE91002B4
        DCD	0xE8D002B6
        DCD	0xE8A002B8
        DCD	0xE87002B9
        DCD	0xE84002BB
        DCD	0xE80002BC
        DCD	0xE7D002BE
        DCD	0xE7A002C0
        DCD	0xE77002C1
        DCD	0xE73002C3
        DCD	0xE70002C4
        DCD	0xE6D002C6
        DCD	0xE6A002C8
        DCD	0xE66002C9
        DCD	0xE63002CB
y_table
        DCD	0x7FFFFFED
        DCD	0x7FFFFFEF
        DCD	0x7FFFFFF0
        DCD	0x7FFFFFF1
        DCD	0x7FFFFFF2
        DCD	0x7FFFFFF3
        DCD	0x7FFFFFF4
        DCD	0x7FFFFFF6
        DCD	0x7FFFFFF7
        DCD	0x7FFFFFF8
        DCD	0x7FFFFFF9
        DCD	0x7FFFFFFA
        DCD	0x7FFFFFFB
        DCD	0x7FFFFFFD
        DCD	0x7FFFFFFE
        DCD	0x7FFFFFFF
        DCD	0x80000000
        DCD	0x80500401
        DCD	0x80900802
        DCD	0x80E00C03
        DCD	0x81301405
        DCD	0x81701806
        DCD	0x81C01C07
        DCD	0x82102008
        DCD	0x82502409
        DCD	0x82A0280A
        DCD	0x82F0300C
        DCD	0x8330340D
        DCD	0x8380380E
        DCD	0x83D03C0F
        DCD	0x84104010
        DCD	0x84604411
        DCD	0x84A04C13
        DCD	0x84F05014
        DCD	0x85405415
        DCD	0x85805816
        DCD	0x85D05C17
        DCD	0x86206018
        DCD	0x8660681A
        DCD	0x86B06C1B
        DCD	0x8700701C
        DCD	0x8740741D
        DCD	0x8790781E
        DCD	0x87E07C1F
        DCD	0x88208421
        DCD	0x88708822
        DCD	0x88C08C23
        DCD	0x89009024
        DCD	0x89509425
        DCD	0x89A09826
        DCD	0x89E0A028
        DCD	0x8A30A429
        DCD	0x8A80A82A
        DCD	0x8AC0AC2B
        DCD	0x8B10B02C
        DCD	0x8B60B42D
        DCD	0x8BA0BC2F
        DCD	0x8BF0C030
        DCD	0x8C40C431
        DCD	0x8C80C832
        DCD	0x8CD0CC33
        DCD	0x8D20D034
        DCD	0x8D60D836
        DCD	0x8DB0DC37
        DCD	0x8DF0E038
        DCD	0x8E40E439
        DCD	0x8E90E83A
        DCD	0x8ED0EC3B
        DCD	0x8F20F43D
        DCD	0x8F70F83E
        DCD	0x8FB0FC3F
        DCD	0x90010040
        DCD	0x90510441
        DCD	0x90910842
        DCD	0x90E11044
        DCD	0x91311445
        DCD	0x91711846
        DCD	0x91C11C47
        DCD	0x92112048
        DCD	0x92512449
        DCD	0x92A1284A
        DCD	0x92F1304C
        DCD	0x9331344D
        DCD	0x9381384E
        DCD	0x93D13C4F
        DCD	0x94114050
        DCD	0x94614451
        DCD	0x94B14C53
        DCD	0x94F15054
        DCD	0x95415455
        DCD	0x95915856
        DCD	0x95D15C57
        DCD	0x96216058
        DCD	0x9671685A
        DCD	0x96B16C5B
        DCD	0x9701705C
        DCD	0x9741745D
        DCD	0x9791785E
        DCD	0x97E17C5F
        DCD	0x98218461
        DCD	0x98718862
        DCD	0x98C18C63
        DCD	0x99019064
        DCD	0x99519465
        DCD	0x99A19866
        DCD	0x99E1A068
        DCD	0x9A31A469
        DCD	0x9A81A86A
        DCD	0x9AC1AC6B
        DCD	0x9B11B06C
        DCD	0x9B61B46D
        DCD	0x9BA1BC6F
        DCD	0x9BF1C070
        DCD	0x9C41C471
        DCD	0x9C81C872
        DCD	0x9CD1CC73
        DCD	0x9D21D074
        DCD	0x9D61D876
        DCD	0x9DB1DC77
        DCD	0x9E01E078
        DCD	0x9E41E479
        DCD	0x9E91E87A
        DCD	0x9EE1EC7B
        DCD	0x9F21F47D
        DCD	0x9F71F87E
        DCD	0x9FC1FC7F
        DCD	0xA0020080
        DCD	0xA0520481
        DCD	0xA0920882
        DCD	0xA0E21084
        DCD	0xA1321485
        DCD	0xA1721886
        DCD	0xA1C21C87
        DCD	0xA2122088
        DCD	0xA2522489
        DCD	0xA2A22C8B
        DCD	0xA2F2308C
        DCD	0xA332348D
        DCD	0xA382388E
        DCD	0xA3D23C8F
        DCD	0xA4124090
        DCD	0xA4624892
        DCD	0xA4B24C93
        DCD	0xA4F25094
        DCD	0xA5425495
        DCD	0xA5925896
        DCD	0xA5D25C97
        DCD	0xA6226098
        DCD	0xA672689A
        DCD	0xA6B26C9B
        DCD	0xA702709C
        DCD	0xA752749D
        DCD	0xA792789E
        DCD	0xA7E27C9F
        DCD	0xA83284A1
        DCD	0xA87288A2
        DCD	0xA8C28CA3
        DCD	0xA90290A4
        DCD	0xA95294A5
        DCD	0xA9A298A6
        DCD	0xA9E2A0A8
        DCD	0xAA32A4A9
        DCD	0xAA82A8AA
        DCD	0xAAC2ACAB
        DCD	0xAB12B0AC
        DCD	0xAB62B4AD
        DCD	0xABA2BCAF
        DCD	0xABF2C0B0
        DCD	0xAC42C4B1
        DCD	0xAC82C8B2
        DCD	0xACD2CCB3
        DCD	0xAD22D0B4
        DCD	0xAD62D8B6
        DCD	0xADB2DCB7
        DCD	0xAE02E0B8
        DCD	0xAE42E4B9
        DCD	0xAE92E8BA
        DCD	0xAEE2ECBB
        DCD	0xAF22F4BD
        DCD	0xAF72F8BE
        DCD	0xAFC2FCBF
        DCD	0xB00300C0
        DCD	0xB05304C1
        DCD	0xB0A308C2
        DCD	0xB0E310C4
        DCD	0xB13314C5
        DCD	0xB18318C6
        DCD	0xB1C31CC7
        DCD	0xB21320C8
        DCD	0xB25324C9
        DCD	0xB2A32CCB
        DCD	0xB2F330CC
        DCD	0xB33334CD
        DCD	0xB38338CE
        DCD	0xB3D33CCF
        DCD	0xB41340D0
        DCD	0xB46348D2
        DCD	0xB4B34CD3
        DCD	0xB4F350D4
        DCD	0xB54354D5
        DCD	0xB59358D6
        DCD	0xB5D35CD7
        DCD	0xB62364D9
        DCD	0xB67368DA
        DCD	0xB6B36CDB
        DCD	0xB70370DC
        DCD	0xB75374DD
        DCD	0xB79378DE
        DCD	0xB7E37CDF
        DCD	0xB83384E1
        DCD	0xB87388E2
        DCD	0xB8C38CE3
        DCD	0xB91390E4
        DCD	0xB95394E5
        DCD	0xB9A398E6
        DCD	0xB9F3A0E8
        DCD	0xBA33A4E9
        DCD	0xBA83A8EA
        DCD	0xBAD3ACEB
        DCD	0xBB13B0EC
        DCD	0xBB63B4ED
        DCD	0xBBA3BCEF
        DCD	0xBBF3C0F0
        DCD	0xBC43C4F1
        DCD	0xBC83C8F2
        DCD	0xBCD3CCF3
        DCD	0xBD23D0F4
        DCD	0xBD63D8F6
        DCD	0xBDB3DCF7
        DCD	0xBE03E0F8
        DCD	0xBE43E4F9
        DCD	0xBE93E8FA
        DCD	0xBEE3ECFB
        DCD	0xBF23F4FD
        DCD	0xBF73F8FE
        DCD	0xBFC3FCFF
        DCD	0xC0040100
        DCD	0xC0540501
        DCD	0xC0A40902
        DCD	0xC0E41104
        DCD	0xC0E41104
        DCD	0xC0E41104
        DCD	0xC0E41104
        DCD	0xC0E41104
        DCD	0xC0E41104
        DCD	0xC0E41104
        DCD	0xC0E41104
        DCD	0xC0E41104
        DCD	0xC0E41104
        DCD	0xC0E41104
        DCD	0xC0E41104
        DCD	0xC0E41104
        DCD	0xC0E41104
        DCD	0xC0E41104
        DCD	0xC0E41104
        DCD	0xC0E41104

	END
