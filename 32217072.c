#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEMORY_SIZE 10000000

int I_count, J_count, R_count = 0; // type count

// control unit
typedef struct {
    int RegDst;
    int RegWrite;
    int ALUSrc;
    int ALUop;
    int MemRead;
    int MemWrite;
    int MemtoReg;
    int PCSrc;
} control;

control CU = { 0 }; // ����ü 0 ���� �ʱ�ȭ

// pipelined registers
typedef struct {
    unsigned int pc;
    unsigned int instruction;
} IF_ID;

typedef struct {
    unsigned int pc;
    unsigned int instruction;
    int opcode;
    int rs;
    int rt;
    int rd;
    int shamt;
    int funct;
    int constant;
    int address;
    char type;
    control CU;
} ID_EX;

typedef struct {
    unsigned int pc;
    int ALUresult;
    int rs;
    int rt;
    int rd;
    int constant;
    int address;
    char type;
    control CU;
} EX_MEM;

typedef struct {
    unsigned int pc;
    int ALUresult;
    int memoryValue;
    int rd;
    int rt;
    char type;
    control CU;
} MEM_WB;

IF_ID ifid[2];
ID_EX idex[2];
EX_MEM exmem[2];
MEM_WB memwb[2];

int Reg[32]; // ��� �������͸� 0���� �ʱ�ȭ
int LO;
int HI;

unsigned int instMemory[MEMORY_SIZE]; // instruction memory
unsigned int dataMemory[MEMORY_SIZE]; // data memory

int pc = 0; // ���α׷� ī����
char* func; // func ǥ�ø� ���� ����

char type; // ���� ������ type ����
int ALUresult = 0; // ALUresult ��

// �������� �̸�����
const char* register_names[32] = {
    "$zero", "$at", "$v0", "$v1", "$a0", "$a1", "$a2", "$a3",
    "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7",
    "$s0", "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7",
    "$t8", "$t9", "$k0", "$k1", "$gp", "$sp", "$s8", "$ra"
};

// pc �� ��� ALU
int Adder(int pc, int input) {
    return pc + (input << 2); // 4����Ʈ ������ ������Ű��
}

// branch pc�� ALU
int branchAdder(int pc, int constant) {
    return pc + 4 + (constant << 2); // branch address ���
}

// jump pc�� ��� ALU
int jumpAdder(int pc, int address) {
    return ((pc + 4) & 0xf0000000 | (address << 2)); // jump address ���
}

int MUX(int input0, int input1, int select) {
    // select�� 0�̸� input0�� ����, 1�̸� input1�� ����
    return select ? input1 : input0;
}

void init_Reg(int* Reg) {
    for (int i = 0; i < 32; i++) {
        Reg[i] = 0; // ��� �������͸� 0���� �ʱ�ȭ
        if (i == 29) Reg[i] = 0x1000000; // ���� ������($sp)�� 0x1000000���� �ʱ�ȭ
        else if (i == 31) Reg[i] = 0xFFFFFFFF; // ��ȯ �ּ�($ra)�� 0xFFFFFFFF�� �ʱ�ȭ
    }
}

// �� ����� ��ȯ �Լ�
unsigned int convertToBigEndian(unsigned int value) {
    unsigned char* bytes = (unsigned char*)&value;
    return ((unsigned int)bytes[3]) | ((unsigned int)bytes[2] << 8) |
        ((unsigned int)bytes[1] << 16) | ((unsigned int)bytes[0] << 24);
}

// ��Ʈ �ʵ忡 ���� �����ϴ� �Լ�
void setField(int* field, int value) {
    *field = value;
}

// ���ɾ �������� �Լ�
void IF() {
    ifid[1].instruction = instMemory[pc / 4];
    ifid[1].pc = pc;
    printf("[IF: 0x%08X]\n", ifid[1].instruction);
    printf("[PC Update] 0x%08X -> 0x%08X\n", pc, pc + 4);
    pc = Adder(pc, 1); // ���� ���ɾ ������ �غ�
}

// ��ȣ Ȯ�� �Լ�
int SignExtend(short value) {
    return (int)value;
}

