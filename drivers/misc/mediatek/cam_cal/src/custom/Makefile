#
# Copyright (C) 2018 MediaTek Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#

MTK_CUSTOM_PATH := $(srctree)/drivers/misc/mediatek
CAM_CAL_SRC_PATH := $(MTK_CUSTOM_PATH)/cam_cal/src
CAM_CAL_CUSTOM_SRC_PATH := $(CAM_CAL_SRC_PATH)/custom
CAM_CAL_CUSTOM_SRC_LAYOUT_PATH := $(CAM_CAL_CUSTOM_SRC_PATH)/layout
COMMON_VERSION := v2

subdir-ccflags-y += -I$(MTK_CUSTOM_PATH)/imgsensor/inc \
                    -I$(MTK_CUSTOM_PATH)/cam_cal/inc \
                    -I$(CAM_CAL_CUSTOM_SRC_PATH) \
                    -I$(CAM_CAL_SRC_PATH)/common/$(COMMON_VERSION)


# Common code
obj-$(CONFIG_MTK_CAM_CAL) += camera_eeprom.o
camera_eeprom-objs := ../common/$(COMMON_VERSION)/eeprom_driver.o
camera_eeprom-objs += ../common/$(COMMON_VERSION)/eeprom_i2c_common_driver.o
camera_eeprom-objs += ../common/$(COMMON_VERSION)/eeprom_utils.o

# Platform code
camera_eeprom-objs += eeprom_i2c_custom_driver.o
camera_eeprom-objs += cam_cal_list.o

comma := ,
empty :=
space := $(empty) $(empty)

ifeq ($(CONFIG_BUILD_XIAOMI_CAMERA_IMAGE_NAMES),"matisse")
BUILD_PROJECT_NAME := matisse
endif
ifeq ($(CONFIG_BUILD_XIAOMI_CAMERA_IMAGE_NAMES),"rubens")
BUILD_PROJECT_NAME := rubens
endif
ifeq ($(CONFIG_BUILD_XIAOMI_CAMERA_IMAGE_NAMES),"xaga")
BUILD_PROJECT_NAME := xaga
endif
ifeq ($(CONFIG_BUILD_XIAOMI_CAMERA_IMAGE_NAMES),"pearl")
BUILD_PROJECT_NAME := pearl
endif
ifeq ($(CONFIG_BUILD_XIAOMI_CAMERA_IMAGE_NAMES),"plato")
BUILD_PROJECT_NAME := plato
endif

KERNEL_CAM_CAL_SRC := $(notdir $(wildcard $(CAM_CAL_CUSTOM_SRC_LAYOUT_PATH)/$(BUILD_PROJECT_NAME)*.c))
KERNEL_CAM_CAL_OBJ := $(addprefix layout/,$(patsubst %.c, %.o, $(KERNEL_CAM_CAL_SRC)))
KERNEL_CAM_CAL := $(basename $(KERNEL_CAM_CAL_SRC))
CAM_CAL_CONFIG_LIST := $(subst $(space),$(comma),$(foreach e,$(subst ",,$(KERNEL_CAM_CAL)),E($(e))))
subdir-ccflags-y += -D'CAM_CAL_CONFIG_LIST=$(CAM_CAL_CONFIG_LIST)'

camera_eeprom-objs += cam_cal_config.o
camera_eeprom-objs += xiaomi_cam_cal_config.o
camera_eeprom-objs += $(KERNEL_CAM_CAL_OBJ)
