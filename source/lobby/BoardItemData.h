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

#ifndef BOARDITEMDATA_H
#define BOARDITEMDATA_H

#include <string>

class BoardItemData
{
	friend class XmppClient;
	friend class BoardListQuery;
public:
	BoardItemData() {}

	virtual ~BoardItemData() {}

	gloox::Tag* tag() const
	{
		gloox::Tag* i = new gloox::Tag( "board" );

		i->addAttribute( "name", m_name );
		i->addAttribute( "rank", m_rank );

		return i;
	}

protected:
	std::string m_name;
	std::string m_rank;
};

#endif
