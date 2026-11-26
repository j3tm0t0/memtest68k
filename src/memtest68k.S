/* MC68000-only kernels. Dispatch, progress and error callbacks are outside
 * the hot loop. Never use MOVEM for test RAM: accesses stay word ordered.
 * mode = (kind * 3 + action) * 2 + reverse; byte=24, word=25. */
    .text
    .even
    .global mt68_sweep
    .type mt68_sweep,@function
mt68_sweep:
    movem.l %d2-%d7/%a2,-(%sp)
    movea.l 32(%sp),%a1
    movea.l (%a1),%a0
    move.l 4(%a1),%d7
    beq.w .Ldone
    subq.l #1,%d7
    move.l 8(%a1),%d3
    move.l 12(%a1),%d4
    move.l 36(%sp),%d0
    add.w %d0,%d0
    lea .Ltable(%pc),%a2
    move.w (%a2,%d0.w),%d0
    jmp (%a2,%d0.w)
.Ltable:
    .word .Lcfu-.Ltable,.Lcfd-.Ltable,.Lccu-.Ltable,.Lccd-.Ltable,.Lciu-.Ltable,.Lcid-.Ltable
    .word .Lafu-.Ltable,.Lafd-.Ltable,.Lacu-.Ltable,.Lacd-.Ltable,.Laiu-.Ltable,.Laid-.Ltable
    .word .Lrfu-.Ltable,.Lrfd-.Ltable,.Lrcu-.Ltable,.Lrcd-.Ltable,.Lriu-.Ltable,.Lrid-.Ltable
    .word .Lwfu-.Ltable,.Lwfd-.Ltable,.Lwcu-.Ltable,.Lwcd-.Ltable,.Lwiu-.Ltable,.Lwid-.Ltable
    .word .Lbyte-.Ltable,.Lword-.Ltable

    .macro constant name,action,reverse
\name:
    move.l %d3,%d0
    move.l %d0,%d2
    not.l %d2
    .if \reverse
    addq.l #4,%a0
    .endif
.Lloop\@:
    .if \action == 0
      .if \reverse
        move.l %d0,-(%a0)
      .else
        move.l %d0,(%a0)+
      .endif
    .else
      .if \reverse
        move.l -(%a0),%d1
      .elseif \action == 1
        move.l (%a0)+,%d1
      .else
        move.l (%a0),%d1
      .endif
      cmp.l %d0,%d1
      bne.s .Lfault\@
      .if \action == 2
        .if \reverse
          move.l %d2,(%a0)
        .else
          move.l %d2,(%a0)+
        .endif
      .endif
    .endif
    dbra %d7,.Lloop\@
    bra.w .Ldone
.Lfault\@:
    .if (\reverse == 0) && (\action == 1)
      subq.l #4,%a0
    .endif
    bra.w .Lbad
    .endm

    constant .Lcfu,0,0
    constant .Lcfd,0,1
    constant .Lccu,1,0
    constant .Lccd,1,1
    constant .Lciu,2,0
    constant .Lcid,2,1

    .macro variable name,kind,action,reverse
\name:
    .if \reverse
      addq.l #4,%a0
    .endif
    .if \kind == 2
      lea mt68_random_table,%a2
    .endif
    .if \kind == 3
      move.l %d3,%d0  /* C supplies the first rotating bit/complement. */
    .endif
.Lloop\@:
    .if \reverse
      subq.l #4,%a0
    .endif
    .if \kind == 2
      move.w %a0,%d5
      and.w #0x3fc,%d5
      move.l (%a2,%d5.w),%d0
      eor.l %d4,%d0
    .elseif \kind == 1
      move.l %a0,%d0
      eor.l %d3,%d0
    .endif
    .if \action != 0
      move.l (%a0),%d1
      cmp.l %d0,%d1
      bne.s .Lfault\@
    .endif
    .if \action == 2
      move.l %d0,%d2
      not.l %d2
      .if \reverse
        move.l %d2,(%a0)
      .else
        move.l %d2,(%a0)+
      .endif
    .elseif \action == 0
      .if \reverse
        move.l %d0,(%a0)
      .else
        move.l %d0,(%a0)+
      .endif
    .elseif \reverse == 0
      addq.l #4,%a0
    .endif
    .if \kind == 3
      .if \reverse
        ror.l #1,%d0
      .else
        rol.l #1,%d0
      .endif
    .endif
    dbra %d7,.Lloop\@
    bra.w .Ldone
.Lfault\@:
    bra.w .Lbad
    .endm

    variable .Lafu,1,0,0
    variable .Lafd,1,0,1
    variable .Lacu,1,1,0
    variable .Lacd,1,1,1
    variable .Laiu,1,2,0
    variable .Laid,1,2,1
    variable .Lrfu,2,0,0
    variable .Lrfd,2,0,1
    variable .Lrcu,2,1,0
    variable .Lrcd,2,1,1
    variable .Lriu,2,2,0
    variable .Lrid,2,2,1
    variable .Lwfu,3,0,0
    variable .Lwfd,3,0,1
    variable .Lwcu,3,1,0
    variable .Lwcd,3,1,1
    variable .Lwiu,3,2,0
    variable .Lwid,3,2,1
.Lbyte:
    move.l %d3,%d0
    rol.l #8,%d0
    move.l %d3,%d1
    swap %d1
    move.l %d3,%d2
    ror.l #8,%d2
.Lbyte_loop:
    move.b %d0,(%a0)+
    move.b %d1,(%a0)+
    move.b %d2,(%a0)+
    move.b %d3,(%a0)+
    dbra %d7,.Lbyte_loop
    bra.s .Ldone
.Lword:
    move.l %d3,%d0
    swap %d0
.Lword_loop:
    move.w %d0,(%a0)+
    move.w %d3,(%a0)+
    dbra %d7,.Lword_loop
    bra.s .Ldone
.Lbad:
    move.l %a0,(%a1)
    move.l %d0,16(%a1)
    move.l %d1,20(%a1)
    moveq #0,%d0
    move.w %d7,%d0
    addq.l #1,%d0
    bra.s .Lreturn
.Ldone:
    moveq #0,%d0
.Lreturn:
    movem.l (%sp)+,%d2-%d7/%a2
    rts
    .size mt68_sweep,.-mt68_sweep
