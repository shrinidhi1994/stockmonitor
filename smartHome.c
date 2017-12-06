/*
This is a Smart-Home program designed for the PIC32MX250 platform as a part
of the final project for the course ECE 4760 at Cornell University by
David Valley, Saelig Khattar and Shrinidhi Kulkarni.

The initial code was taken from Bruce Land's ECE 4760 course website (Serial,
DAC and UART on big board) and greatly modified to enable Wi-Fi connectivity
using the ESP 8266 Wi-Fi chipset which communicates over UART protocol.
*/

#include "config_1_2_2a.h"
// Include the threading library
#include "pt_cornell_1_2_2a.h"
// Include the port expander
#include "port_expander_brl4.h"

////////////////////////////////////
// graphics libraries
// SPI channel 1 connections to TFT
#include "tft_master.h"
#include "tft_gfx.h"
// need for rand function
#include <stdlib.h>
// need for sin function
#include <math.h>
////////////////////////////////////

// lock out timer 2 interrupt during spi comm to port expander
// This is necessary if you use the SPI2 channel in an ISR.
#define start_spi2_critical_section INTEnable(INT_T2, 0)
#define end_spi2_critical_section INTEnable(INT_T2, 1)

// Push states
#define NoPush 0
#define MaybePush 1
#define Push 2
#define MaybeNoPush 3


// pullup/down macros for keypad
// PORT B
#define EnablePullDownB(bits) CNPUBCLR=bits; CNPDBSET=bits;
#define DisablePullDownB(bits) CNPDBCLR=bits;
#define EnablePullUpB(bits) CNPDBCLR=bits; CNPUBSET=bits;
#define DisablePullUpB(bits) CNPUBCLR=bits;
//PORT A
#define EnablePullDownA(bits) CNPUACLR=bits; CNPDASET=bits;
#define DisablePullDownA(bits) CNPDACLR=bits;
#define EnablePullUpA(bits) CNPDACLR=bits; CNPUASET=bits;
#define DisablePullUpA(bits) CNPUACLR=bits;
#define NOP asm("nop");
// 1/2 microsec
#define wait20 NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;
// one microsec
#define wait40 wait20;wait20;

static struct pt pt_serial, pt_DMA_output, pt_input, pt_key,pt_wifiConfig, pt_api, pt_main;

////////////////////////////////////
// DAC ISR
// A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000
// B-channel, 1x, active
#define DAC_config_chan_B 0b1011000000000000
// DDS constant
#define two32 4294967296.0 // 2^32
#define Fs 100000

// === print a line on TFT =====================================================
// print a line on the TFT
// string buffer
char buffer[60];
void printLine(int line_number, char* print_buffer, short text_color, short back_color){
    // line number 0 to 31 
    /// !!! assumes tft_setRotation(0);
    // print_buffer is the string to print
    int v_pos;
    v_pos = line_number * 10 ;
    // erase the pixels
    tft_fillRoundRect(0, v_pos, 239, 8, 1, back_color);// x,y,w,h,radius,color
    tft_setTextColor(text_color); 
    tft_setCursor(0, v_pos);
    tft_setTextSize(1);
    tft_writeString(print_buffer);
}

void printLine2(int line_number, char* print_buffer, short text_color, short back_color){
    // line number 0 to 31 
    /// !!! assumes tft_setRotation(0);
    // print_buffer is the string to print
    int v_pos;
    v_pos = line_number * 20 ;
    // erase the pixels
    tft_fillRoundRect(0, v_pos, 239, 16, 1, back_color);// x,y,w,h,radius,color
    tft_setTextColor(text_color); 
    tft_setCursor(0, v_pos);
    tft_setTextSize(2);
    tft_writeString(print_buffer);
}

void tft_writeLine(char* str){
    //tft_fillRoundRect(10,10, 150, 10, 1, ILI9340_BLACK);// x,y,w,h,radius,color
    tft_writeString(str);
}

