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

#include "v8bridge.h"
#include "libplatform/libplatform.h"
#include "v8.h"

#include <sstream>
#include <cassert>
#include <map>
#include <libgen.h>
#include <unistd.h>

extern "C" {
#include "_cgo_export.h"
}

using namespace v8;

size_t SplitString(const std::string src, const std::string sp, std::vector<std::string> &strings) {
    std::istringstream f(src);
    std::string s;
    while (getline(f, s, sp.c_str()[0])) {
        strings.push_back(s);
    }
    return strings.size();
}

std::string JoinStrings(const std::vector<std::string> &strings, const std::string sp) {
    auto it = strings.begin();
    std::string r = "";
    while (it != strings.end()) {
        r += *it;
        it++;

        if (it != strings.end())
            r += sp;
    }
    return r;
}

std::string ResolveDirPath(const std::string absPath) {
    char sz[4096];
    memcpy(sz, absPath.c_str(), absPath.length() + 1);
    std::string baseDirPath = dirname(sz);
    return baseDirPath;
}

std::string JoinAbsPath(const std::string &relativeFilePath, const std::string &referenceFileAbsPath) {
    if (relativeFilePath[0] == '/') {
        return relativeFilePath;
    }

    char sz[4096];
    memcpy(sz, referenceFileAbsPath.c_str(), referenceFileAbsPath.length() + 1);
    std::string baseDirPath = dirname(sz);

    std::vector<std::string> basePathStmts;
    SplitString(baseDirPath, "/", basePathStmts);

    std::vector<std::string> filePathStmts;
    SplitString(relativeFilePath, "/", filePathStmts);

    while (!filePathStmts.empty()) {
        if (filePathStmts[0] == "..") {
            filePathStmts.erase(filePathStmts.begin());
            if(!basePathStmts.empty())
                basePathStmts.pop_back();
            continue;
        } else if (filePathStmts[0] == ".") {
            filePathStmts.erase(filePathStmts.begin());
            continue;
        } else {
            break;
        }
    }

    return JoinStrings(basePathStmts, "/") + "/" + JoinStrings(filePathStmts, "/");
}

std::string ReadFile(const char *fileName, size_t &s) {
    s = 0;
    FILE *f = fopen(fileName, "r");
    if (f == nullptr) {
        return "";
    }

    std::string content;
    char tmpBuf[1024];
    size_t l = 0;

    do {
        l = fread(tmpBuf, 1, 1024 - 1, f);
        tmpBuf[l] = '\0';
        content += tmpBuf;
        s += l;
    } while (l > 0);

    fclose(f);

    return content;
}


/*
 * 逻辑虚拟机, 与一个指定的上下文绑定, 该上下文被显示调用结束虚拟机方法释放之前，将会一直存在。
 */

typedef struct _VM {
    Isolate *isolate;
    Persistent<Context> context;
    std::string last_exception;
    std::map<std::string, Eternal<Module>> modules;
    ArrayBuffer::Allocator *allocator;
    std::map<std::string, bool> resolvings;
    std::string lastReferrerPath;
} VM;


typedef struct _VMObject {
    Local<Object> object;
} VMObject;

typedef struct _VMValue {
    Persistent<Value> value;
} VMValue;
/*
 * 默认输出回调，直接输出到stdout, 但它未能支持格式化字符.
 */
int stdOutputCallback(const char *tag, FunctionCallbackInfoPtr argsPtr) {
    auto args = static_cast<const FunctionCallbackInfo<Value> *>(argsPtr);

    time_t tt = time(0);
    char s[32];
    size_t l = strftime(s, sizeof(s), "%H:%M:%S", localtime(&tt));
    s[l] = 0;

    int startIndex = strcmp(tag, "A") != 0 ? 0 : 1;
    bool first = true;

    printf("[J][%s]%s >>> ", tag, s);

    for (int i = startIndex; i < args->Length(); i++) {
        if (first) {
            first = false;
        } else {
            printf(" ");
        }
        String::Utf8Value str(args->GetIsolate(), (*args)[i]);
        const char *cstr = *str;
        printf("%s", cstr);
    }
    printf("\n");
    fflush(stdout);

    return 0;
}

/*
 * 初始化输出回调为默认
 */
OutputCallback outputCallback = stdOutputCallback;

/*
 * console.log 回调
 */
