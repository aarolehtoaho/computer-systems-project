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

typedef enum { WRITING_MESSAGE, MESSAGE_READY, RECEIVING_MESSAGE, DISPLAY_MESSAGE } State ;
typedef enum { DOT = '.', DASH = '-', SPACE = ' ' } Character ;

State programState = WRITING_MESSAGE;

Character message[MESSAGE_MAX_LENGTH];
uint8_t message_length = 0;

//static void message_append(Character c) {}
static void message_clear() {
    message_length = 0;
}

static void btn_fxn(uint gpio, uint32_t eventMask){

}

static void sensor_task(void *arg){
    (void)arg;

    for(;;){
        tight_loop_contents(); // comment this out when implemented task

        if (programState == WRITING_MESSAGE) {
            // Adds a character in message based on device position when button 1 is pressed and writes a space when button 2 is pressed.
        }
        
        vTaskDelay(pdMS_TO_TICKS(2000));
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
    gpio_set_irq_enable_with_callback(BUTTON1, GPIO_IRQ_EDGE_RISE, true, btn_fxn);
    gpio_set_irq_enable_with_callback(BUTTON2, GPIO_IRQ_EDGE_RISE, true, btn_fxn);

    TaskHandle_t hSensorTask, hSendMessageTask, hReceiveMessageTask, hActuatorTask = NULL;
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

