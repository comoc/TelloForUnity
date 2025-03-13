include $(CLEAR_VARS)

# override strip command to strip all symbols from output library; no need to ship with those..
# cmd-strip = $(TOOLCHAIN_PREFIX)strip $1

#LOCAL_ARM_MODE  := arm
LOCAL_PATH      := $(NDK_PROJECT_PATH)
LOCAL_MODULE    := libTelloVideoDecoder

#FFMPEG_LIB_PATH := $(LOCAL_PATH)/ffmpeg-android-prebuilt-armeabi-v7a/lib
#LOCAL_C_INCLUDES := $(LOCAL_PATH)/ffmpeg-android-prebuilt-armeabi-v7a/include

FFMPEG_LIB_PATH := $(LOCAL_PATH)/ffmpeg-android-prebuilt-arm64-v8a/lib
LOCAL_C_INCLUDES := $(LOCAL_PATH)/ffmpeg-android-prebuilt-arm64-v8a/include

LOCAL_SRC_FILES := RenderAPI.cpp RenderAPI_OpenGLCoreES.cpp RenderingPlugin.cpp TelloVideoDecoder.cpp

LOCAL_LDFLAGS 	:= -L$(FFMPEG_LIB_PATH) -Wl,-z,notext,-Bsymbolic

# LOCAL_LDLIBS    := -Wl,--no-warn-shared-textrel
LOCAL_LDLIBS    := -Wl \
	-llog -lEGL -lGLESv1_CM -lGLESv2 -lz \
	-lavformat -lavcodec -lswresample -lswscale -lavutil -lx264 -lmp3lame -lvpx

LOCAL_STATIC_LIBRARIES := avcodec avformat avutil swscale x264

APP_ALLOW_MISSING_DEPS=true

LOCAL_CFLAGS += -Wl,-z,notext,-Bsymbolic
LOCAL_CXXFLAGS += -Wl,-z,notext,-Bsymbolic
ifeq ($(NDK_DEBUG), 0)
LOCAL_CFLAGS += -DNDEBUG
endif

include $(BUILD_SHARED_LIBRARY)
#$(call import-add-path, $(FFMPEG_LIB_PATH))