static char ticker[4]; //stores part of ticker that has been entered so far
static char stock_input[4]; //stores final ticker
static int ticker_index = -1; //index of which letter in the ticker needs updating
static int fetch_flag=0; //set to high once ticker has been entered
static float last_price =0; //keep track of last price for arrow drawing
static PT_THREAD(protothread_key(struct pt *pt)) {
    PT_BEGIN(pt);
    static int tmp;
    
    static char letter = 'n';
    static int keypad, i, k, PushState = 0;
    // order is 0 thru 9 then * ==10 and # ==11
    // no press = -1
    // table is decoded to natural digit order (except for * and #)
    static int keytable[12]=
    //        0     1      2    3     4     5     6      7    8     9    10-*  11-#
            {0xd7, 0xbe, 0xde, 0xee, 0xbd, 0xdd, 0xed, 0xbb, 0xdb, 0xeb, 0xb7, 0xe7};
    // bit pattern for each row of the keypad scan -- active LOW
    // bit zero low is first entry
    static char out_table[4] = {0b1110, 0b1101, 0b1011, 0b0111};
    // init the port expander
    start_spi2_critical_section;
    initPE();
    // PortY on Expander ports as digital outputs
    mPortYSetPinsOut(BIT_0 | BIT_1 | BIT_2 | BIT_3);    //Set port as output
    // PortY as inputs
    // note that bit 7 will be shift key input, 
    // separate from keypad
    mPortYSetPinsIn(BIT_4 | BIT_5 | BIT_6 | BIT_7);    //Set port as input
    mPortYEnablePullUp(BIT_4 | BIT_5 | BIT_6 | BIT_7);
    
    end_spi2_critical_section ;
    
    // the read-pattern if no button is pulled down by an output
    #define no_button (0x70)

      while(1) {
          
        // yield time
        PT_YIELD_TIME_msec(50);
    
        for (k=0; k<4; k++) {
            start_spi2_critical_section;
            // scan each rwo active-low
            writePE(GPIOY, out_table[k]);
            //reading the port also reads the outputs
            keypad  = readPE(GPIOY);
            end_spi2_critical_section;
            // was there a keypress?
            if((keypad & no_button) != no_button) break;
            //else keypad = 0;
        }
      

        static int flag = 0;
        static int curr_value = 0;
        
        static int letter_value = 0;

		switch (PushState) {
			case NoPush:
				if (k < 4) PushState = MaybePush;
				else {PushState = NoPush;i=-1;}
				break;
                
			case MaybePush:
                if (k < 4){ // then button is pushed
                    PushState = Push;
                    for (i=0; i<12; i++){
                        if (keytable[i]==keypad) {
                            if (i==11) { //if enter is pressed
                                letter_value = curr_value;  //save final numerical value of letter
                                curr_value = 0;
                                ticker_index += 1;
                            } else if (i==10) { //not needed
                                flag = 1;
                            }
                            else {
                                curr_value = curr_value*10 + i; //keep track of numerical value of letter currently entered
                            }
                            break;
                        }
                    }
                    // if invalid, two button push, set to -1
                    if (i==12) i=-1;
                }
                else {
                    PushState = NoPush; i=-1;
                }
				break;

			case Push:
				if (k < 4) PushState = Push;
				else PushState = MaybeNoPush;
				break;

			case MaybeNoPush:
				if (k < 4) PushState = Push;
				else PushState = NoPush;
				break;
		}
        
        // draw key number
        if (i>-1 && i<10) sprintf(buffer,"   %x %d", keypad, i);
        if (i==10 ) sprintf(buffer,"   %x *", keypad);
        if (i==11 ) sprintf(buffer,"   %x #", keypad);
        if (i==-1 ) sprintf(buffer,"   %d  ", i);
        //printLine2(10, buffer, ILI9340_GREEN, ILI9340_BLACK);
    
        //letter_value = 222;
        if(ticker_index < 4)
        {
			switch(letter_value){ //models a telephone keypad
				case 0: letter = ' ';   break;
				case 2: letter = 'A';   break;
				case 22: letter = 'B';  break;
				case 222: letter = 'C'; break;
				case 3: letter = 'D';   break;
				case 33: letter= 'E';   break;
				case 333: letter = 'F'; break;
				case 4: letter = 'G';   break;
				case 44: letter - 'H';  break;
				case 444: letter = 'I'; break;
				case 5: letter = 'J';   break;
				case 55: letter = 'K';  break;
				case 555: letter = 'L'; break;
				case 6: letter = 'M';   break;
				case 66: letter = 'N';  break;
				case 666: letter = 'O'; break;
				case 7: letter = 'P';   break;
				case 77: letter = 'Q';  break;
				case 777: letter = 'R'; break;
				case 7777: letter = 'S'; break;
				case 8: letter = 'T';   break;
				case 88: letter = 'U';  break;
				case 888: letter = 'V'; break;
				case 9: letter = 'W';   break;
				case 99: letter = 'X';  break;
				case 999: letter = 'Y'; break;
				case 9999: letter = 'Z'; break;
				default: letter= NULL;
			}

			ticker[ticker_index] = letter;
        }
        
        
        if(ticker_index == 3 || flag ==1) //full ticker has been entered
        {
            char tempBuffer[4];
            int a;
            tft_setCursor(0,220);
            tft_writeString("Fetching stock for ");
            for(a=0;a<4;a++){
               sprintf(tempBuffer,"%c",ticker[a]);
               tft_writeString(tempBuffer); //write stock ticker that we are fetching data for
            }
            fetch_flag = 1; //set flag to fetch this stock
            int x;
            for(x=0;x<4;x++) stock_input[x] = ticker[x]; //copy final ticker into stock_input to be used by other functions
            
            ticker_index = -1; //reset for new ticker
            memset(ticker,0, sizeof(ticker)); //clear letters in ticker
            flag = 0;
            last_price =0; //clear last price from previous stock
             
            tft_fillRoundRect(0,20*10, 320, 20, 1, ILI9340_BLACK); //clear line
        }
        
        //tft_setCursor(200,100);
        //tft_setTextSize(1);
        
        sprintf(buffer, "%d |",curr_value);
        //tft_writeString(buffer);
        printLine2(10, buffer, ILI9340_GREEN, ILI9340_BLACK);
        
        sprintf(buffer, "%c |",letter);
        tft_writeString(buffer);
        //printLine2(2, buffer, ILI9340_GREEN, ILI9340_BLACK);
        
        //sprintf(buffer, "%d |",ticker_index);
        //tft_writeLine(buffer);
        //printLine2(3, buffer, ILI9340_GREEN, ILI9340_BLACK);
        
        sprintf(buffer, "%s ",ticker);
        tft_writeLine(buffer);
        //printLine2(4, buffer, ILI9340_GREEN, ILI9340_BLACK);
        
    }
    PT_END(pt);
}


