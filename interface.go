package v8go

import "sync"

type VM interface {
    Dispose()
    Called() int64
    Reset()
    PrintMemStat()
    Load(path string) bool
    SetValue(name string, val interface{})
    SetAssociatedSourceAddr(addr string)
    SetAssociatedSourceId(id uint64)
    GetAssociatedSourceAddr() string
    GetAssociatedSourceId() uint64
    DispatchEnter(sessionId uint64, addr string) int
    DispatchLeave(sessionId uint64, addr string) int
    DispatchMessage(sessionId uint64, msg map[interface{}] interface{}) int
}

var initV8Once sync.Once

var OnSendMessage func(string, uint64, interface{}) int = nil
var OnSendMessageTo func(interface{}) int = nil
var OnOutput func(string) = nil