/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/third_party/blink/renderer/core/farbling/brave_session_cache.h"
#include "third_party/blink/public/platform/web_content_settings_client.h"
#include "ui/gfx/skia_span_util.h"

#define BRAVE_CANVAS_ASYNC_BLOB_CREATOR                                       \
  if (static_bitmap_image_loaded_) {                                          \
    sk_sp<SkImage> farbled_image = SkImages::RasterFromPixmapCopy(src_data_); \
    if (farbled_image && farbled_image->peekPixels(&src_data_)) {             \
      skia_image_ = std::move(farbled_image);                                 \
      brave::BraveSessionCache::From(*context_).PerturbCanvasPixels(          \
          gfx::SkPixmapToWritableSpan(src_data_), src_data_.width(),          \
          src_data_.height(), 0, 0, src_data_.width(), src_data_.height(),    \
          src_data_.rowBytes(), static_cast<int>(src_data_.colorType()));     \
    }                                                                         \
  }

#include <third_party/blink/renderer/core/html/canvas/canvas_async_blob_creator.cc>

#undef BRAVE_CANVAS_ASYNC_BLOB_CREATOR
