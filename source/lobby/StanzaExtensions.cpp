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
#include "StanzaExtensions.h"

/******************************************************
 * GameReport, fairly generic custom stanza extension used
 * to report game statistics.
 */
GameReport::GameReport( const gloox::Tag* tag ):StanzaExtension( ExtGameReport )
{
	if( !tag || tag->name() != "report" || tag->xmlns() != XMLNS_GAMEREPORT )
		return;
	// TODO if we want to handle receiving this stanza extension.
};

/**
 * Required by gloox, used to serialize the GameReport into XML for sending.
 */
gloox::Tag* GameReport::tag() const
{
	gloox::Tag* t = new gloox::Tag( "report" );
	t->setXmlns( XMLNS_GAMEREPORT );

	std::list<const GameReportData*>::const_iterator it = m_GameReport.begin();
	for( ; it != m_GameReport.end(); ++it )
		t->addChild( (*it)->clone() );

	return t;
}

/**
 * Required by gloox, used to find the GameReport element in a recived IQ.
 */
const std::string& GameReport::filterString() const
{
	static const std::string filter = "/iq/report[@xmlns='" + XMLNS_GAMEREPORT + "']";
	return filter;
}

gloox::StanzaExtension* GameReport::clone() const
{
	GameReport* q = new GameReport();
	return q;
}

/******************************************************
 * BoardListQuery, custom IQ Stanza, used solely to
 * request and receive leaderboard data from server.
 */
BoardListQuery::BoardListQuery( const gloox::Tag* tag ):StanzaExtension( ExtBoardListQuery )
{
	if( !tag || tag->name() != "query" || tag->xmlns() != XMLNS_BOARDLIST )
		return;

	const gloox::ConstTagList boardTags = tag->findTagList( "query/board" );
	gloox::ConstTagList::const_iterator it = boardTags.begin();
	for ( ; it != boardTags.end(); ++it )
		m_BoardList.push_back( (*it)->clone() );
}

/**
 * Required by gloox, used to find the BoardList element in a recived IQ.
 */
const std::string& BoardListQuery::filterString() const
{
	static const std::string filter = "/iq/query[@xmlns='" + XMLNS_BOARDLIST + "']";
	return filter;
}

/**
 * Required by gloox, used to serialize the BoardList request into XML for sending.
 */
gloox::Tag* BoardListQuery::tag() const
{
	gloox::Tag* t = new gloox::Tag( "query" );
	t->setXmlns( XMLNS_BOARDLIST );

	std::list<const PlayerData*>::const_iterator it = m_BoardList.begin();
	for( ; it != m_BoardList.end(); ++it )
		t->addChild( (*it)->clone() );

	return t;
}

gloox::StanzaExtension* BoardListQuery::clone() const
{
	BoardListQuery* q = new BoardListQuery();
	return q;
}

BoardListQuery::~BoardListQuery()
{
	m_BoardList.clear();
}

/******************************************************
 * GameListQuery, custom IQ Stanza, used to receive
 * the listing of games from the server, and register/
 * unregister/changestate games on the server. 
 */
GameListQuery::GameListQuery( const gloox::Tag* tag ):StanzaExtension( ExtGameListQuery )
{
	if( !tag || tag->name() != "query" || tag->xmlns() != XMLNS_GAMELIST )
		return;

	const gloox::Tag* c = tag->findTag( "query/game" );
	if (c)
		m_Command = c->cdata();

	const gloox::ConstTagList games = tag->findTagList( "query/game" );
	gloox::ConstTagList::const_iterator it = games.begin();
	for ( ; it != games.end(); ++it )
		m_GameList.push_back( (*it)->clone() );
}

/**
 * Required by gloox, used to find the GameList element in a recived IQ.
 */
const std::string& GameListQuery::filterString() const
{
	static const std::string filter = "/iq/query[@xmlns='" + XMLNS_GAMELIST + "']";
	return filter;
}

/**
 * Required by gloox, used to serialize the game object into XML for sending.
 */
gloox::Tag* GameListQuery::tag() const
{
	gloox::Tag* t = new gloox::Tag( "query" );
	t->setXmlns( XMLNS_GAMELIST );

	// Check for register / unregister command
	if(!m_Command.empty())
		t->addChild(new gloox::Tag("command", m_Command));

	std::list<const GameData*>::const_iterator it = m_GameList.begin();
	for( ; it != m_GameList.end(); ++it )
		t->addChild( (*it)->clone() );

	return t;
}

gloox::StanzaExtension* GameListQuery::clone() const
{
	GameListQuery* q = new GameListQuery();
	return q;
}

GameListQuery::~GameListQuery()
{
	m_GameList.clear();
}
