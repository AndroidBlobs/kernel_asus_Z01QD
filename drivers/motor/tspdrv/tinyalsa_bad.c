/* tinyalsa.c
**
** NOTE: this file was created by copying parts to the tinyalsa library
** and adapting it for use inside the Linux kernel. This file is not part
** of the tinyalsa library. The copyright notice from the tinyalsa source
** code from which this is derived is as follows:
**
** Copyright 2011, The Android Open Source Project
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**     * Redistributions of source code must retain the above copyright
**       notice, this list of conditions and the following disclaimer.
**     * Redistributions in binary form must reproduce the above copyright
**       notice, this list of conditions and the following disclaimer in the
**       documentation and/or other materials provided with the distribution.
**     * Neither the name of The Android Open Source Project nor the names of
**       its contributors may be used to endorse or promote products derived
**       from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY The Android Open Source Project ``AS IS'' AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED. IN NO EVENT SHALL The Android Open Source Project BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
** SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
** CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
** OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
** DAMAGE.
*/

#include <sound/pcm.h>
#include <linux/slab.h>
#include <linux/errno.h>


#define SIZE_MAX_WORKAROUND
#ifdef SIZE_MAX_WORKAROUND
/*
 * fix for obscure preprocessor error
 *
 * include/linux/kernel.h:30:21: error: "size_t" is not defined [-Werror=undef]
 *  #define SIZE_MAX (~(size_t)0)
 */
#ifdef SIZE_MAX
#undef SIZE_MAX
#define SIZE_MAX __SIZE_MAX__
#endif

#endif

#ifndef SIZE_MIN
#define SIZE_MIN 0
#endif

/** A closed range signed interval. */

struct tinyalsa_signed_interval {
	/** The maximum value of the interval */
	ssize_t max;
	/** The minimum value of the interval */
	ssize_t min;
};

/** A closed range unsigned interval. */

struct tinyalsa_unsigned_interval {
	/** The maximum value of the interval */
	size_t max;
	/** The minimum value of the interval */
	size_t min;
};

/** A flag that specifies that the PCM is an output.
 * May not be bitwise AND'd with @ref PCM_IN.
 * Used in @ref pcm_open.
 * @ingroup libtinyalsa-pcm
 */
#define PCM_OUT 0x00000000

/** Specifies that the PCM is an input.
 * May not be bitwise AND'd with @ref PCM_OUT.
 * Used in @ref pcm_open.
 * @ingroup libtinyalsa-pcm
 */
#define PCM_IN 0x10000000

/** Specifies that the PCM will use mmap read and write methods.
 * Used in @ref pcm_open.
 * @ingroup libtinyalsa-pcm
 */
#define PCM_MMAP 0x00000001

/** Specifies no interrupt requests.
 * May only be bitwise AND'd with @ref PCM_MMAP.
 * Used in @ref pcm_open.
 * @ingroup libtinyalsa-pcm
 */
#define PCM_NOIRQ 0x00000002

/** When set, calls to @ref pcm_write
 * for a playback stream will not attempt
 * to restart the stream in the case of an
 * underflow, but will return -EPIPE instead.
 * After the first -EPIPE error, the stream
 * is considered to be stopped, and a second
 * call to pcm_write will attempt to restart
 * the stream.
 * Used in @ref pcm_open.
 * @ingroup libtinyalsa-pcm
 */
#define PCM_NORESTART 0x00000004

/** Specifies monotonic timestamps.
 * Used in @ref pcm_open.
 * @ingroup libtinyalsa-pcm
 */
#define PCM_MONOTONIC 0x00000008

/** If used with @pcm_open and @pcm_params_get,
 * it will not cause the function to block if
 * the PCM is not available. It will also cause
 * the functions @ref pcm_readi and @ref pcm_writei
 * to exit if they would cause the caller to wait.
 * @ingroup libtinyalsa-pcm
 * */
#define PCM_NONBLOCK 0x00000010

