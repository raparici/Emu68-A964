#include <capstone/capstone.h>
#include <tlsf.h>
#include <support.h>
#include <stdarg.h>

extern void *tlsf;

void *my_malloc(size_t size)
{
    return tlsf_malloc(tlsf, size);
}

void *my_calloc(size_t nmemb, size_t size)
{
    void *ptr = tlsf_malloc(tlsf, size * nmemb);
    bzero(ptr, size * nmemb);
    return ptr;
}

void *my_realloc(void *ptr, size_t size)
{
    return tlsf_realloc(tlsf, ptr, size);
}

void my_free(void *ptr)
{
    tlsf_free(tlsf, ptr);
}

struct putc_data {
    char *buffer;
    size_t written;
    size_t limit;
};

void my_putc(void *data, const char c)
{
    struct putc_data *p = data;

    if (p->written < p->limit)
    {
        p->buffer[p->written] = c;
        p->buffer[p->written+1] = 0;
    }

    p->written++;
}

int my_vsnprintf(char *str, size_t size, const char *format, va_list ap)
{
    struct putc_data p;
    p.buffer = str;
    p.limit = size;
    p.written = 0;

    vkprintf_pc(my_putc, &p, format, ap);

    return p.written;
}

void disasm_init()
{
    cs_opt_mem setup;

    setup.malloc = my_malloc;
    setup.calloc = my_calloc;
    setup.realloc = my_realloc;
    setup.free = my_free;
    setup.vsnprintf = my_vsnprintf;

    if (!cs_option(0, CS_OPT_MEM, (uintptr_t)&setup)) {
        kprintf("[BOOT] Disassembler set up\n");
    } else {
        kprintf("[BOOT] Disassembler init error\n");
    }
}

csh h_m68k;
csh h_arm;

void disasm_open()
{
    cs_err err;
    err = cs_open(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_040, &h_m68k);
#ifdef __aarch64__
    err = cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &h_arm);
#else
    err = cs_open(CS_ARCH_ARM, CS_MODE_BIG_ENDIAN | CS_MODE_ARM, &h_arm);
#endif
    (void)err;
}

void disasm_close()
{
    cs_close(&h_m68k);
    cs_close(&h_arm);
}

void disasm_print(uint16_t *m68k_addr, uint16_t m68k_count, uint32_t *arm_addr, size_t arm_size, uint32_t *arm_start)
{
    cs_insn *insn_m68k;
    cs_insn *insn_arm;
    size_t count_m68k = 0;
    size_t count_arm = 0;
	char fixed_op_str[200];

    if (m68k_addr)
        count_m68k = cs_disasm(h_m68k, (const uint8_t *)m68k_addr, 20*m68k_count, (uintptr_t)m68k_addr, m68k_count, &insn_m68k);
    if (arm_addr)
        count_arm = cs_disasm(h_arm, (const uint8_t *)arm_addr, arm_size, (uintptr_t)arm_addr - (uintptr_t)arm_start, 0, &insn_arm);

    for (size_t i=0; i < count_m68k; i++)
    {
        kprintf("[JIT] %08x: %7s %21s", insn_m68k[i].address, insn_m68k[i].mnemonic, insn_m68k[i].op_str);
        if (i != count_m68k - 1)
            kprintf("\n");
    }

    if (count_m68k == 0)
        kprintf("[JIT]                                        ");

    for (size_t i=0; i < count_arm; i++)
    {
		char last_char = 0;
		char size = 0;
		char num = 0;
		int p = 0;
		static const char *regnames[] = {
			"A0", "A1", "A2", "A3", "A4", "PC", "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7", "A5", "A6", "A7"
		};

		for (int j=0; j < 160; j++) {
			if (insn_arm[i].op_str[j] == 0) {
				fixed_op_str[p] = 0;
				break;
			}
			
			if (insn_arm[i].op_str[j] == 'w')
				size = 'w';
			else if (insn_arm[i].op_str[j] == 'x' && last_char != '0')
				size = 'x';
			else
				size = 0;

			if (size) {
				char c1 = insn_arm[i].op_str[j + 1];
				char c2 = insn_arm[i].op_str[j + 2];

				if (
					(c1 >= '0' && c1 <= '9' && c2 >= '0' && c2 <= '9') &&
					(insn_arm[i].op_str[j + 3] < '0' || insn_arm[i].op_str[j + 3] > '9') &&
					(insn_arm[i].op_str[j + 3] < 'a' || insn_arm[i].op_str[j + 3] > 'f'))
				{
					num = (c1-'0')*10 + (c2-'0');
					const char *src;

					if (num < 13 || num > 29) {
						size = 0;
					}
					else {
						src = regnames[num - 13];
						fixed_op_str[p++] = *src++;
						fixed_op_str[p++] = *src++;

						if (size == 'x') {
							fixed_op_str[p++] = ':';
							fixed_op_str[p++] = '6';
							fixed_op_str[p++] = '4';
						}

						j += 2;
					}
				}
				else size = 0;
			}

			if (!size) {
				fixed_op_str[p++] = insn_arm[i].op_str[j];
			}

			last_char = insn_arm[i].op_str[j];
		}

        if (i > 0)
            kprintf("[JIT]                                        ");
        kprintf("-> %08x: %7s %s\n", insn_arm[i].address, insn_arm[i].mnemonic, fixed_op_str); /*insn_arm[i].op_str);*/
    }

    if (count_m68k)
        cs_free(insn_m68k, count_m68k);
    if (count_arm)
        cs_free(insn_arm, count_arm);
}
#if defined(__arm__) && !defined(__aarch64__) && !defined(__APPLE__)
#define R10_TEXT   __attribute__((section(".r10_tail_text"), noinline, optimize("Os")))
#define R10_RODATA __attribute__((section(".r10_tail_rodata")))
#define R10_TEXTEND __attribute__((section(".r10_textend"), noinline, optimize("Os")))   /* both images: .text's end */
#define R10_BSS    __attribute__((section(".bss.~r10")))
#else
#define R10_TEXT   __attribute__((noinline))
#define R10_TEXTEND __attribute__((noinline))
#define R10_RODATA
#define R10_BSS
#endif

