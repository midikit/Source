//
// CClientEvent.cpp
// Copyright Menace Software (www.menasoft.com).
//
#include "graysvr.h"	// predef header.
#include "CClient.h"
#include "../network/network.h"
#include "../network/send.h"
#include "../network/receive.h"
#include <wchar.h>

/////////////////////////////////
// Events from the Client.

LPCTSTR const CClient::sm_szCmd_Redirect[12] =
{
	"BANK",
	"CONTROL",
	"DUPE",
	"FORGIVE",
	"JAIL",
	"KICK",
	"KILL",
	"NUDGEDOWN",
	"NUDGEUP",
	"PRIVSET",
	"REMOVE",
	"SHRINK",
};

void CClient::Event_ChatButton(const NCHAR * pszName) // Client's chat button was pressed
{
	ADDTOCALLSTACK("CClient::Event_ChatButton");
	// See if they've made a chatname yet
	// m_ChatPersona.SetClient(this);

	if (m_pChar->OnTrigger(CTRIG_UserChatButton, m_pChar) == TRIGRET_RET_TRUE)
		return;

	ASSERT(GetAccount());

	if ( GetAccount()->m_sChatName.IsEmpty())
	{
		// No chatname yet, see if the client sent one
		if (pszName[0] == 0) // No name was sent, so ask for a permanent chat system nickname (account based)
		{
			addChatSystemMessage(CHATMSG_GetChatName);
			return;
		}
		// OK, we got a nick, now store it with the account stuff.

		// Make it non unicode
		TCHAR szChatName[ MAX_NAME_SIZE * 2 + 2 ];
		CvtNUNICODEToSystem( szChatName, sizeof(szChatName), pszName, 128 );

		if ( ! CChat::IsValidName(szChatName, true) ||
			g_Accounts.Account_FindChat(szChatName)) // Check for legal name, duplicates, etc
		{
			addChatSystemMessage(CHATMSG_Error);
			addChatSystemMessage(CHATMSG_GetChatName);
			return;
		}
		GetAccount()->m_sChatName = szChatName;
	}

	// Ok, below here we have a chat system nickname
	// Tell the chat system it has a new client using it
	SetChatActive();
}

void CClient::Event_ChatText( const NCHAR * pszText, int len, CLanguageID lang ) // Text from a client
{
	ADDTOCALLSTACK("CClient::Event_ChatText");
	// Just send it all to the chat system
	g_Serv.m_Chats.EventMsg( this, pszText, len, lang );
}

void CClient::Event_Item_Dye( CGrayUID uid, HUE_TYPE wHue ) // Rehue an item
{
	ADDTOCALLSTACK("CClient::Event_Item_Dye");
	// CLIMODE_DYE : Result from addDyeOption()
	CObjBase	*pObj = uid.ObjFind();
	CItem	*pItem;

	if ( !m_pChar->CanTouch(pObj) )
	{
		SysMessage("You can't reach it");
		return;
	}
	if ( GetTargMode() != CLIMODE_DYE )
		return;

	ClearTargMode();

	if ( !IsPriv(PRIV_GM) )
	{
		if ( !pObj->IsChar() )
		{
			pItem = (CItem *) pObj;
			if ( ( pObj->GetBaseID() != 0xFAB ) && (!pItem->IsType(IT_DYE_VAT) || !IsSetOF(OF_DyeType)) )
				return;

			if ( wHue < HUE_BLUE_LOW )
				wHue = HUE_BLUE_LOW;
			if ( wHue > HUE_DYE_HIGH )
				wHue = HUE_DYE_HIGH;
		} else
			return;
	}
	else if ( pObj->IsChar() )
	{
		pObj->RemoveFromView();
		wHue |= HUE_UNDERWEAR;
	}

	pObj->SetHue(wHue);
	pObj->Update();
}


void CClient::Event_Tips( WORD i) // Tip of the day window
{
	ADDTOCALLSTACK("CClient::Event_Tips");
	if (i==0)
		i=1;
	CResourceLock s;
	if ( g_Cfg.ResourceLock( s, RESOURCE_ID( RES_TIP, i )))
	{
		addScrollScript( s, SCROLL_TYPE_TIPS, i );
	}
}



void CClient::Event_Book_Title( CGrayUID uid, LPCTSTR pszTitle, LPCTSTR pszAuthor )
{
	ADDTOCALLSTACK("CClient::Event_Book_Title");
	// XCMD_BookOpen : user is changing the books title/author info.

	CItemMessage * pBook = dynamic_cast <CItemMessage *> (uid.ItemFind());
	if ( !m_pChar->CanTouch(pBook) )
	{
		SysMessage("you can't reach it");
		return;
	}
	if ( !pBook->IsBookWritable() )
		return;

	if ( Str_Check(pszTitle) || Str_Check(pszAuthor) )
		return;

	pBook->SetName(pszTitle);
	pBook->m_sAuthor = pszAuthor;
}

void CClient::Event_Item_Pickup(CGrayUID uid, int amount) // Client grabs an item
{
	ADDTOCALLSTACK("CClient::Event_Item_Pickup");
	EXC_TRY("CClient::Event_Item_Pickup");
	// Player/client is picking up an item.

	EXC_SET("Item");
	CItem	*pItem = uid.ItemFind();
	if ( !pItem || pItem->IsWeird() )
	{
		EXC_SET("Item - addObjectRemove(uid)");
		addObjectRemove(uid);
		EXC_SET("Item - addItemDragCancel(0)");
		PacketDragCancel* cmd = new PacketDragCancel(this, PacketDragCancel::CannotLift);
		return;
	}

	EXC_SET("FastLoot");
	//	fastloot (,emptycontainer) protection
	if ( m_tNextPickup > m_tNextPickup.GetCurrentTime() )
	{
		EXC_SET("FastLoot - addItemDragCancel(0)");
		PacketDragCancel* cmd = new PacketDragCancel(this, PacketDragCancel::CannotLift);
		return;
	}
	m_tNextPickup = m_tNextPickup.GetCurrentTime() + 3;

	EXC_SET("origin");
	// Where is the item coming from ? (just in case we have to toss it back)
	CObjBase * pObjParent = dynamic_cast <CObjBase *>(pItem->GetParent());
	m_Targ_PrvUID = ( pObjParent ) ? (DWORD) pObjParent->GetUID() : UID_CLEAR;
	m_Targ_p = pItem->GetUnkPoint();

	EXC_SET("ItemPickup");
	amount = m_pChar->ItemPickup(pItem, amount);
	if ( amount < 0 )
	{
		EXC_SET("ItemPickup - addItemDragCancel(0)");
		PacketDragCancel* cmd = new PacketDragCancel(this, PacketDragCancel::CannotLift);
		return;
	}
	else if ( amount > 1 )
		m_tNextPickup = m_tNextPickup + 2;	// +100 msec if amount should slow down the client

	EXC_SET("TargMode");
	SetTargMode(CLIMODE_DRAG);
	m_Targ_UID = uid;
	EXC_CATCH;
}

void inline CClient::Event_Item_Drop_Fail( CItem * pItem )
{
	ADDTOCALLSTACK("CClient::Event_Item_Drop_Fail");
	// The item was in the LAYER_DRAGGING.
	// Try to bounce it back to where it came from.
	if ( pItem == NULL )
		return;

	// Game pieces should be returned to their game boards.
	if ( pItem->IsType(IT_GAME_PIECE) )
	{
		CItemContainer *pGame = dynamic_cast<CItemContainer *>( m_Targ_PrvUID.ItemFind() );
		if ( pGame != NULL )
			pGame->ContentAdd( pItem, m_Targ_p );
		else
			pItem->Delete();

		return;
	}

	if ( pItem == m_pChar->LayerFind( LAYER_DRAGGING ) )	// if still being dragged
		m_pChar->ItemBounce( pItem );
}

