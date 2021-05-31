/*
 Arduino MKR Zero I2S demo
 (Enhanced WAV player)
 
 by Bernardo Giovanni (@cyb3rn0id)
 https://www.settorezero.com 
 
 Libraries to be installed:
 - Adafruit ILI9341
 - Adafruit GFX
 - Arduino Sound
 
 Parts used:
 - Arduino MKRZero
 - ILI9341 SPI display 
 - MAX08357 breakout board 
 - Rotary Encoder breakout board
 
 Connections (Component pin => MKR Zero pin)
 
 * ILI9341 display
   - GND:   GND
   - VCC:   VCC 
   - MOSI:  8
   - MISO: 10
   - SCK:   9
   - DC:    7
   - CS:    6
   - RES:   VCC
 
 * MAX08357 breakout board:
   - GND:  GND
   - VIN:  5V
   - LRC:  3
   - BCLK: 2
   - DIN:  A6

 * Rotary Encoder breakout board:
   - Button : 0
   - A (DAT): 4
   - B (CLK): 5
   - GND:     GND 
   - VCC:     VCC
   put 1nF capacitor between encoder pin A and GND, make the same on encoder pin B
  */

#include <SD.h>
#include <ArduinoSound.h>
#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"

// display size, assuming portrait mode (orientation 0 or 2, so height is the longest value)
#define DISPLAY_HEIGHT  320
#define DISPLAY_WIDTH   240

// display orientation
// 0 : vertical, connector on bottom
// 1 : horizontal, connector on the right
// 2 : vertical, connector on top
// 3 : horizontal, connector on the left
#define DISPLAY_ORIENTATION 2

// Standard Adafruit Font used in the ILI9341 library is 5x8 (Size=1)
// If you use font_size=2, font will be 10x16 and so on. I Use this 
// constaNt for designing the interface
#define FONT_SIZE 2

// max number of files to be saved in RAM from the ones on the SD card
#define MAX_FILES 40 

// don't change those values: are calculated (assumed display in portrait mode)
// maximum number of files viewable on display
#define MAX_FILES_ON_DISPLAY (DISPLAY_HEIGHT/(8*FONT_SIZE))
// maximum number of chars viewable on a display row
#define MAX_FILE_LENGTH (DISPLAY_WIDTH/((5*FONT_SIZE)+1))

// Variables used for file list/visualization
String fileName[MAX_FILES];  // filenames saved in ram
uint8_t totalFiles=0;        // counter for the wav files found on the SD Card (is <=MAX_FILES)
uint8_t fileIndexSelected=0; // file index of the file actually selected on the display
uint8_t fileIndexOnTop=0;    // file index of file visualized on top of the display
int8_t  rowPos=0;            // row where selection actually is on display
int8_t  rowPosPrev=-1;       // previous selected row (-1: start condition)

uint8_t volume=50; // wav player volume 

long encButT=0;

// Wave File to be played
SDWaveFile waveFile;

// Display Pins
// Data/Command and Chip Select of the display
// you can choose pins you want
#define TFT_DC 7
#define TFT_CS 6
// I'm using the ILI9341 display with the hardware SPI
// On MKR series pin for the hardware SPI pins are 8=>MOSI, 9=>SCK, 10=>MISO
// so you must connect MOSI of the board to the MOSI of display and so on
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);

// Encoder Pins
#define ENCODER_BUTTON 0 // used interrupt
#define ENCODER_A  4     // used interrupt
#define ENCODER_B  5

// global variable used to track encoder direction
//  0 : stop, encoder not turned
//  1 : encoder turned up (counter-clockwise)
// -1 : encoder turned down (clockwise)
int8_t encDir=0; 
bool encButtonPressed=false;

// program modality
// 0: we're visualizing the file list, with user can select file to be played with encoder
// 1: we're playing a wav file
uint8_t Mode=0;
bool modeJustChanged=false;

