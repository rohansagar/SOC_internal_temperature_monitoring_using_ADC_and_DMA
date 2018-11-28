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

short buffer_pointer = 3 ; // we initialize the first two buffers so the first pointer is for 3

uint32_t raw_adc[1];
volatile uint32_t ui32TempValueC;
volatile uint32_t ui32TempValueF;





// The control table used by the uDMA controller.  This table must be aligned
// to a 1024 byte boundary.
#if defined(ewarm)
#pragma data_alignment=1024
uint8_t ui8ControlTable[1024];
#elif defined(ccs)
#pragma DATA_ALIGN(ui8ControlTable, 1024)
uint8_t ui8ControlTable[1024];
#else
uint8_t ui8ControlTable[1024] __attribute__ ((aligned(1024)));
#endif

//*****************************************************************************
//
// The pair of ping-pong buffers for the converted results from ADC0 SS3
//
//*****************************************************************************
#define ADC_BUF_SIZE    50 // since our windlw size is 20

/* Since we need to have a window length of 100 and window increments of 20 we declare
  6 buffers each of size 20 samples*/
static uint32_t buffer_1[ADC_BUF_SIZE];
//static uint32_t ui32BufB[ADC_BUF_SIZE];
static uint32_t buffer_2[ADC_BUF_SIZE];
static uint32_t buffer_3[ADC_BUF_SIZE];


//*****************************************************************************
//
// The count of ADC buffers filled, one for each ping-pong buffer.
//
//*****************************************************************************
static uint32_t buffer_1_count = 0;
static uint32_t buffer_2_count = 0;
static uint32_t buffer_3_count = 0;


//ADC0 initializaiton
void ADC0_Init(void)
{

        SysCtlClockSet(SYSCTL_SYSDIV_5|SYSCTL_USE_PLL|SYSCTL_OSC_MAIN|SYSCTL_XTAL_16MHZ); // configure the system clock to be 40MHz
        SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0); //activate the clock of ADC0
        SysCtlDelay(2); //insert a few cycles after enabling the peripheral to allow the clock to be fully activated.

        ADCSequenceDisable(ADC0_BASE, 1); //disable ADC0 before the configuration is complete

        ADCSequenceConfigure(ADC0_BASE, 3, ADC_TRIGGER_PROCESSOR, 0); // configure the adc0 to use ss3
        ADCSequenceStepConfigure(ADC0_BASE,3,0,ADC_CTL_TS|ADC_CTL_IE|ADC_CTL_END);
        IntPrioritySet(INT_ADC0SS3, 0x00);       // configure ADC0 SS3 interrupt priority as 0
        IntEnable(INT_ADC0SS3);                 // enabling the interrupt for the adc sequencer This is used for adc triggering
        ADCIntEnableEx(ADC0_BASE, ADC_INT_SS3);      // arm interrupt of ADC0 SS3
        ADCSequenceEnable(ADC0_BASE, 3);    // enabling the sequence

}

