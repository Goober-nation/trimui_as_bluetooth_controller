#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>
#include <linux/input.h>
#include <stdint.h>
#include <string.h>

// --- HARDWARE CONFIGURATION ---
#define AXIS_MIN -32768
#define AXIS_MAX 32767

int nintendo_layout = 1; // Default, will be overwritten by config.conf

// Combo State Tracking
int menu_pressed = 0;
int home_combo_active = 0;
int l3_combo_active = 0;
int r3_combo_active = 0;

volatile sig_atomic_t keep_running = 1;

void handle_signal(int sig) { keep_running = 0; }

// --- CONFIGURATION PARSER ---
void load_config() {
    FILE *fp = fopen("config.conf", "r");
    if (!fp) {
        printf("[*] config.conf not found. Generating default file...\n");
        fp = fopen("config.conf", "w");
        if (fp) {
            fprintf(fp, "# TrimUI Gamepad Configuration\n");
            fprintf(fp, "# nintendo_layout: 1 = Nintendo (A Right, B Down), 0 = Xbox (A Down, B Right)\n");
            fprintf(fp, "nintendo_layout=1\n");
            fclose(fp);
        }
        nintendo_layout = 1;
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        int val;
        if (sscanf(line, "nintendo_layout=%d", &val) == 1) {
            nintendo_layout = val;
        }
    }
    fclose(fp);
}

// --- HID REPORT STRUCTURE ---
uint8_t hid_report[8] = {0xA1, 0x00, 0x00, 0x7F, 0x7F, 0x7F, 0x7F, 0x08};
int32_t hat_x = 0;
int32_t hat_y = 0;

// --- GAMEPAD REPORT DESCRIPTOR ---
uint8_t gamepad_descriptor[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 
    0x05, 0x09, 0x19, 0x01, 0x29, 0x10, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x10, 0x81, 0x02, 
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x33, 0x09, 0x34, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x04, 0x81, 0x02, 
    0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x35, 0x00, 0x46, 0x3B, 0x01, 0x65, 0x14, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42, 
    0x75, 0x04, 0x95, 0x01, 0x81, 0x03, 
    0xC0
};

// --- SOCKET & SDP INITIALIZATION ---
int setup_socket(int psm) {
    struct sockaddr_l2 addr = { 0 };
    int s = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    if (s < 0) return -1;
    addr.l2_family = AF_BLUETOOTH; addr.l2_psm = htobs(psm);
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(s); return -1; }
    if (listen(s, 1) < 0) { close(s); return -1; }
    return s;
}

sdp_session_t *register_gamepad_sdp() {
    sdp_session_t *session = sdp_connect(BDADDR_ANY, BDADDR_LOCAL, SDP_RETRY_IF_BUSY);
    if (!session) return NULL;
    sdp_record_t *record = sdp_record_alloc();
    uuid_t *hid_uuid = malloc(sizeof(uuid_t)); sdp_uuid16_create(hid_uuid, 0x1124);
    sdp_list_t *svclass_id = sdp_list_append(NULL, hid_uuid); sdp_set_service_classes(record, svclass_id);
    sdp_set_info_attr(record, "Trimui_Gamepad", "TrimUI", "Gamepad");

    uint16_t *ctrl_psm = malloc(sizeof(uint16_t)); *ctrl_psm = 0x0011;
    uint16_t *intr_psm = malloc(sizeof(uint16_t)); *intr_psm = 0x0013;
    sdp_attr_add_new(record, 0x0200, SDP_UINT16, ctrl_psm);
    sdp_attr_add_new(record, 0x0201, SDP_UINT16, intr_psm);

    uint16_t *l2cap_val = malloc(sizeof(uint16_t)); *l2cap_val = 0x0100;
    uint16_t *hidp_val  = malloc(sizeof(uint16_t)); *hidp_val  = 0x0011;
    uint16_t *psm_val   = malloc(sizeof(uint16_t)); *psm_val   = 0x0011; 
    sdp_data_t *d_l2cap = sdp_data_alloc(SDP_UUID16, l2cap_val);
    sdp_data_t *d_psm   = sdp_data_alloc(SDP_UINT16, psm_val);
    d_l2cap->next = d_psm;
    sdp_data_t *seq_l2cap = sdp_data_alloc(SDP_SEQ8, d_l2cap);
    sdp_data_t *d_hidp   = sdp_data_alloc(SDP_UUID16, hidp_val);
    sdp_data_t *seq_hidp = sdp_data_alloc(SDP_SEQ8, d_hidp);
    seq_l2cap->next = seq_hidp; 
    sdp_data_t *proto_list_seq = sdp_data_alloc(SDP_SEQ8, seq_l2cap);
    sdp_attr_add(record, 0x0004, proto_list_seq); 

    uint8_t *desc_type = malloc(sizeof(uint8_t)); *desc_type = 0x22;
    sdp_data_t *d1 = sdp_data_alloc(SDP_UINT8, desc_type);
    sdp_data_t *d2 = sdp_data_alloc_with_length(SDP_TEXT_STR8, gamepad_descriptor, sizeof(gamepad_descriptor));
    d1->next = d2; sdp_data_t *seq2 = sdp_data_alloc(SDP_SEQ8, d1); sdp_data_t *seq1 = sdp_data_alloc(SDP_SEQ8, seq2);
    sdp_attr_add(record, 0x0206, seq1); 
    if (sdp_record_register(session, record, 0) < 0) { sdp_close(session); return NULL; } 
    return session;
}

