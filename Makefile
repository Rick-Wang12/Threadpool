
CC = gcc
PROJECT_NAME = libthreadpool.so
PWD           = $(shell pwd)
BUILD_PATH    = $(PWD)/build
SRC_DIR      := $(PWD)/src
SOURCES      := $(wildcard $(SRC_DIR)/*.c)
OBJECTS      := $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_PATH)/%.o)
CFLAGS       += -I$(PWD)/include -fPIC -lpthread 


.PHONY: all
all: $(BUILD_PATH) $(BUILD_PATH)/${PROJECT_NAME} $(TSET_SCRIPTS)
$(BUILD_PATH):
	mkdir -p $(BUILD_PATH)

$(OBJECTS): $(BUILD_PATH)/%.o:$(SRC_DIR)/%.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(BUILD_PATH)/${PROJECT_NAME}: $(OBJECTS)
	${CC} -shared -o $@ $^