//=== Serial terminal thread =================================================
static PT_THREAD (protothread_wifiConfig(struct pt *pt))
{
    PT_BEGIN(pt);
    static char cmd[30];
    static float value;
    // Check if the module works
    /*sprintf(PT_send_buffer,"AT+RST\r\n");
    PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output) );
    PT_SPAWN(pt, &pt_input, PT_GetSerialBuffer(&pt_input));
    /*
    sscanf(PT_term_buffer, "%s",cmd);
    // Check for response from the module
    if(strstr(PT_term_buffer,"OK") != NULL){
        printLine2(1, "Setup OK", ILI9340_GREEN, ILI9340_BLACK);
        //tft_writeLine("Setup OK");
    }
    else if(strstr(PT_term_buffer,"FAIL") != NULL){
        printLine2(3, "Trying to Reset Module", ILI9340_GREEN, ILI9340_BLACK);
        //tft_writeLine("Trying to Reset Module");
        // Reset the Wi-Fi module if it's not working
        sprintf(PT_send_buffer,"AT+RST\r\n");
        PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output) );
        PT_SPAWN(pt, &pt_input, PT_GetSerialBuffer(&pt_input) );
        sscanf(PT_term_buffer, "%s",cmd);
        // Check for response from the module
        if(strstr(PT_term_buffer,"OK") != NULL){
            printLine2(3, "Reset Successful", ILI9340_GREEN, ILI9340_BLACK);
            //tft_writeLine("Reset Successful");
        }
        else if(strstr(PT_term_buffer,"FAIL") != NULL){
            printLine2(3, "Reset Failed", ILI9340_GREEN, ILI9340_BLACK);
            //tft_writeLine("Reset Failed!");
        }
    }
    */
    /*
    // Set the connection mode AT+CWMODE_DEF=<mode>
    // Set up the ESP8266 as a TCP server
    sprintf(PT_send_buffer,"AT+CWMODE_DEF=3\r\n");
    PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output) );
    PT_SPAWN(pt, &pt_input, PT_GetSerialBuffer(&pt_input) );
    sscanf(PT_term_buffer, "%s",cmd);
    // Check for response from the module
    if(strstr(PT_term_buffer,"OK") != NULL){
        //printLine2(3, "Server Started", ILI9340_GREEN, ILI9340_BLACK);
        //tft_writeLine("Server Started");
    }
    else if(strstr(PT_term_buffer,"FAIL") != NULL){
        //printLine2(3, "Unable to Start Server", ILI9340_GREEN, ILI9340_BLACK);
        //tft_writeLine("Unable to Start Server");
    }

    
    sprintf(PT_send_buffer,"AT+CWJAP_DEF=\"RedRover\",\"\"\r\n");
    PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output) );
    PT_SPAWN(pt, &pt_input, PT_GetSerialBuffer(&pt_input) );
    sscanf(PT_term_buffer, "%s",cmd);
    // Check for response from the module
    if(strstr(PT_term_buffer,"OK") != NULL){
        //printLine2(3, "Connected to network", ILI9340_GREEN, ILI9340_BLACK);
        //tft_writeLine("Connected to network");
    }
    else if(strstr(PT_term_buffer,"ERROR") != NULL){
        printLine2(3, "Unable to Connect", ILI9340_GREEN, ILI9340_BLACK);
        //tft_writeLine("Unable to Connect");
    }
     */
    // Get the local IP address
    sprintf(PT_send_buffer,"AT+CIFSR\r\n");
    PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output));
    PT_SPAWN(pt, &pt_input, PT_GetSerialBuffer(&pt_input));
    // Check for response from the module
    static char IP[21];
    tft_setCursor(10,10); tft_setTextColor(ILI9340_YELLOW);
    tft_setTextSize(1);
    strncpy(IP,PT_term_buffer+79,21);//get IP addr from the string AT+CIFSR returns
    tft_writeLine(IP);
    if(strstr(PT_term_buffer,"OK") != NULL){
        //tft_writeLine(PT_term_buffer);
    }
    else if(strstr(PT_term_buffer,"FAIL") != NULL){
        //tft_writeLine("IP Fail");
    }
    
    //set up so ESP can get multiple connections
    sprintf(PT_send_buffer,"AT+CIPMUX=1\r\n");
    PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output) );
    PT_SPAWN(pt, &pt_input, PT_GetSerialBuffer(&pt_input));
    if(strstr(PT_term_buffer,"OK") != NULL){
        //printLine2(2, PT_term_buffer, ILI9340_GREEN, ILI9340_BLACK);
        //tft_writeLine(PT_term_buffer);
    }
    else if(strstr(PT_term_buffer,"FAIL") != NULL){
        //printLine2(2, "IP Fail", ILI9340_GREEN, ILI9340_BLACK);
        //tft_writeLine("IP Fail");
    }
    
    //set up ESP as a Server
    sprintf(PT_send_buffer,"AT+CIPSERVER=1\r\n");
    PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output) );
    PT_SPAWN(pt, &pt_input, PT_GetSerialBuffer(&pt_input));
    if(strstr(PT_term_buffer,"OK") != NULL){
        //tft_writeLine(PT_term_buffer);
    }
    else if(strstr(PT_term_buffer,"FAIL") != NULL){
        //printLine2(3, "IP Fail", ILI9340_GREEN, ILI9340_BLACK);
        //tft_writeLine("IP Fail");
    }