//DMA initializaiton
void DMA_Init(void)
{

        //----- 1. Module Initialization Start -----//
        //
    // Enable the uDMA clock
    //
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UDMA);

        //
    // Enable the uDMA controller by setting the MASTEREN bit of
        // the DMA Configuration (DMA_CFG) register.
    //
    uDMAEnable();

    //
    // Point at the control table to use for channel control structures.
    // Program the location of the channel control table by writing the base address of the table to the
        // DMA Channel Control Base Pointer (DMACTLBASE) register. The base address must be
        // aligned on a 1024-byte boundary.
    uDMAControlBaseSet(ui8ControlTable);

        //----- 1. End -----//


        //----- 2. Configure the Channel Attributes -----//
    // Put the attributes in a known state for the uDMA ADC0 SS1 channel.  These
    // should already be disabled by default.
    // 1. Clear ALTSETLECT: for Ping-Pong mode, the uDMA controller automatically configures
        //                                          the ALT bit, so just use the default value
        // 2. Clear HIGH_PRIORITY: use default priority
        // 3. Clear REQMASK: allow the uDMA controller to recognize requests for this channel
    uDMAChannelAttributeDisable(UDMA_CHANNEL_ADC3,
                                UDMA_ATTR_ALTSELECT |
                                UDMA_ATTR_HIGH_PRIORITY |
                                UDMA_ATTR_REQMASK);

        // 4. Set USEBURST: configure the uDMA controller to respond to burst requests only
        uDMAChannelAttributeEnable(UDMA_CHANNEL_ADC3,
                                UDMA_ATTR_USEBURST);
        //----- 2. End -----//


    //----- 3. Configure the control parameters of the channel control structure-----//
    // Configure the control parameters for the primary control structure for
    // the ADC0 SS1 channel.  The primary contol structure is used for the "A"
    // part of the ping-pong receive.  The transfer data size is 32 bits, the
    // source address does not increment since it will be reading from a
    // register.  The destination address increment is 32 bits (4 bytes).  The
    // arbitration size is set to 2 to match the ADC0 SS1 half FIFO size.
    //
    uDMAChannelControlSet(UDMA_CHANNEL_ADC3 | UDMA_PRI_SELECT,
                          UDMA_SIZE_32 | UDMA_SRC_INC_NONE | UDMA_DST_INC_32 |
                          UDMA_ARB_2);

    //
    // Configure the control parameters for the alternate control structure for
    // the ADC0 SS1 channel.  The alternate contol structure is used for the "B"
    // part of the ping-pong receive.  The configuration is identical to the
    // primary/A control structure.
    //
    uDMAChannelControlSet(UDMA_CHANNEL_ADC3 | UDMA_ALT_SELECT,
                          UDMA_SIZE_32 | UDMA_SRC_INC_NONE | UDMA_DST_INC_32 |
                          UDMA_ARB_2);
        //----- 3. End -----//

        //----- 4. Configure the transfer parameters of the channel control structure-----//
    // Set up the transfer parameters for the ADC0 SS1 primary control
    // structure.  The mode is set to ping-pong, the transfer source is the
    // ADC0 SS1 FIFO result register, and the destination is the receive "A" buffer.  The
    // transfer size is set to match the size of the buffer.
    //
    uDMAChannelTransferSet(UDMA_CHANNEL_ADC3 | UDMA_PRI_SELECT,
                               UDMA_MODE_PINGPONG,
                               (void *)(ADC0_BASE + ADC_O_SSFIFO3),
                               buffer_1, ADC_BUF_SIZE);

        buffer_1_count ++;
    //
    // Set up the transfer parameters for the ADC0 SS1 alternate control
    // structure.  The mode is set to ping-pong, the transfer source is the
    // ADC0 SS1 FIFO result register, and the destination is the receive "B" buffer.  The
    // transfer size is set to match the size of the buffer.
    //
    uDMAChannelTransferSet(UDMA_CHANNEL_ADC3 | UDMA_ALT_SELECT,
                               UDMA_MODE_PINGPONG,
                               (void *)(ADC0_BASE + ADC_O_SSFIFO3),
                               buffer_2, ADC_BUF_SIZE);

        buffer_2_count++;

    //----- 4. End -----//


    //----- 5. Enable the uDMA channel-----//
    // Now the channel is configured and is ready to start.
        // As soon as the channel is enabled, the peripheral will
    // issue a transfer request and the data transfers will begin.
    //
    uDMAChannelEnable(UDMA_CHANNEL_ADC3);
        //----- 5. End -----//
}

//*****************************************************************************
//
// The interrupt handler for ADC0 SS3.  This interrupt will occur when a DMA
// transfer is complete using the ADC0 SS1 uDMA channel.  It will also be
// triggered if the peripheral signals an error.  This interrupt handler will
// switch between receive ping-pong buffers A and B. This will keep the ADC
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
       if(buffer_pointer  == 3)
       {

        uDMAChannelTransferSet(UDMA_CHANNEL_ADC3 | UDMA_PRI_SELECT,
                               UDMA_MODE_PINGPONG,
                               (void *)(ADC0_BASE + ADC_O_SSFIFO3),
                               buffer_3, ADC_BUF_SIZE);
                //Re-enable the uDMA channel
                uDMAChannelEnable(UDMA_CHANNEL_ADC3);
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

void ConfigureTimer(void){
    // this function configures the timers
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
    TimerConfigure(TIMER0_BASE, TIMER_CFG_PERIODIC);
    ui32Period = (SysCtlClockGet()/100 ) ; // this is basically for 100 hertz
    TimerLoadSet(TIMER0_BASE, TIMER_A, ui32Period -1); // the -1 because the interrupr fires at 0
    IntEnable(INT_TIMER0A);
    TimerIntEnable(TIMER0_BASE, TIMER_TIMA_TIMEOUT);  // enabling the timer interrupt
    IntMasterEnable(); // enable interrupts globally
    TimerEnable(TIMER0_BASE, TIMER_A); // now that the configuration is all done enable the timer.
}


void Timer0IntHandler(void)
{
    static int count = 0;
    // Clear the timer interrupt
    TimerIntClear(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
    count++;


    // using sequencer 3
    ADCIntClear(ADC0_BASE, 3);

    ADCProcessorTrigger(ADC0_BASE, 3);

}





int main(void)
{
        DMA_Init();
        ADC0_Init();
        IntMasterEnable();              // globally enable interrupt
        //ADCProcessorTrigger(ADC0_BASE, 1);
        ConfigureTimer();

        while(1)
        {
            if(buffer_1_count - old_1_count > 0 ){
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
        }
}
