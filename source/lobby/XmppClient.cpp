/* Copyright (C) 2013 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "precompiled.h"
#include "XmppClient.h"
#include "StanzaExtensions.h"

//debug
#include <iostream>

//Gloox
#include <gloox/rostermanager.h>
#include <gloox/rosteritem.h>
#include <gloox/error.h>

//Game - script
#include "scriptinterface/ScriptInterface.h"

#include "ps/CLogger.h"

//Configuration
#include "ps/ConfigDB.h"

using namespace gloox;

//global
XmppClient *g_XmppClient = NULL;
bool g_rankedGame = false;

//debug
#if 1
#define DbgXMPP(x)
#else
#define DbgXMPP(x) std::cout << x << std::endl;
#endif

//Hack
#if 1
#if OS_WIN
const std::string gloox::EmptyString = "";
#endif
#endif

//utils
std::string StanzaErrorToString(StanzaError& err)
{
	std::string msg;
#define CASE(X, Y) case X: return Y
	switch (err)
	{
	CASE(StanzaErrorBadRequest, "Bad request");
	CASE(StanzaErrorConflict, "Player name already in use");
	CASE(StanzaErrorFeatureNotImplemented, "Feature not implemented");
	CASE(StanzaErrorForbidden, "Forbidden");
	CASE(StanzaErrorGone, "Recipient or server gone");
	CASE(StanzaErrorInternalServerError, "Internal server error");
	CASE(StanzaErrorItemNotFound, "Item not found");
	CASE(StanzaErrorJidMalformed, "Jid malformed");
	CASE(StanzaErrorNotAcceptable, "Not acceptable");
	CASE(StanzaErrorNotAllowed, "Not allowed");
	CASE(StanzaErrorNotAuthorized, "Not authorized");
	CASE(StanzaErrorNotModified, "Not modified");
	CASE(StanzaErrorPaymentRequired, "Payment required");
	CASE(StanzaErrorRecipientUnavailable, "Recipient unavailable");
	CASE(StanzaErrorRedirect, "Redirect");
	CASE(StanzaErrorRegistrationRequired, "Registration required");
	CASE(StanzaErrorRemoteServerNotFound, "Remote server not found");
	CASE(StanzaErrorRemoteServerTimeout, "Remote server timeout");
	CASE(StanzaErrorResourceConstraint, "Resource constraint");
	CASE(StanzaErrorServiceUnavailable, "Service unavailable");
	CASE(StanzaErrorSubscribtionRequired, "Subscribtion Required");
	CASE(StanzaErrorUndefinedCondition, "Undefined condition");
	CASE(StanzaErrorUnexpectedRequest, "Unexpected request");
	CASE(StanzaErrorUnknownSender, "Unknown sender");
	default:
		return "Error undefined";
	}
#undef CASE
}

XmppClient::XmppClient(ScriptInterface& scriptInterface, std::string sUsername, std::string sPassword, std::string sRoom, std::string sNick, bool regOpt)
	: m_ScriptInterface(scriptInterface), _client(NULL), _mucRoom(NULL), _registration(NULL), _username(sUsername), _password(sPassword), _nick(sNick)
{
	// Read lobby configuration from default.cfg
	std::string sServer;
	std::string sXpartamupp;
	CFG_GET_VAL("lobby.server", String, sServer);
	CFG_GET_VAL("lobby.xpartamupp", String, sXpartamupp);

	_xpartamuppId = sXpartamupp+std::string("@")+sServer+std::string("/CC");
	JID clientJid(sUsername+std::string("@")+sServer+std::string("/0ad"));
	JID roomJid(sRoom+std::string("@conference.")+sServer+std::string("/")+sNick);

	// If we are connecting, use the full jid and a password
	// If we are registering, only use the server name
	if(!regOpt)
		_client = new Client(clientJid, sPassword);
	else
		_client = new Client(sServer);

	// Disable TLS as we haven't set a certificate on the server yet
	_client->setTls(TLSDisabled);

	// Disable use of the SASL PLAIN mechanism, to prevent leaking credentials
	// if the server doesn't list any supported SASL mechanism or the response
	// has been modified to exclude those.
	const int mechs = SaslMechAll ^ SaslMechPlain;
	_client->setSASLMechanisms(mechs);

	_client->registerConnectionListener( this );
	_client->setPresence(Presence::Available, -1);
	_client->disco()->setVersion( "Pyrogenesis", "0.0.15" );
	_client->disco()->setIdentity( "client", "bot" );
	_client->setCompression(false);

	_client->registerStanzaExtension( new GameListQuery() );
	_client->registerIqHandler( this, ExtGameListQuery);

	_client->registerStanzaExtension( new BoardListQuery() );
	_client->registerIqHandler( this, ExtBoardListQuery);

	_client->registerMessageHandler( this );

	// Uncomment to see the raw stanzas
	//_client->logInstance().registerLogHandler( LogLevelDebug, LogAreaAll, this );

	if (!regOpt)
	{
		// Create a Multi User Chat Room
		_mucRoom = new MUCRoom(_client, roomJid, this, 0);
		// Disable the history because its anoying
		_mucRoom->setRequestHistory(0, MUCRoom::HistoryMaxStanzas);
	}
	else
	{
		// Registration
		_registration = new Registration( _client );
		_registration->registerRegistrationHandler( this );
	}
}

XmppClient::~XmppClient()
{
	DbgXMPP("XmppClient destroyed");
	delete _registration;
	delete _mucRoom;
	delete _client;
}

// Game - script
ScriptInterface& XmppClient::GetScriptInterface()
{
	return m_ScriptInterface;
}

//Network
void XmppClient::connect()
{
	_client->connect(false);
}

void XmppClient::disconnect()
{
	_client->disconnect();
}

void XmppClient::recv()
{
	_client->recv(1);
}

/*
 *  MUC Handlers
 */
