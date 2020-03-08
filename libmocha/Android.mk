#
# Copyright (C) 2018 Artyom Bambalov <artem-bambalov@yandex.ru>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES := system/core/init \
                    system/core/base/include
LOCAL_SRC_FILES := init.cpp
LOCAL_MODULE := mocha_init

include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := audio_symbols.c
LOCAL_SHARED_LIBRARIES := libicuuc libicui18n libcutils
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE := libmocha_audio
LOCAL_C_INCLUDES := \
    external/icu/icu4c/source/common

LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)

# Audio HAL
include $(CLEAR_VARS)

LOCAL_MODULE := audio.primary.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := audio_wrapper.c

LOCAL_C_INCLUDES += \
    external/tinyalsa/include \
    system/media/audio_utils/include \
    system/media/audio_effects/include

LOCAL_SHARED_LIBRARIES := liblog libcutils libaudioutils libdl libtinyalsa
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
