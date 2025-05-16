#ifndef TRANSPORT_LAYER_H
#define TRANSPORT_LAYER_H


int transport_layer_init(void);

void transport_send_frame(struct can_frame *frame);

#endif
