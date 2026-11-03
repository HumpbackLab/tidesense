#ifndef TIDE_CAM_MOCK_H
#define TIDE_CAM_MOCK_H
#ifdef __cplusplus
extern "C" {
#endif
void mock_reset(void);
void mock_push(unsigned count);
void mock_fail(int status);
void mock_cameras(unsigned mask);
int mock_active(void);
void mock_jpeg_fail(int status);
int mock_jpeg_on_this_thread(void);
unsigned mock_jpeg_released(void);
void mock_jpeg_hold(int hold);
void mock_wait_jpeg_started(void);
#ifdef __cplusplus
}
#endif
#endif
