// Copyright 2023-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Application.h"

#include <memory>

int main(int argc, char *argv[])
{
  auto app = std::make_unique<anari_cat::Application>(argc, argv);
  return app->exec();
}
