// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/ref_counted.h"
#include "base/message_loop.h"
#include "ppapi/c/pp_completion_callback.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/shared_impl/callback_tracker.h"
#include "ppapi/shared_impl/resource.h"
#include "ppapi/shared_impl/resource_tracker.h"
#include "ppapi/shared_impl/test_globals.h"
#include "ppapi/shared_impl/tracked_callback.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ppapi {

namespace {

class TrackedCallbackTest : public testing::Test {
 public:
  TrackedCallbackTest()
      : message_loop_(MessageLoop::TYPE_DEFAULT),
        pp_instance_(1234) {}

  PP_Instance pp_instance() const { return pp_instance_; }

  virtual void SetUp() OVERRIDE {
    globals_.GetResourceTracker()->DidCreateInstance(pp_instance_);
  }
  virtual void TearDown() OVERRIDE {
    globals_.GetResourceTracker()->DidDeleteInstance(pp_instance_);
  }

 private:
  MessageLoop message_loop_;
  TestGlobals globals_;
  PP_Instance pp_instance_;
};

struct CallbackRunInfo {
  // All valid results (PP_OK, PP_ERROR_...) are nonpositive.
  CallbackRunInfo() : run_count(0), result(1) {}
  unsigned run_count;
  int32_t result;
};

void TestCallback(void* user_data, int32_t result) {
  CallbackRunInfo* info = reinterpret_cast<CallbackRunInfo*>(user_data);
  info->run_count++;
  if (info->run_count == 1)
    info->result = result;
}

}  // namespace

// CallbackShutdownTest --------------------------------------------------------

namespace {

class CallbackShutdownTest : public TrackedCallbackTest {
 public:
  CallbackShutdownTest() {}

  // Cases:
  // (1) A callback which is run (so shouldn't be aborted on shutdown).
  // (2) A callback which is aborted (so shouldn't be aborted on shutdown).
  // (3) A callback which isn't run (so should be aborted on shutdown).
  CallbackRunInfo& info_did_run() { return info_did_run_; }  // (1)
  CallbackRunInfo& info_did_abort() { return info_did_abort_; }  // (2)
  CallbackRunInfo& info_didnt_run() { return info_didnt_run_; }  // (3)

 private:
  CallbackRunInfo info_did_run_;
  CallbackRunInfo info_did_abort_;
  CallbackRunInfo info_didnt_run_;
};

}  // namespace

// Tests that callbacks are properly aborted on module shutdown.
TEST_F(CallbackShutdownTest, AbortOnShutdown) {
  scoped_refptr<Resource> resource(new Resource(OBJECT_IS_IMPL, pp_instance()));

  // Set up case (1) (see above).
  EXPECT_EQ(0U, info_did_run().run_count);
  scoped_refptr<TrackedCallback> callback_did_run = new TrackedCallback(
      resource.get(),
      PP_MakeCompletionCallback(&TestCallback, &info_did_run()));
  EXPECT_EQ(0U, info_did_run().run_count);
  callback_did_run->Run(PP_OK);
  EXPECT_EQ(1U, info_did_run().run_count);
  EXPECT_EQ(PP_OK, info_did_run().result);

  // Set up case (2).
  EXPECT_EQ(0U, info_did_abort().run_count);
  scoped_refptr<TrackedCallback> callback_did_abort = new TrackedCallback(
      resource.get(),
      PP_MakeCompletionCallback(&TestCallback, &info_did_abort()));
  EXPECT_EQ(0U, info_did_abort().run_count);
  callback_did_abort->Abort();
  EXPECT_EQ(1U, info_did_abort().run_count);
  EXPECT_EQ(PP_ERROR_ABORTED, info_did_abort().result);

  // Set up case (3).
  EXPECT_EQ(0U, info_didnt_run().run_count);
  scoped_refptr<TrackedCallback> callback_didnt_run = new TrackedCallback(
      resource.get(),
      PP_MakeCompletionCallback(&TestCallback, &info_didnt_run()));
  EXPECT_EQ(0U, info_didnt_run().run_count);

  PpapiGlobals::Get()->GetCallbackTrackerForInstance(pp_instance())->AbortAll();

  // Check case (1).
  EXPECT_EQ(1U, info_did_run().run_count);

  // Check case (2).
  EXPECT_EQ(1U, info_did_abort().run_count);

  // Check case (3).
  EXPECT_EQ(1U, info_didnt_run().run_count);
  EXPECT_EQ(PP_ERROR_ABORTED, info_didnt_run().result);
}

// CallbackResourceTest --------------------------------------------------------

namespace {

class CallbackResourceTest : public TrackedCallbackTest {
 public:
  CallbackResourceTest() {}
};

class CallbackMockResource : public Resource {
 public:
  CallbackMockResource(PP_Instance instance)
      : Resource(OBJECT_IS_IMPL, instance) {}
  ~CallbackMockResource() {}