void setup() 
  {
  pinMode (ENCODER_A, INPUT_PULLUP);
  pinMode (ENCODER_B, INPUT_PULLUP);
  pinMode (ENCODER_BUTTON, INPUT_PULLUP);
  
  // pins having port change interrupt on mkr series:
  // 0, 1, 4, 5, 6, 7, 8, 9, A1, A2
  // attach interrupt vector on logic status change for pin ENCODER_A
  attachInterrupt(digitalPinToInterrupt(ENCODER_A), encoderRotation_ISR, CHANGE); 
  // attach interrupt vector on ENCODER_BUTTON press
  attachInterrupt(digitalPinToInterrupt(ENCODER_BUTTON), encoderButton_ISR, FALLING);
   
  tft.begin();
  tft.setRotation(DISPLAY_ORIENTATION); 
  tft.fillScreen(ILI9341_BLACK);
  tft.setCursor(0,0);
  tft.setTextSize(FONT_SIZE);
  tft.setTextColor(ILI9341_GREEN);
  
  if(!SD.begin()) 
    {
    tft.setTextColor(ILI9341_RED);
    tft.print("SD Card Error");
    while(1);
    }
  else
    {
    tft.println("SD Init"); 
    }

  // Open SD and make files list
  File root = SD.open("/");
  totalFiles=readDir(root);

  if (totalFiles)
    {
    tft.setTextColor(ILI9341_WHITE);
    tft.print(totalFiles);
    tft.setTextColor(ILI9341_GREEN);
    tft.println(" Wavs found");
    delay(1500);
    }
  else
    {
    tft.setTextColor(ILI9341_RED);
    tft.print("No Wavs found");
    while(1);
    }

  printFileList();
  listSelect(rowPos, -1); // select the first file
  }