void CClient::Event_Item_Drop( CGrayUID uidItem, CPointMap pt, CGrayUID uidOn, unsigned char gridIndex )
{
	ADDTOCALLSTACK("CClient::Event_Item_Drop");
	// This started from the Event_Item_Pickup()
	if ( !m_pChar )
		return;

	CItem * pItem = uidItem.ItemFind();
	CObjBase * pObjOn = uidOn.ObjFind();

	// Are we out of sync ?
	if ( pItem == NULL ||
		pItem == pObjOn ||	// silliness.
		GetTargMode() != CLIMODE_DRAG ||
		pItem != m_pChar->LayerFind( LAYER_DRAGGING ))
	{
		PacketDragCancel* cmd = new PacketDragCancel(this, PacketDragCancel::Other);
		return;
	}

	ClearTargMode();	// done dragging

	if ( pObjOn != NULL )	// Put on or in another object
	{
		if ( ! m_pChar->CanTouch( pObjOn ))	// Must also be LOS !
		{
			Event_Item_Drop_Fail( pItem );
			return;
		}

		if ( pObjOn->IsChar())	// Drop on a chars head.
		{
			CChar * pChar = dynamic_cast <CChar*>( pObjOn );
			if ( pChar != m_pChar )
			{
				if ( ! Cmd_SecureTrade( pChar, pItem ))
					Event_Item_Drop_Fail( pItem );
				return;
			}

			// dropped on myself. Get my Pack.
			pObjOn = m_pChar->GetPackSafe();
		}

		// On a container item ?
		CItemContainer * pContItem = dynamic_cast <CItemContainer *>( pObjOn );

		// Is the object on a person ? check the weight.
		CObjBaseTemplate * pObjTop = pObjOn->GetTopLevelObj();
		if ( pObjTop->IsChar())
		{
			CChar * pChar = dynamic_cast <CChar*>( pObjTop );
			ASSERT(pChar);
			if ( ! pChar->NPC_IsOwnedBy( m_pChar ))
			{
				// Slyly dropping item in someone elses pack.
				// or just dropping on their trade window.
				if ( ! Cmd_SecureTrade( pChar, pItem ))
					Event_Item_Drop_Fail( pItem );
				return;
			}
			if ( ! pChar->m_pPlayer )
			{
				// newbie items lose newbie status when transfered to NPC
				pItem->ClrAttr(ATTR_NEWBIE|ATTR_OWNED);
			}
			if ( pChar->GetBank()->IsItemInside( pContItem ))
			{
				// Diff Weight restrict for bank box and items in the bank box.
				if ( ! pChar->GetBank()->CanContainerHold( pItem, m_pChar ))
				{
					Event_Item_Drop_Fail( pItem );
					return;
				}
			}
			else if ( ! pChar->CanCarry( pItem ))
			{
				// SysMessage( "That is too heavy" );
				Event_Item_Drop_Fail( pItem );
				return;
			}
		}
		if ( pContItem != NULL )
		{
			//	bug with shifting selling list by gold coins
			if ( pContItem->IsType(IT_EQ_VENDOR_BOX) &&
				( pItem->IsType(IT_GOLD) || pItem->IsType(IT_COIN) ))
			{
				Event_Item_Drop_Fail( pItem );
				return;
			}
		}

		CObjBase *pOldCont = pItem->GetContainer();
		CScriptTriggerArgs Args( pObjOn );
		if ( pItem->OnTrigger( ITRIG_DROPON_ITEM, m_pChar, &Args ) == TRIGRET_RET_TRUE )
		{
			Event_Item_Drop_Fail( pItem );
			return;
		}

		if ( pOldCont != pItem->GetContainer() )
			return;

		CItem * pItemOn = dynamic_cast <CItem*> ( pObjOn );
		if ( pItemOn )
		{
			CScriptTriggerArgs Args( pItem );
			if ( pItemOn->OnTrigger( ITRIG_DROPON_SELF, m_pChar, &Args ) == TRIGRET_RET_TRUE )
			{
				Event_Item_Drop_Fail( pItem );
				return;
			}
		}

		if ( pContItem != NULL )
		{
			// pChar->GetBank()->IsItemInside( pContItem )
			bool isCheating = false;
			bool isBank = pContItem->IsType( IT_EQ_BANK_BOX );

			if ( isBank )
				isCheating = isBank &&
						pContItem->m_itEqBankBox.m_pntOpen != m_pChar->GetTopPoint();
			else
				isCheating = m_pChar->GetBank()->IsItemInside( pContItem ) &&
						m_pChar->GetBank()->m_itEqBankBox.m_pntOpen != m_pChar->GetTopPoint();

//			g_Log.Event( LOGL_WARN, "%x:IsBank '%d', IsItemInside '%d'\n", m_Socket.GetSocket(), isBank, isBank ? -1 : m_pChar->GetBank()->IsItemInside( pContItem ) );

//			if ( pContItem->IsType( IT_EQ_BANK_BOX ) && pContItem->m_itEqBankBox.m_pntOpen != m_pChar->GetTopPoint() )

			if ( isCheating )
			{
				Event_Item_Drop_Fail( pItem );
				return;
			}
			if ( !pContItem->CanContainerHold(pItem, m_pChar) )
			{
				Event_Item_Drop_Fail( pItem );
				return;
			}

			// only IT_GAME_PIECE can be dropped on IT_GAME_BOARD or clients will crash
			if (pContItem->IsType( IT_GAME_BOARD ) && !pItem->IsType( IT_GAME_PIECE ))
			{
				Event_Item_Drop_Fail( pItem );
				return;
			}

			// non-vendable items should never be dropped inside IT_EQ_VENDOR_BOX
			if ( pContItem->IsType( IT_EQ_VENDOR_BOX ) &&  !pItem->Item_GetDef()->GetMakeValue(0) )
			{
				SysMessageDefault( DEFMSG_ERR_NOT4SALE );
				Event_Item_Drop_Fail( pItem );
				return;
			}
		}
		else
		{
			// dropped on top of a non container item.
			// can i pile them ?
			// Still in same container.

			ASSERT(pItemOn);
			pObjOn = pItemOn->GetContainer();
			pt = pItemOn->GetUnkPoint();

			if ( ! pItem->Stack( pItemOn ))
			{
				if ( pItemOn->IsTypeSpellbook() )
				{
					if ( pItemOn->AddSpellbookScroll( pItem ))
					{
						SysMessage( g_Cfg.GetDefaultMsg( DEFMSG_CANT_ADD_SPELLBOOK ) );
						Event_Item_Drop_Fail( pItem );
						return;
					}
					// We only need to add a sound here if there is no
					// scroll left to bounce back.
					if (pItem->IsDeleted())
						addSound( 0x057, pItemOn );	// add to inv sound.
					Event_Item_Drop_Fail( pItem );
					return; // We can't drop any remaining scrolls
				}

				// Just drop on top of the current item.
				// Client probably doesn't allow this anyhow.
			}
		}
	}
	else
	{
		if ( ! m_pChar->CanTouch( pt ))	// Must also be LOS !
		{
			Event_Item_Drop_Fail( pItem );
			return;
		}
	}

	// Game pieces can only be dropped on their game boards.
	if ( pItem->IsType(IT_GAME_PIECE))
	{
		if ( pObjOn == NULL || m_Targ_PrvUID != pObjOn->GetUID())
		{
			CItemContainer * pGame = dynamic_cast <CItemContainer *>( m_Targ_PrvUID.ItemFind());
			if ( pGame != NULL )
			{
				pGame->ContentAdd( pItem, m_Targ_p );
			}
			else
				pItem->Delete();	// Not sure what else to do with it.
			return;
		}
	}

	// do the dragging anim for everyone else to see.

	if ( pObjOn != NULL )
	{
		// in pack or other CItemContainer.
		m_pChar->UpdateDrag( pItem, pObjOn );
		CItemContainer * pContOn = dynamic_cast <CItemContainer *>(pObjOn);
		ASSERT(pContOn);

		if ( !pContOn )
		{
			if ( pObjOn->IsChar() )
			{
				CChar* pChar = dynamic_cast <CChar*>(pObjOn);
				
				if ( pChar )
					pContOn = pChar->GetBank( LAYER_PACK );
			}

			if ( !pContOn )
			{
				// on ground
				m_pChar->UpdateDrag( pItem, NULL, &pt );
				m_pChar->ItemDrop( pItem, pt );

				return;
			}
		}

		pContOn->ContentAdd( pItem, pt, gridIndex );
		addSound( pItem->GetDropSound( pObjOn ));
	}
	else
	{
		// on ground
		m_pChar->UpdateDrag( pItem, NULL, &pt );
		m_pChar->ItemDrop( pItem, pt );
	}
}



void CClient::Event_Skill_Use( SKILL_TYPE skill ) // Skill is clicked on the skill list
{
	ADDTOCALLSTACK("CClient::Event_Skill_Use");
	// All the push button skills come through here.
	// Any "Last skill" macro comes here as well. (push button only)
	if ( !g_Cfg.m_SkillIndexDefs.IsValidIndex(skill) )
	{
		SysMessage( "There is no such skill. Please tell support you saw this message.");
		return;
	}

	bool fContinue = false;

	if ( m_pChar->Skill_Wait(skill) )
		return;

	if ( !IsSetEF(EF_Minimize_Triggers) )
	{
		if ( m_pChar->Skill_OnTrigger( skill, SKTRIG_SELECT ) == TRIGRET_RET_TRUE )
		{
			m_pChar->Skill_Fail( true );	// clean up current skill.
			return;
		}
	}

	SetTargMode();
	m_Targ_UID.InitUID();	// This is a start point for targ more.

	bool fCheckCrime	= false;
	bool fDoTargeting	= false;

	if ( g_Cfg.IsSkillFlag( skill, SKF_SCRIPTED ) )
	{
		if ( !g_Cfg.GetSkillDef(skill)->m_sTargetPrompt.IsEmpty() )
		{
			m_tmSkillTarg.m_Skill = skill;	// targetting what skill ?
			addTarget( CLIMODE_TARG_SKILL, g_Cfg.GetSkillDef(skill)->m_sTargetPrompt, false, fCheckCrime );
			return;
		}
		else
			m_pChar->Skill_Start( skill );
	}
	else switch ( skill )
	{
		case SKILL_ARMSLORE:
		case SKILL_ITEMID:
		case SKILL_ANATOMY:
		case SKILL_ANIMALLORE:
		case SKILL_EVALINT:
		case SKILL_FORENSICS:
		case SKILL_TASTEID:

		case SKILL_BEGGING:
		case SKILL_TAMING:
		case SKILL_REMOVETRAP:
			fCheckCrime = false;
			fDoTargeting = true;
			break;

		case SKILL_STEALING:
		case SKILL_ENTICEMENT:
		case SKILL_PROVOCATION:
		case SKILL_POISONING:
			// Go into targtting mode.
			fCheckCrime = true;
			fDoTargeting = true;
			break;

		case SKILL_STEALTH:	// How is this supposed to work.
		case SKILL_HIDING:
		case SKILL_SPIRITSPEAK:
		case SKILL_PEACEMAKING:
		case SKILL_DETECTINGHIDDEN:
		case SKILL_MEDITATION:
			// These start/stop automatically.
			m_pChar->Skill_Start(skill);
			return;

		case SKILL_TRACKING:
			Cmd_Skill_Tracking( -1, false );
			break;

		case SKILL_CARTOGRAPHY:
			// Menu select for map type.
			Cmd_Skill_Cartography( 0 );
			break;

		case SKILL_INSCRIPTION:
			// Menu select for spell type.
			Cmd_Skill_Inscription();
			break;

		default:
			SysMessage( "There is no such skill. Please tell support you saw this message.");
			break;
	}
	if ( fDoTargeting )
	{
		// Go into targtting mode.
		if ( g_Cfg.GetSkillDef(skill)->m_sTargetPrompt.IsEmpty() )
		{
			DEBUG_ERR(( "%x: Event_Skill_Use bad skill %d\n", GetSocketID(), skill ));
			return;
		}

		m_tmSkillTarg.m_Skill = skill;	// targetting what skill ?
		addTarget( CLIMODE_TARG_SKILL, g_Cfg.GetSkillDef(skill)->m_sTargetPrompt, false, fCheckCrime );
		return;
	}
}



bool CClient::Event_WalkingCheck(DWORD dwEcho)
{
	ADDTOCALLSTACK("CClient::Event_WalkingCheck");
	// look for the walk code, and remove it
	// Client will send 0's if we have not given it any EXTDATA_WalkCode_Prime message.
	// The client will use codes in order.
	// But it will skip codes sometimes. so delete codes that get skipped.

	// RETURN:
	//  true = ok to walk.
	//  false = the client is cheating. I did not send that code.

	if ( ! ( g_Cfg.m_wDebugFlags & DEBUGF_WALKCODES ))
		return( true );

	// If the LIFO stack has not been sent, send them out now
	if ( m_Walk_CodeQty < 0 )
	{
		addWalkCode(EXTDATA_WalkCode_Prime,COUNTOF(m_Walk_LIFO));
	}

	// Keep track of echo'd 0's and invalid non 0's
	// (you can get 1 to 4 of these legitimately when you first start walking)
	ASSERT( m_Walk_CodeQty >= 0 );

	for ( int i=0; i<m_Walk_CodeQty; i++ )
	{
		if ( m_Walk_LIFO[i] == dwEcho )	// found a good code.
		{
			// Move next to the head.
			i++;
			memmove( m_Walk_LIFO, m_Walk_LIFO+i, (m_Walk_CodeQty-i)*sizeof(DWORD));
			m_Walk_CodeQty -= i;
			// Set this to negative so we know later if we've gotten at least 1 valid echo
			m_Walk_InvalidEchos = -1;
			return( true );
		}
	}

	if ( m_Walk_InvalidEchos < 0 )
	{
		// If we're here, we got at least one valid echo....therefore
		// we should never get another one
		DEBUG_ERR(( "%x: Invalid walk echo (0%x). Invalid after valid.\n", GetSocketID(), dwEcho ));
		return false;
	}

	// Increment # of invalids received. This is allowed at startup.
	// These "should" always be 0's
	if ( ++ m_Walk_InvalidEchos >= COUNTOF(m_Walk_LIFO))
	{
		// The most I ever got was 2 of these, but I've seen 4
		// a couple of times on a real server...we might have to
		// increase this # if it becomes a problem (but I doubt that)
		DEBUG_ERR(( "%x: Invalid walk echo. Too many initial invalid.\n", GetSocketID()));
		return false;
	}

	// Allow them to walk a bit till we catch up.
	return true;
}



