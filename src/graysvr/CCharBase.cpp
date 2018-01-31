#include "graysvr.h"	// predef header.

/////////////////////////////////////////////////////////////////
// -CCharBase

CCharBase::CCharBase( CREID_TYPE id ) :
	CBaseBaseDef( RESOURCE_ID( RES_CHARDEF, id ))
{
	m_iHireDayWage = 0;
	m_trackID = ITEMID_TRACK_WISP;
	m_soundBase = SOUND_NONE;
	m_soundIdle = SOUND_NONE;
	m_soundNotice = SOUND_NONE;
	m_soundHit = SOUND_NONE;
	m_soundGetHit = SOUND_NONE;
	m_soundDie = SOUND_NONE;
	m_defense = 0;
	m_defenseBase = 0;
	m_defenseRange = 0;
	m_Anims = 0xFFFFFF;
  	m_MaxFood = 0;
	m_wBloodHue = HUE_DEFAULT;
	m_wColor = HUE_DEFAULT;
	m_Str = 0;
	m_Dex = 0;
	m_Int = 0;

	m_iMoveRate = static_cast<short>(g_Cfg.m_iMoveRate);

	if ( IsValidDispID(id))
	{
		// in display range.
		m_dwDispIndex = id;
	}
	else
	{
		m_dwDispIndex = 0;	// must read from SCP file later
	}

	SetResDispDnId(CREID_MAN);
}

// From "Bill the carpenter" or "#HUMANMALE the Carpenter",
// Get "Carpenter"
LPCTSTR CCharBase::GetTradeName() const
{
	ADDTOCALLSTACK("CCharBase::GetTradeName");
	LPCTSTR pName = CBaseBaseDef::GetTypeName();
	if ( pName[0] != '#' )
		return( pName );

	LPCTSTR pSpace = strchr( pName, ' ' );
	if ( pSpace == NULL )
		return( pName );

	pSpace++;
	if ( ! strnicmp( pSpace, "the ", 4 ))
		pSpace += 4;
	return( pSpace );
}

void CCharBase::CopyBasic( const CCharBase * pCharDef )
{
	ADDTOCALLSTACK("CCharBase::CopyBasic");
	m_trackID = pCharDef->m_trackID;
	m_soundBase = pCharDef->m_soundBase;
	m_soundIdle = pCharDef->m_soundIdle;
	m_soundNotice = pCharDef->m_soundNotice;
	m_soundHit = pCharDef->m_soundHit;
	m_soundGetHit = pCharDef->m_soundGetHit;
	m_soundDie = pCharDef->m_soundDie;

	m_wBloodHue = pCharDef->m_wBloodHue;
	m_MaxFood = pCharDef->m_MaxFood;
	m_FoodType = pCharDef->m_FoodType;
	m_Desires = pCharDef->m_Desires;

	m_defense = pCharDef->m_defense;
	m_Anims = pCharDef->m_Anims;

	m_BaseResources = pCharDef->m_BaseResources;

	CBaseBaseDef::CopyBasic( pCharDef );	// This will overwrite the CResourceLink!!
}

// Setting the visual "ID" for this.
bool CCharBase::SetDispID( CREID_TYPE id )
{
	ADDTOCALLSTACK("CCharBase::SetDispID");
	if ( id == GetID())
		return true;
	if ( id == GetDispID())
		return true;
	if ( ! IsValidDispID( id ))
	{
		DEBUG_ERR(( "Creating char SetDispID(0%x) > %d\n", id, CREID_QTY ));
		return false; // must be in display range.
	}

	// Copy the rest of the stuff from the display base.
	CCharBase * pCharDef = FindCharBase( id );
	if ( pCharDef == NULL )
	{
		DEBUG_ERR(( "Creating char SetDispID(0%x) BAD\n", id ));
		return( false );
	}

	CopyBasic( pCharDef );
	return( true );
}

// Setting what do I eat
void CCharBase::SetFoodType( LPCTSTR pszFood )
{
	ADDTOCALLSTACK("CCharBase::SetFoodType");
  	m_FoodType.Load( pszFood );

  	// Try to determine the real value
	m_MaxFood = 0;
	for ( size_t i = 0; i < m_FoodType.GetCount(); i++ )
	{
		if ( m_MaxFood < m_FoodType[i].GetResQty())
			m_MaxFood = static_cast<int>(m_FoodType[i].GetResQty());
	}
}

enum CBC_TYPE
{
	#define ADD(a,b) CBC_##a,
	#include "../tables/CCharBase_props.tbl"
	#undef ADD
	CBC_QTY
};

LPCTSTR const CCharBase::sm_szLoadKeys[CBC_QTY+1] =
{
	#define ADD(a,b) b,
	#include "../tables/CCharBase_props.tbl"
	#undef ADD
	NULL
};

