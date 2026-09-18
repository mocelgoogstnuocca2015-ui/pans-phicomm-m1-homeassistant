#ifndef M1_CAPTIVE_PORTAL_H
#define M1_CAPTIVE_PORTAL_H

#include "mico.h"

/* 只在首次 SoftAP 配网时启动；设备复位后线程随系统释放。 */
OSStatus m1_captive_portal_start(void);

#endif
