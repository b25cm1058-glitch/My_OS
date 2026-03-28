#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include "font.h"

struct limine_framebuffer *global_fb = NULL;

// --- Function Prototypes ---
void clear_screen(struct limine_framebuffer *fb);
void draw_string(const char *str, int x, int y, uint32_t color, struct limine_framebuffer *fb);
void ptr_to_hex(uint64_t ptr, char *buffer);

// Page Table Entry Flags
#define PTE_PRESENT 0x01 // The page is actually in memory
#define PTE_RW 0x02      // Read/Write permission
#define PTE_USER 0x04    // User-mode programs can access this

// Macros to extract the 9-bit index for each of the 4 table levels from a virtual address
#define PML4_GET_INDEX(addr) (((uint64_t)(addr) >> 39) & 0x1FF)
#define PDPT_GET_INDEX(addr) (((uint64_t)(addr) >> 30) & 0x1FF)
#define PD_GET_INDEX(addr) (((uint64_t)(addr) >> 21) & 0x1FF)
#define PT_GET_INDEX(addr) (((uint64_t)(addr) >> 12) & 0x1FF)

// --- NEW: IDT Structures ---
struct idt_entry
{
    uint16_t isr_low;   // Lower 16 bits of the handler function address
    uint16_t kernel_cs; // The Code Segment selector
    uint8_t ist;        // Interrupt Stack Table offset (set to 0)
    uint8_t attributes; // Type and attributes (e.g., is it present?)
    uint16_t isr_mid;   // Middle 16 bits of the handler address
    uint32_t isr_high;  // Highest 32 bits of the handler address
    uint32_t reserved;  // Set to 0
} __attribute__((packed));

struct idtr
{
    uint16_t limit; // Size of the IDT - 1
    uint64_t base;  // Memory address of the first IDT entry
} __attribute__((packed));

struct interrupt_frame
{
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

// The actual table: 256 entries
__attribute__((aligned(0x10))) static struct idt_entry idt[256];
static struct idtr idtr;

// Helper to get the current Code Segment that Limine set up for us
static inline uint16_t get_cs(void)
{
    uint16_t cs;
    // Register the Divide by Zero handler (Vector 0)
    // 0x8E means: Present (1), Privilege Ring 0 (00), Interrupt Gate (1110)
    __asm__ volatile("mov %%cs, %0" : "=r"(cs));
    return cs;
}
static inline uint64_t read_cr2(void) {
    uint64_t cr2_val;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2_val));
    return cr2_val;
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

__attribute__((interrupt)) void exception_div_zero(struct interrupt_frame *frame)
{
    // Note: To print text here, you would need to make your 'fb' pointer global.
    // For now, we will just halt the CPU safely to prevent a reboot loop.
    (void)frame;
    // Send a character to QEMU's serial console (port 0x3F8) just to prove it worked
    outb(0x3F8, 'E'); // Error
    outb(0x3F8, 'R');
    outb(0x3F8, 'R');

    for (;;)
    {
        __asm__ volatile("cli; hlt"); // Stop the system safely
    }
}

__attribute__((interrupt)) void exception_page_fault(struct interrupt_frame *frame, uint64_t error_code) {
    (void)frame;
    (void)error_code; // We won't decode the exact error bits right now, but we must accept the parameter

    uint64_t faulting_address = read_cr2();
    
    // If the framebuffer is ready, print the Blue Screen of Death!
    if (global_fb != NULL) {
        clear_screen(global_fb);
        
        draw_string("KERNEL PANIC: PAGE FAULT!", 10, 10, 0xFF0000, global_fb);
        draw_string("The CPU tried to access unmapped memory.", 10, 30, 0xFFFFFF, global_fb);
        
        draw_string("Faulting Address: ", 10, 50, 0xFFFFFF, global_fb);
        
        // Convert the CR2 address to hex and print it
        char addr_str[20];
        ptr_to_hex(faulting_address, addr_str);
        draw_string(addr_str, 10 + (18 * 8), 50, 0x00FF00, global_fb);
        
        draw_string("System Halted.", 10, 70, 0xFF0000, global_fb);
    }

    // Stop the CPU forever
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

void idt_set_descriptor(uint8_t vector, void *isr, uint8_t flags)
{
    uint64_t descriptor = (uint64_t)isr;

    idt[vector].isr_low = (uint16_t)(descriptor & 0xFFFF);
    idt[vector].kernel_cs = get_cs(); // Grab the active 64-bit code segment
    idt[vector].ist = 0;
    idt[vector].attributes = flags;
    idt[vector].isr_mid = (uint16_t)((descriptor >> 16) & 0xFFFF);
    idt[vector].isr_high = (uint32_t)((descriptor >> 32) & 0xFFFFFFFF);
    idt[vector].reserved = 0;
}

// Function to load the IDT into the CPU
void idt_init(void)
{
    idtr.base = (uint64_t)&idt[0];
    idtr.limit = (uint16_t)sizeof(struct idt_entry) * 256 - 1;

    idt_set_descriptor(0, exception_div_zero, 0x8E);

    idt_set_descriptor(14, exception_page_fault, 0x8E);
    // Load the IDT into the processor
    __asm__ volatile("lidt %0" : : "m"(idtr));

    // Turn on interrupts!
    __asm__ volatile("sti");
}

// --- NEW: Exception Handlers ---
// The CPU will automatically jump here if we divide by zero!

// --- Limine Request ---
__attribute__((used, section(".requests"))) static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0};

__attribute__((used, section(".requests"))) static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0};

__attribute__((used, section(".requests"))) static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0};
__attribute__((used, section(".requests"))) static volatile struct limine_kernel_address_request kernel_address_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0};

    
// --- Port I/O Helpers ---

static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

uint8_t read_rtc(uint8_t reg)
{
    outb(0x70, reg);
    return inb(0x71);
}

// --- Scancode Maps ---
char scancode_map[] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '};

char scancode_map_shift[] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '};

// --- String & Math Helpers ---
// NEW: strlen helps us find the length of a string!

// --- String & Math Helpers ---

// ADD IT HERE: Fills a block of memory with a specific value (usually 0)
void *memset(void *s, int c, size_t n)
{
    unsigned char *p = s;
    while (n--)
    {
        *p++ = (unsigned char)c;
    }
    return s;
}

size_t strlen(const char *str)
{
    size_t len = 0;
    while (str[len])
        len++;
    return len;
}
// --- Virtual Memory Helpers ---
// Converts a Physical Address into a safe Virtual Address we can write to in C
static inline uint64_t phys_to_virt(uint64_t physical_addr)
{
    return physical_addr + hhdm_request.response->offset;
}