void XmppClient::handleMUCParticipantPresence(gloox::MUCRoom*, const gloox::MUCRoomParticipant participant, const gloox::Presence& presence)
{
	//std::string jid = participant.jid->full();
	std::string nick = participant.nick->resource();
	gloox::Presence::PresenceType presenceType = presence.presence();
	if (presenceType == Presence::Unavailable)
	{
		if (!participant.newNick.empty() && (participant.flags & (UserNickChanged | UserSelf)))
		{
			// we have a nick change
			m_PlayerMap[participant.newNick] = Presence::Unavailable;
			CreateSimpleMessage("muc", nick, "nick", participant.newNick);
		}
		else
			CreateSimpleMessage("muc", nick, "leave");

		DbgXMPP(nick << " left the room");
		m_PlayerMap.erase(nick);
	}
	else
	{
		if (m_PlayerMap.find(nick) == m_PlayerMap.end())
			CreateSimpleMessage("muc", nick, "join");
		else
			CreateSimpleMessage("muc", nick, "presence");

		DbgXMPP(nick << " is in the room, presence : " << (int)presenceType);
		m_PlayerMap[nick] = presenceType;
	}
}

void XmppClient::handleMUCMessage( MUCRoom*, const Message& msg, bool )
{
	DbgXMPP(msg.from().resource() << " said " << msg.body());

	CScriptValRooted message;
	m_ScriptInterface.Eval("({ 'type':'mucmessage'})", message);
	m_ScriptInterface.SetProperty(message.get(), "from", msg.from().resource());
	m_ScriptInterface.SetProperty(message.get(), "text", msg.body());
	PushGuiMessage(message);
}

void XmppClient::handleMUCError(gloox::MUCRoom*, gloox::StanzaError err)
{
	std::string msg = StanzaErrorToString(err);
	CreateSimpleMessage("system", msg, "error");
}

/*
 *  Log (debug) Handler
 */
void XmppClient::handleLog( LogLevel level, LogArea area, const std::string& message )
{
	std::cout << "log: level: " << level << ", area: " << area << ", message: " << message << std::endl;
}

/*
 *  IQ Handler
 */
