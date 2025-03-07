#ifndef KCOV_VDEV_H
#define KCOV_VDEV_H

#define KCOV_SET_IRQ 0x0
#define KCOV_RESET_IRQ 0x4
#define KCOV_CMD_OFFSET 0x10
#define KCOV_ARG_OFFSET 0x20
//#define KCOV_CMD_MMAP 0x30
#define KCOV_RET_OFFSET 0x40
#define KCOV_GET_AREA_OFFSET 0x50
#define KCOV_CCMODE_OFFSET 0x60
#define KCOV_COV_FULL 0x70
#define KCOV_COV_ENABLE 0x80
#define KCOV_COV_DISABLE 0x90
#define KCOV_COV_REMOTE_ENABLE 0xa0
#define KCOV_COV_REMOTE_DISABLE 0xb0
#define KCOV_COV_COLLECT 0xc0
#define KCOV_GMODE_OFFSET 0xf0

//#define KCOV_MMAP			_IOR('c', 201, unsigned long)

#endif
