// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chrome_thread.h"

#include "base/message_loop.h"
#include "base/message_loop_proxy.h"

// Friendly names for the well-known threads.
static const char* chrome_thread_names[ChromeThread::ID_COUNT] = {
  "",  // UI (name assembled in browser_main.cc).
  "Chrome_DBThread",  // DB
  "Chrome_WebKitThread",  // WEBKIT
  "Chrome_FileThread",  // FILE
  "Chrome_ProcessLauncherThread",  // PROCESS_LAUNCHER
  "Chrome_IOThread",  // IO
#if defined(USE_X11)
  "Chrome_Background_X11Thread",  // BACKGROUND_X11
#endif
};

// An implementation of MessageLoopProxy to be used in conjunction
// with ChromeThread.
class ChromeThreadMessageLoopProxy : public MessageLoopProxy {
 public:
  explicit ChromeThreadMessageLoopProxy(ChromeThread::ID identifier)
      : id_(identifier) {
  }

  // MessageLoopProxy implementation.
  virtual bool PostTask(const tracked_objects::Location& from_here,
                        Task* task) {
    return ChromeThread::PostTask(id_, from_here, task);
  }

  virtual bool PostDelayedTask(const tracked_objects::Location& from_here,
                               Task* task, int64 delay_ms) {
    return ChromeThread::PostDelayedTask(id_, from_here, task, delay_ms);
  }

  virtual bool PostNonNestableTask(const tracked_objects::Location& from_here,
                                   Task* task) {
    return ChromeThread::PostNonNestableTask(id_, from_here, task);
  }

  virtual bool PostNonNestableDelayedTask(
      const tracked_objects::Location& from_here,
      Task* task,
      int64 delay_ms) {
    return ChromeThread::PostNonNestableDelayedTask(id_, from_here, task,
                                                    delay_ms);
  }

 private:
  ChromeThread::ID id_;
  DISALLOW_COPY_AND_ASSIGN(ChromeThreadMessageLoopProxy);
};


Lock ChromeThread::lock_;

ChromeThread* ChromeThread::chrome_threads_[ID_COUNT];

ChromeThread::ChromeThread(ChromeThread::ID identifier)
    : Thread(chrome_thread_names[identifier]),
      identifier_(identifier) {
  Initialize();
}

ChromeThread::ChromeThread(ID identifier, MessageLoop* message_loop)
    : Thread(message_loop->thread_name().c_str()),
      identifier_(identifier) {
  set_message_loop(message_loop);
  Initialize();
}

void ChromeThread::Initialize() {
  AutoLock lock(lock_);
  DCHECK(identifier_ >= 0 && identifier_ < ID_COUNT);
  DCHECK(chrome_threads_[identifier_] == NULL);
  chrome_threads_[identifier_] = this;
}

ChromeThread::~ChromeThread() {
  // Stop the thread here, instead of the parent's class destructor.  This is so
  // that if there are pending tasks that run, code that checks that it's on the
  // correct ChromeThread succeeds.
  Stop();

  AutoLock lock(lock_);
  chrome_threads_[identifier_] = NULL;
#ifndef NDEBUG
  // Double check that the threads are ordererd correctly in the enumeration.
  for (int i = identifier_ + 1; i < ID_COUNT; ++i) {
    DCHECK(!chrome_threads_[i]) <<
        "Threads must be listed in the reverse order that they die";
  }
#endif
}

// static
bool ChromeThread::IsWellKnownThread(ID identifier) {
  AutoLock lock(lock_);
  return (identifier >= 0 && identifier < ID_COUNT &&
          chrome_threads_[identifier]);
}

// static
bool ChromeThread::CurrentlyOn(ID identifier) {
  AutoLock lock(lock_);
  DCHECK(identifier >= 0 && identifier < ID_COUNT);
  return chrome_threads_[identifier] &&
         chrome_threads_[identifier]->message_loop() == MessageLoop::current();
}

// static
bool ChromeThread::PostTask(ID identifier,
                            const tracked_objects::Location& from_here,
                            Task* task) {
  return PostTaskHelper(identifier, from_here, task, 0, true);
}

// static
bool ChromeThread::PostDelayedTask(ID identifier,
                                   const tracked_objects::Location& from_here,
                                   Task* task,
                                   int64 delay_ms) {
  return PostTaskHelper(identifier, from_here, task, delay_ms, true);
}

// static
bool ChromeThread::PostNonNestableTask(
    ID identifier,
    const tracked_objects::Location& from_here,
    Task* task) {
  return PostTaskHelper(identifier, from_here, task, 0, false);
}

// static
bool ChromeThread::PostNonNestableDelayedTask(
    ID identifier,
    const tracked_objects::Location& from_here,
    Task* task,
    int64 delay_ms) {
  return PostTaskHelper(identifier, from_here, task, delay_ms, false);
}

// static
bool ChromeThread::GetCurrentThreadIdentifier(ID* identifier) {
  MessageLoop* cur_message_loop = MessageLoop::current();
  for (int i = 0; i < ID_COUNT; ++i) {
    if (chrome_threads_[i] &&
        chrome_threads_[i]->message_loop() == cur_message_loop) {
      *identifier = chrome_threads_[i]->identifier_;
      return true;
    }
  }

  return false;
}

// static
scoped_refptr<MessageLoopProxy> ChromeThread::GetMessageLoopProxyForThread(
    ID identifier) {
  scoped_refptr<MessageLoopProxy> proxy =
      new ChromeThreadMessageLoopProxy(identifier);
  return proxy;
}

// static
bool ChromeThread::PostTaskHelper(
    ID identifier,
    const tracked_objects::Location& from_here,
    Task* task,
    int64 delay_ms,
    bool nestable) {
  DCHECK(identifier >= 0 && identifier < ID_COUNT);
  // Optimization: to avoid unnecessary locks, we listed the ID enumeration in
  // order of lifetime.  So no need to lock if we know that the other thread
  // outlives this one.
  // Note: since the array is so small, ok to loop instead of creating a map,
  // which would require a lock because std::map isn't thread safe, defeating
  // the whole purpose of this optimization.
  ID current_thread;
  bool guaranteed_to_outlive_target_thread =
      GetCurrentThreadIdentifier(&current_thread) &&
      current_thread >= identifier;

  if (!guaranteed_to_outlive_target_thread)
    lock_.Acquire();

  MessageLoop* message_loop = chrome_threads_[identifier] ?
      chrome_threads_[identifier]->message_loop() : NULL;
  if (message_loop) {
    if (nestable) {
      message_loop->PostDelayedTask(from_here, task, delay_ms);
    } else {
      message_loop->PostNonNestableDelayedTask(from_here, task, delay_ms);
    }
  } else {
    delete task;
  }

  if (!guaranteed_to_outlive_target_thread)
    lock_.Release();

  return !!message_loop;
}