void consoleLog(const FunctionCallbackInfo<Value> &args) {
    outputCallback("V", &args);
}

/*
 * console.assert 回调
 */
void consoleAssert(const FunctionCallbackInfo<Value> &args) {
    outputCallback("A", &args);
}

/*
 * console.info 回调
 */
void consoleInfo(const FunctionCallbackInfo<Value> &args) {
    outputCallback("I", &args);
}

/*
 * console.warn 回调
 */
void consoleWarn(const FunctionCallbackInfo<Value> &args) {
    outputCallback("W", &args);
}

/*
 * 转换V8的Utf8Value到一个C风格字符串指针
 */
const char *V8ToCString(const String::Utf8Value &value) {
    return *value ? *value : "<string conversion failed>";
}

/*
 * 构造V8引擎异常捕获的格式化字符串
 */
std::string V8ExceptionString(VMPtr vmPtr, TryCatch *try_catch) {
    std::string out;
    size_t scratchSize = 20;
    char scratch[scratchSize];

    HandleScope handle_scope(vmPtr->isolate);
    Local<Context> context = Local<Context>::New(vmPtr->isolate, vmPtr->context);
    String::Utf8Value exception(vmPtr->isolate, try_catch->Exception());
    const char *exception_string = V8ToCString(exception);

    Handle<Message> message = try_catch->Message();

    if (message.IsEmpty()) {
        out.append(exception_string);
        out.append("\n");
    } else {
        String::Utf8Value filename(vmPtr->isolate, message->GetScriptOrigin().ResourceName());
        const char *filename_string = V8ToCString(filename);
        int linenum = message->GetLineNumber(context).ToChecked();

        snprintf(scratch, scratchSize, "%i", linenum);
        out.append(filename_string);
        out.append(":");
        out.append(scratch);
        out.append("\n");

        String::Utf8Value sourceline(vmPtr->isolate, message->GetSourceLine(context).ToLocalChecked());
        const char *sourceline_string = V8ToCString(sourceline);

        out.append(sourceline_string);
        out.append("\n");

        int start = message->GetStartColumn(context).FromJust();
        for (int i = 0; i < start; i++) {
            out.append(" ");
        }
        int end = message->GetEndColumn(context).FromJust();
        for (int i = start; i < end; i++) {
            out.append("^");
        }
        out.append("\n");
        String::Utf8Value stack_trace(vmPtr->isolate, try_catch->StackTrace(context).ToLocalChecked());
        if (stack_trace.length() > 0) {
            const char *stack_trace_string = V8ToCString(stack_trace);
            out.append(stack_trace_string);
            out.append("\n");
        } else {
            out.append(exception_string);
            out.append("\n");
        }
    }
    return out;
}

const char * V8Version() {
    return V8::GetVersion();
}

void v8goVersion(const FunctionCallbackInfo<Value> &args) {
    args.GetReturnValue().Set(String::NewFromUtf8(args.GetIsolate(), V8Version()).ToLocalChecked());
}

int V8DispatchEnterEvent(VMPtr vmPtr, const char * sessionId, const char *addr) {
    Locker locker(vmPtr->isolate);
    HandleScope handle_scope(vmPtr->isolate);
    TryCatch try_catch(vmPtr->isolate);
    Local<Context> context = Local<Context>::New(vmPtr->isolate, vmPtr->context);
    Context::Scope context_scope(context);
    auto global = context->Global();

    MaybeLocal<Value> maybeEnterVal = global->Get(context, String::NewFromUtf8(vmPtr->isolate, "enter").ToLocalChecked());
    if (maybeEnterVal.IsEmpty()) {
        std::string out = "'enter' not found\n";
        vmPtr->last_exception = out;
        return 2;
    }
    Local<Value> enterVal = maybeEnterVal.ToLocalChecked();
    if(!enterVal->IsFunction()) {
        std::string out = "'enter' found, but it's not a function\n";
        vmPtr->last_exception = out;
        return 2;
    }

    Local<Function> enter = Local<Function>::Cast(enterVal);
    if (!enter->IsCallable()) {
        std::string out = "'enter' found, but it's not a callable\n";
        vmPtr->last_exception = out;
        return 2;
    }

    Local<Value> args[2];
    args[0] = String::NewFromUtf8(vmPtr->isolate, sessionId).ToLocalChecked();
    args[1] = String::NewFromUtf8(vmPtr->isolate, addr).ToLocalChecked();
    MaybeLocal<Value> result = enter->CallAsFunction(context, Undefined(vmPtr->isolate), 2, args);
    if(result.IsEmpty()) {
        assert(try_catch.HasCaught());
        vmPtr->last_exception = V8ExceptionString(vmPtr, &try_catch);
        return 2;
    }
    return result.ToLocalChecked()->Uint32Value(context).FromMaybe(-1);
}


