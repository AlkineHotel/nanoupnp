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

.PHONY: all windows clean

all: example/map_port

example/map_port: $(SRCS)
	$(CC) $(CFLAGS) $(INC) -o $@ $^ -lpthread

windows: $(SRCS)
	$(CC) $(CFLAGS) $(INC) -o example/map_port.exe $^ -lws2_32 -liphlpapi

clean:
	rm -f example/map_port example/map_port.exe