bool CClient::Event_Walking( BYTE rawdir ) // Player moves
{
	ADDTOCALLSTACK("CClient::Event_Walking");
	// XCMD_Walk
	// The theory....
	// The client sometimes echos 1 or 2 zeros or invalid echos when you first start
	//	walking (the invalid non zeros happen when you log off and don't exit the
	//	client.exe all the way and then log back in, XXX doesn't clear the stack)

	// Movement whilst freeze-on-cast enabled is not allowed
	if ( IsSetMagicFlags( MAGICF_FREEZEONCAST ) && CChar::IsSkillMagic(m_pChar->m_Act_SkillCurrent) )
	{
		const CSpellDef* pSpellDef = g_Cfg.GetSpellDef(m_pChar->m_atMagery.m_Spell);
		if (pSpellDef != NULL && !pSpellDef->IsSpellType(SPELLFLAG_NOFREEZEONCAST))
		{
			SysMessage( g_Cfg.GetDefaultMsg( DEFMSG_FROZEN ) );
			return false;
		}
	}

	if (( m_pChar->IsStatFlag(STATF_Freeze|STATF_Stone) && m_pChar->OnFreezeCheck() ) || m_pChar->OnFreezeCheck(true) )
	{
		return false;
	}

	m_timeLastEventWalk = CServTime::GetCurrentTime();

	bool fRun = ( rawdir & 0x80 ); // or flying ?

	m_pChar->StatFlag_Mod( STATF_Fly, fRun );

	DIR_TYPE dir = (DIR_TYPE)( rawdir & 0x0F );
	if ( dir >= DIR_QTY )
	{
		return false;
	}

	CPointMap pt = m_pChar->GetTopPoint();
	CPointMap ptold = pt;
	bool	fMove = true;
	bool	fUpdate	= false;

	if ( dir == m_pChar->m_dirFace )
	{
		LONGLONG	CurrTime	= GetTickCount();
		m_iWalkStepCount++;
		// Move in this dir.
		if ( ( m_iWalkStepCount % 7 ) == 0 )	// we have taken 8 steps ? direction changes don't count. (why we do this check also for gm?)
		{
			// Client only allows 4 steps of walk ahead.
			if ( g_Cfg.m_iWalkBuffer )
			{
				int		iTimeDiff	= ((CurrTime - m_timeWalkStep)/10);
				int		iTimeMin;
				if (m_pChar->m_pPlayer)
				{
					switch (m_pChar->m_pPlayer->m_speedMode)
					{
						case 0: // Normal Speed
							iTimeMin = m_pChar->IsStatFlag( STATF_OnHorse|STATF_Hovering )? 70 : 140; // it should check of walking (80 - 160)
							break;
						case 1: // Foot=Double Speed, Mount=Normal
							iTimeMin = 70;
							break;
						case 2: // Foot=Always Walk, Mount=Always Walk (Half Speed)
							iTimeMin = m_pChar->IsStatFlag( STATF_OnHorse|STATF_Hovering )? 140 : 280;
							break;
						case 3: // Foot=Always Run, Mount=Always Walk
						default:
							iTimeMin = 140;
							break;
					}
				}
				else
					iTimeMin = m_pChar->IsStatFlag( STATF_OnHorse|STATF_Hovering ) ? 70 : 140;

				if ( iTimeDiff > iTimeMin )
				{
					int	iRegen	= ((iTimeDiff - iTimeMin) * g_Cfg.m_iWalkRegen) / 150;
					if ( iRegen > g_Cfg.m_iWalkBuffer )
						iRegen	= g_Cfg.m_iWalkBuffer;
					else if ( iRegen < -((g_Cfg.m_iWalkBuffer * g_Cfg.m_iWalkRegen) / 100) )
						iRegen	= -((g_Cfg.m_iWalkBuffer * g_Cfg.m_iWalkRegen) / 100);
					iTimeDiff	= iTimeMin + iRegen;
				}

				m_iWalkTimeAvg		+= iTimeDiff;

				int	oldAvg	= m_iWalkTimeAvg;
				m_iWalkTimeAvg	-= iTimeMin;

				if ( m_iWalkTimeAvg > g_Cfg.m_iWalkBuffer )
					m_iWalkTimeAvg	= g_Cfg.m_iWalkBuffer;
				else if ( m_iWalkTimeAvg < -g_Cfg.m_iWalkBuffer )
					m_iWalkTimeAvg	= -g_Cfg.m_iWalkBuffer;

				if ( IsPriv( PRIV_DETAIL ) && IsPriv( PRIV_DEBUG ) )
				{
					SysMessagef( "Walkcheck trace: %i / %i (%i) :: %i", iTimeDiff, iTimeMin, oldAvg, m_iWalkTimeAvg );
				}

				if ( m_iWalkTimeAvg < 0 && iTimeDiff >= 0 && ! IsPriv(PRIV_GM) )	// TICK_PER_SEC
				{
					// walking too fast.
					DEBUG_WARN(("%s (%x): Fast Walk ?\n", GetName(), GetSocketID()));

					TRIGRET_TYPE iAction = TRIGRET_RET_DEFAULT;
					if ( !IsSetEF(EF_Minimize_Triggers) )
					{
						iAction = m_pChar->OnTrigger( CTRIG_UserExWalkLimit, m_pChar, NULL );
					}
					m_iWalkStepCount--; // eval again next time !

					if ( iAction != TRIGRET_RET_TRUE )
					{
						return false;
					}
				}
			}
			m_timeWalkStep = CurrTime;
		}	// nth step

		pt.Move(dir);

		// Before moving, check if we were indoors
		bool fRoof = m_pChar->IsStatFlag( STATF_InDoors );

		// Check the z height here.
		// The client already knows this but doesn't tell us.
#ifdef _DIAGONALWALKCHECK_PLAYERWALKONLY
		if ( !m_pChar->CanMoveWalkTo(pt, true, false, dir, false, true) )
#else
		if ( !m_pChar->CanMoveWalkTo(pt, true, false, dir) )
#endif
		{
			return false;
		}

		// Are we invis ?
		m_pChar->CheckRevealOnMove();

		if (!m_pChar->MoveToChar( pt ))
		{
			return false;
		}

		// Now we've moved, are we now or no longer indoors and need to update weather?
		if ( fRoof != m_pChar->IsStatFlag( STATF_InDoors ))
		{
			addWeather( WEATHER_DEFAULT );
		}

		// did i step on a telepad, trap, etc ?
		if ( m_pChar->CheckLocation())
		{
			// We stepped on teleporter
			return false;
		}
	}

	if ( dir != m_pChar->m_dirFace )		// Just a change in dir.
	{
		m_pChar->m_dirFace = dir;
		fMove = false;
	}

	if ( !fMove )
		m_pChar->UpdateMode(this);			// Show others I have turned !!
	else
	{
		m_pChar->UpdateMove( ptold, this );	// Who now sees me ?
		addPlayerSee( ptold );				// What new stuff do I now see ?
	}

	return true;
}



void CClient::Event_CombatMode( bool fWar ) // Only for switching to combat mode
{
	ADDTOCALLSTACK("CClient::Event_CombatMode");
	// If peacmaking then this doens't work ??
	// Say "you are feeling too peacefull"
	if ( !IsSetEF(EF_Minimize_Triggers) )
	{
		CScriptTriggerArgs Args;
		Args.m_iN1 = m_pChar->IsStatFlag(STATF_War) ? 1 : 0;
		if (m_pChar->OnTrigger(CTRIG_UserWarmode, m_pChar, &Args) == TRIGRET_RET_TRUE)
			return;
	}

	m_pChar->StatFlag_Mod( STATF_War, fWar );

	if ( m_pChar->IsStatFlag( STATF_DEAD ))
	{
		// Manifest the ghost.
		// War mode for ghosts.
		m_pChar->StatFlag_Mod( STATF_Insubstantial, ! fWar );
	}

	m_pChar->Skill_Fail( true );	// clean up current skill.
	if ( ! fWar )
	{
		m_pChar->Fight_ClearAll();
	}

	addPlayerWarMode();
	m_pChar->UpdateMode( this, m_pChar->IsStatFlag( STATF_DEAD ));
}

bool CClient::Event_Command(LPCTSTR pszCommand, TALKMODE_TYPE mode)
{
	ADDTOCALLSTACK("CClient::Event_Command");
	if ( mode == 13 || mode == 14 ) // guild and alliance don't pass this.
		return false;
	if ( pszCommand[0] == 0 )
		return true;		// should not be said
	if ( Str_Check(pszCommand) )
		return true;		// should not be said
	if ( ( ( m_pChar->GetID() == 0x3db ) && ( pszCommand[0] == '=' ) ) || ( pszCommand[0] == g_Cfg.m_cCommandPrefix ) )
	{
		// Lazy :P
	}
	else
		return false;

	if ( !strcmpi(pszCommand, "q") && ( GetPrivLevel() > PLEVEL_Player ))
	{
		SysMessage("Probably you forgot about Ctrl?");
		return true;
	}

	bool m_bAllowCommand = true;
	bool m_bAllowSay = true;

	pszCommand += 1;
	GETNONWHITESPACE(pszCommand);
	m_bAllowCommand = g_Cfg.CanUsePrivVerb(this, pszCommand, this);

	if ( !m_bAllowCommand )
		m_bAllowSay = ( GetPrivLevel() <= PLEVEL_Player );

	//	filter on commands is active - so trigger it
	if ( !g_Cfg.m_sCommandTrigger.IsEmpty() )
	{
		CScriptTriggerArgs Args(pszCommand);
		Args.m_iN1 = m_bAllowCommand;
		Args.m_iN2 = m_bAllowSay;
		enum TRIGRET_TYPE tr;

		//	Call the filtering function
		if ( m_pChar->r_Call(g_Cfg.m_sCommandTrigger, m_pChar, &Args, NULL, &tr) )
			if ( tr == TRIGRET_RET_TRUE )
				return Args.m_iN2;

		m_bAllowCommand = Args.m_iN1;
		m_bAllowSay = Args.m_iN2;
	}

	if ( !m_bAllowCommand && !m_bAllowSay )
		SysMessage("You can't use this command.");

	if ( m_bAllowCommand )
	{
		m_bAllowSay = false;

		// Assume you don't mean yourself !
		if ( FindTableHeadSorted( pszCommand, sm_szCmd_Redirect, COUNTOF(sm_szCmd_Redirect)) >= 0 )
		{
			// targetted verbs are logged once the target is selected.
			addTargetVerb(pszCommand, "");
		}
		else
		{
			CScript s(pszCommand);
			if ( !m_pChar->r_Verb(s, m_pChar) )
				SysMessageDefault(DEFMSG_CMD_INVALID);
		}
	}

	if ( GetPrivLevel() >= g_Cfg.m_iCommandLog )
		g_Log.Event( LOGM_GM_CMDS, "%x:'%s' commands '%s'=%d\n", GetSocketID(), (LPCTSTR) GetName(), (LPCTSTR) pszCommand, m_bAllowCommand);

	return !m_bAllowSay;
}

void CClient::Event_Attack( CGrayUID uid )
{
	ADDTOCALLSTACK("CClient::Event_Attack");
	// d-click in war mode
	// I am attacking someone.

	CChar * pChar = uid.CharFind();
	if ( pChar == NULL )
		return;

	PacketAttack* cmd = new PacketAttack(this, (m_pChar->Fight_Attack(pChar) ? (DWORD)pChar->GetUID() : 0));
}

// Client/Player buying items from the Vendor

inline void CClient::Event_VendorBuy_Cheater( int iCode )
{
	ADDTOCALLSTACK("CClient::Event_VendorBuy_Cheater");

	// iCode descriptions
	static LPCTSTR const sm_BuyPacketCheats[] =
	{
		"Other",
		"Bad vendor UID",
		"Vendor is off-duty",
		"Bad item UID",
		"Requested items out of stock",
		"Total cost is too great",
	};

	g_Log.Event(LOGL_WARN|LOGM_CHEAT, "%x:Cheater '%s' is submitting illegal buy packet (%s)\n", GetSocketID(),
		GetAccount()->GetName(),
		sm_BuyPacketCheats[iCode]);
	SysMessage("You cannot buy that.");
}

