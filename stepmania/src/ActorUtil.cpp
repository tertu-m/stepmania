#include "global.h"
#include "ActorUtil.h"
#include "Sprite.h"
#include "BitmapText.h"
#include "Model.h"
#include "BGAnimation.h"
#include "IniFile.h"
#include "ThemeManager.h"
#include "RageDisplay.h"
#include "RageLog.h"
#include "arch/ArchHooks/ArchHooks.h"
#include "RageFileManager.h"
#include "SongCreditDisplay.h"
#include "song.h"
#include "GameState.h"
#include "RageTextureManager.h"
#include "SongManager.h"
#include "Course.h"

#include "arch/Dialog/Dialog.h"


Actor* LoadFromActorFile( const CString &sIniPath, const CString &sLayer )
{
	// TODO: Check for recursive loading
	IniFile ini;
	if( !ini.ReadFile( sIniPath ) )
		RageException::Throw( "%s", ini.GetError().c_str() );
	
	if( !ini.GetKey(sLayer) )
		RageException::Throw( "The file '%s' doesn't have layer '%s'.", sIniPath.c_str(), sLayer.c_str() );

	CString sDir = Dirname( sIniPath );

	const IniFile::key* k = ini.GetKey( sLayer );
	if( k == NULL )
		RageException::Throw( "The file '%s' doesn't have layer '%s'.", sIniPath.c_str(), sLayer.c_str() );

	return LoadFromActorFile( sDir, *k );
}

