#ifndef CAN_COMMON_H
#define CAN_COMMON_H

#include <linux/can.h>
#include <linux/module.h>

typedef void (*transport_rx_callback_t) (struct can_frame *frame);

#endif