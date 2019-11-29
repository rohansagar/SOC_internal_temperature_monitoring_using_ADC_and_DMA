# SOC internal temperature monitoring using ADC and DMA:

This project is intended to monitor the internal tempreature of the TM4C123GH6PM microcontroller from 
Texas Instruments. It achieves tempearature monitoring in a highly efficient way by using the least
amount of CPU cycles possible. It does so by leveraging the capabilities of the ADC and DMA peripherals of
the microcontroller. 

Direct memory access (DMA) is a technique that allows a peripheral, such as an ADC, to transfer data directly to 
or from memory without having each byte (or word) handled by the processor, thus reducing the amount of CPU cycles 
used in the process. 

The project uses the ADC module of the microcontroller to read the temperature data collected using the internal 
temperature sensor that is baked into the silicon of the chip. This data is transferred from the ADC data register 
to a circular buffer in memory using DMA. The data is stored and processed in the form of a sliding window average.
We only use the processor to compute the average of the values in a sliding window periodically. The window length 
and window increment are caliberated for optimal performance. 