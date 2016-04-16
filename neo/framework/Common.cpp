/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.
Copyright (C) 2014-2016 Robert Beckebans
Copyright (C) 2014-2016 Kot in Action Creative Artel

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/
#include "precompiled.h"
#pragma hdrstop

#include "Common_local.h"

#include "ConsoleHistory.h"
#include "../renderer/AutoRenderBink.h"

#include "../sound/sound.h"

#include "../sys/sys_savegame.h"

#ifdef WIN32
#include "../sys/win32/win_local.h"  //SS2 fix RoQ videos skipping
#endif

//#ifdef MINGW
//#include "../sys/sdl/sdl_local.h"  //SS2 fix RoQ videos skipping
//#endif

#ifdef ID_ALLOW_TOOLS
#include "tools/edit_public.h"
#endif

#if defined( _DEBUG )
#define BUILD_DEBUG "-debug"
#else
#define BUILD_DEBUG ""
#endif

struct version_s
{
	version_s()
	{
		sprintf( string, "%s.%d%s %s %s %s", ENGINE_VERSION, BUILD_NUMBER, BUILD_DEBUG, BUILD_STRING, __DATE__, __TIME__ );
	}
	char	string[256];
} version;

idCVar com_version( "si_version", version.string, CVAR_SYSTEM | CVAR_ROM | CVAR_SERVERINFO, "engine version" );
idCVar com_forceGenericSIMD( "com_forceGenericSIMD", "0", CVAR_BOOL | CVAR_SYSTEM | CVAR_NOCHEAT, "force generic platform independent SIMD" );

#ifdef ID_RETAIL
idCVar com_allowConsole( "com_allowConsole", "0", CVAR_BOOL | CVAR_SYSTEM | CVAR_INIT, "allow toggling console with the tilde key" );
#else
idCVar com_allowConsole( "com_allowConsole", "1", CVAR_BOOL | CVAR_SYSTEM | CVAR_INIT, "allow toggling console with the tilde key" );
#endif

idCVar com_developer( "developer", "0", CVAR_BOOL | CVAR_SYSTEM | CVAR_NOCHEAT, "developer mode" );
idCVar com_speeds( "com_speeds", "0", CVAR_BOOL | CVAR_SYSTEM | CVAR_NOCHEAT, "show engine timings" );
// DG: support "com_showFPS 2" for fps-only view like in classic doom3 => make it CVAR_INTEGER
idCVar com_showFPS( "com_showFPS", "0", CVAR_INTEGER | CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_NOCHEAT, "show frames rendered per second. 0: off 1: default bfg values, 2: only show FPS (classic view)" );
// DG end
idCVar com_showMemoryUsage( "com_showMemoryUsage", "0", CVAR_BOOL | CVAR_SYSTEM | CVAR_NOCHEAT, "show total and per frame memory usage" );
idCVar com_updateLoadSize( "com_updateLoadSize", "0", CVAR_BOOL | CVAR_SYSTEM | CVAR_NOCHEAT, "update the load size after loading a map" );

idCVar com_productionMode( "com_productionMode", "0", CVAR_SYSTEM | CVAR_BOOL, "0 - no special behavior, 1 - building a production build, 2 - running a production build" );

idCVar com_japaneseCensorship( "com_japaneseCensorship", "0", CVAR_NOCHEAT, "Enable Japanese censorship" );

idCVar preload_CommonAssets( "preload_CommonAssets", "1", CVAR_SYSTEM | CVAR_BOOL, "preload common assets" );

idCVar net_inviteOnly( "net_inviteOnly", "1", CVAR_BOOL | CVAR_ARCHIVE, "whether or not the private server you create allows friends to join or invite only" );

// DG: add cvar for pause
idCVar com_pause( "com_pause", "0", CVAR_BOOL | CVAR_SYSTEM | CVAR_NOCHEAT, "set to 1 to pause game, to 0 to unpause again" );
// DG end

extern idCVar g_demoMode;

idCVar com_engineHz( "com_engineHz", "60", CVAR_FLOAT | CVAR_ARCHIVE, "Frames per second the engine runs at", 10.0f, 1024.0f );
float com_engineHz_latched = 60.0f; // Latched version of cvar, updated between map loads
int64 com_engineHz_numerator = 100LL * 1000LL;
int64 com_engineHz_denominator = 100LL * 60LL;

int				com_editors;			// currently opened editor(s)
bool			com_editorActive;		//  true if an editor has focus

// RB begin
#if defined(_WIN32)
HWND com_hwndMsg = NULL;
#endif
// RB end

#ifdef __DOOM_DLL__
idGame* 		game = NULL;
idGameEdit* 	gameEdit = NULL;
#endif

idCommonLocal	commonLocal;
idCommon* 		common = &commonLocal;


idCVar com_skipIntroVideos( "com_skipIntroVideos", "1", CVAR_BOOL , "skips intro videos" );

idCVar com_firstLaunchIntroVideos( "com_firstLaunchIntroVideos", "1", CVAR_BOOL | CVAR_ARCHIVE, "determines whether the game already ran at least once; used to allow skipping intro video after first game launch" ); // SE2 feature

/*
==================
idCommonLocal::idCommonLocal
==================
*/
idCommonLocal::idCommonLocal() :
	readSnapshotIndex( 0 ),
	writeSnapshotIndex( 0 ),
	optimalPCTBuffer( 0.5f ),
	optimalTimeBuffered( 0.0f ),
	optimalTimeBufferedWindow( 0.0f ),
	lastPacifierSessionTime( 0 ),
	lastPacifierGuiTime( 0 ),
	lastPacifierDialogState( false ),
	showShellRequested( false )
{

	snapCurrent.localTime = -1;
	snapPrevious.localTime = -1;
	snapCurrent.serverTime = -1;
	snapPrevious.serverTime = -1;
	snapTimeBuffered	= 0.0f;
	effectiveSnapRate	= 0.0f;
	totalBufferedTime	= 0;
	totalRecvTime		= 0;
	
	com_fullyInitialized = false;
	com_refreshOnPrint = false;
	com_errorEntered = ERP_NONE;
	com_shuttingDown = false;
	com_isJapaneseSKU = false;
	
	logFile = NULL;
	
	strcpy( errorMessage, "" );
	
	rd_buffer = NULL;
	rd_buffersize = 0;
	rd_flush = NULL;
	
	gameDLL = 0;
	
	loadGUI = NULL;
	nextLoadTip = 0;
	isHellMap = false;
	wipeForced = false;
	defaultLoadscreen = false;
	
	menuSoundWorld = NULL;
	
	insideUpdateScreen = false;
	insideExecuteMapChange = false;
	
	mapSpawnData.savegameFile = NULL;
	
	currentMapName.Clear();
	aviDemoShortName.Clear();
	
	renderWorld = NULL;
	soundWorld = NULL;
	menuSoundWorld = NULL;
	readDemo = NULL;
	writeDemo = NULL;
	
	gameFrame = 0;
	gameTimeResidual = 0;
	syncNextGameFrame = true;
	mapSpawned = false;
	aviCaptureMode = false;
	timeDemo = TD_NO;
	
	nextSnapshotSendTime = 0;
	nextUsercmdSendTime = 0;
	
	clientPrediction = 0;
	
	saveFile = NULL;
	stringsFile = NULL;
	
	ClearWipe();
}

/*
==================
idCommonLocal::Quit
==================
*/
void idCommonLocal::Quit()
{
#ifdef ID_ALLOW_TOOLS
	if ( com_editors & EDITOR_RADIANT ) {
		RadiantInit();
		return;
	}
#endif

	// don't try to shutdown if we are in a recursive error
	if( !com_errorEntered )
	{
		Shutdown();
	}
	Sys_Quit();
}


/*
============================================================================

COMMAND LINE FUNCTIONS

+ characters separate the commandLine string into multiple console
command lines.

All of these are valid:

doom +set test blah +map test
doom set test blah+map test
doom set test blah + map test

============================================================================
*/

#define		MAX_CONSOLE_LINES	32
int			com_numConsoleLines;
idCmdArgs	com_consoleLines[MAX_CONSOLE_LINES];

