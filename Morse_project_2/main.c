/* C Standard library */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>

/* XDCtools Header files */
#include <xdc/std.h>
#include <xdc/runtime/System.h>

/* BIOS Header files */
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Task.h>

/* TI-RTOS Header files */
#include <ti/drivers/PIN.h>

/* Board Header files */
#include "Board.h"

//pin header files for button function
#include <ti/drivers/PIN.h>
#include <ti/drivers/pin/PINCC26XX.h>

//uart header file
#include <ti/drivers/UART.h>

//buzzer header file
#include "buzzer.h"

#include <ti/drivers/I2C.h>
#include <ti/drivers/i2c/I2CCC26XX.h>

#include "sensors/mpu9250.h"

// stacks for different tasks
#define STACKSIZE 1024
Char uartStack[STACKSIZE];

Char buzzStack[2 * STACKSIZE];
Char mpuStack[2 * STACKSIZE];

enum state
{
    IDLE = 0, SEND, RECEIVE, MPU, READ_MSG
};
enum state programState = IDLE;

//test global variables
#define BUFF_SIZE 200
#define TIMEOUT 5000
volatile uint32_t lastCalled = 0;
Char buff[BUFF_SIZE];
Char message[BUFF_SIZE];
static int count = 0;

Bool has_message = false;
Bool stateChange = false;
static int rxIndex = 0;
Bool enableButton = false;
Bool ready = false;


// RTOS global variables for handling the pins
static PIN_Handle buttonHandle;
static PIN_State buttonState;

// Pin configurations, with separate configuration for each pin
// The constant BOARD_BUTTON_0 corresponds to one of the buttons
PIN_Config buttonConfig[] = {
Board_BUTTON0 | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
                              PIN_TERMINATE // The configuration table is always terminated with this constant
        };

static PIN_Handle hBuzzer;
static PIN_State sBuzzer;

PIN_Config cBuzzer[] = {
        Board_BUZZER | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL
                | PIN_DRVSTR_MAX,
        PIN_TERMINATE };

// MPU power pin global variables
static PIN_Handle hMpuPin;
static PIN_State  MpuPinState;

// MPU power pin
static PIN_Config MpuPinConfig[] = {
    Board_MPU_POWER  | PIN_GPIO_OUTPUT_EN | PIN_GPIO_HIGH | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    PIN_TERMINATE
};

// MPU uses its own I2C interface
static const I2CCC26XX_I2CPinCfg i2cMPUCfg = {
    .pinSDA = Board_I2C0_SDA1,
    .pinSCL = Board_I2C0_SCL1
};


void buttonFxn(PIN_Handle handle, PIN_Id pinId);
void buzzfxn(UArg arg0, UArg arg1);
static void uartFxn(UART_Handle handle, void *rxBuf, size_t len);
void uartRead(UArg arg0, UArg arg1);
void sensorFxn(UArg arg0, UArg arg1);
Void clkFxn(UArg arg0);

/*
 *  ======== main ========
 */
