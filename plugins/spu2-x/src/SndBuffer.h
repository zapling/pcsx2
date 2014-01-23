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

#include "SampleConverters.h"

// Developer Note: This is a static class only (all static members).
class SndBuffer
{
private:
	static bool m_underrun_freeze;
	static s32 m_predictData;
	static float lastPct;

	static StereoOut32* sndTempBuffer;
	static StereoOut16* sndTempBuffer16;
	
	static int sndTempProgress;
	static int m_dsp_progress;

	static int m_timestretch_progress;
	static int m_timestretch_writepos;

	static StereoOut32 *m_buffer;
	static s32 m_size;

	static __aligned(4) volatile s32 m_rpos;
	static __aligned(4) volatile s32 m_wpos;
	
	static float lastEmergencyAdj;
	static float cTempo;
	static float eTempo;
	static int ssFreeze;

	static void _InitFail();
	static bool CheckUnderrunStatus( int& nSamples, int& quietSampleCount );

	static void soundtouchInit();
	static void soundtouchClearContents();
	static void soundtouchCleanup();
	static void timeStretchWrite();
	static void timeStretchUnderrun();
	static s32 timeStretchOverrun();

	static void PredictDataWrite( int samples );
	static float GetStatusPct();
	static void UpdateTempoChangeSoundTouch();
	static void UpdateTempoChangeSoundTouch2();

	static void _WriteSamples(StereoOut32* bData, int nSamples);
		
	static void _WriteSamples_Safe(StereoOut32* bData, int nSamples);
	static void _ReadSamples_Safe(StereoOut32* bData, int nSamples);

	static void _WriteSamples_Internal(StereoOut32 *bData, int nSamples);
	static void _DropSamples_Internal(int nSamples);
	static void _ReadSamples_Internal(StereoOut32 *bData, int nSamples);

	static int _GetApproximateDataInBuffer(); 
	
public:
	static void UpdateTempoChangeAsyncMixing();
	static void Init();
	static void Cleanup();
	static void Write( const StereoOut32& Sample );
	static void ClearContents();

	// Note: When using with 32 bit output buffers, the user of this function is responsible
	// for shifting the values to where they need to be manually.  The fixed point depth of
	// the sample output is determined by the SndOutVolumeShift, which is the number of bits
	// to shift right to get a 16 bit result.
	template< typename T >
	static void ReadSamples( T* bData );
};
