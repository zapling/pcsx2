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

#include "Global.h"

#define _WIN32_DCOM
#include "Dialogs.h"

#include "portaudio.h"

#include "wchar.h"

#include <vector>

#ifdef __WIN32__
#include "pa_win_wasapi.h"
#include "pa_win_ds.h"
#endif

int SndOut::m_ApiId=-1;
bool SndOut::m_SuggestedLatencyMinimal = true;
int SndOut::m_SuggestedLatencyMS = 20;
int SndOut::actualUsedChannels = 0;

wxString SndOut::m_Device = wxString();
	
bool SndOut::m_WasapiExclusiveMode = false;
int  SndOut::writtenSoFar = 0;
int  SndOut::writtenLastTime = 0;
int  SndOut::availableLastTime = 0;
bool SndOut::started = false;
void* SndOut::stream = NULL;

void* SndOut::SampleReader = NULL;

//////////////////////////////////////////////////////////////////////////////////////////
// Stuff necessary for speaker expansion
class SampleReader
{
public:
	virtual int ReadSamples( const void *inputBuffer, void *outputBuffer,
		unsigned long framesPerBuffer,
		const PaStreamCallbackTimeInfo* timeInfo,
		PaStreamCallbackFlags statusFlags,
		void *userData ) = 0;
};

template<class T>
class ConvertedSampleReader : public SampleReader
{
	int* written;
public:
	ConvertedSampleReader(int* pWritten)
	{
		written = pWritten;
	}

	virtual int ReadSamples( const void *inputBuffer, void *outputBuffer,
		unsigned long framesPerBuffer,
		const PaStreamCallbackTimeInfo* timeInfo,
		PaStreamCallbackFlags statusFlags,
		void *userData )
	{
		T* p1 = (T*)outputBuffer;

		int packets = framesPerBuffer / SndOutPacketSize;

		for(int p=0; p<packets; p++, p1+=SndOutPacketSize )
			SndBuffer::ReadSamples( p1 );

		(*written) += packets * SndOutPacketSize;

		return 0;
	}
};

int PaCallback( const void *inputBuffer, void *outputBuffer,
	unsigned long framesPerBuffer,
	const PaStreamCallbackTimeInfo* timeInfo,
	PaStreamCallbackFlags statusFlags,
	void *userData )
{
	return ((SampleReader*)SndOut::SampleReader)->ReadSamples(inputBuffer,outputBuffer,framesPerBuffer,timeInfo,statusFlags,userData);
}

s32 SndOut::Test()
{
	ReadSettings();
	
	PaError err = Pa_Initialize();
	if( err != paNoError )
	{
		fprintf(stderr,"* SPU2-X: PortAudio error: %s\n", Pa_GetErrorText( err ) );
		return -1;
	}

	err = Pa_Terminate();
	if( err != paNoError )
	{
		fprintf(stderr,"* SPU2-X: PortAudio error: %s\n", Pa_GetErrorText( err ) );
		return -1;
	}

	return 0;
}