  PP_Resource SetupForTest() {
    PP_Resource resource_id = GetReference();
    EXPECT_NE(0, resource_id);

    callback_did_run_ = new TrackedCallback(
        this,
        PP_MakeCompletionCallback(&TestCallback, &info_did_run_));
    EXPECT_EQ(0U, info_did_run_.run_count);

    callback_did_abort_ = new TrackedCallback(
        this,
        PP_MakeCompletionCallback(&TestCallback, &info_did_abort_));
    EXPECT_EQ(0U, info_did_abort_.run_count);

    callback_didnt_run_ = new TrackedCallback(
        this,
        PP_MakeCompletionCallback(&TestCallback, &info_didnt_run_));
    EXPECT_EQ(0U, info_didnt_run_.run_count);

    callback_did_run_->Run(PP_OK);
    callback_did_abort_->Abort();

    CheckIntermediateState();

    return resource_id;
  }

  void CheckIntermediateState() {
    EXPECT_EQ(1U, info_did_run_.run_count);
    EXPECT_EQ(PP_OK, info_did_run_.result);

    EXPECT_EQ(1U, info_did_abort_.run_count);
    EXPECT_EQ(PP_ERROR_ABORTED, info_did_abort_.result);

    EXPECT_EQ(0U, info_didnt_run_.run_count);
  }

  void CheckFinalState() {
    EXPECT_EQ(1U, info_did_run_.run_count);
    EXPECT_EQ(PP_OK, info_did_run_.result);
    EXPECT_EQ(1U, info_did_abort_.run_count);
    EXPECT_EQ(PP_ERROR_ABORTED, info_did_abort_.result);
    EXPECT_EQ(1U, info_didnt_run_.run_count);
    EXPECT_EQ(PP_ERROR_ABORTED, info_didnt_run_.result);
  }

  scoped_refptr<TrackedCallback> callback_did_run_;
  CallbackRunInfo info_did_run_;

  scoped_refptr<TrackedCallback> callback_did_abort_;
  CallbackRunInfo info_did_abort_;

  scoped_refptr<TrackedCallback> callback_didnt_run_;
  CallbackRunInfo info_didnt_run_;
};

}  // namespace

// Test that callbacks get aborted on the last resource unref.
TEST_F(CallbackResourceTest, AbortOnNoRef) {
  ResourceTracker* resource_tracker =
      PpapiGlobals::Get()->GetResourceTracker();

  // Test several things: Unref-ing a resource (to zero refs) with callbacks
  // which (1) have been run, (2) have been aborted, (3) haven't been completed.
  // Check that the uncompleted one gets aborted, and that the others don't get
  // called again.
  scoped_refptr<CallbackMockResource> resource_1(
      new CallbackMockResource(pp_instance()));
  PP_Resource resource_1_id = resource_1->SetupForTest();

  // Also do the same for a second resource, and make sure that unref-ing the
  // first resource doesn't much up the second resource.
  scoped_refptr<CallbackMockResource> resource_2(
      new CallbackMockResource(pp_instance()));
  PP_Resource resource_2_id = resource_2->SetupForTest();

  // Double-check that resource #1 is still okay.
  resource_1->CheckIntermediateState();

  // Kill resource #1, spin the message loop to run posted calls, and check that
  // things are in the expected states.
  resource_tracker->ReleaseResource(resource_1_id);
  MessageLoop::current()->RunAllPending();
  resource_1->CheckFinalState();
  resource_2->CheckIntermediateState();

  // Kill resource #2.
  resource_tracker->ReleaseResource(resource_2_id);
  MessageLoop::current()->RunAllPending();
  resource_1->CheckFinalState();
  resource_2->CheckFinalState();

  // This shouldn't be needed, but make sure there are no stranded tasks.
  MessageLoop::current()->RunAllPending();
}

// Test that "resurrecting" a resource (getting a new ID for a |Resource|)
// doesn't resurrect callbacks.
TEST_F(CallbackResourceTest, Resurrection) {
  ResourceTracker* resource_tracker =
      PpapiGlobals::Get()->GetResourceTracker();

  scoped_refptr<CallbackMockResource> resource(
      new CallbackMockResource(pp_instance()));
  PP_Resource resource_id = resource->SetupForTest();

  // Unref it, spin the message loop to run posted calls, and check that things
  // are in the expected states.
  resource_tracker->ReleaseResource(resource_id);
  MessageLoop::current()->RunAllPending();
  resource->CheckFinalState();

  // "Resurrect" it and check that the callbacks are still dead.
  PP_Resource new_resource_id = resource->GetReference();
  MessageLoop::current()->RunAllPending();
  resource->CheckFinalState();

  // Unref it again and do the same.
  resource_tracker->ReleaseResource(new_resource_id);
  MessageLoop::current()->RunAllPending();
  resource->CheckFinalState();

  // This shouldn't be needed, but make sure there are no stranded tasks.
  MessageLoop::current()->RunAllPending();
}

}  // namespace ppapi
