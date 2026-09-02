/*
 * Copyright (C) 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <aidl/android/hardware/boot/BnBootControl.h>

namespace aidl::android::hardware::boot {

#define EXYNOS_SLOT_INFO_MAGIC "EXBC"
#define EXYNOS_SLOT_INFO_MAGIC_SIZE 4
#define SLOT_INFO_PARTITION "/slotinfo"
#define MAX_SLOT_NUMBER 2

constexpr size_t SLOTINFO_OFFSET = 2 * 1024;

struct slot_metadata {
    uint8_t bootable;
    uint8_t is_active;
    uint8_t boot_successful;
    uint8_t tries_remaining;
    uint8_t reserved[4];
} __attribute__((packed));

struct slot_data {
    uint8_t magic[EXYNOS_SLOT_INFO_MAGIC_SIZE];
    struct slot_metadata metadata[MAX_SLOT_NUMBER];
    MergeStatus merge_status : 8;
    uint8_t ota_flag;
    uint8_t reserved[10];
} __attribute__((packed));

static_assert(sizeof(struct slot_data) == 32,
              "check struct layout");

class BootControl final : public BnBootControl {
  public:
    BootControl();
    ::ndk::ScopedAStatus getActiveBootSlot(int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus getCurrentSlot(int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus getNumberSlots(int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus getSnapshotMergeStatus(
            ::aidl::android::hardware::boot::MergeStatus* _aidl_return) override;
    ::ndk::ScopedAStatus getSuffix(int32_t in_slot, std::string* _aidl_return) override;
    ::ndk::ScopedAStatus isSlotBootable(int32_t in_slot, bool* _aidl_return) override;
    ::ndk::ScopedAStatus isSlotMarkedSuccessful(int32_t in_slot, bool* _aidl_return) override;
    ::ndk::ScopedAStatus markBootSuccessful() override;
    ::ndk::ScopedAStatus setActiveBootSlot(int32_t in_slot) override;
    ::ndk::ScopedAStatus setSlotAsUnbootable(int32_t in_slot) override;
    ::ndk::ScopedAStatus setSnapshotMergeStatus(
            ::aidl::android::hardware::boot::MergeStatus in_status) override;

  private:
    bool IsValidSlot(int32_t slot);
    bool OpenSlotInfo();
    bool ReadSlot();
    bool WriteSlot();
    void ResetSlot();

    int slotinfo_fd_ = -1;
    struct slot_data slot_{};

    static constexpr const char* kSuffix[MAX_SLOT_NUMBER] = {"_a", "_b"};
};

}  // namespace aidl::android::hardware::boot
