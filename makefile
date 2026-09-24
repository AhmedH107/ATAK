SHELL := cmd.exe
.SHELLFLAGS := /C

CC := C:/raylib/w64devkit/bin/gcc.exe

SRC_DIR := src
SQLITE_DIR := third_party/sqlite
RAYLIB_DIR := C:/raylib/raylib/src

MODE ?= debug

ifeq ($(MODE),release)
BUILD_DIR := build/release
APP_FLAGS := -O2 -DNDEBUG
else
BUILD_DIR := build/debug
APP_FLAGS := -O0 -g3
endif

TARGET := $(BUILD_DIR)/mapboard.exe

OBJECTS := \
	$(BUILD_DIR)/main.o \
	$(BUILD_DIR)/tile_db.o \
	$(BUILD_DIR)/sqlite3.o

DEPENDENCIES := $(OBJECTS:.o=.d)

CPPFLAGS := \
	-I$(SRC_DIR) \
	-I$(SQLITE_DIR) \
	-I$(RAYLIB_DIR) \
	-DSQLITE_THREADSAFE=1

COMMON_FLAGS := \
	-std=c11 \
	-MMD \
	-MP

APP_WARNINGS := \
	-Wall \
	-Wextra \
	-Wpedantic

SQLITE_FLAGS := \
	-O2 \
	-DNDEBUG

LDFLAGS := -L$(RAYLIB_DIR)

LDLIBS := \
	-lraylib \
	-lopengl32 \
	-lgdi32 \
	-lwinmm

.PHONY: all app debug release run clean rebuild

all: app

app: $(TARGET)

debug:
	$(MAKE) MODE=debug app

release:
	$(MAKE) MODE=release app

run:
	$(MAKE) MODE=debug app
	$(TARGET)

rebuild: clean app

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/main.o: $(SRC_DIR)/main.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(APP_WARNINGS) $(APP_FLAGS) -c $< -o $@

$(BUILD_DIR)/tile_db.o: $(SRC_DIR)/tile_db.c $(SRC_DIR)/tile_db.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(APP_WARNINGS) $(APP_FLAGS) -c $< -o $@

$(BUILD_DIR)/sqlite3.o: $(SQLITE_DIR)/sqlite3.c $(SQLITE_DIR)/sqlite3.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) $(SQLITE_FLAGS) -c $< -o $@

$(BUILD_DIR):
	if not exist "$@" mkdir "$@"

clean:
	if exist build rmdir /S /Q build

-include $(DEPENDENCIES)