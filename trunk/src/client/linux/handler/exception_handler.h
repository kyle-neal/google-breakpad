// Copyright (c) 2010 Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef CLIENT_LINUX_HANDLER_EXCEPTION_HANDLER_H_
#define CLIENT_LINUX_HANDLER_EXCEPTION_HANDLER_H_

#include <string>
#include <vector>

#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ucontext.h>

#include "client/linux/crash_generation/crash_generation_client.h"
#include "client/linux/handler/minidump_descriptor.h"
#include "client/linux/minidump_writer/minidump_writer.h"
#include "common/using_std_string.h"
#include "google_breakpad/common/minidump_format.h"
#include "processor/scoped_ptr.h"

namespace google_breakpad {

// ExceptionHandler
//
// ExceptionHandler can write a minidump file when an exception occurs,
// or when WriteMinidump() is called explicitly by your program.
//
// To have the exception handler write minidumps when an uncaught exception
// (crash) occurs, you should create an instance early in the execution
// of your program, and keep it around for the entire time you want to
// have crash handling active (typically, until shutdown).
// (NOTE): There should be only be one this kind of exception handler
// object per process.
//
// If you want to write minidumps without installing the exception handler,
// you can create an ExceptionHandler with install_handler set to false,
// then call WriteMinidump.  You can also use this technique if you want to
// use different minidump callbacks for different call sites.
//
// In either case, a callback function is called when a minidump is written,
// which receives the full path or file descriptor of the minidump.  The
// caller can collect and write additional application state to that minidump,
// and launch an external crash-reporting application.
//
// Caller should try to make the callbacks as crash-friendly as possible,
// it should avoid use heap memory allocation as much as possible.

class ExceptionHandler {
 public:
  // A callback function to run before Breakpad performs any substantial
  // processing of an exception.  A FilterCallback is called before writing
  // a minidump.  |context| is the parameter supplied by the user as
  // callback_context when the handler was created.
  //
  // If a FilterCallback returns true, Breakpad will continue processing,
  // attempting to write a minidump.  If a FilterCallback returns false,
  // Breakpad  will immediately report the exception as unhandled without
  // writing a minidump, allowing another handler the opportunity to handle it.
  typedef bool (*FilterCallback)(void *context);

  // A callback function to run after the minidump has been written.
  // |descriptor| contains the file descriptor or file path containing the
  // minidump. |context| is the parameter supplied by the user as
  // callback_context when the handler was created.  |succeeded| indicates
  // whether a minidump file was successfully written.
  //
  // If an exception occurred and the callback returns true, Breakpad will
  // treat the exception as fully-handled, suppressing any other handlers from
  // being notified of the exception.  If the callback returns false, Breakpad
  // will treat the exception as unhandled, and allow another handler to handle
  // it. If there are no other handlers, Breakpad will report the exception to
  // the system as unhandled, allowing a debugger or native crash dialog the
  // opportunity to handle the exception.  Most callback implementations
  // should normally return the value of |succeeded|, or when they wish to
  // not report an exception of handled, false.  Callbacks will rarely want to
  // return true directly (unless |succeeded| is true).
  typedef bool (*MinidumpCallback)(const MinidumpDescriptor& descriptor,
                                   void* context,
                                   bool succeeded);

  // In certain cases, a user may wish to handle the generation of the minidump
  // themselves. In this case, they can install a handler callback which is
  // called when a crash has occurred. If this function returns true, no other
  // processing of occurs and the process will shortly be crashed. If this
  // returns false, the normal processing continues.
  typedef bool (*HandlerCallback)(const void* crash_context,
                                  size_t crash_context_size,
                                  void* context);

  // Creates a new ExceptionHandler instance to handle writing minidumps.
  // Before writing a minidump, the optional |filter| callback will be called.
  // Its return value determines whether or not Breakpad should write a
  // minidump.  The minidump content will be written to the file path or file
  // descriptor from |descriptor|, and the optional |callback| is called after
  // writing the dump file, as described above.
  // If install_handler is true, then a minidump will be written whenever
  // an unhandled exception occurs.  If it is false, minidumps will only
  // be written when WriteMinidump is called.
  // If |server_fd| is valid, the minidump is generated out-of-process.  If it
  // is -1, in-process generation will always be used.
  ExceptionHandler(const MinidumpDescriptor& descriptor,
                   FilterCallback filter,
                   MinidumpCallback callback,
                   void *callback_context,
                   bool install_handler,
                   const int server_fd);
  ~ExceptionHandler();

