#include <stdio.h>
#include <inttypes.h>
#include "pico-sdk-master/src/common/pico_stdlib/include/pico/stdlib.h"
#include "pico-sdk-master/src/common/pico_binary_info/include/pico/binary_info.h"
#include "pico-sdk-master/src/host/pico_multicore/include/pico/multicore.h"
#include "pico-sdk-master/src/rp2_common/cmsis/stub/CMSIS/Device/RaspberryPi/RP2040/Include/RP2040.h"

float cells[24];
float delta = 0.0;
int cell_num = 0;
int min_cell_no, max_cell_no;

bool chgsns_active = false;
bool reset = true;
bool firstRun = true;

static struct can2040 cbus;
struct can2040_msg msg;
struct can2040_msg msg_out;
uint32_t notify;

float can_data[13] = {0.0, // battery_voltage
                    0.0, // battery_current
                    0.0, // battery_voltage_BMS
                    0.0, // battery_current_BMS
                    0.0, // high_cell_voltage
                    0.0, // low_cell_voltage
                    0.0, // high_battery_temp
                    0.0, // high_BMS_temp
                    0,   // motor_rpm
                    0.0, // total_current
                    0.0, // motor_temperature
                    0.0, // motor_current
                    0.0}; // controller_temperature

char can_labels[13][50] = {"battery_voltage",
                "battery_current",
                "battery_voltage_BMS",
                "battery_current_BMS",
                "high_cell_voltage",
                "low_cell_voltage",
                "high_battery_temp",
                "high_BMS_temp",
                "motor_rpm",
                "total_current",
                "motor_temperature",
                "motor_current",
                "controller_temperature"};

void findDelta(float cells[], int cell_num) {
    
    float min = 5.0;
    float max = 0.0;
    float total = 0.0; 

    for (int i = 0; i < cell_num; i++) {
        if (cells[i] < min) {
            min = cells[i];
            min_cell_no = (i+1);
        } else if (cells[i] > max) {
            max = cells[i];
            max_cell_no = (i+1);
        }     
        total += cells[i];   
    }
    delta = max - min;

    can_data[0] = total;
    can_data[4] = max;
    can_data[5] = min;
}

static void can2040_cb(struct can2040 *cd, uint32_t notify, struct can2040_msg *msg) {
    printf("%" PRIu32 "\n", msg.id);
}

static void PIOx_IRQHandler(void) {
    can2040_pio_irq_handler(&cbus);
}

void canbus_setup(void) {
    uint32_t pio_num = 0;
    uint32_t sys_clock = 125000000, bitrate = 500000;
    uint32_t gpio_rx = 4, gpio_tx = 5;

    // Setup canbus
    can2040_setup(&cbus, pio_num);
    can2040_callback_config(&cbus, can2040_cb);

    // Enable irqs
    irq_set_exclusive_handler(PIO0_IRQ_0_IRQn, PIOx_IRQHandler);
    NVIC_SetPriority(PIO0_IRQ_0_IRQn, 1);
    NVIC_EnableIRQ(PIO0_IRQ_0_IRQn);

    // Start canbus
    can2040_start(&cbus, sys_clock, bitrate, gpio_rx, gpio_tx);
}


// CORE 2
void main2() {
    stdio_init_all();

    while (true) {
        printf("--------------------------------\n");
        printf("\n--- Cell Information ---\n");
        for (int i = 0; i < cell_num; i++) {
            printf("Cell %i:  \t%.2fV\n", i+1, cells[i]);
        }
        findDelta(cells, cell_num);
        printf("\nMax cell delta:\t%.2fV between cell %i and cell %i\n\n", delta, max_cell_no, min_cell_no);
        printf("Press R & Enter to access configuration menu\n\n");
        
        can2040_transmit(&cbus, &msg_out);
        sleep_ms(1000);
    }
}


// CORE 1
int main() {
    stdio_init_all();
    gpio_init(25);
    gpio_set_dir(25, GPIO_OUT);
    
    char userInput;
    char userInputArray[20];

    gpio_put(25, 1);
    sleep_ms(250);
    gpio_put(25, 0);
    sleep_ms(250);
        
    if (firstRun == true) {
        for (int i = 0; i < 24; i++) {
            cells[i] = 0.0;
        } 
        multicore_launch_core1(main2);
        canbus_setup();
        firstRun = false;
    }

    while (true) {
        //Get User Input
        if (reset == true) {
            userInput = getchar();

            printf("--- Configuration Menu ---\n");
            printf("How many cells is your battery pack? Enter between 6-24: ");
            scanf("%20s", userInputArray);
            printf("%s\n", userInputArray);    
            cell_num = atoi(userInputArray);

            if (cell_num >= 6 && cell_num < 12) {
                printf("Please ensure you have the CELSEL jumper fitted in the '<12s' position!\n");
            } else if (cell_num > 12 && cell_num <= 24) {
                printf("Please ensure you have the CELSEL jumper fitted in the '>12s' position!\n");
            } else if (cell_num == 12) {
                printf("Note: Fitment of the CELSEL jumper is optional for 12s packs\n");
            } else if (cell_num < 6) {
                printf("This BMS only support 6S to 24S packs! Please re-run the configurator with the correct information\n");
                cell_num = 6;
            } else if (cell_num > 24) {
                printf("This BMS only support 6S to 24S packs! Please re-run the configurator with the correct information\n");
                cell_num = 24;
            }

            printf("\nAre you using the CHGSNS input? Enter Y or N: ");           
            scanf("%20s", userInputArray);
            printf("%s\n", userInputArray);            
            if (strcmp(userInputArray, "y") == 0 || strcmp(userInputArray, "Y") == 0) {
                chgsns_active = true;
                printf("Balancing will occur when charging or above 4.15V\n"); 
            } else {
                chgsns_active = false;
                printf("Balancing will occur above 4.15V\n"); 
            }

            gpio_put(25, 1);
            sleep_ms(250);
            gpio_put(25, 0);
            sleep_ms(250);
            printf("\nConfigured!\n\n");

            gpio_put(25, 1);
            sleep_ms(250);
            gpio_put(25, 0);
            sleep_ms(250);
            gpio_put(25, 1);
            sleep_ms(250);
            gpio_put(25, 0);
            sleep_ms(250);

            reset = false;
            resumeOtherCore();
        } else {
            // do nothing
        } 

        userInput = getchar();
        if (userInput == 'r' || userInput == 'R'){
            reset = true;
            idleOtherCore();
        }
    }
}