s32 SndOut::Init()
{
	started=false;
	stream=NULL;

	ReadSettings();

	PaError err = Pa_Initialize();
	if( err != paNoError )
	{
		fprintf(stderr,"* SPU2-X: PortAudio error: %s\n", Pa_GetErrorText( err ) );
		return -1;
	}
	started=true;

#ifdef __WIN32__
	CoInitialize(NULL);
#endif

	int deviceIndex = -1;

	int apiId = m_ApiId;

	// Assume linux and mac won't have WASAPI, windows won't have ALSA, etc.
	// Since I don't know if OSX has ALSA, I chose to put it last on the list.
	// Prefer the configured api first.
	int preferenceOrder[] = { m_ApiId, paWASAPI, paDirectSound, paCoreAudio, paALSA };
	bool preferenceSeen[] = { false,   false,    false,         false,       false  };
	const int prefs = sizeof(preferenceOrder)/sizeof(preferenceOrder[0]);

	// First API seen in the list, in case none of the preferred APIs are available
	// or unsupported by the current platform
	int  firstId   = -1;

	// Scan the Host Api list for preferences
	for(int i=0;i<Pa_GetHostApiCount();i++)
	{
		const PaHostApiInfo * apiinfo = Pa_GetHostApiInfo(i);
		const int apiType = apiinfo->type;
		if(apiType > 0)
		{
			if(apiinfo->deviceCount > 0)
			{
				for(int j=0;j<prefs;j++)
				{
					if(apiType == preferenceOrder[j])
					{
						preferenceSeen[j] = true; 
					}
				}
				if (firstId < 0)
					firstId = apiinfo->type;
			}
		}
	}

	if(apiId <= 0)
	{
		for(int j=0;j<prefs;j++)
		{
			if(preferenceSeen[j])
			{
				apiId = preferenceOrder[j];
				break;
			}
		}
		if(apiId <= 0) // was not found
			apiId = firstId;
	}

	fprintf(stderr,"* SPU2-X: Enumerating PortAudio Output Devices:\n");
	for(int i=0, j=0;i<Pa_GetDeviceCount();i++)
	{
		const PaDeviceInfo * info = Pa_GetDeviceInfo(i);
		const PaHostApiInfo * apiinfo = Pa_GetHostApiInfo(info->hostApi);

		if(info->maxOutputChannels > 0)
		{
			fprintf(stderr," *** Device #%d: '%s' (%s)", i, info->name, apiinfo->name);
			
			if(apiinfo->type == apiId)
			{
				if(m_Device == wxString::FromAscii(info->name))
				{
					deviceIndex = i;
					fprintf(stderr," (selected)");
				}
			}

			fprintf(stderr,"\n");

			j++;
		}
	}
	fflush(stderr);

	if(deviceIndex<0)
	{
		if(m_Device == L"default")
			fprintf(stderr,"* SPU2-X: Default device requested.\n");
		else
			fprintf(stderr,"* SPU2-X: Device name not specified or device not found, choosing default.\n");
		
		for(int i=0;i<Pa_GetHostApiCount();i++)
		{
			const PaHostApiInfo * apiinfo = Pa_GetHostApiInfo(i);
			if(apiinfo->type == apiId)
			{
				deviceIndex = apiinfo->defaultOutputDevice;
			}
		}
	}
				
	const PaDeviceInfo * devinfo = Pa_GetDeviceInfo(deviceIndex);
	const PaHostApiInfo * apiinfo = Pa_GetHostApiInfo(devinfo->hostApi);
	
	fprintf(stderr, "* SPU2-X: Selected Device #%d: %s (%s)\n", deviceIndex, devinfo->name, apiinfo->name);

	apiId = apiinfo->type;
			
	int speakers;		
	switch(numSpeakers) // speakers = (numSpeakers + 1) *2; ?
	{
		case 0: speakers = 2; break; // Stereo
		case 1: speakers = 4; break; // Quadrafonic
		case 2: speakers = 6; break; // Surround 5.1
		case 3: speakers = 8; break; // Surround 7.1
		default: speakers = 2;
	}
	actualUsedChannels = std::min(speakers, devinfo->maxOutputChannels);

	switch( actualUsedChannels )
	{
		case 0:
		case 1:
		case 2:
			fprintf(stderr, "* SPU2 > Using normal 2 speaker stereo output.\n" );
			SampleReader = new ConvertedSampleReader<Stereo20Out32>(&writtenSoFar);
			actualUsedChannels = 2;
		break;

		case 3:
			fprintf(stderr, "* SPU2 > 2.1 speaker expansion enabled.\n" );
			SampleReader = new ConvertedSampleReader<Stereo21Out32>(&writtenSoFar);
		break;

		case 4:
			fprintf(stderr, "* SPU2 > 4 speaker expansion enabled [quadraphonic]\n" );
			SampleReader = new ConvertedSampleReader<Stereo40Out32>(&writtenSoFar);
		break;

		case 5:
			fprintf(stderr, "* SPU2 > 4.1 speaker expansion enabled.\n" );
			SampleReader = new ConvertedSampleReader<Stereo41Out32>(&writtenSoFar);
		break;

		case 6:
		case 7:
			switch(dplLevel)
			{
			case 0:
				fprintf(stderr, "* SPU2 > 5.1 speaker expansion enabled.\n" );
				SampleReader = new ConvertedSampleReader<Stereo51Out32>(&writtenSoFar);   //"normal" stereo upmix
				break;
			case 1:
				fprintf(stderr, "* SPU2 > 5.1 speaker expansion with basic ProLogic dematrixing enabled.\n" );
				SampleReader = new ConvertedSampleReader<Stereo51Out32Dpl>(&writtenSoFar); // basic Dpl decoder without rear stereo balancing
				break;
			case 2:
				fprintf(stderr, "* SPU2 > 5.1 speaker expansion with experimental ProLogicII dematrixing enabled.\n" );
				SampleReader = new ConvertedSampleReader<Stereo51Out32DplII>(&writtenSoFar); //gigas PLII
				break;
			}
			actualUsedChannels = 6; // we do not support 7.0 or 6.2 configurations, downgrade to 5.1
		break;

		default:	// anything 8 or more gets the 7.1 treatment!
			fprintf(stderr, "* SPU2 > 7.1 speaker expansion enabled.\n" );
			SampleReader = new ConvertedSampleReader<Stereo71Out32>(&writtenSoFar);
			actualUsedChannels = 8; // we do not support 7.2 or more, downgrade to 7.1
		break;
	}

#ifdef __WIN32__

	int channelMask = 0;	
	if (actualUsedChannels > 2)
	{
		switch( actualUsedChannels )
		{
			case 3: channelMask = PAWIN_SPEAKER_STEREO | PAWIN_SPEAKER_LOW_FREQUENCY; break;
			case 4: channelMask = PAWIN_SPEAKER_QUAD; break;
			case 5: channelMask = PAWIN_SPEAKER_QUAD | PAWIN_SPEAKER_LOW_FREQUENCY; break;
			case 6: channelMask = PAWIN_SPEAKER_5POINT1; break;		
			case 8: channelMask = PAWIN_SPEAKER_7POINT1; break;
		}
	}
		
	void* infoPtr = NULL;
	
	PaWasapiStreamInfo infoWasapi = {
		sizeof(PaWasapiStreamInfo),
		paWASAPI,
		1,
		m_WasapiExclusiveMode ? paWinWasapiExclusive : 0 |
		channelMask ? paWinWasapiUseChannelMask : 0,
		channelMask
	};
		
	PaWinDirectSoundStreamInfo infoDS = {
		sizeof(PaWinDirectSoundStreamInfo),
		paDirectSound,
		2,
		channelMask ? paWinDirectSoundUseChannelMask : 0,
		0,
		channelMask
	};

	if(apiId == paWASAPI && (m_WasapiExclusiveMode || channelMask))
	{
		infoPtr = &infoWasapi;
	}
	else if(apiId == paDirectSound && channelMask)
	{
		infoPtr = &infoDS;
	}
#elif __LINUX__
	// I don't think we need extra extensions -- Gregory
	void* infoPtr = NULL;
#endif

	PaTime minimalSuggestedLatency = devinfo->defaultLowOutputLatency;
	PaStreamParameters outParams = {
			deviceIndex, actualUsedChannels, paInt32,
			std::max((float)minimalSuggestedLatency , 
				m_SuggestedLatencyMinimal ?	(SndOutPacketSize/(float)SampleRate)
										  : (m_SuggestedLatencyMS/1000.0f)),
			infoPtr
		};

	err = Pa_OpenStream(&stream, NULL, &outParams, SampleRate, SndOutPacketSize, paNoFlag, PaCallback, NULL);
	if( err != paNoError )
	{
		fprintf(stderr,"* SPU2-X: PortAudio error creating output stream: %s\n", Pa_GetErrorText( err ) );
		Pa_Terminate();
		return -1;
	}
	
	const PaStreamInfo * info = Pa_GetStreamInfo(stream);
	if(info)
	{
		fprintf(stderr,"* SPU2-X: Output stream latency: %f ms\n", info->outputLatency * 1000 );
	}
	
	err = Pa_StartStream( stream );
	if( err != paNoError )
	{
		fprintf(stderr,"* SPU2-X: PortAudio error starting the stream: %s\n", Pa_GetErrorText( err ) );
		Pa_CloseStream(stream);
		stream=NULL;
		Pa_Terminate();
		return -1;
	}
	return 0;
}

