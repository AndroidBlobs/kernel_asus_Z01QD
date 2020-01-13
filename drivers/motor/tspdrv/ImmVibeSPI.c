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

/*
** This SPI supports only one actuator.
*/
#define NUM_ACTUATORS       1

/*
** Number of extra buffers supported by the hardware. This determines how many
** extra samples we send to the hardware up front. Normally this should be set
** to 0 for TS3000 and TS4000 systems and set to 1, for example, for TS5000
** systems where the hardware can store more than one buffer of data.
*/
#define NUM_EXTRA_BUFFERS   0

/*
** Include optional ImmVibeSPI_Device_GetStatus in driver.
*/
#define IMMVIBESPI_DEVICE_GETSTATUS_SUPPORT

/*
** Called to disable amp (disable output force)
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_AmpDisable(VibeUInt8 nActuatorIndex)
{
    DbgOut((DBL_INFO, "ImmVibeSPI_ForceOut_AmpDisable.\n"));

    return VIBE_S_SUCCESS;
}

/*
** Called to enable amp (enable output force)
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_AmpEnable(VibeUInt8 nActuatorIndex)
{
    DbgOut((DBL_INFO, "ImmVibeSPI_ForceOut_AmpEnable.\n"));

    return VIBE_S_SUCCESS;
}

/*
** Called at initialization time to set PWM freq, disable amp, etc...
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_Initialize(void)
{
    DbgOut((DBL_INFO, "ImmVibeSPI_ForceOut_Initialize.\n"));

    /* Disable amp */
    ImmVibeSPI_ForceOut_AmpDisable(0);

    return VIBE_S_SUCCESS;
}

/*
** Called at termination time to set PWM freq, disable amp, etc...
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_Terminate(void)
{
    DbgOut((DBL_INFO, "ImmVibeSPI_ForceOut_Terminate.\n"));

    /* Disable amp */
    ImmVibeSPI_ForceOut_AmpDisable(0);

    return VIBE_S_SUCCESS;
}

/*
** Called by the real-time loop to set force output, and enable amp if required
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_SetSamples(VibeUInt8 nActuatorIndex, VibeUInt16 nOutputSignalBitDepth, VibeUInt16 nBufferSizeInBytes, VibeInt8* pForceOutputBuffer)
{
    return VIBE_S_SUCCESS;
}

/*
** Called to set force output frequency parameters
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_SetFrequency(VibeUInt8 nActuatorIndex, VibeUInt16 nFrequencyParameterID, VibeUInt32 nFrequencyParameterValue)
{
    return VIBE_S_SUCCESS;
}

#if 0
/*
** Called to save an IVT data file (pIVT) to a file (szPathName)
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_IVTFile_Save(const VibeUInt8 *pIVT, VibeUInt32 nIVTSize, const char *szPathname)
{
    return VIBE_S_SUCCESS;
}

/*
** Called to delete an IVT file
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_IVTFile_Delete(const char *szPathname)
{
    return VIBE_S_SUCCESS;
}
#endif

/*
** Called to get the device name (device name must be returned as ANSI char)
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_Device_GetName(VibeUInt8 nActuatorIndex, char *szDevName, int nSize)
{
    if ((!szDevName) || (nSize < 1)) return VIBE_E_FAIL;

    strncpy(szDevName, "alsa", nSize-1);
    szDevName[nSize - 1] = '\0';    /* make sure the string is NULL terminated */

    return VIBE_S_SUCCESS;
}

/*
** Called by the TouchSense Player Service/Daemon when an application calls
** ImmVibeGetDeviceCapabilityInt32 with VIBE_DEVCAPTYPE_DRIVER_STATUS.
** The TouchSense Player does not interpret the returned status.
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_Device_GetStatus(VibeUInt8 nActuatorIndex)
{
    return VIBE_S_SUCCESS;
}

#if 0
/*
** Called at initialization time to get the number of actuators
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_Device_GetNum(void)
{
    return NUM_ACTUATORS;
}
#endif