void CClient::Event_VendorBuy(CChar* pVendor, const VendorItem* items, DWORD itemCount)
{
	ADDTOCALLSTACK("CClient::Event_VendorBuy");
	if (m_pChar == NULL || pVendor == NULL || items == NULL || itemCount <= 0)
		return;

#define MAX_COST (INT_MAX / 2)
	bool bPlayerVendor = pVendor->IsStatFlag(STATF_Pet);
	CItemContainer* pStock = pVendor->GetBank(LAYER_VENDOR_STOCK);
	CItemContainer* pPack = m_pChar->GetPackSafe();

	CItemVendable* pItem;
	INT64 costtotal = 0;
	int i;

	//	Check if the vendor really has so much items
	for (i = 0; i < itemCount; ++i)
	{
		if ( !items[i].m_serial )
			continue;

		pItem = dynamic_cast <CItemVendable *> (items[i].m_serial.ItemFind());
		if ( pItem == NULL )
			continue;

		if ((items[i].m_amount <= 0) || (items[i].m_amount > pItem->GetAmount()))
		{
			pVendor->Speak("Alas, I don't have all those goods in stock. Let me know if there is something else thou wouldst buy.");
			Event_VendorBuy_Cheater( 0x4 );
			return;
		}

		costtotal += (items[i].m_amount * items[i].m_price);
		if ( costtotal > MAX_COST )
		{
			pVendor->Speak("Alas, I am not allowed to operate such huge sums.");
			Event_VendorBuy_Cheater( 0x5 );
			return;
		}
	}

	//	Check for gold being enough to buy this
	bool fBoss = pVendor->NPC_IsOwnedBy(m_pChar);
	if ( !fBoss )
	{
		if ( ( g_Cfg.m_fPayFromPackOnly ) ?
				m_pChar->GetPackSafe()->ContentConsume(RESOURCE_ID(RES_TYPEDEF,IT_GOLD), costtotal, true) :
				m_pChar->ContentConsume(RESOURCE_ID(RES_TYPEDEF,IT_GOLD), costtotal, true)
			)
		{
			pVendor->Speak("Alas, thou dost not possess sufficient gold for this purchase!");
			return;
		}
	}

	if ( costtotal <= 0 )
	{
		pVendor->Speak("You have bought nothing. But feel free to browse");
		return;
	}

	//	Move the items bought into your pack.
	for ( i = 0; i < itemCount; ++i )
	{
		if ( !items[i].m_serial )
			break;

		pItem = dynamic_cast <CItemVendable *> (items[i].m_serial.ItemFind());
		WORD amount = items[i].m_amount;

		if ( pItem == NULL )
			continue;

		if ( IsSetEF(EF_New_Triggers) )
		{
			CScriptTriggerArgs Args( amount, items[i].m_amount * items[i].m_price, pVendor );
			Args.m_VarsLocal.SetNum( "TOTALCOST", costtotal);
			if ( pItem->OnTrigger( ITRIG_Buy, this->GetChar(), &Args ) == TRIGRET_RET_TRUE )
				continue;
		}

		if ( !bPlayerVendor )									//	NPC vendors
		{
			pItem->SetAmount(pItem->GetAmount() - amount);

			switch ( pItem->GetType() )
			{
				case IT_FIGURINE:
					{
						for ( int f = 0; f < amount; f++ )
							m_pChar->Use_Figurine(pItem, 2);
					}
					goto do_consume;
				case IT_BEARD:
					if (( m_pChar->GetDispID() != CREID_MAN ) && ( m_pChar->GetDispID() != CREID_EQUIP_GM_ROBE ))
					{
						pVendor->Speak("Sorry, I cannot do anything for you.");
						continue;
					}
				case IT_HAIR:
					// Must be added directly. can't exist in pack!
					if ( ! m_pChar->IsHuman())
					{
						pVendor->Speak("Sorry, I cannot do anything for you.");
						continue;
					}
					{
						CItem * pItemNew = CItem::CreateDupeItem( pItem );
						m_pChar->LayerAdd(pItemNew);
						pItemNew->m_TagDefs.SetNum("NOSAVE", 0, true);
						pItemNew->SetTimeout( 55000*TICK_PER_SEC );	// set the grow timer.
						pVendor->UpdateAnimate(ANIM_ATTACK_1H_WIDE);
						m_pChar->Sound( SOUND_SNIP );	// snip noise.
					}
					continue;

				default:
					break;
			}

			if ( amount > 1 && !pItem->Item_GetDef()->IsStackableType() )
			{
				while ( amount -- )
				{
					CItem * pItemNew = CItem::CreateDupeItem(pItem);
					pItemNew->SetAmount(1);
					pItemNew->m_TagDefs.SetNum("NOSAVE", 0, true);
					if ( !pPack->CanContainerHold( pItemNew, m_pChar ) || !m_pChar->CanCarry( pItemNew ) )
						m_pChar->ItemDrop( pItemNew, m_pChar->GetTopPoint() );
					else
						pPack->ContentAdd( pItemNew );
				}
			}
			else
			{
				CItem * pItemNew = CItem::CreateDupeItem(pItem);
				pItemNew->SetAmount(amount);
				pItemNew->m_TagDefs.SetNum("NOSAVE", 0, true);
				if ( !pPack->CanContainerHold( pItemNew, m_pChar ) || !m_pChar->CanCarry( pItemNew ) )
					m_pChar->ItemDrop( pItemNew, m_pChar->GetTopPoint() );
				else
					pPack->ContentAdd( pItemNew );
			}
		}
		else													// Player vendors
		{
			if ( pItem->GetAmount() <= amount )		// buy the whole item
			{
				if ( !pPack->CanContainerHold( pItem, m_pChar ) || !m_pChar->CanCarry( pItem ) )
					m_pChar->ItemDrop( pItem, m_pChar->GetTopPoint() );
				else
					pPack->ContentAdd( pItem );

				pItem->m_TagDefs.SetNum("NOSAVE", 0, true);
			}
			else
			{
				pItem->SetAmount(pItem->GetAmount() - amount);

				CItem *pItemNew = CItem::CreateDupeItem(pItem);
				pItemNew->m_TagDefs.SetNum("NOSAVE", 0, true);
				pItemNew->SetAmount(amount);
				if ( !pPack->CanContainerHold( pItemNew, m_pChar ) || !m_pChar->CanCarry( pItemNew ) )
					m_pChar->ItemDrop( pItemNew, m_pChar->GetTopPoint() );
				else
					pPack->ContentAdd( pItemNew );
			}
		}

do_consume:
		pItem->Update();
	}

	//	Step #5
	//	Say the message about the bought goods
	TCHAR *sMsg = Str_GetTemp();
	TCHAR *pszTemp1 = Str_GetTemp();
	TCHAR *pszTemp2 = Str_GetTemp();
	sprintf(pszTemp1, g_Cfg.GetDefaultMsg(DEFMSG_NPC_VENDOR_HYARE), m_pChar->GetName());
	sprintf(pszTemp2, fBoss ? g_Cfg.GetDefaultMsg(DEFMSG_NPC_VENDOR_S1) : g_Cfg.GetDefaultMsg(DEFMSG_NPC_VENDOR_B1),
		costtotal, (costtotal==1) ? "" : g_Cfg.GetDefaultMsg(DEFMSG_NPC_VENDOR_CA));
	sprintf(sMsg, "%s %s %s", pszTemp1, pszTemp2, g_Cfg.GetDefaultMsg(DEFMSG_NPC_VENDOR_TY));
	pVendor->Speak(sMsg);

	//	Step #6
	//	Take the gold and add it to the vendor
	if ( !fBoss )
	{
		int rc = ( g_Cfg.m_fPayFromPackOnly ) ?
			m_pChar->GetPackSafe()->ContentConsume( RESOURCE_ID(RES_TYPEDEF,IT_GOLD), costtotal) :
			m_pChar->ContentConsume( RESOURCE_ID(RES_TYPEDEF,IT_GOLD), costtotal);
		pVendor->GetBank()->m_itEqBankBox.m_Check_Amount += costtotal;
	}

	//	Clear the vendor display.
	addVendorClose(pVendor);

	if ( i )	// if anything was sold, sound this
		addSound( 0x057 );
}

inline void CClient::Event_VendorSell_Cheater( int iCode )
{
	ADDTOCALLSTACK("CClient::Event_VendorSell_Cheater");

	// iCode descriptions
	static LPCTSTR const sm_SellPacketCheats[] =
	{
		"Other",
		"Bad vendor UID",
		"Vendor is off-duty",
		"Bad item UID",
	};

	g_Log.Event(LOGL_WARN|LOGM_CHEAT, "%x:Cheater '%s' is submitting illegal sell packet (%s)\n", GetSocketID(),
		GetAccount()->GetName(),
		sm_SellPacketCheats[iCode]);
	SysMessage("You cannot sell that.");
}

void CClient::Event_VendorSell(CChar* pVendor, const VendorItem* items, DWORD itemCount)
{
	ADDTOCALLSTACK("CClient::Event_VendorSell");
	// Player Selling items to the vendor.
	// Done with the selling action.
	if (m_pChar == NULL || pVendor == NULL || items == NULL || itemCount <= 0)
		return;

	CItemContainer	*pBank = pVendor->GetBank();
	CItemContainer	*pContStock = pVendor->GetBank( LAYER_VENDOR_STOCK );
	CItemContainer	*pContBuy = pVendor->GetBank( LAYER_VENDOR_BUYS );
	CItemContainer	*pContExtra = pVendor->GetBank( LAYER_VENDOR_EXTRA );
	if ( pBank == NULL || pContStock == NULL )
	{
		addVendorClose(pVendor);
		pVendor->Speak(g_Cfg.GetDefaultMsg(DEFMSG_NPC_VENDOR_GUARDS));
		return;
	}

	int iConvertFactor = -pVendor->NPC_GetVendorMarkup(m_pChar);

	int iGold = 0;
	bool fShortfall = false;

	for (int i = 0; i < itemCount; i++)
	{

		CItemVendable * pItem = dynamic_cast <CItemVendable *> (items[i].m_serial.ItemFind());
		if ( !pItem || !pItem->IsValidSaleItem(true) )
		{
			Event_VendorSell_Cheater( 0x3 );
			return;
		}

		// Do we still have it ? (cheat check)
		if ( pItem->GetTopLevelObj() != m_pChar )
			continue;

		if ( IsSetEF(EF_New_Triggers) )
		{
			CScriptTriggerArgs Args( pItem->GetAmount(), 0, pVendor );
			if ( pItem->OnTrigger( ITRIG_Sell, this->GetChar(), &Args ) == TRIGRET_RET_TRUE )
				continue;
		}

		// Find the valid sell item from vendors stuff.
		CItemVendable * pItemSell = CChar::NPC_FindVendableItem( pItem, pContBuy, pContStock );
		if ( pItemSell == NULL )
			continue;

		// Now how much did i say i wanted to sell ?
		int amount = items[i].m_amount;
		if ( pItem->GetAmount() < amount )	// Selling more than i have ?
		{
			amount = pItem->GetAmount();
		}

		INT64 iPrice = (INT64)pItemSell->GetVendorPrice(iConvertFactor) * amount;

		// Can vendor afford this ?
		if ( iPrice > pBank->m_itEqBankBox.m_Check_Amount )
		{
			fShortfall = true;
			break;
		}
		pBank->m_itEqBankBox.m_Check_Amount -= iPrice;

		// give them the appropriate amount of gold.
		iGold += iPrice;

		// Take the items from player.
		// Put items in vendor inventory.
		if ( amount >= pItem->GetAmount())
		{
			pItem->RemoveFromView();
			if ( pVendor->IsStatFlag(STATF_Pet) && pContExtra )
				pContExtra->ContentAdd(pItem);
			else
				pItem->Delete();
		}
		else
		{
			if ( pVendor->IsStatFlag(STATF_Pet) && pContExtra )
			{
				CItem * pItemNew = CItem::CreateDupeItem(pItem);
				pItemNew->SetAmount(amount);
				pContExtra->ContentAdd(pItemNew);
			}
			pItem->SetAmountUpdate( pItem->GetAmount() - amount );
		}
	}

	if ( iGold )
	{
		char	*z = Str_GetTemp();
		sprintf(z, g_Cfg.GetDefaultMsg(DEFMSG_NPC_VENDOR_SELL_TY),
			iGold, (iGold==1) ? "" : g_Cfg.GetDefaultMsg(DEFMSG_NPC_VENDOR_CA));
		pVendor->Speak(z);

		if ( fShortfall )
			pVendor->Speak(g_Cfg.GetDefaultMsg(DEFMSG_NPC_VENDOR_NOMONEY));

		m_pChar->AddGoldToPack(iGold);
		addVendorClose(pVendor);
	}
	else
	{
		if ( fShortfall )
			pVendor->Speak(g_Cfg.GetDefaultMsg(DEFMSG_NPC_VENDOR_CANTAFFORD));
	}
}

void CClient::Event_Profile( BYTE fWriteMode, CGrayUID uid, LPCTSTR pszProfile, int iProfileLen )
{
	ADDTOCALLSTACK("CClient::Event_Profile");
	// mode = 0 = Get profile, 1 = Set profile

	CChar	*pChar = uid.CharFind();
	if ( !pChar || !pChar->m_pPlayer )
		return;

	if ( pChar->OnTrigger(CTRIG_Profile, m_pChar) == TRIGRET_RET_TRUE )
		return;

	if ( fWriteMode )
	{
		// write stuff to the profile.
		if ( m_pChar != pChar )
		{
			if ( ! IsPriv(PRIV_GM))
				return;
			if ( m_pChar->GetPrivLevel() < pChar->GetPrivLevel())
				return;
		}

		if (pszProfile && !strchr(pszProfile, 0x0A))
			pChar->m_pPlayer->m_sProfile = pszProfile;
	}
	else
	{
		PacketProfile* cmd = new PacketProfile(this, pChar);
	}
}



