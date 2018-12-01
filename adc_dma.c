#include <stdint.h>
#include <stdbool.h>
#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "driverlib/debug.h"
#include "driverlib/sysctl.h"
#include "driverlib/adc.h"
#include "inc/hw_adc.h"
#include "driverlib/interrupt.h"
#include "driverlib/udma.h"
#include "inc/tm4c123gh6pm.h"
#include "driverlib/timer.h"
#include "driverlib/gpio.h"

uint32_t ui32Period; // the period variable for timer configuration
#define ADC_BUF_SIZE    50 // setting the window increment size by declaring buffer size to 50.

// this is the data structure for the buffer
struct _buffer;
typedef struct _buffer buffer;
typedef buffer* buffer_pointer; // pointer to buffer
struct _buffer{
    uint32_t data_array [ADC_BUF_SIZE];
    uint32_t count;
    buffer_pointer next;
    };

static buffer_pointer current_buffer_pointer;

// Variables needed for the ADC conversion
uint32_t raw_adc[1];
volatile uint32_t ui32TempValueC;
volatile uint32_t ui32TempValueF;

#pragma DATA_ALIGN(ControlTable, 1024)
uint8_t ControlTable[1024];




void Configure_GPIO(void){
    // This function configures the on board leds as output so that they can be used as outputs.
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
        while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF))
        {
        }
    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_3|GPIO_PIN_2|GPIO_PIN_1);


}

// Configuring the ADC
void Configure_ADC(void)
{
        SysCtlClockSet(SYSCTL_SYSDIV_5|SYSCTL_USE_PLL|SYSCTL_OSC_MAIN|SYSCTL_XTAL_16MHZ); // Configure the system clock to be 40MHz
        SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0); //Enable the ADC
        SysCtlDelay(2); //small delay to ensure the above steps are sucessfully completed
        ADCSequenceDisable(ADC0_BASE, 1); //disable ADC0 to set the configuration parameters
        ADCSequenceConfigure(ADC0_BASE, 3, ADC_TRIGGER_TIMER, 0);        // configure the ADC0 to use SS3 and be triggered by using timer
        ADCSequenceStepConfigure(ADC0_BASE,3,0,ADC_CTL_TS|ADC_CTL_IE|ADC_CTL_END); // Configuring the step of SS3
        IntPrioritySet(INT_ADC0SS3, 0x00);       // configure ADC0 SS3 interrupt priority as 0
        IntEnable(INT_ADC0SS3);                 // enabling the interrupt for the adc sequencer This is used for adc triggering
        ADCIntEnableEx(ADC0_BASE, ADC_INT_SS3); // arm interrupt of ADC0 SS3
        ADCSequenceEnable(ADC0_BASE, 3);    // enabling the sequence
}

//Configure DMA
void Configure_DMA(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UDMA); // enabling the module
    uDMAEnable();
    uDMAControlBaseSet(ControlTable); // setting the control table
    uDMAChannelAttributeDisable(UDMA_CHANNEL_ADC3, UDMA_ATTR_ALTSELECT | UDMA_ATTR_HIGH_PRIORITY | UDMA_ATTR_REQMASK);
    uDMAChannelAttributeEnable(UDMA_CHANNEL_ADC3, UDMA_ATTR_USEBURST);    //Set USEBURST: configure the uDMA controller to respond to burst requests only
    uDMAChannelControlSet(UDMA_CHANNEL_ADC3 | UDMA_PRI_SELECT, UDMA_SIZE_32 | UDMA_SRC_INC_NONE | UDMA_DST_INC_32 |UDMA_ARB_1); //Configuring the control parameters of the primary control structure
    uDMAChannelControlSet(UDMA_CHANNEL_ADC3 | UDMA_ALT_SELECT,UDMA_SIZE_32 | UDMA_SRC_INC_NONE | UDMA_DST_INC_32 |UDMA_ARB_2);//     Configure the control parameters for the alternate control structure for

    uDMAChannelTransferSet(UDMA_CHANNEL_ADC3 | UDMA_PRI_SELECT,UDMA_MODE_PINGPONG,(void *)(ADC0_BASE + ADC_O_SSFIFO3),current_buffer_pointer -> data_array, ADC_BUF_SIZE); // setting up initial buffers to transfer primary control structure
    current_buffer_pointer -> count ++; // increment the count variable
    current_buffer_pointer = current_buffer_pointer->next; // circular linked list pointer setup

    /* Set up the transfer parameters for the ADC0 SS3 alternate control
    structure.  The mode is set to ping-pong, the transfer source is the
    ADC0 SS3 FIFO result register, and the destination is the receive "2" buffer.  The
    transfer size is set to match the size of the buffer.
    */
    uDMAChannelTransferSet(UDMA_CHANNEL_ADC3 | UDMA_ALT_SELECT,UDMA_MODE_PINGPONG, (void *)(ADC0_BASE + ADC_O_SSFIFO3),current_buffer_pointer->data_array, ADC_BUF_SIZE);// setting up initial buffers to transfer secondary control structure
    current_buffer_pointer -> count ++;// increment the count variable
    current_buffer_pointer = current_buffer_pointer->next;  // circular linked list pointer setup
    uDMAChannelEnable(UDMA_CHANNEL_ADC3);// Enable the uDMA channel
}

