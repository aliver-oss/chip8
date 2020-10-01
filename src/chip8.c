#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/random.h>

#include <chip8.h>
#include <opcodes.h>
#include <graphics.h>

#define rram(a) (ram[a])
#define wram(a, v) (ram[a] = v)

#define offset1(opcode) (opcode & 0xF000 >> 12)
#define offset2(opcode) ((opcode & 0x0F00) >> 8)
#define offset3(opcode) ((opcode & 0x00F0) >> 4)
#define offset4(opcode) ((opcode & 0x000F))

/* 
 *  Lower 8 bits of the rFLAGS register, including sign flag (SF),
 *  zero flag (ZF), auxiliary carryflag (AF), parity flag (PF),
 *  and carry flag (CF).
 *   
 *   -> sign flag (SF)
 *  |  -> zero flag (ZF)
 *  | |  -> not used
 *  | | |  -> Auxiliary Carry Flag(AF)
 *  | | | |          
 *  0 0 0 0 0 0 0 0
 *          | | | |
 *          | | | -> carry flag (CF)
 *          | | -> not used
 *          | -> Parity Flag (PF)
 *          -> not used
*/
extern uint8_t flags(void);

//******************************************************************************
// * ARRAYS OF POINTERS TO FUNCTIONS                                           *
//******************************************************************************

// Handle opcodes starting with 0x0
void (*zeroop[]) (uint16_t opcode) =
{
    cls, cpuNULL, cpuNULL, cpuNULL, cpuNULL, 
    cpuNULL, cpuNULL, cpuNULL, cpuNULL, cpuNULL, 
    cpuNULL, cpuNULL, cpuNULL, cpuNULL, ret, 
};

// Handle opcodes starting with 0x8
void (*eightop[]) (uint16_t opcode) = 
{
    setvxtovy, vxorvy, vxandvy, vxxorvy, 
    vxaddvy, vxsubvy, lsb_vx_in_vf_r, 
    vysubvx, msbvxvf_svvxl1
};

// Handle opcodes starting with 0xE
void (*e_op[]) (uint16_t opcode) = 
{
    cpuNULL, cpuNULL, cpuNULL, cpuNULL, 
    cpuNULL, cpuNULL, cpuNULL, cpuNULL, 
    cpuNULL, skipifdown, skipnotdown
};

// Handle opcodes starting with 0xF
void (*special[]) (uint16_t opcode) =
{
    cpuNULL, set_dt, cpuNULL, set_BCD, cpuNULL, 
    reg_dump, reg_load, vx_to_dt, set_st, load_char_addr, 
    vx_to_key, cpuNULL, cpuNULL, cpuNULL, iaddvx,

};

// Call other arrays of functions 
void (*generalop[16]) (uint16_t opcode) =
{
    msbis0, jump, call, se, sne, 
    sne, setvx, addvx, msbis8, next_if_vx_not_vy, 
    itoa, jvaddv0, vxandrand, cpuNULL, msbise, 
    msbisf
};

// fonts
uint8_t fonts[] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0,   // 0
    0x20, 0x60, 0x20, 0x20, 0x70,   // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0,   // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0,   // 3
    0x90, 0x90, 0xF0, 0x10, 0x10,   // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0,   // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0,   // 6
    0xF0, 0x10, 0x20, 0x40, 0x40,   // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0,   // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0,   // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90,   // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0,   // B
    0xF0, 0x80, 0x80, 0x80, 0xF0,   // C
    0xE0, 0x90, 0x90, 0x90, 0xE0,   // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0,   // E
    0xF0, 0x80, 0xF0, 0x80, 0x80    // F
};

//******************************************************************************
//*                      general processor functions                           *
//******************************************************************************


uint8_t randnum()
{
    ssize_t *buffer[1];
    getrandom(*buffer[1], 1, 0x0);

    // generates number
    srandom(buffer[0]);
    uint8_t number = random() % 255;
    return number;
}

void cpuNULL(uint16_t opcode)
{
    fprintf(stderr, "[WARNING] Unknown opcode %X", opcode);
}

int main(int argc, char *argv[])
{
    // initialize interpreter and load game into memory
    if (argc == 2)
    {
        FILE *filep = fopen(argv[argc-1], "rb");
        initialize ();
        load_game(filep);
    }
    else 
    {
        fprintf(stderr, "usage: ./chip8 <game>\n");
        exit(1);
    }

    game_loop();
}

