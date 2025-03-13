NDK_TOOLCHAIN_VERSION := clang
APP_OPTIM        := release
#APP_ABI          := armeabi-v7a
APP_ABI          := arm64-v8a
#APP_PLATFORM     := android-19
APP_PLATFORM     := android-21
APP_BUILD_SCRIPT := Android.mk
APP_STL := c++_static #gnustl_static
APP_CPPFLAGS += -std=c++11 -Wl,-z,notext,-Bsymbolic
