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
#cgo CXXFLAGS: -I${SRCDIR} -I${SRCDIR}/libv8/linux/include -fno-rtti -fpic -std=c++11
#cgo LDFLAGS: -pthread -L${SRCDIR}/libv8/linux/lib -lv8_monolith
*/
import "C"

import (
    "fmt"
    "strconv"
    "sync"
    "unsafe"
)
import "runtime"

var initV8Once sync.Once

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

func (vm *VM) Load(path string) bool {
    if vm.disposed {
        return false
    }
    cPath := C.CString(path)
    defer func() {
        C.free(unsafe.Pointer(cPath))
    }()

    r := C.V8Load(vm.vmCPtr, C.CString(path), nil)
    if r == 2 {
        fmt.Println(C.GoString(C.V8LastException(vm.vmCPtr)))
    }
    return r == 0
}

func (vm *VM) DispatchEnter(sessionId string, addr string) int {
    if vm.disposed {
        return -1
    }

    cSessionId := C.CString(sessionId)
    cAddr := C.CString(addr)
    defer func() {
        C.free(unsafe.Pointer(cSessionId))
        C.free(unsafe.Pointer(cAddr))
    }()

    r := C.V8DispatchEnterEvent(vm.vmCPtr, cSessionId, cAddr)
    if r == 2 {
        fmt.Println(C.GoString(C.V8LastException(vm.vmCPtr)))
    }

    return int(r)
}

func (vm *VM) DispatchLeave(sessionId string, addr string) int {
    if vm.disposed {
        return -1
    }

    cSessionId := C.CString(sessionId)
    cAddr := C.CString(addr)
    defer func() {
        C.free(unsafe.Pointer(cSessionId))
        C.free(unsafe.Pointer(cAddr))
    }()

    r := C.V8DispatchLeaveEvent(vm.vmCPtr, cSessionId, cAddr)
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

func (vm *VM) DispatchMessage(sessionId string, msg map[interface{}] interface{}) int {
    if vm.disposed {
        return -1
    }

    cSessionId := C.CString(sessionId)
    m := C.V8CreateVMObject(vm.vmCPtr)
    defer func() {
        C.free(unsafe.Pointer(cSessionId))
        C.V8DisposeVMValue(m)
    }()

    transferGoMap2JsObject(vm, m, msg)

    r := C.V8DispatchMessageEvent(vm.vmCPtr, C.CString(sessionId), m)
    if r == 2 {
        fmt.Println(C.GoString(C.V8LastException(vm.vmCPtr)))
    }

    return int(r)
}