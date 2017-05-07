// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "core_io.h"

#include "base58.h"
#include "game/tx.h"
#include "names/common.h"
#include "primitives/transaction.h"
#include "script/names.h"
#include "script/script.h"
#include "script/standard.h"
#include "serialize.h"
#include "streams.h"
#include <univalue.h>
#include "util.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

/* Decode an integer (could be encoded as OP_x or a bignum)
   from the script.  Returns -1 in case of error.  */
static int
GetScriptUint (const CScript& script, CScript::const_iterator& pc)
{
  opcodetype opcode;
  valtype vch;
  if (!script.GetOp (pc, opcode, vch))
    return -1;

  if (opcode >= OP_1 && opcode <= OP_16)
    return opcode - OP_1 + 1;

  /* Convert byte vector to integer.  */
  int res = 0;
  for (unsigned i = 0; i < vch.size (); ++i)
    res += (1 << (i * 8)) * vch[i];

  return res;
}

/**
 * Convert a game tx input script to a JSON representation.  This is used
 * by decoderawtransaction.
 */
UniValue
GameInputToUniv (const CScript& scriptSig)
{
  UniValue res(UniValue::VOBJ);

  CScript::const_iterator pc = scriptSig.begin ();
  opcodetype opcode;
  valtype vch;
  if (!scriptSig.GetOp (pc, opcode, vch))
    goto error;
  res.push_back (Pair ("player", ValtypeToString (vch)));

  if (!scriptSig.GetOp (pc, opcode))
    goto error;

  switch (opcode - OP_1 + 1)
    {
    case GAMEOP_KILLED_BY:
      {
        UniValue killers(UniValue::VARR);
        while (scriptSig.GetOp (pc, opcode, vch))
          killers.push_back (ValtypeToString (vch));

        if (killers.empty ())
          res.push_back (Pair ("op", "spawn_death"));
        else
          {
            res.push_back (Pair ("op", "killed_by"));
            res.push_back (Pair ("killers", killers));
          }

        break;
      }

    case GAMEOP_KILLED_POISON:
      res.push_back (Pair ("op", "poison_death"));
      break;

    case GAMEOP_COLLECTED_BOUNTY:
      res.push_back (Pair ("op", "banking"));
      res.push_back (Pair ("index", GetScriptUint (scriptSig, pc)));
      res.push_back (Pair ("first_block", GetScriptUint (scriptSig, pc)));
      res.push_back (Pair ("last_block", GetScriptUint (scriptSig, pc)));
      res.push_back (Pair ("first_collected", GetScriptUint (scriptSig, pc)));
      res.push_back (Pair ("last_collected", GetScriptUint (scriptSig, pc)));
      break;

    case GAMEOP_REFUND:
      res.push_back (Pair ("op", "refund"));
      res.push_back (Pair ("index", GetScriptUint (scriptSig, pc)));
      res.push_back (Pair ("height", GetScriptUint (scriptSig, pc)));
      break;

    default:
      goto error;
    }

  return res;

error:
  res.push_back (Pair ("error", "could not decode game tx"));
  return res;
}

std::string FormatScript(const CScript& script)
{
    std::string ret;
    CScript::const_iterator it = script.begin();
    opcodetype op;
    while (it != script.end()) {
        CScript::const_iterator it2 = it;
        std::vector<unsigned char> vch;
        if (script.GetOp2(it, op, &vch)) {
            if (op == OP_0) {
                ret += "0 ";
                continue;
            } else if ((op >= OP_1 && op <= OP_16) || op == OP_1NEGATE) {
                ret += strprintf("%i ", op - OP_1NEGATE - 1);
                continue;
            } else if (op >= OP_NOP && op <= OP_NOP10) {
                std::string str(GetOpName(op));
                if (str.substr(0, 3) == std::string("OP_")) {
                    ret += str.substr(3, std::string::npos) + " ";
                    continue;
                }
            }
            if (vch.size() > 0) {
                ret += strprintf("0x%x 0x%x ", HexStr(it2, it - vch.size()), HexStr(it - vch.size(), it));
            } else {
                ret += strprintf("0x%x ", HexStr(it2, it));
            }
            continue;
        }
        ret += strprintf("0x%x ", HexStr(it2, script.end()));
        break;
    }
    return ret.substr(0, ret.size() - 1);
}

