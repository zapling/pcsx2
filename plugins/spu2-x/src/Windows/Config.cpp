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
#include "Dialogs.h"

#include "portaudio.h"

#ifdef PCSX2_DEVBUILD
static const int LATENCY_MAX = 3000;
#else
static const int LATENCY_MAX = 750;
#endif

static const int LATENCY_MIN = 3;
static const int LATENCY_MIN_TS = 30;

// MIXING
int Interpolation = 4;
/* values:
		0: no interpolation (use nearest)
		1. linear interpolation
		2. cubic interpolation
		3. hermite interpolation
		4. catmull-rom interpolation
*/

bool EffectsDisabled = false;
float FinalVolume;
bool postprocess_filter_enabled = 1;
bool postprocess_filter_dealias = false;

// OUTPUT
int SndOutLatencyMS = 100;
int SynchMode = 0; // Time Stretch, Async or Disabled

bool DisableOutput = false;

CONFIG_WAVEOUT Config_WaveOut;
CONFIG_XAUDIO2 Config_XAudio2;

// DSP
bool dspPluginEnabled = false;
int  dspPluginModule = 0;
wchar_t dspPlugin[256];

int numSpeakers = 0;

int dplLevel = 0;

/*****************************************************************************/

void ReadSettings()
{
	Interpolation = CfgReadInt( L"MIXING",L"Interpolation", 4 );	
	EffectsDisabled = CfgReadBool( L"MIXING", L"Disable_Effects", false );
	postprocess_filter_dealias = CfgReadBool( L"MIXING", L"DealiasFilter", false );
	FinalVolume = ((float)CfgReadInt( L"MIXING", L"FinalVolume", 100 )) / 100;
		
	if ( FinalVolume > 1.0f)
		FinalVolume = 1.0f;

	SynchMode = CfgReadInt( L"OUTPUT", L"Synch_Mode", 0);
	numSpeakers = CfgReadInt( L"OUTPUT", L"SpeakerConfiguration", 0);
	dplLevel = CfgReadInt( L"OUTPUT", L"DplDecodingLevel", 0);
	SndOutLatencyMS = CfgReadInt(L"OUTPUT",L"Latency", 100);
	DisableOutput = CfgReadBool(L"OUTPUT",L"DisableOutput", false);
	
	if((SynchMode == 0) && (SndOutLatencyMS < LATENCY_MIN_TS)) // can't use low-latency with timestretcher atm
		SndOutLatencyMS = LATENCY_MIN_TS;
	else if(SndOutLatencyMS < LATENCY_MIN)
		SndOutLatencyMS = LATENCY_MIN;

	CfgReadStr( L"DSP PLUGIN",L"Filename",dspPlugin,255,L"");
	dspPluginModule = CfgReadInt(L"DSP PLUGIN",L"ModuleNum",0);
	dspPluginEnabled= CfgReadBool(L"DSP PLUGIN",L"Enabled",false);

	// Read DSOUNDOUT and WAVEOUT configs:
	CfgReadStr( L"WAVEOUT", L"Device", Config_WaveOut.Device, L"default" );
	Config_WaveOut.NumBuffers = CfgReadInt( L"WAVEOUT", L"Buffer_Count", 4 );

	SndOut::ReadSettings();

	SoundtouchCfg::ReadSettings();
	DebugConfig::ReadSettings();

	// Sanity Checks
	// -------------

	Clampify( SndOutLatencyMS, LATENCY_MIN, LATENCY_MAX );
}

/*****************************************************************************/

