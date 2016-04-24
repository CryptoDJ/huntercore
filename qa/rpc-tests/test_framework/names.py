#!/usr/bin/env python
# Copyright (c) 2014 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# General code for Namecoin tests.

from test_framework import BitcoinTestFramework
from util import *

class NameTestFramework (BitcoinTestFramework):

  def firstupdateName (self, ind, name, newData, value,
                       toAddr = None, allowActive = False):
    """
    Utility routine to perform a name_firstupdate command.  The rand
    and txid are taken from 'newData', as it is returned by name_new.
    """

    node = self.nodes[ind]

    if allowActive:
      if toAddr is None:
        toAddr = node.getnewaddress ()
      return node.name_firstupdate (name, newData[1], newData[0],
                                    value, toAddr, True)

    if toAddr is None:
      return node.name_firstupdate (name, newData[1], newData[0], value)
    return node.name_firstupdate (name, newData[1], newData[0], value, toAddr)

  def generate (self, ind, blocks, syncBefore = True):
    """
    Generate blocks and sync all nodes.
    """

    # Sync before to get the mempools up-to-date and sync afterwards
    # to ensure that all blocks have propagated.

    if syncBefore:
        self.sync_all ()
    self.nodes[ind].generate (blocks)
    self.sync_all ('blocks')

  def checkName (self, ind, name, value, dead):
    """
    Query a name with name_show and check that certain data fields
    match the expectations.  Returns the full JSON object.
    """

    data = self.nodes[ind].name_show (name)
    self.checkNameData (data, name, value, dead)

    return data

  def checkNameData (self, data, name, value, dead):
    """
    Check a name info object against expected data.
    """

    assert_equal (data['name'], name)
    if not dead:
      assert_equal (data['value'], value)
    assert_equal (data['dead'], dead)

  def checkNameHistory (self, ind, name, values):
    """
    Query for the name_history of 'name' and check that its historical
    values, in order of increasing height, are the ones in 'values'.
    """

    data = self.nodes[ind].name_history (name)

    valuesFound = []
    for e in data:
      assert_equal (e['name'], name)
      valuesFound.append (e['value'])

    assert_equal (valuesFound, values)

  def pendingTxid (self, ind, name):
    """
    Look through name_pending for the name and return the corresponding txid.
    """

    for p in self.nodes[ind].name_pending ():
      if p['name'] == name:
        return p['txid']

    return None

  def atomicTrade (self, name, value, price, fee, nameFrom, nameTo):
    """
    Perform an atomic name trade, sending 'name' from the first to the
    second node (referenced by their index).  Also send 'price' (we assume
    that it is less than each unspent output in 'listunspent') the
    other way round.  Returned is the txid.
    """

    addrA = self.nodes[nameFrom].getnewaddress ()
    addrB = self.nodes[nameTo].getnewaddress ()
    addrChange = self.nodes[nameTo].getrawchangeaddress ()

    inputs = []

    unspents = self.nodes[nameTo].listunspent ()
    assert (len (unspents) > 0)
    txin = unspents[0]
    assert (txin['amount'] >= price + fee)
    change = txin['amount'] - price - fee
    inputs.append ({"txid": txin['txid'], "vout": txin['vout']})

    data = self.nodes[nameFrom].name_show (name)
    inputs.append ({"txid": data['txid'], "vout": data['vout']})

    outputs = {addrA: price, addrChange: change}
    nameOp = {"op": "name_update", "name": name,
              "value": value, "address": addrB}

    tx = self.nodes[nameFrom].createrawtransaction (inputs, outputs, nameOp)
    signed = self.nodes[nameFrom].signrawtransaction (tx)
    assert not signed['complete']
    signed = self.nodes[nameTo].signrawtransaction (signed['hex'])
    assert signed['complete']
    tx = signed['hex']
    
    return self.nodes[nameFrom].sendrawtransaction (tx)

  def setupNodesWithArgs (self, extraArgs):
    enable_mocktime ()
    return start_nodes (len (extraArgs), self.options.tmpdir, extraArgs)

  def setup_nodes (self):
    return self.setupNodesWithArgs ([[]] * 4)
