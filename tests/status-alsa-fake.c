#include "status-alsa-fake.h"

#include <alsa/asoundlib.h>
#include <string.h>

struct _snd_mixer {
  int marker;
};

struct _snd_mixer_elem {
  int marker;
};

struct _snd_mixer_selem_id {
  int marker;
};

static struct _snd_mixer mixer;
static struct _snd_mixer_elem element;
static long left_volume;
static long right_volume;
static int muted;
static int handled_events;
static int poll_count;

void
rawpanel_test_alsa_reset(void)
{
  left_volume = 50;
  right_volume = 100;
  muted = 0;
  handled_events = 0;
  poll_count = 1;
}

void
rawpanel_test_alsa_set_poll_count(int count)
{
  poll_count = count;
}

long
rawpanel_test_alsa_left_volume(void)
{
  return left_volume;
}

long
rawpanel_test_alsa_right_volume(void)
{
  return right_volume;
}

int
rawpanel_test_alsa_muted(void)
{
  return muted;
}

int
rawpanel_test_alsa_handled_events(void)
{
  return handled_events;
}

int
snd_mixer_open(snd_mixer_t **out, int mode)
{
  (void)mode;
  *out = &mixer;
  return 0;
}

int
snd_mixer_close(snd_mixer_t *handle)
{
  (void)handle;
  return 0;
}

int
snd_mixer_attach(snd_mixer_t *handle, const char *name)
{
  (void)handle;
  return strcmp(name, "default") ? -1 : 0;
}

int
snd_mixer_selem_register(snd_mixer_t *handle,
                         struct snd_mixer_selem_regopt *options,
                         snd_mixer_class_t **classp)
{
  (void)handle;
  (void)options;
  (void)classp;
  return 0;
}

int
snd_mixer_load(snd_mixer_t *handle)
{
  (void)handle;
  return 0;
}

size_t
snd_mixer_selem_id_sizeof(void)
{
  return sizeof(struct _snd_mixer_selem_id);
}

void
snd_mixer_selem_id_set_index(snd_mixer_selem_id_t *id, unsigned int index)
{
  (void)id;
  (void)index;
}

void
snd_mixer_selem_id_set_name(snd_mixer_selem_id_t *id, const char *name)
{
  (void)id;
  (void)name;
}

snd_mixer_elem_t *
snd_mixer_find_selem(snd_mixer_t *handle, const snd_mixer_selem_id_t *id)
{
  (void)handle;
  (void)id;
  return &element;
}

int
snd_mixer_selem_has_playback_volume(snd_mixer_elem_t *elem)
{
  (void)elem;
  return 1;
}

int
snd_mixer_selem_get_playback_volume_range(snd_mixer_elem_t *elem,
                                           long *minimum, long *maximum)
{
  (void)elem;
  *minimum = 0;
  *maximum = 100;
  return 0;
}

int
snd_mixer_selem_has_playback_switch(snd_mixer_elem_t *elem)
{
  (void)elem;
  return 1;
}

int
snd_mixer_selem_has_playback_channel(snd_mixer_elem_t *elem,
                                     snd_mixer_selem_channel_id_t channel)
{
  (void)elem;
  return channel == SND_MIXER_SCHN_FRONT_LEFT ||
         channel == SND_MIXER_SCHN_FRONT_RIGHT;
}

int
snd_mixer_selem_get_playback_volume(snd_mixer_elem_t *elem,
                                    snd_mixer_selem_channel_id_t channel,
                                    long *value)
{
  (void)elem;
  *value = channel == SND_MIXER_SCHN_FRONT_LEFT
             ? left_volume : right_volume;
  return 0;
}

int
snd_mixer_selem_set_playback_volume(snd_mixer_elem_t *elem,
                                    snd_mixer_selem_channel_id_t channel,
                                    long value)
{
  (void)elem;
  if (channel == SND_MIXER_SCHN_FRONT_LEFT)
    left_volume = value;
  else if (channel == SND_MIXER_SCHN_FRONT_RIGHT)
    right_volume = value;
  else
    return -1;
  return 0;
}

int
snd_mixer_selem_get_playback_switch(snd_mixer_elem_t *elem,
                                    snd_mixer_selem_channel_id_t channel,
                                    int *value)
{
  (void)elem;
  (void)channel;
  *value = !muted;
  return 0;
}

int
snd_mixer_selem_set_playback_switch(snd_mixer_elem_t *elem,
                                    snd_mixer_selem_channel_id_t channel,
                                    int value)
{
  (void)elem;
  if (channel != SND_MIXER_SCHN_FRONT_LEFT &&
      channel != SND_MIXER_SCHN_FRONT_RIGHT)
    return -1;
  muted = !value;
  return 0;
}

int
snd_mixer_poll_descriptors_count(snd_mixer_t *handle)
{
  (void)handle;
  return poll_count;
}

int
snd_mixer_poll_descriptors(snd_mixer_t *handle, struct pollfd *pfds,
                           unsigned int count)
{
  (void)handle;
  if (poll_count <= 0)
    return 0;
  if (count != (unsigned int)poll_count)
    return -1;
  pfds[0].fd = 7;
  pfds[0].events = POLLIN;
  return poll_count;
}

int
snd_mixer_poll_descriptors_revents(snd_mixer_t *handle, struct pollfd *pfds,
                                   unsigned int count,
                                   unsigned short *revents)
{
  (void)handle;
  if (poll_count <= 0 || count != (unsigned int)poll_count)
    return -1;
  *revents = pfds[0].revents;
  return 0;
}

int
snd_mixer_handle_events(snd_mixer_t *handle)
{
  (void)handle;
  ++handled_events;
  return 0;
}

const char *
snd_strerror(int error)
{
  (void)error;
  return "test error";
}