// Talks to the CPU and gets the 12-character vendor string
void get_cpuid_string(char *str)
{
    uint32_t ebx, ecx, edx;

    // Call the 'cpuid' assembly instruction with eax = 0
    __asm__ volatile(
        "cpuid"
        : "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0));

    // Stitch the 12 characters together from the registers into our string
    str[0] = (ebx >> 0) & 0xFF;
    str[1] = (ebx >> 8) & 0xFF;
    str[2] = (ebx >> 16) & 0xFF;
    str[3] = (ebx >> 24) & 0xFF;

    str[4] = (edx >> 0) & 0xFF;
    str[5] = (edx >> 8) & 0xFF;
    str[6] = (edx >> 16) & 0xFF;
    str[7] = (edx >> 24) & 0xFF;

    str[8] = (ecx >> 0) & 0xFF;
    str[9] = (ecx >> 8) & 0xFF;
    str[10] = (ecx >> 16) & 0xFF;
    str[11] = (ecx >> 24) & 0xFF;

    str[12] = '\0'; // Null-terminate the string so C knows where it ends
}

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    while (n && *s1 && (*s1 == *s2))
    {
        ++s1;
        ++s2;
        --n;
    }
    if (n == 0)
        return 0;
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

void itoa(int n, char str[])
{
    int i = 0, sign = n;
    if (n < 0)
        n = -n;
    do
    {
        str[i++] = n % 10 + '0';
    } while ((n /= 10) > 0);
    if (sign < 0)
        str[i++] = '-';
    str[i] = '\0';
    int j = 0, k = i - 1;
    while (j < k)
    {
        char temp = str[j];
        str[j] = str[k];
        str[k] = temp;
        j++;
        k--;
    }
}

uint32_t hex2int(const char *hex)
{
    uint32_t val = 0;
    while (*hex)
    {
        uint8_t byte = *hex++;
        if (byte >= '0' && byte <= '9')
            byte = byte - '0';
        else if (byte >= 'a' && byte <= 'f')
            byte = byte - 'a' + 10;
        else if (byte >= 'A' && byte <= 'F')
            byte = byte - 'A' + 10;
        else
            break;
        val = (val << 4) | (byte & 0xF);
    }
    return val;
}

void ptr_to_hex(uint64_t ptr, char *buffer)
{
    const char *hex_chars = "0123456789ABCDEF";
    buffer[0] = '0';
    buffer[1] = 'x';
    // 64-bit pointers are 16 hex characters long
    for (int i = 0; i < 16; i++)
    {
        buffer[17 - i] = hex_chars[(ptr >> (i * 4)) & 0xF];
    }
    buffer[18] = '\0'; // Null terminate
}

// --- Drawing Functions ---
void draw_pixel(int x, int y, uint32_t color, struct limine_framebuffer *fb)
{
    uint32_t *fb_ptr = fb->address;
    fb_ptr[y * (fb->pitch / 4) + x] = color;
}
void draw_rect(int start_x, int start_y, int width, int height, uint32_t color, struct limine_framebuffer *fb)
{
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            int current_x = start_x + x;
            int current_y = start_y + y;

            // CRITICAL: Prevent the OS from crashing if we draw off-screen!
            if (current_x >= 0 && current_x < (int)fb->width &&
                current_y >= 0 && current_y < (int)fb->height)
            {

                draw_pixel(current_x, current_y, color, fb);
            }
        }
    }
}
void draw_circle(int center_x, int center_y, int radius, uint32_t color, struct limine_framebuffer *fb)
{
    // We create a square "bounding box" around the center point
    for (int y = -radius; y <= radius; y++)
    {
        for (int x = -radius; x <= radius; x++)
        {

            // If the pixel is inside the radius, draw it! (x^2 + y^2 <= radius^2)
            if (x * x + y * y <= radius * radius)
            {
                int current_x = center_x + x;
                int current_y = center_y + y;

                // CRITICAL: Prevent drawing off-screen
                if (current_x >= 0 && current_x < (int)fb->width &&
                    current_y >= 0 && current_y < (int)fb->height)
                {

                    draw_pixel(current_x, current_y, color, fb);
                }
            }
        }
    }
}
void draw_char(char c, int x, int y, uint32_t color, struct limine_framebuffer *fb)
{
    for (int row = 0; row < 8; row++)
    {
        for (int col = 0; col < 8; col++)
        {
            if ((font8x8_basic[(uint8_t)c][row] >> (7 - col)) & 1)
            {
                draw_pixel(x + col, y + row, color, fb);
            }
        }
    }
}

void draw_string(const char *str, int x, int y, uint32_t color, struct limine_framebuffer *fb)
{
    for (int i = 0; str[i] != '\0'; i++)
    {
        draw_char(str[i], x + (i * 8), y, color, fb);
    }
}

void draw_cursor(int x, int y, uint32_t color, struct limine_framebuffer *fb)
{
    for (int i = 0; i < 8; i++)
    {
        for (int j = 8; j < 10; j++)
        {
            draw_pixel(x + i, y + j, color, fb);
        }
    }
}

void clear_screen(struct limine_framebuffer *fb)
{
    uint32_t *fb_ptr = fb->address;
    for (size_t i = 0; i < fb->width * fb->height; i++)
    {
        fb_ptr[i] = 0x300A24;
    }
}

void scroll_screen(struct limine_framebuffer *fb)
{
    uint32_t *fb_ptr = (uint32_t *)fb->address;
    size_t pixels_per_row = fb->pitch / 4;
    size_t offset = 12 * pixels_per_row;
    size_t total_pixels = fb->height * pixels_per_row;

    for (size_t i = 0; i < total_pixels - offset; i++)
    {
        fb_ptr[i] = fb_ptr[i + offset];
    }
    for (size_t i = total_pixels - offset; i < total_pixels; i++)
    {
        fb_ptr[i] = 0x300A24;
    }
}

#define PAGE_SIZE 4096

// Macros to easily manipulate bits in our array
#define BITMAP_SET(bitmap, bit) (bitmap[(bit) / 8] |= (1 << ((bit) % 8)))
#define BITMAP_CLEAR(bitmap, bit) (bitmap[(bit) / 8] &= ~(1 << ((bit) % 8)))
#define BITMAP_TEST(bitmap, bit) (bitmap[(bit) / 8] & (1 << ((bit) % 8)))

