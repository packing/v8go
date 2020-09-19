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

package v8go

/*
#include <stdlib.h>
#include <string.h>
#include "v8bridge.h"
#cgo CXXFLAGS: -I${SRCDIR} -I${SRCDIR}/libv8/darwin/include -fno-rtti -fpic -std=c++11 -DGOOUTPUT
#cgo LDFLAGS: -pthread -L${SRCDIR}/libv8/darwin/lib -lv8_libbase -lv8_libplatform -lv8_monolith
*/
import "C"

import (
    "fmt"
    "strconv"
    "sync"
    "unsafe"
)
import "runtime"

var (
    goV8KindStart       = C.uint(0)
    goV8KindUndefined   = C.uint(1)
    goV8KindNull        = C.uint(1 << 1)
    goV8KindString      = C.uint(1 << 2)
    goV8KindInt         = C.uint(1 << 3)
    goV8KindUint        = C.uint(1 << 4)
    goV8KindBigInt      = C.uint(1 << 5)
    goV8KindNumber      = C.uint(1 << 6)
    goV8KindBool        = C.uint(1 << 7)
    goV8KindObject      = C.uint(1 << 8)
    goV8KindArray       = C.uint(1 << 9)
)

var initV8Once sync.Once

var OnSendMessage func(string, uint64, interface{}) int = nil
var OnSendMessageTo func(interface{}) int = nil
var OnOutput func(string) = nil

//export GoOutput
func GoOutput(c *C.char) {
    s := C.GoString(c)
    if OnOutput != nil {
        OnOutput(s)
    } else {
        fmt.Println(s)
    }
}

//export GoSend
func GoSend(vm C.VMPtr, jsValue C.VMValuePtr) C.int {
    if OnSendMessage == nil {
        return C.int(0)
    }

    sAddr := C.GoString(C.V8GetVMAssociatedSourceAddr(vm))
    sId := uint64(C.V8GetVMAssociatedSourceId(vm))

    data := transferJsValue2GoValue(vm, jsValue)
    if data != nil {
        OnSendMessage(sAddr, sId, data)
    }

    return C.int(0)
}

//export GoSendTo
func GoSendTo(vm C.VMPtr, jsValue C.VMValuePtr) C.int {
    if OnSendMessageTo == nil {
        return C.int(0)
    }

    data := transferJsValue2GoValue(vm, jsValue)
    if data != nil {
        OnSendMessageTo(data)
    }

    return C.int(0)
}

type VM struct {
    vmCPtr C.VMPtr
    disposed bool
}

func Version() string {
    return C.GoString(C.V8Version())
}

func Dispose() {
    C.V8Dispose()
}

func Init() {
    initV8Once.Do(func() {
        C.V8Init()
    })
}

func CreateVM() *VM {
    vm := new(VM)

    vm.vmCPtr = C.V8NewVM()
    vm.disposed = false

    runtime.SetFinalizer(vm, func(vmWillDispose *VM) {
        vmWillDispose.Dispose()
    })
    return vm
}

func (vm *VM) Dispose() {
    C.V8DisposeVM(vm.vmCPtr)
    vm.disposed = true
}

func (vm *VM) PrintMemStat() {
    C.V8PrintVMMemStat(vm.vmCPtr)
}

func (vm *VM) Load(path string) bool {
    if vm.disposed {
        return false
    }
    cPath := C.CString(path)
    defer func() {
        C.free(unsafe.Pointer(cPath))
    }()

    r := C.V8Load(vm.vmCPtr, cPath, nil)
    if r == 2 {
        fmt.Println(C.GoString(C.V8LastException(vm.vmCPtr)))
    }
    if r == -1 {
        fmt.Printf("\nScript entryfile %s is not exists!\n\n", path)
    }
    return r == 0
}

func (vm *VM) SetAssociatedSourceAddr(addr string) {
    cAddr := C.CString(addr)
    defer func() {
        C.free(unsafe.Pointer(cAddr))
    }()
    C.V8SetVMAssociatedSourceAddr(vm.vmCPtr, cAddr)
}

func (vm *VM) SetAssociatedSourceId(id uint64) {
    C.V8SetVMAssociatedSourceId(vm.vmCPtr, C.uint64_t(id))
}