void loop() 
  {
  switch (Mode)
    {
    // --------------------------------------------------------------------------------------------------------------
    // MENU (mode 0)
    // --------------------------------------------------------------------------------------------------------------
    case 0: // file selection
        // just changed from a previous mode: reprint the file selection screen
        if (modeJustChanged)
           {
           //detachInterrupt(digitalPinToInterrupt(ENCODER_BUTTON));
           while (digitalRead(ENCODER_BUTTON)==LOW); // eventually stay stuck if econder still pressed
           encButT=millis(); // for deactivating encoder button for a while
           modeJustChanged=false;
           if (AudioOutI2S.isPlaying()) AudioOutI2S.stop();
           printFileList();
           listSelect(rowPos, -1);
           //attachInterrupt(digitalPinToInterrupt(ENCODER_BUTTON), encoderButton_ISR, FALLING);
           }
        
        // check encoder for making the encoder stuff associated to this page (file selection)
        if (encDir==-1) // encoder turned up/counter-clockwise
           {
           encDir=0; // reset encoder movement
           rowPosPrev=rowPos; // save the actual row position first than move
           if (rowPos==0) // we're on top and trying to move furtherly up
              {
              // if it's already the first file: don't do nothing
              // if is not the first file: move up the window
              if (fileIndexSelected!=0) 
                 {
                 fileIndexSelected--; // put pointer to previous file, no need to decrement the row since we're alreaty on the first row
                 fileIndexOnTop=fileIndexSelected; // set the new file index on top of the list
                 printFileList(); // reprint file list with the new file on top
                 listSelect(0, -1); // select first row, don't "unselect" nothing
                 }
              }
           else
              {
              // we were not on the first row, we can go up
              rowPos--; // go to the row above
              if (fileIndexSelected>0) fileIndexSelected--; // point to the previous file if actual file is not the first
              listSelect(rowPos, rowPosPrev); // select the new row, unselect the previous one
              }
           } // \move up
  
        else if (encDir==1) // encoder turned down/clockwise
           {
           encDir=0; // reset encoder movement
           rowPosPrev=rowPos; // save the actual row position first than move
           // calculate the last Row (0-based index)
           uint8_t lastRow=0;
           // if the total number of files is less than the maximum amount of showable files, the last row is the last file index
           if (totalFiles<MAX_FILES_ON_DISPLAY) lastRow=totalFiles-1; 
              else lastRow=MAX_FILES_ON_DISPLAY-1; 
    
           // we're on the bottom of the list and trying to move furtherly down
           if (rowPos==lastRow)
              {
              // if it's already the last file: don't do nothing
              // if is not the last file: move down the window
              if (fileIndexSelected<totalFiles-1) 
                 {
                 fileIndexSelected++; // put pointer to next file, no need to increment the row since we're already on the last row
                 fileIndexOnTop=fileIndexSelected-lastRow; // set the new file index on top of the list
                 printFileList(); // reprint the file list with new file on top
                 listSelect(rowPos,-1); // select the last row, don't "unselect" nothing
                 }
              }
           else
              {
              // we're not on the bottom of the list, we can go down
              rowPos++; // go to the row under                    
              if (fileIndexSelected<totalFiles-1) fileIndexSelected++; // point to the next file if this is not the last
              listSelect(rowPos, rowPosPrev); // select the new row, unselect the previous one
              }
           } // \move down
    break;

    // --------------------------------------------------------------------------------------------------------------
    // WAV PLAY (mode 1)
    // --------------------------------------------------------------------------------------------------------------
        
        case 1: // wav playing
            if (modeJustChanged) //mode just changed: start to play selected file
                {
                //detachInterrupt(digitalPinToInterrupt(ENCODER_BUTTON));
                while (digitalRead(ENCODER_BUTTON)==LOW); // eventually stay stuck if encoder still pressed
                encButT=millis(); // for deactivating encoder button for a while
                modeJustChanged=false;
                printPlayScreen();
                waveFile=SDWaveFile(fileName[fileIndexSelected]);
                // Is the selected file playable?
                if (AudioOutI2S.canPlay(waveFile))
                    {
                    // file info (4 rows with font size=1)
                    tft.setCursor(0,8*FONT_SIZE*3);
                    tft.setTextSize(1);
                    tft.setTextColor(ILI9341_CYAN);  
                    tft.print("Bits per sample: ");
                    tft.setTextColor(ILI9341_WHITE); 
                    tft.println(waveFile.bitsPerSample());
                    tft.setTextColor(ILI9341_CYAN); 
                    tft.print("Channels: ");
                    tft.setTextColor(ILI9341_WHITE); 
                    tft.println(waveFile.channels());
                    tft.setTextColor(ILI9341_CYAN); 
                    tft.print("Sample Rate: ");
                    tft.setTextColor(ILI9341_WHITE); 
                    tft.print(waveFile.sampleRate());
                    tft.println("Hz");
                    tft.setTextColor(ILI9341_CYAN); 
                    tft.print("Duration: ");
                    tft.setTextColor(ILI9341_WHITE); 
                    tft.print(waveFile.duration());
                    tft.println("sec");

                    manageVolume();

                    // check if there is a txt file with same name of the wav
                    // and print it to screen
                    String finfo=fileNameNoExt(fileName[fileIndexSelected])+".txt";
                    if (SD.exists(finfo))
                      {
                      File finfoFile = SD.open(finfo);
                      if (finfoFile) 
                        {
                        tft.println();
                        tft.println();
                        tft.setTextSize(2);
                        tft.setTextColor(ILI9341_YELLOW);  
                        while (finfoFile.available()) 
                          {
                          tft.print(char(finfoFile.read()));
                          }
                        finfoFile.close();
                        }
                      }
                      
                    AudioOutI2S.play(waveFile);
                    delay(50);
                    }
                else
                    {
                    // I cannot play selected file!
                    tft.setTextColor(ILI9341_RED);
                    tft.println();
                    tft.println("Invalid file");
                    delay(1500);
                    // exit from this mode: this will
                    // return to selection screen
                    Mode=0;
                    modeJustChanged=true;
                    break;
                    }
                } // end mode just changed

            //attachInterrupt(digitalPinToInterrupt(ENCODER_BUTTON), encoderButton_ISR, FALLING);
             
            // check if the file is playing or not
            if (!AudioOutI2S.isPlaying()) 
                {
                // those instructions will return to file selection 
                Mode=0;
                modeJustChanged=true;
                // those instructions will play the next file in the list instead
				        // not tested
                /*
                if (fileIndexSelected<totalFiles-1) 
                   {
                   fileIndexSelected++; // point to the next file if this is not the last
                   // update rows for display visualization if you exit after
                   if (rowPos<lastRow) // the row selected was not the last one on display
                      {
                      rowPos++; // go to the row under                    
                      }
                   else
                      {
                      // the row selected was the last one
                      // so row selected will remain the same 
                      // but the files on top will scroll a row under 
                      fileIndexOnTop=fileIndexSelected-lastRow;
                      }
                      modeJustChanged=true; // remain in this mode but reload the screen
                   }
                else // played the last file
                   {
                   // those instruction will return to file selection screen at end of play of last file
				           // not tested 
                   Mode=0; // return to file selecion mode
                   modeJustChanged=true;

                   // those ones will start from the beginning
                   //fileIndexSelected=0;
                   //fileIndexOnTop=0;
                   //rowPos=0;
                   //modeJustChanged=true;
                   }
                  */  
                 }
            else // audio playing
                {
                // audio playing: use encoder for change the volume
                if (encDir==-1) // encoder turned up/counter-clockwise
                    {
                    encDir=0;
                    if (volume>0)
                        {
                        volume-=5;
                        manageVolume();
                        }
                    }
                else if (encDir==1) // encoder turned down/clockwise
                    {
                    encDir=0;
                    if (volume<100)
                        {
                        volume+=5;
                        manageVolume();
                        }
                    }
                }
            break; // END PLAYER MODE
            
        default:
        break;
        } // \switch mode
  } // \loop

