##  Real Time Stock Monitor 
### using PIC32 and ESP8266

### David Valley (drv34) | Saelig Khattar (sak283) | Shrinidhi Kulkarni (ssk285) 

# Introduction
Intro

Motivation

# High Level Design
## Overview 

## Patents and Legal

## Hardware and Software Tradeoffs 

# Hardware Design 

Circuit - Pictures, schematics 
Keyboard with port expander 
ESP8266 Connections 
LEDs for controlling maybe?  (try and do it today)
Components, calibrations (hardware)

# Software Design

## ESP8266 Wifi Module Setup

To set up the ESP module, we had to send it various  AT commands before we began to use it in our application. To check if the module worked, simply sending it “AT” via serial (which can easily be done using Putty or the Arduino Serial Monitor) and receiving the string “OK” would verify its proper operation. Next, we reset the module, using “AT+RST” to ensure any previous settings would not interfere with our current setup. 

Once we have ensured the module works and is reset, the next step was to connect the module to a Wifi Network. We used RedRover. To connect this module to RedRover, we first registered the device with Cornell IT, and then sent the AT command “AT+CWJAP_DEF=RedRover” to the module. Note, RedRover does not require a password to join the network, but the AT Command can accept a password argument. After it is connected, we can get the IP address of the module using “AT+CIFSR.” Next, we enable the ESP module to have multiple connections using “AT+CIPMUX=1” and configure the ESP module as a server using “AT+CIPSERVER=1”. After we sent this series of AT commands, the ESP module was ready to be used within our project.

## UART Communication Between ESP and PIC32
You can program and communicate with the ESP8266 Wifi Module using AT Commands. There are numerous AT Commands available for this module, and they are provided in Instruction Set Manual which is linked in the Appendix. The PIC32 communicated with the ESP module using UART Serial communication. We would send strings (or rather, character arrays) from the PIC32 to the ESP module containing the AT Command and would await a response the receive line of the PIC.  The exact process for sending and receiving messages via UART is described in greater detail in the next paragraph.

## Reading and Sending via UART
Reading and Sending messages via UART between the PIC32 chip and the ESP Wifi Module was also tricky. To do this, we used the function DMA_PutSerialBuffer provided by Bruce in example code and a heavily modified version of GetSerialBuffer. The DMA_PutSerialBuffer function would send the string placed in PT_send_buffer through UART to the ESP module using DMA one byte at a time. Modifying the GetSerialBuffer function was tricky because we could not find any documentation on how the ESP module responses were terminated. After experimenting, we concluded that most responses terminated with a ‘\n’ (new line) and ‘\r’ (carriage return) character in succession. We read up to 200 characters from the buffer (which was slightly more than the largest response we expected to receive) and stopped reading as soon as we saw that terminator. As each character was read, it was put into a character array that could be used by other functions. At the beginning of this function, we also cleared all UART2 errors. The reason for this is discussed in more detail in the testing and debugging section.

## Socket Communication Between ESP and our Python Client
The ESP communicated with our Python Client via a socket connection. The client connected to the module at its IP address on port 333 (the default when the ESP module is set up a server). The ESP module sent this client our custom commands based on user input on the keypad. The client received these commands, retrieved the necessary information (described in the next section), and sent this information back over the socket connection. The socket communication client side was handled by the standard Python socket library.

## Communication from Python Client to Intrinio Web API
Our Python client communicated with the Intrinio Web API using HTTP GET Requests. Based on the command it received from the ESP, the client would make the necessary GET request, format the response, and send it back to the ESP module. The GET requests were formatted based on the API requirements, and contained our API key . On an API call, Intrinio would return a JSON string with the necessary information. The client then parsed this JSON string to get the stock price. 
 
## Threads
### Keyboard Thread
The keypad thread is similar to the thread used in lab 2 for inputting parameters to be used in cricket call generation. One major difference however is the use of the port expander for the keypad. As outlined in lab 2, the wiring needed to scan the keyboards horizontal rows are mapped to pins RA0-RA3 on the PIC . In this lab, we were making use of RA1 for transmission from the PIC to the ESP module, and so we had to work around it in designing the keypad thread. It was not merely as simple as leaving everything in place and moving that one single wire to a different pin. This is because pins A0-A3 were selected specifically to make the code for scanning the keyboard simpler. This is the code from lab 2 of course, modeled off the sample code designed by Bruce, which works by reading the bits from each of pins A0-A3, and then the bits from each of B7-B9 (which map to the vertical columns of the keypad). The bits are then ORed together and the value compared against a lookup array which we define globally in the thread. 

Using a single different pin would mean different bit masks and subsequently produce different values which would not match any of the array entries. One solution to this is to manually calculate the expected values from these OR operations and construct a separate lookup table to correspond to these. Another, cleaner solution involved using the port expander. For this we referred to the example code written by Bruce which also made provisions for SPI communications. That element of it we did not need, however the code which initialized and set up the port expander, and of course the main keypad scanning logic, was left mostly intact, and needed virtually no further modification.

In addition to the main code pulled from the example sketch online, we added debouncing logic in a fashion similar to lab 2. In fact we reused nearly the exact same debouncing switch statement as the one used in that lab, modifying only the MaybePush state, which is where we put our ticker input logic to feed values to our API call.

