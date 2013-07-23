#include <errno.h>
#include <string.h>
#include <js/jsapi.h>
#include <pthread.h>
#include <unistd.h>

#define OK 0

typedef struct spidermonkey_ctxt {
    JSRuntime *rt;
    JSContext *cx;
    JSObject *global;
} sm_cx_t;

typedef struct thread_ctxt {
    sm_cx_t *jscx;
    char *script;
    int len;
} thread_cx_t;

static JSClass global_class = {
    "global", 
    JSCLASS_GLOBAL_FLAGS, 
    JS_PropertyStub, 
    JS_PropertyStub, 
    JS_PropertyStub, 
    JS_StrictPropertyStub, 
    JS_EnumerateStub, 
    JS_ResolveStub, 
    JS_ConvertStub, 
    NULL, 
    JSCLASS_NO_OPTIONAL_MEMBERS
};

void report_error(JSContext *cxt, const char *message, JSErrorReport *report) {
    pid_t pid = getpid();
    pthread_t self = pthread_self();

    fprintf(stderr, "%u(%u) %s:%u:%s\n",
            (unsigned int)pid, (unsigned int)self,
            report->filename ? report->filename : "<null>",
            (unsigned int) report->lineno, message);
}

JSBool op_callback(JSContext *cx) {
    pid_t pid = getpid();
    pthread_t self = pthread_self();

    fprintf(stdout, "%u(%u) op_callback",
            (unsigned int)pid, (unsigned int)self);

    JS_SetOperationCallback(cx, NULL);

    JS_MaybeGC(cx);

    JS_SetOperationCallback(cx, op_callback);
    return JS_TRUE;
}

int init_jscx(sm_cx_t *jscx) {
    jscx->rt = JS_NewRuntime(8L * 1024L * 1024L);
    if (jscx->rt == NULL) {
        return 1;
    }

    jscx->cx = JS_NewContext(jscx->rt, 8192);
    if (jscx->cx == NULL) {
        return 2;
    }

    JS_SetOptions(jscx->cx, JSOPTION_VAROBJFIX | JSOPTION_METHODJIT);
    JS_SetVersion(jscx->cx, JSVERSION_LATEST);
    JS_SetErrorReporter(jscx->cx, report_error);
    JS_SetOperationCallback(jscx->cx, op_callback);

    jscx->global = JS_NewCompartmentAndGlobalObject(jscx->cx, &global_class, NULL);
    if (jscx->global == NULL) {
        return 3;
    }

    if (!JS_InitStandardClasses(jscx->cx, jscx->global)) {
        return 4;
    }

    return 0;
}

void clean_up_jscx(sm_cx_t *jscx) {
    if (jscx->cx != NULL) JS_DestroyContext(jscx->cx);
    if (jscx->rt != NULL) JS_DestroyRuntime(jscx->rt);
    JS_ShutDown();
}

void clean_up_tcx(thread_cx_t *tcx) {
    clean_up_jscx(tcx->jscx);
    free(tcx->jscx);
    free(tcx->script);
}

void print_object(JSContext *cx, jsval retval) {
    JSObject *val;
    if (JS_ValueToObject(cx, retval, &val) == JS_TRUE) {
        jsval objstr;
        if (JS_CallFunctionName(cx, val, "toString", 0, NULL, &objstr) == JS_TRUE) {
            JSString *jstr = JSVAL_TO_STRING(objstr);
            char *str = JS_EncodeString(cx, jstr);

            fprintf(stdout, "=> \"%s\"\n", str);

            JS_free(cx, str);
            return;
        }
    }

    fprintf(stdout, "no idea what it is\n");
}

void *run_script(void *arg) {
    thread_cx_t *tcx = (thread_cx_t *)arg;
    pid_t pid = getpid();
    pthread_t self = pthread_self();

    fprintf(stdout, "%u(%u) <= \"%s\"\n", 
            (unsigned int)pid, (unsigned int)self, tcx->script);

    JS_BeginRequest(tcx->jscx->cx);
    jsval retval;
    if (JS_EvaluateScript(tcx->jscx->cx, tcx->jscx->global, 
                          tcx->script, tcx->len, NULL, 0, &retval) != JS_TRUE) {
        JS_EndRequest(tcx->jscx->cx);
        return (void *)1;
    }
    JS_EndRequest(tcx->jscx->cx);

    if (JSVAL_IS_NULL(retval)) {
        fprintf(stdout, "=> null\n");
    }
    else if (JSVAL_IS_VOID(retval)) {
        fprintf(stdout, "=> undefined\n");
    }
    else{
        print_object(tcx->jscx->cx, retval);
    }

    return 0;
}

int main(int argc, const char *arg[]) {
    int err = 0;

    pthread_t tid[argc];
    thread_cx_t tcx[argc];

    for (int i = 0; i < argc - 1; i++) {
        int len = strlen(arg[i + 1]);
        char *script = calloc(len + 1, sizeof(char));
        if (script == NULL) {
            return 5;
        }

        fprintf(stdin, "script: %s", arg[i + 1]);

        strncpy(script, arg[i + 1], len);
        tcx[i].jscx  = malloc(sizeof(sm_cx_t));
        tcx[i].script = script;
        tcx[i].len =  len;

        err = init_jscx(tcx[i].jscx);
        if (err != OK) {
            fprintf(stderr, "init_jscx: %d\n", err);
        }

        err = pthread_create(&(tid[i]), NULL, run_script, &(tcx[i]));
        if (err != OK) {
            fprintf(stderr, "pthread_create(%d): %d : %s\n", i, err, strerror(err));
        }
        else {
            fprintf(stdin, "pthread_create(%d): success", i);
        }
    }

    for(int i = 0; i < argc - 1; i++) {
        err = pthread_join(tid[i], (void *)NULL);
        if (err != OK) {
            fprintf(stderr, "pthread_join(%d): %d : %s\n", i, err, strerror(err));
        }
        else {
            fprintf(stdin, "pthread_join(%d): success", i);
        }

        clean_up_tcx(&(tcx[i]));
    }

    return 0;
}

