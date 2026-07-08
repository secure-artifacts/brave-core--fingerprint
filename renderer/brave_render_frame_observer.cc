// Copyright (c) 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/renderer/brave_render_frame_observer.h"

#include <string>

#include "content/public/renderer/render_frame.h"
#include "gin/converter.h"
#include "services/service_manager/public/cpp/binder_registry.h"
#include "third_party/blink/public/platform/scheduler/web_agent_group_scheduler.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "v8/include/v8-context.h"
#include "v8/include/v8-function-callback.h"
#include "v8/include/v8-function.h"
#include "v8/include/v8-isolate.h"
#include "v8/include/v8-object.h"
#include "v8/include/v8-primitive.h"

BraveRenderFrameObserver::BraveRenderFrameObserver(
    content::RenderFrame* render_frame)
    : content::RenderFrameObserver(render_frame) {}

BraveRenderFrameObserver::~BraveRenderFrameObserver() = default;

void BraveRenderFrameObserver::OnDestruct() {
  delete this;
}

void BraveRenderFrameObserver::OnInterfaceRequestForFrame(
    const std::string& interface_name,
    mojo::ScopedMessagePipeHandle* interface_pipe) {
  registry_.TryBindInterface(interface_name, interface_pipe);
}

// static
void BraveRenderFrameObserver::SayHello(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  info.GetReturnValue().Set(gin::StringToV8(isolate, "Hello world"));
}

void BraveRenderFrameObserver::DidClearWindowObject() {
  auto* web_frame = render_frame()->GetWebFrame();
  v8::Isolate* isolate = web_frame->GetAgentGroupScheduler()->Isolate();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Context> context = web_frame->MainWorldScriptContext();
  if (context.IsEmpty()) {
    return;
  }
  v8::Context::Scope context_scope(context);

  v8::Local<v8::Function> function =
      v8::Function::New(context, &BraveRenderFrameObserver::SayHello)
          .ToLocalChecked();
  context->Global()
      ->Set(context, gin::StringToV8(isolate, "sayHello"), function)
      .Check();
}