To actually translate keypad number presses to letters, we make use of a second case statement. We had 27 cases, one for each letter of the alphabet, plus an additional space character. In our debouncing logic, once the pound sign was pressed, which worked as our ‘enter’ button, the current value in a running buffer would be passed as the argument to the second case statement. The letter corresponding to the numbers in the buffer would then be stored in a second ticker symbol buffer. Once a total of four characters was inputted (which is the max length for a NYSE stock symbol), the buffers would reset, and an API call would be made using that ticker.

### Wifi Config Thread
We put the code for the  setup process described in the section “ESP8266 Wifi Module Setup” into this thread. 

### API Call Function (on the PIC)
The API Call Function is perhaps the most important function of our project. It is responsible for sending a custom command to the ESP via serial based on the stock ticker the user entered on the keypad, getting the returned stock price, and displaying this price (along with its ticker) as well as a triangle to indicate how the price instantaneously changed on the TFT LCD.  Since we ran into many problems trying to receive the responses to stock price requests on the PIC (as discussed in the “Testing and Debugging” section), we created a custom GetStock function that would read a preset number of characters from the buffer. This was feasible to do because the response containing the stock price was always the same number of characters. We would then parse this response to get only the stock price so we could display it and compare it with the last price.

### Main Thread
To make the software design more structured and the software workflow more clearer, we developed a “main” protothread. This thread first spawned the Wifi Config thread, which would set up the ESP module and display its IP address on the screen. This thread was also responsible for awaiting for a connecting from our Client and blocking the rest of the system until this requirement was satisfied. Once it had detected the client had connected, it would then check to see if a stock ticker had been entered. If so, it made an API call which would update the corresponding stock price on the TFT in real-time.

### Main Function
In our main function, we set up protothreads, enabled interrupts, set up the TFT display, and initialized and scheduled all of our protothreads.

# Results 
## Data 
Pictures 

## Testing 
Issues with UART errors 
API Call Issues 

# Extensions
An important aspect of this project to note is its extensibility to other projects and applications of the PIC32 microcontroller. In general, this project served as demonstration of the PIC32 microcontroller as a TCP server and client. It’s functionality in this way can be easily applied to other peripheral circuits or additional modules. Wireless communication makes many new functions possible, and allows us to increase the usability and relevance of our smarthome merely by incorporating additional API calls and data requests to various external servers. An original extension to this project was the communication of weather data from the national weather service to the PIC. In general, any database accessible with similar API calls can be incorporated into this project. 

Aside from the incorporation of API calls and data requests to external servers, Wi-Fi communication with the PIC could be used in various other respects, it is extremely versatile in that way. Any sort of light display, temperature control circuit, home security unit, or IoT application could be implemented using the ESP module and serial communication over the PIC .

As mentioned, one interesting add-on to this project is weather service data. In a similar fashion to our own stock quote requests, we could make API calls to servers of the national weather service and display temperature, environmental conditions, and general weather data on our TFT display. We could take this even further by then analyzing the weather data and, based on the conditions, stream music that fits the mood of said weather. Streaming would of course occur via Wi-Fi communication with the ESP module. We in fact originally considered live music streaming with the module, however due to time constraints pivoted and decided on stock API calls.


# Conclusion
Overall it is fair to conclude that the results of the project met our expectations, albeit with various complexities along the way. In a broad sense our project served as a proof of concept for lightweight wireless communication projects using Wifi over the ESP module. We pivoted early on as far as the direction we wanted to take our project in, however in the end made great progress in determining the versatility and extensibility of the ESP module and Wi-Fi communication to the PIC .

It is worth mentioning that, while this project could have been done without using Protothreads or any of our previously used threading libraries, their compact and lightweight nature fit in very well with how our code was broken down. As discussed, our code can be broken down into perhaps three major components: the keypad component, the initial configuration code, and the actual API calls. It worked quite naturally to have each one in its own thread and running continuously. Furthermore, in a fashion similar to inline function calls, whenever external functionality was needed which was not explicitly provided by one of our threads, we could spawn protothreads, which would initialize, execute a given function, and terminate, yielding the processor back to the calling thread. In this way we were able to explore the usability and extensibility of protothreads in applications to communication protocols outside the normal scope of the previous four labs.

It is certainly fair to say that there were many unexpected delays in the development of this project. Something to change would of course to make one of the labs focused on serial communication. This has been a lab in previous semesters of the class, and the general sentiment of including it as a lab is held by most of the staff, Bruce included. While there were of course various aspects to our project outside of serial communication, having a previous lab focus solely on that would have helped reduce the startup costs tremendously in terms of spending time learning the material we needed for our project.

## Patents and Legal
There are several cellphone apps and standalone systems that do a very similar application. However, almost no application uses a PIC32 to do this. Our entire project costs less than 35$ and can be used as a stock display system on a desk. Since almost all of our code is our own or made available to us during the course, there are no intellectual property or legal issues.



# References
Python libraries
Stock APIs
# Appendixes 

ESP Documentation 
UART documentation for PIC32
PIC Code
Python Code 

