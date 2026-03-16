    #include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include "font.h"

// --- Limine Request ---
__attribute__((used, section(".requests"))) static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0};

// --- Port I/O Helpers ---
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
size_t strlen(const char *str)
{
    size_t len = 0;
    while (str[len])
        len++;
    return len;
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

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];

    clear_screen(fb);
    draw_string("Welcome to MyOS Shell!", 10, 10, 0xFFFFFF, fb);

    int cur_x = 10;
    int cur_y = 30;

    char input_buffer[256];
    int buffer_index = 0;

    uint32_t current_text_color = 0xFFFFFF;

    draw_string("root@myos:~$ ", cur_x, cur_y, 0x8AE234, fb);
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
            if (scancode == 0xE0) {
                e0_prefix = 1; // Flag that an arrow key is coming next
                continue;
            }
            
            if (e0_prefix) {
                e0_prefix = 0;
                // 0x48 is Up Arrow, 0x50 is Down Arrow
                if ((scancode == 0x48 || scancode == 0x50) && history_count > 0) {
                    
                    // 1. Visually erase whatever the user is currently typing
                    for(int i = 0; i < buffer_index; i++) {
                        for(int r = 0; r < 8; r++) 
                            for(int c = 0; c < 8; c++) 
                                draw_pixel(cur_x - (i+1)*8 + c, cur_y + r, 0x300A24, fb);
                    }
                    cur_x -= buffer_index * 8; // Move cursor back
                    
                    // 2. Change our history index
                    if (scancode == 0x48) { // Up arrow
                        if (history_index > 0) history_index--;
                    } else if (scancode == 0x50) { // Down arrow
                        if (history_index < history_count - 1) history_index++;
                        else history_index = history_count; // Clear line at bottom
                    }
                    
                    // 3. Load the history onto the screen and into the buffer
                    if (history_index < history_count) {
                        buffer_index = strlen(cmd_history[history_index]);
                        for (int i = 0; i < buffer_index; i++) 
                            input_buffer[i] = cmd_history[history_index][i];
                        
                        input_buffer[buffer_index] = '\0';
                        draw_string(input_buffer, cur_x, cur_y, current_text_color, fb);
                        cur_x += buffer_index * 8;
                    } else {
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
                    if (buffer_index > 0) {
                        if (history_count < 10) {
                            // Array isn't full, just add it
                            for (int i = 0; i <= buffer_index; i++) 
                                cmd_history[history_count][i] = input_buffer[i];
                            history_count++;
                        } else {
                            // Array is full, shift everything up by 1 to make room at the bottom
                            for (int h = 1; h < 10; h++) {
                                for (int i = 0; i < 256; i++) 
                                    cmd_history[h-1][i] = cmd_history[h][i];
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
                             strcmp(input_buffer, "Humanshu") == 0 || strcmp(input_buffer, "humanshu") == 0 ||
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