/** For inputs, this means the PCM is recording audio samples.
 * For outputs, this means the PCM is playing audio samples.
 * @ingroup libtinyalsa-pcm
 */
#define PCM_STATE_RUNNING 0x03

/** For inputs, this means an overrun occured.
 * For outputs, this means an underrun occured.
 */
#define PCM_STATE_XRUN 0x04

/** For outputs, this means audio samples are played.
 * A PCM is in a draining state when it is coming to a stop.
 */
#define PCM_STATE_DRAINING 0x05

/** Means a PCM is suspended.
 * @ingroup libtinyalsa-pcm
 */
#define PCM_STATE_SUSPENDED 0x07

/** Means a PCM has been disconnected.
 * @ingroup libtinyalsa-pcm
 */
#define PCM_STATE_DISCONNECTED 0x08

enum pcm_format {
    /** Signed, 8-bit */
    PCM_FORMAT_S8 = 1,
    /** Signed 16-bit, little endian */
    PCM_FORMAT_S16_LE = 0,
    /** Signed, 16-bit, big endian */
    PCM_FORMAT_S16_BE = 2,
    /** Signed, 24-bit (32-bit in memory), little endian */
    PCM_FORMAT_S24_LE,
    /** Signed, 24-bit (32-bit in memory), big endian */
    PCM_FORMAT_S24_BE,
    /** Signed, 24-bit, little endian */
    PCM_FORMAT_S24_3LE,
    /** Signed, 24-bit, big endian */
    PCM_FORMAT_S24_3BE,
    /** Signed, 32-bit, little endian */
    PCM_FORMAT_S32_LE,
    /** Signed, 32-bit, big endian */
    PCM_FORMAT_S32_BE,
    /** Max of the enumeration list, not an actual format. */
    PCM_FORMAT_MAX
};

struct pcm_config {
    /** The number of channels in a frame */
    unsigned int channels;
    /** The number of frames per second */
    unsigned int rate;
    /** The number of frames in a period */
    unsigned int period_size;
    /** The number of periods in a PCM */
    unsigned int period_count;
    /** The sample format of a PCM */
    enum pcm_format format;
    /* Values to use for the ALSA start, stop and silence thresholds.  Setting
     * any one of these values to 0 will cause the default tinyalsa values to be
     * used instead.  Tinyalsa defaults are as follows.
     *
     * start_threshold   : period_count * period_size
     * stop_threshold    : period_count * period_size
     * silence_threshold : 0
     */
    /** The minimum number of frames required to start the PCM */
    unsigned int start_threshold;
    /** The minimum number of frames required to stop the PCM */
    unsigned int stop_threshold;
    /** The minimum number of frames to silence the PCM */
    unsigned int silence_threshold;
};

#define PCM_ERROR_MAX 128

struct pcm {
    /** The PCM's file descriptor */
    struct file *fd;
    struct snd_pcm_substream *substream;
    /** Flags that were passed to @ref pcm_open */
    unsigned int flags;
    /** Whether the PCM is running or not */
    int running:1;
    /** Whether or not the PCM has been prepared */
    int prepared:1;
    /** The number of underruns that have occured */
    int underruns;
    /** Size of the buffer */
    unsigned int buffer_size;
    /** The boundary for ring buffer pointers */
    unsigned int boundary;
    /** Description of the last error that occured */
    char error[PCM_ERROR_MAX];
    /** Configuration that was passed to @ref pcm_open */
    struct pcm_config config;
    struct snd_pcm_mmap_status *mmap_status;
    struct snd_pcm_mmap_control *mmap_control;
    struct snd_pcm_sync_ptr *sync_ptr;
    void *mmap_buffer;
    unsigned int noirq_frames_per_msec;
    int wait_for_avail_min;
    /** The delay of the PCM, in terms of frames */
    long pcm_delay;
    /** The subdevice corresponding to the PCM */
    unsigned int subdevice;
};

static struct pcm bad_pcm = {
    .fd = 0,
};