int main(void)
{
    //creating clcok paramters
    Clock_Handle clk;
    Clock_Params clkParams;

    /* Creating task parameters uart*/
    Task_Params uartParams;
    Task_Handle uartHandle;



    Task_Handle buzztask;
    Task_Params buzzParams;

    Task_Handle mputask;
    Task_Params mpuParams;

    /* Call board init functions */
    Board_initGeneral();

    /* setup Clock params */
      Clock_Params_init(&clkParams);



      /* Create a periodic Clock Instance with period = 5 system time units */
      clkParams.period = 100000;
      clkParams.startFlag = TRUE;

      clk = Clock_create(clkFxn, 500000, &clkParams, NULL);
      Clock_start(clk);
      System_printf("Use Some Time...\n");  /* add */
      System_flush();  /* add */

      lastCalled = Clock_getTicks();  /* add */

    // Enable the pins for use in the program
    buttonHandle = PIN_open(&buttonState, buttonConfig);
    if (!buttonHandle)
    {
        System_abort("Error initializing button pins\n");
    }

    // Set the button pin�s interrupt handler to function buttonFxn
    if (PIN_registerIntCb(buttonHandle, &buttonFxn) != 0)
    {
        System_abort("Error registering button callback function");
    }

    Board_initUART();

    // Initialize uart task main parameters
    Task_Params_init(&uartParams);
    // Define task stack memory
    uartParams.stackSize = STACKSIZE;
    uartParams.stack = &uartStack;
    // Set task priority
    uartParams.priority = 3;
    // Create task
    uartHandle = Task_create(uartRead, &uartParams, NULL);
    if (uartHandle == NULL)
    {
        System_abort("Task create failed");
    }

    // Buzzer
    hBuzzer = PIN_open(&sBuzzer, cBuzzer);
    if (hBuzzer == NULL)
    {
        System_abort("Pin open failed!");
    }

    Task_Params_init(&buzzParams);
    buzzParams.stackSize = 2 * STACKSIZE;
    buzzParams.stack = &buzzStack;
    buzztask = Task_create((Task_FuncPtr) buzzfxn, &buzzParams, NULL);
    buzzParams.priority = 1;
    if (buzztask == NULL)
    {
        System_abort("Task create failed!");
    }

    Board_initI2C();
    // Open MPU power pin
    hMpuPin = PIN_open(&MpuPinState, MpuPinConfig);
    if (hMpuPin == NULL) {
        System_abort("Pin open failed!");
    }

    Task_Params_init(&mpuParams);
    mpuParams.stackSize = 2 * STACKSIZE;
    mpuParams.stack = &mpuStack;
    mputask = Task_create((Task_FuncPtr)sensorFxn, &mpuParams, NULL);
    mpuParams.priority = 2;
    if (mputask == NULL) {
        System_abort("Task create failed!");
    }




    /* Start BIOS */
    BIOS_start();

    return (0);
}

Void clkFxn(UArg arg0)
{


    if(has_message && Clock_getTicks() - lastCalled > TIMEOUT && programState == IDLE )
            {
                programState = RECEIVE;

                System_printf("%s", buff);
                System_flush();
                Task_sleep(1000000 / Clock_tickPeriod);
            }
}


void buttonFxn(PIN_Handle handle, PIN_Id pinId)
{
    if(enableButton){
        if (programState == RECEIVE)
            {
                programState = READ_MSG;
            } else if(programState == IDLE) {
                stateChange = true;
                //programState = MPU;
                ready = true;

            } else if (programState == MPU) {
                stateChange = true;
                programState = IDLE;

            }
    }





}



Void sensorFxn(UArg arg0, UArg arg1) {

    float ax, ay, az, gx, gy, gz;

    I2C_Handle i2cMPU; // Own i2c-interface for MPU9250 sensor
    I2C_Params i2cMPUParams;

    I2C_Params_init(&i2cMPUParams);
    i2cMPUParams.bitRate = I2C_400kHz;
    // Note the different configuration below
    i2cMPUParams.custom = (uintptr_t)&i2cMPUCfg;

    // MPU power on
    PIN_setOutputValue(hMpuPin,Board_MPU_POWER, Board_MPU_POWER_ON);

    // Wait 100ms for the MPU sensor to power up
    Task_sleep(100000 / Clock_tickPeriod);
    System_printf("MPU9250: Power ON\n");
    System_flush();

    // MPU open i2c
    i2cMPU = I2C_open(Board_I2C, &i2cMPUParams);
    if (i2cMPU == NULL) {
        System_abort("Error Initializing I2CMPU\n");
    }

    // MPU setup and calibration
    System_printf("MPU9250: Setup and calibration...\n");
    System_flush();

    mpu9250_setup(&i2cMPU);

    System_printf("MPU9250: Setup and calibration OK\n");
    enableButton = true;
    //System_printf("\nx, y, z \n");
    System_flush();
    int end_msg = 0;

    // Loop forever
    while (1)
        {
        if(programState == MPU)
        {
        // MPU ask data
        mpu9250_get_data(&i2cMPU, &ax, &ay, &az, &gx, &gy, &gz);

        if (gx > 60 ) {
            sprintf(message[count], " \r\n\0", 200);
            count += 4;
            end_msg ++;

        } else if ((abs(ax) - abs(ay) > 0.1) && !(ax > 1 && ay > 1 )) {

            sprintf(message[count], ".\r\n\0", 200);
            count += 4;
            end_msg = 0;


       } else if ((abs(ay) - abs(ax) > 0.5) && !(ax > 1 && ay > 1 )) {

           sprintf(message[count], "-\r\n\0", 200);
           count += 4;
           end_msg = 0;

       }
        if (end_msg == 2)
        {
            programState = SEND;
        }

     /*  if((abs(ax) >=0.05 || abs(ay)>=0.05 ) && 0)

            {

            char xstr[20], ystr[20], zstr[20];
            sprintf(xstr, "%.2f", gx);
            sprintf(ystr, "%.2f", gy);
            sprintf(zstr, "%.2f", gz);

            System_printf("%s, %s, %s\n", xstr, ystr, zstr);
            System_flush();

            }*/
        // Sleep 200ms
        }
        Task_sleep(200000 / Clock_tickPeriod);
        }

}