func (vm *VM) GetAssociatedSourceAddr() string {
    return C.GoString(C.V8GetVMAssociatedSourceAddr(vm.vmCPtr))
}

func (vm *VM) GetAssociatedSourceId() uint64 {
    return uint64(C.V8GetVMAssociatedSourceId(vm.vmCPtr))
}

func (vm *VM) DispatchEnter(sessionId uint64, addr string) int {
    if vm.disposed {
        return -1
    }

    cAddr := C.CString(addr)
    defer func() {
        C.free(unsafe.Pointer(cAddr))
    }()

    r := C.V8DispatchEnterEvent(vm.vmCPtr, C.uint64_t(sessionId), cAddr)
    if r == 2 {
        fmt.Println(C.GoString(C.V8LastException(vm.vmCPtr)))
    }

    return int(r)
}

func (vm *VM) DispatchLeave(sessionId uint64, addr string) int {
    if vm.disposed {
        return -1
    }

    cAddr := C.CString(addr)
    defer func() {
        C.free(unsafe.Pointer(cAddr))
    }()

    r := C.V8DispatchLeaveEvent(vm.vmCPtr, C.uint64_t(sessionId), cAddr)
    if r == 2 {
        fmt.Println(C.GoString(C.V8LastException(vm.vmCPtr)))
    }
    return int(r)
}

func transferGoArray2JsArray(vm *VM, jsArray C.VMValuePtr, goArray []interface{}) {
    for i, vi := range goArray {
        switch vi.(type) {
        case string:
            func(index int, val interface{}){
                cV := C.CString(val.(string))
                defer C.free(unsafe.Pointer(cV))
                C.V8ObjectSetStringForIndex(vm.vmCPtr, jsArray, C.int(index), cV)
            }(i, vi)

        case int: C.V8ObjectSetIntegerForIndex(vm.vmCPtr, jsArray, C.int(i), C.int64_t(int64(vi.(int))))
        case int8: C.V8ObjectSetIntegerForIndex(vm.vmCPtr, jsArray, C.int(i), C.int64_t(int64(vi.(int8))))
        case int16: C.V8ObjectSetIntegerForIndex(vm.vmCPtr, jsArray, C.int(i), C.int64_t(int64(vi.(int16))))
        case int32: C.V8ObjectSetIntegerForIndex(vm.vmCPtr, jsArray, C.int(i), C.int64_t(int64(vi.(int32))))
        case int64: C.V8ObjectSetIntegerForIndex(vm.vmCPtr, jsArray, C.int(i), C.int64_t(vi.(int64)))

        case uint: C.V8ObjectSetIntegerForIndex(vm.vmCPtr, jsArray, C.int(i), C.int64_t(int64(vi.(uint))))
        case uint8: C.V8ObjectSetIntegerForIndex(vm.vmCPtr, jsArray, C.int(i), C.int64_t(int64(vi.(uint8))))
        case uint16: C.V8ObjectSetIntegerForIndex(vm.vmCPtr, jsArray, C.int(i), C.int64_t(int64(vi.(uint16))))
        case uint32: C.V8ObjectSetIntegerForIndex(vm.vmCPtr, jsArray, C.int(i), C.int64_t(int64(vi.(uint32))))
        case uint64: C.V8ObjectSetIntegerForIndex(vm.vmCPtr, jsArray, C.int(i), C.int64_t(int64(vi.(uint64))))

        case bool: C.V8ObjectSetBooleanForIndex(vm.vmCPtr, jsArray, C.int(i), C._Bool(vi.(bool)))
        case float32: C.V8ObjectSetFloatForIndex(vm.vmCPtr, jsArray, C.int(i), C.double(float64(vi.(float32))))
        case float64: C.V8ObjectSetFloatForIndex(vm.vmCPtr, jsArray, C.int(i), C.double(vi.(float64)))

        case map[interface{}] interface{}:
            func(index int, val interface{}) {
                sm := C.V8CreateVMObject(vm.vmCPtr)
                defer C.V8DisposeVMValue(sm)
                transferGoMap2JsObject(vm, sm, val.(map[interface{}] interface{}))
                C.V8ObjectSetValueForIndex(vm.vmCPtr, jsArray, C.int(index), sm)
            }(i, vi)
        case [] interface{}:
            func(index int, val interface{}) {
                rv := val.([]interface{})
                sa := C.V8CreateVMArray(vm.vmCPtr, C.int(len(rv)))
                defer C.V8DisposeVMValue(sa)
                transferGoArray2JsArray(vm, sa, rv)
                C.V8ObjectSetValueForIndex(vm.vmCPtr, jsArray, C.int(index), sa)
            }(i, vi)
        }
    }
}

