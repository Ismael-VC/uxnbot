#include <stdio.h>
#include <time.h>

typedef unsigned char Uint8;
typedef signed char Sint8;
typedef unsigned short Uint16;

typedef struct {
	Uint8 dat[0x100], ptr;
} Stack;

typedef struct Uxn {
	Uint8 ram[0x10000], dev[0x100];
	Stack wst, rst;
} Uxn;

int debug = 1, uxn_eval(Uxn *u, Uint16 pc);

void
system_print(Stack *s)
{
	Uint8 i;
	for(i = s->ptr - 7; i != (Uint8)(s->ptr + 1); i++)
		fprintf(stderr, "%02x%c", s->dat[i], i == 0 ? '|' : ' ');
	fprintf(stderr, "< \n");
}

void
system_inspect(Uxn *u)
{
	fprintf(stderr, "WST "), system_print(&u->wst);
	fprintf(stderr, "RST "), system_print(&u->rst);
}

Uint8
datetime_dei(Uxn *u, Uint8 addr)
{
	time_t seconds = time(NULL);
	struct tm zt = {0};
	struct tm *t = localtime(&seconds);
	if(t == NULL)
		t = &zt;
	switch(addr) {
	case 0xc0: return (t->tm_year + 1900) >> 8;
	case 0xc1: return (t->tm_year + 1900);
	case 0xc2: return t->tm_mon;
	case 0xc3: return t->tm_mday;
	case 0xc4: return t->tm_hour;
	case 0xc5: return t->tm_min;
	case 0xc6: return t->tm_sec;
	case 0xc7: return t->tm_wday;
	case 0xc8: return t->tm_yday >> 8;
	case 0xc9: return t->tm_yday;
	case 0xca: return t->tm_isdst;
	default: return u->dev[addr];
	}
}

int
emu_error(char *msg){
	fprintf(stdout, msg);
	return 0;
}

Uint8
emu_dei(Uxn *u, Uint8 addr)
{
	switch(addr) {
	case 0xc0: return datetime_dei(u, addr);
	}
	return u->dev[addr];
}

void
emu_deo(Uxn *u, Uint8 addr, Uint8 value)
{
	u->dev[addr] = value;
	debug = 0;
	switch(addr) {
	case 0x18: fputc(u->dev[0x18], stdout), fflush(stdout); return;
	case 0x19: fputc(u->dev[0x19], stderr), fflush(stderr); return;
	}
}

#define FLIP     { s = ins & 0x40 ? &u->wst : &u->rst; }
#define JUMP(x)  { if(m2) pc = (x); else pc += (Sint8)(x); }
#define POP1(o)  { o = s->dat[--*sp]; }
#define POP2(o)  { o = s->dat[--*sp] | (s->dat[--*sp] << 0x8); }
#define POPx(o)  { if(m2) { POP2(o) } else POP1(o) }
#define PUSH1(y) { s->dat[s->ptr++] = (y); }
#define PUSH2(y) { tt = (y); s->dat[s->ptr++] = tt >> 0x8; s->dat[s->ptr++] = tt; }
#define PUSHx(y) { if(m2) { PUSH2(y) } else PUSH1(y) }
#define PEEK(o, x, r) { if(m2) { r = (x); o = ram[r++] << 8 | ram[r]; } else o = ram[(x)]; }
#define POKE(x, y, r) { if(m2) { r = (x); ram[r++] = y >> 8; ram[r] = y; } else ram[(x)] = (y); }
#define DEVR(o, p)    { if(m2) { o = (emu_dei(u, p) << 8) | emu_dei(u, p + 1); } else o = emu_dei(u, p); }
#define DEVW(p, y)    { if(m2) { emu_deo(u, p, y >> 8); emu_deo(u, p + 1, y); } else emu_deo(u, p, y); }

