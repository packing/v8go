/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef V8_BRIDGE_H
#define V8_BRIDGE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

//打开v8指针压缩
#define V8_COMPRESS_POINTERS

#ifdef __cplusplus
extern "C" {
#endif

#define v8KindStart       0
#define v8KindUndefined   1
#define v8KindNull        (1 << 1)
#define v8KindString      (1 << 2)
#define v8KindInt         (1 << 3)
#define v8KindUint        (1 << 4)
#define v8KindBigInt      (1 << 5)
#define v8KindNumber      (1 << 6)
#define v8KindBool        (1 << 7)
#define v8KindObject      (1 << 8)
#define v8KindArray       (1 << 9)


typedef struct _VM VM;
typedef VM *VMPtr;

typedef struct _V8StringArrays V8StringArrays;
typedef V8StringArrays *V8StringArraysPtr;

//typedef struct _VMObject VMObject;
//typedef VMObject *VMObjectPtr;

typedef struct _VMValue VMValue;
typedef VMValue *VMValuePtr;

typedef const void *FunctionCallbackInfoPtr;

typedef const char *KEY;

typedef int (*OutputCallback) (const char *, FunctionCallbackInfoPtr);
extern OutputCallback outputCallback;

const char * V8Version();
void V8Init();
void V8Dispose();
const char *V8WorkDir();
const char *V8LastException(VMPtr);
void V8SetOutputCallback(OutputCallback);

VMPtr V8NewVM();
void V8DisposeVM(VMPtr);
void V8PrintVMMemStat(VMPtr vmPtr);

void V8SetVMAssociatedSourceAddr(VMPtr vmPtr, const char *addr);
void V8SetVMAssociatedSourceId(VMPtr vmPtr, uint64_t id);

const char * V8GetVMAssociatedSourceAddr(VMPtr vmPtr);
uint64_t V8GetVMAssociatedSourceId(VMPtr vmPtr);

int V8Load(VMPtr, const char *, const char *);
int V8LoadModule(VMPtr, const char *, const char *, const char *);

int V8DispatchEnterEvent(VMPtr vmPtr, uint64_t sessionId, const char *addr);
int V8DispatchLeaveEvent(VMPtr vmPtr, uint64_t sessionId, const char *addr);
int V8DispatchMessageEvent(VMPtr vmPtr, uint64_t sessionId, VMValuePtr vmValuePtr);

size_t V8GetStringArraysLength(V8StringArraysPtr v8StringArraysPtr);
const char *V8GetStringArraysItem(V8StringArraysPtr v8StringArraysPtr, int index);
void V8ReleaseStringArrays(V8StringArraysPtr v8StringArraysPtr);

VMValuePtr V8CreateVMObject(VMPtr vmPtr);
VMValuePtr V8CreateVMArray(VMPtr vmPtr, int length);
void V8DisposeVMValue(VMValuePtr vmValuePtr);
void V8ObjectSetString(VMPtr vmPtr, VMValuePtr o, const char *name, const char *val);
void V8ObjectSetStringForIndex(VMPtr vmPtr, VMValuePtr o, int index, const char *val);
void V8ObjectSetInteger(VMPtr vmPtr, VMValuePtr o, const char *name, int64_t val);
void V8ObjectSetIntegerForIndex(VMPtr vmPtr, VMValuePtr o, int index, int64_t val);
void V8ObjectSetValue(VMPtr vmPtr, VMValuePtr o, const char *name, VMValuePtr val);
void V8ObjectSetValueForIndex(VMPtr vmPtr, VMValuePtr o, int index, VMValuePtr val);
void V8ObjectSetFloat(VMPtr vmPtr, VMValuePtr o, const char *name,  double val);
void V8ObjectSetFloatForIndex(VMPtr vmPtr, VMValuePtr o, int index, double val);
void V8ObjectSetBoolean(VMPtr vmPtr, VMValuePtr o, const char *name, bool val);
void V8ObjectSetBooleanForIndex(VMPtr vmPtr, VMValuePtr o, int index, bool val);

V8StringArraysPtr V8ObjectGetKeys(VMPtr vmPtr, VMValuePtr o);
size_t V8ObjectGetLength(VMPtr vmPtr, VMValuePtr o);
VMValuePtr V8GetObjectValue(VMPtr vmPtr, VMValuePtr o, const char *key);
VMValuePtr V8GetObjectValueAtIndex(VMPtr vmPtr, VMValuePtr o, uint32_t index);

unsigned int V8GetVMValueKind(VMValuePtr vmValuePtr);

const char *V8ValueAsString(VMPtr vmPtr, VMValuePtr o, const char *def);
int64_t V8ValueAsInt(VMPtr vmPtr, VMValuePtr o, int64_t def);
uint64_t V8ValueAsUint(VMPtr vmPtr, VMValuePtr o, uint64_t def);
double V8ValueAsFloat(VMPtr vmPtr, VMValuePtr o, double def);
bool V8ValueAsBoolean(VMPtr vmPtr, VMValuePtr o, bool def);

#ifdef __cplusplus
}
#endif

#endif  // !defined(V8_BRIDGE_H)