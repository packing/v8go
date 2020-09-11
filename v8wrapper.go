package main

/*
#include <stdlib.h>
#include <string.h>
#include "v8bridge.h"
#cgo CXXFLAGS: -I${SRCDIR} -I${SRCDIR}/libv8/include -fno-rtti -fpic -std=c++11
#cgo LDFLAGS: -pthread -L${SRCDIR}/libv8/lib -lv8_libbase -lv8_libplatform -lv8_monolith
*/
import "C"

import "errors"

import "unsafe"
import "sync"
import "runtime"

type workerTableIndex int

var workerTableLock sync.Mutex

// These are used for handling ModuleResolverCallbacks per LoadModule invocation
var resolverTableLock sync.Mutex
var nextResolverToken int
var resolverFuncs = make(map[int]ModuleResolverCallback)

// This table will store all pointers to all active workers. Because we can't safely
// pass pointers to Go objects to C, we instead pass a key to this table.
var workerTable = make(map[workerTableIndex]*worker)

// Keeps track of the last used table index. Incremeneted when a worker is created.
var workerTableNextAvailable workerTableIndex = 0

// To receive messages from javascript.
type ReceiveMessageCallback func(msg []byte) []byte

// To resolve modules from javascript.
type ModuleResolverCallback func(moduleName, referrerName string) int

// Don't init V8 more than once.
var initV8Once sync.Once

// Internal worker struct which is stored in the workerTable.
// Weak-ref pattern https://groups.google.com/forum/#!topic/golang-nuts/1ItNOOj8yW8/discussion
type worker struct {
    cWorker    *C.worker
    cb         ReceiveMessageCallback
    tableIndex workerTableIndex
}

// This is a golang wrapper around a single V8 Isolate.
type Worker struct {
    *worker
    disposed bool
}

// Return the V8 version E.G. "6.6.164-v8worker2"
func Version() string {
    return C.GoString(C.worker_version())
}

// Sets V8 flags. Returns the input args but with the V8 flags removed.
// Use --help to print a list of flags to stdout.
func SetFlags(args []string) []string {
    // We need to turn args into a **char so it can be modified by V8.
    // Then we will turn the result back into a Go array and return it.

    // V8 ignores the first arg. To workaround this, unshift a dummy element on to
    // the args and shift it off at the end.
    args = append([]string{"dummy"}, args...)

    // Step 1: turn args into a C array called argv.
    ptrSize := C.size_t(unsafe.Sizeof(uintptr(0)))
    argv := C.malloc(ptrSize * C.size_t(len(args)))
    defer C.free(unsafe.Pointer(argv))
    // Type system abuse here. We choose a large constant, 10000, to fool Go.
    a := (*[10000]*C.char)(argv)
    for i := 0; i < len(args); i++ {
        cstr := unsafe.Pointer(C.CString(args[i]))
        defer C.free(cstr)
        a[i] = (*C.char)(cstr)
    }
    argc := C.int(len(args))
    argcPtr := (*C.int)(unsafe.Pointer(&argc))
    argvPtr := (**C.char)(unsafe.Pointer(argv))
    C.worker_set_flags(argcPtr, argvPtr)
    // Step 2: Turn the modified args back into []string.
    out := make([]string, argc)
    for i := 0; i < int(argc); i++ {
        out[i] = C.GoString(a[i])
    }
    if out[0] != "dummy" {
        panic("Expected the first element to be our dummy")
    }
    return out[1:]
}

func workerTableLookup(index workerTableIndex) *worker {
    workerTableLock.Lock()
    defer workerTableLock.Unlock()
    return workerTable[index]
}

//export recvCb
func recvCb(buf unsafe.Pointer, buflen C.int, index workerTableIndex) C.bufs {
    gbuf := C.GoBytes(buf, buflen)
    w := workerTableLookup(index)
    retbuf := w.cb(gbuf)
    if retbuf != nil {
        retbufptr := C.CBytes(retbuf) // Note it's up to the caller to free this.
        return C.bufs{retbufptr, C.size_t(len(retbuf))}
    } else {
        return C.bufs{nil, 0}
    }
}