bool CCharBase::r_WriteVal( LPCTSTR pszKey, CGString & sVal, CTextConsole * pSrc )
{
	ADDTOCALLSTACK("CCharBase::r_WriteVal");
	EXC_TRY("WriteVal");
	switch ( FindTableSorted( pszKey, sm_szLoadKeys, COUNTOF( sm_szLoadKeys )-1 ))
	{
		//return as string or hex number or NULL if not set
		case CBC_THROWDAM:
		case CBC_THROWOBJ:
		case CBC_THROWRANGE:
			sVal = GetDefStr(pszKey, false);
			break;
		//return as decimal number or 0 if not set
		case CBC_FOLLOWERSLOTS:
		case CBC_MAXFOLLOWER:
		case CBC_BONDED:
		case CBC_TITHING:
			sVal.FormatLLVal(GetDefNum(pszKey));
			break;
		case CBC_ANIM:
			sVal.FormatHex( m_Anims );
			break;
		case CBC_AVERSIONS:
			{
				TCHAR *pszTmp = Str_GetTemp();
				m_Aversions.WriteKeys(pszTmp);
				sVal = pszTmp;
			}
			break;
		case CBC_CAN:
			sVal.FormatHex( m_Can);
			break;
		case CBC_BLOODCOLOR:
			sVal.FormatHex( m_wBloodHue );
			break;
		case CBC_ARMOR:
			sVal.FormatVal( m_defense );
			break;
		case CBC_COLOR:
			sVal.FormatHex( m_wColor );
			break;
		case CBC_DESIRES:
			{
				TCHAR *pszTmp = Str_GetTemp();
				m_Desires.WriteKeys(pszTmp);
				sVal = pszTmp;
			}
			break;
		case CBC_DEX:
			sVal.FormatVal( m_Dex );
			break;
		case CBC_DISPID:
			sVal = g_Cfg.ResourceGetName( RESOURCE_ID( RES_CHARDEF, GetDispID()));
			break;
		case CBC_ID:
			sVal.FormatHex( GetDispID() );
			break;
		case CBC_FOODTYPE:
			{
				TCHAR *pszTmp = Str_GetTemp();
				m_FoodType.WriteKeys(pszTmp);
				sVal = pszTmp;
			}
			break;
		case CBC_HIREDAYWAGE:
			sVal.FormatVal( m_iHireDayWage );
			break;
		case CBC_ICON:
			sVal.FormatHex( m_trackID );
			break;
		case CBC_INT:
			sVal.FormatVal( m_Int );
			break;
		case CBC_JOB:
			sVal = GetTradeName();
			break;
		case CBC_MAXFOOD:
			sVal.FormatVal( m_MaxFood );
			break;
		case CBC_MOVERATE:
			sVal.FormatVal(m_iMoveRate);
			break;
		case CBC_RESDISPDNID:
			sVal = g_Cfg.ResourceGetName( RESOURCE_ID( RES_CHARDEF, GetResDispDnId()));
			break;
		case CBC_SOUND:
			sVal.FormatHex(m_soundBase);
			break;
		case CBC_SOUNDDIE:
			sVal.FormatHex(m_soundDie);
			break;
		case CBC_SOUNDGETHIT:
			sVal.FormatHex(m_soundGetHit);
			break;
		case CBC_SOUNDHIT:
			sVal.FormatHex(m_soundHit);
			break;
		case CBC_SOUNDIDLE:
			sVal.FormatHex(m_soundIdle);
			break;
		case CBC_SOUNDNOTICE:
			sVal.FormatHex(m_soundNotice);
			break;
		case CBC_STR:
			sVal.FormatVal( m_Str );
			break;
		case CBC_TSPEECH:
			m_Speech.WriteResourceRefList( sVal );
			break;
		default:
			return( CBaseBaseDef::r_WriteVal( pszKey, sVal ));
	}
	return true;
	EXC_CATCH;

	EXC_DEBUG_START;
	EXC_ADD_KEYRET(pSrc);
	EXC_DEBUG_END;
	return false;
}