/*
==================
idCommonLocal::ParseCommandLine
==================
*/
void idCommonLocal::ParseCommandLine( int argc, const char* const* argv )
{
	int i, current_count;
	
	com_numConsoleLines = 0;
	current_count = 0;
	// API says no program path
	for( i = 0; i < argc; i++ )
	{
		if( idStr::Icmp( argv[ i ], "+connect_lobby" ) == 0 )
		{
			// Handle Steam bootable invites.
			
			// RB begin
#if defined(_WIN32)
			session->HandleBootableInvite( _atoi64( argv[ i + 1 ] ) );
#else
			session->HandleBootableInvite( atol( argv[ i + 1 ] ) );
#endif
			// RB end
		}
		else if( argv[ i ][ 0 ] == '+' )
		{
			com_numConsoleLines++;
			com_consoleLines[ com_numConsoleLines - 1 ].AppendArg( argv[ i ] + 1 );
		}
		else
		{
			if( !com_numConsoleLines )
			{
				com_numConsoleLines++;
			}
			com_consoleLines[ com_numConsoleLines - 1 ].AppendArg( argv[ i ] );
		}
	}
}

/*
==================
idCommonLocal::SafeMode

Check for "safe" on the command line, which will
skip loading of config file (DoomConfig.cfg)
==================
*/
bool idCommonLocal::SafeMode()
{
	int			i;
	
	for( i = 0 ; i < com_numConsoleLines ; i++ )
	{
		if( !idStr::Icmp( com_consoleLines[ i ].Argv( 0 ), "safe" )
				|| !idStr::Icmp( com_consoleLines[ i ].Argv( 0 ), "cvar_restart" ) )
		{
			com_consoleLines[ i ].Clear();
			return true;
		}
	}
	return false;
}

/*
==================
idCommonLocal::StartupVariable

Searches for command line parameters that are set commands.
If match is not NULL, only that cvar will be looked for.
That is necessary because cddir and basedir need to be set
before the filesystem is started, but all other sets should
be after execing the config and default.
==================
*/
void idCommonLocal::StartupVariable( const char* match )
{
	int i = 0;
	while(	i < com_numConsoleLines )
	{
		if( strcmp( com_consoleLines[ i ].Argv( 0 ), "set" ) != 0 )
		{
			i++;
			continue;
		}
		const char* s = com_consoleLines[ i ].Argv( 1 );
		
		if( !match || !idStr::Icmp( s, match ) )
		{
			cvarSystem->SetCVarString( s, com_consoleLines[ i ].Argv( 2 ) );
		}
		i++;
	}
}

/*
==================
idCommonLocal::AddStartupCommands

Adds command line parameters as script statements
Commands are separated by + signs

Returns true if any late commands were added, which
will keep the demoloop from immediately starting
==================
*/
void idCommonLocal::AddStartupCommands()
{
	// quote every token, so args with semicolons can work
	for( int i = 0; i < com_numConsoleLines; i++ )
	{
		if( !com_consoleLines[i].Argc() )
		{
			continue;
		}
		// directly as tokenized so nothing gets screwed
		cmdSystem->BufferCommandArgs( CMD_EXEC_APPEND, com_consoleLines[i] );
	}
}

/*
=================
idCommonLocal::InitTool
=================
*/
void idCommonLocal::InitTool( const toolFlag_t tool, const idDict *dict ) {
#ifdef ID_ALLOW_TOOLS
	if ( tool & EDITOR_SOUND ) {
		SoundEditorInit( dict );
	} else if ( tool & EDITOR_LIGHT ) {
		LightEditorInit( dict );
	} else if ( tool & EDITOR_PARTICLE ) {
		ParticleEditorInit( dict );
	} else if ( tool & EDITOR_AF ) {
		AFEditorInit( dict );
	}
#endif
}

/*
==================
idCommonLocal::ActivateTool

Activates or Deactivates a tool
==================
*/
void idCommonLocal::ActivateTool( bool active ) {
	com_editorActive = active;
//	 ** Tools don't need to grab the mouse.  The SDL window grabs and release the mouse when activated.
//	Sys_GrabMouseCursor( !active );
}

/*
==================
idCommonLocal::WriteConfigToFile
==================
*/
void idCommonLocal::WriteConfigToFile( const char* filename )
{
	idFile* f = fileSystem->OpenFileWrite( filename );
	if( !f )
	{
		Printf( "Couldn't write %s.\n", filename );
		return;
	}
	
	idKeyInput::WriteBindings( f );
	cvarSystem->WriteFlaggedVariables( CVAR_ARCHIVE, "set", f );
	fileSystem->CloseFile( f );
}

/*
===============
idCommonLocal::WriteConfiguration

Writes key bindings and archived cvars to config file if modified
===============
*/
void idCommonLocal::WriteConfiguration()
{
	// if we are quiting without fully initializing, make sure
	// we don't write out anything
	if( !com_fullyInitialized )
	{
		return;
	}
	
	if( !( cvarSystem->GetModifiedFlags() & CVAR_ARCHIVE ) )
	{
		return;
	}
	cvarSystem->ClearModifiedFlags( CVAR_ARCHIVE );
	
	// save to the profile
	idLocalUser* user = session->GetSignInManager().GetMasterLocalUser();
	if( user != NULL )
	{
		user->SaveProfileSettings();
	}
	
#ifdef CONFIG_FILE
	// disable printing out the "Writing to:" message
	bool developer = com_developer.GetBool();
	com_developer.SetBool( false );
	
	WriteConfigToFile( CONFIG_FILE );
	
	// restore the developer cvar
	com_developer.SetBool( developer );
#endif
}

/*
===============
KeysFromBinding()
Returns the key bound to the command
===============
*/
const char* idCommonLocal::KeysFromBinding( const char* bind )
{
	return idKeyInput::KeysFromBinding( bind );
}

/*
===============
BindingFromKey()
Returns the binding bound to key
===============
*/
const char* idCommonLocal::BindingFromKey( const char* key )
{
	return idKeyInput::BindingFromKey( key );
}

/*
===============
ButtonState()
Returns the state of the button
===============
*/
int	idCommonLocal::ButtonState( int key )
{
	return usercmdGen->ButtonState( key );
}

/*
===============
ButtonState()
Returns the state of the key
===============
*/
int	idCommonLocal::KeyState( int key )
{
	return usercmdGen->KeyState( key );
}

//============================================================================

#ifdef ID_ALLOW_TOOLS
/*
==================
Com_Editor_f

  we can start the editor dynamically, but we won't ever get back
==================
*/
static void Com_Editor_f( const idCmdArgs &args ) {
	RadiantInit();
}


/*
=============
Com_ScriptDebugger_f
=============
*/
static void Com_ScriptDebugger_f( const idCmdArgs &args ) {
#ifdef USE_MFC_TOOLS
	DebuggerServerInit();
	
	// Make sure it wasnt on the command line
	if ( !( com_editors & EDITOR_DEBUGGER ) ) {
		DebuggerClientInit( NULL );
	}
#else
	common->Printf("Debugger not included in build\n");
#endif
}

/*
=============
Com_EditGUIs_f
=============
*/
static void Com_EditGUIs_f( const idCmdArgs &args ) {	

	// motorsep 12-28-2014; safety mechanicsm that ensures AA is off before GUI Editor can be used; eventually we might need a better solution or at least a Dialog window that lets user know about restart
	// potentially I see having a menu screen with all tools shortcuts there and dialog window popping up if restart or some settings changes are required
	int antialiasing = cvarSystem->GetCVarInteger("r_multiSamples");

	if (antialiasing != 0) {		
		idStr cmdLine = Sys_GetCmdLine();		
		cmdLine.Append(" +set r_multiSamples 0");		
		Sys_ReLaunch((void*)cmdLine.c_str(), cmdLine.Length());		
		return;
	}	

	GUIEditorInit();
}

/*
=============
Com_MaterialEditor_f
=============
*/
static void Com_MaterialEditor_f( const idCmdArgs &args ) {
	// Turn off sounds
	soundSystem->SetMute( true );
	MaterialEditorInit();
}
#endif // ID_ALLOW_TOOLS

