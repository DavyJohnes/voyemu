/*
 * Guest-ABI replacement for the OEM pinyin-decoder JNI library.
 * It provides the exported methods without loading the OEM armeabi-v7a
 * library. Pinyin decoding is not implemented: input-method natives return
 * zero, no candidates or decoder closed; sync natives return empty transfers.
 */

#include <jni.h>

/* IM (input-method) state — decoder always reports closed/empty. */

JNIEXPORT jint JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeImAddLetter(JNIEnv *env, jclass cls, jbyte value) {
    return 0;
}

JNIEXPORT jboolean JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeImCancelInput(JNIEnv *env, jclass cls) {
    return JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeImCancelLastChoice(JNIEnv *env, jclass cls) {
    return 0;
}

JNIEXPORT jint JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeImChoose(JNIEnv *env, jclass cls, jint value) {
    return 0;
}

JNIEXPORT jboolean JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeImCloseDecoder(JNIEnv *env, jclass cls) {
    return JNI_TRUE;
}

JNIEXPORT jint JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeImDelSearch(JNIEnv *env, jclass cls, jint value, jboolean flag, jboolean flag2) {
    return 0;
}

JNIEXPORT void JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeImFlushCache(JNIEnv *env, jclass cls) {
}

JNIEXPORT jstring JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeImGetChoice(JNIEnv *env, jclass cls, jint value) {
    return NULL;
}

JNIEXPORT jint JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeImGetFixedLen(JNIEnv *env, jclass cls) {
    return 0;
}

JNIEXPORT jstring JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeImGetPredictItem(JNIEnv *env, jclass cls, jint value) {
    return NULL;
}

JNIEXPORT jint JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeImGetPredictsNum(JNIEnv *env, jclass cls, jstring text) {
    return 0;
}

JNIEXPORT jstring JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeImGetPyStr(JNIEnv *env, jclass cls, jboolean flag) {
    return NULL;
}

JNIEXPORT jint JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeImGetPyStrLen(JNIEnv *env, jclass cls, jboolean flag) {
    return 0;
}

JNIEXPORT jintArray JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeImGetSplStart(JNIEnv *env, jclass cls) {
    return NULL;
}

JNIEXPORT jboolean JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeImOpenDecoder(JNIEnv *env, jclass cls, jbyteArray array, jbyteArray array2) {
    return JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeImOpenDecoderFd(JNIEnv *env, jclass cls, jobject fd, jlong value, jlong value2, jbyteArray array) {
    return JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeImResetSearch(JNIEnv *env, jclass cls) {
}

JNIEXPORT jint JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeImSearch(JNIEnv *env, jclass cls, jbyteArray array, jint value) {
    return 0;
}

JNIEXPORT void JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeImSetMaxLens(JNIEnv *env, jclass cls, jint value, jint value2) {
}

/* Sync (user-dictionary transfer) state — always empty. */

JNIEXPORT jboolean JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeSyncBegin(JNIEnv *env, jclass cls, jbyteArray array) {
    return JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeSyncClearLastGot(JNIEnv *env, jclass cls) {
}

JNIEXPORT void JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeSyncFinish(JNIEnv *env, jclass cls) {
}

JNIEXPORT jint JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeSyncGetCapacity(JNIEnv *env, jclass cls) {
    return 0;
}

JNIEXPORT jint JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeSyncGetLastCount(JNIEnv *env, jclass cls) {
    return 0;
}

JNIEXPORT jstring JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeSyncGetLemmas(JNIEnv *env, jclass cls) {
    return NULL;
}

JNIEXPORT jint JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeSyncGetTotalCount(JNIEnv *env, jclass cls) {
    return 0;
}

JNIEXPORT jint JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeSyncPutLemmas(JNIEnv *env, jclass cls, jstring text) {
    return 0;
}

JNIEXPORT jstring JNICALL
Java_com_qinggan_app_qgime_PinyinDecoderService_nativeSyncUserDict(JNIEnv *env, jclass cls, jbyteArray array, jstring text) {
    return NULL;
}
