/* SPU2-X, A plugin for Emulating the Sound Processing Unit of the Playstation 2
 * Developed and maintained by the Pcsx2 Development Team.
 *
 * Original portions from SPU2ghz are (c) 2008 by David Quintana [gigaherz]
 *
 * SPU2-X is free software: you can redistribute it and/or modify it under the terms
 * of the GNU Lesser General Public License as published by the Free Software Found-
 * ation, either version 3 of the License, or (at your option) any later version.
 *
 * SPU2-X is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with SPU2-X.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

// Number of stereo samples per SndOut block.
// All drivers must work in units of this size when communicating with
// SndOut.
static const int SndOutPacketSize = 64;

// Overall master volume shift; this is meant to be a precision value and does not affect
// actual output volumes.  It converts SPU2 16 bit volumes to 32-bit volumes, and likewise
// downsamples 32 bit samples to 16 bit sound driver output (this way timestretching and
// DSP effects get better precision results)
static const int SndOutVolumeShift = 12;
static const int SndOutVolumeShift32 = 16-SndOutVolumeShift; // shift up, not down

// Samplerate of the SPU2. For accurate playback we need to match this
// exactly.  Trying to scale samplerates and maintain SPU2's Ts timing accuracy
// is too problematic. :)
static const int SampleRate = 48000;

#include "SndBuffer.h"

class SndOut
{
private:
	//////////////////////////////////////////////////////////////////////////////////////////
	// Configuration Vars

	static int m_ApiId;
	static wxString m_Device;
	
	static bool m_WasapiExclusiveMode;
	
	static bool m_SuggestedLatencyMinimal;
	static int m_SuggestedLatencyMS;

	//////////////////////////////////////////////////////////////////////////////////////////
	// Instance vars

	static int writtenSoFar;
	static int writtenLastTime;
	static int availableLastTime;

	static int actualUsedChannels;

	static bool started;
	static void* stream;

public:
	static void* SampleReader;

	//static SndOut();

	static s32 Init();

	static void Close();

	static int GetApiId() { return m_ApiId; }
	static wxString GetDevice () {return m_Device; }
	static int GetSuggestedLatencyMS() { return m_SuggestedLatencyMS; }
	static bool GetSuggestedLatencyMinimal() { return m_SuggestedLatencyMinimal; }
	static bool GetWasapiExclusiveMode() { return m_WasapiExclusiveMode; }

	static void SetApiId (int apiId) { m_ApiId = apiId; }
	static void SetDevice (wxString deviceName) { m_Device = deviceName; }
	static void SetSuggestedLatencyMS (int ms) { m_SuggestedLatencyMS = ms; }
	static void SetSuggestedLatencyMinimal (bool latencyMinimal) { m_SuggestedLatencyMinimal = latencyMinimal; }
	static void SetWasapiExclusiveMode (bool exclusiveMode) { m_WasapiExclusiveMode = exclusiveMode; }

	static int GetEmptySampleCount();

	static void ReadSettings();

	static void SetApiSettings(wxString api);

	static void WriteSettings();

	static s32 Test();
};

// =====================================================================================================

extern bool WavRecordEnabled;

extern void RecordStart();
extern void RecordStop();
extern void RecordWrite( const StereoOut16& sample );

extern s32  DspLoadLibrary(wchar_t *fileName, int modNum);
extern void DspCloseLibrary();
extern int  DspProcess(s16 *buffer, int samples);
extern void DspUpdate(); // to let the Dsp process window messages
