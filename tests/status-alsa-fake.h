#ifndef RAWPANEL_TEST_STATUS_ALSA_FAKE_H
#define RAWPANEL_TEST_STATUS_ALSA_FAKE_H

void rawpanel_test_alsa_reset(void);
void rawpanel_test_alsa_set_attach_failure(int enabled);
long rawpanel_test_alsa_left_volume(void);
long rawpanel_test_alsa_right_volume(void);
int rawpanel_test_alsa_muted(void);
int rawpanel_test_alsa_mixer_open(void);
unsigned int rawpanel_test_alsa_open_count(void);
unsigned int rawpanel_test_alsa_close_count(void);

#endif
