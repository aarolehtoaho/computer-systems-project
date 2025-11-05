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
typedef enum { OK, INVALID_CHARACTER, MESSAGE_FULL} Status ;

// Tasks
static void sensor_task(void *arg);
static void send_message_task(void *arg);
static void receive_message_task(void *arg);
static void actuator_task(void *arg);
// Callbacks
static void btn_fxn(uint gpio, uint32_t eventMask);
// Helper functions
static Status message_append(char character);
static void message_clear();
static char getCharByPosition(float gx, float gy, float gz);

// Global variables
State programState = WRITING_MESSAGE;
char message[MESSAGE_MAX_LENGTH];
uint8_t message_length = 0;
volatile bool spaceButtonIsPressed = false;
volatile bool characterButtonIsPressed = false;

static Status message_append(char character) {
    bool isValidCharacter = character == DOT || character == DASH || character == SPACE;
    if (!isValidCharacter) {
        return INVALID_CHARACTER;
    }
    if (message_length >= MESSAGE_MAX_LENGTH - 1) {
        message[MESSAGE_MAX_LENGTH - 1] = '\n';
        message[MESSAGE_MAX_LENGTH - 2] = ' ';
        message[MESSAGE_MAX_LENGTH - 3] = ' ';
        message_length++;
        return MESSAGE_FULL;
    }
    if (character == SPACE && message_length > 1) {
        bool isThirdSpace = message[message_length - 1] == SPACE && message[message_length - 2] == SPACE;
        if (isThirdSpace) {
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
    //clears every character of the message
    memset(message, 0, sizeof(message));
    message_length = 0;
}

static void btn_fxn(uint gpio, uint32_t eventMask){
    switch (gpio) {
        case BUTTON1:
            spaceButtonIsPressed = true;
            break;
        case BUTTON2:
            characterButtonIsPressed = true;
            break;
        default:
            printf("Unknown gpio");
    }
}

static char getCharByPosition(float gx, float gy, float gz) {
    // TODO: improve the logic based on measurements. Current logic is not accurate enough

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

    //values read by the ICM42670 sensor
    float ax, ay, az, gx, gy, gz, t;

    for(;;){
        if (programState == WRITING_MESSAGE) {
            if (characterButtonIsPressed) {
                // Adds a character in message based on device position
                int readStatus = ICM42670_read_sensor_data(&ax, &ay, &az, &gx, &gy, &gz, &t);
                if (readStatus == OK) {
                    //printf("Accel: X=%f, Y=%f, Z=%f | Gyro: X=%f, Y=%f, Z=%f| Temp: %2.2f°C\n", ax, ay, az, gx, gy, gz, t);
                    char character = getCharByPosition(gx, gy, gz);
                    //printf("Character: %c", character);
                    Status messageStatus = message_append(character);
                    switch (messageStatus) {
                        case OK:
                            break;
                        case MESSAGE_FULL:
                            programState = MESSAGE_READY;
                            break;
                    }
                    characterButtonIsPressed = false;
                } else {
                    printf("Cannot read sensor");
                    printf("Status: %d", readStatus);
                }
            }

            if (spaceButtonIsPressed) {
                switch (message_append(SPACE)) {
                    case OK:
                        break;
                    case MESSAGE_FULL:
                        programState = MESSAGE_READY;
                        break;
                }
                spaceButtonIsPressed = false;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void send_message_task(void *arg){
    (void)arg;

    uint8_t index = 0;

    // TODO: Sending message does not work properly. While using serialclient on workstation, the message is not displayed.

    for(;;){
        if (programState == MESSAGE_READY) {
            // Sends message stored in a global variable 'message'
            putchar(message[index]);
            index++;
            if (index >= message_length) {
                message_clear();
                index = 0;
                programState = RECEIVING_MESSAGE;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(400));
    }
}

static void receive_message_task(void *arg){
    (void)arg;

    // TODO: Does not work properly. When state changes to RECEIVING_MESSAGE, no characters are
    // written in 'message' and state almost immediately changes to DISPLAY_MESSAGE.

    for(;;){
        if (programState == RECEIVING_MESSAGE) {
            char receivedCharacter = (char)getchar_timeout_us(0);
            if (receivedCharacter != PICO_ERROR_TIMEOUT) {
                message_append(receivedCharacter);
                if (receivedCharacter == '\n') {
                    message[message_length - 1] = '\0';
                    programState = DISPLAY_MESSAGE;                    
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(400));
    }
}

static void actuator_task(void *arg){
    (void)arg;

    init_display();
    init_buzzer();

    int textBeginIndex = 0;
    clear_display();

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
    init_hat_sdk();
    sleep_ms(300); //Wait some time so initialization of USB and hat is done.

    // button initializtions + interruption handelers
    init_button1();
    init_button2();
    gpio_set_irq_enabled_with_callback(BUTTON1, GPIO_IRQ_EDGE_RISE, true, btn_fxn);
    gpio_set_irq_enabled(BUTTON2, GPIO_IRQ_EDGE_RISE, true);

    if (init_ICM42670() == OK) {
        ICM42670_start_with_default_values();
    }

    stdio_usb_init();

    TaskHandle_t hSensorTask = NULL, hSendMessageTask = NULL, hReceiveMessageTask = NULL, hActuatorTask = NULL;

    BaseType_t result = xTaskCreate(sensor_task, "sensor", DEFAULT_STACK_SIZE, NULL, 2, &hSensorTask);
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

