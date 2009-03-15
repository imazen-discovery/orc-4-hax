
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <sys/types.h>

#include <orc/orcprogram.h>
#include <orc/arm.h>
#include <orc/orcutils.h>


const char *
arm_reg_name (int reg)
{
#if 1
  static const char *gp_regs[] = {
    "a1", "a2", "a3", "a4",
    "v1", "v2", "v3", "v4",
    "v5", "v6", "v7", "v8",
    "ip", "sp", "lr", "pc" };
#else
  static const char *gp_regs[] = {
    "r0", "r1", "r2", "r3",
    "r4", "r5", "r6", "r7",
    "r8", "r9", "r10", "r11",
    "r12", "r13", "r14", "r15" };
#endif

  if (reg < ORC_GP_REG_BASE || reg >= ORC_GP_REG_BASE+16) {
    return "ERROR";
  }

  return gp_regs[reg&0xf];
}

void
arm_emit (OrcCompiler *compiler, uint32_t insn)
{
  ORC_WRITE_UINT32_LE (compiler->codeptr, insn);
  compiler->codeptr+=4;
}

void
arm_emit_bx_lr (OrcCompiler *compiler)
{
  ORC_ASM_CODE(compiler,"  bx lr\n");
  arm_emit (compiler, 0xe12fff1e);
}

void
arm_emit_push (OrcCompiler *compiler, int regs)
{
  int i;
  int x = 0;

  ORC_ASM_CODE(compiler,"  push {");
  for(i=0;i<16;i++){
    if (regs & (1<<i)) {
      x |= (1<<i);
      ORC_ASM_CODE(compiler,"r%d", i);
      if (x != regs) {
        ORC_ASM_CODE(compiler,", ");
      }
    }
  }
  ORC_ASM_CODE(compiler,"}\n");

  arm_emit (compiler, 0xe92d0000 | regs);
}

void
arm_emit_pop (OrcCompiler *compiler, int regs)
{
  int i;
  int x = 0;

  ORC_ASM_CODE(compiler,"  pop {");
  for(i=0;i<16;i++){
    if (regs & (1<<i)) {
      x |= (1<<i);
      ORC_ASM_CODE(compiler,"r%d", i);
      if (x != regs) {
        ORC_ASM_CODE(compiler,", ");
      }
    }
  }
  ORC_ASM_CODE(compiler,"}\n");

  arm_emit (compiler, 0xe8bd0000 | regs);
}

void
arm_emit_mov (OrcCompiler *compiler, int dest, int src)
{
  uint32_t code;

  code = 0xe1a00000;
  code |= (src&0xf) << 0;
  code |= (dest&0xf) << 12;

  ORC_ASM_CODE(compiler,"  mov %s, %s\n", arm_reg_name (dest), arm_reg_name (src));

  arm_emit (compiler, code);
}

void
arm_emit_label (OrcCompiler *compiler, int label)
{
  ORC_ASM_CODE(compiler,".L%d:\n", label);

  compiler->labels[label] = compiler->codeptr;
}

void
arm_add_fixup (OrcCompiler *compiler, int label, int type)
{
  compiler->fixups[compiler->n_fixups].ptr = compiler->codeptr;
  compiler->fixups[compiler->n_fixups].label = label;
  compiler->fixups[compiler->n_fixups].type = type;
  compiler->n_fixups++;
}

void
arm_do_fixups (OrcCompiler *compiler)
{
  int i;
  for(i=0;i<compiler->n_fixups;i++){
    unsigned char *label = compiler->labels[compiler->fixups[i].label];
    unsigned char *ptr = compiler->fixups[i].ptr;
    uint32_t code;
    int diff;

    code = ORC_READ_UINT32_LE (ptr);
    diff = ORC_READ_UINT32_LE (ptr) + ((label - ptr) >> 2);
    ORC_WRITE_UINT32_LE(ptr, (code&0xff000000) | (diff&0x00ffffff));
  }

}

void
arm_emit_branch (OrcCompiler *compiler, int cond, int label)
{
  static const char *cond_names[] = {
    "eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc",
    "hi", "ls", "ge", "lt", "gt", "le", "", "" };
  uint32_t code;

  code = 0x0afffffe;
  code |= (cond&0xf) << 28;
  arm_add_fixup (compiler, label, 0);
  arm_emit (compiler, code);

  ORC_ASM_CODE(compiler,"  b%s .L%d\n", cond_names[cond], label);
}

