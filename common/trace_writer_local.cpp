/**************************************************************************
 *
 * Copyright 2007-2011 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 **************************************************************************/


#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os.hpp"
#include "os_thread.hpp"
#include "os_string.hpp"
#include "trace_file.hpp"
#include "trace_writer_local.hpp"
#include "trace_format.hpp"


namespace trace {


static const char *memcpy_args[3] = {"dest", "src", "n"};
const FunctionSig memcpy_sig = {0, "memcpy", 3, memcpy_args};

static const char *malloc_args[1] = {"size"};
const FunctionSig malloc_sig = {1, "malloc", 1, malloc_args};

static const char *free_args[1] = {"ptr"};
const FunctionSig free_sig = {2, "free", 1, free_args};

static const char *realloc_args[2] = {"ptr", "size"};
const FunctionSig realloc_sig = {3, "realloc", 2, realloc_args};


static void exceptionCallback(void)
{
    localWriter.flush();
}


LocalWriter::LocalWriter() :
    acquired(0)
{
    // Install the signal handlers as early as possible, to prevent
    // interfering with the application's signal handling.
    os::setExceptionCallback(exceptionCallback);
}

LocalWriter::~LocalWriter()
{
    os::resetExceptionCallback();
}

void
LocalWriter::open(void) {
    os::String szFileName;

    const char *lpFileName;

    lpFileName = getenv("TRACE_FILE");
    if (!lpFileName) {
        static unsigned dwCounter = 0;

        os::String process = os::getProcessName();
#ifdef _WIN32
        process.trimExtension();
#endif
        process.trimDirectory();

        os::String prefix = os::getCurrentDir();
        prefix.join(process);

        for (;;) {
            FILE *file;

            if (dwCounter)
                szFileName = os::String::format("%s.%u.trace", prefix.str(), dwCounter);
            else
                szFileName = os::String::format("%s.trace", prefix.str());

            lpFileName = szFileName;
            file = fopen(lpFileName, "rb");
            if (file == NULL)
                break;

            fclose(file);

            ++dwCounter;
        }
    }

    os::log("apitrace: tracing to %s\n", lpFileName);

    if (!Writer::open(lpFileName)) {
        os::log("apitrace: error: failed to open %s\n", lpFileName);
        os::abort();
    }

#if 0
    // For debugging the exception handler
    *((int *)0) = 0;
#endif
}

static unsigned next_thread_id = 0;
static os::thread_specific_ptr<unsigned> thread_id_specific_ptr;

unsigned LocalWriter::beginEnter(const FunctionSig *sig) {
    os::acquireMutex();
    ++acquired;

    if (!m_file->isOpened()) {
        open();
    }

    unsigned *thread_id_ptr = thread_id_specific_ptr.get();
    unsigned thread_id;
    if (thread_id_ptr) {
        thread_id = *thread_id_ptr;
    } else {
        thread_id = next_thread_id++;
        thread_id_ptr = new unsigned;
        *thread_id_ptr = thread_id;
        thread_id_specific_ptr.reset(thread_id_ptr);
    }

    return Writer::beginEnter(sig, thread_id);
}

void LocalWriter::endEnter(void) {
    Writer::endEnter();
    --acquired;
    os::releaseMutex();
}

void LocalWriter::beginLeave(unsigned call) {
    os::acquireMutex();
    ++acquired;
    Writer::beginLeave(call);
}

void LocalWriter::endLeave(void) {
    Writer::endLeave();
    --acquired;
    os::releaseMutex();
}

void LocalWriter::flush(void) {
    /*
     * Do nothing if the mutex is already acquired (e.g., if a segfault happen
     * while writing the file) to prevent dead-lock.
     */

    os::acquireMutex();
    if (acquired) {
        os::log("apitrace: ignoring exception while tracing\n");
    } else {
        ++acquired;
        if (m_file->isOpened()) {
            os::log("apitrace: flushing trace due to an exception\n");
            m_file->flush();
        }
        --acquired;
    }
    os::releaseMutex();
}


LocalWriter localWriter;


} /* namespace trace */