//export ResolveModule
func ResolveModule(moduleSpecifier *C.char, referrerSpecifier *C.char, resolverToken int) C.int {
    moduleName := C.GoString(moduleSpecifier)
    // TODO: Remove this when I'm not dealing with Node resolution anymore
    referrerName := C.GoString(referrerSpecifier)

    resolverTableLock.Lock()
    resolve := resolverFuncs[resolverToken]
    resolverTableLock.Unlock()

    if resolve == nil {
        return C.int(1)
    }
    ret := resolve(moduleName, referrerName)
    return C.int(ret)
}

// Creates a new worker, which corresponds to a V8 isolate. A single threaded
// standalone execution context.
func NewWorker(cb ReceiveMessageCallback) *Worker {
    workerTableLock.Lock()
    w := &worker{
        cb:         cb,
        tableIndex: workerTableNextAvailable,
    }

    workerTableNextAvailable++
    workerTable[w.tableIndex] = w
    workerTableLock.Unlock()

    initV8Once.Do(func() {
        C.v8_init()
    })

    w.cWorker = C.worker_new(C.int(w.tableIndex))

    externalWorker := &Worker{
        worker:   w,
        disposed: false,
    }

    runtime.SetFinalizer(externalWorker, func(final_worker *Worker) {
        final_worker.Dispose()
    })
    return externalWorker
}

// Forcefully frees up memory associated with worker.
// GC will also free up worker memory so calling this isn't strictly necessary.
func (w *Worker) Dispose() {
    if w.disposed {
        panic("worker already disposed")
    }
    w.disposed = true
    workerTableLock.Lock()
    internalWorker := w.worker
    delete(workerTable, internalWorker.tableIndex)
    workerTableLock.Unlock()
    C.worker_dispose(internalWorker.cWorker)
}

// Load and executes a javascript file with the filename specified by
// scriptName and the contents of the file specified by the param code.
func (w *Worker) Load(scriptName string, code string) error {
    scriptName_s := C.CString(scriptName)
    code_s := C.CString(code)
    defer C.free(unsafe.Pointer(scriptName_s))
    defer C.free(unsafe.Pointer(code_s))

    r := C.worker_load(w.worker.cWorker, scriptName_s, code_s)
    if r != 0 {
        errStr := C.GoString(C.worker_last_exception(w.worker.cWorker))
        return errors.New(errStr)
    }
    return nil
}

// LoadModule loads and executes a javascript module with filename specified by
// scriptName and the contents of the module specified by the param code.
// All `import` dependencies must be loaded before a script otherwise it will error.
func (w *Worker) LoadModule(scriptName string, code string, resolve ModuleResolverCallback) error {
    scriptName_s := C.CString(scriptName)
    code_s := C.CString(code)
    defer C.free(unsafe.Pointer(scriptName_s))
    defer C.free(unsafe.Pointer(code_s))

    // Register the callback before we attempt to load a module
    resolverTableLock.Lock()
    nextResolverToken++
    token := nextResolverToken
    resolverFuncs[token] = resolve
    resolverTableLock.Unlock()
    token_i := C.int(token)

    r := C.worker_load_module(w.worker.cWorker, scriptName_s, code_s, token_i)

    // Unregister the callback after the module is loaded
    resolverTableLock.Lock()
    delete(resolverFuncs, token)
    resolverTableLock.Unlock()

    if r != 0 {
        errStr := C.GoString(C.worker_last_exception(w.worker.cWorker))
        return errors.New(errStr)
    }
    return nil
}

// Same as Send but for []byte. $recv callback will get an ArrayBuffer.
func (w *Worker) SendBytes(msg []byte) error {
    msg_p := C.CBytes(msg)

    // C.CBytes allocates memory on the C heap that is used as the backing
    // storage for the ArrayBuffer given to javascript. v8 will free the buffer
    // as part of the ArrayBuffer garbage collection as we create the AB with
    // ArrayBufferCreationMode::kInternalized in worker_send_bytes.
    r := C.worker_send_bytes(w.worker.cWorker, msg_p, C.size_t(len(msg)))
    if r != 0 {
        errStr := C.GoString(C.worker_last_exception(w.worker.cWorker))
        return errors.New(errStr)
    }

    return nil
}

// Terminates execution of javascript
func (w *Worker) TerminateExecution() {
    C.worker_terminate_execution(w.worker.cWorker)
}
