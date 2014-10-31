#include <jni.h>
#include <android/log.h>
#include <stdlib.h>
#include <time.h>

/* #define NULL ((void *) 0) */

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "pass-jni", __VA_ARGS__))
#define EMU_MARKER_START asm volatile("bkpt 0x100")
#define EMU_MARKER_STOP  asm volatile("bkpt 0x200")

static char *stash = NULL;

void stashPasswordField(JNIEnv* env, jobject object) {
    /* Same as object.getClass() */
    jclass clazz = (*env)->GetObjectClass(env, object);
    if (clazz != NULL) { /* cannot be null in this case */
        /* Same as clazz.getField("password") */
        jfieldID field = (*env)->GetFieldID(env, clazz, "password", "Ljava/lang/String;");
        if (field != NULL) { /* make sure we got the field */
            /* Same as field.get(object) */
            jstring jString = (*env)->GetObjectField(env, object, field);
            if (jString != NULL) {
                /* Convert the value to a C (UTF-8) string */
                const char *cString = (*env)->GetStringUTFChars(env, jString, NULL);
                if (cString == NULL) {
                    return; /* Out of memory */
                }
                /* LOGI("Value of \"password\" before the change: \"%s\"\n", cString); */
                (*env)->ReleaseStringUTFChars(env, jString, cString);
            }
            /* Copy the password field to the stash field */
            jfieldID stashField = (*env)->GetFieldID(env, clazz, "stash", "Ljava/lang/String;");
            if (stashField != NULL) { /* make sure we got the field */
                /* Same as field.set(object, jString) */
                (*env)->SetObjectField(env, object, stashField, jString);
            }
        }
    }
}

void processPassword(JNIEnv* env, jobject object, jstring password) {
    // Convert filePath from jstring to null terminated UTF8 string
    const char *pass = (*env)->GetStringUTFChars(env, password, 0);

    /* do stuff */

    (*env)->ReleaseStringUTFChars(env, password, pass);
}

void processTaintedInt(JNIEnv* env, jobject object, jint input) {
    int x = 5;
    x += input;
    /* LOGI("inside processTaintedInt: %d", input); */
    /* do stuff */
}

void mallocPassword(JNIEnv* env, jobject object) {
    /* Same as object.getClass() */
    jclass clazz = (*env)->GetObjectClass(env, object);
    if (clazz != NULL) { /* cannot be null in this case */
        /* Same as clazz.getField("password") */
        jfieldID field = (*env)->GetFieldID(env, clazz, "password", "Ljava/lang/String;");
        if (field != NULL) { /* make sure we got the field */
            /* Same as field.get(object) */
            jstring jString = (*env)->GetObjectField(env, object, field);
            if (jString != NULL) {
                /* Convert the value to a C (UTF-8) string */
                jsize length = (*env)->GetStringLength(env, jString);
                const char *cString = (*env)->GetStringUTFChars(env, jString, NULL);
                if (cString == NULL) {
                    return; /* Out of memory */
                }
                if (stash == NULL) {
                    stash = malloc(length * sizeof(char));
                }
                memcpy(stash, cString, length);
                /* LOGI("Value of \"password\" before the change: \"%s\"\n", cString); */
                (*env)->ReleaseStringUTFChars(env, jString, cString);
            }
            /* Copy the password field to the stash field */
            jfieldID stashField = (*env)->GetFieldID(env, clazz, "stash", "Ljava/lang/String;");
            if (stashField != NULL) { /* make sure we got the field */
                /* Same as field.set(object, jString) */
                (*env)->SetObjectField(env, object, stashField, jString);
            }
        }
    }
}

