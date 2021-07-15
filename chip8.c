#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SUCCESS 0
#define FAILURE 1
#define MAX_RAM 4096
#define DISPLAY_WIDTH 64
#define DISPLAY_HEIGHT 32
#define MAX_REG 15

typedef struct Chip8 {
    int16_t PC;
    int16_t I; // index register
    int8_t VR[16]; // general-purpose variable registers
    // TODO: stack
    int8_t DT; // delay timer
    int8_t ST; // sound timer    
} Chip8;

Chip8 CHIP8 = { 0x200, 0, { '\0' }, 0, 0 };

u_int8_t RAM[MAX_RAM] = { '\0' };
int8_t DISPLAY[DISPLAY_WIDTH * DISPLAY_HEIGHT] = { '\0' }; // 64 pixels wide 32 pixels tall

u_int8_t FONT[] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

void draw() {
	for (u_int8_t y = 0; y < DISPLAY_HEIGHT; y++) {
		for (u_int8_t x = 0; x < DISPLAY_WIDTH; x++) {
			if (DISPLAY[x + y * DISPLAY_WIDTH]) {
				printf("%d", DISPLAY[x + y * DISPLAY_WIDTH]);
			} else {
				printf(" ");
			}
		}
		printf("\n");
	}
}

void draw_display(u_int16_t X, u_int16_t Y, u_int16_t N) {
    u_int16_t VX = CHIP8.VR[X];
    u_int16_t VY = CHIP8.VR[Y];

	u_int32_t offset;
    u_int8_t sprite = 0;

    CHIP8.VR[0xF] &= 0; // set VF to 0

    for (u_int16_t y = 0; y < N; y++) {

        sprite = RAM[CHIP8.I + y];

		for (u_int8_t x = 0; x < 8; x++) {

			offset = x + VX + ((y + VY) * DISPLAY_WIDTH);

			if (sprite & (0x80 >> x)) {
				if (DISPLAY[offset]) {
					DISPLAY[offset] = 0;
					CHIP8.VR[0xF] = 1;
				} else {
					DISPLAY[offset] ^= 1;
				}
			}
		}
	} // end for
}

void clear_display() {
    int x;
    int y;
	memset(&DISPLAY, '\0', sizeof(DISPLAY));
}

int load_rom(char * filename) {
    FILE * fp;
    int ret_val = SUCCESS;

    fp = fopen(filename, "rb");
    if (!fp) {
        ret_val = FAILURE;        
        goto Exit;
    }

    // get rom file size
    fseek(fp, 0L, SEEK_END);
    unsigned int sz = ftell(fp);
    rewind(fp);

	if (sz < 0 || sz >= MAX_RAM - 0x200) {
		fprintf(stderr, "bad rom size %d\n", sz);
		exit(FAILURE);
	}
    // copy byte-code into RAM starting at 0x200
    fread(&RAM[0x200], sz, 1, fp);

Exit:
    if (fp) {
        fclose(fp);
    }
    return ret_val; 
}

u_int16_t fetch() {
    u_int16_t op_hi = RAM[CHIP8.PC] << 8;
    u_int16_t op_lo = RAM[CHIP8.PC + 1];
    u_int16_t opcode = op_hi | op_lo;

    fprintf(stderr, "pc = 0x%x opcode = %04X\n", CHIP8.PC, opcode);
    CHIP8.PC += 2;

    if (CHIP8.PC >= MAX_RAM) {
        fprintf(stderr, "pc 0x%x is out-of-bounds\n", CHIP8.PC);
        exit(FAILURE);
    }
    return opcode;
}

void verify_register_index(u_int16_t index) {
	if (index < 0 || index > MAX_REG) {
		fprintf(stderr, "register index %d is out of bounds!\n", index);
		exit(FAILURE);
	}
}

u_int16_t decode(u_int16_t opcode, const char * value) {
    u_int16_t decoded = 0xFFFF;
    if (strcmp(value, "op") == 0) {
        decoded = (opcode & 0xF000) >> 12;
    } else if (strcmp(value, "X") == 0) {
        decoded = (opcode & 0x0F00) >> 8; // index to lookup register V0-VF
		verify_register_index(decoded);
    } else if (strcmp(value, "Y") == 0) {
        decoded = (opcode & 0x00F0) >> 4; // index to lookup register V0-VF
		verify_register_index(decoded);
    } else if (strcmp(value, "N") == 0) {
        decoded = (opcode & 0x000F);      // 4-bit number
    } else if (strcmp(value, "NN") == 0) {
        decoded = (opcode & 0x00FF);      // 8-bit immediate number
    } else if (strcmp(value, "NNN") == 0) {
        decoded = (opcode & 0x0FFF);      // 12-bit immediate memory address
    } else {
        fprintf(stderr, "Unknown value %s for decode option\n", value);
        exit(FAILURE);
    }

    return decoded;
}

void emulate() {
    while (1) {
        u_int16_t opcode = fetch();
        u_int16_t op  = decode(opcode, "op");
        u_int16_t X   = decode(opcode, "X");   // index to lookup reg V0-VF
        u_int16_t Y   = decode(opcode, "Y");   // index to lookup reg V0-VF
        u_int16_t N   = decode(opcode, "N");   // 4-bit num
        u_int16_t NN  = decode(opcode, "NN");  // 8-bit immediate num
        u_int16_t NNN = decode(opcode, "NNN"); // 12-bit immediate mem addr
        switch (op) {
            case 0x0:
                if (X == 0) {
                    if (NN == 0xE0) { // Clear the screen
                        clear_display();
                    } else if (NN == 0xEE) {
                        // Return from a subroutine
                    }
                } else {
                    // Execute machine language subroutine at address
                }
                break;
            case 0x1: // Jump to address NNN
                CHIP8.PC = NNN;
                break;
            case 0x2:
                break;
            case 0x3:
                break;
            case 0x4:
                break;
            case 0x5:
                break;
            case 0x6: // Store number NN in register VX
                CHIP8.VR[X] = NN;
                break;
            case 0x7: // Add the value NN to register VX 
                CHIP8.VR[X] += NN;
                break;
            case 0x8:
                break;
            case 0x9:
                break;
            case 0xA: // Store memory address NNN in register I 
                CHIP8.I = NNN;
                break;
            case 0xB:
                break;
            case 0xC:
                break;
            case 0xD:
                /*
                Draw a sprite at position VX, VY with N bytes of sprite data 
                starting at the address stored in I.
                Set VF to 01 if any set pixels are changed to unset, and 00 otherwise.
                */
                draw_display(X, Y, N);
				draw();
                break;
            case 0xE:
                break;
            case 0xF:
                break;
            default:
                fprintf(stderr, "unknown op %X\n", op);
                break;
        }
    }
}

int main(int argc, char * argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s rom_file\n", argv[0]);
        exit(FAILURE);
    }

    // copy FONT data to 0x50-0x9f
    memcpy(&RAM[0x50], FONT, sizeof(FONT));

    int ret_val = SUCCESS;
    char * rom_filename = argv[1];
    ret_val = load_rom(rom_filename);
    if (ret_val) {
        fprintf(stderr, "Failed to load rom %s\n", rom_filename);
        exit(FAILURE);
    }

    fprintf(stderr, "Loaded rom %s successfully\n", rom_filename);
    emulate();

    return 0;
}