/*
============
idCmdSystemLocal::PrintMemInfo_f

This prints out memory debugging data
============
*/
CONSOLE_COMMAND( printMemInfo, "prints memory debugging data", NULL )
{
	MemInfo_t mi;
	memset( &mi, 0, sizeof( mi ) );
	mi.filebase = commonLocal.GetCurrentMapName();
	
	renderSystem->PrintMemInfo( &mi );			// textures and models
	soundSystem->PrintMemInfo( &mi );			// sounds
	
	common->Printf( " Used image memory: %s bytes\n", idStr::FormatNumber( mi.imageAssetsTotal ).c_str() );
	mi.assetTotals += mi.imageAssetsTotal;
	
	common->Printf( " Used model memory: %s bytes\n", idStr::FormatNumber( mi.modelAssetsTotal ).c_str() );
	mi.assetTotals += mi.modelAssetsTotal;
	
	common->Printf( " Used sound memory: %s bytes\n", idStr::FormatNumber( mi.soundAssetsTotal ).c_str() );
	mi.assetTotals += mi.soundAssetsTotal;
	
	common->Printf( " Used asset memory: %s bytes\n", idStr::FormatNumber( mi.assetTotals ).c_str() );
	
	// write overview file
	idFile* f;
	
	f = fileSystem->OpenFileAppend( "maps/printmeminfo.txt" );
	if( !f )
	{
		return;
	}
	
	f->Printf( "total(%s ) image(%s ) model(%s ) sound(%s ): %s\n", idStr::FormatNumber( mi.assetTotals ).c_str(), idStr::FormatNumber( mi.imageAssetsTotal ).c_str(),
			   idStr::FormatNumber( mi.modelAssetsTotal ).c_str(), idStr::FormatNumber( mi.soundAssetsTotal ).c_str(), mi.filebase.c_str() );
			   
	fileSystem->CloseFile( f );
}

#ifdef ID_ALLOW_TOOLS
/*
==================
Com_EditLights_f
==================
*/
static void Com_EditLights_f( const idCmdArgs &args ) {
	LightEditorInit( NULL );
	cvarSystem->SetCVarInteger( "g_editEntityMode", 1 );
}

/*
==================
Com_EditSounds_f
==================
*/
static void Com_EditSounds_f( const idCmdArgs &args ) {
	SoundEditorInit( NULL );
	cvarSystem->SetCVarInteger( "g_editEntityMode", 2 );
}

/*
==================
Com_EditDecls_f
==================
*/
static void Com_EditDecls_f( const idCmdArgs &args ) {
	DeclBrowserInit( NULL );
}

/*
==================
Com_EditAFs_f
==================
*/
static void Com_EditAFs_f( const idCmdArgs &args ) {
	AFEditorInit( NULL );
}

/*
==================
Com_EditParticles_f
==================
*/
static void Com_EditParticles_f( const idCmdArgs &args ) {
	ParticleEditorInit( NULL );
}

/*
==================
Com_EditScripts_f
==================
*/
static void Com_EditScripts_f( const idCmdArgs &args ) {
	ScriptEditorInit( NULL );
}

/*
==================
Com_EditPDAs_f
==================
*/
static void Com_EditPDAs_f( const idCmdArgs &args ) {
	PDAEditorInit( NULL );
}
#endif // ID_ALLOW_TOOLS

/*
==================
Com_Error_f

Just throw a fatal error to test error shutdown procedures.
==================
*/
CONSOLE_COMMAND( error, "causes an error", NULL )
{
	if( !com_developer.GetBool() )
	{
		commonLocal.Printf( "error may only be used in developer mode\n" );
		return;
	}
	
	if( args.Argc() > 1 )
	{
		commonLocal.FatalError( "Testing fatal error" );
	}
	else
	{
		commonLocal.Error( "Testing drop error" );
	}
}

/*
==================
Com_Freeze_f

Just freeze in place for a given number of seconds to test error recovery.
==================
*/
CONSOLE_COMMAND( freeze, "freezes the game for a number of seconds", NULL )
{
	float	s;
	int		start, now;
	
	if( args.Argc() != 2 )
	{
		commonLocal.Printf( "freeze <seconds>\n" );
		return;
	}
	
	if( !com_developer.GetBool() )
	{
		commonLocal.Printf( "freeze may only be used in developer mode\n" );
		return;
	}
	
	s = atof( args.Argv( 1 ) );
	
	start = eventLoop->Milliseconds();
	
	while( 1 )
	{
		now = eventLoop->Milliseconds();
		if( ( now - start ) * 0.001f > s )
		{
			break;
		}
	}
}

/*
=================
Com_Crash_f

A way to force a bus error for development reasons
=================
*/
CONSOLE_COMMAND( crash, "causes a crash", NULL )
{
	if( !com_developer.GetBool() )
	{
		commonLocal.Printf( "crash may only be used in developer mode\n" );
		return;
	}
#ifdef __GNUC__
	__builtin_trap();
#else
	* ( int* ) 0 = 0x12345678;
#endif
}

/*
=================
Com_Quit_f
=================
*/
CONSOLE_COMMAND_SHIP( quit, "quits the game", NULL )
{
	commonLocal.Quit();
}
CONSOLE_COMMAND_SHIP( exit, "exits the game", NULL )
{
	commonLocal.Quit();
}

/*
===============
Com_WriteConfig_f

Write the config file to a specific name
===============
*/
CONSOLE_COMMAND( writeConfig, "writes a config file", NULL )
{
	idStr	filename;
	
	if( args.Argc() != 2 )
	{
		commonLocal.Printf( "Usage: writeconfig <filename>\n" );
		return;
	}
	
	filename = args.Argv( 1 );
	filename.DefaultFileExtension( ".cfg" );
	commonLocal.Printf( "Writing %s.\n", filename.c_str() );
	commonLocal.WriteConfigToFile( filename );
}

/*
========================
idCommonLocal::CheckStartupStorageRequirements
========================
*/
void idCommonLocal::CheckStartupStorageRequirements()
{
	// RB: disabled savegame and profile storage checks, because it fails sometimes without any clear reason
#if 0
	int64 availableSpace = 0;
	
	// ------------------------------------------------------------------------
	// Savegame and Profile required storage
	// ------------------------------------------------------------------------
	{
		// Make sure the save path exists in case it was deleted.
		// If the path cannot be created we can safely assume there is no
		// free space because in that case nothing can be saved anyway.
		const char* savepath = cvarSystem->GetCVarString( "fs_savepath" );
		idStr directory = savepath;
		//idStr directory = fs_savepath.GetString();
		directory += "\\";	// so it doesn't think the last part is a file and ignores in the directory creation
		fileSystem->CreateOSPath( directory );
		
		// Get the free space on the save path.
		availableSpace = Sys_GetDriveFreeSpaceInBytes( savepath );
		
		// If free space fails then get space on drive as a fall back
		// (the directory will be created later anyway)
		if( availableSpace <= 1 )
		{
			idStr savePath( savepath );
			if( savePath.Length() >= 3 )
			{
				if( savePath[ 1 ] == ':' && savePath[ 2 ] == '\\' &&
						( ( savePath[ 0 ] >= 'A' && savePath[ 0 ] <= 'Z' ) ||
						  ( savePath[ 0 ] >= 'a' && savePath[ 0 ] <= 'z' ) ) )
				{
					savePath = savePath.Left( 3 );
					availableSpace = Sys_GetDriveFreeSpaceInBytes( savePath );
				}
			}
		}
	}
	
	const int MIN_SAVE_STORAGE_PROFILE		= 1024 * 1024;
	const int MIN_SAVE_STORAGE_SAVEGAME		= MIN_SAVEGAME_SIZE_BYTES;
	
	uint64 requiredSizeBytes = MIN_SAVE_STORAGE_SAVEGAME + MIN_SAVE_STORAGE_PROFILE;
	
	idLib::Printf( "requiredSizeBytes: %lld\n", requiredSizeBytes );
	
	if( ( int64 )( requiredSizeBytes - availableSpace ) > 0 )
	{
		class idSWFScriptFunction_Continue : public idSWFScriptFunction_RefCounted
		{
		public:
			virtual ~idSWFScriptFunction_Continue() {}
			idSWFScriptVar Call( idSWFScriptObject* thisObject, const idSWFParmList& parms )
			{
				common->Dialog().ClearDialog( GDM_INSUFFICENT_STORAGE_SPACE );
				common->Quit();
				return idSWFScriptVar();
			}
		};
		
		idStaticList< idSWFScriptFunction*, 4 > callbacks;
		idStaticList< idStrId, 4 > optionText;
		callbacks.Append( new( TAG_SWF ) idSWFScriptFunction_Continue() );
		optionText.Append( idStrId( "#STR_SWF_ACCEPT" ) );
		
		// build custom space required string
		// #str_dlg_space_required ~= "There is insufficient storage available.  Please free %s and try again."
		idStr format = idStrId( "#str_dlg_startup_insufficient_storage" ).GetLocalizedString();
		idStr size;
		if( requiredSizeBytes > ( 1024 * 1024 ) )
		{
			size = va( "%.1f MB", ( float )requiredSizeBytes / ( 1024.0f * 1024.0f ) + 0.1f );	// +0.1 to avoid truncation
		}
		else
		{
			size = va( "%.1f KB", ( float )requiredSizeBytes / 1024.0f + 0.1f );
		}
		idStr msg = va( format.c_str(), size.c_str() );
		
		common->Dialog().AddDynamicDialog( GDM_INSUFFICENT_STORAGE_SPACE, callbacks, optionText, true, msg );
	}
#endif
	// RB end
	
	session->GetAchievementSystem().Start();
}

