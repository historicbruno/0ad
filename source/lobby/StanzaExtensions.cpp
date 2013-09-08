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
#include "GameReportItemData.h"

#include <gloox/rostermanager.h>

/****************************************************
 * GameReport, fairly generic custom stanza extension used
 * to report game statistics.
 */
GameReport::GameReport( const gloox::Tag* tag ):StanzaExtension(ExtGameReport)
{
	if( !tag || tag->name() != "report" || tag->xmlns() != XMLNS_GAMEREPORT )
		return;
	// TODO if we want to handle receiving this stanza extension.
};

// Function used by gloox to serialize the object into XML for sending
gloox::Tag* GameReport::tag() const
{
	gloox::Tag* t = new gloox::Tag( "report" );
	t->setXmlns( XMLNS_GAMEREPORT );

	std::list<GameReportItemData*>::const_iterator it = GameReportIQ.begin();
	for( ; it != GameReportIQ.end(); ++it )
		t->addChild( (*it)->tag() );

	return t;
}

/* Required by gloox, used to find the extension in a recived IQ */
const std::string& GameReport::filterString() const
{
	static const std::string filter = "/iq/report[@xmlns='" + XMLNS_GAMEREPORT + "']";
	return filter;
}

/* Required by gloox */
gloox::StanzaExtension* GameReport::clone() const
{
	GameReport* q = new GameReport();
	return q;
}

/******************************************************
 *  BoardListQuery, custom IQ Stanza, used solely to
 *  request and receive leaderboard from server. This
 *  could probably be cleaned up some.
 */
BoardListQuery::BoardListQuery( const gloox::Tag* tag )
: StanzaExtension( ExtBoardListQuery )
{
	if( !tag || tag->name() != "query" || tag->xmlns() != XMLNS_BOARDLIST )
		return;

	m_IQBoardList = tag->findTagList( "query/board" );
}

BoardListQuery::~BoardListQuery()
{
	gloox::util::clearList( m_IQBoardList );
}

const std::string& BoardListQuery::filterString() const
{
	static const std::string filter = "/iq/query[@xmlns='" + XMLNS_BOARDLIST + "']";
	return filter;
}

// Function used by gloox to serialize the object into XML for sending
gloox::Tag* BoardListQuery::tag() const
{
	gloox::Tag* t = new gloox::Tag( "query" );
	t->setXmlns( XMLNS_BOARDLIST );

	std::list<const PlayerData*>::const_iterator it = m_IQBoardList.begin();
	for( ; it != m_IQBoardList.end(); ++it )
		t->addChild( (*it)->clone() );

	return t;
}

gloox::StanzaExtension* BoardListQuery::clone() const
{
	BoardListQuery* q = new BoardListQuery();

	return q;
}

const std::list<const PlayerData*>& BoardListQuery::boardList() const
{
	return m_IQBoardList;
}

/*
 *  GameListQuery, custom IQ Stanza
 */

GameListQuery::GameListQuery( const gloox::Tag* tag )
: StanzaExtension( ExtGameListQuery )
{
	if( !tag || tag->name() != "query" || tag->xmlns() != XMLNS_GAMELIST )
		return;

	const gloox::Tag* c = tag->findTag( "query/game" );
	if (c)
		m_command = c->cdata();

    m_IQGameList = tag->findTagList( "query/game" );
}

GameListQuery::~GameListQuery()
{
	gloox::util::clearList( m_IQGameList );
}

const std::string& GameListQuery::filterString() const
{
	static const std::string filter = "/iq/query[@xmlns='" + XMLNS_GAMELIST + "']";
	return filter;
}

// Function used by gloox to serialize the object into XML for sending
gloox::Tag* GameListQuery::tag() const
{
	gloox::Tag* t = new gloox::Tag( "query" );
	t->setXmlns( XMLNS_GAMELIST );
/*
	RosterData::const_iterator it = m_roster.begin();
	for( ; it != m_roster.end(); ++it )
		t->addChild( (*it)->tag() );
*/

	// register / unregister command
	if(!m_command.empty())
		t->addChild(new gloox::Tag("command", m_command));

	std::list<const GameData*>::const_iterator it = m_IQGameList.begin();
	for( ; it != m_IQGameList.end(); ++it )
		t->addChild( (*it)->clone() );

	return t;
}

gloox::StanzaExtension* GameListQuery::clone() const
{
	GameListQuery* q = new GameListQuery();

	return q;
}

const std::list<const GameData*>& GameListQuery::gameList() const
{
	return m_IQGameList;
}
