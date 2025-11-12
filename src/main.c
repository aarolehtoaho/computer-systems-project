#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <pico/stdlib.h>
#include <FreeRTOS.h>
#include <task.h>
#include "tkjhat/sdk.h"

// Default stack size for the tasks. It can be reduced to 1024 if task is not using lot of memory.
#define DEFAULT_STACK_SIZE 2048
#define MESSAGE_MAX_LENGTH 256

#define DOT '.'
#define DASH '-'
#define SPACE ' '

#define SKIP_CHAR_CHECK false // Set this to true to send all characters valid or not

typedef enum { WRITING_MESSAGE, MESSAGE_READY, RECEIVING_MESSAGE, DISPLAY_MESSAGE } State ;
typedef enum { OK, INVALID_CHARACTER, MESSAGE_FULL} MessageStatus ;

static const char *morse_codes[] = {
    // letters from A to Å
    ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..",
    ".---", "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.",
    "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--..",".-.-","---.",".--.-",NULL};

// Tasks
static void sensor_task(void *arg);
static void send_message_task(void *arg);
static void receive_message_task(void *arg);
static void actuator_task(void *arg);
// Callbacks
static void btn_fxn(uint gpio, uint32_t eventMask);
// Helper functions
static MessageStatus message_append(char character);
static void message_clear();
static char get_char_by_position(float gx, float gy, float gz);
static void send_message_by_characters(int *index);
static bool check_last_characters();
static void clear_invalid_characters();
// Util
static void debug_print(char *text);

// Global variables
State programState = WRITING_MESSAGE;
char message[MESSAGE_MAX_LENGTH];
uint8_t messageLength = 0;
volatile bool spaceButtonIsPressed = false;
volatile bool characterButtonIsPressed = false;

static MessageStatus message_append(char character) {
    // Handles writing to the message. Caller should handle the possible return statuses
    bool isValidCharacter = character == DOT || character == DASH || character == SPACE;
    if (!isValidCharacter) {
        return INVALID_CHARACTER;
    }
    if (messageLength >= MESSAGE_MAX_LENGTH - 1) {
        message[MESSAGE_MAX_LENGTH - 1] = '\n';
        message[MESSAGE_MAX_LENGTH - 2] = ' ';
        message[MESSAGE_MAX_LENGTH - 3] = ' ';
        messageLength++;
        return MESSAGE_FULL;
    }
    if (character == SPACE && messageLength > 1) {
        bool isThirdSpace = message[messageLength - 1] == SPACE && message[messageLength - 2] == SPACE;
        if (isThirdSpace) {
            message[messageLength] = '\n';
            messageLength++;
            return MESSAGE_FULL;
        }
    }
    message[messageLength++] = character;
    return OK;
}
static void message_clear() {
    //clears every character of the message
    memset(message, 0, sizeof(message));
    messageLength = 0;
}

static void btn_fxn(uint gpio, uint32_t eventMask){
    // Sometimes space button does not work properly
    switch (gpio) {
        case BUTTON1:
            spaceButtonIsPressed = true;
            break;
        case BUTTON2:
            characterButtonIsPressed = true;
            break;
        default:
            debug_print("Unknown gpio");
    }
}

/*
See gyro_measurements.ods for measurements when sensor is on table or in another position.
it is possible to use sum (gx + gy + gz), average ((gx + gy + gz) / 3) or product (gx * gy * gz).
Using product seems the most accurate method.
*/
static char get_char_by_position(float gx, float gy, float gz) {
    float gyroPositionProduct = gx * gy * gz;
    float minProductOnTable = -1;
    float maxProductOnTable = 1;
    bool deviceOnTable = gyroPositionProduct > minProductOnTable && gyroPositionProduct < maxProductOnTable;
    return deviceOnTable ? DOT : DASH;
}