uint32_t g_disasm_ok R10_BSS;               /* both handles open (disasm_open_b) */
uint32_t g_disasm_last_count R10_BSS;       /* m68k instructions the last dump decoded */
uintptr_t g_r10_span_addr R10_BSS;          /* the dumped unit's m68k start (src/jit_levers.c jit_dbg_unit_w) */
uintptr_t g_r10_span_bytes R10_BSS;         /* and its span: [mt_M68kAddress, mt_M68kHigh) */

/* written + 1 < limit: the NUL after the last character fits too */
R10_TEXTEND static void my_putc_b(void *data, const char c)
{
    struct putc_data *p = data;

    if (p->written + 1 < p->limit)
    {
        p->buffer[p->written] = c;
        p->buffer[p->written+1] = 0;
    }

    p->written++;
}

R10_TEXTEND static int my_vsnprintf_b(char *str, size_t size, const char *format, va_list ap)
{
    struct putc_data p;
    p.buffer = str;
    p.limit = size;
    p.written = 0;
    if (size)
        str[0] = 0;

    vkprintf_pc(my_putc_b, &p, format, ap);

    return p.written;
}

static const char s_r10_err[] R10_RODATA = "[JIT] capstone %d %d\n";
R10_TEXT void disasm_open_b(void)
{
    cs_opt_mem setup;
    cs_err e_m68k, e_arm;

    setup.malloc = my_malloc;
    setup.calloc = my_calloc;
    setup.realloc = my_realloc;
    setup.free = my_free;
    setup.vsnprintf = my_vsnprintf_b;
    cs_option(0, CS_OPT_MEM, (uintptr_t)&setup);

    e_m68k = cs_open(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_040, &h_m68k);
#ifdef __aarch64__
    e_arm = cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &h_arm);
#else
    e_arm = cs_open(CS_ARCH_ARM, CS_MODE_ARM, &h_arm);
#endif
    g_disasm_ok = e_m68k == CS_ERR_OK && e_arm == CS_ERR_OK;
    if (!g_disasm_ok)
        kprintf(s_r10_err, (int)e_m68k, (int)e_arm);
}


#define R10_M68K_ADDR(in) ((uint32_t)(in)->address)

R10_TEXT void disasm_print_b(uint16_t *m68k_addr, uint16_t m68k_count, uint32_t *arm_addr, size_t arm_size, uint32_t *arm_start)
{
    cs_insn *in;
    const uint8_t *code;
    size_t size;
    uint64_t addr;
    uint32_t n = 0;

    g_disasm_last_count = 0;
    if (!g_disasm_ok || !h_m68k || !h_arm)
        return;                         /* not open (disasm_open_b said why), or closed by the translator's path */
    in = cs_malloc(h_m68k);             /* ONE instruction for both sides: detail is off, the struct is the same */
    if (!in)
    {
        kprintf(s_r10_err, (int)cs_errno(h_m68k), (int)cs_errno(h_arm));
        return;
    }
    if (m68k_addr)
    {
        code = (const uint8_t *)m68k_addr;
        size = (uintptr_t)m68k_addr == g_r10_span_addr ? g_r10_span_bytes : 22u * m68k_count;
        addr = (uintptr_t)m68k_addr;
        while (n < m68k_count && cs_disasm_iter(h_m68k, &code, &size, &addr, in))
        {
            if (n)
                kprintf("\n");
            kprintf("[JIT] %08x: %7s %21s", R10_M68K_ADDR(in), in->mnemonic, in->op_str);
            n++;
        }
    }
    g_disasm_last_count = n;
    if (n == 0)
        kprintf("[JIT]                                        ");
    if (arm_addr)
    {
        code = (const uint8_t *)arm_addr;
        size = arm_size;
        addr = (uintptr_t)arm_addr - (uintptr_t)arm_start;
        n = 0;
        while (cs_disasm_iter(h_arm, &code, &size, &addr, in))
        {
            if (n)
                kprintf("[JIT]                                        ");
            kprintf("-> %08x: %7s %s\n", (uint32_t)in->address, in->mnemonic, in->op_str);
            n++;
        }
    }
    cs_free(in, 1);
}
