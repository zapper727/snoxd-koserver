#include "stdafx.h"

#include "Knights.h"
#include "EbenezerDlg.h"
#include "User.h"

using std::string;

CKnights::CKnights()
{
	m_sIndex = 0;
	m_byFlag = 0;			// 1 : Clan, 2 : 기사단
	m_byNation = 0;		// nation
	m_byGrade = 5;			// clan 등급 (1 ~ 5등급)
	m_byRanking = 0;		// clan 등급 (1 ~ 5등)
	m_sMembers = 1;
	memset(&m_Image, 0, sizeof(m_Image));
	m_nMoney = 0;
	m_sDomination = 0;
	m_nPoints = 0;
	m_nClanPointFund = 0;
	m_sCape = -1;
	m_sAlliance = 0;
	m_sMarkLen = 0;
	m_sMarkVersion = 0;
	m_bCapeR = m_bCapeG = m_bCapeB = 0;
}

void CKnights::OnLogin(CUser *pUser)
{
	Packet result;

	// Set the active session for this user
	foreach_array (i, m_arKnightsUser)
	{
		_KNIGHTS_USER * p = &m_arKnightsUser[i];
		if (!p->byUsed
			|| STRCASECMP(p->strUserName.c_str(), pUser->GetName().c_str()) != 0)
			continue;

		p->pSession = pUser;
		pUser->m_pKnightsUser = p;
		break;
	}

	// Send login notice
	// TO-DO: Shift this to SERVER_RESOURCE
	std::string buffer = string_format("*** %s has logged in ***", pUser->GetName().c_str());
	ChatPacket::Construct(&result, KNIGHTS_CHAT, &buffer);
	Send(&result);

	// Construct the clan notice packet to send to the logged in player
	if (!m_strClanNotice.empty())
	{
		ConstructClanNoticePacket(&result);
		pUser->Send(&result);
	}
}

void CKnights::ConstructClanNoticePacket(Packet *result)
{
	result->Initialize(WIZ_NOTICE);
	result->DByte();
	*result	<< uint8(4)			// type
			<< uint8(1)			// total blocks
			<< "Clan Notice"	// header
			<< m_strClanNotice;
}

void CKnights::UpdateClanNotice(std::string & clanNotice)
{
	Packet result;

	// Update the stored clan notice
	m_strClanNotice = clanNotice;

	// Construct the update notice packet to inform players the clan notice has changed
	std::string updateNotice = string_format("*** %s updated the clan notice ***", m_strChief.c_str());
	ChatPacket::Construct(&result, KNIGHTS_CHAT, &updateNotice);
	Send(&result);

	// Construct the new clan notice packet
	ConstructClanNoticePacket(&result);
	Send(&result);
}

void CKnights::OnLogout(CUser *pUser)
{
	// Unset the active session for this user
	if (pUser->m_pKnightsUser != nullptr)
	{
		pUser->m_pKnightsUser->pSession = nullptr;
		pUser->m_pKnightsUser = nullptr;
	}

	Packet logoutNotice;
	// TO-DO: Shift this to SERVER_RESOURCE
	std::string buffer = string_format("*** %s has logged out ***", pUser->GetName().c_str());
	ChatPacket::Construct(&logoutNotice, KNIGHTS_CHAT, &buffer);
	Send(&logoutNotice);
}

bool CKnights::AddUser(std::string & strUserID)
{
	for (int i = 0; i < MAX_CLAN_USERS; i++)
	{
		if (m_arKnightsUser[i].byUsed == 0)
		{
			m_arKnightsUser[i].byUsed = 1;
			m_arKnightsUser[i].strUserName = strUserID;
			m_arKnightsUser[i].pSession = g_pMain->GetUserPtr(strUserID, TYPE_CHARACTER);
			return true;
		}
	}

	return false;
}

bool CKnights::AddUser(CUser *pUser)
{
	if (pUser == nullptr
		|| !AddUser(pUser->GetName()))
		return false;

	pUser->SetClanID(m_sIndex);
	pUser->m_bFame = TRAINEE;
	return true;
}

/**
 * @brief	Removes the specified user from the clan array.
 *
 * @param	strUserID	Identifier for the user.
 *
 * @return	.
 */