/*
===============
idCommonLocal::JapaneseCensorship
===============
*/
bool idCommonLocal::JapaneseCensorship() const
{
	return com_japaneseCensorship.GetBool() || com_isJapaneseSKU;
}

/*
===============
idCommonLocal::FilterLangList
===============
*/
void idCommonLocal::FilterLangList( idStrList* list, idStr lang )
{

	idStr temp;
	for( int i = 0; i < list->Num(); i++ )
	{
		temp = ( *list )[i];
		temp = temp.Right( temp.Length() - strlen( "strings/" ) );
		temp = temp.Left( lang.Length() );
		if( idStr::Icmp( temp, lang ) != 0 )
		{
			list->RemoveIndex( i );
			i--;
		}
	}
}

/*
===============
idCommonLocal::InitLanguageDict
===============
*/
extern idCVar sys_lang;
void idCommonLocal::InitLanguageDict()
{
	idStr fileName;
	
	//D3XP: Instead of just loading a single lang file for each language
	//we are going to load all files that begin with the language name
	//similar to the way pak files work. So you can place english001.lang
	//to add new strings to the english language dictionary
	idFileList*	langFiles;
	langFiles =  fileSystem->ListFilesTree( "strings", ".lang", true );
	
	idStrList langList = langFiles->GetList();
	
	// Loop through the list and filter
	idStrList currentLangList = langList;
	FilterLangList( &currentLangList, sys_lang.GetString() );
	
	if( currentLangList.Num() == 0 )
	{
		// reset to english and try to load again
		sys_lang.SetString( ID_LANG_ENGLISH );
		currentLangList = langList;
		FilterLangList( &currentLangList, sys_lang.GetString() );
	}
	
	idLocalization::ClearDictionary();
	for( int i = 0; i < currentLangList.Num(); i++ )
	{
		//common->Printf("%s\n", currentLangList[i].c_str());
		const byte* buffer = NULL;
		int len = fileSystem->ReadFile( currentLangList[i], ( void** )&buffer );
		if( len <= 0 )
		{
			assert( false && "couldn't read the language dict file" );
			break;
		}
		idLocalization::LoadDictionary( buffer, len, currentLangList[i] );
		fileSystem->FreeFile( ( void* )buffer );
	}
	
	fileSystem->FreeFileList( langFiles );

	Sys_InitScanTable();
}

/*
=================
ReloadLanguage_f
=================
*/
CONSOLE_COMMAND( reloadLanguage, "reload language dict", NULL )
{
	commonLocal.InitLanguageDict();
}

#include "../renderer/Image.h"

/*
=================
Com_StartBuild_f
=================
*/
CONSOLE_COMMAND( startBuild, "prepares to make a build", NULL )
{
	globalImages->StartBuild();
}

/*
=================
Com_FinishBuild_f
=================
*/
CONSOLE_COMMAND( finishBuild, "finishes the build process", NULL )
{
	if( game )
	{
		game->CacheDictionaryMedia( NULL );
	}
	globalImages->FinishBuild( ( args.Argc() > 1 ) );
}


/*
=================
idCommonLocal::InitCommands
=================
*/
void idCommonLocal::InitCommands()
{
	cmdSystem->AddCommand( "dmap", Dmap_f, CMD_FL_TOOL, "compiles a map", idCmdSystem::ArgCompletion_MapName );
	cmdSystem->AddCommand( "runAAS", RunAAS_f, CMD_FL_TOOL, "compiles an AAS file for a map", idCmdSystem::ArgCompletion_MapName );
	cmdSystem->AddCommand( "runAASDir", RunAASDir_f, CMD_FL_TOOL, "compiles AAS files for all maps in a folder", idCmdSystem::ArgCompletion_MapName );
	cmdSystem->AddCommand( "runReach", RunReach_f, CMD_FL_TOOL, "calculates reachability for an AAS file", idCmdSystem::ArgCompletion_MapName );
	
#ifdef ID_ALLOW_TOOLS
	// editors
	cmdSystem->AddCommand( "editor", Com_Editor_f, CMD_FL_TOOL, "launches the level editor Radiant" );
	cmdSystem->AddCommand( "editLights", Com_EditLights_f, CMD_FL_TOOL, "launches the in-game Light Editor" );
	cmdSystem->AddCommand( "editSounds", Com_EditSounds_f, CMD_FL_TOOL, "launches the in-game Sound Editor" );
	cmdSystem->AddCommand( "editDecls", Com_EditDecls_f, CMD_FL_TOOL, "launches the in-game Declaration Editor" );
	cmdSystem->AddCommand( "editAFs", Com_EditAFs_f, CMD_FL_TOOL, "launches the in-game Articulated Figure Editor" );
	cmdSystem->AddCommand( "editParticles", Com_EditParticles_f, CMD_FL_TOOL, "launches the in-game Particle Editor" );
	cmdSystem->AddCommand( "editScripts", Com_EditScripts_f, CMD_FL_TOOL, "launches the in-game Script Editor" );
	cmdSystem->AddCommand( "editGUIs", Com_EditGUIs_f, CMD_FL_TOOL, "launches the GUI Editor" );
	cmdSystem->AddCommand( "editPDAs", Com_EditPDAs_f, CMD_FL_TOOL, "launches the in-game PDA Editor" );
	cmdSystem->AddCommand( "debugger", Com_ScriptDebugger_f, CMD_FL_TOOL, "launches the Script Debugger" );

	//BSM Nerve: Add support for the material editor
	cmdSystem->AddCommand( "materialEditor", Com_MaterialEditor_f, CMD_FL_TOOL, "launches the Material Editor" );
#endif
}

