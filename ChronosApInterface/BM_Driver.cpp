//---------------------------------------------------------------------------
// Copyright (C) 2000 Dallas Semiconductor Corporation, All Rights Reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY,  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL DALLAS SEMICONDUCTOR BE LIABLE FOR ANY CLAIM, DAMAGES
// OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//
// Except as contained in this notice, the name of Dallas Semiconductor
// shall not be used except as stated in the Dallas Semiconductor
// Branding Policy.
//---------------------------------------------------------------------------

// *************************************************************************************************
// Generic RS232 functions based on DS2480 reference code. 
// Original filename was "Win32Lnk.C", http://www.koders.com/c/fidDAEC3C64150B7DD361249A5869EC63954A2A6EAA.aspx
// *************************************************************************************************
#include <tchar.h>
#include <stdlib.h>

#include "BM_Driver.h"

// Typedefs
#ifndef OW_UCHAR
#define OW_UCHAR
	typedef unsigned char  uchar;
#ifdef WIN32
	typedef unsigned short ushort;
	typedef unsigned long  ulong;
#endif
#endif

// exportable functions 
bool OpenCOM(int, char *);
void CloseCOM(int);
void FlushCOM(int);
int  WriteCOM(int, int, UCHAR *);
int  ReadCOM(int, int, UCHAR *);

// defines
#define MAX_PORTNUM 1

// Win32 globals needed
HANDLE ComID[MAX_PORTNUM];
OVERLAPPED osRead[MAX_PORTNUM],osWrite[MAX_PORTNUM];   


//---------------------------------------------------------------------------
// Attempt to open a com port.  Keep the handle in ComID.
// Set the starting baud rate to 115200.
//
// 'portnum'   - number 0 to MAX_PORTNUM-1.  This number provided will 
//               be used to indicate the port number desired when calling
//               all other functions in this library.
//
// 'port_zstr' - zero terminate port name.  For this platform
//               use format COMX where X is the port number.
//
//
// Returns: TRUE(1)  - success, COM port opened
//          FALSE(0) - failure, could not open specified port
//
bool OpenCOM(int portnum, char *port_zstr)
{
   char tempstr[80];
   short fRetVal;
   COMMTIMEOUTS CommTimeOuts;
   DCB dcb;

   // open COM device
   if ((ComID[portnum] =
      CreateFile( port_zstr, GENERIC_READ | GENERIC_WRITE,
                  0, 
                  NULL,                 // no security attrs
                  OPEN_EXISTING,
                  FILE_FLAG_OVERLAPPED, // overlapped I/O
                  NULL )) == (HANDLE) -1 )
   {
      ComID[portnum] = 0;
      return (FALSE) ;
   }
   else
   {
      // create events for detection of reading and write to com port 
      osRead[portnum].hEvent = CreateEvent(NULL,TRUE,FALSE,tempstr);  
      osWrite[portnum].hEvent = CreateEvent(NULL,TRUE,FALSE,tempstr); 

      // get any early notifications
      SetCommMask(ComID[portnum], EV_RXCHAR | EV_TXEMPTY | EV_ERR | EV_BREAK);

      // setup device buffers
      SetupComm(ComID[portnum], 2048, 2048);

      // purge any information in the buffer
      PurgeComm(ComID[portnum], PURGE_TXABORT | PURGE_RXABORT |
                           PURGE_TXCLEAR | PURGE_RXCLEAR );

      // set up for overlapped non-blocking I/O 
      CommTimeOuts.ReadIntervalTimeout = 0; 
      CommTimeOuts.ReadTotalTimeoutMultiplier = 20; 
      CommTimeOuts.ReadTotalTimeoutConstant = 40; 
      CommTimeOuts.WriteTotalTimeoutMultiplier = 20; 
      CommTimeOuts.WriteTotalTimeoutConstant = 40; 
      SetCommTimeouts(ComID[portnum], &CommTimeOuts);

      // setup the com port
      GetCommState(ComID[portnum], &dcb);

	  dcb.BaudRate = CBR_115200;             // current baud rate 
      dcb.fBinary = TRUE;                    // binary mode, no EOF check 
      dcb.fParity = FALSE;                   // enable parity checking 
      dcb.fOutxCtsFlow = FALSE;              // CTS output flow control 
      dcb.fOutxDsrFlow = FALSE;              // DSR output flow control 
      dcb.fDtrControl = DTR_CONTROL_ENABLE;  // DTR flow control type 
      dcb.fDsrSensitivity = FALSE;           // DSR sensitivity 
      dcb.fTXContinueOnXoff = TRUE;          // XOFF continues Tx 
      dcb.fOutX = FALSE;                     // XON/XOFF out flow control 
      dcb.fInX = FALSE;                      // XON/XOFF in flow control 
      dcb.fErrorChar = FALSE;                // enable error replacement 
      dcb.fNull = FALSE;                     // enable null stripping 
      dcb.fRtsControl = RTS_CONTROL_ENABLE;  // RTS flow control 
      dcb.fAbortOnError = FALSE;             // abort reads/writes on error 
      dcb.XonLim = 0;                        // transmit XON threshold 
      dcb.XoffLim = 0;                       // transmit XOFF threshold 
      dcb.ByteSize = 8;                      // number of bits/byte, 4-8 
      dcb.Parity = NOPARITY;                 // 0-4=no,odd,even,mark,space 
      dcb.StopBits = ONESTOPBIT;             // 0,1,2 = 1, 1.5, 2 
      dcb.XonChar = 0;                       // Tx and Rx XON character 
      dcb.XoffChar = 1;                      // Tx and Rx XOFF character 
      dcb.ErrorChar = 0;                     // error replacement character 
      dcb.EofChar = 0;                       // end of input character 
      dcb.EvtChar = 0;                       // received event character 

      fRetVal = SetCommState(ComID[portnum], &dcb);
   }

   // check if successfull
   if (!fRetVal)
   {
      CloseHandle(ComID[portnum]);
      ComID[portnum] = 0;
	  return false;
   }

   return true;
}