func transferGoMap2JsObject(vm *VM, jsMap C.VMValuePtr, goMap map[interface{}] interface{}) {
    for k, v := range goMap {
        sk := ""
        switch k.(type) {
        case string: sk = k.(string)
        case int: sk = strconv.FormatInt(int64(k.(int)), 10)
        case int8: sk = strconv.FormatInt(int64(k.(int8)), 10)
        case int16: sk = strconv.FormatInt(int64(k.(int16)), 10)
        case int32: sk = strconv.FormatInt(int64(k.(int32)), 10)
        case int64: sk = strconv.FormatInt(k.(int64), 10)
        case uint: sk = strconv.FormatUint(uint64(k.(uint)), 10)
        case uint8: sk = strconv.FormatUint(uint64(k.(uint8)), 10)
        case uint16: sk = strconv.FormatUint(uint64(k.(uint16)), 10)
        case uint32: sk = strconv.FormatUint(uint64(k.(uint32)), 10)
        case uint64: sk = strconv.FormatUint(uint64(k.(uint64)), 10)
        default:
            continue
        }

        func(kstr string, vi interface{}) {
            cK := C.CString(kstr)
            defer C.free(unsafe.Pointer(cK))
            switch vi.(type) {
            case string:
                func(val interface{}){
                    cV := C.CString(val.(string))
                    defer C.free(unsafe.Pointer(cV))
                    C.V8ObjectSetString(vm.vmCPtr, jsMap, cK, cV)
                }(vi)

            case int: C.V8ObjectSetInteger(vm.vmCPtr, jsMap, cK, C.int64_t(int64(vi.(int))))
            case int8: C.V8ObjectSetInteger(vm.vmCPtr, jsMap, cK, C.int64_t(int64(vi.(int8))))
            case int16: C.V8ObjectSetInteger(vm.vmCPtr, jsMap, cK, C.int64_t(int64(vi.(int16))))
            case int32: C.V8ObjectSetInteger(vm.vmCPtr, jsMap, cK, C.int64_t(int64(vi.(int32))))
            case int64: C.V8ObjectSetInteger(vm.vmCPtr, jsMap, cK, C.int64_t(vi.(int64)))

            case uint: C.V8ObjectSetInteger(vm.vmCPtr, jsMap, cK, C.int64_t(int64(vi.(uint))))
            case uint8: C.V8ObjectSetInteger(vm.vmCPtr, jsMap, cK, C.int64_t(int64(vi.(uint8))))
            case uint16: C.V8ObjectSetInteger(vm.vmCPtr, jsMap, cK, C.int64_t(int64(vi.(uint16))))
            case uint32: C.V8ObjectSetInteger(vm.vmCPtr, jsMap, cK, C.int64_t(int64(vi.(uint32))))
            case uint64: C.V8ObjectSetInteger(vm.vmCPtr, jsMap, cK, C.int64_t(int64(vi.(uint64))))

            case bool: C.V8ObjectSetBoolean(vm.vmCPtr, jsMap, cK, C._Bool(vi.(bool)))
            case float32: C.V8ObjectSetFloat(vm.vmCPtr, jsMap, cK, C.double(float64(vi.(float32))))
            case float64: C.V8ObjectSetFloat(vm.vmCPtr, jsMap, cK, C.double(vi.(float64)))

            case map[interface{}] interface{}:
                func(val interface{}) {
                    sm := C.V8CreateVMObject(vm.vmCPtr)
                    defer C.V8DisposeVMValue(sm)
                    transferGoMap2JsObject(vm, sm, val.(map[interface{}] interface{}))
                    C.V8ObjectSetValue(vm.vmCPtr, jsMap, cK, sm)
                }(vi)
            case [] interface{}:
                func(val interface{}) {
                    rv := val.([]interface{})
                    sa := C.V8CreateVMArray(vm.vmCPtr, C.int(len(rv)))
                    defer C.V8DisposeVMValue(sa)
                    transferGoArray2JsArray(vm, sa, rv)
                    C.V8ObjectSetValue(vm.vmCPtr, jsMap, cK, sa)
                }(vi)
            }
        }(sk, v)
    }
}