/*
=================
idCommonLocal::RenderSplash
=================
*/
void idCommonLocal::RenderSplash()
{
	const float sysWidth = renderSystem->GetWidth() * renderSystem->GetPixelAspect();
	const float sysHeight = renderSystem->GetHeight();
	const float sysAspect = sysWidth / sysHeight;
	const float splashAspect = 16.0f / 9.0f;
	const float adjustment = sysAspect / splashAspect;
	const float barHeight = ( adjustment >= 1.0f ) ? 0.0f : ( 1.0f - adjustment ) * ( float )SCREEN_HEIGHT * 0.25f;
	const float barWidth = ( adjustment <= 1.0f ) ? 0.0f : ( adjustment - 1.0f ) * ( float )SCREEN_WIDTH * 0.25f;
	if( barHeight > 0.0f )
	{
		renderSystem->SetColor( colorBlack );
		renderSystem->DrawStretchPic( 0, 0, SCREEN_WIDTH, barHeight, 0, 0, 1, 1, whiteMaterial );
		renderSystem->DrawStretchPic( 0, SCREEN_HEIGHT - barHeight, SCREEN_WIDTH, barHeight, 0, 0, 1, 1, whiteMaterial );
	}
	if( barWidth > 0.0f )
	{
		renderSystem->SetColor( colorBlack );
		renderSystem->DrawStretchPic( 0, 0, barWidth, SCREEN_HEIGHT, 0, 0, 1, 1, whiteMaterial );
		renderSystem->DrawStretchPic( SCREEN_WIDTH - barWidth, 0, barWidth, SCREEN_HEIGHT, 0, 0, 1, 1, whiteMaterial );
	}
	renderSystem->SetColor4( 1, 1, 1, 1 );
	renderSystem->DrawStretchPic( barWidth, barHeight, SCREEN_WIDTH - barWidth * 2.0f, SCREEN_HEIGHT - barHeight * 2.0f, 0, 0, 1, 1, splashScreen );
	
	const emptyCommand_t* cmd = renderSystem->SwapCommandBuffers( &time_frontend, &time_backend, &time_shadows, &time_gpu );
	renderSystem->RenderCommandBuffers( cmd );
}

/*
=================
idCommonLocal::RenderBink
=================
*/
/*void idCommonLocal::RenderBink( const char* path,  const char* path_audio )
{
	// TODO: Fix video skipping on Linux
	const float sysWidth = renderSystem->GetWidth() * renderSystem->GetPixelAspect();
	const float sysHeight = renderSystem->GetHeight();
	const float sysAspect = sysWidth / sysHeight;
	const float movieAspect = ( 16.0f / 9.0f );
	const float imageWidth = SCREEN_WIDTH * movieAspect / sysAspect;
	const float chop = 0.5f * ( SCREEN_WIDTH - imageWidth );
	
	idStr materialText;
	materialText.Format( "{ translucent { videoMap %s } }", path );
	
	//idMaterial* material = const_cast<idMaterial*>( declManager->FindMaterial( "splashbink" ) );
	//idMaterial* material = const_cast<idMaterial*>( declManager->FindMaterial( "video/loadvideo" ) );		// Beth-iD logo, should be removed rather sooner, once KiA logo has sound
	idMaterial* material = const_cast<idMaterial*>( declManager->FindMaterial( "video/kialogo" ) );		
	material->Parse( materialText.c_str(), materialText.Length(), false );
	material->ResetCinematicTime( Sys_Milliseconds() );
	
	soundWorld->PlayShaderDirectly( path_audio, 0 ); // "gui/loadvideointro"
	soundWorld->UnPause();
	soundSystem->SetPlayingSoundWorld( soundWorld );
	soundSystem->Render();		

	int mouseEvents[MAX_MOUSE_EVENTS][2];	

#if !defined(WIN32)
	sysEvent_t	ev; //SS2 fix RoQ videos skipping
#endif
	//while( Sys_Milliseconds() <= material->GetCinematicStartTime() + material->CinematicLength() ) 

	// FIX ME: timing should be polled from the video
	while( Sys_Milliseconds() <= 14000 ) // 13 sec (+1 sec delay) is the length of the test video we have
	{		
#if !defined(WIN32)
		// pump all the events
		Sys_GenerateEvents();
		// Activate Events.
		ev = Sys_GetEvent();
#endif
		Sys_GenerateEvents();		
		Sys_GetEvent();
		IN_ActivateMouse();		
		int keyBoardEvents = Sys_PollKeyboardInputEvents();	 //SS2 fix RoQ videos skipping
		int numMouseEvents = Sys_PollMouseInputEvents( mouseEvents );
		int numJoystickEvents = Sys_PollJoystickInputEvents( 0 );
		renderSystem->DrawStretchPic( chop, 0, imageWidth, SCREEN_HEIGHT, 0, 0, 1, 1, material );
		const emptyCommand_t* cmd = renderSystem->SwapCommandBuffers( &time_frontend, &time_backend, &time_shadows, &time_gpu );
		renderSystem->RenderCommandBuffers( cmd );		
		if(!com_firstLaunchIntroVideos.GetBool()) {
			if ( keyBoardEvents > 0 ) //SS2 fix RoQ videos skipping
				break;
			if ( numMouseEvents > 0 ) 
				break;		
		}
			// TODO: Fix Joystick input to enable skip.		
		Sys_Sleep( 10 );
	}
	
	soundSystem->StopAllSounds(); // Stop Playing Video Sound track.
	material->MakeDefault();
} */

void idCommonLocal::RenderBink( const char* path,  const char* path_audio )
{
	// TODO: Fix video skipping on Linux
	const float sysWidth = renderSystem->GetWidth() * renderSystem->GetPixelAspect();
	const float sysHeight = renderSystem->GetHeight();
	const float sysAspect = sysWidth / sysHeight;
	const float movieAspect = ( 16.0f / 9.0f );
	const float imageWidth = SCREEN_WIDTH * movieAspect / sysAspect;
	const float chop = 0.5f * ( SCREEN_WIDTH - imageWidth );
	
	idStr materialText;
	materialText.Format( "{ translucent { videoMap %s } }", path );
		
	idMaterial* material = const_cast<idMaterial*>( declManager->FindMaterial( "video/kialogo" ) );		
	material->Parse( materialText.c_str(), materialText.Length(), false );
	material->ResetCinematicTime( Sys_Milliseconds() );
	
	soundWorld->PlayShaderDirectly( path_audio, 0 ); // "gui/loadvideointro"
	soundWorld->UnPause();
	soundSystem->SetPlayingSoundWorld( soundWorld );
	soundSystem->Render();			

	sysEvent_t	ev; //SS2 fix RoQ videos skipping
	bool EndVideo = false;

	// FIX ME: timing should be polled from the video
	while( Sys_Milliseconds() <= 14000 ) // 13 sec (+1 sec delay) is the length of the test video we have
	{		
		Sys_GenerateEvents();		
		ev = Sys_GetEvent();
		Sys_PollJoystickInputEvents( 0 );
		renderSystem->DrawStretchPic( chop, 0, imageWidth, SCREEN_HEIGHT, 0, 0, 1, 1, material );
		const emptyCommand_t* cmd = renderSystem->SwapCommandBuffers( &time_frontend, &time_backend, &time_shadows, &time_gpu );
		renderSystem->RenderCommandBuffers( cmd );		
		if ( !com_firstLaunchIntroVideos.GetBool() ) {
			if(ev.evType != SE_NONE) {				
				while(ev.evType != SE_NONE) {				
					if(ev.evType == SE_KEY) {
						if(ev.evValue < K_JOY_STICK1_UP || K_JOY_DPAD_RIGHT  < ev.evValue )			
							EndVideo = true;					
					}					
					ev = Sys_GetEvent();
				}
			}
		}
		if( EndVideo )
 			break;			
				// TODO: Restrict Joystick input to only allow pressing buttons/triggers to skip video, no dpad or sticks.		
		Sys_Sleep( 10 );		
	}
	
	soundSystem->StopAllSounds(); // Stop Playing Video Sound track.
	material->MakeDefault();
}


/*
=================
idCommonLocal::InitSIMD
=================
*/
void idCommonLocal::InitSIMD()
{
	idSIMD::InitProcessor( "stormengine2", com_forceGenericSIMD.GetBool() ); // SE2 fix: was "doom"
	idDmapSIMD::InitProcessor( "stormengine2-dmap", com_forceGenericSIMD.GetBool() ); // foresthale 2014-05-21: changed phaeton to phaeton-dmap
	com_forceGenericSIMD.ClearModified();
}


