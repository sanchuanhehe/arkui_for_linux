/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Linux/Wayland native port of the macOS Objective-C InstanceIdGenerator.
// The original was an NSObject with +getAndIncrement / +get guarded by
// @synchronized(self); the portable C++ form is a static counter guarded by a
// std::mutex.

#ifndef FOUNDATION_ACE_ADAPTER_LINUX_STAGE_ABILITY_INSTANCE_ID_GENERATOR_H
#define FOUNDATION_ACE_ADAPTER_LINUX_STAGE_ABILITY_INSTANCE_ID_GENERATOR_H

#include <cstdint>

class InstanceIdGenerator {
public:
    /**
     * get and increment id
     * @return id
     */
    static int32_t GetAndIncrement();

    /**
     * get id
     * @return id
     */
    static int32_t Get();
};

#endif // FOUNDATION_ACE_ADAPTER_LINUX_STAGE_ABILITY_INSTANCE_ID_GENERATOR_H
