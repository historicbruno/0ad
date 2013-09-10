#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Copyright (C) 2013 Wildfire Games.
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
"""

import logging
import time
from optparse import OptionParser

import sleekxmpp
from sleekxmpp.stanza import Iq
from sleekxmpp.xmlstream import ElementBase, register_stanza_plugin, ET
from sleekxmpp.xmlstream.handler import Callback
from sleekxmpp.xmlstream.matcher import StanzaPath

from LobbyRanking import session as db, Game, Player, PlayerInfo
from ELO import get_rating_adjustment
from config import default_rating, leaderboard_minimum_games, leaderboard_active_games

## Class that contains and manages leaderboard data ##
class LeaderboardList():
  def __init__(self):
    self.leaderboard = {}

    ## Add some Fake leaderboard figures for testing
    ## ***remove when actual user reporting works***
    self.leaderboard["666.666.666.666"] = {"name":"badmadblacksad", "rank":"1"}
    self.leaderboard["666.666.666.665"] = {"name":"Josh", "rank":"2"}
    self.leaderboard["666.666.666.664"] = {"name":"leper", "rank":"3"}
    self.leaderboard["666.666.666.663"] = {"name":"alpha123", "rank":"4"}
    self.leaderboard["666.666.666.662"] = {"name":"scythetwirler", "rank":"5"}
  def getOrCreatePlayer(self, JID):
    """
      Stores a player(JID) in the database if they
        don't yet exist.
      Returns either the newly created instance of
      the Player model, or the one that already
      exists in the database.
    """
    players = db.query(Player).filter_by(jid=JID)
    if not players.first():
      player = Player(jid=JID, rating=default_rating)
      db.add(player)
      db.commit()
      return player
    return players.first()
  def removePlayer(self, JID):
    """
      Remove a player(JID) from database.
      Returns the player that was removed, or None
      if that player didn't exist.
    """
    players = db.query(Player).filter_by(jid=JID)
    player = players.first()
    if not player:
      return None
    players.delete()
    return player
  def addGame(self, gamereport):
    """
      Adds a game (dictionary) to the database and
        updates the data on a player(JID) from game
        results.
      Returns the created Game object, or None if
      the creation failed for any reason.
      Side effects:
        Inserts a new Game instance into the database.
    """
    # Discard any games still in progress.
    if any(map(lambda state: state == 'active',
               dict.values(gamereport['playerStates']))):
      return None

    players = map(lambda jid: db.query(Player).filter_by(jid=jid).first(),
                  dict.keys(gamereport['playerStates']))

    winning_jid = list(dict.keys({jid: state for jid, state in
                                  gamereport['playerStates'].items()
                                  if state == 'won'}))[0]

    def get(stat, jid):
      return gamereport[stat]['playerStates'][jid]

    stats = {'civ': 'civs', 'foodGathered': 'foodGathered', 'foodUsed': 'foodUsed',
             'woodGathered': 'woodGathered', 'woodUsed': 'woodUsed',
             'stoneGathered': 'stoneGathered', 'stoneUsed': 'stoneUsed',
             'metalGathered': 'metalGathered', 'metalUsed': 'metalUsed'}

    playerInfos = []
    for player in players:
      jid = player.jid
      playerinfo = PlayerInfo(player=player)
      for dbname, reportname in stats.items():
        setattr(playerinfo, dbname, get(reportname, jid))
      playerInfos.append(playerinfo)

    game = Game(map=gamereport['mapName'], duration=int(gamereport['timeElapsed']))
    game.players.extend(players)
    game.player_info.extend(playerInfos)
    game.winner = db.query(Player).filter_by(jid=winning_jid).first()
    db.add(game)
    db.commit()
    return game
  def rateGame(self, game):
    """
      Takes a game with 2 players and alters their ratings
      based on the result of the game.
      Returns self.
      Side effects:
        Changes the game's players' ratings in the database.
    """
    player1 = game.players[0]
    player2 = game.players[1]
    # TODO: Support draws. Since it's impossible to draw in the game currently,
    # the database model, and therefore this code, requires a winner.
    # The Elo implementation does not, however.
    result = 1 if player1 == game.winner else -1
    rating_adjustment = get_rating_adjustment(player1.rating, player2.rating,
      player1.games.length, player2.games.length, result)
    if result == 1:
      player1.rating += rating_adjustment
      player2.rating -= rating_adjustment
    else:
      player1.rating -= rating_adjustment
      player2.rating += rating_adjustment
    return self
  def addAndRateGame(self, gamereport):
    """
      Calls addGame and if the game has only two
      players, also calls rateGame.
      Returns the result of addGame.
    """

    game = self.addGame(gamereport)
    if len(game.players) == 2:
      self.rateGame(game)
    return game

  def getBoard(self):
    """
      Returns a dictionary of player rankings to
        JIDs for sending.
    """
    return self.leaderboard

## Class to tracks all games in the lobby ##
class GameList():
  def __init__(self):
    self.gameList = {}
  def addGame(self, JID, dataTup):
    """
      Add a game
    """
    dataDic = self.TupToDic(dataTup)
    dataDic['players-init'] = dataDic['players']
    dataDic['nbp-init'] = dataDic['nbp']
    dataDic['state'] = 'init'
    self.gameList[str(JID)] = dataDic
  def removeGame(self, JID):
    """
      Remove a game attached to a JID
    """
    del self.gameList[str(JID)]
  def getAllGames(self):
    """
      Returns all games
    """
    return self.gameList
  def changeGameState(self, JID, data):
    """
      Switch game state between running and waiting
    """
    JID = str(JID)
    data = self.TupToDic(data)
    if JID in self.gameList:
      if self.gameList[JID]['nbp-init'] > data['nbp']:
        logging.debug("change game (%s) state from %s to %s", JID, self.gameList[JID]['state'], 'waiting')
        self.gameList[JID]['nbp'] = data['nbp']
        self.gameList[JID]['state'] = 'waiting'
      else:
        logging.debug("change game (%s) state from %s to %s", JID, self.gameList[JID]['state'], 'running')
        self.gameList[JID]['nbp'] = data['nbp']
        self.gameList[JID]['state'] = 'running'
  def TupToDic(self, Tuple):
    """
      Converts game recived as a tuple and converts it to a dictionary
    """
    return {'name':Tuple[0], 'ip':Tuple[1], 'state':Tuple[2], 'mapName':Tuple[3], 'mapSize':Tuple[4], 'victoryCondition':Tuple[5], 'nbp':Tuple[6], 'tnbp':Tuple[7], 'players':Tuple[8]}
  def DicToTup(self, Dict):
    """
      Converts game saved as a dictionary to a tuple formatted for sending
    """
    return Dict['name'], Dict['ip'], Dict['state'], Dict['nbp'], Dict['tnbp'], Dict['players'], Dict['mapName'], Dict['mapSize'], Dict['victoryCondition']
## Class for custom gamelist stanza extension ##
class GameListXmppPlugin(ElementBase):
  name = 'query'
  namespace = 'jabber:iq:gamelist'
  interfaces = set(('game', 'command'))
  sub_interfaces = interfaces
  plugin_attrib = 'gamelist'

  def addGame(self, data):
    itemXml = ET.Element("game", {"name":data["name"], "ip":data["ip"], "state":data["state"], "nbp":data["nbp"], "tnbp":data["tnbp"], "players":data["players"], "mapName":data["mapName"], "mapSize":data["mapSize"], "victoryCondition":data["victoryCondition"]})
    self.xml.append(itemXml)

  def getGame(self):
    game = self.xml.find('{%s}game' % self.namespace)
    return game.get("name"), game.get("ip"), game.get("state"), game.get("mapName"), game.get("mapSize"), game.get("victoryCondition"), game.get("nbp"), game.get("tnbp"), game.get("players")

## Class for custom boardlist stanza extension ##
class BoardListXmppPlugin(ElementBase):
  name = 'query'
  namespace = 'jabber:iq:boardlist'
  interfaces = set(('board'))
  sub_interfaces = interfaces
  plugin_attrib = 'boardlist'

  def addItem(self, name, rank):
    itemXml = ET.Element("board", {"name":name, "rank":rank})
    self.xml.append(itemXml)

  def getItem(self):
    board = self.xml.find('{%s}board' % self.namespace)
    return board.get("name"), board.get("ip")

  def getCommand(self):
    command = self.xml.find('{%s}command' % self.namespace)
    return command

class GameReportXmppPlugin(ElementBase):
  name = 'report'
  namespace = 'jabber:iq:gamereport'
  plugin_attrib = 'gamereport'

## Main class which handles IQ data and sends new data ##
class XpartaMuPP(sleekxmpp.ClientXMPP):
  """
  A simple list provider
  """
  def __init__(self, sjid, password, room, nick):
    sleekxmpp.ClientXMPP.__init__(self, sjid, password)
    self.sjid = sjid
    self.room = room
    self.nick = nick

    # Game collection
    self.gameList = GameList()

    # Init leaderboard object
    self.leaderboard = LeaderboardList()

    # Store mapping of nicks and XmppIDs, attached via presence stanza
    self.nicks = {}
    # Store client JIDs, attached via client request
    self.JIDs = []

    register_stanza_plugin(Iq, GameListXmppPlugin)
    register_stanza_plugin(Iq, BoardListXmppPlugin)
    register_stanza_plugin(Iq, GameReportXmppPlugin)

    self.register_handler(Callback('Iq Gamelist',
                                       StanzaPath('iq/gamelist'),
                                       self.iqhandler,
                                       instream=True))
    self.register_handler(Callback('Iq Boardlist',
                                       StanzaPath('iq/boardlist'),
                                       self.iqhandler,
                                       instream=True))
    self.register_handler(Callback('Iq GameReport',
                                       StanzaPath('iq/gamereport'),
                                       self.iqhandler,
                                       instream=True))
    self.add_event_handler("session_start", self.start)
    self.add_event_handler("session_start", self.start)
    self.add_event_handler("muc::%s::got_online" % self.room, self.muc_online)
    self.add_event_handler("muc::%s::got_offline" % self.room, self.muc_offline)

  def start(self, event):
    """
    Process the session_start event
    """
    self.plugin['xep_0045'].joinMUC(self.room, self.nick)
    self.send_presence()
    self.get_roster()
    logging.info("XpartaMuPP started")

  def muc_online(self, presence):
    """
    Process presence stanza from a chat room.
    """
    if presence['muc']['nick'] != self.nick:
      self.send_message(mto=presence['from'], mbody="Hello %s, welcome to the 0 A.D. lobby. Polish your weapons and get ready to fight!" %(presence['muc']['nick']), mtype='')
      # Store player JID with room prefix
      if str(presence['from']) not in self.nicks:
        self.nicks[str(presence['from'])] = presence['muc']['nick']
      logging.debug("Player '%s (%s - %s)' connected" %(presence['muc']['nick'], presence['muc']['jid'], presence['muc']['jid'].bare))
      # Send Gamelist to new player
      self.sendGameList(presence['from'])

  def muc_offline(self, presence):
    """
    Process presence stanza from a chat room.
    """
    # Clean up after a player leaves
    if presence['muc']['nick'] != self.nick:
      for JID in self.gameList.getAllGames():
        if self.gameList.getAllGames()[JID]['players'].split(',')[0] == presence['muc']['nick']:
          self.gameList.removeGame(JID)
          break
      del self.nicks[str(presence['from'])]

  def iqhandler(self, iq):
    """
    Handle the custom stanzas
      This method should be very robust because we could receive anything
    """
    if iq['type'] == 'error':
      logging.error('iqhandler error' + iq['error']['condition'])
      #self.disconnect()
    elif iq['type'] == 'get':
      """
      Request lists.
      """
      # We expect each client to register for future updates by sending at least one get request.
      try:
        self.leaderboard.getOrCreatePlayer(iq['from'])
        if iq['from'] not in self.JIDs:
          self.JIDs.append(iq['from'])
        self.sendGameList(iq['from'])
        self.sendBoardList(iq['from'])
      except:
        logging.error("Failed to send data to %s" % iq['from'].bare)
    elif iq['type'] == 'result':
      """
      Iq successfully received
      """
      pass
    elif iq['type'] == 'set':
      if 'gamelist' in iq.values:
        """
        Register-update / unregister a game
        """
        command = iq['gamelist']['command']
        if command == 'register':
          # Add game
          try:
            self.gameList.addGame(iq['from'], iq['gamelist']['game'])
          except:
            logging.error("Failed to process game registration data")
        elif command == 'unregister':
          # Remove game
          try:
            self.gameList.removeGame(iq['from'])
          except:
            logging.error("Failed to process game unregistration data")

        elif command == 'changestate':
          # Change game status (waiting/running)
          try:
            self.gameList.changeGameState(iq['from'], iq['gamelist']['game'])
          except:
            logging.error("Failed to process changestate data")
        else:
          logging.error("Failed to process command '%s' received from %s" % command, iq['from'].bare)
      elif 'gamereport' in iq.values:
        """
        Client is reporting end of game statistics
        """
        try:
          self.leaderboard.addAndRateGame(iq['from'], iq['gamereport']['game'])
        except:
          logging.error("Failed to update post-game statistics for %s" % iq['from'].bare)
    else:
       logging.error("Failed to process type '%s' received from %s" % iq['type'], iq['from'].bare)

  def sendGameList(self, to):
    """
    Send a massive stanza with the whole game list
    """
    ## Check recipient exists
    if to not in self.JIDs:
      logging.error("No player with the xmpp id '%s' known" % to.bare)
      return

    stz = GameListXmppPlugin()

    ## Pull games and add each to the stanza
    games = self.gameList.getAllGames()
    for JID in games:
      g = games[JID]
      # Only send the games that are in the 'init' state and games
      # that are in the 'waiting' state which the receiving player is in. TODO
      if g['state'] == 'init' or (g['state'] == 'waiting' and self.nicks[to] in g['players-init']):
        stz.addGame(g)

    ## Set additional IQ attributes
    iq = self.Iq()
    iq['type'] = 'result'
    iq['to'] = to
    iq.setPayload(stz)

    ## Try sending the stanza
    try:
      iq.send()
    except:
      logging.error("Failed to send game list")

  def sendBoardList(self, to):
    """
    Send the whole leaderboard list
    """
    ## Check recipiant exists
    if to not in self.JIDs:
      logging.error("No player with the XmPP ID '%s' known" % to.bare)
      return

    stz = BoardListXmppPlugin()

    ## Pull leaderboard data and add it to the stanza
    board = self.leaderboard.getBoard()
    for i in board:
      stz.addItem(board[i]['name'], board[i]['rank'])

    ## Set aditional IQ attributes
    iq = self.Iq()
    iq['type'] = 'result'
    iq['to'] = to
    iq.setPayload(stz)

    ## Try sending the stanza
    try:
      iq.send()
    except:
      logging.error("Failed to send leaderboard list")

## Main Program ##
if __name__ == '__main__':
  # Setup the command line arguments.
  optp = OptionParser()

  # Output verbosity options.
  optp.add_option('-q', '--quiet', help='set logging to ERROR',
                  action='store_const', dest='loglevel',
                  const=logging.ERROR, default=logging.INFO)
  optp.add_option('-d', '--debug', help='set logging to DEBUG',
                  action='store_const', dest='loglevel',
                  const=logging.DEBUG, default=logging.INFO)
  optp.add_option('-v', '--verbose', help='set logging to COMM',
                  action='store_const', dest='loglevel',
                  const=5, default=logging.INFO)

  # XpartaMuPP configuration options
  optp.add_option('-m', '--domain', help='set xpartamupp domain',
                  action='store', dest='xdomain',
                  default="lobby.wildfiregames.com")
  optp.add_option('-l', '--login', help='set xpartamupp login',
                  action='store', dest='xlogin',
                  default="xpartamupp")
  optp.add_option('-p', '--password', help='set xpartamupp password',
                  action='store', dest='xpassword',
                  default="XXXXXX")
  optp.add_option('-n', '--nickname', help='set xpartamupp nickname',
                  action='store', dest='xnickname',
                  default="WFGbot")
  optp.add_option('-r', '--room', help='set muc room to join',
                  action='store', dest='xroom',
                  default="arena")

  opts, args = optp.parse_args()

  # Setup logging.
  logging.basicConfig(level=opts.loglevel,
                      format='%(levelname)-8s %(message)s')

  # XpartaMuPP
  xmpp = XpartaMuPP(opts.xlogin+'@'+opts.xdomain+'/CC', opts.xpassword, opts.xroom+'@conference.'+opts.xdomain, opts.xnickname)
  xmpp.register_plugin('xep_0030') # Service Discovery
  xmpp.register_plugin('xep_0004') # Data Forms
  xmpp.register_plugin('xep_0045') # Multi-User Chat	# used
  xmpp.register_plugin('xep_0060') # PubSub
  xmpp.register_plugin('xep_0199') # XMPP Ping

  if xmpp.connect():
    xmpp.process(block=False)
    while True:
      time.sleep(5)
      logging.debug('Sending GameList')
      for to in xmpp.JIDs:
        xmpp.sendGameList(to)
  else:
    logging.error("Unable to connect")
