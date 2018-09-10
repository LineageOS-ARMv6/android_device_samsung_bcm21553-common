# Copyright (C) 2010 The Android Open Source Project
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

## SAMSUNG_BOOTLOADER is the product model changed into appropriate string parsed by init.
## Example: -GT-I5500 becomes gt-i5500board, -GT-S5830 becomes gt-s5830board, and so on.
SAMSUNG_BOOTLOADER := $(shell echo $(PRODUCT_VERSION_DEVICE_SPECIFIC)board | tr '[A-Z]' '[a-z]' | cut -c 2-)

# Add common package overlay
DEVICE_PACKAGE_OVERLAYS += $(LOCAL_PATH)/overlay

TARGET_SPECIFIC_HEADER_PATH := $(LOCAL_PATH)/include

# These are the hardware-specific configuration files

# Ramdisk
PRODUCT_PACKAGES += \
    recovery.fstab \
    init.$(SAMSUNG_BOOTLOADER).rc \
    init.$(SAMSUNG_BOOTLOADER).bt.rc \
    init.$(SAMSUNG_BOOTLOADER).fs.rc \
    init.$(SAMSUNG_BOOTLOADER).gps.rc \
    init.$(SAMSUNG_BOOTLOADER).sensors.rc \
    init.$(SAMSUNG_BOOTLOADER).usb.rc \
    init.$(SAMSUNG_BOOTLOADER).wifi.rc \
    init.recovery.$(SAMSUNG_BOOTLOADER).rc \
    ueventd.$(SAMSUNG_BOOTLOADER).rc

# Charger
PRODUCT_PACKAGES += \
    charger \
    charger_res_images

# HW drivers
PRODUCT_PACKAGES += \
    libGLES_hgl \
    hwcomposer.bcm21553 \
    gralloc.bcm21553
    
# Lights
PRODUCT_PACKAGES += \
    lights.bcm21553

# Audio
PRODUCT_PACKAGES += \
    audio.primary.bcm21553 \
    audio.a2dp.default \
    audio.usb.default \
    audio.r_submix.default


# Camera
PRODUCT_PACKAGES += \
    camera.bcm21553

# Other
PRODUCT_PACKAGES += \
    CMFileManager \
    make_ext4fs \
    setup_fs

# Device-specific packages
PRODUCT_PACKAGES += \
    SamsungServiceMode

# Usb accessory
PRODUCT_PACKAGES += \
    com.android.future.usb.accessory

# These are the hardware-specific features
PRODUCT_COPY_FILES += \
    frameworks/base/data/etc/handheld_core_hardware.xml:system/etc/permissions/handheld_core_hardware.xml \
    frameworks/base/data/etc/android.hardware.location.gps.xml:system/etc/permissions/android.hardware.location.gps.xml \
    frameworks/base/data/etc/android.hardware.location.xml:system/etc/permissions/android.hardware.location.xml \
    frameworks/base/data/etc/android.hardware.sensor.accelerometer.xml:system/etc/permissions/android.hardware.sensor.accelerometer.xml \
    frameworks/base/data/etc/android.hardware.sensor.compass.xml:system/etc/permissions/android.hardware.sensor.compass.xml \
    frameworks/base/data/etc/android.hardware.sensor.proximity.xml:system/etc/permissions/android.hardware.sensor.proximity.xml \
    frameworks/base/data/etc/android.hardware.telephony.gsm.xml:system/etc/permissions/android.hardware.telephony.gsm.xml \
    frameworks/base/data/etc/android.hardware.touchscreen.multitouch.xml:system/etc/permissions/android.hardware.touchscreen.multitouch.xml \
    frameworks/base/data/etc/android.hardware.touchscreen.multitouch.distinct.xml:system/etc/permissions/android.hardware.touchscreen.multitouch.distinct.xml \
    frameworks/base/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml \
    frameworks/base/data/etc/android.software.sip.xml:system/etc/permissions/android.software.sip.xml \
    frameworks/base/data/etc/android.software.sip.voip.xml:system/etc/permissions/android.software.sip.voip.xml \
    frameworks/base/data/etc/android.hardware.usb.accessory.xml:system/etc/permissions/android.hardware.usb.accessory.xml \
    packages/wallpapers/LivePicker/android.software.live_wallpaper.xml:system/etc/permissions/android.software.live_wallpaper.xml

# Vold config
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/prebuilt/etc/vold.fstab:system/etc/vold.fstab

# Bluetooth config
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/prebuilt/etc/bluetooth/main.conf:system/etc/bluetooth/main.conf

# WIFI
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/prebuilt/etc/wifi/wpa_supplicant.conf:system/etc/wifi/wpa_supplicant.conf \
    $(LOCAL_PATH)/prebuilt/etc/wifi/hostapd.conf:system/etc/wifi/hostapd.conf \
    $(LOCAL_PATH)/prebuilt/bin/get_macaddrs:system/bin/get_macaddrs

# Keylayout
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/prebuilt/usr/keylayout/AVRCP.kl:system/usr/keylayout/AVRCP.kl \
    $(LOCAL_PATH)/prebuilt/usr/keylayout/bcm_headset.kl:system/usr/keylayout/bcm_headset.kl \
    $(LOCAL_PATH)/prebuilt/usr/keylayout/qwerty.kl:system/usr/keylayout/qwerty.kl \
    $(LOCAL_PATH)/prebuilt/usr/keylayout/sec_jack.kl:system/usr/keylayout/sec_jack.kl \
    $(LOCAL_PATH)/prebuilt/usr/keylayout/sec_key.kl:system/usr/keylayout/sec_key.kl \
    $(LOCAL_PATH)/prebuilt/usr/keylayout/sec_keypad.kl:system/usr/keylayout/sec_keypad.kl

