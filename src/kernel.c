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

// --- Shifted US QWERTY Scancode Map ---
char scancode_map_shift[] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '
};

// --- String & Math Helpers (No Standard C Library) ---
int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++; s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        ++s1; ++s2; --n;
    }
    if (n == 0) return 0;
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

// Converts an actual integer into a text string so we can draw it
void itoa(int n, char str[]) {
    int i = 0, sign = n;
    
    if (n < 0) n = -n; // Make number positive for math
    
    // Chop off digits one by one and convert to ASCII
    do {
        str[i++] = n % 10 + '0'; 
    } while ((n /= 10) > 0);
    
    if (sign < 0) str[i++] = '-'; // Add negative sign back if needed
    str[i] = '\0'; // End the string
    
    // Reverse the string (since we extracted digits backwards)
    int j = 0, k = i - 1;
    while (j < k) {
        char temp = str[j];
        str[j] = str[k];
        str[k] = temp;
        j++; k--;
    }
}

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

void clear_screen(struct limine_framebuffer *fb) {
    uint32_t *fb_ptr = fb->address;
    for (size_t i = 0; i < fb->width * fb->height; i++) {
        fb_ptr[i] = 0x300A24; // Ubuntu Dark Purple Background
    }
}

// --- Main Execution ---
void _start(void) {
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
        for (;;) { __asm__("hlt"); }
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];

    // 1. Setup the initial screen
    clear_screen(fb);
    draw_string("Welcome to MyOS Shell!", 10, 10, 0xFFFFFF, fb);
    
    int cur_x = 10;
    int cur_y = 30;

    // 2. The Input Buffer
    char input_buffer[256];
    int buffer_index = 0;

    // Draw the first prompt
    draw_string("root@myos:~$ ", cur_x, cur_y, 0x8AE234, fb); // Green
    cur_x += 13 * 8; // Move cursor past "root@myos:~$ "

    int shift_pressed = 0; // 0 = false, 1 = true

    // 3. Main Loop
    while (1) {
        // Poll keyboard status
        if (inb(0x64) & 1) {
            uint8_t scancode = inb(0x60);
            
            // --- Detect Shift Press and Release ---
            if (scancode == 0x2A || scancode == 0x36) {
                shift_pressed = 1; // Left or Right Shift pressed
                continue;          // Skip drawing, wait for next key
            } 
            else if (scancode == 0xAA || scancode == 0xB6) {
                shift_pressed = 0; // Left or Right Shift released
                continue;          // Skip drawing
            }

            if (scancode < 0x80) { // Key Pressed (not released)
                
                // --- Pick the right letter based on the Shift state! ---
                char c;
                if (shift_pressed) {
                    c = scancode_map_shift[scancode];
                } else {
                    c = scancode_map[scancode];
                }
                
                if (c == '\b') { 
                    // Backspace logic
                    if (buffer_index > 0) {
                        buffer_index--; // Remove from memory
                        cur_x -= 8;     // Move visual cursor back
                        
                        // Erase character by drawing a purple box
                        for(int i = 0; i < 8; i++) 
                            for(int j = 0; j < 8; j++) 
                                draw_pixel(cur_x + i, cur_y + j, 0x300A24, fb);
                    }
                } 
                else if (c == '\n') { 
                    // Enter key logic
                    input_buffer[buffer_index] = '\0'; // End the string
                    
                    cur_y += 12; // Move down a line
                    cur_x = 10;  // Reset to left side
                    
                    // --- COMMAND PROCESSING ---
                    if (strcmp(input_buffer, "help") == 0) {
                        draw_string("Commands: help, about, clear, echo, calc", cur_x, cur_y, 0xFFFFFF, fb);
                        cur_y += 12;
                    } 
                    else if (strcmp(input_buffer, "about") == 0) {
                        draw_string("MyOS v1.1 - Built from scratch!", cur_x, cur_y, 0x34E2E2, fb); // Blue
                        cur_y += 12;
                    }
                    else if (strcmp(input_buffer, "clear") == 0) {
                        clear_screen(fb);
                        cur_y = 10; // Reset to top
                    }
                    else if (strncmp(input_buffer, "echo ", 5) == 0) {
                        draw_string(input_buffer + 5, cur_x, cur_y, 0x00FFFF, fb); // Cyan
                        cur_y += 12;
                    }
                    // --- THE CALCULATOR COMMAND ---
                    else if (strncmp(input_buffer, "calc ", 5) == 0) {
                        int i = 5;
                        int num1 = 0, num2 = 0;
                        char op = 0;

                        // 1. Read the first number from the string
                        while(input_buffer[i] >= '0' && input_buffer[i] <= '9') {
                            num1 = num1 * 10 + (input_buffer[i] - '0');
                            i++;
                        }
                        
                        // 2. Skip any spaces
                        while(input_buffer[i] == ' ') i++;
                        
                        // 3. Read the math operator (+, -, *, /)
                        op = input_buffer[i];
                        i++;
                        
                        // 4. Skip spaces again
                        while(input_buffer[i] == ' ') i++;
                        
                        // 5. Read the second number
                        while(input_buffer[i] >= '0' && input_buffer[i] <= '9') {
                            num2 = num2 * 10 + (input_buffer[i] - '0');
                            i++;
                        }

                        // 6. Do the actual math!
                        int result = 0;
                        int valid = 1; // Flag to check for errors
                        
                        if (op == '+') result = num1 + num2;
                        else if (op == '-') result = num1 - num2;
                        else if (op == '*') result = num1 * num2;
                        else if (op == '/') {
                            if (num2 == 0) valid = 0; // Prevent "divide by zero" kernel crash!
                            else result = num1 / num2;
                        } else {
                            valid = 0; // Unknown operator
                        }

                        // 7. Print the result back to the screen
                        if (valid) {
                            char res_str[32]; // Temporary memory for the answer string
                            itoa(result, res_str);
                            draw_string(res_str, cur_x, cur_y, 0x00FFFF, fb); // Draw in Cyan
                        } else {
                            draw_string("Error. Usage: calc 5 + 10", cur_x, cur_y, 0xEF2929, fb);
                        }
                        cur_y += 12;
                    }
                    else if (buffer_index > 0) {
                        draw_string("Command not found.", cur_x, cur_y, 0xEF2929, fb); // Red
                        cur_y += 12;
                    }

                    // --- PREVENT SCROLLING OFF SCREEN ---
                    if (cur_y > (int)(fb->height - 24)) {
                        clear_screen(fb);
                        cur_y = 10;
                    }

                    // Prepare for the next command
                    buffer_index = 0;
                    draw_string("root@myos:~$ ", 10, cur_y, 0x8AE234, fb);
                    cur_x = 10 + (13 * 8);
                } 
                else if (c > 0 && buffer_index < 255) { 
                    // Regular typing logic
                    input_buffer[buffer_index] = c;
                    buffer_index++;
                    
                    draw_char(c, cur_x, cur_y, 0xFFFFFF, fb);
                    cur_x += 8;
                }
            }
        }
    }
}