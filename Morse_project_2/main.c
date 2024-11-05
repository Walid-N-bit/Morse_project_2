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
    IDLE = 0, SEND, RECEIVE
};
enum state programState = IDLE;

//test global variables
static uint8_t button = 0;
char test[20];

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
void uartSend(UArg arg0, UArg arg1);
void sensorFxn(UArg arg0, UArg arg1);
/*
 *  ======== main ========
 */
int main(void)
{
    /* Creating task parameters uart*/
    Task_Params uartParams;
    Task_Handle uartHandle;

    Task_Handle buzztask;
    Task_Params buzzParams;

    Task_Handle mputask;
    Task_Params mpuParams;

    /* Call board init functions */
    Board_initGeneral();

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
    uartParams.priority = 2;
    // Create task
    uartHandle = Task_create(uartSend, &uartParams, NULL);
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


    System_printf("Hello world\n");
    System_flush();

    /* Start BIOS */
    BIOS_start();

    return (0);
}

void buttonFxn(PIN_Handle handle, PIN_Id pinId)
{

    button++;
    //sprintf(test, "uart read check\r\n", strlen("uart read check\r\n"));
    if (programState == IDLE)
    {
        programState = SEND;
    }
    else if (programState == SEND)
    {
        programState = IDLE;
    }
    System_printf("button test %d \r\n", button);
    System_printf("program state: %d \r\n", programState);
    System_flush();
}

void buzzfxn(UArg arg0, UArg arg1)
{

    System_printf("buzz on \r\n");
    System_flush();
    while (0)
    {
        if (programState != IDLE)
        {
            System_printf("beep \r\n");
            System_flush();
            buzzerOpen(hBuzzer);
            buzzerSetFrequency(1000);
            Task_sleep(50000 / Clock_tickPeriod);
            buzzerClose();
            Task_sleep(950000 / Clock_tickPeriod);
        }
    }
}

void uartSend(UArg arg0, UArg arg1)
{

    //Char buff[2000];
    Char letter[5];
    // UART library settings
    UART_Handle uart;
    UART_Params uartParams;

    // Initialize serial communication
    UART_Params_init(&uartParams);
    uartParams.writeDataMode = UART_DATA_TEXT;
    uartParams.readDataMode = UART_DATA_TEXT;
    uartParams.readEcho = UART_ECHO_OFF;
    uartParams.readMode = UART_MODE_BLOCKING;
    uartParams.baudRate = 9600; // nopeus 9600baud
    uartParams.dataLength = UART_LEN_8; // 8
    uartParams.parityType = UART_PAR_NONE; // n
    uartParams.stopBits = UART_STOP_ONE; // 1

    // Open connection to device's serial port defined by Board_UART0
    uart = UART_open(Board_UART0, &uartParams);
    if (uart == NULL)
    {
        System_abort("Error opening the UART");
    }

    UART_write(uart, "uart check\r\n", strlen("uart check\r\n"));

    // Infinite loop
    while (1)
    {

        // Let's send the current state
        //sprintf(buff, "button pressed %1d times\n\r", button);
        UART_read(uart, letter, 5);
        UART_write(uart, letter, 5);

        // Let's sleep for one second
        Task_sleep(1000000 / Clock_tickPeriod);
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
    System_printf("\nx, y, z \n");
    System_flush();

    // Loop forever
    while (0) {

        // MPU ask data
        mpu9250_get_data(&i2cMPU, &ax, &ay, &az, &gx, &gy, &gz);

        if(abs(ax) >=0.05 || abs(ay)>=0.05) {
            char xstr[20], ystr[20], zstr[20];
            sprintf(xstr, "%.2f", ax);
            sprintf(ystr, "%.2f", ay);
            sprintf(zstr, "%.2f", az);

            //System_printf("%s, %s, %s\n", xstr, ystr, zstr);
            //System_flush();

        }
        // Sleep 100ms
        Task_sleep(100000 / Clock_tickPeriod);
    }

    // Program never gets here..
    // MPU close i2c
    // I2C_close(i2cMPU);
    // MPU power off
    // PIN_setOutputValue(hMpuPin,Board_MPU_POWER, Board_MPU_POWER_OFF);
}