int V8DispatchLeaveEvent(VMPtr vmPtr, const char * sessionId, const char *addr) {
    Locker locker(vmPtr->isolate);
    HandleScope handle_scope(vmPtr->isolate);
    TryCatch try_catch(vmPtr->isolate);
    Local<Context> context = Local<Context>::New(vmPtr->isolate, vmPtr->context);
    Context::Scope context_scope(context);
    auto global = context->Global();

    MaybeLocal<Value> maybeEnterVal = global->Get(context, String::NewFromUtf8(vmPtr->isolate, "leave").ToLocalChecked());
    if (maybeEnterVal.IsEmpty()) {
        std::string out = "'leave' not found\n";
        vmPtr->last_exception = out;
        return 2;
    }
    Local<Value> enterVal = maybeEnterVal.ToLocalChecked();
    if(!enterVal->IsFunction()) {
        std::string out = "'leave' found, but it's not a function\n";
        vmPtr->last_exception = out;
        return 2;
    }

    Local<Function> enter = Local<Function>::Cast(enterVal);
    if (!enter->IsCallable()) {
        std::string out = "'leave' found, but it's not a callable\n";
        vmPtr->last_exception = out;
        return 2;
    }

    Local<Value> args[2];
    args[0] = String::NewFromUtf8(vmPtr->isolate, sessionId).ToLocalChecked();
    args[1] = String::NewFromUtf8(vmPtr->isolate, addr).ToLocalChecked();
    MaybeLocal<Value> result = enter->CallAsFunction(context, Undefined(vmPtr->isolate), 2, args);
    if(result.IsEmpty()) {
        assert(try_catch.HasCaught());
        vmPtr->last_exception = V8ExceptionString(vmPtr, &try_catch);
        return 2;
    }
    return result.ToLocalChecked()->Uint32Value(context).FromMaybe(-1);
}


int V8DispatchMessageEvent(VMPtr vmPtr, const char * sessionId, VMValuePtr vmValuePtr) {
    Locker locker(vmPtr->isolate);
    HandleScope handle_scope(vmPtr->isolate);
    TryCatch try_catch(vmPtr->isolate);
    Local<Context> context = Local<Context>::New(vmPtr->isolate, vmPtr->context);
    Context::Scope context_scope(context);
    auto global = context->Global();

    MaybeLocal<Value> maybeEnterVal = global->Get(context, String::NewFromUtf8(vmPtr->isolate, "message").ToLocalChecked());
    if (maybeEnterVal.IsEmpty()) {
        std::string out = "'message' not found\n";
        vmPtr->last_exception = out;
        return 2;
    }
    Local<Value> enterVal = maybeEnterVal.ToLocalChecked();
    if(!enterVal->IsFunction()) {
        std::string out = "'message' found, but it's not a function\n";
        vmPtr->last_exception = out;
        return 2;
    }

    Local<Function> enter = Local<Function>::Cast(enterVal);
    if (!enter->IsCallable()) {
        std::string out = "'message' found, but it's not a callable\n";
        vmPtr->last_exception = out;
        return 2;
    }

    Local<Value> args[2];
    args[0] = String::NewFromUtf8(vmPtr->isolate, sessionId).ToLocalChecked();
    args[1] = vmValuePtr->value.Get(vmPtr->isolate);
    MaybeLocal<Value> result = enter->CallAsFunction(context, Undefined(vmPtr->isolate), 2, args);
    if(result.IsEmpty()) {
        assert(try_catch.HasCaught());
        vmPtr->last_exception = V8ExceptionString(vmPtr, &try_catch);
        return 2;
    }
    return result.ToLocalChecked()->Uint32Value(context).FromMaybe(-1);
}

