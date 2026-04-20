#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include "font.h"

struct limine_framebuffer *global_fb = NULL;

void clear_screen(struct limine_framebuffer *fb);
void draw_string(const char *str, int x, int y, uint32_t color, struct limine_framebuffer *fb);
void ptr_to_hex(uint64_t ptr, char *buffer);


#define PTE_PRESENT 0x01
#define PTE_RW      0x02
#define PTE_USER    0x04

#define PML4_GET_INDEX(addr) (((uint64_t)(addr) >> 39) & 0x1FF)
#define PDPT_GET_INDEX(addr) (((uint64_t)(addr) >> 30) & 0x1FF)
#define PD_GET_INDEX(addr)   (((uint64_t)(addr) >> 21) & 0x1FF)
#define PT_GET_INDEX(addr)   (((uint64_t)(addr) >> 12) & 0x1FF)


struct idt_entry {
    uint16_t isr_low;
    uint16_t kernel_cs;
    uint8_t  ist;
    uint8_t  attributes;
    uint16_t isr_mid;
    uint32_t isr_high;
    uint32_t reserved;
} __attribute__((packed));

struct idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct interrupt_frame {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

__attribute__((aligned(0x10))) static struct idt_entry idt[256];
static struct idtr idtr;

static inline uint16_t get_cs(void)
{
    uint16_t cs;
    __asm__ volatile("mov %%cs, %0" : "=r"(cs));
    return cs;
}

static inline uint64_t read_cr2(void)
{
    uint64_t cr2_val;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2_val));
    return cr2_val;   /* BUG FIX: removed stray '/' that was here */
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}


__attribute__((interrupt)) void exception_div_zero(struct interrupt_frame *frame)
{
    (void)frame;
    outb(0x3F8, 'E');
    outb(0x3F8, 'R');
    outb(0x3F8, 'R');
    for (;;) { __asm__ volatile("cli; hlt"); }
}

__attribute__((interrupt)) void exception_page_fault(struct interrupt_frame *frame, uint64_t error_code)
{
    (void)frame;
    (void)error_code;

    uint64_t faulting_address = read_cr2();

    if (global_fb != NULL) {
        clear_screen(global_fb);
        draw_string("KERNEL PANIC: PAGE FAULT!", 10, 10, 0xFF0000, global_fb);
        draw_string("The CPU tried to access unmapped memory.", 10, 30, 0xFFFFFF, global_fb);
        draw_string("Faulting Address: ", 10, 50, 0xFFFFFF, global_fb);

        char addr_str[20];
        ptr_to_hex(faulting_address, addr_str);
        draw_string(addr_str, 10 + (18 * 8), 50, 0x00FF00, global_fb);
        draw_string("System Halted.", 10, 70, 0xFF0000, global_fb);
    }
    for (;;) { __asm__ volatile("cli; hlt"); }
}

void idt_set_descriptor(uint8_t vector, void *isr, uint8_t flags)
{
    uint64_t descriptor = (uint64_t)isr;
    idt[vector].isr_low   = (uint16_t)(descriptor & 0xFFFF);
    idt[vector].kernel_cs = get_cs();
    idt[vector].ist       = 0;
    idt[vector].attributes = flags;
    idt[vector].isr_mid   = (uint16_t)((descriptor >> 16) & 0xFFFF);
    idt[vector].isr_high  = (uint32_t)((descriptor >> 32) & 0xFFFFFFFF);
    idt[vector].reserved  = 0;
}

void idt_init(void)
{
    idtr.base  = (uint64_t)&idt[0];
    idtr.limit = (uint16_t)sizeof(struct idt_entry) * 256 - 1;
    idt_set_descriptor(0,  exception_div_zero,  0x8E);
    idt_set_descriptor(14, exception_page_fault, 0x8E);
    __asm__ volatile("lidt %0" : : "m"(idtr));
    __asm__ volatile("sti");
}