void CClient::Event_MailMsg( CGrayUID uid1, CGrayUID uid2 )
{
	ADDTOCALLSTACK("CClient::Event_MailMsg");
	// NOTE: How do i protect this from spamming others !!!
	// Drag the mail bag to this clients char.

	CChar * pChar = uid1.CharFind();
	if ( pChar == NULL )
	{
		SysMessageDefault( DEFMSG_MAILBAG_DROP_1 );
		return;
	}

	if ( !IsSetEF(EF_Minimize_Triggers) )
	{
		if (pChar->OnTrigger(CTRIG_UserMailBag, m_pChar, NULL) == TRIGRET_RET_TRUE)
			return;
	}

	if ( pChar == m_pChar ) // this is normal (for some reason) at startup.
	{
		return;
	}
	// Might be an NPC ?
	TCHAR *pszMsg = Str_GetTemp();
	sprintf(pszMsg, g_Cfg.GetDefaultMsg( DEFMSG_MAILBAG_DROP_2 ), (LPCTSTR) m_pChar->GetName());
	pChar->SysMessage(pszMsg);
}



void CClient::Event_ToolTip( CGrayUID uid )
{
	ADDTOCALLSTACK("CClient::Event_ToolTip");
	CObjBase * pObj = uid.ObjFind();
	if ( pObj == NULL )
		return;

	if ( !IsSetEF(EF_Minimize_Triggers) )
	{
		if ( pObj->OnTrigger("@ToolTip", this) == TRIGRET_RET_TRUE )	// CTRIG_ToolTip, ITRIG_ToolTip
			return;
	}

	char *z = Str_GetTemp();
	sprintf(z, "'%s'", pObj->GetName());
	addToolTip(uid.ObjFind(), z);
}

void CClient::Event_PromptResp( LPCTSTR pszText, int len, DWORD context1, DWORD context2, DWORD type, bool bNoStrip )
{
	ADDTOCALLSTACK("CClient::Event_PromptResp");
	// result of addPrompt
	TCHAR szText[MAX_TALK_BUFFER];

	if ( Str_Check( pszText ) )
		return;

	CLIMODE_TYPE promptMode = m_Prompt_Mode;
	m_Prompt_Mode = CLIMODE_NORMAL;

	if ( m_Prompt_Uid != context1 )
		return;

	if ( len <= 0 )	// cancel
	{
		szText[0] = 0;
	}
	else
	{
		if ( bNoStrip )	// Str_GetBare will eat unicode characters
			len = strcpylen( szText, pszText, sizeof(szText) );
		else if ( promptMode == CLIMODE_PROMPT_SCRIPT_VERB )
			len = Str_GetBare( szText, pszText, sizeof(szText), "|~=[]{|}~" );
		else
			len = Str_GetBare( szText, pszText, sizeof(szText), "|~,=[]{|}~" );
	}

	LPCTSTR pszReName = NULL;
	LPCTSTR pszPrefix = NULL;

	switch ( promptMode )
	{
		case CLIMODE_PROMPT_GM_PAGE_TEXT:
			// m_Targ_Text
			Cmd_GM_Page( szText );
			return;

		case CLIMODE_PROMPT_VENDOR_PRICE:
			// Setting the vendor price for an item.
			{
				if ( type == 0 || szText[0] == '\0' )	// cancel
					return;
				CChar * pCharVendor = CGrayUID(context2).CharFind();
				if ( pCharVendor )
				{
					pCharVendor->NPC_SetVendorPrice( m_Prompt_Uid.ItemFind(), ATOI(szText) );
				}
			}
			return;

		case CLIMODE_PROMPT_NAME_RUNE:
			pszReName = g_Cfg.GetDefaultMsg(DEFMSG_RUNE_NAME);
			pszPrefix = g_Cfg.GetDefaultMsg(DEFMSG_RUNE_TO);
			break;

		case CLIMODE_PROMPT_NAME_KEY:
			pszReName = g_Cfg.GetDefaultMsg(DEFMSG_KEY_NAME);
			pszPrefix = g_Cfg.GetDefaultMsg(DEFMSG_KEY_TO);
			break;

		case CLIMODE_PROMPT_NAME_SHIP:
			pszReName = "Ship";
			pszPrefix = "SS ";
			break;

		case CLIMODE_PROMPT_NAME_SIGN:
			pszReName = "Sign";
			pszPrefix = "";
			break;

		case CLIMODE_PROMPT_STONE_NAME:
			pszReName = g_Cfg.GetDefaultMsg(DEFMSG_STONE_NAME);
			pszPrefix = g_Cfg.GetDefaultMsg(DEFMSG_STONE_FOR);
			break;

		case CLIMODE_PROMPT_STONE_SET_ABBREV:
			pszReName = "Abbreviation";
			pszPrefix = "";
			break;

		case CLIMODE_PROMPT_STONE_GRANT_TITLE:
		case CLIMODE_PROMPT_STONE_SET_TITLE:
			pszReName = "Title";
			pszPrefix = "";
			break;

		case CLIMODE_PROMPT_TARG_VERB:
			// Send a msg to the pre-tergetted player. "ETARGVERB"
			// m_Prompt_Uid = the target.
			// m_Prompt_Text = the prefix.
			if ( szText[0] != '\0' )
			{
				CObjBase * pObj = m_Prompt_Uid.ObjFind();
				if ( pObj )
				{
					CScript script( m_Prompt_Text, szText );
					pObj->r_Verb( script, this );
				}
			}
			return;

		case CLIMODE_PROMPT_SCRIPT_VERB:
			{
				// CChar * pChar = CGrayUID(context2).CharFind();
				CScript script( m_Prompt_Text, szText );
				if ( m_pChar )
					m_pChar->r_Verb( script, this );
			}
			return;

		default:
			// DEBUG_ERR(( "%x:Unrequested Prompt mode %d\n", m_Socket.GetSocket(), PrvTargMode ));
			SysMessage( "Unexpected prompt info" );
			return;
	}

	ASSERT(pszReName);

	CGString sMsg;

	CItem * pItem = m_Prompt_Uid.ItemFind();
	if ( pItem == NULL || type == 0 || szText[0] == '\0' )
	{
		SysMessagef( g_Cfg.GetDefaultMsg( DEFMSG_RENAME_CANCEL ), pszReName );
		return;
	}

	if ( g_Cfg.IsObscene( szText ))
	{
		SysMessagef( g_Cfg.GetDefaultMsg( DEFMSG_RENAME_WNAME ), pszReName, szText );
		return;
	}

	sMsg.Format("%s%s", pszPrefix, szText);
	switch (pItem->GetType())
	{
#ifndef _NEWGUILDSYSTEM
		case IT_STONE_GUILD:
		case IT_STONE_TOWN:
			{
				CItemStone * pStone = dynamic_cast <CItemStone*> ( pItem );
				if ( !pStone || !pStone->OnPromptResp(this, promptMode, szText, sMsg, CGrayUID(context2)) )
					return;
			}
			break;
#endif

		default:
			pItem->SetName(sMsg);
			sMsg.Format(g_Cfg.GetDefaultMsg( DEFMSG_RENAME_SUCCESS ), pszReName, (LPCTSTR) pItem->GetName());
			break;
	}

	SysMessage(sMsg);
}




void CClient::Event_Talk_Common(char *szText) // PC speech
{
	ADDTOCALLSTACK("CClient::Event_Talk_Common");
	// ??? Allow NPC's to talk to each other in the future.
	// Do hearing here so there is not feedback loop with NPC's talking to each other.
	if ( !m_pChar || !m_pChar->m_pPlayer || !m_pChar->m_pArea )
		return;

	if ( ! strnicmp( szText, "I resign from my guild", 22 ))
	{
		m_pChar->Guild_Resign(MEMORY_GUILD);
		return;
	}
	if ( ! strnicmp( szText, "I resign from my town", 21 ))
	{
		m_pChar->Guild_Resign(MEMORY_TOWN);
		return;
	}

	static LPCTSTR const sm_szTextMurderer[] =
	{
		g_Cfg.GetDefaultMsg( DEFMSG_NPC_TEXT_MURD_1 ),
		g_Cfg.GetDefaultMsg( DEFMSG_NPC_TEXT_MURD_2 ),
		g_Cfg.GetDefaultMsg( DEFMSG_NPC_TEXT_MURD_3 ),
		g_Cfg.GetDefaultMsg( DEFMSG_NPC_TEXT_MURD_4 ),
	};

	if ( ! strnicmp( szText, "I must consider my sins", 23 ))
	{
		int i = m_pChar->m_pPlayer->m_wMurders;
		if ( i >= COUNTOF(sm_szTextMurderer))
			i = COUNTOF(sm_szTextMurderer)-1;
		SysMessage( sm_szTextMurderer[i] );
		return;
	}

	// Guards are special
	// They can't hear u if your dead.
	bool fGhostSpeak = m_pChar->IsSpeakAsGhost();
	if ( ! fGhostSpeak && ( FindStrWord( szText, "GUARD" ) || FindStrWord( szText, "GUARDS" )))
	{
		m_pChar->CallGuards(NULL);
	}

	// Are we in a region that can hear ?
	if ( m_pChar->m_pArea->GetResourceID().IsItem())
	{
		CItemMulti * pItemMulti = dynamic_cast <CItemMulti *>( m_pChar->m_pArea->GetResourceID().ItemFind());
		if ( pItemMulti )
			pItemMulti->OnHearRegion( szText, m_pChar );
	}

	// Are there items on the ground that might hear u ?
	CSector * pSector = m_pChar->GetTopSector();
	if ( pSector->HasListenItems())
	{
		pSector->OnHearItem( m_pChar, szText );
	}

	// Find an NPC that may have heard us.
	CChar * pCharAlt = NULL;
	int iAltDist = UO_MAP_VIEW_SIGHT;
	CChar * pChar;
	int i=0;

	CWorldSearch AreaChars( m_pChar->GetTopPoint(), UO_MAP_VIEW_SIGHT );
	while (true)
	{
		pChar = AreaChars.GetChar();
		if ( !pChar )
			break;

		if ( pChar->IsStatFlag(STATF_COMM_CRYSTAL))
		{
			CItem	*pItemNext, *pItem = pChar->GetContentHead();
			for ( ; pItem ; pItem = pItemNext )
			{
				pItemNext = pItem->GetNext();
				pItem->OnHear(szText, m_pChar);
			}
		}

		if ( pChar == m_pChar )
			continue;

		if ( fGhostSpeak && ! pChar->CanUnderstandGhost())
			continue;

		bool fNamed = false;
		i = 0;
		if ( ! strnicmp( szText, "PETS", 4 ))
			i = 5;
		else if ( ! strnicmp( szText, "ALL ", 4 ))
			i = 4;
		else
		{
			// Named the char specifically ?
			i = pChar->NPC_OnHearName( szText );
			fNamed = true;
		}
		if ( i )
		{
			while ( ISWHITESPACE( szText[i] ))
				i++;

			if ( pChar->NPC_OnHearPetCmd( szText+i, m_pChar, !fNamed ))
			{
				if ( fNamed )
					return;
				if ( GetTargMode() == CLIMODE_TARG_PET_CMD )
					return;
				// The command might apply to other pets.
				continue;
			}
			if ( fNamed )
				break;
		}

		// Are we close to the char ?
		int iDist = m_pChar->GetTopDist3D( pChar );

		if ( pChar->Skill_GetActive() == NPCACT_TALK &&
			pChar->m_Act_Targ == m_pChar->GetUID()) // already talking to him
		{
			pCharAlt = pChar;
			iAltDist = 1;
		}
		else if ( pChar->IsClient() && iAltDist >= 2 )	// PC's have higher priority
		{
			pCharAlt = pChar;
			iAltDist = 2;	// high priority
		}
		else if ( iDist < iAltDist )	// closest NPC guy ?
		{
			pCharAlt = pChar;
			iAltDist = iDist;
		}

		// NPC's with special key words ?
		if ( pChar->m_pNPC )
		{
			if ( pChar->m_pNPC->m_Brain == NPCBRAIN_BANKER )
			{
				if ( FindStrWord( szText, "BANK" ))
					break;
			}
		}
	}

	if ( pChar == NULL )
	{
		i = 0;
		pChar = pCharAlt;
		if ( pChar == NULL )
			return;	// no one heard it.
	}

	// Change to all upper case for ease of search. ???
	_strupr( szText );

	// The char hears you say this.
	pChar->NPC_OnHear( &szText[i], m_pChar );
}




