/* i_main.c */

#include "h2stdinc.h"
#include "doomdef.h"
#include "soundst.h"

void main(int argc, char **argv)
{
	myargc = argc; 
	myargv = argv; 
	rfork(RFNOTEG);
	D_DoomMain ();
} 