static int oops(struct pcm *pcm, int e, const char *fmt, ...)
{
    va_list ap;
    int sz;

    va_start(ap, fmt);
    vsnprintf(pcm->error, PCM_ERROR_MAX, fmt, ap);
    va_end(ap);
    sz = strlen(pcm->error);

    if (e) {
        snprintf(pcm->error + sz, PCM_ERROR_MAX - sz, ": %d", e);
    }

    DbgOut((DBL_ERROR, pcm->error));

    return -1;
}

#define PARAM_MAX SNDRV_PCM_HW_PARAM_LAST_INTERVAL
#define SNDRV_PCM_HW_PARAMS_NO_PERIOD_WAKEUP (1<<2)

static inline int pcm_hw_param_is_mask(int p)
{
    return (p >= SNDRV_PCM_HW_PARAM_FIRST_MASK) &&
        (p <= SNDRV_PCM_HW_PARAM_LAST_MASK);
}

static inline int pcm_hw_param_is_interval(int p)
{
    return (p >= SNDRV_PCM_HW_PARAM_FIRST_INTERVAL) &&
        (p <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL);
}

static inline const struct snd_interval *pcm_hw_param_get_interval(const struct snd_pcm_hw_params *p, int n)
{
    return &(p->intervals[n - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL]);
}

static inline struct snd_interval *pcm_hw_param_to_interval(struct snd_pcm_hw_params *p, int n)
{
    return &(p->intervals[n - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL]);
}

static inline struct snd_mask *pcm_hw_param_to_mask(struct snd_pcm_hw_params *p, int n)
{
    return &(p->masks[n - SNDRV_PCM_HW_PARAM_FIRST_MASK]);
}

static void pcm_hw_param_set_mask(struct snd_pcm_hw_params *p, int n, unsigned int bit)
{
    if (bit >= SNDRV_MASK_MAX)
        return;
    if (pcm_hw_param_is_mask(n)) {
        struct snd_mask *m = pcm_hw_param_to_mask(p, n);
        m->bits[0] = 0;
        m->bits[1] = 0;
        m->bits[bit >> 5] |= (1 << (bit & 31));
    }
}

static void pcm_hw_param_set_min(struct snd_pcm_hw_params *p, int n, unsigned int val)
{
    if (pcm_hw_param_is_interval(n)) {
        struct snd_interval *i = pcm_hw_param_to_interval(p, n);
        i->min = val;
    }
}

#if 0
static unsigned int pcm_hw_param_get_min(const struct snd_pcm_hw_params *p, int n)
{
    if (pcm_hw_param_is_interval(n)) {
        const struct snd_interval *i = pcm_hw_param_get_interval(p, n);
        return i->min;
    }
    return 0;
}

static unsigned int pcm_hw_param_get_max(const struct snd_pcm_hw_params *p, int n)
{
    if (pcm_hw_param_is_interval(n)) {
        const struct snd_interval *i = pcm_hw_param_get_interval(p, n);
        return i->max;
    }
    return 0;
}
#endif

static void pcm_hw_param_set_int(struct snd_pcm_hw_params *p, int n, unsigned int val)
{
    if (pcm_hw_param_is_interval(n)) {
        struct snd_interval *i = pcm_hw_param_to_interval(p, n);
        i->min = val;
        i->max = val;
        i->integer = 1;
    }
}

static unsigned int pcm_hw_param_get_int(struct snd_pcm_hw_params *p, int n)
{
    if (pcm_hw_param_is_interval(n)) {
        struct snd_interval *i = pcm_hw_param_to_interval(p, n);
        if (i->integer)
            return i->max;
    }
    return 0;
}