void
arm_emit_loadimm (OrcCompiler *compiler, int dest, int imm)
{
  uint32_t code;
  int shift2;

  shift2 = 0;
  while (imm && ((imm&3)==0)) {
    imm >>= 2;
    shift2++;
  }

  code = 0xe3a00000;
  code |= (dest&0xf) << 12;
  code |= (((16-shift2)&0xf) << 8);
  code |= (imm&0xff);

  ORC_ASM_CODE(compiler,"  mov %s, #0x%08x\n", arm_reg_name (dest), imm << (shift2*2));
  arm_emit (compiler, code);
}

void
arm_emit_add (OrcCompiler *compiler, int dest, int src1, int src2)
{
  uint32_t code;

  code = 0xe0800000;
  code |= (src1&0xf) << 16;
  code |= (dest&0xf) << 12;
  code |= (src2&0xf) << 0;

  ORC_ASM_CODE(compiler,"  add %s, %s, %s\n",
      arm_reg_name (dest),
      arm_reg_name (src1),
      arm_reg_name (src2));
  arm_emit (compiler, code);
}

void
arm_emit_sub (OrcCompiler *compiler, int dest, int src1, int src2)
{
  uint32_t code;

  code = 0xe0400000;
  code |= (src1&0xf) << 16;
  code |= (dest&0xf) << 12;
  code |= (src2&0xf) << 0;

  ORC_ASM_CODE(compiler,"  sub %s, %s, %s\n",
      arm_reg_name (dest),
      arm_reg_name (src1),
      arm_reg_name (src2));
  arm_emit (compiler, code);
}

void
arm_emit_sub_imm (OrcCompiler *compiler, int dest, int src1, int value)
{
  uint32_t code;

  code = 0xe2500000;
  code |= (src1&0xf) << 16;
  code |= (dest&0xf) << 12;
  code |= (value) << 0;

  ORC_ASM_CODE(compiler,"  subs %s, %s, #%d\n",
      arm_reg_name (dest),
      arm_reg_name (src1),
      value);
  arm_emit (compiler, code);
}

void
arm_emit_cmp_imm (OrcCompiler *compiler, int src1, int value)
{
  uint32_t code;

  code = 0xe3500000;
  code |= (src1&0xf) << 16;
  code |= (value) << 0;

  ORC_ASM_CODE(compiler,"  cmp %s, #%d\n",
      arm_reg_name (src1),
      value);
  arm_emit (compiler, code);
}

void
arm_emit_load_reg (OrcCompiler *compiler, int dest, int src1, int offset)
{
  uint32_t code;

  code = 0xe5900000;
  code |= (src1&0xf) << 16;
  code |= (dest&0xf) << 12;
  code |= offset&0xfff;

  ORC_ASM_CODE(compiler,"  ldr %s, [%s, #%d]\n",
      arm_reg_name (dest),
      arm_reg_name (src1), offset);
  arm_emit (compiler, code);
}



void
arm_emit_dp_reg (OrcCompiler *compiler, int cond, int opcode, int dest,
    int src1, int src2)
{
  static const char *dp_insn_names[] = {
    "and", "eor", "sub", "rsb", "add", "adc", "sbc", "rsc",
    "tst", "teq", "cmp", "cmn", "orr", "mov", "bic", "mvn"
  };
  static const int shift_expn[] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 0, 1, 0, 1
  };
  uint32_t code;
  int update = 0;
  
  code = cond << 28;
  code |= opcode << 21;
  code |= update << 20; /* update condition codes */
  if (opcode >= 8 && opcode < 12) {
    code |= 1 << 20;
  }
  code |= (src1&0xf) << 16;
  code |= (dest&0xf) << 12;
  code |= (src2&0xf) << 0;

  if (shift_expn[opcode]) {
    ORC_ASM_CODE(compiler,"  %s%s %s, %s\n",
        dp_insn_names[opcode],
        update ? "s" : "",
        arm_reg_name (src1),
        arm_reg_name (src2));
  } else {
    ORC_ASM_CODE(compiler,"  %s%s %s, %s, %s\n",
        dp_insn_names[opcode],
        update ? "s" : "",
        arm_reg_name (dest),
        arm_reg_name (src1),
        arm_reg_name (src2));
  }
  arm_emit (compiler, code);
}