// --- DATA TRANSLATION ---
uint8_t map_axis(int32_t value) {
    if (value < AXIS_MIN) return 0;
    if (value > AXIS_MAX) return 255;
    return (uint8_t)(((int64_t)(value - AXIS_MIN) * 255) / (AXIS_MAX - AXIS_MIN));
}

void update_hat_switch() {
    if (hat_x == 0 && hat_y == -1)      hid_report[7] = 0; 
    else if (hat_x == 1 && hat_y == -1) hid_report[7] = 1; 
    else if (hat_x == 1 && hat_y == 0)  hid_report[7] = 2; 
    else if (hat_x == 1 && hat_y == 1)  hid_report[7] = 3; 
    else if (hat_x == 0 && hat_y == 1)  hid_report[7] = 4; 
    else if (hat_x == -1 && hat_y == 1) hid_report[7] = 5; 
    else if (hat_x == -1 && hat_y == 0) hid_report[7] = 6; 
    else if (hat_x == -1 && hat_y == -1)hid_report[7] = 7; 
    else hid_report[7] = 8; 
}

void process_input_event(uint16_t type, uint16_t code, int32_t value) {
    if (type == EV_KEY) {
        int is_p = (value == 1);
        
        // Track Menu Button State
        if (code == 316) menu_pressed = is_p;

        // Physical Bottom Button (304)
        if (code == 304) {
            int bit = nintendo_layout ? 1 : 0; 
            if (is_p) hid_report[1] |= (1 << bit); else hid_report[1] &= ~(1 << bit);
        }
        // Physical Right Button (305)
        else if (code == 305) { 
            int bit = nintendo_layout ? 0 : 1; 
            if (is_p) hid_report[1] |= (1 << bit); else hid_report[1] &= ~(1 << bit);
        }
        // Physical Left Button (308) 
        else if (code == 308) { 
            int bit = nintendo_layout ? 4 : 3; 
            if (is_p) hid_report[1] |= (1 << bit); else hid_report[1] &= ~(1 << bit);
        }
        
        // Physical Top Button (307) -> MENU + X = HOME COMBO
        else if (code == 307) { 
            if (is_p && menu_pressed) {
                hid_report[2] |= (1 << 4); 
                home_combo_active = 1;     
            } 
            else if (!is_p && home_combo_active) {
                hid_report[2] &= ~(1 << 4); 
                home_combo_active = 0;
            } 
            else {
                int bit = nintendo_layout ? 3 : 4; 
                if (is_p) hid_report[1] |= (1 << bit); else hid_report[1] &= ~(1 << bit);
            }
        }
        
        // L1 & L3 Combo
        else if (code == 310) { 
            if (is_p && menu_pressed) { 
                hid_report[2] |= (1<<5); 
                l3_combo_active = 1;
            } 
            else if (!is_p && l3_combo_active) {
                hid_report[2] &= ~(1<<5); 
                l3_combo_active = 0;
            } 
            else { 
                if (is_p) hid_report[1] |= (1<<6); else hid_report[1] &= ~(1<<6); 
            }
        }
        
        // R1 & R3 Combo
        else if (code == 311) { 
            if (is_p && menu_pressed) { 
                hid_report[2] |= (1<<6); 
                r3_combo_active = 1;
            } 
            else if (!is_p && r3_combo_active) {
                hid_report[2] &= ~(1<<6); 
                r3_combo_active = 0;
            } 
            else { 
                if (is_p) hid_report[1] |= (1<<7); else hid_report[1] &= ~(1<<7); 
            }
        }

        // Select & Start (AND THE KILL SWITCH)
        else if (code == 314) { 
            if (is_p) hid_report[2] |= (1<<2); else hid_report[2] &= ~(1<<2); 
        } 
        else if (code == 315) { 
            if (is_p && menu_pressed) {
                // FORCE QUIT TRIGGERED: Menu + Start
                keep_running = 0; 
            } 
            else {
                if (is_p) hid_report[2] |= (1<<3); else hid_report[2] &= ~(1<<3); 
            }
        }
    } 
    else if (type == EV_ABS) {
        switch(code) {
            case 0: hid_report[3] = map_axis(value); break;
            case 1: hid_report[4] = map_axis(value); break;
            case 3: hid_report[5] = map_axis(value); break;
            case 4: hid_report[6] = map_axis(value); break;
            case 2: if (value > 127) hid_report[2] |= (1<<0); else hid_report[2] &= ~(1<<0); break;
            case 5: if (value > 127) hid_report[2] |= (1<<1); else hid_report[2] &= ~(1<<1); break;
            case 16: hat_x = value; update_hat_switch(); break;
            case 17: hat_y = value; update_hat_switch(); break;
        }
    }
}