//*****************************************************************************
// The interrupt handler for ADC0 SS3.  This interrupt will occur when a DMA
// transfer is complete using the ADC0 SS3 uDMA channel.
//*****************************************************************************
void adc0_ss3_handler(void)
{
    uint32_t ui32Mode;

    ui32Mode = uDMAChannelModeGet(UDMA_CHANNEL_ADC3 | UDMA_PRI_SELECT);
    // when the primary buffer is full
    if(ui32Mode == UDMA_MODE_STOP)
    {
        GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1, GPIO_PIN_1);
        uDMAChannelTransferSet(UDMA_CHANNEL_ADC3 | UDMA_PRI_SELECT, UDMA_MODE_PINGPONG, (void *)(ADC0_BASE + ADC_O_SSFIFO3),current_buffer_pointer->data_array, ADC_BUF_SIZE);
        uDMAChannelEnable(UDMA_CHANNEL_ADC3); //Re-enable the uDMA channel
        current_buffer_pointer -> count ++; //increment the buffer count
        current_buffer_pointer = current_buffer_pointer->next; // rotate the circular buffer.
    }

    ui32Mode = uDMAChannelModeGet(UDMA_CHANNEL_ADC3 | UDMA_ALT_SELECT);
    // when the alternate structure is full
    if(ui32Mode == UDMA_MODE_STOP)
    {
        GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1, 0);
        uDMAChannelTransferSet(UDMA_CHANNEL_ADC3 | UDMA_ALT_SELECT,UDMA_MODE_PINGPONG,(void *)(ADC0_BASE + ADC_O_SSFIFO3), current_buffer_pointer->data_array, ADC_BUF_SIZE);
        uDMAChannelEnable(UDMA_CHANNEL_ADC3);//Re-enable the uDMA channel
        current_buffer_pointer -> count ++; //increment the buffer count
        current_buffer_pointer = current_buffer_pointer->next; // rotate the circular buffer
    }

}



/*
 In this project we use timer to trigger the ADC in such a way that the sampling frequency is 100 hz
 */
void ConfigureTimer(void){
    // this function configures the timers
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0); // Enable Timer 0
    TimerConfigure(TIMER0_BASE, TIMER_CFG_PERIODIC);// Configure Timer 0 to Periodic Mode
    ui32Period = (SysCtlClockGet()/100 ) ; // this is for 100 hertz
    TimerLoadSet(TIMER0_BASE, TIMER_A, ui32Period -1); // the -1 because the interrupt fires at 0
    TimerControlTrigger(TIMER0_BASE, TIMER_A, 1);
    IntMasterEnable(); // enable interrupts globally
    TimerEnable(TIMER0_BASE, TIMER_A); // now that the configuration is all done enable the timer.
}


int main(void)
{
        Configure_GPIO();   // configure the GPIOs
        Configure_DMA();    // Configure DMA
        Configure_ADC();    // Configure ADC
        IntMasterEnable();  // globally enable interrupt
        ConfigureTimer();   // Configure Timer

        static buffer buffer_1, buffer_2, buffer_3;
        // circular buffer structute.
        buffer_1.next = &buffer_2;
        buffer_2.next = &buffer_3;
        buffer_3.next = &buffer_1;
        current_buffer_pointer = &(buffer_1);
        while(1)
        {}
/*            if(buffer_1_count - old_1_count > 0 ){
                // this indecates that buffer a has just been incremented
                SysCtlDelay(SysCtlClockGet()/4/3); // quarter second delay
                //    ui32TempValueC = (1475 - ((2475 * buffer_1[0])) / 4096)/10;
                //    ui32TempValueF = ((ui32TempValueC * 9) + 160) / 5;

                old_1_count = buffer_1_count;


            }

            else if(buffer_2_count - old_2_count > 0){
                // this indecates that b has just been triggered
                SysCtlDelay(SysCtlClockGet()/4/3); // quarter second delay
                 //   ui32TempValueC = (1475 - ((2475 * buffer_2[0])) / 4096)/10;
                 //   ui32TempValueF = ((ui32TempValueC * 9) + 160) / 5;

                old_2_count = buffer_2_count;

            }
            else if(buffer_3_count - old_3_count > 0){
                // this means that the bufer c has just been triggered
                SysCtlDelay(SysCtlClockGet()/4/3); // quarter second delay
                //ui32TempValueC = (1475 - ((2475 * buffer_2[0])) / 4096)/10;
                //ui32TempValueF = ((ui32TempValueC * 9) + 160) / 5;

                old_3_count = buffer_3_count;



            }
        */





}