VMValuePtr V8CreateVMObject(VMPtr vmPtr) {
    Locker locker(vmPtr->isolate);
    HandleScope handle_scope(vmPtr->isolate);
    TryCatch try_catch(vmPtr->isolate);
    Local<Context> context = Local<Context>::New(vmPtr->isolate, vmPtr->context);
    Context::Scope context_scope(context);

    Local<Object> o = ObjectTemplate::New(vmPtr->isolate)->NewInstance(context).ToLocalChecked();
    VMValuePtr vmValuePtr = new VMValue;
    vmValuePtr->value.Reset(vmPtr->isolate, o);
    return vmValuePtr;
}

VMValuePtr V8CreateVMArray(VMPtr vmPtr, int length) {
    Locker locker(vmPtr->isolate);
    HandleScope handle_scope(vmPtr->isolate);
    TryCatch try_catch(vmPtr->isolate);
    Local<Context> context = Local<Context>::New(vmPtr->isolate, vmPtr->context);
    Context::Scope context_scope(context);

    Local<Array> o = Array::New(vmPtr->isolate, length);
    VMValuePtr vmValuePtr = new VMValue;
    vmValuePtr->value.Reset(vmPtr->isolate, o);
    return vmValuePtr;
}

void V8DisposeVMValue(VMValuePtr vmValuePtr) {
    vmValuePtr->value.Reset();
    delete vmValuePtr;
}

void V8ObjectSetString(VMPtr vmPtr, VMValuePtr o, const char *name, const char *val) {
    Locker locker(vmPtr->isolate);
    HandleScope handle_scope(vmPtr->isolate);
    TryCatch try_catch(vmPtr->isolate);
    Local<Context> context = Local<Context>::New(vmPtr->isolate, vmPtr->context);
    Context::Scope context_scope(context);

    Local<Object> oo = Local<Object>::Cast(o->value.Get(vmPtr->isolate));
    auto success = oo->Set(context, String::NewFromUtf8(vmPtr->isolate, name).ToLocalChecked(), String::NewFromUtf8(vmPtr->isolate, val).ToLocalChecked());
}

void V8ObjectSetStringForIndex(VMPtr vmPtr, VMValuePtr o, int index, const char *val) {
    Locker locker(vmPtr->isolate);
    HandleScope handle_scope(vmPtr->isolate);
    TryCatch try_catch(vmPtr->isolate);
    Local<Context> context = Local<Context>::New(vmPtr->isolate, vmPtr->context);
    Context::Scope context_scope(context);

    Local<Object> oo = Local<Object>::Cast(o->value.Get(vmPtr->isolate));
    auto success = oo->Set(context, (uint32_t)index, String::NewFromUtf8(vmPtr->isolate, val).ToLocalChecked());
}

void V8ObjectSetInteger(VMPtr vmPtr, VMValuePtr o, const char *name, int64_t val) {
    Locker locker(vmPtr->isolate);
    HandleScope handle_scope(vmPtr->isolate);
    TryCatch try_catch(vmPtr->isolate);
    Local<Context> context = Local<Context>::New(vmPtr->isolate, vmPtr->context);
    Context::Scope context_scope(context);

    Local<Object> oo = Local<Object>::Cast(o->value.Get(vmPtr->isolate));
    auto success = oo->Set(context, String::NewFromUtf8(vmPtr->isolate, name).ToLocalChecked(), Integer::New(vmPtr->isolate, val));
}

void V8ObjectSetIntegerForIndex(VMPtr vmPtr, VMValuePtr o, int index, int64_t val) {
    Locker locker(vmPtr->isolate);
    HandleScope handle_scope(vmPtr->isolate);
    TryCatch try_catch(vmPtr->isolate);
    Local<Context> context = Local<Context>::New(vmPtr->isolate, vmPtr->context);
    Context::Scope context_scope(context);

    Local<Object> oo = Local<Object>::Cast(o->value.Get(vmPtr->isolate));
    auto success = oo->Set(context, (uint32_t)index, Integer::New(vmPtr->isolate, val));
}

void V8ObjectSetValue(VMPtr vmPtr, VMValuePtr o, const char *name, VMValuePtr val) {
    Locker locker(vmPtr->isolate);
    HandleScope handle_scope(vmPtr->isolate);
    TryCatch try_catch(vmPtr->isolate);
    Local<Context> context = Local<Context>::New(vmPtr->isolate, vmPtr->context);
    Context::Scope context_scope(context);

    Local<Object> oo = Local<Object>::Cast(o->value.Get(vmPtr->isolate));
    Local<Value> vv = val->value.Get(vmPtr->isolate);
    auto success = oo->Set(context, String::NewFromUtf8(vmPtr->isolate, name).ToLocalChecked(), vv);
}

