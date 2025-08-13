// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/embedder/embedder_android_asset.h"

#if FML_OS_ANDROID

namespace flutter {

std::unique_ptr<fml::Mapping> CreateAndroidAssetMapping(
    const char* asset_path) {}

}  // namespace flutter

#endif  // FML_OS_ANDROID
