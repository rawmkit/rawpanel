/* SPDX-License-Identifier: MIT */
#include "alsa.h"

#include <alsa/asoundlib.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>

struct RawpanelStatusAlsa {
  snd_mixer_t *mixer;
};

static void
alsa_error(FILE *err, const char *operation, int rc)
{
  fprintf(err, "rawpanel: status worker: ALSA %s: %s\n", operation,
          snd_strerror(rc));
}

void
rawpanel_status_alsa_destroy(RawpanelStatusAlsa **state)
{
  if (!state || !*state)
    return;
  if ((*state)->mixer)
    snd_mixer_close((*state)->mixer);
  free(*state);
  *state = NULL;
}

static int
open_mixer(RawpanelStatusAlsa **state, const RawpanelStatusSourceConfig *config,
           FILE *err)
{
  RawpanelStatusAlsa *alsa;
  int rc;

  if (*state)
    return 0;
  alsa = calloc(1, sizeof(*alsa));
  if (!alsa) {
    fputs("rawpanel: status worker: out of memory\n", err);
    return -1;
  }
  rc = snd_mixer_open(&alsa->mixer, 0);
  if (rc < 0) {
    alsa_error(err, "open", rc);
    goto fail;
  }
  rc = snd_mixer_attach(alsa->mixer, config->device);
  if (rc < 0) {
    alsa_error(err, "attach", rc);
    goto fail;
  }
  rc = snd_mixer_selem_register(alsa->mixer, NULL, NULL);
  if (rc < 0) {
    alsa_error(err, "register", rc);
    goto fail;
  }
  rc = snd_mixer_load(alsa->mixer);
  if (rc < 0) {
    alsa_error(err, "load", rc);
    goto fail;
  }

  *state = alsa;
  return 0;

fail:
  if (alsa->mixer)
    snd_mixer_close(alsa->mixer);
  free(alsa);
  return -1;
}

static snd_mixer_elem_t *
find_playback_element(RawpanelStatusAlsa *state,
                      const RawpanelStatusSourceConfig *config, FILE *err)
{
  snd_mixer_elem_t *element;
  snd_mixer_selem_id_t *id;

  snd_mixer_selem_id_alloca(&id);
  snd_mixer_selem_id_set_index(id, 0);
  snd_mixer_selem_id_set_name(id, config->control);
  element = snd_mixer_find_selem(state->mixer, id);
  if (!element || !snd_mixer_selem_has_playback_volume(element)) {
    fprintf(err,
            "rawpanel: status worker: ALSA control '%s' on '%s' has no playback volume\n",
            config->control, config->device);
    return NULL;
  }
  return element;
}