Actor* LoadFromActorFile( const CString& sAniDir, const IniKey& layer )
{
	Actor* pActor = NULL;	// fill this in before we return;

	CString sType;
	layer.GetValue( "Type", sType );
	CString sFile;
	layer.GetValue( "File", sFile );
	FixSlashesInPlace( sFile );

	if( sType == "SongCreditDisplay" )
	{
		pActor = new SongCreditDisplay;
	}
	else
	{

		/* XXX: How to handle translations?  Maybe we should have one metrics section,
		 * "Text", eg:
		 *
		 * [Text]
		 * SoundVolume=Sound Volume
		 * TextItem=Hello
		 *
		 * and allow "$TextItem$" in .actors to reference that.
		 */
		/* Be careful: if sFile is "", and we don't check it, then we can end up recursively
		 * loading the BGAnimationLayer that we're in. */
		if( sFile == "" )
			RageException::Throw( "The actor file in '%s' is missing the File argument",
				sAniDir.c_str() );

		CString text;
		if( layer.GetValue("Text", text) )
		{
			/* It's a BitmapText. Note that we could do the actual text setting with metrics,
			 * by adding "text" and "alttext" commands, but right now metrics can't contain
			 * commas or semicolons.  It's useful to be able to refer to fonts in the real
			 * theme font dirs, too. */
			CString alttext;
			layer.GetValue("AltText", alttext );
			text.Replace( "::", "\n" );
			alttext.Replace( "::", "\n" );

			BitmapText* pBitmapText = new BitmapText;

			pBitmapText->LoadFromFont( THEME->GetPathToF( sFile ) );
			pBitmapText->SetText( text, alttext );
			pActor = pBitmapText;
		}
		else
		{
			if( sFile.CompareNoCase("songbackground")==0 )
			{
				Song *pSong = GAMESTATE->m_pCurSong;
				if( pSong && pSong->HasBackground() )
					sFile = pSong->GetBackgroundPath();
				else
					sFile = THEME->GetPathToG("Common fallback background");

				/* Always load song backgrounds with SongBGTexture.  It sets texture properties;
				 * if we load a background without setting those properties, we'll end up
				 * with duplicates. */
				Sprite* pSprite = new Sprite;
				pSprite->LoadBG( sFile );
				pActor = pSprite;
			}
			else if( sFile.CompareNoCase("songbanner")==0 )
			{
				Song *pSong = GAMESTATE->m_pCurSong;
				if( pSong == NULL )
				{
					// probe for a random banner
					for( int i=0; i<300; i++ )
					{
						pSong = SONGMAN->GetRandomSong();
						if( pSong == NULL )
							break;
						if( !pSong->ShowInDemonstrationAndRanking() )
							continue;
						break;
					}
				}

				if( pSong && pSong->HasBanner() )
					sFile = pSong->GetBannerPath();
				else
					sFile = THEME->GetPathToG("Common fallback banner");

				TEXTUREMAN->DisableOddDimensionWarning();

				/* Always load banners with BannerTex.  It sets texture properties;
				 * if we load a background without setting those properties, we'll end up
				 * with duplicates. */
				Sprite* pSprite = new Sprite;
				pSprite->Load( Sprite::SongBannerTexture(sFile) );
				pActor = pSprite;

				TEXTUREMAN->EnableOddDimensionWarning();
			}
			else if( sFile.CompareNoCase("coursebanner")==0 )
			{
				Course *pCourse = GAMESTATE->m_pCurCourse;
				if( pCourse == NULL )
				{
					// probe for a random banner
					for( int i=0; i<300; i++ )
					{
						pCourse = SONGMAN->GetRandomCourse();
						if( pCourse == NULL )
							break;
						if( !pCourse->ShowInDemonstrationAndRanking() )
							continue;
						if( pCourse->m_bIsAutogen )
							continue;
						break;
					}
				}

				if( pCourse && pCourse->HasBanner() )
					sFile = pCourse->m_sBannerPath;
				else
					sFile = THEME->GetPathToG("Common fallback banner");

				TEXTUREMAN->DisableOddDimensionWarning();
				Sprite* pSprite = new Sprite;
				pSprite->Load( Sprite::SongBannerTexture(sFile) );
				pActor = pSprite;
				TEXTUREMAN->EnableOddDimensionWarning();
			}
			else 
			{
retry:
				/* XXX: We need to do a theme search, since the file we're loading might
				 * be overridden by the theme. */
				CString sNewPath = sAniDir+sFile;

				// If we know this is an exact match, don't bother with the GetDirListing;
				// it's causing problems with partial matching BGAnimation directory names.
				if( !IsAFile(sNewPath) && !IsADirectory(sNewPath) )
				{
					CStringArray asPaths;
					GetDirListing( sNewPath + "*", asPaths, false, true );	// return path too

					if( asPaths.empty() )
					{
						CString sError = ssprintf( "The actor file in '%s' references a file '%s' which doesn't exist.", sAniDir.c_str(), sFile.c_str() );
						switch( Dialog::AbortRetryIgnore( sError, "BROKEN_ACTOR_REFERENCE" ) )
						{
						case Dialog::abort:
							RageException::Throw( sError ); 
							break;
						case Dialog::retry:
							FlushDirCache();
							goto retry;
						case Dialog::ignore:
							asPaths.push_back( sNewPath );
							if( GetExtension(asPaths[0]) == "" )
								asPaths[0] = SetExtension( asPaths[0], "png" );
							break;
						default:
							ASSERT(0);
						}
					}
					else if( asPaths.size() > 1 )
					{
						CString sError = ssprintf( "The actor file in '%s' references a file '%s' which has multiple matches.", sAniDir.c_str(), sFile.c_str() );
						switch( Dialog::AbortRetryIgnore( sError, "DUPLICATE_ACTOR_REFERENCE" ) )
						{
						case Dialog::abort:
							RageException::Throw( sError ); 
							break;
						case Dialog::retry:
							FlushDirCache();
							goto retry;
						case Dialog::ignore:
							asPaths.erase( asPaths.begin()+1, asPaths.end() );
							break;
						default:
							ASSERT(0);
						}
					}

					sNewPath = asPaths[0];
				}

				sNewPath = DerefRedir( sNewPath );

				pActor = MakeActor( sNewPath );
			}
		}
	}

	float f;
	if( layer.GetValue( "BaseRotationXDegrees", f ) )	pActor->SetBaseRotationX( f );
	if( layer.GetValue( "BaseRotationYDegrees", f ) )	pActor->SetBaseRotationY( f );
	if( layer.GetValue( "BaseRotationZDegrees", f ) )	pActor->SetBaseRotationZ( f );
	if( layer.GetValue( "BaseZoomX", f ) )				pActor->SetBaseZoomX( f );
	if( layer.GetValue( "BaseZoomY", f ) )				pActor->SetBaseZoomY( f );
	if( layer.GetValue( "BaseZoomZ", f ) )				pActor->SetBaseZoomZ( f );


	//
	// Load commands
	//
	for( IniKey::const_iterator i = layer.begin();
		 i != layer.end(); ++i)
	{
		CString KeyName = i->first; /* "OnCommand" */
		KeyName.MakeLower();

		if( KeyName.Right(7) != "command" )
			continue; /* not a command */

		const CString &sCommands = i->second;
		Commands cmds = ParseCommands( sCommands );
		CString sCmdName;
		/* Special case: "Command=foo" -> "OnCommand=foo" */
		if( KeyName.size() == 7 )
			sCmdName="on";
		else
			sCmdName = KeyName.Left( KeyName.size()-7 );
		pActor->AddCommands( sCmdName, cmds );
	}


	ASSERT( pActor );	// we should have filled this in above
	return pActor;
}