// control signal
void CU_signal(control* CU, int opcode, char type) {
    CU->RegDst = 0;
    CU->RegWrite = 0;
    CU->ALUSrc = 0;
    CU->ALUop = 0;
    CU->MemRead = 0;
    CU->MemWrite = 0;
    CU->MemtoReg = 0;
    CU->PCSrc = 0;

    if (type == 'R') {
        CU->RegDst = 1;
        CU->RegWrite = 1;
        CU->ALUSrc = 0;
        CU->ALUop = 2; // R�� ���ɾ� ALUOp ����
    }
    else if (type == 'I') {
        CU->RegWrite = 1;
        CU->ALUSrc = 1;
        switch (opcode) {
        case 0x23: // lw
        case 0x20: // lb
        case 0x24: // lbu
        case 0x21: // lh
        case 0x25: // lhu
            CU->MemRead = 1;
            CU->MemtoReg = 1;
            CU->ALUop = 0; // ADD
            break;
        case 0x2b: // sw
            CU->MemWrite = 1;
            CU->ALUop = 0; // ADD
            break;
        case 0x04: // beq
            CU->PCSrc = 1;
            CU->ALUop = 1; // SUB
            break;
        case 0x08: // addi
        case 0x09: // addiu
            CU->ALUop = 0; // ADD
            break;
        case 0x0c: // andi
            CU->ALUop = 3; // AND
            break;
        case 0x0d: // ori
            CU->ALUop = 4; // OR
            break;
        case 0x0a: // slti
        case 0x0b: // sltiu
            CU->ALUop = 5; // SLT
            break;
        }
    }
    else if (type == 'J') {
        // J�� ���ɾ�� ALU ������ ������� �����Ƿ� �������� ����
    }
}

// ���ɾ� ���ڵ� �Լ�
void ID() {
    idex[1].pc = ifid[1].pc;
    idex[1].instruction = ifid[1].instruction;

    // opcode: ���� 6��Ʈ ����
    idex[1].opcode = (idex[1].instruction >> 26) & 0x3F;

    if ((idex[1].instruction >> 26) == 0x00) { // R Ÿ�� ���ɾ�
        idex[1].rs = (idex[1].instruction >> 21) & 0x1F;
        idex[1].rt = (idex[1].instruction >> 16) & 0x1F;
        idex[1].rd = (idex[1].instruction >> 11) & 0x1F;
        idex[1].shamt = (idex[1].instruction >> 6) & 0x1F;
        idex[1].funct = idex[1].instruction & 0x3F;
        idex[1].type = 'R'; // ���� ���� type ����
        R_count++;
    }
    else if ((idex[1].instruction >> 26) == 0x02 || (idex[1].instruction >> 26) == 0x03) { // J Ÿ�� ���ɾ�
        idex[1].address = idex[1].instruction & 0x3FFFFFF;
        idex[1].type = 'J'; // ���� ���� type ����
        J_count++;
    }
    else { // I Ÿ�� ���ɾ�
        idex[1].rs = (idex[1].instruction >> 21) & 0x1F;
        idex[1].rt = (idex[1].instruction >> 16) & 0x1F;
        idex[1].constant = SignExtend(idex[1].instruction & 0xFFFF);
        idex[1].type = 'I'; // ���� ���� type ����
        I_count++;
    }

    CU_signal(&idex[1].CU, idex[1].opcode, idex[1].type); // control unit �� ����

    printf("[ID: 0x%08X]\n", idex[1].instruction);
    if (idex[1].type == 'R') {
        printf("Type: R, Inst: %s %s %s %s\n", func, register_names[idex[1].rd], register_names[idex[1].rs], register_names[idex[1].rt]);
        printf("opcode: 0x%X, rs: %d (value), rt: %d (value), rd: %d, shamt: %d, funct: 0x%X\n", idex[1].opcode, idex[1].rs, idex[1].rt, idex[1].rd, idex[1].shamt, idex[1].funct);
        printf("RegDst: %d, ALUOp: %d, ALUSrc: %d, Branch: %d, MemRead: %d, MemWrite: %d, RegWrite: %d, MemtoReg: %d\n", idex[1].CU.RegDst, idex[1].CU.ALUop, idex[1].CU.ALUSrc, idex[1].CU.PCSrc, idex[1].CU.MemRead, idex[1].CU.MemWrite, idex[1].CU.RegWrite, idex[1].CU.MemtoReg);
    }
    else if (idex[1].type == 'I') {
        printf("Type: I, Inst: %s %s, %d(%s)\n", func, register_names[idex[1].rt], idex[1].constant, register_names[idex[1].rs]);
        printf("opcode: 0x%X, rs: %d (value), rt: %d, imm: 0x%X\n", idex[1].opcode, idex[1].rs, idex[1].rt, idex[1].constant);
        printf("RegDst: %d, ALUOp: %d, ALUSrc: %d, Branch: %d, MemRead: %d, MemWrite: %d, RegWrite: %d, MemtoReg: %d\n", idex[1].CU.RegDst, idex[1].CU.ALUop, idex[1].CU.ALUSrc, idex[1].CU.PCSrc, idex[1].CU.MemRead, idex[1].CU.MemWrite, idex[1].CU.RegWrite, idex[1].CU.MemtoReg);
    }
    else if (idex[1].type == 'J') {
        printf("Type: J, Inst: %s 0x%X\n", func, idex[1].address);
        printf("opcode: 0x%X, addr: 0x%X\n", idex[1].opcode, idex[1].address);
        printf("RegDst: %d, ALUOp: %d, ALUSrc: %d, Branch: %d, MemRead: %d, MemWrite: %d, RegWrite: %d, MemtoReg: %d\n", idex[1].CU.RegDst, idex[1].CU.ALUop, idex[1].CU.ALUSrc, idex[1].CU.PCSrc, idex[1].CU.MemRead, idex[1].CU.MemWrite, idex[1].CU.RegWrite, idex[1].CU.MemtoReg);
    }

    // Branch decision
    if (idex[1].type == 'I' && idex[1].opcode == 0x04) { // BEQ
        if (Reg[idex[1].rs] == Reg[idex[1].rt]) {
            pc = branchAdder(idex[1].pc, idex[1].constant);
            // Flush the pipeline
            memset(&ifid[1], 0, sizeof(IF_ID));
            memset(&idex[1], 0, sizeof(ID_EX));
            return;
        }
    }
    else if (idex[1].type == 'I' && idex[1].opcode == 0x05) { // BNE
        if (Reg[idex[1].rs] != Reg[idex[1].rt]) {
            pc = branchAdder(idex[1].pc, idex[1].constant);
            // Flush the pipeline
            memset(&ifid[1], 0, sizeof(IF_ID));
            memset(&idex[1], 0, sizeof(ID_EX));
            return;
        }
    }
}

