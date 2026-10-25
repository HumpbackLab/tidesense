#ifndef TIDE_CAM_MOCK_H
#define TIDE_CAM_MOCK_H
#ifdef __cplusplus
extern "C" {
#endif
void mock_reset(void);
void mock_push(unsigned count);
void mock_fail(int status);
int mock_active(void);
#ifdef __cplusplus
}
#endif
#endif