void CClient::Event_Talk( LPCTSTR pszText, HUE_TYPE wHue, TALKMODE_TYPE mode, bool bNoStrip) // PC speech
{
	ADDTOCALLSTACK("CClient::Event_Talk");
	if ( !GetAccount() || !GetChar() )
		return;

	if ( mode < 0 || mode > 14 ) // Less or greater is an exploit
		return;

	// These modes are server->client only
	if ( mode == 1 || mode == 3 || mode == 4 || mode == 5 || mode == 6 || mode == 7 || mode == 10 || mode == 11 || mode == 12 )
		return;

	if (( wHue < 2 ) || ( wHue > 0x03e9 ))
		wHue = HUE_TEXT_DEF;

	// store the language of choice.
	GetAccount()->m_lang.Set( NULL );	// default.

	// Rip out the unprintables first.
	TCHAR szText[MAX_TALK_BUFFER];
	int len;

	if ( bNoStrip )
	{
		// The characters in Unicode speech don't need to be filtered
		strncpy( szText, pszText, MAX_TALK_BUFFER - 1 );
		len = strlen( szText );
	}
	else
	{
		TCHAR szTextG[MAX_TALK_BUFFER];
		strncpy( szTextG, pszText, MAX_TALK_BUFFER - 1 );
		len = Str_GetBare( szText, szTextG, sizeof(szText)-1 );
	}

	if ( len <= 0 )
		return;

	pszText = szText;
	GETNONWHITESPACE(pszText);

	if ( !Event_Command(pszText,mode) )
	{
		bool	fCancelSpeech	= false;
		char	z[MAX_TALK_BUFFER];

		if ( m_pChar->OnTriggerSpeech(false, (TCHAR *)pszText, m_pChar, mode, wHue) )
			fCancelSpeech	= true;

		if ( g_Log.IsLoggedMask(LOGM_PLAYER_SPEAK) )
		{
			g_Log.Event( LOGM_PLAYER_SPEAK, "%x:'%s' Says '%s' mode=%d%s\n",
				GetSocketID(), m_pChar->GetName(), pszText, mode, fCancelSpeech ? " (muted)" : "");
		}

		// Guild and Alliance mode will not pass this.
		if ( mode == 13 || mode == 14 )
			return;

		strcpy(z, pszText);

		if ( g_Cfg.m_fSuppressCapitals )
		{
			int chars = strlen(z);
			int capitals = 0;
			int i = 0;
			for ( i = 0; i < chars; i++ )
				if (( z[i] >= 'A' ) && ( z[i] <= 'Z' ))
					capitals++;

			if (( chars > 5 ) && ((( capitals * 100 )/chars) > 75 ))
			{							// 80% of chars are in capital letters. lowercase it
				for ( i = 1; i < chars; i++ )				// instead of the 1st char
					if (( z[i] >= 'A' ) && ( z[i] <= 'Z' )) z[i] += 0x20;
			}
		}

		if ( !fCancelSpeech && ( len <= 128 ) ) // From this point max 128 chars
		{
			m_pChar->SpeakUTF8(z, wHue, (TALKMODE_TYPE)mode, m_pChar->m_fonttype, GetAccount()->m_lang);
			Event_Talk_Common((char *)z);
		}
	}
}

void CClient::Event_TalkUNICODE( NWORD* wszText, int iTextLen, HUE_TYPE wHue, TALKMODE_TYPE mMode, FONT_TYPE font, LPCTSTR pszLang )
{
	ADDTOCALLSTACK("CClient::Event_TalkUNICODE");
	// Get the text in wide bytes.
	// ENU = English
	// FRC = French
	// mode == TALKMODE_SYSTEM if coming from player talking.

	CAccount * pAccount = GetAccount();
	if ( !pAccount )	// this should not happen
		return;

	if ( iTextLen <= 0 )
		return;

	if ( mMode < 0 || mMode > 14 ) // Less or greater is an exploit
		return;

	// These modes are server->client only
	if ( mMode == 1 || mMode == 3 || mMode == 4 || mMode == 5 || mMode == 6 || mMode == 7 || mMode == 10 || mMode == 11 || mMode == 12 )
		return;

	if (( wHue < 0 ) || ( wHue > 0x03e9 ))
		wHue = HUE_TEXT_DEF;

	// store the default language of choice. CLanguageID
	pAccount->m_lang.Set(pszLang);

	TCHAR szText[MAX_TALK_BUFFER];
	const NWORD * puText = wszText;

	int iLen = CvtNUNICODEToSystem( szText, sizeof(szText), wszText, iTextLen );
	if ( iLen <= 0 )
		return;

	LPCTSTR pszText = szText;
	GETNONWHITESPACE(pszText);

	if ( !Event_Command(pszText, mMode) )
	{
		bool	fCancelSpeech	= false;

		if ( m_pChar->OnTriggerSpeech( false, (TCHAR *)pszText, m_pChar, mMode, wHue) )
			fCancelSpeech	= true;

		if ( g_Log.IsLoggedMask(LOGM_PLAYER_SPEAK) )
		{
			g_Log.Event(LOGM_PLAYER_SPEAK, "%x:'%s' Says UNICODE '%s' '%s' mode=%d%s\n", GetSocketID(),
				m_pChar->GetName(), pAccount->m_lang.GetStr(), pszText, mMode, fCancelSpeech ? " (muted)" : "" );
		}

		// Guild and Alliance mode will not pass this.
		if ( mMode == 13 || mMode == 14 )
			return;

		if ( IsSetEF(EF_UNICODE) )
		{
			if ( g_Cfg.m_fSuppressCapitals )
			{
				int chars = strlen(szText);
				int capitals = 0;
				int i = 0;
				for ( i = 0; i < chars; i++ )
					if (( szText[i] >= 'A' ) && ( szText[i] <= 'Z' ))
						capitals++;

				if (( chars > 5 ) && ((( capitals * 100 )/chars) > 75 ))
				{							// 80% of chars are in capital letters. lowercase it
					for ( i = 1; i < chars; i++ )				// instead of the 1st char
						if (( szText[i] >= 'A' ) && ( szText[i] <= 'Z' ))
							szText[i] += 0x20;

					CvtSystemToNUNICODE(wszText, COUNTOF(wszText), szText, chars);
				}
			}
		}

		if ( !fCancelSpeech && ( iLen <= 128 ) ) // From this point max 128 chars
		{
			m_pChar->SpeakUTF8Ex(puText, wHue, mMode, font, pAccount->m_lang);
			Event_Talk_Common((char *)pszText);
		}
	}
}

void CClient::Event_SetName( CGrayUID uid, const char * pszCharName )
{
	ADDTOCALLSTACK("CClient::Event_SetName");
	// Set the name in the character status window.
	CChar * pChar = uid.CharFind();
	if ( !pChar || !m_pChar )
		return;

   if ( Str_CheckName(pszCharName) || !strlen(pszCharName) )
		return;

	// Do we have the right to do this ?
	if ( m_pChar == pChar || ! pChar->NPC_IsOwnedBy( m_pChar, true ))
		return;
	if ( FindTableSorted( pszCharName, sm_szCmd_Redirect, COUNTOF(sm_szCmd_Redirect) ) >= 0 )
		return;
	if ( FindTableSorted( pszCharName, CCharNPC::sm_szVerbKeys, 14 ) >= 0 )
		return;
	if ( g_Cfg.IsObscene(pszCharName))
		return;

	CScriptTriggerArgs args;
	args.m_pO1 = pChar;
	args.m_s1 = pszCharName;
	if ( m_pChar->OnTrigger(CTRIG_Rename, this, &args) == TRIGRET_RET_TRUE )
		return;

	pChar->SetName(pszCharName);
}

bool CDialogResponseArgs::r_WriteVal( LPCTSTR pszKey, CGString &sVal, CTextConsole * pSrc )
{
	ADDTOCALLSTACK("CDialogResponseArgs::r_WriteVal");
	EXC_TRY("WriteVal");
	if ( ! strnicmp( pszKey, "ARGCHK", 6 ))
	{
		// CGTypedArray <DWORD,DWORD> m_CheckArray;
		pszKey += 6;
		SKIP_SEPARATORS(pszKey);

		int iQty = m_CheckArray.GetCount();
		if ( pszKey[0] == '\0' )
		{
			sVal.FormatVal(iQty);
			return( true );
		}
		else if ( ! strnicmp( pszKey, "ID", 2) )
		{
			pszKey += 2;

			if ( iQty && m_CheckArray[0] )
				sVal.FormatVal( m_CheckArray[0] );
			else
				sVal.FormatVal( -1 );

			return( true );
		}

		int iNum = Exp_GetSingle( pszKey );
		SKIP_SEPARATORS(pszKey);
		for ( int i=0; i<iQty; i++ )
		{
			if ( iNum == m_CheckArray[i] )
			{
				sVal = "1";
				return( true );
			}
		}
		sVal = "0";
		return( true );
	}
	if ( ! strnicmp( pszKey, "ARGTXT", 6 ))
	{
		pszKey += 6;
		SKIP_SEPARATORS(pszKey);

		int iQty = m_TextArray.GetCount();
		if ( pszKey[0] == '\0' )
		{
			sVal.FormatVal(iQty);
			return( true );
		}

		int iNum = Exp_GetSingle( pszKey );
		SKIP_SEPARATORS(pszKey);

		for ( int i=0; i<m_TextArray.GetCount(); i++ )
		{
			if ( iNum == m_TextArray[i]->m_ID )
			{
				sVal = m_TextArray[i]->m_sText;
				return( true );
			}
		}
		sVal.Empty();
		return( false );
	}
	return CScriptTriggerArgs::r_WriteVal( pszKey, sVal, pSrc);
	EXC_CATCH;

	EXC_DEBUG_START;
	EXC_ADD_KEYRET(pSrc);
	EXC_DEBUG_END;
	return false;
}

bool CClient::Event_DoubleClick( CGrayUID uid, bool fMacro, bool fTestTouch, bool fScript )
{
	ADDTOCALLSTACK("CClient::Event_DoubleClick");
	// Try to use the object in some way.
	// will trigger a OnTarg_Use_Item() usually.
	// fMacro = ALTP vs dbl click. no unmount.

	// Allow some static in game objects to have function?
	// Not possible with dclick.

	ASSERT(m_pChar);
	CObjBase * pObj = uid.ObjFind();
	if ( !pObj || ( fTestTouch && !m_pChar->CanSee( pObj )) )
	{
		addObjectRemoveCantSee( uid, "the target" );
		return false;
	}

	// Face the object we are using/activating.
	SetTargMode();
	m_Targ_UID = uid;
	m_pChar->UpdateDir( pObj );

	if ( pObj->IsItem())
	{
		return Cmd_Use_Item( dynamic_cast <CItem *>(pObj), fTestTouch, fScript );
	}

	CChar * pChar = dynamic_cast <CChar*>(pObj);

	if ( pChar->OnTrigger( CTRIG_DClick, m_pChar ) == TRIGRET_RET_TRUE )
		return true;

	if ( ! fMacro )
	{
		if ( pChar == m_pChar )
		{
			if ( pChar->IsStatFlag(STATF_OnHorse) )
			{
				// in war mode not to drop from horse accidentally we need this check
				// Should also check for STATF_War in case someone starts fight and runs away.
				if ( pChar->IsStatFlag(STATF_War) && pChar->Memory_FindTypes(MEMORY_FIGHT) && !IsSetCombatFlags(COMBAT_DCLICKSELF_UNMOUNTS))
				{
					addCharPaperdoll(pChar);
					return true;
				}
				else if ( pChar->Horse_UnMount() )
					return true;
			}
		}

		if ( pChar->m_pNPC && ( pChar->GetNPCBrain(true) != NPCBRAIN_HUMAN ))
		{
			if ( m_pChar->Horse_Mount( pChar ))
				return true;
			switch ( pChar->GetID())
			{
				case CREID_HORSE_PACK:
				case CREID_LLAMA_PACK:
					// pack animals open container.
					return Cmd_Use_Item( pChar->GetPackSafe(), fTestTouch );
				default:
					if ( IsPriv(PRIV_GM))
					{
						// snoop the creature.
						return Cmd_Use_Item( pChar->GetPackSafe(), false );
					}
					return false;
			}
		}
	}

	// open paper doll.
	addCharPaperdoll(pChar);

	return( true );
}