// ���ɾ� �з� �� ����
void EX() {
    exmem[1].pc = idex[1].pc;
    exmem[1].CU = idex[1].CU;
    exmem[1].rs = idex[1].rs;
    exmem[1].rt = idex[1].rt;
    exmem[1].rd = idex[1].rd;
    exmem[1].constant = idex[1].constant;
    exmem[1].address = idex[1].address;
    exmem[1].type = idex[1].type;

    // Forwarding unit for EX hazard detection
    int forwardA = 0;
    int forwardB = 0;

    if (exmem[0].CU.RegWrite && exmem[0].rd != 0 && exmem[0].rd == idex[1].rs) {
        forwardA = 1;
    }
    if (exmem[0].CU.RegWrite && exmem[0].rd != 0 && exmem[0].rd == idex[1].rt) {
        forwardB = 1;
    }

    if (memwb[0].CU.RegWrite && memwb[0].rd != 0 && memwb[0].rd == idex[1].rs) {
        forwardA = 2;
    }
    if (memwb[0].CU.RegWrite && memwb[0].rd != 0 && memwb[0].rd == idex[1].rt) {
        forwardB = 2;
    }

    int operand1 = (forwardA == 2) ? memwb[0].ALUresult : (forwardA == 1) ? exmem[0].ALUresult : Reg[idex[1].rs];
    int operand2 = (forwardB == 2) ? memwb[0].ALUresult : (forwardB == 1) ? exmem[0].ALUresult : Reg[idex[1].rt];

    int validALUresult = 0; // ALUresult ��ȿ�� Ȯ�� ����

    if (idex[1].type == 'R') {
        switch (idex[1].funct) {
        case 0x20: // ADD
            func = "add";
            ALUresult = operand1 + operand2;
            Reg[idex[1].rd] = ALUresult;
            validALUresult = 1;
            break;
        case 0x21: // ADDU
            func = "addu";
            ALUresult = (unsigned int)(operand1 + operand2);
            Reg[idex[1].rd] = ALUresult;
            validALUresult = 1;
            break;
        case 0x24: // AND
            func = "and";
            ALUresult = operand1 & operand2;
            Reg[idex[1].rd] = ALUresult;
            validALUresult = 1;
            break;
        case 0x08: // JR
            func = "jr";
            pc = operand1;
            if (pc == 0xFFFFFFFF) {
                return; // ���� ������ �����ϸ� ���� ��ȯ
            }
            return; // ���� ��ȯ�Ͽ� pc�� �ٽ� ������� �ʵ��� ��
        case 0x27: // NOR
            func = "nor";
            ALUresult = ~(operand1 | operand2);
            Reg[idex[1].rd] = ALUresult;
            validALUresult = 1;
            break;
        case 0x25: // OR
            func = "or";
            ALUresult = operand1 | operand2;
            Reg[idex[1].rd] = ALUresult;
            validALUresult = 1;
            break;
        case 0x2a: // SLT
            func = "slt";
            ALUresult = (operand1 < operand2) ? 1 : 0;
            Reg[idex[1].rd] = ALUresult;
            validALUresult = 1;
            break;
        case 0x2b: // SLTU
            func = "sltu";
            ALUresult = (unsigned int)(operand1 < operand2) ? 1 : 0;
            Reg[idex[1].rd] = ALUresult;
            validALUresult = 1;
            break;
        case 0x00: // SLL
            func = "sll";
            ALUresult = (operand2 << idex[1].shamt);
            Reg[idex[1].rd] = ALUresult;
            validALUresult = 1;
            break;
        case 0x02: // SRL
            func = "srl";
            ALUresult = (operand2 >> idex[1].shamt);
            Reg[idex[1].rd] = ALUresult;
            validALUresult = 1;
            break;
        case 0x22: // SUB
            func = "sub";
            ALUresult = operand1 - operand2;
            Reg[idex[1].rd] = ALUresult;
            validALUresult = 1;
            break;
        case 0x23: // SUBU
            func = "subu";
            ALUresult = (unsigned int)(operand1 - operand2);
            Reg[idex[1].rd] = ALUresult;
            validALUresult = 1;
            break;
        case 0x1a: // DIV
            func = "div";
            LO = (int)(operand1 / operand2);
            HI = (int)(operand1 % operand2);
            validALUresult = 0;
            break;
        case 0x1b: // DIVU
            func = "diviu";
            LO = (unsigned int)(operand1 / operand2);
            HI = (unsigned int)(operand1 % operand2);
            validALUresult = 0;
            break;
        case 0x19: // MUL
            func = "mul";
            ALUresult = operand1 * operand2;
            Reg[idex[1].rd] = ALUresult;
            validALUresult = 1;
            break;
        }
    }
    else if (idex[1].type == 'I') {
        switch (idex[1].opcode) {
        case 0x08: // ADDI
            func = "addi";
            ALUresult = operand1 + idex[1].constant;
            Reg[idex[1].rt] = ALUresult;
            validALUresult = 1;
            break;
        case 0x09: // ADDI unsigned
            func = "addiu";
            ALUresult = (unsigned int)(operand1 + idex[1].constant);
            Reg[idex[1].rt] = ALUresult;
            validALUresult = 1;
            break;
        case 0x0c: // ANDI
            func = "andi";
            ALUresult = operand1 & (idex[1].constant & 0xFFFF); // 16��Ʈ 0���� ä��
            Reg[idex[1].rt] = ALUresult;
            validALUresult = 1;
            break;
        case 0x04: // beq
            func = "beq";
            if (operand1 == operand2) {
                pc = branchAdder(idex[1].pc, idex[1].constant);
            }
            else {
                pc = Adder(idex[1].pc, 1);
            }
            validALUresult = 0;
            break;
        case 0x05: // bne
            func = "bne";
            if (operand1 != operand2) {
                pc = branchAdder(idex[1].pc, idex[1].constant);
            }
            else {
                pc = Adder(idex[1].pc, 1);
            }
            validALUresult = 0;
            break;
        case 0x24: // lbu
            func = "lbu";
            ALUresult = operand1 + idex[1].constant;
            Reg[idex[1].rt] = instMemory[ALUresult / 4] & 0xFF;
            validALUresult = 1;
            break;
        case 0x25: // lhu
            func = "lhu";
            ALUresult = operand1 + idex[1].constant;
            Reg[idex[1].rt] = instMemory[ALUresult / 4] & 0xFFFF;
            validALUresult = 1;
            break;
        case 0x30: // ll
            func = "ll";
            ALUresult = operand1 + idex[1].constant;
            Reg[idex[1].rt] = instMemory[ALUresult / 4];
            validALUresult = 1;
            break;
        case 0x0f: // lui
            func = "lui";
            Reg[idex[1].rt] = idex[1].constant << 16;
            validALUresult = 1;
            break;
        case 0x23: // lw
            func = "lw";
            ALUresult = operand1 + idex[1].constant;
            Reg[idex[1].rt] = instMemory[ALUresult / 4];
            validALUresult = 1;
            break;
        case 0x0d: // ori
            func = "ori";
            ALUresult = operand1 | (idex[1].constant & 0xFFFF);
            Reg[idex[1].rt] = ALUresult;
            validALUresult = 1;
            break;
        case 0x0a: // slti
            func = "slti";
            ALUresult = (int)((operand1 < idex[1].constant) ? 1 : 0);
            Reg[idex[1].rt] = ALUresult;
            validALUresult = 1;
            break;
        case 0x0b: // sltiu
            func = "sltiu";
            ALUresult = (unsigned int)((operand1 < idex[1].constant) ? 1 : 0);
            Reg[idex[1].rt] = ALUresult;
            validALUresult = 1;
            break;
        case 0x28: // sb
            func = "sb";
            ALUresult = operand1 + idex[1].constant;
            instMemory[ALUresult / 4] = Reg[idex[1].rt] & 0xFF;
            validALUresult = 1;
            break;
        case 0x38: { // sc
            func = "sc";
            int addr = operand1 + idex[1].constant;
            if (1) { // Replace with actual condition check
                instMemory[addr / 4] = Reg[idex[1].rt];
                Reg[idex[1].rt] = 1;  // Store successful
            }
            else {
                Reg[idex[1].rt] = 0;  // Store failed
            }
            validALUresult = 1;
            break;
        }
        case 0x29: // sh
            func = "sh";
            ALUresult = operand1 + idex[1].constant;
            instMemory[ALUresult / 4] = Reg[idex[1].rt] & 0xFFFF;
            validALUresult = 1;
            break;
        case 0x2b: // sw
            func = "sw";
            ALUresult = operand1 + idex[1].constant;
            instMemory[ALUresult / 4] = Reg[idex[1].rt];
            validALUresult = 1;
            break;
        }
    }
    else if (idex[1].type == 'J') {
        switch (idex[1].opcode) {
        case 0x02: // j
            func = "j";
            pc = (pc & 0xF0000000) | (idex[1].address << 2);
            if (pc == 0xFFFFFFFF) {
                return; // ���� ������ �����ϸ� ���� ��ȯ
            }
            return; // ���� ��ȯ�Ͽ� pc�� �ٽ� ������� �ʵ��� ��
        case 0x03: // jal
            func = "jal";
            Reg[31] = pc + 8; // $ra �������� ����
            pc = (pc & 0xF0000000) | (idex[1].address << 2);
            if (pc == 0xFFFFFFFF) {
                return; // ���� ������ �����ϸ� ���� ��ȯ
            }
            return; // ���� ��ȯ�Ͽ� pc�� �ٽ� ������� �ʵ��� ��
        }
    }

    exmem[1].ALUresult = ALUresult;
    printf("[EX: 0x%08X]\n", idex[1].instruction);
    printf("data1: %d, data2: %d (forwarding), ALU result: %d\n", operand1, operand2, ALUresult);
    printf("RegDst: %d, ALUOp: %d, ALUSrc: %d, Branch: %d, MemRead: %d, MemWrite: %d, RegWrite: %d, MemtoReg: %d\n", idex[1].CU.RegDst, idex[1].CU.ALUop, idex[1].CU.ALUSrc, idex[1].CU.PCSrc, idex[1].CU.MemRead, idex[1].CU.MemWrite, idex[1].CU.RegWrite, idex[1].CU.MemtoReg);
}

