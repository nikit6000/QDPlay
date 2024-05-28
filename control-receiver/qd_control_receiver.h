#ifndef _QD_CONTROL_RECEIVER_H_
#define _QD_CONTROL_RECEIVER_H_

typedef int (*qd_touch_callback)(bool inPress, uint16_t inX, uint16_t inY);
typedef int (*qd_keyframe_callback)(void);

int qd_input_reconnect(void);
int qd_input_process(qd_touch_callback touch_callback, qd_keyframe_callback keyframe_callback);

#endif