void WriteSettings()
{
	CfgWriteInt(L"MIXING",L"Interpolation",Interpolation);

	CfgWriteBool(L"MIXING",L"Disable_Effects",EffectsDisabled);
	CfgWriteBool(L"MIXING",L"DealiasFilter",postprocess_filter_dealias);
	CfgWriteInt(L"MIXING",L"FinalVolume",(int)(FinalVolume * 100 + 0.5f));

	CfgWriteInt(L"OUTPUT",L"Latency", SndOutLatencyMS);
	CfgWriteInt(L"OUTPUT",L"Synch_Mode", SynchMode);
	CfgWriteInt(L"OUTPUT",L"SpeakerConfiguration", numSpeakers);
	CfgWriteInt(L"OUTPUT",L"DplDecodingLevel", dplLevel);
	CfgWriteBool(L"OUTPUT",L"DisableOutput", DisableOutput);

	if( Config_WaveOut.Device.empty() ) Config_WaveOut.Device = L"default";
	CfgWriteStr(L"WAVEOUT",L"Device",Config_WaveOut.Device);
	CfgWriteInt(L"WAVEOUT",L"Buffer_Count",Config_WaveOut.NumBuffers);

	CfgWriteStr(L"DSP PLUGIN",L"Filename",dspPlugin);
	CfgWriteInt(L"DSP PLUGIN",L"ModuleNum",dspPluginModule);
	CfgWriteBool(L"DSP PLUGIN",L"Enabled",dspPluginEnabled);
	
	SndOut::WriteSettings();
	SoundtouchCfg::WriteSettings();
	DebugConfig::WriteSettings();

}

