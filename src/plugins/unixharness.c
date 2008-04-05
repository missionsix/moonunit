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

#define _GNU_SOURCE

#ifdef HAVE_CONFIG_H
#    include <config.h>
#endif

#include <moonunit/test.h>
#include <moonunit/harness.h>
#include <moonunit/loader.h>
#include <moonunit/util.h>
#include <moonunit/interface.h>
#include <uipc/ipc.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>

static long default_timeout;

typedef struct
{
    MuTestToken base;
    MuTestStage current_stage;
    MuTestStatus expected;
    MuTest* current_test;
    uipc_handle* ipc_handle;
    pid_t child;
} UnixToken;

static UnixToken* current_token;

static uipc_typeinfo testsummary_info =
{
    .size = sizeof(MuTestResult),
    .members =
    {
        UIPC_STRING(MuTestResult, file),
        UIPC_STRING(MuTestResult, reason),
        UIPC_END
    }
};

static uipc_typeinfo logevent_info =
{
    .size = sizeof(MuLogEvent),
    .members =
    {
        UIPC_STRING(MuLogEvent, file),
        UIPC_STRING(MuLogEvent, message),
        UIPC_END
    }
};

#define MSG_TYPE_RESULT 0
#define MSG_TYPE_EVENT 1

void unixtoken_event(MuTestToken* _token, const MuLogEvent* event)
{
    UnixToken* token = (UnixToken*) _token;
    uipc_handle* ipc_handle = token->ipc_handle;

    if (!ipc_handle)
    {
        exit(0);
    }

    ((MuLogEvent*) event)->stage = token->current_stage;    

    uipc_message* message = uipc_msg_new(MSG_TYPE_EVENT);
    uipc_msg_set_payload(message, event, &logevent_info);
    uipc_waitwrite(ipc_handle, message, NULL);
    uipc_msg_free(message);
}

void unixtoken_result(MuTestToken* _token, const MuTestResult* summary)
{    
    UnixToken* token = (UnixToken*) _token;
    uipc_handle* ipc_handle = token->ipc_handle;

    if (!ipc_handle)
    {
        exit(0);
    }

    ((MuTestResult*) summary)->stage = token->current_stage;
    ((MuTestResult*) summary)->expected = token->expected;
    uipc_message* message = uipc_msg_new(MSG_TYPE_RESULT);
    uipc_msg_set_payload(message, summary, &testsummary_info);
    uipc_waitwrite(ipc_handle, message, NULL);
    uipc_msg_free(message);

    uipc_detach(ipc_handle);
    
    exit(0);
}

void
unixtoken_meta(MuTestToken* _token, MuTestMeta type, ...)
{
    UnixToken* token = (UnixToken*) _token;
    va_list ap;
    
    va_start(ap, type);

    switch (type)
    {
    case MU_META_EXPECT:
        token->expected = va_arg(ap, MuTestStatus);
        break;
    }

    va_end(ap);
}

static char*
signal_description(int sig)
{
#ifdef HAVE_STRSIGNAL
    return strdup(strsignal(sig));
#else
    return format("Signal %i", sig);
#endif
}

static void
signal_handler(int sig)
{
    if (getpid() == current_token->child)
    {
        MuTestResult summary;
    
        summary.status = MU_STATUS_CRASH;
        summary.expected = current_token->expected;
        summary.stage = current_token->current_stage;
        summary.reason = signal_description(sig);
        summary.file = NULL;
        summary.line = 0;
    
        current_token->base.result((MuTestToken*) current_token, &summary);
    }
    else
    {
        signal(sig, SIG_DFL);
        raise(sig);
    }
}

static UnixToken*
unixtoken_new(MuTest* test)
{
    UnixToken* token = calloc(1, sizeof(UnixToken));

    Mu_TestToken_FillMethods((MuTestToken*) token);

    token->base.meta = unixtoken_meta;
    token->base.result = unixtoken_result;
    token->base.event = unixtoken_event;
    token->expected = test->expected;

    return token;
}

