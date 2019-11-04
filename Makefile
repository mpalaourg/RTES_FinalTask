SHELL := /bin/bash

# ==================================================
# COMMANDS

CC = arm-linux-gnueabihf-gcc
CFLAGS = -pthread

# ==================================================
# TARGETS

TARGET = MessengerApp

# ==================================================
# LISTS

all: $(TARGET)
SRCS := $(wildcard *.c)
OBJS := $(SRCS:%.c=%.o)

$(TARGET): $(OBJS)
	@$(CC) $(OBJS) -o $@ $(CFLAGS)

$(OBJS): %.o : %.c
	$(CC) -c $< -o $@ $(CFLAGS)
