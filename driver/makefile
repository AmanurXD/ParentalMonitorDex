# Configuration
DRIVER_NAME = ParentalMonitorDex
TARGET = $(DRIVER_NAME).sys
ARCH = x64
BUILD = debug

# Directories
SRC_DIR = src
OBJ_DIR = obj/$(ARCH)
OUT_DIR = bin/$(ARCH)

# Source files (minimal WDM build)
SOURCES = $(SRC_DIR)/pmx.c
OBJECTS = $(OBJ_DIR)/pmx.obj

# Compiler and linker
CC = clang
LD = lld-link

# SDK paths
SDK_VER = 10.0.19041.0
SDK_PATH = "C:\Program Files (x86)\Windows Kits\10"
WDK_LIB_PATH = "$(SDK_PATH)\Lib\$(SDK_VER)\km\x64"
WDK_INC_KM = "$(SDK_PATH)\Include\$(SDK_VER)\km"
WDK_INC_SHARED = "$(SDK_PATH)\Include\$(SDK_VER)\shared"

# Includes
INCLUDES = -I$(WDK_INC_KM) \
           -I$(WDK_INC_SHARED)

# Compiler flags
CFLAGS = -target x86_64-pc-windows-msvc \
         -fshort-wchar \
         -mno-stack-arg-probe \
         -fms-extensions \
         -fms-compatibility \
         -nostdlib \
         -D_WIN64 \
         -D_WIN32_WINNT=0x0A00 \
         -DWINVER=0x0A00 \
         -DNTDDI_VERSION=0x0A000000 \
         -DNT_INST \
         -D_AMD64_ \
         -D__KERNEL_MODE__ \
         -D_WINDOWS \
         -D_DEBUG \
         -Wall \
         -O2

# Linker flags
LDFLAGS = /NOLOGO \
          /SUBSYSTEM:NATIVE \
          /DRIVER \
          /DYNAMICBASE:NO \
          /ENTRY:DriverEntry \
          /INCREMENTAL:NO \
          /MANIFEST:NO \
          /DEBUG \
          /OPT:REF \
          /OPT:ICF \
          /NODEFAULTLIB \
          /BASE:0x10000 \
          /VERSION:1.0

# Libraries
LIBS = "$(WDK_LIB_PATH)\ntoskrnl.lib" \
       "$(WDK_LIB_PATH)\hal.lib" \
       "$(WDK_LIB_PATH)\BufferOverflowK.lib" \
       "$(WDK_LIB_PATH)\wmilib.lib"

# Build rules
all: prepare $(OUT_DIR)/$(TARGET)

prepare:
	@if not exist "$(OBJ_DIR)" mkdir "$(OBJ_DIR)"
	@if not exist "$(OUT_DIR)" mkdir "$(OUT_DIR)"

$(OBJ_DIR)/pmx.obj: $(SRC_DIR)/pmx.c $(SRC_DIR)/pmx.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OUT_DIR)/$(TARGET): $(OBJECTS)
	$(LD) $(LDFLAGS) $(OBJECTS) $(LIBS) -out:$@

clean:
	@if exist "$(OBJ_DIR)" rmdir /s /q "$(OBJ_DIR)"
	@if exist "$(OUT_DIR)" rmdir /s /q "$(OUT_DIR)"

.PHONY: all clean prepare