void V8ObjectSetValueForIndex(VMPtr vmPtr, VMValuePtr o, int index, VMValuePtr val) {
    Locker locker(vmPtr->isolate);
    HandleScope handle_scope(vmPtr->isolate);
    TryCatch try_catch(vmPtr->isolate);
    Local<Context> context = Local<Context>::New(vmPtr->isolate, vmPtr->context);
    Context::Scope context_scope(context);

    Local<Object> oo = Local<Object>::Cast(o->value.Get(vmPtr->isolate));
    Local<Value> vv = val->value.Get(vmPtr->isolate);
    auto success = oo->Set(context, (uint32_t)index, vv);
}

void V8ObjectSetFloat(VMPtr vmPtr, VMValuePtr o, const char *name, double val) {
    Locker locker(vmPtr->isolate);
    HandleScope handle_scope(vmPtr->isolate);
    TryCatch try_catch(vmPtr->isolate);
    Local<Context> context = Local<Context>::New(vmPtr->isolate, vmPtr->context);
    Context::Scope context_scope(context);

    Local<Object> oo = Local<Object>::Cast(o->value.Get(vmPtr->isolate));
    auto success = oo->Set(context, String::NewFromUtf8(vmPtr->isolate, name).ToLocalChecked(), Number::New(vmPtr->isolate, val));
}

void V8ObjectSetFloatForIndex(VMPtr vmPtr, VMValuePtr o, int index, double val) {
    Locker locker(vmPtr->isolate);
    HandleScope handle_scope(vmPtr->isolate);
    TryCatch try_catch(vmPtr->isolate);
    Local<Context> context = Local<Context>::New(vmPtr->isolate, vmPtr->context);
    Context::Scope context_scope(context);

    Local<Object> oo = Local<Object>::Cast(o->value.Get(vmPtr->isolate));
    auto success = oo->Set(context, (uint32_t)index, Number::New(vmPtr->isolate, val));
}

void V8ObjectSetBoolean(VMPtr vmPtr, VMValuePtr o, const char *name, bool val) {
    Locker locker(vmPtr->isolate);
    HandleScope handle_scope(vmPtr->isolate);
    TryCatch try_catch(vmPtr->isolate);
    Local<Context> context = Local<Context>::New(vmPtr->isolate, vmPtr->context);
    Context::Scope context_scope(context);

    Local<Object> oo = Local<Object>::Cast(o->value.Get(vmPtr->isolate));
    auto success = oo->Set(context, String::NewFromUtf8(vmPtr->isolate, name).ToLocalChecked(), val ? True(vmPtr->isolate) : False(vmPtr->isolate));
}

void V8ObjectSetBooleanForIndex(VMPtr vmPtr, VMValuePtr o, int index, bool val) {
    Locker locker(vmPtr->isolate);
    HandleScope handle_scope(vmPtr->isolate);
    TryCatch try_catch(vmPtr->isolate);
    Local<Context> context = Local<Context>::New(vmPtr->isolate, vmPtr->context);
    Context::Scope context_scope(context);

    Local<Object> oo = Local<Object>::Cast(o->value.Get(vmPtr->isolate));
    auto success = oo->Set(context, (uint32_t)index, val ? True(vmPtr->isolate) : False(vmPtr->isolate));
}
/*
 * 初始化V8运行环境, 请注意，此处是初始化V8环境，并没有创建任何虚拟机上下文.
 */
std::unique_ptr<Platform> _priv_platform = platform::NewDefaultPlatform();
std::string globalCWD;

void V8Init() {
    globalCWD = getcwd(nullptr, 0);
    V8::InitializeICU();
    V8::InitializePlatform(_priv_platform.get());
    V8::SetFlagsFromString("--es_staging --harmony");
    V8::Initialize();
}

/*
 * 销毁V8运行环境.
 */
void V8Dispose() {
    V8::Dispose();
    V8::ShutdownPlatform();
}

/*
 * 获取当前工作目录.
 */
const char *V8WorkDir() {
    return globalCWD.c_str();
}

