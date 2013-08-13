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

import sys
import logging
import time
from optparse import OptionParser

import sleekxmpp
from sleekxmpp.stanza import Iq
from sleekxmpp.stanza import *
from sleekxmpp.xmlstream import ElementBase, register_stanza_plugin, ET
from sleekxmpp.xmlstream.handler import Callback
from sleekxmpp.xmlstream.matcher import StanzaPath

from xml.dom.minidom import Document

## Configuration ##
configDEMOModOn = False
## !Configuration ##

## Class for games in gamelist ##
class BasicGameList(ElementBase):
  name = 'query'
  namespace = 'jabber:iq:gamelist'
  interfaces = set(('game', 'command'))
  sub_interfaces = interfaces
  plugin_attrib = 'gamelist'

  def addGame(self, name, ip, state, mapName, mapSize, victoryCondition, nbp, tnbp, players):
    itemXml = ET.Element("game", {"name":name, "ip":ip, "state": state, "nbp":nbp, "tnbp":tnbp, "players":players, "mapName":mapName, "mapSize":mapSize, "victoryCondition":victoryCondition})
    self.xml.append(itemXml)

  def getGame(self):
    game = self.xml.find('{%s}game' % self.namespace)
    return game.get("name"), game.get("ip"), game.get("state"), game.get("mapName"), game.get("mapSize"), game.get("victoryCondition"), game.get("nbp"), game.get("tnbp"), game.get("players")

  def getCommand(self):
    command = self.xml.find('{%s}command' % self.namespace)
    return command

## Class for leaderboard data ##
class BasicBoardList(ElementBase):
  name = 'query'
  namespace = 'jabber:iq:boardlist'
  interfaces = set(('board', 'command'))
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

