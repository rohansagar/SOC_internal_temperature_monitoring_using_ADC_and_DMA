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

short buffer_pointer = 3 ; // we initialize the first two buffers so the first pointer is for 3

uint32_t raw_adc[1];
volatile uint32_t ui32TempValueC;
volatile uint32_t ui32TempValueF;

#pragma DATA_ALIGN(ControlTable, 1024)
uint8_t ControlTable[1024];

// setting the window increment size by declaring buffer size to 50.
#define ADC_BUF_SIZE    50

/* Since we need to have a window length of 100 and window increments of 50 we declare
3 buffers each of size 50 samples so that we can sample data without interruption*/
static uint32_t buffer_1[ADC_BUF_SIZE];
static uint32_t buffer_2[ADC_BUF_SIZE];
static uint32_t buffer_3[ADC_BUF_SIZE];

// Counter variables for individual buffers
static uint32_t buffer_1_count = 0;
static uint32_t buffer_2_count = 0;
static uint32_t buffer_3_count = 0;


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
        SysCtlDelay(2); //small delay to ensure the above steps are succenfully completed
        ADCSequenceDisable(ADC0_BASE, 1); //disable ADC0 to set the configuration parameters

        // configure the ADC0 to use SS3 and be triggered by using timer
        // the timer trigger is configured using TimerControlTrigger()

        ADCSequenceConfigure(ADC0_BASE, 3, ADC_TRIGGER_TIMER, 0);
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
    /* configuring the channel attributes:
    - Clearing ALTSETLECT to enable Ping-Pong mode
    - Clear HIGH_PRIORITY to use default priority
    - Clear REQMASK to allow the uDMA controller to recognize requests for this channel
     */
    uDMAChannelAttributeDisable(UDMA_CHANNEL_ADC3,
                                UDMA_ATTR_ALTSELECT |
                                UDMA_ATTR_HIGH_PRIORITY |
                                UDMA_ATTR_REQMASK);

    /*Set USEBURST: configure the uDMA controller to respond to burst requests only*/
        uDMAChannelAttributeEnable(UDMA_CHANNEL_ADC3,
                                UDMA_ATTR_USEBURST);
    /*Configure the control parameters of the channel control structure:
     Configure the control parameters for the primary control structure for
     the ADC0 SS3 channel.  The primary contol structure is used for the "A"
     part of the ping-pong receive.  The transfer data size is 32 bits, the
     source address does not increment since it will be reading from a
     register.  The destination address increment is 32 bits (4 bytes).  The
     arbitration size is set to 1 to match the ADC0 SS3 FIFO size.
    */
    uDMAChannelControlSet(UDMA_CHANNEL_ADC3 | UDMA_PRI_SELECT,
                          UDMA_SIZE_32 | UDMA_SRC_INC_NONE | UDMA_DST_INC_32 |
                          UDMA_ARB_1);
    /*
     Configure the control parameters for the alternate control structure for
    // the ADC0 SS3 channel.  The alternate contol structure is used for the "B"
    // part of the ping-pong receive.  The configuration is identical to the
    // primary/A control structure.
    */
    uDMAChannelControlSet(UDMA_CHANNEL_ADC3 | UDMA_ALT_SELECT,
                          UDMA_SIZE_32 | UDMA_SRC_INC_NONE | UDMA_DST_INC_32 |
                          UDMA_ARB_2);
    /* Configure the transfer parameters of the channel control structure:
     Set up the transfer parameters for the ADC0 SS3 primary control
     structure.  The mode is set to ping-pong, the transfer source is the
     ADC0 SS3 FIFO result register, and the destination is the receive "1" buffer.  The
     transfer size is set to match the size of the buffer. */
    uDMAChannelTransferSet(UDMA_CHANNEL_ADC3 | UDMA_PRI_SELECT,
                               UDMA_MODE_PINGPONG,
                               (void *)(ADC0_BASE + ADC_O_SSFIFO3),
                               buffer_1, ADC_BUF_SIZE);
    buffer_1_count ++;

    /* Set up the transfer parameters for the ADC0 SS3 alternate control
    structure.  The mode is set to ping-pong, the transfer source is the
    ADC0 SS3 FIFO result register, and the destination is the receive "2" buffer.  The
    transfer size is set to match the size of the buffer.
    */
    uDMAChannelTransferSet(UDMA_CHANNEL_ADC3 | UDMA_ALT_SELECT,
                               UDMA_MODE_PINGPONG,
                               (void *)(ADC0_BASE + ADC_O_SSFIFO3),
                               buffer_2, ADC_BUF_SIZE);

    buffer_2_count++;

    // Enable the uDMA channel
    uDMAChannelEnable(UDMA_CHANNEL_ADC3);
}

