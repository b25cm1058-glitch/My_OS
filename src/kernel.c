#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include "font.h"

// --- Limine Request ---
__attribute__((used, section(".requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

// --- Port I/O Helpers ---
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// --- Basic US QWERTY Scancode Map ---
char scancode_map[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

// --- Drawing Functions ---
void draw_pixel(int x, int y, uint32_t color, struct limine_framebuffer *fb) {
    uint32_t *fb_ptr = fb->address;
    fb_ptr[y * (fb->pitch / 4) + x] = color;
}

void draw_char(char c, int x, int y, uint32_t color, struct limine_framebuffer *fb) {
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if ((font8x8_basic[(uint8_t)c][row] >> (7 - col)) & 1) {
                draw_pixel(x + col, y + row, color, fb);
            }
        }
    }
}

void draw_string(const char *str, int x, int y, uint32_t color, struct limine_framebuffer *fb) {
    for (int i = 0; str[i] != '\0'; i++) {
        draw_char(str[i], x + (i * 8), y, color, fb);
    }
}

// --- Main Execution ---
void _start(void) {
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
        for (;;) { __asm__("hlt"); }
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];

    // 1. Draw Background (Gradient)
    for (int y = 0; y < (int)fb->height; y++) {
        uint32_t color = ( (y * 50 / fb->height) << 16) | 0x000044; // Fade to blue
        for (int x = 0; x < (int)fb->width; x++) draw_pixel(x, y, color, fb);
    }

    // 2. Print Static Text (Phase 1)
    draw_string("HELLO WORLD!", 100, 50, 0xFFFF00, fb);
    draw_string("Kernel is active. Type below:", 100, 70, 0xFFFFFF, fb);

    // 3. Input Loop (Phase 2)
    int cur_x = 100;
    int cur_y = 100;

    while (1) {
        // Poll keyboard status
        if (inb(0x64) & 1) {
            uint8_t scancode = inb(0x60);
            if (scancode < 0x80) { // Key Pressed
                char c = scancode_map[scancode];
                
                if (c == '\b') { // Backspace
                    if (cur_x > 100) {
                        cur_x -= 8;
                        // Erase character by drawing a background-colored box
                        for(int i=0; i<8; i++) 
                            for(int j=0; j<8; j++) 
                                draw_pixel(cur_x+i, cur_y+j, 0x000044, fb);
                    }
                } else if (c == '\n') { // Enter
                    cur_x = 100;
                    cur_y += 12;
                } else if (c > 0) { // Regular character
                    draw_char(c, cur_x, cur_y, 0x00FF00, fb);
                    cur_x += 8;
                }
            }
        }
    }
}