// memory access
void MEM() {
    memwb[1].pc = exmem[1].pc;
    memwb[1].ALUresult = exmem[1].ALUresult;
    memwb[1].CU = exmem[1].CU;
    memwb[1].rd = exmem[1].rd;
    memwb[1].rt = exmem[1].rt;
    memwb[1].type = exmem[1].type;

    printf("[MEM: 0x%08X]\n", exmem[1].pc);
    unsigned int address;
    unsigned int value;

    if (exmem[1].CU.MemRead == 1) {
        address = exmem[1].ALUresult;
        memwb[1].memoryValue = instMemory[address / 4];
        printf("Load, Address: 0x%08X, Value: %d\n", address, memwb[1].memoryValue);
    }
    else {
        printf("Pass\n");
    }

    if (exmem[1].CU.MemWrite == 1) {
        address = exmem[1].ALUresult;
        value = Reg[exmem[1].rt];
        instMemory[address / 4] = value;
        printf("Store, Address: 0x%08X, Value: %d\n", address, value);
    }
    else {
        printf("Pass\n");
    }

    printf("Branch: %d, MemRead: %d, MemWrite: %d, RegWrite: %d, MemtoReg: %d\n", exmem[1].CU.PCSrc, exmem[1].CU.MemRead, exmem[1].CU.MemWrite, exmem[1].CU.RegWrite, exmem[1].CU.MemtoReg);
}

