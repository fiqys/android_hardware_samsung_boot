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

#include "BootControl.h"

#include <cstring>

#include <fcntl.h>
#include <unistd.h>

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <fstab/fstab.h>

using ndk::ScopedAStatus;
using android::fs_mgr::Fstab;
using android::fs_mgr::ReadDefaultFstab;

namespace aidl::android::hardware::boot {

BootControl::BootControl() {
    CHECK(OpenSlotInfo()) << "Failed to open " << SLOT_INFO_PARTITION;
    CHECK(ReadSlot()) << "Failed to read " << SLOT_INFO_PARTITION;
}

bool BootControl::IsValidSlot(int32_t slot) {
    return slot >= 0 && slot < MAX_SLOT_NUMBER;
}

bool BootControl::OpenSlotInfo() {
    if (slotinfo_fd_ != -1) {
        return true;
    }

    Fstab fstab;
    if (!ReadDefaultFstab(&fstab)) {
        LOG(ERROR) << "Cannot open fstab";
        return false;
    }

    for (const auto& entry : fstab) {
        if (entry.mount_point == SLOT_INFO_PARTITION) {
            if (entry.blk_device.empty()) {
                LOG(ERROR) << "Block device for " << SLOT_INFO_PARTITION << " is empty";
                return false;
            }
            slotinfo_fd_ = open(entry.blk_device.c_str(), O_RDWR);
            if (slotinfo_fd_ == -1) {
                PLOG(ERROR) << "Cannot open " << entry.blk_device;
                return false;
            }
            return true;
        }
    }

    LOG(ERROR) << "Cannot find " << SLOT_INFO_PARTITION << " in fstab";
    return false;
}

bool BootControl::ReadSlot() {
    if (slotinfo_fd_ == -1) {
        return false;
    }
    if (lseek(slotinfo_fd_, SLOTINFO_OFFSET, SEEK_SET) != static_cast<off_t>(SLOTINFO_OFFSET)) {
        PLOG(ERROR) << "Cannot seek " << SLOT_INFO_PARTITION;
        return false;
    }
    if (read(slotinfo_fd_, &slot_, sizeof(slot_)) != static_cast<ssize_t>(sizeof(slot_))) {
        PLOG(ERROR) << "Cannot read " << SLOT_INFO_PARTITION;
        return false;
    }
    if (memcmp(slot_.magic, EXYNOS_SLOT_INFO_MAGIC, EXYNOS_SLOT_INFO_MAGIC_SIZE) != 0) {
        ResetSlot();
        if (!WriteSlot()) {
            LOG(ERROR) << "Failed to reset " << SLOT_INFO_PARTITION;
            return false;
        }
    }
    return true;
}

bool BootControl::WriteSlot() {
    if (slotinfo_fd_ == -1 && !OpenSlotInfo()) {
        return false;
    }
    if (lseek(slotinfo_fd_, SLOTINFO_OFFSET, SEEK_SET) != static_cast<off_t>(SLOTINFO_OFFSET)) {
        PLOG(ERROR) << "Cannot seek " << SLOT_INFO_PARTITION;
        return false;
    }
    if (write(slotinfo_fd_, &slot_, sizeof(slot_)) != static_cast<ssize_t>(sizeof(slot_))) {
        PLOG(ERROR) << "Cannot write " << SLOT_INFO_PARTITION;
        return false;
    }
    return true;
}

void BootControl::ResetSlot() {
    LOG(WARNING) << "Resetting slot info";
    memcpy(slot_.magic, EXYNOS_SLOT_INFO_MAGIC, EXYNOS_SLOT_INFO_MAGIC_SIZE);
    slot_.merge_status = MergeStatus::NONE;
    slot_.ota_flag = 0;
    for (int i = 0; i < MAX_SLOT_NUMBER; i++) {
        slot_.metadata[i].bootable = true;
        slot_.metadata[i].is_active = false;
        slot_.metadata[i].boot_successful = false;
        slot_.metadata[i].tries_remaining = 7;
    }
    slot_.metadata[0].is_active = true;
}

ScopedAStatus BootControl::getActiveBootSlot(int32_t* _aidl_return) {
    for (int i = 0; i < MAX_SLOT_NUMBER; i++) {
        if (slot_.metadata[i].is_active) {
            *_aidl_return = i;
            return ScopedAStatus::ok();
        }
    }
    *_aidl_return = 0;
    return ScopedAStatus::ok();
}

ScopedAStatus BootControl::getCurrentSlot(int32_t* _aidl_return) {
    std::string suffix = ::android::base::GetProperty("ro.boot.slot_suffix", "");
    for (int i = 0; i < MAX_SLOT_NUMBER; i++) {
        if (suffix == kSuffix[i]) {
            *_aidl_return = i;
            return ScopedAStatus::ok();
        }
    }
    LOG(ERROR) << "ro.boot.slot_suffix is invalid: " << suffix;
    *_aidl_return = -1;
    return ScopedAStatus::ok();
}

ScopedAStatus BootControl::getNumberSlots(int32_t* _aidl_return) {
    *_aidl_return = MAX_SLOT_NUMBER;
    return ScopedAStatus::ok();
}

ScopedAStatus BootControl::getSnapshotMergeStatus(MergeStatus* _aidl_return) {
    if (!ReadSlot()) {
        LOG(ERROR) << "Failed to read " << SLOT_INFO_PARTITION;
        *_aidl_return = MergeStatus::UNKNOWN;
        return ScopedAStatus::ok();
    }
    *_aidl_return = slot_.merge_status;
    return ScopedAStatus::ok();
}

ScopedAStatus BootControl::getSuffix(int32_t in_slot, std::string* _aidl_return) {
    if (!IsValidSlot(in_slot)) {
        _aidl_return->clear();
    } else {
        *_aidl_return = kSuffix[in_slot];
    }
    return ScopedAStatus::ok();
}

ScopedAStatus BootControl::isSlotBootable(int32_t in_slot, bool* _aidl_return) {
    if (!IsValidSlot(in_slot)) {
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(
                INVALID_SLOT, (std::string("Invalid slot ") + std::to_string(in_slot)).c_str());
    }
    if (!ReadSlot()) {
        LOG(ERROR) << "Failed to read " << SLOT_INFO_PARTITION;
        *_aidl_return = false;
        return ScopedAStatus::ok();
    }
    *_aidl_return = slot_.metadata[in_slot].bootable;
    return ScopedAStatus::ok();
}

ScopedAStatus BootControl::isSlotMarkedSuccessful(int32_t in_slot, bool* _aidl_return) {
    if (!IsValidSlot(in_slot)) {
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(
                INVALID_SLOT, (std::string("Invalid slot ") + std::to_string(in_slot)).c_str());
    }
    if (!ReadSlot()) {
        LOG(ERROR) << "Failed to read " << SLOT_INFO_PARTITION;
        *_aidl_return = false;
        return ScopedAStatus::ok();
    }
    *_aidl_return = slot_.metadata[in_slot].boot_successful;
    return ScopedAStatus::ok();
}

ScopedAStatus BootControl::markBootSuccessful() {
    int32_t current;
    getCurrentSlot(&current);
    if (!IsValidSlot(current)) {
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(COMMAND_FAILED,
                                                                  "Invalid current slot");
    }
    if (!ReadSlot()) {
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(COMMAND_FAILED,
                                                                  "Operation failed");
    }
    slot_.metadata[current].boot_successful = true;
    if (!WriteSlot()) {
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(COMMAND_FAILED,
                                                                  "Operation failed");
    }
    return ScopedAStatus::ok();
}

ScopedAStatus BootControl::setActiveBootSlot(int32_t in_slot) {
    if (!IsValidSlot(in_slot)) {
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(
                INVALID_SLOT, (std::string("Invalid slot ") + std::to_string(in_slot)).c_str());
    }

    int32_t current;
    getCurrentSlot(&current);

    if (!ReadSlot()) {
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(COMMAND_FAILED,
                                                                  "Operation failed");
    }

    slot_.metadata[in_slot].bootable = true;
    slot_.metadata[in_slot].is_active = true;
    slot_.metadata[in_slot].boot_successful = false;
    slot_.metadata[in_slot].tries_remaining = 7;
    slot_.metadata[1 - in_slot].is_active = false;
    slot_.ota_flag = (in_slot != current) ? 1 : 0;

    if (!WriteSlot()) {
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(COMMAND_FAILED,
                                                                  "Operation failed");
    }
    return ScopedAStatus::ok();
}

ScopedAStatus BootControl::setSlotAsUnbootable(int32_t in_slot) {
    if (!IsValidSlot(in_slot)) {
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(
                INVALID_SLOT, (std::string("Invalid slot ") + std::to_string(in_slot)).c_str());
    }

    if (!ReadSlot()) {
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(COMMAND_FAILED,
                                                                  "Operation failed");
    }

    slot_.metadata[in_slot].bootable = false;
    slot_.metadata[in_slot].is_active = false;
    slot_.metadata[in_slot].boot_successful = false;
    slot_.metadata[in_slot].tries_remaining = 0;

    int32_t other = 1 - in_slot;
    if (slot_.metadata[other].bootable) {
        slot_.metadata[other].is_active = true;
    }

    if (!WriteSlot()) {
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(COMMAND_FAILED,
                                                                  "Operation failed");
    }
    return ScopedAStatus::ok();
}

ScopedAStatus BootControl::setSnapshotMergeStatus(MergeStatus in_status) {
    if (!ReadSlot()) {
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(COMMAND_FAILED,
                                                                  "Operation failed");
    }
    slot_.merge_status = in_status;
    if (!WriteSlot()) {
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(COMMAND_FAILED,
                                                                  "Operation failed");
    }
    return ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::boot
