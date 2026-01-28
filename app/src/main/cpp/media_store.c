#include "media_store.h"

#include <stdio.h>
#include <string.h>

static void set_err(char *err, size_t errLen, const char *msg) {
    if (err && errLen > 0) {
        snprintf(err, errLen, "%s", msg);
    }
}

static bool get_env(JavaVM *vm, JNIEnv **outEnv, bool *outAttached) {
    *outAttached = false;
    if ((*vm)->GetEnv(vm, (void **)outEnv, JNI_VERSION_1_6) != JNI_OK) {
        if ((*vm)->AttachCurrentThread(vm, outEnv, NULL) != JNI_OK) {
            return false;
        }
        *outAttached = true;
    }
    return true;
}

static void detach_if_needed(JavaVM *vm, bool attached) {
    if (attached) {
        (*vm)->DetachCurrentThread(vm);
    }
}

static jobject new_string(JNIEnv *env, const char *value) {
    return (*env)->NewStringUTF(env, value);
}

bool media_store_create_audio(JavaVM *vm, jobject activity,
                              const char *displayName, const char *mimeType,
                              MediaStoreHandle *outHandle,
                              char *err, size_t errLen) {
    if (!vm || !activity || !outHandle) {
        set_err(err, errLen, "MediaStore invalid params");
        return false;
    }
    outHandle->fd = -1;
    outHandle->uri = NULL;
    outHandle->pfd = NULL;

    JNIEnv *env = NULL;
    bool attached = false;
    if (!get_env(vm, &env, &attached)) {
        set_err(err, errLen, "JNI attach failed");
        return false;
    }

    jclass activityCls = (*env)->GetObjectClass(env, activity);
    jmethodID getContentResolver = (*env)->GetMethodID(env, activityCls,
                                                       "getContentResolver",
                                                       "()Landroid/content/ContentResolver;");
    jobject resolver = (*env)->CallObjectMethod(env, activity, getContentResolver);

    jclass valuesCls = (*env)->FindClass(env, "android/content/ContentValues");
    jmethodID valuesCtor = (*env)->GetMethodID(env, valuesCls, "<init>", "()V");
    jobject values = (*env)->NewObject(env, valuesCls, valuesCtor);
    jmethodID putString = (*env)->GetMethodID(env, valuesCls, "put",
                                              "(Ljava/lang/String;Ljava/lang/String;)V");
    jmethodID putInt = (*env)->GetMethodID(env, valuesCls, "put",
                                           "(Ljava/lang/String;Ljava/lang/Integer;)V");
    jclass integerCls = (*env)->FindClass(env, "java/lang/Integer");
    jmethodID integerCtor = (*env)->GetMethodID(env, integerCls, "<init>", "(I)V");

    jobject keyDisplay = new_string(env, "_display_name");
    jobject keyMime = new_string(env, "mime_type");
    jobject keyRelPath = new_string(env, "relative_path");
    jobject keyPending = new_string(env, "is_pending");
    jobject valueDisplay = new_string(env, displayName);
    jobject valueMime = new_string(env, mimeType);
    jobject valueRelPath = new_string(env, "Music/BGMDWLDR");
    jobject valuePending = (*env)->NewObject(env, integerCls, integerCtor, 1);

    (*env)->CallVoidMethod(env, values, putString, keyDisplay, valueDisplay);
    (*env)->CallVoidMethod(env, values, putString, keyMime, valueMime);
    (*env)->CallVoidMethod(env, values, putString, keyRelPath, valueRelPath);
    (*env)->CallVoidMethod(env, values, putInt, keyPending, valuePending);

    jclass mediaCls = (*env)->FindClass(env, "android/provider/MediaStore$Audio$Media");
    jfieldID externalField = (*env)->GetStaticFieldID(env, mediaCls,
                                                      "EXTERNAL_CONTENT_URI",
                                                      "Landroid/net/Uri;");
    jobject externalUri = (*env)->GetStaticObjectField(env, mediaCls, externalField);

    jclass resolverCls = (*env)->GetObjectClass(env, resolver);
    jmethodID insertMethod = (*env)->GetMethodID(env, resolverCls, "insert",
                                                 "(Landroid/net/Uri;Landroid/content/ContentValues;)Landroid/net/Uri;");
    jobject uri = (*env)->CallObjectMethod(env, resolver, insertMethod,
                                           externalUri, values);
    if (!uri) {
        detach_if_needed(vm, attached);
        set_err(err, errLen, "MediaStore insert failed");
        return false;
    }

    jmethodID openPfd = (*env)->GetMethodID(env, resolverCls, "openFileDescriptor",
                                            "(Landroid/net/Uri;Ljava/lang/String;)Landroid/os/ParcelFileDescriptor;");
    jobject mode = new_string(env, "w");
    jobject pfd = (*env)->CallObjectMethod(env, resolver, openPfd, uri, mode);
    if (!pfd) {
        detach_if_needed(vm, attached);
        set_err(err, errLen, "OpenFileDescriptor failed");
        return false;
    }
    jclass pfdCls = (*env)->GetObjectClass(env, pfd);
    jmethodID getFd = (*env)->GetMethodID(env, pfdCls, "getFd", "()I");
    int fd = (*env)->CallIntMethod(env, pfd, getFd);

    outHandle->fd = fd;
    outHandle->uri = (*env)->NewGlobalRef(env, uri);
    outHandle->pfd = (*env)->NewGlobalRef(env, pfd);
    detach_if_needed(vm, attached);
    return true;
}