//---------------------------------------------------------------------------
// Closes the connection to the port.
//
// 'portnum'  - number 0 to MAX_PORTNUM-1.  This number was provided to
//              OpenCOM to indicate the port number.
//
void CloseCOM(int portnum)
{
   // disable event notification and wait for thread
   // to halt
   SetCommMask(ComID[portnum], 0);

   // drop DTR
   EscapeCommFunction(ComID[portnum], CLRDTR);

   // purge any outstanding reads/writes and close device handle
   PurgeComm(ComID[portnum], PURGE_TXABORT | PURGE_RXABORT |
                    PURGE_TXCLEAR | PURGE_RXCLEAR );
   CloseHandle(ComID[portnum]);
   ComID[portnum] = 0;
} 

//---------------------------------------------------------------------------
// Flush the rx and tx buffers
//
// 'portnum'  - number 0 to MAX_PORTNUM-1.  This number was provided to
//              OpenCOM to indicate the port number.
//
void FlushCOM(int portnum)
{
   // purge any information in the buffer
   PurgeComm(ComID[portnum], PURGE_TXABORT | PURGE_RXABORT |
                    PURGE_TXCLEAR | PURGE_RXCLEAR );
}

//--------------------------------------------------------------------------
// Write an array of bytes to the COM port, verify that it was
// sent out.  Assume that baud rate has been set.
//
// 'portnum'  - number 0 to MAX_PORTNUM-1.  This number was provided to
//              OpenCOM to indicate the port number.
// 'outlen'   - number of bytes to write to COM port
// 'outbuf'   - pointer ot an array of bytes to write
//
// Returns:  TRUE(1)  - success 
//           FALSE(0) - failure
//
int WriteCOM(int portnum, int outlen, UCHAR *outbuf)
{
   BOOL fWriteStat;
   DWORD dwBytesWritten=0;
   DWORD ler=0,to;
   int i = 0;
	
   // calculate a timeout
   to = 20 * outlen + 60;


	// reset the write event 
	ResetEvent(osWrite[portnum].hEvent);

	// write the byte
	fWriteStat = WriteFile(ComID[portnum], (LPSTR) &outbuf[0], 
			outlen, &dwBytesWritten, &osWrite[portnum] );

	// check for an error
	if (!fWriteStat)
	  ler = GetLastError();

	   // if not done writting then wait 
	if (!fWriteStat && ler == ERROR_IO_PENDING)
	{                                         
		WaitForSingleObject(osWrite[portnum].hEvent,to);

		// verify all is written correctly
		fWriteStat = GetOverlappedResult(ComID[portnum], &osWrite[portnum], 
		&dwBytesWritten, FALSE); 
	} 

	// check results of write
	if (!fWriteStat || (dwBytesWritten != (DWORD)outlen))
		return 0;
	else
		return 1;
}

//--------------------------------------------------------------------------
// Read an array of bytes to the COM port, verify that it was
// sent out.  Assume that baud rate has been set.
//
// 'portnum'  - number 0 to MAX_PORTNUM-1.  This number was provided to
//               OpenCOM to indicate the port number.
// 'inlen'     - number of bytes to read from COM port
// 'inbuf'     - pointer to a buffer to hold the incomming bytes
//
// Returns: number of characters read
//
int ReadCOM(int portnum, int inlen, UCHAR *inbuf)
{
   DWORD dwLength=0; 
   BOOL fReadStat;
   DWORD ler=0,to;

   // calculate a timeout
   to = 20 * inlen + 60;

   // reset the read event 
   ResetEvent(osRead[portnum].hEvent);

   // read
   fReadStat = ReadFile(ComID[portnum], (LPSTR) &inbuf[0],
                      inlen, &dwLength, &osRead[portnum]) ;
   
   // check for an error
   if (!fReadStat)
      ler = GetLastError();

   // if not done writing then wait 
   if (!fReadStat && ler == ERROR_IO_PENDING)
   {
      // wait until everything is read
      WaitForSingleObject(osRead[portnum].hEvent,to);

      // verify all is read correctly
      fReadStat = GetOverlappedResult(ComID[portnum], &osRead[portnum], 
                   &dwLength, FALSE); 
   }

   // check results
   if (fReadStat)
      return dwLength;
   else
      return 0;
}

