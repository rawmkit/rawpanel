#ifndef RAWPANEL_TEST_STATUS_ALSA_FAKE_H
#define RAWPANEL_TEST_STATUS_ALSA_FAKE_H

void rawpanel_test_alsa_reset(void);
void rawpanel_test_alsa_set_poll_count(int count);
long rawpanel_test_alsa_left_volume(void);
long rawpanel_test_alsa_right_volume(void);
int rawpanel_test_alsa_muted(void);
int rawpanel_test_alsa_handled_events(void);

#endif