bool XmppClient::handleIq( const IQ& iq )
{
	DbgXMPP("handleIq [" << iq.tag()->xml() << "]");

	if(iq.subtype() == gloox::IQ::Result)
	{
		const GameListQuery* gq = iq.findExtension<GameListQuery>( ExtGameListQuery );
		const BoardListQuery* bq = iq.findExtension<BoardListQuery>( ExtBoardListQuery );
		if(gq)
		{
			m_GameList.clear();
			std::list<const GameData*>::const_iterator it = gq->m_GameList.begin();
			for(; it != gq->m_GameList.end(); ++it)
			{
				m_GameList.push_back(*it);
			}
			CreateSimpleMessage("system", "gamelist updated", "internal");
		}
		if(bq)
		{
			m_BoardList.clear();
			std::list<const PlayerData*>::const_iterator it = bq->m_BoardList.begin();
			for(; it != bq->m_BoardList.end(); ++it)
			{
				m_BoardList.push_back(*it);
			}
			CreateSimpleMessage("system", "boardlist updated", "internal");
		}
	}
	else if(iq.subtype() == gloox::IQ::Error)
	{
		StanzaError err = iq.error()->error();
		std::string msg = StanzaErrorToString(err);
		CreateSimpleMessage("system", msg, "error");
	}
	else
	{
		CreateSimpleMessage("system", std::string("unknown subtype : ") + iq.tag()->name(), "error");
	}

	return true;
}

/*
 *  Connection Handlers
 */
void XmppClient::onConnect()
{
	if (_mucRoom)
	{
		CreateSimpleMessage("system", "connected");
		_mucRoom->join();
		SendIqGetGameList();
		SendIqGetBoardList();
	}

	if (_registration)
		_registration->fetchRegistrationFields();
}

void XmppClient::onDisconnect( ConnectionError e )
{
	// Make sure we properly leave the room so than
	// everything work if we decide to come back later
	if (_mucRoom)
		_mucRoom->leave();

	if( e == ConnAuthenticationFailed )
		CreateSimpleMessage("system", "authentication failed", "error");
	else
		CreateSimpleMessage("system", "disconnected");

	m_PlayerMap.clear();
	m_GameList.clear();
	m_BoardList.clear();
}

bool XmppClient::onTLSConnect( const CertInfo& info )
{
	UNUSED2(info);
	DbgXMPP("onTLSConnect");
	DbgXMPP("status: " << info.status << "\nissuer: " << info.issuer << "\npeer: " << info.server << "\nprotocol: " << info.protocol << "\nmac: " << info.mac << "\ncipher: " << info.cipher << "\ncompression: " << info.compression );
	return true;
}

/*
 *  Requests
 */

/* Request GameList from cloud */
void XmppClient::SendIqGetGameList()
{
	JID xpartamuppJid(_xpartamuppId);

	// Send IQ
	IQ iq(gloox::IQ::Get, xpartamuppJid);
	iq.addExtension( new GameListQuery() );
	DbgXMPP("SendIqGetGameList [" << iq.tag()->xml() << "]");
	_client->send(iq);
}

/* Request BoardList from cloud */
void XmppClient::SendIqGetBoardList()
{
	JID xpartamuppJid(_xpartamuppId);

	// Send IQ
	IQ iq(gloox::IQ::Get, xpartamuppJid);
	iq.addExtension( new BoardListQuery() );
	DbgXMPP("SendIqGetBoardList [" << iq.tag()->xml() << "]");
	_client->send(iq);
}