// ============================================================
//  Limine Requests
// ============================================================
__attribute__((used, section(".requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST, .revision = 0
};
__attribute__((used, section(".requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST, .revision = 0
};
__attribute__((used, section(".requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST, .revision = 0
};
__attribute__((used, section(".requests")))
static volatile struct limine_kernel_address_request kernel_address_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST, .revision = 0
};

uint8_t read_rtc(uint8_t reg)
{
    outb(0x70, reg);
    return inb(0x71);
}


char scancode_map[] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    'z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
};

char scancode_map_shift[] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
    'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' '
};


void *memset(void *s, int c, size_t n)
{
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

size_t strlen(const char *str)
{
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    while (n && *s1 && (*s1 == *s2)) { ++s1; ++s2; --n; }
    if (n == 0) return 0;
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

/* Safe string copy — always null-terminates */
void strncpy_safe(char *dst, const char *src, size_t max)
{
    size_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

void itoa(int n, char str[])
{
    int i = 0, sign = n;
    if (n < 0) n = -n;
    do { str[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
    if (sign < 0) str[i++] = '-';
    str[i] = '\0';
    int j = 0, k = i - 1;
    while (j < k) {
        char temp = str[j]; str[j] = str[k]; str[k] = temp;
        j++; k--;
    }
}

uint32_t hex2int(const char *hex)
{
    uint32_t val = 0;
    while (*hex) {
        uint8_t byte = *hex++;
        if      (byte >= '0' && byte <= '9') byte = byte - '0';
        else if (byte >= 'a' && byte <= 'f') byte = byte - 'a' + 10;
        else if (byte >= 'A' && byte <= 'F') byte = byte - 'A' + 10;
        else break;
        val = (val << 4) | (byte & 0xF);
    }
    return val;
}

void ptr_to_hex(uint64_t ptr, char *buffer)
{
    const char *hex_chars = "0123456789ABCDEF";
    buffer[0] = '0';
    buffer[1] = 'x';
    for (int i = 0; i < 16; i++)
        buffer[17 - i] = hex_chars[(ptr >> (i * 4)) & 0xF];
    buffer[18] = '\0';
}

// ============================================================
//  Virtual Memory Helpers
// ============================================================
static inline void *phys_to_virt(uint64_t physical_addr)
{
    return (void *)physical_addr + hhdm_request.response->offset;
}

void get_cpuid_string(char *str)
{
    uint32_t ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
    str[0]  = (ebx >> 0)  & 0xFF; str[1]  = (ebx >> 8)  & 0xFF;
    str[2]  = (ebx >> 16) & 0xFF; str[3]  = (ebx >> 24) & 0xFF;
    str[4]  = (edx >> 0)  & 0xFF; str[5]  = (edx >> 8)  & 0xFF;
    str[6]  = (edx >> 16) & 0xFF; str[7]  = (edx >> 24) & 0xFF;
    str[8]  = (ecx >> 0)  & 0xFF; str[9]  = (ecx >> 8)  & 0xFF;
    str[10] = (ecx >> 16) & 0xFF; str[11] = (ecx >> 24) & 0xFF;
    str[12] = '\0';
}



#define VAR_MAX_ENTRIES  32    /* maximum number of variables   */
#define VAR_KEY_LEN      32    /* maximum length of a name      */
#define VAR_VAL_LEN      128   /* maximum length of a value     */

typedef struct {
    char key[VAR_KEY_LEN];
    char value[VAR_VAL_LEN];
    int  used;                 /* 0 = free slot, 1 = occupied   */
} var_entry_t;

static var_entry_t var_table[VAR_MAX_ENTRIES];

/* ----------------------------------------------------------
   var_table_init  –  zero every slot at boot
   ---------------------------------------------------------- */
void var_table_init(void)
{
    for (int i = 0; i < VAR_MAX_ENTRIES; i++)
        var_table[i].used = 0;
}

/* ----------------------------------------------------------
   var_alloc  –  reserve an empty slot for a new variable.
                 Returns a pointer to the entry, or NULL
                 when the table is full.
                 (If the key already exists its slot is
                  returned instead, so we never duplicate.)
   ---------------------------------------------------------- */
static var_entry_t *var_alloc(const char *key)
{
    int first_free = -1;

    for (int i = 0; i < VAR_MAX_ENTRIES; i++) {
        if (var_table[i].used && strcmp(var_table[i].key, key) == 0)
            return &var_table[i];          /* key already exists  */
        if (!var_table[i].used && first_free < 0)
            first_free = i;               /* remember first hole */
    }

    if (first_free < 0)
        return NULL;                      /* table is full       */

    var_table[first_free].used = 1;
    strncpy_safe(var_table[first_free].key, key, VAR_KEY_LEN);
    var_table[first_free].value[0] = '\0';
    return &var_table[first_free];
}

/* ----------------------------------------------------------
   var_set  –  store (or overwrite) a key → value pair.
               Returns 1 on success, 0 if table is full.
   ---------------------------------------------------------- */
int var_set(const char *key, const char *value)
{
    var_entry_t *entry = var_alloc(key);
    if (entry == NULL) return 0;
    strncpy_safe(entry->value, value, VAR_VAL_LEN);
    return 1;
}

/* ----------------------------------------------------------
   var_get  –  look up a key and return its value string,
               or NULL if the variable does not exist.
   ---------------------------------------------------------- */
const char *var_get(const char *key)
{
    for (int i = 0; i < VAR_MAX_ENTRIES; i++) {
        if (var_table[i].used && strcmp(var_table[i].key, key) == 0)
            return var_table[i].value;
    }
    return NULL;
}

/* ----------------------------------------------------------
   var_delete  –  free a slot by name.
                  Returns 1 if deleted, 0 if not found.
   ---------------------------------------------------------- */
int var_delete(const char *key)
{
    for (int i = 0; i < VAR_MAX_ENTRIES; i++) {
        if (var_table[i].used && strcmp(var_table[i].key, key) == 0) {
            var_table[i].used = 0;
            return 1;
        }
    }
    return 0;
}

/* ----------------------------------------------------------
   var_expand  –  copy `src` into `dst`, replacing every
                  $NAME token with its stored value.
                  dst must be at least `dst_len` bytes.
   ---------------------------------------------------------- */
void var_expand(const char *src, char *dst, size_t dst_len)
{
    size_t di = 0;                         /* index into dst     */

    for (size_t si = 0; src[si] && di < dst_len - 1; si++) {

        if (src[si] == '$') {
            /* collect the variable name (letters, digits, _) */
            char name[VAR_KEY_LEN];
            int ni = 0;
            si++;                          /* skip '$'           */
            while (src[si] && ni < VAR_KEY_LEN - 1 &&
                   (  (src[si] >= 'a' && src[si] <= 'z')
                   || (src[si] >= 'A' && src[si] <= 'Z')
                   || (src[si] >= '0' && src[si] <= '9')
                   ||  src[si] == '_')) {
                name[ni++] = src[si++];
            }
            name[ni] = '\0';
            si--;                          /* outer loop will ++ */

            const char *val = var_get(name);
            if (val) {
                for (size_t vi = 0; val[vi] && di < dst_len - 1; vi++)
                    dst[di++] = val[vi];
            } else {
                /* unknown variable — emit $name literally       */
                dst[di++] = '$';
                for (int ni2 = 0; name[ni2] && di < dst_len - 1; ni2++)
                    dst[di++] = name[ni2];
            }
        } else {
            dst[di++] = src[si];
        }
    }
    dst[di] = '\0';
}

// ============================================================
//  Drawing Functions
// ============================================================
void draw_pixel(int x, int y, uint32_t color, struct limine_framebuffer *fb)
{
    uint32_t *fb_ptr = fb->address;
    fb_ptr[y * (fb->pitch / 4) + x] = color;
}

void draw_safe_pixel(int x, int y, uint32_t color, struct limine_framebuffer *fb)
{
    if (x >= 0 && x < (int)fb->width && y >= 0 && y < (int)fb->height)
        draw_pixel(x, y, color, fb);
}

void draw_rect(int sx, int sy, int w, int h, uint32_t color, struct limine_framebuffer *fb)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int cx = sx + x, cy = sy + y;
            if (cx >= 0 && cx < (int)fb->width && cy >= 0 && cy < (int)fb->height)
                draw_pixel(cx, cy, color, fb);
        }
}

void draw_line_fb(int x0, int y0, int x1, int y1, uint32_t color, struct limine_framebuffer *fb)
{
    int dx = (x1>x0)?(x1-x0):(x0-x1), dy = (y1>y0)?(y1-y0):(y0-y1);
    int sx = (x0<x1)?1:-1, sy = (y0<y1)?1:-1;
    int err = (dx>dy?dx:-dy)/2, e2;
    while (1) {
        if (x0>=0&&x0<(int)fb->width&&y0>=0&&y0<(int)fb->height) draw_pixel(x0,y0,color,fb);
        if (x0==x1&&y0==y1) break;
        e2=err;
        if (e2>-dx){err-=dy;x0+=sx;}
        if (e2< dy){err+=dx;y0+=sy;}
    }
}

void draw_hollow_circle(int cx, int cy, int r, uint32_t color, struct limine_framebuffer *fb)
{
    int x=0,y=r,p=3-2*r;
    while(y>=x){
        draw_safe_pixel(cx+x,cy+y,color,fb); draw_safe_pixel(cx-x,cy+y,color,fb);
        draw_safe_pixel(cx+x,cy-y,color,fb); draw_safe_pixel(cx-x,cy-y,color,fb);
        draw_safe_pixel(cx+y,cy+x,color,fb); draw_safe_pixel(cx-y,cy+x,color,fb);
        draw_safe_pixel(cx+y,cy-x,color,fb); draw_safe_pixel(cx-y,cy-x,color,fb);
        x++;
        if(p>0){y--;p=p+4*(x-y)+10;}else{p=p+4*x+6;}
    }
}

void draw_hexagon(int cx, int cy, int r, uint32_t color, struct limine_framebuffer *fb)
{
    int dx=(r*866)/1000, dy=r/2;
    int x0=cx,y0=cy-r, x1=cx+dx,y1=cy-dy, x2=cx+dx,y2=cy+dy;
    int x3=cx,y3=cy+r, x4=cx-dx,y4=cy+dy, x5=cx-dx,y5=cy-dy;
    draw_line_fb(x0,y0,x1,y1,color,fb); draw_line_fb(x1,y1,x2,y2,color,fb);
    draw_line_fb(x2,y2,x3,y3,color,fb); draw_line_fb(x3,y3,x4,y4,color,fb);
    draw_line_fb(x4,y4,x5,y5,color,fb); draw_line_fb(x5,y5,x0,y0,color,fb);
}

void draw_circle(int cx, int cy, int r, uint32_t color, struct limine_framebuffer *fb)
{
    for (int y=-r;y<=r;y++)
        for (int x=-r;x<=r;x++)
            if (x*x+y*y<=r*r) draw_safe_pixel(cx+x,cy+y,color,fb);
}

void draw_heart(int cx, int cy, int r, uint32_t color, struct limine_framebuffer *fb)
{
    int bound=(r*13)/10;
    for (int y=-bound;y<=bound;y++)
        for (int x=-bound;x<=bound;x++){
            int64_t X=x,Y=-y,r2=(int64_t)r*r;
            int64_t p1=X*X+Y*Y-r2;
            int64_t t1=p1*p1*p1, t2=X*X*(Y*Y*Y)*r;
            if(t1-t2<=0) draw_safe_pixel(cx+x,cy+y,color,fb);
        }
}

void draw_triangle(int x1,int y1,int x2,int y2,int x3,int y3,uint32_t color,struct limine_framebuffer *fb)
{
    draw_line_fb(x1,y1,x2,y2,color,fb);
    draw_line_fb(x2,y2,x3,y3,color,fb);
    draw_line_fb(x3,y3,x1,y1,color,fb);
}

void draw_char(char c, int x, int y, uint32_t color, struct limine_framebuffer *fb)
{
    for (int row=0;row<8;row++)
        for (int col=0;col<8;col++)
            if ((font8x8_basic[(uint8_t)c][row]>>(7-col))&1)
                draw_pixel(x+col,y+row,color,fb);
}

void draw_string(const char *str, int x, int y, uint32_t color, struct limine_framebuffer *fb)
{
    for (int i=0;str[i];i++) draw_char(str[i],x+(i*8),y,color,fb);
}

void draw_cursor(int x, int y, uint32_t color, struct limine_framebuffer *fb)
{
    for (int i=0;i<8;i++)
        for (int j=8;j<10;j++)
            draw_pixel(x+i,y+j,color,fb);
}

void clear_screen(struct limine_framebuffer *fb)
{
    uint32_t *fb_ptr = fb->address;
    for (size_t i=0;i<fb->width*fb->height;i++) fb_ptr[i]=0x300A24;
}

void scroll_screen(struct limine_framebuffer *fb)
{
    uint32_t *fb_ptr = (uint32_t *)fb->address;
    size_t ppr    = fb->pitch / 4;
    size_t offset = 12 * ppr;
    size_t total  = fb->height * ppr;
    for (size_t i=0;i<total-offset;i++) fb_ptr[i]=fb_ptr[i+offset];
    for (size_t i=total-offset;i<total;i++) fb_ptr[i]=0x300A24;
}

// ============================================================
//  PMM
// ============================================================
#define PAGE_SIZE 4096
#define BITMAP_SET(bm,b)  ((bm)[(b)/8] |=  (1<<((b)%8)))
#define BITMAP_CLEAR(bm,b)((bm)[(b)/8] &= ~(1<<((b)%8)))
#define BITMAP_TEST(bm,b) ((bm)[(b)/8] &   (1<<((b)%8)))

uint8_t  *pmm_bitmap      = NULL;
uint64_t  pmm_bitmap_size = 0;
uint64_t  pmm_total_pages = 0;

void *pmm_alloc_page(void)
{
    for (uint64_t i=0;i<pmm_bitmap_size*8;i++)
        if (!BITMAP_TEST(pmm_bitmap,i)) {
            BITMAP_SET(pmm_bitmap,i);
            return (void *)(i*PAGE_SIZE);
        }
    return NULL;
}

void pmm_free_page(void *ptr)
{
    BITMAP_CLEAR(pmm_bitmap,(uint64_t)ptr/PAGE_SIZE);
}

// ============================================================
//  VMM
// ============================================================
void map_page(uint64_t *pml4, uint64_t va, uint64_t pa, uint64_t flags)
{
    #define ENSURE_TABLE(tbl, idx) \
        if (!(tbl[idx] & PTE_PRESENT)) { \
            uint64_t np = (uint64_t)pmm_alloc_page(); \
            memset(phys_to_virt(np), 0, PAGE_SIZE); \
            tbl[idx] = np | PTE_PRESENT | PTE_RW | PTE_USER; \
        }

    uint64_t pml4i = PML4_GET_INDEX(va);
    ENSURE_TABLE(pml4, pml4i);

    uint64_t *pdpt = (uint64_t *)phys_to_virt(pml4[pml4i] & ~0xFFFULL);
    uint64_t pdpti = PDPT_GET_INDEX(va);
    ENSURE_TABLE(pdpt, pdpti);

    uint64_t *pd  = (uint64_t *)phys_to_virt(pdpt[pdpti] & ~0xFFFULL);
    uint64_t pdi  = PD_GET_INDEX(va);
    ENSURE_TABLE(pd, pdi);

    uint64_t *pt  = (uint64_t *)phys_to_virt(pd[pdi] & ~0xFFFULL);
    pt[PT_GET_INDEX(va)] = pa | flags;

    #undef ENSURE_TABLE
}

// ============================================================
//  Shell Helper: parse one word from input at index *i
//  into `out` (null-terminated), advance *i past it.
// ============================================================
static void parse_word(const char *buf, int *i, char *out, int max)
{
    while (buf[*i] == ' ') (*i)++;
    int k = 0;
    while (buf[*i] && buf[*i] != ' ' && k < max-1)
        out[k++] = buf[(*i)++];
    out[k] = '\0';
}

// ============================================================
//  Shell Helper: ask a y/n question, return 'y' or 'n'
// ============================================================
static char ask_yn(const char *question, int *cur_x, int *cur_y,
                   uint32_t text_color, struct limine_framebuffer *fb)
{
    draw_string(question, *cur_x, *cur_y, 0xFCE94F, fb);
    *cur_x += strlen(question) * 8;
    draw_cursor(*cur_x, *cur_y, text_color, fb);

    while (1) {
        if (inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if (sc < 0x80) {
                char k = scancode_map[sc];
                if (k=='y'||k=='n'||k=='Y'||k=='N') {
                    draw_char(k, *cur_x, *cur_y, text_color, fb);
                    return k;
                }
            }
        }
    }
}

// ============================================================
//  _start
// ============================================================
void _start(void)
{
    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count < 1)
        for (;;) __asm__("hlt");

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    global_fb = fb;
    idt_init();
    var_table_init();         /* ← initialize the variable dictionary */

    clear_screen(fb);
    draw_string("Welcome to MyOS Shell!", 10, 10, 0xFFFFFF, fb);

    /* cur_y starts just below the welcome line (y=10, font=8px, +4 gap) */
    int cur_x = 10, cur_y = 22;
    char input_buffer[256];
    int  buffer_index = 0;
    uint32_t current_text_color = 0xFFFFFF;

    int shift_pressed = 0;

    char cmd_history[10][256];
    int  history_count = 0;
    int  history_index = 0;
    int  e0_prefix     = 0;

    uint32_t blink_speed   = 3000000;
    uint32_t blink_counter = 0;
    int      cursor_visible = 1;

    /* ---- Memory Map ---- */
    if (memmap_request.response == NULL) {
        draw_string("PANIC: No memory map!", 10, 10, 0xFF0000, fb);
        for (;;) __asm__("hlt");
    }
    struct limine_memmap_response *memmap = memmap_request.response;
    struct limine_memmap_entry   **entries = memmap->entries;

    uint64_t total_usable_ram  = 0;
    uint64_t highest_ram_addr  = 0;

    for (uint64_t i=0;i<memmap->entry_count;i++)
        if (entries[i]->type == LIMINE_MEMMAP_USABLE) {
            total_usable_ram += entries[i]->length;
            uint64_t top = entries[i]->base + entries[i]->length;
            if (top > highest_ram_addr) highest_ram_addr = top;
        }

    /* ---- PMM init ---- */
    pmm_total_pages = highest_ram_addr / PAGE_SIZE;
    pmm_bitmap_size = pmm_total_pages / 8;
    if (pmm_total_pages % 8) pmm_bitmap_size++;

    for (uint64_t i=0;i<memmap->entry_count;i++)
        if (entries[i]->type==LIMINE_MEMMAP_USABLE && entries[i]->length>=pmm_bitmap_size)
            { pmm_bitmap=(uint8_t *)entries[i]->base; break; }

    if (!pmm_bitmap) {
        draw_string("PANIC: No RAM for PMM bitmap!", 10, cur_y, 0xFF0000, fb);
        for (;;) __asm__("hlt");
    }

    for (uint64_t i=0;i<pmm_bitmap_size;i++) pmm_bitmap[i]=0xFF;

    for (uint64_t i=0;i<memmap->entry_count;i++)
        if (entries[i]->type==LIMINE_MEMMAP_USABLE) {
            uint64_t sp = entries[i]->base/PAGE_SIZE;
            uint64_t np = entries[i]->length/PAGE_SIZE;
            for (uint64_t p=0;p<np;p++) BITMAP_CLEAR(pmm_bitmap,sp+p);
        }

    uint64_t bsp = (uint64_t)pmm_bitmap/PAGE_SIZE;
    uint64_t bnp = pmm_bitmap_size/PAGE_SIZE;
    if (pmm_bitmap_size%PAGE_SIZE) bnp++;
    for (uint64_t p=0;p<bnp;p++) BITMAP_SET(pmm_bitmap,bsp+p);

    void *test_page = pmm_alloc_page();
    if (test_page) {
        char addr_str[20];
        ptr_to_hex((uint64_t)test_page, addr_str);
        draw_string("PMM OK – first frame:", 10, cur_y, 0x00FF00, fb);
        draw_string(addr_str, 10+(22*8), cur_y, 0xFFFFFF, fb);
        cur_y += 16;
        pmm_free_page(test_page);
    } else {
        draw_string("PMM error.", 10, cur_y, 0xFF0000, fb);
        cur_y += 16;
    }

    /* ---- VMM init ---- */
    uint64_t  pml4_phys = (uint64_t)pmm_alloc_page();
    uint64_t *pml4      = (uint64_t *)phys_to_virt(pml4_phys);
    memset(pml4, 0, PAGE_SIZE);

    for (uint64_t i=0;i<memmap->entry_count;i++) {
        uint64_t base=memmap->entries[i]->base, len=memmap->entries[i]->length;
        for (uint64_t off=0;off<len;off+=PAGE_SIZE) {
            uint64_t phys=base+off;
            map_page(pml4,(uint64_t)phys_to_virt(phys),phys,PTE_PRESENT|PTE_RW);
        }
    }

    uint64_t kp=kernel_address_request.response->physical_base;
    uint64_t kv=kernel_address_request.response->virtual_base;
    for (uint64_t off=0;off<16*1024*1024;off+=PAGE_SIZE)
        map_page(pml4,kv+off,kp+off,PTE_PRESENT|PTE_RW);

    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_phys));
    draw_string("SUCCESS: Custom Paging Active!", 10, cur_y, 0x00FF00, fb);
    cur_y += 14;

    /* first shell prompt, properly aligned to cur_y */
    draw_string("root@myos:~$ ", 10, cur_y, 0x8AE234, fb);
    cur_x = 10 + 13 * 8;
    draw_cursor(cur_x, cur_y, current_text_color, fb);

    // ==================================================
    //  Main shell loop
    // ==================================================
    while (1) {

        /* --- cursor blink --- */
        blink_counter++;
        if (blink_counter >= blink_speed) {
            blink_counter = 0;
            cursor_visible = !cursor_visible;
            draw_cursor(cur_x, cur_y,
                        cursor_visible ? current_text_color : 0x300A24, fb);
        }

        if (!(inb(0x64) & 1)) continue;
        uint8_t scancode = inb(0x60);

        /* --- extended key prefix --- */
        if (scancode == 0xE0) { e0_prefix = 1; continue; }

        if (e0_prefix) {
            e0_prefix = 0;
            if ((scancode==0x48||scancode==0x50) && history_count>0) {

                /* erase current line */
                for (int i=0;i<buffer_index;i++)
                    for (int r=0;r<8;r++)
                        for (int c=0;c<8;c++)
                            draw_pixel(cur_x-(i+1)*8+c,cur_y+r,0x300A24,fb);
                cur_x -= buffer_index*8;

                if (scancode==0x48) { if (history_index>0) history_index--; }
                else { if (history_index<history_count-1) history_index++; else history_index=history_count; }

                if (history_index < history_count) {
                    buffer_index = strlen(cmd_history[history_index]);
                    for (int i=0;i<=buffer_index;i++)
                        input_buffer[i]=cmd_history[history_index][i];
                    draw_string(input_buffer, cur_x, cur_y, current_text_color, fb);
                    cur_x += buffer_index*8;
                } else {
                    buffer_index = 0;
                }
                draw_cursor(cur_x, cur_y, current_text_color, fb);
            }
            continue;
        }

        /* --- shift --- */
        if (scancode==0x2A||scancode==0x36) { shift_pressed=1; continue; }
        if (scancode==0xAA||scancode==0xB6) { shift_pressed=0; continue; }

        if (scancode >= 0x80) continue;

        char c = shift_pressed ? scancode_map_shift[scancode] : scancode_map[scancode];

        draw_cursor(cur_x, cur_y, 0x300A24, fb);
        cursor_visible  = 1;
        blink_counter   = 0;

        /* --- backspace --- */
        if (c == '\b') {
            if (buffer_index > 0) {
                buffer_index--;
                cur_x -= 8;
                for (int i=0;i<8;i++)
                    for (int j=0;j<8;j++)
                        draw_pixel(cur_x+i,cur_y+j,0x300A24,fb);
                draw_cursor(cur_x,cur_y,current_text_color,fb);
            }
            continue;
        }

        /* --- enter --- */
        if (c == '\n') {
            input_buffer[buffer_index] = '\0';

            /* save history */
            if (buffer_index > 0) {
                if (history_count < 10) {
                    for (int i=0;i<=buffer_index;i++)
                        cmd_history[history_count][i]=input_buffer[i];
                    history_count++;
                } else {
                    for (int h=1;h<10;h++)
                        for (int i=0;i<256;i++)
                            cmd_history[h-1][i]=cmd_history[h][i];
                    for (int i=0;i<=buffer_index;i++)
                        cmd_history[9][i]=input_buffer[i];
                }
                history_index = history_count;
            }

            cur_y += 12;
            cur_x  = 10;

            // ======================================================
            // ======================================================
            //  Expand $variables before dispatch — works in ALL commands.
            //  e.g.  set x 10  then  calc $x + 5  →  calc 10 + 5
            // ======================================================
            {
                char _exp[256];
                var_expand(input_buffer, _exp, sizeof(_exp));
                int _i = 0;
                while (_exp[_i]) { input_buffer[_i] = _exp[_i]; _i++; }
                input_buffer[_i] = '\0';
                buffer_index = _i;
            }

            //  COMMAND DISPATCH
            // ======================================================

            /* ---- help ---- */
            if (strcmp(input_buffer, "help") == 0) {
                draw_string("Commands: help about clear echo calc time color", cur_x, cur_y, 0xFFFFFF, fb); cur_y+=12;
                draw_string("          set get unset vars vartest ask sysinfo shutdown", cur_x, cur_y, 0xFFFFFF, fb); cur_y+=12;
                draw_string("          rect circle hcircle hex hexagon tri heart", cur_x, cur_y, 0xFFFFFF, fb); cur_y+=12;
                draw_string("Tip: $name expands anywhere  e.g. calc $x + $y", cur_x, cur_y, 0xAAAAAA, fb); cur_y+=12;
            }

            /* ---- about ---- */
            else if (strcmp(input_buffer, "about") == 0) {
                draw_string("MyOS v1.2 - now with a variable dictionary!", cur_x, cur_y, 0x34E2E2, fb); cur_y+=12;
            }

            // ==================================================
            //  VARIABLE COMMANDS
            // ==================================================

            /*
             *  set <name> <value>
             *  Allocates a new slot (or updates an existing one)
             *  and stores the value string.
             */
            else if (strncmp(input_buffer, "set ", 4) == 0) {
                int idx = 4;
                char var_name[VAR_KEY_LEN];
                char var_val[VAR_VAL_LEN];

                parse_word(input_buffer, &idx, var_name, VAR_KEY_LEN);

                /* rest of line is the value (may contain spaces) */
                while (input_buffer[idx] == ' ') idx++;
                strncpy_safe(var_val, &input_buffer[idx], VAR_VAL_LEN);

                if (var_name[0] == '\0') {
                    draw_string("Usage: set <name> <value>", cur_x, cur_y, 0xEF2929, fb);
                } else if (var_set(var_name, var_val)) {
                    draw_string("Variable set: ", cur_x, cur_y, 0x8AE234, fb);
                    draw_string(var_name, cur_x + 14*8, cur_y, 0xFFFFFF, fb);
                    draw_string(" = ", cur_x + (14 + strlen(var_name))*8, cur_y, 0xAAAAAA, fb);
                    draw_string(var_val, cur_x + (17 + strlen(var_name))*8, cur_y, 0xFCE94F, fb);
                } else {
                    draw_string("Error: variable table is full (max 32).", cur_x, cur_y, 0xEF2929, fb);
                }
                cur_y += 12;
            }

            /*
             *  get <name>
             *  Looks up and prints the value stored for a variable.
             */
            else if (strncmp(input_buffer, "get ", 4) == 0) {
                int idx = 4;
                char var_name[VAR_KEY_LEN];
                parse_word(input_buffer, &idx, var_name, VAR_KEY_LEN);

                const char *val = var_get(var_name);
                if (val) {
                    draw_string(var_name, cur_x, cur_y, 0xFCE94F, fb);
                    draw_string(" = ", cur_x + strlen(var_name)*8, cur_y, 0xAAAAAA, fb);
                    draw_string(val, cur_x + (strlen(var_name)+3)*8, cur_y, 0xFFFFFF, fb);
                } else {
                    draw_string("Undefined variable: ", cur_x, cur_y, 0xEF2929, fb);
                    draw_string(var_name, cur_x + 20*8, cur_y, 0xFFFFFF, fb);
                }
                cur_y += 12;
            }

            /*
             *  unset <name>
             *  Frees the slot for that variable.
             */
            else if (strncmp(input_buffer, "unset ", 6) == 0) {
                int idx = 6;
                char var_name[VAR_KEY_LEN];
                parse_word(input_buffer, &idx, var_name, VAR_KEY_LEN);

                if (var_delete(var_name)) {
                    draw_string("Unset: ", cur_x, cur_y, 0x8AE234, fb);
                    draw_string(var_name, cur_x + 7*8, cur_y, 0xFFFFFF, fb);
                } else {
                    draw_string("Variable not found: ", cur_x, cur_y, 0xEF2929, fb);
                    draw_string(var_name, cur_x + 20*8, cur_y, 0xFFFFFF, fb);
                }
                cur_y += 12;
            }


            /*
             *  vartest
             *  Runs a self-contained test of the variable dictionary:
             *  alloc, set, get, overwrite, delete, and overflow guard.
             */
            else if (strcmp(input_buffer, "vartest") == 0) {
                int pass = 0, fail = 0;

                draw_string("--- Variable Dictionary Test ---", cur_x, cur_y, 0x34E2E2, fb); cur_y+=12;

                /* TEST 1: set and get */
                var_set("os", "MyOS");
                const char *v = var_get("os");
                if (v && strcmp(v, "MyOS")==0) {
                    draw_string("[PASS] set/get: os = MyOS", cur_x, cur_y, 0x8AE234, fb); pass++;
                } else {
                    draw_string("[FAIL] set/get: os", cur_x, cur_y, 0xEF2929, fb); fail++;
                }
                cur_y+=12;

                /* TEST 2: set a second variable */
                var_set("version", "1.2");
                v = var_get("version");
                if (v && strcmp(v, "1.2")==0) {
                    draw_string("[PASS] set/get: version = 1.2", cur_x, cur_y, 0x8AE234, fb); pass++;
                } else {
                    draw_string("[FAIL] set/get: version", cur_x, cur_y, 0xEF2929, fb); fail++;
                }
                cur_y+=12;

                /* TEST 3: overwrite existing key */
                var_set("os", "MyOS-v2");
                v = var_get("os");
                if (v && strcmp(v, "MyOS-v2")==0) {
                    draw_string("[PASS] overwrite: os = MyOS-v2", cur_x, cur_y, 0x8AE234, fb); pass++;
                } else {
                    draw_string("[FAIL] overwrite: os", cur_x, cur_y, 0xEF2929, fb); fail++;
                }
                cur_y+=12;

                /* TEST 4: get a variable that does not exist */
                v = var_get("ghost");
                if (v == NULL) {
                    draw_string("[PASS] get missing key returns NULL", cur_x, cur_y, 0x8AE234, fb); pass++;
                } else {
                    draw_string("[FAIL] get missing key returned non-NULL", cur_x, cur_y, 0xEF2929, fb); fail++;
                }
                cur_y+=12;

                /* TEST 5: delete a key, then confirm it is gone */
                var_set("temp", "delete_me");
                var_delete("temp");
                v = var_get("temp");
                if (v == NULL) {
                    draw_string("[PASS] delete: temp is gone", cur_x, cur_y, 0x8AE234, fb); pass++;
                } else {
                    draw_string("[FAIL] delete: temp still exists", cur_x, cur_y, 0xEF2929, fb); fail++;
                }
                cur_y+=12;

                /* TEST 6: fill remaining slots and confirm overflow guard */
                /* first clean up test vars so we have known capacity */
                var_delete("os");
                var_delete("version");

                int filled = 0;
                char key_buf[VAR_KEY_LEN];
                for (int t = 0; t < VAR_MAX_ENTRIES; t++) {
                    key_buf[0]='t'; key_buf[1]='_';
                    key_buf[2]='0'+(t/10); key_buf[3]='0'+(t%10); key_buf[4]='\0';
                    if (var_set(key_buf, "x")) filled++;
                }
                /* one more must fail */
                int overflow_ok = (var_set("overflow_key", "y") == 0);
                if (overflow_ok) {
                    draw_string("[PASS] overflow guard: table-full returns 0", cur_x, cur_y, 0x8AE234, fb); pass++;
                } else {
                    draw_string("[FAIL] overflow guard: accepted beyond capacity", cur_x, cur_y, 0xEF2929, fb); fail++;
                }
                cur_y+=12;

                /* clean up all test slots */
                for (int t = 0; t < VAR_MAX_ENTRIES; t++) {
                    key_buf[0]='t'; key_buf[1]='_';
                    key_buf[2]='0'+(t/10); key_buf[3]='0'+(t%10); key_buf[4]='\0';
                    var_delete(key_buf);
                }

                /* summary */
                char p_str[8], f_str[8];
                itoa(pass, p_str); itoa(fail, f_str);
                draw_string("Result: ", cur_x, cur_y, 0xFFFFFF, fb);
                draw_string(p_str, cur_x+8*8, cur_y, 0x8AE234, fb);
                draw_string(" passed  ", cur_x+9*8, cur_y, 0xFFFFFF, fb);
                draw_string(f_str, cur_x+18*8, cur_y, fail?0xEF2929:0x8AE234, fb);
                draw_string(" failed", cur_x+19*8, cur_y, 0xFFFFFF, fb);
                cur_y+=12;
            }
            /*
             *  vars
             *  Dumps every entry in the dictionary.
             */
            else if (strcmp(input_buffer, "vars") == 0) {
                int found = 0;
                draw_string("--- Variable Dictionary ---", cur_x, cur_y, 0x34E2E2, fb);
                cur_y += 12;
                for (int i = 0; i < VAR_MAX_ENTRIES; i++) {
                    if (!var_table[i].used) continue;
                    found = 1;

                    /* draw index */
                    char idx_str[8];
                    itoa(i, idx_str);
                    draw_string(idx_str,      cur_x,          cur_y, 0xAAAAAA, fb);
                    draw_string(": ",         cur_x + 3*8,    cur_y, 0xAAAAAA, fb);
                    draw_string(var_table[i].key,   cur_x + 5*8,    cur_y, 0xFCE94F, fb);
                    draw_string(" = ",        cur_x + (5 + strlen(var_table[i].key))*8, cur_y, 0xAAAAAA, fb);
                    draw_string(var_table[i].value, cur_x + (8 + strlen(var_table[i].key))*8, cur_y, 0xFFFFFF, fb);
                    cur_y += 12;

                    if (cur_y > (int)(fb->height - 24)) { scroll_screen(fb); cur_y -= 12; }
                }
                if (!found) {
                    draw_string("(no variables set)", cur_x, cur_y, 0xAAAAAA, fb);
                    cur_y += 12;
                }
            }

            /* ---- echo ---- */
            /* $vars already expanded globally above, just print the rest */
            else if (strncmp(input_buffer, "echo ", 5) == 0) {
                draw_string(input_buffer + 5, cur_x, cur_y, current_text_color, fb);
                cur_y += 12;
            }

            /* ---- sysinfo ---- */
            else if (strcmp(input_buffer, "sysinfo") == 0) {
                char vendor[13];
                get_cpuid_string(vendor);
                draw_string("--- System Information ---", cur_x, cur_y, 0x34E2E2, fb); cur_y+=12;
                draw_string("CPU Vendor : ", cur_x, cur_y, 0xFCE94F, fb);
                draw_string(vendor, cur_x+13*8, cur_y, current_text_color, fb); cur_y+=12;
                draw_string("Resolution : ", cur_x, cur_y, 0xFCE94F, fb);
                char ws[10], hs[10];
                itoa(fb->width,ws); itoa(fb->height,hs);
                int tx=cur_x+13*8;
                draw_string(ws,tx,cur_y,current_text_color,fb); tx+=strlen(ws)*8;
                draw_string(" x ",tx,cur_y,current_text_color,fb); tx+=3*8;
                draw_string(hs,tx,cur_y,current_text_color,fb); cur_y+=12;
            }

            /* ---- clear ---- */
            else if (strcmp(input_buffer, "clear") == 0) {
                clear_screen(fb);
                cur_y = 10;
            }

            /* ---- calc ---- */
            else if (strncmp(input_buffer, "calc ", 5) == 0) {
                int i=5, n1=0, n2=0; char op=0;
                while (input_buffer[i]>='0'&&input_buffer[i]<='9') { n1=n1*10+(input_buffer[i]-'0'); i++; }
                while (input_buffer[i]==' ') i++;
                op=input_buffer[i++];
                while (input_buffer[i]==' ') i++;
                while (input_buffer[i]>='0'&&input_buffer[i]<='9') { n2=n2*10+(input_buffer[i]-'0'); i++; }
                int res=0, valid=1;
                if      (op=='+') res=n1+n2;
                else if (op=='-') res=n1-n2;
                else if (op=='*') res=n1*n2;
                else if (op=='/') { if(n2==0) valid=0; else res=n1/n2; }
                else valid=0;
                if (valid) { char rs[32]; itoa(res,rs); draw_string(rs,cur_x,cur_y,0x00FFFF,fb); }
                else draw_string("Error. Usage: calc 5 + 10", cur_x, cur_y, 0xEF2929, fb);
                cur_y+=12;
            }

            /* ---- time ---- */
            else if (strcmp(input_buffer, "time") == 0) {
                uint8_t sec=read_rtc(0x00),min=read_rtc(0x02),hour=read_rtc(0x04);
                sec =(sec &0x0F)+((sec /16)*10);
                min =(min &0x0F)+((min /16)*10);
                hour=(hour&0x0F)+((hour/16)*10);
                min+=30; if(min>=60){min-=60;hour++;}
                hour+=5; if(hour>=24)hour-=24;
                char ts[]="Current IST Time: 00:00:00";
                ts[18]=(hour/10)+'0'; ts[19]=(hour%10)+'0';
                ts[21]=(min/10)+'0';  ts[22]=(min%10)+'0';
                ts[24]=(sec/10)+'0';  ts[25]=(sec%10)+'0';
                draw_string(ts,cur_x,cur_y,0xFCE94F,fb); cur_y+=12;
            }

            /* ---- color ---- */
            else if (strncmp(input_buffer, "color ", 6) == 0) {
                int i=6;
                while(input_buffer[i]==' ')i++;
                char *ca=&input_buffer[i];
                if      (strcmp(ca,"red")    ==0) current_text_color=0xFF0000;
                else if (strcmp(ca,"green")  ==0) current_text_color=0x00FF00;
                else if (strcmp(ca,"blue")   ==0) current_text_color=0x0000FF;
                else if (strcmp(ca,"yellow") ==0) current_text_color=0xFFFF00;
                else if (strcmp(ca,"cyan")   ==0) current_text_color=0x00FFFF;
                else if (strcmp(ca,"magenta")==0) current_text_color=0xFF00FF;
                else if (strcmp(ca,"white")  ==0) current_text_color=0xFFFFFF;
                else {
                    if(ca[0]=='0'&&(ca[1]=='x'||ca[1]=='X'))ca+=2;
                    current_text_color=hex2int(ca);
                }
                draw_string("Terminal color updated!", cur_x, cur_y, current_text_color, fb); cur_y+=12;
            }

            /* ---- ask ---- */
            else if (strcmp(input_buffer, "ask") == 0) {
                char ans = ask_yn("Are you having fun building this OS? (y/n): ",
                                  &cur_x, &cur_y, current_text_color, fb);
                cur_y+=12; cur_x=10;
                if(ans=='y'||ans=='Y')
                    draw_string("Awesome! Keep going!", cur_x, cur_y, 0x8AE234, fb);
                else
                    draw_string("Hang in there – OS dev is tough but rewarding.", cur_x, cur_y, 0xEF2929, fb);
                cur_y+=12;
            }

            /* ---- shutdown ---- */
            else if (strcmp(input_buffer, "shutdown") == 0) {
                draw_string("System going down for halt NOW!", cur_x, cur_y, 0xEF2929, fb); cur_y+=12;
                outw(0x604, 0x2000);
                outw(0xB004, 0x2000);
                draw_string("It is now safe to turn off your computer.", cur_x, cur_y, 0xFFFFFF, fb);
                for(;;) __asm__("cli; hlt");
            }

            /* ---- drawing commands ---- */
            else if (strncmp(input_buffer,"rect ",5)==0) {
                int i=5; int p[4]={0};
                for(int k=0;k<4;k++){while(input_buffer[i]==' ')i++;while(input_buffer[i]>='0'&&input_buffer[i]<='9'){p[k]=p[k]*10+(input_buffer[i]-'0');i++;}}
                while(input_buffer[i]==' ')i++;
                uint32_t col=0xFFFFFF;
                if(input_buffer[i]){if(input_buffer[i]=='0'&&(input_buffer[i+1]=='x'||input_buffer[i+1]=='X'))i+=2; col=hex2int(&input_buffer[i]);}
                draw_rect(p[0],p[1],p[2],p[3],col,fb);
                draw_string("Rectangle drawn!", cur_x, cur_y, current_text_color, fb); cur_y+=12;
            }
            else if (strncmp(input_buffer,"circle ",7)==0) {
                int i=7; int p[3]={0};
                for(int k=0;k<3;k++){while(input_buffer[i]==' ')i++;while(input_buffer[i]>='0'&&input_buffer[i]<='9'){p[k]=p[k]*10+(input_buffer[i]-'0');i++;}}
                while(input_buffer[i]==' ')i++;
                uint32_t col=0xFFFFFF;
                if(input_buffer[i]){if(input_buffer[i]=='0'&&(input_buffer[i+1]=='x'||input_buffer[i+1]=='X'))i+=2; col=hex2int(&input_buffer[i]);}
                draw_circle(p[0],p[1],p[2],col,fb);
                draw_string("Circle drawn!", cur_x, cur_y, current_text_color, fb); cur_y+=12;
            }
            else if (strncmp(input_buffer,"hcircle ",8)==0) {
                int i=8; int p[3]={0};
                for(int k=0;k<3;k++){while(input_buffer[i]==' ')i++;while(input_buffer[i]>='0'&&input_buffer[i]<='9'){p[k]=p[k]*10+(input_buffer[i]-'0');i++;}}
                while(input_buffer[i]==' ')i++;
                uint32_t col=0xFFFFFF;
                if(input_buffer[i]){if(input_buffer[i]=='0'&&(input_buffer[i+1]=='x'||input_buffer[i+1]=='X'))i+=2; col=hex2int(&input_buffer[i]);}
                draw_hollow_circle(p[0],p[1],p[2],col,fb);
                draw_string("Hollow circle drawn!", cur_x, cur_y, current_text_color, fb); cur_y+=12;
            }
            else if (strncmp(input_buffer,"hex ",4)==0) {
                int i=4; int p[3]={0};
                for(int k=0;k<3;k++){while(input_buffer[i]==' ')i++;while(input_buffer[i]>='0'&&input_buffer[i]<='9'){p[k]=p[k]*10+(input_buffer[i]-'0');i++;}}
                while(input_buffer[i]==' ')i++;
                uint32_t col=0xFFFFFF;
                if(input_buffer[i]){if(input_buffer[i]=='0'&&(input_buffer[i+1]=='x'||input_buffer[i+1]=='X'))i+=2; col=hex2int(&input_buffer[i]);}
                draw_hexagon(p[0],p[1],p[2],col,fb);
                draw_string("Hexagon drawn!", cur_x, cur_y, current_text_color, fb); cur_y+=12;
            }
            else if (strncmp(input_buffer,"tri ",4)==0) {
                int i=4; int p[6]={0};
                for(int k=0;k<6;k++){while(input_buffer[i]==' ')i++;while(input_buffer[i]>='0'&&input_buffer[i]<='9'){p[k]=p[k]*10+(input_buffer[i]-'0');i++;}}
                while(input_buffer[i]==' ')i++;
                uint32_t col=0xFFFFFF;
                if(input_buffer[i]){if(input_buffer[i]=='0'&&(input_buffer[i+1]=='x'||input_buffer[i+1]=='X'))i+=2; col=hex2int(&input_buffer[i]);}
                draw_triangle(p[0],p[1],p[2],p[3],p[4],p[5],col,fb);
                draw_string("Triangle drawn!", cur_x, cur_y, current_text_color, fb); cur_y+=12;
            }
            else if (strncmp(input_buffer,"heart ",6)==0) {
                int i=6; int p[3]={0};
                for(int k=0;k<3;k++){while(input_buffer[i]==' ')i++;while(input_buffer[i]>='0'&&input_buffer[i]<='9'){p[k]=p[k]*10+(input_buffer[i]-'0');i++;}}
                while(input_buffer[i]==' ')i++;
                uint32_t col=0xFF0044;
                if(input_buffer[i]){if(input_buffer[i]=='0'&&(input_buffer[i+1]=='x'||input_buffer[i+1]=='X'))i+=2; col=hex2int(&input_buffer[i]);}
                draw_heart(p[0],p[1],p[2],col,fb);
                draw_string("Heart drawn!", cur_x, cur_y, current_text_color, fb); cur_y+=12;
            }

            /* ---- Easter eggs ---- */
            else if (strcmp(input_buffer,"Tushar")==0||strcmp(input_buffer,"tushar")==0) {
                draw_string("Hello Godfather, I,kernel am highly obliged.", cur_x, cur_y, current_text_color, fb); cur_y+=12;
                draw_string("Thanks for making me come to existence.", cur_x, cur_y, current_text_color, fb); cur_y+=12;
                draw_string("Hoping for your day to be good. Lets work.", cur_x, cur_y, current_text_color, fb); cur_y+=12;
            }
            else if (strcmp(input_buffer,"Sarah")==0||strcmp(input_buffer,"sarah")==0) {
                draw_string("Hello Mentor! Good to see you here.", cur_x, cur_y, current_text_color, fb); cur_y+=12;
                char ans=ask_yn("Did you like our project? (y/n): ",&cur_x,&cur_y,current_text_color,fb);
                cur_y+=12; cur_x=10;
                if(ans=='y'||ans=='Y') draw_string("We are glad you liked it!", cur_x, cur_y, 0x8AE234, fb);
                else draw_string("We'll work harder, sorry!", cur_x, cur_y, 0xEF2929, fb);
                cur_y+=12;
            }
            else if (strcmp(input_buffer,"Lahari")==0||strcmp(input_buffer,"lahari")==0) {
                draw_string("Hello Bhabhi ji ", cur_x, cur_y, current_text_color, fb); cur_y+=12;
                draw_string("Accept Aayushman's proposal first. Then you are allowed to work. ", cur_x, cur_y, current_text_color, fb); cur_y+=12;
                char ans=ask_yn("Will you accept ?(y/n): ",&cur_x,&cur_y,current_text_color,fb);
                cur_y+=12; cur_x=10;
                if(ans=='y'||ans=='Y'){
                    draw_string("Congratulations Aayushman you found yourself a girlfriend.", cur_x, cur_y, 0x8AE234, fb); cur_y+=12;
                } else {
                    draw_string("Think again I will get closed! You wont be able to do your work.", cur_x, cur_y, 0xEF2929, fb); cur_y+=12;
                    char ans2=ask_yn("Will you accept ?(y/n): ",&cur_x,&cur_y,current_text_color,fb);
                    cur_y+=12; cur_x=10;
                    if(ans2=='y'||ans2=='Y'){
                        draw_string("Congratulations Aayushman you found yourself a girlfriend.", cur_x, cur_y, 0x8AE234, fb); cur_y+=12;
                    } else {
                        outw(0x604, 0x2000);
                        outw(0xB004, 0x2000);
                        cur_y+=12;
                    }
                }
            }
            else if (strcmp(input_buffer,"Swasti")==0||strcmp(input_buffer,"swasti")==0) {
                draw_string("Hello Bhabhi ji ", cur_x, cur_y, current_text_color, fb); cur_y+=12;
                draw_string("Accept Lakshit's proposal first. Then you are allowed to work. ", cur_x, cur_y, current_text_color, fb); cur_y+=12;
                char ans=ask_yn("Will you accept ?(y/n): ",&cur_x,&cur_y,current_text_color,fb);
                cur_y+=12; cur_x=10;
                if(ans=='y'||ans=='Y'){
                    draw_string("Congratulations Lakshit you found yourself a girlfriend.", cur_x, cur_y, 0x8AE234, fb); cur_y+=12;
                } else {
                    draw_string("Think again I will get closed! You wont be able to do your work.", cur_x, cur_y, 0xEF2929, fb); cur_y+=12;
                    char ans2=ask_yn("Will you accept ?(y/n): ",&cur_x,&cur_y,current_text_color,fb);
                    cur_y+=12; cur_x=10;
                    if(ans2=='y'||ans2=='Y'){
                        draw_string("Congratulations Lakshit you found yourself a girlfriend.", cur_x, cur_y, 0x8AE234, fb); cur_y+=12;
                    } else {
                        outw(0x604, 0x2000);
                        outw(0xB004, 0x2000);
                        cur_y+=12;
                    }
                }
            }
            else if (strcmp(input_buffer,"Chanpa")==0||strcmp(input_buffer,"chanpa")==0) {
                draw_string("Hello Bhabhi ji ", cur_x, cur_y, current_text_color, fb); cur_y+=12;
                draw_string("Accept Mohit's proposal first. Then you are allowed to work. ", cur_x, cur_y, current_text_color, fb); cur_y+=12;
                char ans=ask_yn("Will you accept ?(y/n): ",&cur_x,&cur_y,current_text_color,fb);
                cur_y+=12; cur_x=10;
                if(ans=='y'||ans=='Y'){
                    draw_string("Congratulations Mohit you found yourself a girlfriend.", cur_x, cur_y, 0x8AE234, fb); cur_y+=12;
                } else {
                    draw_string("Think again I will get closed! You wont be able to do your work.", cur_x, cur_y, 0xEF2929, fb); cur_y+=12;
                    char ans2=ask_yn("Will you accept ?(y/n): ",&cur_x,&cur_y,current_text_color,fb);
                    cur_y+=12; cur_x=10;
                    if(ans2=='y'||ans2=='Y'){
                        draw_string("Congratulations Mohit you found yourself a girlfriend.", cur_x, cur_y, 0x8AE234, fb); cur_y+=12;
                    } else {
                        outw(0x604, 0x2000);
                        outw(0xB004, 0x2000);
                        cur_y+=12;
                    }
                }
            }
            else if (strcmp(input_buffer,"Aayushman")==0||strcmp(input_buffer,"aayushman")==0||
                     strcmp(input_buffer,"Lakshit")==0||strcmp(input_buffer,"lakshit")==0||
                     strcmp(input_buffer,"Himanshu")==0||strcmp(input_buffer,"himanshu")==0||
                     strcmp(input_buffer,"Mohit Panwar")==0||strcmp(input_buffer,"mohit panwar")==0||
                     strcmp(input_buffer,"Vaman")==0||strcmp(input_buffer,"vaman")==0) {
                draw_string("Hello project member. Lets work --", cur_x, cur_y, current_text_color, fb); cur_y+=12;
            }

            /* ---- unknown ---- */
            else if (buffer_index > 0) {
                draw_string("Command not found. Type 'help' for a list.", cur_x, cur_y, 0xEF2929, fb); cur_y+=12;
            }

            /* ---- scroll if near bottom ---- */
            if (cur_y > (int)(fb->height - 24)) { scroll_screen(fb); cur_y -= 12; }

            buffer_index = 0;
            draw_string("root@myos:~$ ", 10, cur_y, 0x8AE234, fb);
            cur_x = 10 + 13*8;
            draw_cursor(cur_x, cur_y, current_text_color, fb);
            continue;
        }

        /* --- printable character --- */
        if (c > 0 && buffer_index < 255) {
            input_buffer[buffer_index++] = c;
            draw_char(c, cur_x, cur_y, current_text_color, fb);
            cur_x += 8;
            draw_cursor(cur_x, cur_y, current_text_color, fb);
        }
    }
}