class XpartaMuPP(sleekxmpp.ClientXMPP):
  """
  A simple game list provider
  """

  def __init__(self, sjid, password, room, nick):
    sleekxmpp.ClientXMPP.__init__(self, sjid, password)
    self.sjid = sjid
    self.room = room
    self.nick = nick

    # Game collection
    self.m_gameList = {}

    # Board collection
    self.m_boardList = {}

    # Store mapping of nicks and XmppIDs
    self.m_xmppIdToNick = {}

    #DEMO
    if configDEMOModOn:
      self.storeGame("666.666.666.666", "Unplayable map", "666.666.666.666", "Serengeti", "128", "gold rush", "4", "4", "alice, bob")
      self.storeGame("666.666.666.667", "Unreachale liberty", "666.666.666.667", "oasis", "256", "conquest", "2", "4", "alice, bob")
      self.storeGame("666.666.666.700", "No map preview", "666.666.666.700", "Bridge demo", "256", "conquest", "1", "4", "alice")
      self.storeGame("666.666.666.668", "Waiting.. (your nickname must be 'alice' or 'bob') ", "666.666.666.668", "oasis", "256", "conquest", "2", "4", "alice, bob", "waiting")
      self.storeGame("666.666.666.669", "Running.. (this game should not be sent)", "666.666.666.669", "oasis", "256", "conquest", "2", "4", "alice, bob", "running")
      #for i in range(50):
      #  self.storeGame("666.666.666."+str(i), "X"+str(i), "666.666.666."+str(i), "Oasis", "large", "conquest", "1", "4", "alice, bob")

    register_stanza_plugin(Iq, BasicGameList)
    register_stanza_plugin(Iq, BasicBoardList)
    self.register_handler(Callback('Iq Gamelist',
                                       StanzaPath('iq/gamelist'),
                                       self.iqhandler,
                                       instream=True))
    self.register_handler(Callback('Iq Boardlist',
                                       StanzaPath('iq/boardlist'),
                                       self.iqhandler,
                                       instream=True))
    self.add_event_handler("session_start", self.start)
    self.add_event_handler("message", self.message)
    self.add_event_handler("muc::%s::got_online" % self.room, self.muc_online)
    self.add_event_handler("muc::%s::got_offline" % self.room, self.muc_offline)

  def start(self, event):
    """
    Process the session_start event
    """
    self.plugin['xep_0045'].joinMUC(self.room, self.nick)

    self.send_presence()
    self.get_roster()
    logging.info("xpartamupp started")

    #DEMO
    #self.DEMOregisterGame()
    #self.DEMOchangeStateGame()
    #self.DEMOrequestGameList()

  def message(self, msg):
    """
    Process incoming message stanzas
    """
    if msg['type'] == 'chat':
      msg.reply("Thanks for sending\n%(body)s" % msg).send()
      logging.comm("receive message"+msg['body'])

  def muc_online(self, presence):
    """
    Process presence stanza from a chat room.
    """
    if presence['muc']['nick'] != self.nick:
      self.send_message(mto=presence['from'], mbody="Hello %s, welcome in the 0ad alpha chatroom. Polish your weapons and get ready to fight!" %(presence['muc']['nick']), mtype='')
      self.m_xmppIdToNick[presence['from']] = presence['muc']['nick']
      logging.debug("Player '%s (%s - %s)' connected" %(presence['muc']['nick'], presence['muc']['jid'], presence['muc']['jid'].bare))
      self.sendGameList(presence['from'])

  def muc_offline(self, presence):
    """
    Process presence stanza from a chat room.
    """
    if presence['muc']['nick'] != self.nick:
      self.removeGame(presence['muc']['jid'].bare)

  def iqhandler(self, iq):
    """
    Handle the custom <gamelist> stanza
    TODO: This method should be very robust because
    we could receive anything
    """
    if iq['type'] == 'error':
      logging.error('iqhandler error' + iq['error']['condition'])
      #self.disconnect()
    elif iq['type'] == 'get':
      """
      Request lists
      """
      self.sendGameList(iq['from'])
      self.sendBoardList(iq['from'])
    elif iq['type'] == 'result':
      """
      Iq successfully received
      """
      pass
    elif iq['type'] == 'set':
      """
      Register-update / unregister a game
      """
      command = iq['gamelist']['command'].text
      if command == 'register':
        try:
          name, ip, state, mapName, mapSize, victoryCondition, nbp, tnbp, players = iq['gamelist']['game']
          logging.debug("ip " + ip)
          self.storeGame(iq['from'].bare,name,ip,mapName,mapSize,victoryCondition,nbp,tnbp,players)
        except:
          logging.error("Failed to process register data")
      elif command == 'unregister':
        self.removeGame(iq['from'].bare)
      elif command == 'changestate':
        try:
          name, ip, state, mapName, mapSize, victoryCondition, nbp, tnbp, players = iq['gamelist']['game']
          self.changeGameState(iq['from'].bare, nbp)
        except:
          logging.error("Failed to process changestate data")
      else:
        logging.error("Failed to process command '%s' received from %s" % command, iq['from'].bare)

  def sendGameList(self, to):
    """
    Send a massive stanza with the whole game list
    """
    if to not in self.m_xmppIdToNick:
      logging.error("No player with the xmpp id '%s' known" % to.bare)
      return

    stz = BasicGameList()
    for k in self.m_gameList:
      g = self.m_gameList[k]
      # Only send the game that are in the 'init' state and games
      # and games that have to.bare in the list of players and are in the 'waiting' state.
      if g['state'] == 'init' or (g['state'] == 'waiting' and self.m_xmppIdToNick[to] in g['players-init']):
        stz.addGame(g['name'], g['ip'], g['state'], g['mapName'], g['mapSize'], g['victoryCondition'], g['nbp'], g['tnbp'], ', '.join(g['players']))
    iq = self.Iq()
    iq['type'] = 'result'
    iq['to'] = to
    iq.setPayload(stz)
    try:
      iq.send()
    except:
      logging.error("Failed to send game list")

  def sendBoardList(self, to):
    """
    Send a massive stanza with the whole leaderboard list
    """
    if to not in self.m_xmppIdToNick:
      logging.error("No player with the xmpp id '%s' known" % to.bare)
      return

    stz = BasicBoardList()
    for k in self.m_boardList:
      g = self.m_boardList[k]
      stz.addItem(g['name'], g['rank'])
    iq = self.Iq()
    iq['type'] = 'result'
    iq['to'] = to
    iq.setPayload(stz)
    try:
      iq.send()
    except:
      logging.error("Failed to send leaderboard list")

  def DEMOrequestGameList(self):
    """
    Test function
    """
    iq = self.Iq()
    iq['type'] = 'get'
    iq['gamelist']['field'] = 'x'
    iq['to'] = self.boundjid.full
    iq.send(now=True, block=False)
    return True

  def DEMOregisterGame(self):
    """
    Test function
    """
    stz = BasicGameList()
    stz.addGame("DEMOregister","DEMOip","garbage","DEMOmap","","","2","2","bob charlie")
    stz['command'] = 'register'
    iq = self.Iq()
    iq['type'] = 'set'
    iq['to'] = self.boundjid.full
    iq.setPayload(stz)
    iq.send(now=True, block=False)
    return True

  def DEMOchangeStateGame(self):
    """
    Test function
    """
    stz = BasicGameList()
    stz.addGame("","","running","","","","1","","")
    stz['command'] = 'changestate'
    iq = self.Iq()
    iq['type'] = 'set'
    iq['to'] = self.boundjid.full
    iq.setPayload(stz)
    iq.send(now=True, block=False)
    return True

  # Game management

  def storeGame(self, sid, name, ip, mapName, mapSize, victoryCondition, nbp, tnbp, players, state='init'):
    game = { 'name':name, 'ip':ip, 'state':state, 'nbp':nbp, 'nbp-init':nbp, 'tnbp':tnbp, 'players':players.split(', '), 'players-init':players.split(', '), 'mapName':mapName, 'mapSize':mapSize, 'victoryCondition':victoryCondition }
    self.m_gameList[sid] = game

  def removeGame(self, sid):
    if sid in self.m_gameList:
      del self.m_gameList[sid]

  def changeGameState(self, sid, nbp):
    if sid in self.m_gameList:
      if self.m_gameList[sid]['nbp-init'] > nbp:
        logging.debug("change game (%s) state from %s to %s", sid, self.m_gameList[sid]['state'], 'waiting')
        self.m_gameList[sid]['nbp'] = nbp
        self.m_gameList[sid]['state'] = 'waiting'
      else:
        logging.debug("change game (%s) state from %s to %s", sid, self.m_gameList[sid]['state'], 'running')
        self.m_gameList[sid]['nbp'] = nbp
        self.m_gameList[sid]['state'] = 'running'


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
                  default="localhost")
  optp.add_option('-l', '--login', help='set xpartamupp login',
                  action='store', dest='xlogin',
                  default="xpartamupp")
  optp.add_option('-p', '--password', help='set xpartamupp password',
                  action='store', dest='xpassword',
                  default="XXXXXX")
  optp.add_option('-n', '--nickname', help='set xpartamupp nickname',
                  action='store', dest='xnickname',
                  default="XpartaMuCC")
  optp.add_option('-D', '--demo', help='set xpartamupp in DEMO mode (add a few fake games)',
                  action='store_true', dest='xdemomode',
                  default=False)

  opts, args = optp.parse_args()

  # Set DEMO mode
  configDEMOModOn = opts.xdemomode

  # Setup logging.
  logging.basicConfig(level=opts.loglevel,
                      format='%(levelname)-8s %(message)s')

  # XpartaMuPP
  xmpp = XpartaMuPP(opts.xlogin+'@'+opts.xdomain+'/CC', opts.xpassword, 'arena@conference.'+opts.xdomain, opts.xnickname)
  xmpp.register_plugin('xep_0030') # Service Discovery
  xmpp.register_plugin('xep_0004') # Data Forms
  xmpp.register_plugin('xep_0045') # Multi-User Chat	# used
  xmpp.register_plugin('xep_0060') # PubSub
  xmpp.register_plugin('xep_0199') # XMPP Ping

  if xmpp.connect():
    xmpp.process(block=False)
    while True:
      time.sleep(5)
      logging.debug('Send Lists')
      for to in xmpp.m_xmppIdToNick:
        xmpp.sendGameList(to)
        xmpp.sendBoardList(to)
  else:
    logging.error("Unable to connect")