/* Send game report */
void XmppClient::SendIqGameReport(CScriptVal data)
{
#define SEND_STAT(stat) \
	std::string stat; \
	m_ScriptInterface.GetProperty( dataval, #stat, (stat) ); \
	report->addAttribute( #stat, (stat) );
	JID xpartamuppJid(_xpartamuppId);

	// Convert the values from the CScriptVal to std
	jsval dataval = data.get();
	// Compose IQ
	GameReport* game = new GameReport();
	GameReportData *report = new GameReportData( "game" );
	SEND_STAT( timeElapsed );
	SEND_STAT( playerStates );
	SEND_STAT( playerID );
	SEND_STAT( matchID );
	SEND_STAT( civs );
	SEND_STAT( mapName );
	SEND_STAT( foodGathered );
	SEND_STAT( woodGathered );
	SEND_STAT( stoneGathered );
	SEND_STAT( metalGathered );
	SEND_STAT( foodUsed );
	SEND_STAT( woodUsed );
	SEND_STAT( stoneUsed );
	SEND_STAT( metalUsed );
	SEND_STAT( unitsLost );
	SEND_STAT( unitsTrained );
	SEND_STAT( enemyUnitsKilled );
	SEND_STAT( buildingsLost );
	SEND_STAT( buildingsConstructed );
	SEND_STAT( enemyBuildingsDestroyed );
	SEND_STAT( foodBought );
	SEND_STAT( woodBought );
	SEND_STAT( stoneBought );
	SEND_STAT( metalBought );
	SEND_STAT( foodSold );
	SEND_STAT( woodSold );
	SEND_STAT( stoneSold );
	SEND_STAT( metalSold );
	SEND_STAT( tributeSent );
	SEND_STAT( tributeReceived );
	SEND_STAT( precentMapExplored );
	SEND_STAT( civCentersBuilt );
	SEND_STAT( enemyCivCentersDestroyed );
	SEND_STAT( treasuresCollected );
	SEND_STAT( tradeIncome );
	game->m_GameReport.push_back( report );

	// Send IQ
	IQ iq(gloox::IQ::Set, xpartamuppJid);
	iq.addExtension(game);
	DbgXMPP("SendGameReport [" << iq.tag()->xml() << "]");
	_client->send(iq);
#undef SEND_STAT
};

/* Register a game */
void XmppClient::SendIqRegisterGame(CScriptVal data)
{
#define SEND_STAT(stat) \
	m_ScriptInterface.GetProperty( dataval, #stat, (stat) ); \
	game->addAttribute( #stat, (stat) );
	JID xpartamuppJid(_xpartamuppId);

	std::string name, mapName, mapSize, mapType, victoryCondition, nbp, tnbp, players;
	jsval dataval = data.get();

	// Send IQ
	GameListQuery* g = new GameListQuery();
	g->m_Command = "register";
	GameData *game = new GameData( "game" );
	// This fake ip will be overwritten by the ip stamp XMPP module.
	game->addAttribute( "ip", "fake" );
	SEND_STAT( name );
	SEND_STAT( mapName );
	SEND_STAT( mapSize );
	SEND_STAT( mapType );
	SEND_STAT( victoryCondition );
	SEND_STAT( nbp );
	SEND_STAT( players );
	SEND_STAT( tnbp );
	g->m_GameList.push_back( game );

	IQ iq( gloox::IQ::Set, xpartamuppJid );
	iq.addExtension( g );
	DbgXMPP( "SendIqRegisterGame [" << iq.tag()->xml() << "]" );
	_client->send( iq );
#undef SEND_STAT
}

/* Unregister a game */
void XmppClient::SendIqUnregisterGame()
{
	JID xpartamuppJid( _xpartamuppId );

	// Send IQ
	GameListQuery* g = new GameListQuery();
	g->m_Command = "unregister";
	g->m_GameList.push_back( new GameData( "game" ) );

	IQ iq( gloox::IQ::Set, xpartamuppJid );
	iq.addExtension( g );
	DbgXMPP("SendIqUnregisterGame [" << iq.tag()->xml() << "]");
	_client->send( iq );
}

/* Change the state of a registered game to 'running' or 'waiting' - it's Xpartamupp that decide the game's state
   comparing the current number of player 'nbp' to the number of players when the game was last registered. */
void XmppClient::SendIqChangeStateGame(std::string nbp, std::string players)
{
	JID xpartamuppJid(_xpartamuppId);

	// Send IQ
	GameListQuery* g = new GameListQuery();
	g->m_Command = "changestate";
	GameData* game = new GameData("game");
	game->addAttribute("nbp", nbp);
	game->addAttribute("players", players);
	g->m_GameList.push_back(game);

	IQ iq(gloox::IQ::Set, xpartamuppJid);
	iq.addExtension( g );
	DbgXMPP("SendIqChangeStateGame [" << iq.tag()->xml() << "]");
	_client->send(iq);
}

/*
 *  Registration
 */
void XmppClient::handleRegistrationFields( const JID& /*from*/, int fields, std::string )
{
	RegistrationFields vals;
	vals.username = _username;
	vals.password = _password;
	_registration->createAccount(fields, vals);
}

void XmppClient::handleRegistrationResult( const JID& /*from*/, RegistrationResult result )
{
	if (result == gloox::RegistrationSuccess)
	{
		CreateSimpleMessage("system", "registered");
	}
	else
	{
	std::string msg;
#define CASE(X, Y) case X: msg = Y; break
		switch(result)
		{
		CASE(RegistrationNotAcceptable, "Registration not acceptable");
		CASE(RegistrationConflict, "Registration conflict");
		CASE(RegistrationNotAuthorized, "Registration not authorized");
		CASE(RegistrationBadRequest, "Registration bad request");
		CASE(RegistrationForbidden, "Registration forbidden");
		CASE(RegistrationRequired, "Registration required");
		CASE(RegistrationUnexpectedRequest, "Registration unexpected request");
		CASE(RegistrationNotAllowed, "Registration not allowed");
		default: msg = "Registration unknown error";
		}
#undef CASE
		CreateSimpleMessage("system", msg, "error");
	}
	disconnect();
}

void XmppClient::handleAlreadyRegistered( const JID& /*from*/ )
{
	DbgXMPP("the account already exists");
}

void XmppClient::handleDataForm( const JID& /*from*/, const DataForm& /*form*/ )
{
	DbgXMPP("dataForm received");
}

void XmppClient::handleOOB( const JID& /*from*/, const OOB& /* oob */ )
{
	DbgXMPP("OOB registration requested");
}

/**
  * Message
  */
void XmppClient::handleMessage( const Message& msg, MessageSession * /*session*/ )
{
	DbgXMPP("type " << msg.subtype() << ", subject " << msg.subject().c_str()
	  << ", message " << msg.body().c_str() << ", thread id " << msg.thread().c_str());

	CScriptValRooted message;
	m_ScriptInterface.Eval("({ 'type':'message'})", message);
	m_ScriptInterface.SetProperty(message.get(), "from", msg.from().username());
	m_ScriptInterface.SetProperty(message.get(), "text", msg.body());
	PushGuiMessage(message);
}

/**
  * Requests from GUI
  */
/* Get list of players */
CScriptValRooted XmppClient::GUIGetPlayerList()
{
	std::string presence;
	CScriptValRooted playerList;
	m_ScriptInterface.Eval("({})", playerList);
	for(std::map<std::string, Presence::PresenceType>::const_iterator it = m_PlayerMap.begin(); it != m_PlayerMap.end(); ++it)
	{
		CScriptValRooted player;
		GetPresenceString(it->second, presence);
		m_ScriptInterface.Eval("({})", player);
		m_ScriptInterface.SetProperty(player.get(), "name", it->first.c_str());
		m_ScriptInterface.SetProperty(player.get(), "presence", presence.c_str());

		m_ScriptInterface.SetProperty(playerList.get(), it->first.c_str(), player);
	}

	return playerList;
}

/* Get game listing */
CScriptValRooted XmppClient::GUIGetGameList()
{
	CScriptValRooted gameList;
	m_ScriptInterface.Eval("([])", gameList);
	for(std::list<const GameData*>::const_iterator it = m_GameList.begin(); it !=m_GameList.end(); ++it)
	{
		CScriptValRooted game;
		m_ScriptInterface.Eval("({})", game);

		const char* stats[] = { "name", "ip", "state", "nbp", "tnbp", "players", "mapName", "mapSize", "mapType", "victoryCondition" };
		short stats_length = 10;
		for (short i = 0; i < stats_length; i++)
			m_ScriptInterface.SetProperty(game.get(), stats[i], (*it)->findAttribute(stats[i]).c_str());

		m_ScriptInterface.CallFunctionVoid(gameList.get(), "push", game);
	}

	return gameList;
}

/* Get leaderboard data */
CScriptValRooted XmppClient::GUIGetBoardList()
{
	CScriptValRooted boardList;
	m_ScriptInterface.Eval("([])", boardList);
	for(std::list<const PlayerData*>::const_iterator it = m_BoardList.begin(); it != m_BoardList.end(); ++it)
	{
		CScriptValRooted board;
		m_ScriptInterface.Eval("({})", board);

		const char* attributes[] = { "name", "rank", "rating" };
		short attributes_length = 3;
		for (short i = 0; i < attributes_length; i++)
			m_ScriptInterface.SetProperty(board.get(), attributes[i], (*it)->findAttribute(attributes[i]).c_str());

		m_ScriptInterface.CallFunctionVoid(boardList.get(), "push", board);
	}

	return boardList;
}


/* Messages */
CScriptValRooted XmppClient::GuiPollMessage()
{
	if (m_GuiMessageQueue.empty())
		return CScriptValRooted();

	CScriptValRooted r = m_GuiMessageQueue.front();
	m_GuiMessageQueue.pop_front();
	return r;
}

void XmppClient::SendMUCMessage(std::string message)
{
	_mucRoom->send(message);
}

void XmppClient::PushGuiMessage(const CScriptValRooted& message)
{
	ENSURE(!message.undefined());

	m_GuiMessageQueue.push_back(message);
}

void XmppClient::CreateSimpleMessage(std::string type, std::string text, std::string level, std::string data)
{
	CScriptValRooted message;
	m_ScriptInterface.Eval("({})", message);
	m_ScriptInterface.SetProperty(message.get(), "type", type);
	m_ScriptInterface.SetProperty(message.get(), "level", level);
	m_ScriptInterface.SetProperty(message.get(), "text", text);
	m_ScriptInterface.SetProperty(message.get(), "data", data);
	PushGuiMessage(message);
}

// Request nick change, real change via mucRoomHandler
void XmppClient::SetNick(const std::string& nick)
{
	_mucRoom->setNick(nick);
}

void XmppClient::GetNick(std::string& nick)
{
	nick = _mucRoom->nick();
}

void XmppClient::kick(const std::string& nick, const std::string& reason)
{
	_mucRoom->kick(nick, reason);
}

void XmppClient::ban(const std::string& nick, const std::string& reason)
{
	_mucRoom->ban(nick, reason);
}

void XmppClient::SetPresence(const std::string& presence)
{
#define IF(x,y) if (presence == x) _mucRoom->setPresence(Presence::y)
	IF("available", Available);
	else IF("chat", Chat);
	else IF("away", Away);
	else IF("playing", DND);
	else IF("gone", XA);
	else IF("offline", Unavailable);
	// The others are not to be set
#undef IF
	else LOGERROR(L"Unknown presence '%hs'", presence.c_str());
}

void XmppClient::GetPresence(const std::string& nick, std::string& presence)
{
	if (m_PlayerMap.find(nick) != m_PlayerMap.end())
		GetPresenceString(m_PlayerMap[nick], presence);
	else
		presence = "offline";
}

void XmppClient::GetPresenceString(const Presence::PresenceType p, std::string& presence) const
{
	switch(p)
	{
#define CASE(x,y) case Presence::x: presence = y; break
	CASE(Available, "available");
	CASE(Chat, "chat");
	CASE(Away, "away");
	CASE(DND, "playing");
	CASE(XA, "gone");
	CASE(Unavailable, "offline");
	CASE(Probe, "probe");
	CASE(Error, "error");
	CASE(Invalid, "invalid");
	default:
		LOGERROR(L"Unknown presence type '%d'", (int)p);
		break;
#undef CASE
	}
}
