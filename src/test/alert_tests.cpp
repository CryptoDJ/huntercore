// Copyright (c) 2013-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Unit tests for alert system

#include "chainparams.h"
#include "main.h" // For PartitionCheck

#include "test/testutil.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(Alert_tests, TestingSetup)

static bool falseFunc() { return false; }

BOOST_AUTO_TEST_CASE(PartitionAlert)
{
    // Test PartitionCheck
    CCriticalSection csDummy;
    CBlockIndex indexDummy[1000];
    CChainParams& params = Params(CBaseChainParams::MAIN);
    int64_t nPowTargetSpacing = params.GetConsensus().nPowTargetSpacing;

    /* Fix dual-algo effect on target spacing!  Note that we still want
       to pass the original nPowTargetSpacing to the PartitionCheck,
       as the NUM_ALGOS are already corrected there.  */
    const int64_t realTargetSpacing = nPowTargetSpacing / NUM_ALGOS;

    /* Another thing we have to fix for the changed target spacing
       is the number of blocks used at all.  Bitcoin has 100 here,
       we use 1,000.  This is necessary since the window at which
       PartitionCheck looks is based on *time* and not number of blocks.  */

    // Generate fake blockchain timestamps relative to
    // an arbitrary time:
    int64_t now = 1427379054;
    SetMockTime(now);
    for (int i = 0; i < 1000; i++)
    {
        indexDummy[i].phashBlock = NULL;
        if (i == 0) indexDummy[i].pprev = NULL;
        else indexDummy[i].pprev = &indexDummy[i-1];
        indexDummy[i].nHeight = i;
        indexDummy[i].nTime = now - (1000-i)*realTargetSpacing;
        // Other members don't matter, the partition check code doesn't
        // use them
    }

    strMiscWarning = "";

    // Test 1: chain with blocks every nPowTargetSpacing seconds,
    // as normal, no worries:
    PartitionCheck(falseFunc, csDummy, &indexDummy[999], nPowTargetSpacing);
    BOOST_CHECK_MESSAGE(strMiscWarning.empty(), strMiscWarning);

    // Test 2: go 3.5 hours without a block, expect a warning:
    now += 3*60*60+30*60;
    SetMockTime(now);
    PartitionCheck(falseFunc, csDummy, &indexDummy[999], nPowTargetSpacing);
    BOOST_CHECK(!strMiscWarning.empty());
    BOOST_TEST_MESSAGE(std::string("Got alert text: ")+strMiscWarning);
    strMiscWarning = "";

    // Test 3: test the "partition alerts only go off once per day"
    // code:
    now += 60*10;
    SetMockTime(now);
    PartitionCheck(falseFunc, csDummy, &indexDummy[999], nPowTargetSpacing);
    BOOST_CHECK(strMiscWarning.empty());

    // Test 4: get 2.5 times as many blocks as expected:
    now += 60*60*24; // Pretend it is a day later
    SetMockTime(now);
    int64_t quickSpacing = realTargetSpacing*2/5;
    for (int i = 0; i < 1000; i++) // Tweak chain timestamps:
        indexDummy[i].nTime = now - (1000-i)*quickSpacing;
    PartitionCheck(falseFunc, csDummy, &indexDummy[999], nPowTargetSpacing);
    BOOST_CHECK(!strMiscWarning.empty());
    BOOST_TEST_MESSAGE(std::string("Got alert text: ")+strMiscWarning);
    strMiscWarning = "";

    SetMockTime(0);
}

BOOST_AUTO_TEST_SUITE_END()