/*
 * 获取最后一条异常信息.
 */
const char *V8LastException(VMPtr vmPtr) {
    if (vmPtr->last_exception.length() == 0)
        return "";

    std::string s = "Uncaught exception: \n" + vmPtr->last_exception;
    return s.c_str();
}

/*
 * 设置输出回调.
 */
void V8SetOutputCallback(OutputCallback cb) {
    assert(cb == nullptr && "V8SetOutputCallback's arg[OutputCallback] is NULL");
    outputCallback = cb;
}

/*
 * 创建一个新的V8虚拟机上下文, 调用前必须确保已经初始化了V8运行环境.
 */
VMPtr V8NewVM() {
    VM *vmPtr = new VM;
    Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = ArrayBuffer::Allocator::NewDefaultAllocator();
    Isolate *isolate = Isolate::New(create_params);
    HandleScope scope(isolate);
    Local<Context> context = Context::New(isolate);
    Context::Scope context_scope(context);

    auto global = context->Global();

    Local<Value> consoleV = global->Get(context, String::NewFromUtf8(isolate, "console").ToLocalChecked()).ToLocalChecked();
    Local<Object> console = Local<Object>::Cast(consoleV);

    bool success = console->Set(context, String::NewFromUtf8(isolate, "log").ToLocalChecked(),
                   FunctionTemplate::New(isolate, consoleLog)->GetFunction(context).ToLocalChecked()).FromMaybe(false);

    success = console->Set(context, String::NewFromUtf8(isolate, "info").ToLocalChecked(),
                   FunctionTemplate::New(isolate, consoleInfo)->GetFunction(context).ToLocalChecked()).FromMaybe(false);

    success = console->Set(context, String::NewFromUtf8(isolate, "assert").ToLocalChecked(),
                   FunctionTemplate::New(isolate, consoleAssert)->GetFunction(context).ToLocalChecked()).FromMaybe(false);

    success = console->Set(context, String::NewFromUtf8(isolate, "warn").ToLocalChecked(),
                           FunctionTemplate::New(isolate, consoleWarn)->GetFunction(context).ToLocalChecked()).FromMaybe(false);

    Local<ObjectTemplate> v8goTmpl = ObjectTemplate::New(isolate);
    v8goTmpl->Set(isolate, "version", FunctionTemplate::New(isolate, v8goVersion));
    Local<Object> v8go = v8goTmpl->NewInstance(context).ToLocalChecked();

    success = global->Set(context, String::NewFromUtf8(isolate, "v8go").ToLocalChecked(), v8go).FromMaybe(false);

    vmPtr->isolate = isolate;
    vmPtr->context.Reset(isolate, context);
    vmPtr->allocator = create_params.array_buffer_allocator;
    vmPtr->lastReferrerPath = V8WorkDir();

    isolate->SetData(0, vmPtr);

    return vmPtr;
}

/*
 * 销毁一个V8虚拟机上下文.
 */
void V8DisposeVM(VMPtr vmPtr) {
    vmPtr->context.Reset();
    vmPtr->isolate->Dispose();
    delete vmPtr->allocator;
    delete vmPtr;
}


int ResolveModule(VMPtr vmPtr, const char *specifier, const char *referrer) {
    vmPtr->lastReferrerPath = referrer;
    std::string specifierPath = JoinAbsPath(specifier, referrer);
    if (vmPtr->resolvings.count(specifierPath) != 0) {
        return 3;
    }
    return V8LoadModule(vmPtr, specifierPath.c_str(), nullptr, referrer);
}

MaybeLocal<Module> V8ResolveCallback(Local<Context> context, Local<String> specifier, Local<Module> referrer) {
    auto isolate = Isolate::GetCurrent();
    auto vmPtr = static_cast<VMPtr>(isolate->GetData(0));

    HandleScope handle_scope(isolate);

    String::Utf8Value str(isolate, specifier);
    const char *moduleName = *str;

    std::string specifierPath = JoinAbsPath(moduleName, vmPtr->lastReferrerPath);

    if (vmPtr->modules.count(specifierPath.c_str()) == 0) {
        std::string out;
        out.append("Module (");
        out.append(moduleName);
        out.append(") has not been loaded");
        out.append("\n");
        vmPtr->last_exception = out;
        MaybeLocal<Module> r;
        return r;
    }

    return vmPtr->modules[specifierPath.c_str()].Get(isolate);
}

