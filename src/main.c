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
    if (message_length >= MESSAGE_MAX_LENGTH - 1) {
        message[MESSAGE_MAX_LENGTH - 1] = '\n';
        message_length++;
        return MESSAGE_FULL;
    }
    if (character == SPACE && message_length > 1) {
        bool isThridSpace = message[message_length - 1] == SPACE && message[message_length - 2] == SPACE;
        if (isThridSpace) {
            message[message_length] = '\n';
            message_length++;
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
    printf("btn_fxn: gpio=%u eventMask=0x%lu\n", gpio, eventMask);

    if (gpio == BUTTON1) {
        button1IsPressed = true;
    }
    if (gpio == BUTTON2) {
        button2IsPressed = true;
    }
}

static char getCharByPosition(float gx, float gy, float gz) {
    // Initial logic for assigning character for a position.
    // With taken measurements from the gyro, the sum of x, y and z was
    // between range of -0.5 to 0. The range can be adjusted or
    // the logic can be changed completely.
    float gyroPositionSum = gx + gy + gz;
    float dotMinSum = -0.5;
    float dotMaxSum = 0;
    if (gyroPositionSum > dotMinSum && gyroPositionSum < dotMaxSum) {
        return DOT;
    }
    return DASH;
}

static void sensor_task(void *arg){
    (void)arg;

    if (init_ICM42670() == OK) {
        ICM42670_start_with_default_values();
    } else {
        printf("Failed to initialize ICM42670");
    }

    //values read by the ICM42670 sensor
    float ax, ay, az, gx, gy, gz, t;

    for(;;){
        if (programState == WRITING_MESSAGE) {
            // Adds a character in message based on device position when button 1 is pressed and writes a space when button 2 is pressed.

            if (button2IsPressed) {
                int readStatus = ICM42670_read_sensor_data(&ax, &ay, &az, &gx, &gy, &gz, &t);
                if (readStatus == OK) {
                    //printf("Accel: X=%f, Y=%f, Z=%f | Gyro: X=%f, Y=%f, Z=%f| Temp: %2.2f°C\n", ax, ay, az, gx, gy, gz, t);
                    char character = getCharByPosition(gx, gy, gz);
                    Status messageStatus = message_append(character);
                    switch (messageStatus) {
                        case OK:
                            break;
                        case MESSAGE_FULL:
                            programState = MESSAGE_READY;
                            printf("Sending message\n"); 
                            break;
                    }

                    button2IsPressed = false;
                } else {
                    printf("Cannot read sensor");
                    printf("Status: %d", readStatus);
                }
            }

            if (button1IsPressed) {
                switch (message_append(SPACE)) {
                    case OK:
                        break;
                    case MESSAGE_FULL:
                        programState = MESSAGE_READY;
                        printf("Sending message\n"); 
                        break;
                }
                button1IsPressed = false;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void send_message_task(void *arg){
    (void)arg;

    uint8_t index = 0;

    for(;;){
        if (programState == MESSAGE_READY) {
            // Sends message stored in a global variable when writing message is finished with 3 spaces.
            putchar(message[index]);
            index++;
            if (index >= message_length) {
                message_clear();
                index = 0;
                programState = RECEIVING_MESSAGE;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void receive_message_task(void *arg){
    (void)arg;

    for(;;){
        if (programState == RECEIVING_MESSAGE) {
            // Receives message from workstation.
            char receivedCharacter = (char)getchar_timeout_us(0);
            if (receivedCharacter != PICO_ERROR_TIMEOUT) {
                message_append(receivedCharacter);
                if (receivedCharacter == '\n') {
                    message[message_length - 1] = '\0';
                    programState = DISPLAY_MESSAGE;                    
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(200));
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
    // TODO: test button 1 if it works
    init_button1();
    gpio_pull_down(SW1_PIN);
    gpio_set_irq_enabled_with_callback(BUTTON1, GPIO_IRQ_EDGE_RISE, true, btn_fxn);
    init_button2();
    gpio_set_irq_enabled_with_callback(BUTTON2, GPIO_IRQ_EDGE_RISE, true, btn_fxn);

    stdio_usb_init();

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