const std::map<unsigned char, std::string> mapSigHashTypes =
    boost::assign::map_list_of
    (static_cast<unsigned char>(SIGHASH_ALL), std::string("ALL"))
    (static_cast<unsigned char>(SIGHASH_ALL|SIGHASH_ANYONECANPAY), std::string("ALL|ANYONECANPAY"))
    (static_cast<unsigned char>(SIGHASH_NONE), std::string("NONE"))
    (static_cast<unsigned char>(SIGHASH_NONE|SIGHASH_ANYONECANPAY), std::string("NONE|ANYONECANPAY"))
    (static_cast<unsigned char>(SIGHASH_SINGLE), std::string("SINGLE"))
    (static_cast<unsigned char>(SIGHASH_SINGLE|SIGHASH_ANYONECANPAY), std::string("SINGLE|ANYONECANPAY"))
    ;

/**
 * Create the assembly string representation of a CScript object.
 * @param[in] script    CScript object to convert into the asm string representation.
 * @param[in] fAttemptSighashDecode    Whether to attempt to decode sighash types on data within the script that matches the format
 *                                     of a signature. Only pass true for scripts you believe could contain signatures. For example,
 *                                     pass false, or omit the this argument (defaults to false), for scriptPubKeys.
 */
std::string ScriptToAsmStr(const CScript& script, const bool fAttemptSighashDecode)
{
    std::string str;
    opcodetype opcode;
    std::vector<unsigned char> vch;
    CScript::const_iterator pc = script.begin();
    while (pc < script.end()) {
        if (!str.empty()) {
            str += " ";
        }
        if (!script.GetOp(pc, opcode, vch)) {
            str += "[error]";
            return str;
        }
        if (0 <= opcode && opcode <= OP_PUSHDATA4) {
            if (vch.size() <= static_cast<std::vector<unsigned char>::size_type>(4)) {
                str += strprintf("%d", CScriptNum(vch, false).getint());
            } else {
                // the IsUnspendable check makes sure not to try to decode OP_RETURN data that may match the format of a signature
                if (fAttemptSighashDecode && !script.IsUnspendable()) {
                    std::string strSigHashDecode;
                    // goal: only attempt to decode a defined sighash type from data that looks like a signature within a scriptSig.
                    // this won't decode correctly formatted public keys in Pubkey or Multisig scripts due to
                    // the restrictions on the pubkey formats (see IsCompressedOrUncompressedPubKey) being incongruous with the
                    // checks in CheckSignatureEncoding.
                    if (CheckSignatureEncoding(vch, SCRIPT_VERIFY_STRICTENC, NULL)) {
                        const unsigned char chSigHashType = vch.back();
                        if (mapSigHashTypes.count(chSigHashType)) {
                            strSigHashDecode = "[" + mapSigHashTypes.find(chSigHashType)->second + "]";
                            vch.pop_back(); // remove the sighash type byte. it will be replaced by the decode.
                        }
                    }
                    str += HexStr(vch) + strSigHashDecode;
                } else {
                    str += HexStr(vch);
                }
            }
        } else {
            str += GetOpName(opcode);
        }
    }
    return str;
}

std::string EncodeHexTx(const CTransaction& tx, const int serializeFlags)
{
    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION | serializeFlags);
    ssTx << tx;
    return HexStr(ssTx.begin(), ssTx.end());
}

