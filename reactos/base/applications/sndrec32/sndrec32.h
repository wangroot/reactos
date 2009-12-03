#pragma once

#include "resource.h"
#include "audio_api.hpp"



#define MAX_LOADSTRING 100

#define MAINWINDOW_W 350
#define MAINWINDOW_H 190


#define CONTROLS_CX 10

#define INITIAL_BUFREC_SECONDS 30.0f


#define BUTSTART_ID 0
#define BUTEND_ID 1
#define BUTPLAY_ID 2
#define BUTSTOP_ID 3
#define BUTREC_ID 4
#define SLIDER_ID 5

#define BUTTONS_H 30
#define BUTTONS_W 60
#define BUTTONS_CY 100
#define BUTTONS_CX CONTROLS_CX
#define BUTTONS_SPACE 5


#define SLIDER_CX CONTROLS_CX
#define SLIDER_CY 65
#define SLIDER_H 30
#define SLIDER_W 320




struct riff_hdr
{
	DWORD magic;
	DWORD chunksize;
	DWORD format;
};


struct wave_hdr
{

	DWORD Subchunkid;
	DWORD Subchunk1Size;
	WORD AudioFormat;
	WORD NumChannels;
	DWORD SampleRate;
	DWORD ByteRate;
	WORD BlockAlign;
	WORD BitsPerSample;
};

struct data_chunk
{
	DWORD subc;
	DWORD subc_size;
	//unsigned char data[];
};



//
// Functions prototypes
//
LRESULT CALLBACK
Buttons_proc(HWND, UINT, WPARAM, LPARAM);


BOOL
write_wav( TCHAR * );

BOOL
open_wav( TCHAR * );


VOID enable_but( DWORD );
VOID disable_but( DWORD );



void
l_play_finished ( void );

void
l_audio_arrival ( unsigned int );

void
l_buffer_resized ( unsigned int );