/*
    // End the TCP Session AT+CIPCLOSE
    sprintf(PT_send_buffer,"AT+CIPCLOSE\r\n");
    PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output) );
    PT_SPAWN(pt, &pt_input, PT_GetSerialBuffer(&pt_input) );
    sscanf(PT_term_buffer, "%s",cmd);
    // Check for response from the module
    if(strstr(PT_term_buffer,"OK") != NULL){
        tft_writeString("Closed TCP Session");
    }

    //Disconnect from network before exiting program: AT+CWQAP
    sprintf(PT_send_buffer,"AT+CWQAP\r\n");
    PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output) );
    PT_SPAWN(pt, &pt_input, PT_GetSerialBuffer(&pt_input) );
    sscanf(PT_term_buffer, "%s",cmd);
    // Check for response from the module
    if(strstr(PT_term_buffer,"OK") != NULL){
        tft_writeString("Disconnected");
    }
*/
    PT_EXIT(pt);
  PT_END(pt);
} // thread 3


static float float_price;
int PT_APICall(struct pt *pt)
{
    PT_BEGIN(pt);
    tft_setCursor(0,100); tft_setTextColor(ILI9340_YELLOW);
    tft_setTextSize(1);
    tft_writeLine("Sending...");
    PT_YIELD_TIME_msec(500);
    //tft_writeLine(stock_input);
    sprintf(PT_send_buffer,"AT+CIPSEND=0,%d\r\n",10);// send number of bytes that we are planning on sending
    PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output)); //send the data in PT_send_buffer to the ESP
    int x =0;
    while(x<500000){//wait 500 msec for response
      wait40; 
      x++;
    } 
    sprintf(PT_send_buffer,"stock_%s\r\n",stock_input); //custom command to let client know what stock to pull
    int length = 61; //fixed number of characters that are in stock related ESP responses
    PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output));
    
    PT_SPAWN(pt, &pt_api, PT_GetStock(&pt_api,length)); //special function for this case, put result in PT_term_buffer_2
    tft_fillRoundRect(280,100, 300, 40, 1, ILI9340_BLACK); 
    tft_fillRoundRect(0,11*20, 320, 20, 1, ILI9340_BLACK); //clear line
    tft_setCursor(0,110);
    tft_setTextSize(3);
    char price[7];
    strncpy(price,PT_term_buffer2+54,7); //extract just stock price from ESP response
    char priceBuffer[7];
    char tickerBuffer[4];
    tft_fillRoundRect(0,110, 255, 40, 1, ILI9340_BLACK); //clear line up until triangle
    if(strstr(price,"Error")){ //client will send Error if bad or broken API call
        tft_fillRect(256,100,30,60,ILI9340_BLACK);
        tft_writeString("Error, try again");
        
    }
    else{
        int w;
        for(w=0;w<4;w++){
            sprintf(tickerBuffer,"%c",stock_input[w]);
            tft_writeString(tickerBuffer); //print stock ticker
        }
        tft_writeString(": ");
        for(w=0;w<7;w++){
            sprintf(priceBuffer,"%c",price[w]);
            //priceBuffer[w] = price[w];
            tft_writeString(priceBuffer); //print received price
        }
        
        //draw triangles or dash based on last_price comparison
        for(w=0;w<7;w++){
            priceBuffer[w] = price[w];
        }
        float_price = atof(priceBuffer); //convert char array to float
        if(float_price > last_price){
            //draw upward green arrow
            tft_fillRect(256,100,30,60,ILI9340_BLACK);
            tft_fillTriangle(256,130, 286, 130,271, 100,ILI9340_GREEN);
        }
        else if (float_price == last_price){ //draw dash
            tft_fillRect(256,100,30,60,ILI9340_BLACK);
            tft_fillRect(256, 115,30,10,ILI9340_BLUE);
        }
        else{ //draw downward red arrow
            tft_fillRect(256,100,30,60,ILI9340_BLACK);
            tft_fillTriangle(256,130, 286, 130,271, 160,ILI9340_RED);
        }
        
        last_price = atof(priceBuffer); //convert char array to float
        
    }
    PT_EXIT(pt);
    PT_END(pt);
}