# Keychars
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/prebuilt/usr/keychars/qwerty.kcm.bin:system/usr/keychars/qwerty.kcm.bin \
    $(LOCAL_PATH)/prebuilt/usr/keychars/qwerty2.kcm.bin:system/usr/keychars/qwerty2.kcm.bin \
    $(LOCAL_PATH)/prebuilt/usr/keychars/sec_key.kcm.bin:system/usr/keychars/sec_key.kcm.bin

# Touchscreen
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/prebuilt/usr/idc/sec_touchscreen.idc:system/usr/idc/sec_touchscreen.idc \
    $(LOCAL_PATH)/prebuilt/usr/idc/synaptics-rmi-touchscreen.idc:system/usr/idc/synaptics-rmi-touchscreen.idc

# GPS configuration
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/prebuilt/etc/gps.conf:system/etc/gps.conf

# Media profiles
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/prebuilt/etc/media_profiles.xml:system/etc/media_profiles.xml

# Audio policy
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/prebuilt/etc/audio_policy.conf:system/etc/audio_policy.conf

# The OpenGL ES API level that is natively supported by this device.
# This is a 16.16 fixed point number
PRODUCT_PROPERTY_OVERRIDES := \
    ro.opengles.version=131072
    
# Graphics density
ifneq (,$(filter cooperve,$(CM_BUILD)))
PRODUCT_PROPERTY_OVERRIDES += \
    ro.sf.lcd_density=160
else
PRODUCT_PROPERTY_OVERRIDES += \
    ro.sf.lcd_density=120
endif

# Memory
PRODUCT_PROPERTY_OVERRIDES += \
    persist.sys.purgeable_assets=1

# Graphics properties
PRODUCT_PROPERTY_OVERRIDES += \
    debug.sf.hw=1 \
    debug.enabletr=false \
    debug.composition.type=gpu \
    hwui.render_dirty_regions=false \
    hwui.disable_vsync=true \
    hwui.print_config=choice \
    ro.hwui.layer_cache_size=0 \
    persist.sys.strictmode.visual=false \
    persist.sys.use_dithering=0 \
    persist.sys.prefer_16bpp=1

# Misc. Tweaks
PRODUCT_PROPERTY_OVERRIDES += \
    media.stagefright.enable-player=true \
    media.stagefright.enable-meta=false \
    media.stagefright.enable-scan=false \
    media.stagefright.enable-http=true \
    media.stagefright.enable-aac=true \
    media.stagefright.enable-qcp=true

# Dalvik
PRODUCT_PROPERTY_OVERRIDES += \
    dalvik.vm.execution-mode=int:jit \
    dalvik.vm.debug.alloc=0 \
    dalvik.vm.dexopt-data-only=1 \
    dalvik.vm.dexopt-flags=v=a,o=v,m=y,u=y

$(call inherit-product, frameworks/base/build/phone-hdpi-dalvik-heap.mk)

# RIL
PRODUCT_PROPERTY_OVERRIDES += \
    rild.libpath=/system/lib/libbrcm_ril.so \
    ro.ril.disable.power.collapse=0 \
    ro.ril.gprsclass=10 \
    ro.ril.hsxpa=1 \
    ro.telephony.call_ring.delay=0 \
    ro.telephony.default_network=0

# Wifi interface
PRODUCT_PROPERTY_OVERRIDES += \
    wifi.supplicant_scan_interval=60

# USB / SD card
PRODUCT_PROPERTY_OVERRIDES += \
    persist.sys.usb.config=adb,mass_storage \
    persist.service.adb.enable=1 \
    ro.vold.umsdirtyratio=20

# Development & ADB authentication settings
ADDITIONAL_DEFAULT_PROPERTIES += \
    ro.debuggable=1 \
    ro.secure=0 \
    ro.allow.mock.location=0

# Memory
PRODUCT_PROPERTY_OVERRIDES += \
    ro.HOME_APP_ADJ=1

# Extended JNI checks
# The extended JNI checks will cause the system to run more slowly, but they can spot a variety of nasty bugs 
# before they have a chance to cause problems.
# Default=true for development builds, set by android buildsystem.
PRODUCT_PROPERTY_OVERRIDES += \
    dalvik.vm.checkjni=false \
    ro.kernel.android.checkjni=0

# These are the hardware-specific settings that are stored in system properties.
# Note that the only such settings should be the ones that are too low-level to
# be reachable from resources or other mechanisms.
PRODUCT_PROPERTY_OVERRIDES += \
    wifi.interface=eth0 \
    mobiledata.interfaces=pdp0,eth0,gprs,ppp0

# enable Google-specific location features,
# like NetworkLocationProvider and LocationCollector
PRODUCT_PROPERTY_OVERRIDES += \
    ro.com.google.locationfeatures=1 \
    ro.com.google.networklocation=1

# We have enough storage space to hold precise GC data
PRODUCT_TAGS += dalvik.gc.type-precise

# The gps config appropriate for this device
$(call inherit-product, device/common/gps/gps_us_supl.mk)