void buzzfxn(UArg Overflowarg0, UArg arg1)
{

    System_printf("buzz on \r\n");
    System_flush();

    while (1)
    {
        if (stateChange){
            if(programState == MPU) {
                buzzerOpen(hBuzzer);
                buzzerSetFrequency(1200);
                Task_sleep(150000 / Clock_tickPeriod);
                buzzerClose();
                Task_sleep(50000 / Clock_tickPeriod);
                buzzerOpen(hBuzzer);
                buzzerSetFrequency(1400);
                Task_sleep(150000 / Clock_tickPeriod);
                buzzerClose();
                System_printf("MPU\n");
                System_flush();

            } else if (programState == IDLE) {
                buzzerOpen(hBuzzer);
                buzzerSetFrequency(800);
                Task_sleep(100000 / Clock_tickPeriod);
                buzzerClose();
                Task_sleep(50000 / Clock_tickPeriod);
                buzzerOpen(hBuzzer);
                buzzerSetFrequency(900);
                Task_sleep(100000 / Clock_tickPeriod);
                buzzerClose();
                Task_sleep(50000 / Clock_tickPeriod);
                buzzerOpen(hBuzzer);
                buzzerSetFrequency(1000);
                Task_sleep(200000 / Clock_tickPeriod);
                buzzerClose();
                System_printf("IDLE\n");
                System_flush();
            }
            stateChange = false;
        }
        if (programState == RECEIVE)
        {
            buzzerOpen(hBuzzer);
            buzzerSetFrequency(440);  // A4
            Task_sleep(150000 / Clock_tickPeriod);  // Short beep
            buzzerClose();
            Task_sleep(50000 / Clock_tickPeriod);   // Short pause

            buzzerOpen(hBuzzer);
            buzzerSetFrequency(494);  // B4
            Task_sleep(150000 / Clock_tickPeriod);  // Short beep
            buzzerClose();
            Task_sleep(50000 / Clock_tickPeriod);

            buzzerOpen(hBuzzer);
            buzzerSetFrequency(523);  // C5
            Task_sleep(200000 / Clock_tickPeriod);  // Slightly longer beep
            buzzerClose();
            Task_sleep(100000 / Clock_tickPeriod);

            // Melody Part 2: Descending pattern
            buzzerOpen(hBuzzer);
            buzzerSetFrequency(494);  // B4
            Task_sleep(150000 / Clock_tickPeriod);
            buzzerClose();
            Task_sleep(50000 / Clock_tickPeriod);

            buzzerOpen(hBuzzer);
            buzzerSetFrequency(440);  // A4
            Task_sleep(150000 / Clock_tickPeriod);
            buzzerClose();
            Task_sleep(50000 / Clock_tickPeriod);

            buzzerOpen(hBuzzer);
            buzzerSetFrequency(392);  // G4
            Task_sleep(300000 / Clock_tickPeriod);  // Long beep
            buzzerClose();
            Task_sleep(200000 / Clock_tickPeriod);

            // Melody Part 3: Alternating quick beeps
            int i;
            for (i = 0; i < 3; i++)
            {
                buzzerOpen(hBuzzer);
                buzzerSetFrequency(523);  // C5
                Task_sleep(100000 / Clock_tickPeriod);
                buzzerClose();
                Task_sleep(100000 / Clock_tickPeriod);

                System_printf("space \n");
      buzzerOpen(hBuzzer);
                buzzerSetFrequency(392);  // G4
                Task_sleep(100000 / Clock_tickPeriod);
                buzzerClose();
                Task_sleep(100000 / Clock_tickPeriod);
            }

            // Melody Part 4: Sustained tone with fade-out effect
            buzzerOpen(hBuzzer);
            buzzerSetFrequency(440);  // A4
            Task_sleep(500000 / Clock_tickPeriod);  // Long, sustained beep
            buzzerClose();
            Task_sleep(200000 / Clock_tickPeriod);

        } else if(programState == READ_MSG) {
            Task_sleep(500000 / Clock_tickPeriod);
            int i = 0;

            while (buff[i] != '\0') {
                if (buff[i] == '.') {
                    buzzerOpen(hBuzzer);
                    buzzerSetFrequency(1000);
                    Task_sleep(100000 / Clock_tickPeriod);
                    buzzerClose();
                    Task_sleep(500000 / Clock_tickPeriod);
                } else if (buff[i] == '-') {
                    buzzerOpen(hBuzzer);
                    buzzerSetFrequency(1000);
                    Task_sleep(500000 / Clock_tickPeriod);
                    buzzerClose();
                    Task_sleep(500000 / Clock_tickPeriod);
                } else if (buff[i] == ' ') {
                    Task_sleep(1000000 / Clock_tickPeriod);
                }
                i++;
            }
            rxIndex = 0;
            has_message = false;
            memset(buff, '\0', BUFF_SIZE);
            programState = IDLE;
            System_printf("Played messgae\n");
            System_flush();
        }
        Task_sleep(950000 / Clock_tickPeriod);
    }

}