//*****************************************************************************
//
// The interrupt handler for ADC0 SS3.  This interrupt will occur when a DMA
// transfer is complete using the ADC0 SS3 uDMA channel.  It will also be
// triggered if the peripheral signals an error.  This interrupt handler will
// switch between receive ping-pong buffers 1,2 and 3. This will keep the ADC
// running continuously.
//
//*****************************************************************************
void adc0_ss3_handler(void)
{
        //uint32_t ui32Status;
        uint32_t ui32Mode;

    //ui32Status = ADCIntStatus(ADC0_BASE, 1, false);
        //ADCIntClear(ADC0_BASE, 1);


        //
    // Check the DMA control table to see if the ping-pong "A" transfer is
    // complete.  The "A" transfer uses receive buffer "A", and the primary
    // control structure.
    //
    ui32Mode = uDMAChannelModeGet(UDMA_CHANNEL_ADC3 | UDMA_PRI_SELECT);

    //
    // If the primary control structure indicates stop, that means the "A"
    // receive buffer is done.  The uDMA controller should still be receiving
    // data into the "B" buffer.
    //
    if(ui32Mode == UDMA_MODE_STOP)
    {
        //
        // Increment a counter to indicate data was received into buffer A.  In
        // a real application this would be used to signal the main thread that
        // data was received so the main thread can process the data.
        //



        //
        // Set up the next transfer for the "A" buffer, using the primary
        // control structure.  When the ongoing receive into the "B" buffer is
        // done, the uDMA controller will switch back to this one.  This
        // example re-uses buffer A, but a more sophisticated application could
        // use a rotating set of buffers to increase the amount of time that
        // the main thread has to process the data in the buffer before it is
        // reused.
        //
        GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1, GPIO_PIN_1);

        if(buffer_pointer  == 3)
       {

           uDMAChannelTransferSet(UDMA_CHANNEL_ADC3 | UDMA_PRI_SELECT, UDMA_MODE_PINGPONG, (void *)(ADC0_BASE + ADC_O_SSFIFO3),buffer_3, ADC_BUF_SIZE);
           uDMAChannelEnable(UDMA_CHANNEL_ADC3); //Re-enable the uDMA channel
           buffer_3_count++;
           buffer_pointer = 1;
       }


       else if(buffer_pointer == 2)
       {

           uDMAChannelTransferSet(UDMA_CHANNEL_ADC3 | UDMA_PRI_SELECT,
                                          UDMA_MODE_PINGPONG,
                                          (void *)(ADC0_BASE + ADC_O_SSFIFO3),
                                          buffer_2, ADC_BUF_SIZE);
                           //Re-enable the uDMA channel
                           uDMAChannelEnable(UDMA_CHANNEL_ADC3);

                           buffer_2_count++;

                           buffer_pointer = 3;
       }

       else{

           uDMAChannelTransferSet(UDMA_CHANNEL_ADC3 | UDMA_PRI_SELECT,
                                          UDMA_MODE_PINGPONG,
                                          (void *)(ADC0_BASE + ADC_O_SSFIFO3),
                                          buffer_1, ADC_BUF_SIZE);
                           //Re-enable the uDMA channel
                           uDMAChannelEnable(UDMA_CHANNEL_ADC3);

                           buffer_1_count++;

                           buffer_pointer = 2;
       }

    }

    //
    // Check the DMA control table to see if the ping-pong "B" transfer is
    // complete.  The "B" transfer uses receive buffer "B", and the alternate
    // control structure.
    //
    ui32Mode = uDMAChannelModeGet(UDMA_CHANNEL_ADC3 | UDMA_ALT_SELECT);

    //
    // If the alternate control structure indicates stop, that means the "B"
    // receive buffer is done.  The uDMA controller should still be receiving
    // data into the "A" buffer.
    //
    if(ui32Mode == UDMA_MODE_STOP)
    {
        //
        // Increment a counter to indicate data was received into buffer A.  In
        // a real application this would be used to signal the main thread that
        // data was received so the main thread can process the data.
        //

        //
        // Set up the next transfer for the "B" buffer, using the alternate
        // control structure.  When the ongoing receive into the "A" buffer is
        // done, the uDMA controller will switch back to this one.  This
        // example re-uses buffer B, but a more sophisticated application could
        // use a rotating set of buffers to increase the amount of time that
        // the main thread has to process the data in the buffer before it is
        // reused.
        //
        GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1, 0);

        if(buffer_pointer == 1)
        {
        uDMAChannelTransferSet(UDMA_CHANNEL_ADC3 | UDMA_ALT_SELECT,
                               UDMA_MODE_PINGPONG,
                               (void *)(ADC0_BASE + ADC_O_SSFIFO3),
                               buffer_1, ADC_BUF_SIZE);
                //Re-enable the uDMA channel
                uDMAChannelEnable(UDMA_CHANNEL_ADC3);
                buffer_1_count++;
                buffer_pointer = 2;
            }


        else if(buffer_pointer == 2)
              {
              uDMAChannelTransferSet(UDMA_CHANNEL_ADC3 | UDMA_ALT_SELECT,
                                     UDMA_MODE_PINGPONG,
                                     (void *)(ADC0_BASE + ADC_O_SSFIFO3),
                                     buffer_2, ADC_BUF_SIZE);
                      //Re-enable the uDMA channel
                      uDMAChannelEnable(UDMA_CHANNEL_ADC3);
                      buffer_2_count++;
                      buffer_pointer = 3;
                  }
        else  {
              uDMAChannelTransferSet(UDMA_CHANNEL_ADC3 | UDMA_ALT_SELECT,
                                     UDMA_MODE_PINGPONG,
                                     (void *)(ADC0_BASE + ADC_O_SSFIFO3),
                                     buffer_3, ADC_BUF_SIZE);
                      //Re-enable the uDMA channel
                      uDMAChannelEnable(UDMA_CHANNEL_ADC3);
                      buffer_3_count++;
                      buffer_pointer = 1;
                  }








    }
}




static uint32_t old_1_count = 0;
static uint32_t old_2_count = 0;
static uint32_t old_3_count = 0;

uint32_t ui32Period; // the period variable for timer configuration



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
        Configure_GPIO();
        Configure_DMA();
        Configure_ADC();
        IntMasterEnable();              // globally enable interrupt
        ConfigureTimer();

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