static void pcm_hw_param_init(struct snd_pcm_hw_params *p)
{
    int n;

    memset(p, 0, sizeof(*p));
    for (n = SNDRV_PCM_HW_PARAM_FIRST_MASK;
         n <= SNDRV_PCM_HW_PARAM_LAST_MASK; n++) {
            struct snd_mask *m = pcm_hw_param_to_mask(p, n);
            m->bits[0] = ~0;
            m->bits[1] = ~0;
    }
    for (n = SNDRV_PCM_HW_PARAM_FIRST_INTERVAL;
         n <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL; n++) {
            struct snd_interval *i = pcm_hw_param_to_interval(p, n);
            i->min = 0;
            i->max = ~0;
    }
    p->rmask = ~0U;
    p->cmask = 0;
    p->info = ~0U;
}

static unsigned int pcm_format_to_alsa(enum pcm_format format)
{
    switch (format) {

    case PCM_FORMAT_S8:
        return SNDRV_PCM_FORMAT_S8;

    default:
    case PCM_FORMAT_S16_LE:
        return SNDRV_PCM_FORMAT_S16_LE;
    case PCM_FORMAT_S16_BE:
        return SNDRV_PCM_FORMAT_S16_BE;

    case PCM_FORMAT_S24_LE:
        return SNDRV_PCM_FORMAT_S24_LE;
    case PCM_FORMAT_S24_BE:
        return SNDRV_PCM_FORMAT_S24_BE;

    case PCM_FORMAT_S24_3LE:
        return SNDRV_PCM_FORMAT_S24_3LE;
    case PCM_FORMAT_S24_3BE:
        return SNDRV_PCM_FORMAT_S24_3BE;

    case PCM_FORMAT_S32_LE:
        return SNDRV_PCM_FORMAT_S32_LE;
    case PCM_FORMAT_S32_BE:
        return SNDRV_PCM_FORMAT_S32_BE;
    };
}

static unsigned int ta_pcm_format_to_bits(enum pcm_format format)
{
    switch (format) {
    case PCM_FORMAT_S32_LE:
    case PCM_FORMAT_S32_BE:
    case PCM_FORMAT_S24_LE:
    case PCM_FORMAT_S24_BE:
        return 32;
    case PCM_FORMAT_S24_3LE:
    case PCM_FORMAT_S24_3BE:
        return 24;
    default:
    case PCM_FORMAT_S16_LE:
    case PCM_FORMAT_S16_BE:
        return 16;
    case PCM_FORMAT_S8:
        return 8;
    };
}

static unsigned int pcm_bytes_to_frames(const struct pcm *pcm, unsigned int bytes)
{
    return bytes / (pcm->config.channels *
        (ta_pcm_format_to_bits(pcm->config.format) >> 3));
}

#if 0
static unsigned int pcm_frames_to_bytes(const struct pcm *pcm, unsigned int frames)
{
    return frames * pcm->config.channels *
        (ta_pcm_format_to_bits(pcm->config.format) >> 3);
}
#endif