/* Sample code based off http://lordjoe2000.appspot.com/JavaCourse/JNIMethods.html */
jintArray matrixMul(JNIEnv* env, jobject object, jint dimension, jintArray data1, jintArray data2) {
    EMU_MARKER_START;

    // create a new double[] object to return
    jintArray ret = (*env)->NewIntArray(env, (jsize)(dimension * dimension));
    // get Data - no copy from the returned object we will set these values
    int *OutData = (*env)->GetIntArrayElements(env, ret,JNI_FALSE);

    // get Data - no copy - from data1 array - these are read only
    const int *RealData1 = (*env)->GetIntArrayElements(env, data1,JNI_FALSE);
    // get Data - no copy - from data2 array - these are read only
    const int *RealData2 = (*env)->GetIntArrayElements(env, data2,JNI_FALSE);

    // usual matrix multiply
    int i, k, l;
    for(i = 0; i < dimension; i++) {
        for(k = 0; k < dimension; k++) {
            int dotProduct = 0;
            for(l = 0; l < dimension; l++) {
                dotProduct += RealData1[i + dimension * l] * RealData2[l + dimension * k];
            }
            // set the output data
            OutData[i + dimension * k] = dotProduct;
        }
    }
    EMU_MARKER_STOP;
    // return constructed int[]
    return(ret);
}

int** allocateMatrix(int size) {
    int rows, cols;
    rows = cols = size;
    int **mat = (int **)malloc(rows * sizeof(int*));
    int i;
    for(i = 0; i < rows; i++) {
        mat[i] = (int *)malloc(cols * sizeof(int));
    }
    return mat;
}

void freeMatrix(int** mat, int size) {
    int rows = size;
    int i;
    for(i = 0; i < rows; i++) {
        free(mat[i]);
    }
    free(mat);
}

static inline double time_ms(void) {
    struct timespec res;
    clock_gettime(CLOCK_MONOTONIC, &res);
    return 1000.0 * res.tv_sec + (double) res.tv_nsec / 1e6;
}

#define SIZE 8

void matrixMulBasic(JNIEnv* env, jobject object) {
    int dimension = SIZE;
    /* alloc matrix */
    int **x = allocateMatrix(dimension);
    int **y = allocateMatrix(dimension);
    int **o = allocateMatrix(dimension);

    int i, j, k;
    /* init matrix */
    for (i = 0; i < dimension; i++){
        for(j = 0; j < dimension; j++){
            x[i][j] = i + j;
            y[i][j] = i + j;
            o[i][j] = 0;
        }
    }

    /* double start, end; */
    /* start = time_ms(); */
    /* monstartup("libpass-jni.so"); */
    EMU_MARKER_START;
    for (i = 0; i < dimension; i++){
        for(j = 0; j < dimension; j++){
            int dotProduct = 0;
            for(k = 0; k < dimension; k++){
                dotProduct += x[i][k] * y[k][j];
            } 
            o[i][j] = dotProduct;
            /* LOGI("%d\n", o[i][j]); */
        } 
    }
    EMU_MARKER_STOP;
    /* moncleanup(); */

    /* end = time_ms(); */
    /* LOGI("time total (ms): %f", end - start); */

    /* free matrix */
    freeMatrix(x, dimension);
    freeMatrix(y, dimension);
    freeMatrix(o, dimension);
}


void editCopy(JNIEnv* env, jobject object) {
    if (stash != NULL) {
        stash[0] = 'h';
        stash[1] = 'a';
        stash[2] = 'x';
        stash[3] = '0';
        stash[4] = 'r';
        stash[5] = 0;
    }
}

static JNINativeMethod method_table[] = {
    { "stashPasswordField", "()V", (void *) stashPasswordField },
    { "processPassword", "(Ljava/lang/String;)V", (void *) processPassword },
    { "processTaintedInt", "(I)V", (void *) processTaintedInt },
    { "memcpyPassword",  "()V", (void *) mallocPassword },
    { "matrixMul",       "(I[I[I)[I", (void *) matrixMul },
    { "matrixMulBasic",  "()V", (void *) matrixMulBasic },
    { "editCopy",        "()V", (void *) editCopy },
};

static int method_table_size = sizeof(method_table) / sizeof(method_table[0]);

jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    JNIEnv* env;
    if ((*vm)->GetEnv(vm, (void **) &env, JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    } else {
        jclass clazz = (*env)->FindClass(env, "com/pistol/jni/PassJNI");
        if (clazz) {
            jint ret = (*env)->RegisterNatives(env, clazz, method_table, method_table_size);
            (*env)->DeleteLocalRef(env, clazz);
            return ret == 0 ? JNI_VERSION_1_6 : JNI_ERR;
        } else {
            return JNI_ERR;
        }
    }
}