bool CCharBase::r_LoadVal( CScript & s )
{
	ADDTOCALLSTACK("CCharBase::r_LoadVal");
	EXC_TRY("LoadVal");
	if ( ! s.HasArgs())
		return( false );
	switch ( FindTableSorted( s.GetKey(), sm_szLoadKeys, COUNTOF( sm_szLoadKeys )-1 ))
	{
		//Set as Strings
		case CBC_THROWDAM:
		case CBC_THROWOBJ:
		case CBC_THROWRANGE:
			{
				bool fQuoted = false;
				SetDefStr(s.GetKey(), s.GetArgStr( &fQuoted ), fQuoted);
			}
			break;
		//Set as number only
		case CBC_FOLLOWERSLOTS:
		case CBC_MAXFOLLOWER:
		case CBC_BONDED:
		case CBC_TITHING:
			SetDefNum(s.GetKey(), s.GetArgVal(), false);
			break;

		case CBC_ANIM:
			m_Anims = s.GetArgVal();
			break;
		case CBC_AVERSIONS:
			m_Aversions.Load( s.GetArgStr() );
			break;
		case CBC_CAN:
			m_Can = s.GetArgVal();
			break;
		case CBC_BLOODCOLOR:
			m_wBloodHue = static_cast<HUE_TYPE>(s.GetArgVal());
			break;
		case CBC_ARMOR:
			m_defense = static_cast<WORD>(s.GetArgVal());
			break;
		case CBC_COLOR:
			m_wColor = static_cast<HUE_TYPE>(s.GetArgVal());
			break;
		case CBC_DESIRES:
			m_Desires.Load( s.GetArgStr() );
			break;
		case CBC_DEX:
			m_Dex = static_cast<int>(s.GetArgVal());
			break;
		case CBC_DISPID:
			return( false );
		case CBC_FOODTYPE:
			SetFoodType( s.GetArgStr());
			break;
		case CBC_HIREDAYWAGE:
			m_iHireDayWage = s.GetArgVal();
			break;
		case CBC_ICON:
			{
				ITEMID_TYPE id = static_cast<ITEMID_TYPE>(g_Cfg.ResourceGetIndexType( RES_ITEMDEF, s.GetArgStr()));
				if ( id < 0 || id >= ITEMID_MULTI )
				{
					return( false );
				}
				m_trackID = id;
			}
			break;
		case CBC_ID:
			{
				return SetDispID(static_cast<CREID_TYPE>(g_Cfg.ResourceGetIndexType( RES_CHARDEF, s.GetArgStr())));
			}
		case CBC_INT:
			m_Int = static_cast<int>(s.GetArgVal());
			break;
		case CBC_MAXFOOD:
			m_MaxFood = static_cast<int>(s.GetArgVal());
			break;
		case CBC_MOVERATE:
			m_iMoveRate = static_cast<short>(s.GetArgVal());
			break;
		case CBC_RESDISPDNID:
			SetResDispDnId(static_cast<WORD>(g_Cfg.ResourceGetIndexType(RES_CHARDEF, s.GetArgStr())));
			break;
		case CBC_SOUND:
			m_soundBase = static_cast<SOUND_TYPE>(s.GetArgVal());
			break;
		case CBC_SOUNDDIE:
			m_soundDie = static_cast<SOUND_TYPE>(s.GetArgVal());
			break;
		case CBC_SOUNDGETHIT:
			m_soundGetHit = static_cast<SOUND_TYPE>(s.GetArgVal());
			break;
		case CBC_SOUNDHIT:
			m_soundHit = static_cast<SOUND_TYPE>(s.GetArgVal());
			break;
		case CBC_SOUNDIDLE:
			m_soundIdle = static_cast<SOUND_TYPE>(s.GetArgVal());
			break;
		case CBC_SOUNDNOTICE:
			m_soundNotice = static_cast<SOUND_TYPE>(s.GetArgVal());
			break;
		case CBC_STR:
			m_Str = static_cast<int>(s.GetArgVal());
			break;
		case CBC_TSPEECH:
			return( m_Speech.r_LoadVal( s, RES_SPEECH ));
		default:
			return( CBaseBaseDef::r_LoadVal( s ));
	}
	return true;
	EXC_CATCH;

	EXC_DEBUG_START;
	EXC_ADD_SCRIPT;
	EXC_DEBUG_END;
	return false;
}

bool CCharBase::r_Load( CScript & s )
{
	ADDTOCALLSTACK("CCharBase::r_Load");
	// Do a prelim read from the script file.
	CScriptObj::r_Load(s);

	if ( m_sName.IsEmpty() )
	{
		g_Log.EventError("Char script '%s' has no name!\n", GetResourceName());
		return false;
	}

	if ( !IsValidDispID(GetDispID()) )
	{
 		g_Log.Event(LOGL_WARN, "Char script '%s' has bad DISPID 0%x. Defaulting to 0%x.\n", GetResourceName(), GetDispID(), static_cast<int>(CREID_MAN));
		m_dwDispIndex = CREID_MAN;
	}
	if ( m_Can == CAN_C_INDOORS )
		g_Log.Event(LOGL_WARN, "Char script '%s' has no CAN flags specified!\n", GetResourceName());

	return true;
}

////////////////////////////////////////////

// find it (or near it) if already loaded.
CCharBase * CCharBase::FindCharBase( CREID_TYPE baseID ) // static
{
	ADDTOCALLSTACK("CCharBase::FindCharBase");
	RESOURCE_ID rid = RESOURCE_ID(RES_CHARDEF, baseID);
	size_t index = g_Cfg.m_ResHash.FindKey(rid);
	if ( index == g_Cfg.m_ResHash.BadIndex() )
		return NULL;

	CResourceLink *pBaseLink = static_cast<CResourceLink *>(g_Cfg.m_ResHash.GetAt(rid, index));
	ASSERT(pBaseLink);
	CCharBase *pBase = dynamic_cast<CCharBase *>(pBaseLink);
	if ( pBase )
		return pBase;	// already loaded

	// create a new base.
	pBase = new CCharBase(baseID);
	ASSERT(pBase);
	pBase->CResourceLink::CopyTransfer(pBaseLink);

	// replace existing one
	g_Cfg.m_ResHash.SetAt(rid, index, pBase);

	// load it's data on demand.
	CResourceLock s;
	if ( !pBase->ResourceLock(s) || !pBase->r_Load(s) )
		return NULL;

	return pBase;
}