static int pcm_set_config(struct pcm *pcm, const struct pcm_config *config)
{
    struct snd_pcm_hw_params params;
    struct snd_pcm_sw_params sparams;
    int errno;

    if (pcm == NULL)
        return -EFAULT;
    else if (config == NULL) {
        config = &pcm->config;
        pcm->config.channels = 2;
        pcm->config.rate = 48000;
        pcm->config.period_size = 1024;
        pcm->config.period_count = 4;
        pcm->config.format = PCM_FORMAT_S16_LE;
        pcm->config.start_threshold = config->period_count * config->period_size;
        pcm->config.stop_threshold = config->period_count * config->period_size;
        pcm->config.silence_threshold = 0;
    } else
        pcm->config = *config;

    pcm_hw_param_init(&params);
    pcm_hw_param_set_mask(&params, SNDRV_PCM_HW_PARAM_FORMAT,
                   pcm_format_to_alsa(config->format));
    pcm_hw_param_set_mask(&params, SNDRV_PCM_HW_PARAM_SUBFORMAT,
                   SNDRV_PCM_SUBFORMAT_STD);
    pcm_hw_param_set_min(&params, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, config->period_size);
    pcm_hw_param_set_int(&params, SNDRV_PCM_HW_PARAM_SAMPLE_BITS,
                  ta_pcm_format_to_bits(config->format));
    pcm_hw_param_set_int(&params, SNDRV_PCM_HW_PARAM_FRAME_BITS,
                  ta_pcm_format_to_bits(config->format) * config->channels);
    pcm_hw_param_set_int(&params, SNDRV_PCM_HW_PARAM_CHANNELS,
                  config->channels);
    pcm_hw_param_set_int(&params, SNDRV_PCM_HW_PARAM_PERIODS, config->period_count);
    pcm_hw_param_set_int(&params, SNDRV_PCM_HW_PARAM_RATE, config->rate);

    if (pcm->flags & PCM_NOIRQ) {

        if (!(pcm->flags & PCM_MMAP)) {
            oops(pcm, -EINVAL, "noirq only currently supported with mmap().");
            return -EINVAL;
        }

        params.flags |= SNDRV_PCM_HW_PARAMS_NO_PERIOD_WAKEUP;
        pcm->noirq_frames_per_msec = config->rate / 1000;
    }

    if (pcm->flags & PCM_MMAP)
        pcm_hw_param_set_mask(&params, SNDRV_PCM_HW_PARAM_ACCESS,
                   SNDRV_PCM_ACCESS_MMAP_INTERLEAVED);
    else
        pcm_hw_param_set_mask(&params, SNDRV_PCM_HW_PARAM_ACCESS,
                   SNDRV_PCM_ACCESS_RW_INTERLEAVED);

    errno = snd_pcm_kernel_ioctl(pcm->substream, SNDRV_PCM_IOCTL_HW_PARAMS, &params);
    if (errno) {
        int errno_copy = errno;
        oops(pcm, -errno, "cannot set hw params");
        return -errno_copy;
    }

    /* get our refined hw_params */
    pcm->config.period_size = pcm_hw_param_get_int(&params, SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
    pcm->config.period_count = pcm_hw_param_get_int(&params, SNDRV_PCM_HW_PARAM_PERIODS);
    pcm->buffer_size = config->period_count * config->period_size;

	//mark++
	#if 0
    if (pcm->flags & PCM_MMAP) {
        pcm->mmap_buffer = mmap(NULL, pcm_frames_to_bytes(pcm, pcm->buffer_size),
                                PROT_READ | PROT_WRITE, MAP_FILE | MAP_SHARED, pcm->fd, 0);
        if (pcm->mmap_buffer == MAP_FAILED) {
            oops(pcm, -errno, "failed to mmap buffer %d bytes\n",
                 pcm_frames_to_bytes(pcm, pcm->buffer_size));
			return -1;
        }
    }
    #endif
	//end
    memset(&sparams, 0, sizeof(sparams));
    sparams.tstamp_mode = SNDRV_PCM_TSTAMP_ENABLE;
    sparams.period_step = 1;
    sparams.avail_min = 1;

    if (!config->start_threshold) {
        if (pcm->flags & PCM_IN)
            pcm->config.start_threshold = sparams.start_threshold = 1;
        else
            pcm->config.start_threshold = sparams.start_threshold =
                config->period_count * config->period_size / 2;
    } else
        sparams.start_threshold = config->start_threshold;

    /* pick a high stop threshold - todo: does this need further tuning */
    if (!config->stop_threshold) {
        if (pcm->flags & PCM_IN)
            pcm->config.stop_threshold = sparams.stop_threshold =
                config->period_count * config->period_size * 10;
        else
            pcm->config.stop_threshold = sparams.stop_threshold =
                config->period_count * config->period_size;
    }
    else
        sparams.stop_threshold = config->stop_threshold;

	//mark++
	#if 0
    if (!pcm->config.avail_min) {
        if (pcm->flags & PCM_MMAP)
            pcm->config.avail_min = sparams.avail_min = pcm->config.period_size;
        else
            pcm->config.avail_min = sparams.avail_min = 1;
    } else
        sparams.avail_min = config->avail_min;
       #endif
	//end
    sparams.xfer_align = config->period_size / 2; /* needed for old kernels */
    sparams.silence_size = 0;
    sparams.silence_threshold = config->silence_threshold;
    pcm->boundary = sparams.boundary = pcm->buffer_size;

    while (pcm->boundary * 2 <= INT_MAX - pcm->buffer_size)
        pcm->boundary *= 2;

    errno = snd_pcm_kernel_ioctl(pcm->substream, SNDRV_PCM_IOCTL_SW_PARAMS, &sparams);
    if (errno) {
        int errno_copy = errno;
        oops(pcm, -errno, "cannot set sw params");
        return -errno_copy;
    }

    return 0;
}

static int pcm_sync_ptr(struct pcm *pcm, int flags)
{
    int errno;
	printk("%s\n", __func__);
    if (pcm->sync_ptr) {
        pcm->sync_ptr->flags = flags;
        errno = snd_pcm_kernel_ioctl(pcm->substream, SNDRV_PCM_IOCTL_SYNC_PTR, pcm->sync_ptr);
        if (errno < 0) {
            oops(pcm, errno, "failed to sync mmap ptr");
            return -1;
        }
        return 0;
    }
    return -1;
}

static int pcm_hw_mmap_status(struct pcm *pcm)
{
    if (pcm->sync_ptr)
        return 0;
	printk("%s\n", __func__);
	//mark+++
	#if 0
    int page_size = sysconf(_SC_PAGE_SIZE);
    pcm->mmap_status = mmap(NULL, page_size, PROT_READ, MAP_FILE | MAP_SHARED,
                            pcm->fd, SNDRV_PCM_MMAP_OFFSET_STATUS);
    if (pcm->mmap_status == MAP_FAILED)
        pcm->mmap_status = NULL;
    if (!pcm->mmap_status)
        goto mmap_error;

    pcm->mmap_control = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
                             MAP_FILE | MAP_SHARED, pcm->fd, SNDRV_PCM_MMAP_OFFSET_CONTROL);
    if (pcm->mmap_control == MAP_FAILED)
        pcm->mmap_control = NULL;
    if (!pcm->mmap_control) {
        munmap(pcm->mmap_status, page_size);
        pcm->mmap_status = NULL;
        goto mmap_error;
    }
    if (pcm->flags & PCM_MMAP)
        pcm->mmap_control->avail_min = pcm->config.avail_min;
    else
        pcm->mmap_control->avail_min = 1;

    return 0;
    #endif
	//end
	#if 1
    pcm->sync_ptr = kzalloc(sizeof(*pcm->sync_ptr), GFP_KERNEL);
    if (!pcm->sync_ptr)
        return -ENOMEM;
    pcm->mmap_status = &pcm->sync_ptr->s.status;
    pcm->mmap_control = &pcm->sync_ptr->c.control;
    pcm->mmap_control->avail_min = 1;
    pcm_sync_ptr(pcm, 0);
	#else
	mmap_error:
	
		pcm->sync_ptr = calloc(1, sizeof(*pcm->sync_ptr));
		if (!pcm->sync_ptr)
			return -ENOMEM;
		pcm->mmap_status = &pcm->sync_ptr->s.status;
		pcm->mmap_control = &pcm->sync_ptr->c.control;
		if (pcm->flags & PCM_MMAP)
			pcm->mmap_control->avail_min = pcm->config.avail_min;
		else
			pcm->mmap_control->avail_min = 1;
	
		pcm_sync_ptr(pcm, 0);
	#endif

    return 0;
}