MuTestResult*
unixharness_dispatch(MuHarness* _self, MuTest* test, MuLogCallback cb, void* data)
{
    int sockets[2];
    pid_t pid;
    UnixToken* token = current_token = unixtoken_new(test);
    
    socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);
    
    // We must force a flush of all open output streams or the child
    // will end up flushing non-empty buffers on exit, resulting in
    // bizarre duplicate output
    
    fflush(NULL);
    
    if (!(pid = fork()))
    {
        MuTestThunk thunk;
        uipc_handle* ipc_test = uipc_attach(sockets[1]);
        token->child = getpid();
        
        close(sockets[0]);
        
        token->base.test = test;
        token->ipc_handle = ipc_test;
    
        Mu_Interface_SetCurrentToken((MuTestToken*) token);
        
        signal(SIGSEGV, signal_handler);
        signal(SIGPIPE, signal_handler);
        signal(SIGFPE, signal_handler);
        signal(SIGABRT, signal_handler);
    
        token->current_stage = MU_STAGE_SETUP;
    
        if ((thunk = Mu_Loader_FixtureSetup(test->loader, test->library, test->suite)))
            thunk((MuTestToken*) token);
    
        token->current_stage = MU_STAGE_TEST;
    
        test->run((MuTestToken*) token);
    
        token->current_stage = MU_STAGE_TEARDOWN;
    
        if ((thunk = Mu_Loader_FixtureTeardown(test->loader, test->library, test->suite)))
            thunk((MuTestToken*) token);
    
        token->base.method.success((MuTestToken*) token);
    
        uipc_detach(ipc_test);
        
        close(sockets[1]);
        
        exit(0);
    }
    else
    {
        uipc_handle* ipc_harness = uipc_attach(sockets[0]);
        MuTestResult *summary = NULL;
        uipc_message* message = NULL;
        int status;
        uipc_status uipc_result;
        long timeleft = default_timeout;
        bool done = false;    
        
        close(sockets[1]);
        
        while (!done)
        {    
            uipc_result = uipc_waitread(ipc_harness, &message, &timeleft);
            
            if (uipc_result == UIPC_SUCCESS)
            {
                switch (uipc_msg_get_type(message))
                {
                case MSG_TYPE_RESULT:
                    summary = uipc_msg_get_payload(message, &testsummary_info);
                    done = true;
                    break;
                case MSG_TYPE_EVENT:
                {
                    MuLogEvent* event = uipc_msg_get_payload(message, &logevent_info);
                    cb(event, data);
                    uipc_msg_free_payload(event, &logevent_info);
                    uipc_msg_free(message);
                    message = NULL;
                    break;
                } 
                }
            }
            else
            {
                done = true;
            }
        }
        
        uipc_detach(ipc_harness);    
        close(sockets[0]);
        
        if (uipc_result == UIPC_TIMEOUT)
        {
            kill(pid, SIGKILL);
        }
        
        waitpid(pid, &status, 0);
        
        if (!summary)
        {
            summary = calloc(1, sizeof(MuTestResult));
            // Timed out waiting for response
            if (uipc_result == UIPC_TIMEOUT)
            {
                char* reason = format("Test timed out after %li milliseconds", default_timeout);
                
                summary->expected = token->expected;
                summary->status = MU_STATUS_TIMEOUT;
                summary->reason = reason;
                summary->stage = MU_STAGE_UNKNOWN;
                summary->line = 0;
            }
            // Couldn't get message or an error occurred, try to figure out what happend
            else if (WIFSIGNALED(status))
            {
                summary->status = MU_STATUS_CRASH;
                summary->stage = MU_STAGE_UNKNOWN;
                summary->line = 0;
        
                if (WTERMSIG(status))
                    summary->reason = signal_description(WTERMSIG(status));
            }
            else
            {
                summary->status = MU_STATUS_FAILURE;
                summary->stage = MU_STAGE_UNKNOWN;
                summary->line = 0;
                summary->reason = strdup("Unexpected termination");
            }
        }

        if (message)
            uipc_msg_free(message);

        return summary;
    }
}

void
unixharness_free_result(MuHarness* _self, MuTestResult* result)
{
    uipc_msg_free_payload(result, &testsummary_info);
}

pid_t unixharness_debug(MuHarness* _self, MuTest* test)
{
    int sockets[2];
    pid_t pid;
    UnixToken* token = current_token = unixtoken_new(test);
    
    socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);
    
    if (!(pid = fork()))
    {
        MuTestThunk thunk;

        close(sockets[0]);

        token->base.test = test;
        Mu_Interface_SetCurrentToken((MuTestToken*) token);

        select(0, NULL, NULL, NULL, NULL);
        
        token->current_stage = MU_STAGE_SETUP;
        
        if ((thunk = Mu_Loader_FixtureSetup(test->loader, test->library, test->suite)))
            thunk((MuTestToken*) test);
            
        token->current_stage = MU_STAGE_TEST;
        
        test->run((MuTestToken*) token);
        
        token-> current_stage = MU_STAGE_TEARDOWN;
        
        if ((thunk = Mu_Loader_FixtureTeardown(test->loader, test->library, test->suite)))
            thunk((MuTestToken*) token);
        
        token->base.method.success((MuTestToken*) token);
    
        exit(0);
    }
    else
    {
        return pid;
    }
}
  
static void
option_set(void* _self, const char* name, void* data)
{
    if (!strcmp(name, "timeout"))
    {
        default_timeout = *(int*) data;
    }
}

static const void*
option_get(void* _self, const char* name)
{
    if (!strcmp(name, "timeout"))
    {
        return &default_timeout;
    }
    else
    {
        return NULL;
    }
}

static MuType
option_type(void* _self, const char* name)
{
    if (!strcmp(name, "timeout"))
    {
        return MU_TYPE_INTEGER;
    }
    else
    {
        return MU_TYPE_UNKNOWN;
    }
}

MuHarness mu_unixharness =
{
    .plugin = NULL,
    .dispatch = unixharness_dispatch,
    .free_result = unixharness_free_result,
    .debug = unixharness_debug,
    .option =
    {
        .set = option_set,
        .get = option_get,
        .type = option_type
    }
};