/*
=================
idCommonLocal::LoadGameDLL
=================
*/
void idCommonLocal::LoadGameDLL()
{
#ifdef __DOOM_DLL__
	char			dllPath[ MAX_OSPATH ];
	
	gameImport_t	gameImport;
	gameExport_t	gameExport;
	GetGameAPI_t	GetGameAPI;
	
	fileSystem->FindDLL( "game", dllPath, true );
	
	if( !dllPath[ 0 ] )
	{
		common->FatalError( "couldn't find game dynamic library" );
		return;
	}
	common->DPrintf( "Loading game DLL: '%s'\n", dllPath );
	gameDLL = sys->DLL_Load( dllPath );
	if( !gameDLL )
	{
		common->FatalError( "couldn't load game dynamic library" );
		return;
	}
	
	const char* functionName = "GetGameAPI";
	GetGameAPI = ( GetGameAPI_t ) Sys_DLL_GetProcAddress( gameDLL, functionName );
	if( !GetGameAPI )
	{
		Sys_DLL_Unload( gameDLL );
		gameDLL = NULL;
		common->FatalError( "couldn't find game DLL API" );
		return;
	}
	
	gameImport.version					= GAME_API_VERSION;
	gameImport.sys						= ::sys;
	gameImport.common					= ::common;
	gameImport.cmdSystem				= ::cmdSystem;
	gameImport.cvarSystem				= ::cvarSystem;
	gameImport.fileSystem				= ::fileSystem;
	gameImport.renderSystem				= ::renderSystem;
	gameImport.soundSystem				= ::soundSystem;
	gameImport.renderModelManager		= ::renderModelManager;
	gameImport.uiManager				= ::uiManager;
	gameImport.declManager				= ::declManager;
	gameImport.AASFileManager			= ::AASFileManager;
	gameImport.collisionModelManager	= ::collisionModelManager;
	
	gameExport							= *GetGameAPI( &gameImport );
	
	if( gameExport.version != GAME_API_VERSION )
	{
		Sys_DLL_Unload( gameDLL );
		gameDLL = NULL;
		common->FatalError( "wrong game DLL API version" );
		return;
	}
	
	game								= gameExport.game;
	gameEdit							= gameExport.gameEdit;
	
#endif
	
	// initialize the game object
	if( game != NULL )
	{
		game->Init();
	}
}

/*
=================
idCommonLocal::UnloadGameDLL
=================
*/
void idCommonLocal::CleanupShell()
{
	if( game != NULL )
	{
		game->Shell_Cleanup();
	}
}

/*
=================
idCommonLocal::UnloadGameDLL
=================
*/
void idCommonLocal::UnloadGameDLL()
{

	// shut down the game object
	if( game != NULL )
	{
		game->Shutdown();
	}
	
#ifdef __DOOM_DLL__
	
	if( gameDLL )
	{
		Sys_DLL_Unload( gameDLL );
		gameDLL = NULL;
	}
	game = NULL;
	gameEdit = NULL;
	
#endif
}

/*
=================
idCommonLocal::IsInitialized
=================
*/
bool idCommonLocal::IsInitialized() const
{
	return com_fullyInitialized;
}


//======================================================================================


/*
=================
idCommonLocal::Init
=================
*/
void idCommonLocal::Init( int argc, const char* const* argv, const char* cmdline )
{	
	try
	{
		// set interface pointers used by idLib
		idLib::sys			= sys;
		idLib::common		= common;
		idLib::cvarSystem	= cvarSystem;
		idLib::fileSystem	= fileSystem;
		
		// initialize idLib
		idLib::Init();
		
		// clear warning buffer
		ClearWarnings( GAME_NAME " initialization" );
		
		idLib::Printf( va( "Command line: %s\n", cmdline ) );
		//::MessageBox( NULL, cmdline, "blah", MB_OK );
		// parse command line options
		idCmdArgs args;
		if( cmdline )
		{
			// tokenize if the OS doesn't do it for us
			args.TokenizeString( cmdline, true );
			argv = args.GetArgs( &argc );
		}
		ParseCommandLine( argc, argv );
		
		// init console command system
		cmdSystem->Init();
		
		// init CVar system
		cvarSystem->Init();
		
		// register all static CVars
		idCVar::RegisterStaticVars();
		
		idLib::Printf( "QA Timing INIT: %06dms\n", Sys_Milliseconds() );
		
		// print engine version
		Printf( "%s\n", version.string );
		
		// initialize key input/binding, done early so bind command exists
		idKeyInput::Init();
		
		// init the console so we can take prints
		console->Init();
		
		// get architecture info
		Sys_Init();
		
		// initialize networking
		Sys_InitNetworking();
		
		// override cvars from command line
		StartupVariable( NULL );
		
		consoleUsed = com_allowConsole.GetBool();
		
		if( Sys_AlreadyRunning() )
		{
			Sys_Quit();
		}
		
		// initialize processor specific SIMD implementation
		InitSIMD();

		// init commands
		InitCommands();
		
		// initialize the file system
		fileSystem->Init();
		
		const char* defaultLang = Sys_DefaultLanguage();
		com_isJapaneseSKU = ( idStr::Icmp( defaultLang, ID_LANG_JAPANESE ) == 0 );
		
		// Allow the system to set a default lanugage
		Sys_SetLanguageFromSystem();
		
		// Pre-allocate our 20 MB save buffer here on time, instead of on-demand for each save....
		
		saveFile.SetNameAndType( SAVEGAME_CHECKPOINT_FILENAME, SAVEGAMEFILE_BINARY );
		saveFile.PreAllocate( MIN_SAVEGAME_SIZE_BYTES );
		
		stringsFile.SetNameAndType( SAVEGAME_STRINGS_FILENAME, SAVEGAMEFILE_BINARY );
		stringsFile.PreAllocate( MAX_SAVEGAME_STRING_TABLE_SIZE );

		// Pre-Allocate the Thumbnail file in memory.
		ThumbnailFile.SetNameAndType(SAVEGAME_THUMB_FILENAME, SAVEGAMEFILE_THUMB);
		ThumbnailFile.PreAllocate( MIN_SAVEGAME_PREVIEW_SIZE_BYTES );
		
		// motorsep 01-16-2015; Doom 3 BFG never had _startup.resources, we don't have it either. No idea what needs to go there, but apparently it's non-essential
		//fileSystem->BeginLevelLoad( "_startup", saveFile.GetDataPtr(), saveFile.GetAllocated() );
		
		// initialize the declaration manager
		declManager->Init();
		
		// init journalling, etc
		eventLoop->Init();
		
		// init the parallel job manager
		parallelJobManager->Init();
		
		// exec the startup scripts
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "exec editor.cfg\n" );
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "exec default.cfg\n" );
		
#ifdef CONFIG_FILE
		// skip the config file if "safe" is on the command line
		if( !SafeMode() && !g_demoMode.GetBool() )
		{
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "exec " CONFIG_FILE "\n" );
		}