// --- MAIN EXECUTION ---
int main() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL); sigaction(SIGTERM, &sa, NULL); signal(SIGPIPE, SIG_IGN); 

    load_config(); 
    printf("[*] Starting Gamepad Server...\n");
    
    sdp_session_t *sdp_session = register_gamepad_sdp();
    int ctrl_s = setup_socket(0x11), intr_s = setup_socket(0x13);
    if (!sdp_session || ctrl_s < 0 || intr_s < 0) { if(sdp_session) sdp_close(sdp_session); return 1; }

    printf("[*] Waiting for host connection...\n");
    int ctrl_c = -1, intr_c = -1;
    while ((ctrl_c = accept(ctrl_s, NULL, NULL)) < 0) { if (errno == EINTR && keep_running) continue; break; }
    if (ctrl_c >= 0) printf("[+] Control Linked.\n");
    while ((intr_c = accept(intr_s, NULL, NULL)) < 0) { if (errno == EINTR && keep_running) continue; break; }
    if (intr_c >= 0) printf("[+] Interrupt Linked. Host connected.\n");

    int input_fd = open("/dev/input/event3", O_RDONLY | O_NONBLOCK);
    if (input_fd < 0) { perror("[!] Failed to open /dev/input/event3"); keep_running = 0; }

    struct input_event ev;
    fd_set readfds;

    while(keep_running && input_fd >= 0 && intr_c >= 0) {
        FD_ZERO(&readfds);
        FD_SET(input_fd, &readfds);
        if (select(input_fd + 1, &readfds, NULL, NULL, NULL) > 0) {
            int report_changed = 0;
            while(read(input_fd, &ev, sizeof(ev)) > 0) {
                process_input_event(ev.type, ev.code, ev.value);
                report_changed = 1;
            }
            if (report_changed) { if (write(intr_c, hid_report, sizeof(hid_report)) < 0) break; }
        }
    }

    printf("\n[*] Shutting down...\n");
    if (input_fd >= 0) close(input_fd);
    if (intr_c >= 0) close(intr_c); if (ctrl_c >= 0) close(ctrl_c);
    if (intr_s >= 0) close(intr_s); if (ctrl_s >= 0) close(ctrl_s);
    if (sdp_session) sdp_close(sdp_session);
    return 0;
}