void CClient::Event_SingleClick( CGrayUID uid )
{
	ADDTOCALLSTACK("CClient::Event_SingleClick");
	// the ALLNAMES macro comes through here.
	ASSERT(m_pChar);

	CObjBase * pObj = uid.ObjFind();
	if ( !m_pChar->CanSee(pObj) )
	{
		// ALLNAMES makes this happen as we are running thru an area.
		// So display no msg. Do not use (addObjectRemoveCantSee)
		addObjectRemove( uid );
		return;
	}

	CScriptTriggerArgs Args( this );
	if ( pObj->OnTrigger( "@Click", m_pChar, &Args ) == TRIGRET_RET_TRUE )	// CTRIG_Click, ITRIG_Click
		return;

	if ( pObj->IsItem())
	{
		addItemName( dynamic_cast <CItem *>(pObj));
		return;
	}

	// TODO: if tooltip are enabled this is not used.
	// and move char name creation to tooltip for .debug
	if ( pObj->IsChar())
	{
		addCharName( dynamic_cast <CChar*>(pObj) );
		return;
	}

	SysMessagef( "Bogus item uid=0%x?", (DWORD) uid );
}

void CClient::Event_Target(DWORD context, CGrayUID uid, CPointMap pt, BYTE flags, ITEMID_TYPE id)
{
	ADDTOCALLSTACK("CClient::Event_Target");
	// XCMD_Target
	// If player clicks on something with the targetting cursor
	// Assume addTarget was called before this.
	// NOTE: Make sure they can actually validly trarget this item !

	if (context != GetTargMode())
	{
		// unexpected context
		if (context != 0 && (pt.m_x != 0xFFFF || uid.GetPrivateUID() != 0))
			SysMessage("Unexpected target info");

		return;
	}

	if (pt.m_x == 0xFFFF && uid.GetPrivateUID() == 0)
	{
		// cancelled
		SetTargMode();
		return;
	}

	CLIMODE_TYPE prevmode = GetTargMode();
	ClearTargMode();

	if (GetNetState()->isClientKR() && (flags & 0xA0))
		uid = m_Targ_Last;

	CObjBase* pTarget = uid.ObjFind();
	if (IsPriv(PRIV_GM))
	{
		if (uid.IsValidUID() && pTarget == NULL)
		{
			addObjectRemoveCantSee(uid, "the target");
			return;
		}
	}
	else
	{
		if (uid.IsValidUID())
		{
			if (m_pChar->CanSee(pTarget) == false)
			{
				addObjectRemoveCantSee(uid, "the target");
				return;
			}
		}
		else
		{
			// the point must be valid
			if (m_pChar->GetTopDistSight(pt) > UO_MAP_VIEW_SIZE)
				return;
		}
	}

	if (pTarget != NULL)
	{
		// remove the last existing target
		m_Targ_Last = uid;

		// point inside a container is not really meaningful here
		pt = pTarget->GetTopLevelObj()->GetTopPoint();
	}

	switch (prevmode)
	{
		// GM stuff.
		case CLIMODE_TARG_OBJ_SET:			OnTarg_Obj_Set( pTarget ); break;
		case CLIMODE_TARG_OBJ_INFO:			OnTarg_Obj_Info( pTarget, pt, id );  break;
		case CLIMODE_TARG_OBJ_FUNC:			OnTarg_Obj_Function( pTarget, pt, id );  break;

		case CLIMODE_TARG_UNEXTRACT:		OnTarg_UnExtract( pTarget, pt ); break;
		case CLIMODE_TARG_ADDITEM:			OnTarg_Item_Add( pTarget, pt ); break;
		case CLIMODE_TARG_LINK:				OnTarg_Item_Link( pTarget ); break;
		case CLIMODE_TARG_TILE:				OnTarg_Tile( pTarget, pt );  break;

		// Player stuff.
		case CLIMODE_TARG_SKILL:			OnTarg_Skill( pTarget ); break;
		case CLIMODE_TARG_SKILL_MAGERY:     OnTarg_Skill_Magery( pTarget, pt ); break;
		case CLIMODE_TARG_SKILL_HERD_DEST:  OnTarg_Skill_Herd_Dest( pTarget, pt ); break;
		case CLIMODE_TARG_SKILL_POISON:		OnTarg_Skill_Poison( pTarget ); break;
		case CLIMODE_TARG_SKILL_PROVOKE:	OnTarg_Skill_Provoke( pTarget ); break;

		case CLIMODE_TARG_REPAIR:			m_pChar->Use_Repair( uid.ItemFind() ); break;
		case CLIMODE_TARG_PET_CMD:			OnTarg_Pet_Command( pTarget, pt ); break;
		case CLIMODE_TARG_PET_STABLE:		OnTarg_Pet_Stable( uid.CharFind() ); break;

		case CLIMODE_TARG_USE_ITEM:			OnTarg_Use_Item( pTarget, pt, id );  break;
		case CLIMODE_TARG_STONE_RECRUIT:	OnTarg_Stone_Recruit( uid.CharFind() );  break;
		case CLIMODE_TARG_STONE_RECRUITFULL:OnTarg_Stone_Recruit( uid.CharFind(), true ); break;
		case CLIMODE_TARG_PARTY_ADD:		OnTarg_Party_Add( uid.CharFind() );  break;

		default:							break;
	}
}

void CClient::Event_AOSPopupMenuRequest( DWORD uid ) //construct packet after a client request
{
	ADDTOCALLSTACK("CClient::Event_AOSPopupMenuRequest");
	CGrayUID uObj = uid;
	CChar *pChar = uObj.CharFind();

	if ( !CanSee( uObj.ObjFind() ) )
		return;

	if ( m_pChar && !(m_pChar->CanSeeLOS( uObj.ObjFind(), 0x0 )) )
		return;

	if (m_pPopupPacket != NULL)
	{
		DEBUG_ERR(("New popup packet being formed before previous one has been released."));

		delete m_pPopupPacket;
		m_pPopupPacket = NULL;
	}

	m_pPopupPacket = new PacketDisplayPopup(this, uid);

	CScriptTriggerArgs Args;
	bool fPreparePacket = false;

	if ( uObj.IsItem() )
	{
		if ( !IsSetEF(EF_Minimize_Triggers))
		{
			Args = 1;
			uObj.ItemFind()->OnTrigger(ITRIG_ContextMenuRequest, this->GetChar(), &Args);
			fPreparePacket = true; // There is no hardcoded stuff for items
		}
		else
		{
			delete m_pPopupPacket;
			m_pPopupPacket = NULL;
			return;
		}
	}
	else if ( uObj.IsChar() )
	{
		if ( !IsSetEF(EF_Minimize_Triggers))
		{
			Args = 1;
			int iRet = pChar->OnTrigger(CTRIG_ContextMenuRequest, this->GetChar(), &Args);
			if ( iRet  == TRIGRET_RET_TRUE )
				fPreparePacket = true;
		}
	}
	else
	{
		delete m_pPopupPacket;
		m_pPopupPacket = NULL;
		return;
	}

	if ( ! fPreparePacket )
	{

		if ( pChar->IsHuman() )
			m_pPopupPacket->addOption(POPUP_PAPERDOLL, 6123, POPUPFLAG_COLOR, 0xFFFF);

		if ( pChar == m_pChar )
			m_pPopupPacket->addOption(POPUP_BACKPACK, 6145, POPUPFLAG_COLOR, 0xFFFF);

		if ( pChar->m_pNPC )
		{
			switch ( pChar->m_pNPC->m_Brain )
			{
				case NPCBRAIN_BANKER:
					{
						m_pPopupPacket->addOption(POPUP_BANKBOX, 6105, POPUPFLAG_COLOR, 0xFFFF);
						m_pPopupPacket->addOption(POPUP_BANKBALANCE, 6124, POPUPFLAG_COLOR, 0xFFFF);
						break;
					}

				case NPCBRAIN_STABLE:
					m_pPopupPacket->addOption(POPUP_STABLESTABLE, 6126, POPUPFLAG_COLOR, 0xFFFF);
					m_pPopupPacket->addOption(POPUP_STABLERETRIEVE, 6127, POPUPFLAG_COLOR, 0xFFFF);

				case NPCBRAIN_VENDOR:
				case NPCBRAIN_HEALER:
					m_pPopupPacket->addOption(POPUP_VENDORBUY, 6103, POPUPFLAG_COLOR, 0xFFFF);
					m_pPopupPacket->addOption(POPUP_VENDORSELL, 6104, POPUPFLAG_COLOR, 0xFFFF);
					break;

				default:
					break;
			}

			if ( pChar->NPC_IsOwnedBy( m_pChar, false ) )
			{
				m_pPopupPacket->addOption(POPUP_PETGUARD, 6107, POPUPFLAG_COLOR, 0xFFFF);
				m_pPopupPacket->addOption(POPUP_PETFOLLOW, 6108, POPUPFLAG_COLOR, 0xFFFF);
				m_pPopupPacket->addOption(POPUP_PETDROP, 6109, POPUPFLAG_COLOR, 0xFFFF);
				m_pPopupPacket->addOption(POPUP_PETKILL, 6111, POPUPFLAG_COLOR, 0xFFFF);
				m_pPopupPacket->addOption(POPUP_PETSTOP, 6112, POPUPFLAG_COLOR, 0xFFFF);
				m_pPopupPacket->addOption(POPUP_PETSTAY, 6114, POPUPFLAG_COLOR, 0xFFFF);
				m_pPopupPacket->addOption(POPUP_PETFRIEND, 6110, POPUPFLAG_COLOR, 0xFFFF);
				m_pPopupPacket->addOption(POPUP_PETTRANSFER, 6113, POPUPFLAG_COLOR, 0xFFFF);
			}
		}

		if (( Args.m_iN1 != 1 ) && ( !IsSetEF(EF_Minimize_Triggers)))
		{
			Args = 2;
			pChar->OnTrigger(CTRIG_ContextMenuRequest, this->GetChar(), &Args);
		}
	}
	
	if (m_pPopupPacket->getOptionCount() <= 0)
	{
		delete m_pPopupPacket;
		m_pPopupPacket = NULL;
		return;
	}

	m_pPopupPacket->finalise();
	m_pPopupPacket->push(this);
	m_pPopupPacket = NULL;
}

