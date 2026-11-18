/*
 * Guest-ABI replacement for the OEM handwriting-recognition JNI library.
 * It provides the exported methods without loading the OEM armeabi-v7a
 * library. Chinese handwriting recognition is not implemented; both natives
 * return no result.
 */

#include <jni.h>

JNIEXPORT jcharArray JNICALL
Java_com_qinggan_app_qgime_HandWriteDecoder_getAssociate(JNIEnv *env, jobject thiz, jcharArray array, jint value) {
    return NULL;
}

JNIEXPORT jcharArray JNICALL
Java_com_qinggan_app_qgime_HandWriteDecoder_getResult(JNIEnv *env, jobject thiz, jshortArray array, jint value, jint value2, jobject assetManager, jstring text) {
    return NULL;
}