// prepare screen for playing
void printPlayScreen(void)
  {
  tft.fillScreen(ILI9341_BLACK);
  tft.setCursor(0,0);
  tft.setTextSize(FONT_SIZE);
  tft.setTextColor(ILI9341_BLACK, ILI9341_WHITE); 
  tft.println(" FILE PLAYER ");
  tft.setTextColor(ILI9341_GREEN); 
  tft.print(">");
  tft.print(fileName[fileIndexSelected]);
  }

// print volume level in play mode  
void manageVolume(void)
    {
    // print volume value
    tft.setTextSize(FONT_SIZE);
    tft.setTextColor(ILI9341_WHITE,ILI9341_BLACK);
    tft.setCursor(0,(FONT_SIZE*8*3)+(8*4)+FONT_SIZE*8);
    tft.print("Volume: ");
    // red color 
    if (volume>=85) tft.setTextColor(ILI9341_RED,ILI9341_BLACK);  
        else tft.setTextColor(ILI9341_GREEN,ILI9341_BLACK);  
    tft.print(volume);
    tft.print("%  ");
    // set volume
    AudioOutI2S.volume(volume);
    }

// print the file list on display
void printFileList(void)
    {
    tft.fillScreen(ILI9341_BLACK);
    tft.setCursor(0,0);
    tft.setTextSize(FONT_SIZE);
    tft.setTextColor(ILI9341_GREEN);
    uint8_t t=MAX_FILES_ON_DISPLAY;
    if (totalFiles<t) t=totalFiles;
    for (uint8_t i; i<t; i++)
        {
        if (i<t-1) printFileName(fileName[i+fileIndexOnTop],true);
            else printFileName(fileName[i+fileIndexOnTop],false); // no carriage return for last file
        }
    }

// make the row selection on display
void listSelect(int8_t actualPos, int8_t prevPos) 
  {
  // print the selected file in reverse, first color is the foreground, second one is the background
  tft.setTextColor(ILI9341_BLACK, ILI9341_GREEN);
  tft.setCursor(0,actualPos*8*FONT_SIZE); // 8 is the standard font height for size=1
  printFileName(fileName[fileIndexOnTop+actualPos],false);
  // additional spaces for making selection bar arriving to the border of display
  int8_t i=MAX_FILE_LENGTH-fileName[fileIndexOnTop+actualPos].length()-1;
  if (FONT_SIZE>2) i--;
  while (i-->0) tft.print(" ");
  
  // re-print the previous file in "normal" colors (black background)
  // prevPos is -1 if no previous selection was done (example: after a screen clearing)
  if (prevPos>-1)
    {
    tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
    tft.setCursor(0,prevPos*8*FONT_SIZE);
    printFileName(fileName[fileIndexOnTop+prevPos],false);
    // additional spaces for removing previous selection bar
    i=MAX_FILE_LENGTH-fileName[fileIndexOnTop+prevPos].length()-1;
    if (FONT_SIZE>2) i--;
    while (i-->0) tft.print(" ");
    }
  }