void CClient::Event_AOSPopupMenuSelect( DWORD uid, WORD EntryTag ) //do something after a player selected something from a pop-up menu
{
	ADDTOCALLSTACK("CClient::Event_AOSPopupMenuSelect");
	if ( !EntryTag )
		return;

	CGrayUID uObj = uid;
	CScriptTriggerArgs Args;

	if ( !CanSee( uObj.ObjFind() ) )
		return;

	if ( m_pChar && !(m_pChar->CanSeeLOS( uObj.ObjFind(), 0x0 )) )
		return;

	if ( uObj.IsItem() )
	{
		if ( !IsSetEF(EF_Minimize_Triggers) )
		{
			Args = EntryTag;
			uObj.ItemFind()->OnTrigger(ITRIG_ContextMenuSelect, this->GetChar(), &Args);
		}
		return; //There is no hardcoded stuff for items
	}
	else if ( uObj.IsChar() )
	{
		if ( !IsSetEF(EF_Minimize_Triggers))
		{
			Args = EntryTag;
			int iRet = uObj.CharFind()->OnTrigger(CTRIG_ContextMenuSelect, this->GetChar(), &Args);
			if ( iRet == TRIGRET_RET_TRUE )
				return;
		}
	}
	else
		return;

	CChar *pChar = uObj.CharFind();

	if (( pChar->m_pNPC ) && pChar->NPC_IsOwnedBy( m_pChar, false ))
	{
		switch ( EntryTag )
		{
			case POPUP_PETGUARD:
				pChar->NPC_OnHearPetCmd( "guard", m_pChar, false );
				break;

			case POPUP_PETFOLLOW:
				pChar->NPC_OnHearPetCmd( "follow", m_pChar, false );
				break;

			case POPUP_PETDROP:
				pChar->NPC_OnHearPetCmd( "drop", m_pChar, false );
				break;

			case POPUP_PETKILL:
				pChar->NPC_OnHearPetCmd( "kill", m_pChar, false );
				break;

			case POPUP_PETSTOP:
				pChar->NPC_OnHearPetCmd( "stop", m_pChar, false );
				break;

			case POPUP_PETSTAY:
				pChar->NPC_OnHearPetCmd( "stay", m_pChar, false );
				break;

			case POPUP_PETFRIEND:
				pChar->NPC_OnHearPetCmd( "friend", m_pChar, false );
				break;

			case POPUP_PETTRANSFER:
				pChar->NPC_OnHearPetCmd( "transfer", m_pChar, false );
				break;
		}
	}

	switch ( EntryTag )
	{
		case POPUP_PAPERDOLL:
			if ( m_pChar == pChar )
				Event_DoubleClick(m_pChar->GetUID(), true, false);
			else
				m_pChar->Use_Obj((CObjBase *)pChar, false, false);
			break;

		case POPUP_BACKPACK:
			m_pChar->Use_Obj( (CObjBase *)m_pChar->LayerFind( LAYER_PACK ), false, false );
			break;

		case POPUP_BANKBOX:
			if ( pChar->m_pNPC->m_Brain == NPCBRAIN_BANKER )
				addBankOpen( m_pChar );
			break;

		case POPUP_BANKBALANCE:
			if ( pChar->m_pNPC->m_Brain == NPCBRAIN_BANKER )
				SysMessagef( "You have %d gold piece(s) in your bankbox", m_pChar->GetBank()->ContentCount( RESOURCE_ID(RES_TYPEDEF,IT_GOLD) ) );
			break;

		case POPUP_VENDORBUY:
			if ( pChar->NPC_IsVendor() )
				pChar->NPC_OnHear("buy", m_pChar);
			break;

		case POPUP_VENDORSELL:
			if ( pChar->NPC_IsVendor() )
				pChar->NPC_OnHear("sell", m_pChar);
			break;

		case POPUP_STABLESTABLE:
			if ( pChar->m_pNPC->m_Brain == NPCBRAIN_STABLE )
				pChar->NPC_OnHear("stable", m_pChar);
			break;

		case POPUP_STABLERETRIEVE:
			if ( pChar->m_pNPC->m_Brain == NPCBRAIN_STABLE )
				pChar->NPC_OnHear("retrieve", m_pChar);
			break;
	}
}

void CClient::Event_BugReport( const TCHAR * pszText, int len, BUGREPORT_TYPE type, CLanguageID lang )
{
	ADDTOCALLSTACK("CClient::Event_BugReport");
	if ( !m_pChar )
		return;

	CScriptTriggerArgs Args(type);
	Args.m_s1 = pszText;
	Args.m_VarsLocal.SetStr("LANG", false, lang.GetStr());

	m_pChar->OnTrigger(CTRIG_UserBugReport, m_pChar, &Args);
}

void CClient::Event_UseToolbar(BYTE bType, DWORD dwArg)
{
	ADDTOCALLSTACK("CClient::Event_UseToolbar");
	if ( !m_pChar )
		return;

	if ( !IsSetEF(EF_Minimize_Triggers) )
	{
		CScriptTriggerArgs Args( bType, dwArg );
		if ( m_pChar->OnTrigger( CTRIG_UserKRToolbar, m_pChar, &Args ) == TRIGRET_RET_TRUE )
			return;
	}

	switch(bType)
	{
		case 0x01: // Spell call
		{
			Cmd_Skill_Magery((SPELL_TYPE)dwArg, m_pChar);
		} break;

		case 0x02: // Weapon ability
		{

		} break;

		case 0x03: // Skill
		{
			Event_Skill_Use((SKILL_TYPE)dwArg);
		} break;

		case 0x04: // Item
		{
			Event_DoubleClick(CGrayUID(dwArg), true, true);
		} break;

		case 0x05: // Scroll
		{

		} break;
	}

}

//----------------------------------------------------------------------

void CClient::Event_ExtCmd( EXTCMD_TYPE type, TCHAR * pszName )
{
	ADDTOCALLSTACK("CClient::Event_ExtCmd");

	if ( m_pChar )
	{
		if ( !IsSetEF(EF_Minimize_Triggers) )
		{
			CScriptTriggerArgs	Args( pszName );
			Args.m_iN1	= type;
			if ( m_pChar->OnTrigger( CTRIG_UserExtCmd, m_pChar, &Args ) == TRIGRET_RET_TRUE )
				return;
			strcpy( pszName, Args.m_s1 );
		}
	}

	TCHAR * ppArgs[2];
	Str_ParseCmds( pszName, ppArgs, COUNTOF(ppArgs), " " );
	switch ( type )
	{
		case EXTCMD_OPEN_SPELLBOOK: // 67 = open spell book if we have one.
			{
				CItem * pBook = m_pChar->GetSpellbook();
				if ( pBook == NULL )
				{
					SysMessageDefault( DEFMSG_NOSPELLBOOK );
					break;
				}
				// Must send proper context info or it will crash tha client.
				// if ( pBook->GetParent() != m_pChar )
				{
					addItem( m_pChar->GetPackSafe());
				}
				addItem( pBook );
				addSpellbookOpen( pBook );
			}
			break;

		case EXTCMD_ANIMATE: // Cmd_Animate
			if ( !strcmpi( ppArgs[0],"bow"))
				m_pChar->UpdateAnimate( ANIM_BOW );
			else if ( ! strcmpi( ppArgs[0],"salute"))
				m_pChar->UpdateAnimate( ANIM_SALUTE );
			else
			{
				DEBUG_ERR(( "%x:Event Animate '%s'\n", GetSocketID(), ppArgs[0] ));
			}
			break;

		case EXTCMD_SKILL:			// Skill
			Event_Skill_Use( (SKILL_TYPE) ATOI( ppArgs[0] ));
			break;

		case EXTCMD_AUTOTARG:	// bizarre new autotarget mode.
			// "target x y z"
			{
				CGrayUID uid( ATOI( ppArgs[0] ));
				CObjBase * pObj = uid.ObjFind();
				if ( pObj )
				{
					DEBUG_ERR(( "%x:Event Autotarget '%s' '%s'\n", GetSocketID(), pObj->GetName(), ppArgs[1] ));
				}
				else
				{
					DEBUG_ERR(( "%x:Event Autotarget UNK '%s' '%s'\n", GetSocketID(), ppArgs[0], ppArgs[1] ));
				}
			}
			break;

		case EXTCMD_CAST_MACRO:	// macro spell.
		case EXTCMD_CAST_BOOK:	// cast spell from book.
			{
				SPELL_TYPE spell = (SPELL_TYPE) ATOI(ppArgs[0]);
				const CSpellDef* pSpellDef = g_Cfg.GetSpellDef(spell);
				if (pSpellDef == NULL)
					return;

				if ( IsSetMagicFlags( MAGICF_PRECAST ) && !pSpellDef->IsSpellType( SPELLFLAG_NOPRECAST ) )
				{
					int skill;
					if (!pSpellDef->GetPrimarySkill(&skill, NULL))
						return;

					m_tmSkillMagery.m_Spell = spell;
					m_pChar->m_atMagery.m_Spell = spell;	// m_atMagery.m_Spell
					m_Targ_UID = m_pChar->GetUID();	// default target.
					m_Targ_PrvUID = m_pChar->GetUID();
					m_pChar->Skill_Start( (SKILL_TYPE)skill );
				}
				else
					Cmd_Skill_Magery(spell, m_pChar );
			}
			break;

		case EXTCMD_DOOR_AUTO: // open door macro = Attempt to open a door around us.
			if ( m_pChar && !m_pChar->IsStatFlag( STATF_DEAD ) )
			{
				CWorldSearch Area( m_pChar->GetTopPoint(), 4 );
				while(true)
				{
					CItem * pItem = Area.GetItem();
					if ( pItem == NULL )
						break;
					switch ( pItem->GetType() )
					{
						case IT_PORT_LOCKED:	// Can only be trigered.
						case IT_PORTCULIS:
						case IT_DOOR_LOCKED:
						case IT_DOOR:
							m_pChar->Use_Obj( pItem, false );
							return;

						default:
							break;
					}
				}
			}
			break;

		case EXTCMD_UNKGODCMD: // 107, seen this but no idea what it does.
			break;

		case EXTCMD_INVOKE_VIRTUE:
			{
				int iVirtueID = ppArgs[0][0] - '0';	// 0x1=Honor, 0x2=Sacrifice, 0x3=Valor
				CScriptTriggerArgs Args(m_pChar);
				Args.m_iN1 = iVirtueID;

				m_pChar->OnTrigger(CTRIG_UserVirtueInvoke, m_pChar, &Args);
			}
			break;

		default:
			DEBUG_ERR(( "%x:Event_ExtCmd unk %d, '%s'\n", GetSocketID(), type, pszName ));
			break;
	}
}

// ---------------------------------------------------------------------

bool CClient::xPacketFilter( const CEvent * pEvent, int iLen )
{
	ADDTOCALLSTACK("CClient::xPacketFilter");
	
	EXC_TRY("packet filter");
	if ( iLen && g_Serv.m_PacketFilter[pEvent->Default.m_Cmd][0] )
	{
		CScriptTriggerArgs Args(pEvent->Default.m_Cmd);
		enum TRIGRET_TYPE trigReturn;
		char idx[5];

		Args.m_s1 = GetPeerStr();
		Args.m_pO1 = this; // Yay for ARGO.SENDPACKET
		Args.m_VarsLocal.SetNum("CONNECTIONTYPE", GetConnectType());

		int bytes = iLen;
		int bytestr = minimum(bytes, SCRIPT_MAX_LINE_LEN);
		char *zBuf = Str_GetTemp();

		Args.m_VarsLocal.SetNum("NUM", bytes);
		memcpy(zBuf, &(pEvent->m_Raw[0]), bytestr);
		zBuf[bytestr] = 0;
		Args.m_VarsLocal.SetStr("STR", true, zBuf, true);
		if ( m_pAccount )
		{
			Args.m_VarsLocal.SetStr("ACCOUNT", false, m_pAccount->GetName());
			if ( m_pChar )
			{
				Args.m_VarsLocal.SetNum("CHAR", m_pChar->GetUID());
			}
		}

		//	Fill locals [0..X] to the first X bytes of the packet
		for ( int i = 0; i < bytes; ++i )
		{
			sprintf(idx, "%d", i);
			Args.m_VarsLocal.SetNum(idx, (int)pEvent->m_Raw[i]);
		}

		//	Call the filtering function
		if ( g_Serv.r_Call(g_Serv.m_PacketFilter[pEvent->Default.m_Cmd], &g_Serv, &Args, NULL, &trigReturn) )
			if ( trigReturn == TRIGRET_RET_TRUE )
				return true;	// do not cry about errors
	}

	EXC_CATCH;
	return false;
}