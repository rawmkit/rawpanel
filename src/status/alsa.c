/* SPDX-License-Identifier: MIT */
#include "alsa.h"

#include <alsa/asoundlib.h>
#include <limits.h>
#include <stdbool.h>

static void
alsa_error(FILE *err, const char *operation, int rc)
{
  fprintf(err, "rawpanel: status worker: ALSA %s: %s\n", operation,
          snd_strerror(rc));
}

static int
open_mixer(snd_mixer_t **mixer, const RawpanelStatusSourceConfig *config,
           FILE *err)
{
  int rc;

  *mixer = NULL;
  rc = snd_mixer_open(mixer, 0);
  if (rc < 0) {
    alsa_error(err, "open", rc);
    return -1;
  }
  rc = snd_mixer_attach(*mixer, config->device);
  if (rc < 0) {
    alsa_error(err, "attach", rc);
    goto fail;
  }
  rc = snd_mixer_selem_register(*mixer, NULL, NULL);
  if (rc < 0) {
    alsa_error(err, "register", rc);
    goto fail;
  }
  rc = snd_mixer_load(*mixer);
  if (rc < 0) {
    alsa_error(err, "load", rc);
    goto fail;
  }
  return 0;

fail:
  snd_mixer_close(*mixer);
  *mixer = NULL;
  return -1;
}

static snd_mixer_elem_t *
find_playback_element(snd_mixer_t *mixer,
                      const RawpanelStatusSourceConfig *config, FILE *err)
{
  snd_mixer_elem_t *element;
  snd_mixer_selem_id_t *id = NULL;

  if (snd_mixer_selem_id_malloc(&id) < 0) {
    fputs("rawpanel: status worker: out of memory\n", err);
    return NULL;
  }
  snd_mixer_selem_id_set_index(id, 0);
  snd_mixer_selem_id_set_name(id, config->control);
  element = snd_mixer_find_selem(mixer, id);
  snd_mixer_selem_id_free(id);
  if (!element || !snd_mixer_selem_has_playback_volume(element)) {
    fprintf(err,
            "rawpanel: status worker: ALSA control '%s' on '%s' has no playback volume\n",
            config->control, config->device);
    return NULL;
  }
  return element;
}

static int
sample_mixer(snd_mixer_t *mixer, const RawpanelStatusSourceConfig *config,
             char *buffer, size_t size, RawpanelStatusState *semantic,
             FILE *err)
{
  snd_mixer_elem_t *element;
  long minimum, maximum;
  long double normalized = 0.0L;
  unsigned int channel_count = 0;
  unsigned int percent;
  bool has_switch = false;
  bool any_switch_on = false;
  int written;

  element = find_playback_element(mixer, config, err);
  if (!element)
    return -1;
  if (snd_mixer_selem_get_playback_volume_range(element, &minimum, &maximum) < 0 ||
      maximum <= minimum)
    return -1;

  has_switch = snd_mixer_selem_has_playback_switch(element) != 0;
  for (int i = SND_MIXER_SCHN_FRONT_LEFT; i <= SND_MIXER_SCHN_LAST; ++i) {
    snd_mixer_selem_channel_id_t channel = (snd_mixer_selem_channel_id_t)i;
    long value;

    if (!snd_mixer_selem_has_playback_channel(element, channel))
      continue;
    if (snd_mixer_selem_get_playback_volume(element, channel, &value) < 0)
      return -1;
    if (value < minimum)
      value = minimum;
    if (value > maximum)
      value = maximum;
    normalized += (long double)(value - minimum) /
                  (long double)(maximum - minimum);
    ++channel_count;

    if (has_switch) {
      int on;

      if (snd_mixer_selem_get_playback_switch(element, channel, &on) < 0)
        return -1;
      if (on)
        any_switch_on = true;
    }
  }
  if (!channel_count)
    return -1;

  percent = (unsigned int)(normalized * 100.0L /
                           (long double)channel_count + 0.5L);
  if (percent > 100)
    percent = 100;

  if (has_switch && !any_switch_on) {
    written = snprintf(buffer, size, "vol mute");
    *semantic = RAWPANEL_STATUS_WARNING;
  } else {
    written = snprintf(buffer, size, "vol %u%%", percent);
    *semantic = RAWPANEL_STATUS_NORMAL;
  }
  return written >= 0 && (size_t)written < size ? 0 : -1;
}