Actor* MakeActor( const RageTextureID &ID )
{
	CString sExt = GetExtension( ID.filename );
	sExt.MakeLower();
	
	if( sExt=="actor" )
	{
		return LoadFromActorFile( ID.filename );
	}
	else if( sExt=="png" ||
		sExt=="jpg" || 
		sExt=="gif" || 
		sExt=="bmp" || 
		sExt=="avi" || 
		sExt=="mpeg" || 
		sExt=="mpg" ||
		sExt=="sprite" )
	{
		Sprite* pSprite = new Sprite;
		pSprite->Load( ID );
		return pSprite;
	}
	else if( sExt=="txt" ||
		sExt=="model" )
	{
		Model* pModel = new Model;
		pModel->Load( ID.filename );
		return pModel;
	}
	/* Do this last, to avoid the IsADirectory in most cases. */
	else if( IsADirectory(ID.filename)  )
	{
		BGAnimation *pBGA = new BGAnimation( true );
		pBGA->LoadFromAniDir( ID.filename );
		return pBGA;
	}
	else 
	{
		RageException::Throw("File \"%s\" has unknown type, \"%s\"",
			ID.filename.c_str(), sExt.c_str() );
	}
}

void UtilSetXY( Actor& actor, const CString &sScreenName )
{
	ASSERT( !actor.GetID().empty() );
	actor.SetXY( THEME->GetMetricF(sScreenName,actor.GetID()+"X"), THEME->GetMetricF(sScreenName,actor.GetID()+"Y") );
}

void UtilCommand( Actor& actor, const CString &sScreenName, const CString &sCommandName )
{
	// If Actor is hidden, it won't get updated or drawn, so don't bother tweening.
	/* ... but we might be unhiding it, or setting state for when we unhide it later */
//	if( actor.GetHidden() )
//		return 0;

	Command ac;
	ac.Load("playcommand,"+sCommandName);
	actor.HandleCommand( ac );

	// HACK:  It's very often that we command things to TweenOffScreen 
	// that we aren't drawing.  We know that an Actor is not being
	// used if its name is blank.  So, do nothing on Actors with a blank name.
	// (Do "playcommand" anyway; BGAs often have no name.)
	if( sCommandName=="Off" )
	{
		if( actor.GetID().empty() )
			return;
	}
	else
	{
		ASSERT_M( !actor.GetID().empty(), ssprintf("!actor.GetID().empty() ('%s', '%s')",
												   sScreenName.c_str(), sCommandName.c_str()) );
	}

	actor.RunCommands( THEME->GetMetricA(sScreenName,actor.GetID()+sCommandName+"Command") );
}

void AutoActor::Load( const CString &sPath )
{
	Unload();
	m_pActor = MakeActor( sPath );
}

void AutoActor::LoadAndSetName( const CString &sScreenName, const CString &sActorName )
{
	Load( THEME->GetPathG(sScreenName,sActorName) );
	m_pActor->SetName( sActorName );
}

/*
 * (c) 2003-2004 Chris Danford
 * All rights reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