bool media_store_finalize(JavaVM *vm, jobject activity,
                          MediaStoreHandle *handle,
                          char *err, size_t errLen) {
    if (!vm || !activity || !handle || !handle->uri) {
        set_err(err, errLen, "MediaStore finalize invalid params");
        return false;
    }
    JNIEnv *env = NULL;
    bool attached = false;
    if (!get_env(vm, &env, &attached)) {
        set_err(err, errLen, "JNI attach failed");
        return false;
    }
    jclass activityCls = (*env)->GetObjectClass(env, activity);
    jmethodID getContentResolver = (*env)->GetMethodID(env, activityCls,
                                                       "getContentResolver",
                                                       "()Landroid/content/ContentResolver;");
    jobject resolver = (*env)->CallObjectMethod(env, activity, getContentResolver);

    jclass valuesCls = (*env)->FindClass(env, "android/content/ContentValues");
    jmethodID valuesCtor = (*env)->GetMethodID(env, valuesCls, "<init>", "()V");
    jobject values = (*env)->NewObject(env, valuesCls, valuesCtor);
    jmethodID putInt = (*env)->GetMethodID(env, valuesCls, "put",
                                           "(Ljava/lang/String;Ljava/lang/Integer;)V");
    jclass integerCls = (*env)->FindClass(env, "java/lang/Integer");
    jmethodID integerCtor = (*env)->GetMethodID(env, integerCls, "<init>", "(I)V");
    jobject keyPending = new_string(env, "is_pending");
    jobject valuePending = (*env)->NewObject(env, integerCls, integerCtor, 0);
    (*env)->CallVoidMethod(env, values, putInt, keyPending, valuePending);

    jclass resolverCls = (*env)->GetObjectClass(env, resolver);
    jmethodID updateMethod = (*env)->GetMethodID(env, resolverCls, "update",
                                                 "(Landroid/net/Uri;Landroid/content/ContentValues;Ljava/lang/String;[Ljava/lang/String;)I");
    (*env)->CallIntMethod(env, resolver, updateMethod,
                          handle->uri, values, NULL, NULL);

    detach_if_needed(vm, attached);
    return true;
}

void media_store_close(JavaVM *vm, MediaStoreHandle *handle) {
    if (!vm || !handle) {
        return;
    }
    JNIEnv *env = NULL;
    bool attached = false;
    if (!get_env(vm, &env, &attached)) {
        return;
    }
    if (handle->pfd) {
        jclass pfdCls = (*env)->GetObjectClass(env, handle->pfd);
        jmethodID closeMethod = (*env)->GetMethodID(env, pfdCls, "close", "()V");
        (*env)->CallVoidMethod(env, handle->pfd, closeMethod);
        (*env)->DeleteGlobalRef(env, handle->pfd);
        handle->pfd = NULL;
    }
    if (handle->uri) {
        (*env)->DeleteGlobalRef(env, handle->uri);
        handle->uri = NULL;
    }
    detach_if_needed(vm, attached);
    handle->fd = -1;
}