bool CKnights::RemoveUser(std::string & strUserID)
{
	for (int i = 0; i < MAX_CLAN_USERS; i++)
	{
		_KNIGHTS_USER * p = &m_arKnightsUser[i];
		if (p->byUsed == 0)
			continue;

		if (STRCASECMP(p->strUserName.c_str(), strUserID.c_str()) == 0)
		{
			// If they're not logged in (note: logged in users being removed have their NP refunded in the other handler)
			// but they've donated NP, ensure they're refunded for the next time they login.
			if (p->nDonatedNP > 0)
				RefundDonatedNP(p->nDonatedNP, nullptr, p->strUserName.c_str());

			p->Initialise();
			return true;
		}
	}

	return false;
}

/**
 * @brief	Removes the specified user from this clan.
 *
 * @param	pUser	The user.
 */
bool CKnights::RemoveUser(CUser *pUser)
{
	if (pUser == nullptr
		|| pUser->m_pKnightsUser == nullptr)
		return false;
	
	// Ensure users are refunded when they leave/are removed from a clan.
	// NOTE: If we bring back multiserver setups, this is going to cause synchronisation issues.
	uint32 nDonatedNP = pUser->m_pKnightsUser->nDonatedNP;
	if (nDonatedNP > 0)
		RefundDonatedNP(nDonatedNP, pUser);
	
	pUser->SetClanID(0);
	pUser->m_bFame = 0;

	pUser->m_pKnightsUser->Initialise();
	pUser->m_pKnightsUser = nullptr;

	if (!pUser->isClanLeader())
		pUser->SendClanUserStatusUpdate();

	return true;
}

/**
 * @brief	Refunds 30% of the user's donated NP.
 *
 * @param	nDonatedNP	The donated NP.
 * @param	pUser	  	The user's session, when refunding the user in-game.
 * 						Set to nullptr to indicate the use of the character's name
 * 						and consequently a database update instead.
 * @param	strUserID 	Logged out character's name. 
 * 						Used to refund logged out characters' national points 
 * 						when pUser is set to nullptr.
 */
void CKnights::RefundDonatedNP(uint32 nDonatedNP, CUser * pUser /*= nullptr*/, const char * strUserID /*= nullptr*/)
{
	nDonatedNP = (nDonatedNP * 30) / 100;

	// Remove the refunded NP from the clan fund
	m_nClanPointFund -= nDonatedNP;

	// If the player's logged in, just adjust their national points in-game.
	if (pUser != nullptr)
	{
		pUser->m_iLoyalty += nDonatedNP;
		return;
	}

	// For logged out players, we must update the player's national points in the database.
	Packet result(WIZ_KNIGHTS_PROCESS, uint8(KNIGHTS_REFUND_POINTS));
	result << strUserID << nDonatedNP;
	g_pMain->AddDatabaseRequest(result);
}

void CKnights::Disband(CUser *pLeader /*= nullptr*/)
{
	string clanNotice;
	g_pMain->GetServerResource(m_byFlag == ClanTypeTraining ? IDS_CLAN_DESTROY : IDS_KNIGHTS_DESTROY, 
		&clanNotice, m_strName.c_str());
	SendChat(clanNotice.c_str());

	foreach_array (i, m_arKnightsUser)
	{
		_KNIGHTS_USER *p = &m_arKnightsUser[i];
		if (!p->byUsed)
			continue;

		// If the user's logged in, handle the player data removal in-game.
		// It will be saved to the database when they log out.
		if (p->pSession != nullptr)
			RemoveUser(p->pSession);
		else
			RemoveUser(p->strUserName);

	}
	g_pMain->m_KnightsArray.DeleteData(m_sIndex);

	if (pLeader == nullptr)
		return;

	Packet result(WIZ_KNIGHTS_PROCESS, uint8(KNIGHTS_DESTROY));
	result << uint8(1);
	pLeader->Send(&result);
}

void CKnights::SendChat(const char * format, ...)
{
	char buffer[128];
	va_list ap;
	va_start(ap, format);
	vsnprintf(buffer, 128, format, ap);
	va_end(ap);

	Packet result;
	ChatPacket::Construct(&result, KNIGHTS_CHAT, buffer);
	Send(&result);
}

void CKnights::Send(Packet *pkt)
{
	foreach_array (i, m_arKnightsUser)
	{
		_KNIGHTS_USER *p = &m_arKnightsUser[i];
		if (p->byUsed && p->pSession != nullptr)
			p->pSession->Send(pkt);
	}
}

CKnights::~CKnights()
{
}
