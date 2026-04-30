#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

int main() {
    int fd = open("/dev/input/event3", O_RDONLY);
    if (fd < 0) {
        perror("[!] Failed to open /dev/input/event3");
        return 1;
    }
    
    struct input_event ev;
    printf("[*] Hooked into Event3. \n[*] Press buttons and move sticks to see their true hardware codes (Ctrl+C to exit)...\n\n");
    
    while (read(fd, &ev, sizeof(ev)) > 0) {
        // Only print key presses (1) and analog movements, ignore releases (0) to reduce spam
        if (ev.type == EV_KEY && ev.value == 1) {
            printf("[BUTTON] Code: %d (0x%02x) | Pressed\n", ev.code, ev.code);
        } 
        else if (ev.type == EV_ABS) {
            printf("[ANALOG] Code: %d (0x%02x) | Value: %d\n", ev.code, ev.code, ev.value);
        }
    }
    
    close(fd);
    return 0;
}