void initialize()
{

    PC = START_ADDRS;
    SP = 0;
    I = 0;

    // clear
    memset(&gfx, 0, sizeof(gfx));
    memset(&stack, 0, sizeof(stack) / sizeof(stack[0]));
    memset(&reg, 0, sizeof(reg));
    memset(&ram, 0, sizeof(ram));
    
    // load fontset
    int address = FONTSET_ADDRESS;
    for (; address <= sizeof(fonts); address++) {
        wram(address, fonts[address]);
    }



}

void game_loop()
{
    // store the file_size in a variable so we just read loaded memory
    // sectors, we normalize the file_size to comply to the starting address
    // of the ram
    long saddr = file_size + 0x200;

    // store the opcode to be executed
    uint16_t opcode;

    for (; PC <= saddr; PC += 2) {
        // xor 2 subsequent memory locations to get a 2 bytes opcode
        opcode = (rram(PC) << 8) | (rram(PC+1));
        (*generalop[offset1(opcode)]) (opcode);
        printf("%X\n", opcode);

    }
}


void load_game(FILE *filep)
{

    // check for errors 
    int stream_status = ferror(filep);
    if (stream_status != 0) {
        fprintf(stderr, "[ERROR] Error reading file, exiting");
        exit(stream_status);
    }

    // finds size of file 
    int end_stream = fseek(filep, 1, SEEK_END);
    file_size = ftell(filep);

    // set position indicator to start of file
    rewind(filep);

    // load ram with file contents
    fread(&ram[START_ADDRS], 1, file_size, filep);

    fclose(filep);
    fprintf(stdout, "[OK] successfully loaded file into memory\n");
}


//******************************************************************************
//*                     OPCODES FUNCTIONS DEFINITIONS                          *
//******************************************************************************

// fuctions for the general function pointers array

void msbis0(uint16_t opcode)
{
    uint16_t index = offset4(opcode);
    (*zeroop[index]) (opcode);
}

void msbis8(uint16_t opcode)
{
    uint16_t index = offset4(opcode);
    (*eightop[index]) (opcode);
}

void msbise(uint16_t opcode) 
{
    uint16_t index = offset3(opcode);
    (*e_op[index]) (opcode);
}

void msbisf(uint16_t opcode)
{
    uint16_t index = offset4(opcode);
    if (index == 0x5) {
        index = offset3(opcode);
    }

    (*special[index]) (opcode);

}

// data registers functions

void setvx(uint16_t opcode) 
{
    // set register specified in msb - 4 bits to 
    // the lsb
    reg[offset2(opcode)] = opcode & 0x00ff;
}

void setvxtovy(uint16_t opcode)
{
    reg[offset2(opcode)] = reg[offset3(opcode)];
}

void addvx(uint16_t opcode)
{   
    reg[offset2(opcode)] += opcode & 0x00ff;
}

void vxaddvy(uint16_t opcode) 
{
    reg[offset3(opcode)] += reg[offset2(opcode)];
    uint8_t rflag = flags();

    reg[0xf] = 0x10 & flags();
}

void vxsubvy(uint16_t opcode)
{
    reg[offset2(opcode)] -= reg[offset3(opcode)];

    reg[0xf] = flags() & 0x10;
}

void vysubvx(uint16_t opcode)
{
    // register vx = vy - vx
    reg[offset2(opcode)] = reg[offset3(opcode)] - reg[offset2(opcode)];

    reg[0xf] = flags() & 0x10;
}

void vxorvy(uint16_t opcode)
{
    reg[offset2(opcode)] = reg[offset2(opcode)] | reg[offset3(opcode)];
}

void vxandvy(uint16_t opcode)
{
    reg[offset2(opcode)] = reg[offset2(opcode)] & reg[offset3(opcode)];
}

void vxxorvy(uint16_t opcode) 
{
    reg[offset2(opcode)] = reg[offset2(opcode)] ^ reg[offset3(opcode)];
}

void lsb_vx_in_vf_r(uint16_t opcode)
{
    uint8_t vx = offset2(opcode);
    uint8_t vy = offset3(opcode);

    reg[vx] = reg[vy] >> 1;
    reg[0xf] = reg[vx] & 0x1; 
}

// TODO: check if this opcode is implemented the right way
void msbvxvf_svvxl1(uint16_t opcode)
{
    uint8_t vx = offset2(opcode);
    uint8_t vy = offset3(opcode);

    reg[vx] = reg[vy] << 1;

    reg[0xf] = (reg[vx] & 0x80) >> 8;
}

void vxandrand(uint16_t opcode)
{
    uint8_t mask = offset3(opcode) | offset4(opcode);

    uint16_t rand_i = randnum();

    
}