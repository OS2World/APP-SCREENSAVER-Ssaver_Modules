#
#	sample saver module makefile
#

#
# $$$$$ replace MODULENAME below with the file name of your module $$$$$
#
MODULENAME=hopalong

all:	$(MODULENAME).ssm

# if you do NOT use EMX, put a '#' in front of the following line
#USE_EMX = 1

# if you use the IBM compiler, remove the '#' in the following line
USE_ICC = 1

# if you use the Borland C++ compiler, remove the '#' in the following line
# ! please read the file SAMPLE11.TXT (included in SAMPLE.ZIP) before !
#USE_BCC = 1

$(MODULENAME).res:	$(MODULENAME).rc $(MODULENAME).h ssaver.ico
	rc -r -p -x $(MODULENAME).rc



!ifdef USE_ICC
##### COMPILING WITH ICC #####################################################
#	you MUST use dynmic linking for C runtime library
$(MODULENAME).ssm:	$(MODULENAME).c $(MODULENAME).h $(MODULENAME).res $(MODULENAME).def
	icc /Ss /Gd /O /Gm /Ge- /Ms $(MODULENAME).c $(MODULENAME).def /B"/NOE" /Fe $(MODULENAME).ssm
	rc $(MODULENAME).res $(MODULENAME).ssm
!endif



!ifdef USE_BCC
##### COMPILING WITH BORLAND C++ #############################################
# !!!!! please read the file SAMPLE11.TXT !!!!!
# replace C: with the appropriate drive letter below
LIBPATH		= D:\SVILUPPO\BCOS2\LIB
INCLUDEPATH	= D:\SVILUPPO\BCOS2\INCLUDE
CC		= bcc -L$(LIBPATH) -I$(INCLUDEPATH) -O1 -sd -v- -sm -w

.c.obj:
	$(CC) -c $*.c

$(MODULENAME).ssm:	bccdll0.obj minirtl.obj $(MODULENAME).obj $(MODULENAME).res
	tlink -v- -c -x -Tod -aa -L$(LIBPATH) bccdll0.obj minirtl.obj $(MODULENAME).obj,$(MODULENAME).ssm,,OS2.LIB,$(MODULENAME).def
	rc $(MODULENAME).res $(MODULENAME).ssm

minirtl.obj:	minirtl.c 
$(MODULENAME).obj:	$(MODULENAME).c 

bccdll0.obj:	bccdll0.asm
	tasm -oi -ml bccdll0.asm
!endif



!ifdef USE_EMX
##### COMPILING WITH EMX #####################################################
$(MODULENAME).ssm:	$(MODULENAME).c $(MODULENAME).h $(MODULENAME).res $(MODULENAME).def
	gcc -s -O2 -Wall -ZC++-comments -Zmtd -Zomf -Zdll $(MODULENAME).c -o $(MODULENAME).ssm $(MODULENAME).def $(MODULENAME).res
!endif

