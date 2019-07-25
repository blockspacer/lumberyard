/*
* All or portions of this file Copyright (c) Amazon.com, Inc. or its affiliates or
* its licensors.
*
* For complete copyright and license terms please see the LICENSE at the root of this
* distribution (the "License"). All use of this software is governed by the License,
* or, if provided, by the license below or the license accompanying this file. Do not
* remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*
*/
#include <AzCore/Debug/StackTracer.h>
#include <AzCore/Math/MathUtils.h>

#include <AzCore/std/parallel/config.h>

#include <cxxabi.h>
#include <signal.h>
#include <cstdio>
#include <cstdlib>
#define UNW_LOCAL_ONLY
#include <libunwind.h>

#include <AzCore/std/parallel/mutex.h>

#if defined(AZ_PLATFORM_APPLE)
    #define UNW_WORD_FORMAT "llx"
#else
    #define UNW_WORD_FORMAT "lx"
#endif

using namespace AZ;
using namespace AZ::Debug;

AZStd::mutex g_mutex;               /// All dbg help functions are single threaded, so we need to control the access.

void SymbolStorage::RegisterModuleListeners()
{
}

void SymbolStorage::UnregisterModuleListeners()
{
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
// Stack Recorder
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

//=========================================================================
// Record
// [7/29/2009]
//=========================================================================
unsigned int
StackRecorder::Record(StackFrame* frames, unsigned int maxNumOfFrames, unsigned int suppressCount /* = 0 */, void* nativeThread /*= NULL*/)
{
    int count = 0;
    unw_cursor_t cursor;
    unw_context_t context;

    // Initialize cursor to current frame for local unwinding.
    unw_getcontext(&context);
    unw_init_local(&cursor, &context);

    int skip = static_cast<int>((suppressCount == 0) ? 1 : suppressCount); // Skip at least this function
    while ((unw_step(&cursor) > 0) && (count < maxNumOfFrames))
    {
        unw_word_t offset, pc;
        unw_get_reg(&cursor, UNW_REG_IP, &pc);
        if (pc == 0)
        {
            break;
        }
        else if (--skip < 0)
        {
            frames[count++].m_programCounter = pc;
        }
    }

    // Clear reset of the buffer
    for (int i = count; i < maxNumOfFrames; ++i)
    {
        frames[i].m_programCounter = 0;
    }

    return count;
}

void
SymbolStorage::DecodeFrames(const StackFrame* frames, unsigned int numFrames, StackLine* textLines)
{
    int count = 0;
    unw_cursor_t cursor;
    unw_context_t context;

    g_mutex.lock();

    // Initialize cursor to current frame for local unwinding.
    unw_getcontext(&context);
    unw_init_local(&cursor, &context);

    for (unsigned int i = 0; i < numFrames; ++i)
    {
        if (frames[i].IsValid())
        {
            unw_set_reg(&cursor, UNW_REG_IP, frames[i].m_programCounter);

            SymbolStorage::StackLine& textLine = textLines[count++];
            textLine[0] = 0;

            unw_word_t offset;
            char sym[1024] = { '\0' };
            if (unw_get_proc_name(&cursor, sym, sizeof(sym), &offset) == 0)
            {
                char* nameptr = sym;
                int status = 0;
                char* demangled = abi::__cxa_demangle(sym, nullptr, nullptr, &status);
                if (status == 0)
                {
                    nameptr = demangled;
                }

                ::std::snprintf(textLine, AZ_ARRAY_SIZE(textLine), "%s (+0x%" UNW_WORD_FORMAT ") [0x%" UNW_WORD_FORMAT "]",
                    nameptr, offset, frames[i].m_programCounter);
                ::std::free(demangled);
            }
            else
            {
                ::std::snprintf(textLine, AZ_ARRAY_SIZE(textLine), "%s", " -- error: unable to obtain symbol name for this frame");
            }
        }
    }

    // Empty rest of the buffer so we don't print junk
    for (unsigned int i = count; i < numFrames; ++i)
    {
        textLines[i][0] = '\0';
    }

    g_mutex.unlock();
}

void StackPrinter::Print(FILE *const ftrace)
{
    unw_cursor_t cursor;
    unw_context_t context;

    // Initialize cursor to current frame for local unwinding.
    unw_getcontext(&context);
    unw_init_local(&cursor, &context);

    while (unw_step(&cursor) > 0)
    {
        unw_word_t pc = 0;
        unw_get_reg(&cursor, UNW_REG_IP, &pc);
        if (pc == 0)
        {
            break;
        }

        unw_word_t offset = 0;
        char sym[1024] = { '\0' };
        if (unw_get_proc_name(&cursor, sym, sizeof(sym), &offset) == 0)
        {
            char* nameptr = sym;
            int status = 0;
            char* demangled = abi::__cxa_demangle(sym, nullptr, nullptr, &status);
            if (status == 0)
            {
                nameptr = demangled;
            }

            ::std::fprintf(ftrace, "%s (+0x%" UNW_WORD_FORMAT ") [0x%" UNW_WORD_FORMAT "]", nameptr, offset, pc);
            ::std::free(demangled);
        }
        else
        {
            ::std::fprintf(ftrace, "%s", " -- error: unable to obtain symbol name for this frame");
        }
    }
    ::std::fflush(ftrace);
}
