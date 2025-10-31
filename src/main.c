#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <pico/stdlib.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

#include "tkjhat/sdk.h"

// Default stack size for the tasks. It can be reduced to 1024 if task is not using lot of memory.
#define DEFAULT_STACK_SIZE 2048
#define MESSAGE_MAX_LENGTH 256

#define DOT '.'
#define DASH '-'
#define SPACE ' '

typedef enum { WRITING_MESSAGE, MESSAGE_READY, RECEIVING_MESSAGE, DISPLAY_MESSAGE } State ;
State programState = WRITING_MESSAGE;

typedef enum { OK, INVALID_CHARACTER, MESSAGE_FULL} Status ;
char message[MESSAGE_MAX_LENGTH];
uint8_t message_length = 0;

static Status message_append(char character) {
    bool isValidCharacter = character == DOT || character == DASH || character == SPACE;
    if (!isValidCharacter) {
        return INVALID_CHARACTER;
    }
    if (message_length >= MESSAGE_MAX_LENGTH) {
        return MESSAGE_FULL;
    }
    if (character == SPACE && message_length > 1) {
        bool isThridSpace = message[message_length - 1] == SPACE && message[message_length - 2] == SPACE;
        if (isThridSpace) {
            return MESSAGE_FULL;
        }
    }
    message[message_length] = character;
    message_length++;
    return OK;
}
static void message_clear() {
    message_length = 0;
}

volatile bool button1IsPressed = false;
volatile bool button2IsPressed = false;

static void btn_fxn(uint gpio, uint32_t eventMask){
    //changes the global value of button 1 or 2 to true if pressed. These values could be used in sensor_task when capturing the values from actuators.
    //the rising edge should trigger the interrupt immediately when the button is pressed? 
    if (eventMask & GPIO_IRQ_EDGE_RISE){
        if(gpio == BUTTON1) {
            button1IsPressed = true;
        }else if(gpio == BUTTON2){
            button2IsPressed = true;
        }
    }
}

static void sensor_task(void *arg){
    (void)arg;

    init_ICM42670();

    //values read by the ICM42670 sensor
    float ax, ay, az, gx, gy, gz, t;

    for(;;){
        tight_loop_contents(); // comment this out when implemented task
        
        if (programState == WRITING_MESSAGE) {
            // Adds a character in message based on device position when button 1 is pressed and writes a space when button 2 is pressed.

            if (button1IsPressed && ICM42670_read_sensor_data(&ax, &ay, &az, &gx, &gy, &gz, &t) == 0) {
                // TODO: handle sensor position and append corresponding character

                // Placeholder print
                printf("Accel: X=%f, Y=%f, Z=%f | Gyro: X=%f, Y=%f, Z=%f| Temp: %2.2f°C\n", ax, ay, az, gx, gy, gz, t);

                button1IsPressed = false;
            }
            if (button2IsPressed) {
                // TODO: handle full message
                switch (message_append(SPACE)) {
                    case OK:
                        break;
                    case MESSAGE_FULL:
                        programState = MESSAGE_READY;
                        break;
                }
                button2IsPressed = false;
            }

        }
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void send_message_task(void *arg){
    (void)arg;

    for(;;){
        tight_loop_contents(); // comment out

        if (programState == MESSAGE_READY) {
            // Sends message stored in a global variable when writing message is finished with 3 spaces.

        }
        
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void receive_message_task(void *arg){
    (void)arg;

    for(;;){
        tight_loop_contents(); // comment out

        if (programState == RECEIVING_MESSAGE) {
            // Receives message from workstation.
        }
        
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void actuator_task(void *arg){
    (void)arg;

    init_display();
    init_buzzer();
    int textBeginIndex = 0;

    for(;;){
        if (programState == DISPLAY_MESSAGE) {
            clear_display();

            // Max amount of characters displayed is 10. If there is less
            // characters after begin index, the amount is reduced.
            int8_t display_text_length = 10;
            int8_t lastIndex = display_text_length + textBeginIndex;
            if (lastIndex > message_length) {
                int8_t overflow = lastIndex - message_length;
                display_text_length -= overflow;
            }

            char display_text[display_text_length + 1];
            strncpy(display_text, message + textBeginIndex, display_text_length);
            display_text[display_text_length] = '\0';
            write_text(display_text);

            textBeginIndex++;
            bool wholeMessageDisplayed = textBeginIndex >= message_length;
            if (wholeMessageDisplayed) {
                textBeginIndex = 0;
                clear_display();
                message_clear();
                buzzer_play_tone(440, 500);
                programState = WRITING_MESSAGE;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

int main() {
    stdio_init_all();
    // Uncomment this lines if you want to wait till the serial monitor is connected
    /*while (!stdio_usb_connected()){
        sleep_ms(10);
    }*/ 
    init_hat_sdk();
    sleep_ms(300); //Wait some time so initialization of USB and hat is done.

    // button initializtions + interruption handelers
    //(Not sure if this work. Also haven't found anything for GPIO_IRQ_EDGE_RISE)
    init_button1();
    init_button2();
    gpio_set_irq_enabled_with_callback(BUTTON1, GPIO_IRQ_EDGE_RISE, true, btn_fxn);
    gpio_set_irq_enabled_with_callback(BUTTON2, GPIO_IRQ_EDGE_RISE, true, btn_fxn);

    TaskHandle_t hSensorTask = NULL, hSendMessageTask = NULL, hReceiveMessageTask = NULL, hActuatorTask = NULL;
    // Create the tasks with xTaskCreate
    BaseType_t result = xTaskCreate(sensor_task,       // (en) Task function
                "sensor",              // (en) Name of the task 
                DEFAULT_STACK_SIZE, // (en) Size of the stack for this task (in words). Generally 1024 or 2048
                NULL,               // (en) Arguments of the task 
                2,                  // (en) Priority of this task
                &hSensorTask);    // (en) A handle to control the execution of this task

    if(result != pdPASS) {
        printf("Sensor Task creation failed\n");
        return 0;
    }

    result = xTaskCreate(send_message_task, "send_message", DEFAULT_STACK_SIZE, NULL, 2, &hSendMessageTask);
    if(result != pdPASS) {
        printf("Send Message Task creation failed\n");
        return 0;
    }

    result = xTaskCreate(receive_message_task, "receive_message", DEFAULT_STACK_SIZE, NULL, 2, &hReceiveMessageTask);
    if(result != pdPASS) {
        printf("Receive Message Task creation failed\n");
        return 0;
    }

    result = xTaskCreate(actuator_task, "actuator", DEFAULT_STACK_SIZE, NULL, 2, &hActuatorTask);
    if(result != pdPASS) {
        printf("Actuator Task creation failed\n");
        return 0;
    }

    // Start the scheduler (never returns)
    vTaskStartScheduler();

    // Never reach this line.
    return 0;
}

