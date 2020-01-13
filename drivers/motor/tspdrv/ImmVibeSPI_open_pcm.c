/*
** =============================================================================
**
** File: ImmVibeSPI.c
**
** Description:
**     Device-dependent functions called by Immersion TSP API
**     to control PWM duty cycle, amp enable/disable, save IVT file, etc...
**
**
** Copyright (c) 2012-2018 Immersion Corporation. All Rights Reserved.
**
** This file contains Original Code and/or Modifications of Original Code
** as defined in and that are subject to the GNU Public License v2 -
** (the 'License'). You may not use this file except in compliance with the
** License. You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or contact
** TouchSenseSales@immersion.com.
**
** The Original Code and all software distributed under the License are
** distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
** EXPRESS OR IMPLIED, AND IMMERSION HEREBY DISCLAIMS ALL SUCH WARRANTIES,
** INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY, FITNESS
** FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT. Please see
** the License for the specific language governing rights and limitations
** under the License.
**
** =============================================================================
*/

#ifdef IMMVIBESPIAPI
#undef IMMVIBESPIAPI
#endif
#define IMMVIBESPIAPI static

#include "tinyalsa.c"

/*
 * ALSA audio card... move this to .dtsi?
 */
#define IMMVIBESPI_ALSA_CARD    0
#define IMMVIBESPI_ALSA_DEVICE 17

/*
** Number of actuators this SPI supports
*/
#define NUM_ACTUATORS 1

/*
** Name displayed in TouchSense API and tools (usually product name)
*/
#define DEVICE_NAME "alsa"

/*
 * Audio path uses 16-bit, but TouchSense service sends 8-bit data
 */
typedef VibeInt16 immvibespi_output;

/*
** Force immvibed mono input to audio stereo output
*/
#define IMMVIBESPI_MONO_TO_STEREO

/*
** Check hardware buffer status to determine if more data should be sent.
** If disabled, NUM_EXTRA_BUFFERS will automatically send a fixed number
** of sample periods at beginning of effect but will not speed up or slow
** down due to variations in system performance. This is only an issue
** for very long effects.
*/
//#define IMMVIBESPI_USE_BUFFERFULL

/*
** Prebuffer some number of periods if there is no way to detect
** the status of the output buffer.
*/
#ifndef IMMVIBESPI_USE_BUFFERFULL
#define NUM_EXTRA_BUFFERS  2
#endif

/*
** todo: determine the right time during Android kernel boot process
** process to initialize TouchSense driver, since the audio system
** may not be ready until late in the boot process. This option
** attempts to initialize driver once each time a haptic effect
** plays until initialization succeeds.
*/
#define IMMVIBESPI_DRIVER_INITIALIZATION_SEQUENCE_UNKNOWN

/*
** Uncomment to display all the samples to kernel log (untested)
*/
#define SIMPLE_SETSAMPLES_LOG

IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_Initialize(void);

/*
 * Driver data
 */
static struct immvibespi {
    struct pcm *pcm;
} *immvibespi=0;

IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_AmpDisable(VibeUInt8 nActuatorIndex)
{
    DbgOut((DBL_INFO, "ImmVibeSPI_ForceOut_AmpDisable.\n"));

    return VIBE_S_SUCCESS;
}

IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_AmpEnable(VibeUInt8 nActuatorIndex)
{
    DbgOut((DBL_INFO, "ImmVibeSPI_ForceOut_AmpEnable.\n"));

#ifdef IMMVIBESPI_DRIVER_INITIALIZATION_SEQUENCE_UNKNOWN
    if (!immvibespi) {
        DbgOut((DBL_INFO, "ImmVibeSPI_ForceOut_AmpEnable: trying initialization again...\n"));
        ImmVibeSPI_ForceOut_Initialize();
    }
#endif

    return VIBE_S_SUCCESS;
}

IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_Initialize(void)
{
    // open: /dev/snd/pcmC%uD%u%c
    struct pcm *pcm;
    int card = IMMVIBESPI_ALSA_CARD,
        device = IMMVIBESPI_ALSA_DEVICE,
        //flags = PCM_OUT;
        flags = PCM_OUT | PCM_MMAP | PCM_NOIRQ| PCM_NONBLOCK;
    const struct pcm_config config = {
        .channels = 2,
        //.rate = 8000, /* TouchSense sends 40 samples every 5ms: 40*1000/5 = 8000hz */
        .rate = 48000, /*4800Hz*/
        .format = PCM_FORMAT_S16_LE,
        .period_size = 40, /* 40 samples per 5ms */
        .period_count = 4, /* 2 bytes (16-bit) * 2 channels */
        .start_threshold = 0,
        .silence_threshold = 0,
        .stop_threshold = 0
    };

    DbgOut((DBL_INFO, "ImmVibeSPI_ForceOut_Initialize (card=%d device=%d)\n", card, device));

    pcm = pcm_open(card, device, flags, &config);
    if (!pcm) {
        DbgOut((DBL_ERROR, "ImmVibeSPI_ForceOut_Initialize: pcm_open failed\n"));
        goto err;
    }

    if (!pcm_is_ready(pcm)) {
        DbgOut((DBL_ERROR, "ImmVibeSPI_ForceOut_Initialize: pcm_is_ready failed\n"));
        goto err_close;
    }

    immvibespi = kzalloc(sizeof(struct immvibespi), GFP_KERNEL);
    if (!immvibespi) {
        DbgOut((DBL_ERROR, "ImmVibeSPI_ForceOut_Initialize: kzalloc failed\n"));
        goto err_close;
    }

    immvibespi->pcm = pcm;

    return VIBE_S_SUCCESS;

err_close:
    pcm_close(pcm);    
err:
#ifdef IMMVIBESPI_DRIVER_INITIALIZATION_SEQUENCE_UNKNOWN
    return VIBE_S_SUCCESS;
#else
    return VIBE_E_FAIL;
#endif
}

IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_Terminate(void)
{
    DbgOut((DBL_INFO, "ImmVibeSPI_ForceOut_Terminate.\n"));

    if (immvibespi) {
        if (immvibespi->pcm) {
            pcm_close(immvibespi->pcm);
        }
        kfree(immvibespi);
        immvibespi = 0;
    }

    return VIBE_S_SUCCESS;
}

/*
** Called by the real-time loop to set the force
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_SetSamples(VibeUInt8 nActuatorIndex, VibeUInt16 nOutputSignalBitDepth, VibeUInt16 nBufferSizeInBytes, VibeInt8* pForceOutputBuffer)
{
#ifdef IMMVIBESPI_MONO_TO_STEREO
    immvibespi_output output[VIBE_OUTPUT_SAMPLE_SIZE*2];
#else
    immvibespi_output output[VIBE_OUTPUT_SAMPLE_SIZE];
#endif

    /* output format conversion */
    if (nBufferSizeInBytes < sizeof(output)) {
        int i = 0;

        /* convert 8bit to 16bit */
        if (nOutputSignalBitDepth < sizeof(immvibespi_output)) {
            for (; i < nBufferSizeInBytes; i++) {
                output[i] = ((VibeInt16)pForceOutputBuffer[i]) << 8;
            }
        }

        /* pad with zeros if immvibed sends less than VIBE_OUTPUT_SAMPLE_SIZE samples */
        for (; i < VIBE_OUTPUT_SAMPLE_SIZE; i++) {
            output[i] = 0;
        }

#ifdef IMMVIBESPI_MONO_TO_STEREO
        /* mono input to stereo output */
        //memcpy(output + sizeof(output)/2, output, sizeof(output)/2);
        memcpy(output + VIBE_OUTPUT_SAMPLE_SIZE, output, VIBE_OUTPUT_SAMPLE_SIZE);
#endif

        nBufferSizeInBytes = sizeof(output);
        pForceOutputBuffer = (VibeInt8*)&output[0];
    }

    /* diagnostic print during development */
#ifdef SIMPLE_SETSAMPLES_LOG
    DbgOut((DBL_INFO, "ImmVibeSPI_ForceOut_SetSamples: received %d bytes\n", nBufferSizeInBytes));
#else
    {
        #define VIBE_OUTPUT_SAMPLE_PRINT_SIZE (sizeof(immvibespi_output) * 2 + 1) /* two characters per number plus space */
        char buffer[VIBE_OUTPUT_SAMPLE_SIZE * VIBE_OUTPUT_SAMPLE_PRINT_SIZE];
        int i, count = sizeof(output) / sizeof(immvibespi_output);
        const char *fmt = sizeof(immvibespi_output) == 1 ? " %02hhx" : " %04hx";
        for (i = 0; i < count; i++) {
            snprintf(buffer + (i * VIBE_OUTPUT_SAMPLE_PRINT_SIZE), sizeof(buffer), fmt, output[i]);
        }
        DbgOut((DBL_INFO, "ImmVibeSPI_ForceOut_SetSamples:%s\n", buffer));
    }
#endif

    /* write buffer to audio stream */
    if (immvibespi && immvibespi->pcm) {
        pcm_write(immvibespi->pcm, pForceOutputBuffer, nBufferSizeInBytes);
    }

    /* todo: error handling, currently errors are ignored */

    return VIBE_S_SUCCESS;
}

/*
** Called to set force output frequency parameters
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_SetFrequency(VibeUInt8 nActuatorIndex, VibeUInt16 nFrequencyParameterID, VibeUInt32 nFrequencyParameterValue)
{
    return VIBE_S_SUCCESS;
}

/*
** Called to get the device name (device name must be returned as ANSI char)
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_Device_GetName(VibeUInt8 nActuatorIndex, char *szDevName, int nSize)
{
    if ((!szDevName) || (nSize < 1)) return VIBE_E_FAIL;

    DbgOut((DBL_INFO, "ImmVibeSPI_Device_GetName.\n"));

    /* Append device name */
    if (strlen(szDevName) + strlen(DEVICE_NAME) < nSize - 1)
        strcat(szDevName, DEVICE_NAME);

    /* Guarantee NULL termination */
    szDevName[nSize - 1] = '\0';

    return VIBE_S_SUCCESS;
}

#ifdef IMMVIBESPI_USE_BUFFERFULL
#define BUFFERFULL_TRUE   1
#define BUFFERFULL_FALSE  0
#define BUFFERFULL_ERROR -1
/*
** Check if the amplifier sample buffer is full (not ready for more data).
*/
IMMVIBESPIAPI int ImmVibeSPI_ForceOut_BufferFull(void)
{
    struct timespec ts;
    int res;
    unsigned int avail = 0;

    if (!immvibespi)
        return BUFFERFULL_ERROR;

    res = pcm_get_htimestamp(immvibespi->pcm, &avail, &ts);
    if (res < 0) {
        /* if htimestamp returns -1, underrun may have happened, so let caller wait and try again */
        /* if htimestamp returns -2, communication error happened, so let caller stop checking buffer status */
        return res == -1 ? BUFFERFULL_TRUE : BUFFERFULL_ERROR;
    }

    return avail == 0 ? BUFFERFULL_TRUE : BUFFERFULL_FALSE;
}
#endif

