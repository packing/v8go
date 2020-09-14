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

typedef struct _VM VM;
typedef VM *VMPtr;

typedef struct _VMObject VMObject;
typedef VMObject *VMObjectPtr;

typedef struct _VMValue VMValue;
typedef VMValue *VMValuePtr;

typedef const void *FunctionCallbackInfoPtr;

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

int V8Load(VMPtr, const char *, const char *);
int V8LoadModule(VMPtr, const char *, const char *, const char *);

int V8DispatchEnterEvent(VMPtr vmPtr, const char * sessionId, const char *addr);
int V8DispatchLeaveEvent(VMPtr vmPtr, const char * sessionId, const char *addr);
int V8DispatchMessageEvent(VMPtr vmPtr, const char * sessionId, VMValuePtr vmValuePtr);

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
#ifdef __cplusplus
}
#endif

#endif  // !defined(V8_BRIDGE_H)