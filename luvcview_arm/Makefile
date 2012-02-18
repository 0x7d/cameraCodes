##############################
# spcaview Makefile
##############################

INSTALLROOT=$(PWD)

CC=arm-marvell-linux-gnueabi-gcc
CPP=arm-marvell-linux-gnueabi-g++
INSTALL=install
APP_BINARY=luvcview
BIN=/usr/local/bin
VERSION = 0.2.1

#WARNINGS = -Wall \
#           -Wundef -Wpointer-arith -Wbad-function-cast \
#           -Wcast-align -Wwrite-strings -Wstrict-prototypes \
#           -Wmissing-prototypes -Wmissing-declarations \
#           -Wnested-externs -Winline -Wcast-qual -W \
#           -Wno-unused
#           -Wunused

CFLAGS +=  -O2
CPPFLAGS = $(CFLAGS)

OBJECTS= luvcview.o color.o utils.o v4l2uvc.o avilib.o

all:	luvcview

clean:
	@echo "Cleaning up directory."
	rm -f *.a *.o $(APP_BINARY) core *~ log errlog *.avi

# Applications:
luvcview:	$(OBJECTS)
	$(CC)	$(CFLAGS) $(OBJECTS) $(XPM_LIB)\
		-o $(APP_BINARY)
	chmod 755 $(APP_BINARY)


install: luvcview
	$(INSTALL) -s -m 755 -g root -o root $(APP_BINARY) $(BIN)
	rm -f $(BIN)/uvcview