int
uxn_eval(Uxn *u, Uint16 pc)
{
	int recurse = 0;
	int halt = 0x1000000; // INT_MAX = 0x7FFFFFFF
	Uint8 *ram = u->ram;
	if(!pc || u->dev[0x0f]) return 0;
	for(;;) {
		Uint16 tt, a, b, c;
		Uint8 t, kp, *sp, ins = ram[pc++];
		/* 2 */ Uint8 m2 = ins & 0x20;
		/* r */ Stack *s = ins & 0x40 ? &u->rst : &u->wst;
		/* k */ if(ins & 0x80) kp = s->ptr, sp = &kp; else sp = &s->ptr;
		/* recurse */ if(recurse++ > halt) return emu_error("Infinite Loop.");
		switch(ins & 0x1f) {
		case 0x00:
		switch(ins) {
			case 0x00: /* BRK */ return 1;
			case 0x20: /* JCI */ POP1(b) if(!b) { pc += 2; break; } /* fall */
			case 0x40: /* JMI */ a = ram[pc++] << 8 | ram[pc++]; pc += a; break;
			case 0x60: /* JSI */ PUSH2(pc + 2) a = ram[pc++] << 8 | ram[pc++]; pc += a; break;
			case 0xa0: case 0xe0: /* LIT2 */ PUSH1(ram[pc++]) /* fall */
			case 0x80: case 0xc0: /* LIT  */ PUSH1(ram[pc++]) break;
		} break;
		case 0x01: /* INC */ POPx(a) PUSHx(a + 1) break;
		case 0x02: /* POP */ POPx(a) break;
		case 0x03: /* NIP */ POPx(a) POPx(b) PUSHx(a) break;
		case 0x04: /* SWP */ POPx(a) POPx(b) PUSHx(a) PUSHx(b) break;
		case 0x05: /* ROT */ POPx(a) POPx(b) POPx(c) PUSHx(b) PUSHx(a) PUSHx(c) break;
		case 0x06: /* DUP */ POPx(a) PUSHx(a) PUSHx(a) break;
		case 0x07: /* OVR */ POPx(a) POPx(b) PUSHx(b) PUSHx(a) PUSHx(b) break;
		case 0x08: /* EQU */ POPx(a) POPx(b) PUSH1(b == a) break;
		case 0x09: /* NEQ */ POPx(a) POPx(b) PUSH1(b != a) break;
		case 0x0a: /* GTH */ POPx(a) POPx(b) PUSH1(b > a) break;
		case 0x0b: /* LTH */ POPx(a) POPx(b) PUSH1(b < a) break;
		case 0x0c: /* JMP */ POPx(a) JUMP(a) break;
		case 0x0d: /* JCN */ POPx(a) POP1(b) if(b) JUMP(a) break;
		case 0x0e: /* JSR */ POPx(a) FLIP PUSH2(pc) JUMP(a) break;
		case 0x0f: /* STH */ POPx(a) FLIP PUSHx(a) break;
		case 0x10: /* LDZ */ POP1(a) PEEK(b, a, t) PUSHx(b) break;
		case 0x11: /* STZ */ POP1(a) POPx(b) POKE(a, b, t) break;
		case 0x12: /* LDR */ POP1(a) PEEK(b, pc + (Sint8)a, tt) PUSHx(b) break;
		case 0x13: /* STR */ POP1(a) POPx(b) POKE(pc + (Sint8)a, b, tt) break;
		case 0x14: /* LDA */ POP2(a) PEEK(b, a, tt) PUSHx(b) break;
		case 0x15: /* STA */ POP2(a) POPx(b) POKE(a, b, tt) break;
		case 0x16: /* DEI */ POP1(a) DEVR(b, a) PUSHx(b) break;
		case 0x17: /* DEO */ POP1(a) POPx(b) DEVW(a, b) break;
		case 0x18: /* ADD */ POPx(a) POPx(b) PUSHx(b + a) break;
		case 0x19: /* SUB */ POPx(a) POPx(b) PUSHx(b - a) break;
		case 0x1a: /* MUL */ POPx(a) POPx(b) PUSHx(b * a) break;
		case 0x1b: /* DIV */ POPx(a) POPx(b) PUSHx(a ? b / a : 0) break;
		case 0x1c: /* AND */ POPx(a) POPx(b) PUSHx(b & a) break;
		case 0x1d: /* ORA */ POPx(a) POPx(b) PUSHx(b | a) break;
		case 0x1e: /* EOR */ POPx(a) POPx(b) PUSHx(b ^ a) break;
		case 0x1f: /* SFT */ POP1(a) POPx(b) PUSHx(b >> (a & 0xf) << (a >> 4)) break;
		}
	}
}

void
console_input(Uxn *u, char c, int type)
{
	u->dev[0x12] = c, u->dev[0x17] = type;
	uxn_eval(u, u->dev[0x10] << 8 | u->dev[0x11]);
}

int
main(int argc, char **argv)
{
	FILE *f;
	int i = 1;
	Uxn u = {0};
	if(argc < 2) 
		return emu_error("usage: uxnbot file.rom [args..]");
	if(!(f = fopen(argv[i++], "rb"))) 
		return emu_error("No rom file.");
	u.dev[0x17] = argc - i;
	fread(&u.ram[0x0100], 0xff00, 1, f), fclose(f);
	if(uxn_eval(&u, 0x0100) && (u.dev[0x10] << 8 | u.dev[0x11])) {
		for(; i < argc; i++) {
			char *p = argv[i];
			while(*p) console_input(&u, *p++, 0x2);
			console_input(&u, '\n', i == argc - 1 ? 0x4 : 0x3);
		}
		while(!u.dev[0x0f]) {
			int c = fgetc(stdin);
			if(c == EOF) {
				console_input(&u, 0x00, 0x4);
				break;
			}
			console_input(&u, (Uint8)c, 0x1);
		}
	}
	if(debug) system_inspect(&u);
	return u.dev[0x0f] & 0x7f;
}