func (vm *VM) DispatchMessage(sessionId uint64, msg map[interface{}] interface{}) int {
    if vm.disposed {
        return -1
    }

    m := C.V8CreateVMObject(vm.vmCPtr)
    defer func() {
        C.V8DisposeVMValue(m)
    }()

    transferGoMap2JsObject(vm, m, msg)

    r := C.V8DispatchMessageEvent(vm.vmCPtr, C.uint64_t(sessionId), m)
    if r == 2 {
        fmt.Println(C.GoString(C.V8LastException(vm.vmCPtr)))
    }

    return int(r)
}

func transferJsArray2GoArray(vm C.VMPtr, jsArray C.VMValuePtr) [] interface{} {
    length := int(C.V8ObjectGetLength(vm, jsArray))
    l := make([] interface{}, length)

    for i := 0; i < length; i ++ {
        val := C.V8GetObjectValueAtIndex(vm, jsArray, C.uint(i))
        l[i] = transferJsValue2GoValue(vm, val)
    }

    return l
}

func transferJsObject2GoObject(vm C.VMPtr, jsObject C.VMValuePtr) map[interface{}] interface{} {
    cKeys := C.V8ObjectGetKeys(vm, jsObject)
    defer C.V8ReleaseStringArrays(cKeys)

    length := int(C.V8GetStringArraysLength(cKeys))

    m := make(map[interface{}] interface{})

    for i := 0; i < length; i ++ {
        func(index int, mDst map[interface{}] interface{}) {
            k := C.V8GetStringArraysItem(cKeys, C.int(index))
            nk, e := strconv.Atoi(C.GoString(k))
            if e != nil {
                return
            }
            val := C.V8GetObjectValue(vm, jsObject, k)
            defer C.V8DisposeVMValue(val)
            mDst[nk] = transferJsValue2GoValue(vm, val)
        }(i, m)
    }

    return m
}

func transferJsValue2GoValue(vm C.VMPtr, jsValue C.VMValuePtr) interface{} {
    cEmptyStr := C.CString("")
    defer C.free(unsafe.Pointer(cEmptyStr))
    if (C.V8GetVMValueKind(jsValue) & goV8KindUndefined) == goV8KindUndefined {
        return nil
    } else if (C.V8GetVMValueKind(jsValue) & goV8KindNull) == goV8KindNull {
        return nil
    } else if (C.V8GetVMValueKind(jsValue) & goV8KindString) == goV8KindString {
        return C.GoString(C.V8ValueAsString(vm, jsValue, cEmptyStr))
    } else if (C.V8GetVMValueKind(jsValue) & goV8KindArray) == goV8KindArray {
        return transferJsArray2GoArray(vm, jsValue)
    } else if (C.V8GetVMValueKind(jsValue) & goV8KindObject) == goV8KindObject {
        return transferJsObject2GoObject(vm, jsValue)
    } else if (C.V8GetVMValueKind(jsValue) & goV8KindUint) == goV8KindUint {
        return uint64(C.V8ValueAsUint(vm, jsValue, C.uint64_t(0)))
    } else if (C.V8GetVMValueKind(jsValue) & goV8KindInt) == goV8KindInt {
        return int64(C.V8ValueAsInt(vm, jsValue, C.int64_t(0)))
    } else if (C.V8GetVMValueKind(jsValue) & goV8KindBigInt) == goV8KindBigInt {
        return int64(C.V8ValueAsInt(vm, jsValue, C.int64_t(0)))
    } else if (C.V8GetVMValueKind(jsValue) & goV8KindNumber) == goV8KindNumber {
        return float64(C.V8ValueAsFloat(vm, jsValue, C.double(0)))
    } else if (C.V8GetVMValueKind(jsValue) & goV8KindBool) == goV8KindBool {
        return bool(C.V8ValueAsBoolean(vm, jsValue, false))
    }
    return nil
}