// PMM Globals

uint8_t *pmm_bitmap = NULL;
uint64_t pmm_bitmap_size = 0; // Size of the bitmap in bytes
uint64_t pmm_total_pages = 0; // Total 4KB frames in the system

// Find the first free 4KB frame of RAM, mark it as used, and return its physical address
void *pmm_alloc_page(void)
{
    // Scan every single bit in the bitmap
    for (uint64_t i = 0; i < (pmm_bitmap_size * 8); i++)
    {

        // If the bit is 0, the page is free!
        if (!BITMAP_TEST(pmm_bitmap, i))
        {

            // Mark it as used (1) so nothing else claims it
            BITMAP_SET(pmm_bitmap, i);

            // Calculate the actual physical memory address (Bit Index * 4096)
            return (void *)(i * PAGE_SIZE);
        }
    }

    // If we loop through the entire bitmap and find no 0s, we are out of RAM!
    return NULL;
}

// Give a 4KB frame back to the system so it can be used again
void pmm_free_page(void *ptr)
{
    uint64_t addr = (uint64_t)ptr;

    // Figure out which bit represents this address
    uint64_t bit = addr / PAGE_SIZE;

    // Mark it as free (0)
    BITMAP_CLEAR(pmm_bitmap, bit);
}

// Maps a single 4KB Virtual Page to a 4KB Physical Frame
void map_page(uint64_t *pml4, uint64_t virtual_addr, uint64_t physical_addr, uint64_t flags)
{

    // 1. Get the PML4 index and check if the PDPT exists
    uint64_t pml4_index = PML4_GET_INDEX(virtual_addr);
    if (!(pml4[pml4_index] & PTE_PRESENT))
    {
        // Doesn't exist! Allocate a new frame for the PDPT
        uint64_t new_table_phys = (uint64_t)pmm_alloc_page();
        // Zero it out safely using our HHDM offset
        memset((void *)phys_to_virt(new_table_phys), 0, PAGE_SIZE);
        // Link it into the PML4
        pml4[pml4_index] = new_table_phys | PTE_PRESENT | PTE_RW | PTE_USER;
    }

    // 2. Move down to the PDPT
    // Extract the physical address from the entry (mask out the bottom 12 flag bits)
    uint64_t *pdpt = (uint64_t *)phys_to_virt(pml4[pml4_index] & ~(0xFFF));
    uint64_t pdpt_index = PDPT_GET_INDEX(virtual_addr);

    if (!(pdpt[pdpt_index] & PTE_PRESENT))
    {
        uint64_t new_table_phys = (uint64_t)pmm_alloc_page();
        memset((void *)phys_to_virt(new_table_phys), 0, PAGE_SIZE);
        pdpt[pdpt_index] = new_table_phys | PTE_PRESENT | PTE_RW | PTE_USER;
    }

    // 3. Move down to the Page Directory (PD)
    uint64_t *pd = (uint64_t *)phys_to_virt(pdpt[pdpt_index] & ~(0xFFF));
    uint64_t pd_index = PD_GET_INDEX(virtual_addr);

    if (!(pd[pd_index] & PTE_PRESENT))
    {
        uint64_t new_table_phys = (uint64_t)pmm_alloc_page();
        memset((void *)phys_to_virt(new_table_phys), 0, PAGE_SIZE);
        pd[pd_index] = new_table_phys | PTE_PRESENT | PTE_RW | PTE_USER;
    }

    // 4. Move down to the final Page Table (PT)
    uint64_t *pt = (uint64_t *)phys_to_virt(pd[pd_index] & ~(0xFFF));
    uint64_t pt_index = PT_GET_INDEX(virtual_addr);

    // 5. Finally, map the actual physical frame to this exact virtual address!
    pt[pt_index] = physical_addr | flags;
}
// --- Main Execution ---
void _start(void)
{
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1)
    {
        for (;;)
        {
            __asm__("hlt");
        }
    }

    // Make sure we actually got the memory map from the bootloader

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    global_fb = fb;
    idt_init(); // Boot up the IDT!

    clear_screen(fb);
    draw_string("Welcome to MyOS Shell!", 10, 10, 0xFFFFFF, fb);

    int cur_x = 10;
    int cur_y = 30;

    char input_buffer[256];
    int buffer_index = 0;

    uint32_t current_text_color = 0xFFFFFF;

    draw_string("root@myos:~$ ", cur_x, cur_y + 48, 0x8AE234, fb);
    cur_x += 13 * 8;

    draw_cursor(cur_x, cur_y, current_text_color, fb);

    int shift_pressed = 0;

    // --- NEW: Command History Variables ---
    char cmd_history[10][256]; // Stores the last 10 commands
    int history_count = 0;     // How many commands we've saved
    int history_index = 0;     // Where we are when pressing Up/Down
    int e0_prefix = 0;         // Helps us detect Arrow Keys

    uint32_t blink_speed = 3000000;
    uint32_t blink_counter = 0;
    int cursor_visible = 1;
    if (memmap_request.response == NULL)
    {
        draw_string("PANIC: No memory map found!", 10, 10, 0xFF0000, fb);
        for (;;)
            __asm__("hlt");
    }

    struct limine_memmap_response *memmap = memmap_request.response;
    struct limine_memmap_entry **entries = memmap->entries;

    uint64_t total_usable_ram = 0;
    uint64_t highest_ram_address = 0;

    // Loop through the memory map provided by Limine
    for (uint64_t i = 0; i < memmap->entry_count; i++)
    {
        // We only care about RAM that is free for the OS to use
        if (entries[i]->type == LIMINE_MEMMAP_USABLE)
        {
            total_usable_ram += entries[i]->length;

            // Find the absolute highest physical address we have access to
            uint64_t top_of_region = entries[i]->base + entries[i]->length;
            if (top_of_region > highest_ram_address)
            {
                highest_ram_address = top_of_region;
            }
        }
    }

    // --- Initialize the Physical Memory Manager (PMM) ---

    // 1. Calculate how many pages exist, and how big the bitmap needs to be
    pmm_total_pages = highest_ram_address / PAGE_SIZE;
    pmm_bitmap_size = pmm_total_pages / 8;
    if (pmm_total_pages % 8 != 0)
        pmm_bitmap_size++; // Round up if it doesn't divide evenly

    // 2. Find a chunk of usable RAM large enough to hold our bitmap array
    for (uint64_t i = 0; i < memmap->entry_count; i++)
    {
        if (entries[i]->type == LIMINE_MEMMAP_USABLE && entries[i]->length >= pmm_bitmap_size)
        {
            pmm_bitmap = (uint8_t *)entries[i]->base;
            break;
        }
    }

    if (pmm_bitmap == NULL)
    {
        draw_string("PANIC: Not enough RAM for PMM Bitmap!", 10, cur_y, 0xFF0000, fb);
        for (;;)
            __asm__("hlt");
    }

    // 3. By default, mark EVERY page as "used" (1) so we don't accidentally overwrite reserved BIOS memory.
    for (uint64_t i = 0; i < pmm_bitmap_size; i++)
    {
        pmm_bitmap[i] = 0xFF; // 0xFF is 11111111 in binary
    }

    // 4. Now, loop through the memory map again and mark ONLY the usable regions as "free" (0)
    for (uint64_t i = 0; i < memmap->entry_count; i++)
    {
        if (entries[i]->type == LIMINE_MEMMAP_USABLE)
        {
            uint64_t start_page = entries[i]->base / PAGE_SIZE;
            uint64_t pages_in_region = entries[i]->length / PAGE_SIZE;

            for (uint64_t p = 0; p < pages_in_region; p++)
            {
                BITMAP_CLEAR(pmm_bitmap, start_page + p);
            }
        }
    }

    // 5. CRITICAL: Mark the pages that the bitmap ITSELF occupies as "used" so we don't overwrite our own map!
    uint64_t bitmap_start_page = (uint64_t)pmm_bitmap / PAGE_SIZE;
    uint64_t bitmap_pages = pmm_bitmap_size / PAGE_SIZE;
    if (pmm_bitmap_size % PAGE_SIZE != 0)
        bitmap_pages++; // Round up

    for (uint64_t p = 0; p < bitmap_pages; p++)
    {
        BITMAP_SET(pmm_bitmap, bitmap_start_page + p);
    }

    // --- Test the PMM ---
    void *my_first_page = pmm_alloc_page();

    if (my_first_page != NULL)
    {
        char addr_str[20];
        ptr_to_hex((uint64_t)my_first_page, addr_str);

        draw_string("Success! Allocated 4KB frame at:", 10, cur_y, 0x00FF00, fb);
        draw_string(addr_str, 10 + (33 * 8), cur_y, 0xFFFFFF, fb);
        cur_y += 16;

        // Let's print the address (we will need a quick hex-to-string function later,
        // but for now we know it didn't return NULL!)
        draw_string("Memory is ready to use.", 10, cur_y, 0x00FF00, fb);
        cur_y += 16;

        // Give it back to the system
        pmm_free_page(my_first_page);
    }
    else
    {
        draw_string("Uh oh, PMM returned NULL.", 10, cur_y, 0xFF0000, fb);
        cur_y += 16;
    }
    // --- Initialize the Virtual Memory Manager (VMM) ---

    // 1. Ask the PMM for a fresh 4KB frame to act as our root page table (PML4)
    // We convert it to a virtual address so C can write to it without crashing.
    uint64_t pml4_phys = (uint64_t)pmm_alloc_page();
    uint64_t *pml4 = (uint64_t *)phys_to_virt(pml4_phys);
    memset(pml4, 0, PAGE_SIZE); // Fill it with zeros so there's no garbage data!

    // 2. Map the Higher Half Direct Map (HHDM)
    // We loop through the memory map and map EVERY region to its HHDM offset.
    // This ensures our physical RAM, framebuffer, and ACPI tables stay accessible.
    for (uint64_t i = 0; i < memmap->entry_count; i++)
    {
        uint64_t base = memmap->entries[i]->base;
        uint64_t length = memmap->entries[i]->length;

        // Map page-by-page (4KB at a time)
        for (uint64_t offset = 0; offset < length; offset += PAGE_SIZE)
        {
            uint64_t phys = base + offset;
            uint64_t virt = phys_to_virt(phys);
            map_page(pml4, virt, phys, PTE_PRESENT | PTE_RW);
        }
    }

    // 3. Map the Kernel Code and Data
    // We grab the exact physical and virtual base of the kernel from Limine.
    uint64_t kernel_phys = kernel_address_request.response->physical_base;
    uint64_t kernel_virt = kernel_address_request.response->virtual_base;

    // We will map 16MB of space for the kernel, which is more than enough for our OS.
    for (uint64_t offset = 0; offset < 16 * 1024 * 1024; offset += PAGE_SIZE)
    {
        map_page(pml4, kernel_virt + offset, kernel_phys + offset, PTE_PRESENT | PTE_RW);
    }

    // 4. THE GRAND FINALE: Load the new page table into the CPU!
    // The CPU requires the physical address to be loaded into the CR3 register.
    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_phys));

    // If we make it to this line, we survived the switch!
    draw_string("SUCCESS: Custom Paging is Active!", 10, cur_y, 0x00FF00, fb);


    cur_y += 16;

    while (1)
    {
        blink_counter++;
        if (blink_counter >= blink_speed)
        {
            blink_counter = 0;
            cursor_visible = !cursor_visible;

            if (cursor_visible)
            {
                draw_cursor(cur_x, cur_y, current_text_color, fb);
            }
            else
            {
                draw_cursor(cur_x, cur_y, 0x300A24, fb);
            }
        }

        if (inb(0x64) & 1)
        {
            uint8_t scancode = inb(0x60);

            // --- NEW: Arrow Key Interception ---
            if (scancode == 0xE0)
            {
                e0_prefix = 1; // Flag that an arrow key is coming next
                continue;
            }

            if (e0_prefix)
            {
                e0_prefix = 0;
                // 0x48 is Up Arrow, 0x50 is Down Arrow
                if ((scancode == 0x48 || scancode == 0x50) && history_count > 0)
                {

                    // 1. Visually erase whatever the user is currently typing
                    for (int i = 0; i < buffer_index; i++)
                    {
                        for (int r = 0; r < 8; r++)
                            for (int c = 0; c < 8; c++)
                                draw_pixel(cur_x - (i + 1) * 8 + c, cur_y + r, 0x300A24, fb);
                    }
                    cur_x -= buffer_index * 8; // Move cursor back

                    // 2. Change our history index
                    if (scancode == 0x48)
                    { // Up arrow
                        if (history_index > 0)
                            history_index--;
                    }
                    else if (scancode == 0x50)
                    { // Down arrow
                        if (history_index < history_count - 1)
                            history_index++;
                        else
                            history_index = history_count; // Clear line at bottom
                    }

                    // 3. Load the history onto the screen and into the buffer
                    if (history_index < history_count)
                    {
                        buffer_index = strlen(cmd_history[history_index]);
                        for (int i = 0; i < buffer_index; i++)
                            input_buffer[i] = cmd_history[history_index][i];

                        input_buffer[buffer_index] = '\0';
                        draw_string(input_buffer, cur_x, cur_y, current_text_color, fb);
                        cur_x += buffer_index * 8;
                    }
                    else
                    {
                        buffer_index = 0; // User scrolled past the bottom, leave it blank
                    }

                    draw_cursor(cur_x, cur_y, current_text_color, fb);
                }
                continue; // Skip the rest of the normal key processing
            }

            if (scancode == 0x2A || scancode == 0x36)
            {
                shift_pressed = 1;
                continue;
            }
            else if (scancode == 0xAA || scancode == 0xB6)
            {
                shift_pressed = 0;
                continue;
            }

            if (scancode < 0x80)
            {
                char c;
                if (shift_pressed)
                    c = scancode_map_shift[scancode];
                else
                    c = scancode_map[scancode];

                draw_cursor(cur_x, cur_y, 0x300A24, fb);
                cursor_visible = 1;
                blink_counter = 0;

                if (c == '\b')
                {
                    if (buffer_index > 0)
                    {
                        buffer_index--;
                        cur_x -= 8;

                        for (int i = 0; i < 8; i++)
                            for (int j = 0; j < 8; j++)
                                draw_pixel(cur_x + i, cur_y + j, 0x300A24, fb);

                        draw_cursor(cur_x, cur_y, current_text_color, fb);
                    }
                }
                else if (c == '\n')
                {
                    input_buffer[buffer_index] = '\0';
                    // --- NEW: Save to Command History ---
                    if (buffer_index > 0)
                    {
                        if (history_count < 10)
                        {
                            // Array isn't full, just add it
                            for (int i = 0; i <= buffer_index; i++)
                                cmd_history[history_count][i] = input_buffer[i];
                            history_count++;
                        }
                        else
                        {
                            // Array is full, shift everything up by 1 to make room at the bottom
                            for (int h = 1; h < 10; h++)
                            {
                                for (int i = 0; i < 256; i++)
                                    cmd_history[h - 1][i] = cmd_history[h][i];
                            }
                            for (int i = 0; i <= buffer_index; i++)
                                cmd_history[9][i] = input_buffer[i];
                        }
                        history_index = history_count; // Reset scrolling position
                    }
                    cur_y += 12;
                    cur_x = 10;

                    // --- COMMAND PROCESSING ---
                    if (strcmp(input_buffer, "help") == 0)
                    {
                        draw_string("Commands: help, about, clear, echo, calc, time, color, ask, shutdown", cur_x, cur_y, 0xFFFFFF, fb);
                        cur_y += 12;
                    }
                    else if (strcmp(input_buffer, "about") == 0)
                    {
                        draw_string("MyOS v1.1 - Built from scratch!", cur_x, cur_y, 0x34E2E2, fb);
                        cur_y += 12;
                    }
                    // --- NEW: INTERACTIVE PROMPT COMMAND ---
                    else if (strcmp(input_buffer, "ask") == 0)
                    {
                        char *question = "Are you having fun building this OS? (y/n): ";
                        draw_string(question, cur_x, cur_y, 0xFCE94F, fb); // Yellow text

                        // Move cursor to the end of the question
                        cur_x += strlen(question) * 8;
                        draw_cursor(cur_x, cur_y, current_text_color, fb);

                        char user_answer = 0;

                        // Mini-loop: Trap the OS here until 'y' or 'n' is pressed
                        while (1)
                        {
                            if (inb(0x64) & 1)
                            {
                                uint8_t answer_scancode = inb(0x60);
                                if (answer_scancode < 0x80)
                                { // Key press
                                    char key = scancode_map[answer_scancode];
                                    if (key == 'y' || key == 'n' || key == 'Y' || key == 'N')
                                    {
                                        user_answer = key;
                                        // Draw the letter they typed
                                        draw_char(user_answer, cur_x, cur_y, current_text_color, fb);
                                        break; // Break out of the mini-loop!
                                    }
                                }
                            }
                        }

                        cur_y += 12;
                        cur_x = 10;

                        // Make a decision based on input!
                        if (user_answer == 'y' || user_answer == 'Y')
                        {
                            draw_string("Awesome! You are doing a great job, keep going!", cur_x, cur_y, 0x8AE234, fb); // Green
                        }
                        else
                        {
                            draw_string("Hang in there! OS dev is hard but very rewarding.", cur_x, cur_y, 0xEF2929, fb); // Red
                        }
                        cur_y += 12;
                    }
                    // --- CREATOR EASTER EGGS ---
                    else if (strcmp(input_buffer, "Tushar") == 0 || strcmp(input_buffer, "tushar") == 0)
                    {
                        draw_string("Hello Godfather, I,kernel am highly obliged that you built me .", cur_x, cur_y, current_text_color, fb);
                        cur_y += 12;
                        draw_string("Thanks for making me come to existence .", cur_x, cur_y, current_text_color, fb);
                        cur_y += 12;
                        draw_string("Hoping for your day to be good. Lets work--.", cur_x, cur_y, current_text_color, fb);
                        cur_y += 12;
                    }
                    else if (strcmp(input_buffer, "Lahari") == 0 || strcmp(input_buffer, "lahari") == 0)
                    {
                        draw_string("Hello Bhabhi ji ", cur_x, cur_y, current_text_color, fb);
                        cur_y += 12;
                        draw_string("Accept Aayushman's proposal first. Then you are allowed to work. ", cur_x, cur_y, current_text_color, fb);
                        cur_y += 12;
                        char *question = "Will you accept ?(y/n)";
                        draw_string(question, cur_x, cur_y, 0xFCE94F, fb); // Yellow text
                        // Move cursor to the end of the question
                        cur_x += strlen(question) * 8;
                        draw_cursor(cur_x, cur_y, current_text_color, fb);
                        char user_answer = 0;
                        // Mini-loop: Trap the OS here until 'y' or 'n' is pressed
                        while (1)
                        {
                            if (inb(0x64) & 1)
                            {
                                uint8_t answer_scancode = inb(0x60);
                                if (answer_scancode < 0x80)
                                { // Key press
                                    char key = scancode_map[answer_scancode];
                                    if (key == 'y' || key == 'n' || key == 'Y' || key == 'N')
                                    {
                                        user_answer = key;
                                        // Draw the letter they typed
                                        draw_char(user_answer, cur_x, cur_y, current_text_color, fb);
                                        break; // Break out of the mini-loop!
                                    }
                                }
                            }
                        }

                        cur_y += 12;
                        cur_x = 10;

                        // Make a decision based on input!
                        if (user_answer == 'y' || user_answer == 'Y')
                        {
                            draw_string("Congratulations Aayushman you found yourself a girlfriend.", cur_x, cur_y, 0x8AE234, fb); // Green
                            cur_y += 12;
                        }
                        else
                        {
                            draw_string("Think again I will get closed! You wont be able to do your work.", cur_x, cur_y, 0xEF2929, fb); // Red
                            cur_y += 12;
                            char *question = "Will you accept ?(y/n)";
                            draw_string(question, cur_x, cur_y, 0xFCE94F, fb); // Yellow text
                            // Move cursor to the end of the question
                            cur_x += strlen(question) * 8;
                            draw_cursor(cur_x, cur_y, current_text_color, fb);
                            char user_answer = 0;
                            // Mini-loop: Trap the OS here until 'y' or 'n' is pressed
                            while (1)
                            {
                                if (inb(0x64) & 1)
                                {
                                    uint8_t answer_scancode = inb(0x60);
                                    if (answer_scancode < 0x80)
                                    { // Key press
                                        char key = scancode_map[answer_scancode];
                                        if (key == 'y' || key == 'n' || key == 'Y' || key == 'N')
                                        {
                                            user_answer = key;
                                            // Draw the letter they typed
                                            draw_char(user_answer, cur_x, cur_y, current_text_color, fb);
                                            break; // Break out of the mini-loop!
                                        }
                                    }
                                }
                            }

                            cur_y += 12;
                            cur_x = 10;
                            // Make a decision based on input!
                            if (user_answer == 'y' || user_answer == 'Y')
                            {
                                draw_string("Congratulations Aayushman you found yourself a girlfriend.", cur_x, cur_y, 0x8AE234, fb); // Green
                            }
                            else
                            {
                                outw(0x604, 0x2000);
                                outw(0xB004, 0x2000);
                            }
                            cur_y += 12;
                        }
                    }
                    else if (strcmp(input_buffer, "Swasti") == 0 || strcmp(input_buffer, "swasti") == 0)
                    {
                        draw_string("Hello Bhabhi ji ", cur_x, cur_y, current_text_color, fb);
                        cur_y += 12;
                        draw_string("Accept Lakshit's proposal first. Then you are allowed to work. ", cur_x, cur_y, current_text_color, fb);
                        cur_y += 12;
                        char *question = "Will you accept ?(y/n)";
                        draw_string(question, cur_x, cur_y, 0xFCE94F, fb); // Yellow text
                        // Move cursor to the end of the question
                        cur_x += strlen(question) * 8;
                        draw_cursor(cur_x, cur_y, current_text_color, fb);
                        char user_answer = 0;
                        // Mini-loop: Trap the OS here until 'y' or 'n' is pressed
                        while (1)
                        {
                            if (inb(0x64) & 1)
                            {
                                uint8_t answer_scancode = inb(0x60);
                                if (answer_scancode < 0x80)
                                { // Key press
                                    char key = scancode_map[answer_scancode];
                                    if (key == 'y' || key == 'n' || key == 'Y' || key == 'N')
                                    {
                                        user_answer = key;
                                        // Draw the letter they typed
                                        draw_char(user_answer, cur_x, cur_y, current_text_color, fb);
                                        break; // Break out of the mini-loop!
                                    }
                                }
                            }
                        }

                        cur_y += 12;
                        cur_x = 10;

                        // Make a decision based on input!
                        if (user_answer == 'y' || user_answer == 'Y')
                        {
                            draw_string("Congratulations Lakshit you found yourself a girlfriend.", cur_x, cur_y, 0x8AE234, fb); // Green
                            cur_y += 12;
                        }
                        else
                        {
                            draw_string("Think again I will get closed! You wont be able to do your work.", cur_x, cur_y, 0xEF2929, fb); // Red
                            cur_y += 12;
                            char *question = "Will you accept ?(y/n)";
                            draw_string(question, cur_x, cur_y, 0xFCE94F, fb); // Yellow text
                            // Move cursor to the end of the question
                            cur_x += strlen(question) * 8;
                            draw_cursor(cur_x, cur_y, current_text_color, fb);
                            char user_answer = 0;
                            // Mini-loop: Trap the OS here until 'y' or 'n' is pressed
                            while (1)
                            {
                                if (inb(0x64) & 1)
                                {
                                    uint8_t answer_scancode = inb(0x60);
                                    if (answer_scancode < 0x80)
                                    { // Key press
                                        char key = scancode_map[answer_scancode];
                                        if (key == 'y' || key == 'n' || key == 'Y' || key == 'N')
                                        {
                                            user_answer = key;
                                            // Draw the letter they typed
                                            draw_char(user_answer, cur_x, cur_y, current_text_color, fb);
                                            break; // Break out of the mini-loop!
                                        }
                                    }
                                }
                            }

                            cur_y += 12;
                            cur_x = 10;
                            // Make a decision based on input!
                            if (user_answer == 'y' || user_answer == 'Y')
                            {
                                draw_string("Congratulations Lakshit you found yourself a girlfriend.", cur_x, cur_y, 0x8AE234, fb); // Green
                            }
                            else
                            {
                                outw(0x604, 0x2000);
                                outw(0xB004, 0x2000);
                            }
                            cur_y += 12;
                        }
                    }
                    else if (strcmp(input_buffer, "Chanpa") == 0 || strcmp(input_buffer, "chanpa") == 0)
                    {
                        draw_string("Hello Bhabhi ji ", cur_x, cur_y, current_text_color, fb);
                        cur_y += 12;
                        draw_string("Accept Mohit's proposal first. Then you are allowed to work. ", cur_x, cur_y, current_text_color, fb);
                        cur_y += 12;
                        char *question = "Will you accept ?(y/n)";
                        draw_string(question, cur_x, cur_y, 0xFCE94F, fb); // Yellow text
                        // Move cursor to the end of the question
                        cur_x += strlen(question) * 8;
                        draw_cursor(cur_x, cur_y, current_text_color, fb);
                        char user_answer = 0;
                        // Mini-loop: Trap the OS here until 'y' or 'n' is pressed
                        while (1)
                        {
                            if (inb(0x64) & 1)
                            {
                                uint8_t answer_scancode = inb(0x60);
                                if (answer_scancode < 0x80)
                                { // Key press
                                    char key = scancode_map[answer_scancode];
                                    if (key == 'y' || key == 'n' || key == 'Y' || key == 'N')
                                    {
                                        user_answer = key;
                                        // Draw the letter they typed
                                        draw_char(user_answer, cur_x, cur_y, current_text_color, fb);
                                        break; // Break out of the mini-loop!
                                    }
                                }
                            }
                        }

                        cur_y += 12;
                        cur_x = 10;

                        // Make a decision based on input!
                        if (user_answer == 'y' || user_answer == 'Y')
                        {
                            draw_string("Congratulations Mohit you found yourself a girlfriend.", cur_x, cur_y, 0x8AE234, fb); // Green
                            cur_y += 12;
                        }
                        else
                        {
                            draw_string("Think again I will get closed! You wont be able to do your work.", cur_x, cur_y, 0xEF2929, fb); // Red
                            cur_y += 12;
                            char *question = "Will you accept ?(y/n)";
                            draw_string(question, cur_x, cur_y, 0xFCE94F, fb); // Yellow text
                            // Move cursor to the end of the question
                            cur_x += strlen(question) * 8;
                            draw_cursor(cur_x, cur_y, current_text_color, fb);
                            char user_answer = 0;
                            // Mini-loop: Trap the OS here until 'y' or 'n' is pressed
                            while (1)
                            {
                                if (inb(0x64) & 1)
                                {
                                    uint8_t answer_scancode = inb(0x60);
                                    if (answer_scancode < 0x80)
                                    { // Key press
                                        char key = scancode_map[answer_scancode];
                                        if (key == 'y' || key == 'n' || key == 'Y' || key == 'N')
                                        {
                                            user_answer = key;
                                            // Draw the letter they typed
                                            draw_char(user_answer, cur_x, cur_y, current_text_color, fb);
                                            break; // Break out of the mini-loop!
                                        }
                                    }
                                }
                            }

                            cur_y += 12;
                            cur_x = 10;
                            // Make a decision based on input!
                            if (user_answer == 'y' || user_answer == 'Y')
                            {
                                draw_string("Congratulations Mohit you found yourself a girlfriend.", cur_x, cur_y, 0x8AE234, fb); // Green
                            }
                            else
                            {
                                outw(0x604, 0x2000);
                                outw(0xB004, 0x2000);
                            }
                            cur_y += 12;
                        }
                    }
                    else if (strcmp(input_buffer, "Aayushman") == 0 || strcmp(input_buffer, "aayushman") == 0 ||
                             strcmp(input_buffer, "Lakshit") == 0 || strcmp(input_buffer, "lakshit") == 0 ||
                             strcmp(input_buffer, "Himanshu") == 0 || strcmp(input_buffer, "himanshu") == 0 ||
                             strcmp(input_buffer, "Mohit") == 0 || strcmp(input_buffer, "mohit") == 0 ||
                             strcmp(input_buffer, "Vaman") == 0 || strcmp(input_buffer, "vaman") == 0)
                    {

                        draw_string("Hello project member . Lets work --", cur_x, cur_y, current_text_color, fb);
                        cur_y += 12;
                    }
                    // --- SYSINFO COMMAND ---
                    else if (strcmp(input_buffer, "sysinfo") == 0)
                    {
                        char vendor[13];
                        get_cpuid_string(vendor); // Fetch the hardware string

                        // Print the header
                        draw_string("--- System Information ---", cur_x, cur_y, 0x34E2E2, fb);
                        cur_y += 12;

                        // Print the CPU Vendor
                        draw_string("CPU Vendor : ", cur_x, cur_y, 0xFCE94F, fb);
                        draw_string(vendor, cur_x + (13 * 8), cur_y, current_text_color, fb);
                        cur_y += 12;

                        // Print the Resolution
                        draw_string("Resolution : ", cur_x, cur_y, 0xFCE94F, fb);

                        char w_str[10], h_str[10];
                        itoa(fb->width, w_str);
                        itoa(fb->height, h_str);

                        // Calculate where to draw the width, the "x", and the height
                        int temp_x = cur_x + (13 * 8);
                        draw_string(w_str, temp_x, cur_y, current_text_color, fb);

                        temp_x += strlen(w_str) * 8;
                        draw_string(" x ", temp_x, cur_y, current_text_color, fb);

                        temp_x += 3 * 8;
                        draw_string(h_str, temp_x, cur_y, current_text_color, fb);
                        cur_y += 12;
                    }
                    else if (strcmp(input_buffer, "clear") == 0)
                    {
                        clear_screen(fb);
                        cur_y = 10;
                    }
                    // --- DRAW RECTANGLE COMMAND ---
                    else if (strncmp(input_buffer, "rect ", 5) == 0)
                    {
                        int i = 5;
                        int params[4] = {0, 0, 0, 0};

                        // Parse the 4 integer arguments (X, Y, Width, Height)
                        for (int p = 0; p < 4; p++)
                        {
                            while (input_buffer[i] == ' ')
                                i++; // Skip spaces
                            while (input_buffer[i] >= '0' && input_buffer[i] <= '9')
                            {
                                params[p] = params[p] * 10 + (input_buffer[i] - '0');
                                i++;
                            }
                        }

                        // Parse the final argument (Color)
                        while (input_buffer[i] == ' ')
                            i++;
                        uint32_t rect_color = 0xFFFFFF; // Default to white

                        if (input_buffer[i] != '\0')
                        {
                            // Skip "0x" if the user typed it
                            if (input_buffer[i] == '0' && (input_buffer[i + 1] == 'x' || input_buffer[i + 1] == 'X'))
                            {
                                i += 2;
                            }
                            rect_color = hex2int(&input_buffer[i]);
                        }

                        // Draw the shape!
                        draw_rect(params[0], params[1], params[2], params[3], rect_color, fb);

                        draw_string("Rectangle drawn!", cur_x, cur_y, current_text_color, fb);
                        cur_y += 12;
                    }
                    // --- DRAW CIRCLE COMMAND ---
                    else if (strncmp(input_buffer, "circle ", 7) == 0)
                    {
                        int i = 7;
                        int params[3] = {0, 0, 0}; // X, Y, Radius

                        // Parse the 3 integer arguments
                        for (int p = 0; p < 3; p++)
                        {
                            while (input_buffer[i] == ' ')
                                i++; // Skip spaces
                            while (input_buffer[i] >= '0' && input_buffer[i] <= '9')
                            {
                                params[p] = params[p] * 10 + (input_buffer[i] - '0');
                                i++;
                            }
                        }

                        // Parse the final argument (Color)
                        while (input_buffer[i] == ' ')
                            i++;
                        uint32_t circle_color = 0xFFFFFF; // Default to white

                        if (input_buffer[i] != '\0')
                        {
                            // Skip "0x" if the user typed it
                            if (input_buffer[i] == '0' && (input_buffer[i + 1] == 'x' || input_buffer[i + 1] == 'X'))
                            {
                                i += 2;
                            }
                            circle_color = hex2int(&input_buffer[i]);
                        }

                        // Draw the shape!
                        draw_circle(params[0], params[1], params[2], circle_color, fb);

                        draw_string("Circle drawn!", cur_x, cur_y, current_text_color, fb);
                        cur_y += 12;
                    }
                    else if (strcmp(input_buffer, "shutdown") == 0)
                    {
                        draw_string("System going down for halt NOW!", cur_x, cur_y, 0xEF2929, fb);
                        cur_y += 12;

                        outw(0x604, 0x2000);
                        outw(0xB004, 0x2000);

                        draw_string("It is now safe to turn off your computer.", cur_x, cur_y, 0xFFFFFF, fb);
                        for (;;)
                        {
                            __asm__("cli; hlt");
                        }
                    }
                    else if (strncmp(input_buffer, "echo ", 5) == 0)
                    {
                        draw_string(input_buffer + 5, cur_x, cur_y, current_text_color, fb);
                        cur_y += 12;
                    }
                    else if (strncmp(input_buffer, "calc ", 5) == 0)
                    {
                        int i = 5;
                        int num1 = 0, num2 = 0;
                        char op = 0;
                        while (input_buffer[i] >= '0' && input_buffer[i] <= '9')
                        {
                            num1 = num1 * 10 + (input_buffer[i] - '0');
                            i++;
                        }
                        while (input_buffer[i] == ' ')
                            i++;
                        op = input_buffer[i];
                        i++;
                        while (input_buffer[i] == ' ')
                            i++;
                        while (input_buffer[i] >= '0' && input_buffer[i] <= '9')
                        {
                            num2 = num2 * 10 + (input_buffer[i] - '0');
                            i++;
                        }

                        int result = 0;
                        int valid = 1;
                        if (op == '+')
                            result = num1 + num2;
                        else if (op == '-')
                            result = num1 - num2;
                        else if (op == '*')
                            result = num1 * num2;
                        else if (op == '/')
                        {
                            if (num2 == 0)
                                valid = 0;
                            else
                                result = num1 / num2;
                        }
                        else
                            valid = 0;

                        if (valid)
                        {
                            char res_str[32];
                            itoa(result, res_str);
                            draw_string(res_str, cur_x, cur_y, 0x00FFFF, fb);
                        }
                        else
                        {
                            draw_string("Error. Usage: calc 5 + 10", cur_x, cur_y, 0xEF2929, fb);
                        }
                        cur_y += 12;
                    }
                    else if (strcmp(input_buffer, "time") == 0)
                    {
                        uint8_t sec = read_rtc(0x00);
                        uint8_t min = read_rtc(0x02);
                        uint8_t hour = read_rtc(0x04);

                        sec = (sec & 0x0F) + ((sec / 16) * 10);
                        min = (min & 0x0F) + ((min / 16) * 10);
                        hour = (hour & 0x0F) + ((hour / 16) * 10);

                        min += 30;
                        if (min >= 60)
                        {
                            min -= 60;
                            hour += 1;
                        }
                        hour += 5;
                        if (hour >= 24)
                            hour -= 24;

                        char time_str[] = "Current IST Time: 00:00:00";
                        time_str[18] = (hour / 10) + '0';
                        time_str[19] = (hour % 10) + '0';
                        time_str[21] = (min / 10) + '0';
                        time_str[22] = (min % 10) + '0';
                        time_str[24] = (sec / 10) + '0';
                        time_str[25] = (sec % 10) + '0';

                        draw_string(time_str, cur_x, cur_y, 0xFCE94F, fb);
                        cur_y += 12;
                    }
                    else if (strncmp(input_buffer, "color ", 6) == 0)
                    {
                        int i = 6;
                        while (input_buffer[i] == ' ')
                            i++;
                        char *color_arg = &input_buffer[i];

                        if (strcmp(color_arg, "red") == 0)
                            current_text_color = 0xFF0000;
                        else if (strcmp(color_arg, "green") == 0)
                            current_text_color = 0x00FF00;
                        else if (strcmp(color_arg, "blue") == 0)
                            current_text_color = 0x0000FF;
                        else if (strcmp(color_arg, "yellow") == 0)
                            current_text_color = 0xFFFF00;
                        else if (strcmp(color_arg, "cyan") == 0)
                            current_text_color = 0x00FFFF;
                        else if (strcmp(color_arg, "magenta") == 0)
                            current_text_color = 0xFF00FF;
                        else if (strcmp(color_arg, "white") == 0)
                            current_text_color = 0xFFFFFF;
                        else
                        {
                            if (color_arg[0] == '0' && (color_arg[1] == 'x' || color_arg[1] == 'X'))
                            {
                                color_arg += 2;
                            }
                            current_text_color = hex2int(color_arg);
                        }

                        draw_string("Terminal color updated!", cur_x, cur_y, current_text_color, fb);
                        cur_y += 12;
                    }
                    else if (buffer_index > 0)
                    {
                        draw_string("Command not found.", cur_x, cur_y, 0xEF2929, fb);
                        cur_y += 12;
                    }

                    if (cur_y > (int)(fb->height - 24))
                    {
                        scroll_screen(fb);
                        cur_y -= 12;
                    }

                    buffer_index = 0;
                    draw_string("root@myos:~$ ", 10, cur_y, 0x8AE234, fb);
                    cur_x = 10 + (13 * 8);

                    draw_cursor(cur_x, cur_y, current_text_color, fb);
                }
                else if (c > 0 && buffer_index < 255)
                {
                    input_buffer[buffer_index] = c;
                    buffer_index++;
                    draw_char(c, cur_x, cur_y, current_text_color, fb);
                    cur_x += 8;

                    draw_cursor(cur_x, cur_y, current_text_color, fb);
                }
            }
        }
    }
}