/*
 * 加载一个脚本文件. 指定文件名和代码.
 */
int V8Load(VMPtr vmPtr, const char *fileName, const char *inSourceCode) {

    std::string sourceStr = "";
    const char * sourceCode = inSourceCode;
    if(sourceCode == nullptr) {
        size_t sourceLen = 0;
        sourceStr = ReadFile(fileName, sourceLen);
        if (sourceLen == 0) {
            std::string out;
            out.append("Failure to exec script (");
            out.append(fileName);
            out.append("), maybe the file is not exists?");
            out.append("\n");
            vmPtr->last_exception = out;
            return -1;
        }

        sourceCode = sourceStr.c_str();
    }

    Locker locker(vmPtr->isolate);

    Isolate::Scope isolate_scope(vmPtr->isolate);
    HandleScope handle_scope(vmPtr->isolate);

    Local<Context> context = Local<Context>::New(vmPtr->isolate, vmPtr->context);
    Context::Scope context_scope(context);

    TryCatch try_catch(vmPtr->isolate);

    Local<String> name = String::NewFromUtf8(vmPtr->isolate, fileName).ToLocalChecked();
    Local<String> source_text = String::NewFromUtf8(vmPtr->isolate, sourceCode).ToLocalChecked();

    Local<Integer> line_offset = Integer::New(vmPtr->isolate, 0);
    Local<Integer> column_offset = Integer::New(vmPtr->isolate, 0);
    Local<Boolean> is_cross_origin = True(vmPtr->isolate);
    Local<Integer> script_id = Local<Integer>();
    Local<Value> source_map_url = Local<Value>();
    Local<Boolean> is_opaque = False(vmPtr->isolate);
    Local<Boolean> is_wasm = False(vmPtr->isolate);
    Local<Boolean> is_module = False(vmPtr->isolate);

    ScriptOrigin origin(name, line_offset, column_offset, is_cross_origin,
                        script_id, source_map_url, is_opaque, is_wasm, is_module);

    MaybeLocal<Script> mScript = Script::Compile(context, source_text, &origin);
    if (mScript.IsEmpty()) {
        assert(try_catch.HasCaught());
        vmPtr->last_exception = V8ExceptionString(vmPtr, &try_catch);
        return 1;
    }

    Local<Script> script = mScript.ToLocalChecked();

    MaybeLocal<Value> result = script->Run(context);
    if (result.IsEmpty()) {
        assert(try_catch.HasCaught());
        vmPtr->last_exception = V8ExceptionString(vmPtr, &try_catch);
        return 2;
    }

    auto global = context->Global();

    MaybeLocal<Value> maybeMainVal = global->Get(context, String::NewFromUtf8(vmPtr->isolate, "main").ToLocalChecked());
    if (maybeMainVal.IsEmpty()) {
        std::string out = "'main' not found\n";
        vmPtr->last_exception = out;
        return 2;
    }
    Local<Value> mainVal = maybeMainVal.ToLocalChecked();
    if(!mainVal->IsFunction()) {
        std::string out = "'main' found, but it's not a function\n";
        vmPtr->last_exception = out;
        return 2;
    }

    Local<Function> main = Local<Function>::Cast(mainVal);
    if (!main->IsCallable()) {
        std::string out = "'main' found, but it's not a callable\n";
        vmPtr->last_exception = out;
        return 2;
    }
    auto s = main->CallAsFunction(context, Undefined(vmPtr->isolate), 0, nullptr);

    return 0;
}

/*
 * 加载一个模块. 指定文件名和代码.
 */
