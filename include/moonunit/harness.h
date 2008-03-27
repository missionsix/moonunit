/*
 * Copyright (c) 2007, Brian Koropoff
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Moonunit project nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BRIAN KOROPOFF ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL BRIAN KOROPOFF BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __MU_HARNESS_H__
#define __MU_HARNESS_H__

#include <sys/types.h>
#include <moonunit/test.h>
#include <moonunit/plugin.h>

typedef void (*MuLogCallback)(MuLogEvent* event, void* data);

typedef struct MuHarness
{
    struct MuPlugin* plugin;
    // Called by a unit test to log a non-failing event.  The passed
    // structure is owned by the caller and must be copied if preservation
    // is required.
    void (*event)(struct MuHarness*, struct MuTest*, const MuLogEvent*);

    // Called to run a single unit test.  Results should be stored
    // in the passed in MoonTestSummary structure.
    void (*dispatch)(struct MuHarness*, struct MuTest*, MuTestSummary*, MuLogCallback, void*);

    // Called to run and immediately suspend a unit test in
    // a separate process.  The test can then be traced by
    // a debugger.
    pid_t (*debug)(struct MuHarness*, struct MuTest*);

    // Clean up any memory in a MoonTestSummary filled in by
    // a call to dispatch
    void (*cleanup)(struct MuHarness*, MuTestSummary*);
} MuHarness;

const char* Mu_TestResultToString(MuTestResult result);
const char* Mu_TestStageToString(MuTestStage stage);

#endif
