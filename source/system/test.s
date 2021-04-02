.data      
.align 4
.global memcpy_asm_4b
.global memcpy_asm
.global test
.global memory_test
.global arg_test
.global test_set
.global set_yuv
.global read_b
.global read_g
.global read_r
.type memcpy_asm, "function"
.type memcpy_asm_4b, "function"
.type test, "function"
.type memory_test, "function"
.type arg_test, "function"
.type test_set, "function"
.type set_yuv, "function"
.type read_b, "function"
.type read_g, "function"
.type read_r, "function"

mul_value_y: .int 298
mul_value_ug: .int 100
mul_value_ub: .int 516
mul_value_vr: .int 409
mul_value_vg: .int 208
color_b: .int 0
color_g: .int 0
color_r: .int 0
value_0: .int 0

//#define C(Y) ( (Y) - 16  )
//#define D(U) ( (U) - 128 )
//#define E(V) ( (V) - 128 )

//#define YUV2R(Y, V) CLIP(( 298 * C(Y)              + 409 * E(V) + 128) >> 8)
//#define YUV2G(Y, U, V) CLIP(( 298 * C(Y) - 100 * D(U) - 208 * E(V) + 128) >> 8)
//#define YUV2B(Y, U) CLIP(( 298 * C(Y) + 516 * D(U)              + 128) >> 8)

.text
memcpy_asm:
    push { r4-r11 }
    mov r11, r0
    add r11, r2
    sub r11, #1

    cpy_loop:
    cmp r0, r11
    bgt cpy_end
    ldm r1, { r3-r10 }
    stm r0, { r3-r10 }
    add r1, #32
    add r0, #32
    b cpy_loop
    cpy_end:
    pop { r4-r11 }
    bx lr

memcpy_asm_4b:
    ldr r2, [r1]
    str r2, [r0]
    bx lr

test:
    ldr r0, =0x10000000
    mov r1, #0

    test_loop:
    cmp r1, r0
    bgt test_end
    add r1, #1
    b test_loop
    test_end:
    bx lr

memory_test:
    ldr r0, =0x10000000
    ldr r1, =value_0

    memory_test_loop:
    ldr r2, [r1]
    cmp r2, r0
    bgt memory_test_end
    add r2, #1
    str r2, [r1]
    b memory_test_loop
    memory_test_end:
    bx lr

arg_test:
//    ldr r4, [sp]
    bkpt
    bx lr

test_set:
    push { r4-r6 }
    //setup fill data
    mov r4, r1
    mov r5, r1
    mov r6, r1
    lsl r4, #24
    lsl r5, #16
    lsl r6, #8
    add r1, r4
    add r1, r5
    add r1, r6
    mov r3, r1
    mov r4, r1
    mov r5, r1
    mov r6, r1

    add r2, r0
    sub r2, #1

    start:
    cmp r0, r2
    bgt end
    stm r0, { r3-r6 }
    add r0, #16
    b start
    end:
    pop { r4-r6 }
    bx lr

yuv_bgr555:
    push { r3-r9, r10-r11 }
    sub r0, #16
    sub r1, #128
    sub r2, #128
    ldr r3, =mul_value_y
    ldr r3, [r3]
    ldr r4, =mul_value_ub
    ldr r4, [r4]
    mul r5, r0, r3
    mul r6, r1, r4
    add r10, r5, r6
    add r10, #128
    asr r10, #8
    cmp r10, #255
    movgt r10, #255
    cmp r10, #0
    movlt r10, #0
    asr r10, #3
    lsl r10, #8

    ldr r4, =mul_value_vr
    ldr r4, [r4]
    mul r6, r2, r4
    add r11, r5, r6
    add r11, #128
    asr r11, #8
    cmp r11, #255
    movgt r11, #255
    cmp r11, #0
    movlt r11, #0
    asr r11, #3
    lsl r11, #8

    ldr r4, =mul_value_ug
    ldr r4, [r4]
    ldr r6, =mul_value_vg
    ldr r6, [r6]
    mul r7, r4, r1
    mul r8, r6, r2
    sub r5, r7
    sub r5, r8
    add r5, #128
    asr r5, #8
    cmp r5, #255
    movgt r5, #255
    cmp r5, #0
    movlt r5, #0
    mov r9, r5, asl #27
    asr r5, #5
    
    asr r9, #29
    lsl r9, #13

    mov r0, r11
//    add r0, r11, r9
  //  add r0, r5
    //add r0, r10

    pop { r3-r9, r10-r11 }
    bx lr
    //0~3 5 10~11

/*

yuv_bgr555:
    push { r3-r8, r10-r11 }
    sub r0, #16
    sub r1, #128
    sub r2, #128
    ldr r3, =mul_value_y
    ldr r3, [r3]
    ldr r4, =mul_value_ub
    ldr r4, [r4]
    mul r5, r0, r3
    mul r6, r1, r4
    add r10, r5, r6
    add r10, #128
    asr r10, #8
    cmp r10, #255
    movgt r10, #255
    cmp r10, #0
    movlt r10, #0
    asr r10, #3
    lsl r10, #11

    ldr r4, =mul_value_vr
    ldr r4, [r4]
    mul r6, r2, r4
    add r11, r5, r6
    add r11, #128
    asr r11, #8
    cmp r11, #255
    movgt r11, #255
    cmp r11, #0
    movlt r11, #0
    asr r11, #3

    ldr r4, =mul_value_ug
    ldr r4, [r4]
    ldr r6, =mul_value_vg
    ldr r6, [r6]
    mul r7, r4, r1
    mul r8, r6, r2
    sub r5, r7
    sub r5, r8
    add r5, #128
    asr r5, #8
    cmp r11, #255
    movgt r11, #255
    cmp r11, #0
    movlt r11, #0
    asr r5, #2
    lsl r5, #5

    add r0, r11, r5
    add r0, r10

    pop { r3-r8, r10-r11 }
    bx lr
    //0~3 5 10~11

    */

set_yuv:
    push { r3-r10 }
    sub r0, #16//y - 16
    sub r1, #128//u - 128
    sub r2, #128//v - 128

    //blue
    ldr r3, =mul_value_y
    ldr r3, [r3]
    ldr r4, =mul_value_ub
    ldr r4, [r4]
    mul r5, r0, r3
    mul r6, r1, r4
    add r10, r5, r6
    add r10, #128
    asr r10, #8
    cmp r10, #255
    movgt r10, #255
    cmp r10, #0
    movlt r10, #0
    asr r10, #3
    ldr r9, =color_b
    str r10, [r9]

    //red
    ldr r4, =mul_value_vr
    ldr r4, [r4]
    mul r6, r2, r4
    add r10, r5, r6
    add r10, #128
    asr r10, #8
    cmp r10, #255
    movgt r10, #255
    cmp r10, #0
    movlt r10, #0
    asr r10, #3
    ldr r9, =color_r
    str r10, [r9]

    ldr r4, =mul_value_ug
    ldr r4, [r4]
    ldr r6, =mul_value_vg
    ldr r6, [r6]
    mul r7, r4, r1
    mul r8, r6, r2
    sub r5, r7
    sub r5, r8
    add r5, #128
    asr r5, #8
    cmp r5, #255
    movgt r5, #255
    cmp r5, #0
    movlt r5, #0
    asr r5, #2
    ldr r9, =color_g
    str r5, [r9]

    pop { r3-r10 }
    bx lr

read_b:
    ldr r0, =color_b
    ldr r0, [r0]
    bx lr

read_g:
    ldr r0, =color_g
    ldr r0, [r0]
    bx lr

read_r:
    ldr r0, =color_r
    ldr r0, [r0]
    bx lr