#endif
		
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "exec autoexec.cfg\n" );
		
		// run cfg execution
		cmdSystem->ExecuteCommandBuffer();
		
		// re-override anything from the config files with command line args
		StartupVariable( NULL );
		
		// if any archived cvars are modified after this, we will trigger a writing of the config file
		cvarSystem->ClearModifiedFlags( CVAR_ARCHIVE );
		
		// init OpenGL, which will open a window and connect sound and input hardware
		renderSystem->InitOpenGL();
		
		// Support up to 2 digits after the decimal point
		com_engineHz_denominator = 100LL * com_engineHz.GetFloat();
		com_engineHz_latched = com_engineHz.GetFloat();
		
		// start the sound system, but don't do any hardware operations yet
		soundSystem->Init();
		
		// initialize the renderSystem data structures
		renderSystem->Init();
		
		whiteMaterial = declManager->FindMaterial( "_white" );
		
		if( idStr::Icmp( sys_lang.GetString(), ID_LANG_FRENCH ) == 0 )
		{
			// If the user specified french, we show french no matter what SKU
			splashScreen = declManager->FindMaterial( "guis/assets/splash/legal_french" );
		}
		else if( idStr::Icmp( defaultLang, ID_LANG_FRENCH ) == 0 )
		{
			// If the lead sku is french (ie: europe), display figs
			splashScreen = declManager->FindMaterial( "guis/assets/splash/legal_figs" );
		}
		else
		{
			// Otherwise show it in english
			splashScreen = declManager->FindMaterial( "guis/assets/splash/legal_english" );
		}
		
		//int legalStartTime = Sys_Milliseconds();		
		declManager->Init2();
		
		// initialize string database so we can use it for loading messages
		InitLanguageDict();
		
		// spawn the game thread, even if we are going to run without SMP
		// one meg stack, because it can parse decls from gui surfaces (unfortunately)
		// use a lower priority so job threads can run on the same core
		gameThread.StartWorkerThread( "Game/Draw", CORE_1B, THREAD_BELOW_NORMAL, 0x100000 );
		// boost this thread's priority, so it will prevent job threads from running while
		// the render back end still has work to do
		
		// init the user command input code
		usercmdGen->Init();
		
		Sys_SetRumble( 0, 0, 0 );
		
		// initialize the user interfaces
		uiManager->Init();
				
		// load the game dll
		LoadGameDLL();

		// On the PC touch them all so they get included in the resource build
		if( !fileSystem->UsingResourceFiles() )
		{
			declManager->FindMaterial( "guis/assets/splash/legal_english" );
			declManager->FindMaterial( "guis/assets/splash/legal_french" );
			declManager->FindMaterial( "guis/assets/splash/legal_figs" );
			// register the japanese font so it gets included
			renderSystem->RegisterFont( "DFPHeiseiGothicW7" );
			// Make sure all videos get touched because you can bring videos from one map to another, they need to be included in all maps
			for( int i = 0; i < declManager->GetNumDecls( DECL_VIDEO ); i++ )
			{
				declManager->DeclByIndex( DECL_VIDEO, i );
			}
		}
		
		fileSystem->UnloadResourceContainer( "_ordered" );
		
		// the same idRenderWorld will be used for all games
		// and demos, insuring that level specific models
		// will be freed
		renderWorld = renderSystem->AllocRenderWorld();
		soundWorld = soundSystem->AllocSoundWorld( renderWorld );
		
		menuSoundWorld = soundSystem->AllocSoundWorld( NULL );
		menuSoundWorld->PlaceListener( vec3_origin, mat3_identity, 0 );
		
		// init the session
		session->Initialize();
		session->InitializeSoundRelatedSystems();
		session->GetSignInManager().RegisterLocalUser( 0 );
		session->UpdateSignInManager();
		session->GetSaveGameManager().Pump();
		session->GetSaveGameManager().WaitForAllProcessors();
		session->Pump();
		InitializeMPMapsModes();

		// leaderboards need to be initialized after InitializeMPMapsModes, which populates the MP Map list.
		if( game != NULL )
		{
			game->Leaderboards_Init();
		}
		
		CreateMainMenu();
		
		commonDialog.Init();
		
		// load the console history file
		consoleHistory.LoadHistoryFile();
		
		AddStartupCommands();
		
		StartMenu( true );
				
		const int legalMinTime = 4000; // was 4000; how long to keep legal screen visible before bringing up menu
		const bool showVideo = ( !com_skipIntroVideos.GetBool() && fileSystem->UsingResourceFiles() );
		
		// SE2 fix begins
		if( showVideo )
		{
			//RenderBink( "video\\loadvideo.roq", "gui/loadvideointro" ); // video file, sound shader
			RenderBink( "video\\kialogo.roq", "into_sound/kialogo" ); // video file, sound shader
			//RenderSplash();
			//RenderSplash();
		}
		/*else
		{
			idLib::Printf( "Skipping Intro Videos!\n" );
			// display the legal splash screen
			// No clue why we have to render this twice to show up...
			RenderSplash();
			RenderSplash();
		}		*/
		
		int legalStartTime = Sys_Milliseconds(); // SE2 fix ends

	    renderSystem->LoadLevelImages();
#ifndef _DEBUG
		while( Sys_Milliseconds() - legalStartTime < legalMinTime )		
		{			
			RenderSplash();
			Sys_GenerateEvents();
			Sys_Sleep( 10 );
		}	
#endif
		
		com_firstLaunchIntroVideos.SetBool( false ); // SE2 feature; allow intro video to be skippable

		// print all warnings queued during initialization
		PrintWarnings();
		
		// remove any prints from the notify lines
		console->ClearNotifyLines();
		
		CheckStartupStorageRequirements();
				
		if( preload_CommonAssets.GetBool() && fileSystem->UsingResourceFiles() )
		{
			idPreloadManifest manifest;
			manifest.LoadManifest( "_common.preload" );
			globalImages->Preload( manifest, false );
			soundSystem->Preload( manifest );
		}
		
		fileSystem->EndLevelLoad();
		
		com_fullyInitialized = true;
		
		
		// No longer need the splash screen
		if( splashScreen != NULL )
		{
			for( int i = 0; i < splashScreen->GetNumStages(); i++ )
			{
				idImage* image = splashScreen->GetStage( i )->texture.image;
				if( image != NULL )
				{
					image->PurgeImage();
				}
			}
		}
		
		Printf( "--- Common Initialization Complete ---\n" );
		
		idLib::Printf( "QA Timing IIS: %06dms\n", Sys_Milliseconds() );
	}
	catch( idException& )
	{
		Sys_Error( "Error during initialization" );
	}
}

/*
=================
idCommonLocal::Shutdown
=================
*/
void idCommonLocal::Shutdown()
{
	if( com_shuttingDown )
	{
		return;
	}
	com_shuttingDown = true;
	
	
	// Kill any pending saves...
	printf( "session->GetSaveGameManager().CancelToTerminate();\n" );
	session->GetSaveGameManager().CancelToTerminate();
	
	// kill sound first
	printf( "soundSystem->StopAllSounds();\n" );
	soundSystem->StopAllSounds();
	
	// shutdown the script debugger
	DebuggerServerShutdown();
	
	if( aviCaptureMode )
	{
		printf( "EndAVICapture();\n" );
		EndAVICapture();
	}
	
	printf( "Stop();\n" );
	Stop();
	
	printf( "CleanupShell();\n" );
	CleanupShell();
	
	printf( "delete loadGUI;\n" );
	delete loadGUI;
	loadGUI = NULL;
	
	printf( "delete renderWorld;\n" );
	delete renderWorld;
	renderWorld = NULL;
	
	printf( "delete soundWorld;\n" );
	delete soundWorld;
	soundWorld = NULL;
	
	printf( "delete menuSoundWorld;\n" );
	delete menuSoundWorld;
	menuSoundWorld = NULL;
	
	// shut down the session
	printf( "session->ShutdownSoundRelatedSystems();\n" );
	session->ShutdownSoundRelatedSystems();
	printf( "session->Shutdown();\n" );
	session->Shutdown();
	
	// shutdown, deallocate leaderboard definitions.
	if( game != NULL )
	{
		printf( "game->Leaderboards_Shutdown();\n" );
		game->Leaderboards_Shutdown();
	}
	
	// shut down the user interfaces
	printf( "uiManager->Shutdown();\n" );
	uiManager->Shutdown();
	
	// shut down the sound system
	printf( "soundSystem->Shutdown();\n" );
	soundSystem->Shutdown();
	
	// shut down the user command input code
	printf( "usercmdGen->Shutdown();\n" );
	usercmdGen->Shutdown();
	
	// shut down the event loop
	printf( "eventLoop->Shutdown();\n" );
	eventLoop->Shutdown();
	
	// shutdown the decl manager
	printf( "declManager->Shutdown();\n" );
	declManager->Shutdown();
	
	// shut down the renderSystem
	printf( "renderSystem->Shutdown();\n" );
	renderSystem->Shutdown();
	
	printf( "commonDialog.Shutdown();\n" );
	commonDialog.Shutdown();

	// foresthale 2014-05-28: stop the game thread before we get to doexit() and hit all the dtors and end up waiting on a dead idGameThread
	gameThread.StopThread();

	// unload the game dll
	printf( "UnloadGameDLL();\n" );
	UnloadGameDLL();
	
	printf( "saveFile.Clear( true );\n" );
	saveFile.Clear( true );
	printf( "stringsFile.Clear( true );\n" );
	stringsFile.Clear( true );
	printf( "ThumbnailFile.Clear( true );\n" );	
	ThumbnailFile.Clear(true);
	
	// only shut down the log file after all output is done
	printf( "CloseLogFile();\n" );
	CloseLogFile();
	
	// shut down the file system
	printf( "fileSystem->Shutdown( false );\n" );
	fileSystem->Shutdown( false );
	
	// shut down non-portable system services
	printf( "Sys_Shutdown();\n" );
	Sys_Shutdown();
	
	// shut down the console
	printf( "console->Shutdown();\n" );
	console->Shutdown();
	
	// shut down the key system
	printf( "idKeyInput::Shutdown();\n" );
	idKeyInput::Shutdown();
	
	// shut down the cvar system
	printf( "cvarSystem->Shutdown();\n" );
	cvarSystem->Shutdown();
	
	// shut down the console command system
	printf( "cmdSystem->Shutdown();\n" );
	cmdSystem->Shutdown();
	
	// free any buffered warning messages
	printf( "ClearWarnings( GAME_NAME \" shutdown\" );\n" );
	ClearWarnings( GAME_NAME " shutdown" );
	printf( "warningCaption.Clear();\n" );
	warningCaption.Clear();
	printf( "errorList.Clear();\n" );
	errorList.Clear();
	
	// shutdown idLib
	printf( "idLib::ShutDown();\n" );
	idLib::ShutDown();
}