// writeback
void WB() {
    printf("[WB: 0x%08X]\n", memwb[1].pc);
    switch (memwb[1].type) {
    case 'R':
        if (memwb[1].CU.RegWrite == 1) {
            Reg[memwb[1].rd] = memwb[1].ALUresult;
            printf("Write Register: %s, Value: %d\n", register_names[memwb[1].rd], Reg[memwb[1].rd]);
        }
        break;
    case 'I':
        if (memwb[1].CU.RegWrite == 1) {
            if (memwb[1].CU.MemtoReg == 1) {
                Reg[memwb[1].rt] = memwb[1].memoryValue;
            }
            else {
                Reg[memwb[1].rt] = memwb[1].ALUresult;
            }
            printf("Write Register: %s, Value: %d\n", register_names[memwb[1].rt], Reg[memwb[1].rt]);
        }
        break;
    case 'J':
        printf("Write Register: $ra, Value: %d\n", pc);
        break;
    default:
        break;
    }
    printf("RegWrite: %d, MemtoReg: %d\n", memwb[1].CU.RegWrite, memwb[1].CU.MemtoReg);
}

// ���� �Լ�
int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <binary_file>\n", argv[0]);
        return 1;
    }

    const char* filename = argv[1];

    init_Reg(Reg); // �������� �ʱ�ȭ
    int cycle = 0;

    FILE* file = fopen(filename, "rb");
    if (file == NULL) {
        perror("���� ���� ����");
        return 1;
    }

    size_t bytesRead;
    int i = 0;

    // 4����Ʈ ������ �б�
    while ((bytesRead = fread(&instMemory[i], sizeof(unsigned int), 1, file)) == 1 && i < MEMORY_SIZE) {
        instMemory[i] = convertToBigEndian(instMemory[i]); // �� ����� ��ȯ
        i++;
    }

    fclose(file);

    // �޸𸮿��� ���ɾ �������� ���ڵ� �� ���
    while (1) {
        if (pc == 0xFFFFFFFF) {
            printf("32217072> Final Result\n");
            printf("Cycles: %d, R-type instructions: %d, I-type instructions: %d, J-type instructions: %d\n", cycle, R_count, I_count, J_count);
            printf("Return value (v0): %d\n", Reg[2]);
            break;  // pc�� 0xFFFFFFFF�̸� ������ ����
        }

        cycle++;
        printf("32217072> cycle: %d\n", cycle);

        // ���������� �ܰ� ȣ��
        WB();
        MEM();
        EX();
        ID();
        IF();

        // ���������� �������� �̵�
        ifid[0] = ifid[1];
        idex[0] = idex[1];
        exmem[0] = exmem[1];
        memwb[0] = memwb[1];
    }

    return 0;
}