// Handler function
static void uartFxn(UART_Handle handle, void *rxBuf, size_t len) {

    lastCalled = Clock_getTicks();

    char* rxData = (char*)rxBuf;
    int i;
    for (i =0; i < len; i++){
        if(rxIndex < BUFF_SIZE - 1) {
            buff[rxIndex++] = rxData[i];

            if(rxData[i] == '\n') {
                has_message = true;
                //System_printf("Received:");
                //System_flush();
                break;
            }
        } else {
            rxIndex = 0;
            System_printf("Overflow");
            System_flush();
        }
    }

   UART_read(handle, rxBuf, 3);
}
void uartRead(UArg arg0, UArg arg1)
{

    // UART library settings
    UART_Handle uart;
    UART_Params uartParams;
    Char readChar[3];

    // Initialize serial communication
    UART_Params_init(&uartParams);
    uartParams.readMode = UART_MODE_CALLBACK; // Interrupt-based reception
    uartParams.readCallback = &uartFxn; // Handler function
    uartParams.readDataMode = UART_DATA_TEXT;
    uartParams.writeDataMode = UART_DATA_TEXT;
    uartParams.baudRate = 9600; // nopeus 9600baud
    uartParams.dataLength = UART_LEN_8; // 8
    uartParams.parityType = UART_PAR_NONE; // n
    uartParams.stopBits = UART_STOP_ONE; // 1

    // Open connection to device's serial port defined by Board_UART0
    uart = UART_open(Board_UART0, &uartParams);
    if (uart == NULL)
    {
        System_abort("Error opening the UARTread");
    }

    UART_write(uart, "uart check\r\n", strlen("uart check\r\n"));

    // Start waiting for data

    UART_read(uart, readChar, 3);

    //Char echo_msg[4] = ".\r\n\0";
    //Char echo_space[4] =  " \r\n\0";
    int i;
    // Infinite loop
        while(1)
        {
            if(ready)
            {
            // Send the string back
            //UART_write(uart, echo_msg, strlen(echo_msg));
            for(i = 0; i < 2;i++)
            {
                UART_write(uart, message, strlen(message));
            }
            }
            Task_sleep(1000000 / Clock_tickPeriod);

        }
}