static void pcm_hw_munmap_status(struct pcm *pcm) {
	//mark+++
	#if 0
    if (pcm->sync_ptr) {
        kfree(pcm->sync_ptr);
        pcm->sync_ptr = NULL;
    }
    pcm->mmap_status = NULL;
    pcm->mmap_control = NULL;
	#else
    if (pcm->sync_ptr) {
        free(pcm->sync_ptr);
        pcm->sync_ptr = NULL;
    } else {
        int page_size = sysconf(_SC_PAGE_SIZE);
        if (pcm->mmap_status)
            munmap(pcm->mmap_status, page_size);
        if (pcm->mmap_control)
            munmap(pcm->mmap_control, page_size);
    }
    pcm->mmap_status = NULL;
    pcm->mmap_control = NULL;
	#endif
	//dnd
}

struct pcm *pcm_open(unsigned int card, unsigned int device,
                     unsigned int flags, const struct pcm_config *config)
{
    struct pcm *pcm;
    struct snd_pcm_info info;
    struct snd_pcm_file *pcm_file;
    char fn[256];
    int rc;

	printk("%s +++\n", __func__);
    pcm = kzalloc(sizeof(struct pcm), GFP_KERNEL);
    if (!pcm)
        return &bad_pcm;

    snprintf(fn, sizeof(fn), "/dev/snd/pcmC%uD%u%c", card, device,
             flags & PCM_IN ? 'c' : 'p');

    pcm->flags = flags;

    if (flags & PCM_NONBLOCK)
        pcm->fd = filp_open(fn, O_RDWR | O_NONBLOCK, 0);
    else
        pcm->fd = filp_open(fn, O_RDWR, 0);

    if (IS_ERR(pcm->fd)) {
        oops(pcm, -ENOENT, "cannot open device '%s'", fn);
        return pcm;
    }

    pcm_file = (struct snd_pcm_file *)pcm->fd->private_data;
    if (!pcm_file) {
        oops(pcm, -EFAULT, "no pcm_file for '%s'", fn);
        return pcm;
    }

    pcm->substream = pcm_file->substream;
    if (!pcm->substream) {
        oops(pcm, -EFAULT, "no substream for '%s'", fn);
        return pcm;
    }

    if (snd_pcm_kernel_ioctl(pcm->substream, SNDRV_PCM_IOCTL_INFO, &info)) {
        oops(pcm, -EIO, "cannot get info");
        goto fail_close;
    }
    pcm->subdevice = info.subdevice;

    if (pcm_set_config(pcm, config) != 0)
        goto fail_close;

    rc = pcm_hw_mmap_status(pcm);
    if (rc < 0) {
        oops(pcm, rc, "mmap status failed %d", rc);
        goto fail;
    }

#ifdef SNDRV_PCM_IOCTL_TTSTAMP
    if (pcm->flags & PCM_MONOTONIC) {
        int arg = SNDRV_PCM_TSTAMP_TYPE_MONOTONIC;
        rc = snd_pcm_kernel_ioctl(pcm->substream, SNDRV_PCM_IOCTL_TTSTAMP, &arg);
        if (rc < 0) {
            oops(pcm, rc, "cannot set timestamp type %d", rc);
            goto fail;
        }
    }
#endif

    pcm->underruns = 0;
	printk("%s ---\n", __func__);
    return pcm;

fail:
fail_close:
    filp_close(pcm->fd, 0);
    pcm->fd = 0;
	printk("%s ---\n", __func__);
    return pcm;
}