int pa_init=0;
BOOL CALLBACK ConfigProc(HWND hWnd,UINT uMsg,WPARAM wParam,LPARAM lParam)
{
	int wmId,wmEvent;
	wchar_t temp[384]={0};

	switch(uMsg)
	{
		case WM_PAINT:
			return FALSE;

		case WM_INITDIALOG:
		{
			SendDialogMsg( hWnd, IDC_INTERPOLATE, CB_RESETCONTENT,0,0 );
			SendDialogMsg( hWnd, IDC_INTERPOLATE, CB_ADDSTRING,0,(LPARAM) L"0 - Nearest (fastest/bad quality)" );
			SendDialogMsg( hWnd, IDC_INTERPOLATE, CB_ADDSTRING,0,(LPARAM) L"1 - Linear (simple/okay sound)" );
			SendDialogMsg( hWnd, IDC_INTERPOLATE, CB_ADDSTRING,0,(LPARAM) L"2 - Cubic (artificial highs)" );
			SendDialogMsg( hWnd, IDC_INTERPOLATE, CB_ADDSTRING,0,(LPARAM) L"3 - Hermite (better highs)" );
			SendDialogMsg( hWnd, IDC_INTERPOLATE, CB_ADDSTRING,0,(LPARAM) L"4 - Catmull-Rom (PS2-like/slow)" );
			SendDialogMsg( hWnd, IDC_INTERPOLATE, CB_SETCURSEL,Interpolation,0 );

			SendDialogMsg( hWnd, IDC_SYNCHMODE, CB_RESETCONTENT,0,0 );
			SendDialogMsg( hWnd, IDC_SYNCHMODE, CB_ADDSTRING,0,(LPARAM) L"TimeStretch (Recommended)" );
			SendDialogMsg( hWnd, IDC_SYNCHMODE, CB_ADDSTRING,0,(LPARAM) L"Async Mix (Breaks some games!)" );
			SendDialogMsg( hWnd, IDC_SYNCHMODE, CB_ADDSTRING,0,(LPARAM) L"None (Audio can skip.)" );
			SendDialogMsg( hWnd, IDC_SYNCHMODE, CB_SETCURSEL,SynchMode,0 );
			
			SendDialogMsg( hWnd, IDC_SPEAKERS, CB_RESETCONTENT,0,0 );
			SendDialogMsg( hWnd, IDC_SPEAKERS, CB_ADDSTRING,0,(LPARAM) L"Stereo (none, default)" );
			SendDialogMsg( hWnd, IDC_SPEAKERS, CB_ADDSTRING,0,(LPARAM) L"Quadrafonic" );
			SendDialogMsg( hWnd, IDC_SPEAKERS, CB_ADDSTRING,0,(LPARAM) L"Surround 5.1" );
			SendDialogMsg( hWnd, IDC_SPEAKERS, CB_ADDSTRING,0,(LPARAM) L"Surround 7.1" );			
			SendDialogMsg( hWnd, IDC_SPEAKERS, CB_SETCURSEL,numSpeakers,0 );

			SendDialogMsg( hWnd, IDC_OUTPUT, CB_RESETCONTENT,0,0 );

			double minlat = (SynchMode == 0)?LATENCY_MIN_TS:LATENCY_MIN;
			int minexp = (int)(pow( minlat+1, 1.0/3.0 ) * 128.0);
			int maxexp = (int)(pow( (double)LATENCY_MAX+2, 1.0/3.0 ) * 128.0);
			INIT_SLIDER( IDC_LATENCY_SLIDER, minexp, maxexp, 200, 42, 1 );

			SendDialogMsg( hWnd, IDC_LATENCY_SLIDER, TBM_SETPOS, TRUE, (int)((pow( (double)SndOutLatencyMS, 1.0/3.0 ) * 128.0) + 1) );
			swprintf_s(temp,L"%d ms (avg)",SndOutLatencyMS);
			SetWindowText(GetDlgItem(hWnd,IDC_LATENCY_LABEL),temp);

			int configvol = (int)(FinalVolume * 100 + 0.5f);
			INIT_SLIDER( IDC_VOLUME_SLIDER, 0, 100, 10, 42, 1 );

			SendDialogMsg( hWnd, IDC_VOLUME_SLIDER, TBM_SETPOS, TRUE, configvol );
			swprintf_s(temp,L"%d%%",configvol);
			SetWindowText(GetDlgItem(hWnd,IDC_VOLUME_LABEL),temp);
			
			EnableWindow( GetDlgItem( hWnd, IDC_OPEN_CONFIG_SOUNDTOUCH ), (SynchMode == 0) );
			EnableWindow( GetDlgItem( hWnd, IDC_OPEN_CONFIG_DEBUG ), DebugEnabled );

			SET_CHECK(IDC_EFFECTS_DISABLE,	EffectsDisabled);
			SET_CHECK(IDC_DEALIASFILTER,	postprocess_filter_dealias);
			SET_CHECK(IDC_DEBUG_ENABLE,		DebugEnabled);
							
			SendMessage(GetDlgItem(hWnd,IDC_PA_DEVICE),CB_RESETCONTENT,0,0);
			SendMessageA(GetDlgItem(hWnd,IDC_PA_DEVICE),CB_ADDSTRING,0,(LPARAM)"Default Device");
			SendMessage(GetDlgItem(hWnd,IDC_PA_DEVICE),CB_SETCURSEL,0,0);

			SendMessage(GetDlgItem(hWnd,IDC_PA_HOSTAPI),CB_RESETCONTENT,0,0);
			SendMessageA(GetDlgItem(hWnd,IDC_PA_HOSTAPI),CB_ADDSTRING,0,(LPARAM)"Developer's Choice");
			SendMessage(GetDlgItem(hWnd,IDC_PA_DEVICE),CB_SETCURSEL,0,0);

			PaError err = Pa_Initialize();
			pa_init = (err = paNoError);

			int apiId = SndOut::GetApiId();
			int idx = 0;
			int count = 0;
			for(int i=0;i<Pa_GetHostApiCount();i++)
			{
				const PaHostApiInfo * apiinfo = Pa_GetHostApiInfo(i);
				if(apiinfo->deviceCount > 0)
				{
					count++;

					SendMessageA(GetDlgItem(hWnd,IDC_PA_HOSTAPI),CB_ADDSTRING,0,(LPARAM)apiinfo->name);

					if(apiinfo->type == apiId)
					{
						idx = count;
					}
				}
			}

			SendMessage(GetDlgItem(hWnd,IDC_PA_HOSTAPI),CB_SETCURSEL,idx,0);
				
			if(idx > 0)
			{
				int api_idx = idx-1;
				int dev_idx=0;
				int i=1;
				for(int j=0;j<Pa_GetDeviceCount();j++)
				{
					const PaDeviceInfo * info = Pa_GetDeviceInfo(j);
					if(info->hostApi == api_idx && info->maxOutputChannels > 0)
					{
						SendMessageA(GetDlgItem(hWnd,IDC_PA_DEVICE),CB_ADDSTRING,0,(LPARAM)info->name);
						SendMessage(GetDlgItem(hWnd,IDC_PA_DEVICE),CB_SETITEMDATA,i,(LPARAM)info);			
						if(wxString::FromAscii(info->name) == SndOut::GetDevice())
						{
							dev_idx = i;
						}
						i++;
					}
				}
				SendMessage(GetDlgItem(hWnd,IDC_PA_DEVICE),CB_SETCURSEL,dev_idx,0);
			}
			
			INIT_SLIDER( IDC_LATENCY_PORTAUDIO, 10, 200, 10, 1, 1 );
			SendMessage(GetDlgItem(hWnd,IDC_LATENCY_PORTAUDIO),TBM_SETPOS,TRUE,SndOut::GetSuggestedLatencyMS());
			swprintf_s(temp, L"%d ms", SndOut::GetSuggestedLatencyMS());
			SetWindowText(GetDlgItem(hWnd,IDC_LATENCY_LABEL2),temp);

			if(SndOut::GetSuggestedLatencyMinimal())
				SET_CHECK( IDC_MINIMIZE, true );
			else
				SET_CHECK( IDC_MANUAL, true );
				
			SET_CHECK( IDC_EXCLUSIVE, SndOut::GetWasapiExclusiveMode() );
			
			SET_CHECK( IDC_DISABLE_OUTPUT, DisableOutput );

		}
		break;

		case WM_COMMAND:
			wmId    = LOWORD(wParam);
			wmEvent = HIWORD(wParam);
			// Parse the menu selections:
			switch (wmId)
			{
				case IDOK:
				{
					double res = ((int)SendDialogMsg( hWnd, IDC_LATENCY_SLIDER, TBM_GETPOS, 0, 0 )) / 128.0;
					SndOutLatencyMS = (int)pow( res, 3.0 );
					Clampify( SndOutLatencyMS, LATENCY_MIN, LATENCY_MAX );
					FinalVolume = (float)(SendDialogMsg( hWnd, IDC_VOLUME_SLIDER, TBM_GETPOS, 0, 0 )) / 100;
					Interpolation = (int)SendDialogMsg( hWnd, IDC_INTERPOLATE, CB_GETCURSEL,0,0 );
					//OutputModule = (int)SendDialogMsg( hWnd, IDC_OUTPUT, CB_GETCURSEL,0,0 );
					SynchMode = (int)SendDialogMsg( hWnd, IDC_SYNCHMODE, CB_GETCURSEL,0,0 );
					numSpeakers = (int)SendDialogMsg( hWnd, IDC_SPEAKERS, CB_GETCURSEL,0,0 );

					int idx = (int)SendMessage(GetDlgItem(hWnd,IDC_PA_HOSTAPI),CB_GETCURSEL,0,0);
					
					SndOut::SetApiId (idx > 0 ? Pa_GetHostApiInfo(idx-1)->type : 0);

					idx = (int)SendMessage(GetDlgItem(hWnd,IDC_PA_DEVICE),CB_GETCURSEL,0,0);
					const PaDeviceInfo * info = (const PaDeviceInfo *)SendMessage(GetDlgItem(hWnd,IDC_PA_DEVICE),CB_GETITEMDATA,idx,0);
					if(info)
						SndOut::SetDevice (wxString::FromAscii( info->name ));
					else
						SndOut::SetDevice (L"default");
														
					int m_SuggestedLatencyMS = (int)SendMessage( GetDlgItem( hWnd, IDC_LATENCY_PORTAUDIO ), TBM_GETPOS, 0, 0 );

					if( m_SuggestedLatencyMS < 10 ) m_SuggestedLatencyMS = 10;
					if( m_SuggestedLatencyMS > 200 ) m_SuggestedLatencyMS = 200;

					SndOut::SetSuggestedLatencyMS (m_SuggestedLatencyMS);

					SndOut::SetSuggestedLatencyMinimal (SendMessage(GetDlgItem(hWnd,IDC_MINIMIZE),BM_GETCHECK,0,0)==BST_CHECKED);
						
					SndOut::SetWasapiExclusiveMode (SendMessage(GetDlgItem(hWnd,IDC_EXCLUSIVE),BM_GETCHECK,0,0)==BST_CHECKED);
					
					if(pa_init > 0)
						Pa_Terminate();

					WriteSettings();
					EndDialog(hWnd,0);
				}
				break;

				case IDCANCEL:

					if(pa_init > 0)
						Pa_Terminate();

					EndDialog(hWnd,0);
				break;
				
				case IDC_OPEN_CONFIG_DEBUG:
				{
					// Quick Hack -- DebugEnabled is re-loaded with the DebugConfig's API,
					// so we need to override it here:

					bool dbgtmp = DebugEnabled;
					DebugConfig::OpenDialog();
					DebugEnabled = dbgtmp;
				}
				break;

				case IDC_SYNCHMODE:
				{
					if(wmEvent == CBN_SELCHANGE)
					{
						int sMode = (int)SendDialogMsg( hWnd, IDC_SYNCHMODE, CB_GETCURSEL,0,0 );
						double minlat = (sMode == 0)?LATENCY_MIN_TS:LATENCY_MIN;
						int minexp = (int)(pow( minlat+1, 1.0/3.0 ) * 128.0);
						int maxexp = (int)(pow( (double)LATENCY_MAX+2, 1.0/3.0 ) * 128.0);
						INIT_SLIDER( IDC_LATENCY_SLIDER, minexp, maxexp, 200, 42, 1 );
						
						int curpos = (int)SendMessage(GetDlgItem( hWnd, IDC_LATENCY_SLIDER ),TBM_GETPOS,0,0);
						double res = pow( curpos / 128.0, 3.0 );
						curpos = (int)res;
						swprintf_s(temp,L"%d ms (avg)",curpos);
						SetDlgItemText(hWnd,IDC_LATENCY_LABEL,temp);
					}
				}
				break;


				case IDC_OPEN_CONFIG_SOUNDTOUCH:
					SoundtouchCfg::OpenDialog( hWnd );
				break;

				HANDLE_CHECK(IDC_EFFECTS_DISABLE,EffectsDisabled);
				HANDLE_CHECK(IDC_DEALIASFILTER,postprocess_filter_dealias);
				HANDLE_CHECK(IDC_DSP_ENABLE,dspPluginEnabled);
				HANDLE_CHECK(IDC_DISABLE_OUTPUT,DisableOutput);
				
				// Fixme : Eh, how to update this based on drop list selections? :p
				// IDC_TS_ENABLE already deleted!

				//HANDLE_CHECKNB(IDC_TS_ENABLE,timeStretchEnabled);
				//	EnableWindow( GetDlgItem( hWnd, IDC_OPEN_CONFIG_SOUNDTOUCH ), timeStretchEnabled );
				break;

				HANDLE_CHECKNB(IDC_DEBUG_ENABLE,DebugEnabled);
					DebugConfig::EnableControls( hWnd );
					EnableWindow( GetDlgItem( hWnd, IDC_OPEN_CONFIG_DEBUG ), DebugEnabled );
				break;
								
				case IDC_PA_HOSTAPI:
				{
					if(wmEvent == CBN_SELCHANGE)
					{
						SendMessage(GetDlgItem(hWnd,IDC_PA_DEVICE),CB_RESETCONTENT,0,0);
						SendMessageA(GetDlgItem(hWnd,IDC_PA_DEVICE),CB_ADDSTRING,0,(LPARAM)"Default Device");
						SendMessage(GetDlgItem(hWnd,IDC_PA_DEVICE),CB_SETITEMDATA,0,0);

						int api_idx = (int)SendMessage(GetDlgItem(hWnd,IDC_PA_HOSTAPI),CB_GETCURSEL,0,0)-1;

						int idx=0;
						if(api_idx >= 0)
						{
							int i=1;
							for(int j=0;j<Pa_GetDeviceCount();j++)
							{
								const PaDeviceInfo * info = Pa_GetDeviceInfo(j);
								if(info->hostApi == api_idx && info->maxOutputChannels > 0)
								{
									SendMessageA(GetDlgItem(hWnd,IDC_PA_DEVICE),CB_ADDSTRING,0,(LPARAM)info->name);
									SendMessage(GetDlgItem(hWnd,IDC_PA_DEVICE),CB_SETITEMDATA,i,(LPARAM)info);
									i++;
								}
							}
						}
						SendMessage(GetDlgItem(hWnd,IDC_PA_DEVICE),CB_SETCURSEL,idx,0);
					}
				}
				break;


				default:
					return FALSE;
			}
		break;

		case WM_HSCROLL:
		{
			wmEvent = LOWORD(wParam);
			HWND hwndDlg = (HWND)lParam;

			// TB_THUMBTRACK passes the curpos in wParam.  Other messages
			// have to use TBM_GETPOS, so they will override this assignment (see below)

			int curpos = HIWORD( wParam );

			switch( wmEvent )
			{
				//case TB_ENDTRACK:
				//case TB_THUMBPOSITION:
				case TB_LINEUP:
				case TB_LINEDOWN:
				case TB_PAGEUP:
				case TB_PAGEDOWN:
					curpos = (int)SendMessage(hwndDlg,TBM_GETPOS,0,0);

				case TB_THUMBTRACK:
					Clampify( curpos,
						(int)SendMessage(hwndDlg,TBM_GETRANGEMIN,0,0),
						(int)SendMessage(hwndDlg,TBM_GETRANGEMAX,0,0)
					);

					SendMessage((HWND)lParam,TBM_SETPOS,TRUE,curpos);

					if( hwndDlg == GetDlgItem( hWnd, IDC_LATENCY_SLIDER ) )
					{
						double res = pow( curpos / 128.0, 3.0 );
						curpos = (int)res;
						swprintf_s(temp,L"%d ms (avg)",curpos);
						SetDlgItemText(hWnd,IDC_LATENCY_LABEL,temp);
					}
					
					if( hwndDlg == GetDlgItem( hWnd, IDC_VOLUME_SLIDER ) )
					{
						swprintf_s(temp,L"%d%%",curpos);
						SetDlgItemText(hWnd,IDC_VOLUME_LABEL,temp);
					}
					
					if( hwndDlg == GetDlgItem( hWnd, IDC_LATENCY_PORTAUDIO ) )
					{
						swprintf_s(temp, L"%d ms", curpos);
						SetDlgItemText(hWnd,IDC_LATENCY_LABEL2,temp);
					}
				break;

				default:
					return FALSE;
			}
		}
		break;

		default:
			return FALSE;
	}
	return TRUE;
}

void configure()
{
	INT_PTR ret;
	ReadSettings();
	ret = DialogBoxParam(hInstance,MAKEINTRESOURCE(IDD_CONFIG),GetActiveWindow(),(DLGPROC)ConfigProc,1);
	if(ret==-1)
	{
		MessageBox(GetActiveWindow(),L"Error Opening the config dialog.",L"OMG ERROR!",MB_OK | MB_SETFOREGROUND);
		return;
	}
	ReadSettings();
}