int V8LoadModule(VMPtr vmPtr, const char *fileName, const char *inSourceCode, const char *referrer) {

    std::string stlFileName = fileName;

    std::string stlReferrerFileName;

    if (referrer == nullptr) {
        vmPtr->resolvings.clear();
        stlFileName = JoinAbsPath(stlFileName, globalCWD);
        stlReferrerFileName = JoinAbsPath("./__main__", globalCWD);;
    } else {
        stlReferrerFileName = referrer;
    }
    vmPtr->resolvings[stlFileName] = true;

    std::string sourceStr = "";
    const char * sourceCode = inSourceCode;
    if(sourceCode == nullptr) {
        size_t sourceLen = 0;
        sourceStr = ReadFile(stlFileName.c_str(), sourceLen);
        if (sourceLen == 0) {
            std::string out;
            out.append("Module (");
            out.append(stlFileName);
            out.append(") not found, maybe the file is not exists?");
            out.append("\n");
            vmPtr->last_exception = out;
            return -1;
        }

        sourceCode = sourceStr.c_str();
    }

    //printf("\n============= Code =============\n");
    //printf(sourceCode);
    //printf("\n============= Code =============\n");

    Locker locker(vmPtr->isolate);

    Isolate::Scope isolate_scope(vmPtr->isolate);
    HandleScope handle_scope(vmPtr->isolate);

    Local<Context> context = Local<Context>::New(vmPtr->isolate, vmPtr->context);
    Context::Scope context_scope(context);

    TryCatch try_catch(vmPtr->isolate);

    Local<String> name = String::NewFromUtf8(vmPtr->isolate, stlFileName.c_str()).ToLocalChecked();
    Local<String> source_text = String::NewFromUtf8(vmPtr->isolate, sourceCode).ToLocalChecked();

    Local<Integer> line_offset = Integer::New(vmPtr->isolate, 0);
    Local<Integer> column_offset = Integer::New(vmPtr->isolate, 0);
    Local<Boolean> is_cross_origin = True(vmPtr->isolate);
    Local<Integer> script_id = Local<Integer>();
    Local<Value> source_map_url = Local<Value>();
    Local<Boolean> is_opaque = False(vmPtr->isolate);
    Local<Boolean> is_wasm = False(vmPtr->isolate);
    Local<Boolean> is_module = True(vmPtr->isolate);

    ScriptOrigin origin(name, line_offset, column_offset, is_cross_origin,
                        script_id, source_map_url, is_opaque, is_wasm, is_module);

    ScriptCompiler::Source source(source_text, origin);
    Local<Module> module;

    if (!ScriptCompiler::CompileModule(vmPtr->isolate, &source).ToLocal(&module)) {
        assert(try_catch.HasCaught());
        vmPtr->last_exception = V8ExceptionString(vmPtr, &try_catch);
        return 1;
    }

    for (int i = 0; i < module->GetModuleRequestsLength(); i++) {
        Local<String> dependency = module->GetModuleRequest(i);
        String::Utf8Value str(vmPtr->isolate, dependency);
        char *dependencySpecifier = *str;

        std::string dependencySpecifierPath = JoinAbsPath(dependencySpecifier, stlFileName);

        // If we've already loaded the module, skip resolving it.
        // TODO: Is there ever a time when the specifier would be the same
        // but would need to be resolved again?
        if (vmPtr->modules.count(dependencySpecifierPath) != 0) {
            continue;
        }

        int ret = ResolveModule(vmPtr, dependencySpecifierPath.c_str(), stlFileName.c_str());
        if (ret != 0) {
            // TODO: Use module->GetModuleRequestLocation() to get source locations
            std::string out;
            if (ret == -1) {
                out.append("Module (");
                out.append(dependencySpecifier);
                out.append(") not found, maybe the file is not exists?");
            } else if (ret == 3) {
                out.append("Cross-reference Found. import Module (");
                out.append(dependencySpecifier);
                out.append(") failure.");
            } else {
                out.append("Module (");
                out.append(dependencySpecifier);
                out.append(") has not been loaded");
            }
            out.append("\n");
            vmPtr->last_exception = out;
            return ret;
        }
    }

    Eternal<Module> persModule(vmPtr->isolate, module);
    vmPtr->modules[stlFileName] = persModule;

    vmPtr->lastReferrerPath = stlFileName;
    Maybe<bool> ok = module->InstantiateModule(context, V8ResolveCallback);

    if (!ok.FromMaybe(false)) {
        // TODO: I'm not sure if this is needed
        if (try_catch.HasCaught()) {
            assert(try_catch.HasCaught());
            vmPtr->last_exception = V8ExceptionString(vmPtr, &try_catch);
        }
        return 2;
    }

    MaybeLocal<Value> result = module->Evaluate(context);

    if (result.IsEmpty()) {
        assert(try_catch.HasCaught());
        vmPtr->last_exception = V8ExceptionString(vmPtr, &try_catch);
        return 2;
    }

    return 0;
}