  const MinidumpDescriptor& minidump_descriptor() const {
    return minidump_descriptor_;
  }

  void set_crash_handler(HandlerCallback callback) {
    crash_handler_ = callback;
  }

  // Writes a minidump immediately.  This can be used to capture the execution
  // state independently of a crash.
  // Returns true on success.
  // If the ExceptionHandler has been created with a path, a new file is
  // generated for each minidump.  The file path can be retrieved in the
  // MinidumpDescriptor passed to the MinidumpCallback or by accessing the
  // MinidumpDescriptor directly from the ExceptionHandler (with
  // minidump_descriptor()).
  // If the ExceptionHandler has been created with a file descriptor, the file
  // descriptor is repositioned to its beginning and the previous generated
  // minidump is overwritten.
  // Note that this method is not supposed to be called from a compromised
  // context as it uses the heap.
  bool WriteMinidump();

  // Convenience form of WriteMinidump which does not require an
  // ExceptionHandler instance.
  static bool WriteMinidump(const string& dump_path,
                            MinidumpCallback callback,
                            void* callback_context);

  // This structure is passed to minidump_writer.h:WriteMinidump via an opaque
  // blob. It shouldn't be needed in any user code.
  struct CrashContext {
    siginfo_t siginfo;
    pid_t tid;  // the crashing thread.
    struct ucontext context;
#if !defined(__ARM_EABI__)
    // #ifdef this out because FP state is not part of user ABI for Linux ARM.
    struct _libc_fpstate float_state;
#endif
  };

  // Returns whether out-of-process dump generation is used or not.
  bool IsOutOfProcess() const {
      return crash_generation_client_.get() != NULL;
  }

  // Add information about a memory mapping. This can be used if
  // a custom library loader is used that maps things in a way
  // that the linux dumper can't handle by reading the maps file.
  void AddMappingInfo(const string& name,
                      const u_int8_t identifier[sizeof(MDGUID)],
                      uintptr_t start_address,
                      size_t mapping_size,
                      size_t file_offset);

  // Force signal handling for the specified signal.
  bool SimulateSignalDelivery(int sig);
 private:
  // Save the old signal handlers and install new ones.
  static bool InstallHandlersLocked();
  // Restore the old signal handlers.
  static void RestoreHandlersLocked();

  void PreresolveSymbols();
  bool GenerateDump(CrashContext *context);
  void SendContinueSignalToChild();
  void WaitForContinueSignal();

  static void SignalHandler(int sig, siginfo_t* info, void* uc);
  bool HandleSignal(int sig, siginfo_t* info, void* uc);
  static int ThreadEntry(void* arg);
  bool DoDump(pid_t crashing_process, const void* context,
              size_t context_size);

  const FilterCallback filter_;
  const MinidumpCallback callback_;
  void* const callback_context_;

  scoped_ptr<CrashGenerationClient> crash_generation_client_;

  MinidumpDescriptor minidump_descriptor_;

  HandlerCallback crash_handler_;

  // The global exception handler stack. This is need becuase there may exist
  // multiple ExceptionHandler instances in a process. Each will have itself
  // registered in this stack.
  static std::vector<ExceptionHandler*> *handler_stack_;
  static pthread_mutex_t handler_stack_mutex_;

  // We need to explicitly enable ptrace of parent processes on some
  // kernels, but we need to know the PID of the cloned process before we
  // can do this. We create a pipe which we can use to block the
  // cloned process after creating it, until we have explicitly enabled 
  // ptrace. This is used to store the file descriptors for the pipe
  int fdes[2];

  // Callers can add extra info about mappings for cases where the
  // dumper code cannot extract enough information from /proc/<pid>/maps.
  MappingList mapping_list_;
};

}  // namespace google_breakpad

#endif  // CLIENT_LINUX_HANDLER_EXCEPTION_HANDLER_H_