/*
========================
idCommonLocal::CreateMainMenu
========================
*/
void idCommonLocal::CreateMainMenu()
{
	if( game != NULL )
	{
		// note which media we are going to need to load
		declManager->BeginLevelLoad();
		renderSystem->BeginLevelLoad();
		soundSystem->BeginLevelLoad();
		uiManager->BeginLevelLoad();
		
		// create main inside an "empty" game level load - so assets get
		// purged automagically when we transition to a "real" map
		game->Shell_CreateMenu( false );
		game->Shell_Show( true );
		game->Shell_SyncWithSession();
		
		// load
		renderSystem->EndLevelLoad();
		soundSystem->EndLevelLoad();
		declManager->EndLevelLoad();
		uiManager->EndLevelLoad( "" );
	}
}

/*
===============
idCommonLocal::Stop

called on errors and game exits
===============
*/
void idCommonLocal::Stop( bool resetSession )
{
	ClearWipe();
	
	// clear mapSpawned and demo playing flags
	UnloadMap();
	
	soundSystem->StopAllSounds();
	
	insideUpdateScreen = false;
	insideExecuteMapChange = false;
	
	// drop all guis
	ExitMenu();
	
	if( resetSession )
	{
		session->QuitMatchToTitle();
	}
}

/*
===============
idCommonLocal::BusyWait
===============
*/
void idCommonLocal::BusyWait()
{
	Sys_GenerateEvents();
	
	const bool captureToImage = false;
	UpdateScreen( captureToImage );
	
	session->UpdateSignInManager();
	session->Pump();
}

/*
===============
idCommonLocal::WaitForSessionState
===============
*/
bool idCommonLocal::WaitForSessionState( idSession::sessionState_t desiredState )
{
	if( session->GetState() == desiredState )
	{
		return true;
	}
	
	while( true )
	{
		BusyWait();
		
		idSession::sessionState_t sessionState = session->GetState();
		if( sessionState == desiredState )
		{
			return true;
		}
		if( sessionState != idSession::LOADING &&
				sessionState != idSession::SEARCHING &&
				sessionState != idSession::CONNECTING &&
				sessionState != idSession::BUSY &&
				sessionState != desiredState )
		{
			return false;
		}
		
		Sys_Sleep( 10 );
	}
}

/*
========================
idCommonLocal::LeaveGame
========================
*/
void idCommonLocal::LeaveGame()
{

	const bool captureToImage = false;
	UpdateScreen( captureToImage );
	
	ResetNetworkingState();
	
	
	Stop( false );
	
	CreateMainMenu();
	
	StartMenu();
	
	
}

/*
===============
idCommonLocal::ProcessEvent
===============
*/
bool idCommonLocal::ProcessEvent( const sysEvent_t* event )
{
	// hitting escape anywhere brings up the menu
	if( game && game->IsInGame() )
	{
		if( event->evType == SE_KEY && event->evValue2 == 1 && ( event->evValue == K_ESCAPE || event->evValue == K_JOY9 ) )
		{
			if( !game->Shell_IsActive() )
			{
			
				// menus / etc
				if( MenuEvent( event ) )
				{
					return true;
				}
				
				console->Close();
				
				StartMenu();
				return true;
			}
			else
			{
				console->Close();
				
				// menus / etc
				if( MenuEvent( event ) )
				{
					return true;
				}
				
				game->Shell_ClosePause();
			}
		}
	}
	
	// let the pull-down console take it if desired
	if( console->ProcessEvent( event, false ) )
	{
		return true;
	}
	if( session->ProcessInputEvent( event ) )
	{
		return true;
	}
	
	if( Dialog().IsDialogActive() )
	{
		Dialog().HandleDialogEvent( event );
		return true;
	}
	
	// menus / etc
	if( MenuEvent( event ) )
	{
		return true;
	}
	
	// if we aren't in a game, force the console to take it
	if( !mapSpawned )
	{
		console->ProcessEvent( event, true );
		return true;
	}
	
	// in game, exec bindings for all key downs
	if( event->evType == SE_KEY && event->evValue2 == 1 )
	{
		idKeyInput::ExecKeyBinding( event->evValue );
		return true;
	}
	
	return false;
}

/*
========================
idCommonLocal::ResetPlayerInput
========================
*/
void idCommonLocal::ResetPlayerInput( int playerIndex )
{
	userCmdMgr.ResetPlayer( playerIndex );
}

/*
==================
idCommonLocal::WriteFlaggedCVarsToFile
==================
*/
void idCommonLocal::WriteFlaggedCVarsToFile( const char *filename, int flags, const char *setCmd ) {
	idFile *f;

	f = fileSystem->OpenFileWrite( filename, "fs_configpath" );
	if ( !f ) {
		Printf( "Couldn't write %s.\n", filename );
		return;
	}
	cvarSystem->WriteFlaggedVariables( flags, setCmd, f );
	fileSystem->CloseFile( f );
}

/*
==================
Common_WritePrecache_f
==================
*/
CONSOLE_COMMAND( writePrecache, "writes precache commands", NULL )
{
	if( args.Argc() != 2 )
	{
		common->Printf( "USAGE: writePrecache <execFile>\n" );
		return;
	}
	idStr	str = args.Argv( 1 );
	str.DefaultFileExtension( ".cfg" );
	idFile* f = fileSystem->OpenFileWrite( str );
	declManager->WritePrecacheCommands( f );
	renderModelManager->WritePrecacheCommands( f );
	uiManager->WritePrecacheCommands( f );
	
	fileSystem->CloseFile( f );
}

/*
================
Common_Disconnect_f
================
*/
CONSOLE_COMMAND_SHIP( disconnect, "disconnects from a game", NULL )
{
	if ( !(com_editors & EDITOR_DMAP) )
	{
		session->QuitMatch();
	}
	else
	{
		commonLocal.LeaveGame();
	}
}

/*
===============
Common_Hitch_f
===============
*/
CONSOLE_COMMAND( hitch, "hitches the game", NULL )
{
	if( args.Argc() == 2 )
	{
		Sys_Sleep( atoi( args.Argv( 1 ) ) );
	}
	else
	{
		Sys_Sleep( 100 );
	}
}

CONSOLE_COMMAND( showStringMemory, "shows memory used by strings", NULL )
{
	idStr::ShowMemoryUsage_f( args );
}
CONSOLE_COMMAND( showDictMemory, "shows memory used by dictionaries", NULL )
{
	idDict::ShowMemoryUsage_f( args );
}
CONSOLE_COMMAND( listDictKeys, "lists all keys used by dictionaries", NULL )
{
	idDict::ListKeys_f( args );
}
CONSOLE_COMMAND( listDictValues, "lists all values used by dictionaries", NULL )
{
	idDict::ListValues_f( args );
}
CONSOLE_COMMAND( testSIMD, "test SIMD code", NULL )
{
	idSIMD::Test_f( args );
}