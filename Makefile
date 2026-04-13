# Makefile for c-upnp-igd
#
# Targets:
#   make              — build example (Linux/macOS)
#   make windows      — build example for Windows (MinGW cross or native)
#   make clean        — remove build artifacts

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wno-format-truncation -Wno-stringop-truncation
SRCS     = upnp.c example/map_port.c
INC      = -I.

.PHONY: all windows test clean

all: example/map_port test/test_upnp

example/map_port: upnp.c example/map_port.c
	$(CC) $(CFLAGS) $(INC) -o $@ $^ -lpthread

test/test_upnp: upnp.c test/test_upnp.c
	$(CC) $(CFLAGS) $(INC) -o $@ $^ -lpthread

test: test/test_upnp
	./test/test_upnp

windows: upnp.c example/map_port.c test/test_upnp.c
	$(CC) $(CFLAGS) $(INC) -o example/map_port.exe example/map_port.c upnp.c -lws2_32 -liphlpapi
	$(CC) $(CFLAGS) $(INC) -o test/test_upnp.exe   test/test_upnp.c   upnp.c -lws2_32 -liphlpapi

clean:
	rm -f example/map_port example/map_port.exe test/test_upnp test/test_upnp.exe