/*
The task reads ICM42670 sensor data and adds corresponding character to the message
based on gyro values. State is changed to MESSAGE_READY when the message is finished    
*/
static void sensor_task(void *arg) {
    (void)arg;
    message_clear();

    //values read by the ICM42670 sensor
    float ax, ay, az, gx, gy, gz, t;

    for(;;){
        if (programState == WRITING_MESSAGE) {
            if (messageLength == 0) {
                // Serial client always displays ?s if there is only one word. 
                // Adding constant text 'ms ' to the message so ? are not printed.
                message_append(DASH);
                message_append(DASH);
                message_append(SPACE);
                message_append(DOT);
                message_append(DOT);
                message_append(DOT);
                message_append(SPACE);
                message_append(SPACE);
            }
            if (characterButtonIsPressed) {
                int readStatus = ICM42670_read_sensor_data(&ax, &ay, &az, &gx, &gy, &gz, &t);
                if (readStatus == OK) {
                    /*
                    char debugText[9];
                    sprintf(debugText, "%f,%f,%f", gx, gy, gz);
                    debug_print(debugText);
                    */

                    char characterToAdd = get_char_by_position(gx, gy, gz);
                    switch (characterToAdd) {
                        case DOT:
                            buzzer_play_tone(440, 100);
                            break;
                        case DASH:
                            buzzer_play_tone(350, 150);
                            break;
                    }

                    MessageStatus status = message_append(characterToAdd);
                    switch (status) {
                        case OK:
                            clear_display();
                            char addedCharacter[2];
                            sprintf(addedCharacter, "%c", characterToAdd);
                            write_text(addedCharacter);                            
                            break;
                        case MESSAGE_FULL:
                            programState = MESSAGE_READY;
                            write_text("sending");
                            break;
                    }
                    characterButtonIsPressed = false;
                } else {
                    debug_print("Cannot read sensor");
                }
            }
            if (spaceButtonIsPressed) {
                buzzer_play_tone(250, 100);
                clear_display(); 
                switch (message_append(SPACE)) {
                    case OK:
                        bool validCharacterCombination = check_last_characters();
                        if (!validCharacterCombination && !SKIP_CHAR_CHECK) {
                            clear_invalid_characters();
                        }
                        break;
                    case MESSAGE_FULL:
                        programState = MESSAGE_READY;
                        write_text("sending");
                        break;
                }
                spaceButtonIsPressed = false;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(400));
    }
}

static bool check_last_characters() {
    // This function is called after appending a SPACE and checks
    // if last character combination was valid or not.

    int end_of_char = messageLength - 1;
    if(messageLength == 0 || end_of_char < 0){  //this shouldn't happen though
        return true;
    }
    //finds the starting point of the character (first morse symbol after space)
    int start_of_char = end_of_char;
    while (start_of_char > 0 && message[start_of_char -1] != SPACE){
        start_of_char--;
    }
    int length_of_char = end_of_char - start_of_char;
    //if the length of character symbols is more than 5 (alphabet letters have at most 5 symbols in morse code)
    bool overMaxLenght = length_of_char > 5;
    if (overMaxLenght) {
        return false;
    }
     //this happens for example when second space is pressed.
    if (length_of_char <= 0){ 
        return true;
    }
    //then we take the wanted symbol sequence from the whole message.
    char charSequence[6];
    strncpy(charSequence, &message[start_of_char], length_of_char);
    charSequence[length_of_char] = '\0';

    //checks whether the symbol sequence is valid (returns true) or not (returns false)
    for(int i=0; morse_codes[i] != NULL; i++){
        if(strcmp(charSequence, morse_codes[i]) == 0){
            return true;
        }
    }
    return false;
}

static void clear_invalid_characters() {
    // Removes last symbol combination (invalid character) from message
    messageLength--;
    while (message[messageLength - 1] != SPACE && messageLength >= 0) {
        messageLength--;
    }
    if (messageLength < 0) {
        message_clear();
    }
}

static void send_message_task(void *arg){
    //sends the ready message and goes back to receiving
    (void)arg;

    for(;;){
        if (programState == MESSAGE_READY) {
            // Checks wheter the received message is valid
            if(message != NULL && messageLength > 2) {
                //send_message_by_characters(index);
                puts(message);
                message_clear();
            }
            /* else {
                programState = RECEIVING_MESSAGE;
            }*/

            //shouldn't the state change be here?
            programState = RECEIVING_MESSAGE;
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

//should we remove this or keep it here if not used?
/*static void send_message_by_characters(int *index) {
    // Sending the whole message works better
    putchar(message[*index++]);
    //fflush(stdout); //clears output buffer
    if (*index >= messageLength) {
        fflush(stdout);
        message_clear();
        *index = 0;
        programState = RECEIVING_MESSAGE;
        debug_print("Receiving message from workstation");
    }   
}*/

static void receive_message_task(void *arg){
    (void)arg;

    for(;;){
        if (programState == RECEIVING_MESSAGE) {
            int delayMs100 = 100000;
            int receivedCharacter = getchar_timeout_us(delayMs100);
            if (receivedCharacter != PICO_ERROR_TIMEOUT) {
                char receivedChar = (char)receivedCharacter;
                message_append(receivedChar);
                if (receivedChar == '\n') {
                    message[messageLength] = '\0';
                    programState = DISPLAY_MESSAGE;
                    debug_print("Displaying message on lcd screen");                  
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}


/* This task handles the actuators of the device (buzzer and LCD).
   It starts by first clearing the LCD-screen and then checks the programState.
   When a any button is pressed, it plays a tone associated with that button and
   displays the correct symbol (DOT or DASH) of the screen.

   When the program receives a message from serial client, it displays the message in morse code
   accompanied by the corresponding buzzer sounds. 
   
   When the message is finished, the led light turns on and the buzzer plays a short sound effect.
   It also displays a checmark on the LCD-screen, wich remains lit until a new message is sent.
*/


static void actuator_task(void *arg){
    (void)arg;

    int textBeginIndex = 0;
    clear_display();

    for(;;){
        if (programState == DISPLAY_MESSAGE) {
            clear_display();

            // Max amount of characters displayed is 10. If there is less
            // characters after begin index, the amount is reduced.
            int8_t displayTextLength = 10;
            int8_t lastIndex = displayTextLength + textBeginIndex;
            if (lastIndex > messageLength) {
                int8_t overflow = lastIndex - messageLength;
                displayTextLength -= overflow;
            }

            // Write text from the message between current range
            char display_text[displayTextLength + 1];
            strncpy(display_text, message + textBeginIndex, displayTextLength);
            display_text[displayTextLength] = '\0';
            write_text(display_text);

            // Play sound for the first letter written
            char firstLetter = display_text[0];

            switch (firstLetter) {
                case DOT:
                    buzzer_play_tone(440, 100);
                    break;
                case DASH:
                    buzzer_play_tone(350, 150);
                    break;
                case SPACE:
                    break;
                case '\n':
                    break;
                default:
                    char debugText[32];
                    sprintf(debugText, "Invalid character: %c (int: %d)", firstLetter, (int)firstLetter);
                    debug_print(debugText);  
                    break;
            }

            textBeginIndex++;
            bool wholeMessageDisplayed = textBeginIndex >= messageLength;
            if (wholeMessageDisplayed) {
                textBeginIndex = 0;
                clear_display();
                message_clear();
                programState = WRITING_MESSAGE;
                debug_print("Message displayed");

                // plays the "Zelda item get" sound effect.
                // Got the correct tones from ChatGPT with prompt: 
                // "Give tones raging from 200 to 700 so that I can play Zelda item get sound effect".
                gpio_put(RED_LED_PIN, true);
                buzzer_play_tone(200, 100);
                buzzer_play_tone(360, 100);
                buzzer_play_tone(320, 100);
                buzzer_play_tone(400, 100);
                buzzer_play_tone(480, 100);
                buzzer_play_tone(560, 100); 
                buzzer_play_tone(640, 100); 
                buzzer_play_tone(700, 200); 
                gpio_put(RED_LED_PIN, false);

                // Draw a checkmark
                draw_line(30, 30, 50, 45);
                draw_line(50, 45, 80, 10);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void debug_print(char *text) {
    // Serial client does not decode text between __
    printf("__%s__", text);
    fflush(stdout);
}

// Initializes everything and runs the whole program.
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

    init_led();
    init_display();
    init_buzzer();

    TaskHandle_t hSensorTask = NULL, hSendMessageTask = NULL, hReceiveMessageTask = NULL, hActuatorTask = NULL;

    BaseType_t result = xTaskCreate(sensor_task, "sensor", DEFAULT_STACK_SIZE, NULL, 2, &hSensorTask);
    if(result != pdPASS) {
        debug_print("Sensor Task creation failed\n");
        return 0;
    }

    result = xTaskCreate(send_message_task, "send_message", DEFAULT_STACK_SIZE, NULL, 2, &hSendMessageTask);
    if(result != pdPASS) {
        debug_print("Send Message Task creation failed\n");
        return 0;
    }

    result = xTaskCreate(receive_message_task, "receive_message", DEFAULT_STACK_SIZE, NULL, 2, &hReceiveMessageTask);
    if(result != pdPASS) {
        debug_print("Receive Message Task creation failed\n");
        return 0;
    }

    result = xTaskCreate(actuator_task, "actuator", DEFAULT_STACK_SIZE, NULL, 2, &hActuatorTask);
    if(result != pdPASS) {
        debug_print("Actuator Task creation failed\n");
        return 0;
    }

    // Start the scheduler (never returns)
    vTaskStartScheduler();

    // Never reach this line.
    return 0;
}

 