// print the file name with maximum number of chars allowable on a row
// second parameter is for printing the newline
void printFileName(String fn, bool cr)
  {
    if (fn.length()>MAX_FILE_LENGTH)
        {
        tft.print(fn.substring(0,MAX_FILE_LENGTH-1));
        }
    else
        {
        tft.print(fn);
        }
    if (cr) tft.println();
    }
    
// Interrupt service routine on encoder rotation (encoder pin A is changed)
void encoderRotation_ISR(void)
  {
  static long t=millis();
  static int8_t m=0; // movement: 0=no movement, 1=clockwise, -1=counter-clockwise
  static int8_t c=0; // the tick will be checked two consecutive times, this will count the consecutive times
  if (t>millis()) t=millis(); // millis() rollover
  if (millis()-t<30) return; // too few time passed, maybe is a bounce
  c++; // a tick has occurred
  if (digitalRead(ENCODER_A)==digitalRead(ENCODER_B)) 
    {
    if (c==1) m=1; // first tick with clockwise movement
    if (c==2) // second tick
      {
      if (m==1) // the movement is still clockwise?
        {
        encDir=1; // encoder direction: DOWN
        }
      c=0;
      m=0;
      }
    }
  else // movement is counter-clockwise
    {
    if (c==1) m=-1;
    if (c==2)
      {
      if (m==-1)
        {
        encDir=-1; // encoder direction: UP
        }
      c=0;
      m=0;
      }
    }
  t=millis(); // last time we had a change interrupt
  }
 
// Interrupt service routine on encoder button pressing (encoder button turned low)
void encoderButton_ISR(void)
    {
    static long t=0;
    if (t>millis()) t=millis(); // millis() rollover
    if ((millis()-t)<200) return; // less than 200ms from the last interrupt: it's probably a bounce
    if ((millis()-encButT)<1000) return; // wait at least 1 seconds from the last press
    switch(Mode)
        {
        case 0: // we're in file list
          Mode=1; // pass in play mode
          modeJustChanged=true;
        break;

        case 1: // we're in play mode
          Mode=0;
          modeJustChanged=true;
         break;
        }
    t=millis();
    }

// read specified directory and save file list to RAM
// returns the number of files
uint8_t readDir(File dir)
  {
  uint8_t filesFound=0;
  while(true)
    {
    File entry =  dir.openNextFile();
    if (!entry) break; // no more files => exit
    if (!entry.isDirectory()) // I don't want to read sub-directories
      {
      if (fileIsWAV(entry.name())) // check if file has wav extension
        {
        //fileName[filesFound]=fileNameNoExt(entry.name());
        fileName[filesFound]=String(entry.name());
        filesFound++;
        if (filesFound==MAX_FILES) break;
        }
      }
    entry.close();
    }
  return filesFound;
  }

// debug function for printing on the display entire directory 
// with sub-directories and their content. Not used in this code
void printDirectory(File dir) 
  {
  while (true) 
    {
    File entry=dir.openNextFile();
    if (!entry) break;
    tft.println(entry.name());
    //tft.print(fileNameNoExt(entry.name())); // filename without extension
    if (entry.isDirectory()) 
      {
      // entry is a directory
      tft.println("/");
      printDirectory(entry);
      } 
    else 
      {
      // entry is not a directory: is a file
      // file size in bytes
      tft.print(" ");
      tft.println(entry.size(), DEC);
      }
    entry.close();
    } // \while
  }

// return true if file has wav extension
bool fileIsWAV(char* filename) 
  {
  int8_t len = strlen(filename);
  // for making comparison, I turn the entire array in lower-case using strlwr function
  if (strstr(strlwr(filename + (len - 4)), ".wav")) 
    {
    return true;
    }
  return false;
  }

// returns filename without extension
// assuming extension has only 3 chars for simplicity
String fileNameNoExt(String filename)
  {
  uint8_t len=filename.length();
  if (len>4)
    {
    String fn=filename.substring(0,len-4); // remove last 4 chars, assuming extension is 3 chars
    return(fn);
    }
  else
    {
    return(String(filename));
    }
  }