//main protothread to structure flow of software
static PT_THREAD (protothread_main(struct pt *pt))
{
    PT_BEGIN(pt);
    // Configure everything
    PT_SPAWN(pt, &pt_wifiConfig,(protothread_wifiConfig(&pt_wifiConfig))); //set up ESP module
    // Do an API call
    while(1){ //block until client is connected to the ESP
        tft_setCursor(0,40);
        tft_setTextSize(1);
        tft_writeString("Waiting for Connection from Server...");
        PT_SPAWN(pt, &pt_input, PT_GetSerialBuffer(&pt_input));
        if(strstr(PT_term_buffer,"CONNECT")){ //client will send CONNECT once it connects
            tft_fillRoundRect(0,40, 320, 20, 1, ILI9340_BLACK); //clear line
            tft_setCursor(0,40);
            tft_setTextSize(1);
            tft_writeLine("Connected!");
            tft_setCursor(0,180);
            tft_writeLine("Enter in Stock Ticker using the keypad:");
            break;
         }
    }
    while(1){
        if(fetch_flag){ //if ticker has been entered
            PT_SPAWN(pt, &pt_input,PT_APICall(&pt_input)); //make the API call for the corresponding ticker that was entered
            PT_YIELD_TIME_msec(9000); //don't need to update stock so frequently
        }
        PT_YIELD_TIME_msec(1000); //allow keyboard thread to change ticker
    }
    PT_END(pt);
}



// === Main  ======================================================
void main(void) {

  PT_setup(); //set up protothreads
  INTEnableSystemMultiVectoredInt();

  PT_INIT(&pt_key); //intialize threads
  PT_INIT(&pt_main); 
  //set up TFT
  tft_init_hw();
  tft_begin();
  tft_fillScreen(ILI9340_BLACK);
  tft_setRotation(1);

  while(1){ //schedule threads forever
      PT_SCHEDULE(protothread_key(&pt_key));
      PT_SCHEDULE(protothread_main(&pt_main));
  }
} // main

// === end  ======================================================