int
rawpanel_status_alsa_sample(RawpanelStatusAlsa **state,
                        const RawpanelStatusSourceConfig *config,
                        char *buffer, size_t size,
                        RawpanelStatusState *semantic, FILE *err)
{
  snd_mixer_elem_t *element;
  long minimum, maximum;
  long double normalized = 0.0L;
  unsigned int channel_count = 0;
  unsigned int percent;
  bool has_switch = false;
  bool any_switch_on = false;
  int written;

  if (open_mixer(state, config, err) < 0)
    return -1;
  element = find_playback_element(*state, config, err);
  if (!element)
    return -1;
  if (snd_mixer_selem_get_playback_volume_range(element,
                                                 &minimum, &maximum) < 0 ||
      maximum <= minimum) {
    rawpanel_status_alsa_destroy(state);
    return -1;
  }

  has_switch = snd_mixer_selem_has_playback_switch(element) != 0;
  for (int i = SND_MIXER_SCHN_FRONT_LEFT; i <= SND_MIXER_SCHN_LAST; ++i) {
    snd_mixer_selem_channel_id_t channel = (snd_mixer_selem_channel_id_t)i;
    long value;

    if (!snd_mixer_selem_has_playback_channel(element, channel))
      continue;
    if (snd_mixer_selem_get_playback_volume(element, channel,
                                            &value) < 0) {
      return -1;
    }
    if (value < minimum)
      value = minimum;
    if (value > maximum)
      value = maximum;
    normalized += (long double)(value - minimum) /
                  (long double)(maximum - minimum);
    ++channel_count;

    if (has_switch) {
      int on;

      if (snd_mixer_selem_get_playback_switch(element, channel,
                                              &on) < 0) {
        rawpanel_status_alsa_destroy(state);
        return -1;
      }
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
rawpanel_status_alsa_control(RawpanelStatusAlsa **state,
                         const RawpanelStatusSourceConfig *config,
                         RawpanelStatusControl control, FILE *err)
{
  snd_mixer_elem_t *element;
  long minimum, maximum;

  if (open_mixer(state, config, err) < 0)
    return -1;
  element = find_playback_element(*state, config, err);
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
      if (snd_mixer_selem_get_playback_switch(element, channel, &on) < 0) {
        rawpanel_status_alsa_destroy(state);
        return -1;
      }
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
                                               any_on ? 0 : 1) < 0) {
        rawpanel_status_alsa_destroy(state);
        return -1;
      }
    }
    return 0;
  }

  if (control != RAWPANEL_STATUS_CONTROL_VOLUME_UP &&
      control != RAWPANEL_STATUS_CONTROL_VOLUME_DOWN)
    return -1;
  if (snd_mixer_selem_get_playback_volume_range(element, &minimum, &maximum) < 0 ||
      maximum <= minimum) {
    rawpanel_status_alsa_destroy(state);
    return -1;
  }

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
      if (snd_mixer_selem_get_playback_volume(element, channel, &value) < 0) {
        rawpanel_status_alsa_destroy(state);
        return -1;
      }
      target = (long double)value +
               (control == RAWPANEL_STATUS_CONTROL_VOLUME_UP
                  ? (long double)delta : -(long double)delta);
      if (target > (long double)maximum)
        target = (long double)maximum;
      if (target < (long double)minimum)
        target = (long double)minimum;
      if (snd_mixer_selem_set_playback_volume(element, channel,
                                               (long)target) < 0) {
        rawpanel_status_alsa_destroy(state);
        return -1;
      }
    }
    if (!found)
      return -1;
  }
  return 0;
}

size_t
rawpanel_status_alsa_pollfd_count(RawpanelStatusAlsa *state)
{
  int count;

  if (!state || !state->mixer)
    return 0;
  count = snd_mixer_poll_descriptors_count(state->mixer);
  return count > 0 ? (size_t)count : 0;
}

int
rawpanel_status_alsa_pollfds(RawpanelStatusAlsa *state, struct pollfd *pfds,
                         size_t count)
{
  int rc;

  if (!state || !state->mixer || !count || count > UINT_MAX)
    return count ? -1 : 0;
  rc = snd_mixer_poll_descriptors(state->mixer, pfds, (unsigned int)count);
  return rc >= 0 && (size_t)rc == count ? 0 : -1;
}

int
rawpanel_status_alsa_handle_events(RawpanelStatusAlsa **state, struct pollfd *pfds,
                               size_t count, FILE *err)
{
  unsigned short revents = 0;
  int rc;

  if (!state || !*state || !(*state)->mixer || !count || count > UINT_MAX)
    return -1;
  rc = snd_mixer_poll_descriptors_revents((*state)->mixer, pfds,
                                           (unsigned int)count, &revents);
  if (rc < 0) {
    alsa_error(err, "poll revents", rc);
    rawpanel_status_alsa_destroy(state);
    return -1;
  }
  if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
    rawpanel_status_alsa_destroy(state);
    return -1;
  }
  if (!(revents & POLLIN))
    return 0;
  rc = snd_mixer_handle_events((*state)->mixer);
  if (rc < 0) {
    alsa_error(err, "handle events", rc);
    rawpanel_status_alsa_destroy(state);
    return -1;
  }
  return 1;
}
