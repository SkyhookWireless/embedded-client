CFLAGS = -g -Wall -Werror -Os -std=c99


BIN_DIR = ./bin
CONF_DIR = ./conf
LIB_DIR = ../bin/

LIBEL_INCS = ../libel
LIBEZ_INCS = ../libez
JSON_INCS = ${LIB_DIR}/libjson
ZLOG_INCS = ${LIB_DIR}/zlog
AES_INCS = ../.submodules/tiny-AES128-C


INCLUDES = -I. -I${LIBEZ_INCS} -I${LIBEL_INCS} -I${JSON_INCS} -I${AES_INCS}
LIBS = ${LIB_DIR}/libez.a ${LIB_DIR}/libel.a libs/libjson-c.a -lpthread
SIM_NAME = elgsim

SIM_SRCS = elgsim.c scans.c elgconfig.c
SIM_OBJS = $(SIM_SRCS:.c=.o)

.PHONY: ${SIM_NAME}

${SIM_NAME}: ${SIM_OBJS} ${LIBS}
	$(CC) -lc -lm -o ${BIN_DIR}/$(SIM_NAME) ${SIM_OBJS} ${LIBS}

# Explicitly defined dependencies.
elgsim.o: elgsim.c
scans.o: scans.c
send.o: send.c
elgconfig.o: elgconfig.c

elgsim.o:
	$(CC) -c $(CFLAGS) ${INCLUDES} -o $@ $<

scans.o:
	$(CC) -c $(CFLAGS) ${INCLUDES} -o $@ $<

send.o:
	$(CC) -c $(CFLAGS) ${INCLUDES} -o $@ $<

elgconfig.o:
	$(CC) -c $(CFLAGS) ${INCLUDES} -o $@ $<


clean:
	rm  *.o ${BIN_DIR}/$(SIM_NAME)