void SndOut::Close()
{
#ifdef __WIN32__
	CoUninitialize();
#endif

	PaError err;
	if(started)
	{
		if(stream)
		{
			if(Pa_IsStreamActive(stream))
			{
				err = Pa_StopStream(stream);
				if( err != paNoError )
					fprintf(stderr,"* SPU2-X: PortAudio error: %s\n", Pa_GetErrorText( err ) );
			}

			err = Pa_CloseStream(stream);
			if( err != paNoError )
				fprintf(stderr,"* SPU2-X: PortAudio error: %s\n", Pa_GetErrorText( err ) );

			stream=NULL;
		}

		// Seems to do more harm than good.
		//PaError err = Pa_Terminate();
		//if( err != paNoError )
		//	fprintf(stderr,"* SPU2-X: PortAudio error: %s\n", Pa_GetErrorText( err ) );

		started=false;
	}
}
	
int SndOut::GetEmptySampleCount()
{
	long availableNow = Pa_GetStreamWriteAvailable(stream);

	int playedSinceLastTime = (writtenSoFar - writtenLastTime) + (availableNow - availableLastTime);
	writtenLastTime = writtenSoFar;
	availableLastTime = availableNow;

	// Lowest resolution here is the SndOutPacketSize we use.
	return playedSinceLastTime;
}
	
