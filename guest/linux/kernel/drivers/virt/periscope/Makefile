KCOV_INSTRUMENT_kcov.o := n
KASAN_SANITIZE_kcov.o := n
CFLAGS_kcov.o := $(call cc-option, -fno-conserve-stack -fno-stack-protector)
KCOV_INSTRUMENT_i2c.o := n
KASAN_SANITIZE_i2c.o := n
CFLAGS_i2c.o := $(call cc-option, -fno-conserve-stack -fno-stack-protector)

obj-$(CONFIG_KCOV_VDEV)			+= kcov.o
obj-$(CONFIG_PERISCOPE_I2C)		+= i2c.o
