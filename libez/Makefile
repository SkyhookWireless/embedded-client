ARMCC = $(shell which armcc)

ifeq ($(ARMCC), )
CFLAGS = -Wall -Os --std=c99 -DVERSION=\"$(GIT_VERSION)\"
else
CC = armcc
CFLAGS = --c90 --no_strict -Ospace -DVERSION=\"$(GIT_VERSION)\"
endif

GIT_VERSION := $(shell git describe --dirty --always --tags)

# Disposable build products are deposited in build dir
# Durable build products are deposited in bin dir
BUILD_DIR = ../build
BIN_DIR = ../bin
API_DIR = ../libel
SKY_PROTO_DIR = $(API_DIR)/protocol
NANO_PB_DIR = ../.submodules/nanopb
AES_DIR = ../.submodules/tiny-AES128-C

INCLUDES = -I${SKY_PROTO_DIR} -I${NANO_PB_DIR} -I${AES_DIR} -I${API_DIR}

LIBEZ_SRCS = libez.c ezutil.c ezoption.c ezsend.c

LIBEZ_ALL = ${LIBEZ_SRCS} ${PROTO_SRCS} ${TINYAES_SRCS} 
LIBEZ_OBJS = $(addprefix ${BUILD_DIR}/, $(notdir $(LIBEZ_ALL:.c=.o)))

.PHONY: lib

lib: ${BIN_DIR} ${BUILD_DIR} ${BIN_DIR}/libez.a

unit_test: ${BUILD_DIR}/unit_test.o ${BIN_DIR}/libez.a
	$(CC) -lc -lm -o ${BIN_DIR}/unit_test \
	${BUILD_DIR}/unit_test.o ${BIN_DIR}/libez.a

${BIN_DIR}/libez.a: ${LIBEZ_OBJS}
	ar rcs $@ ${LIBEZ_OBJS}

${BIN_DIR} ${BUILD_DIR}:
	mkdir -p $@

${BUILD_DIR}/%.o: %.c
	$(CC) -c $(CFLAGS) ${INCLUDES} -o $@ $<

clean:
	make -C ${SKY_PROTO_DIR} clean
	rm -rf ${BIN_DIR} ${BUILD_DIR}
