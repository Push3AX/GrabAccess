/** @file
  Copyright (C) 2014 - 2017, CupertinoNet.  All rights reserved.<BR>

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
**/

#ifndef APPLE_EVENT_MIN_H
#define APPLE_EVENT_MIN_H

//
// This file is based on AppleEvent.h taken from https://github.com/CupertinoNet.
// The declarations present in this file were cut for the needs of CrScreenshotDxe,
// check the original source for complete definitions and the implementation.
//

#define APPLE_EVENT_PROTOCOL_REVISION  0x00000007

#define APPLE_EVENT_PROTOCOL_GUID  \
  { 0x33BE0EF1, 0x89C9, 0x4A6D,    \
    { 0xBB, 0x9F, 0x69, 0xDC, 0x8D, 0xD5, 0x16, 0xB9 } }

#define APPLE_EVENT_TYPE_KEY_UP              BIT9

#define APPLE_MODIFIER_LEFT_CONTROL  BIT0
#define APPLE_MODIFIER_LEFT_OPTION   BIT2

typedef UINT32 APPLE_EVENT_TYPE;
typedef UINT16 APPLE_MODIFIER_MAP;
typedef VOID *APPLE_EVENT_HANDLE;

typedef struct {
  UINT16         NumberOfKeyPairs;
  EFI_INPUT_KEY  InputKey;
} APPLE_KEY_EVENT_DATA;

typedef struct {
  UINT64                 CreationTime;
  APPLE_EVENT_TYPE       EventType;
  APPLE_KEY_EVENT_DATA   *KeyData;
  APPLE_MODIFIER_MAP     Modifiers;
} APPLE_EVENT_INFORMATION;

typedef
VOID
(EFIAPI *APPLE_EVENT_NOTIFY_FUNCTION)(
  IN APPLE_EVENT_INFORMATION  *Information,
  IN VOID                     *NotifyContext
  );

typedef
EFI_STATUS
(*EVENT_REGISTER_HANDLER)(
  IN  APPLE_EVENT_TYPE             Type,
  IN  APPLE_EVENT_NOTIFY_FUNCTION  NotifyFunction,
  OUT APPLE_EVENT_HANDLE           *Handle,
  IN  VOID                         *NotifyContext
  );

typedef struct {
  UINT32                    Revision;
  EVENT_REGISTER_HANDLER    RegisterHandler;
} APPLE_EVENT_PROTOCOL;

#endif