int pcm_close(struct pcm *pcm)
{
    if (!pcm || pcm == &bad_pcm)
        return 0;

    pcm_hw_munmap_status(pcm);
	if(IS_ERR(pcm->fd))
		oops(pcm, -ENOENT, "pcm_close: cannot close device");
	else{
	    if (pcm->fd)
	        filp_close(pcm->fd, 0);
	}
    pcm->prepared = 0;
    pcm->running = 0;
    pcm->buffer_size = 0;
    pcm->fd = 0;
    kfree(pcm);
    return 0;
}

int pcm_is_ready(const struct pcm *pcm)
{
    return pcm && pcm->fd && pcm->substream;
}

int pcm_prepare(struct pcm *pcm)
{
    int errno;

    if (pcm->prepared)
        return 0;

    errno = snd_pcm_kernel_ioctl(pcm->substream, SNDRV_PCM_IOCTL_PREPARE, 0);
    if (errno < 0)
        return oops(pcm, errno, "cannot prepare channel");

    pcm->prepared = 1;
    return 0;
}

#define TINYALSA_SIGNED_INTERVAL_MAX SSIZE_MAX
#define TINYALSA_SIGNED_INTERVAL_MIN SSIZE_MIN

#define TINYALSA_UNSIGNED_INTERVAL_MAX SIZE_MAX
#define TINYALSA_UNSIGNED_INTERVAL_MIN SIZE_MIN