void ScriptPubKeyToUniv(const CScript& scriptPubKey,
                        UniValue& out, bool fIncludeHex)
{
    txnouttype type;
    std::vector<CTxDestination> addresses;
    int nRequired;

    const CNameScript nameOp(scriptPubKey);
    if (nameOp.isNameOp ())
    {
        UniValue jsonOp(UniValue::VOBJ);
        switch (nameOp.getNameOp ())
        {
        case OP_NAME_NEW:
            jsonOp.push_back (Pair("op", "name_new"));
            jsonOp.push_back (Pair("hash", HexStr (nameOp.getOpHash ())));
            break;

        case OP_NAME_FIRSTUPDATE:
        {
            const std::string name = ValtypeToString (nameOp.getOpName ());
            const std::string value = ValtypeToString (nameOp.getOpValue ());
            const bool newStyle = nameOp.isNewStyleRegistration ();

            if (newStyle)
              jsonOp.push_back (Pair("op", "name_register"));
            else
              jsonOp.push_back (Pair("op", "name_firstupdate"));
            jsonOp.push_back (Pair("name", name));
            jsonOp.push_back (Pair("value", value));
            if (!newStyle)
              jsonOp.push_back (Pair("rand", HexStr (nameOp.getOpRand ())));
            break;
        }

        case OP_NAME_UPDATE:
        {
            const std::string name = ValtypeToString (nameOp.getOpName ());
            const std::string value = ValtypeToString (nameOp.getOpValue ());

            jsonOp.push_back (Pair("op", "name_update"));
            jsonOp.push_back (Pair("name", name));
            jsonOp.push_back (Pair("value", value));
            break;
        }

        default:
            assert (false);
        }

        out.push_back (Pair("nameOp", jsonOp));
    }

    out.pushKV("asm", ScriptToAsmStr(scriptPubKey));
    if (fIncludeHex)
        out.pushKV("hex", HexStr(scriptPubKey.begin(), scriptPubKey.end()));

    if (!ExtractDestinations(scriptPubKey, type, addresses, nRequired)) {
        out.pushKV("type", GetTxnOutputType(type));
        return;
    }

    out.pushKV("reqSigs", nRequired);
    out.pushKV("type", GetTxnOutputType(type));

    UniValue a(UniValue::VARR);
    BOOST_FOREACH(const CTxDestination& addr, addresses)
        a.push_back(CBitcoinAddress(addr).ToString());
    out.pushKV("addresses", a);
}

void TxToUniv(const CTransaction& tx, const uint256& hashBlock, UniValue& entry)
{
    entry.pushKV("txid", tx.GetHash().GetHex());
    entry.pushKV("hash", tx.GetWitnessHash().GetHex());
    entry.pushKV("version", tx.nVersion);
    entry.pushKV("size", (int)::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION));
    entry.pushKV("vsize", (GetTransactionWeight(tx) + WITNESS_SCALE_FACTOR - 1) / WITNESS_SCALE_FACTOR);
    entry.pushKV("locktime", (int64_t)tx.nLockTime);

    UniValue vin(UniValue::VARR);
    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        const CTxIn& txin = tx.vin[i];
        UniValue in(UniValue::VOBJ);
        if (tx.IsCoinBase())
            in.pushKV("coinbase", HexStr(txin.scriptSig.begin(), txin.scriptSig.end()));
        else {
            if (tx.IsGameTx()) {
                const UniValue gametx = GameInputToUniv(txin.scriptSig);
                in.pushKV("gametx", gametx);
            }
            else {
                in.pushKV("txid", txin.prevout.hash.GetHex());
                in.pushKV("vout", (int64_t)txin.prevout.n);
            }
            UniValue o(UniValue::VOBJ);
            o.pushKV("asm", ScriptToAsmStr(txin.scriptSig, true));
            o.pushKV("hex", HexStr(txin.scriptSig.begin(), txin.scriptSig.end()));
            in.pushKV("scriptSig", o);
            if (!tx.vin[i].scriptWitness.IsNull()) {
                UniValue txinwitness(UniValue::VARR);
                for (const auto& item : tx.vin[i].scriptWitness.stack) {
                    txinwitness.push_back(HexStr(item.begin(), item.end()));
                }
                in.pushKV("txinwitness", txinwitness);
            }
        }
        in.pushKV("sequence", (int64_t)txin.nSequence);
        vin.push_back(in);
    }
    entry.pushKV("vin", vin);

    UniValue vout(UniValue::VARR);
    for (unsigned int i = 0; i < tx.vout.size(); i++) {
        const CTxOut& txout = tx.vout[i];

        UniValue out(UniValue::VOBJ);

        UniValue outValue(UniValue::VNUM, FormatMoney(txout.nValue));
        out.pushKV("value", outValue);
        out.pushKV("n", (int64_t)i);

        UniValue o(UniValue::VOBJ);
        ScriptPubKeyToUniv(txout.scriptPubKey, o, true);
        out.pushKV("scriptPubKey", o);
        vout.push_back(out);
    }
    entry.pushKV("vout", vout);

    if (!hashBlock.IsNull())
        entry.pushKV("blockhash", hashBlock.GetHex());

    entry.pushKV("hex", EncodeHexTx(tx)); // the hex-encoded transaction. used the name "hex" to be consistent with the verbose output of "getrawtransaction".
}
