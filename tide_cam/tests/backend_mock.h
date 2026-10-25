#ifndef TIDE_CAM_MOCK_H
#define TIDE_CAM_MOCK_H
void mock_reset(void);
void mock_push(unsigned count);
void mock_fail(int status);
int mock_active(void);
#endif