int
rawpanel_status_alsa_sample(const RawpanelStatusSourceConfig *config,
                            char *buffer, size_t size,
                            RawpanelStatusState *semantic, FILE *err)
{
  snd_mixer_t *mixer;
  int rc;

  if (open_mixer(&mixer, config, err) < 0)
    return -1;
  rc = sample_mixer(mixer, config, buffer, size, semantic, err);
  snd_mixer_close(mixer);
  return rc;
}

static int
control_mixer(snd_mixer_t *mixer, const RawpanelStatusSourceConfig *config,
              RawpanelStatusControl control, FILE *err)
{
  snd_mixer_elem_t *element;
  long minimum, maximum;

  element = find_playback_element(mixer, config, err);
  if (!element)
    return -1;

  if (control == RAWPANEL_STATUS_CONTROL_VOLUME_MUTE) {
    bool any_on = false;
    bool found = false;

    if (!snd_mixer_selem_has_playback_switch(element)) {
      fprintf(err,
              "rawpanel: status worker: ALSA control '%s' on '%s' has no playback switch\n",
              config->control, config->device);
      return -1;
    }
    for (int i = SND_MIXER_SCHN_FRONT_LEFT; i <= SND_MIXER_SCHN_LAST; ++i) {
      snd_mixer_selem_channel_id_t channel = (snd_mixer_selem_channel_id_t)i;
      int on;

      if (!snd_mixer_selem_has_playback_channel(element, channel))
        continue;
      found = true;
      if (snd_mixer_selem_get_playback_switch(element, channel, &on) < 0)
        return -1;
      if (on)
        any_on = true;
    }
    if (!found)
      return -1;
    for (int i = SND_MIXER_SCHN_FRONT_LEFT; i <= SND_MIXER_SCHN_LAST; ++i) {
      snd_mixer_selem_channel_id_t channel = (snd_mixer_selem_channel_id_t)i;

      if (!snd_mixer_selem_has_playback_channel(element, channel))
        continue;
      if (snd_mixer_selem_set_playback_switch(element, channel,
                                               any_on ? 0 : 1) < 0)
        return -1;
    }
    return 0;
  }

  if (control != RAWPANEL_STATUS_CONTROL_VOLUME_UP &&
      control != RAWPANEL_STATUS_CONTROL_VOLUME_DOWN)
    return -1;
  if (snd_mixer_selem_get_playback_volume_range(element, &minimum, &maximum) < 0 ||
      maximum <= minimum)
    return -1;

  {
    long double scaled = ((long double)maximum - (long double)minimum) *
                         (long double)config->step / 100.0L;
    long delta;
    bool found = false;

    if (scaled < 1.0L)
      delta = 1;
    else if (scaled >= (long double)LONG_MAX)
      delta = LONG_MAX;
    else
      delta = (long)(scaled + 0.5L);
    for (int i = SND_MIXER_SCHN_FRONT_LEFT; i <= SND_MIXER_SCHN_LAST; ++i) {
      snd_mixer_selem_channel_id_t channel = (snd_mixer_selem_channel_id_t)i;
      long double target;
      long value;

      if (!snd_mixer_selem_has_playback_channel(element, channel))
        continue;
      found = true;
      if (snd_mixer_selem_get_playback_volume(element, channel, &value) < 0)
        return -1;
      target = (long double)value +
               (control == RAWPANEL_STATUS_CONTROL_VOLUME_UP
                  ? (long double)delta : -(long double)delta);
      if (target > (long double)maximum)
        target = (long double)maximum;
      if (target < (long double)minimum)
        target = (long double)minimum;
      if (snd_mixer_selem_set_playback_volume(element, channel,
                                               (long)target) < 0)
        return -1;
    }
    if (!found)
      return -1;
  }
  return 0;
}

int
rawpanel_status_alsa_control(const RawpanelStatusSourceConfig *config,
                             RawpanelStatusControl control, FILE *err)
{
  snd_mixer_t *mixer;
  int rc;

  if (open_mixer(&mixer, config, err) < 0)
    return -1;
  rc = control_mixer(mixer, config, control, err);
  snd_mixer_close(mixer);
  return rc;
}