#define TINYALSA_CHANNELS_MAX 32U
#define TINYALSA_CHANNELS_MIN 1U

#define TINYALSA_FRAMES_MAX (ULONG_MAX / (TINYALSA_CHANNELS_MAX * 4))
#define TINYALSA_FRAMES_MIN 0U

#if TINYALSA_FRAMES_MAX > TINYALSA_UNSIGNED_INTERVAL_MAX
#error "Frames max exceeds measurable value."
#endif

#if TINYALSA_FRAMES_MIN < TINYALSA_UNSIGNED_INTERVAL_MIN
#error "Frames min exceeds measurable value."
#endif

int pcm_writei(struct pcm *pcm, const void *data, unsigned int frame_count)
{
    struct snd_xferi x;
    int errno;

    if (pcm->flags & PCM_IN)
        return -EINVAL;
#if UINT_MAX > TINYALSA_FRAMES_MAX
    if (frame_count > TINYALSA_FRAMES_MAX)
        return -EINVAL;
#endif
    if (frame_count > INT_MAX)
        return -EINVAL;

    x.buf = (void*)data;
    x.frames = frame_count;
    x.result = 0;
    for (;;) {
        if (!pcm->running) {
            int prepare_error = pcm_prepare(pcm);
            if (prepare_error)
                return prepare_error;
            errno = snd_pcm_kernel_ioctl(pcm->substream, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &x);
            if (errno)
                return oops(pcm, errno, "cannot write initial data");
            pcm->running = 1;
            return 0;
        }
        errno = snd_pcm_kernel_ioctl(pcm->substream, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &x);
        if (errno) {
            pcm->prepared = 0;
            pcm->running = 0;
            if (errno == EPIPE) {
                /* we failed to make our window -- try to restart if we are
                 * allowed to do so.  Otherwise, simply allow the EPIPE error to
                 * propagate up to the app level */
                pcm->underruns++;
                if (pcm->flags & PCM_NORESTART)
                    return -EPIPE;
                continue;
            }
            return oops(pcm, errno, "cannot write stream data");
        }
        return x.result;
    }
}

int pcm_write(struct pcm *pcm, const void *data, unsigned int count)
{
    return pcm_writei(pcm, data, pcm_bytes_to_frames(pcm, count));
}

int pcm_get_htimestamp(struct pcm *pcm, unsigned int *avail,
                       struct timespec *tstamp)
{
    int frames;
    int rc;
    snd_pcm_uframes_t hw_ptr;

    if (!pcm_is_ready(pcm))
        return -2;

    rc = pcm_sync_ptr(pcm, SNDRV_PCM_SYNC_PTR_APPL|SNDRV_PCM_SYNC_PTR_HWSYNC);
    if (rc < 0)
        return -2;

    if ((pcm->mmap_status->state != PCM_STATE_RUNNING) &&
            (pcm->mmap_status->state != PCM_STATE_DRAINING))
        return -1;

    *tstamp = pcm->mmap_status->tstamp;
    if (tstamp->tv_sec == 0 && tstamp->tv_nsec == 0)
        return -1;

    hw_ptr = pcm->mmap_status->hw_ptr;
    if (pcm->flags & PCM_IN)
        frames = hw_ptr - pcm->mmap_control->appl_ptr;
    else
        frames = hw_ptr + pcm->buffer_size - pcm->mmap_control->appl_ptr;

    if (frames < 0)
        return -1;

    *avail = (unsigned int)frames;

    return 0;
}