void SndOut::ReadSettings()
{
	wxString api( L"EMPTYEMPTYEMPTY" );
	m_Device = L"EMPTYEMPTYEMPTY";
#ifdef __LINUX__
	// By default on linux use the ALSA API (+99% users) -- Gregory
	CfgReadStr( L"PORTAUDIO", L"HostApi", api, L"ALSA" );
#else
	CfgReadStr( L"PORTAUDIO", L"HostApi", api, L"Unknown" );
#endif
	CfgReadStr( L"PORTAUDIO", L"Device", m_Device, L"default" );

	SetApiSettings(api);

	m_WasapiExclusiveMode = CfgReadBool( L"PORTAUDIO", L"Wasapi_Exclusive_Mode", false);
	m_SuggestedLatencyMinimal = CfgReadBool( L"PORTAUDIO", L"Minimal_Suggested_Latency", true);
	m_SuggestedLatencyMS = CfgReadInt( L"PORTAUDIO", L"Manual_Suggested_Latency_MS", 20);
		
	if( m_SuggestedLatencyMS < 10 ) m_SuggestedLatencyMS = 10;
	if( m_SuggestedLatencyMS > 200 ) m_SuggestedLatencyMS = 200;
}

void SndOut::SetApiSettings(wxString api)
{
	m_ApiId = -1;
	if(api == L"InDevelopment") m_ApiId = paInDevelopment; /* use while developing support for a new host API */
	if(api == L"DirectSound")	m_ApiId = paDirectSound;
	if(api == L"MME")			m_ApiId = paMME;
	if(api == L"ASIO")			m_ApiId = paASIO;
	if(api == L"SoundManager")	m_ApiId = paSoundManager;
	if(api == L"CoreAudio")		m_ApiId = paCoreAudio;
	if(api == L"OSS")			m_ApiId = paOSS;
	if(api == L"ALSA")			m_ApiId = paALSA;
	if(api == L"AL")			m_ApiId = paAL;
	if(api == L"BeOS")			m_ApiId = paBeOS;
	if(api == L"WDMKS")			m_ApiId = paWDMKS;
	if(api == L"JACK")			m_ApiId = paJACK;
	if(api == L"WASAPI")		m_ApiId = paWASAPI;
	if(api == L"AudioScienceHPI") m_ApiId = paAudioScienceHPI;
}

void SndOut::WriteSettings()
{
	wxString api;
	switch(m_ApiId)
	{
	case paInDevelopment:	api = L"InDevelopment"; break; /* use while developing support for a new host API */
	case paDirectSound:		api = L"DirectSound"; break;
	case paMME:				api = L"MME"; break;
	case paASIO:			api = L"ASIO"; break;
	case paSoundManager:	api = L"SoundManager"; break;
	case paCoreAudio:		api = L"CoreAudio"; break;
	case paOSS:				api = L"OSS"; break;
	case paALSA:			api = L"ALSA"; break;
	case paAL:				api = L"AL"; break;
	case paBeOS:			api = L"BeOS"; break;
	case paWDMKS:			api = L"WDMKS"; break;
	case paJACK:			api = L"JACK"; break;
	case paWASAPI:			api = L"WASAPI"; break;
	case paAudioScienceHPI: api = L"AudioScienceHPI"; break;
	default: api = L"Unknown";
	}

	CfgWriteStr( L"PORTAUDIO", L"HostApi", api);
	CfgWriteStr( L"PORTAUDIO", L"Device", m_Device);

	CfgWriteBool( L"PORTAUDIO", L"Wasapi_Exclusive_Mode", m_WasapiExclusiveMode);
	CfgWriteBool( L"PORTAUDIO", L"Minimal_Suggested_Latency", m_SuggestedLatencyMinimal);
	CfgWriteInt( L"PORTAUDIO", L"Manual_Suggested_Latency_MS", m_SuggestedLatencyMS);
}
